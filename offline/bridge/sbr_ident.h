/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_ident: the console reads the player's identity for the Slippi bridge from the card file "MultishineIdentity"
 * (written by the build-time tools offline/provision/multishine_provision.py and tools/vmc_builder --identity, from the
 * player's OWN user.json).  Pure parser, project-owned C99, no libc, no heap.
 *
 * Payload v1 (0x12C bytes at file offset 0x1000; offline/provision docstring):
 *   "SDID" | u8 version 1 | u8 flags | u16 reserved | uid[64] | playKey[128] | connectCode[32] | displayName[64] |
 *   u32 CRC32 (zlib polynomial) over everything before it.  Text fields: UTF-8, NUL terminated inside the field, NUL padded.
 *
 * The same rules as multishine_provision.parse_payload / validate_identity, plus the bridge's own: the connect code must be a
 * Slippi code the bridge can send (sbr_code_valid), no control characters, every field non-empty.  R7: on ANY failure the
 * output is wiped; the caller wipes the source buffer (sbr_wipe) as soon as this returns.  Nothing here prints or logs.
 */
#ifndef SBR_IDENT_H
#define SBR_IDENT_H

#include "sbr.h"

#define SBR_ID_FILE "MultishineIdentity"
#define SBR_ID_FILE_OFF 0x1000u          /* payload offset inside the file's first block */
#define SBR_ID_PAYLOAD_LEN 0x12Cu
#define SBR_ID_BODY_LEN 0x128u

enum {
    SBR_ID_OK = 0, SBR_ID_SHORT, SBR_ID_MAGIC, SBR_ID_VERSION, SBR_ID_CRC, SBR_ID_FIELD, SBR_ID_CODE, SBR_ID_COUNT
};

/* 0 (SBR_ID_OK) and *out filled, else the reason (out wiped) */
int sbr_ident_parse(const sbr_u8 *p, sbr_u32 len, struct sbr_ident *out);
const char *sbr_ident_name(int r);

/* helpers shared by the card modules */
sbr_u32 sbr_crc32(const sbr_u8 *p, sbr_u32 n);           /* zlib.crc32 */
void sbr_wipe(void *p, sbr_u32 n);                        /* volatile stores: never optimised away */

#endif
