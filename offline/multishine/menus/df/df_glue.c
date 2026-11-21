/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* df_glue: the console side of the disc-file pack (df.h).  Hook bodies called from df_stub.S, the card access (the same retail CARD SDK sequence as sbr_card.c, the shared
   CARD work area owned through ms_card_wa_acquire) and the poll that reads the pack's table of contents from the menu loop.  Soft-float, no libc, no heap. */
#include "df.h"
#include "../../../payload_pack/ms_lz.h"
#ifndef DF_NO_VERIFY
#include "../../../cardloader/cl/mscf_verify.h"
#endif

typedef unsigned int u32;
typedef int s32;
typedef unsigned char u8;
typedef unsigned short u16;

#define DF_DATA __attribute__((section(".data")))

/* --- retail entry points (symbols.txt) --- */
#define DVD_PATH ((s32 (*)(const char *))0x8033796Cu)                    /* DVDConvertPathToEntrynum (hooked: df_in guards the recursion) */
#define DEVCOM_REQ ((s32 (*)(s32, u32, u32, u32, s32, s32, void *, void *))0x8038F6D4u)   /* HSD_DevComRequest (hooked: synthetic entries are ours, real ones pass) */
#define LB_SPIN ((void (*)(void))0x800195D0u)                           /* lb_800195D0: what waitForDisc loops on */
#define DVD_STATUS ((s32 (*)(void))0x80339B4Cu)                         /* DVDGetDriveStatus: 0 = idle */
#define DVD_FASTOPEN ((s32 (*)(s32, u32 *))0x80337C60u)                 /* DVDFastOpen (hooked: df_open answers 0 for a real entry, so the retail function runs; no disc access: the FST is in RAM) */
#define VIWaitForRetrace ((void (*)(void))0x8034F314u)
typedef s32 (*fn_probe)(s32 chan, s32 *mem, s32 *sector);
typedef s32 (*fn_mount)(s32 chan, void *wa, void *detach_cb, void *attach_cb);
typedef s32 (*fn_chan)(s32 chan);
struct card_fi { s32 chan, fileNo, offset, length; u16 iBlock, pad; };
typedef s32 (*fn_open)(s32 chan, const char *name, struct card_fi *fi);
typedef s32 (*fn_close)(struct card_fi *fi);
typedef s32 (*fn_rw)(struct card_fi *fi, void *buf, s32 len, s32 off);
#define CARDProbeEx ((fn_probe)0x80355F94)
#define CARDMountAsync ((fn_mount)0x80356688)
#define CARDUnmount ((fn_chan)0x803568C4)
#define CARDOpen ((fn_open)0x80357534)
#define CARDClose ((fn_close)0x803576AC)
#define CARDRead ((fn_rw)0x80357E88)
typedef s32 (*fn_rwa)(struct card_fi *fi, void *buf, s32 len, s32 off, void *cb);
#define CARDReadAsync ((fn_rwa)0x80357D40)
#define CARDGetStatus ((s32 (*)(s32, s32, void *))0x803585F8)   /* directory entry of an open file: CARDStat.length at +0x20 */
#define CARDSync ((fn_chan)0x80353424)               /* __CARDSync */
#define CARDSyncCallback ((void *)0x80352274)        /* __CARDSyncCallback */
#define LBCN_STATE 0x80432A68u                       /* Melee's card layer: same idle check as sbr_card.c / on_hist.c */
#define LBCG_STATE 0x80433318u
#define CARD_BLOCK 0x804A8B38u
#define SCENE_WORD (*(volatile u32 *)0x80479D30)

extern u8 *ms_card_wa_p;
extern u8 *ms_card_wa_acquire(u32 owner);
extern void ms_card_wa_release(void);

#define WA_SIZE 0xA000
#define RETAIL_TMO 324000000u                        /* time base ticks (40.5 MHz): 8 s */
#define DVD_IGN_TRIES 120                            /* retraces of 'only the DVD queue is busy' after which that check is dropped (sticky) */
#define WAIT_TRIES 700                               /* retraces (about 11 s) a producer waits for Melee's card layer or the DVD queue */
#define PACK_NAME "MultishinePack"                   /* derived entries (the user's disc / Slippi install): patcher-built, NEVER replaced by OTA */
#define OVL_NAME "MultishineOvl"                     /* our own entries ("ms/..."), a signed container (ABI 2): the only file OTA may replace */
#define OVL_ABI 2u
static const char *const g_names[2] = { PACK_NAME, OVL_NAME };
/* MultishineOvl carries CODE (ms/sign.ovl): it is verified when the table is read, with the same message and primitives as the loader and the installer (cl/mscf_verify.h).  Keys: the build's pinned
   public keys (-DDF_PUBKEYS=0x..,0x.. from build_card.sh, 32 bytes each); a build with none (lab cards, plain loader) accepts an unsigned container only (-DDF_ALLOW_UNSIGNED). */
