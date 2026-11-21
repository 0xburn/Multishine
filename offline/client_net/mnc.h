/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNC_H
#define MNC_H

/*
 * mnc: the console-side client of the Multishine server's binary UDP protocol.
 * Portable C89, no libc, no heap, integer only, fixed buffers, big-endian byte access, never blocks.
 *
 * Use: fill an mnc_io with a send and a recv callback (the console's sdnet UDP socket, or a host socket), mnc_init, start ONE
 * operation (mnc_hello / mnc_enrol / ... / mnc_find) and call mnc_poll(m, now_ms) from the frame loop until it returns MNC_DONE or
 * MNC_FAILED.  poll does at most MNC_RX_BUDGET receives and one resend per call.  Reliability is ours: the same request (same seq)
 * is resent every timeout_ms, max_tries sends in total; the server answers a repeated seq from its reply cache.
 *
 * Everything that arrives is untrusted: a reply is accepted only with the exact header (magic 0x6D, version 1, op|0x80 of the
 * request in flight, the seq in flight), the exact body length and well-formed fields; anything else is dropped (counted) and
 * the timer decides. Text lists go through the same sanitising, cache and hash rules as.
 */

#include "mw2.h"

typedef unsigned char mnc_u8;
typedef unsigned short mnc_u16;
typedef unsigned int mnc_u32;

#define MNC_MAX_PACKET 512
#define MNC_N 16                    /* texts per list */
#define MNC_TXT 26                  /* 25 characters + NUL */
#define MNC_MAXC 25
#define MNC_CACHE 4                 /* lists of other players kept (the hashes advertised as "have") */
#define MNC_MAX_PLAYERS 4
#define MNC_STR 24                  /* connect code / name buffers (accepts up to 23 characters) */
#define MNC_RX_BUDGET 4
#define MNC_DEFAULT_TIMEOUT_MS 500u
#define MNC_DEFAULT_TRIES 4u
#define MNC_POLL_INTERVAL_MS 2000u  /* ticket poll */
#define MNC_FIND_MAX_MS 300000u     /* hard cap of a find when the server's timeout is 0 or absurd */
#define MNC_FIND_SLACK_MS 6000u
#define MNC_RECORD_SIZE 428         /* "SDCT" v1 record */

/* wire constants */
#define MNC_OP_HELLO 0x01
#define MNC_OP_ENROL 0x02
#define MNC_OP_LOGOUT 0x03
#define MNC_OP_PROFILE 0x04
#define MNC_OP_CHAT_SET 0x05
#define MNC_OP_CHAT_GET 0x06
#define MNC_OP_TICKET_CREATE 0x07
#define MNC_OP_TICKET_POLL 0x08
#define MNC_OP_TICKET_CANCEL 0x09
#define MNC_OP_CHAT_FETCH 0x0A
#define MNC_OP_WHOAMI 0x0B
#define MNC_OP_ROOM_RESERVE 0x0C
#define MNC_OP_ROOM_RELEASE 0x0D
#define MNC_OP_GAME_REPORT 0x0F
#define MNC_OP_LAB_DIAG 0x1E        /* console readout lines of a test build (server lab_diag.log): up to MNC_LAB_DIAG_MAX bytes, empty reply */
#define MNC_LAB_DIAG_MAX 200
#define MNC_OP_GAME_RECORD 0x1B     /* our own 7-day copy of a game of OUR modes: 54-byte body, empty reply, never a Slippi-route game */
#define MNC_OP_LINK_START 0x10
#define MNC_OP_LINK_POLL 0x11
#define MNC_OP_MATCH_STATUS 0x15
#define MNC_OP_GUEST_START 0x17     /* guest tier: anonymous session, reply = the ENROL credentials */

#define MNC_MODE_RANKED 0
#define MNC_MODE_UNRANKED 1
#define MNC_MODE_DIRECT 2
#define MNC_MODE_ROOM 5
#define MNC_MODE_CONSOLE 6

