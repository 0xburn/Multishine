/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* df_core: see df.h.  Freestanding (no libc, no SDK, no globals except the CRC table), soft-float safe. */
#include "df.h"
#include "../../../payload_pack/ms_lz.h"

static df_u32 rd32(const df_u8 *p) { return ((df_u32)p[0] << 24) | ((df_u32)p[1] << 16) | ((df_u32)p[2] << 8) | p[3]; }

df_u32 df_crc32(const df_u8 *p, df_u32 n, df_u32 crc)
{
    static const df_u32 T[16] = { 0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
                                  0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu };
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ T[crc & 15u];
        crc = (crc >> 4) ^ T[crc & 15u];
    }
    return ~crc;
}

int df_toc_parse(const df_u8 *b, struct df_ent *tab, int max, df_u32 hdr_off, df_u32 file_len)
{
    const df_u8 *h = b + hdr_off;
    df_u32 toc = hdr_off + 0x20u, count, i, k;
    if (rd32(h) != DF_MAGIC || rd32(h + 4) != DF_VERSION) return -1;
    count = rd32(h + 8);
    if (count > (df_u32)max || count > DF_MAX || toc + count * DF_ENT_SIZE > DF_BLOCK) return -2;
    if (rd32(h + 12) > file_len) return -2;                      /* the header's total length must lie inside the card file */
    if (df_crc32(b + toc, count * DF_ENT_SIZE, 0) != rd32(h + 16)) return -3;
    for (i = 0; i < count; i++) {
        const df_u8 *s = b + toc + i * DF_ENT_SIZE;
        struct df_ent *e = &tab[i];
        for (k = 0; k < DF_NAME_MAX; k++) e->name[k] = (char)s[k];
        e->name[DF_NAME_MAX - 1] = 0;
        e->mode = rd32(s + 0x18); e->new_size = rd32(s + 0x1C); e->old_size = rd32(s + 0x20); e->stored_len = rd32(s + 0x24);
        e->data_off = rd32(s + 0x28); e->gap = rd32(s + 0x2C); e->crc_new = rd32(s + 0x30); e->crc_old = rd32(s + 0x34);
        e->p_pos = rd32(s + 0x38); e->p_old = rd32(s + 0x3C);
        if (e->name[0] == 0 || e->mode == DF_PATCH_UNUSED || e->mode > DF_DELTA_IP || e->new_size == 0 || e->new_size > DF_FILE_MAX || (e->data_off & (DF_BLOCK - 1u))) return -4;
        /* every length is bounded before any sum or round-up is made (32-bit wraps were the F2 finding of the review): data lies inside the card file, sizes are at most 64 MiB */
        if (e->data_off > file_len || e->stored_len > file_len - e->data_off || e->old_size > DF_FILE_MAX || e->gap > DF_FILE_MAX || e->p_pos > DF_FILE_MAX) return -4;
        if (e->mode == DF_RAW && e->stored_len != e->new_size) return -4;
        if (e->mode == DF_MSLZ && (e->gap > e->new_size || e->stored_len == 0)) return -4;
        if ((e->mode == DF_DELTA || e->mode == DF_DELTA_IP) && (e->old_size == 0 || e->p_pos == 0 || e->stored_len == 0)) return -4;
    }
    return (int)count;
}

