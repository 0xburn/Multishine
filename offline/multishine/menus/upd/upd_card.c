/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_card: see upd_card.h. The SDK call sequence for probe / mount / create / status is the one om_card.c proved in Dolphin (retail entry points, symbols);
 * CARDDeleteAsync and CARDRenameAsync are new.  Soft-float, no heap, no libc. */
#pragma GCC diagnostic ignored "-Wunused-function"      /* ota_core.h pulls in the loader's static signature check, which this file does not call */
#include "upd_card.h"

typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;

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
#define R_EXIST (-7)
#define R_NOENT (-8)
#define R_INSSPACE (-9)

#ifdef UPC_HOST                     /* host tests: a model of the SDK behind the same entry points (tests/upc_fake.c) */
#include "upc_host.h"
#define CARDProbeEx h_CARDProbeEx
#define CARDMountAsync h_CARDMountAsync
#define CARDUnmount h_CARDUnmount
#define CARDOpen h_CARDOpen
#define CARDClose h_CARDClose
#define CARDRead h_CARDRead
#define CARDWrite h_CARDWrite
#define CARDCreateAsync h_CARDCreateAsync
#define CARDDeleteAsync h_CARDDeleteAsync
#define CARDRenameAsync h_CARDRenameAsync
#define CARDFreeBlocks h_CARDFreeBlocks
#define CARDGetStatus h_CARDGetStatus
#define CARDSetStatusAsync h_CARDSetStatusAsync
#define CARDSync h_CARDSync
#define CARDSyncCallback ((void *)0)
#define VIWaitForRetrace h_VIWaitForRetrace
#define upc_melee_idle h_melee_idle
#define upc_melee_quiet h_melee_quiet
#define UPC_WA_ACQUIRE() h_wa_acquire()
#define UPC_WA_RELEASE() h_wa_release()
#else
typedef s32 (*fn_probe)(s32 chan, s32 *mem, s32 *sector);
typedef s32 (*fn_mount)(s32 chan, void *wa, void *detach_cb, void *attach_cb);
typedef s32 (*fn_chan)(s32 chan);
typedef s32 (*fn_open)(s32 chan, const char *name, struct card_fi *fi);
typedef s32 (*fn_close)(struct card_fi *fi);
typedef s32 (*fn_rw)(struct card_fi *fi, void *buf, s32 len, s32 off);
typedef s32 (*fn_create)(s32 chan, const char *name, u32 size, struct card_fi *fi, void *cb);
typedef s32 (*fn_delete)(s32 chan, const char *name, void *cb);
typedef s32 (*fn_rename)(s32 chan, const char *from, const char *to, void *cb);
typedef s32 (*fn_free)(s32 chan, s32 *bytes, s32 *files);
typedef s32 (*fn_getstat)(s32 chan, s32 fileNo, struct card_stat *st);
typedef s32 (*fn_setstat)(s32 chan, s32 fileNo, struct card_stat *st, void *cb);
typedef void (*fn_void)(void);
#define CARDProbeEx ((fn_probe)0x80355F94)
#define CARDMountAsync ((fn_mount)0x80356688)
#define CARDUnmount ((fn_chan)0x803568C4)
#define CARDOpen ((fn_open)0x80357534)
#define CARDClose ((fn_close)0x803576AC)
#define CARDRead ((fn_rw)0x80357E88)
#define CARDWrite ((fn_rw)0x80358204)
#define CARDCreateAsync ((fn_create)0x80357838)
#define CARDDeleteAsync ((fn_delete)0x803582F0)          /* symbols.txt; never called by an image of this project before */
#define CARDRenameAsync ((fn_rename)0x80358898)          /* same */
#define CARDFreeBlocks ((fn_free)0x803532D4)
#define CARDGetStatus ((fn_getstat)0x803585F8)
#define CARDSetStatusAsync ((fn_setstat)0x80358724)
#define CARDSync ((fn_chan)0x80353424)                   /* __CARDSync */
#define CARDSyncCallback ((void *)0x80352274)            /* __CARDSyncCallback */
#define VIWaitForRetrace ((fn_void)0x8034F314)

/* Melee's card layer (retail offsets, the same checks as om_card.c omc_melee_idle) */
#define LBCN_STATE 0x80432A68u
#define LBCG_STATE 0x80433318u
#define CARD_BLOCK 0x804A8B38u
static int upc_melee_idle(void)
{
    int i;
    if (*(volatile s32 *)(LBCN_STATE + 0x80) || *(volatile s32 *)(LBCN_STATE + 0x8AC)) return 0;
    for (i = 0; i < 11; i++)
        if (*(volatile s32 *)(LBCN_STATE + 0x510 + 0x54 * i) != 14) return 0;
    if (*(volatile s32 *)(LBCG_STATE + 0xC) || *(volatile s32 *)(LBCG_STATE + 0x10)) return 0;
    if (*(volatile s32 *)CARD_BLOCK || *(volatile s32 *)(CARD_BLOCK + 0x110)) return 0;
    return 1;
}
/* The same checks for a moment when OUR mount is held (upc_art_repair, right before its RAM write): __CARDBlock[g_chan].attached is ours then, so only the OTHER slot's block counts.
   upc_melee_idle here is always false on a real console. */
