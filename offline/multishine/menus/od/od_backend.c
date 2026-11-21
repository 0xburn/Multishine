/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* 'Multishine online-direct' image: the EXI answers of the online title menu with a real Direct mode.
 *
 * Same wrapper idea as the online-menu image's (s12_gc.c is compiled with -Dsdshim_set_backend=od_set_backend; the engine's own
 * slots B0 pad exchange, B5 selections, B1/B2 snapshots stay), but B4 / B3 / BA are no longer refused:
 *   B9  GET_ONLINE_STATUS   appState 1, name and connect code of the identity in the config block (od_cfg), else the fixed lab identity MULTI / LAB#1
 *   B4  FIND_OPPONENT       Direct (mode 2): the typed code goes to the history (card) and to od_glue, which asks the server; Unranked (mode 1) only
 *                           when the config enables it; Ranked: its own "not available" text ( is a design, not a build); everything else
 *                           "Online play is not available yet"
 *   B3  GET_MATCH_STATE     od_glue answers states 0..3 and the refusal texts (state 5); once the engine's session runs (od phase UP) the ENGINE's
 *                           answer (state 4, selections, names) is used, with the names and codes of both players filled in from the ticket
 *   BA  CLEANUP             cancel the search / clear the error (the engine session itself is not torn down yet: one session per boot, ONLINE_GLUE 9)
 *   B5, B0, B1/B2          the engine's, unchanged
 *   BE  FETCH_SUGGESTION    the local code history of om_card.c / codehist.c (unchanged from the online-menu image)
 * The network side (ticket, peer hand-over) runs in the engine's network slice (s12_gc.c, OD block); this file runs in the game thread with
 * interrupts off and never touches the network.  Soft-float, no heap, no libc, no FPU; C99.
 */
#include "../../exi_shim/sdshim.h"
#include "om_card.h"
#include "od_glue.h"

#ifndef OD_DATA                      /* the flat image has no .bss: state lives.data (the host test defines it empty) */
#define OD_DATA __attribute__((section(".data")))
#endif

#define OD_FALLBACK_NAME "MULTI"     /* fixed test identity when the config block carries none (as the online-menu image) */
#define OD_FALLBACK_CODE "LAB#1"

