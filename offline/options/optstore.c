/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* optstore: see optstore.h.  Original work, no third-party code. */
#include "optstore.h"

static opt_u32 be32(const opt_u8* p)
{
    return ((opt_u32) p[0] << 24) | ((opt_u32) p[1] << 16) | ((opt_u32) p[2] << 8) | p[3];
}

static void put32(opt_u8* p, opt_u32 v)
{
    p[0] = (opt_u8) (v >> 24);
    p[1] = (opt_u8) (v >> 16);
    p[2] = (opt_u8) (v >> 8);
    p[3] = (opt_u8) v;
}

opt_u32 os_crc32(const opt_u8* p, opt_u32 len)
{
    opt_u32 c = 0xFFFFFFFFu;
    int k;
    while (len--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

int optstore_check(const opt_u8* p)
{
    if (be32(p) != OS_MAGIC)
        return OS_DAMAGED;
    if (p[4] > OS_VERSION)
        return OS_NEWER;
    if (p[4] != OS_VERSION || p[5] != OS_NVAL)
        return OS_DAMAGED;
    if (be32(p + 8) != os_crc32(p + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM))
        return OS_DAMAGED;
    return OS_OK;
}

void optstore_raw(const opt_u8* p, opt_u8 raw[OPT_MAX])
{
    int i;
    for (i = 0; i < OPT_MAX; i++)
        raw[i] = p[OS_VAL_OFF + i];
}

int optstore_load(struct opt_block* b, const opt_u8 raw[OPT_MAX])
{
    int i, n = 0;
    for (i = 0; i < OPT_COUNT; i++) {
        if (raw[i] != OS_UNSET && raw[i] >= b->minv[i] && raw[i] <= b->maxv[i]) {
            b->val[i] = raw[i];
            n++;
        } else {
            b->val[i] = b->dflt[i];
        }
    }
    return n;
}

int optstore_merge(opt_u8 raw[OPT_MAX], const opt_u8 val[OPT_MAX], const opt_u8 open_val[OPT_MAX])
{
    int i, n = 0;
    for (i = 0; i < OPT_COUNT; i++) {
        if (val[i] != open_val[i]) {
            raw[i] = val[i];
            n++;
        }
    }
    return n;
}

void optstore_fill(opt_u8* p, const opt_u8 raw[OPT_MAX], int keep)
{
    int i;
    if (!keep) {
        for (i = 0; i < OS_PAYLOAD_SIZE; i++)
            p[i] = 0;
        for (i = 0; i < OS_NVAL; i++)
            p[OS_VAL_OFF + i] = OS_UNSET;
    }
    put32(p, OS_MAGIC);
    p[4] = OS_VERSION;
    p[5] = OS_NVAL;
    for (i = 0; i < OPT_COUNT; i++)   /* ids >= OPT_COUNT: kept (a later build's options), or 0xFF when rebuilt */
        p[OS_VAL_OFF + i] = raw[i];
    put32(p + 8, os_crc32(p + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM));
}

void optstore_new_block(opt_u8* blk, const char* line1, const char* line2, const opt_u8 raw[OPT_MAX])
{
    int i;
    for (i = 0; i < OS_BLOCK; i++)
        blk[i] = 0;
    for (i = 0; i < OS_COMMENT_LEN - 1 && line1[i]; i++)
        blk[i] = (opt_u8) line1[i];
    for (i = 0; i < OS_COMMENT_LEN - 1 && line2[i]; i++)
        blk[OS_COMMENT_LEN + i] = (opt_u8) line2[i];
    optstore_fill(blk + OS_PAYLOAD_OFF, raw, 0);
}
