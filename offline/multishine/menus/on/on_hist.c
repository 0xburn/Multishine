/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_hist: the code-history card layer of the 'Multishine online' image.  A FORK of ../om/om_card.c (which stays byte-identical so that the ids of the
 * online-menu and online-direct images do not move); it is #included by on_card.c (one translation unit: the 40 KB CARD work area and the 8 KB block buffer
 * are shared with the setup reader) and implements the SLOT POLICY of, rules 1-4:
 *  - a slot whose probe/mount says BROKEN (-6), ENCODING (-13), WRONGDEVICE (-2, also a sector size other than 0x2000) or NO CARD is SKIPPED (counted as
 *    'unusable'), not an error, as long as the other slot gives a definite answer (a valid / damaged / newer file, or no file on a good card);
 *    status ERROR only when no slot gave a definite answer; a hard error (read failure, probe still BUSY after 2 s) on one slot is NOT skipped when the
 *    other slot has no file (the file could be on the failing slot): it stays ERROR and is retried.
 *  - writes never format and never touch a slot that is not mountable; target: the slot that holds the file, else the slot that holds Melee's save,
 *    else slot A, else slot B.
 * Unchanged from om_card.c: see its header. Original work; the SDK call sequence is the one proved in Dolphin (retail entry points,
 * config/GALE01/symbols.txt): CARDProbeEx (BUSY retried) -> sector size 0x2000 -> CARDMountAsync + __CARDSync -> ... -> CARDUnmount on
 * EVERY path after a mount attempt, so both channels are unmounted after every pass and Melee's own card tasks find the card as they expect.
 *
 * File "<name>Codes" (OMC_NAME, GALE/01, ONE block = 0x2000 bytes: two comment lines at 0, the SDCH record at 0x1000, see codehist.h).
 *  - load: slot A then slot B, the first file that opens decides (a valid record wins; a damaged one is replaced by the next save; a record from a
 *    newer version is never read and never overwritten).  Never creates anything.
 *  - save: only our own file; overwrite in place (comment kept), or create it.  Creating on slot A while Melee's own save is missing needs room for
 *    that save (11 blocks + 1 directory entry) after ours ( rule), like.  The write is read back and the record
 *    compared; a mismatch is reported (OMC_W_VERIFY), nothing else is done.
 *  - Power loss inside the ~50 ms sector write can damage this one block: the CRC catches it, the history starts again from RAM/empty.  No second
 *    copy: a file with two slots would need two blocks, and the list is a convenience.
 * Memory: the 40 KB CARD work area and the 8 KB block buffer are static .data of the image (the flat image has no .bss).
 * Soft-float, no heap, no libc. */
#include "om_card.h"
#include "on_card.h"

typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef OM_DATA
#define OM_DATA __attribute__((section(".data")))
#endif
#ifndef OMC_NAME
#define OMC_NAME "MultishineCodes"
#endif
#ifndef OMC_LINE1
#define OMC_LINE1 "Multishine"
#endif
#ifndef OMC_LINE2
#define OMC_LINE2 "Direct code history"
#endif

struct card_fi { s32 chan, fileNo, offset, length; u16 iBlock, pad; };
struct card_stat {                  /* CARDStat (0x6C) */
    char fileName[32];
    u32 length, time;
    u8 gameName[4], company[2], bannerFormat, pad;
    u32 iconAddr;
    u16 iconFormat, iconSpeed;
    u32 commentAddr, offBanner, offBannerTlut, offIcon[8], offIconTlut, offData;
};

#define R_READY 0
#define R_BUSY (-1)
#define R_NOCARD (-3)
#define R_NOFILE (-4)
#define R_NOENT (-8)
#define R_INSSPACE (-9)

