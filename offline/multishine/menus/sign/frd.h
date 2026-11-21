/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Console Friends screen: the MODEL.  Pure C89, no libc, no heap, no
 * Melee address, no network.  It owns the friend list cache (the server sends it sorted: online first, then last seen; 8 per page), the incoming requests, the
 * cursor in the retail Event Match names (`first` = scroll position, `page` = cursor row 0..8), the screens (list, the action list of a friend, the 16 message
 * presets, the connect-code entry, waiting for a challenge's answer, busy), the keys and every text.  Two front ends draw it: frd_ev.c (console, the retail Event
 * Match screen) and the character-cell text view (frd_text: host golden tests, console fallback).  The network is the glue's (sg_entry.c with client_net/socc.c):
 * the model raises frd.req and the glue answers through frd_net_*.
 * Rows of the list (no action page, the list itself acts): requests first (A accepts, X declines), then ONE row per friend (a friend's Slippi code and name are on the same entry and shown in the
 * panel), then the recent players.  On a friend: A = the obvious action (Join room, Watch, Mano a mano; nothing when offline), Y = Slippi challenge (with a Slippi code), X = Remove or block (the panel asks:
 * A Remove, Y Block, B Cancel); L R page; Z adds a friend.  Recent players keep a small action list (Add friend, Remove from recent, Block, Back).  Left: a status dot (frd_dot) and a short state word.
 * The panel has a title bar that always holds a title (frd_req_badge) and at most three wrapped body lines.  Text: the menu font through the glue's SJIS expansion; names from the server are reduced to printable ASCII.
 * Codes: Multishine codes are TAG@n, Slippi codes TAG#n.  Every row shows the FULL code (dimmed, at the right on the console); a long display name is cut first (frd_fit).
 */
#ifndef FRD_H
#define FRD_H

#include "frd_hold.h"
#include "qc_pick.h"            /* the D-Pad quick-chat picker */             /* FRD_HOLD_TICKS: the one deliberate-hold length (1.0 s) */

#define FRD_VIS 9u                 /* rows of the retail Event list */
#define FRD_MAX 48u                /* friends cached: 6 server pages of 8 */
#define FRD_RQ 16u                 /* incoming requests cached (one server page) */
#define FRD_IN 6u                  /* invites cached (one server page: FRIEND_LIST kind 5; Mano a mano and Game Night room invites that may still be accepted) */
#define FRD_INV_FRAMES 300u        /* the Invites are asked again every 10 s at 30 frames/s while the list is open */
#define FRD_RC 20u                 /* recent players cached (the server keeps at most 20) */
#define FRD_NTXT 16u               /* message presets */
#define FRD_TXT 26u
#define FRD_PANEL 4u
#define FRD_MSG_LINE 2u            /* the status line sits on the third panel line (the box holds three; the fourth stays empty so the key line always shows) */
/* Body of the description box (Friends list), row of each line's top in table rows below the first row (frd_ev.c panel_draw), by used lines m = 1..4; font 0.030 for m <= 3, 0.024 for 4.
   The box body spans rows 10.30 .. 12.55; one line is centred, two and three are spread with the pitch of the status messages (0.74 .. 0.84 rows, the font is 0.60 rows high) so nothing touches. */
#define FRD_BODY_ROWS { { 11.14f, 0.0f, 0.0f, 0.0f }, { 10.78f, 11.62f, 0.0f, 0.0f }, { 10.40f, 11.14f, 11.88f, 0.0f }, { 10.30f, 10.90f, 11.50f, 12.05f } }
#define FRD_LINE 44u               /* characters of a panel line */
#define FRD_TITLE 24u              /* characters of a row title ("<name>  <code>") */
#define FRD_NUM 9u                 /* status word column ("Searching", "In a game") */
#define FRD_CODE 8u                /* connect code characters */
#define FRD_AUTO_FRAMES 900u       /* the list refreshes itself every 30 s at 30 frames/s while it is open */
#define FRD_POLL_FRAMES 60u        /* a challenge's answer is asked every 2 s */
#define FRD_TOAST_FRAMES 240u      /* a result toast stays 4 s at 60 frames/s */
#define FRD_BUSY_FRAMES 360u       /* a request the glue has not answered after 12 s is dropped */
#define FRD_WAIT_FRAMES 840u       /* ... for 28 s at most: the recipient's fetch (up to 6 s) + the 20 s accept window + the grace (20 s for every challenge; the server hold is 28 s) */
#define FRD_REP_FIRST 12u
#define FRD_REP_NEXT 3u

