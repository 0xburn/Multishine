/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr_card: see sbr_card.h.  Original work; the SDK call sequence is the one offline/multishine/menus/om/om_card.c and
 * proved in Dolphin (retail entry points, config/GALE01/symbols.txt). */
#include "sbr_card.h"
#include "sbr_ident.h"
#include "sbr_netcfg.h"

typedef sbr_u32 u32;
typedef sbr_s32 s32;
typedef sbr_u16 u16;
typedef sbr_u8 u8;

#ifndef SBRC_DATA
#ifdef SBRC_HOST
#define SBRC_DATA
#else
#define SBRC_DATA __attribute__((section(".data")))
#endif
#endif

struct card_fi { s32 chan, fileNo, offset, length; u16 iBlock, pad; };

#define R_READY 0
#define R_BUSY (-1)
#define R_NOCARD (-3)
#ifdef MS_CARD_SHARED_WA   /* ENGINE_MEMORY_BREAKDOWN steps 2a/2b: on_card and sbr_card share ONE CARD work area + block buffer, owned by on_hist.c (its header comment has the contract):
                              held for the whole operation (load below) via ms_card_wa_acquire(2) / ms_card_wa_release; a refusal (the other user holds it, or the area is
                              not available) is SBRC_L_BUSY = 'ask again at the next menu command', never an error. */
extern u8 *ms_card_wa_p;
extern u8 *ms_card_wa_acquire(u32 owner);
extern void ms_card_wa_release(void);
#endif
#define R_NOFILE (-4)
#define BLOCK 0x2000

#ifdef SBRC_HOST                    /* host tests: a fake card behind the same entry points */
#include "tests/sbrc_host.h"
#define CARDProbeEx h_CARDProbeEx
#define CARDMountAsync h_CARDMountAsync
#define CARDUnmount h_CARDUnmount
#define CARDOpen h_CARDOpen
#define CARDClose h_CARDClose
#define CARDRead h_CARDRead
#define CARDSync h_CARDSync
#define CARDSyncCallback ((void *)0)
#define VIWaitForRetrace h_VIWaitForRetrace
#define melee_idle h_melee_idle
static u32 tbl(void) { return 0; }
extern int fake_ee_off;
static int ee_on(void) { return !fake_ee_off; }
#else
typedef s32 (*fn_probe)(s32 chan, s32 *mem, s32 *sector);
typedef s32 (*fn_mount)(s32 chan, void *wa, void *detach_cb, void *attach_cb);
typedef s32 (*fn_chan)(s32 chan);
typedef s32 (*fn_open)(s32 chan, const char *name, struct card_fi *fi);
typedef s32 (*fn_close)(struct card_fi *fi);
typedef s32 (*fn_rw)(struct card_fi *fi, void *buf, s32 len, s32 off);
typedef void (*fn_void)(void);
#define CARDProbeEx ((fn_probe)0x80355F94)
#define CARDMountAsync ((fn_mount)0x80356688)
#define CARDUnmount ((fn_chan)0x803568C4)
#define CARDOpen ((fn_open)0x80357534)
#define CARDClose ((fn_close)0x803576AC)
#define CARDRead ((fn_rw)0x80357E88)
#define CARDSync ((fn_chan)0x80353424)               /* __CARDSync */
#define CARDSyncCallback ((void *)0x80352274)        /* __CARDSyncCallback */
#define VIWaitForRetrace ((fn_void)0x8034F314)
#define LBCN_STATE 0x80432A68u                       /* Melee's card layer (retail; the same checks as om_card / opt_card) */
#define LBCG_STATE 0x80433318u
#define CARD_BLOCK 0x804A8B38u
static u32 tbl(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}
static int ee_on(void)                        /* MSR[EE]: the bridge's menu poll runs with interrupts off (br_glue.c irq_off); the retrace count only moves in the VI interrupt */
{
    u32 m;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    return (m & 0x8000u) != 0;
}
#ifdef MS_CARD_SHARED_WA                      /* the engine links on_hist.c (our of the shared CARD work area): its ms_card_melee_idle is this same check, one copy (engine size) */
extern int ms_card_melee_idle(void);
#define melee_idle ms_card_melee_idle
#else
static int melee_idle(void)
{
    int i;
    if (*(volatile s32 *)(LBCN_STATE + 0x80) || *(volatile s32 *)(LBCN_STATE + 0x8AC)) return 0;
    for (i = 0; i < 11; i++)
        if (*(volatile s32 *)(LBCN_STATE + 0x510 + 0x54 * i) != 14) return 0;
    if (*(volatile s32 *)(LBCG_STATE + 0xC) || *(volatile s32 *)(LBCG_STATE + 0x10)) return 0;
    if (*(volatile s32 *)CARD_BLOCK || *(volatile s32 *)(CARD_BLOCK + 0x110)) return 0;
    return 1;
}
#endif
#endif