#if defined(DF_PUBKEYS) && !defined(DF_NO_VERIFY)
#pragma GCC diagnostic ignored "-Wmissing-braces"
static const mw2_u8 g_pubs[][32] = { DF_PUBKEYS };      /* flat byte list: 32 bytes per key */
#define DF_NPUB ((unsigned)(sizeof g_pubs / 32))
#elif !defined(DF_NO_VERIFY)
static const mw2_u8 g_pubs[1][32] = { { 0 } };
#define DF_NPUB 0u
#endif
#ifdef DF_ALLOW_UNSIGNED
#define DF_UNSIGNED 1
#else
#define DF_UNSIGNED 0
#endif

/* RAM proof block: words, never a secret */
enum { S_MAGIC, S_STATE, S_COUNT, S_TOC, S_H1, S_H2, S_H3, S_RC, S_ERR, S_CARD, S_BYTES, S_TICKS, S_POLLS, S_WAITS, S_LASTK, S_DUMMY, S_BADCRC, S_BAD, S_SCRFREE, S_SCRREQ, S_SCRFAIL, S_DTICKS, S_SCRMIN, S_BADSIG, S_STOCK, S_BAKED, S_UNKN, S_FULL,
#ifdef UPD_TITLE
     S_OVLSEQ,          /* the build_seq of the verified MultishineOvl container (the update check on the title compares it with the server's; hub_res.c DF_S_OVLSEQ) */
#endif
     S_N };
#ifdef UPD_TITLE
typedef char df_ovlseq_index_is_28[S_OVLSEQ == 28 ? 1 : -1];       /* hub_res.c reads df_st[28] */
#endif
/* S_STOCK..S_FULL: base-aware decoder counters: DELTA entries applied to a stock file / found already patched / disc file of an unknown state / served from a FULL entry instead */
u32 df_st[S_N] DF_DATA = { 0x44465354u };                          /* 'DFST' */
#define ST df_st
enum { ST_UNREAD = 0, ST_OK = 1, ST_ABSENT = 2, ST_BAD = 3 };       /* ST[S_STATE]: ABSENT = no pack file on the card (final), BAD = unreadable or invalid (retried a few times) */

static struct df_ent g_tab[DF_MAX] DF_DATA;
static int g_n DF_DATA;
static int g_in DF_DATA;                                            /* recursion guard for the hooked entry points */
static s32 g_dummy DF_DATA;                                         /* a real disc entry whose first 32 bytes stand in for every served request */
static u8 g_src[DF_MAX] DF_DATA;                                   /* which card file an entry lives in: 0 MultishinePack, 1 MultishineOvl */
static u8 g_pack_bad DF_DATA;                                      /* the pack file is there but its table is unusable */
static struct { s32 real; u32 len; } g_rd[DF_MAX] DF_DATA;          /* DELTA / DELTA_IP: the real disc entry of each table entry and the length of the disc's file (FST) */
static u32 g_fi_buf[16] __attribute__((aligned(32))) DF_DATA;      /* a DVDFileInfo for the length probe */
static u32 g_scratch[8] __attribute__((aligned(32))) DF_DATA;
static u32 g_polls_t DF_DATA, g_bad_tries DF_DATA;
static volatile s32 g_cb_done DF_DATA;
static struct card_fi g_fi DF_DATA;
static u32 g_flen DF_DATA;                                              /* its length: no read and no table field may reach past it */
static u8 *g_wa DF_DATA;
static u8 g_dvd_ign DF_DATA;

