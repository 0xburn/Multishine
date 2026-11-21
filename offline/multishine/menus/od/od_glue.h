/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* od_glue: the 'Direct' controller of the 'Multishine online-direct' image.
 *
 * Portable C89, no libc, no heap, never blocks, no globals: one `struct od` owned by the caller (the EXI backend od_backend.c on the
 * console, a line-command driver on the host).  It turns "the player confirmed a connect code" (EXI B4) into
 *   mnc_find(direct, code)  ->  the server pairs both players  ->  peer address + side  ->  the caller connects the engine
 * and tells the Slippi menu what to show (EXI B3 states 1..3, error text) until the engine's own B3 answer takes over (state 4).
 *
 * Two callers, never concurrent on the console (both run with interrupts off on the one core):
 *   game thread (EXI shim)  od_request / od_cancel / od_b3
 *   network slice (frame)   od_net_up / od_rx / od_tick / od_take_peer / od_core_report
 * The network side is two things the caller provides: `send` (one datagram to the server from the game socket) and the clock.
 * Datagrams from the server are handed over with od_rx (the caller routes by source address; the same socket also carries the peer game).
 */
#ifndef OD_GLUE_H
#define OD_GLUE_H

#include "mnc.h"

struct sddns;                                /* offline/net/sddns.h (od_glue.c includes it) */

/* ---- configuration block ('ODCFG001', patched into the image by od_patch.py; production: built from the card record) ------------- */
#define OD_CFG_MAGIC0 0x4F444346u            /* 'ODCF' */
#define OD_CFG_MAGIC1 0x47303031u            /* 'G001' */
#define OD_HOST_MAX 48                       /* the server host name: up to 47 characters + NUL, [A-Za-z0-9.-] */
#define OD_CFG_SIZE 232
struct od_cfg {
    unsigned int magic0, magic1;             /* +0x00 */
    unsigned int flags;                      /* +0x08 bit0 = identity valid (dev/tok/code/name set), bit1 = lab: accept loopback/own-host peers (host tests only), bit2 = serve Unranked too */
    unsigned int server_ip;                  /* +0x0C host order */
    unsigned int server_port;                /* +0x10 0 = 43150 */
    unsigned int find_ms;                    /* +0x14 overall time for "find an opponent" before we give up and cancel; 0 = 60000 */
    unsigned int connect_ms;                 /* +0x18 time for the engine to reach the peer once the ticket named it; 0 = 20000 */
    unsigned int net_ms;                     /* +0x1C time to wait for an address (DHCP) before "no network"; 0 = 20000 */
    unsigned int build_id;                   /* +0x20 version a.b.c in the top three bytes (the server's gate) */
    unsigned int codeset, rules;             /* +0x24 +0x28 compatibility ids the server checks */
    unsigned int seed;                       /* +0x2C mixed into the nonce pool (the console adds the time base) */
    unsigned char pin[2][32];                /* +0x30 pinned server keys (Ed25519), the second may be zero */
    unsigned char dev[8];                    /* +0x70 device id */
    unsigned char tok[16];                   /* +0x78 device secret (never leaves the card/image except as a MAC key) */
    char code[24];                           /* +0x88 own connect code, e.g. AB#1234 */
    char name[24];                           /* +0xA0 own display name */
    char host[OD_HOST_MAX];                  /* +0xB8 server host name (NUL terminated), used when server_ip is 0 and OD_CFGF_HOST is set; all zero = none */
};                                           /* 0xE8 = 232 = OD_CFG_SIZE (the first 184 bytes are the old layout, the same magic: an all-zero tail means "no host") */
#define OD_CFGF_ID 1u
#define OD_CFGF_LAX 2u
#define OD_CFGF_UNRANKED 4u
#define OD_CFGF_RANKED 8u                    /* serve the Ranked row too (B4 mode 0 = a ranked ticket) and send its result report */
#define OD_CFGF_HOST 16u                     /* host[] holds a valid server name (the card record carries the optional host extension, on_cfgrec.h); the numeric server_ip wins when it is non-zero */
#ifdef MNC_LAB_SAMENAT                       /* LAB builds only (MANIFEST option labnat=on): bit 5 = the server and the opponent seat sit behind one NAT, so an opponent with the server's address is accepted; not in the card mask */
#define OD_CFGF_SAMENAT 32u
#define OD_LAXV(f) ((((f) & OD_CFGF_LAX) ? 1 : 0) | (((f) & OD_CFGF_SAMENAT) ? 2 : 0))
#else
#define OD_LAXV(f) (((f) & OD_CFGF_LAX) ? 1 : 0)   /* release: the same-NAT bit does not exist */
#endif
#define OD_CFGF_SRVLOCK 64u                  /* patched only (tools/card/patch_odcfg.py --lock): the server, port, host name and pinned keys of THIS block win over the
                                                card record's; the card's identity is used only when its pinned key equals a patched one, else the console is a guest; the card's backup
                                                server is ignored.  Not in the card mask: a card can neither set nor clear it */