/* server status codes the client reacts to (full list: ms_wire.STATUS) */
#define MNC_ST_OK 0
#define MNC_ST_AUTH_FAILED 4
#define MNC_ST_TICKET_EXISTS 14
#define MNC_ST_NO_TICKET 15
#define MNC_ST_LINK_PENDING 21
#define MNC_ST_LINK_EXPIRED 22
#define MNC_ST_SLOW_DOWN 23
#define MNC_ST_TICKET_TIMEOUT 24
#define MNC_ST_SERVER_FULL 17
#define MNC_ST_BUSY_10 40               /* busy statuses with a retry hint in the name (seconds); an old client shows them as a generic refusal ( C3) */
#define MNC_ST_BUSY_30 41
#define MNC_ST_BUSY_120 42
#define MNC_ST_MAINT 43                 /* maintenance: do not retry soon */
#define MNC_ST_COOLDOWN_15 50           /* : matchmaking paused after repeated mid-game leaving; the number is the minutes still to wait (rounded up): 15, 60, 240, 1440 */
#define MNC_ST_COOLDOWN_60 51
#define MNC_ST_COOLDOWN_240 52
#define MNC_ST_COOLDOWN_1440 53
#define MNC_ST_NO_LINK_MATCH 54         /*  9: a 'wired opponents only' search found nobody within 60 s and was dropped by the server */
#define MNC_ST_ACCOUNT_NEEDED 46        /* a guest asked for something that needs a linked account */
#define MNC_REHS_JITTER_MS 1500u        /* C1: random delay before a new handshake after SESSION_UNKNOWN */

/* client errors (m->err) */
#define MNC_E_NONE 0
#define MNC_E_BUSY 1                /* start refused: an operation is running */
#define MNC_E_ARG 2                 /* start refused: bad argument or missing identity */
#define MNC_E_TIMEOUT 3             /* no (valid) reply after max_tries sends */
#define MNC_E_BAD_REPLY 4           /* like timeout, but only malformed replies came */
#define MNC_E_SERVER 5              /* the server answered with m->status != 0 */
#define MNC_E_DEADLINE 6            /* find/link: the overall time ran out */
#define MNC_E_TICKET 8              /* a ticket named an address we refuse to connect to (see mnc_set_net) */
#define MNC_E_HASH 7                /* the server's hash of our list is not ours */

enum mnc_state { MNC_IDLE = 0, MNC_BUSY = 1, MNC_DONE = 2, MNC_FAILED = 3 };

typedef struct mnc_io {
    /* send one datagram to the server: 0 = handed over, < 0 = could not (counts as a lost datagram) */
    int (*send)(void *ctx, const mnc_u8 *p, mnc_u32 len);
    /* receive one datagram into buf (cap bytes): > 0 length, 0 = none waiting, < 0 = error (treated as none) */
    int (*recv)(void *ctx, mnc_u8 *buf, mnc_u32 cap);
    void *ctx;
} mnc_io;

typedef struct mnc_player {
    mnc_u8 slot;
    char name[MNC_STR];
    char code[MNC_STR];
    mnc_u32 ip;                     /* host order, 192.168.1.2 == 0xC0A80102 */
    mnc_u16 port;
    mnc_u32 lan_ip;                 /* 0 = none */
    mnc_u16 lan_port;
    mnc_u32 chat_hash;
    mnc_u8 custom;                  /* 1 = the list advertised for this player is not the default list */
} mnc_player;

typedef struct mnc_list {           /* one cached list of another player */
    mnc_u32 key;                    /* the hash the server advertises it under */
    mnc_u32 age;                    /* 0 = empty slot, larger = more recently used */
    char t[MNC_N][MNC_TXT];
} mnc_list;