static int g_chan;
static int upc_melee_quiet(void)
{
    int i;
    if (*(volatile s32 *)(LBCN_STATE + 0x80) || *(volatile s32 *)(LBCN_STATE + 0x8AC)) return 0;
    for (i = 0; i < 11; i++)
        if (*(volatile s32 *)(LBCN_STATE + 0x510 + 0x54 * i) != 14) return 0;
    if (*(volatile s32 *)(LBCG_STATE + 0xC) || *(volatile s32 *)(LBCG_STATE + 0x10)) return 0;
    if (*(volatile s32 *)(CARD_BLOCK + (g_chan ? 0u : 0x110u))) return 0;
    return 1;
}
/* the shared 48 KiB CARD work area of the resident (on_hist.c ms_card_wa_acquire; 0 = refused: busy / not available).  Reached through two pointers the host hands over
   (upc_set_wa), because this file is linked into the on-demand overlay, which has no import relocations. */
static u8 *(*g_wa_acq)(u32 owner);
static void (*g_wa_rel)(void);
void upc_set_wa(u8 *(*acquire)(u32 owner), void (*release)(void)) { g_wa_acq = acquire; g_wa_rel = release; }
#define UPC_WA_ACQUIRE() (g_wa_acq ? g_wa_acq(UPC_OWNER) : (u8 *)0)
#define UPC_WA_RELEASE() do { if (g_wa_rel) g_wa_rel(); } while (0)
#endif

#define PROBE_TRIES 120             /* retraces (2 s) while CARDProbeEx says BUSY */
#define ICON_OFFSET 0x40u           /* the MultishineCode file: comment at 0, one CI8 icon at 0x40, format 1 (offline/provision make_dir_entry) */

struct upc_status upc_st = { 0x55504344u, 0, 0, 0, 0, 0, 0, 0, 0x55504152u, 0, 0, 0, 0, 0, 0 };
static int g_mounted, g_have_wa;
#ifdef UPC_HOST
static int g_chan;
#endif
static u8 *g_wa;
static const char g_melee_save[] = "SuperSmashBros0110290334";   /* :121 */
static struct card_fi g_fi;

void upc_set_chan(int chan)
{
    g_chan = chan == 1 ? 1 : 0;
    upc_st.chan = (u32)g_chan;
}

static int fail(u32 op, s32 res)
{
    upc_st.fails++;
    upc_st.last_op = op;
    upc_st.last_res = (u32)res;
    return 3;
}

/* probe: 0 = a card with 8 KiB sectors is there.  The SDK result otherwise. */
static s32 g_mem_mbit;
static s32 probe(void)
{
    s32 mem = 0, sector = 0, r;
    int tries = PROBE_TRIES;
    while ((r = CARDProbeEx(g_chan, &mem, &sector)) == R_BUSY && --tries > 0) VIWaitForRetrace();
    if (r != R_READY) return r;
    g_mem_mbit = mem;
    return sector == 0x2000 ? 0 : -2;               /* CARD_RESULT_WRONGDEVICE: Melee's own rule */
}

int upc_ready(void)
{
    if (!upc_melee_idle()) { upc_st.refused++; return 0; }
    if (g_mounted) return 1;
    if (probe() != 0) { upc_st.refused++; return 0; }
    return 1;
}

static s32 mount(void)
{
    s32 r;
    u8 *wa;
    if (g_mounted) return 0;
    if (!upc_melee_idle()) { upc_st.refused++; return R_BUSY; }
    wa = UPC_WA_ACQUIRE();
    if (!wa) { upc_st.refused++; return R_BUSY; }
    g_have_wa = 1; g_wa = wa;
    r = probe();
    if (r == 0) {
        r = CARDMountAsync(g_chan, wa, 0, CARDSyncCallback);
        if (r >= 0) r = CARDSync(g_chan);
        if (r != R_READY) CARDUnmount(g_chan);      /* BROKEN / ENCODING leave it mounted */
    }
    if (r != R_READY) {
        UPC_WA_RELEASE(); g_have_wa = 0;
        return r;
    }
    g_mounted = 1;
    upc_st.mounts++;
    return 0;
}

void upc_done(void)
{
    if (g_mounted) { CARDUnmount(g_chan); g_mounted = 0; }
    if (g_have_wa) { UPC_WA_RELEASE(); g_have_wa = 0; g_wa = 0; }
}

