/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* OTA installer core: see ota_core.h and. */
#include "ota_core.h"

static ota_u32 be32(const mw2_u8* p) { return ((ota_u32) p[0] << 24) | ((ota_u32) p[1] << 16) | ((ota_u32) p[2] << 8) | p[3]; }

static ota_u32 crc32(ota_u32 crc, const mw2_u8* p, ota_u32 n)
{
    crc = ~crc;
    while (n--) {
        int i;
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (ota_u32) -(long) (crc & 1u));
    }
    return ~crc;
}

/* Walks an MSLZ stream exactly like offline/payload_pack/ms_lz.c ms_lz_decode (same token rules, same bounds) without writing anything.  Returns 1 when the stream
 * is well formed and decodes to exactly outlen bytes; *gap = the smallest in-place margin (offset of the stream above the destination) that never overwrites an
 * unread stream byte (tools: offline/cardloader/mscf_z.py inplace_gap). */
static int lz_walk(const mw2_u8* z, ota_u32 n, ota_u32 outlen, ota_u32* gap)
{
    ota_u32 ip = 0, op = 0, len, off, need = 0;
    mw2_u8 tok, b;
    for (;;) {
        if (ip >= n)
            return 0;
        tok = z[ip++];
        len = tok >> 4;
        if (len == 15) {
            do {
                if (ip >= n)
                    return 0;
                b = z[ip++];
                len += b;
                if (len > outlen)
                    return 0;
            } while (b == 255);
        }
        if (len > n - ip || len > outlen - op)
            return 0;
        if (op > ip && op - ip > need)
            need = op - ip;                         /* literal copy: write at op+i after reading ip+i */
        ip += len; op += len;
        if (ip == n) {
            *gap = need;
            return op == outlen;
        }
        if (n - ip < 2)
            return 0;
        off = (ota_u32) z[ip] | ((ota_u32) z[ip + 1] << 8);
        ip += 2;
        if (off == 0 || off > op)
            return 0;
        len = tok & 15u;
        if (len == 15) {
            do {
                if (ip >= n)
                    return 0;
                b = z[ip++];
                len += b;
                if (len > outlen)
                    return 0;
            } while (b == 255);
        }
        len += 4;
        if (len > outlen - op)
            return 0;
        if (op + len > ip && op + len - ip > need)
            need = op + len - ip;                   /* match copy: every unread byte lies at >= ip */
        op += len;
    }
}

int ota_verify(const mw2_u8* file, ota_u32 n, const struct ota_policy* pol, ota_u32* seq)
{
    const mw2_u8* h;
    ota_u32 len, nseg, npatch, ntab, s, ver, size, zlen = 0, gap = 0;
    unsigned v;
    int comp;
    if (n < 2 * OTA_BLOCK || (n % OTA_BLOCK))
        return OTA_E_HDR;
    h = file + 0x1000;
    ver = be32(h + 4) >> 16;
    comp = (ver == 2u);
    if (be32(h) != 0x4D534346u || (ver != 1u && ver != 2u) || comp != ((be32(h + 0x0C) & 4u) != 0))
        return OTA_E_HDR;
    if ((be32(h + 4) & 0xFFFFu) != pol->abi)
        return OTA_E_ABI;
    s = be32(h + 8);
    if (s < pol->min_seq)
        return OTA_E_SEQ;
    nseg = be32(h + 0x20); npatch = be32(h + 0x24); len = be32(h + 0x18); size = be32(h + 0x14);
    if (nseg > 64 || npatch > 400 || 0x80u + nseg * 16u + npatch * 8u > OTA_BLOCK - 0x1000u)
        return OTA_E_HDR;
    ntab = nseg * 16u + npatch * 8u;
    if (crc32(crc32(0, h, 0x2C), h + 0x80, ntab) != be32(h + 0x2C))
        return OTA_E_HDR;
    if (comp) {
        zlen = be32(h + 0x38);
        /* the loader's E_RANGE rules, and the stream must fill the file's payload blocks exactly */
        if (len == 0 || (len + OTA_BLOCK - 1u) / OTA_BLOCK * OTA_BLOCK > size || zlen == 0 || zlen > size || zlen > n - OTA_BLOCK ||
            (zlen + OTA_BLOCK - 1u) / OTA_BLOCK * OTA_BLOCK != n - OTA_BLOCK)
            return OTA_E_RANGE;
        if (crc32(0, file + 0x2000, zlen) != be32(h + 0x3C))
            return OTA_E_CRC;
        v = mscf_verify_sig(h, h + 0x80, ntab, file + 0x2000, zlen, pol->pubs, pol->npub, pol->allow_unsigned, 1);
        if (v == 0)
            return OTA_E_SIG;
        /* only now, with the stream authenticated, is it parsed (still without writing) */
        if (!lz_walk(file + 0x2000, zlen, len, &gap) || gap > size - (zlen + OTA_BLOCK - 1u) / OTA_BLOCK * OTA_BLOCK)
            return OTA_E_DECODE;
        if (seq)
            *seq = s;
        return OTA_OK;
    }
    if (len == 0 || len > n - OTA_BLOCK || (len + OTA_BLOCK - 1u) / OTA_BLOCK * OTA_BLOCK != n - OTA_BLOCK)
        return OTA_E_RANGE;
    if (crc32(0, file + 0x2000, len) != be32(h + 0x1C))
        return OTA_E_CRC;
    v = mscf_verify_sig(h, h + 0x80, ntab, file + 0x2000, len, pol->pubs, pol->npub, pol->allow_unsigned, 0);
    if (v == 0)
        return OTA_E_SIG;
    if (seq)
        *seq = s;
    return OTA_OK;
}

