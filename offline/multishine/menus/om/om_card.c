/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* om_card: see om_card.h. Original work; the SDK call sequence is the one proved in Dolphin (retail entry points,
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
#define OMC_LINE1 "Multishine friend codes"
#endif
#ifndef OMC_LINE2
#define OMC_LINE2 "Direct play history"
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
static u32 tbl(void) { return 0; }
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
static int omc_melee_idle(void)
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

#define WA_SIZE 0xA000
#define PROBE_TRIES 120             /* retraces (2 s) while CARDProbeEx says BUSY */
#define ERROR_TRIES 3               /* card errors retried at later menu loads */

struct omc_status omc_st OM_DATA = { 0x4F4D4348u, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static u8 g_mem[WA_SIZE + CH_BLOCK] __attribute__((aligned(32))) OM_DATA;
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

static void load_card(void)
{
    s32 res[2];
    int got[2], chan, found = -1, valid = -1, cards = 0, verdict[2];
    struct ch_hist tmp;
    u32 t0 = tbl();
    res[0] = res[1] = R_NOCARD;
    got[0] = got[1] = -1000;
    verdict[0] = verdict[1] = -1;
    omc_st.load_tries++;
    if (!omc_melee_idle()) {                       /* Melee is using the card: try at the next menu command */
        omc_st.load_status = OMC_L_BUSY;
        return;
    }
    for (chan = 0; chan < 2 && valid < 0 && !g_newer; chan++) {
        got[chan] = read_chan(chan, &res[chan]);
        if (got[chan] != -1000) cards++;
        if (got[chan] == 1) {
            verdict[chan] = ch_check(buf() + CH_PAYLOAD_OFF);
            if (found < 0) found = chan;
            if (verdict[chan] == CH_OK) {
                valid = chan;
                found = chan;
                ch_decode(&tmp, buf() + CH_PAYLOAD_OFF);
            } else if (verdict[chan] == CH_NEWER) {
                g_newer = 1;
                found = chan;
            }
        }
    }
    omc_st.load_res = ((u32)res[0] << 16) | ((u32)res[1] & 0xFFFFu);
    omc_st.ticks = tbl() - t0;
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
    } else if ((got[0] < 0 && got[0] != -1000) || (got[1] < 0 && got[1] != -1000)) {
        omc_st.load_status = OMC_L_ERROR;
        if (omc_st.load_tries >= ERROR_TRIES) g_done = 1;
    } else {
        omc_st.load_status = OMC_L_NOFILE;
        g_done = 1;
    }
    omc_st.load_slot = (u32)g_slot;
    OSReport("SDCH codes: read status=%d slot=%d card=%d/%d n=%d\n", (int)omc_st.load_status, g_slot, (int)res[0], (int)res[1], (int)g_h.n);
}

/* the file does not exist on chan (mounted): may we create it?  On slot A without Melee's save, keep room for that save. */
static int room_ok(s32 chan, s32 *res)
{
    s32 bytes = 0, files = 0, r;
    if (chan != 0) return 1;
    r = CARDOpen(chan, g_melee_save, &g_fi);
    if (r == R_READY) { CARDClose(&g_fi); return 1; }
    if (r != R_NOFILE) { *res = r; return 0; }
    r = CARDFreeBlocks(chan, &bytes, &files);
    *res = r;
    return r == R_READY && bytes >= 12 * 0x2000 && files >= 2;
}

/* ICON STAMP: a file the console creates itself gets the same CI8 icon as the PC made files.  The icon (32x32 pixels + 256 entry
   palette, 0x600 bytes) sits at 0x40 of block 0 of MultishineCode, which every working card has (system-files builder / updater / PC tool).  Called with the new file's block b
   filled and the channel mounted, before the CARDWrite: the icon is read into the unused 0x800..0xFFF of b, copied to 0x40 and the scratch cleared again (the
   payload at 0x1000 is untouched).  Returns 1 when stamped; the caller then gives the entry iconAddr 0x40, CI8, speed 1 (MS_ICON_STATUS), else it keeps no icon. */
#define MS_ICON_OFF 0x40u
#define MS_ICON_LEN 0x600u
#define MS_ICON_SCR 0x800u
#define MS_ICON_STATUS(st, ok) do { (st).commentAddr = 0; (st).iconAddr = (ok) ? MS_ICON_OFF : 0xFFFFFFFFu; (st).iconFormat = (ok) ? 1 : 0; (st).iconSpeed = (ok) ? 1 : 0; (st).bannerFormat = 0; } while (0)
static int ms_icon_stamp(s32 chan, u8 *b)
{
    struct card_fi fi;
    u32 i, any = 0;
    int ok = 0;
    if (CARDOpen(chan, "MultishineCode", &fi) != R_READY) return 0;
    if (CARDRead(&fi, b + MS_ICON_SCR, 0x800, 0) == R_READY) {
        for (i = 0; i < MS_ICON_LEN; i++) { b[MS_ICON_OFF + i] = b[MS_ICON_SCR + MS_ICON_OFF + i]; any |= b[MS_ICON_OFF + i]; }
        ok = any != 0;
        if (!ok) for (i = 0; i < MS_ICON_LEN; i++) b[MS_ICON_OFF + i] = 0;
    }
    for (i = 0; i < 0x800; i++) b[MS_ICON_SCR + i] = 0;
    CARDClose(&fi);
    return ok;
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

/* one write attempt on chan; returns OMC_W_* */
static int write_chan(s32 chan)
{
    struct card_stat st;
    u8 rec[CH_REC_SIZE];
    u32 i;
    int icon = 0;
    s32 r = card_mount(chan);
    int out = OMC_W_ERROR;
    omc_st.write_res = (u32)r;
    if (r != 0) return r == R_NOCARD ? OMC_W_NOCARD : OMC_W_ERROR;
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
                icon = ms_icon_stamp(chan, buf());
                r = CARDWrite(&g_fi, buf(), CH_BLOCK, 0);
                if (r == R_READY) r = CARDGetStatus(chan, g_fi.fileNo, &st);
                if (r == R_READY) {
                    MS_ICON_STATUS(st, icon);      /* comment lines at 0, the stamped icon at 0x40 (no banner) */
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
    int out = OMC_W_NOCARD, chan;
    if (g_newer) { omc_st.write_status = OMC_W_NEWER; return; }
    if (!g_done) { omc_st.write_status = OMC_W_NOLOAD; return; }   /* never write before the card file was looked at */
    if (!omc_melee_idle()) { omc_st.write_status = OMC_W_BUSY; return; }
    omc_st.write_tries++;
    omc_st.write_slot = 0;
    if (g_slot) {
        out = write_chan(g_slot - 1);
    } else {
        u32 res = (u32)R_NOCARD, slot = 0;
        for (chan = 0; chan < 2; chan++) {
            int r = write_chan(chan);
            if (r != OMC_W_NOCARD) { out = r; res = omc_st.write_res; slot = omc_st.write_slot; }
            if (r == OMC_W_OK || r == OMC_W_NEWER) break;
        }
        omc_st.write_res = res;
        omc_st.write_slot = slot;
    }
    if (out == OMC_W_OK || out == OMC_W_VERIFY) {
        omc_st.writes++;
        g_slot = (int)omc_st.write_slot;
        if (out == OMC_W_OK) g_dirty = 0;
    }
    omc_st.write_status = (u32)out;
    omc_st.ticks = tbl() - t0;
    OSReport("SDCH codes: write status=%d slot=%d card=%d n=%d\n", out, (int)omc_st.write_slot, (int)omc_st.write_res, (int)g_h.n);
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
    r = ch_add(&g_h, code);
    if (r > 0) { omc_st.add_changes++; g_dirty = 1; }
    load_if_needed();
    if (g_dirty) save_card();
    sync_status();
}

int omc_answer(const unsigned char *req, unsigned char *rsp)
{
    int r;
    omc_st.be++;
    r = ch_answer(&g_h, req, rsp);
    if (r) omc_st.be_hits++;
    return r;
}