unsigned char *upc_block(void) { return g_mounted && g_wa ? g_wa + 0xA000u : 0; }

static int enter(u32 op)
{
    s32 r = mount();
    upc_st.ops++;
    if (r != 0) { fail(op, r); return 1; }
    return 0;
}

static int sync_wait(s32 r)
{
    if (r >= 0) r = CARDSync(g_chan);
    return r;
}
#ifndef UPC_BOOT_ONLY
static int sync_wait_ch(s32 chan, s32 r)
{
    if (r >= 0) r = CARDSync(chan);
    return r;
}
#endif

/* earlier image: usable blocks of the card in the slot (Mbit * 16 - 5 system blocks: 59, 123, 251, 507, 1019, 2043), 0 when unknown / no card; free blocks as the SDK counts them (no reserve) */
unsigned upc_total_blocks(void)
{
    if (!g_mem_mbit && probe() != 0 && !g_mem_mbit) return 0;
    return g_mem_mbit > 0 ? (unsigned)g_mem_mbit * 16u - 5u : 0u;
}
int upc_free_raw(void)
{
    s32 bytes = 0, files = 0, r;
    if (enter(UPC_OP_EXISTS)) return -1;
    r = CARDFreeBlocks(g_chan, &bytes, &files);
    if (r != R_READY) { fail(UPC_OP_EXISTS, r); return -1; }
    return bytes / (s32)OTA_BLOCK;
}

/* create nblocks whole blocks, then give it the same card status as the loader's own file (comment at 0, icon at 0x40) */
static int op_create(void *ctx, const char *name, ota_u32 nblocks)
{
    struct card_stat st;
    s32 r;
    (void)ctx;
    if (enter(UPC_OP_CREATE)) return 3;
    r = sync_wait(CARDCreateAsync(g_chan, name, nblocks * OTA_BLOCK, &g_fi, CARDSyncCallback));
    if (r == R_INSSPACE || r == R_NOENT) return 1;
    if (r == R_EXIST) return 2;
    if (r != R_READY) return fail(UPC_OP_CREATE, r);
    r = CARDGetStatus(g_chan, g_fi.fileNo, &st);
    if (r == R_READY) {
        st.commentAddr = 0; st.iconAddr = ICON_OFFSET; st.iconFormat = 1; st.iconSpeed = 1; st.bannerFormat = 0;
        r = sync_wait(CARDSetStatusAsync(g_chan, g_fi.fileNo, &st, CARDSyncCallback));
    }
    CARDClose(&g_fi);
    return r == R_READY ? 0 : fail(UPC_OP_STATUS, r);        /* the caller (ota_core) deletes the half-made staging file */
}

static int op_rw(const char *name, ota_u32 off, void *buf, ota_u32 n, int write)
{
    s32 r;
    if (enter(write ? UPC_OP_WRITE : UPC_OP_READ)) return 3;
    r = CARDOpen(g_chan, name, &g_fi);
    if (r != R_READY) return fail(write ? UPC_OP_WRITE : UPC_OP_READ, r);
    r = write ? CARDWrite(&g_fi, buf, (s32)n, (s32)off) : CARDRead(&g_fi, buf, (s32)n, (s32)off);       /* the SDK flushes / invalidates the buffer itself */
    CARDClose(&g_fi);
    return r == R_READY ? 0 : fail(write ? UPC_OP_WRITE : UPC_OP_READ, r);
}
static int op_write(void *ctx, const char *name, ota_u32 off, const mw2_u8 *buf, ota_u32 n) { (void)ctx; return op_rw(name, off, (void *)buf, n, 1); }
static int op_read(void *ctx, const char *name, ota_u32 off, mw2_u8 *buf, ota_u32 n) { (void)ctx; return op_rw(name, off, buf, n, 0); }

#ifndef UPC_BOOT_ONLY
static int op_delete(void *ctx, const char *name)
{
    s32 r;
    (void)ctx;
    if (enter(UPC_OP_DELETE)) return 3;
    r = sync_wait(CARDDeleteAsync(g_chan, name, CARDSyncCallback));
    if (r == R_NOFILE) return 0;                    /* gone already: what the caller wants */
    return r == R_READY ? 0 : fail(UPC_OP_DELETE, r);
}

static int op_rename(void *ctx, const char *from, const char *to)
{
    s32 r;
    (void)ctx;
    if (enter(UPC_OP_RENAME)) return 3;
    r = sync_wait(CARDRenameAsync(g_chan, from, to, CARDSyncCallback));
    return r == R_READY ? 0 : fail(UPC_OP_RENAME, r);
}

#endif

