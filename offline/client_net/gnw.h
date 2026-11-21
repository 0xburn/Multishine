/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GNW_H
#define GNW_H
/*
 * gnw: the console-side WATCHER of "Watch Last Game" (2.3 / 3 / 11): the wire client of the server ops GN_SPEC 0x3B,
 * GN_SPEC_HEAD 0x3C, GN_SPEC_RANGE 0x3E and the pushed GN_SPEC_FEED 0x3D (the server's is the reference), the tag ring the feed fills, and the two engine
 * rewrites of the spectator (the same as the harness-proven ): the B0 receive block with BOTH game ports remote and
 * local index 2, and the B3 block markers.  It lives in the Game Night match-time block ms/gns.ovl (menus/gns), never in the resident image.
 *
 * Zero-risk rules (our): the watcher is a separate console; nothing here touches a player's match.  It never blocks, sends only small requests (GN_SPEC, HEAD, RANGE;
 * at most 4 RANGE per second), resends a lost SPEC / HEAD with the SAME sealed datagram, treats every byte of a reply as hostile (the feed parser is a port of
 * ms_spec.parse_feed with every length and flag checked), and leaves the match (B0 result 3) on a lost sync (a mismatch of the engine's finalized-frame checksum with the
 * players' series), a stream that stays silent too long, or the end of the log.
 *
 * C89, no libc, no heap, caller-owned buffers.
 */
#include "mnc.h"

#ifndef MNC_S32_DEFINED
#define MNC_S32_DEFINED
typedef int mnc_s32;                /* the engine's s3_s32 */
#endif

#define GNW_OP_SPEC 0x3Bu
#define GNW_OP_HEAD 0x3Cu
#define GNW_OP_FEED 0x3Du
#define GNW_OP_RANGE 0x3Eu
#define GNW_VER 1u
#define GNW_RING 256u               /* tags held: the server never sends more than 256 beyond what we say we have (ms_spec.AHEAD_MAX) */
#define GNW_FEED_TAGS 24u
#define GNW_FEED_CHK 3u
#define GNW_RANGE_MAX 96u           /* tags one RANGE may ask for */
#define GNW_CHK_N 64u               /* check entries kept (one per 16 frames: 17 s) */
#define GNW_DESC_MAX 100u           /* 8 words + 3 + 2 x p12 + 2 x (1 + 14) = 96 */
#define GNW_SEL_LEN 14u             /* S7_SEL_LEN */
#define GNW_RESEND_MS 700u
#define GNW_TRIES 5u
#define GNW_RANGE_EVERY_MS 250u     /* at most 4 per second (the server's limit); also the progress report */
#define GNW_SILENT_MS 8000u         /* RUN: no new tag for this long while tags are missing: the stream is dead */
#define GNW_SILENT_LIVE_MS 40000u   /* a live game: the server closes an idle one after 30 s, then the watcher ends cleanly; only a stream silent longer than that is dead */
#define GNW_CATCH_LAG 20            /* live: more than this many received frames ahead of the one being played = answer ADVANCE (Slippi's RESP_ADVANCE: the engine ticks twice in the next refresh, 2x until the watcher is at the feed's head: start live(ish)) */
#define GNW_LEAD_LIVE 8             /* live: tags buffered before the first B0 (the server already holds us D = 3 s behind the players, so the lead is kept short) */
#define GNW_SF_LIVE 16u             /* GN_SPEC reply flag: the game is still being played; frames = the visible head */
#define GNW_RF_CALLED 4u            /* ... the watcher is called up (their turn in the room): the console shows the toast */
#define GNW_RF_LIVE 2u              /* GN_SPEC_RANGE reply flags (ms_spec.RF_*) */
#define GNW_LEAD 30                 /* tags buffered before the first B0 is let through (half a second at 60 fps) */
#define GNW_DF_WORDS 1u             /* descriptor flags (ms_spec DF_*) */
#define GNW_DF_SEL 2u
#define GNW_DESC_MAGIC 0xD35C0001u
#define GNW_SF_RTYPE 32u            /* GN_SPEC reply flag (ms_spec.SF_RTYPE): the game was played in a Game Night room; HEAD can add the room-type block */
#define GNW_HEAD_EXT_RTYPE 1u       /* GN_SPEC_HEAD request, trailing byte: send the room-type block (ms_spec.HEAD_EXT_RTYPE) */
#define GNW_RT_LEN 5u               /* the block: ver 1 | room type | game index (0-based) | patch flags (bit 0 Zelda down-B does not transform) | items hint */
#define GNW_RTP_ZELDA 1u

enum { GNW_OFF = 0, GNW_SPEC = 1, GNW_HEAD = 2, GNW_READY = 3, GNW_RUN = 4, GNW_DONE = 5, GNW_FAIL = 6 };
/* why the watcher failed (gnw.why); the resident turns it into the CSS's error line */
enum { GNW_W_NONE = 0, GNW_W_NOSESSION, GNW_W_TIMEOUT, GNW_W_NOGAME, GNW_W_NOSUPPORT, GNW_W_FULL, GNW_W_RATE, GNW_W_BAD, GNW_W_DESC, GNW_W_STADIUM, GNW_W_SILENT, GNW_W_SYNC };

