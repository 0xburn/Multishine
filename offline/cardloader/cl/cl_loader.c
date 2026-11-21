/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Card code loader (stage 1.5): runs once inside the save-exploit's Gecko list (a C0 "execute ASM" code, offline/cardloader/mscf.py wraps it),
 * reads the card file "MultishineCode" (format: mscf.h), claims a hole at the top of AllM, loads the image there, applies the fixed-address
 * copies and word patches, flushes the caches and calls the image's entry.  Original work; retail SDK entry points only (symbols.txt of
 * , the same ones uses), so nothing here depends on Melee's own card layer being usable.
 *
 * Never crashes the game: every failure leaves the heap bounds and the code as they were and returns; the reason is in the status block
 * (CL_STATUS).  No libc, no globals (the stack only), soft float.  Linked at its final address (build_cl.sh BASE). */
#include "mscf.h"

#ifndef CL_FILE_NAME
#define CL_FILE_NAME "MultishineCode"   /* offline/projname.py SHORT + "Code"; build_cl.sh passes the real one */
#endif

typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef CL_VERIFY
#define CL_VERIFY 0
#endif
#if CL_VERIFY
/* OTA stage 0: the code file must carry a valid Ed25519 signature (SHA-512 of the header, the tables and the image)
 * under one of the pinned public keys, its ABI must match, its build_seq must be >= the floor, and it falls
 * back from MultishineCode to MultishineCodeB.  CL_DEV builds (separate loader, separate key, never a release) also accept an unsigned file. */
#include "cl_keys.h"
#include "mscf_verify.h"
#ifndef CL_MIN_SEQ
#define CL_MIN_SEQ 1u
#endif
/* Automatic recovery from a signed build that crashes at start (both parts READ-ONLY, the loader still never writes the card):
 *  - CL_TRIES: the boot record of MultishineState (written by the updater at install and counted by the engine at start, offline/cardloader/ota/ota_core.c bootrec_*).  A file whose
 *    build_seq is the record's candidate and that started CL_TRIES times without reaching the Main Menu is refused (E_TRIED) and the previous file loads.  0 = off.
 *  - CL_SAFE: L+R held on any controller while the loader runs skips the active file (E_SAFE) and loads the previous one.  0 = off. */
#ifndef CL_TRIES
#define CL_TRIES 3u
#endif
#ifndef CL_SAFE
#define CL_SAFE 1
#endif
#endif

#if CL_VERIFY
/* CL_MUTE (default 1 in the verifying loader): the Name Entry menu track is silent while the loader reads and checks the card.
 * Melee's music keeps streaming from interrupts while this loader blocks the game's frame loop, so writing only the live volume word would act after the load; the loader
 * therefore writes the three words fn_80024654 itself derives from the live word (live 0..127, the synth's stream volume float, the last value it applied)
 * and queues every voice in use for the AX callback's volume pass (HSD_SynthSFXUpdateVolume, integer code, retail word guarded).  The START-volume word 0x80023FFC is never
 * touched.  An engine of build_seq >= CL_MUTE_SEQ puts the live word back (hub_res.c, first pass
 * outside the menu scene: the title); for any other outcome (no file, a refused file, an older engine such as a B copy below CL_MUTE_SEQ) the loader puts the three words
 * back itself before it returns.  0 = no mute. */
#ifndef CL_MUTE
#define CL_MUTE 1
#endif
#ifndef CL_MUTE_SEQ
#define CL_MUTE_SEQ 1294u
#endif
#else
#undef CL_MUTE
#define CL_MUTE 0
#endif

#ifndef CL_COMP
#define CL_COMP 0
#endif
#if CL_COMP
/* compressed code files (MSCF version 2, F_COMP): the MSLZ stream is read to the tail of the hole and decoded in place.
 * Off (default) = this loader byte for byte as before: it refuses version 2 with E_HDR. */
#include "../../payload_pack/ms_lz.c"
#endif

struct card_fi { s32 chan, fileNo, offset, length; u16 iBlock, pad; };

