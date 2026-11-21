/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GNREC_H
#define GNREC_H
/*
 * gnrec: the console-side uploader of SPECTATE stage S1 (and 2.5, server: the server's op GN_REC 0x3F).
 * HOST-TESTED ONLY: it is not linked into any console image yet (same status as mnu.c; see  "S1 as built" for what the console wiring still needs).
 *
 * What it does: every GNREC_EVERY (12) frames, in a Game Night set of a spectatable room, it builds ONE datagram with the last GNREC_WIN (24) pads of this console
 * (own ring) and the last 24 pads of the opponent (remote ring), plus up to GNREC_CHK (2) finalized-frame checksums (every 16th frame), seals it with the wire v2 session
 * and hands it to the io send callback.  It reads the engine's EXISTING rings (s7_core.h ltag/lpad/rtag/rpad, hist_frame/hist_cks); it keeps no second recorder.
 *
 * Zero-risk rules: send and forget (one sendto-class call, never a receive, never a wait, never a retry); at most one
 * datagram per call and one per GNREC_EVERY frames (5/s at 60 fps, about 19 kbit/s); skipped on a rollback frame (the next window repeats the pads); a send error or a server
 * that stays silent (only when the caller feeds replies) switches the uploader off for good, silently; a kill switch.  Cost per datagram: 2 x 24 pad copies, the HMAC of
 * a 442-byte datagram; nothing on the other 11 frames but a compare.
 *
 * C89, no libc, no heap, caller-owned buffers. Layout of the payload: (pack_rec is the reference, gnrec_build must produce the same bytes).
 */
#include "mnc.h"

#ifndef MNC_S32_DEFINED
#define MNC_S32_DEFINED
typedef int mnc_s32;                /* the engine's s3_s32 */
#endif

#define GNREC_OP 0x3Fu
#define GNREC_VER 1u
#define GNREC_EVERY 12              /* frames between datagrams */
#define GNREC_WIN 24u               /* pads per stream per datagram */
#define GNREC_CHK 2u                /* checksum entries per datagram */
#define GNREC_PAD 8u                /* S3_PAD_BYTES */
#define GNREC_SEL_MAX 32u
#define GNREC_SEL_REPEAT 3u         /* the selections blob rides in the first 3 datagrams (loss) */
#define GNREC_F_SEL 1u
#define GNREC_F_END 2u
#define GNREC_PAYLOAD_MAX (18u + 1u + GNREC_SEL_MAX + 2u * GNREC_WIN * GNREC_PAD + 8u * GNREC_CHK)    /* 18 fixed + sel length + sel + pads + checks = 451 with a selections blob, 418 without */
#define GNREC_FAIL_MAX 8u           /* consecutive send errors before the uploader gives up */
#define GNREC_SILENT_MAX 60u        /* datagrams sent since the last reply (only counted when replies are fed): about 12 s at 5/s */
#define GNREC_BACKOFF 120           /* frames to pause after RATE_LIMITED (2 s: the gap stays inside the engine's 256-tag rings, where the ring repair can still send it; it was 600 = a permanent hole once the history had overflowed) */
/* ON DEMAND (Live-ish / On demand): the server says in the GN_REC reply flags whether somebody watches (WATCHED) or the game is wanted at its end (ARCHIVE) */
#define GNREC_RF_WATCHED 8u
#define GNREC_RF_ARCHIVE 16u
#define GNREC_RF_ONDEMAND 32u       /* the server understands idle uploaders: until a reply carries this bit the uploader streams as before (an older server never sets it) */
#define GNREC_IDLE_EVERY 120        /* frames between heartbeats while nobody watches (2 s: 18 payload bytes + checks, no pads) */
#define GNREC_BL_MS 100u            /* backlog / flush datagrams: one per 100 ms (10/s; the server allows a watched game's uploader 16/s) */
#define GNREC_BL_TAGS 24u           /* tags per backlog datagram, OWN and OPPONENT pads both (2 x 24 x 8 + 18 = 402 payload bytes, the size of a window datagram; the server accepts 48 per stream).  Until the backlog was the own pads only
                                       (48 per datagram): that is complete only when the opponent's console uploads too; a single uploader (the other side a bot, a console with Spectate Upload off) left a hole in the opponent's stream for every
                                       tag before the watcher came, and the live watch never started */