#define OD_CFGF_FIXPORT 128u                 /* patched only: the game socket keeps s3_cfg.local_port (43200); without it a console binds 43201..44199, a fresh port per boot (a NAT or firewall on the path held state per 5-tuple: repeats with the same ports failed) */
/* the game port of this boot from a seed (clock, MAC): 43201..44199, away from the server's 43150/43151 and the DNS sockets' 49152+ */
#define OD_GAME_PORT(seed) ((unsigned short)(43201u + ((((unsigned int)(seed)) * 2654435761u) >> 7) % 999u))   /* (a macro: the C89 builds of od_glue have no inline) */
#define OD_CFGF_RING 256u                    /* patched only: a Wii keeps the full punch ring (default on a Wii: ring off, a carrier firewall may take the port sweep for a scan); a GameCube always has it */
#define OD_CFGF_CARD_MASK (OD_CFGF_ID | OD_CFGF_UNRANKED | OD_CFGF_RANKED | OD_CFGF_HOST)   /* the flags a memory-card config may set (never LAX) */

/* ---- phases and errors -------------------------------------------------------------------------------------------------------- */
enum { OD_IDLE = 0, OD_WAIT_NET = 1, OD_FIND = 2, OD_PEER = 3, OD_CONNECT = 4, OD_UP = 5, OD_ERR = 6 };
enum {
    OD_E_NONE = 0,
    OD_E_NOCFG = 1,        /* no identity / no pinned key / no server address in the config */
    OD_E_NONET = 2,        /* no address after net_ms (no adapter, no cable, no DHCP) */
    OD_E_SERVER = 3,       /* the server did not answer (or only malformed answers) */
    OD_E_NOOPP = 4,        /* nobody entered our code in time (server timeout or our find_ms) */
    OD_E_REFUSED = 5,      /* the server answered with a status: m.status */
    OD_E_PEER = 6,         /* the ticket named a peer we refuse to connect to */
    OD_E_NOCONN = 7,       /* the engine did not reach the peer within connect_ms */
    OD_E_LOST = 8,         /* the connection to the peer dropped before the match */
    OD_E_BADCODE = 9,      /* the code is not a connect code (empty, too long, characters) */
    OD_E_MODE = 10,        /* a mode this build does not serve (ranked, teams, party, unranked) */
    OD_E_SESSION = 11,     /* (kept for old tests: the engine now ends the old session itself, s12_gc.c) */
    OD_E_LEFT = 12,        /* the connection was up and the opponent is gone (peer silence, ENet timeout, disconnect): "Opponent left" */
    OD_E_DNS = 13,         /* the server NAME could not be resolved (DNS): o->dns_text holds the compact diagnostics (code, tx, rx, ignored, malformed, resolver) */
    OD_E_HUB = 14          /* hub overlay builds (HUB_OVL_RES): the file ms/hub.ovl could not be loaded from the memory card (menus/hub/hub_ovl.h) */
};
/* why the config is empty (od.cfg_why, set by the caller that tried the memory card; 0 = no card was asked, the image's own block decides) */
enum { OD_WHY_NONE = 0, OD_WHY_NOCARD = 1, OD_WHY_NOFILE = 2, OD_WHY_DAMAGED = 3, OD_WHY_NEWER = 4, OD_WHY_ERROR = 5, OD_WHY_BUSY = 6, OD_WHY_INCOMPLETE = 7 };

