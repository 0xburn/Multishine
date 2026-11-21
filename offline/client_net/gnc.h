/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GNC_H
#define GNC_H
/*
 * gnc: the console-side client of the Game Night room ops: GN_LIST (v2 request: page + version
 * byte 2), GN_CREATE, GN_JOIN, GN_LEAVE, GN_STATE, GN_ACTION.  Same style as mnu.c: C89, no libc, no heap, never blocks, one request at a time, riding on an mnc's
 * wire v2 session and io (run mnc_hello first: the ops need a DEVICE session, an anonymous one is refused with AUTH_FAILED).  A lost reply is asked again with the
 * SAME sealed datagram (same wire counter): the server answers a repeated counter from its reply cache, so a create or join is never executed twice.
 * Every reply is hostile input: the parsers below are ports of ms_rooms.parse_list / parse_view (every length, count, flag combination and character checked; any
 * violation = the reply is ignored as malformed, never a crash).  Host-tested against the reference parser and the real server (tests/gnc_driver.c, tests/test_gnc.py).
 */
#include "mnc.h"

#define GNC_OP_LIST 0x35
#define GNC_OP_CREATE 0x36
#define GNC_OP_JOIN 0x37
#define GNC_OP_LEAVE 0x38
#define GNC_OP_STATE 0x39
#define GNC_OP_ACTION 0x3A
#define GNC_LIST_V2 2u
#define GNC_LIST_V3 3u              /* + region and stage mode per entry */
#define GNC_LIST_V4 4u              /* + the stage mode may be 2 (All stages) and an optional filter byte in the request (deploy the server first) */
#define GNC_VIEW_V5 5u              /* the v4 layout, the stage mode may be 2 (All stages) */
#define GNC_SM_LEGAL 0u             /* stage rule: random among the six legal stages (Stadium always in) */
#define GNC_SM_ALL 2u               /* ... among all 29 Vs stages (only while the server's HELLO caps carry bit 12) */
#define GNC_SM_ANY 0xFFu            /* create: the server's default (Legal); list filter: every room */
#define GNC_LIST_V5 5u              /* the v4 layout + a list-flags byte after the 3-byte header (GNC_LF_ALL_STAGES); a server without v5 answers BAD_REQUEST and gnc retries v4 (list_all stays 0) */
#define GNC_LF_ALL_STAGES 1u        /* list v5 flags bit 0: our switched All stages on (HELLO bit 12 is CAP_LINK_SEALED, not this) */
#define GNC_LIST_V6 6u              /*: the v5 layout + room type u8 per entry; the filter byte is the room type (0..5, 0xFF any); a server without it answers BAD_REQUEST and gnc retries v5 */
#define GNC_LF_ITEMS 2u             /* list v6 flags bit 1: the items switch is on (Wild And Crazy may be created) */
#define GNC_LF_TYPES 4u             /* list v6 flags bit 2: this server serves room types (always set in a v6 reply; the console's one reliable "room types" signal, the HELLO caps2 byte says the same) */
#define GNC_VIEW_V6 6u              /* view v6: the v5 layout + the room type trailer (type, set score, step, stage list, forced / excluded characters, struck, rules hash); asked for in GN_JOIN / GN_CREATE */
#define GNC_RT_NONE 0xFFu           /* create: no room type byte (the server default, Chill); also "no forced character" in the view trailer */
#define GNC_STEP_STRIKE 2u          /* view v6 trailer step: 0 none, 1 colour (never sent), 2 strike, 3 ban, 4 pick stage, 5 pick character */
#define GNC_STEP_BAN 3u
#define GNC_STEP_PICK_STAGE 4u
#define GNC_STEP_PICK_CHAR 5u
#define GNC_TURN_WAIT 0xFDu         /* view v6 turn: Sweaty between two games, nothing runs until both consoles are back in the room */
#define GNC_STAGE_LIST_MAX 8u
#define GNC_CHAR_COUNT 27u          /* external character ids 0..25 and 26 Random: the excluded mask covers bits 0..26 */
#define GNC_VIEW_V3 3u              /* view v3: + region, stage mode, capacity, pick turn, picks, stage, password (asked for in GN_JOIN / GN_CREATE) */
#define GNC_VIEW_V4 4u              /* view v4: the v3 layout + one trailing byte of watch facts; asked for in GN_JOIN / GN_CREATE, so a server that lacks it answers BAD_REQUEST (deploy the server first) */
#define GNC_VF_NOREPLAY 1u          /* v4 watch facts: the room has no last game a watcher could replay */
#define GNC_VF_NOLIVE 2u            /* ... no game that is being uploaded live */
#define GNC_TURN_GO 0xFEu           /* view v3 turn: both picked, the called pair starts its search */
#define GNC_REGION_NONE 0xFFu
#define GNC_REGION_N 7u             /* room regions 0..6 = NAE NAW EU LA AS OC NAC (ms_region.CODES, append only; NAC) */
#define GNC_VER_RG7 0x80u           /* OR'd into the list v6 / view v6 version byte: this console knows region 6 (NAC); the reply layout is unchanged.  Without it the
                                       server sends a NAC room as 0xFF.  A server that predates it answers BAD_REQUEST: gnc asks once more without the bit (nrg7) */