int df_find(const struct df_ent *tab, int n, const char *path)
{
    int i;
    if (path[0] == '/') path++;
    for (i = 0; i < n; i++) {
        const char *a = tab[i].name, *b = path;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

static df_u32 rup32(df_u32 x) { return (x + 31u) & ~31u; }

int df_classify(const struct df_ent *e, df_u32 rl)
{
    if (e->mode < DF_DELTA) return DF_CL_FULL;
    if (rl == e->old_size) return rl == e->new_size ? DF_CL_BOTH : DF_CL_STOCK;
    return rl == e->new_size ? DF_CL_BAKED : DF_CL_UNKNOWN;
}

int df_find_alt(const struct df_ent *tab, int n, int k)
{
    int i;
    for (i = k + 1; i < n; i++) {
        const char *a = tab[k].name, *b = tab[i].name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0 && tab[i].mode < DF_DELTA) return i;
    }
    return -1;
}

/* LEB128 at ops[*i]: 0 = ok */
static int rdv(const df_u8 *ops, df_u32 n, df_u32 *i, df_u32 *out)
{
    df_u32 v = 0, sh = 0;
    while (1) {
        df_u32 b;
        if (*i >= n || sh > 28) return -1;
        b = ops[(*i)++];
        v |= (b & 0x7Fu) << sh; sh += 7;
        if (!(b & 0x80u)) { *out = v; return 0; }
    }
}

/* DELTA: build the new file from the retail copy `old` and the op stream (tools/card/mdelta.py); every read and write is bounds checked, lengths are compared by subtraction */
static int delta_apply(const df_u8 *old, df_u32 old_n, const df_u8 *ops, df_u32 ops_n, df_u8 *dest, df_u32 new_n, int ip, df_u32 shift)
{
    df_u32 i = 0, d = 0, prev_end = 0, a, len, k;
    while (1) {
        df_u32 t;
        if (i >= ops_n) return -12;
        t = ops[i++];
        if (t == 0) break;
        if (t == 2) {                                       /* COPY: zigzag(src - prev_end), len */
            df_u32 src;
            if (rdv(ops, ops_n, &i, &a) || rdv(ops, ops_n, &i, &len)) return -12;
            src = (a & 1u) ? prev_end - ((a >> 1) + 1u) : prev_end + (a >> 1);
            if (src > old_n || len > old_n - src || len > new_n - d) return -12;
            if (ip && (src < prev_end || d > shift + src)) return -12;        /* in place: forward only, and never ahead of the unread source */
            for (k = 0; k < len; k++) dest[d + k] = old[src + k];
            d += len; prev_end = src + len;
        } else if (t == 1 || t == 3) {                      /* ADD: len, literal bytes   |   RUN: len, byte */
            if (rdv(ops, ops_n, &i, &len)) return -12;
            if (len > new_n - d || (ip && (d > shift + prev_end || len > shift + prev_end - d))) return -12;      /* in place: a write may not reach the unread source (shift + prev_end <= d + len is refused; d <= shift + prev_end holds) */
            if (t == 1) { if (len > ops_n - i) return -12; for (k = 0; k < len; k++) dest[d + k] = ops[i + k]; i += len; }
            else { if (i >= ops_n) return -12; for (k = 0; k < len; k++) dest[d + k] = ops[i]; i++; }
            d += len;
        } else return -12;
    }
    return d == new_n ? 0 : -12;
}

int df_produce(const struct df_ent *e, const struct df_io *io, df_u8 *dest, df_u32 cap)
{
    df_u32 n = e->new_size;
    ms_u32 out = 0;
    if (n > DF_FILE_MAX || e->old_size > DF_FILE_MAX || e->gap > DF_FILE_MAX || e->p_pos > DF_FILE_MAX || e->stored_len > DF_FILE_MAX || cap < rup32(n)) return -11;   /* an entry that did not come through df_toc_parse */
    if (e->mode == DF_DELTA || e->mode == DF_DELTA_IP) {
        int ip = e->mode == DF_DELTA_IP, rc;
        df_u32 oldcap = rup32(e->old_size), newcap = rup32(n), dlen = e->p_pos, shift = 0, rl = io->retail_len ? io->retail_len(io->ctx, e) : e->old_size, k, cn;
        df_u32 dbuf = rup32(dlen > e->gap + e->stored_len ? dlen : e->gap + e->stored_len);   /* every term is <= 64 MiB (df_toc_parse): no wrap */
        df_u8 *scratch, *ops, *old;
        ms_u32 dout = 0;
        if (rl == n && rl != e->old_size) {                 /* PATCHED by length: the disc file is read straight into dest; equal CRC = it is the result, nothing to decode (no scratch, no pack read) */
            if (io->read_retail(io->ctx, e, dest, newcap)) return -10;
            return df_crc32(dest, n, 0) == e->crc_new ? DF_OK_BAKED : DF_E_BASE;      /* else dest holds the disc's own bytes */
        }
        if (rl != e->old_size) return DF_E_BASE;            /* neither length: nothing is read, dest is left alone */
        if (!io->alloc || (ip && newcap < oldcap)) return -14;
        if (ip) shift = newcap - oldcap;
        scratch = (df_u8 *)io->alloc(io->ctx, ip ? dbuf : oldcap + dbuf);
        if (!scratch) return -14;
        old = ip ? dest + shift : scratch;                  /* in place: the retail file sits at dest + shift and the ops rewrite it forward (the packer proved the order) */
        ops = ip ? scratch : scratch + oldcap;
        rc = -10;
        if (io->read_retail(io->ctx, e, old, oldcap)) goto dfail;
        if (df_crc32(old, e->old_size, 0) != e->crc_old) {   /* not the stock base: the intended result already (same length), else unknown; either way dest ends up holding the disc's bytes */
            rc = (e->old_size == n && df_crc32(old, n, 0) == e->crc_new) ? DF_OK_BAKED : DF_E_BASE;
            cn = e->old_size < n ? e->old_size : n;
            for (k = 0; k < cn; k++) dest[k] = old[k];      /* in place: dest <= old, a forward copy is safe */
            for (k = cn; k < newcap; k++) dest[k] = 0;
            goto dfail;
        }
        rc = -10;
        if (io->read_pack(io->ctx, e->data_off, ops + e->gap, e->stored_len)) goto dfail;
        rc = -12;
        if (ms_lz_decode(ops + e->gap, e->stored_len, ops, dbuf, &dout) != MS_LZ_OK || (df_u32)dout != dlen) goto dfail;
        rc = delta_apply(old, e->old_size, ops, dlen, dest, n, ip, shift);
dfail:
        io->release(io->ctx, scratch);
        if (rc) return rc;
        if (df_crc32(dest, n, 0) != e->crc_new) return -13;
        return 0;
    }
    if (e->mode == DF_RAW) {
        if (e->stored_len != n) return -11;
        if (io->read_pack(io->ctx, e->data_off, dest, n)) return -10;
    } else if (e->mode == DF_MSLZ) {
        df_u32 g = e->gap;
        if (g > cap || e->stored_len > cap - g) return -11;
        if (io->read_pack(io->ctx, e->data_off, dest + g, e->stored_len)) return -10;
        if (ms_lz_decode(dest + g, e->stored_len, dest, cap, &out) != MS_LZ_OK || (df_u32)out != n) return -12;
    } else return -11;
    if (df_crc32(dest, n, 0) != e->crc_new) return -13;
    return 0;
}