/* the engine's rings, read only (struct s3_core in s7_core.h: ltag/lpad, rtag/rpad, hist_frame/hist_cks); ring sizes are powers of two */
struct gnrec_rings {
    mnc_u32 mask;                   /* S3_RING - 1 */
    const mnc_s32 *ltag; const mnc_u8 *lpad; mnc_s32 lnewest;      /* own pads: tag f at index f & mask, newest enqueued tag */
    const mnc_s32 *rtag; const mnc_u8 *rpad; mnc_s32 rlatest;      /* opponent pads as received (never predicted) */
    const mnc_s32 *hframe; const mnc_u32 *hcks; mnc_u32 hmask;      /* finalized frame -> Slippi checksum history (S3_CKS_HIST - 1) */
    mnc_s32 fin;                    /* the newest finalized frame (0 = none yet) */
};

enum { GNREC_OFF = 0, GNREC_ON = 1, GNREC_DEAD = 2 };      /* DEAD: gave up (error, silence, refused by the server, kill switch) until the next arm */

typedef struct gnrec {
    mnc_u8 state, game, end_req, expect_replies, sel_len, sel_left;
    mnc_u32 set_id;
    mnc_s32 last_frame;             /* frame of the last datagram (or the arm baseline) */
    mnc_s32 pause_until;            /* RATE_LIMITED: no datagram before this frame */
    mnc_u32 fails, since_reply;
    mnc_u32 st_sent, st_rollback, st_notready, st_replies, st_dead_why;
    /* on demand */
    mnc_u8 on_demand, watched, archive, flushing, od_ok, overflow;
    mnc_u8 *hist;                   /* the first hist_cap tags of the game, tag t at (t-1)*16: own pad (8 bytes) then the opponent's pad as received (8 bytes); caller-owned, hist_cap * 16 bytes; NULL = no backlog (a watcher then joins at the live window only) */
    mnc_u32 hist_cap, hist_n, hist_own, hist_opp, bl_next, bl_ms, st_idle, st_backlog, srv_complete, st_ring;      /* srv_complete: the newest GN_REC reply's merged-log length; st_ring: ring repair datagrams */      /* hist_n = tags held for BOTH sides (the backlog limit) */
    mnc_u8 sel[GNREC_SEL_MAX];
    mnc_u8 buf[MW2_HDR + GNREC_PAYLOAD_MAX + MW2_MAC];
} gnrec;

void gnrec_init(gnrec *r);
/* a Game Night set of a SPECTATABLE room starts (set_id, game from the room's view / assignment); sel = the MATCH_SELECTIONS copy (0..32 bytes, may be NULL) */
void gnrec_arm(gnrec *r, mnc_u32 set_id, unsigned game, const mnc_u8 *sel, unsigned sel_len, mnc_s32 frame);
void gnrec_kill(gnrec *r, unsigned why);                  /* the kill switch (flag, ejected card, menu); sticks until the next arm */
void gnrec_finish(gnrec *r);
/* the closing datagram without pads (18 bytes payload, 42 on the wire: it fits the menu-thread send queue): flags END, nothing else.  1 = sent, 0 = nothing to do.  Used at the game's end (BD) when no B0 call follows. */
int gnrec_close(gnrec *r, mw2 *w, const mnc_io *io, mnc_u32 now_ms);                              /* the game ended: the next call sends the closing datagram with the end flag, then the uploader stops */
/* the payload builder (pure, no session): returns the payload length (0 = nothing worth sending); out needs GNREC_PAYLOAD_MAX bytes */
mnc_u32 gnrec_build(gnrec *r, const struct gnrec_rings *v, mnc_u8 *out);
/* called once per B0 call after the engine updated its rings.  Returns 1 when a datagram was handed to io->send, 0 when nothing was due / skipped, -1 when it just gave up */
int gnrec_step(gnrec *r, mw2 *w, const mnc_io *io, const struct gnrec_rings *v, mnc_s32 frame, int in_rollback, mnc_u32 now_ms);
/* switch the uploader to ON DEMAND (call after gnrec_arm): idle = one heartbeat per GNREC_IDLE_EVERY frames, streaming only while the server says WATCHED; hist (hist_cap * 16 bytes, may be NULL) is filled with
   the console's own pads AND the opponent's pads as received, from frame 1 and is what a watcher who joins mid-game gets as backlog; with ARCHIVE set at the end of the game (gnrec_finish) the whole game is flushed before the closing datagram */
void gnrec_set_ondemand(gnrec *r, mnc_u8 *hist, mnc_u32 hist_cap);
/* the reply with its flags byte (GNREC_RF_*): keeps the watchdog quiet like gnrec_reply and drives the idle / streaming switch and the backlog */
void gnrec_reply2(gnrec *r, unsigned status, mnc_u32 complete, unsigned flags);
/* optional: the glue read a reply (status per ms_wire.STATUS, complete = tags the server merged); keeps the silence watchdog quiet, switches off on a refusal */
void gnrec_reply(gnrec *r, unsigned status, mnc_u32 complete);
#endif
