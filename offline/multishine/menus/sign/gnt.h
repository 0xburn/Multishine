/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Multishine > Tournament: the MODEL.  Pure C89, no libc, no heap, no Melee address, no network.
 * An online single elimination bracket, each player on their own console; a bracket match is a server-made two-seat Game Night room (the glue joins it through
 * the Game Night browser's room hand-off, so the strike screen, Bo3 and the auto return are the room flow).  This file owns the screens, the cursor, the cached
 * server data and every text; two front ends draw it (the same split as frd.h / stb.h):
 *   - gnt_ev.c (console only, -DGNB_MELEE): the retail Event Match screen on Tournament's own look (list rows, description box, key line),
 *   - gnt_text (here): the same texts in the sign overlay's character-cell view (host golden tests; console fallback when the Event screen cannot be built).
 * Screens: LIST (tournaments; Create and Sign up by ID are footer keys), PAGE (one tournament: join, leave, bracket), CREATE (the host's form), BRACKET (a mirror of the server bracket, read
 * only: round by round, your match marked, "your match is ready" with A to play).  The bracket is never advanced here: it is what the server sent last.
 * The network is the glue's (sg_entry.c): the model raises gnt.req and the glue answers through gnt_net_*.  Text: menu font safe set only (A-Z a-z 0-9 space. , ' - :).
 */
#ifndef GNT_H
#define GNT_H

struct sg_view;

#define GNT_VIS 9u                 /* row slots (the retail Event list's 9; the forms show 9 rows) */
#define GNT_LIST_VIS 7u            /* rows the list, bracket and page windows show. The row draw makes up to 4 texts per row (count, name, state word, star); the cube reset when a list drew 35 of them (9 rows, 8 starred) and not at 33: 7 rows = at most 28, plus the header status text and the chrome. build 364: 8 rows plus a stray star plus the 'You are in.' status reset it again */
#define GNT_MAX 16u                /* tournaments cached (two pages of the server's 8) */
#define GNT_NAME 12u
#define GNT_TITLE 24u
#define GNT_NUM 12u                /* the entrants column: "64 of max 64" (12 characters) */
#define GNT_OFFICIAL_NAME "Multishine dailies"
#define GNT_LINE 28u               /* characters of a panel line */
#define GNT_SAY 36u                /* characters of a one-line answer ("Too many tries, wait 10 minutes.") */
#define GNT_EXTRA 20u              /* characters of a row's right column ("Double Elimination"); callers give a buffer of GNT_EXTRA + 1 or more */
#define GNT_PRESETS 7u             /* the Create form's Preset row: Custom, Grand Open, Midnight Cup, Pocket Bracket, Locals Night, Evening Cup, Last Call (client side only, no wire change) */
#define GNT_PRESET_VALS 10u
#define GNT_FORM_VIS 9u           /* visible rows of Create / Sign up by ID: 9 rows centred in their own box (the 10th would sit on its edge); the form scrolls */
#define GNT_PANEL 4u
#define GNT_ENTRANTS 64u
#define GNT_MATCHES 127u            /* a 64 double elimination bracket: 2 * 64 - 1 */
#define GNT_NONE 0xFFu             /* an empty slot (a bye, or a winner not known yet) */
#define GNT_REFRESH_GAP 60u
#define GNT_AUTO_FRAMES 900u       /* the list refreshes every 30 s while open */
#define GNT_POLL_FRAMES 150u       /* the bracket is asked again every 5 s (a match may be called any time) */
#define GNT_REP_FIRST 12u
#define GNT_REP_NEXT 3u
#define GNT_REG_N 8u               /* tournament region values: 0 Any, 1..7 NAE NAW EU LA AS OC NAC (ms_region.CODES index + 1; NAC appended) */
#define GNT_LR_GAP 2u              /* frames L and R must both be up before the next press of either cycles the region filter (67 ms at 30 frames/s) */
#define GNT_HOLD_B_FRAMES 30u      /* Hold B: 1.0 s at the 30 frames/s of the overlay (one hold length; Back on the Tournament screens is a hold)  */
#define GNT_RULE 40u               /* characters of a rules line (the wide Tournament panel) */
#define GNT_RULE_N 10u              /* lines of the cup rules page */