/* the configuration block: patched into the image by od_patch.py (found by its magic), later built from the card record */
struct od_cfg od_cfg OD_DATA = { OD_CFG_MAGIC0, OD_CFG_MAGIC1, 0u, 0u, 0u, 0u, 0u, 0u, 0x00010000u, 0xC0DE5E7u, 0x201E5u, 0u,
                                 { { 0 }, { 0 } }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
struct od od_o OD_DATA;
struct od_status od_st OD_DATA;

extern int od_send(void *ctx, const unsigned char *p, unsigned int len);   /* s12_gc.c: one datagram to the server from the game socket */
extern int od_core_state(void);
extern unsigned int od_clock_ms(void);

static struct sdshim_backend od_be OD_DATA;
static const struct sdshim_backend *eng OD_DATA;      /* the engine's own table (B0, B3, B5, B1/B2) */

static void put_text(sdshim_u8 *dst, const char *s, unsigned int cap)
{
    unsigned int i;
    for (i = 0; s[i] && i + 1 < cap; i++) dst[i] = (sdshim_u8)s[i];
}

/* A DISPLAY NAME into Melee's name field: Slippi's ConvertStringForGame / ConvertNarrowSpecialSHIFTJIS (Ishiiruka StringUtil.cpp) turns every ASCII punctuation mark into its
   full-width Shift-JIS code, because Melee's SIS encoder (HSD_SisLib_803A67EC) takes any byte it does not know as the lead byte of a two byte code and SWALLOWS the next character: the seat
   label "Sandbag (bot)" drew as "Sandbag ot" (the "(b" pair swallowed, the ")" ate the end) on the character screen and the HUD, "Sandbag ot-" on the VS screen.  Letters, digits and the
   space stay single bytes.  At most cap-1 bytes, a two byte code never cut in half.  Error texts keep put_text (they are drawn by other paths). */
static const char nm_chr[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
static const unsigned char nm_lo[] = { 0x49, 0x68, 0x94, 0x90, 0x93, 0x95, 0x66, 0x69, 0x6A, 0x96, 0x7B, 0x43, 0x7C, 0x44, 0x5E, 0x46, 0x47, 0x83, 0x81, 0x84, 0x48, 0x97, 0x6D, 0x5F, 0x6E, 0x4F, 0x51, 0x4D, 0x6F, 0x62, 0x70, 0x60 };
static void put_name(sdshim_u8 *dst, const char *s, unsigned int cap)
{
    unsigned int i, n = 0, k;
    for (i = 0; s[i]; i++) {
        for (k = 0; k < sizeof nm_lo && nm_chr[k] != s[i]; k++) ;
        if (n + (k < sizeof nm_lo ? 3u : 2u) > cap) break;
        if (k < sizeof nm_lo) { dst[n++] = 0x81; dst[n++] = nm_lo[k]; }
        else dst[n++] = (sdshim_u8)s[i];
    }
}

/* ASCII code -> Melee's code field: '#' is the full-width SJIS 0x8194, '@' (Multishine universe) 0x8197 */
static void put_code(sdshim_u8 *dst, const char *c, unsigned int cap)
{
    unsigned int i, n = 0;
    for (i = 0; c[i] && n + 2 <= cap; i++) {
        if (c[i] == '#') { dst[n++] = 0x81; dst[n++] = 0x94; }
        else if (c[i] == '@') { dst[n++] = 0x81; dst[n++] = 0x97; }
        else dst[n++] = (sdshim_u8)c[i];
    }
}

static int od_status_b9(void *ctx, sdshim_u8 *osb)
{
    const char *name = OD_FALLBACK_NAME, *code = OD_FALLBACK_CODE;
    unsigned int i;
    (void)ctx;
    od_st.b9++;
    omc_on_status();                              /* first B9: read the code history from the card (and retry a pending save) */
    if (od_o.cfg_ok) { name = od_cfg.name; code = od_cfg.code; }
    for (i = 0; i < 42; i++) osb[i] = 0;
    osb[0] = 1;                                   /* appState: logged in */
    put_name(osb + 1, name, 31);                  /* name[31] */
    put_code(osb + 32, code, 10);                 /* code[10] */
    return 0;
}

static int od_find(void *ctx, sdshim_u8 mode, const sdshim_u8 *code)
{
    char c[CH_LEN + 1];
    (void)ctx;
    od_st.b4++;
    od_st.last_mode = mode;
    c[0] = 0;
    if (mode == CH_MODE_DIRECT) {                 /* Slippi logs the confirmed direct code (startFindMatch) before it connects or refuses */
        if (ch_from_melee(code, 18, c)) omc_on_code(c);
    }
    od_st.latch = 0;
    if (mode == 0) { od_st.latch = 1; return 0; } /* Ranked: refused with its own text (design only) */
    (void)od_request(&od_o, od_clock_ms(), (int)mode, c);   /* an error is shown by the next B3; the search starts in the network slice */
    return 0;                                     /* >= 0: handled (the shim's own "no backend" latch stays off) */
}

/* names and codes of both players from the ticket into the engine's answer (MSRB: local name 15, P1 name 46, P2 name 77, opp name 170,
   P1 code 201, P2 code 211; the engine writes SIDE A / SIDE B / SDA#1 / SDB#2 there) */
static void names_from_ticket(sdshim_u8 *msrb)
{
    const char *mine = od_o.m.name, *mcode = od_o.m.code;
    const char *n0, *n1, *c0, *c1;
    unsigned int i;
    if (od_o.side == 0) { n0 = mine; c0 = mcode; n1 = od_o.opp_name; c1 = od_o.opp_code; }
    else { n1 = mine; c1 = mcode; n0 = od_o.opp_name; c0 = od_o.opp_code; }
    for (i = 0; i < 31; i++) { msrb[15 + i] = 0; msrb[46 + i] = 0; msrb[77 + i] = 0; msrb[170 + i] = 0; }
    for (i = 0; i < 20; i++) { msrb[201 + i] = 0; }
    put_name(msrb + 15, mine, 31);
    put_name(msrb + 46, n0, 31);
    put_name(msrb + 77, n1, 31);
    put_name(msrb + 170, od_o.opp_name, 31);
    put_code(msrb + 201, c0, 10);
    put_code(msrb + 211, c1, 10);
}

static int od_state(void *ctx, sdshim_u8 *msrb)
{
    int rc;
    od_st.b3++;
    if (od_st.latch == 1) {                       /* Ranked refusal */
        const char *t = "Ranked is not available";
        msrb[0] = SDSHIM_MM_ERROR;
        put_text(msrb + SDSHIM_MSRB_ERROR_OFFSET, t, 241);
        return 0;
    }
    if (od_b3(&od_o, msrb)) return 0;             /* idle, searching, connecting, error: od_glue's answer */
    od_st.b3_eng++;
    rc = eng && eng->match_state ? eng->match_state(eng->ctx, msrb) : -1;   /* the engine's session runs: state 4 and the selections */
    if (rc == 0 && msrb[0] == 4) names_from_ticket(msrb);
    (void)ctx;
    return rc;
}

/* BE: req[32] = cmd, input[24] (8 x 3 bytes), input_len, scroll index (u32 BE), scroll dir, mode; rsp[30] = has_suggestion, suggestion[24], len, new index (u32 BE) */
static int od_suggest(void *ctx, const sdshim_u8 *req, sdshim_u8 *rsp)
{
    (void)ctx;
    od_st.be++;
    (void)omc_answer(req, rsp);
    return 0;
}

static void od_cleanup(void *ctx)
{
    (void)ctx;
    od_st.ba++;
    od_st.latch = 0;
    od_cancel(&od_o, od_clock_ms());
}

static void od_ui(void *ctx, sdshim_u8 cmd)
{
    (void)ctx;
    (void)cmd;                                    /* Log in / out / Update: locked rows, nothing to do */
}

void od_set_backend(struct sdshim *s, const struct sdshim_backend *b)
{
    unsigned int seed;
    od_be = *b;
    eng = b;
    od_be.match_state = od_state;
    od_be.find_opponent = od_find;
    od_be.online_status = od_status_b9;
    od_be.cleanup = od_cleanup;
    od_be.ui_action = od_ui;
    od_be.fetch_suggestion = od_suggest;
#if defined(__powerpc__) || defined(__PPC__)
    __asm__ volatile("mftbl %0" : "=r"(seed));      /* the time base: only mixed into the nonce pool, never a secret */
#else
    seed = 1u;                                       /* host tests */
#endif
    (void)od_init(&od_o, &od_cfg, &od_st, od_send, 0, seed, od_clock_ms());
    sdshim_set_backend(s, &od_be);
}
