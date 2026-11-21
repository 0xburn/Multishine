/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_stun: STUN Binding client pieces for the Slippi 3.7.0 join message.
 * Pure C89, no libc, no heap, caller buffers, no I/O: the caller sends the 20 byte request from the SAME UDP socket as the ENet peer
 * (the bridge's single sdnet socket) and feeds the datagrams that come back.  Transaction ids come from the caller.
 *
 * Mirrors the public client (Ishiiruka v3.7.0 SlippiMatchmaking.cpp + slippi-rust-extensions matchmaking/stun.rs): Binding Request type 0x0001,
 * length 0, cookie 0x2112A442; response must be 0x0101 with the cookie and our transaction id; XOR-MAPPED-ADDRESS (0x0020) preferred,
 * MAPPED-ADDRESS (0x0001) fallback; IPv4 only; unknown attributes skipped; 3 tries of 500 ms, keepalive 15 s.
 * STUN servers of the public client ( STUN_SERVERS; two are asked, the first two answering are compared):
 * stun.l.google.com:19302, stun1.l.google.com:19302, stun.cloudflare.com:3478, global.stun.twilio.com:3478.
 */
#ifndef SBR_STUN_H
#define SBR_STUN_H

typedef unsigned char sbrs_u8;
typedef unsigned short sbrs_u16;
typedef unsigned int sbrs_u32;

#define SBR_STUN_REQ_LEN 20u
#define SBR_STUN_TID_LEN 12u
#define SBR_STUN_TRIES 3u
#define SBR_STUN_WAIT_MS 500u
#define SBR_STUN_KEEPALIVE_MS 15000u
#define SBR_STUN_MAX_REPLY 512u          /* receive buffer the public client uses */

enum { SBR_NAT_UNKNOWN = 0, SBR_NAT_CONE = 1, SBR_NAT_SYMMETRIC = 2 };
enum { SBR_CAND_HOST = 0, SBR_CAND_SRFLX = 1, SBR_CAND_OBSERVED = 2 };

struct sbr_stun_addr { sbrs_u32 ip; sbrs_u16 port; };          /* ip host order: a.b.c.d = a<<24|b<<16|c<<8|d */
struct sbr_stun_cand { int type; sbrs_u32 ip; sbrs_u16 port; };

/* writes 20 bytes: type 0x0001, length 0, cookie, tid[12] */
void sbr_stun_build_request(sbrs_u8 *out, const sbrs_u8 *tid);

/* cheap demux for the shared UDP socket: 1 when d looks like a STUN message (first two bits 0, cookie at offset 4, length field fits).
   An ENet datagram is rejected by the cookie (it would need 4 chosen bytes) and ENet from a known peer is matched on ip:port first anyway. */
int sbr_stun_is_stun(const sbrs_u8 *d, sbrs_u32 len);

/* 1 and *out set for a Binding Success Response matching req (20 bytes built above): else 0 (not STUN, wrong type, cookie, transaction id,
   truncated attribute, no IPv4 mapped address) */
int sbr_stun_parse_response(const sbrs_u8 *req, const sbrs_u8 *resp, sbrs_u32 len, struct sbr_stun_addr *out);

/* two servers asked on the same socket: equal = cone, different = symmetric, a missing answer = unknown */
int sbr_stun_nat_type(int have1, const struct sbr_stun_addr *a1, int have2, const struct sbr_stun_addr *a2);
const char *sbr_stun_nat_name(int nat);       /* "cone" | "symmetric" | "unknown" */

/* "a.b.c.d:port" + NUL; returns the length (without NUL) or -1 when cap is too small */
int sbr_stun_addr_str(sbrs_u32 ip, sbrs_u16 port, char *out, sbrs_u32 cap);
/* the join message's candidates value: [{"type":"host","ip":"a.b.c.d","port":N},...]; n == 0 gives length 0 and out[0] = 0 (the key is then omitted).
   Returns the length (without NUL) or -1 when cap is too small (out is then an empty string). */
int sbr_stun_cands_json(const struct sbr_stun_cand *c, sbrs_u32 n, char *out, sbrs_u32 cap);

#endif
