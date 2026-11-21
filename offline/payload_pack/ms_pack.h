/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
 * MSPK container verifier SKETCH (freestanding C89).  Order and error codes are real and host-tested; the Ed25519 verify is an
 * injected function (a console build links a compact audited implementation,: not written here). */
#ifndef MS_PACK_H
#define MS_PACK_H
#include "ms_lz.h"

#define MS_HDR 136
#define MS_SIG 64
#define MS_OFF_PAYLOAD 200
#define MS_MAX_RAW 0x100000UL

#define MS_PK_OK 0
#define MS_PK_STRUCT (-10)      /* magic, version, ids, sizes, build id */
#define MS_PK_SIG (-11)
#define MS_PK_DOWNGRADE (-12)
#define MS_PK_LOADER (-13)
#define MS_PK_CODESET (-14)
#define MS_PK_STORED_HASH (-15)
#define MS_PK_DECODE (-16)
#define MS_PK_RAW_HASH (-17)
#define MS_PK_BUFSIZE (-18)

typedef int (*ms_ed25519_verify_fn)(const ms_u8 pub[32], const ms_u8 *msg, ms_u32 n, const ms_u8 sig[64]);

struct ms_pack_policy {
    const ms_u8 *pub;                 /* 32 bytes, patched into the loader */
    ms_u32 min_seq;                   /* highest build_seq ever accepted (kept on the card/SD, monotonic) */
    ms_u32 loader_version;
    const ms_u8 *codeset_sha256;      /* 32 bytes or 0 to skip */
    ms_ed25519_verify_fn verify;
};

/* blob/n = the whole file.  On MS_PK_OK raw[0..*raw_len) holds the engine and *seq the accepted build_seq. */
int ms_pack_open(const ms_u8 *blob, ms_u32 n, const struct ms_pack_policy *pol, ms_u8 *raw, ms_u32 raw_cap,
                 ms_u32 *raw_len, ms_u32 *seq);
#endif