#define GNC_TN_VER 5u               /* TN_LIST / TN_INFO version 5: the version 4 layout, the region byte may be 7 (NAC); deploy the server first */
#define GNC_TN_REGION_MAX 7u        /* tournament region: 0 Any, 1..7 NAE NAW EU LA AS OC NAC */
#define GNC_PAGE_MAX 10u
#define GNC_UPNEXT 8u
#define GNC_VIEW_MAX 400u
#define GNC_TRIES 5u

/* Grandmaster sets: RK_SET_STATE 0x1C (match id) and RK_SET_ACTION 0x1D (match id, action, value, optional costume), both with a
   device session; the reply is the set view.  The same stage prep screen (gnb_prep.h) shows it. */
#define GNC_OP_RK_STATE 0x1C
#define GNC_OP_RK_ACTION 0x1D
#define GNC_RK_STRIKE 5u            /* RK_SET_ACTION numbers: the same as GN_ACTION's, plus 7 = ban (3 colour, 4 character) */
#define GNC_RK_PICK 6u
#define GNC_RK_BAN 7u
#define GNC_RK_CHAR 4u
#define GNC_OP_MATCH_STATUS 0x15    /* MATCH_STATUS (the ranked path's status report): match id (8) + code u8, reply 1 byte (the match state); piece G2 sends 2 = abandoned from the set screen (Hold B) */
struct gnc_setview {
    mnc_u8 have, ver, phase, my_slot, best_of, game, score[2], p0, prev_winner, step, owner, remaining, secs, avail, n, id[8], state[8], stage, ch[4], result, reason, tier[2];
};

#define GNC_E_NONE 0
#define GNC_E_BUSY 1
#define GNC_E_ARG 2
#define GNC_E_NOSESSION 3           /* no READY session, or the server forgot it: hello again */
#define GNC_E_TIMEOUT 4
#define GNC_E_SERVER 5              /* status != 0 in gnc.status (ms_wire STATUS numbers) */
#define GNC_E_BAD_REPLY 6           /* the reply did not parse */

/* server statuses the browser tells apart (ms_wire.STATUS) */
#define GNC_ST_BAD_REQUEST 1
#define GNC_ST_UNKNOWN_OP 2
#define GNC_ST_RATE_LIMITED 3
#define GNC_ST_AUTH_FAILED 4
#define GNC_ST_CODESET_MISMATCH 8
#define GNC_ST_RULES_MISMATCH 9
#define GNC_ST_NOT_FOUND 16
#define GNC_ST_NOT_SUPPORTED 18
#define GNC_ST_UPDATE_REQUIRED 32
#define GNC_ST_LIMIT_REACHED 33
#define GNC_ST_ACCOUNT_NEEDED 46    /* a guest session: shown as "Needs An Account" */

#define GNC_F_SPECT 2u
#define GNC_F_PASS 4u
#define GNC_F_FULL 8u
#define GNC_F_RUN 16u
#define GNC_F_MOCK 32u

