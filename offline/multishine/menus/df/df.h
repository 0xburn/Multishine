/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * df: the DISC-FILE PACK.  Serves the changed or added disc files of the RC1 feature set from a memory-card file
 * ("MultishinePack") so that the game disc can stay stock.  Three hooks on Melee's file funnel (df_stub.S):
 *   H1 DVDConvertPathToEntrynum 0x8033796C   name -> entry:        a name in the pack gets the SYNTHETIC entry DF_SYN_BASE + k (no FST entry is needed)
 *   H2 DVDFastOpen              0x80337C60   entry -> size:         a synthetic entry gets a DVDFileInfo with the pack's new size (all inlined size reads end here)
 *   H3 HSD_DevComRequest        0x8038F6D4   entry -> data:         a synthetic entry: the bytes are produced into `dest` right here (caller's context, interrupts on),
 *                                              then the request is turned into a 32-byte read of a real file so that the callback and the queue run as always
 * df_core.c (this header) is plain C with no SDK call: the host tests (tests/df_host.c) run it against packs made by tools/card/build_pack.py.
 *
 * TWO FILES: MultishinePack = entries derived from the user's disc / Slippi install (patcher-built, NEVER delivered by OTA); MultishineOvl = our own files
 * (ms/sign.ovl and later our own strings), a signed MSCF-style container (ABI 2, flag 8) the OTA may replace: its payload block 0 holds the same header + TOC at offset 0, its data blocks follow;
 * the engine accepts only names starting "ms/" from it, so an OTA can never shadow a derived file.  Both use the entry format below.
 * PACK FILE (a GCI "MultishinePack"; every offset is a multiple of 0x2000 where data starts):
 *   block 0   0x0000..0x003F comment strings, 0x0040 icon (as every Multishine file); 0x1000 header: 'MSPK', u32 version (1), u32 count, u32 file_len, u32 toc_crc32
 *             (CRC-32 of the count*64 entry bytes), 12 reserved; 0x1020 count entries of 64 bytes (struct below, big endian)
 *   data      each entry's stored bytes at data_off (block aligned), stored_len bytes
 *   3 DELTA  against the retail disc file (guard: old_size and crc_old): stored = MSLZ stream (decoded in place at offset `gap` of a scratch block) of an op stream of delta_len (= p_pos in
 *            the file) bytes, tools/card/mdelta.py (ADD literals / COPY ranges of the retail file / RUN).  The retail file is read into scratch memory (io->alloc, the HSD heap on the console), the new
 *            file is built in dest from it, the scratch is freed.  If the scratch cannot be allocated the entry fails (-14): measure first, keep the whole-file mode as the fallback.
 * Entry modes (all produce exactly new_size bytes at dest; dest is the caller's buffer of ROUND_UP_32(new_size)):
 *   0 RAW    stored = the file.
 *   1 MSLZ   stored = MSLZ stream of the file (tools/payload_pack/mslz.py); read to dest + gap, decoded in place (gap from the packer's walk of the stream, as mscf_z.py).
 *   4 DELTA_IP  as DELTA but in place (no retail-sized scratch, only the op stream's): the retail file is read into dest + shift (shift = ROUND_UP_32(new) - ROUND_UP_32(old), so the new file is not smaller),
 *            the ops run forward over it: every COPY must come from at or after the end of the previous one, no write may reach the unread source (the packer simulates, the console refuses otherwise).
 *            This is what MnExtAll.usd uses on the online CSS, where the heap has no room for a 1.3 MB scratch.
 */
#ifndef DF_H
#define DF_H

typedef unsigned int df_u32;
typedef unsigned char df_u8;

#define DF_MAGIC 0x4D53504Bu          /* 'MSPK' */
#define DF_VERSION 1u
#define DF_BLOCK 0x2000u
#define DF_OVL_SLACK 32u              /* extra buffer bytes the overlay loaders (ml_glue.c sd_open, hub_res.c) pass df_produce beyond rup32(new_size): an MSLZ entry whose size is a multiple of 32 needs gap + stream >= new_size + 1 */
#define DF_FILE_MAX 0x4000000u        /* no file, delta or retail size beyond 64 MiB is accepted (all sums then stay far below 2^32) */
#define DF_HDR_OFF 0x1000u
#define DF_TOC_OFF (DF_HDR_OFF + 0x20u)
#define DF_ENT_SIZE 64u
#ifdef MS_REP_OVL
#define DF_MAX 12u                    /* entries the engine keeps (12 * 64 B = 768 B): the stock pack's five plus the overlay container's FIVE (ms/hub.ovl, ms/sign.ovl, ms/gns.ovl, ms/menu.ovl, ms/rep.ovl) = 10, two spare; only with repovl=on, so the other builds stay byte for byte as they were */
#else
#define DF_MAX 10u                    /* entries the engine keeps (10 * 64 B = 640 B): the stock pack's five entries plus the overlay container's four (ms/hub.ovl, ms/sign.ovl, ms/gns.ovl, ms/menu.ovl) = 9, one spare; with too few slots the container read FIRST (ours) leaves the pack too few, its count of 5 is rejected (-2) and the whole stock pack is ignored (labels_fix, DF_MAX 6; 8 was exactly full with the three overlays, ms/menu.ovl made it 9:  6.4.4) */
#endif
#define DF_NAME_MAX 24u
#define DF_SYN_BASE 0x4000            /* synthetic entry numbers: beyond any FST entry of a Melee disc (about 800) */
#define DF_PASS (-2)
#define DF_E_BASE (-16)               /* DELTA / DELTA_IP: the disc file is neither the stock base nor the intended result (dest then holds the disc file's own bytes, zero padded) */
#define DF_OK_BAKED 1                 /* df_produce: success because the disc file already IS the intended result (CRC equal): nothing was applied */

/* BASE-AWARE: a DELTA entry carries crc_old (the stock base) and crc_new (the intended result) and both sizes; the state of the
 * DISC's file is classified by its length first (df_classify, no disc access: the FST is in RAM) and by its CRC when it is read (df_produce):
 *   STOCK    length == old_size and CRC == crc_old        -> the delta is applied
 *   PATCHED    CRC == crc_new (length == new_size)          -> nothing to apply, the disc bytes are the result (DF_OK_BAKED)
 *   UNKNOWN  anything else                                -> a FULL entry of the same name (RAW / MSLZ, listed after the primary entries) is used when the pack has one, else the disc's own file is served
 *            (df_path passes it through at once when the length matches neither; DF_E_BASE when only the CRC disagrees); the pack is never reported damaged for it.
 * The format is unchanged (version 1): the fields were always there.  Two entries may share a name; df_find returns the first, df_find_alt the next FULL one. */
enum { DF_CL_FULL = 0, DF_CL_STOCK, DF_CL_BAKED, DF_CL_BOTH, DF_CL_UNKNOWN };

enum { DF_RAW = 0, DF_MSLZ = 1, DF_PATCH_UNUSED = 2 /* retired: in-place edits are DELTA_IP now */, DF_DELTA = 3, DF_DELTA_IP = 4 };

struct df_ent {                       /* the in-RAM copy: 64 bytes, native order after df_toc_parse; the file holds the same fields big endian */
    char name[DF_NAME_MAX];           /* 0x00 */
    df_u32 mode;                      /* 0x18 */
    df_u32 new_size;                  /* 0x1C */
    df_u32 old_size;                  /* 0x20 retail size (DELTA / DELTA_IP), else 0 */
    df_u32 stored_len;                /* 0x24 */
    df_u32 data_off;                  /* 0x28 file offset of the stored bytes (multiple of DF_BLOCK) */
    df_u32 gap;                       /* 0x2C MSLZ: offset of the stream inside dest */
    df_u32 crc_new;                   /* 0x30 CRC-32 of the produced file */
    df_u32 crc_old;                   /* 0x34 CRC-32 of the retail file (DELTA / DELTA_IP): checked on the console before the delta is applied */
    df_u32 p_pos;                     /* 0x38 DELTA: length of the decoded op stream (else 0); the console keeps the real disc entry in a separate array */
    df_u32 p_old;                     /* 0x3C CRC-32 of the stored bytes (0 = not recorded): the engine drops an entry whose stored bytes do not match */
};

/* IO the caller provides (console: card reads / DevCom reads; host: files) */
struct df_io {
    /* `len` bytes of the pack file from `off` (off is a multiple of DF_BLOCK) into dst; 0 = ok */
    int (*read_pack)(void *ctx, df_u32 off, df_u8 *dst, df_u32 len);
    /* the retail disc file of entry e (ROUND_UP_32(old_size) bytes) into dst; 0 = ok */
    int (*read_retail)(void *ctx, const struct df_ent *e, df_u8 *dst, df_u32 len);
    /* DELTA: scratch memory (0 = none available) and its release */
    void *(*alloc)(void *ctx, df_u32 n);
    void (*release)(void *ctx, void *p);
    /* DELTA: the length of the disc's file (0 = the callback is absent: old_size is assumed, as before the base-aware decoder) */
    df_u32 (*retail_len)(void *ctx, const struct df_ent *e);
    void *ctx;
};

df_u32 df_crc32(const df_u8 *p, df_u32 n, df_u32 crc);
/* parse a block of DF_BLOCK bytes whose pack header sits at hdr_off (DF_HDR_OFF in block 0 of MultishinePack; 0 in the first payload block of the signed MultishineOvl container) into tab[]; (file_len = the length of the card file: every offset and stored length must lie inside it)  -> entry count (>= 0) or a negative error (-1 magic/version, -2 count, -3 toc crc, -4 entry fields) */
int df_toc_parse(const df_u8 *blk, struct df_ent *tab, int max, df_u32 hdr_off, df_u32 file_len);
/* index of the entry for a DVD path (a leading '/' is ignored, exact match otherwise), -1 when none */
int df_find(const struct df_ent *tab, int n, const char *path);
/* DF_CL_* of an entry against the length of the disc's file (rl; a non-DELTA entry is DF_CL_FULL; BOTH = both sizes are equal, the CRC decides) */
int df_classify(const struct df_ent *e, df_u32 rl);
/* the next entry after k with the same name that is a FULL file (RAW / MSLZ), -1 when none */
int df_find_alt(const struct df_ent *tab, int n, int k);
/* produce the file into dest (cap = ROUND_UP_32(new_size)); 0 = ok (applied), DF_OK_BAKED = ok (the disc file already was the result), else a negative error (-10 io, -11 geometry, -12 decode, -13 crc, -14 no scratch, DF_E_BASE unknown base) */
int df_produce(const struct df_ent *e, const struct df_io *io, df_u8 *dest, df_u32 cap);

#endif