#ifdef OMC_HOST                     /* host tests: a fake card behind the same entry points */
#include "omc_host.h"
#define CARDProbeEx h_CARDProbeEx
#define CARDMountAsync h_CARDMountAsync
#define CARDUnmount h_CARDUnmount
#define CARDOpen h_CARDOpen
#define CARDClose h_CARDClose
#define CARDRead h_CARDRead
#define CARDWrite h_CARDWrite
#define CARDCreateAsync h_CARDCreateAsync
#define CARDGetStatus h_CARDGetStatus
#define CARDSetStatusAsync h_CARDSetStatusAsync
#define CARDFreeBlocks h_CARDFreeBlocks
#define CARDSync h_CARDSync
#define CARDSyncCallback ((void *)0)
#define VIWaitForRetrace h_VIWaitForRetrace
#define OSReport h_OSReport
#define omc_melee_idle h_melee_idle
u32 h_tbl_now;                               /* host tests set the time base */
static u32 tbl(void) { return h_tbl_now; }
#else
typedef s32 (*fn_probe)(s32 chan, s32 *mem, s32 *sector);
typedef s32 (*fn_mount)(s32 chan, void *wa, void *detach_cb, void *attach_cb);
typedef s32 (*fn_chan)(s32 chan);
typedef s32 (*fn_open)(s32 chan, const char *name, struct card_fi *fi);
typedef s32 (*fn_close)(struct card_fi *fi);
typedef s32 (*fn_rw)(struct card_fi *fi, void *buf, s32 len, s32 off);
typedef s32 (*fn_create)(s32 chan, const char *name, u32 size, struct card_fi *fi, void *cb);
typedef s32 (*fn_getstat)(s32 chan, s32 fileNo, struct card_stat *st);
typedef s32 (*fn_setstat)(s32 chan, s32 fileNo, struct card_stat *st, void *cb);
typedef s32 (*fn_free)(s32 chan, s32 *bytes, s32 *files);
typedef void (*fn_void)(void);
typedef void (*fn_report)(const char *fmt, ...);
#define CARDProbeEx ((fn_probe)0x80355F94)
#define CARDMountAsync ((fn_mount)0x80356688)
#define CARDUnmount ((fn_chan)0x803568C4)
#define CARDOpen ((fn_open)0x80357534)
#define CARDClose ((fn_close)0x803576AC)
#define CARDRead ((fn_rw)0x80357E88)
#define CARDWrite ((fn_rw)0x80358204)
#define CARDCreateAsync ((fn_create)0x80357838)
#define CARDGetStatus ((fn_getstat)0x803585F8)
#define CARDSetStatusAsync ((fn_setstat)0x80358724)
#define CARDFreeBlocks ((fn_free)0x803532D4)
#define CARDSync ((fn_chan)0x80353424)               /* __CARDSync */
#define CARDSyncCallback ((void *)0x80352274)        /* __CARDSyncCallback */
#define VIWaitForRetrace ((fn_void)0x8034F314)
#define OSReport ((fn_report)0x803456A8)

/* Melee's card layer (retail offsets, same checks as optcard_melee_idle) */
#define LBCN_STATE 0x80432A68u
#define LBCG_STATE 0x80433318u
#define CARD_BLOCK 0x804A8B38u
static u32 tbl(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}
int ms_card_melee_idle(void)                    /* not static: with MS_CARD_SHARED_WA it is also the bridge's melee_idle (offline/bridge/sbr_card.c, the same checks; engine size) */
{
    int i;
    if (*(volatile s32 *)(LBCN_STATE + 0x80) || *(volatile s32 *)(LBCN_STATE + 0x8AC)) return 0;
    for (i = 0; i < 11; i++)
        if (*(volatile s32 *)(LBCN_STATE + 0x510 + 0x54 * i) != 14) return 0;
    if (*(volatile s32 *)(LBCG_STATE + 0xC) || *(volatile s32 *)(LBCG_STATE + 0x10)) return 0;
    if (*(volatile s32 *)CARD_BLOCK || *(volatile s32 *)(CARD_BLOCK + 0x110)) return 0;
    return 1;
}
#define omc_melee_idle ms_card_melee_idle
#endif

#define WA_SIZE 0xA000
#define PROBE_TRIES 120             /* retraces (2 s) while CARDProbeEx says BUSY */
#define ERROR_TRIES 3               /* card errors retried at later menu loads */