#define GNT_B_LEFT 0x0001u
#define GNT_B_RIGHT 0x0002u
#define GNT_B_DOWN 0x0004u
#define GNT_B_UP 0x0008u
#define GNT_B_Z 0x0010u
#define GNT_B_R 0x0020u
#define GNT_B_L 0x0040u
#define GNT_B_A 0x0100u
#define GNT_B_B 0x0200u
#define GNT_B_X 0x0400u
#define GNT_B_Y 0x0800u
#define GNT_B_START 0x1000u

enum { GNT_LIST, GNT_PAGE, GNT_CREATE, GNT_BRACKET, GNT_RULES, GNT_JOIN };   /* JOIN: Sign up by ID (rows ID, Passcode, Join) */
/* the list filter (L R): the REGION, 0 = Any, 1..7 = NAE NAW EU LA AS OC NAC (the server's tournament region byte, TN version 5); a cup with region 0 shows under every region */
enum { GNT_N_LOADING, GNT_N_OK, GNT_N_EMPTY, GNT_N_UNREACH, GNT_N_NOSUP, GNT_N_SIGNIN };
enum { GNT_REQ_NONE, GNT_REQ_LIST, GNT_REQ_INFO, GNT_REQ_JOIN, GNT_REQ_LEAVE, GNT_REQ_CREATE, GNT_REQ_BRACKET, GNT_REQ_PLAY, GNT_REQ_CHECKIN, GNT_REQ_START, GNT_REQ_MYMATCH, GNT_REQ_JOINID, GNT_REQ_CANCEL, GNT_REQ_ANIM };   /* CANCEL: the host calls the cup off (TN_CANCEL); JOINID: join by the 8 digit id + passcode (Tournament v2), then the page of that tournament */
enum { GNT_SFX_NONE = 0, GNT_SFX_BACK = 1, GNT_SFX_FWD = 2, GNT_SFX_MOVE = 3, GNT_SFX_DENY = 4 };
#define GNT_D_ROWS 1u
#define GNT_D_PANEL 2u
#define GNT_D_ALL 3u

/* tournament state (wire 10.3 of the design doc) */
enum { GNT_S_REG, GNT_S_CHECKIN, GNT_S_RUNNING, GNT_S_DONE, GNT_S_CANCEL };
/* entry flags = the wire's eflags (TN_LIST / TN_INFO) */
#define GNT_F_PASS 1u              /* a passcode is asked */
#define GNT_F_CHECKIN_ON 2u        /* the tournament has a check-in window */
#define GNT_F_HOST 4u              /* you are the host */
#define GNT_F_JOINED 8u            /* you are an entrant */
#define GNT_F_CHECKED 16u          /* you checked in */
#define GNT_F_OUT 32u              /* you are out */
#define GNT_F_OFFICIAL 64u         /* an official cup (scheduled by the server, no host) */
#define GNT_F_LATE 128u            /* late registration is open and there are places */
/* my relation (wire `my`): 0 not in, 1 entrant, 2 host + entrant, 3 host only, 4 eliminated or out, 5 champion */
enum { GNT_Y_OUT, GNT_Y_IN, GNT_Y_HOSTIN, GNT_Y_HOST, GNT_Y_ELIM, GNT_Y_WON };
/* match state (wire): pending (a slot is not decided), open (both known), called, playing, done, bye (never played), walkover */
enum { GNT_M_WAIT, GNT_M_READY, GNT_M_CALLED, GNT_M_LIVE, GNT_M_DONE, GNT_M_BYE, GNT_M_WALK, GNT_M_SKIP };
/* match side (wire): winners bracket, losers bracket, grand final (round 1 = GF1, round 2 = the reset) */
enum { GNT_SIDE_W, GNT_SIDE_L, GNT_SIDE_F };
/* format (wire): 0 double elimination (default), 1 single elimination */
enum { GNT_FMT_DOUBLE, GNT_FMT_SINGLE };