typedef s32 (*fn_probe)(s32, s32*, s32*);
typedef s32 (*fn_mount)(s32, void*, void*, void*);
typedef s32 (*fn_chan)(s32);
typedef s32 (*fn_open)(s32, const char*, struct card_fi*);
typedef s32 (*fn_close)(struct card_fi*);
typedef s32 (*fn_rw)(struct card_fi*, void*, s32, s32);
typedef void (*fn_void)(void);
typedef void (*fn_entry)(void*);

#define CARDProbeEx ((fn_probe) 0x80355F94)
#define CARDMountAsync ((fn_mount) 0x80356688)
#define CARDUnmount ((fn_chan) 0x803568C4)
#define CARDOpen ((fn_open) 0x80357534)
#define CARDClose ((fn_close) 0x803576AC)
#define CARDRead ((fn_rw) 0x80357E88)
#define CARDSync ((fn_chan) 0x80353424)
#define CARDSyncCallback ((void*) 0x80352274)
#define VIWaitForRetrace ((fn_void) 0x8034F314)

#define LBHEAP 0x80431FA0u               /* lbHeap_80431FA0: arena_hi +4, heap_array[i] at +0x10 + 0x1C*i: start +8, size +C, status +0x18 */
#define ALLM_IDX 4u

#define W(a) (*(volatile u32*) (a))
#define ST ((volatile u32*) CL_STATUS)

static u32 be32(const u8* p) { return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | p[3]; }

#if CL_COMP
/* CRC-32 (zlib), two 16-entry nibble steps per byte: ~3x faster than the bit loop, which matters now that two images are checked (stream, then decoded image) */
static u32 crc32(u32 crc, const u8* p, u32 n)
{
    static const u32 t[16] = { 0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                               0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ t[crc & 15u];
        crc = (crc >> 4) ^ t[crc & 15u];
    }
    return ~crc;
}
#else
static u32 crc32(u32 crc, const u8* p, u32 n)
{
    crc = ~crc;
    while (n--) {
        u32 i;
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (u32) -(s32) (crc & 1u));
    }
    return ~crc;
}
#endif

