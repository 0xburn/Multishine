/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * optstore: the options card file payload (SDOF v1) and the rules that turn it into option values (portable C, host tested;
 * the same format as offline/options/optfile.py, which the host tests cross-check).
 *
 * Card file "<projname.SHORT>Options" (GALE/01, 1 block = 0x2000 bytes).  Block layout: comment 0x000 (2 x 32 bytes),
 * optional icon from 0x040 (PC tool only), payload at 0x1000:
 *   +0x000 'SDOF'  +0x004 u8 version 1  +0x005 u8 count 16  +0x006 u16 flags (bit0: chat table present)
 *   +0x008 u32 CRC-32 (zlib) of payload[0x00C:0x240]
 *   +0x00C u8 value[16] by option id; 0xFF = use the build default (also any value outside the option's range)
 *   +0x01C 4 reserved bytes  +0x020 SDQC chat table (524 bytes) or zeros;  payload size 0x240.
 *
 * Rules:
 *  - optstore_check: OK, or why the payload cannot be used.  NEWER (same magic, version > 1) is never overwritten by this
 *    build; DAMAGED (bad magic, version 0, count != 16, CRC) is replaced by the next save.
 *  - optstore_load: value = card byte when it is inside [min,max], else the build default.
 *  - optstore_merge: on save, only options the user changed since the page opened get an explicit byte; everything else keeps
 *    the byte already on the card (0xFF stays 0xFF, so an untouched option keeps following the build default).
 *  - optstore_fill: a valid payload keeps its flags, chat table and unknown value bytes (ids >= OPT_COUNT); a damaged or new
 *    one is rebuilt from zero.  The CRC is recomputed.
 */
#ifndef OPTSTORE_H
#define OPTSTORE_H

#include "optengine.h"

#define OS_MAGIC 0x53444F46u /* 'SDOF' */
#define OS_VERSION 1
#define OS_NVAL 16
#define OS_UNSET 0xFF
#define OS_PAYLOAD_OFF 0x1000
#define OS_PAYLOAD_SIZE 0x240
#define OS_VAL_OFF 0x0C
#define OS_CRC_FROM 0x0C
#define OS_BLOCK 0x2000
#define OS_COMMENT_LEN 32

enum { OS_OK = 0, OS_DAMAGED = 1, OS_NEWER = 2 };

opt_u32 os_crc32(const opt_u8* p, opt_u32 len);
int optstore_check(const opt_u8* payload);
/* raw[] <- value bytes of a checked payload */
void optstore_raw(const opt_u8* payload, opt_u8 raw[OPT_MAX]);
/* b->val[] from raw[] (range check per option, 0xFF/out of range -> b->dflt); returns how many came from the card */
int optstore_load(struct opt_block* b, const opt_u8 raw[OPT_MAX]);
/* raw[i] = val[i] for every option whose value differs from open_val[i]; returns the number of changed options */
int optstore_merge(opt_u8 raw[OPT_MAX], const opt_u8 val[OPT_MAX], const opt_u8 open_val[OPT_MAX]);
/* write raw[] into payload (keep = 1: payload passed optstore_check, keep its other bytes) and recompute the CRC */
void optstore_fill(opt_u8* payload, const opt_u8 raw[OPT_MAX], int keep);
/* a fresh block (zeros, two comment lines from name/comment strings, then the payload) */
void optstore_new_block(opt_u8* block, const char* line1, const char* line2, const opt_u8 raw[OPT_MAX]);

#endif
