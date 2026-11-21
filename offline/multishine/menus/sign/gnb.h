/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Night room browser ("hybrid"): the MODEL of the Melee-style browser.  Pure C89, no libc,
 * no heap, no Melee address, no network: it owns the room list cache, the cursor (the retail Event Match names: `first` = scroll position, `page` = cursor row
 * 0..8), the screens of the browser (list, Create Room form, passcode entry, joining, room lobby), the key map and every text the screen shows.  Two front ends
 * draw it:
 *   - gnb_ev.c (console only, -DGNB_MELEE): the retail Event Match screen (MnMaAll art, cursor, arrows, sounds) with our rows and panel texts,
 *   - gnb_text (here): the same texts in the sign overlay's character-cell view (host golden tests; console fallback when the Event screen cannot be built).
 * The network is not here either: the model raises a request (gnb.req) and the glue (sg_entry.c, client_net/gnc.c) answers through gnb_net_* below.
 * Two data sources: LIVE (the Game Night row: GN_LIST / GN_JOIN / GN_CREATE / GN_STATE of the rooms server) and EXAMPLE (the Mockups hub entry
 * "Rooms Browser (Graphic)": 8 invented rooms compiled in, R cycles the states).  Rooms the server marks as mock (entry flag 32, server option --rooms-mock) carry
 * a "Mock" tag in the list and the panel.
 * Text: menu font safe set only (A-Z a-z 0-9 space. , ' - :), Title Case labels; a row title is at most GNB_TITLE characters, a panel line GNB_LINE.
 * Room types (pieces D and E2): the Create form's Type row, the list's type column and filter, the lobby's type name and set score, and the
 * pick grid's rules (excluded characters greyed, a forced character) come from list v6 / view v6; against an older server (no list v6) everything behaves as before.
 */
#ifndef GNB_H
#define GNB_H

#include "qc_pick.h"            /* the D-Pad quick-chat picker */
#include "gnb_prep.h"           /* the stage prep screen between two games of a set (Sweaty, Grandmaster): strike / ban / pick, E1 */

#define GNB_VIS 9u                 /* rows of the retail Event list */
#define GNB_MAX 24u                /* rooms cached (3 pages of the server's 8) */
#define GNB_PANEL 8u               /* panel lines */
#define GNB_TITLE 24u              /* characters of a row title (room name 12 + " Mock") */
#define GNB_NUM 8u                 /* characters of the number column ("12 of 32") */
#define GNB_LINE 24u               /* characters of a panel line */
#define GNB_LOAD_FRAMES 45u        /* EXAMPLE data: a refresh shows Loading this long (1.5 s at 30 frames/s) */
#define GNB_REFRESH_GAP 60u        /* frames between two Start refreshes (2 s) */
#define GNB_AUTO_FRAMES 300u       /* LIVE list: automatic refresh every 10 s while the list is open (the server allows 20 lists a minute) */
#define GNB_POLL_FRAMES 60u        /* LIVE lobby: GN_STATE every 2 s */
#define GNB_PREP_POLL 30u          /* while the stage prep screen is up: every second (the other side's strike shows within a second) */
#define GNB_REP_FIRST 12u          /* held Up/Down: first repeat after 12 frames, then every 3 */
#define GNB_REP_NEXT 3u

/* buttons (HSD_PadStatus.button bits, the same values as sg_ui.h SG_B_*) */
#define GNB_B_LEFT 0x0001u
#define GNB_B_RIGHT 0x0002u
#define GNB_B_DOWN 0x0004u
#define GNB_B_UP 0x0008u
#define GNB_B_Z 0x0010u
#define GNB_B_R 0x0020u
#define GNB_B_L 0x0040u
#define GNB_B_A 0x0100u
#define GNB_B_B 0x0200u
#define GNB_B_X 0x0400u
#define GNB_B_Y 0x0800u
#define GNB_B_START 0x1000u

/* list entry flags (GN_LIST entry; 16 and 32 only in v2 replies) */
#define GNB_F_SPECT 2u
#define GNB_F_PASS 4u
#define GNB_F_FULL 8u
#define GNB_F_RUN 16u              /* a set is being played (call-up or playing) */
#define GNB_F_MOCK 32u             /* a synthetic room the server advertises with --rooms-mock: shown with a Mock tag */

/* screens of the browser */
enum { GNB_LIST, GNB_CREATE, GNB_PASS, GNB_BUSY, GNB_LOBBY };
/* list state */
enum { GNB_N_LOADING, GNB_N_OK, GNB_N_EMPTY, GNB_N_UNREACH, GNB_N_NOGN, GNB_N_SIGNIN };
/* EXAMPLE data states (R cycles them in the Mockups variant) */
enum { GNB_X_LIVE, GNB_X_LOADING, GNB_X_UNREACH, GNB_X_EMPTY, GNB_X_FULL, GNB_X_SPECONLY, GNB_X_N };
/* requests to the glue (gnb.req) */
enum { GNB_REQ_NONE, GNB_REQ_LIST, GNB_REQ_JOIN, GNB_REQ_CREATE, GNB_REQ_STATE, GNB_REQ_ACTION, GNB_REQ_LEAVE, GNB_REQ_ROSTER, GNB_REQ_CHAT, GNB_REQ_CHATSEND };
/* sounds the front end plays (gnb.sfx): the retail menu ids */
enum { GNB_SFX_NONE = 0, GNB_SFX_BACK = 1, GNB_SFX_FWD = 2, GNB_SFX_MOVE = 3, GNB_SFX_DENY = 4 };
/* dirty bits for the front end */
#define GNB_D_ROWS 1u
#define GNB_D_PANEL 2u
#define GNB_D_CURSOR 4u
#define GNB_D_ALL 7u

struct gnb_room {
    char code[9], name[13], host[13];
    unsigned char mode, mem, cap, flags, stadium;
    unsigned char region, smode;   /* list v3: region index (0 NAE, 1 NAW, 2 EU, 3 LA, 4 AS, 5 OC, 6 NAC, 0xFF unknown), stage mode (0 Random) */
    unsigned char rtype;           /* list v6: the room type (GNB_RT_*); an older server: 0 Chill, or 2 Wild when the stage mode is All */
    unsigned short minutes;        /* list v3: minutes the room has been open (server time; not shown in the list) */
};

/* the Y crowns panel (GN_STATE with the roster flag, ms_rooms.Room.roster): the room's members by crowns; place 0xFE winner side, 0xFD challenger side, 1.. queue position, 0 none; crowns bit 7 = the viewer, count <= 127 */
#define GNB_ROSTER_MAX 8u
struct gnb_roster { unsigned char n, streak, crowns[GNB_ROSTER_MAX], place[GNB_ROSTER_MAX], pstreak[GNB_ROSTER_MAX], plead[GNB_ROSTER_MAX]; char name[GNB_ROSTER_MAX][13]; };   /* pstreak: the player's streak wins of the night (a win that follows their own win in a row; the first win of a run is not one); plead: 1 = in the lead, set by the server, which ranks */

/* the parsed personal view (parse_view) */
struct gnb_lobby {
    unsigned char have, rev, phase, flags, members, qlen, pos, side, streak, crowns, secs, nnext, rflags, stadium, mode;
    unsigned char side_on[2], side_char[2], side_crowns[2];
    char code[9], rname[13], side_name[2][13], next[8][13];
    /* view v3 (Rooms-aligned): v3 = 1 when the server sent them; turn 0 winner side picks, 1 challenger picks, GNB_TURN_GO both picked
       (the called pair starts its search), 0xFF no call-up; picked bit 0 winner side, bit 1 challenger; stage 0xFF none; pw = the room's password ("" = open) */
    unsigned char v3, region, smode, cap, turn, picked, stage;
    unsigned short set_id, minutes;   /* minutes: how long the room has been open (view v3, server time, saturating) */
    unsigned char wfacts;             /* view v4: GNB_VF_* bits, 0 from a server that sends no v4 view (then nothing is greyed) */
    char pw[5];
    /* view v6 trailer: 0 / 0xFF from an older view. step: 0 none, 2 strike, 3 ban, 4 pick stage, 5 pick character (piece E1 draws 2..4); step_owner = the SIDE whose action is awaited */
    unsigned char v6, rtype, best_of, game, score_w, score_c, step, step_owner, step_secs, stage_avail, stage_n, stage_list[8], forced, struck;
    unsigned long excl, rules_hash;   /* excl: bit c = external character c may not be played; forced: the character both sides play (Ditto Dojo), 0xFF none */
};
#define GNB_TURN_WAIT 0xFDu        /* view v6 turn: Sweaty between two games, nothing runs until both consoles are back in the room */
#define GNB_STEP_PICK_CHAR 5u
/* room types (ms_roomtypes ids, stable) */
enum { GNB_RT_CHILL, GNB_RT_SWEATY, GNB_RT_WILD, GNB_RT_WILD_CRAZY, GNB_RT_DITTO, GNB_RT_LOWMID, GNB_RT_N };
#define GNB_RT_NONE 0xFFu          /* create: no type (the server's default, Chill); view: no forced character */
#define GNB_TURN_GO 0xFEu
#define GNB_VF_NOREPLAY 1u         /* the room has no last game a watcher could replay: X would end with the watcher's own message */
#define GNB_VF_NOLIVE 2u           /* no game of the room is being uploaded: X: Live would end the same way */
#define GNB_AUTO_MAX 7u            /* consecutive automatic watches without the lobby seeing the set end (the chain is carried in 3 bits of gn_room[19]) */
#define GNB_V_CROWNED 1u
#define GNB_V_CALLED 2u
#define GNB_V_READY 4u
#define GNB_V_QUEUED 8u
#define GNB_V_ONSIDE 16u
#define GNB_V_OWNER 32u
#define GNB_V_BOUND 64u

/* room chat on the console: the strip of the last lines in the lobby's bottom box, the D-Pad quick-chat picker (qc_pick.h) */
#define GNB_CL_N 3u                /* lines of the strip */
#define GNB_CL_NAME 13u
#define GNB_CL_TXT 48u             /* characters of a line kept ("Name: " + this fits the strip) */
#define GNB_CL_LIFE_S 60u          /* seconds a chat line stays in the room strip (counted from its post) */
#define GNB_CL_FLASH 90u           /* frames a new line is drawn in the highlight colour (3 s at 30 frames/s) */
#define GNB_CHAT_MUTE_FRAMES 900u  /* after the host muted us the picker refuses for this long, then tries once more (30 s) */
struct gnb_cl { char name[GNB_CL_NAME]; char text[GNB_CL_TXT]; unsigned char flash; unsigned short ttl; };     /* ttl: frames the line stays in the strip (GNB_CL_LIFE_S seconds from its post: old lines must not hang around, nor greet a console that just joined) */
struct gnb_chat_in { unsigned long id; char name[GNB_CL_NAME]; char text[GNB_CL_TXT]; unsigned age; };     /* age: seconds since it was posted (the server's FETCH field) */

/* a Grandmaster set on the browser (gnb_rk.c): the lobby is the set's screen, the glue (sg_entry.c) polls RK_SET_STATE */
#define GNRK_REPLAY_FRAMES 360u    /* 12 s at the overlay's 30 frames/s: a READY view for the game just played that does not change is a replay (tie, void game) */
struct gnrk {
    unsigned char on;              /* 1: the browser shows a Grandmaster set */
    unsigned char fin;             /* the set is over (the result is in msg): A or B leaves */
    unsigned char played;          /* the last game this console started (the hand-off block's game byte), 0 = none yet */
    unsigned char go;              /* 1: the next game is decided: the glue arms the hand-off */
    unsigned char wait;            /* 1: the view is READY for the game that was just played, the server has not counted it yet */
    unsigned char game, stage, ch, cos;   /* go: the game number, its stage (external id), this console's character and costume as the server knows them (0xFF: not known) */
    unsigned short waited;         /* frames spent waiting at the same game */
};
struct gnb {
    unsigned char live;            /* 1 = rooms server (Game Night row), 0 = example data (Mockups hub) */
    unsigned char scr;             /* GNB_LIST ... */
    unsigned char net;             /* GNB_N_* of the list */
    unsigned char n, total;        /* rooms cached / rooms the server lists */
    unsigned char first, page;     /* retail names: first shown room, cursor row 0..8 */
    unsigned char xst;             /* EXAMPLE state (GNB_X_*) */
    unsigned char dirty;           /* GNB_D_* */
    unsigned char req;             /* GNB_REQ_* for the glue, cleared by the glue when taken */
    unsigned char act, act_val;    /* GNB_REQ_ACTION: action and value (GN_ACTION) */
    unsigned char exit;            /* 1: the player left the browser (B on the list) */
    unsigned char frd, back_frd;   /*: frd = the room was joined from the Friends list (sgm_frd_join); leaving the room then sets back_frd and exit: the Friends list opens again, not the Game Night list */
    unsigned char sfx;             /* GNB_SFX_* to play this frame (front end clears it) */
    unsigned char busy_what;       /* GNB_BUSY: what is running (GNB_REQ_*) */
    unsigned char lpage;           /* GNB_REQ_LIST: the page to fetch (0 = a refresh from the top; later pages are fetched while n < total) */
    unsigned char rep, rep_dir;    /* held Up/Down repeat */
    unsigned short timer;          /* EXAMPLE loading delay / LIVE auto refresh / lobby poll */
    unsigned short cool;           /* Start cooldown */
    unsigned short prev;           /* buttons held last frame */
    unsigned short real_held, real_prev;   /* the pad's REAL buttons (sg_mockup.c sets real_held and real_on): `held` also carries the stick pushed sideways as the d-pad; the quick-chat picker (the D-Pad opens it) must not open from the stick */
    unsigned char real_on;
    const char *msg;               /* status line of the panel, 0 = none */
    struct gnb_room r[GNB_MAX];
    /* Create Room form */
    unsigned char c_row, c_size, c_spect, c_listed, c_pass;      /* c_pass: 1 = a passcode is set (c_pin) */
    unsigned char c_all;           /* Create Room "Stages" against a server without room types: 0 Legal, 1 All (All only while all_ok) */
    unsigned char c_type;          /* Create Room "Mode" (room types served): the GNB_RT_* chosen (the row cycles the six) */
    unsigned char f_stage;         /* the list's filter (More popup): room types served = 0 every room, else the type + 1; an older server: the Stages filter, 0 every room, 1 Legal, 2 All stages (a server-side filter, list v4; also applied here) */
    unsigned char f_reg;           /* the Region plate: 0 Any, else the account region code 1..7 (NAE NAW EU LA AS OC NAC; room region index + 1); filtered here when a page arrives */
    unsigned char acct_rgn;        /* the glue: the account's region (sg rgn 1..7, 8 not set, 0 not known); f_reg follows it until the player cycles the plate */
    unsigned char reg_user, reg_seen;
    unsigned char more, more_row, more_chg;   /* the More popup: open, row under the cursor (GNB_MORE_*), a value changed (the list is fetched again on close) */
    unsigned char f_hide, f_sort;  /* More: hide full rooms (1 yes), sort (GNB_SORT_*) */
    unsigned char got;             /* rooms of the server's list received so far (the cache keeps only those that pass the filters) */
    unsigned char all_ok;          /* the glue: the last GN_LIST v5 reply carries the All-stages flag (our switched it on); without it the choice is greyed "Coming soon" */
    unsigned char rt_ok;           /* the glue: the last GN_LIST was a v6 reply (list flag bit 2: the server serves room types); 0 = behave as before (Stages row, Stages filter) */
    unsigned char items_ok;        /* the glue: the v6 list says the items switch is on (Wild And Crazy may be created) */
    unsigned char c_fskip;         /* "Friends Skip Passcode" (default 1 = Yes): the host's friends join without the passcode (create flag 8, always sent with the choice) */
    char c_pin[5];
    /* passcode entry (join or create) */
    char p_pin[5];
    unsigned char p_pos, p_for;    /* digit under the cursor; 0 = joining room `sel` (passcode), 1 = the Create Room form, 2 = Join by room code */
    unsigned char sel;             /* the room being joined */
    unsigned char fj, fpin, frf, fj_lob;   /* fj_lob: that join started in a room lobby (a toast's room invite): a failed join resumes that room */   /* a join from the Friends list (gnb_join_friend): fj = the code in fcode is the room, fpin = the passcode screen was answered (send p_pin), frf = the friend entry's room flags (bit 0 passcode asked, bit 1 the room has one) */
    char fcode[5];
    char my_name[13];              /* the host's display name (Create Room preset "<name>'s Room" is not offered: names may not fit; kept for the lobby "You") */
    struct gnb_lobby lob;
    unsigned char lob_ex;          /* EXAMPLE lobby: ready bits of the 5 example members */
    unsigned short polls;          /* lobby polls done (proof) */
    unsigned char gfx;             /* set by the console glue while the Event screen front end owns the screen (the glue then performs the exit) */
    unsigned char gfx_tried;       /* the glue tried to build the Event screen for this visit (a failure falls back to the text view once, no retry loop) */
    /* original-style list: action row Create / Join / Mode / Region / Random and the lobby's hold-B leave */
    unsigned char act_sel;         /* GNB_ACT_* under the action-row cursor */
    unsigned char focus;           /* 0 = the list rows (A joins the row), 1 = the action row (A runs the button) */
    unsigned char hold_x;          /* lobby: frames X has been held (GNB_HOLD_B_FRAMES stops watching) */
    unsigned char hold_b;          /* lobby: frames B has been held (GNB_HOLD_B_FRAMES leaves the room) */
    unsigned char rnd;             /* free-running counter: Random picks from it */
    /* the match loop: the hand-off to the character screen when both picked, the resume after the set */
    unsigned char play;            /* 1: the call-up is at "go": the glue arms the room search (play_name) and the browser closes; 2: armed */
    unsigned char resumed;         /* 1 looked for a hand-off at entry, 2 resumed the room after a set (gnb_resume) */
    char play_name[20];            /* the call-up ticket name GN<code>S<set> */
    unsigned char watch_live;      /* the X press found a set running (view phase 2): the watch is the LIVE one */
    unsigned char autow;           /* AUTO-WATCH: 1 from the first manual Watch until Hold B leaves the room, a Join, or R turns it off: the next game of a running set is watched at once; carried over a watch in gn_room[19] (sg_entry.c) */
    unsigned char autoc;           /* automatic watches in a row (reset when the lobby sees a view whose set is not running; at GNB_AUTO_MAX the chain stops) */
    unsigned char watch;           /* 1: X pressed in a spectatable room ("X: Last game"): the glue arms the watch hand-off and the browser closes; 2: armed */
    /* the in-room pick (the original Rooms grid popup): opened by A on "Your Pick", 27 cells in the retail character-select order
       (external ids 0..25, then 26 Random: rows of 7, the last row 6), A locks (GN_ACTION ready + the id), B closes it keeping the previous pick */
    unsigned char ros_open, ros_have;   /* Y: the room's crowns panel is up; ros holds a reply (the poll then asks GN_STATE with the roster flag) */
    struct gnb_roster ros;
    /* room chat */
    struct qc qc;                  /* the quick-chat picker (D-Pad in the lobby) */
    unsigned char qc_on;           /* the glue: a linked console on a server with rooms, the 16 texts are loaded */
    unsigned char chat_pend, chat_arg, chat_busy;   /* a send waiting for the glue's turn (index + 1) / the index being sent / a send in flight */
    unsigned char chat_muted_f;    /* frames-left counter source: nonzero while the host's mute holds (set by a CHAT_INVALID answer) */
    unsigned short chat_mute_left; /* frames until the picker tries again after "The host muted you" */
    unsigned char chat_first;      /* the first fetch of this room was taken (its lines are not flashed) */
    unsigned long chat_cur;        /* the newest line id seen: the next FETCH asks for more */
    unsigned long clk;             /* frames since the browser opened (the picker's clock, the send limit) */
    unsigned long chat_t[6];       /* the frames of the last six sends (the server allows 6 in 10 s) */
    unsigned char chat_tn;
    unsigned char ncl;             /* lines in the strip */
    struct gnb_cl cl[GNB_CL_N];    /* oldest first */
    unsigned char ntxt;            /* quick-chat texts known (non-empty) */
    char txt[QC_N][QC_TXT];        /* the 16 texts IN PLACE (slot = group * 4 + position) */
    unsigned char pick_open;       /* 1: the grid is up */
    unsigned char pick_lock;       /* 1: the grid shows the room's forced character, locked (Ditto Dojo: the player who does not pick sees what the picker chose; nothing can be changed) */
    unsigned char pick_cur;        /* the cell under the gold cursor: external id 0..25, 26 Random */
    unsigned char pick_cos[27];    /* the costume (0-based) chosen per character in this session (L / R in the picker); goes with the pick (GN_ACTION ready, third byte) */
    unsigned char act_cos;         /* GNB_REQ_ACTION ready: the costume byte to send (0xFF = none) */
    unsigned char st_miss;         /* lobby state polls in a row that got no answer: "Server not available" only from the third (one lost poll showed it) */
    unsigned char pick_ok;         /* 1 once this console locked a fighter (pick_mine and pick_cos survive gnb_enter, the browser is opened again after every game) */
    unsigned char pick_mine;       /* the character this console locked last (GNB_CHAR_NONE: none yet): its slot shows it before the server's view does */
    unsigned char pick_secs;       /* seconds left of the pick turn in the last view (0xFF: no turn running) */
    unsigned short pick_frames;    /* frames since that view: the clock counts down locally between two views */
    struct gnp prep;               /* the stage prep screen of a set room (Sweaty): strike / pick the stage, the wait between two games (gnb_prep.h); prep.open = it replaces the lobby's centre */
    struct gnrk rk;                /* a Grandmaster set instead of a room (gnb_rk.c) */
    struct { unsigned char on, bo, over; unsigned short secs, shown; unsigned long t0; char name[19], round[24], opp[13]; } tn;   /* the match room of a tournament call: its identity replaces the Game Night chrome (gnb_tn_set) */
    unsigned short busy_t;         /* frames the current busy screen (a request in flight) has been up: from GNB_BUSY_ESC on, holding B backs out (a rejoin after a crash left B dead until the request timed out) */
};
#define GNB_PICK_N 27u             /* grid cells: 26 fighters and Random */
#define GNB_PICK_COLS 7u
#define GNB_PICK_ROWS 4u
#define GNB_FPS 30u                /* the overlay's frame rate (the countdown) */
#define GNB_BUSY_ESC (3u * 30u)   /* a busy screen older than 3 s can be left by holding B for GNB_HOLD_B_FRAMES (the menu exit, like a tap of B on the list) */
#define GNB_HOLD_B_FRAMES 30u      /* 1.0 s at the 30 frames/s of the overlay (one hold threshold, same value as SG_HOLD_B_FRAMES) */
/* the action bar: Join random is first and the one selected by default (joining beats creating empty rooms) */
enum { GNB_ACT_RANDOM, GNB_ACT_CREATE, GNB_ACT_JOIN, GNB_ACT_MORE, GNB_ACT_N };
/* the More popup's rows */
enum { GNB_MORE_STAGES, GNB_MORE_HIDE, GNB_MORE_SORT, GNB_MORE_REGION, GNB_MORE_N };
#define GNB_REG_N 8u               /* Region plate values: 0 Any, 1..7 NAE NAW EU LA AS OC NAC (room region index + 1; NAC appended) */
enum { GNB_SORT_PLAYERS, GNB_SORT_NEWEST, GNB_SORT_SERVER };      /* GNB_SORT_SERVER: the server's order, kept (not offered in the popup; the host tests use it) */

/* ---- life cycle and input ---- */
void gnb_enter(struct gnb *g, int live);
void gnb_enter_keep(struct gnb *g, int live);        /* gnb_enter that keeps the fighter locked last (the browser opened again between two games) */
/* one frame: held buttons and stick edges (up/down = the stick crossed the threshold this frame) */
void gnb_frame(struct gnb *g, unsigned held, int stick_up, int stick_down);

/* a friend's room from the Friends list: call right after gnb_enter(g, 1).  room = the 4-digit code, rflags = the friend entry's room flags:
   bit 0 set -> the passcode screen first (the host's friends are not asked, everybody else is); otherwise the join goes out at once without a passcode.  A refusal of a join
   that went without a passcode and that the room may have wanted one for opens the passcode screen (the fallback). */
void gnb_join_friend(struct gnb *g, const char *room, unsigned rflags);

/* ---- room chat ---- */
void gnb_set_texts(struct gnb *g, const char (*t)[QC_TXT], unsigned n);        /* the player's 16 chat texts or the defaults; empty and "." slots stay empty */
void gnb_load_texts(struct gnb *g, const char (*own)[QC_TXT], unsigned n);   /* own (or NULL) with the defaults for empty and "." slots: always 16 texts */
void gnb_net_chat(struct gnb *g, unsigned n, const struct gnb_chat_in *l);     /* a FETCH answer: new lines (oldest first) */
void gnb_net_chat_sent(struct gnb *g);                                         /* a PRESET went through */
void gnb_net_chat_fail(struct gnb *g, unsigned why);                           /* GNB_CW_*: a PRESET failed (a failed FETCH is silent) */
enum { GNB_CW_MUTED, GNB_CW_RATE, GNB_CW_OTHER };
unsigned gnb_chat_line(const struct gnb *g, unsigned i, char *text);           /* strip line i (0 = the oldest shown) as "Name: text"; 1 = flashing, 2 = the mute notice, 0 = none; the strip is empty outside the plain lobby */
unsigned gnb_qc_open(const struct gnb *g);
void gnb_qc_cell(const struct gnb *g, unsigned pos, char *out);                /* cell 0 Up, 1 Left, 2 Right, 3 Down of the open group, the whole text (the window cuts it by width, qc_win.h) */
void gnb_qc_head(const struct gnb *g, char *out);                              /* "Chat: Up" */

/* ---- answers from the glue (LIVE) ---- */
void gnb_net_list(struct gnb *g, unsigned total, unsigned page, unsigned n, const struct gnb_room *e);   /* one page; page 0 replaces the cache */
void gnb_net_fail(struct gnb *g, unsigned what, unsigned why);          /* what = GNB_REQ_*; why = GNB_W_* */
void gnb_net_view(struct gnb *g, const struct gnb_lobby *v);           /* a join / create / state / action reply */
void gnb_net_roster(struct gnb *g, const struct gnb_roster *r);       /* a roster reply (the Y panel) */
unsigned gnb_roster_row(const struct gnb *g, unsigned i, char *name, char *crowns, char *status);
void gnb_roster_streak(const struct gnb *g, unsigned i, char *out);    /* the Streak column: the player's streak wins (0 shown as 0) */
/* the panel's top right line from the server's lead flags (crowns first, then streak wins, then the latest streak win; the server decides): "<name> currently in the lead",
   "A and B are in the lead" for two, "<n> players tied for the lead" for more, "No crowns yet" when nobody leads; names are cut to `maxname` characters (a cut ends with a dot) */
void gnb_lead_text(const struct gnb *g, char *out, unsigned maxname);
/* the same as a two-line block for the panel header (full names, never cut with a dot): l1 (big, the title's size) "Wobble Pilot" / "A and B" / "3 players" / "No crowns yet", l2 (small) "currently in the lead" / "are in the lead" / "tied for the lead" / ""; compact = 1 swaps a two-name l1 for "2 players" + "in the lead" when the names are too wide */
void gnb_lead_lines(const struct gnb *g, char *l1, char *l2, unsigned compact);   /* panel row i (name[20], crowns[8], status[12]); 0 = none, else 1 + 1 viewer + 2 stage */
void gnb_net_left(struct gnb *g);                                       /* GN_LEAVE done */
/* back from a set (the menu reopened the browser after the character screen / match): ask the server for the room (GN_STATE), the lobby or, when we are no longer
   a member, the list */
void gnb_resume(struct gnb *g);
/* a Grandmaster set (gnb_rk.c): start (played = the last game this console started), feed it each set view (the Grandmaster SET_VIEW as struct gnp_gm, the four character / costume bytes of the view, the two names), one frame */
void gnrk_start(struct gnb *g, unsigned played);
void gnrk_view(struct gnb *g, const struct gnp_gm *s, const unsigned char ch[4], const char *me, const char *opp);
void gnrk_tick(struct gnb *g);
/* why a request failed (shown as a short text) */
enum { GNB_W_UNREACH, GNB_W_NOGN, GNB_W_SIGNIN, GNB_W_NOTFOUND, GNB_W_FULL, GNB_W_BUILD, GNB_W_RULES, GNB_W_RATE, GNB_W_INROOM, GNB_W_BAD, GNB_W_SERVER };

/* ---- what the screen shows ---- */
unsigned gnb_rows(const struct gnb *g);                                 /* entries of the list being shown (0 in a state screen) */
unsigned gnb_max_first(const struct gnb *g);                            /* scroll limit: rows - 9, at least 0 */
/* row `slot` (0..8) of the screen: number column and title; returns attribute 0 normal, 1 dim, 2 highlight */
unsigned gnb_row(const struct gnb *g, unsigned slot, char *num, char *title);
/* panel line `line` (0..GNB_PANEL-1) of the selected entry / state; returns attribute 0 normal, 1 dim, 2 highlight, 3 warning */
unsigned gnb_panel(const struct gnb *g, unsigned line, char *out);
unsigned gnb_pass_view(const struct gnb *g, char *title, char *digits);      /* the passcode entry of the Rooms look: title, the four digits, the cursor cell (0..3) */
void gnb_badge(const struct gnb *g, char *out);                         /* the small box at the top right (retail score box): "5 of 8", "Mock", "Loading" */
int gnb_char_box(const struct gnb *g);                                  /* 1 only when the retail "Select Character" box makes sense: in a room lobby while you are called up (never on the list, the forms or a plain queue) */
int gnb_act_enabled(const struct gnb *g, unsigned i);                   /* action-row button i (all of them) */
const char *gnb_act_label(const struct gnb *g, unsigned i);              /* "Join random", "Create", "Join", "Region: EU" (or "Region: Any"), "More" */
const char *gnb_more_label(const struct gnb *g, unsigned row, char *out);   /* the More popup's row: "Type: Any" ("Stages: Any" against an older server), "Hide full rooms: No", "Sort: Most players" (out has 26 bytes; returns out) */
void gnb_status_text(const struct gnb *g, char *out);                   /* the line above the bar: "Showing: Europe, any stages" (out has 56 bytes) */
unsigned gnb_create_smode(const struct gnb *g);                          /* GN_CREATE stage mode byte: 2 All stages, 0xFF the server default (Legal) (a server without room types) */
unsigned gnb_create_type(const struct gnb *g);                           /* GN_CREATE room type byte (view v6): the Type row's choice, GNB_RT_NONE (0xFF) against a server without room types */
unsigned gnb_list_filter(const struct gnb *g);                           /* GN_LIST filter byte: room types served = the type 0..5, 0xFF every room; an older server: 0 Legal, 2 All stages, 0xFF every room */
/* room types: the names ("Wild And Crazy"), the one-line descriptions of the Create form (<= 44 characters, the server's ms_roomtypes.desc), the list column's short names ("Crazy") */
const char *gnb_type_name(unsigned t);
const char *gnb_type_desc(unsigned t);
const char *gnb_type_short(unsigned t);
int gnb_type_open(const struct gnb *g, unsigned t);                       /* 0 while the server's switches keep a type closed: Wild (All stages) and Wild And Crazy (+ the items switch) */
unsigned gnb_room_type(const struct gnb *g);                              /* the type of the room of the lobby (the view's, else the list entry's by code, else Chill) */
void gnb_create_desc(const struct gnb *g, char *out);                    /* the description line of the Create form ("" when the server serves no room types) */
/* the pick grid's rules (view v6 trailer): a cell is selectable unless the room excludes it; a forced character (Ditto Dojo) replaces the grid by a locked view */
int gnb_cell_ok(const struct gnb *g, unsigned cell);                      /* 0 for an excluded fighter in the room of the lobby (Random is always fine) */
unsigned gnb_forced_char(const struct gnb *g);                            /* the forced character of the room's round (GNB_RT_NONE 0xFF: none yet / not a Ditto room) */
/* list columns of row `slot` (original Rooms list): code (last 4 characters), host, region ("-": the list has none yet), mode, "n of 8"; returns 0 if the slot is empty or the list is in a state screen */
unsigned gnb_cols(const struct gnb *g, unsigned slot, char *code, char *host, char *region, char *mode, char *cnt);
/* layout kind of the screen: 0 = list with rooms (columns), 1 = list state screen (message rows), 2 = a form (Create Room, passcode of a room to create), 3 = room screen */
unsigned gnb_layout(const struct gnb *g);
void gnb_code4(const char *code, char *out);                            /* the room code as shown: its last four characters */
unsigned gnb_lobby_cap(const struct gnb *g);                            /* capacity of the room of the lobby (list entry by code, 8 when unknown) */
/* the fighter of the player on side 0 / 1 (the portrait in the slot): Melee's external character id 0..25 from the view, GNB_CHAR_NONE when the slot is empty or the
   character is unknown (a member that never picked joins as Random, 26) */
#define GNB_CHAR_RANDOM 26u
#define GNB_CHAR_NONE 0xFFu
unsigned gnb_side_char(const struct gnb *g, unsigned side);
/* the pick grid: the cell's row / column (cell = external id, 26 Random), its name ("Captain Falcon" ... "Random"); the pick turn's clock "0:23" ("" when no turn runs) */
void gnb_pick_cell(unsigned cell, unsigned *row, unsigned *col);
const char *gnb_char_name(unsigned id);
const char *gnb_cell_label(unsigned id);                               /* the label of a pick-grid cell that has no retail icon: the name in capitals ("SHEIK"), as the retail icons' patched names are */
void gnb_pick_clock(const struct gnb *g, char *out);
unsigned gnb_costumes(unsigned id);                                    /* costumes of external character id 0..25 in the retail game (4 to 6); Random 0 */
unsigned gnb_pick_left(const struct gnb *g);                           /* seconds left of the pick turn now (0xFF: none) */
const char *gnb_lobby_mode(const struct gnb *g);                        /* "Singles" */
const char *gnb_lobby_rule(const struct gnb *g);
unsigned gnb_upload_room(unsigned rflags, unsigned stadium);        /* 1 = the joined room is spectatable (rflags bit 1): the match-time uploader may run, whatever its Stadium policy (normal, Frozen or removed) */
void gnb_tn_set(struct gnb *g, const char *name, const char *round, const char *opp, unsigned bo, unsigned secs);   /* the room just joined is a tournament match: name, round title ("Losers Round 1"), the opponent, games of the set, seconds the call gave (0 none) */
void gnb_tn_clock(const struct gnb *g, char *out);                         /* "Join in 2:41" while the opponent is not in the room yet and the call's time has not run out, else "" */
void gnb_open_text(const struct gnb *g, char *out);                    /* the lobby header's left line under the room code: "Open for 12 min", "Open for 1 h 12 min", "Open for 30 h", "Open for 99+ h", "Just opened" (0 min); at most 20 characters */
void gnb_lobby_rule_parts(const struct gnb *g, char *a, char *b);        /* the lobby header's top right: "Legal stages" or "All stages" (big, the second part is empty); the mode (Singles) is no longer shown there */
const char *gnb_foot(const struct gnb *g);                              /* the lobby footer: "Start: Join queue  Hold B: Leave" (state dependent) */
void gnb_head(const struct gnb *g, char *out);                          /* the heading that replaces the retail "High Score" label: "Rooms", "New room", the room name in a lobby */
const char *gnb_title(const struct gnb *g);                             /* "Game Night", "Create room", "Join room", "Room lobby" */
const char *gnb_hint(const struct gnb *g);                              /* the key line */
/* what the glue sends: the room being joined (code, the passcode typed or ""), the Create Room form (name, flags 1 listed / 2 spectators, stadium policy 0xFF = the
   server's default for the choice, passcode or "") */
const char *gnb_join_code(const struct gnb *g);
const char *gnb_join_pin(const struct gnb *g);
unsigned gnb_create_cap(const struct gnb *g);                            /* Create Room > Players: 2, 4, 8, 16 or 32 */
void gnb_create_args(const struct gnb *g, const char **name, unsigned *flags, unsigned *policy, const char **pin);

/* the same screen in the character-cell view of the sign overlay (52 x 26), for the host golden tests and as the console fallback */
struct sg_view;
void gnb_text(const struct gnb *g, struct sg_view *v, int mock_tag);
#endif