/* engine states the caller reports (s7_core.h numbering): connecting 4, running 6, lost 0x91 */
#define OD_CORE_RUN 6
#define OD_CORE_LOST 0x91

#define OD_INBOX 4
#define OD_REP_LEN 52                        /* ms_ranked.REPORT_FMT: ver game tie winner endm stage frames ff checksum stocks1 stocks2 H(32) */
#define OD_REP_TRIES 3u
#define OD_REP_RETRY_MS 3000u
#define OD_RT_MAX 5u                         /* console client resilience ( C2/C4): retries of one search after "no answer" or a busy status */
#define OD_RT_BASE_MS 2000u                  /* backoff base: 2 s, 4 s, 8 s, 16 s, 32 s (cap 60 s), each +-20%; a busy status raises the wait to its hint */
#define OD_RT_CAP_MS 60000u
#ifndef OD_CN_MAX                            /* (host tests override it with -DOD_CN_MAX=n) */
#define OD_CN_MAX 10u                        /* a failed peer connection (Unranked, Ranked, Direct) searches again silently up to this many times, then the error (like the stock client, bounded) */
#endif
#ifndef OD_CN_MAX_Q
#define OD_CN_MAX_Q 4u                       /* review M2: in the QUEUES (Unranked, Ranked) the server keeps a failed pair apart for cfg.conn_fail_s (20 s), so the silent phase is bounded at 4 retries (about 2 minutes with few testers); Direct has no separation and keeps OD_CN_MAX */
#endif
#define OD_CN_PAUSE_MS 1500u                 /* the pause before the next ticket (a good neighbour to the server and to the other player's own retry) */
#define OD_BK_AFTER 2u                       /* consecutive "no answer" failures on one server before the other one (primary <-> backup) is tried */