static int op_exists(void *ctx, const char *name)
{
    s32 r;
    (void)ctx;
    if (enter(UPC_OP_EXISTS)) return 0;
    r = CARDOpen(g_chan, name, &g_fi);
    if (r == R_READY) { CARDClose(&g_fi); return 1; }
    if (r != R_NOFILE) fail(UPC_OP_EXISTS, r);
    return 0;
}

#ifndef UPC_BOOT_ONLY                    /* the resident engine's boot-record build (UPD_BOOT_ENGINE, upd_host.c) needs neither of these */
int upc_room_ok(unsigned nblocks)
{
    s32 bytes = 0, files = 0, r;
    if (enter(UPC_OP_EXISTS)) return 0;
    if (g_chan != 0) return 1;
    r = CARDOpen(g_chan, g_melee_save, &g_fi);
    if (r == R_READY) { CARDClose(&g_fi); return 1; }
    if (r != R_NOFILE) { fail(UPC_OP_EXISTS, r); return 0; }
    r = CARDFreeBlocks(g_chan, &bytes, &files);
    if (r != R_READY) { fail(UPC_OP_EXISTS, r); return 0; }
    return (u32)bytes >= (12u + nblocks) * OTA_BLOCK && files >= 3;
}

#endif

/* blocks of an existing file of ours (0 when it is not there) */
static unsigned file_blocks(const char *name)
{
    struct card_stat st;
    unsigned n = 0;
    if (!name || CARDOpen(g_chan, name, &g_fi) != R_READY) return 0;
    if (CARDGetStatus(g_chan, g_fi.fileNo, &st) == R_READY) n = st.length / OTA_BLOCK;
    CARDClose(&g_fi);
    return n;
}

/* ota_plan's question: the blocks free once `previous` and `staging` (and `active` when given) are gone, minus the 12 blocks Melee's own save still needs on
   slot A when that save does not exist yet (the rule of upc_room_ok).  -1 = cannot tell. */
static int op_free_blocks(void *ctx, const char *previous, const char *staging, const char *active)
{
    s32 bytes = 0, files = 0, r;
    unsigned free_b;
    (void)ctx;
    if (enter(UPC_OP_EXISTS)) return -1;
    r = CARDFreeBlocks(g_chan, &bytes, &files);
    if (r != R_READY) { fail(UPC_OP_EXISTS, r); return -1; }
    free_b = (u32)bytes / OTA_BLOCK + file_blocks(previous) + file_blocks(staging) + file_blocks(active);
    if (g_chan == 0) {
        r = CARDOpen(g_chan, g_melee_save, &g_fi);
        if (r == R_READY) CARDClose(&g_fi);
        else if (r == R_NOFILE) free_b = free_b > 12u ? free_b - 12u : 0u;
        else { fail(UPC_OP_EXISTS, r); return -1; }
    }
    return (int)free_b;
}

#ifndef UPC_BOOT_ONLY
#include "upd_text.h"
/* CARD FILE REPAIR: (1) the files the resident engine creates itself (MultishineOptions, MultishineCodes, the guest MultishineOnline, MultishineDiag) are
   made without an icon, because the engine image has no bytes to spare for it; (2) older builds wrote other comment lines (the two 32-byte lines the card manager shows) than the
   readable set of upd_text.h (generated from offline/card_text.py).  This pass (overlay only, once per boot from updc_load, one mount) fixes both in ONE write of block 0 of the file:
   it reads the first 512 bytes, and only when the comment lines are EXACTLY a known older pair (a DEV-SIGNED watermark, a foreign text, a Melee save never match) or an icon is
   missing, it reads block 0, puts in the new lines and/or the CI8 icon of block 0 of MultishineCode (0x40..0x63F, taken through the unused 0x800..0xFFF of the same buffer), writes
   block 0 back (payload from 0x1000 untouched, no other block touched) and sets the card status icon fields when an icon was added.  A file with an icon, data in the icon area, or a
   missing Code file only misses the icon part.  MultishineCode is rewritten only while MultishineCodeB exists (the loader's fallback if a torn write ever damaged it); the
   MultishineOvl container is never rewritten in place (its text arrives with the next overlay OTA).  Returns the number of files changed, -1 when the card could not be used. */
static int upt_field(const u8 *f, const char *s)
{
    u32 i = 0;
    for (; s[i]; i++) if (i >= 32 || f[i] != (u8)s[i]) return 0;
    for (; i < 32; i++) if (f[i]) return 0;
    return 1;
}
static int upt_old(const struct upt_row *r, const u8 *b)
{
    u32 k;
    for (k = 0; k < r->nold; k++) if (upt_field(b, r->old[k][0]) && upt_field(b + 32, r->old[k][1])) return 1;
    return 0;
}
static void upt_put(u8 *b, const char *l1, const char *l2)
{
    u32 i;
    for (i = 0; i < 64; i++) b[i] = 0;
    for (i = 0; i < 31 && l1[i]; i++) b[i] = (u8)l1[i];
    for (i = 0; i < 31 && l2[i]; i++) b[32 + i] = (u8)l2[i];
}