/* write the file to `name` (created fresh), read it back block by block and compare; returns OTA_OK / OTA_E_SPACE / OTA_E_IO / OTA_E_VERIFY */
static int stage_once(const struct ota_card_ops* ops, const char* name, const mw2_u8* file, ota_u32 n, mw2_u8* scratch)
{
    ota_u32 off, k;
    int r = ops->create(ops->ctx, name, n / OTA_BLOCK);
    if (r == 1)
        return OTA_E_SPACE;
    if (r != 0)
        return OTA_E_IO;
    if (ops->write(ops->ctx, name, 0, file, n) != 0)
        return OTA_E_IO;
    for (off = 0; off < n; off += OTA_BLOCK) {
        if (ops->read(ops->ctx, name, off, scratch, OTA_BLOCK) != 0)
            return OTA_E_IO;
        for (k = 0; k < OTA_BLOCK; k++)
            if (scratch[k] != file[off + k])
                return OTA_E_VERIFY;
    }
    return OTA_OK;
}

int ota_install_verified(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                         const mw2_u8* file, ota_u32 n, mw2_u8* scratch)
{
    int r, tries, had_active;
    if (ops->del(ops->ctx, previous) || ops->del(ops->ctx, staging))
        return OTA_E_IO;                            /* the previous copy is dropped first: the card never holds three versions */
    for (tries = 0; tries < 2; tries++) {           /* one retry: a bit error on the card must not cost the update */
        r = stage_once(ops, staging, file, n, scratch);
        if (r == OTA_OK)
            break;
        ops->del(ops->ctx, staging);
        if (r != OTA_E_VERIFY)
            return r;
    }
    if (r != OTA_OK)
        return r;
    had_active = ops->exists(ops->ctx, active);
    if (had_active && ops->rename(ops->ctx, active, previous)) {
        ops->del(ops->ctx, staging);
        return OTA_E_IO;
    }
    if (ops->rename(ops->ctx, staging, active)) {
        /* Active is absent now, but the previous file holds the old code and the loader falls back to it.  Put the old code back under its own name as well (best effort:
           a failure here leaves exactly that fallback state), and drop the staging file so that the card is as it was.  The update is reported as failed. */
        if (had_active && ops->rename(ops->ctx, previous, active) == 0)
            ops->del(ops->ctx, staging);
        return OTA_E_IO;
    }
    return OTA_OK;
}

