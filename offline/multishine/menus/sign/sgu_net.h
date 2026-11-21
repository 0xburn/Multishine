/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sgu_net: the NETWORK PATCH applier.  A signed container net-N.mscf (MSCF, ABI 3) carries a few allow-listed fields of two card files that the
 * updater never touches otherwise: the online record "MultishineOnline" (server address, port, host name, pinned keys 1 and 2, backup server) and the SBNC section of
 * "MultishineBridge" (mm_ip, dns_ip, mask).  This file parses the payload and patches those bytes IN PLACE in the card block buffers, recomputes the CRCs, checks the result
 * and writes the blocks back.  It never copies, moves or overwrites the device id, device secret, connect code or display name (bytes 0x60..0xA8 of the record), and keeps no
 * second copy of the record block: the undo data is only the patched fields (public data).  Pure C89, no libc, no heap; all card access through ota_card_ops.
 *
 * Payload (image of the signed container, big endian):
 *   +0 'MSNP'  +4 u8 version 1  +5 u8 number of fields  +6 u16 total length of the payload including the CRC  +8 u32 seq (must equal the container's build_seq)
 *   +12 fields: tag u8, length u8, value ...   then u32 CRC-32 (zlib) of every byte before it.   Tags (each at most once):
 *     0x01 server IPv4 (4; 0 = none: the host name must then be given or already present)   0x02 port (2; 0 = default 43150)   0x03 host name (0..47 bytes, 0 = remove)
 *     0x04 pinned key 1 (32, not all zero)   0x05 pinned key 2 (32; all zero = none)   0x06 backup server IPv4 (4; 0 = none)
 *     0x10 SBNC mm_ip (4; 0 = clear)   0x11 SBNC dns_ip (4; 0 = clear)   0x12 SBNC mask (1; bits 1 and 4 only, applied after the two addresses)
 * The sequence number of the last applied patch lives in the online block at 0x1200: 'MSNQ' u8 version 1, 3 bytes 0, u32 seq, u32 CRC-32 of the first 12 bytes.
 * Rule: a patch is applied only when its seq is HIGHER than the stored one (a later "back to the server" patch is simply a higher seq).
 */
#ifndef SGU_NET_H
#define SGU_NET_H
#pragma GCC diagnostic ignored "-Wunused-function"      /* ota_core.h pulls in the static signature check of the loader, which this file does not call */
#include "../../../cardloader/ota/ota_core.h"

#define NGP_ABI 3u
#define NGP_MAX_PAYLOAD 0x200u
#define NGP_WORK (3u * OTA_BLOCK)       /* work buffer: online block, bridge block, read-back block (scrubbed before ngp_apply returns) */

enum { NGP_OK = 0, NGP_E_FORMAT = 1, NGP_E_RANGE = 2, NGP_E_SEQ = 3, NGP_E_NOREC = 4, NGP_E_BADREC = 5, NGP_E_NOBRIDGE = 6, NGP_E_IO = 7, NGP_E_VERIFY = 8, NGP_E_NOROOM = 9 };      /* NGP_E_NOBRIDGE is no longer returned (the file is created); 9 = card full when the bridge file had to be created */

/* Stored seq of the last applied patch: 0 when none (or no record).  work: NGP_WORK bytes.  Returns NGP_OK / NGP_E_IO; *seq is set to 0 first. */
int ngp_stored_seq(const struct ota_card_ops *ops, mw2_u8 *work, unsigned *seq);
/* Pure parse + value checks of a payload (no card access): NGP_OK or NGP_E_FORMAT / NGP_E_RANGE.  `seq` is the container's build_seq. */
int ngp_check(const mw2_u8 *pl, unsigned n, unsigned seq);
/* Apply a payload that ota_verify accepted.  Card unchanged on any error other than NGP_E_VERIFY (a failed read-back whose undo also failed: the card may hold the new bytes). */
int ngp_apply(const struct ota_card_ops *ops, const mw2_u8 *pl, unsigned n, unsigned seq, mw2_u8 *work);
#endif