struct od_status {                           /* RAM proof block (symbol od_st, od_status_addr); word index = offset / 4 */
    volatile unsigned int magic;             /* 0  'ODST' */
    volatile unsigned int phase;             /* 1 */
    volatile unsigned int err;               /* 2 OD_E_* */
    volatile unsigned int mnc_state;         /* 3 */
    volatile unsigned int mnc_err;           /* 4 */
    volatile unsigned int mnc_status;        /* 5 server status of the last refusal */
    volatile unsigned int requests;          /* 6 B4 requests accepted */
    volatile unsigned int finds_done;        /* 7 tickets that paired */
    volatile unsigned int peer_ip;           /* 8 */
    volatile unsigned int peer_port;         /* 9 */
    volatile unsigned int side;              /* 10 */
    volatile unsigned int my_slot;           /* 11 */
    volatile unsigned int st_sent, st_resent, st_rx, st_ignored, st_bad;   /* 12..16 */
    volatile unsigned int rx_in, rx_drop;    /* 17 datagrams queued / dropped (inbox full) */
    volatile unsigned int cancels;           /* 18 */
    volatile unsigned int net_ip;            /* 19 */
    volatile unsigned int core;              /* 20 last engine state reported */
    volatile unsigned int t_find_ms;         /* 21 time from request to the ticket result */
    volatile unsigned int t_up_ms;           /* 22 time from request to engine RUN */
    volatile unsigned int b3_state;          /* 23 last state od_b3 answered (0 = engine) */
    volatile unsigned int b9, b4, b3, b3_eng;   /* 24..27 EXI commands seen by od_backend.c (b3_eng: answered by the engine) */
    volatile unsigned int ba, be;            /* 28 29 */
    volatile unsigned int last_mode;         /* 30 mode byte of the last B4 */
    volatile unsigned int latch;             /* 31 0 none, 1 ranked refused (backend text) */
    /* ---- result reports (Ranked), words 32.. ---- */
    volatile unsigned int rep_queued;        /* 32 game reports + status reports queued */
    volatile unsigned int rep_acked;         /* 33 answered by the server */
    volatile unsigned int rep_failed;        /* 34 given up (refused, or no answer after the retries) */
    volatile unsigned int rep_game_state;    /* 35 server's game state of the last game report (0 open, 1 agreed, 2 dispute, 3 desync) */
    volatile unsigned int rep_match_state;   /* 36 server's match state (0 open, 1 counted, 2 held, 3 void, 4 abandoned, 5 admin) */
    volatile unsigned int rep_winner;        /* 37 winner slot (1, 2) of the last game report, 0 none */
    volatile unsigned int rep_frames;        /* 38 frames field of the last game report */
    volatile unsigned int rep_ff;            /* 39 final frame of the evidence */
    volatile unsigned int rep_ck;            /* 40 Slippi checksum of that frame (the evidence) */
    volatile unsigned int rep_stocks;        /* 41 stocks1 << 8 | stocks2 */
    volatile unsigned int rep_skipped;       /* 42 results not reported (tie, desync, no ticket, wrong mode), see rep_why */
    volatile unsigned int rep_why;           /* 43 why the last one was skipped (OD_RS_*) */
    volatile unsigned int rep_status_code;   /* 44 last MATCH_STATUS code queued */
    volatile unsigned int cfg_why;           /* 45 od.cfg_why */
    volatile unsigned int rep_err;           /* 46 mnc error of the last failed report */
    volatile unsigned int rep_try;           /* 47 sends of the current report */
    /* ---- server name lookup, words 48.. ---- */
    volatile unsigned int dns_state;         /* 48 OD_DNS_* */
    volatile unsigned int dns_err;           /* 49 SDDNS_E_* of the last failed lookup */
    volatile unsigned int dns_lookups;       /* 50 lookups started */
    volatile unsigned int dns_sent, dns_rx, dns_ignored, dns_bad;   /* 51..54 of the last lookup */
    volatile unsigned int dns_server;        /* 55 resolver address used (host order) */
    volatile unsigned int srv_ip;            /* 56 the server address in force (from the card, or resolved); 0 = none yet */
    volatile unsigned int dns_ms;            /* 57 time the last lookup took */
    /* ---- retry / backup server ( C2, C4), words 58.. ---- */
    volatile unsigned int rt_n;              /* 58 retries used by the current search */
    volatile unsigned int rt_why;            /* 59 0 none, 1 waiting after a busy status, 2 waiting after no answer ("Server busy, retrying" / "Server unavailable, retrying") */
    volatile unsigned int on_bk;             /* 60 1 = the backup server is in force */
    volatile unsigned int rt_wait_ms;        /* 61 the last backoff wait chosen (jittered) */
    /* ---- Game Night hand-off: byte offset 252 (word 63; the word numbers above run one low from cancels on) ---- */
    volatile unsigned int gn;                /* 63 mnc gn_state (2 searching the call-up name, 3 the set is over) | sets handed off << 8 | room searches started << 16 */
};
enum { OD_DNS_NONE = 0, OD_DNS_BUSY = 1, OD_DNS_OK = 2, OD_DNS_FAILED = 3 };
enum { OD_RS_NONE = 0, OD_RS_MODE = 1, OD_RS_TIE = 2, OD_RS_DESYNC = 3, OD_RS_NOTICKET = 4, OD_RS_NOWINNER = 5, OD_RS_BUSY = 6 };