static u32 tbl(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

/* WAIT BREADCRUMBS (WII_DOJO_FREEZE): df_dbg[0] = the last blocking step this file entered, (kind << 24) | (a << 16) | count; [1] bounded card syncs that timed out, [2] retail reads that timed out, [3] DVD-busy checks given up.
   kinds: 1 waiting for Melee's card layer (a = melee_code, count = retraces), 2 card probe, 3 card sync (a = chan), 5 CARDRead, 6 retail read spin (a = entry, count = spins / 4096), 0xA file request (a = table index),
   0xB card session open, 0xD retail read requested, 0xF file produced.  Lab builds with MS_HUB_DIAG also paint the word as 8 hex digits into the video frame buffers, so a frozen screen names the step (the frame
   buffers are not redrawn while the main thread is stuck). */
u32 df_dbg[4] DF_DATA;
#ifdef MS_HUB_DIAG
static const u16 g_fnt[16] = { 0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF, 0x7BED, 0x6BAE, 0x7927, 0x6B6E, 0x79E7, 0x79E4 };   /* 3 x 5 hex digits, bit 14 = top left */
static void paint(u32 v)
{
    u32 b, g, y, x, a;
    for (b = 0; b < 3; b++) {
        u32 *p = (u32 *)((u32 *)0x804C0948u)[b];                       /* FrameBuffer[3] (symbols.txt) */
        if (((u32)p >> 28) < 8u) continue;
        for (y = 0; y < 30; y++) {
            u32 *row = p + (24 + y) * 320 + 8;                          /* 640-pixel YUY2 lines: 320 words, 2 pixels each */
            for (g = 0; g < 8; g++) {
                u32 gl = g_fnt[(v >> (28 - 4 * g)) & 15u];
                for (x = 0; x < 4; x++) {
                    u32 w = (x < 3 && ((gl >> (14 - (y / 6) * 3 - x)) & 1u)) ? 0xEB80EB80u : 0x10801080u;
                    row[g * 8 + x * 2] = w;
                    row[g * 8 + x * 2 + 1] = w;
                }
            }
            for (a = (u32)row & ~31u; a < (u32)(row + 64); a += 32) __asm__ volatile("dcbf 0,%0" : : "r"(a) : "memory");
        }
        __asm__ volatile("sync" : : : "memory");
    }
}
#define NOTE_PAINT(v) paint(v)
#else
#define NOTE_PAINT(v) ((void)0)
#endif
static void note(u32 kind, u32 a, u32 count, int show)
{
    u32 v = (kind << 24) | ((a & 0xFFu) << 16) | (count & 0xFFFFu);
    df_dbg[0] = v;
    if (show) NOTE_PAINT(v);
}
void df_note(u32 kind, u32 a, u32 count) { note(kind, a, count, 1); }      /* the menu glue marks its own steps too (ml_glue.c go_mode) */

/* the first failing check of Melee's card layer (0 = idle): 0xB1..0xB6 = the layer words, 0xC0+i = card slot i not idle.  The same words as melee_idle always read, now named (WII_OVL_RETRY: the Wii's first sign-in load was refused as busy for over 11 s) */
static u32 melee_code(void)
{
    int i;
    if (*(volatile s32 *)(LBCN_STATE + 0x80)) return 0xB1u;
    if (*(volatile s32 *)(LBCN_STATE + 0x8AC)) return 0xB2u;
    for (i = 0; i < 11; i++)
        if (*(volatile s32 *)(LBCN_STATE + 0x510 + 0x54 * i) != 14) return 0xC0u + (u32)i;
    if (*(volatile s32 *)(LBCG_STATE + 0xC)) return 0xB3u;
    if (*(volatile s32 *)(LBCG_STATE + 0x10)) return 0xB4u;
    if (*(volatile s32 *)CARD_BLOCK) return 0xB5u;
    if (*(volatile s32 *)(CARD_BLOCK + 0x110)) return 0xB6u;
    return 0;
}

/* __CARDSync without the endless sleep: the SDK sleeps the thread until the card's callback wakes it, and an emulated card that never answers (Nintendont) would hold the menu / scene thread for ever.
   Poll the channel's result word (__CARDBlock + 0x110 * chan + 4, -1 = busy) a retrace at a time, give up after CSYNC_TRIES; the SDK call then only collects an already finished result. */
#define CSYNC_TRIES 600                                  /* retraces: about 10 s */
static s32 csync(s32 chan, u32 id)
{
    volatile s32 *res = (volatile s32 *)(CARD_BLOCK + 0x110u * (u32)chan + 4u);
    u32 w = 0;
    while (*res == -1) {
        if (++w > CSYNC_TRIES) { df_dbg[1]++; return -128; }
        if (!(w & 15u)) note(id, (u32)chan, w, 1);
        VIWaitForRetrace();
    }
    return CARDSync(chan);
}
static s32 cread(struct card_fi *fi, void *buf, s32 len, s32 off)
{
    s32 r = CARDReadAsync(fi, buf, len, off, CARDSyncCallback);
    return r < 0 ? r : csync(fi->chan, 5);
}

/* ------------------------------------------------------------------ card session: held WA, mounted, file open */
static s32 card_mount(s32 chan)
{
    s32 mem = 0, sector = 0, r;
    int tries = 120;
    while ((r = CARDProbeEx(chan, &mem, &sector)) == -1 && --tries > 0) VIWaitForRetrace();
    if (r != 0) return r;
    if (sector != 0x2000) return -2;
    r = CARDMountAsync(chan, g_wa, 0, CARDSyncCallback);
    if (r >= 0) r = csync(chan, 3);
    if (r != 0) { CARDUnmount(chan); return r; }
    return 0;
}

/* -> chan (0/1) with the pack open, -1 no such file, -2 no card / error (result in ST[S_CARD]), -3 busy (retry later).  On >= 0 the card stays mounted + open: df_session_end. */
static int df_session_begin(int patient, int file)
{
    int chan, w = 0, seen = 0, err = 0;
    s32 r;
    while (1) {
        u32 c = melee_code();
        s32 d = DVD_STATUS();
        if (!c && (d <= 0 || g_dvd_ign)) break;
        if (!patient || ++w > WAIT_TRIES) return -3;
        if (!c && w > DVD_IGN_TRIES) { g_dvd_ign = 1; df_dbg[3]++; break; }      /* only the DVD queue is "busy" for 2 s: Nintendont reports it so while a scene loads; stop asking */
        ST[S_WAITS]++;
        if (!(w & 15u)) note(1, c ? c : 0xB7u, w, 1);
        VIWaitForRetrace();
    }
    g_wa = ms_card_wa_acquire(3);
    if (!g_wa) return -3;
    for (chan = 0; chan < 2; chan++) {
        r = card_mount(chan);
        ST[S_CARD] = (u32)r;
        if (r != 0) { if (r != -3) err = 1; continue; }
        seen = 1;
        r = CARDOpen(chan, g_names[file], &g_fi);
        if (r == 0) {
            g_flen = 0;
            { u32 *st = (u32 *)(g_wa + WA_SIZE); if (CARDGetStatus(chan, g_fi.fileNo, st) == 0) g_flen = st[0x20 / 4]; }     /* CARDStat into the (not yet used) block buffer */
            if (g_flen) return chan;
            CARDClose(&g_fi);
        }
        CARDUnmount(chan);
        if (r != -4) err = 1;
    }
    ms_card_wa_release();
    g_wa = 0;
    return err ? -2 : (seen ? -1 : -2);
}

static void df_session_end(int chan)
{
    CARDClose(&g_fi);
    CARDUnmount(chan);
    ms_card_wa_release();
    g_wa = 0;
}

/* the produced bytes were written by the CPU into a buffer that the GPU or a DMA may read (textures): write the data cache lines back, as a DVD DMA would have left them in RAM */
static void flush_range(u32 p, u32 n)
{
    u32 a = p & ~31u, e = p + n;
    for (; a < e; a += 32) __asm__ volatile("dcbf 0,%0" : : "r"(a) : "memory");
    __asm__ volatile("sync" : : : "memory");
}

static int rd_pack(void *ctx, u32 off, u8 *dst, u32 len)
{
    u8 *buf = g_wa + WA_SIZE;
    (void)ctx;
    if ((off & (DF_BLOCK - 1u)) || off > g_flen || len > g_flen - off) return -1;     /* never past the card file */
    while (len) {
        u32 n = len < DF_BLOCK ? len : DF_BLOCK, i;
        if (cread(&g_fi, buf, DF_BLOCK, (s32)off) != 0) return -1;
        for (i = 0; i < n; i++) dst[i] = buf[i];
        off += DF_BLOCK; dst += n; len -= n;
    }
    return 0;
}

static void cb_done(s32 req, s32 args, void *buf, s32 cancelflag)
{
    (void)req; (void)args; (void)buf; (void)cancelflag;
    g_cb_done = 1;
}

static u32 rd_len(void *ctx, const struct df_ent *e)
{
    (void)e;
    return ((const u32 *)ctx)[1];
}

static int rd_retail(void *ctx, const struct df_ent *e, u8 *dst, u32 len)   /* the original disc file by the real entry the hook remembered */
{
    s32 real = (s32)((const u32 *)ctx)[0];
    u32 t0 = tbl(), n = 0;
    (void)e;
    g_cb_done = 0;
    note(0xD, (u32)real, 0, 1);
    DEVCOM_REQ(real, 0, (u32)dst, len, 0x21, 1, (void *)cb_done, 0);
    while (!g_cb_done) {
        LB_SPIN();
        if (!(++n & 0xFFFu)) {
            note(6, (u32)real, n >> 12, 1);
            if (tbl() - t0 > RETAIL_TMO) { df_dbg[2]++; return -1; }     /* the retail read never completed: give up (the file stays as the caller found it) instead of freezing the thread */
        }
    }
    return 0;
}

/* DELTA scratch: the HSD heap through OSAllocFromHeap (NOT HSD_MemAlloc: that asserts when the heap is full).  OSCheckHeap before the call records the free bytes (the measurement) */
#define OS_ALLOC ((void *(*)(s32, u32))0x80343EF0u)
#define OS_FREE ((void (*)(s32, void *))0x80343FECu)
#define HSD_HEAP ((s32 (*)(void))0x80375404u)
static void *h_alloc(void *ctx, u32 n)
{
    void *p = OS_ALLOC(HSD_HEAP(), n);
    (void)ctx;
    if (!p) { ST[S_SCRFAIL]++; if (!ST[S_SCRMIN] || n < ST[S_SCRMIN]) ST[S_SCRMIN] = n; }     /* the smallest request that failed: the scratch measurement of  15 */
    return p;
}
static void h_release(void *ctx, void *p) { (void)ctx; OS_FREE(HSD_HEAP(), p); }

/* ------------------------------------------------------------------ table of contents: read from the menu loop (br_tick) or, blocking, at the first main-menu entry (df_arm) */
/* stored-bytes CRC of one entry through the open session (the damaged-pack check: an entry whose stored bytes do not match the packer's CRC is dropped, so its file is read from the disc as on a stock
   console and nothing corrupt ever reaches a scene).  p_old is the reserved TOC word that the packer fills with it (0 = not recorded). */
static int entry_ok(const struct df_ent *e)
{
    u8 *buf = g_wa + WA_SIZE;
    u32 off = e->data_off, left = e->stored_len, crc = 0;
    if (!e->p_old) return 1;
    while (left) {
        u32 n = left < DF_BLOCK ? left : DF_BLOCK;
        if (cread(&g_fi, buf, DF_BLOCK, (s32)off) != 0) return 0;
        crc = df_crc32(buf, n, crc);
        off += DF_BLOCK; left -= n;
    }
    return crc == e->p_old;
}

/* the signed container: the whole payload (table block + data blocks) is read into one heap block, CRC-checked against the header and verified with the loader's own message (header, tables, payload hash);
   0 = refuse.  h = a copy of the 0x80-byte header (the chunk buffer is reused for the payload reads). */
#ifdef DF_NO_VERIFY       /* size-constrained lab builds: the container is trusted as the installer verified it */
static int ovl_verify(const u8 *h) { (void)h; return 1; }
#else
static int ovl_verify(const u8 *h)
{
    u32 len = ((u32)h[0x18] << 24) | ((u32)h[0x19] << 16) | ((u32)h[0x1A] << 8) | h[0x1B], crc = ((u32)h[0x1C] << 24) | ((u32)h[0x1D] << 16) | ((u32)h[0x1E] << 8) | h[0x1F];
    u8 *img;
    int ok = 0;
    if (!len || len > 0x100000u || len > g_flen - 0x2000u || (((u32)h[0x20] << 24) | ((u32)h[0x21] << 16) | ((u32)h[0x22] << 8) | h[0x23]) || (((u32)h[0x24] << 24) | ((u32)h[0x25] << 16) | ((u32)h[0x26] << 8) | h[0x27])) return 0;
    img = (u8 *)h_alloc(0, (len + 31u) & ~31u);
    if (!img) return 0;
    if (rd_pack(0, 0x2000, img, len) == 0 && df_crc32(img, len, 0) == crc) ok = mscf_verify_sig(h, h + 0x80, 0, img, len, g_pubs, DF_NPUB, DF_UNSIGNED, 0) != 0;
    h_release(0, img);
    return ok;
}
#endif

/* one file's table into g_tab[at..]: 0 = ok (*got entries), 1 = file absent, 2 = unreadable/damaged, 3 = busy (retry) */
static int toc_file(int patient, int file, int at, int *got)
{
    int chan, n, i;
    u32 vlen = 0;
    int vfy = 0;                                                  /* 1: the whole container payload was read, CRC-checked and signature-verified just now (ovl_verify), so the per-entry CRC pass below adds nothing */
    u8 *blk;
    *got = 0;
    chan = df_session_begin(patient, file);
    if (chan == -3) return 3;
    if (chan == -1) return 1;
    if (chan < 0) return 2;
    blk = g_wa + WA_SIZE;
    if (cread(&g_fi, blk, DF_BLOCK, 0) != 0) { df_session_end(chan); return 2; }
    if (file == 0) {
        n = df_toc_parse(blk, g_tab + at, DF_MAX - at, DF_HDR_OFF, g_flen);
    } else {   /* the signed container: MSCF header (magic 'MSCF', ABI 2, flag 8) in block 0, the table in the first payload block */
        const u8 *h = blk + 0x1000;
        u8 hd[0x80];
        u32 q, t0;
        int vok;
        for (q = 0; q < 0x80; q++) hd[q] = h[q];
        if (((u32)h[0] << 24 | (u32)h[1] << 16 | (u32)h[2] << 8 | h[3]) != 0x4D534346u || (((u32)h[6] << 8) | h[7]) != OVL_ABI || !(h[15] & 8u)) n = -1;
        else {
            t0 = tbl(); vok = ovl_verify(hd); ST[S_DTICKS] = tbl() - t0;              /* timebase ticks of the container check (read, CRC, SHA-512, Ed25519):  5 */
#ifndef DF_NO_VERIFY
            vfy = vok; vlen = ((u32)hd[0x18] << 24) | ((u32)hd[0x19] << 16) | ((u32)hd[0x1A] << 8) | hd[0x1B];
#endif
            if (!vok) { n = -1; ST[S_BADSIG]++; }
#ifdef UPD_TITLE
            else if ((ST[S_OVLSEQ] = ((u32)hd[8] << 24) | ((u32)hd[9] << 16) | ((u32)hd[10] << 8) | hd[11]), cread(&g_fi, blk, DF_BLOCK, 0x2000) != 0) { df_session_end(chan); return 2; }
#else
            else if (cread(&g_fi, blk, DF_BLOCK, 0x2000) != 0) { df_session_end(chan); return 2; }
#endif
            else n = df_toc_parse(blk, g_tab + at, DF_MAX - at, 0, g_flen);
        }
    }
    ST[S_TOC] = (u32)n;
    if (n < 0) { df_session_end(chan); return 2; }
    for (i = 0; i < n; i++) {
        struct df_ent *e = &g_tab[at + i];
        g_src[at + i] = (u8)file;
        if (file == 1 && !(e->name[0] == 'm' && e->name[1] == 's' && e->name[2] == '/')) { e->name[0] = 0; ST[S_BAD]++; continue; }   /* OTA-able file: our own names only */
        if (!(vfy && e->data_off >= 0x2000u && e->stored_len <= vlen && e->data_off - 0x2000u <= vlen - e->stored_len) && !entry_ok(e)) { e->name[0] = 0; ST[S_BAD]++; }          /* the signed container (MultishineOvl) was verified as a whole above: reading every entry again was a second 62-block pass of the same bytes (about 5,000 card commands, 10 s on a Wii); the unsigned pack keeps its per-entry CRC */
    }
    df_session_end(chan);
    *got = n;
    return 0;
}

static void toc_read(int patient)
{
    int n1 = 0, n2 = 0, r1, r2;
    ST[S_POLLS]++;
    ST[S_BAD] = 0;
    r1 = toc_file(patient, 1, 0, &n1);                    /* our own entries first: they win a name clash */
    if (r1 == 3) return;
    r2 = toc_file(patient, 0, n1, &n2);
    if (r2 == 3) return;
    if (r1 == 2 || r2 == 2) { if (++g_bad_tries < 5 && n1 + n2 == 0) return; }
    g_pack_bad = (u8)(r2 == 2);
    if (n1 + n2 == 0) { ST[S_STATE] = (r1 == 2 || r2 == 2) ? ST_BAD : ST_ABSENT; return; }
    {
        s32 d;
        g_in = 1;
        d = DVD_PATH("opening.bnr");
        if (d < 0) d = DVD_PATH("SdMenu.usd");
        g_in = 0;
        if (d < 0) { ST[S_STATE] = ST_BAD; return; }
        g_dummy = d;
        ST[S_DUMMY] = (u32)d;
        g_n = n1 + n2;
        ST[S_COUNT] = (u32)g_n;
        ST[S_STATE] = ST_OK;
    }
}

/* once per menu frame (br_glue.c br_tick, title + menus, no EXI lock): the first successful call arms the hooks */
void df_poll(void)
{
    u32 now = tbl();
    if (ST[S_STATE] != ST_UNREAD) return;
    if (g_polls_t && now - g_polls_t < 40500000u / 2u) return;   /* one try per second (time base 40.5 MHz / 2) */
    g_polls_t = now ? now : 1;
    toc_read(0);
}

/* the first main-menu entry (ml_enter, before the menu archive is read): a very fast START must not beat the table of contents, so wait for it here (bounded: a card that never answers
   falls through to the disc files) */
void df_arm(void)
{
    if (ST[S_STATE] == ST_UNREAD) toc_read(1);
}

/* The Update machine wrote MultishinePack (the system files set-up): read the tables of contents again now, so the pack's entries (slpCSS.dat and the menu files) are served without a restart.
   Bounded (a busy card is waited for), runs from the menu frame that follows the write.  Returns df_pack_state. */
int df_pack_state(void);
int df_reload(void)
{
    g_bad_tries = 0;
    toc_read(1);
    return df_pack_state();
}

/* 0 = no pack read yet / absent (plain disc), 1 = pack in use, 2 = pack present but damaged (bad table, or entries dropped: ST[S_BAD]) */
int df_pack_state(void)
{
    if (ST[S_STATE] == ST_BAD || (ST[S_STATE] == ST_OK && (ST[S_BAD] || g_pack_bad))) return 2;
    return ST[S_STATE] == ST_OK ? 1 : 0;
}

/* ------------------------------------------------------------------ the three hook bodies (df_stub.S) */
/* H1: DVDConvertPathToEntrynum(path).  A pack name -> SYN entry (DELTA entries remember the real entry for the retail read); anything else DF_PASS. */
s32 df_path(const char *path)
{
    int k;
    if (ST[S_STATE] != ST_OK || g_in) return DF_PASS;
    k = df_find(g_tab, g_n, path);
    if (k < 0) return DF_PASS;
    if (g_tab[k].mode >= DF_DELTA) {
        s32 real;
        u32 rl = 0;
        int alt = df_find_alt(g_tab, g_n, k);
        g_in = 1;
        real = DVD_PATH(path);
        if (real >= 0 && DVD_FASTOPEN(real, g_fi_buf)) rl = g_fi_buf[0x34 / 4];      /* the disc file's length */
        g_in = 0;
        if (real < 0 || df_classify(&g_tab[k], rl) == DF_CL_UNKNOWN) {    /* the disc lacks the file, or it has neither the stock nor the intended length: a FULL entry of the pack, else the disc's own file is left alone (never 'damaged') */
            if (alt < 0) { ST[S_UNKN]++; return DF_PASS; }
            k = alt;
            ST[S_FULL]++;
        } else { g_rd[k].real = real; g_rd[k].len = rl; }
    }
    ST[S_H1]++;
    ST[S_LASTK] = (u32)k;
    return DF_SYN_BASE + k;
}

/* H2: DVDFastOpen(entry, info).  Melee reads a file's size through DVDFastOpen + info.length in several INLINED copies (lbFileGetSize, lbFile_800164A4, ...; lbFile_8001634C is
   only one of them), so this leaf function is the common point.  A synthetic entry gets a zeroed DVDFileInfo (command block state 0 = END, so DVDClose is a no-op) with the length. */
s32 df_open(s32 entry, u32 *info)
{
    s32 k = entry - DF_SYN_BASE;
    int i;
    if (ST[S_STATE] != ST_OK || k < 0 || k >= g_n) return 0;
    for (i = 0; i < 15; i++) info[i] = 0;
    info[0x30 / 4] = 0;
    info[0x34 / 4] = g_tab[k].new_size;
    ST[S_H2]++;
    return 1;
}

/* H3: HSD_DevComRequest(a[0]=file, a[1]=src, a[2]=dest, a[3]=size, a[4]=type, a[5]=pri, a[6]=cb, a[7]=args).  A synthetic entry: produce the file into dest here, then turn
   the request into a 32-byte read of a real disc file (a[0..4] rewritten) so that the DevCom queue, the priority and the callback behave as for a real read. */
/* the file of table entry k into dest (cap bytes): 0 = ok, else df_produce's negative error, or -100 + the card session result when the pack cannot be read now.  scratch: DELTA may allocate. */
static int produce(int k, u8 *dest, u32 cap, int scratch)
{
    struct df_io io;
    int chan, rc;
    io.read_pack = rd_pack; io.read_retail = rd_retail; io.retail_len = rd_len; io.alloc = scratch ? h_alloc : 0; io.release = scratch ? h_release : 0;
    io.ctx = (void *)&g_rd[k];
    note(0xA, (u32)k, 0, 1);
    chan = df_session_begin(scratch ? 1 : 0, g_src[k]);
    note(0xB, (u32)k, (u32)chan, 1);      /* overlay files (no scratch) never wait: the menu thread must not sit in a retrace loop (the caller checks df_ovl_why first and retries per frame) */
    if (chan < 0) return -100 + chan;
    rc = df_produce(&g_tab[k], &io, dest, cap);
    df_session_end(chan);
    note(0xF, (u32)k, (u32)rc, 0);
    flush_range((u32)dest, cap);
    return rc;
}

void df_req(u32 *a)
{
    s32 k = (s32)a[0] - DF_SYN_BASE;
    u32 t0;
    int rc;
    if (ST[S_STATE] != ST_OK || k < 0 || k >= g_n) return;
    ST[S_H3]++;
    t0 = tbl();
    rc = produce(k, (u8 *)a[2], a[3], 1);
    if (rc == DF_E_BASE) {                                    /* the disc's file is neither the stock base nor the intended result (same length, other CRC): a FULL entry of the same size, else the disc's own bytes stay in dest */
        int alt = df_find_alt(g_tab, g_n, k);
        ST[S_UNKN]++;
        if (alt >= 0 && g_tab[alt].new_size == g_tab[k].new_size) { rc = produce(alt, (u8 *)a[2], a[3], 1); ST[S_FULL]++; k = alt; }
    }
    if (rc > 0) { ST[S_BAKED]++; rc = 0; }                    /* DF_OK_BAKED: the disc file already is the result */
    else if (rc == 0 && g_tab[k].mode >= DF_DELTA) ST[S_STOCK]++;
    if (rc <= -100) {                                         /* cannot read the pack now: the scene gets a zero-filled file (its own parse will fail loudly) rather than stale bytes */
        u32 i, n = a[3];
        for (i = 0; i < n; i++) ((u8 *)a[2])[i] = 0;
    }
    ST[S_RC] = (u32)rc;
    if (rc == -13) ST[S_BADCRC]++; else if (rc && rc != DF_E_BASE) ST[S_ERR]++;
    ST[S_BYTES] += g_tab[k].new_size;
    ST[S_TICKS] = tbl() - t0;
    a[0] = (u32)g_dummy; a[1] = 0; a[2] = (u32)g_scratch; a[3] = 32; a[4] = 0x21;
}

/* ------------------------------------------------------------------ overlay files (ms/sign.ovl): the resident overlay loader (ml_glue.c sd_open) asks for a pack file directly,
   without the DVD queue.  Only the entries of the pack that H1 answered with a synthetic entry; called from the menu frame, never from the slice. */
s32 df_ovl_size(s32 entry)
{
    s32 k = entry - DF_SYN_BASE;
    if (ST[S_STATE] != ST_OK || k < 0 || k >= g_n || g_tab[k].mode >= DF_DELTA) return -1;
    return (s32)g_tab[k].new_size;
}

/* the file into dest (cap = ROUND_UP_32(new_size)); 0 = ok, else df_produce's negative error or -100 + the card session result */
int df_ovl_produce(s32 entry, u8 *dest, u32 cap)
{
    s32 k = entry - DF_SYN_BASE;
    if (ST[S_STATE] != ST_OK || k < 0 || k >= g_n || g_tab[k].mode >= DF_DELTA) return -1;
    return produce(k, dest, cap, 0);
}

/* the same for the menu overlay (ms/menu.ovl, ml_glue.c sd_open): its load is one blocking call at the entry of the menu scene (the table is patched before the retail OnEnter builds
   the first screen), so a busy card layer (Melee's own save just after a match) is waited for like the pack's scene files are (produce with scratch = 1 = patient: WAIT_TRIES retraces, then -3).
   Raw / MSLZ entries only (as df_ovl_produce); the scratch allocator is not used for them. */
int df_ovl_produce_w(s32 entry, u8 *dest, u32 cap)
{
    s32 k = entry - DF_SYN_BASE;
    if (ST[S_STATE] != ST_OK || k < 0 || k >= g_n || g_tab[k].mode >= DF_DELTA) return -1;
    return produce(k, dest, cap, 1);
}

/* WHY the overlay file cannot be produced now, without waiting: 0 = ready; 1 = the table is not read yet (df_poll reads it once a second; retry); 2 = no usable pack (final);
   0xB1..0xB6, 0xC0+i = Melee's card layer busy (melee_code); 0xB7 = DVD queue busy; 0xB8 = the shared CARD work area is held.  *last (if not null): state | S_CARD << 8 | polls << 16 | DVD status << 24 */
u32 df_ovl_why(u32 *last)
{
    u32 c;
    s32 d = DVD_STATUS();
    if (last) *last = (ST[S_STATE] & 0xFFu) | ((ST[S_CARD] & 0xFFu) << 8) | ((ST[S_POLLS] & 0xFFu) << 16) | (((u32)d & 0xFFu) << 24);
    if (ST[S_STATE] == ST_UNREAD) return 1u;
    if (ST[S_STATE] != ST_OK) return 2u;
    c = melee_code();
    if (c) return c;
    if (d > 0) return 0xB7u;
    if (ms_card_wa_p) return 0xB8u;
    return 0;
}