struct gnc_entry { char code[9], name[13], host[13]; mnc_u8 mode, members, capacity, flags, stadium, region, smode, rtype; mnc_u16 minutes; };   /* rtype: list v6 (else 0 Chill, or 2 Wild when smode is All) */   /* region / smode / minutes: v3 (else 0xFF / 0 / 0); minutes = open for, server time, saturating at 65535 */
struct gnc_view {
    mnc_u8 rev, phase, flags, members, qlen, pos, side, streak, crowns, secs, nnext, rflags, stadium, mode;
    mnc_u16 set_id;
    mnc_u8 side_on[2], side_char[2], side_color[2], side_crowns[2];
    char code[9], rname[13], side_name[2][13], next[GNC_UPNEXT][13];
    /* view v3 (0 / 0xFF when the server sent v2) */
    mnc_u8 v3, region, smode, capacity, turn, picked, stage;
    char pw[5];
    mnc_u16 minutes;                /* v3 view: minutes the room has been open (server time, saturating) */
    mnc_u8 wfacts;                  /* v4 view: GNC_VF_* (0 for a v2 / v3 view) */
    /* view v6 trailer (all zero / 0xFF for an older view) */
    mnc_u8 v6, rtype, best_of, game, score_w, score_c, step, step_owner, step_secs, stage_avail, stage_n, stage_list[GNC_STAGE_LIST_MAX], forced, struck;
    mnc_u32 excl, rules_hash;       /* excl: bit c = external character id c may not be played */
};

/* GN_STATE with the roster flag (the Y standings panel; the request's 2nd byte is 2 = with each player's streak wins, reply tag 0x03, rows end with streak wins u8 | flags u8 (bit 0 in the lead)): reply 0x03 | streak u8 | n u8 (<= 8) | n x (name p12, crowns u8, place u8: 0xFE winner side, 0xFD challenger side, 1.. queue position, 0 none) */
#define GNC_ROSTER_MAX 8u
#define GNC_PL_WINNER 0xFEu
#define GNC_PL_CHALLENGER 0xFDu
struct gnc_roster { mnc_u8 streak, n, crowns[GNC_ROSTER_MAX], place[GNC_ROSTER_MAX], pstreak[GNC_ROSTER_MAX], plead[GNC_ROSTER_MAX]; char name[GNC_ROSTER_MAX][13]; };   /* pstreak: the player's streak wins of the night, plead: 1 = in the lead (the server ranks) */

/* Grandmaster standings: STANDINGS_LIST 0x33, any session (anonymous too), HELLO capability bit 9 */
#define GNC_OP_STAND 0x33
#define GNC_CAP_STAND 512u
#define GNC_STAND_VER 2u            /* the only request/reply version: the reply ends with season_days_left u8 (the server's UTC calendar month; the console has no clock) */
#define GNC_STAND_MAX 10u           /* page size limit of the server */
#define GNC_STAND_F_MOCK 1u         /* reply flags bit 0: example data (--standings-mock) */
#define GNC_TIER_PLACEMENT 255u
#define GNC_TIER_MAX 8u             /* ms_rating.TIERS: 0 Copper .. 8 Onyx */
struct gnc_stand_row { mnc_u16 rank, rating, games; mnc_u8 tier, you; char name[13]; };      /* rank 0 = in placement; you = flags bit 0: this row is this console's own player (the server sets it; no connect code is ever sent) */
struct gnc_stand {
    mnc_u8 flags, page, n, have_you;
    mnc_u8 days_left;               /* version 2 replies: days left of the season counting today, 1..31 */
    mnc_u16 season, total;
    mnc_u32 updated;                /* unix seconds of the server's table build */
    struct gnc_stand_row e[GNC_STAND_MAX], you;
};


/* Tournament (wire ops 0x55-0x5E): the same client, one request at a time; the reply goes into *g->tn
   (set by the caller after gnc_init; 0 = the request is refused).  A match call (TN_MYMATCH kind 2) carries the match room's code: the caller joins it with gnc_join. */