struct gnt_ent {
    unsigned short id;
    char name[GNT_NAME + 1], host[GNT_NAME + 1];
    unsigned char state, format, cap, joined, flags, round, rounds, you, checked, seeding, reason;   /* reason: 1 = the last cup was called off, not enough players */
    unsigned long pubid;             /* the 8 digit public id (Tournament v2), 0 unknown */
    unsigned long signup_left;       /* seconds to the start of a hosted cup in sign-up (sign-up timer or scheduled start), 0 none */
    unsigned long start_at;          /* a scheduled start: epoch seconds (0 = a "now" cup) */
    unsigned char region;            /* the cup's region: 0 Any (the dailies, old cups), 1..7 NAE NAW EU LA AS OC NAC; informational and the list filter, never a join limit */
    unsigned char scheduled, host_plays, signup_min;   /* scheduled: 1 = Upcoming (list state word), host_plays: 0 = the organiser does not play (not counted), signup_min: 15/30/60/120 or 0 */
    unsigned long secs;              /* an official cup: seconds until its start (as of the last answer), else 0 */   /* joined: entrants; round: the round being played (1..rounds, 0 before the start), rounds: total; you: the wire's `my` (GNT_Y_*) */
    char champion[GNT_NAME + 1];
};

struct gnt_match {
    unsigned char mid, side, round, state;   /* mid 1..127, side GNT_SIDE_*, round 1.. within the side, GNT_M_* */
    unsigned char a, b;              /* entrant index (seed - 1) or GNT_NONE (not decided yet, or a bye) */
    unsigned char wa, wb;            /* game wins */
    unsigned char win;               /* 0 none, 1 a, 2 b */
    unsigned char bo, next_win, next_lose;   /* games of the set (3 or 5), the match ids the winner / loser go to (0 none) */
};

struct gnt {
    unsigned char scr;               /* GNT_* */
    unsigned char net;               /* GNT_N_* of the list */
    unsigned char n;                 /* tournaments in the list (as the server sent them, g->e) */
    unsigned char f_user;            /* the region filter is the player's own pick (L R); 0 = Auto: the filter follows the account's region (as Game Night) */
    unsigned char nv, filter;        /* shown rows (g->v) and the region filter 0..7; v: the shown rows as indexes into e: dailies first (soonest start first), then custom cups */
    unsigned char v[GNT_MAX];
    unsigned char dl[GNT_MAX], dn, dsel;   /* the Multishine dailies, soonest first (indexes into e), their count and the one the top row shows (Left Right cycle; v[0] = dl[dsel] when dn) */
    unsigned short did;              /* id of the daily the top row shows, kept across refreshes */
    unsigned char sel, first;        /* list cursor row, scroll position */
    unsigned char req, busy;         /* GNT_REQ_* raised for the glue; a request is out */
    unsigned char exit, sfx, dirty, gfx, gfx_tried;
    unsigned char rep, rep_dir;
    unsigned char lr_cool;           /* frames left before L / R count as pressed again: L or R seen held re-arms it (GNT_LR_GAP), so a held or chattering trigger cycles the region filter once */
    unsigned char hold_a;            /* frames A has been held on the cup page's Call Off Tournament row (GNT_HOLD_B_FRAMES calls off, +1 = spent until released) */
    unsigned char hold_b;            /* frames B has been held (GNT_HOLD_B_FRAMES goes back; GNT_HOLD_B_FRAMES + 1 = spent until released) */
    unsigned short req_id;           /* tournament id of the request */
    unsigned short cool, poll, auto_t, prev;
    unsigned long next_cup;          /* seconds until the next official cup starts, 0xFFFFFFFF = none scheduled */
    unsigned char mock, can_create;  /* mock: example data; can_create: the server allows creating (a capability) */
    unsigned char fail;              /* last failure (a short code shown as a line), 0 none */
    char say[GNT_SAY + 1];          /* a one-line answer on the page ("Joined.") */
    unsigned char say_warn;
    struct gnt_ent e[GNT_MAX];
    struct gnt_ent cur;              /* the tournament of PAGE and BRACKET */
    unsigned char auto_mine;         /* gnt_enter_mine: the next lists open the bracket of the running cup you are in (back from a match room after the overlay was reloaded) */
    /* create form */
    unsigned char cf_row, cf_name, cf_size, cf_format, cf_seed, cf_checkin;   /* cf_seed: 0 random, 1 rating, 2 join order */
    unsigned char cf_signup, cf_hostplays, cf_start, cf_pass_on;   /* Tournament v2: cf_signup index into 15/30/60/120 (default 2 = 60), cf_hostplays 1/0 (default 1), cf_start index into the start presets (0 = Now), cf_pass_on: a passcode is set */
    unsigned char cf_region, cf_region_set, acct_rgn;   /* the Create form's Region (0 Any, 1..7); cf_region_set: the player changed it; acct_rgn: the account's region 0..7 (the default for the form), given by the glue */
    unsigned char cf_days, cf_hours; /* the Start row's "Pick time": days 0..30 and hours 0..23 from now (at least 1 hour, at most 30 days) */
    unsigned char cf_preset;         /* the Preset row: 0 Custom, 1..GNT_PRESETS-1 a named preset; any change to a row a preset sets turns it back to Custom */
    unsigned char cf_own[GNT_PRESET_VALS];   /* Custom's values (format, size, sign-up, check-in, seeding, host plays, start, day, hour, passcode on), kept while the player flicks through the presets */
    char cf_pass[9];                 /* the passcode typed in Create (8 digits) */
    unsigned char cf_pass_pos;
    char jid[9], jpw[9];             /* Sign up by ID: the 8 digit id and the passcode being typed */
    unsigned char jid_pos, jpw_pos, jpw_on;   /* jpw_on: a passcode is typed (private cup) */
    /* bracket */
    unsigned char b_rx, b_have, b_n, b_size, b_rounds, b_format, b_me, b_side, b_round, b_sel, b_first, b_tree;   /* b_tree: Z shows the bracket as a tree (gnt_tree.h) instead of the rows of the round (console only); b_me: your entrant index or GNT_NONE; b_round: round shown (1..); b_sel: match row in the round */
    char b_name[GNT_ENTRANTS][GNT_NAME + 1];
    unsigned char b_nstate[GNT_ENTRANTS];
    struct gnt_match b_m[GNT_MATCHES];
    /* the call: your match is ready */
    unsigned char called, call_round, call_side, my_kind, my_place;   /* my_kind: the last TN_MYMATCH kind (0 none yet), my_place: the place when out */
    char call_opp[GNT_NAME + 1], call_room[9];
    unsigned short call_secs;
    unsigned short loads;
    unsigned short peeked;           /* LIST: the tournament id whose INFO was asked for the description box (the list carries no host or round) */
    unsigned char dwell, my_asked, dismissed;   /* dismissed: Back was held on the bracket of the call in call_room: the poll must not pull the player back in */
    unsigned long clk, call_clk;     /* frames (30 a second); the frame the call's seconds were read */
    char dis_room[9];   /* LIST: frames the cursor rested on a row that lacks its host; PAGE: the place of a finished tournament was asked */
};