#define WA_SIZE 0xA000
#define PROBE_TRIES 120             /* retraces (2 s) while CARDProbeEx says BUSY */

struct sbrc_status sbrc_st SBRC_DATA = { 0x53424344u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#ifdef MS_CARD_SHARED_WA
#define g_mem ms_card_wa_p                         /* the area on_hist.c hands out (WA_SIZE + 0x2000 bytes) */
#else
static u8 g_mem[WA_SIZE + BLOCK] __attribute__((aligned(32))) SBRC_DATA;
#endif
static struct card_fi g_fi SBRC_DATA;
static const char g_idname[] = SBR_ID_FILE;

static u8 *wa(void) { return g_mem; }
static u8 *buf(void) { return g_mem + WA_SIZE; }

static int g_deferred SBRC_DATA;                   /* 1 = the last probe was BUSY and interrupts were off: nothing was waited for */
static s32 card_mount(s32 chan)     /* probe + mount; 0 when mounted, else the SDK result (left unmounted) */
{
    s32 mem = 0, sector = 0, r;
    int tries = PROBE_TRIES;
    g_deferred = 0;
    while ((r = CARDProbeEx(chan, &mem, &sector)) == R_BUSY && --tries > 0) {
        /* with interrupts off VIWaitForRetrace never returns (field report the console froze for good on the Slippi row; the probe says BUSY for a moment after
           another card session, e.g. the Event overlay's prefetch): do not wait, ask again at the next poll (SBRC_L_BUSY, not an error) */
        if (!ee_on()) { g_deferred = 1; return R_BUSY; }
        VIWaitForRetrace();
    }
    if (r != R_READY) return r;
    if (sector != BLOCK) return -2;                /* CARD_RESULT_WRONGDEVICE: Melee's own rule */
#ifdef MS_CARD_SHARED_WA
    if (!wa()) return R_BUSY;                      /* no work area held: never mount on a null area */
#endif
    r = CARDMountAsync(chan, wa(), 0, CARDSyncCallback);
    if (r >= 0) r = CARDSync(chan);
    if (r != R_READY) {
        CARDUnmount(chan);                         /* BROKEN/ENCODING leave it mounted */
        return r;
    }
    return 0;
}

/* read the first block of `name` on chan into buf: 1 = read, 0 = no file, -1000 = no card, other < 0 = SDK error.  Always
   unmounts. */
static int read_chan(s32 chan, const char *name, s32 *res)
{
    s32 r = card_mount(chan);
    int got;
    *res = r;
    if (r != 0) return r == R_NOCARD ? -1000 : (r < 0 ? r : -1);
    r = CARDOpen(chan, name, &g_fi);
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), BLOCK, 0);
        got = r == R_READY ? 1 : (r < 0 ? r : -1);
        CARDClose(&g_fi);
    } else if (r == R_NOFILE) {
        got = 0;
    } else {
        got = r < 0 ? r : -1;
    }
    *res = r;
    CARDUnmount(chan);
    return got;
}

/* the common loop: slot A then B, the first file found decides.  check rates buf: 0 valid, 1 damaged, 2 newer.
   Returns SBRC_L_* and the slot (1/2) in *slot. */
static int load_held(const char *name, int (*check)(void *ctx), void *ctx, u32 *res_out, int *slot)
{
    s32 res[2];
    int got[2], chan, cards = 0, v = -1;
    res[0] = res[1] = R_NOCARD;
    got[0] = got[1] = -1000;
    *slot = 0;
    if (!melee_idle()) return SBRC_L_BUSY;       /* Melee is using the card: ask again at the next menu command */
    for (chan = 0; chan < 2; chan++) {
        got[chan] = read_chan(chan, name, &res[chan]);
        if (g_deferred) break;
        if (got[chan] != -1000) cards++;
        if (got[chan] == 1) {
            v = check(ctx);
            sbr_wipe(buf(), BLOCK);                /* R7: nothing of the file stays in the block buffer */
            *slot = chan + 1;
            break;
        }
    }
    *res_out = ((u32)res[0] << 16) | ((u32)res[1] & 0xFFFFu);
    if (v < 0 && g_deferred) return SBRC_L_BUSY;     /* a probe that was BUSY with interrupts off: ask again later */
    if (v == 0) return SBRC_L_OK;
    if (v == 1) return SBRC_L_DAMAGED;
    if (v == 2) return SBRC_L_NEWER;
    if (!cards) return SBRC_L_NOCARD;
    if ((got[0] < 0 && got[0] != -1000) || (got[1] < 0 && got[1] != -1000)) return SBRC_L_ERROR;
    return SBRC_L_NOFILE;
}