typedef struct mnc {
    mnc_io io;
    /* configuration */
    mnc_u32 timeout_ms, max_tries;
    mnc_u8 version[3], platform;
    mnc_u32 build_id, codeset, rules;
    mnc_u16 caps;                   /* ours (client_recog.h CR_CAP_*) */
    int (*drawable)(mnc_u8 c);      /* NULL = printable ASCII except quote, backslash, backtick; the console passes cc_drawable */
    /* identity */
    mnc_u8 have_id, dev[8], tok[16], server_id[8];   /* server_id: set by HELLO / ENROL / LINK_POLL */
    char code[MNC_STR], name[MNC_STR];
    /* server facts (HELLO) */
    mnc_u8 have_hello, srv_latest[3], srv_min[3];
    mnc_u16 srv_caps;
    mnc_u8 srv_caps2;               /* HELLO extension byte (we ask for it): bit 2 = Slippi matchmaking switched off (MNC_SRV_CAPS2_SLIPPI_MM_OFF) */
    mnc_u32 srv_time;
    int update_available;
    /* operation in flight */
    int state, err, status;
    mnc_u8 op, task;
    mnc_u32 sent_ms, tries;
    mnc_u8 bad_seen, inflight, phase, rehs;
    mnc_u16 req_len, pay_len;
    mnc_u32 own_ip, own_mask, srv_ip;
    mnc_u8 lax_addr;
    mnc_u8 lab_samenat;             /* LAB builds only (-DMNC_LAB_SAMENAT): the opponent may have the server's address; always 0 in a release build */
    mw2 w;                          /* wire v2 session,, Wire v2 */
    mnc_u8 req[MNC_MAX_PACKET];
    mnc_u8 rx[MNC_MAX_PACKET];
    /* results of single operations */
    mnc_u32 whoami_ip;
    mnc_u16 whoami_port;
    mnc_u32 profile_hash;
    mnc_u8 profile_flags;
    char link_user[MNC_STR];
    mnc_u8 link_dev[16];
    mnc_u16 link_expires;
    mnc_u8 link_interval;
    mnc_u8 ticket_state;            /* 0 queued, 1 assigned */
    mnc_u16 ticket_timeout, seconds_left;
    mnc_u16 room_ttl;
    mnc_u8 rep_game, rep_match;     /* GAME_REPORT reply: game state (0 open, 1 agreed, 2 dispute, 3 desync), match state; MATCH_STATUS: rep_match */
    /* lists */
    char defaults[MNC_N][MNC_TXT];
    mnc_u32 def_hash;
    char own[MNC_N][MNC_TXT];
    mnc_u32 own_hash;
    char fetched[MNC_N][MNC_TXT];   /* result of mnc_chat_get */
    mnc_list cache[MNC_CACHE];
    mnc_u32 age_clock;
    mnc_u32 rng;                    /* jitter generator (mnc_rand) */
    /* match (valid when mnc_find is DONE) */
    mnc_u8 match_id[8], is_host, my_slot, nplayers;
    mnc_player pl[MNC_MAX_PLAYERS];
#ifdef MNC_SETUP_BLOCK
    /* MATCH SETUP (valid when su_have, with the match): the server's canonical match settings, TICKET_POLL ext bit 1.
       su_alt = the engine's S7R_ALT_* (0 chooser, 1 off, 2 frozen); su_flags bit 0 = pause off; 0 stocks / time = the defaults.  An older server sends none.
       OFF in the console builds until the hub overlay ABI step (struct mnc is part of the resident/overlay layout check, hub_ovl.h sz_mnc: growing it makes every
       card's current hub.ovl refuse the engine, and the Update row lives in hub.ovl).  Host tests build it on. */
    mnc_u8 su_have, su_flags, su_alt, su_stocks, su_n;
    mnc_u16 su_time;
    mnc_u32 su_items;
    mnc_u16 su_stages[16];
#endif
    /* find / link-wait task */
    mnc_u8 f_stage, f_next;
    mnc_u32 f_deadline, f_next_at, f_fetch_base;
    mnc_u32 l_interval_ms;
    mnc_u8 f_mode, f_retried;
    char f_target[19];
    mnc_u16 f_gport, f_lan_port;
    mnc_u32 f_lan_ip;
    /* statistics */
    mnc_u32 st_sent, st_resent, st_rx, st_ignored, st_bad;
    mnc_u32 st_chat[3];             /* accepted CHAT_FETCH answers: hash only, delta, full */
    /* Game Night hand-off: the sign overlay (Game Night lobby, which already drives this client) arms a room search here,
       the hub overlay's B4 answer searches it, the result report marks it done, the menu returns to the lobby.  Kept across mnc_init (a reload of the hub overlay).
       gn_magic = MNC_GN_MAGIC once this block exists: an overlay checks it before it writes (an older resident has no such block). */
    mnc_u32 gn_magic;
    mnc_u8 gn_state;                /* MNC_GN_* */
    mnc_u8 gn_sets;                 /* sets handed off since boot (proof) */
    char gn_room[20];               /* the call-up ticket name GN<code>S<set>, NUL terminated */
    mnc_u8 f_open;                  /* appended a queue search (Unranked / Ranked) has no total limit: every "still waiting" poll moves the deadline on (the server's
                                       timeoutSeconds is its IDLE budget, not a cap); the caller sets it before mnc_find */
} mnc;
#define MNC_GN_MAGIC 0x474E4831u    /* 'GNH1' */
#define MNC_GN_FLAGS_AT 19u         /* gn_room[19] = the hand-off flags (gns_ovl.h GNS_ROOM_FLAGS); bits 0x20 / 0x10 = the room type's code patch is on / refused (GNS_RF_ZON / GNS_RF_ZERR), set by the resident: the ticket poll tells the server */
#define MNC_GN_ZON 0x20u
#define MNC_GN_ZERR 0x10u
#define MNC_GN_STAGE_AT 14u         /* gn_room[14] = the stage of a room game (the room view's stage), valid when the room name is at most 13 characters (gn_room[13] == 0); 0 = none: the engine draws as before */
#define MNC_UPLOAD_AT 18u           /* gn_room[18] while no Game Night hand-off is armed (gn_room[0] == 0): 'U' = the server said upload this Dojo / Grandmaster / Mano a mano match (TICKET_POLL ext bit 3;: the resident uploader reads it, no struct change) */
#define MNC_UPLOAD_MARK 'U'
enum { MNC_GN_NONE = 0, MNC_GN_ARMED = 1, MNC_GN_SEARCH = 2, MNC_GN_DONE = 3,
       MNC_GN_DIRECT = 4,            /* a Mano A Mano answer (toast accept, Friends screen challenge accepted): gn_room = the connect code the next lock-in searches in Direct mode */
       MNC_GN_WATCH = 5,
       MNC_GN_LEFT = 6,
       MNC_GN_ADDFR = 7,             /* Friends Z: the next Direct lock-in's typed code is a friend request, not a search */
       MNC_GN_ADDFR_DONE = 8,
       MNC_GN_STAND = 10,            /* Y on the Grandmaster character screen (hub_soc.c stand_draw): the screen is left for the menu, whose entry opens Standings; cleared by the hub overlay in the menu scene */
       MNC_GN_RKSET = 9 };           /* a Grandmaster SET is paired: the menu scene shows the set's prep screen (stage strikes, bans, picks), then arms the next game (ARMED + MNC_RK_FLAG) */      /* that code is in gn_room: the Friends list sends the request when it opens again */            /* the player left the character screen of a WATCH on purpose (hold B): the menu does NOT return to the room; acknowledged (-> NONE) by the menu entry or the next browser open */           /* "X: Last Game" in a spectatable room: the next lock-in on the character screen WATCHES the room's last game; gn_room = "GNWATCH", gn_room[19] = GNS_ROOM_FLAGS */