typedef struct gnw {
    mnc_u8 state, why, status, game, flags, delay10, cap, desc_flags;
    mnc_u8 called, leave_req;               /* called: the last RANGE reply said the watcher is called up; leave_req: the player (hold B) or the toast (hold Z) ends the watch: the next B0 leaves the match */
    mnc_u8 live;                            /* 1 while the watched game is still being played (frames grows, the end of the log is not the end of the game) */
    mnc_u8 digest[4];
    mnc_u8 rt_ask, rt_have, rt_type, rt_game, rt_patches, rt_items;      /* the room-type block of the watched game (rt_ask: the server said it has one, rt_have: it came): the watcher applies the same per-match patches as the players */
    mnc_u32 set_id, frames;                 /* the kept game: its id and the number of merged frames */
    mnc_u32 have;                           /* contiguous tags held from 1 */
    mnc_u32 desc_len;
    mnc_u8 desc[GNW_DESC_MAX];
    mnc_u32 words[8];                       /* DF_WORDS descriptor */
    mnc_u8 sel[2][GNW_SEL_LEN]; mnc_u8 sel_n[2];     /* DF_SEL: the two players' MATCH_SELECTIONS (side 0, side 1) */
    char names[2][13];
    /* the request in flight (SPEC / HEAD): resent as is */
    mnc_u8 pkt[48];
    mnc_u32 pkt_len, ctr, sent_ms, tries;
    mnc_u8 op;
    /* the tag ring */
    mnc_s32 rtag[GNW_RING];
    mnc_u8 pad[GNW_RING][16];               /* port 0 pad (8) | port 1 pad (8) */
    mnc_s32 floor;                          /* tags below this are consumed and may be overwritten */
    mnc_u32 chk_f[GNW_CHK_N], chk_c[GNW_CHK_N];
    mnc_s32 cmp_hi;
    mnc_u32 range_ms, last_new_ms, last_have;
    mnc_u32 calls; mnc_s32 last_frame;
    mnc_s32 lag, lag_max;                   /* live: frames the watcher is behind the players' real time (visible head + D - the frame being played), and its maximum */
    /* proof counters */
    mnc_u32 st_feed, st_dup, st_bad, st_range, st_stall, st_fed, st_cmp, st_cmp_bad, st_first_bad, st_leave;
    mnc_u32 st_adv;                         /* frames answered with ADVANCE (live catch-up: two game ticks in one refresh) */
} gnw;

void gnw_init(gnw *g);
/* start: sends GN_SPEC (the room's last game).  0 = started, else a GNW_W_* (no session ...) */
int gnw_begin(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now);
/* start the LIVE variant: GN_SPEC action A_LIVE, the room's game that is still being played (S3 live-ish).  Same states; the match starts at frame 1 and plays at 1x
   (a game already running is watched from its start, constant lag = the time it had run when the watcher joined + D). */
int gnw_begin_live(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now);
/* GN_SPEC action A_FRIEND: the live match of the friend with this connect code (outside Game Night); the same states and the same feed as begin_live.  Every refusal is NOT_FOUND. */
int gnw_begin_friend(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now, const char *code);
/* the watcher's toast actions: decline a pending call-up (GN_SPEC action 3, the server applies the missed-call-up rule); leave = the next B0 ends the match (hold B / accept) */
int gnw_decline(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now);
void gnw_leave(gnw *g);
/* every menu / game pass: resends, the HEAD request after the SPEC reply, the RANGE requests while running.  Returns the state. */
int gnw_tick(gnw *g, mw2 *w, const mnc_io *io, mnc_u32 now);
/* a server datagram: 1 = consumed (a reply or a feed datagram of ours), 0 = not ours */
int gnw_rx(gnw *g, mw2 *w, const mnc_u8 *d, mnc_u32 n, mnc_u32 now);
/* the engine's B0 call (after the real s3_core_b0 filled rxb): txb = the 26-byte TXB, rxb = the receive block (rewritten in place).  Returns the result byte written to rxb[0]. */
int gnw_b0(gnw *g, const mnc_u8 *txb, mnc_u8 *rxb);
/* B3 post-processing for the DF_WORDS descriptor (the demo recording): overwrites the match block fields from the words; the markers msrb[3] = 2, msrb[4] = 0 are set for every descriptor.
   0 = done, < 0 = refused.  DF_SEL descriptors are served by the engine itself from the two selections (the glue swaps them into the core for the call). */
int gnw_b3(gnw *g, mnc_u8 *msrb, int markers);   /* markers 0: the character select (it reads msrb[3] as its own player index): only the descriptor fields */
/* the CSS answer while the watcher is not READY: fills msrb (state 2 searching, or 5 + the text) and returns 1; 0 = READY / RUN: ask the engine */
int gnw_css(const gnw *g, mnc_u8 *msrb);
const char *gnw_why_text(unsigned why);
/* parsers (ports of ms_spec.parse_feed / parse_spec_reply / parse_head_reply): 0 ok, < 0 refused */
int gnw_parse_feed(const mnc_u8 *b, mnc_u32 n, mnc_u32 *set_id, mnc_u8 *game, mnc_u8 *digest, mnc_u32 *first, mnc_u8 *count, mnc_u8 *flags, const mnc_u8 **pads,
                   mnc_u8 *nchk, mnc_u32 *chk_f, mnc_u32 *chk_c);
#endif