struct od {
    const struct od_cfg *cfg;
    mnc m;
    struct od_status *st;                    /* optional (NULL = none) */
    int (*send)(void *ctx, const unsigned char *p, unsigned int len);
    void *send_ctx;
    int phase, err, cfg_ok;
    int reenrol;                             /* 0 not tried in this search, 1 the silent re-enrolment is running, 2 spent (od_tick) */
    int ident_dirty;                         /* 1: od_cfg holds a fresh identity (re-enrolment) that the card record does not have yet (on_backend.c writes it) */
    unsigned int now;                        /* last clock seen */
    unsigned int t_req, t_phase;
    int mode;
    char target[19];
    unsigned char cn_n;                      /* peer-connection failures retried silently in this search (OD_CN_MAX); sits in the padding after target[19], the struct size does not change */
    int net_up;
    unsigned int own_ip, mask;
    int started;                             /* mnc_find started */
    int peer_taken;
    unsigned int peer_ip, peer_port;
    int side, my_slot;
    char opp_name[MNC_STR], opp_code[MNC_STR];
    unsigned char in_buf[OD_INBOX][MNC_MAX_PACKET];
    unsigned int in_len[OD_INBOX];
    unsigned int in_head, in_n;
    int core_state;
    int cfg_why;                             /* OD_WHY_*: why the config is empty (caller-set) */
    /* result report (Ranked): one pending item (a game report or a status), sent by od_tick, retried, then dropped */
    unsigned char mid[8], rep_mid[8];
    int mid_ok;                              /* the match id of the last ticket that paired */
    int rep_kind;                            /* 0 none, 1 game report, 2 match status */
    unsigned char rep_blob[OD_REP_LEN];
    unsigned int rep_len, rep_code, rep_tries, rep_next;
    int rep_started;                         /* the request is in flight on the mnc */
    /* server address: cfg->server_ip when non-zero ("the address wins"), else resolved from cfg->host once per session at menu time (od_tick, phase WAIT_NET) */
    unsigned int srv_ip;                     /* the address in force; 0 = not known yet */
    int use_host;                            /* 1: srv_ip comes from cfg->host (resolved, cached in srv_ip) */
    int dns_state;                           /* OD_DNS_* */
    struct sddns *dns;                       /* the caller's resolver (shared with the bridge in the combined image); NULL = names unsupported */
    unsigned int dhcp_dns;                   /* the DHCP-provided resolver (host order); the only resolver used */
    unsigned int dns_t0;
    char dns_text[96];                       /* the error box text of OD_E_DNS */
    /* retry with backoff and the optional backup server ( C2, C4) */
    unsigned int rt_at;                      /* a retry does not start before this time (rt_why != 0) */
    unsigned int bk_ip;                      /* the backup server address (host order, same port; card record +0xA8), 0 = none; set by the caller (od_set_backup) */
    unsigned char rt_n, rt_why, fails, on_bk;
    unsigned int nc_n[2], nc_tot[2], nc_max[2];   /* NET COST probe (Settings > Diagnostics, lab builds): [0] the hub overlay's whole menu proc, [1] its network step; count, total and maximum in time base ticks
                                                    since boot (hub_soc.c hs_proc); ms_net_cost reads them */
    unsigned char test_toast;                /* Settings > Diagnostics > Test Toast: 1 friend request, 2 invite, 3 friends added; the hub overlay raises it and clears it (local, no server) */
    unsigned int hello_fail_ms;              /* hub client: the clock of the last failed HELLO, 0 = none or it worked since (the offline guard, ms_offline_server); appended last */
    unsigned int unread_ms;                  /* the clock of the last menu tick reply that carried the unread message count (0 = none yet); a count older than 45 s is not shown (offline / guest / gone) */
    unsigned short room_note;                /* a Game Night room code (0..9999) + 1 from a toast the player accepted: the menu glue takes it (ms_net_cost 101) and opens the browser in that room; 0 = none */
    unsigned char unread;                    /* unread messages in total (PRESENCE_SET ext bit 0), 0..255; the Main Menu Friends row (ms_net_cost 100) */
    unsigned int srv_ok_ms;                  /* the clock of the last request the Multishine server answered (any mnc request that went BUSY -> DONE: hub tick, HELLO, WHOAMI, search steps); 0 = none yet (Settings > Network status, ms_net_cost 103) */
    unsigned int net_ask_ms;                 /* the clock of the last time a Settings page asked for a real server check (ms_net_cost 104); the hub overlay sends one WHOAMI per 10 s while this is under 3 s old; 0 = nobody asks */
    unsigned char gn_lobby;                  /* toasts in the Game Night room: bit 0 the room lobby is on screen (sign overlay, sg_entry.c lobby_pad), bit 2 the Friends screen is on screen (the hub tick goes on), bit 1 a toast that
                                                waits for an answer is up (hub overlay, hub_soc.c menu_frame); fills the struct's last padding byte (the size does not change) */
    unsigned char news_req, news_inv;        /* the Main Menu's Friends panel: incoming friend requests and pending invites from the last menu tick (fresh as unread_ms; ms_net_cost 105); fill the struct's last padding bytes (the size does not change) */
    unsigned char news_online;               /* friends online as the server shows them to this player (PRESENCE_SET ext bit 2), stored as count + 1 (0 = the last tick answer carried no number: an older server); ms_net_cost 106; takes the last padding byte (the size stays 0x1d78, checked with the PPC compiler) */
    /* RESIDENT state of the why-it-failed record and the NAT probe.  The hub overlay (HUB_OVL) is freed at every scene change and its own globals start at 0 at every load;
       od_why[] is resident, not an overlay global: as an overlay global the resident engine's weak reference would be NULL and end_session never filled it: no record ever reached the server. */
    unsigned int why[9];                     /* the glue's end_session: 8 words + [8] = 1 when filled (mnc_note_why) */
    unsigned char why_hs;                    /* 0 no HELLO tried for the record yet, 1 in flight, 2 done */
    unsigned char why_mid[8];                /* BLACKOUT: the match id of a match that dropped in its first minute (why[8] = 2: the next ticket of ANY search carries it) */
    unsigned char nat_probe;                 /* 1 = a ticket just left: the glue sends 'MSNP' + device id to wire port + 1 */
    unsigned short lan_port;                 /* the game socket's real local port (the LAN hint names it); 0 = OD_LAN_PORT */
    unsigned char link_note;                 /* LINK QUALITY heads-up: 1 = this console measured Wi-Fi and its newest game stalled a lot (on_backend.c gr_record); ms_net_cost 116, the Settings > Network status help line */
    unsigned char tm_note;                   /* TIERED MATCHMAKING: which network the opponent was found on / why Slippi was skipped (tmm_core.h TMM_N_*), written by br_glue.c, read by hub_soc.c css_frame; 0 = nothing */
};