/* Grandmaster sets (piece G2).  The set's facts ride in the hand-off block's tail while gn_room[0] == 0 (no struct change): [MNC_RK_GAME_AT] the number of the last game this console started (0 = none yet),
   [MNC_RK_STAGE_AT] the stage of the game being armed (external id), [MNC_RK_LEN_AT] the games of the set, [17] / [18] the character and costume the next game is played with (GNS_ROOM_PICK / GNS_ROOM_COSTUME),
   [19] the flag byte, bit MNC_RK_FLAG = this hand-off belongs to a ranked set.  mnc_rk_set (outside struct mnc, reset by mnc_find) = the set length the last assigned ranked reply announced (TICKET_POLL ext bit 4), 0 = best of 1. */
#define MNC_RK_GAME_AT 1u
#define MNC_RK_LEN_AT 2u
#define MNC_RK_STAGE_AT 3u
#define MNC_RK_FLAGS_AT 19u
#define MNC_RK_FLAG 0x40u
#define MNC_TICKET_EXT_SET 1u
#define MNC_TICKET_EXT_LINK 4u      /* ext bit 2 (Unranked only, only to a server whose HELLO set caps2 bit 4): two bytes follow the optional region byte: link class (0 unknown, 1 wired, 2 Wi-Fi, 3 other), preference (0 none, 1 wired opponents only, 2 Wi-Fi only) */
#define MNC_TICKET_EXT_REGION 2u    /* ext bit 1 (CSS region chip): one more byte follows the ext byte = the region of THIS ranked search, 0..6 = NAE NAW EU LA AS OC NAC (ms_region.CODES); omitted for Auto */
extern mnc_u8 mnc_link_cls, mnc_link_pref;   /* the next UNRANKED ticket's link class (0 unknown, 1 wired, 2 Wi-Fi) and preference (1 = wired opponents only); the hub overlay sets them before the search (on_backend.c on_link_ticket); 0 = nothing is sent.  Static, like mnc_region_chip */
extern mnc_u8 mnc_region_chip;   /* the CSS region chip's value for the next ranked tickets: 0 Auto (nothing is sent: the account's region), 1..7 NAE NAW EU LA AS OC NAC (the byte sent is chip - 1; the hub chip writes it, 0..7 only) */
#define MNC_POLL_EXT_SET 16u
#ifdef MNC_RKSET
extern mnc_u8 mnc_rk_set;
#endif
#ifdef MNC_WATCH_TELL
/* TICKET_POLL ext bit 3: 1 when the last assigned reply told this console to upload the match with GN_REC (the player has a friend who may watch).  Outside struct mnc on
   purpose (the struct is layout-checked: no ABI step); reset by mnc_find.  Not defined in any console build yet: the resident engine does not arm an uploader from it (the hand-off to the engine needs the hub ABI step). */