int ota_reinstall_verified(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                           const mw2_u8* file, ota_u32 n, mw2_u8* scratch)
{
    int r = OTA_E_IO, tries;
    if (!ops->exists(ops->ctx, previous)) {         /* no fallback to protect: the normal install, or in place on a card with room for one copy only */
        r = ota_install_verified(ops, active, previous, staging, file, n, scratch);
        return r == OTA_E_SPACE ? ota_install_inplace_verified(ops, active, previous, staging, file, n, scratch) : r;
    }
    if (ops->del(ops->ctx, staging))
        return OTA_E_IO;
    for (tries = 0; tries < 2; tries++) {
        r = stage_once(ops, staging, file, n, scratch);
        if (r == OTA_OK)
            break;
        ops->del(ops->ctx, staging);
        if (r != OTA_E_VERIFY)
            break;
    }
    if (r == OTA_OK) {
        if (ops->del(ops->ctx, active)) {
            ops->del(ops->ctx, staging);
            return OTA_E_IO;
        }
        if (ops->rename(ops->ctx, staging, active) && ops->rename(ops->ctx, staging, active))
            return OTA_E_IO;                        /* the fallback boots; the complete staging file stays and is replaced by the next install */
        return OTA_OK;
    }
    if (r != OTA_E_SPACE)
        return r;                                   /* nothing of the old files was touched */
    if (ops->del(ops->ctx, active))                 /* no room for a second copy: rewrite the active file in place, the fallback keeps every cut bootable */
        return OTA_E_IO;
    for (tries = 0; tries < 2; tries++) {
        r = stage_once(ops, active, file, n, scratch);
        if (r == OTA_OK)
            return OTA_OK;
        ops->del(ops->ctx, active);
        if (r != OTA_E_VERIFY)
            break;
    }
    return r;
}

int ota_plan(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging, ota_u32 n)
{
    int f;
    if (!ops->free_blocks)
        return OTA_TIER_STAGED;
    f = ops->free_blocks(ops->ctx, previous, staging, 0);
    if (f < 0)
        return OTA_TIER_STAGED;                     /* cannot tell: the staged install reports OTA_E_SPACE itself, the old file untouched */
    if ((ota_u32) f >= n / OTA_BLOCK)
        return OTA_TIER_STAGED;
    f = ops->free_blocks(ops->ctx, previous, staging, active);
    return f >= 0 && (ota_u32) f >= n / OTA_BLOCK ? OTA_TIER_INPLACE : OTA_TIER_NOROOM;
}

int ota_install_inplace_verified(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                                 const mw2_u8* file, ota_u32 n, mw2_u8* scratch)
{
    int r = OTA_E_IO, tries;
    if (ops->del(ops->ctx, previous) || ops->del(ops->ctx, staging))
        return OTA_E_IO;                            /* nothing important was lost yet: only the spare copies */
    if (ops->del(ops->ctx, active))
        return OTA_E_IO;                            /* the failed delete left the old file (or not: unknown, so no stronger claim than "card unchanged" is made for the active file) */
    for (tries = 0; tries < 2; tries++) {
        r = stage_once(ops, active, file, n, scratch);
        if (r == OTA_OK)
            return OTA_OK;
        ops->del(ops->ctx, active);
        if (r != OTA_E_VERIFY)
            break;
    }
    return OTA_E_LOST;
}

int ota_install(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                const mw2_u8* file, ota_u32 n, const struct ota_policy* pol, mw2_u8* scratch)
{
    int r = ota_verify(file, n, pol, 0);
    if (r != OTA_OK)
        return r;                                   /* nothing on the card was touched */
    return ota_install_verified(ops, active, previous, staging, file, n, scratch);
}

/* ---- bundle: engine + overlay as one release (ota_core.h) ---- */
static int stage_retry(const struct ota_card_ops* ops, const char* name, const mw2_u8* file, ota_u32 n, mw2_u8* scratch)
{
    int r = OTA_E_IO, t;
    for (t = 0; t < 2; t++) {                       /* one retry: a bit error on the card must not cost the update */
        r = stage_once(ops, name, file, n, scratch);
        if (r == OTA_OK)
            return OTA_OK;
        ops->del(ops->ctx, name);
        if (r != OTA_E_VERIFY)
            break;
    }
    return r;
}

