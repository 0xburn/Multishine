/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#include "ms_pack.h"
#include "ms_sha256.h"

static ms_u32 be32(const ms_u8 *p) { return ((ms_u32)p[0] << 24) | ((ms_u32)p[1] << 16) | ((ms_u32)p[2] << 8) | p[3]; }

static int same(const ms_u8 *a, const ms_u8 *b, int n)
{
    ms_u8 d = 0; int i;
    for (i = 0; i < n; i++) d |= (ms_u8)(a[i] ^ b[i]);
    return d == 0;
}

int ms_pack_open(const ms_u8 *blob, ms_u32 n, const struct ms_pack_policy *pol, ms_u8 *raw, ms_u32 raw_cap,
                 ms_u32 *raw_len, ms_u32 *seq)
{
    ms_u32 stored, rawsz, seqv, minl, outn, i;
    ms_u8 h[32], comp;
    if (n < MS_OFF_PAYLOAD || blob[0] != 'M' || blob[1] != 'S' || blob[2] != 'P' || blob[3] != 'K') return MS_PK_STRUCT;
    comp = blob[5];
    if (blob[4] != 1 || (comp != 0 && comp != 1) || blob[6] != 1 || blob[7] != 0) return MS_PK_STRUCT;
    seqv = be32(blob + 8); stored = be32(blob + 12); rawsz = be32(blob + 16); minl = be32(blob + 20);
    if (stored > n - MS_OFF_PAYLOAD || n - MS_OFF_PAYLOAD != stored) return MS_PK_STRUCT;
    if (rawsz == 0 || rawsz > MS_MAX_RAW || (comp == 0 && rawsz != stored)) return MS_PK_STRUCT;
    if (blob[24] == 0) return MS_PK_STRUCT;
    for (i = 24; i < 40; i++) {
        if (blob[i] == 0) { ms_u32 j; for (j = i; j < 40; j++) if (blob[j]) return MS_PK_STRUCT; break; }
        if (blob[i] < 0x20 || blob[i] > 0x7E) return MS_PK_STRUCT;
    }
    if (rawsz > raw_cap) return MS_PK_BUFSIZE;
    if (!pol->verify(pol->pub, blob, MS_HDR, blob + MS_HDR)) return MS_PK_SIG;        /* before any decoder runs */
    if (seqv < pol->min_seq) return MS_PK_DOWNGRADE;
    if (minl > pol->loader_version) return MS_PK_LOADER;
    if (pol->codeset_sha256 && !same(pol->codeset_sha256, blob + 40, 32)) return MS_PK_CODESET;
    ms_sha256(blob + MS_OFF_PAYLOAD, stored, h);
    if (!same(h, blob + 72, 32)) return MS_PK_STORED_HASH;
    if (comp == 1) {
        if (ms_lz_decode(blob + MS_OFF_PAYLOAD, stored, raw, rawsz, &outn) != MS_LZ_OK) return MS_PK_DECODE;
    } else {
        for (i = 0; i < stored; i++) raw[i] = blob[MS_OFF_PAYLOAD + i];
        outn = stored;
    }
    if (outn != rawsz) return MS_PK_RAW_HASH;
    ms_sha256(raw, outn, h);
    if (!same(h, blob + 104, 32)) return MS_PK_RAW_HASH;
    *raw_len = outn; *seq = seqv;
    return MS_PK_OK;
}