struct omc_status omc_st OM_DATA = { 0x4F4D4348u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#ifdef MS_CARD_SHARED_WA
/* ONE CARD work area + block buffer (WA_SIZE + CH_BLOCK = 49,152 B) for on_card and sbr_card (ENGINE_MEMORY_BREAKDOWN steps 2a and 2b).  Both are synchronous mount-use-unmount on the main
   thread, so one area is enough; every card OPERATION (load_card, save_card, onc_on_status here, load in sbr_card.c) brackets itself with ms_card_wa_acquireour /
   ms_card_wa_release and treats a refusal as 'card busy, ask again at the next menu command' (never an error, never a halt).  The buffer is used after the unmount (the
   parsers read buf()), so the area is held for the whole operation, not per mount.
   ms_card_owner: 0 free, 1 on_card, 2 sbr_card.  ms_card_conflicts counts an acquire that found it held (must stay 0: the proof that the two users never overlap; the second
   user is refused, so a conflict cannot corrupt the first).  ms_card_refused counts every refusal (conflict, allocation failure, arena guard).
   Where the area lives (MS_CARD_EXT_WA = outside the image):
     - MS_CARD_WA_ADDR defined (card route, card_payload.py): the loader's own work area [CL_HOLE_MAX-0xC000, CL_HOLE_MAX) which sits ABOVE the claimed hole, i.e. above the lowered
       lbHeap arena_hi and AllM end, so no heap owns it; guard: lbHeap arena_hi (0x80431FA4) must be <= the area, else the layout does not match the loader and the operation is refused;
     - not defined (patched / ISO route): 49,152 B from the HSD heap with OSAllocFromHeap (NOT HSD_MemAlloc, which asserts on failure) and OSFreeToHeap at the end of the operation;
       a failed allocation refuses the operation.
   Without MS_CARD_EXT_WA the area is a static array of the image (step 2a layout). */
u32 ms_card_owner OM_DATA, ms_card_conflicts OM_DATA, ms_card_refused OM_DATA, ms_card_alloc_fail OM_DATA, ms_card_guard_fail OM_DATA, ms_card_ops OM_DATA, ms_card_wa_at OM_DATA;
u8 *ms_card_wa_p OM_DATA;
#ifndef MS_CARD_EXT_WA
u8 ms_card_wa[WA_SIZE + CH_BLOCK] __attribute__((aligned(32))) OM_DATA;
#else
#ifdef OMC_HOST                                    /* host tests: a fake heap, a fake lbHeap arena_hi and a fixed area (omc_host.h) */
#define MS_WA_ARENA_HI() h_arena_hi
#define MS_WA_FIXED_PTR() ((u8 *)h_wa_fixed)
#define MS_WA_ALLOC(n) h_wa_alloc(n)
#define MS_WA_FREE(p) h_wa_free(p)
#endif
#ifndef MS_WA_ARENA_HI
#define MS_WA_ARENA_HI() (*(volatile u32 *)0x80431FA4u)                        /* lbHeap_80431FA0.arena_hi */
#endif
#ifndef MS_WA_FIXED_PTR
#define MS_WA_FIXED_PTR() ((u8 *)(MS_CARD_WA_ADDR))
#endif
#ifndef MS_WA_ALLOC
#define MS_WA_ALLOC(n) (((void *(*)(int, u32))0x80343EF0u)(((int (*)(void))0x80375404u)(), (n)))     /* OSAllocFromHeap(HSD_GetHeap, n): 0 when it does not fit */
#define MS_WA_FREE(p) (((void (*)(int, void *))0x80343FECu)(((int (*)(void))0x80375404u)(), (p)))      /* OSFreeToHeap */
#endif
#endif
#define g_mem ms_card_wa_p
u8 *ms_card_wa_acquire(u32 owner)
{
    u8 *p;
    if (ms_card_owner) {                           /* the other user is in the middle of an operation: never share, refuse */
        ms_card_conflicts++;
        ms_card_refused++;
        return 0;
    }
#ifndef MS_CARD_EXT_WA
    p = ms_card_wa;
#elif defined(MS_CARD_WA_ADDR)
    if (MS_WA_ARENA_HI() > (u32)(MS_CARD_WA_ADDR)) {   /* the area would lie inside the heap arena: this payload was built for another loader layout */
        ms_card_guard_fail++;
        ms_card_refused++;
        return 0;
    }
    p = MS_WA_FIXED_PTR();
#else
    p = (u8 *)MS_WA_ALLOC(WA_SIZE + CH_BLOCK);
    if (!p) {
        ms_card_alloc_fail++;
        ms_card_refused++;
        return 0;
    }
#endif
    ms_card_owner = owner;
    ms_card_wa_p = p;
    ms_card_wa_at = (u32)(unsigned long)p;
    ms_card_ops++;
    return p;
}
void ms_card_wa_release(void)
{
    u32 i;
    u8 *p = ms_card_wa_p;
    if (!ms_card_owner) return;
    for (i = 0; i < CH_BLOCK; i++) p[WA_SIZE + i] = 0;      /* nothing of a file (the identity) stays in the block buffer */
#ifndef OMC_HOST
    for (i = 0; i < WA_SIZE + CH_BLOCK; i += 32)            /* write back + invalidate the whole area: the CPU touched it (parsers, wipe) and the next owner (a heap block, a DMA target) must not get dirty lines */
        __asm__ volatile("dcbf 0,%0" : : "r"(p + i) : "memory");
    __asm__ volatile("sync" : : : "memory");
#endif
#if defined(MS_CARD_EXT_WA) && !defined(MS_CARD_WA_ADDR)
    MS_WA_FREE(p);
#endif
    ms_card_wa_p = 0;
    ms_card_owner = 0;
}
#define WA_GET(o) (ms_card_wa_acquire(o) != 0)
#define WA_PUT() ms_card_wa_release()
#else
static u8 g_mem[WA_SIZE + CH_BLOCK] __attribute__((aligned(32))) OM_DATA;
#define WA_GET(o) 1
#define WA_PUT() ((void)0)
#endif
static struct card_fi g_fi OM_DATA;
static struct ch_hist g_h OM_DATA;
static int g_done OM_DATA, g_dirty OM_DATA, g_newer OM_DATA, g_slot OM_DATA;   /* g_slot: 0 none found, 1 = A, 2 = B */
static const char g_name[] = OMC_NAME;
static const char g_melee_save[] = "SuperSmashBros0110290334";   /* :121 */

static u8 *wa(void) { return g_mem; }
static u8 *buf(void) { return g_mem + WA_SIZE; }

static void sync_status(void)
{
    omc_st.count = g_h.n;
    omc_st.flags = (u32)((g_done ? 1 : 0) | (g_dirty ? 2 : 0) | (g_newer ? 4 : 0));
}

/* probe + mount; 0 when mounted, else the SDK result (the channel is left unmounted) */
static s32 card_mount(s32 chan)
{
    s32 mem = 0, sector = 0, r;
    int tries = PROBE_TRIES;
    while ((r = CARDProbeEx(chan, &mem, &sector)) == R_BUSY && --tries > 0) VIWaitForRetrace();
    if (r != R_READY) return r;
    if (sector != 0x2000) return -2;               /* CARD_RESULT_WRONGDEVICE: Melee's own rule */
#ifdef MS_CARD_SHARED_WA
    if (!wa()) return R_BUSY;                      /* no work area held (a caller without WA_GET): never mount on a null area */
#endif
    r = CARDMountAsync(chan, wa(), 0, CARDSyncCallback);
    if (r >= 0) r = CARDSync(chan);
    if (r != R_READY) {
        CARDUnmount(chan);                         /* BROKEN/ENCODING leave it mounted */
        return r;
    }
    return 0;
}

/* read the file's block on chan into buf: 1 = read, 0 = no file, -1000 = no card, other < 0 = SDK error.  Always unmounts. */
static int read_chan(s32 chan, s32 *res)
{
    s32 r = card_mount(chan);
    int got;
    *res = r;
    if (r != 0) return r == R_NOCARD ? -1000 : (r < 0 ? r : -1);
    r = CARDOpen(chan, g_name, &g_fi);
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
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

/* per-slot verdict of one pass (; shown by the diagnostic variant as one digit per slot) */
enum { ONH_SV_NONE = 0, ONH_SV_VALID = 1, ONH_SV_NOFILE = 2, ONH_SV_DAMAGED = 3, ONH_SV_NEWER = 4, ONH_SV_UNUSABLE = 5, ONH_SV_ERROR = 6 };

/* an SDK result that makes the SLOT unusable, not the read wrong: no card, BROKEN (also an unformatted card), ENCODING, WRONGDEVICE (also a sector size != 0x2000) */
static int slot_unusable(s32 r) { return r == -1000 || r == R_NOCARD || r == -6 || r == -13 || r == -2; }

static void load_card(void)
{
    s32 res[2];
    int got[2], chan, found = -1, valid = -1, cards = 0, verdict[2], sv[2], unusable = 0, errors = 0, nofiles = 0;
    struct ch_hist tmp;
    u32 t0 = tbl();
    res[0] = res[1] = R_NOCARD;
    got[0] = got[1] = -1000;
    verdict[0] = verdict[1] = -1;
    sv[0] = sv[1] = ONH_SV_NONE;
    omc_st.load_tries++;
    if (!omc_melee_idle()) {                       /* Melee is using the card: try at the next menu command */
        omc_st.load_status = OMC_L_BUSY;
        return;
    }
    if (!WA_GET(1)) {                              /* no CARD work area (other user, or no heap room): not an error, not a try */
        omc_st.load_tries--;
        omc_st.load_status = OMC_L_BUSY;
        return;
    }
    for (chan = 0; chan < 2 && valid < 0 && !g_newer; chan++) {
        got[chan] = read_chan(chan, &res[chan]);
        if (got[chan] != -1000) cards++;
        if (got[chan] == 1) {
            verdict[chan] = ch_check(buf() + CH_PAYLOAD_OFF);
            sv[chan] = ONH_SV_DAMAGED;
            if (found < 0) found = chan;
            if (verdict[chan] == CH_OK) {
                valid = chan;
                found = chan;
                sv[chan] = ONH_SV_VALID;
                ch_decode(&tmp, buf() + CH_PAYLOAD_OFF);
            } else if (verdict[chan] == CH_NEWER) {
                g_newer = 1;
                found = chan;
                sv[chan] = ONH_SV_NEWER;
            }
        } else if (got[chan] == 0) {
            sv[chan] = ONH_SV_NOFILE;
            nofiles++;
        } else if (slot_unusable(got[chan])) {
            sv[chan] = ONH_SV_UNUSABLE;
            unusable++;
        } else {
            sv[chan] = ONH_SV_ERROR;
            errors++;
        }
    }
    WA_PUT();                                      /* everything below reads tmp / g_h, never buf */
    omc_st.load_res = ((u32)res[0] << 16) | ((u32)res[1] & 0xFFFFu);
    omc_st.ticks = tbl() - t0;
    onc_st.hist_sv = (u32)sv[0] | ((u32)sv[1] << 4);
    onc_st.hist_unusable = (u32)unusable;
    if (valid >= 0) {
        omc_st.load_status = OMC_L_OK;
        g_slot = valid + 1;
        if (g_h.n == 0) {
            g_h = tmp;
        } else {                                   /* codes confirmed before the card could be read stay first */
            (void)ch_merge(&g_h, &tmp);
            if (tmp.seq > g_h.seq) g_h.seq = tmp.seq;
            g_dirty = 1;
        }
        g_done = 1;
    } else if (found >= 0) {
        omc_st.load_status = g_newer ? OMC_L_NEWER : OMC_L_DAMAGED;
        g_slot = found + 1;
        g_done = 1;
    } else if (!cards) {
        omc_st.load_status = OMC_L_NOCARD;         /* a card may be inserted later: asked again at the next menu load */
    } else if (errors == 0 && nofiles > 0) {
        omc_st.load_status = OMC_L_NOFILE;         /* a definite answer from a good card; a broken / unformatted / wrong other slot was skipped */
        g_done = 1;
    } else {                                       /* a hard error on a slot, or every card present is unusable: no definite answer */
        omc_st.load_status = OMC_L_ERROR;
        if (omc_st.load_tries >= ERROR_TRIES) g_done = 1;
    }
    omc_st.load_slot = (u32)g_slot;
#ifdef MS_CARD_REPORT                              /* diagnostics for a debugger / Dolphin log only (OSReport prints nowhere on a console): compiled out of the release engine, which sits 1 KB under its card hole */
    OSReport("SDCH codes: read status=%d slot=%d card=%d/%d n=%d sv=%d%d\n", (int)omc_st.load_status, g_slot, (int)res[0], (int)res[1], (int)g_h.n, sv[0], sv[1]);
#else
    (void)res; (void)sv;
#endif
}

/* the file does not exist on chan (mounted): may we create it?  On a slot without Melee's save, keep room for that save (either slot: with A broken Melee uses B). */
static int room_ok(s32 chan, s32 *res)
{
    s32 bytes = 0, files = 0, r;
    r = CARDOpen(chan, g_melee_save, &g_fi);
    if (r == R_READY) { CARDClose(&g_fi); return 1; }
    if (r != R_NOFILE) { *res = r; return 0; }
    r = CARDFreeBlocks(chan, &bytes, &files);
    *res = r;
    return r == R_READY && bytes >= 12 * 0x2000 && files >= 2;
}

/* verify: read the file back and compare the record with what was written */
static int verify(s32 chan, const u8 *rec, s32 *res)
{
    u32 i;
    s32 r = CARDOpen(chan, g_name, &g_fi);
    int ok = 0;
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
        if (r == R_READY) {
            ok = 1;
            for (i = 0; i < CH_REC_SIZE; i++) if (buf()[CH_PAYLOAD_OFF + i] != rec[i]) ok = 0;
        }
        CARDClose(&g_fi);
    }
    *res = r;
    return ok;
}

#define ONH_W_SKIP 100              /* internal: this slot does not mount (BROKEN / ENCODING / WRONGDEVICE): try the other one, never format */

/* the slot that holds Melee's own save (A first), -1 if none: one mount cycle per slot, read only */
static int melee_save_slot(void)
{
    int chan;
    for (chan = 0; chan < 2; chan++) {
        int has = 0;
        if (card_mount(chan) == 0) {
            if (CARDOpen(chan, g_melee_save, &g_fi) == R_READY) { CARDClose(&g_fi); has = 1; }
            CARDUnmount(chan);
        }
        if (has) return chan;
    }
    return -1;
}

/* one write attempt on chan; returns OMC_W_* (or ONH_W_SKIP) */
static int write_chan(s32 chan)
{
    struct card_stat st;
    u8 rec[CH_REC_SIZE];
    u32 i;
    s32 r = card_mount(chan);
    int out = OMC_W_ERROR;
    omc_st.write_res = (u32)r;
    if (r != 0) return r == R_NOCARD ? OMC_W_NOCARD : (slot_unusable(r) ? ONH_W_SKIP : OMC_W_ERROR);   /* never format, never write a slot that does not mount */
    omc_st.write_slot = (u32)(chan + 1);
    g_h.seq++;
    ch_encode(rec, &g_h);
    r = CARDOpen(chan, g_name, &g_fi);
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
        if (r == R_READY) {
            if (ch_check(buf() + CH_PAYLOAD_OFF) == CH_NEWER) {
                out = OMC_W_NEWER;
                g_newer = 1;
            } else {
                for (i = 0; i < CH_REC_SIZE; i++) buf()[CH_PAYLOAD_OFF + i] = rec[i];
                r = CARDWrite(&g_fi, buf(), CH_BLOCK, 0);
                out = r == R_READY ? OMC_W_OK : OMC_W_ERROR;
            }
        }
        CARDClose(&g_fi);
    } else if (r == R_NOFILE) {
        if (!room_ok(chan, &r)) {
            out = r == R_READY ? OMC_W_RESERVE : OMC_W_ERROR;
        } else {
            r = CARDCreateAsync(chan, g_name, CH_BLOCK, &g_fi, CARDSyncCallback);
            if (r >= 0) r = CARDSync(chan);
            if (r == R_INSSPACE || r == R_NOENT) {
                out = OMC_W_FULL;
            } else if (r == R_READY) {
                ch_new_block(buf(), OMC_LINE1, OMC_LINE2, &g_h);
                r = CARDWrite(&g_fi, buf(), CH_BLOCK, 0);
                if (r == R_READY) r = CARDGetStatus(chan, g_fi.fileNo, &st);
                if (r == R_READY) {
                    st.commentAddr = 0;            /* the two comment lines at the start of the block */
                    st.iconAddr = 0xFFFFFFFFu;     /* no icon, no banner */
                    st.iconFormat = 0;
                    st.iconSpeed = 0;
                    st.bannerFormat = 0;
                    r = CARDSetStatusAsync(chan, g_fi.fileNo, &st, CARDSyncCallback);
                    if (r >= 0) r = CARDSync(chan);
                }
                out = r == R_READY ? OMC_W_OK : OMC_W_ERROR;
                CARDClose(&g_fi);
            }
        }
    }
    if (out == OMC_W_OK && !verify(chan, rec, &r)) out = OMC_W_VERIFY;
    omc_st.write_res = (u32)r;
    CARDUnmount(chan);
    return out;
}