int ota_bundle_plan(const struct ota_card_ops* ops, const struct ota_bundle* b, struct ota_bplan* out)
{
    int f0, fc, fo, oa, ca = 0;
    ota_u32 f1, oold, cold, cn, on, s1, s2;
    out->shortfall = out->free_blocks = out->ovl_old = 0;
    if (!ops->free_blocks)
        return OTA_PLAN_A;
    f0 = ops->free_blocks(ops->ctx, 0, 0, 0);
    fc = ops->free_blocks(ops->ctx, b->keep_code_prev ? 0 : b->code_prev, b->code_stage, 0);
    fo = ops->free_blocks(ops->ctx, b->ovl_prev, b->ovl_stage, 0);
    oa = ops->free_blocks(ops->ctx, 0, 0, b->ovl);
    if (b->keep_code_prev)
        ca = ops->free_blocks(ops->ctx, 0, 0, b->code);
    if (f0 < 0 || fc < 0 || fo < 0 || oa < 0 || ca < 0)
        return OTA_PLAN_A;                          /* cannot tell */
    f1 = (ota_u32) fc + (ota_u32) fo - (ota_u32) f0;
    oold = (ota_u32) oa - (ota_u32) f0;
    cold = b->keep_code_prev ? (ota_u32) ca - (ota_u32) f0 : 0;
    cn = b->code_n / OTA_BLOCK; on = b->ovl_n / OTA_BLOCK;
    out->free_blocks = f1; out->ovl_old = oold;
    if (f1 >= cn + on)
        return OTA_PLAN_A;
    if (f1 >= on && f1 - on + oold + cold >= cn)
        return OTA_PLAN_B;
    s1 = on > f1 ? on - f1 : 0;
    s2 = cn + on > f1 + oold + cold ? cn + on - f1 - oold - cold : 0;
    out->shortfall = s1 > s2 ? s1 : s2;
    if (out->shortfall == 0)
        out->shortfall = 1;
    return OTA_PLAN_C;
}

static int bundle_drop(const struct ota_card_ops* ops, const struct ota_bundle* b)
{
    if (ops->del(ops->ctx, b->ovl_prev) || ops->del(ops->ctx, b->ovl_stage) || ops->del(ops->ctx, b->code_stage))
        return OTA_E_IO;
    if (!b->keep_code_prev && ops->del(ops->ctx, b->code_prev))
        return OTA_E_IO;                            /* the spare copies go first: the card never holds three versions */
    return OTA_OK;
}

/* plan A, after the overlay swap: the new overlay is `ovl`, the old one (if there was one) `ovl_prev`; put the old one back under its name */
static void bundle_undo_ovl(const struct ota_card_ops* ops, const struct ota_bundle* b, int o_had)
{
    ops->del(ops->ctx, b->ovl);
    if (o_had && ops->rename(ops->ctx, b->ovl_prev, b->ovl))
        ops->rename(ops->ctx, b->ovl_prev, b->ovl);
}