/* ---- life cycle and input ---- */
void gnt_enter(struct gnt *g);                                         /* the list, Loading, page asked */
void gnt_frame(struct gnt *g, unsigned held, int stick_up, int stick_down);
/* ---- answers from the glue ---- */
void gnt_net_list(struct gnt *g, unsigned flags, unsigned n, const struct gnt_ent *e);   /* flags bit 0: example data, bit 1: the server allows Create */
void gnt_net_info(struct gnt *g, const struct gnt_ent *e);
void gnt_net_next(struct gnt *g, unsigned long secs);                  /* TN_LIST version 2: next_cup_secs (after gnt_net_list) */
void gnt_fmt_secs(char *out, unsigned long secs);                      /* "2h 15m", "42m", "under 1m" */
void gnt_net_done(struct gnt *g, unsigned what, unsigned ok);          /* GNT_REQ_JOIN / LEAVE / CREATE / FORFEIT answered (ok 0 = refused) */
void gnt_net_created(struct gnt *g, const struct gnt_ent *e);          /* CREATE answered: the new tournament's page */
void gnt_net_bracket(struct gnt *g, unsigned n, unsigned size, unsigned me, const char (*names)[GNT_NAME + 1], unsigned nm, const struct gnt_match *m);   /* (host test helper: single elimination in one call) */
/* the bracket arrives page by page (TN_BRACKET): begin, then every match and every name as they come, then end (me: the wire's `me` byte) */
struct tmb_pack;
/* the animated bracket: pack the cached winners bracket for the engine's retail-scene view.  0 = packed; 1 no bracket / not a full power-of-two cup of 4..64 (byes
   are not supported yet); 2 a feeder structure the retail table does not have; 3 the results do not fit the table.  `ver` goes into the pack: the engine writes the snapshot again when it changes. */