static void save_card(void)
{
    u32 t0 = tbl();
    int out = OMC_W_NOCARD;
    if (g_newer) { omc_st.write_status = OMC_W_NEWER; return; }
    if (!g_done) { omc_st.write_status = OMC_W_NOLOAD; return; }   /* never write before the card file was looked at */
    if (!omc_melee_idle()) { omc_st.write_status = OMC_W_BUSY; return; }
    if (!WA_GET(1)) { omc_st.write_status = OMC_W_BUSY; return; }   /* no CARD work area: try at the next menu command (not a try) */
    omc_st.write_tries++;
    omc_st.write_slot = 0;
    if (g_slot) {                                  /* the slot that already holds the file */
        out = write_chan(g_slot - 1);
        if (out == ONH_W_SKIP) out = OMC_W_ERROR;
    } else {                                       /* no file anywhere: the slot with Melee's save, else A, else B; a slot that does not mount is skipped */
        u32 res = (u32)R_NOCARD, slot = 0;
        int first = melee_save_slot(), k, skipped = 0;
        if (first < 0) first = 0;
        for (k = 0; k < 2; k++) {
            int chan = k == 0 ? first : 1 - first;
            int r = write_chan(chan);
            if (r == ONH_W_SKIP) { skipped = 1; res = omc_st.write_res; continue; }
            if (r != OMC_W_NOCARD) { out = r; res = omc_st.write_res; slot = omc_st.write_slot; }
            if (r == OMC_W_OK || r == OMC_W_NEWER) break;
        }
        if (out == OMC_W_NOCARD && skipped) out = OMC_W_ERROR;
        omc_st.write_res = res;
        omc_st.write_slot = slot;
    }
    WA_PUT();
    if (out == OMC_W_OK || out == OMC_W_VERIFY) {
        omc_st.writes++;
        g_slot = (int)omc_st.write_slot;
        if (out == OMC_W_OK) g_dirty = 0;
    }
    omc_st.write_status = (u32)out;
    omc_st.ticks = tbl() - t0;
#ifdef MS_CARD_REPORT                              /* diagnostics for a debugger / Dolphin log only (OSReport prints nowhere on a console): compiled out of the release engine, which sits 1 KB under its card hole */
    OSReport("SDCH codes: write status=%d slot=%d card=%d n=%d\n", out, (int)omc_st.write_slot, (int)omc_st.write_res, (int)g_h.n);
#else
    (void)out;
#endif
}