#define GNC_OP_TN_LIST 0x55
#define GNC_OP_TN_INFO 0x56
#define GNC_OP_TN_CREATE 0x57
#define GNC_OP_TN_JOIN 0x58
#define GNC_OP_TN_LEAVE 0x59
#define GNC_OP_TN_CHECKIN 0x5A
#define GNC_OP_TN_BRACKET 0x5B
#define GNC_OP_TN_MYMATCH 0x5C
#define GNC_OP_TN_START 0x5D
#define GNC_OP_TN_CANCEL 0x5E
#define GNC_ST_TN_FULL 47
#define GNC_ST_TN_CLOSED 48
#define GNC_ST_TN_NOT_ALLOWED 49
#define GNC_TN_LIST_MAX 8u
#define GNC_TN_MPAGE 38u            /* matches per bracket page (wire v2: 12-byte items) */
#define GNC_TN_MAXM 127u            /* matches of a 64 double elimination bracket */
#define GNC_TN_NPAGE 22u            /* names per bracket page */
struct gnc_tn_ent { mnc_u16 tid; char name[13]; mnc_u8 state, entrants, cap, format, my, eflags; mnc_u32 secs, pubid, signup_left; mnc_u8 scheduled, region; };   /* region (version 4 / 5): 0 Any, 1..7 NAE NAW EU LA AS OC NAC */   /* version 3 (Tournament v2): pubid = the 8 digit id, signup_left = seconds to the start (sign-up timer or scheduled start), scheduled 1 = a scheduled start (Upcoming) */   /* secs: version 2, seconds until an official cup starts (0 otherwise); eflags bit 6 official, bit 7 late registration open */       /* format: 0 double, 1 single elimination */
struct gnc_tn_info { char name[13], host[13], champion[13]; mnc_u8 state, entrants, checked, cap, format, seeding, eflags, round, rounds, rev, official, reason; mnc_u16 checkin_left; mnc_u32 rules_hash, secs, next_cup;
                       mnc_u16 tid; mnc_u32 pubid, signup_left, start_at; mnc_u8 signup_min, host_plays, scheduled, region; };   /* version 3 tail: tid, 8 digit id, seconds to the start, sign-up window minutes (0 none), host plays 1/0, scheduled 1/0, start_at epoch (0 now); seeding 0 random, 1 rating, 2 join order */
struct gnc_tn_m { mnc_u8 mid, side, round, state, a, b, wa, wb, win, bo, next_win, next_lose; };
struct gnc_tn_my { mnc_u8 kind, rev, mid, side, round, rounds, best_of, place; char opp[13], code[9], room[9]; mnc_u16 secs; };
struct gnc_tn {
    mnc_u8 flags, total, n;                         /* TN_LIST */
    mnc_u32 next_cup;                               /* TN_LIST version 2: seconds until the next official cup starts, 0xFFFFFFFF off */
    struct gnc_tn_ent e[GNC_TN_LIST_MAX];
    struct gnc_tn_info info;                        /* TN_INFO */
    mnc_u8 b_state, b_rev, b_size, b_entrants, b_rounds, b_format, b_nm, b_me, b_page, b_pages, b_n, b_is_match;   /* TN_BRACKET (version 2): the header and which kind of page it was; b_me: your entrant index, 0xFF none */
    struct gnc_tn_m m[GNC_TN_MPAGE];
    char names[GNC_TN_NPAGE][13];
    mnc_u8 nstate[GNC_TN_NPAGE], nseed[GNC_TN_NPAGE];
    struct gnc_tn_my my;                            /* TN_MYMATCH */
    mnc_u16 tid;                                    /* TN_CREATE: the new tournament; TN_JOIN by id: the tournament joined */
    mnc_u32 pubid;                                  /* TN_CREATE, TN_JOIN by id: its 8 digit id */
    mnc_u8 result;                                  /* TN_JOIN, TN_LEAVE: the result byte; TN_START: the state */
};

enum { GNC_IDLE = 0, GNC_BUSY = 1, GNC_DONE = 2, GNC_FAILED = 3 };