/* buttons (HSD_PadStatus.button bits, as sg_ui.h SG_B_*) */
#define FRD_B_LEFT 0x0001u
#define FRD_B_RIGHT 0x0002u
#define FRD_B_DOWN 0x0004u
#define FRD_B_UP 0x0008u
#define FRD_B_Z 0x0010u
#define FRD_B_R 0x0020u
#define FRD_B_L 0x0040u
#define FRD_B_A 0x0100u
#define FRD_B_B 0x0200u
#define FRD_B_X 0x0400u
#define FRD_B_Y 0x0800u
#define FRD_B_START 0x1000u

enum { FRD_LIST, FRD_ACT, FRD_CODE_ENTRY, FRD_WAIT, FRD_BUSY };
enum { FRD_N_LOADING, FRD_N_OK, FRD_N_UNREACH, FRD_N_NOSOC, FRD_N_SIGNIN };
enum { FRD_REQ_NONE, FRD_REQ_LIST, FRD_REQ_ADD, FRD_REQ_ANSWER, FRD_REQ_REMOVE, FRD_REQ_BLOCK, FRD_REQ_INVITE, FRD_REQ_STATUS, FRD_REQ_MSG, FRD_REQ_RMREC, FRD_REQ_INVITES, FRD_REQ_INVRESP };
enum { FRD_W_UNREACH, FRD_W_NOSOC, FRD_W_SIGNIN, FRD_W_NOTFOUND, FRD_W_RATE, FRD_W_FULL, FRD_W_GONE, FRD_W_NOMSG, FRD_W_BAD, FRD_W_PENDING, FRD_W_TIMEOUT };
enum { FRD_SFX_NONE = 0, FRD_SFX_BACK = 1, FRD_SFX_FWD = 2, FRD_SFX_MOVE = 3, FRD_SFX_DENY = 4 };
enum { FRD_A_INVITE, FRD_A_MSG, FRD_A_REMOVE, FRD_A_BLOCK, FRD_A_BACK, FRD_A_N,
       FRD_A_WATCH = FRD_A_N, FRD_A_SLIPPI, FRD_A_JOIN, FRD_A_ALL };      /*: a friend with a Slippi code also offers "Challenge on Slippi", a friend in a Game Night room "Join room" */
/* (the logical actions above; the rows shown are the subset that applies to the friend, in the order Invite, Watch, Challenge on Slippi, Join room, Message, Remove, Block, Back: a_rows / a_at in frd.c) */      /* FRD_A_N rows always; Watch is a sixth one, shown second (after Invite) only while the friend's match is watchable (frd_watchable): the list maps its rows through a_at() */
enum { FRD_AR_ADD, FRD_AR_REMOVE, FRD_AR_BLOCK, FRD_AR_BACK, FRD_AR_N };       /* the action list of a recent player: Add Friend, Remove From Recent, Block, Back (no invite, no message, no presence: those are for friends) */
#define FRD_D_ALL 7u

struct frd_ent {
    char code[9], name[13];
    unsigned char presence, flags, seen;   /* presence 0 offline, 1 online, 2 menus, 3 searching, 4 in a match; flags bit 0 bot; seen: days since (255 unknown / hidden) */
    char scode[9], sname[16];              /* player-typed Slippi code and name (not verified) */
    char room[5];                          /* flags bit 2: the friend is in a Game Night room THIS console may join: its 4-digit code (the server reveals it to no one else) */
    unsigned char rflags;                  /* ... bit 0 the passcode is asked of this console, bit 1 the room has a passcode (the host's friends are not asked) */
    unsigned char gmode, gflags;           /* flags bit 5: the friend is in a Dojo / Grandmaster / Mano a mano match: mode (ms_wire.MODES: 0 Grandmaster, 1 Dojo, 2 Mano a mano), gflags bit 0 = watchable by this console */
};
struct frd_rq { char code[9], name[13]; };
struct frd_rc { char code[9], name[13]; };
struct frd_in { unsigned char type; unsigned long id; char code[9], name[13]; unsigned short age, left; };      /* an invite of the Invites rows: type 1 Mano a mano (code = their connect code), 2 Game Night room (code = the room); id answers INVITE_RESPOND; age / left in seconds when it was fetched */     /* a recent player: no presence, no flags (the server never sends one) */