#ifdef MS_CARD_SHARED_WA
static int load(const char *name, int (*check)(void *ctx), void *ctx, u32 *res_out, int *slot)
{
    int r;
    *slot = 0;
    *res_out = 0;
    if (!melee_idle()) return SBRC_L_BUSY;       /* Melee is using the card: ask again at the next menu command */
    if (!ms_card_wa_acquire(2)) return SBRC_L_BUSY;   /* the other card user holds the area, or it is not available (heap full): ask again later */
    r = load_held(name, check, ctx, res_out, slot);
    ms_card_wa_release();                         /* also zeroes the block buffer */
    return r;
}
#else
#define load load_held
#endif

static int check_ident(void *ctx)
{
    int r = sbr_ident_parse(buf() + SBR_ID_FILE_OFF, SBR_ID_PAYLOAD_LEN, (struct sbr_ident *)ctx);
    sbrc_st.id_parse = (u32)r;
    return r == SBR_ID_OK ? 0 : 1;
}

int sbrc_read_ident(struct sbr_ident *id)
{
    u32 t0 = tbl(), res = 0;
    int slot, r;
    sbrc_st.id_tries++;
    sbr_wipe(id, sizeof *id);
    r = load(g_idname, check_ident, id, &res, &slot);
    if (r != SBRC_L_OK) sbr_wipe(id, sizeof *id);
    sbrc_st.id_status = (u32)r;
    sbrc_st.id_slot = (u32)slot;
    sbrc_st.id_res = res;
    sbrc_st.ticks = tbl() - t0;
    return r;
}

/* ---- LOCAL FORK: the Lucky Stats key (menus/rep/lucky.h) ---- */
int lucky_parse(const unsigned char *p, unsigned int n, char *key)
{
    unsigned int i, ok;
    if (n < 84u || p[0] != 'L' || p[1] != 'K' || p[2] != 'Y' || p[3] != 'S' || p[4] != 1u) return 0;
    if (sbr_crc32(p, 80u) != (((u32)p[80] << 24) | ((u32)p[81] << 16) | ((u32)p[82] << 8) | p[83])) return 0;
    ok = p[8] == 'l' && p[9] == '7' && p[10] == 'u' && p[11] == '_';
    for (i = 12; i < 76u; i++) if (!((p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f'))) ok = 0;
    for (; i < 80u; i++) if (p[i]) ok = 0;
    if (!ok) return 0;
    for (i = 0; i < 72u; i++) key[i] = (char)p[8u + i];
    return 1;
}
static int check_lucky(void *ctx) { return lucky_parse(buf() + 0x1000u, 84u, (char *)ctx) ? 0 : 1; }
int sbrc_read_lucky(char *key)
{
    u32 res = 0;
    int slot, r = load("MultishineLucky", check_lucky, key, &res, &slot);
    if (r != SBRC_L_OK) sbr_wipe(key, 72u);
    return r;
}

#ifdef SBR_NETCFG
/* dev/lab builds only: the one-block file "MultishineBridge" holds nothing but the net-config section.  Since the bridge
   no longer reads or writes a status-document record in it (the status server is gone) and never writes the card at all. */
static int check_netcfg(void *ctx)
{
    (void)ctx;
    sbr_nc_parse_block(buf());
    return 0;
}

int sbrc_read_netcfg(void)
{
    u32 t0 = tbl(), res = 0;
    int slot, r;
    sbrc_st.c_tries++;
    r = load(SBR_NC_FILE, check_netcfg, 0, &res, &slot);
    sbrc_st.c_status = (u32)r;
    sbrc_st.c_slot = (u32)slot;
    sbrc_st.c_res = res;
    sbrc_st.ticks = tbl() - t0;
    return r;
}
#endif
