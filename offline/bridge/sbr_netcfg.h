/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_netcfg: DEV/LAB ONLY card override of the bridge's network addresses.
 *
 * Compiled in only with -DSBR_NETCFG=1 (offline/multishine/MANIFEST.json option `netcfg=on`, --netcfg). Without
 * the flag every accessor below is a macro that returns its argument and sbr_netcfg.c is an empty translation unit: the
 * release image has no code and no data for this (checked by: no sbr_nc_ symbol, no "SBNC" magic).
 *
 * What a card can override (IPv4 ADDRESSES only): the matchmaker address mm_ip (the name mm.slippi.gg and port 43113 stay patched and
 * are never read from the card), the DNS server address.  Flags, names, ports of the matchmaker and identity are never overridable.
 * The status server address + port of format version 1 (mask bit1) are IGNORED since (the status server was removed from the Slippi path):
 * a card that still carries them stays valid, the fields are neither validated nor used.
 *
 * The section lives inside the existing one-block file "MultishineBridge" (no fourth file), at file offset SBR_NC_OFF (the old
 * status-document record at 0x1000 is no longer read or written).  It is parsed from the block buffer of the one card read: no resident
 * memory beyond struct sbr_nc (16 bytes).  28-byte layout, big endian:
 *   0  "SBNC"  4 u8 version (1)  5 u8 mask (bit0 mm_ip, bit1 status, bit2 dns)  6 u16 length (28)  8 u32 mm_ip  12 u32 status_ip (ignored)
 *   16 u32 dns_ip  20 u16 status_port (ignored)  22 u16 0  24 u32 CRC32 over bytes 0..23
 * The bridge never writes this file.
 */
#ifndef SBR_NETCFG_H
#define SBR_NETCFG_H

#include "sbr.h"

#define SBR_NC_FILE "MultishineBridge"
#define SBR_NC_OFF 0x1100u
#define SBR_NC_LEN 28u
#define SBR_NC_VER 1u
#define SBR_NC_MM 0x01u
#define SBR_NC_STATUS 0x02u        /* ignored (see above): accepted in the mask, never validated or used */
#define SBR_NC_DNS 0x04u

/* status word (struct sbra_status nc_state); 0 forever in a build without SBR_NETCFG */
enum { SBR_NC_ST_OFF = 0, SBR_NC_ST_PENDING, SBR_NC_ST_ABSENT, SBR_NC_ST_ACTIVE, SBR_NC_ST_DAMAGED, SBR_NC_ST_INVALID, SBR_NC_ST_NEWER };

#ifdef SBR_NETCFG
struct sbr_nc { sbr_u32 mm_ip, dns_ip, mask; };

/* pure codec + validation: in = SBR_NC_LEN bytes.  Returns SBR_NC_ST_ABSENT (no magic), ACTIVE (*o filled), DAMAGED (length or CRC),
   INVALID (a rejected address, unknown mask bit, nothing selected, non-zero pad) or NEWER (version > 1); *o is zeroed unless ACTIVE. */
int sbr_nc_decode(const sbr_u8 *in, struct sbr_nc *o);
void sbr_nc_encode(sbr_u8 *out, const struct sbr_nc *c);                 /* host tool / tests */
int sbr_nc_ip_ok(sbr_u32 ip, int private_only);                           /* 1 = acceptable as an override */
/* the card layer feeds the block buffer here (sbr_card.c check_netcfg) and reports a missing file */
void sbr_nc_parse_block(const sbr_u8 *block_at_off);
void sbr_nc_no_file(void);
void sbr_nc_reset(void);                                                 /* back to PENDING (tests, a re-init) */
int sbr_nc_state(void);
const struct sbr_nc *sbr_nc_get(void);                                   /* the active override, or 0 */
const char *sbr_nc_words(void);                                          /* "Net cfg from card" while an override is active (and Real Slippi is not chosen), else 0 */
/* Settings > Online > Slippi: 1 = Real Slippi, the card's override is ignored by the two accessors below (patched name, DHCP DNS); 0 = Our mock (default, boot state).
   Set by the settings layer (on_set.c) from the stored option; nothing else changes it. */
void sbr_nc_set_real(int on);
int sbr_nc_real(void);
sbr_u32 sbr_nc_card_mm_ip(void);                                         /* the ACTIVE card override whatever the choice (0 = none): the read-only 'in use' row */
sbr_u32 sbr_nc_mm_ip(sbr_u32 baked);
sbr_u32 sbr_nc_dns_ip(sbr_u32 baked);
#define SBR_NC_PENDING_INIT SBR_NC_ST_PENDING
#else
#define sbr_nc_mm_ip(b) (b)
#define sbr_nc_dns_ip(b) (b)
#define sbr_nc_words() ((const char *)0)
#define SBR_NC_PENDING_INIT SBR_NC_ST_OFF
#endif

#endif