extern mnc_u8 mnc_watch_upload;
#endif
#ifdef MNC_POLL_INVITE
/* TICKET_POLL ext bit 2: a Direct search's challenge status from the last queued reply: 0 nothing yet, 1 accepted (the other player searches
   us now), 2 not answered.  Outside struct mnc on purpose (the struct is part of the resident / overlay layout check); reset by mnc_find. */
extern mnc_u8 mnc_invite_status;
#endif

/*  option A: with a key pair and an opener set, ENROL / GUEST_START send the console's X25519 public key (when the server's HELLO announced bit 14) and
   the device secret comes back sealed to it.  open_fn is lnk_open_reply (client_net/lnk.c; the caller links the crypto and fills pub = x25519(priv, 9)); the context is
   "ms-enrol-v1" / "ms-guest-v1" padded to 16 bytes.  The key serves ONE successful reply (wiped and cleared after it: the next enrol is cleartext unless a new key is set); mnc_set_seal(0, 0, 0) clears.  Without it: the old cleartext reply. */
#define MNC_HELLO_EXT_CAPS2 1u       /* HELLO request trailing byte: append the caps2 byte to the reply */
#define MNC_SRV_CAPS2_SLIPPI_MM_OFF 4u
#define MNC_SRV_CAPS2_LINKPREF 16u        /* the server reads the LINK extension of a ticket (link class + preference) */
#define MNC_SRV_CAPS2_WHYFAIL 8u          /* the server reads the why-it-failed record behind the failed match id of a ticket */
#define MNC_SRV_CAP_SEAL 0x4000u    /* HELLO caps bit 14 (CAP_SEAL_CREDS) */
typedef int (*mnc_seal_open_fn)(const mnc_u8 *b, mnc_u32 n, const mnc_u8 ctx[16], const mnc_u8 c_priv[32], const mnc_u8 c_pub[32],
                                char code[24], char name[24], mnc_u8 dev[8], mnc_u8 secret[16], mnc_u8 sid[8]);
void mnc_set_seal(const mnc_u8 priv[32], const mnc_u8 pub[32], mnc_seal_open_fn open_fn);