int ota_bundle_install(const struct ota_card_ops* ops, const struct ota_bundle* b, int plan, const mw2_u8* code_file, const mw2_u8* ovl_file,
                       mw2_u8* scratch, mw2_u8* restore, ota_u32 restore_cap)
{
    int r, o_had, c_had;
    r = bundle_drop(ops, b);
    if (r != OTA_OK)
        return r;
    o_had = ops->exists(ops->ctx, b->ovl);
    c_had = ops->exists(ops->ctx, b->code);
    if (plan == OTA_PLAN_B) {
        ota_u32 rbytes = 0;
        int have_rest = 0;
        if (o_had && restore && ops->free_blocks) {
            int a = ops->free_blocks(ops->ctx, 0, 0, b->ovl), z = ops->free_blocks(ops->ctx, 0, 0, 0);
            if (a > z && z >= 0) {
                rbytes = (ota_u32) (a - z) * OTA_BLOCK;
                if (rbytes <= restore_cap && ops->read(ops->ctx, b->ovl, 0, restore, rbytes) == 0)
                    have_rest = 1;
            }
        }
        r = stage_retry(ops, b->ovl_stage, ovl_file, b->ovl_n, scratch);
        if (r != OTA_OK)
            return r;                               /* the old pair is whole; the spare copies are gone */
        if (o_had && ops->del(ops->ctx, b->ovl)) {
            ops->del(ops->ctx, b->ovl_stage);
            return OTA_E_IO;
        }
        if (ops->rename(ops->ctx, b->ovl_stage, b->ovl) && ops->rename(ops->ctx, b->ovl_stage, b->ovl)) {
            ops->del(ops->ctx, b->ovl_stage);
            if (have_rest && stage_retry(ops, b->ovl, restore, rbytes, scratch) == OTA_OK)
                return OTA_E_IO;
            return OTA_E_HALF;
        }
        r = b->keep_code_prev ? ota_reinstall_verified(ops, b->code, b->code_prev, b->code_stage, code_file, b->code_n, scratch)
                              : ota_install_verified(ops, b->code, b->code_prev, b->code_stage, code_file, b->code_n, scratch);
        if (r == OTA_OK)
            return OTA_OK;
        ops->del(ops->ctx, b->code_stage);
        if (have_rest) {                            /* power on: write the old overlay back, the old pair is whole again */
            ops->del(ops->ctx, b->ovl);
            if (stage_retry(ops, b->ovl, restore, rbytes, scratch) == OTA_OK)
                return r;
        }
        return OTA_E_HALF;
    }
    /* plan A: both staged first, then four directory operations */
    r = stage_retry(ops, b->ovl_stage, ovl_file, b->ovl_n, scratch);
    if (r != OTA_OK)
        return r;
    r = stage_retry(ops, b->code_stage, code_file, b->code_n, scratch);
    if (r != OTA_OK) {
        ops->del(ops->ctx, b->ovl_stage);
        return r;
    }
    if (o_had && ops->rename(ops->ctx, b->ovl, b->ovl_prev)) {
        ops->del(ops->ctx, b->ovl_stage); ops->del(ops->ctx, b->code_stage);
        return OTA_E_IO;
    }
    if (ops->rename(ops->ctx, b->ovl_stage, b->ovl)) {
        if (o_had)
            ops->rename(ops->ctx, b->ovl_prev, b->ovl);
        ops->del(ops->ctx, b->ovl_stage); ops->del(ops->ctx, b->code_stage);
        return OTA_E_IO;
    }
    if (c_had && (b->keep_code_prev ? ops->del(ops->ctx, b->code) : ops->rename(ops->ctx, b->code, b->code_prev))) {
        bundle_undo_ovl(ops, b, o_had);
        ops->del(ops->ctx, b->code_stage);
        return OTA_E_IO;
    }
    if (ops->rename(ops->ctx, b->code_stage, b->code) && ops->rename(ops->ctx, b->code_stage, b->code)) {
        if (c_had && !b->keep_code_prev)
            ops->rename(ops->ctx, b->code_prev, b->code);
        bundle_undo_ovl(ops, b, o_had);
        ops->del(ops->ctx, b->code_stage);
        return OTA_E_IO;
    }
    return OTA_OK;
}

/* ---- boot record ---- */
static void wr32(mw2_u8* p, ota_u32 v) { p[0] = (mw2_u8) (v >> 24); p[1] = (mw2_u8) (v >> 16); p[2] = (mw2_u8) (v >> 8); p[3] = (mw2_u8) v; }

static void parse(const mw2_u8* blk, struct bootrec* r)
{
    const mw2_u8* p = blk + BR_OFF + BR_REC;
    r->cand = r->tries = r->good = 0; r->valid = 0;
    if (be32(p) == 0x4D534254u && crc32(0, p, 12) == be32(p + 12)) {        /* 'MSBT' */
        ota_u32 w = be32(p + 8);
        r->cand = be32(p + 4); r->tries = w >> 16; r->good = w & 0xFFFFu; r->valid = 1;
    }
}

static void put(mw2_u8* blk, ota_u32 cand, ota_u32 tries, ota_u32 good)
{
    mw2_u8* p = blk + BR_OFF + BR_REC;
    wr32(p, 0x4D534254u); wr32(p + 4, cand); wr32(p + 8, (tries << 16) | good);
    wr32(p + 12, crc32(0, p, 12));
}

int bootrec_read(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, struct bootrec* r)
{
    r->cand = r->tries = r->good = 0; r->valid = 0;
    if (!ops->exists(ops->ctx, state))
        return BR_NOROOM;
    if (ops->read(ops->ctx, state, 0, blk, OTA_BLOCK) != 0)
        return BR_IO;
    parse(blk, r);
    return BR_OK;
}

