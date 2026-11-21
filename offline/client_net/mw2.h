/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MW2_H
#define MW2_H
/*
 * mw2: wire v2 session layer of the console client.  Pure: no I/O, no clock of its own,
 * no libc, no heap. The caller (mnc.c) owns packets, timers and retries; this file only builds and checks them.
 *
 *   handshake   client -> server  HS1 (96 bytes: device id + client nonce + tag, or anonymous)
 *               server -> client  HS2 (91 bytes: session id, server nonce, ttl, Ed25519 signature by the PINNED server key)
 *   data        12-byte header (magic, ver 2, op, flags/status, sid, ctr), payload, then
 *               device session: 12-byte HMAC-SHA256 tag (client key for requests, server key for replies)
 *               anonymous session: requests unauthenticated, replies carry a 64-byte Ed25519 signature
 *   A reply is accepted only if its tag/signature verifies; the server's identity is its pinned key (or, in a device session,
 *   knowledge of the device secret); the secret never goes on the wire after it was issued.
 */
#include "mw2_crypto.h"

#define MW2_HDR 12
#define MW2_MAC 12
#define MW2_SIG 64
#define MW2_HS1_LEN 96
#define MW2_HS2_LEN 91
#define MW2_MAX_PACKET 512
#define MW2_MAX_PAYLOAD (MW2_MAX_PACKET - MW2_HDR - MW2_MAC)

#define MW2_OP_HS 0x70
#define MW2_OP_UPD_FETCHU 0x32      /* OTA chunk fetch whose status-0 reply in an ANONYMOUS session has no signature (mw2_open returns 3), see mnu.h and  9.4 */
#define MW2_ST_SESSION_UNKNOWN 31

enum { MW2_NONE = 0, MW2_HS_SENT = 1, MW2_READY = 2 };

typedef struct mw2 {
    mw2_u8 pin[2][32];
    int npin;
    int have_dev;
    mw2_u8 dev[8], secret[16];
    mw2_u8 pool[32];
    mw2_u32 pool_ctr;
    int state, dev_sess;
    mw2_u32 sid, ctr, ttl_ms, last_ok;
    int pin_used;
    mw2_u8 cn[16], sn[16], kc[32], ks[32];
    mw2_u8 hs1[MW2_HS1_LEN];
    mw2_u32 st_bad;                 /* replies that failed verification */
} mw2;

void mw2_init(mw2 *m);
void mw2_set_pins(mw2 *m, const mw2_u8 *keys, int n);                 /* n <= 2 keys of 32 bytes */
void mw2_seed(mw2 *m, const mw2_u8 *data, mw2_u32 n);                  /* mix in anything unpredictable (time base, frame counter, card serial) */
/* credentials for the next session: dev/secret (8/16 bytes) or both NULL for an anonymous session; drops the session if they change */
void mw2_use(mw2 *m, const mw2_u8 *dev, const mw2_u8 *secret);
void mw2_drop(mw2 *m);
int mw2_ready(const mw2 *m, mw2_u32 now);
/* HS1: returns the 96 bytes to send (same bytes on every resend of this handshake) */
const mw2_u8 *mw2_hs1(mw2 *m, mw2_u32 now);
/* HS2 (or an unsigned status reply to it): 1 = session ready, 0 = ignore, < 0 = -(status), the status of a server refusal */
int mw2_hs2(mw2 *m, mw2_u32 now, const mw2_u8 *p, mw2_u32 n);
/* p holds the payload at p + MW2_HDR (plen bytes); writes header and trailer around it, returns the datagram length; m->ctr is its counter */
mw2_u32 mw2_seal(mw2 *m, mw2_u8 *p, mw2_u8 op, mw2_u32 plen);
/* reply check for the request sealed with counter ctr: 1 = authentic reply (status in *status, body at p + MW2_HDR, *blen bytes),
   2 = the server forgot our session (unauthenticated notice), 0 = ignore,
   3 = (op MW2_OP_UPD_FETCHU, anonymous session, status 0 only) a reply that is NOT authenticated: header echo (sid, counter) matched and nothing else.  Only a caller that
   authenticates the payload by other means (mnu.c: the SHA-256 of the whole file from a signed UPD_CHECK reply) may use it; every other caller treats 3 as "ignore".
   It never refreshes the session's liveness (last_ok), and errors of that op (status != 0) still need the signature. */
int mw2_open(mw2 *m, mw2_u32 now, const mw2_u8 *p, mw2_u32 n, mw2_u8 op, mw2_u32 ctr, mw2_u8 *status, mw2_u32 *blen);
void mw2_wipe(mw2 *m);
#endif