/* SEARCH-LEVEL mode accessors: `mode` is the mode of the last search REQUEST, written by od_request only.  It says nothing about a running match (the bridge route never
   writes it; a finished search leaves it behind): match-time code asks the per-match record (ms_match_rules.h), search-time code asks these two. */
static __inline__ int od_search_mode(const struct od *o) { return (o->phase >= OD_FIND && o->phase <= OD_UP) ? o->mode : -1; }   /* the mode while OUR search / session is live, else -1 (= none) */
static __inline__ int od_req_mode(const struct od *o) { return o->mode; }                                                          /* the last request's mode as the menus and the error texts use it */

/* ---- setup ------------------------------------------------------------------------------------------------------------------- */
/* returns 0, or OD_E_NOCFG (the controller then refuses every request with that error: the menu shows it) */
int od_init(struct od *o, const struct od_cfg *cfg, struct od_status *st,
            int (*send)(void *ctx, const unsigned char *p, unsigned int len), void *send_ctx, unsigned int seed, unsigned int now_ms);

/* (re)apply the config block after its contents changed (the memory-card loader fills it at the first B9): pinned keys, identity, server address.
   Returns 0 or OD_E_NOCFG.  A search or report in flight is not touched. */
int od_apply_config(struct od *o);

/* ---- game thread -------------------------------------------------------------------------------------------------------------- */
/* B4: mode and the code as typed (ASCII, upper case, e.g. "AB#1234"); starts the search.  Returns 0, or the OD_E_* the menu shows at once. */
int od_request(struct od *o, unsigned int now_ms, int mode, const char *code);
#ifdef MNC_RKSET
/* Grandmaster sets: a game of the held set starts from the character screen's lock-in (hand-off ARMED + MNC_RK_FLAG): the held pairing's peer, the game's stage, no new ticket. 0 or OD_E_* */
int od_rk_go(struct od *o, unsigned int now_ms);
#endif
/* BA: cancel everything (best effort TICKET_CANCEL), back to IDLE */
void od_cancel(struct od *o, unsigned int now_ms);
/* B3: 1 = od answered (msrb[0] and the error text are filled; the caller must not use the engine's answer), 0 = the engine answers (phase UP) */
int od_b3(struct od *o, unsigned char *msrb);
/* the message the menu shows for an error, never NULL */
const char *od_err_text(const struct od *o);