int upc_icon_repair(void)
{
    struct card_stat st;
    u8 *b;
    u32 i, k, any, want_text, want_icon;
    int fixed = 0;
    s32 r;
    if (enter(UPC_OP_STATUS)) return -1;
    b = upc_block();
    if (!b) return -1;
    for (k = 0; k < UPT_N; k++) {
        const struct upt_row *row = &upt_rows[k];
        const char *nm = row->name;
        if (CARDOpen(g_chan, nm, &g_fi) != R_READY) continue;
        if (CARDGetStatus(g_chan, g_fi.fileNo, &st) != R_READY || CARDRead(&g_fi, b, 0x200, 0) != R_READY) { CARDClose(&g_fi); continue; }
        want_text = upt_old(row, b);
        want_icon = (row->flags & UPT_ICON) && ((st.iconFormat == 0 && st.iconAddr == 0xFFFFFFFFu) || st.iconAddr == ICON_OFFSET);   /* no icon at all, OR an icon declared at 0x40 whose bytes are all zero: the all-zero test below keeps a real icon untouched */
        if (!want_text && !want_icon) { CARDClose(&g_fi); continue; }
        if (CARDRead(&g_fi, b, (s32)OTA_BLOCK, 0) != R_READY) { CARDClose(&g_fi); continue; }
        CARDClose(&g_fi);
        if (want_icon) { for (any = 0, i = 0x40; i < 0x640; i++) any |= b[i]; if (any) want_icon = 0; }   /* something lives where the icon goes: not ours to touch */
        if ((row->flags & UPT_NEED_B) && want_text) {            /* the active code file: only with its fallback in place */
            if (CARDOpen(g_chan, "MultishineCodeB", &g_fi) != R_READY) want_text = 0; else CARDClose(&g_fi);
        }
        if (want_icon) {
            if (CARDOpen(g_chan, "MultishineCode", &g_fi) != R_READY) want_icon = 0;
            else {
                r = CARDRead(&g_fi, b + 0x800, 0x800, 0);
                CARDClose(&g_fi);
                if (r != R_READY) return fixed;
                for (any = 0, i = 0; i < 0x600; i++) { b[0x40 + i] = b[0x840 + i]; any |= b[0x40 + i]; }
                for (i = 0; i < 0x800; i++) b[0x800 + i] = 0;
                if (!any) { for (i = 0; i < 0x600; i++) b[0x40 + i] = 0; want_icon = 0; }   /* the Code file has no icon (a test card) */
            }
        }
        if (!want_text && !want_icon) continue;
        if (want_text) upt_put(b, row->l1, row->l2);
        r = CARDOpen(g_chan, nm, &g_fi);
        if (r != R_READY) continue;
        r = CARDWrite(&g_fi, b, (s32)OTA_BLOCK, 0);
        if (r == R_READY && want_icon) r = CARDGetStatus(g_chan, g_fi.fileNo, &st);
        if (r == R_READY && want_icon) {
            st.commentAddr = 0; st.iconAddr = ICON_OFFSET; st.iconFormat = 1; st.iconSpeed = 1; st.bannerFormat = 0;
            r = sync_wait(CARDSetStatusAsync(g_chan, g_fi.fileNo, &st, CARDSyncCallback));
        }
        CARDClose(&g_fi);
        if (r == R_READY) fixed++; else (void)fail(UPC_OP_STATUS, r);
    }
    return fixed;
}

#ifdef UPC_ART        /* overlay builds (sg_ovl_pack.py, links ms_lz.c) and tests/upd_art_test.c */
/* MELEE SAVE ART: MultishineMelee is a Melee-format save; Melee owns its header and rewrites banner / icon / palette from the
   RAM copies it loaded from LbMcGame.usd (icon_data at 0x80433374 -> four buffers: three banners 0x1800 each, then icon + palette 0x600) whenever header and RAM differ (first save task,
   every new calendar day).  So the art is put in BOTH places, RAM first: our 7680 bytes (upd_art.h, MSLZ) go into the four buffers, then block 0 of the file gets the same bytes and
   the digest Melee computes (16 running byte sums over data 0..0x1E3F, tools/release/save_art.py digest).  Guards, any failure touches NOTHING: game id GALE01 v1.02; icon_data and
   the four pointers inside RAM, 4-aligned, spaced exactly 0x1800; Melee's card layer idle; the file starts "Super Smash Bros. Melee"; its stored digest is valid (a header Melee
   wrote, not a shell or a foreign file); the decode of our art matches its CRC.  The comment lines (line 2 holds Melee's date) are kept.  Only block 0 of MultishineMelee is written,
   and only when its art differs.  The exploit save SuperSmashBros0110290334 is never opened.  Returns bit0 RAM buffers written, bit1 file header written; -1 card not usable,
   -2 a guard failed (nothing touched). */