static u32 tb(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

static void flush_range(u32 a, u32 n)
{
    u32 p = a & ~31u, e = a + n;
    for (; p < e; p += 32u)
        __asm__ volatile("dcbst 0,%0\n\tsync\n\ticbi 0,%0" ::"r"(p) : "memory");
    __asm__ volatile("sync\n\tisync" ::: "memory");
}

/* write back and drop cached lines of a range we are about to receive by DMA (the SDK does not do it for CARDRead into our buffer) */
static void dcache_drop(u32 a, u32 n)
{
    u32 p = a & ~31u, e = a + n;
    for (; p < e; p += 32u)
        __asm__ volatile("dcbf 0,%0" ::"r"(p) : "memory");
    __asm__ volatile("sync" ::: "memory");
}

#ifdef CL_PARTS
#include "cl_parts.h"
#endif

#if CL_MUTE
#define MU_LIVE  0x804D387Cu      /* lbl_804D387C: the stream volume 0..127 (fn_80024654 turns it into the synth volume every frame) */
#define MU_SYN   0x804D6030u      /* HSD_Synth_804D6030: the synth's stream volume (f32), the factor of every voice volume the AX callback computes */
#define MU_LAST  0x804D63F0u      /* lbl_804D63F0: the synth volume fn_80024654 applied last (f32): kept equal to MU_SYN so the game's next change is applied */
#define MU_NODES 0x804C2C64u      /* hsd_SynthSFXNodes[64], 0x50 bytes each, word 0 > 0 = in use */
#define MU_UPD   0x8038A5A4u      /* HSD_SynthSFXUpdateVolume(node): queue the node for the AX callback's volume pass */
#define MU_UPD_W0 0x7C0802A6u     /* its retail first word (mflr r0) */
#define CL_MUTE_MARK 0x4D555445u  /* 'MUTE' in ST[32] (ST[33] = the live word before the mute): the music is muted for the engine to restore (words 32/33 lie in the status
                                     area the exploit's copy table rewrites at every load, beyond the 32 words a 1291 loader clears) */
static void mu_set(u32 live, u32 syn, u32 last)
{
    u32 i, n;
    W(MU_LIVE) = live; W(MU_SYN) = syn; W(MU_LAST) = last;
    if (W(MU_UPD) != MU_UPD_W0)
        return;
    for (i = 0; i < 64; i++) {
        n = MU_NODES + 0x50u * i;
        if ((s32) W(n) > 0)
            ((void (*)(u32)) MU_UPD)(n);
    }
}
#endif

static s32 card_mount(s32 chan, void* wa)
{
    s32 mem = 0, sector = 0, r;
    int tries = 120;
    while ((r = CARDProbeEx(chan, &mem, &sector)) == -1 && --tries > 0)
        VIWaitForRetrace();
    if (r != 0)
        return r;
    if (sector != 0x2000)
        return -2;
    r = CARDMountAsync(chan, wa, 0, CARDSyncCallback);
    if (r >= 0)
        r = CARDSync(chan);
    if (r != 0) {
        CARDUnmount(chan);
        return r;
    }
    return 0;
}

#if CL_VERIFY
#ifndef CL_STATE_NAME
#define CL_STATE_NAME "MultishineState"
#endif
#ifndef CL_DEV
#define CL_DEV 0
#endif
/* The signature check (message definition, pinned keys) is mscf_verify.h, shared with the update installer and mirrored by tools/card/mscf_sign.py. */
static u32 verify_sig(const u8* h, u32 base, u32 len, u32 nseg, u32 npatch, int comp)
{
    return mscf_verify_sig(h, h + H_SEGS, nseg * 16u + npatch * 8u, (const u8*) base, len, cl_pub, CL_NKEYS, CL_DEV, comp);
}

/* returns E_*; on E_OK the image is loaded, patched; run[0] = entry to call (0 = none), run[1] = hole base.  The caller mounted the card. */
static u32 load_one(s32 chan, const char* name, u32 floor, u32 tried, u32* run)
#else
/* returns E_*; on E_OK the image is loaded, patched and the entry ran */
static u32 load_from(s32 chan)
#endif
{
#if !CL_VERIFY
    static const char name[] = CL_FILE_NAME;
#endif
    struct card_fi fi;
    u8* top = (u8*) CL_HOLE_MAX;
    u8* wa = top - CL_WA_SIZE;                 /* temporary work area: the tail of the maximal hole */
    u8* hb = wa - MSCF_BLOCK;                  /* header block buffer */
    const u8* h = hb;
    s32 r;
    u32 flags, base, size, len, nseg, npatch, entry, crc, t, i, claimed = 0, savedsz = 0, savedhi = 0, res = E_OK;
    u32 rdst, rlen;
#if CL_COMP
    u32 zlen = 0, zcrc = 0;
#endif
    u32 allm = LBHEAP + 0x10u + 0x1Cu * ALLM_IDX;

#if !CL_VERIFY
    ST[3] = (u32) chan;
    r = card_mount(chan, wa);
    ST[4] = (u32) r;
    if (r != 0)
        return r == -3 ? E_NOCARD : E_MOUNT;
#endif
    ST[1] = 2;
    r = CARDOpen(chan, name, &fi);
    if (r != 0) {
#if !CL_VERIFY
        CARDUnmount(chan);
#endif
        return r == -4 ? E_NOFILE : E_MOUNT;
    }
    ST[1] = 3;
    dcache_drop((u32) hb, MSCF_BLOCK);
    r = CARDRead(&fi, hb, MSCF_BLOCK, 0);
    if (r != 0) {
        res = E_READ;
        goto out;
    }
    h = hb + MSCF_HDR_OFF;
#if CL_VERIFY
    if (be32(h + H_MAGIC) != MSCF_MAGIC || ((be32(h + H_VERSION) >> 16) != MSCF_VERSION && (!CL_COMP || (be32(h + H_VERSION) >> 16) != MSCF_VERSION_Z))) {
        res = E_HDR;
        goto out;
    }
    if ((be32(h + H_VERSION) & 0xFFFFu) != MSCF_ABI) {       /* ABI: the loader <-> image contract; an ABI change needs a new loader (new exploit save) */
        res = E_ABI;
        goto out;
    }
    ST[18] = be32(h + H_SEQ);
    if (ST[18] < floor) {                                      /* anti-rollback: below the floor (loader constant or the state file) */
        res = E_SEQ;
        goto out;
    }
    if (ST[18] == tried) {                                     /* the boot record says this build started CL_TRIES times and never reached the Main Menu */
        res = E_TRIED;
        goto out;
    }
#else
    if (be32(h + H_MAGIC) != MSCF_MAGIC || (be32(h + H_VERSION) != ((MSCF_VERSION << 16) | MSCF_ABI) && (!CL_COMP || be32(h + H_VERSION) != ((MSCF_VERSION_Z << 16) | MSCF_ABI)))) {
        res = E_HDR;
        goto out;
    }
#endif
    flags = be32(h + H_FLAGS); base = be32(h + H_HOLE_BASE); size = be32(h + H_HOLE_SIZE); len = be32(h + H_IMG_LEN);
    crc = be32(h + H_IMG_CRC); nseg = be32(h + H_NSEG); npatch = be32(h + H_NPATCH); entry = be32(h + H_ENTRY);
    ST[5] = base; ST[6] = len;
#if CL_COMP
    if (((be32(h + H_VERSION) >> 16) == MSCF_VERSION_Z) != ((flags & F_COMP) != 0)) {      /* header version 2 <=> F_COMP (ota_core.c and mscf_sign.py refuse the mixed forms too) */
        res = E_HDR;
        goto out;
    }
    if (flags & F_COMP) {
        zlen = be32(h + H_Z_LEN); zcrc = be32(h + H_Z_CRC);
        ST[24] = zlen;
    }
#endif
    if (nseg > 64 || npatch > 400 || H_SEGS + nseg * 16u + npatch * 8u > MSCF_BLOCK - MSCF_HDR_OFF) {
        res = E_HDR;
        goto out;
    }
    t = crc32(crc32(0, h, H_HDR_CRC), h + H_SEGS, nseg * 16u + npatch * 8u);
    if (t != be32(h + H_HDR_CRC)) {
        res = E_HDR;
        goto out;
    }
    /* SECURITY_REVIEW_2026-10-13 F1: no 32-bit wrap in base + size (this check runs BEFORE the signature check and the payload read lands at base / base+size-rlen) */
    if ((base & 31u) || base < CL_HOLE_MIN || base >= (u32) hb || len == 0 || size > (u32) hb - base || len > size ||
        (len + MSCF_BLOCK - 1u) / MSCF_BLOCK * MSCF_BLOCK > size) {
        res = E_RANGE;
        goto out;
    }
    rdst = base; rlen = (len + MSCF_BLOCK - 1u) / MSCF_BLOCK * MSCF_BLOCK;     /* where and how much the card read goes to */
#ifdef CL_PARTS
    rlen = cl_parts_p0(h, rlen);                 /* split file: part 0 carries only the first p0 blocks */
#endif
#if CL_COMP
    if (flags & F_COMP) {                              /* the stream sits at the tail of the hole, in whole blocks; decoding runs forward, in place */
        rlen = (zlen + MSCF_BLOCK - 1u) / MSCF_BLOCK * MSCF_BLOCK;
        if (zlen == 0 || zlen > size || rlen > size) {
            res = E_RANGE;
            goto out;
        }
        rdst = base + size - rlen;
    }
#endif
    /* guards first: every patch with an expected word must match, nothing is written otherwise */
    if (be32(h + H_GUARD_CRC)) {
        const u8* pt = h + H_SEGS + nseg * 16u;
        u32 g = 0;
        for (i = 0; i < be32(h + H_GUARD_N) && i < npatch; i++, pt += 8) {
            u32 ga = be32(pt), cur;
            u8 b[4];
            if (ga < 0x80000000u || ga >= 0x81800000u || (ga & 3u)) {   /* F13: the patch table is not authenticated yet: never read outside RAM */
                res = E_GUARD;
                goto out;
            }
            cur = W(ga);
            b[0] = (u8) (cur >> 24); b[1] = (u8) (cur >> 16); b[2] = (u8) (cur >> 8); b[3] = (u8) cur;
            g = crc32(g, b, 4);
        }
        ST[10] = g;
        if (g != be32(h + H_GUARD_CRC)) {
            res = E_GUARD;
            goto out;
        }
    }
#if defined(CL_TEST) && CL_TEST == 2          /* diagnostic build: never touch the heap bounds; CL_TEST 3: only arena_hi, 4: only AllM size */
    if (0) {
#else
    if (flags & F_CLAIM) {
#endif
        u32 hi = W(LBHEAP + 4u), st = W(allm + 8u), sz = W(allm + 0xCu);
        ST[11] = hi; ST[12] = st; ST[13] = sz; ST[14] = W(allm + 0x18u);
        if (hi < base + size || st + sz < base || st >= base || W(allm + 0x18u) != 1u) {
            res = E_ARENA;                       /* arena too small, or AllM currently created (a live heap must not lose its top) */
            goto out;
        }
        savedhi = hi; savedsz = sz; claimed = 1;
#if !defined(CL_TEST) || CL_TEST != 4
        W(LBHEAP + 4u) = base;                   /* the HSD main heap ends at the hole */
#endif
#if !defined(CL_TEST) || CL_TEST != 3
        W(allm + 0xCu) = base - st;              /* AllM ends at the hole (it is created at start, size at the next scene change) */
#endif
    }
    ST[1] = 4;
    dcache_drop(rdst, rlen);
#ifdef CL_CHUNK        /* diagnostic build: CL_CHUNK whole blocks per CARDRead, CL_DELAY retraces between reads, 3 tries per chunk; never a release */
    {
        u32 total = (len + MSCF_BLOCK - 1u) / MSCF_BLOCK * MSCF_BLOCK, off = 0, n, tries;
        r = 0;
        while (off < total && r == 0) {
            n = total - off < CL_CHUNK * MSCF_BLOCK ? total - off : CL_CHUNK * MSCF_BLOCK;
            tries = 0;
            do {
                r = CARDRead(&fi, (u8*) base + off, (s32) n, (s32) (MSCF_PAYLOAD_OFF + off));
                if (r != 0)
                    VIWaitForRetrace();
            } while (r != 0 && ++tries < 3);
            off += n;
#ifdef CL_DELAY
            {
                u32 d;
                for (d = 0; d < CL_DELAY; d++)
                    VIWaitForRetrace();
            }
#endif
        }
    }
#else
#if CL_COMP
    {
        u32 t0 = tb();
        r = CARDRead(&fi, (void*) rdst, (s32) rlen, (s32) MSCF_PAYLOAD_OFF);
        ST[25] = tb() - t0;
    }
#else
    r = CARDRead(&fi, (void*) rdst, (s32) rlen, (s32) MSCF_PAYLOAD_OFF);
#endif
#endif
#ifdef CL_PARTS
    if (r == 0 && cl_parts_read(chan, name, h, base, len) != 0) {
        res = E_PART;
        goto out;
    }
#endif
    if (r != 0) {
        res = E_READ;
        goto out;
    }
#if CL_COMP
    if (flags & F_COMP) {
        ms_u32 outn = 0;
        u32 t0 = tb(), t1, k;
        t = crc32(0, (const u8*) rdst, zlen);          /* the stream first: a bad read never reaches the decoder */
        t1 = tb();
        ST[26] = t1 - t0;
        if (t != zcrc) {
            ST[7] = t;
            res = E_CRC;
            goto out;
        }
#if CL_VERIFY
        {   /* COMBINED loader: the signature covers the STORED stream and is checked BEFORE the decoder sees a byte;
               the decoded image is then only a deterministic function of signed bytes (still length- and CRC-checked below) */
            u32 tv = tb(), v = verify_sig(h, rdst, zlen, nseg, npatch, 1);
            ST[19] = v;
            ST[21] = tb() - tv;
            if (v == 0) {
                res = E_SIG;
                goto out;
            }
        }
#endif
        if (ms_lz_decode((const ms_u8*) rdst, zlen, (ms_u8*) base, len, &outn) != MS_LZ_OK || outn != len) {
            res = E_DECODE;
            goto out;
        }
        ST[27] = tb() - t1;
        for (k = len; k < size; k++)                    /* what the stream left behind the image reads as zero, like the padding of an uncompressed file */
            ((volatile u8*) base)[k] = 0;
    }
#endif
#if CL_COMP
    {
        u32 t0 = tb();
        t = crc32(0, (const u8*) base, len);
        ST[28] = tb() - t0;
    }
#else
    t = crc32(0, (const u8*) base, len);
#endif
    ST[7] = t;
    if (t != crc) {
        res = E_CRC;
        goto out;
    }
#if CL_VERIFY
#if CL_COMP
    if (!(flags & F_COMP))      /* a compressed file was verified over its stream before decoding */
#endif
    {   /* nothing below runs or is applied before the signature is good; the padding after the image is zeroed (it is not signed) */
        u32 t1 = tb(), v = verify_sig(h, base, len, nseg, npatch, 0);
        ST[19] = v;
        ST[21] = tb() - t1;
        if (v == 0) {
            res = E_SIG;
            goto out;
        }
        for (i = len; i < (len + MSCF_BLOCK - 1u) / MSCF_BLOCK * MSCF_BLOCK; i++)
            ((volatile u8*) base)[i] = 0;
        flush_range(base + len, (len + MSCF_BLOCK - 1u) / MSCF_BLOCK * MSCF_BLOCK - len);
    }
#endif
    ST[1] = 5;
    {
        const u8* sg = h + H_SEGS;
        const u8* pt = sg + nseg * 16u;
        for (i = 0; i < nseg; i++, sg += 16) {          /* fixed-address copies out of the image */
            u32 dst = be32(sg), off = be32(sg + 4), n = be32(sg + 8), k;
            if (off + n > len) { res = E_RANGE; goto out; }
            for (k = 0; k < n; k++)
                ((volatile u8*) dst)[k] = ((const u8*) base)[off + k];
            flush_range(dst, n);
        }
        for (i = 0; i < npatch; i++, pt += 8) {
            u32 a = be32(pt);
            W(a) = be32(pt + 4);
            flush_range(a, 4);
        }
    }
    ST[9] = npatch;
    flush_range(base, len);
    ST[1] = 6;
out:
    if (res != E_OK && claimed) {                      /* undo the claim */
        W(LBHEAP + 4u) = savedhi;
        W(allm + 0xCu) = savedsz;
    }
    CARDClose(&fi);
#if CL_VERIFY
    run[0] = (res == E_OK && (flags & F_ENTRY)) ? entry : 0;
    run[1] = res == E_OK ? base : 0;
    return res;
}

/* the optional state file (MultishineState; block offset 0x1000, two records).  Floor record: 'MSST', floor u32, CRC-32 of those 8 bytes (written by nothing yet; the
   loader constant applies when it is missing or damaged).  Boot record at +16: 'MSBT', candidate build_seq u32, tries u16, good u16, CRC-32 of those 12 bytes (written by
   the updater and the engine, ota_core.c bootrec_*).  Returns the floor; *tried = the candidate's build_seq when it is exhausted (tries >= CL_TRIES and not good), else 0.  Missing or
   damaged records change nothing.  A forged record can only REFUSE a file (the floor check runs on the file that is left), never accept an older one. */
static u32 read_floor(s32 chan, u8* buf, u32* tried)
{
    struct card_fi fi;
    u32 f = 0;
    (void) tried;
    if (CARDOpen(chan, CL_STATE_NAME, &fi) != 0)
        return 0;
    dcache_drop((u32) buf, 512);
    if (CARDRead(&fi, buf, 512, MSCF_HDR_OFF) == 0) {
        if (be32(buf) == 0x4D535354u && crc32(0, buf, 8) == be32(buf + 8))
            f = be32(buf + 4);
#if CL_TRIES
        if (be32(buf + 16) == 0x4D534254u && crc32(0, buf + 16, 12) == be32(buf + 28)) {
            u32 w = be32(buf + 24);
            ST[29] = be32(buf + 20); ST[30] = w;
            if ((w & 0xFFFFu) == 0 && (w >> 16) >= CL_TRIES)
                *tried = be32(buf + 20);
        }
#endif
    }
    CARDClose(&fi);
    return f;
}

static u32 load_from(s32 chan)
{
    static const char* const names[2] = { CL_FILE_NAME, CL_FILE_NAME "B" };
    u8* wa = (u8*) CL_HOLE_MAX - CL_WA_SIZE;
    u32 run[2] = { 0, 0 }, floor, f, k, res = E_NOFILE, r, tried = 0, safe = 0;
    s32 m;
    ST[3] = (u32) chan;
    m = card_mount(chan, wa);
    ST[4] = (u32) m;
    if (m != 0)
        return m == -3 ? E_NOCARD : E_MOUNT;
    floor = CL_MIN_SEQ;
    f = read_floor(chan, wa - MSCF_BLOCK, &tried);
    ST[17] = f;
    ST[23] = tried;
    if (f > floor)
        floor = f;
    ST[16] = 1u | (CL_DEV ? 2u : 0u);
#if CL_SAFE
    for (k = 0; k < 4; k++)                            /* L+R held on any port (HSD_PadMasterStatus[k].button: L 0x40, R 0x20): safe mode */
        if ((W(0x804C1FACu + 0x44u * k) & 0x60u) == 0x60u)
            safe = 1;
    ST[31] = safe;
#endif
    for (k = 0; k < 2; k++) {                          /* the active file, then the previous one: a bad update never costs the old code */
        r = (k == 0 && safe) ? E_SAFE : load_one(chan, names[k], floor, tried, run);
        ST[22] = (ST[22] << 8) | r;
        if (r == E_OK) {
            ST[20] = k;
            res = E_OK;
            break;
        }
        if (k == 0 || res == E_NOFILE)                 /* report the primary's reason, unless it was simply absent */
            res = r;
    }
    CARDUnmount(chan);
    if (res == E_OK && run[0])
        ((fn_entry) run[0])((void*) run[1]);
    return res;
}
#else
    CARDUnmount(chan);
    if (res == E_OK && (flags & F_ENTRY) && entry)
        ((fn_entry) entry)((void*) base);
    return res;
}
#endif

void cl_main(void)
{
#if defined(CL_TEST) && CL_TEST == 1          /* diagnostic build: no card access at all (isolates the entry / handler interplay) */
    ST[0] = 0x434C0001u;
    ST[2] = E_OK;
    ST[1] = 7;
    return;
#endif
    u32 t0 = tb(), res = E_NOCARD, r[2] = {E_NOCARD, E_NOCARD}, chan, i;
#if CL_MUTE
    u32 m0 = W(MU_LIVE), m1 = W(MU_SYN), m2 = W(MU_LAST);
    mu_set(0, 0, 0);                                   /* first: the track that plays is silent within one AX frame */
    ST[32] = 0;
#endif
    for (i = 0; i < ((CL_VERIFY || CL_COMP) ? 32u : 16u); i++)
        ST[i] = 0;
    ST[0] = 0x434C0001u;
    ST[1] = 1;
    for (chan = 0; chan < 2; chan++) {
        r[chan] = load_from((s32) chan);
        if (r[chan] == E_OK)
            break;
    }
    /* the most informative failure: a card that answered beats "no card" / "no file" */
    res = r[0];
    if (res != E_OK && (res == E_NOCARD || res == E_NOFILE) && r[1] != E_NOCARD)
        res = r[1];
    if (r[1] == E_OK)
        res = E_OK;
    ST[15] = (r[0] << 8) | r[1];
    ST[2] = res;
    ST[8] = tb() - t0;
#if CL_MUTE
    if (res != E_OK || ST[18] < CL_MUTE_SEQ) {        /* no engine, or one that does not restore the music (ST[18] = build_seq of the file that loaded) */
        mu_set(m0, m1, m2);
    } else {                                           /* the hand-over to the engine (hub_res.c lm_tick): the live word it had, then the mark */
        ST[33] = m0;
        ST[32] = CL_MUTE_MARK;
    }
#endif
    ST[1] = res == E_OK ? 7 : 0x80;
}