int bootrec_exhausted(const struct bootrec* r, ota_u32 seq, ota_u32 limit)
{
    return r->valid && r->cand == seq && !r->good && r->tries >= limit;
}

/* the block to modify: the file's own, or a fresh one (comment lines like mscf_sign.make_state) when the file has to be created; a creation that finds no room is BR_NOROOM */
static int open_block(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, int create)
{
    ota_u32 i;
    if (ops->exists(ops->ctx, state))
        return ops->read(ops->ctx, state, 0, blk, OTA_BLOCK) == 0 ? BR_OK : BR_IO;
    if (!create)
        return BR_NOROOM;
    if (ops->free_blocks && ops->free_blocks(ops->ctx, 0, 0, 0) == 0)       /* the card layer counts what Melee's own save still needs (slot A, save not made yet): this block must not eat it */
        return BR_NOROOM;
    for (i = 0; i < OTA_BLOCK; i++)
        blk[i] = 0;
    {
        static const char c1[] = "Multishine", c2[] = "Build state";
        for (i = 0; c1[i]; i++) blk[i] = (mw2_u8) c1[i];
        for (i = 0; c2[i]; i++) blk[32 + i] = (mw2_u8) c2[i];
    }
    i = (ota_u32) ops->create(ops->ctx, state, 1);
    return i == 0 ? BR_OK : i == 1 ? BR_NOROOM : BR_IO;
}

/* write the block with the new record, read it back into the same buffer and compare the 16 record bytes (kept aside); one retry */
static int write_rec(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, ota_u32 cand, ota_u32 tries, ota_u32 good)
{
    mw2_u8 want[16];
    ota_u32 k;
    int t;
    put(blk, cand, tries, good);
    for (k = 0; k < 16; k++)
        want[k] = blk[BR_OFF + BR_REC + k];
    for (t = 0; t < 2; t++) {
        int bad = 0;
        if (ops->write(ops->ctx, state, 0, blk, OTA_BLOCK) != 0)
            return BR_IO;
        for (k = 0; k < 16; k++)
            blk[BR_OFF + BR_REC + k] = 0;
        if (ops->read(ops->ctx, state, 0, blk, OTA_BLOCK) != 0)
            return BR_IO;
        for (k = 0; k < 16; k++)
            if (blk[BR_OFF + BR_REC + k] != want[k])
                bad = 1;
        if (!bad)
            return BR_OK;
        for (k = 0; k < 16; k++)
            blk[BR_OFF + BR_REC + k] = want[k];
    }
    return BR_IO;
}

int bootrec_arm(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, ota_u32 seq)
{
    int r = open_block(ops, state, blk, 1);
    if (r != BR_OK)
        return r;
    return write_rec(ops, state, blk, seq, 0, 0);
}

int bootrec_start(const struct ota_card_ops* ops, const char* state, const char* previous, mw2_u8* blk, ota_u32 seq)
{
    struct bootrec r;
    int rc;
    if (!ops->exists(ops->ctx, previous))
        return BR_SKIP;                                 /* no fallback file: a refusal could only leave vanilla Melee */
    rc = open_block(ops, state, blk, 1);
    if (rc != BR_OK)
        return rc;
    parse(blk, &r);
    if (r.valid && r.cand > seq)
        return BR_SKIP;                                 /* running as the fallback of a newer, failed build: leave its record alone */
    if (r.valid && r.cand == seq) {
        if (r.good)
            return BR_SKIP;                             /* steady state: no write */
        if (r.tries >= 0xFFFFu)
            return BR_SKIP;
        return write_rec(ops, state, blk, seq, r.tries + 1u, 0);
    }
    return write_rec(ops, state, blk, seq, 1, 0);       /* no record, a damaged one, or one for an older build */
}

int bootrec_good(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, ota_u32 seq)
{
    struct bootrec r;
    int rc = open_block(ops, state, blk, 0);
    if (rc != BR_OK)
        return rc == BR_NOROOM ? BR_SKIP : rc;
    parse(blk, &r);
    if (!r.valid || r.cand != seq || r.good)
        return BR_SKIP;
    return write_rec(ops, state, blk, seq, 0, 1);
}