/* ---- setup ---- */
/* defaults: 16 NUL-terminated strings of 1..25 characters (the console's chat block); NULL = ".".  build/version identify the
   client; platform 1 console, 2 Wii vBBA; codeset/rules are the u32 compatibility ids the server checks. */
void mnc_init(mnc *m, const mnc_io *io, const char defaults[MNC_N][MNC_TXT], mnc_u32 build_id, mnc_u32 codeset, mnc_u32 rules, mnc_u32 seq_seed);
/* wire v2: the server's pinned Ed25519 public key(s), 1 or 2 x 32 bytes (SD config server_key, server_key2).  Without one nothing is sent (MNC_E_ARG).
   mnc_seed_entropy: anything unpredictable (time base, frame counter, card serial); only used for nonces, never for secrets. */
void mnc_set_server_keys(mnc *m, const mnc_u8 *keys, int n);
/* network facts for ticket address checks: our address and netmask (host order), the server's address; lax != 0 allows loopback, the
   server's and our own address as peers (a lab on one host, host tests).  Defaults: all zero, strict: no LAN hint is accepted without a subnet. */
#define MNC_LAX_ALL 1               /* mnc_set_net 'lax' bit 0: lab on one host (host tests) */
#define MNC_LAX_SAMENAT 2           /* bit 1, honoured only when built with -DMNC_LAB_SAMENAT: server and seat behind one NAT */
void mnc_set_net(mnc *m, mnc_u32 own_ip, mnc_u32 own_mask, mnc_u32 server_ip, int lax);
void mnc_seed_entropy(mnc *m, const mnc_u8 *data, mnc_u32 len);
void mnc_set_identity(mnc *m, const mnc_u8 dev[8], const mnc_u8 tok[16], const char *code, const char *name);
void mnc_clear_identity(mnc *m);
/* our own 16 texts, sanitised on the way in; returns the number of slots that fell back to the default */
int mnc_set_texts(mnc *m, const char texts[MNC_N][MNC_TXT]);

/* ---- operations: 0 = started, else MNC_E_BUSY / MNC_E_ARG.  Drive with mnc_poll, then read m->state / m->err / m->status. ---- */
int mnc_hello(mnc *m, mnc_u32 now);
int mnc_whoami(mnc *m, mnc_u32 now);
int mnc_enrol(mnc *m, mnc_u32 now, const char *wanted_name, const char *tag);
/* guest tier: a short-lived server-minted token (no entropy, no card write); DONE = the identity in force is the guest (m->dev/tok/code/name) */
int mnc_guest(mnc *m, mnc_u32 now);
int mnc_link_start(mnc *m, mnc_u32 now);           /* shows m->link_user; then mnc_link_wait */
int mnc_link_wait(mnc *m, mnc_u32 now);            /* polls every interval (SLOW_DOWN adds 5 s) until approved, expired or deadline */
int mnc_profile(mnc *m, mnc_u32 now);
int mnc_chat_set(mnc *m, mnc_u32 now);             /* uploads m->own */
int mnc_chat_get(mnc *m, mnc_u32 now);             /* into m->fetched */
int mnc_logout(mnc *m, mnc_u32 now);               /* best effort: a repeat after a lost reply counts as done; identity cleared */
int mnc_room_reserve(mnc *m, mnc_u32 now, const char *room, const char *guest, mnc_u16 ttl);
int mnc_room_release(mnc *m, mnc_u32 now);
/* Ranked result reports (the server's ): `blob` is the 52-byte report of ms_ranked.pack_report (the caller builds it,
   evidence hash included); the match id is the one of the last find (m->match_id).  mnc_match_status: code 1 connecting, 2 abandoned, 3 disconnected,
   4 poor performance, 5 set complete.  Replies: m->rep_game / m->rep_match. */