struct frd {
    unsigned char live;            /* 1 = the server (Main Menu row Friends), 0 = example data (host tests, no server) */
    unsigned char scr, net;        /* FRD_LIST ...; FRD_N_* of the list */
    unsigned char nf, total, nrq;  /* friends cached, friends the server has, requests cached */
    unsigned char nin, inv_chk;    /* invites cached (rows after the requests); inv_chk: 1 = an accept of a Mano a mano invite waits for the glue's data check (frd_inv_checked) */
    unsigned char arg_type;        /* FRD_REQ_INVRESP: the invite's type */
    unsigned short itimer;         /* frames since the Invites were last asked (FRD_INV_FRAMES) */
    unsigned long arg_id;          /* FRD_REQ_INVRESP: the invite id */
    unsigned char nrc, a_rec;      /* recent players cached (rows below the friends and the "Recent players" separator); 1: the action list is about a recent player (sel indexes rc[]) */
    unsigned char first, page;     /* list: first shown row, cursor row 0..8 */
    unsigned char sel;             /* the friend the action list / message list / wait is about (index into f[]) */
    unsigned char a_cur, a_confirm;    /* action list cursor (recent players only); 1 = Block asked once */
    unsigned char rm;                  /* 1: the panel asks Remove or Block for the friend under the cursor (A Remove, Y Block, B Cancel) */
    unsigned char m_first, m_page;     /* message list scroll and cursor */
    char cbuf[FRD_CODE + 1];       /* connect code entry: the characters typed, cpos = the character under the cursor */
    unsigned char cpos;
    unsigned char req;             /* FRD_REQ_* for the glue, cleared by the glue when taken */
    unsigned char abort;           /* 1: the screen dropped the running request (B or the 12 s limit); the glue stops it and clears this */
    unsigned char lpage;           /* FRD_REQ_LIST: next friend page offset to fetch (the glue fetches the requests first at offset 0) */
    char inv_name[13];             /* the friend the open Mano a mano invite went to (set when the server accepted it): the 'Already invited <name>' line */
    char arg_code[9];              /* the request's argument: a code ... */
    unsigned char arg_yes;         /* ... accept / decline ... */
    unsigned char arg_txt;         /* ... the preset index of a message */
    unsigned long inv_id;          /* the challenge being waited for */
    unsigned short timer, wait;
    unsigned char busy_what, back_to;  /* FRD_BUSY: the request running; the screen to return to */
    unsigned char exit;            /* 1: the player left (B on the list) */
    unsigned char join;            /* 1: Y on a Room row: the list closes and the Game Night browser joins join_code (the glue) */
    unsigned char join_rf;         /* the room flags of that friend entry (bit 0 the passcode screen comes first) */
    char join_code[5];
    unsigned char kbd;             /* the glue can hand off to Slippi's Direct code keyboard (console with the hub block): Z Add a friend goes there (play 5) */
    unsigned char play_after;      /* 1, 3 or 4 (4: Watch; play_code = the friend's code): a hand-off (accepted challenge / Slippi friend) waits for the Event screen to close first: the screen is left properly (exit), play is raised after (frd_handoff) */
    unsigned char wchk;            /* 1: Watch was pressed and the fresh friends page decides (frd_net_friends): watchable = the hand-off, otherwise "No game to watch right now" and the list stays */
    unsigned char play;            /* 4: Watch was chosen on a friend (the glue arms the watch hand-off and leaves); 1: a challenge was accepted: the glue arms the Direct search of play_code and leaves for the character screen */
    char play_code[9];
    unsigned char dirty, sfx;
    unsigned char rep, rep_dir;
    unsigned short prev;
    struct frd_hold hold;          /* hold Z on a request row accepts it (a tap does nothing; X declines with a tap) */
    unsigned long tb, hold_lim;    /* the glue sets them before each frd_frame: the time base now, and the length of the hold in ticks (0 = FRD_HOLD_TICKS) */
    const char *msg;               /* the status line, 0 = none */
    char info[FRD_LINE + 1];       /* a status line built at run time (the Slippi row's "Play On Slippi: Direct, Code TAG#n") */
    unsigned char msg_warn;        /* 1: the status line is a warning */
    char toast[FRD_LINE + 1];      /* the Add-a-friend result as a TOAST (not a line in the description box): the text, drawn by the console as a plate at the top right; "" = none */
    unsigned char toast_warn;      /* 1: a refusal (the deny sound) */
    unsigned short toast_t;        /* frames left; any cursor move clears it */
    unsigned char jump_name;       /* the jump also renames the toast (already on the list, the list was not loaded when the answer came) */
    char jump_code[9];             /* after an add: the cursor goes to the player this code belongs to once the list holds them */
    unsigned char gfx, gfx_tried;  /* the console glue's Event screen front end owns the screen / was tried */
    unsigned char ntxt;            /* message presets known (0: "no messages") */
    char txt[FRD_NTXT][FRD_TXT];   /* the 16 quick-chat texts IN PLACE (slot = group * 4 + position, Up Left Right Down); an empty slot is an empty string */
    /* quick chat on the list (qc_pick.h): the glue sets qc_on when the console can send (a linked console, the texts are loaded); io_busy while another request of the glue runs */
    struct qc qc;
    unsigned char qc_on, io_busy, msg_busy;
    char qc_code[9];               /* the friend the open picker is about */
    struct frd_ent f[FRD_MAX];
    struct frd_rq r[FRD_RQ];
    struct frd_rc rc[FRD_RC];
    struct frd_in in[FRD_IN];
    unsigned short sent, answered; /* counters (RAM proof) */
};