/* the result of one game, as the Slippi report command (B BD) gives it, mapped to the server's terms (slots are 1 and 2 = side + 1) */
struct od_game {
    int game;                                /* 0-based index of the game in the set */
    int tie;                                 /* tiebreak index (0 = a normal game) */
    int winner;                              /* slot 1 or 2 */
    int endm;                                /* Melee's game end method */
    int stage;
    unsigned int frames;                     /* length of the game in frames */
    unsigned int final_frame, cks;           /* the evidence: the last finalized frame and the Slippi checksum of it (the same on both consoles) */
    int stocks1, stocks2;                    /* stocks left of slot 1 and 2 */
    int chars_ok, char1, char2;              /* Game Night on a server with room types: the characters as played (external ids 0..25), appended to the report as two bytes, slot 1 then slot 2; chars_ok 0 = no extension (an older server refuses a longer report) */
};
#define OD_GN_ROOM_FLAGS 19u                 /* = gns_ovl.h GNS_ROOM_FLAGS / GNS_RF_CHARS (checked at compile time in gns_res.c): the Game Night hand-off's flag byte, bit 2 = the server serves room types, so the report carries the two character bytes */
#define OD_GN_RF_CHARS 0x04u
#define OD_REP_CHARS 0x10000u                /* rep_code of a game report (the field is unused for one, so the struct keeps its size): bit 16 = the two character bytes follow the blob, bits 15..8 slot 1, 7..0 slot 2 */
/* Ranked only (the caller checks the mode).  game thread.  Builds the report (SHA-256 evidence hash included) and queues it for od_tick;
   0 = queued, else OD_RS_*.  A report in flight is replaced only when it was already answered. */
int od_report_game(struct od *o, unsigned int now_ms, const struct od_game *g);
unsigned int od_rep_payload(const struct od *o, unsigned char *out);     /* what od_tick sends after the match id: the blob and, when queued with the characters, the two bytes (OD_REP_LEN + 2 at most); returns the length */
/* a MATCH_STATUS code of ms_ranked (1 connecting, 2 abandoned, 3 disconnected, 4 poor performance, 5 set complete) */
int od_report_status(struct od *o, unsigned int now_ms, int code);
/* 1 while a report is queued or in flight */
int od_report_busy(const struct od *o);

/* ---- network slice ------------------------------------------------------------------------------------------------------------ */
void od_net_up(struct od *o, unsigned int own_ip, unsigned int mask);     /* DHCP bound (call again when the address changes) */
void od_net_down(struct od *o);
/* the resolver for a configured server NAME: d = the caller's sddns (bound to a UDP socket by the caller), dhcp_dns = the DHCP-provided resolver (call again when it changes).
   Without it a hub config that holds only a name fails with OD_E_DNS ("no DNS in this build").  The lookup runs only in phase WAIT_NET (a search was asked for), once per session. */
void od_set_dns(struct od *o, struct sddns *d, unsigned int dhcp_dns);
/* the optional backup server (host order IPv4, 0 = none; the same port and pinned key): tried after OD_BK_AFTER failed attempts on the primary, and back again after as many on the backup */
void od_set_backup(struct od *o, unsigned int ip);
/* DNS fallback for the hub's own requests outside a search: a HELLO / WHOAMI without an answer, one that was answered, and the lookup step (1 = an address is in force) */
void od_idle_noanswer(struct od *o);
void od_idle_ok(struct od *o);
int od_idle_resolve(struct od *o, unsigned int now_ms);
/* the server address in force (0 = a name not resolved yet); the caller's send and receive routing use this, never cfg->server_ip */
unsigned int od_server_ip(const struct od *o);
/* 1 if s is an acceptable server host name for the card/image: 1..47 characters of [A-Za-z0-9.-], labels 1..63, no leading/trailing dot or hyphen at a label edge */
int od_host_ok(const char *s);
void od_rx(struct od *o, const unsigned char *d, unsigned int len);       /* one datagram FROM THE SERVER */
void od_tick(struct od *o, unsigned int now_ms);
/* 1 once per match when the ticket named the peer: the engine must connect to ip:port as player `side` (0 or 1) */
int od_take_peer(struct od *o, unsigned int *ip, unsigned int *port, int *side);
void od_core_report(struct od *o, unsigned int now_ms, int core_state);   /* the engine's state, every slice while phase >= OD_CONNECT; OD_CORE_LOST while UP = OD_E_LEFT */
/* the session was ended on our side (BA cleanup or a new search): nothing to report; phase UP/ERR(LEFT) only changes through od_cancel / od_request */

#endif