static void load_if_needed(void)
{
    if (!g_done) load_card();
}

void omc_on_status(void)
{
    load_if_needed();
    if (g_dirty && g_done && omc_st.write_tries < 64) save_card();
    sync_status();
}

void omc_on_code(const char *code)
{
    int r;
    omc_st.adds++;
    r = ch_add_ns(&g_h, code, '@');         /* Mano a mano keeps '@' codes only; '#' codes (Slippi lists) are never added here and never removed */
    if (r > 0) { omc_st.add_changes++; g_dirty = 1; }
    load_if_needed();
    if (g_dirty) save_card();
    sync_status();
}

/* SLIPPI ROUTE ('#' namespace, owner bug the bridge kept its Direct code list in RAM and never wrote MultishineCodes).  One call from the bridge glue (br_tick, menus only,
   interrupts on, no EXI lock): `add` = a code Slippi's rule just recorded (the B4 of a Direct search; 0 = none) goes into the '#' namespace (10 newest, newest first, Slippi's dedupe),
   the card file is read if that has not happened yet and written when the list changed (a pending save is retried by every call, 64 tries); '@' entries are never touched.
   Returns the number of '#' codes copied to out[] (newest first, at most max), or -1 while the card has not given its definite answer yet (ask again at the next menu poll). */
int omc_slippi_sync(const char *add, char out[][CH_LEN + 1], int max)
{
    struct ch_hist v;
    int i, k;
    if (add && add[0] && ch_add_ns(&g_h, add, '#') > 0) { omc_st.add_changes++; g_dirty = 1; }
    load_if_needed();
    if (g_dirty && g_done && omc_st.write_tries < 64) save_card();
    sync_status();
    if (!g_done) return -1;
    ch_filter(&v, &g_h, '#');
    for (i = 0; i < (int)v.n && i < max; i++) for (k = 0; k <= CH_LEN; k++) out[i][k] = v.code[i][k];
    return i;
}

int omc_answer(const unsigned char *req, unsigned char *rsp)
{
    int r;
    omc_st.be++;
    { struct ch_hist v; ch_filter(&v, &g_h, '@'); r = ch_answer(&v, req, rsp); }   /* suggestions from the '@' entries only */
    if (r) omc_st.be_hits++;
    return r;
}
