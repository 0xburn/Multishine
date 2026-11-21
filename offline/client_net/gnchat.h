/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GNCHAT_H
#define GNCHAT_H
/*
 * gnchat: the console client of the Game Night ROOM CHAT op GN_CHAT 0x47 (server: the server's wire):
 *   FETCH  after u32            -> n u8 (<= 6) | n x (id u32 | age u16 | name p12 | text p60), oldest first (the room's lines after the cursor, blocks applied; a web guest's name ends " (web)")
 *   PRESET idx u8 (0..15)       -> id u32   (one of the sender's own 16 chat texts; the server posts the text)
 * Errors: NOT_FOUND (not in a room), CHAT_INVALID (the host muted you), CHAT_RATE_LIMITED (6 lines per 10 s), UNKNOWN_OP (a server without rooms).
 * One request at a time on a mw2 session, a lost datagram is resent as is (the server answers a repeated counter from its reply cache, so a preset is never posted twice).
 * Two users, so it is NOT tied to an mnc: the sign overlay's room lobby (pull: gnchat_pump reads the socket) and the Game Night match-time block ms/gns.ovl (push: its rx hook
 * hands the datagram to gnchat_rx).  C89, no libc, no heap, never blocks.  Host-tested against the real server: tests/test_gnchat.py.
 */
#include "mnc.h"

#define GNCHAT_OP 0x47u
#define GNCHAT_MAX 6u               /* lines per FETCH reply (ms_roomchat.wire) */
#define GNCHAT_NAME 13u
#define GNCHAT_TXT 61u
#define GNCHAT_RESEND_MS 700u
#define GNCHAT_TRIES 4u
#define GNCHAT_RATE_N 6u            /* the server's limit: 6 posts per 10 s per player (ms_roomchat.RATE, RATE_SPAN) */
#define GNCHAT_RATE_MS 10000u

/* statuses the screens tell apart (ms_wire.STATUS) */
#define GNCHAT_ST_UNKNOWN_OP 2
#define GNCHAT_ST_RATE_LIMITED 3
#define GNCHAT_ST_NOT_FOUND 16
#define GNCHAT_ST_NOT_SUPPORTED 18
#define GNCHAT_ST_CHAT_INVALID 19   /* the host muted you (ms_roomchat maps MUTED to CHAT_INVALID) */
#define GNCHAT_ST_CHAT_RATE 28      /* CHAT_RATE_LIMITED: more than 6 lines in 10 s */

enum { GNCHAT_IDLE = 0, GNCHAT_BUSY = 1, GNCHAT_DONE = 2, GNCHAT_FAILED = 3 };
enum { GNCHAT_E_NONE = 0, GNCHAT_E_BUSY, GNCHAT_E_ARG, GNCHAT_E_NOSESSION, GNCHAT_E_TIMEOUT, GNCHAT_E_SERVER, GNCHAT_E_BAD_REPLY };
enum { GNCHAT_K_FETCH = 0, GNCHAT_K_PRESET = 1 };

struct gnchat_msg { mnc_u32 id; mnc_u16 age; char name[GNCHAT_NAME]; char text[GNCHAT_TXT]; };

typedef struct gnchat {
    mnc_u8 state, err, kind, tries, n;
    int status;
    mnc_u32 ctr, sent_ms, pkt_len, post_id;
    mnc_u8 pkt[40];
    struct gnchat_msg m[GNCHAT_MAX];    /* FETCH: n lines */
    mnc_u32 st_resent, st_bad;
} gnchat;

void gnchat_init(gnchat *g);
/* start a request; 0 = sent, else GNCHAT_E_* (no session, bad index) with state FAILED */
int gnchat_fetch(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now, mnc_u32 after);
int gnchat_preset(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now, unsigned idx);
/* a server datagram: 1 = it was the reply of the request in flight (consumed), 0 = not ours */
int gnchat_rx(gnchat *g, mw2 *w, const mnc_u8 *d, mnc_u32 n, mnc_u32 now);
/* resends the request in flight, fails it after GNCHAT_TRIES; returns the state */
int gnchat_tick(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now);
/* pull model (the sign overlay): reads datagrams from io->recv into gnchat_rx, then gnchat_tick */
int gnchat_pump(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now);
void gnchat_abort(gnchat *g);
/* the reference parser (ms_roomchat.wire): 0 ok, < 0 refused (a count over 6, a cut line, a non-printable character, an empty text, id 0) */
int gnchat_parse_fetch(const mnc_u8 *b, mnc_u32 n, mnc_u8 *count, struct gnchat_msg *out);

/* the send limit of the server, kept by the caller: ring of the last GNCHAT_RATE_N send times.  gnchat_rate_ok = 1 when another line may go out now (and records it when `take`). */
struct gnchat_rate { mnc_u32 t[GNCHAT_RATE_N]; mnc_u8 n, head; };
int gnchat_rate_ok(struct gnchat_rate *r, mnc_u32 now, int take);
#endif