int mnc_game_report(mnc *m, mnc_u32 now, const mnc_u8 *blob, mnc_u32 len);
#define MNC_GAME_RECORD_LEN 54
#define MNC_GAME_RECORD_MAX 103      /* the 54 bytes + optional trailing blocks; an older server ignores the trailing bytes (a longer block is read for its known fields) */
/* GAME_RECORD (ms_records.REQ): the whole body (54 bytes, or 54 + trailing blocks up to MNC_GAME_RECORD_MAX) is the caller's; the reply is empty (fire and forget; a repeat of the same (identity, match id, game) is ignored by the server) */
int mnc_game_record(mnc *m, mnc_u32 now, const mnc_u8 *blob, mnc_u32 len);
int mnc_lab_diag(mnc *m, mnc_u32 now, const mnc_u8 *blob, mnc_u32 len);
int mnc_match_status(mnc *m, mnc_u32 now, mnc_u8 code);
int mnc_cancel(mnc *m, mnc_u32 now);               /* TICKET_CANCEL (also aborts a running find) */
/* the match id (8 bytes) whose peer connection failed: the next mnc_find's ticket carries it (the server then keeps that pair apart in the queues); 0 clears */
void mnc_note_connect_failed(const mnc_u8 *id);
/* WHY-IT-FAILED record, sent behind the failed match id only when the server's HELLO set MNC_SRV_CAPS2_WHYFAIL: 8 words tx_dgrams, rx_dgrams, rx_drop_allow,
   rx_drop_peer, connects_out, punch_n, last dropped source ip, its port | connect window in 1.024 s units << 16 (the pointer is kept, not the words).  0 = none; cleared with mnc_note_connect_failed(0) */
void mnc_note_why(const mnc_u32 *v);
/* The whole queue step before the character select: create ticket, poll every 2 s, then fetch every other player's list.
   mode MNC_MODE_*, target = the opponent's code (direct) or room name (room), "" otherwise; game_port = our game socket's port
   (0 = let the server use the source port); lan_ip/lan_port 0 = none.  DONE: m->pl[], m->my_slot, m->is_host valid. */
int mnc_find(mnc *m, mnc_u32 now, int mode, const char *target, mnc_u16 game_port, mnc_u32 lan_ip, mnc_u16 lan_port);

/* retry helpers (the callers' backoff): mnc_rand = a 32-bit jitter value; mnc_jitter = base +-20%; mnc_busy_hint = seconds the server asks us to wait for a busy status
   (SERVER_FULL counts as 30), 0 for any other status */
mnc_u32 mnc_rand(mnc *m);
mnc_u32 mnc_jitter(mnc *m, mnc_u32 base_ms);
int mnc_busy_hint(int status);

/* frame-loop entry: returns m->state */
int mnc_poll(mnc *m, mnc_u32 now);
/* give up the running operation or task (an old reply is then ignored: the next start uses a new seq); state -> IDLE */
void mnc_abort(mnc *m);

/* ---- results ---- */
/* the 16 texts to show for m->pl[i] (cached list, or the defaults when it was not delivered); never NULL */
const char (*mnc_player_texts(const mnc *m, int i))[MNC_TXT];

/* ---- texts and records (public for the host tests and the console's card/SD code) ---- */
mnc_u32 mnc_crc32(const mnc_u8 *p, mnc_u32 len);
mnc_u32 mnc_slot_hash(int index, const char *text);
mnc_u32 mnc_list_hash(const char t[MNC_N][MNC_TXT]);
/* 0 ok, 1 missing/short, 2 bad magic, 3 version/count, 4 bad crc; 100+n = ok but n slots repaired.  Never fails: out is always usable. */
int mnc_record_load(mnc *m, const mnc_u8 *blob, mnc_u32 len, char out[MNC_N][MNC_TXT]);
void mnc_record_save(const mnc *m, const char t[MNC_N][MNC_TXT], mnc_u8 out[MNC_RECORD_SIZE]);

/* ---- peer hello (ENet message 0xD0, offline/chat/client_recog.h): bytes only, the tag comes from the caller ---- */
mnc_u32 mnc_peer_hello(const mnc *m, mnc_u8 *out, mnc_u32 cap, mnc_u32 nonce, mnc_u32 tag);   /* returns 20, or 0 if cap is short */
int mnc_peer_hello_ok(const mnc_u8 *p, mnc_u32 len, mnc_u16 *caps_out);                       /* 1 = well-formed, caps cleared to known bits */
int mnc_peer_may_send(unsigned msg_id, int peer_level);

#endif