#ifdef UPC_HOST
extern unsigned h_ram_rd32(unsigned a);
extern void h_ram_copy(unsigned a, const unsigned char *src, unsigned n);
#define UPA_RD32(a) h_ram_rd32(a)
#define UPA_COPY(a, src, n) h_ram_copy((a), (src), (n))
#else
#define UPA_RD32(a) (*(volatile u32 *)(a))
static void upa_copy(u32 a, const u8 *src, u32 n) { volatile u8 *d = (volatile u8 *)a; u32 i; for (i = 0; i < n; i++) d[i] = src[i]; }
#define UPA_COPY(a, src, n) upa_copy((a), (src), (n))
#endif
#define UPA_ICON_DATA 0x80433374u
#define UPA_BAN 0x1800u
static u32 upa_crc32(const u8 *p, u32 n)
{
    u32 c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) { c ^= p[i]; for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static void upa_digest(const u8 *d, u8 *o)         /* hsd_803B2B20 over d[0..0x1E3F] */
{
    static const u8 seed[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 };
    u32 i;
    for (i = 0; i < 16; i++) o[i] = seed[i];
    for (i = 0; i < 0x1E40u; i++) o[i & 15] = (u8)(o[i & 15] + d[i]);
    for (i = 1; i < 16; i++) if (o[i - 1] == o[i]) o[i] ^= 0xFF;
}
#define UPA_W_GAME 1u      /* art_why: not GALE01 at 0x80000000 */
#define UPA_W_RAM 2u       /* icon_data / the four buffer pointers fail the window, alignment or 0x1800 spacing guard */
#define UPA_W_MAGIC 3u     /* the file does not start with "Super Smash Bros. Melee" */
#define UPA_W_DIGEST 4u    /* its stored digest is not valid (a shell, a torn header, a foreign file) */
#define UPA_W_DECODE 5u    /* our art did not decode to its CRC */
#define UPA_W_BUSY 6u      /* Melee's card layer turned busy after the mount (transient: -3) */
#define UPA_W_NOFILE 7u    /* no MultishineMelee yet, or its block 0 could not be read (transient: 0) */
static int upa_ram_ok(u32 q[4])
{
    u32 p = UPA_RD32(UPA_ICON_DATA), i;
    upc_st.art_icon = p;
    /* "GALE" "01"; the version byte is not asked for: the retail-only icon_data layout below is the version check (a v1.00/1.01 image reads another address there and fails it) */
    if (UPA_RD32(0x80000000u) != 0x47414C45u || (UPA_RD32(0x80000004u) >> 16) != 0x3031u) { upc_st.art_why = UPA_W_GAME; return 0; }
    upc_st.art_why = UPA_W_RAM;
    if (p < 0x80003000u || p >= 0x817F0000u || (p & 3u)) return 0;
    for (i = 0; i < 4; i++) {
        q[i] = UPA_RD32(p + 4u * i);
        if (q[i] < 0x80003000u || q[i] >= 0x817F0000u || (q[i] & 3u)) return 0;
        if (i && q[i] != q[i - 1] + UPA_BAN) return 0;
    }
    if (q[3] + 0x600u >= 0x817F0000u) return 0;
    upc_st.art_why = 0;
    return 1;
}
#include "upd_art.h"
#include "../../../payload_pack/ms_lz.h"
static int upa_done(int res)
{
    upc_st.art_res = (u32)res;
    if (res >= 0 && (res & 1)) upc_st.art_ok++;
    if (res >= 0 && (res & 2)) upc_st.art_files++;
    return res;
}
int upc_art_repair(void)
{
    static const char magic[] = "Super Smash Bros. Melee";
    u32 q[4], i, same;
    ms_u32 out_len = 0;
    u8 *b, dg[16];
    int res = 0;
    s32 r;
    upc_st.art_runs++;
    if (enter(UPC_OP_STATUS)) return upa_done(-1);
    b = upc_block();
    if (!b) return upa_done(-1);
    if (CARDOpen(g_chan, "MultishineMelee", &g_fi) != R_READY) { upc_st.art_why = UPA_W_NOFILE; return upa_done(0); }     /* no such save yet: try again at a later pass */
    r = CARDRead(&g_fi, b, (s32)OTA_BLOCK, 0);
    CARDClose(&g_fi);
    if (r != R_READY) { upc_st.art_why = UPA_W_NOFILE; return upa_done(0); }
    if (!upa_ram_ok(q)) return upa_done(-2);
    upc_st.art_why = UPA_W_MAGIC;
    for (i = 0; magic[i]; i++) if (b[i] != (u8)magic[i]) return upa_done(-2);
    upc_st.art_why = UPA_W_DIGEST;
    upa_digest(b, dg);
    for (i = 0; i < 16; i++) if (dg[i] != b[0x1E40 + i]) return upa_done(-2);       /* not a header Melee wrote: leave it */
    same = upa_crc32(b + 0x40, UPA_RAW) == UPA_CRC;
    if (!same) {
        upc_st.art_why = UPA_W_DECODE;
        if (ms_lz_decode(upa_z, UPA_Z, b + 0x40, UPA_RAW, &out_len) != MS_LZ_OK || out_len != UPA_RAW || upa_crc32(b + 0x40, UPA_RAW) != UPA_CRC) return upa_done(-2);
        upa_digest(b, b + 0x1E40);
    }
    if (!upc_melee_quiet()) { upc_st.art_why = UPA_W_BUSY; return upa_done(-3); }
    upc_st.art_why = 0;
    UPA_COPY(q[0], b + 0x40, UPA_BAN); UPA_COPY(q[1], b + 0x40, UPA_BAN); UPA_COPY(q[2], b + 0x40, UPA_BAN);
    UPA_COPY(q[3], b + 0x40 + UPA_BAN, 0x600u);
    res = 1;
    if (!same) {
        if (CARDOpen(g_chan, "MultishineMelee", &g_fi) != R_READY) return upa_done(res | 4);
        r = CARDWrite(&g_fi, b, (s32)OTA_BLOCK, 0);
        CARDClose(&g_fi);
        if (r == R_READY) res |= 2; else { (void)fail(UPC_OP_WRITE, r); res |= 4; }
    }
    return upa_done(res);
}

#endif /* UPC_ART */

/* ---- earlier image additions: the size of one of our files, and a create that copies the card status from a.gci header ---- */
unsigned upc_file_blocks(const char *name)
{
    if (enter(UPC_OP_EXISTS)) return 0;
    return file_blocks(name);
}

/* create nblocks whole blocks and give the file the card status of the .gci directory entry `hdr` (0x40 bytes: banner format at 7, icon address at 0x2C, icon format / speed at
   0x30, comment address at 0x3C).  Same return codes as op_create: 1 no room, 2 name exists, 3 failed. */
int upc_create_gci(const char *name, ota_u32 nblocks, const unsigned char *hdr)
{
    struct card_stat st;
    s32 r;
    if (enter(UPC_OP_CREATE)) return 3;
    r = sync_wait(CARDCreateAsync(g_chan, name, nblocks * OTA_BLOCK, &g_fi, CARDSyncCallback));
    if (r == R_INSSPACE || r == R_NOENT) return 1;
    if (r == R_EXIST) return 2;
    if (r != R_READY) return fail(UPC_OP_CREATE, r);
    r = CARDGetStatus(g_chan, g_fi.fileNo, &st);
    if (r == R_READY) {
        st.bannerFormat = hdr[7];
        st.iconAddr = ((u32)hdr[0x2C] << 24) | ((u32)hdr[0x2D] << 16) | ((u32)hdr[0x2E] << 8) | hdr[0x2F];
        st.iconFormat = (u16)(((u32)hdr[0x30] << 8) | hdr[0x31]);
        st.iconSpeed = (u16)(((u32)hdr[0x32] << 8) | hdr[0x33]);
        st.commentAddr = ((u32)hdr[0x3C] << 24) | ((u32)hdr[0x3D] << 16) | ((u32)hdr[0x3E] << 8) | hdr[0x3F];
        r = sync_wait(CARDSetStatusAsync(g_chan, g_fi.fileNo, &st, CARDSyncCallback));
    }
    CARDClose(&g_fi);
    return r == R_READY ? 0 : fail(UPC_OP_STATUS, r);
}
#endif

#ifndef UPC_BOOT_ONLY
/* ---- Backup to slot B (see upd_card.h).  Own state, own work area, channel 1 hard-coded: nothing below can address slot A for writing. ---- */
#define BCH 1
static int b_mounted;
static struct card_fi b_fi;
static s32 b_mem_mbit;

int upc_a_entry(int idx, struct upc_ent *e)
{
    struct card_stat st;
    s32 r;
    int i;
    if (enter(UPC_OP_STATUS)) return 3;
    r = CARDGetStatus(g_chan, idx, &st);
    if (r == R_NOFILE || r == -10) return 1;         /* empty, or another game's file (NOPERM) */
    if (r != R_READY) { fail(UPC_OP_STATUS, r); return 3; }
    for (i = 0; i < 32 && st.fileName[i]; i++) e->name[i] = st.fileName[i];
    e->name[i] = 0;
    e->nblk = st.length / OTA_BLOCK;
    e->banner = st.bannerFormat; e->icon_addr = st.iconAddr; e->icon_fmt = st.iconFormat; e->icon_speed = st.iconSpeed; e->comment_addr = st.commentAddr;
    return 0;
}

int upc_b_probe(unsigned *blocks)
{
    s32 mem = 0, sector = 0, r;
    int tries = PROBE_TRIES;
    *blocks = 0;
    if (g_chan != 0) return 1;                       /* the card in use IS slot B: no second card to copy to */
    if (!upc_melee_idle()) return 4;
    while ((r = CARDProbeEx(BCH, &mem, &sector)) == R_BUSY && --tries > 0) VIWaitForRetrace();
    if (r == R_NOCARD || r == R_BUSY) return 1;
    if (r != R_READY) return 2;
    if (sector != 0x2000) return 2;
    b_mem_mbit = mem;
    *blocks = mem > 0 ? (unsigned)mem * 16u - 5u : 0u;
    return 0;
}

int upc_b_mount(void *wa)
{
    s32 r;
    if (b_mounted) return 0;
    r = CARDMountAsync(BCH, wa, 0, CARDSyncCallback);
    if (r >= 0) r = CARDSync(BCH);
    if (r != R_READY) { CARDUnmount(BCH); return r ? r : 3; }
    b_mounted = 1;
    return 0;
}

void upc_b_done(void) { if (b_mounted) { CARDUnmount(BCH); b_mounted = 0; } }

int upc_b_free(int *blocks, int *files)
{
    s32 bytes = 0, nf = 0, r;
    if (!b_mounted) return 3;
    r = CARDFreeBlocks(BCH, &bytes, &nf);
    if (r != R_READY) return 3;
    *blocks = bytes / (s32)OTA_BLOCK; *files = nf;
    return 0;
}

unsigned upc_b_file_blocks(const char *name)
{
    struct card_stat st;
    unsigned n = 0;
    if (!b_mounted || CARDOpen(BCH, name, &b_fi) != R_READY) return 0;
    if (CARDGetStatus(BCH, b_fi.fileNo, &st) == R_READY) n = st.length / OTA_BLOCK;
    CARDClose(&b_fi);
    return n;
}

int upc_b_create(const char *name, unsigned nblk, const struct upc_ent *e)
{
    struct card_stat st;
    s32 r;
    if (!b_mounted) return 3;
    r = sync_wait_ch(BCH, CARDCreateAsync(BCH, name, nblk * OTA_BLOCK, &b_fi, CARDSyncCallback));
    if (r == R_INSSPACE || r == R_NOENT) return 1;
    if (r == R_EXIST) return 2;
    if (r != R_READY) return 3;
    r = CARDGetStatus(BCH, b_fi.fileNo, &st);
    if (r == R_READY) {
        st.bannerFormat = (u8)e->banner; st.iconAddr = e->icon_addr; st.iconFormat = (u16)e->icon_fmt; st.iconSpeed = (u16)e->icon_speed; st.commentAddr = e->comment_addr;
        r = sync_wait_ch(BCH, CARDSetStatusAsync(BCH, b_fi.fileNo, &st, CARDSyncCallback));
    }
    CARDClose(&b_fi);
    return r == R_READY ? 0 : 3;
}

static int b_rw(const char *name, unsigned off, void *buf, unsigned n, int write)
{
    s32 r;
    if (!b_mounted) return 3;
    r = CARDOpen(BCH, name, &b_fi);
    if (r != R_READY) return 3;
    r = write ? CARDWrite(&b_fi, buf, (s32)n, (s32)off) : CARDRead(&b_fi, buf, (s32)n, (s32)off);
    CARDClose(&b_fi);
    return r == R_READY ? 0 : 3;
}
int upc_b_write(const char *name, unsigned off, const unsigned char *buf, unsigned n) { return b_rw(name, off, (void *)buf, n, 1); }
int upc_b_read(const char *name, unsigned off, unsigned char *buf, unsigned n) { return b_rw(name, off, buf, n, 0); }

int upc_b_delete(const char *name)
{
    s32 r;
    if (!b_mounted) return 3;
    r = sync_wait_ch(BCH, CARDDeleteAsync(BCH, name, CARDSyncCallback));
    if (r == R_NOFILE) return 0;
    return r == R_READY ? 0 : 3;
}
#endif

static struct ota_card_ops g_ops;
const struct ota_card_ops *upc_ops(void)
{
    g_ops.ctx = 0; g_ops.create = op_create; g_ops.write = op_write; g_ops.read = op_read; g_ops.exists = op_exists; g_ops.free_blocks = op_free_blocks;
#ifndef UPC_BOOT_ONLY
    g_ops.del = op_delete; g_ops.rename = op_rename;
#endif
    return &g_ops;
}
