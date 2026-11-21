/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* 'Multishine online' image: the EXI answers of the online title menu with real Direct, Unranked and Ranked modes through our server, the server setup
 * read from the memory card, and the Ranked result report (10, 11).  Fork of../od/od_backend.c (the online-direct image keeps its own).
 *
 * Same wrapper idea as the online-menu image's (s12_gc.c is compiled with -Dsdshim_set_backend=od_set_backend; the engine's own
 * slots B0 pad exchange, B5 selections, B1/B2 snapshots stay):
 *   B9  GET_ONLINE_STATUS   appState 1, name and connect code of the identity in the config block (od_cfg); the block is filled from the memory card file
 *                           MultishineOnline at the first B9 (on_card.c) unless the image carries a patched block (host and rig tests); without any setup the fixed
 *                           lab identity MULTI / LAB#1 is shown and every online row refuses with a message that names the reason (no card, no file, damaged, ...)
 *   B4  FIND_OPPONENT       Direct (mode 2): the typed code goes to the history (card) and to od_glue, which asks the server; Unranked (1) and Ranked (0) when the setup
 *                           enables the row (a setup without the ranked flag keeps Ranked's own "not available" text); Teams and Party: "Online play is not available yet"
 *   B3  GET_MATCH_STATE     od_glue answers states 0..3 and the refusal texts (state 5); once the engine's session runs (od phase UP) the ENGINE's
 *                           answer (state 4, selections, names) is used, with the names and codes of both players filled in from the ticket; after a reported
 *                           ranked game it answers idle so that the scene decision returns to the character screen (a ranked set is one game in this build)
 *   BA  CLEANUP             cancel the search / clear the error (the engine session itself is not torn down yet: one session per boot, ONLINE_GLUE 9)
 *   BD  REPORT_GAME         Ranked: the result of the game (Slippi's own winner decision) becomes a GAME_REPORT with the evidence (final frame and Slippi checksum
 *                           from the engine); C2 / C4 are counted only (ONLINE_GLUE 11)
 *   B5, B0, B1/B2          the engine's, unchanged
 *   BE  FETCH_SUGGESTION    the local code history of om_card.c / codehist.c (unchanged from the online-menu image)
 * The network side (ticket, peer hand-over) runs in the engine's network slice (s12_gc.c, OD block); this file runs in the game thread with
 * interrupts off and never touches the network.  Soft-float, no heap, no libc, no FPU; C99.
 */
#include "../../exi_shim/sdshim.h"
#include "om_card.h"
#include "od_glue.h"
#include "on_card.h"

/* Hub overlay split (menus/hub/hub_ovl.h): neither macro = everything (flat image, host tests: the code before the split);
   HUB_OVL_RES = the resident remainder (state, B3, BA head, B5/B0 through the engine, the thunks and the loader of hub_res.c); HUB_OVL = the overlay part (B9, B4, BE, BD). */
#if defined(HUB_OVL) && defined(HUB_OVL_RES)
#error "HUB_OVL and HUB_OVL_RES exclude each other"
#endif
#if defined(HUB_OVL) && defined(ON_DIAG)
#error "the diagnostic variant has no hub overlay"
#endif
#if defined(HUB_OVL)
#define ON_RES 0
#define ON_OVL 1
#elif defined(HUB_OVL_RES)
#define ON_RES 1
#define ON_OVL 0
#else
#define ON_RES 1
#define ON_OVL 1
#endif
#if defined(HUB_OVL) || defined(HUB_OVL_RES)
#include "hub_ovl.h"
#endif

#ifndef OD_DATA                      /* the flat image has no .bss: state lives.data (the host test defines it empty) */
#define OD_DATA __attribute__((section(".data")))
#endif

#define OD_FALLBACK_NAME "MULTI"     /* fixed test identity when the config block carries none (as the online-menu image) */
#define OD_FALLBACK_CODE "LAB#1"

#if ON_RES
/* the configuration block: patched into the image by od_patch.py (found by its magic), later built from the card record */
struct od_cfg od_cfg OD_DATA = { OD_CFG_MAGIC0, OD_CFG_MAGIC1, 0u, 0u, 0u, 0u, 0u, 0u, 0x00010000u, 0xC0DE5E7u, 0x201E5u, 0u,
                                 { { 0 }, { 0 } }, { 0 }, { 0 }, { 0 }, { 0 }, { 0 } };
struct od od_o OD_DATA;
volatile unsigned int od_end_req OD_DATA;     /* BA arrived: s12_gc.c ends the engine session (peer_watch); set by od_cleanup, and by the bridge's dispatcher for its own BA */
struct od_status od_st OD_DATA;
#endif

struct on_status {                           /* RAM proof block (symbol on_st, on_status_addr) */
    volatile unsigned int magic;             /* 0 'ONST' */
    volatile unsigned int bd, bd_sent;       /* 1 2 REPORT_GAME commands seen / turned into a queued report */
    volatile unsigned int c2, c4;            /* 3 4 REPORT_SET_COMPLETION / REPORT_MATCH_STATUS commands seen (counted only) */
    volatile unsigned int rk_idle;           /* 5 1 while B3 answers idle after a reported ranked game (the set is over, back to the CSS) */
    volatile unsigned int winner_idx;        /* 6 winnerIdx byte of the last BD (s8) */
    volatile unsigned int endm, lras;        /* 7 8 game end method, LRAS initiator of the last BD */
    volatile unsigned int stocks;            /* 9 stocks of the players 1..4, one byte each */
    volatile unsigned int frames, game;      /* 10 11 */
    volatile unsigned int skip_why;          /* 12 OD_RS_* or 100+ (tie, desync, disconnect) of the last BD that was not reported */
    volatile unsigned int final_ok;          /* 13 od_core_final found the checksum of the final frame */
    volatile unsigned int cfg_loads;         /* 14 card config loads that filled the block */
    volatile unsigned int diag_shown;        /* 15 diagnostic texts shown (diagnostic variant) */
    volatile unsigned int mode_patched;      /* 16 times the Ranked row's online-mode byte was set to Unranked for the match (see on_ranked_as_unranked) */
    volatile unsigned int mode_restored;     /* 17 times it was set back to Ranked (BD report, BA cleanup) */
    volatile unsigned int mode_state;        /* 18 1 while the byte is patched */
};
#if ON_RES
struct on_status on_st OD_DATA = { 0x4F4E5354u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#else
/* the overlay reaches the resident state through the env table (no import relocations) */
extern const struct hub_env *hub_E;
#define od_cfg (*hub_E->cfg)
#define od_o (*hub_E->o)
#define od_st (*hub_E->st)
#define on_st (*hub_E->on_st)
#define onc_st (*hub_E->onc_st)
#define on_ranked_as_unranked(on) hub_E->ranked_as_unranked(on)
#endif

/* Slippi's online mode byte (r13 - 0x5060 = 0x804D6640, OFST_R13_ONLINE_MODE; ram var online_mode).  WHY it is touched: with the byte at 0 (Ranked) Slippi's CSS, once both
   players are locked in, requests its Game Preparation scene (CSSSceneDecide_Adv_IsRanked: best of three with stage picks and bans, driven by match-state fields and EXI
   commands this shim does not answer): the match never starts (Dolphin run rk1, ONLINE_GLUE 11).  So for the match the byte is set to
   1 (Unranked): random stage, no game prep, one game; the ticket and the result report stay Ranked.  It is set back to 0 when the result is reported (so Slippi's
   ranked scene decision after the game takes the "connection idle: set over" path back to the CSS) and at BA.  Host tests redirect the byte. */
#if ON_RES
#ifdef ON_HOST_MODE_BYTE
extern volatile unsigned char on_mode_byte_host;
#define ON_MODE_PTR (&on_mode_byte_host)
#else
#define ON_MODE_PTR ((volatile unsigned char *)0x804D6640u)
#endif
static void on_ranked_as_unranked(int on)
{
    if (on && !on_st.mode_state) { *ON_MODE_PTR = 1u; on_st.mode_state = 1; on_st.mode_patched++; }
    else if (!on && on_st.mode_state) { *ON_MODE_PTR = 0u; on_st.mode_state = 0; on_st.mode_restored++; }
}
#endif

extern int od_send(void *ctx, const unsigned char *p, unsigned int len);   /* s12_gc.c: one datagram to the server from the game socket */
extern int od_core_state(void);
extern unsigned int od_clock_ms(void);
extern int od_core_final(unsigned int *frame, unsigned int *cks);   /* s12_gc.c (ON_BUILD): newest finalized frame and its Slippi checksum */
extern void od_core_stats(unsigned int *w);                         /* s12_gc.c (ON_BUILD): the newest match's rollback / RTT facts (12 words, [0] = valid, [9] build in the hub wrapper) */

#if ON_RES
static struct sdshim_backend od_be OD_DATA;
static const struct sdshim_backend *eng OD_DATA;      /* the engine's own table (B0, B3, B5, B1/B2) */
#endif

static void __attribute__((unused)) put_text(sdshim_u8 *dst, const char *s, unsigned int cap)   /* unused in the hub overlay build since put_name (5f976432): -Werror */
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

#if ON_OVL
#ifndef ON_DIAG                                                  /* the diagnostic variant has no search request (on_b4 below), so it carries no caller: no unused static (-Werror, profile s17-compat-ond) */
static void on_link_ticket(int mode);
#endif
#ifdef ON_DIAG
static char diag_name[31] OD_DATA;
#endif

/* a fresh identity from a silent re-enrolment (od_glue.c od_adopt) goes into the card record when no search is running and the card is free; at most 3 failed writes */
static void ident_flush(void)
{
    int r;
    if (!od_o.ident_dirty || od_o.phase == OD_WAIT_NET || od_o.phase == OD_FIND || od_o.phase == OD_PEER || od_o.phase == OD_CONNECT || od_o.phase == OD_UP) return;
    r = onc_save_ident(&od_cfg);
    if (r == 1) return;
    if (r == -2 && ++onc_st.ident_tries < 3u) return;
    od_o.ident_dirty = 0;
}

/* THE SETUP READ, whenever the overlay runs (hub_soc.c net_step each menu frame, the title check): one card attempt a second at most (Melee's card layer may be busy at the title or while the Main
   Menu scans the card, tens of seconds on a Wii in single-context mode).  A spent busy / error budget is only soft (on_card.c onc_final lifts it after 2, 4, 8, 16, 30 s, Reconnect at once), so a START
   pressed during the card load no longer leaves the console without a setup for good.  1 = loaded and applied, 0 = not yet (asked again), -1 = definitely no usable setup. */
static unsigned int cfg_next OD_DATA;
#define CFG_SOFT() (od_o.cfg_why == OD_WHY_BUSY || od_o.cfg_why == OD_WHY_ERROR)    /* a final verdict with these reasons is only a spent busy / error budget (the overlay has no onc_soft: struct hub_env stays as it is) */
int od_cfg_poll(unsigned now)
{
    int why;
    if (od_o.cfg_ok) return 1;
    if (onc_final()) return CFG_SOFT() ? 0 : -1;               /* no file, damaged, newer: final; a spent budget: asked again later */
    if (cfg_next && (int)(now - cfg_next) < 0) return 0;
    cfg_next = now + 1000u;
    if (!cfg_next) cfg_next = 1;
    why = onc_on_status(&od_cfg);                               /* the very step of the first B9 (od_status_b9): the B9 of the menu then finds cfg_ok and skips it */
    od_o.cfg_why = why;
    if (why != OD_WHY_NONE) return onc_final() && !CFG_SOFT() ? -1 : 0;
    if (od_apply_config(&od_o) == 0) { on_st.cfg_loads++; od_set_backup(&od_o, onc_backup()); return 1; }
    od_o.cfg_why = OD_WHY_INCOMPLETE;
    return -1;
}
static int od_status_b9(void *ctx, sdshim_u8 *osb)
{
    const char *name = OD_FALLBACK_NAME, *code = OD_FALLBACK_CODE;
    unsigned int i;
    (void)ctx;
    od_st.b9++;
    if (!od_o.cfg_ok && !onc_final()) {           /* first B9 (and later ones until the verdict is final): the online setup from the card */
        int why = onc_on_status(&od_cfg);
        od_o.cfg_why = why;
        if (why == OD_WHY_NONE && od_apply_config(&od_o) == 0) { on_st.cfg_loads++; od_set_backup(&od_o, onc_backup()); }
        else if (why == OD_WHY_NONE) od_o.cfg_why = OD_WHY_INCOMPLETE;
    }
    omc_on_status();                              /* first B9: read the code history from the card (and retry a pending save) */
    ident_flush();
    if (od_o.cfg_ok) { name = od_cfg.name; code = od_cfg.code; }
#ifdef ON_DIAG
    onc_diag_short(diag_name, sizeof diag_name);  /* the CSS shows the card status as the user name: it is there again every time the player reopens a row */
    name = diag_name;
#endif
    for (i = 0; i < 42; i++) osb[i] = 0;
    osb[0] = 1;                                   /* appState: logged in */
    put_name(osb + 1, name, 31);                  /* name[31] */
    put_code(osb + 32, code, 10);                 /* code[10] */
    return 0;
}

#ifdef ON_DIAG
static char diag_txt[SDSHIM_MSRB_ERROR_LEN] OD_DATA;       /* the diagnostic variant shows the card status in the error box instead of searching */
#endif

#ifdef MNC_RKSET
static int rk_hand(void)                                                                            /* the hand-off block belongs to a Grandmaster set (no room name, MNC_RK_FLAG) */
{
    return od_o.m.gn_magic == MNC_GN_MAGIC && !od_o.m.gn_room[0] && (od_o.m.gn_room[MNC_RK_FLAGS_AT] & MNC_RK_FLAG) != 0u;
}
#endif
static int od_find(void *ctx, sdshim_u8 mode, const sdshim_u8 *code)
{
    char c[CH_LEN + 1];
    (void)ctx;
    od_st.b4++;
    od_st.last_mode = mode;
    c[0] = 0;
    if (mode == CH_MODE_DIRECT) {                 /* Slippi logs the confirmed direct code (startFindMatch) before it connects or refuses */
        if (ch_from_melee(code, 18, c) && !(od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_ADDFR)) omc_on_code(c);   /* (a friend's code is no Mano a mano history entry) */
    }
    od_st.latch = 0;
    on_st.rk_idle = 0;
#ifdef ON_DIAG
    omc_on_status();                              /* a menu load that never reached B9 would show nothing: look at the card now */
    onc_diag_text(diag_txt, sizeof diag_txt);
    on_st.diag_shown++;
    od_st.latch = 2;
    return 0;
#else
#ifdef MNC_RKSET
    if (rk_hand() && (od_o.m.gn_state == MNC_GN_ARMED || od_o.m.gn_state == MNC_GN_SEARCH)) {          /* a game of a held Grandmaster set: the sign overlay armed it with the game's stage; the held pairing's peer, no ticket.
                                                                                                     SEARCH = Z cleared a failed attempt of the same game: start it again.  State SEARCH is what the resident return-to-menu tick (gns_res.c gns_skipcss_tick) and the report callback key on */
        od_o.m.gn_state = MNC_GN_SEARCH;
        od_o.m.gn_sets++;
        (void)od_rk_go(&od_o, od_clock_ms());
        od_st.gn = (unsigned int)MNC_GN_SEARCH | ((unsigned int)od_o.m.gn_sets << 8) | (((od_st.gn >> 16) + 1u) << 16);
        return 0;
    }
#endif
    if (od_o.m.gn_magic == MNC_GN_MAGIC && (od_o.m.gn_state == MNC_GN_ARMED || od_o.m.gn_state == MNC_GN_SEARCH) && od_o.m.gn_room[0]) {   /* Game Night hand-off: the lobby armed;
                                                                                                     SEARCH = Z cleared a failed attempt of the same set: search the set again */
        od_o.m.gn_state = MNC_GN_SEARCH;                                                             /* the call-up name; this lock-in searches it (mode room) */
        od_o.m.gn_sets++;
        (void)od_request(&od_o, od_clock_ms(), MNC_MODE_ROOM, od_o.m.gn_room);
        od_st.gn = (unsigned int)MNC_GN_SEARCH | ((unsigned int)od_o.m.gn_sets << 8) | (((od_st.gn >> 16) + 1u) << 16);
        return 0;
    }
#ifdef HUB_OVL                                                                                      /* only the hub overlay has the toasts that arm it: a flat engine stays byte for byte */
    if (od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_ADDFR && mode == CH_MODE_DIRECT && c[0]) {   /* Friends Z: the typed code is a friend request */
        unsigned int i;
        for (i = 0; i + 1u < sizeof od_o.m.gn_room && c[i]; i++) od_o.m.gn_room[i] = c[i];
        od_o.m.gn_room[i] = 0;
        od_o.m.gn_state = MNC_GN_ADDFR_DONE;
        od_st.latch = 3;                                                                            /* no search: the error box says what happens next (od_state) */
        return 0;
    }
    if (od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_DIRECT && od_o.m.gn_room[0]) {  /* a Mano a mano answer (toast accept, Friends screen */
        unsigned int i;                                                                             /* challenge accepted): this lock-in searches that player in Direct mode */
        for (i = 0; i + 1u < sizeof c && od_o.m.gn_room[i]; i++) c[i] = od_o.m.gn_room[i];
        c[i] = 0;
        od_o.m.gn_state = MNC_GN_NONE; od_o.m.gn_room[0] = 0;
        (void)od_request(&od_o, od_clock_ms(), MNC_MODE_DIRECT, c);
        return 0;
    }
#endif
    if (mode == 0 && od_o.cfg_ok && !(od_cfg.flags & OD_CFGF_RANKED)) { od_st.latch = 1; return 0; }   /* a setup without the ranked flag: Ranked keeps its own text */
    on_link_ticket((int)mode);                    /*  9: this search's link class and Wired opponents only (Unranked only; every other mode sends nothing) */
    if (od_request(&od_o, od_clock_ms(), (int)mode, c) == 0 && mode == 0) on_ranked_as_unranked(1);   /* an error is shown by the next B3; the search starts in the network slice */
    return 0;                                     /* >= 0: handled (the shim's own "no backend" latch stays off) */
#endif
}
#endif /* ON_OVL */

#if ON_RES
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

#ifdef HUB_OVL_RES
static int gns_watch_state(sdshim_u8 *msrb, int *rc);          /* gns_res.c (included below): a watch session ("X: Last game") answers the character select and the match block by itself */
extern volatile unsigned int gns_whot;
#endif
#if defined(HUB_OVL_RES) && (defined(MS_RB_READOUT) || defined(MS_HUB_DIAG))
/* HANG LOCATOR.  gns_bc[0] counter, [1] and [2] the last 8 rare events (newest in the low byte of [1]), [3] the stage of the latest event,
   [4] F = engine passes since the hub overlay's last load.  gns_bc_txt = "UP ss cc r1 r2 Fffff", rewritten at every rare event and every event of the 8 passes after a load (the SIS text keeps a pointer
   to it and re-reads it when it draws), shown by on_bhold.c on the character screen.  A frozen console shows the last RENDERED frame: F says how many passes ran after the load, the ring which events the
   last passes made. */
volatile unsigned int gns_bc[5] OD_DATA;
volatile unsigned int hub_dbg[4] OD_DATA;                 /* hub loader: [0] last reason (low byte: 2 table unread, 3 card busy, 0x41.. hub_fail error, 8 read refused, 13 loaded) | idle detail << 8, [1] attempts, [2] ticks skipped (gave up), [3] ticks skipped (scene loading) */
#define HD_SET(i, v) (hub_dbg[i] = (unsigned int)(v))
#define HD_INC(i) (hub_dbg[i]++)
char gns_bc_txt[40] OD_DATA;
static void ms_bc_fmt(void)
{
    static const char hx[] = "0123456789ABCDEF";
    char *q = gns_bc_txt;
    unsigned int i, j;
    *q++ = 'U'; *q++ = 'P'; *q++ = ' ';
    *q++ = hx[(gns_bc[3] >> 4) & 15u]; *q++ = hx[gns_bc[3] & 15u]; *q++ = ' ';
    *q++ = hx[(gns_bc[0] >> 4) & 15u]; *q++ = hx[gns_bc[0] & 15u];
    for (j = 1; j <= 2u; j++) { *q++ = ' '; for (i = 0; i < 8u; i++) *q++ = hx[(gns_bc[j] >> (28u - 4u * i)) & 15u]; }
    *q++ = ' '; *q++ = 'F';
    for (i = 0; i < 4u; i++) *q++ = hx[((gns_bc[4] > 0xFFFFu ? 0xFFFFu : gns_bc[4]) >> (12u - 4u * i)) & 15u];
    *q = 0;
}
/* the events are functions, not inline code: a call site is two instructions instead of about a dozen (502: the inline form was about 2 KB of the engine) */
static void __attribute__((noinline)) ms_bc1(unsigned int x) { gns_bc[3] = x; gns_bc[0]++; }
static void __attribute__((noinline)) ms_bc(unsigned int x)
{
    gns_bc[2] = (gns_bc[2] << 8) | (gns_bc[1] >> 24);
    gns_bc[1] = (gns_bc[1] << 8) | x;
    ms_bc1(x);
    ms_bc_fmt();
}
static void __attribute__((noinline)) ms_bcw(unsigned int x) { if (gns_bc[4] < 8u) ms_bc(x); else ms_bc1(x); }       /* frequent events: ring events only in the 8 passes after a hub load */
#define MS_BC1(x) ms_bc1((unsigned int)(x))
#define MS_BC(x) ms_bc((unsigned int)(x))
#define MS_BCW(x) ms_bcw((unsigned int)(x))
static void ms_bc_done(const unsigned char *p) { ms_bc(*p); }
static void ms_bc_done1(const unsigned char *p) { ms_bcw(*p); }
#define MS_BCF(en, ex) unsigned char ms_bcx __attribute__((cleanup(ms_bc_done), unused)) = (MS_BC(en), (unsigned char)(ex))
#define MS_BCF1(en, ex) unsigned char ms_bcx __attribute__((cleanup(ms_bc_done1), unused)) = (MS_BCW(en), (unsigned char)(ex))
#define MS_BC_PASS() (gns_bc[4]++)
#define MS_BC_LOADED() (gns_bc[4] = 0u)
unsigned int ms_bc_get(unsigned int i) { if (i == 9u) ms_bc_fmt(); return i < 5u ? gns_bc[i] : 0u; }
#else
#define MS_BC1(x) ((void)0)
#define MS_BC(x) ((void)0)
#define MS_BCW(x) ((void)0)
#define MS_BCF(en, ex) ((void)0)
#define MS_BCF1(en, ex) ((void)0)
#define MS_BC_PASS() ((void)0)
#define MS_BC_LOADED() ((void)0)
#define HD_SET(i, v) ((void)0)
#define HD_INC(i) ((void)0)
#endif
static int od_state(void *ctx, sdshim_u8 *msrb)
{
    MS_BCF1(0x60, 0x61);
    int rc;
    od_st.b3++;
#ifdef ON_DIAG
    if (od_st.latch == 2) {
        msrb[0] = SDSHIM_MM_ERROR;
        put_text(msrb + SDSHIM_MSRB_ERROR_OFFSET, diag_txt, SDSHIM_MSRB_ERROR_LEN);
        return 0;
    }
#endif
#ifdef HUB_OVL_RES
    { int wrc; if (gns_whot && gns_watch_state(msrb, &wrc)) return wrc; }   /* a watch session answers by itself (gns_res.c) */
#endif
    if (on_st.rk_idle) return 0;                  /* a ranked game was reported: idle, so the scene decision returns to the CSS (set over: best of one) */
    if (od_st.latch == 3) {                       /* Friends Z: the typed code waits for the Friends list */
        msrb[0] = SDSHIM_MM_ERROR;
        put_text(msrb + SDSHIM_MSRB_ERROR_OFFSET, "Friend request ready.", 241);
        return 0;
    }
    if (od_st.latch == 1) {                       /* Ranked refusal */
        const char *t = "Grandmaster is not available";   /* the hub row formerly named Ranked; the Slippi bridge never reaches this path */
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
#endif /* ON_RES */

#if ON_OVL
/* BE: req[32] = cmd, input[24] (8 x 3 bytes), input_len, scroll index (u32 BE), scroll dir, mode; rsp[30] = has_suggestion, suggestion[24], len, new index (u32 BE) */
static int od_suggest(void *ctx, const sdshim_u8 *req, sdshim_u8 *rsp)
{
    (void)ctx;
    od_st.be++;
    (void)omc_answer(req, rsp);
    return 0;
}

/* BD REPORT_GAME: rgb[369] (Online.s RGB_*): +1 online mode, +2 frame length u32, +6 game index u32 (1-based), +10 tiebreak u32, +14 winner s8, +15 end method,
   +16 LRAS initiator s8, +21 four players x 9 bytes (slot type, stocks, damage float, synced stocks, synced health u16), +57 game info block (+0xE stage u16).
   The winner is Slippi's own (SinglesDetermineWinner: stocks, then damage on a time-out, a tie is -1); a quit-out (end method 7) is a tie in the game, in ranked the
   player who left loses; desync (-2) and tie are not reported (the server holds a one-sided or replays), a disconnect is reported as a status. */
static unsigned int be32(const sdshim_u8 *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | (unsigned int)p[3];
}

/* ---- GAME_RECORD of OUR modes  --------------------------------------------------------------------------
   Built here, in the Multishine backend's BD callback, which br_glue.c d_bd reaches ONLY for games of our own universe (g_ms): a Slippi-route (bridge) game goes to the bridge backend
   and never gets here, so it never produces a record (the route byte is a constant 0; the server drops anything else).  The record waits in the RESIDENT queue (hub_ovl.h
   HUB_GR_SLOT) and is sent by gr_step on the overlay's tick, i.e. only while the hub overlay is loaded (menus and the online character screen), never in a match.
   RECORD LOSS: the sender used to wait for phase IDLE / ERR, but a set keeps the
   controller in OD_UP on the character screen between games, so nothing went out until the set ended and the two-slot queue had kept only the newest two.  It also sends in OD_UP
   (between games, on the character screen, like the Game Night reports od_rep_step sends there), with a ready session only (no handshake on that screen).  A record that still goes unsent
   (pushed out of the full queue, or given up after its tries) is counted in queue byte HUB_GR_LOST; the next record that reaches the server carries the count (PQ block body byte 7). */
#define GR_LEN 54u
#define GR_TRIES 3u
#ifndef HUB_GR_LOST
#define HUB_GR_LOST 114u                               /* resident queue byte (after the refresh-HELLO pair 112/113): records lost since the last one the server took, 255 = 255 or more */
#endif
static void gr_put32(unsigned char *p, unsigned int v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void gr_put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static unsigned int gr_cl(unsigned int v, unsigned int mx) { return v > mx ? mx : v; }
/* platform byte (0 unknown, 1 GameCube, 2 Wii, 3 Wii U): the OS bus clock word, 162 MHz on a GameCube (and in Dolphin), 243 MHz on a Wii / Wii U (the test the Wii readout
   lines use, on_bhold.c); a Wii U is told by word 121 of the vBBA kernel's published status block (0xD31BE000, 'VBXT' at word 126; the NIN_MS_PASSHIST kernel writes IsWiiU there, older kernels
   leave 0 = reported as a Wii) (WII_PERFORMANCE_STUDY_2026-11-04 part 2: the records could not tell the consoles apart).  Host tests keep 0. */
static unsigned char gr_platform(void)
{
#if defined(__powerpc__) || defined(__PPC__)
    volatile unsigned int *sb = (volatile unsigned int *)0xD31BE000u;
    if (*(volatile unsigned int *)0x800000F8u <= 200000000u) return 1;
    return (sb[126] == 0x56425854u && sb[80] >= 1u && sb[121] == 1u) ? 3 : 2;
#else
    return 0;
#endif
}
/* LINK block: id 4, length 4, type u8 (0 unknown, 1 wired, 2 Wi-Fi), signal u8 (0 = unknown; no kernel publishes a verified signal yet),
   changes u8 (interface publications after the first since boot: reconnects, saturating), flags u8 (bit 0 the type is the interface kind the Wii's vBBA kernel published, bit 1 the signal was
   sampled, bit 2 the type is assumed from the platform: a GameCube / Dolphin has the BBA = wired).  Built here, in the overlay, at send time: no engine byte.  The kernel words are the ones
   Settings > Network > Adapter reads (s12_macpub.h: 'VBMC' at PPC 0xD3026680, kind in w3 bits 8..15, sequence in w1 = 2 per publication); a GameCube never touches 0xD3000000.  Returns the
   block's length (6) or 0 when the type is unknown (an old kernel, a host test): the server then reads NULL. */
static unsigned int gr_link(const volatile unsigned int *b, unsigned int bus, unsigned char *o)
{
    unsigned int ty = 0, ch = 0, fl = 0, s, w3, kd;
    if (bus && bus <= 200000000u) { ty = 1; fl = 4; }                                  /* GameCube / Dolphin: the BBA */
    else if (bus && b[0] == 0x56424D43u && (s = b[1]) && !(s & 1u) && ((w3 = b[3]) & 1u) && b[1] == s) {
        kd = (w3 >> 8) & 255u; ty = kd == 1u ? 2u : kd == 2u ? 1u : 0u; fl = ty ? 1u : 0u;
        ch = (s >> 1) - 1u; if (ch > 255u) ch = 255u;
    }
    if (!ty) return 0;
    o[0] = 4; o[1] = 4; o[2] = (unsigned char)ty; o[3] = 0; o[4] = (unsigned char)ch; o[5] = (unsigned char)fl;
    return 6;
}
#if defined(__powerpc__) || defined(__PPC__)
static unsigned int gr_link_now(unsigned char *o) { return gr_link((const volatile unsigned int *)0xD3026680u, *(volatile unsigned int *)0x800000F8u, o); }
#else
unsigned int on_link_host[5];                                                         /* host test: bus clock, then the four published words */
static unsigned int gr_link_now(unsigned char *o) { return gr_link(on_link_host + 1, on_link_host[0], o); }
#endif
/* WIRED OPPONENTS ONLY: the ticket of an UNRANKED search carries this console's link class (the game-record source: gr_link, the vBBA kernel's
   interface words; a GameCube = wired) and the Settings switch (Settings > Network status; bit 2 of hudraise in the resident settings; the hub overlay reads it through hub_env wf_cur, a flat build
   through ons_cur).  mnc.c sends them only to a server whose HELLO announced caps2 bit 4.  Ranked, Direct, Game Night and the Slippi route send nothing (the server would ignore it there too).
   Overlay code only: no engine byte. */
#if !defined(ONS_SETTINGS)                                       /* an image without the settings layer (the early compat profiles): no switch, the class alone goes */
#define WF_ON() 0
#elif defined(HUB_OVL)
#define WF_ON() (hub_E && hub_E->wf_magic == HUB_WF_MAGIC && hub_E->wf_cur && (hub_E->wf_cur()->hudraise & 4u))
#else
extern const struct ms_settings *ons_cur(void) __attribute__((weak));
#define WF_ON() (ons_cur && (ons_cur()->hudraise & 4u))
#endif
#ifndef ON_DIAG
static void on_link_ticket(int mode)
{
    unsigned char lb[6];
    mnc_link_cls = mnc_link_pref = 0;
    if (mode != MNC_MODE_UNRANKED) return;
    if (gr_link_now(lb)) mnc_link_cls = lb[2];
    if (WF_ON()) mnc_link_pref = 1;
}
#endif
#ifndef HUB_GR_SLOTS
#define HUB_GR_SLOTS 2u
#define HUB_GR_SLOT 56u
#define HUB_GR_HELLO 112u
#define HUB_GR_BYTES 120u
#define HUB_GR_IOX 120u
#define HUB_GR_IO_LEN 23u
#define HUB_GR_IOX_SLOT 24u
#define HUB_GR_XBYTES 168u
#define HUB_GR_GOX 168u
#define HUB_GR_GO_LEN 10u
#define HUB_GR_GOX_SLOT 10u
#define HUB_GR_X2BYTES 188u
#define HUB_GR_PQX 188u
#define HUB_GR_PQ_LEN 9u
#define HUB_GR_PQX_SLOT 10u
#define HUB_GR_X3BYTES 208u
#endif
/* The queue's size comes from the RESIDENT (hub_env gr_q_n), which may be older than this overlay (engine and overlay are separate packages: a partial OTA or a one-package auto-rollback).
   Residents built between e512b944 and d635ef5d have a 112-byte queue (the two slots only) under the same magic: the records work there, but the refresh-HELLO state (bytes 112/113)
   does not exist, so on_gr_step's h is 0 and the refresh is skipped (review H1).  An older overlay on a newer resident reads only the first 112 bytes, whose layout is unchanged. */
#ifdef HUB_OVL
static unsigned char *gr_q_raw(unsigned int *n)
{
    if (hub_E && hub_E->gs_magic == HUB_GS_MAGIC && hub_E->gr_q) { *n = hub_E->gr_q_n; return hub_E->gr_q; }
    *n = 0; return 0;
}
#else
unsigned char on_gr_q_store[HUB_GR_X3BYTES];                                    /* (a build without the hub overlay keeps the queue here; the host test reads it) */
unsigned int on_gr_q_n = HUB_GR_X3BYTES;                                        /* the host test sets 112 / 120 to play an older resident */
static unsigned char *gr_q_raw(unsigned int *n) { *n = on_gr_q_n; return on_gr_q_store; }
#endif
static unsigned char *gr_q(void) { unsigned int n; unsigned char *q = gr_q_raw(&n); return (q && n >= HUB_GR_SLOTS * HUB_GR_SLOT) ? q : 0; }
static void gr_lost(unsigned char *q) { unsigned int n; (void)gr_q_raw(&n); if (n >= HUB_GR_BYTES && q[HUB_GR_LOST] < 255u) q[HUB_GR_LOST]++; }   /* (no byte on an older resident's 112-byte queue) */
/* w = rgb winner index (0/1), -1 tie, -2 desync, -3 connection lost; lras as in rgb[16] */
static void gr_record(const sdshim_u8 *rgb, int w, int lras, unsigned int gi)
{
    unsigned char b[GR_LEN], *q = gr_q(), *slot = 0;
    unsigned int st[12], ff = 0, ck = 0, side, k, i;
    if (w == -1 && rgb[15] == 7 && (lras == 0 || lras == 1)) w = 1 - lras;                 /* the one who quit loses (as the Ranked report does) */
    if (!q || !od_o.mid_ok || od_o.mode < 0 || od_o.mode > 6 || od_o.mode == 3 || od_o.mode == 4) return;      /* no pairing from OUR server: nothing to record (mode 3/4 do not exist) */
    side = od_o.side ? 1u : 0u;
    (void)od_core_final(&ff, &ck);
    od_core_stats(st);
    if (w == -2 && st[10]) { ff = st[10]; ck = st[11]; }                                    /* DESYNC LOCATOR (the engine saw the two checksums disagree): final frame = the first frame they differed at, checksum = OUR checksum of that frame, so the server's pair rule compares both consoles at the same frame; without it the last confirmed frame as before */
    for (i = 0; i < GR_LEN; i++) b[i] = 0;
    b[0] = 1; b[1] = 0; b[2] = (unsigned char)od_o.mode; b[3] = gr_platform();             /* ver, route 0 = our universe (a constant), mode, platform (1 GameCube / Dolphin, 2 Wii / Wii U, 0 host) */
    for (i = 0; i < 8; i++) b[4 + i] = od_o.mid[i];
    b[12] = (unsigned char)(gi >= 1u && gi <= 99u ? gi : 1u);
    b[13] = (unsigned char)(w == -3 ? 2 : w == -2 ? 3 : rgb[15] == 7 ? 1 : 0);              /* completed / quit / disconnect / desync */
    b[14] = (unsigned char)((w == 0 || w == 1) ? ((unsigned int)w == side ? 0 : 1) : w == -1 ? 2 : 255);
    b[15] = rgb[15]; b[16] = (unsigned char)(lras < 0 ? 255 : lras); b[17] = rgb[72];
    b[18] = rgb[57 + 0x60 + 0x24 * side]; b[19] = rgb[57 + 0x60 + 0x24 * side + 3]; b[20] = rgb[57 + 0x60 + 0x24 * (1u - side)];
    b[21] = rgb[22 + 9 * side]; b[22] = rgb[22 + 9 * (1u - side)];
    b[23] = 255;                                                                           /* region: the console does not know it */
    gr_put32(b + 24, be32(rgb + 2)); gr_put32(b + 28, ff); gr_put32(b + 32, ck);
    if (st[0]) {
        gr_put16(b + 36, gr_cl(st[1], 65535u)); b[38] = (unsigned char)gr_cl(st[2], 255u); gr_put16(b + 39, gr_cl(st[3], 65535u));
        gr_put16(b + 41, gr_cl(st[6], 65535u)); gr_put16(b + 43, gr_cl(st[7], 65535u)); b[45] = (unsigned char)gr_cl(st[8], 255u);
        gr_put16(b + 46, gr_cl(st[4], 65535u)); gr_put16(b + 48, gr_cl(st[5], 65535u));
    }
    if (w == -2 && st[10]) gr_put16(b + 43, 0xD501u);                                       /* DESYNC LOCATOR marker (frames over 17 ms was 0 = unknown): final frame and checksum above are the first mismatching frame and OUR checksum of it */
    b[50] = (unsigned char)(w == -3 ? 1 : 0);
    { unsigned char lb[6]; od_o.link_note = (unsigned char)(st[0] && gr_link_now(lb) && lb[2] == 2u && be32(rgb + 2) >= 1200u && st[3] * 10u >= be32(rgb + 2)); }   /* HEADS-UP (Settings > Network status, ms_net_cost 116): THIS console measured Wi-Fi and its own stall share of this game is >= 100 per 1000 frames (the 'bad' line of), never from a short game */
    gr_put16(b + 51, gr_cl(st[9], 65535u)); b[53] = 0;
    for (k = 0; k < HUB_GR_SLOTS; k++) if (!q[k * HUB_GR_SLOT]) { slot = q + k * HUB_GR_SLOT; break; }     /* (the rule of hub_ovl.h hub_gr_io_slot: core_stats above put the IO block in the same place) */
    if (!slot) {                                                                           /* full: replace the older (slot 0 shifts to 1 ... newest always last) */
        gr_lost(q);                                                                        /* the older record goes unsent: counted, the next record says so */
        for (i = 0; i < HUB_GR_SLOT; i++) q[i] = q[HUB_GR_SLOT + i];
        slot = q + (HUB_GR_SLOTS - 1u) * HUB_GR_SLOT;
    }
    slot[0] = 1; slot[1] = 0;
    for (i = 0; i < GR_LEN; i++) slot[2 + i] = b[i];
}

/* GAME_RECORD sender: one datagram at a time on the hub's mnc, only while no search or other report uses it, retried 3 times 2 s apart, then dropped.  A server refusal drops
   it at once (guest, rate limit).  The overlay is gone at the next scene change; the record stays in the resident queue and goes out at the next idle visit (the server ignores a repeat). */
static unsigned int gr_started, gr_slot, gr_next, hr_started, gr_lsent;
/* The title / Main Menu update check (UPD_TITLE, ut_io below) uses the same client (od_o.m and its mw2 session): mnc_poll and mnu_poll read the same socket and drop each other's
   replies, and a refresh HELLO would replace the session under a running check (review M7).  The update check has priority: while its UPD_CHECK is in flight the sender starts
   nothing (a record waits, it is not a try), and the check starts nothing while a record or the refresh HELLO is in flight. */
#if defined(HUB_OVL) && defined(UPD_TITLE)
static int gr_ut_busy(void);
#elif defined(HUB_OVL)
static int gr_ut_busy(void) { return 0; }
#else
int on_ut_busy_host;                                                            /* host test: 1 = the title's update check is in flight */
static int gr_ut_busy(void) { return on_ut_busy_host; }
#endif
void on_gr_step(struct od *o, unsigned int now_ms)
{
    unsigned int i, n;
    unsigned char *q = gr_q_raw(&n), *h = n >= HUB_GR_BYTES ? q + HUB_GR_HELLO : 0;   /* h = 0 on an older resident's 112-byte queue (no refresh HELLO there) */
    if (!q || n < HUB_GR_SLOTS * HUB_GR_SLOT) return;
#ifdef MS_REC_SELFTEST
    /* LAB / DOLPHIN PROOF ONLY (never in a release; REC_XCF): once the console is enrolled and online, ONE synthetic record of a Game Night game goes into the queue so the
       sender path (queue, mnc op 0x1B, server) runs without a real match.  The server ignores a repeat of the same (identity, match id, game). */
    if (!q[0] && !q[HUB_GR_SLOT] && !gr_started && o->net_up && o->srv_ip && o->cfg_ok && o->m.have_id) {
        static const unsigned char syn[GR_LEN] = { 1, 0, 5, 0, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 1, 0, 0, 2, 255, 31, 2, 0, 10, 2, 0, 255,
                                                   0, 0, 0x0E, 0x1A, 0, 0, 0x0E, 0x10, 0x12, 0x34, 0x56, 0x78, 0, 3, 2, 0, 150, 0, 0, 0, 0, 4, 0, 40, 0, 90, 0, 0x04, 0x2E, 1 };
        for (i = 0; i < GR_LEN; i++) q[2 + i] = syn[i];
        q[0] = 1; q[1] = 0;
    }
#endif
    /* ONE refresh HELLO on a device session: the title's update check says HELLO before the card identity exists, i.e. on an ANONYMOUS session, and the server's 'only'
       mode announces capability bit 15 only on a device session.  Once the console has its identity and the bit is not set, ask again (a device session starts by itself: mw2_use drops the old
       one); the resident (rep_res.c rp_k_server) treats "no bit" as final only after this answered.  Costs one datagram per boot of a console that does not report. */
    if (hr_started && h) {
        if (o->m.state == MNC_DONE) { h[0] = 2; hr_started = 0; }
        else if (o->m.state == MNC_FAILED || o->m.state == MNC_IDLE) { hr_started = 0; if (++h[1] >= 3) h[0] = 2; }
        return;
    }
    if (h && !gr_started && !gr_ut_busy() && h[0] == 0 && o->m.have_id && o->m.have_hello && !(o->m.srv_caps & 0x8000u) && !o->rep_kind && o->m.state != MNC_BUSY
        && o->net_up && o->srv_ip && (o->phase == OD_IDLE || o->phase == OD_ERR)) {
        if (mnc_hello(&o->m, now_ms) == 0) hr_started = 1; else if (++h[1] >= 3) h[0] = 2;
        return;
    }
    if (gr_started) {
        unsigned char *s = q + gr_slot * HUB_GR_SLOT;
        if (o->m.state == MNC_DONE) { s[0] = 0; gr_started = 0; if (h) h[HUB_GR_LOST - HUB_GR_HELLO] -= (unsigned char)(gr_lsent < h[HUB_GR_LOST - HUB_GR_HELLO] ? gr_lsent : h[HUB_GR_LOST - HUB_GR_HELLO]); }   /* the server has the count it carried */
        else if (o->m.state == MNC_FAILED) {
            gr_started = 0;
            if (o->m.err == MNC_E_SERVER || ++s[1] >= GR_TRIES) { s[0] = 0; gr_lost(q); } else gr_next = now_ms + 2000u;
        } else if (o->m.state == MNC_IDLE) gr_started = 0;
        return;
    }
    if (o->phase != OD_IDLE && o->phase != OD_ERR && !(o->phase == OD_UP && mw2_ready(&o->m.w, now_ms))) return;   /* OD_UP = between the games of a set (1255, RECORD LOSS above): only on a ready session */
    if (gr_ut_busy()) return;                                                   /* the update check owns the client: start nothing, the record waits (review M7) */
    if (o->rep_kind || o->m.state == MNC_BUSY || !o->net_up || !o->srv_ip || !o->cfg_ok || !o->m.have_id) return;
    if ((unsigned int)(now_ms - gr_next) > 0x7FFFFFFFu) return;
    for (i = 0; i < HUB_GR_SLOTS; i++) {
        unsigned char *s = q + i * HUB_GR_SLOT, *x = q + HUB_GR_IOX + i * HUB_GR_IOX_SLOT, *g = q + HUB_GR_GOX + i * HUB_GR_GOX_SLOT, *pq = q + HUB_GR_PQX + i * HUB_GR_PQX_SLOT, r[GR_LEN + HUB_GR_IO_LEN + HUB_GR_GO_LEN + HUB_GR_PQ_LEN + 1u + 6u];
        unsigned int k, len = GR_LEN;
        if (!s[0]) continue;
        if (n >= HUB_GR_XBYTES && x[0] == 1u) len += HUB_GR_IO_LEN;            /* the slot's trailing IO block (ms_records.IO; the resident wrote it, hub_ovl.h HUB_GR_IOX): only a resident whose queue has them */
        for (k = 0; k < len; k++) r[k] = k < GR_LEN ? s[2 + k] : x[k - GR_LEN];
        if (n >= HUB_GR_X2BYTES && g[0] == 2u) { for (k = 0; k < HUB_GR_GO_LEN; k++) r[len + k] = g[k]; len += HUB_GR_GO_LEN; }   /* the game-over block (ms_records.GO): written by the resident only when the game's own game-over flag was seen */
        gr_lsent = 0;
        if (n >= HUB_GR_X3BYTES && pq[0] == 3u && len + HUB_GR_PQ_LEN + 1u <= MNC_GAME_RECORD_MAX) {   /* the PQ block (ms_records.PQ, DESYNC_DEEP_REVIEW_2026-11-13 section 7) + body byte 7 since */
            for (k = 0; k < HUB_GR_PQ_LEN; k++) r[len + k] = pq[k];                           /* records this console lost before this one (ms_records pq_lost; an older server reads the 7 known bytes) */
            r[len + 1] = 8u; r[len + HUB_GR_PQ_LEN] = (unsigned char)(gr_lsent = q[HUB_GR_LOST]); len += HUB_GR_PQ_LEN + 1u;
        }
        len += gr_link_now(r + len);                                                          /* the LINK block (id 4) last: type, signal, changes, flags (no byte when the type is unknown) */
        if (mnc_game_record(&o->m, now_ms, r, len) == 0) { gr_started = 1; gr_slot = i; }
        else if (++s[1] >= GR_TRIES) { s[0] = 0; gr_lost(q); }
        return;
    }
}
/* LAB_DIAG upload (test builds with the readout): once a readout is made, three lines per datagram go to the server (mnc op 0x1E) so nobody has to photograph them.
   Payload: 'R', readout number, first line number, then the lines joined with LF.  One datagram at a time, no retry (a lost part is a lost part), only while the client is idle. */
#ifdef HUB_OVL
#include "ld_plan.h"
static struct ld_st ld;
#ifdef MM_FC
unsigned int ms_ld_stat(void) { return (ld.sent & 0xFFFu) | ((ld.nref & 0xFFu) << 12) | ((ld.done & 0xFu) << 20) | ((ld.pass & 1u) << 24) | ((ld.line & 0x1Fu) << 25); }   /* FC word 120: the upload's own state */
#endif
static unsigned int ld_started;
static void on_ld_step(struct od *o, unsigned int now_ms)
{
    unsigned int gen, n, i, k, len;
    int pl;
    const char *l;
    unsigned char b[MNC_LAB_DIAG_MAX];
    if (!hub_E || hub_E->ld_magic != HUB_LD_MAGIC || !hub_E->ld_lines) return;
    if (ld_started) {
        if (o->m.state == MNC_BUSY) return;
        ld_started = 0;
        return;
    }
    n = 0u;
    l = hub_E->ld_lines(&gen, &n);
    if (!gen) return;
    pl = (int)ld_plan(&ld, gen, n, now_ms);
    if (pl == LD_DONE) { n = 0xFFFFFFFFu; hub_E->ld_lines(&gen, &n); return; }       /* every part sent twice: tell the engine, so a later overlay load does not send it again */
    if (pl != LD_SEND) return;
    if ((o->phase != OD_IDLE && o->phase != OD_ERR) || o->rep_kind || o->m.state == MNC_BUSY || !o->net_up || !o->srv_ip || !o->cfg_ok || !o->m.have_id || gr_started || hr_started || gr_ut_busy()) return;
    len = 0; b[len++] = 'R'; b[len++] = (unsigned char)gen; b[len++] = (unsigned char)ld.line;
    for (i = ld.line, k = 0; i < n && k < 3u; i++, k++) {
        const char *s = l + i * 56u;
        unsigned int m = 0;
        while (m < 55u && s[m]) m++;
        if (len + m + 1u > sizeof b) break;
        while (m--) b[len++] = (unsigned char)*s++;
        b[len++] = 10;
    }
    k = mnc_lab_diag(&o->m, now_ms, b, len) == 0;
    if (k) ld_started = 1;
    ld_done(&ld, (int)k, i, n, now_ms);
}
#endif   /* HUB_OVL: only hub_tick (HUB_OVL) calls on_ld_step, so a build without it carries no stub (an unused static function stops a -Werror build) */
static void od_report_game_cb(void *ctx, const sdshim_u8 *rgb)
{
    struct od_game g;
    int w = (signed char)rgb[14], lras = (signed char)rgb[16];
    unsigned int ff = 0, ck = 0, gi = be32(rgb + 6);
    int room = 0, tie = 0;
    (void)ctx;
    on_st.bd++;
    on_st.winner_idx = (unsigned int)(w & 0xFF); on_st.endm = rgb[15]; on_st.lras = (unsigned int)(lras & 0xFF);
    on_st.stocks = ((unsigned int)rgb[22] << 24) | ((unsigned int)rgb[31] << 16) | ((unsigned int)rgb[40] << 8) | (unsigned int)rgb[49];
    on_st.frames = be32(rgb + 2); on_st.game = gi;
#ifdef HUB_SOC
    if (od_o.mid_ok) od_o.mid_ok |= 4;            /* the paired match really ended: the post-fight prompt may come on the character screen (hub_soc.c, tst_fight_allowed) */
#endif
    room = od_o.m.gn_magic == MNC_GN_MAGIC && (od_o.mode == MNC_MODE_ROOM || od_o.m.gn_state == MNC_GN_SEARCH);   /* a Game Night set (the hand-off state counts too: a changed search mode must not leave the console on the character screen) */
    if (room) {                                                                                      /* Game Night: the set is over (reported or not); the menu goes back to the lobby */
#ifdef MNC_RKSET
        if (rk_hand()) od_o.m.gn_state = MNC_GN_RKSET;                                               /* a game of a Grandmaster set: back to the set's prep screen (the server says whether the set goes on) */
        else
#endif
        od_o.m.gn_state = MNC_GN_DONE;
        od_st.gn = (od_st.gn & ~0xFFu) | (unsigned int)od_o.m.gn_state;
    }
    gr_record(rgb, w, lras, gi);                                                                   /* our own 7-day copy of this game (every mode of OUR universe, whether or not it is also a GAME_REPORT) */
    if (!od_o.cfg_ok || !((od_o.mode == MNC_MODE_RANKED && (od_cfg.flags & OD_CFGF_RANKED)) || od_o.mode == MNC_MODE_ROOM)) { on_st.skip_why = OD_RS_MODE; od_st.rep_skipped++; od_st.rep_why = OD_RS_MODE; if (room) on_st.rk_idle = 1; return; }
    if (w == -3) { (void)od_report_status(&od_o, od_clock_ms(), 3); on_st.skip_why = 103u; if (room) on_st.rk_idle = 1; return; }   /* the connection was lost: ms_ranked S_DISCONNECTED */
    if (w == -1 && rgb[15] == 7 && (lras == 0 || lras == 1)) w = 1 - lras;                         /* the one who quit loses */
    if (w == -1 && room) { w = 0; tie = 1; }                                                        /* Game Night draw (equal stocks and damage at the time-out, Stadium with still pads): both sides report slot 1 with the tie flag; the room replays the pair (ms_rooms draw path) */
    if (w != 0 && w != 1) {                                                                         /* Ranked: a tie / desync is not reported.  Game Night: the set is over anyway, back to the lobby (the server voids it at its cap), never stuck on the character screen */
        on_st.skip_why = (w == -2) ? 102u : 101u; od_st.rep_skipped++; od_st.rep_why = (w == -2) ? OD_RS_DESYNC : OD_RS_TIE;
        if (room) on_st.rk_idle = 1;
        return;
    }
    g.game = gi ? (int)gi - 1 : 0;
    if (room) {                                                                                     /* a Bo3 room set (Sweaty): every game is its own Slippi session, so Slippi's game index is always 1; the server wants the set's game (ticket name GN<code>S<set>G<game>, ms_rooms _report: game - 1) */
        const char *t = od_o.target;
        unsigned int tn = 0;
        while (t[tn] && tn < 19u) tn++;
        if (tn >= 4u && t[tn - 2u] == 'G' && t[tn - 1u] >= '1' && t[tn - 1u] <= '9' && t[tn - 3u] >= '0' && t[tn - 3u] <= '9') g.game = (int)(t[tn - 1u] - '1');
    }
#ifdef MNC_RKSET
    if (room && rk_hand() && od_o.m.gn_room[MNC_RK_GAME_AT] >= 1u) g.game = (int)od_o.m.gn_room[MNC_RK_GAME_AT] - 1;      /* a Grandmaster set: Slippi's game index is always 1 (a fresh session per game), the server wants the set's game */
#endif
    g.tie = room ? tie : (int)(be32(rgb + 10) & 0xFFu);                                             /* Game Night: only a real draw is flagged (a tiebreak index is not a draw) */
    g.winner = w + 1;
    g.endm = rgb[15];
    g.stage = rgb[72];                                                    /* low byte of the stage id at game info +0xE */
    g.frames = be32(rgb + 2);
    on_st.final_ok = (unsigned int)od_core_final(&ff, &ck);
    g.final_frame = ff; g.cks = ck;
    g.stocks1 = rgb[22]; g.stocks2 = rgb[31];
    g.chars_ok = room && od_o.m.gn_magic == MNC_GN_MAGIC && (od_o.m.gn_room[OD_GN_ROOM_FLAGS] & OD_GN_RF_CHARS) != 0u;       /* a Game Night set on a server with room types: the characters as played (the game info's player blocks, slot 1 = side 0) */
    g.char1 = rgb[57 + 0x60]; g.char2 = rgb[57 + 0x60 + 0x24];
    if (od_report_game(&od_o, od_clock_ms(), &g) == 0) { on_st.bd_sent++; on_st.rk_idle = 1; on_st.skip_why = 0; on_ranked_as_unranked(0); }
    else { on_st.skip_why = od_st.rep_why; if (room) on_st.rk_idle = 1; }
}
#endif /* ON_OVL */

#if ON_RES
#ifdef HUB_OVL_RES
#include "hub_res.c"                              /* the loader, the scene policy and the thunks of the overlay's entry points (B9, B4, BE, BD, BA tail, od_tick, Update row) */
#include "gns_res.c"                              /* the Game Night match-time block ms/gns.ovl (spectate uploader): AllM loader, B0 hook entry, reply filter */
#ifdef MS_REP_OVL
#include "rep_res.c"                              /* the Slippi game-report overlay ms/rep.ovl: the resident queue, the HSD-heap loader, the scene-change release */
#endif
#endif
#if defined(HUB_OVL_RES) && (defined(MS_RB_READOUT) || defined(MS_HUB_DIAG))
/* the lab readout's BD line (on_bhold.c): the result report chain at a glance, so a cube run shows where it stops */
unsigned int ms_bd_diag(unsigned int i)
{
    switch (i) {
    case 0: return on_st.bd; case 1: return on_st.bd_sent; case 2: return on_st.skip_why; case 3: return on_st.rk_idle;
    case 4: return hub_st[HB_STATE]; case 5: return hub_st[HB_ERR]; case 6: return hub_st[HB_LOADS]; case 7: return H.pend_bd | (H.pend_ba << 1);
    case 8: return od_o.m.gn_magic == MNC_GN_MAGIC ? od_o.m.gn_state : 99u; case 9: return (unsigned int)od_o.mode; case 10: return (unsigned int)od_o.rep_kind; case 11: return hub_st[HB_REARMS];
    case 12: return hub_dbg[0] & 0xFFu; case 13: return hub_dbg[0] >> 8; case 14: return hub_dbg[1]; case 15: return hub_dbg[2]; case 16: return hub_dbg[3];
    case 17: return df_st ? df_st[1] : 99u; case 18: return H.tries; case 19: return (H.gave_up ? 1u : 0u) | (H.api ? 2u : 0u) | (H.pend_bd ? 4u : 0u) | (H.pend_ba ? 8u : 0u);
    case 20: return (unsigned int)((int (*)(void))0x80339B4Cu)() & 0xFFFFu;       /* S: DVDGetDriveStatus now (0 idle, 1 busy, 2.. waiting / cover / error; read raw, not the loader's yes/no) */
    case 21: return (GN.api ? 1u : 0u) | (gns_hot ? 2u : 0u) | (GN.idle ? 4u : 0u) | (GN.gave_up ? 8u : 0u) | (GN.armed ? 16u : 0u) | (gns_alive() ? 32u : 0u);   /* N: the uploader block: loaded, hot, idle, gave up, armed, still listed by AllM */
    }
    return 0;
}
#endif
static void od_report_status_cb(void *ctx, sdshim_u8 cmd, sdshim_u8 arg)
{
    (void)ctx; (void)arg;
    if (cmd == SDSHIM_CMD_REPORT_SET_COMPLETE) on_st.c2++;
    else on_st.c4++;                              /* Slippi's own status strings (game_start_n ...): our server has no use for them */
}

/* OFFLINE GUARD: 1 = the rows that need OUR server must not be entered now, and "Console is offline" is on the status line.  Offline = the BBA link is down (the bridge's
   network state: no adapter, link failed, still waiting for the link) or the hub client has no session and its last HELLO failed within the last 20 s.  No polling of its own: both facts are kept anyway. */
extern int br_link_down(void) __attribute__((weak));
extern void br_say_offline(void) __attribute__((weak));
extern unsigned br_panel_reason(void) __attribute__((weak));
/* ACCOUNT GATE: a GUEST console (no linked account: all-zero device id) cannot open Grandmaster, Mano a mano, Game Night or Friends; the status line says what to do,
   the same way the Slippi row says "Slippi: not logged in".  Checked AFTER the offline guard: without a link the console cannot link either, "Console is offline" is the first thing to fix. */
extern void br_say(const char *msg) __attribute__((weak));
static int guest_console(void)
{
    unsigned i, any = 0;
    if (!od_o.cfg) return 1;
    for (i = 0; i < 8u; i++) any |= od_o.cfg->dev[i];
    return !any;
}
/* FORCED UPDATE: the server's minimum build is above this build -> online modes closed.  Weak: images without the update client never block. */
extern int upd_glue_required(void) __attribute__((weak));
#ifdef UPD_TITLE
int ut_gate_required(void);                                                                  /* hub_res.c: the title check's answer (hub_tchk.req_min) against the running engine build */
#define MS_UPD_REQUIRED() ((upd_glue_required && upd_glue_required()) || ut_gate_required())
#else
#define MS_UPD_REQUIRED() (upd_glue_required && upd_glue_required())
#endif
/* OUR SERVER UNREACHABLE FOR SURE (the fresh seeded card: Dojo hung the console while "No server" showed): 1 when the card was read and gave no setup the client can use, or when the setup is fine but the
   server has not answered once since boot (a HELLO, a WHOAMI, any request) after a grace of SRV_GRACE_MS.  A guest sends no HELLO, so the hub client probes with a WHOAMI until the first answer (hub_soc.c, every 10 s); the
   first answer ends this, and Settings > Network status > Reconnect (ms_net_cost 108) starts it over.  Caught before the row is entered: no search that could wait for a server that is not there. */
#define SRV_GRACE_MS 20000u
static int srv_never_heard(unsigned int now)
{
    if (!od_o.cfg_ok) return onc_final();
    return !od_o.srv_ok_ms && now > SRV_GRACE_MS;
}
int ms_offline_server(int need_account)
{
    unsigned int f = od_o.hello_fail_ms, now = od_clock_ms();
    int off;
    if (need_account == 2) return MS_UPD_REQUIRED() ? 2 : 0;     /* UPDREQ_QUERY: only the update question (the Slippi bridge row) */
    off = br_link_down && br_link_down();
    if (!off && f && (unsigned int)(now - f) < 20000u && !od_o.m.have_hello) off = 1;
    if (!off && srv_never_heard(now)) off = 1;
    if (off && br_say_offline) br_say_offline();
    if (!off && MS_UPD_REQUIRED()) return 2;                         /* UPDREQ_BLOCK: the caller opens the Update screen; offline comes first (no network = nothing to update from) */
    if (!off && need_account && guest_console()) off = 1;      /* the right panel says 'Link this console: Settings, Account.' (no yellow line) */
    return off;
}
/* SLIPPI MATCHMAKING SWITCH (server, admin slippi-mm): the last HELLO carried caps2 bit 2 for this console; read by the bridge (br_glue.c) and the Main Menu's panel (ms_net_cost 107 reason 5) */
/* FAIL CLOSED: no HELLO yet = off for the bridge.  The panel
   below keeps the old test (a HELLO that says off), so it does not say 'switched off' while the console is only connecting.  No helper function: the engine file is at its size limit. */
/* WII ON WI-FI: the Slippi modes are closed for a Wii on Wi-Fi (s12_gc.c ms_wii_wifi: a valid published interface kind 1; unknown = open, a GameCube never);
   our own modes do not ask this gate.  The panel says reason 6. */
extern int ms_wii_wifi(void) __attribute__((weak));
#define WII_WIFI() (ms_wii_wifi && ms_wii_wifi())
int ms_slippi_mm_off(void) { return !od_o.m.have_hello || (od_o.m.srv_caps2 & MNC_SRV_CAPS2_SLIPPI_MM_OFF) != 0 || WII_WIFI(); }
/* TIERED MATCHMAKING (br_glue.c reads these through weak references): the same server switch without the Wii Wi-Fi term (the tier rules decide Wi-Fi: Mano a mano may use Slippi on Wi-Fi,
   Dojo may not), 'no Game Night / Grandmaster-set / Friends hand-off owns this search', 'a linked account is on the card', and the player-facing note for the character screen (hub_soc.c css_frame). */
int ms_slippi_srv_off(void) { return !od_o.m.have_hello || (od_o.m.srv_caps2 & MNC_SRV_CAPS2_SLIPPI_MM_OFF) != 0; }
int ms_tmm_plain(void) { return !(od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state != MNC_GN_NONE); }
int ms_tmm_account(void) { return !guest_console(); }
void ms_tmm_note(unsigned int n) { od_o.tm_note = (unsigned char)n; }
/* NET COST probe (Settings > Diagnostics): i 0/1 count, 2/3 total us, 4/5 max us of the hub proc / its network step, 6 the bridge's menu tick maximum us (br_tick: card poll, state sync) */
extern unsigned int br_tick_max_us(void) __attribute__((weak));
void ms_test_toast(int kind) { od_o.test_toast = (unsigned char)(kind + 1); }      /* TEST TOAST (lab builds): the hub overlay picks it up at its next menu frame */
extern int ms_net_reconnect(void) __attribute__((weak));                                                                          /* s12_gc.c: the full stack, glue side */
extern unsigned int ms_net_test_q(int i) __attribute__((weak));                                                         /* s12_gc.c: Settings > Network status > Test connection */
unsigned int ms_net_cost(int i)
{
    if (i == 100) return od_o.unread_ms && (unsigned int)(od_clock_ms() - od_o.unread_ms) < 45000u ? od_o.unread : 0u;   /* not a cost: the unread message count for the Main Menu, 0 while offline / guest */
    if (i == 105) return od_o.unread_ms && (unsigned int)(od_clock_ms() - od_o.unread_ms) < 45000u ? (unsigned int)od_o.news_req | ((unsigned int)od_o.news_inv << 8) : 0u;   /* the Friends panel: requests | invites << 8 */
    if (i == 106) return od_o.unread_ms && (unsigned int)(od_clock_ms() - od_o.unread_ms) < 45000u && od_o.news_online ? (unsigned int)od_o.news_online - 1u : 0u;   /* friends online: 0 while the server sends none (older server) or the answer is stale */
    if (i == 116) return od_o.link_note;                                                                                    /* LINK QUALITY heads-up: Wi-Fi measured here and the newest game stalled a lot (gr_record); Settings > Network status help line */
    if (i == 103) return od_o.srv_ok_ms && (unsigned int)(od_clock_ms() - od_o.srv_ok_ms) < 30000u ? 1u : 0u;      /* Settings > Network status: the server really answered (a hub tick, HELLO or WHOAMI, any request) within 30 s */
    if (i == 104) { od_o.net_ask_ms = od_clock_ms() | 1u; return 0u; }                                             /* a Settings page is up: the hub overlay sends one WHOAMI per 10 s while this is under 3 s old */
    if (i == 101) { unsigned int v = od_o.room_note; od_o.room_note = 0; return v; }                                    /* take the room a toast sent us to (code + 1), 0 = none */
#ifdef HUB_OVL_RES
    if (i == 102) return H.pend_bd | H.pend_ba;                                                          /* a kept report / cancel / setup still waits for the hub overlay (the Game Night return opens the browser only after it was delivered) */
#endif
    if (i >= 110 && i <= 115) return ms_net_test_q ? ms_net_test_q(i) : 0u;                                         /* the connection test: 110 start, 111 cancel, 112 state, 113 router, 114 internet; 115 the Auto delay word */
    if (i == 109) {                                                                                         /* Settings > Network status: what is missing (0 = nothing known, 1 waiting for the memory card, 2 no online setup, 3 starting, 4 no server address) */
        if (!od_o.cfg_ok) {
#ifdef HUB_OVL_RES
            if (H.pend_cfg) return 3u;                                                                      /* read, the hub overlay applies it at its next enter */
#endif
            return !onc_final() || onc_soft() ? 1u : 2u;
        }
        if (!od_o.net_up) return 3u;
        if (!od_o.srv_ip) return 4u;
#ifdef HUB_OVL_RES
        if (!H.api) return 3u;                                                                              /* the hub overlay is not loaded yet: nobody asks the server */
#endif
        return 0u;
    }
    if (i == 108) {                                                                                         /* Settings > Network status > Reconnect: 0 started, 1 refused (search / match active), 2 already running */
        int rc;
        onc_rearm(1);                                                                                      /* a setup read that gave up on a busy card is asked again at once (Wii early START) */
        rc = ms_net_reconnect ? ms_net_reconnect() : 0;
        if (rc) return (unsigned int)rc;
        mw2_drop(&od_o.m.w); od_o.hello_fail_ms = 0; od_o.srv_ok_ms = 0;                                  /* the server session goes too; the hub client handshakes again once the address is back */
        return 0u;
    }
    if (i == 107) {                                                                                         /* the Main Menu's right panel: 1 offline, 2 guest, Slippi row reason << 4 (br_panel_reason) */
        unsigned int f = od_o.hello_fail_ms, now = od_clock_ms(), v = 0;
        if ((br_link_down && br_link_down()) || (f && (unsigned int)(now - f) < 20000u && !od_o.m.have_hello)) v |= 1u;
        if (guest_console()) v |= 2u;
        if (!(v & 1u) && ((od_o.srv_ok_ms && !(v & 2u) && (unsigned int)(now - od_o.srv_ok_ms) > 30000u) || srv_never_heard(now))) v |= 8u;     /* a linked console's server answered before but not for 30 s, the same window as Settings > Network status; a GUEST has no hub ticks (its only answers are the boot probe's WHOAMI and a Settings page's), so for it one answer since boot counts */     /* the server answered before but not for 16 s (three menu ticks): a cable pulled while running, or the server is down */
        if (br_panel_reason) v |= (br_panel_reason() & 15u) << 4;
        if (((v >> 4) & 15u) != 1u && ((v >> 4) & 15u) != 2u && ((v >> 4) & 15u) != 3u) {
            if (od_o.m.have_hello && (od_o.m.srv_caps2 & MNC_SRV_CAPS2_SLIPPI_MM_OFF)) v = (v & ~0xF0u) | (5u << 4);
            if (WII_WIFI()) v = (v & ~0xF0u) | (6u << 4);              /* a Wii on Wi-Fi: 'Slippi needs a wired connection.' (the more useful reason wins over 5) */
        }   /* Slippi matchmaking switched off by the server: reason 5 unless offline / files missing already say more */
        return v;
    }
    if (i == 6) return br_tick_max_us ? br_tick_max_us() : 0;
    if (i < 2) return od_o.nc_n[i];
    if (i < 4) return od_o.nc_tot[i - 2] / 40u;           /* 40.5 MHz time base: ticks / 40 = about us */
    return od_o.nc_max[i - 4] / 40u;
}

static void od_cleanup(void *ctx)
{
    (void)ctx;
    od_st.ba++;
    od_end_req = 1;
    od_st.latch = 0;
    on_st.rk_idle = 0;
    on_ranked_as_unranked(0);
#ifdef HUB_OVL_RES
    hub_ba_tail();                                /* od_cancel + the identity flush: through the overlay, or kept for its next load (the search state is reset at once) */
#else
    od_cancel(&od_o, od_clock_ms());
    ident_flush();
#endif
}

static void od_ui(void *ctx, sdshim_u8 cmd)
{
    (void)ctx;
    (void)cmd;                                    /* Log in / out / Update: locked rows, nothing to do */
}

#ifdef HUB_OVL_RES
#define od_find hub_b4
#define od_status_b9 hub_b9
#define od_suggest hub_be
#define od_report_game_cb hub_bd
#endif

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
    od_be.report_game = od_report_game_cb;
    od_be.report_status = od_report_status_cb;
#if defined(__powerpc__) || defined(__PPC__)
    __asm__ volatile("mftbl %0" : "=r"(seed));      /* the time base: only mixed into the nonce pool, never a secret */
#else
    seed = 1u;                                       /* host tests */
#endif
    (void)od_init(&od_o, &od_cfg, &od_st, od_send, 0, seed, od_clock_ms());
#ifdef HUB_OVL_RES
    H.seed = seed;                                /* the overlay's first enter seeds the mnc client with it (od_init_mnc) */
#endif
    sdshim_set_backend(s, &od_be);
}

#ifdef SD_CARD_HOLE
/* ---- OTA on the card route: the Update row of the sign-in overlay (a separate image, ms/sign.ovl from the card pack) drives the wire v2 session
   of THIS hub client from the menu thread.  The overlay sees only these four plain functions (ml_glue.c hands them over in struct sgo_updcfg).  Sending happens in the network
   slice: s12_gc.c's od_send queues datagrams that the menu thread sends (armed by ml_upd_hello, drained by od_ob_flush at every od_tick). ---- */
extern void od_ob_arm(void);
void *ml_upd_net(void) { return &od_o.m; }
#ifdef HUB_OVL_RES
int ml_upd_hello(unsigned now_ms) { if (!hub_ready()) return -1; od_ob_arm(); return H.api->upd_hello(now_ms); }   /* no overlay: the Update row reports the failure */
int ml_upd_poll(unsigned now_ms) { return hub_ready() ? H.api->upd_poll(now_ms) : MNC_FAILED; }
#else
int ml_upd_hello(unsigned now_ms) { od_ob_arm(); return mnc_hello(&od_o.m, now_ms); }
int ml_upd_poll(unsigned now_ms) { return mnc_poll(&od_o.m, now_ms); }
#endif
int ml_upd_link(void) { return od_o.net_up && od_o.srv_ip && od_o.cfg_ok; }
/* the Online page of the sign overlay: the identity in force and its card writer, handed over in struct sgo_idcfg */
void *ml_ident_cfg(void) { return &od_cfg; }
int ml_ident_save(const void *c) { return onc_save_ident((const struct od_cfg *)c); }
#if defined(SBR_NETCFG) && defined(SD_CARD_HOLE)   /* the unit that defines it need not see ONS_SETTINGS (on_card.h ONS_NETSEL): same two conditions, lab/dev card route only */
/* lab/dev builds: the card layer re-decoded the setup after Settings > Online > Server changed: apply it to the hub client (at once, or at the overlay's next enter) */
#ifdef HUB_OVL_RES
void ml_cfg_reapply(void) { if (hub_ready()) H.api->cfg_apply(); else H.pend_cfg = 1; }
#else
void ml_cfg_reapply(void) { if (od_apply_config(&od_o) == 0) { on_st.cfg_loads++; od_set_backup(&od_o, onc_backup()); } }
#endif
#endif
/* Game Night hand-off: the menu (ml_glue.c) asks whether the lobby armed a room search when the sign overlay closes */
/* the menu entry after a Game Night character screen: 1 = the player left a watch on purpose (do not return to the room); the mark is spent */
unsigned ml_gn_left_ack(void) { if (od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_LEFT) { od_o.m.gn_state = MNC_GN_NONE; od_o.m.gn_room[0] = 0; return 1u; } return 0u; }
unsigned ml_gn_state(void) { return od_o.m.gn_magic == MNC_GN_MAGIC ? od_o.m.gn_state : 0u; }
unsigned ml_gn_rk(void) { return od_o.m.gn_magic == MNC_GN_MAGIC && !od_o.m.gn_room[0] && (od_o.m.gn_room[MNC_RK_FLAGS_AT] & MNC_RK_FLAG) != 0u; }   /* the hand-off is a Grandmaster set: its character screen reads Grandmaster.  Not behind MNC_RKSET */
#endif
#endif /* ON_RES (od_cleanup .. the Update row) */

#ifdef HUB_OVL
#ifdef HUB_SOC
void hs_enter(void);                                                  /* hub_soc.c */
void hs_leave(void);
#endif
/* ---- the overlay's entry table (hub_ovl.h) ---- */
static void od_ba_tail(void)
{
    od_cancel(&od_o, od_clock_ms());
    ident_flush();
}
static void hub_set_net_ovl(struct od *o) { mnc_set_net(&o->m, o->own_ip, o->mask, o->srv_ip, OD_LAXV(o->cfg->flags)); }
static int hub_enter(const struct hub_env *e, int first)
{
    if (e->abi != HUB_ABI || e->sz_od != (unsigned int)sizeof(struct od) || e->sz_mnc != (unsigned int)sizeof(mnc) || e->sz_cfg != (unsigned int)sizeof(*e->cfg) ||
        e->sz_st != (unsigned int)sizeof(struct od_status)) return -1;
    hub_E = e;
    if (first) od_init_mnc(e->o, e->send_cb, e->recv_cb, e->seed);   /* od_init's second half: the client, the config block, mnc_set_net when the address is known */
    else if (e->o->net_up) hub_set_net_ovl(e->o);                     /* an address that arrived while the overlay was absent */
#ifdef HUB_SOC
    hs_enter();                                                       /* menu toasts, the Mano a mano answer, the post-fight prompt (hub_soc.c) */
#endif
    return 0;
}
#ifdef HUB_SOC
static void hub_leave(void) { hs_leave(); }
#else
static void hub_leave(void) { }
#endif
static int hub_upd_hello(unsigned int now_ms) { return mnc_hello(&od_o.m, now_ms); }
static int hub_upd_poll(unsigned int now_ms) { return mnc_poll(&od_o.m, now_ms); }
static void hub_cfg_apply(void)                  /* the resident B9 read the setup while the overlay was absent (hub_res.c): the rest of od_status_b9's first-B9 step */
{
    if (od_apply_config(&od_o) == 0) { on_st.cfg_loads++; od_set_backup(&od_o, onc_backup()); }
    else od_o.cfg_why = OD_WHY_INCOMPLETE;
}
#ifdef UPD_TITLE
/* ---- update check on the title: the io of upd/ut_check.c on THIS client (the resident od_o.m), called by hub_res.c through
   hub_api.tchk on the menu thread (title and Main Menu scenes only).  Sends go through the resident send queue (ob_arm), as the Update row's.  The UPD_CHECK client lives in
   this block's .data (the bss bound of the packer is the relocation table): a reload drops it and ut_check redoes the step. ---- */
#include "mnu.h"
static mnu ut_u OD_DATA;
static unsigned int ut_have OD_DATA;
static int ut_cfg(unsigned now)
{
    return od_cfg_poll(now);
}
static int ut_link(void) { return od_o.net_up && od_o.srv_ip && od_o.cfg_ok; }
static int ut_net_busy(void) { return od_o.phase != OD_IDLE && od_o.phase != OD_ERR; }    /* a search (or its hand-off) owns the client */
static int gr_ut_busy(void) { return ut_u.state == MNU_BUSY; }                 /* (declared above on_gr_step: the record sender waits for the check) */
static int ut_sess(unsigned now) { return od_o.m.state != MNC_BUSY && !gr_started && !hr_started && mw2_ready(&od_o.m.w, now); }
static int ut_hello(unsigned now) { if (od_o.m.state == MNC_BUSY || gr_started || hr_started) return 1; hub_E->ob_arm(); return mnc_hello(&od_o.m, now) == 0 ? 0 : -1; }   /* a record or the refresh HELLO in flight: a later frame (r > 0) */
static int ut_hello_poll(unsigned now)
{
    int st = mnc_poll(&od_o.m, now);
    return st == MNC_DONE ? 1 : st == MNC_FAILED ? -1 : 0;
}
static int ut_check(unsigned now, unsigned abi, unsigned have)
{
    mnu_init(&ut_u, &od_o.m);
    ut_u.want_adv = 1;                                          /* FORCED UPDATE: also ask for the advisory record (ours_min); a server without it answers as before */
    ut_have = have;
    hub_E->ob_arm();
    return mnu_check(&ut_u, now, abi, have);
}
static int ut_check_poll(unsigned now, unsigned *latest)
{
    int st = mnu_poll(&ut_u, now);
    if (st == MNU_BUSY) return 0;
    if (st == MNU_DONE) {
        if ((ut_u.flags & MNU_UF_NEWER) && ut_u.latest > ut_have) { *latest = ut_u.latest; return 1; }
        return 2;
    }
    if (ut_u.err == MNU_E_TIMEOUT || ut_u.err == MNU_E_NOSESSION || ut_u.status == MNU_ST_RATE_LIMITED || mnc_busy_hint(ut_u.status)) return -1;
    return -2;
}
static unsigned ut_req_min(void) { return ut_u.have_adv ? ut_u.adv_ours_min : 0; }
static void ut_abort(void) { if (ut_u.state == MNU_BUSY) mnu_abort(&ut_u); }
static unsigned ut_rand(void) { return mnc_rand(&od_o.m); }
static const struct ut_io ut_io = { ut_cfg, ut_link, ut_net_busy, ut_sess, ut_hello, ut_hello_poll, ut_check, ut_check_poll, ut_abort, ut_rand, ut_req_min };
static void hub_tchk(struct hub_tchk *t, unsigned int now_ms)
{
    if (t && t->magic == HUB_TCHK_MAGIC) ut_step(t, &ut_io, now_ms);
}
#define HUB_TCHK_FIELDS , HUB_TCHK_MAGIC, hub_tchk
#else
#define HUB_TCHK_FIELDS
#endif
static void hub_tick(struct od *o, unsigned int now_ms) { od_tick(o, now_ms); on_gr_step(o, now_ms); on_ld_step(o, now_ms); }
const struct hub_api hub_api = { HUB_ABI, hub_enter, hub_leave, hub_tick, hub_set_net_ovl, od_status_b9, od_find, od_suggest, od_report_game_cb, od_ba_tail,
                                 hub_upd_hello, hub_upd_poll, hub_cfg_apply HUB_TCHK_FIELDS };
#endif