void frd_enter(struct frd *f, int live);
void frd_say(struct frd *f, const char *m, int warn);
void frd_add_code(struct frd *f, const char *code);                   /* a code typed on Slippi's keyboard: send the friend request (sent exactly as typed: TAG@NUM a Multishine code, TAG#NUM a Slippi code) */
void frd_handoff(struct frd *f);                                           /* the Event screen has closed: a waiting hand-off (play_after) becomes play (the glue starts the search and the character screen from the Main Menu) */                      /* the glue's status line (a static string) */
void frd_frame(struct frd *f, unsigned held, int stick_up, int stick_down);
/* the 16 message presets (the player's own chat texts or the defaults); empty strings are skipped */
void frd_set_texts(struct frd *f, const char (*t)[FRD_TXT], unsigned n);

/* ---- answers from the glue ---- */
void frd_net_requests(struct frd *f, unsigned total, unsigned n, const struct frd_rq *e);
void frd_net_friends(struct frd *f, unsigned total, unsigned offset, unsigned n, const struct frd_ent *e);   /* offset 0 replaces the cache */
void frd_net_invites(struct frd *f, unsigned n, const struct frd_in *e);      /* FRIEND_LIST kind 5 answered (n 0: none, or a server without it); replaces the rows */
void frd_net_invresp(struct frd *f, const char *peer);                       /* INVITE_RESPOND answered: peer = the sender's code (Mano a mano) or the room code */
void frd_inv_checked(struct frd *f, int missing);                            /* the glue's answer to inv_chk: missing 1 = no Multishine system files (nothing is accepted), else the accept goes to the server */
void frd_net_recent(struct frd *f, unsigned total, unsigned offset, unsigned n, const struct frd_rc *e);   /* FRIEND_LIST kind 4 answered (the glue asks it after the last friends page, only when the server announces the capability); offset 0 replaces the cache */
void frd_net_done(struct frd *f, unsigned what, unsigned result);           /* a successful op (what = FRD_REQ_*); result: FRIEND_ADD's byte, INVITE_STATUS's state */
void frd_net_invite(struct frd *f, unsigned long id);                       /* INVITE_SEND answered: wait for the answer */
void frd_net_status(struct frd *f, unsigned state, const char *peer);      /* INVITE_STATUS: 0 pending, 1 accepted (peer = the code to search), 2 not answered */
void frd_net_fail(struct frd *f, unsigned what, unsigned why);             /* FRD_W_* */
unsigned frd_qc_open(const struct frd *f);                                 /* 1 while the quick-chat picker is shown */
void frd_qc_cell(const struct frd *f, unsigned pos, char *out);            /* the picker's text in cell pos (0 Up, 1 Left, 2 Right, 3 Down), cut to 15 characters; "" when closed */
void frd_qc_head(const struct frd *f, char *out);                          /* the picker's title: "Chat: Up" */