typedef struct gnc {
    mnc *c;
    int state, err, status;
    mnc_u8 op;
    mnc_u32 timeout_ms, sent_ms, ctr;
    mnc_u8 tries;
    mnc_u8 pkt[128];                /* the sealed request, resent as is */
    mnc_u32 pkt_len;
    /* results */
    mnc_u8 total, page, n;          /* GN_LIST */
    mnc_u8 list_all, lv4;           /* list_all: the last v5 list said All stages is on (0 until one did); lv4: the server refused v5, ask v4 */
    mnc_u8 list_items, list_rt, lv5;   /* the last v6 list: the items switch is on / the server serves room types; lv5: the server refused v6, ask v5 (and v4 after lv4) */
    mnc_u8 vver;                    /* the view version asked for in GN_JOIN / GN_CREATE: 0 = GNC_VIEW_V6 (the default), 5 = an older server (set by the caller, or by the one retry below) */
    mnc_u8 nrg7;                    /* the server refused GNC_VER_RG7 (it predates NAC): list v6 / view v6 are asked without the bit from now on */
    mnc_u8 jretry;                  /* a GN_JOIN / GN_CREATE that was refused with BAD_REQUEST on view v6 was asked again once with v5 */
    char jcode[9], jpin[5], jname[13];   /* the arguments of the last GN_JOIN / GN_CREATE, for that retry */
    mnc_u8 jch, jflags, jpolicy, jcap, jsm, jrt, jkind;
    mnc_u8 lreq_page, lreq_filter, lretry;   /* the list request, for the v5 -> v4 retry */
    struct gnc_entry e[GNC_PAGE_MAX];
    mnc_u8 have_roster;             /* GN_STATE with the roster flag: ros was parsed */
    struct gnc_roster ros;
    mnc_u8 have_view, unchanged;    /* GN_JOIN / CREATE / STATE / ACTION: a view was parsed; GN_STATE: 1 = "unchanged" (rev) */
    struct gnc_view v;
    struct gnc_setview sv;          /* RK_SET_STATE / RK_SET_ACTION: the parsed set view (have_set = 1) */
    mnc_u8 have_set;
    mnc_u8 rk_match;                /* MATCH_STATUS reply: the server's match state code */
    char created[9];                /* GN_CREATE: the new room's code */
    struct gnc_tn *tn;              /* the Tournament ops: where the reply goes */
    struct gnc_stand *st;           /* STANDINGS_LIST: where the reply goes (set by the caller after gnc_init; 0 = the request is refused) */
    mnc_u32 st_sent, st_resent, st_rx, st_bad;
} gnc;

void gnc_init(gnc *g, mnc *c);
int gnc_list(gnc *g, mnc_u32 now, unsigned page);
int gnc_list_f(gnc *g, mnc_u32 now, unsigned page, unsigned filter);   /* filter GNC_SM_LEGAL / GNC_SM_ALL / GNC_SM_ANY; with list v6 the filter is the room type 0..5 (the caller maps), 0xFF any */
int gnc_join(gnc *g, mnc_u32 now, const char *code, const char *pin, unsigned ch);
/* name "" = server default; flags 1 listed, 2 spectators; policy 0 normal, 1 frozen, 2 removed, 0xFF the server's default; pin "" or 4 digits; capacity 0 = default */
int gnc_create(gnc *g, mnc_u32 now, const char *name, unsigned flags, unsigned policy, const char *pin, unsigned capacity, unsigned ch);
int gnc_create_sm(gnc *g, mnc_u32 now, const char *name, unsigned flags, unsigned policy, const char *pin, unsigned capacity, unsigned ch, unsigned smode);   /* smode GNC_SM_LEGAL / GNC_SM_ALL / GNC_SM_ANY */
/* rtype 0..5 (ms_roomtypes ids) or GNC_RT_NONE: a room type is asked for with view v6 and a stage mode of 0xFF (the server derives the stage rule from the type); GNC_RT_NONE is gnc_create_sm */
int gnc_create_rt(gnc *g, mnc_u32 now, const char *name, unsigned flags, unsigned policy, const char *pin, unsigned capacity, unsigned ch, unsigned smode, unsigned rtype);
int gnc_state(gnc *g, mnc_u32 now, int have_rev);          /* have_rev < 0: always the full view */
int gnc_action(gnc *g, mnc_u32 now, unsigned action, unsigned value);
int gnc_action_cos(gnc *g, mnc_u32 now, unsigned action, unsigned value, unsigned costume);   /* ready with the costume (0..15) as the additive third byte of GN_ACTION */
int gnc_roster(gnc *g, mnc_u32 now);                       /* GN_STATE with the roster flag: the room's members by crowns (ros) */
int gnc_parse_roster(const mnc_u8 *b, mnc_u32 n, struct gnc_roster *out);   /* the body after the 0x03 tag (per row after place: streak wins, lead flag): 0 ok, < 0 refused */
int gnc_leave(gnc *g, mnc_u32 now);
int gnc_standings(gnc *g, mnc_u32 now, unsigned page, unsigned size);  /* the reply carries days_left */   /* size 1..10; the reply is parsed into *g->st */
int gnc_tn_list(gnc *g, mnc_u32 now, unsigned kind, unsigned offset);            /* kind 0 public, 1 mine */
int gnc_tn_info(gnc *g, mnc_u32 now, unsigned tid);
/* Tournament v2: pin = the 8 digit passcode of a private cup or "" (public); signup_min 15/30/60/120 (a "now" cup, start_in 0); seeding 0 random, 1 rating, 2 join order; host_plays 1/0;
   start_in = seconds from now (300 .. 2592000) for a scheduled start, 0 = now.  Reply: tid + 8 digit id (t->tid, t->pubid). */