unsigned gnt_tmv_pack(const struct gnt *g, struct tmb_pack *out, unsigned ver);
void gnt_br_begin(struct gnt *g, unsigned n, unsigned size, unsigned format);
void gnt_br_match(struct gnt *g, unsigned mid, const struct gnt_match *m);          /* mid 1.. (round 1 first) */
void gnt_br_name(struct gnt *g, unsigned i, const char *name, unsigned state);
void gnt_br_end(struct gnt *g, unsigned me);                                       /* me: your entrant index, 0xFF none */
void gnt_resume(struct gnt *g, const struct gnt_ent *cur);
void gnt_enter_mine(struct gnt *g);   /* the list, then by itself the bracket of the running cup the player is in (none: the list stays) */                          /* back from a match room: the bracket of `cur` again, asked fresh */
void gnt_net_call(struct gnt *g, unsigned side, unsigned round, const char *opp, const char *room, unsigned secs);     /* your match is called: the room code to join */
void gnt_net_fail(struct gnt *g, unsigned what, unsigned why);          /* GNT_W_*: what = GNT_REQ_* */
enum { GNT_W_UNREACH, GNT_W_NOSUP, GNT_W_BAD, GNT_W_RATE, GNT_W_FULL, GNT_W_CLOSED, GNT_W_NOTLINKED, GNT_W_LIMIT, GNT_W_DENIED, GNT_W_NOANIM };
void gnt_net_my(struct gnt *g, unsigned kind, unsigned place);          /* TN_MYMATCH answered with something other than a call (kind 0..6, 2 is gnt_net_call): clears a stale call */
const char *gnt_create_name(const struct gnt *g);                      /* the Create form's values, for the glue's TN_CREATE */
unsigned gnt_create_cap(const struct gnt *g);
unsigned gnt_create_format(const struct gnt *g);                      /* GNT_FMT_* */
unsigned gnt_create_flags(const struct gnt *g);                       /* TN_CREATE flags: bit 1 seed by rating */
unsigned gnt_create_signup(const struct gnt *g);                       /* Tournament v2 create options: sign-up minutes 15 / 30 / 60 / 120 */
unsigned gnt_create_seeding(const struct gnt *g);                      /* 0 random, 1 rating, 2 join order */
unsigned gnt_create_hostplays(const struct gnt *g);                    /* 1 / 0 */
unsigned long gnt_create_start_in(const struct gnt *g);                /* 0 = Now, else seconds from now (300 .. 2592000) for a scheduled start */
const char *gnt_create_pass(const struct gnt *g);                      /* the Create passcode: 8 digits or "" (none) */
unsigned long gnt_join_id(const struct gnt *g);                        /* Sign up by ID: the typed id as a number, 0 while it is not 8 digits */
const char *gnt_join_pass(const struct gnt *g);                        /* Sign up by ID: the typed passcode, "" when empty or not 8 digits */
unsigned gnt_create_checkin(const struct gnt *g);                      /* 0, 5 or 10 minutes */
void gnt_played(struct gnt *g);                                         /* the glue took the call (joined the room): clears it */
void gnt_back_from_room(struct gnt *g);                                 /* the room of a bracket match was left: the bracket again, asked fresh */

