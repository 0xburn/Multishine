/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REP_KEEP_H
#define REP_KEEP_H
/* rep_keep: the RESIDENT half of the Slippi game-report client: constants, the queue entry, the counters, the HELLO clock and
   `struct rep_keep`, with the enqueue (rep_q.c).  No TCP / TLS headers: the resident image includes only this.  Strict C89, no libc, no float. */
typedef unsigned char rep_u8;
typedef unsigned short rep_u16;
typedef unsigned int rep_u32;

/* ---- constants (all -D tunable) ---- */
#ifndef REP_HOST
#define REP_HOST "internal.slippi.gg"
#endif
#ifndef REP_PORT
#define REP_PORT 443
#endif
#ifndef REP_PATH
#define REP_PATH "/graphql"
#endif
#ifndef REP_REPORT_DIRECT
#define REP_REPORT_DIRECT 1     /*: Direct is reported like Unranked (the bridge gets a matchId from get-ticket-resp for Direct too; the stock client has no mode filter) */
#endif
#ifndef REP_ISOHASH_FROM_CARD
#define REP_ISOHASH_FROM_CARD 1 /* 1: env->iso_hash (the card-file MD5 of the image the card was built for); 0: always the empty string (what the stock client sends while its hasher is unfinished) */
#endif
#ifndef REP_CLIENT_NAME
#define REP_CLIENT_NAME "Multishine"      /* User-Agent: "<name> (b: <build>) (v: 3.7.0) (o: <platform>)": the stock format with our own honest name */
#endif
#ifndef REP_SLIPPI_VERSION
#define REP_SLIPPI_VERSION "3.7.0"
#endif
#define REP_QUEUE 4             /* reports waiting in RAM; the oldest is dropped when a fifth arrives */
#define REP_MAX_ATTEMPTS 5      /* stock client: 5 tries, then the report is dropped */
#define REP_RETRY_STEP_MS 100u  /* stock client: wait n x 100 ms after attempt n (HTTP-level failures) */
#define REP_RECONNECT_MS_1 30000u   /* after a TCP/TLS-level failure: new session no sooner than 30 s, then 2 min, then 10 min */
#define REP_RECONNECT_MS_2 120000u
#define REP_RECONNECT_MS_3 600000u
#define REP_AGE_MAX_MS 1800000u /* a report older than 30 minutes is dropped */
#define REP_WEB_COOLDOWN_MS 600000u /* after UNTRUSTED / HOST / CERT: the web is broken for us, no session for 10 minutes */
#define REP_REQ_TIMEOUT_MS 5000u    /* stock client: 5 s per request */
#define REP_SESSION_TIMEOUT_MS 20000u   /* TCP + TLS handshake deadline */
#define REP_REQ_MAX 2048
#define REP_RSP_MAX 1536        /* response body kept for parsing; a longer answer counts as an error */
#define REP_ID_MAX 40           /* uid / play key / isoHash buffers (characters, no NUL) */
#define REP_MID_MAX 51          /* the ticket's matchId (characters, no NUL): Slippi's own EXI device hands the game 51 bytes (EXI_DeviceSlippi.cpp matchId.resize(51)) and the bridge keeps 51 + NUL (sbr.h match_id[52]); real ids seen: 'mode.direct-2026-10-11T08:07:13.52Z-1' (37).  Review M8: was 40. */

/* ---- result of rep_enqueue ---- */
#define REP_Q_OK 0
#define REP_Q_OFF 1             /* our server did not announce capability bit 15 to this console: nothing queued, nothing will be sent */
#define REP_Q_MODE 2            /* Ranked / Teams (and Direct when REP_REPORT_DIRECT is 0): never reported */
#define REP_Q_BAD 3             /* malformed (match id characters, short buffer) */

/* ---- offsets in the engine's report buffer rgb[369] (Online.s RGB_*; on_backend.c od_report_game_cb) ---- */
#define REP_RGB_LEN 369
#define REP_RGB_MODE 1          /* u8 online mode: 0 Ranked, 1 Unranked, 2 Direct, 3 Teams */
#define REP_RGB_FRAMES 2        /* u32 BE */
#define REP_RGB_GAME 6          /* u32 BE, 1-based game index as Slippi's hook writes it */
#define REP_RGB_TIEBREAK 10     /* u32 BE */
#define REP_RGB_WINNER 14       /* s8 */
#define REP_RGB_ENDM 15         /* u8 */
#define REP_RGB_LRAS 16         /* s8 */
#define REP_RGB_PLAYERS 21      /* four 9-byte records: slot type, stocks, damage (float32 BE), synced stocks, synced health (u16) */
#define REP_RGB_INFO 57         /* game info block (312 bytes) */
#define REP_INFO_STAGE 0x0E     /* u16 BE inside the info block */
#define REP_INFO_PLAYER0 0x60   /* player blocks, 0x24 bytes each: +0 character, +2 starting stocks, +3 colour (the .slp Game Start layout; [U] until diffed against Dolphin) */
#define REP_INFO_PLAYER_STEP 0x24