int gnc_tn_create(gnc *g, mnc_u32 now, const char *name, unsigned cap, unsigned format, unsigned checkin, const char *pin, unsigned signup_min, unsigned seeding, unsigned host_plays, mnc_u32 start_in, unsigned region);
int gnc_tn_join(gnc *g, mnc_u32 now, unsigned tid, const char *pin);
int gnc_tn_join_id(gnc *g, mnc_u32 now, mnc_u32 pubid, const char *pin);                 /* join by the 8 digit id (+ passcode); reply: t->result, t->tid, t->pubid */
int gnc_tn_info_id(gnc *g, mnc_u32 now, mnc_u32 pubid);                                    /* TN_INFO by id (also a private cup): t->info.tid / pubid say which */
int gnc_tn_leave(gnc *g, mnc_u32 now, unsigned tid);
int gnc_tn_checkin(gnc *g, mnc_u32 now, unsigned tid);
int gnc_tn_bracket(gnc *g, mnc_u32 now, unsigned tid, unsigned page);
int gnc_tn_mymatch(gnc *g, mnc_u32 now, unsigned tid);
int gnc_tn_start(gnc *g, mnc_u32 now, unsigned tid);
int gnc_tn_cancel(gnc *g, mnc_u32 now, unsigned tid);
int gnc_parse_tn(unsigned op, const mnc_u8 *b, mnc_u32 n, struct gnc_tn *out);    /* 0 ok, < 0 refused */
int gnc_poll(gnc *g, mnc_u32 now);                          /* -> state */
void gnc_abort(gnc *g);

/* the reference parsers (ms_rooms.parse_list with v2 = 1, parse_view): 0 ok, < 0 refused */
int gnc_parse_list(const mnc_u8 *b, mnc_u32 n, int v2, mnc_u8 *total, mnc_u8 *page, mnc_u8 *count, struct gnc_entry *out);   /* v2: 0 = v1, 1 or 2 = v2, 3 = v3, 4, 5, 6 = list v6 */
int gnc_parse_view(const mnc_u8 *b, mnc_u32 n, struct gnc_view *v);
int gnc_rk_state(gnc *g, mnc_u32 now, const mnc_u8 mid[8]);                                                  /* RK_SET_STATE: the set view of a Grandmaster match (sv) */
int gnc_rk_action(gnc *g, mnc_u32 now, const mnc_u8 mid[8], unsigned action, unsigned value, unsigned costume);   /* RK_SET_ACTION (costume 0xFF = none): the new set view */
int gnc_rk_status(gnc *g, mnc_u32 now, const mnc_u8 mid[8], unsigned code);                                       /* MATCH_STATUS of a ranked match (2 abandoned): the reply is the match state, kept in rk_match */
int gnc_parse_setview(const mnc_u8 *b, mnc_u32 n, struct gnc_setview *v);
/* STANDINGS_LIST reply (ms_standings.wire): version 1, known flags only, n <= 10 and <= total, names / codes printable, tier 0..8 or 255, 'you' 0 or 1 + entry, nothing after */
int gnc_parse_standings(const mnc_u8 *b, mnc_u32 n, struct gnc_stand *out);
#endif