/* ---- what the screen shows ---- */
unsigned gnt_rows(const struct gnt *g);                                 /* rows of the current list (0 in a state screen) */
unsigned gnt_cur_page(const struct gnt *g);                             /* cursor row on screen 0..8 */
unsigned gnt_cur_first(const struct gnt *g);
unsigned gnt_vis(const struct gnt *g);        /* visible rows of the current screen: GNT_FORM_VIS on Create / Sign up by ID, else GNT_VIS */
unsigned gnt_max_first(const struct gnt *g);
unsigned gnt_scroll_more(const struct gnt *g);   /* bit 0: rows above the window, bit 1: rows below (the markers of the Tournament look; list, cup page, Create, Sign up by ID) */
/* row `slot`: returns 0 for an empty slot, else 1 + attribute (0 normal, 1 dim, 2 highlight (yours / ready), 3 warning) */
unsigned gnt_row(const struct gnt *g, unsigned slot, char *num, char *title, char *extra);
unsigned gnt_status_view(const struct gnt *g);                          /* the list shows a status message: its line count 1..3, else 0 */
unsigned gnt_panel(const struct gnt *g, unsigned line, char *out);       /* attribute as gnt_row */
const char *gnt_title(const struct gnt *g);
const char *gnt_hint(const struct gnt *g);                               /* the footer bar's keys */
const char *gnt_hint2(const struct gnt *g);                              /* LIST: the action keys (A Z X Y) for the line above the footer bar, else "" */
const char *gnt_filter_name(const struct gnt *g);                        /* the region filter: "Auto" / "Any" / "NAE" / "NAW" / "EU" / "LA" / "AS" / "OC" / "NAC" */
void gnt_set_account_region(struct gnt *g, unsigned rgn);               /* the glue: the account's region (sg rgn: 1..7, anything else = none) is the Create form's default until the player changes it */
unsigned gnt_create_region(const struct gnt *g);                         /* 0 Any, 1..7 for TN_CREATE */
unsigned gnt_row_cycle(const struct gnt *g, unsigned slot);              /* LIST: 1 on the dailies row with more than one daily (the console draws < > at the box edges) */
unsigned gnt_row_star(const struct gnt *g, unsigned slot);               /* LIST: 1 when the row is a cup you are signed up for (the console draws a gold star) */
unsigned gnt_box(const struct gnt *g, unsigned line, char *out);         /* the small box: 3 centred lines ("3", "of 8", "joined"), returns 1 when it has text */
unsigned gnt_calls(const struct gnt *g);                                /* 1: a match is called (the screen shows the call) */
const char *gnt_ent_name(const struct gnt_ent *e);                    /* the name the console shows: an official cup is 'Multishine dailies' (the server calls it 'Official Cup'), else the entry's own name */
const char *gnt_cur_name(const struct gnt *g);                         /* the same for the open tournament (page title, match room) */
unsigned gnt_rules(const struct gnt *g, unsigned line, char *out);       /* RULES: line 0..GNT_RULE_N - 1 of the cup rules, returns 1 when it has text */
unsigned gnt_call_info(const struct gnt *g, char *round_title, unsigned *bo);  /* the match room's identity of the call: the round title ("Losers Round 1", at most 30 characters), the games of the set (1 when unknown); 0 = no call */
unsigned gnt_call_secs(const struct gnt *g);                             /* seconds the server gave for the call (as of the last answer), 0 none */
void gnt_state_text(const struct gnt *g, char *out);                     /* PAGE: the tournament's state in a few words for the title bar ("Check-in open", "Round 2 of 4") */
const char *gnt_z_label(const struct gnt *g);                            /* what Z does now: "Sign up", "Leave", "Check in", or "" (LIST row / RULES) */
unsigned gnt_is_mine(const struct gnt *g, unsigned slot);                /* BRACKET: the row's match is yours */
void gnt_text(const struct gnt *g, struct sg_view *v);

/* ---- bracket helpers (pure) ---- */
unsigned gnt_round_count(unsigned size);                                /* 4 -> 2, 8 -> 3 ... 64 -> 6 */
unsigned gnt_size_for(unsigned n);                                      /* the bracket size for n entrants: next power of two >= max(n, 2), at most 64 */
unsigned gnt_round_matches(unsigned size, unsigned round);              /* matches of a round (1-based) */
unsigned gnt_round_start(unsigned size, unsigned round);                /* index of the first match of a round in the match array (round 1 first) */
const char *gnt_round_name(unsigned rounds, unsigned round, char *buf);
void gnt_round_title(const struct gnt *g, unsigned side, unsigned round, char *buf);   /* a round's title for a column head: "Winners Semifinal", "Losers Round 3", "Grand Final" (at most 30 characters) */
unsigned gnt_tree_on(const struct gnt *g);      /* 1: the console draws the bracket as a tree (b_tree and the Event screen is up) */  /* "Final", "Semifinal", "Quarterfinal", "Round N" */

#endif