struct rep_player {
    rep_u8 slot_type, stocks, char_id, color_id, start_stocks, who;     /* who: 0 = us (uid from the card), 1 = the opponent (uid from the ticket), 2 = none known */
    rep_u32 damage_milli;                                               /* percent x 1000, from the float32 by integer math (no float on the console) */
};

struct rep_rec {
    rep_u8 used, mode, attempts, np;
    rep_u16 stage;
    rep_u32 frames, game, tiebreak, t_enq;
    signed char winner, lras;
    rep_u8 endm, pad;
    char match_id[REP_MID_MAX + 1];
    char opp_uid[REP_ID_MAX + 1];
    struct rep_player pl[4];
};

struct rep_stats {
    rep_u32 enq, off, mode_refused, bad, full_dropped;
    rep_u32 attempts, ok, success_false, graphql_errors, http_status_bad, parse_bad, rsp_timeout;
    rep_u32 tcp_fail, tls_fail, dns_fail, no_ident, no_mem;
    rep_u32 dropped_max, dropped_age, dropped_cert, dropped_off;
    rep_u32 held_scene, held_time, held_cooldown, held_ent, scene_aborts, stale_retry;
    rep_u32 sessions, heavy_skipped;
    rep_u8 last_tls_err, last_cert_err, last_sec_err, last_tcp_err;
    rep_u16 last_http;                                  /* status code of the last complete HTTP answer (0 none yet) */
    char last_msg[24];                                  /* first characters of the last answer's error message (or of its body), menu-font safe (letters, numerals, space. , ' - :); never a key or id */
};

/* the RESIDENT part: it outlives the report overlay (rep_q.c).  `ms` and `server` are resident functions (valid while the engine runs). */
struct rep_clock { rep_u32 srv_time, stamp_ms, last_seen; rep_u8 have; };
struct rep_keep {
    rep_u32 (*ms)(void);                                /* the sdnet clock */
    int (*server)(void);                                /* the signed HELLO's capability bit 15 for THIS console (the only gate): 1 announced, 0 not announced, -1 no HELLO answered yet (a game is queued meanwhile, nothing is sent; the first answer without the bit flushes the queue) */
    struct rep_rec q[REP_QUEUE];
    struct rep_stats st;
    struct rep_clock clk;
    rep_u32 next_ms, t_cooldown_until;                  /* back-off and web cool-down (sdnet clock): they must outlive the overlay, which is unloaded while nothing is due */
    rep_u8 fails_tls;
    int cur;                        /* queue index of the report in flight, -1 none (the enqueue never evicts it) */
};


void rep_keep_init(struct rep_keep *k, rep_u32 (*ms)(void), int (*server)(void));
int rep_enabled(const struct rep_keep *k);          /* 0 only when our server answered without capability bit 15 (unknown counts as enabled: queue, do not send) */
int rep_pending(const struct rep_keep *k);          /* reports waiting or in flight */
rep_u32 rep_damage_milli(const rep_u8 *f32be);                                      /* float32 percent -> thousandths, integer math, clamped */
/* Build a queue entry from rgb[369].  opp_uid: the opponent's Slippi uid from the ticket (copied into RAM, never logged); match_id: the ticket's matchId.
   my_idx / opp_idx: player indices 0..3 of the two humans.  Returns REP_Q_*. */
int rep_enqueue(struct rep_keep *k, const rep_u8 *rgb, const char *match_id, const char *opp_uid, int my_idx, int opp_idx);
void rep_clock_init(struct rep_clock *c);
/* call each frame with the mnc facts: have_hello, srv_time (Unix seconds), now_ms. */
void rep_clock_note(struct rep_clock *c, int have_hello, rep_u32 srv_time, rep_u32 now_ms);
rep_u32 rep_clock_unix(const struct rep_clock *c, rep_u32 now_ms);                   /* 0 = none */
#define REP_CAP_SLIPPI_REPORTS 0x8000u          /* HELLO caps bit 15 (ms_core.CAP_SLIPPI_REPORTS) */
#endif
