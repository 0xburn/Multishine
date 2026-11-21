/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_cfgrec: the online setup record of the 'Multishine online' image, kept on the memory card in the file "MultishineOnline"
 * ( and 10).  Pure portable C89: no libc, no heap, no I/O, no globals; the card side is on_card.c.
 * Reference implementation and writer in Python: onfile.py (the host tests run both and compare).
 *
 * The file is written by a PC tool (tools/vmc_builder, `online-config`), never by the console: the console only reads it.  Same conventions as
 * MultishineOptions / MultishineCodes: GALE / 01, ONE block of 0x2000 bytes, two comment lines at 0, the record at 0x1000.
 *
 * Record (offsets from the record start, all numbers big endian), size 0xC0:
 *   +0x00 'SDON'  +0x04 u8 version 1  +0x05 u8 0  +0x06 u16 0 (reserved)  +0x08 u32 CRC-32 (zlib) of record[0x0C..0xC0)
 *   +0x0C u32 server IPv4 (host order value 0xC0A80102 = 192.168.1.2)   +0x10 u16 server port (0 = 43150)
 *   +0x12 u16 modes: bit0 serve the Unranked row, bit1 serve the Ranked row (Direct is always served)
 *   +0x14 u32 find_ms (0 = default 60000; else 5000..300000)  +0x18 u32 connect_ms (0 = 20000; else 5000..120000)  +0x1C u32 net_ms (0 = 20000; else 5000..120000)
 *   +0x20 pinned server key 1 (Ed25519, 32 bytes, not all zero)   +0x40 pinned server key 2 (32 bytes, may be all zero)
 *   +0x60 device id (8)   +0x68 device secret (16: a SECRET, treat the card like a password)
 *   +0x78 own connect code (24 bytes, NUL terminated, e.g. AB#1234)   +0x90 display name (24 bytes, NUL terminated)
 *   +0xA8 u32 BACKUP server IPv4 (optional, 0 = none; same port and pinned keys;  C4) +0xAC reserved (20 bytes, zero).  The bytes are inside the CRC;
 *         readers that predate the field ignore them, so such a card still reads OK and simply has no backup.  An invalid backup address (loopback, multicast, broadcast, the primary's own) is ignored (onr_backup returns 0).
 * OPTIONAL HOST EXTENSION, at record +0x100 (file offset 0x1100, outside the CRC of the record above; the record and every
 * reader that does not know it are unchanged): the server's NAME, resolved by DNS at menu time when the numeric server address (+0x0C) is 0 (a non-zero address wins).
 *   +0x100 'SDHN'  +0x104 u8 version 1  +0x105 u8 n = host length (1..47)  +0x106 u16 0  +0x108 u32 CRC-32 (zlib) of the next 48 bytes  +0x10C host (48 bytes, ASCII
 *   [A-Za-z0-9.-], NUL terminated, zero padded)   -> 0x3C bytes.  An invalid, damaged or newer extension is ignored (never an error by itself); with it valid a record with
 *   server address 0 is complete.  Older images read such a card as INCOMPLETE ("online setup incomplete"), never as something else.
 * Verdicts (onr_check): OK; DAMAGED (bad magic, version 0, CRC); NEWER (same magic, version > 1: never used, and nothing here ever writes it);
 * INCOMPLETE (magic and CRC good but the content is unusable: no server address, no key, a code that is not a connect code, an empty or unprintable name).
 */
#ifndef ON_CFGREC_H
#define ON_CFGREC_H

#include "od_glue.h"

#define ONR_MAGIC 0x53444F4Eu         /* 'SDON' */
#define ONR_VERSION 1
#define ONR_SIZE 0xC0u
#define ONR_PAYLOAD_OFF 0x1000u       /* inside the 0x2000 card block */
#define ONR_BLOCK 0x2000u
#define ONR_EXT_OFF 0x100u            /* the host extension, relative to the record */
#define ONR_EXT_SIZE 0x3Cu
#define ONR_EXT_END (ONR_EXT_OFF + ONR_EXT_SIZE)
#define ONR_EXT_MAGIC 0x5344484Eu     /* 'SDHN' */
/* IDENTITY SLOTS: server-locked builds keep one device identity per environment in a second optional extension at record +0x140 (outside the CRC of the
 * record and of the host extension; older readers ignore it): 'SDSL' u8 version 1, u8 3 (slots), u16 0, u32 CRC-32 (zlib) of the 3 slots, then slot 0 prod, 1 dev, 2 lab, 0x50 bytes each:
 * +0x00 tag (8: the first 8 bytes of the server key that minted the identity; all zero = empty), +0x08 device id (8), +0x10 secret (16), +0x20 code (24), +0x38 name (24).
 * Byte for byte the same field order as record +0x60..+0xA8, so a slot entry and the old single identity are interchangeable. */
#define ONR_SL_OFF 0x140u
#define ONR_SL_N 3
#define ONR_SL_ENT 0x50u
#define ONR_SL_HDR 0x0Cu
#define ONR_SL_SIZE (ONR_SL_HDR + ONR_SL_N * ONR_SL_ENT)
#define ONR_SL_END (ONR_SL_OFF + ONR_SL_SIZE)
#define ONR_SL_MAGIC 0x5344534Cu      /* 'SDSL' */
#define ONR_ENV_PROD 0
#define ONR_ENV_DEV 1
#define ONR_ENV_LAB 2
#define ONR_MODE_UNRANKED 1u
#define ONR_MODE_RANKED 2u

enum { ONR_OK = 0, ONR_DAMAGED = 1, ONR_NEWER = 2, ONR_INCOMPLETE = 3 };

/* verdict of a record (rec: ONR_SIZE readable bytes); the host extension is not looked at (a record with server address 0 is INCOMPLETE) */
int onr_check(const unsigned char *rec);
/* the same verdict, with the host extension counted (rec: ONR_EXT_END readable bytes, i.e. the whole card block from the record on) */
int onr_check2(const unsigned char *rec);
/* 1 and the host name in out[OD_HOST_MAX] if the extension is present and valid, else 0 (out is zeroed); rec: ONR_EXT_END readable bytes */
int onr_host(const unsigned char *rec, char *out);
/* only for onr_check == ONR_OK: fill the fields the card owns (flags, server, timeouts, keys, identity); the image's own fields (magic, build id, codeset,
   rules, seed) and the LAX flag are never taken from the card.
   OD_CFGF_SRVLOCK in dst->flags (a build patched for one server): the server (address, port, HOST flag + name) and the pinned keys stay the
   patched ones; the card gives timeouts, modes and identity, and the identity only when the card's pinned key equals a patched one (a device secret belongs to the server
   that issued it): otherwise device id and secret are cleared, i.e. the console is a guest there (anonymous sessions: the Update row works, nothing is written). */
void onr_decode(struct od_cfg *dst, const unsigned char *rec);
/* GUEST SETUP ( rule 5): a server-locked build on a good card that has NO MultishineOnline.  onr_guest: dst (the patched config, OD_CFGF_SRVLOCK) becomes a guest of the patched
   server in RAM: identity flag, both modes, all-zero device id and secret, placeholder code and name; nothing else changes.  onr_make_guest: the same as a card record into r (ONR_EXT_END bytes, cleared first;
   the record start, i.e. block offset ONR_PAYLOAD_OFF): patched address, port, key (+ the host extension for a name-only build), NO secret.  It is what the first Link writes the identity into. */
void onr_guest(struct od_cfg *dst);
void onr_make_guest(unsigned char *rec, const struct od_cfg *baked);
/* after onr_decode: sets dst->host and OD_CFGF_HOST from the extension (rec: ONR_EXT_END readable bytes), or clears both when there is none; no-op with OD_CFGF_SRVLOCK */
void onr_decode_host(struct od_cfg *dst, const unsigned char *rec);
/* the backup server address (host order) of an OK record, or 0 when there is none or it is not acceptable; rec: ONR_SIZE readable bytes */
unsigned int onr_backup(const unsigned char *rec);
/* IDENTITY SLOTS.  rec: ONR_SL_END readable bytes from the record start (the whole card block from the record on); bk0 / bk1: the two patched server keys (32 bytes each, NULL or all zero = none).
   env: the active environment (ONR_ENV_*).  lab_build: 1 in a lab build (where a legacy identity of unknown origin goes to the lab slot, else to dev).
   A card without a valid SDSL is read through the old single identity (record +0x60..+0xA8), which belongs to ONE environment: the one whose key it was linked on (the card's key 1 or 2 equals
   a patched key: prod; else the active environment if it is not prod, else lab in a lab build and dev in a release build).  No write happens by reading.
   An identity is only used when its tag equals the key in force (prod: a patched key; dev / lab: the card record's key 1); else the environment is a guest. */
/* 1 and *ident -> the 0x48 byte identity (device id, secret, code, name) of env, else 0 (guest) */
int onr_slot_get(const unsigned char *rec, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1, const unsigned char **ident);
/* after onr_decode: replaces dev, tok, code and name of dst by the active slot's identity, or clears them (a guest) when it has none */
void onr_decode_slot(struct od_cfg *dst, const unsigned char *rec, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1);
/* store the identity (0x48 bytes; an all-zero device id clears the slot) in env's slot of rec (modified in place; the other slots and the record's old single identity are not touched).  A card
   without a valid SDSL first gets one made from the old single identity (migration).  0 = refused (newer SDSL version); 1 = done. */
int onr_slot_put(unsigned char *rec, int env, int lab_build, const unsigned char *bk0, const unsigned char *bk1, const unsigned char *ident);
/* 1 if rec holds a valid SDSL, 2 if it holds a newer one, 0 if there is none (or a damaged one) */
int onr_slots_state(const unsigned char *rec);

/* copy n bytes from src to dst, or clear them when src is NULL (shared byte-copy helper: smaller than the open loops it replaces) */
void onr_cpz(void *dst, const unsigned char *src, unsigned int n);

#ifdef SBR_NETCFG
/* lab/dev builds only: the same two decoders with lab = 1 ("Server: Lab" chosen in Settings > Online): the patched server lock is not applied, so the card record's
   address, port, pinned keys and host name win and its identity is kept (its own pinned key is the one in force).  lab = 0 is exactly onr_decode / onr_decode_host. */
void onr_decode_lab(struct od_cfg *dst, const unsigned char *rec, int lab);
void onr_decode_host_lab(struct od_cfg *dst, const unsigned char *rec, int lab);
#endif

#endif