/* ---- what the screen shows ---- */
unsigned frd_rows(const struct frd *f);                                    /* rows of the current list (list, actions, messages) */
unsigned frd_max_first(const struct frd *f);
unsigned frd_row(const struct frd *f, unsigned slot, char *num, char *title);   /* attribute 0 normal, 1 dim, 2 highlight (online), 3 warning */
unsigned frd_status_view(const struct frd *f);                              /* the list shows a status message (loading, offline, not linked, not on this server): its line count 1..3, else 0; the console draws it centred in the box (frd_ev.c panel_draw) */
unsigned frd_toast(const struct frd *f, char *out);                         /* the Add-a-friend result toast text (returns 1 + warn flag when one is showing, else 0 and "") */
unsigned frd_panel(const struct frd *f, unsigned line, char *out);         /* attribute as gnb_panel */
const char *frd_title(const struct frd *f);
const char *frd_hint(const struct frd *f);
unsigned frd_hold_pct(const struct frd *f);                                 /* 0..100: how far the hold on a request row has come */
void frd_badge(const struct frd *f, char *out);                            /* "1 Req  3 Online" (text view header) */
void frd_req_badge(const struct frd *f, char *out);                        /* the Event view's panel line: "1 Request" / "2 Requests", empty when none (the Online count is the box) */
unsigned frd_watchable(const struct frd_ent *e);                           /* 1: the friend is in a Dojo / Grandmaster / Mano a mano match that this console may watch (server game tail) */
const char *frd_game_name(unsigned mode);                                  /* "Grandmaster", "Dojo", "Mano a mano" ("" for another mode) */
unsigned frd_online(const struct frd *f);                                  /* friends online: any presence (menus, searching, match) or in a Game Night room; not Offline, not Requests, not S rows */
unsigned frd_box(const struct frd *f, unsigned line, char *out);           /* the Event view's small box (2 centred lines): "7" / "online"; empty lines until the list is loaded; returns 1 when the line is the big number */
/* one list row split for the console: the status word is frd_dot_label of frd_dot, `name` and `code` are drawn apart (the code dimmed, at the right); returns frd_row's attribute */
unsigned frd_row_parts(const struct frd *f, unsigned slot, char *num, char *name, char *code);
unsigned frd_mark(const struct frd *f, unsigned slot);                      /* row slot marker right of the name: 0 none, 1 empty circle (a friend), 2 red filled circle (unread messages) */
unsigned frd_cur_first(const struct frd *f);                               /* scroll position of the list on screen */
unsigned frd_cur_page(const struct frd *f);
/* which row of the list the cursor is on: 1 a request, 6 an invite, 2 a friend, 3 the Slippi row of a friend (right below its row), 4 the separator "Recent players" (never rests the cursor), 5 a recent player (below the separator, after the friends) (the list has no Add
   row any more: Z adds a friend); *idx = the index in r[] / f[] */
unsigned frd_row_kind(const struct frd *f, unsigned row, unsigned *idx);

/* the status dot in the left column of row slot `slot`: FRD_DOT_* (frd.c dot_of: green menus, yellow busy, red searching or in a match, grey offline, white request) */
enum { FRD_DOT_NONE, FRD_DOT_GREEN, FRD_DOT_YELLOW, FRD_DOT_RED, FRD_DOT_GREY, FRD_DOT_REQ, FRD_DOT_ROOM, FRD_DOT_SLIPPI, FRD_DOT_SEARCH, FRD_DOT_INV };   /* ROOM: a friend in a Game Night room you may join (blue); SLIPPI: the friend's Slippi row, no presence (dim word "Slippi") */
unsigned frd_dot(const struct frd *f, unsigned slot);
const char *frd_dot_label(unsigned dot);                                   /* "Ready", "Busy", "Match", "Offline", "Request", "Room", "Slippi": the Event view's status word */
const char *frd_dot_word(unsigned dot);                                    /* "Menu", "Busy", "Match", "Off", "Req" (text view, legend) */
unsigned frd_legend(const struct frd *f);                                  /* 1: panel line 3 is the dot legend (the console draws its words in their colours) */

/* "<name>  <code>" in at most `width` characters: the code is never cut; a name that does not fit is cut and ends with a dot ("Wobble Pi.  WOBB@123") */
void frd_fit(char *out, const char *name, const char *code, unsigned width);

struct sg_view;
void frd_text(const struct frd *f, struct sg_view *v);
#endif
