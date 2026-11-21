/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LNK_H
#define LNK_H
/*
 * lnk: the console side of "Link This Console": LINK_START 0x10, LINK_POLL 0x11 with the SEALED
 * reply (the console's ephemeral X25519 key; the server encrypts the 16-byte device secret to it, the server's ) and LINK_CANCEL 0x13.
 *
 * It runs its OWN anonymous wire v2 session (mw2) over the hub client's socket (mnc io) and pinned keys: every reply of an anonymous session carries the server's Ed25519
 * signature under the PINNED key, so the server half of the key agreement (s_pub) cannot be replaced on the way, and a passive listener never sees the secret.  (The hub
 * client's own session may be a guest DEVICE session whose secret went over the wire in the clear: not used for this.)
 * Same style as gnc.c: C89, no libc, no heap, never blocks, one request at a time; a lost reply is asked again with the SAME sealed datagram (the server's reply cache
 * answers a repeat). Every reply is hostile input: exact lengths, printable strings, the tag must open. Host-tested against the real server (tests/lnk_driver.c)
 * and the Python reference.
 */
#include "mnc.h"

#define LNK_OP_START 0x10
#define LNK_OP_POLL 0x11
#define LNK_OP_CANCEL 0x13
#define LNK_OP_FORGET 0x1A          /* DEVICE_FORGET (the server's ): in the hub client's DEVICE session */
#define LNK_TRIES 4u
#define LNK_CAP_SEALED 4096u        /* HELLO capability bit 12 (ms_wire.CAP_LINK_SEALED) */
#define LNK_CAP_FORGET 1024u        /* bit 10: DEVICE_FORGET 0x1A */

/* errors (l->err) */
#define LNK_E_NONE 0
#define LNK_E_BUSY 1
#define LNK_E_ARG 2                 /* bad argument, no pinned key, no start reply yet */
#define LNK_E_TIMEOUT 3             /* no (valid) reply after LNK_TRIES sends */
#define LNK_E_SERVER 4              /* status != 0 in l->status (ms_wire STATUS: 21 LINK_PENDING, 22 LINK_EXPIRED, 23 SLOW_DOWN, 27 DEVICE_LIMIT, 5 IDENTITY_BANNED, 3 RATE_LIMITED, 40-43 busy) */
#define LNK_E_BAD_REPLY 5           /* malformed, or the sealed secret did not open */
#define LNK_E_HS 6                  /* the handshake was refused (status in l->status) */

#define LNK_ST_PENDING 21
#define LNK_ST_EXPIRED 22
#define LNK_ST_SLOW_DOWN 23
#define LNK_ST_DEVICE_LIMIT 27
#define LNK_ST_BANNED 5
#define LNK_ST_RATE 3

enum { LNK_IDLE = 0, LNK_BUSY = 1, LNK_DONE = 2, LNK_FAILED = 3 };

typedef struct lnk {
    mnc *c;                         /* io (the shared socket) and the version / platform / ids of the client block; its session is NOT used */
    mw2 w;                          /* our anonymous session */
    mw2 *sw;                        /* the session of the op in flight: &w, or the hub client's device session (DEVICE_FORGET) */
    int state, err, status;
    mnc_u8 op, phase;               /* phase 1 handshake, 2 request */
    mnc_u32 timeout_ms, sent_ms, ctr, tries;
    mnc_u8 req[96];                 /* the request payload, sealed once the session is ready */
    mnc_u32 req_len;
    mnc_u8 pkt[160];                /* the sealed request, resent as is */
    mnc_u32 pkt_len;
    mnc_u8 rx[MNC_MAX_PACKET];
    /* LINK_START result */
    char user[10];                  /* "ABCD-EFGH" */
    mnc_u8 dc[16];                  /* device code */
    mnc_u16 expires;                /* seconds */
    mnc_u8 interval;                /* seconds between polls */
    mnc_u8 have_start;
    /* LINK_POLL result (state DONE after a poll): the identity; secret wiped by lnk_wipe */
    char code[24], name[24];
    mnc_u8 dev[8], secret[16], server_id[8];
    mnc_u8 c_priv[32], c_pub[32];   /* the ephemeral key of this link attempt (lnk_set_key) */
    mnc_u32 st_sent, st_resent, st_rx, st_bad;
} lnk;

/* c: the hub client (io, pinned keys, client ids); seed: anything unpredictable for the handshake nonce (16 bytes from the entropy pool) */
void lnk_init(lnk *l, mnc *c, const mnc_u8 *seed, mnc_u32 n);
/* the ephemeral X25519 private key of this attempt (32 bytes from the entropy pool's SESSION level); the public half is computed here */
void lnk_set_key(lnk *l, const mnc_u8 priv[32]);
int lnk_start(lnk *l, mnc_u32 now);         /* LINK_START: DONE with user / dc / expires / interval */
int lnk_poll_req(lnk *l, mnc_u32 now);      /* LINK_POLL (sealed): DONE with code / name / dev / secret; FAILED + status for PENDING / SLOW_DOWN / EXPIRED / ... */
int lnk_cancel(lnk *l, mnc_u32 now);        /* LINK_CANCEL, one send, no wait (fire and forget): the state becomes IDLE at once */
/* DEVICE_FORGET (guard 0xA5, flags) in the hub client's READY device session (c->w; run its HELLO first): DONE = the server forgot this device; FAILED + status otherwise
   (AUTH_FAILED 4 / TOKEN_REVOKED 26 also mean the server no longer knows it) */
int lnk_forget(lnk *l, mnc_u32 now, unsigned flags);
int lnk_poll(lnk *l, mnc_u32 now);          /* drive: -> state */
void lnk_abort(lnk *l);
void lnk_wipe(lnk *l);                      /* zero the whole struct (keys, secret, session) */

/* pure: open a sealed LINK_POLL reply body (b, n) with the device code and the console's key pair; fills code / name / dev / secret / server id.  0 ok, -1 malformed,
   -2 the tag does not open (or a low-order server key) */
int lnk_open_reply(const mnc_u8 *b, mnc_u32 n, const mnc_u8 dc[16], const mnc_u8 c_priv[32], const mnc_u8 c_pub[32],
                   char code[24], char name[24], mnc_u8 dev[8], mnc_u8 secret[16], mnc_u8 sid[8]);
#endif
