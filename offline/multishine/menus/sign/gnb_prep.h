/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Stage prep screen (pieces E1 and F): the MODEL of the screen that sits between two games of a set.  One screen serves
 *   - Sweaty (a Game Night room, view v6): game 1 strike (one side strikes 1, the other 2, the first 1 more: the stage left is played), games 2 and 3 the previous
 *     loser picks the stage; the server drives the order, the clock and the timeouts (a timed-out step is completed by the SERVER, never by the console),
 *   - Grandmaster (our ranked path, RK_SET_STATE): the same plus the winner's ban and Dave's rule (a stage state "not allowed").
 * Pure C89, no libc, no heap, no Melee address, no network.  It owns the cursor, the busy guard (one action per step), the clock between two views and every text the
 * screen shows (menu font safe characters, sentence case).  Two front ends draw it: gnb_prep_ev.h (console only, included by gnb_ev.c: the retail Event look, the roster
 * panel's look) and the text view in gnb_prep.c (host golden tests).  The player whose step it is moves the cursor and presses A; the other side sees the same screen update
 * with each view of the room (a poll a second while the screen is up; the answer to an action is the new view).
 * Inputs: gnp_from_room (the view v6 trailer of a Game Night room) or gnp_from_gm (the Grandmaster SET_VIEW), both fill the same struct gnp.
 * Output: gnp.act / gnp.val = a GN_ACTION (room) or RK_SET_ACTION (ranked) request: the action numbers are the same for both (5 strike, 6 pick the stage, 7 ban), the value is the
 * index into the stage list.
 */
#ifndef GNB_PREP_H
#define GNB_PREP_H

#define GNP_MAX 6u                 /* stages in a list (five in game 1, six with Pokemon Stadium after it) */
#define GNP_NAME 13u
#define GNP_CODE 12u               /* a connect code as shown (TAG#123, up to 11 characters) */
#define GNP_FPS 30u                /* the overlay's frame rate (the clock) */
#define GNP_WARN_SECS 10u          /* the clock turns yellow for the player to act at this many seconds left, red at GNP_PANIC_SECS (Slippi's WARN_SECONDS / PANIC_SECONDS) */
#define GNP_PANIC_SECS 3u

/* what the screen is showing */
enum { GNP_NONE, GNP_STRIKE, GNP_BAN, GNP_PICK, GNP_WAIT, GNP_PLAY, GNP_CHAR };    /* GNP_CHAR: a later game's character step (Slippi's GameSetup does it in the scene): the fighter picked last game with OK / Change; Change opens the lobby's grid */
/* one stage row */
enum { GNP_S_FREE, GNP_S_STRUCK, GNP_S_BANNED, GNP_S_DAVE, GNP_S_OUT, GNP_S_CHOSEN };
/* wire action numbers (GN_ACTION and RK_SET_ACTION agree) */
#define GNP_A_STRIKE 5u
#define GNP_A_PICK 6u
#define GNP_A_BAN 7u
#define GNP_A_CHAR 4u                /* GN_ACTION / RK_SET_ACTION: a character (value = the fighter, the costume with it, gnb.c) */
/* room view v6 step codes (ms_rooms ST_*) */
#define GNP_RV_STRIKE 2u
#define GNP_RV_BAN 3u
#define GNP_RV_PICK_STAGE 4u
#define GNP_RV_PICK_CHAR 5u
#define GNP_TURN_WAIT 0xFDu
#define GNP_TURN_GO 0xFEu

/* the view v6 trailer as the console parsed it (gnc_view fields) */
struct gnp_t6 {
    unsigned char have, rtype, best_of, game, sc[2], step, owner, secs, avail, n, st[8], forced, struck;
};
/* the Grandmaster SET_VIEW (ms_ranked.SET_VIEW), fields as the wire carries them */
struct gnp_gm {
    unsigned char have, phase, my_slot, best_of, game, score[2], p0, prev_winner, step, owner, remaining, secs, avail, n, id[8], state[8], stage, result, tier[2];   /* tier: the server tier byte of slot 1 / 2 (0xFF placement / unknown) */
};

struct gnp {
    unsigned char open;            /* the screen is up (the lobby / ranked flow draws it instead of its own centre) */
    unsigned char kind;            /* 0 a Game Night room, 1 Grandmaster */
    unsigned char step;            /* GNP_* */
    unsigned char mine;            /* it is this console's step */
    unsigned char best_of, game, sc_me, sc_opp;
    unsigned char n, id[GNP_MAX], state[GNP_MAX];
    unsigned char cur;             /* the cursor row */
    unsigned char chosen;          /* row of the game's stage once it is fixed, 0xFF before */
    unsigned char struck_n;        /* stages struck so far (game 1 of a room: "Strike 2 of 4") */
    unsigned char remaining;       /* strikes this step still asks of its owner (0 = unknown) */
    unsigned char secs;            /* seconds left in the last view, 0xFF none */
    unsigned short frames;         /* frames since that view: the clock counts down between two views */
    unsigned char busy;            /* an action was sent for this step: the next view clears it */
    unsigned char dirty;           /* 1: redraw (any change of what the screen shows) */
    unsigned char sfx;             /* sound for the front end: 1 back, 2 forward, 3 move, 4 refusal (the gnb.h GNB_SFX_* values); the front end clears it */
    unsigned char act, val;        /* the request for the glue (0 = none): GNP_A_*, index into the list; the glue clears act when it takes it */
    unsigned char prev;            /* last view's step code (for the sounds) */
    unsigned char ch[2];           /* the fighters of this console's side and the opponent's (Melee's external ids, 0xFF unknown / not picked yet): the timeline bar's two icons */
    unsigned char tier[2];         /* rank tier 0..6 of the two players for the header's icon, 0xFF none (Sweaty has no rank; Grandmaster fills it) */
    unsigned char srv[GNP_MAX];    /* the row's index in the server's list (the rows are shown in Slippi's order: gnp_rank), the value an action carries */
    unsigned char sel[GNP_MAX];    /* rows the player has marked this step (A marks, the X shows) but not yet confirmed; OK / Redo appear when nsel == need */
    unsigned char nsel, need;      /* marks made / marks the step asks for (two for the second player's first strike in game 1) */
    unsigned char confirm;         /* the OK / Redo buttons are up instead of the prompt */
    unsigned char btn;             /* 0 OK, 1 Redo */
    unsigned char pend;            /* the second row of a two-strike confirm, sent when the view for the first comes back (0xFF none) */
    unsigned char me_first;        /* this console's player is the first of the pair on the bar (game 1: the first striker; later: the previous winner) */
    unsigned char res[9];          /* result of each game as this console saw the set go: 0 not known / not played, 1 this console won, 2 lost (the pips under the clock) */
    unsigned char grid;            /* a character step: Change was chosen, the glue opens the lobby's grid (gnb.c prep_out clears it) */
    unsigned char prevc, cos;      /* the fighter and costume this console locked last (gnb.c prep_chars): what OK sends on a character step, 0xFF none */
    unsigned char sc_seen;         /* sc_me + sc_opp when res was last updated */
    unsigned char prev_win;        /* a Grandmaster view's previous winner is this console (1), the opponent (2), none (0) */
    unsigned char order[GNP_MAX];  /* the order the console saw the stages struck / banned in: 0 = not (yet), else 1, 2, 3 ...; the bottom timeline shows the strikes in it */
    unsigned char nord;            /* how many of order[] are set */
    char me[GNP_NAME], opp[GNP_NAME];
    char code[2][GNP_CODE];        /* the connect codes under the names (this console's player first), empty = none known */
};

void gnp_clear(struct gnp *p);
/* a room view: side = my side (0 / 1, 0xFF none), phase / flags / turn / stage as the view carries them, names of the two sides; sets p->open */
void gnp_from_room(struct gnp *p, const struct gnp_t6 *t, unsigned side, unsigned phase, unsigned called, unsigned turn, unsigned stage, const char *name0, const char *name1);
/* a Grandmaster SET_VIEW; sets p->open while the selection runs (phase 0) or the game is decided (phase 1) */
void gnp_from_gm(struct gnp *p, const struct gnp_gm *s, const char *me, const char *opp);
/* one frame: edge = buttons pressed this frame (gnb.h GNB_B_*), up / down = stick or D-Pad edge.  Moves the cursor, sends nothing itself (sets act / val on A) */
void gnp_frame(struct gnp *p, unsigned edge, int up, int down);
/* texts (menu font safe characters, sentence case) */
const char *gnp_stage_name(unsigned id);
void gnp_title(const struct gnp *p, char *out);                      /* "Strike a stage", "Your opponent strikes", "Pick a stage", ... (at most 30 characters) */
void gnp_sub(const struct gnp *p, char *out);                        /* the line under it: "Strike 2 of 4", "Both players must be in the room" ... */
void gnp_score(const struct gnp *p, char *out);                      /* "Game 2 of 3   You 1  Opponent 0" */
void gnp_clock(const struct gnp *p, char *out);                      /* "0:23", "" when no clock runs */
unsigned gnp_secs(const struct gnp *p);
unsigned gnp_clock_state(const struct gnp *p);                        /* 0 normal, 1 warn (yellow), 2 panic (red): only for the player whose step it is */
unsigned gnp_rank(unsigned n, unsigned id);                          /* the place of a stage id in Slippi's stage row (strike row of five, counterpick row of six) */                              /* seconds left now (0xFF none) */
/* row i: name into name, state text into note; returns the attribute 0 normal (selectable), 1 dim (struck, banned, not allowed), 2 highlight (the game's stage) */
unsigned gnp_row(const struct gnp *p, unsigned i, char *name, char *note);
const char *gnp_foot(const struct gnp *p);                           /* the badge footer: "A Strike  Hold B Leave" */
/* ---- the GameSetup look: the big line, the bottom timeline ---- */
enum { GNP_L_YOUR, GNP_L_OPP, GNP_L_STRIKE1, GNP_L_STRIKE23, GNP_L_STRIKE4, GNP_L_BAN, GNP_L_STAGE };
#define GNP_TL_MAX 5u
struct gnp_tl {
    unsigned char label;           /* GNP_L_* */
    unsigned char n;               /* pictures in the entry: 2 for "STRIKE 2/3", else 1 */
    unsigned char id[2];           /* the fighter (YOUR / OPP) or the stage ids the entry shows, 0xFF = not known yet: the "?" box */
    unsigned char state;           /* 0 later, 1 this is the step now, 2 done */
};
/* the timeline of this screen's game: game 1 (strike): YOUR CHAR, OPP CHAR, STRIKE 1, STRIKE 2/3, STRIKE 4; later games: [BAN (Grandmaster)], STAGE, YOUR CHAR, OPP CHAR (the fighters "?" until the stage is fixed).  Returns the count */
unsigned gnp_timeline(const struct gnp *p, struct gnp_tl *tl);
const char *gnp_label(unsigned label);                               /* "YOUR CHAR", "OPP CHAR", "STRIKE 1", "STRIKE 2/3", "STRIKE 4", "BAN", "STAGE" */
void gnp_msg(const struct gnp *p, char *out);                        /* the line in the middle of the panel, Slippi's own words: "Waiting for opponent to strike", "Select a stage you do not want to play" ... (at most 43 characters) */
const char *gnp_btn_label(const struct gnp *p, unsigned i);          /* "OK" / "Redo" (a character step: "OK" / "Change") */
unsigned gnp_pip(const struct gnp *p, unsigned i);                  /* the pips of the set: 0 later, 1 won, 2 lost, 3 the game now, for i < best_of */
void gnp_sub_name(const struct gnp *p, unsigned who, char *out);     /* the small line under a player's name (0 this console's player, 1 the opponent): the connect code, "" when unknown */
void gnp_codes(struct gnp *p, const char *me, const char *opp);      /* sets the two connect codes ('@' of a TAG@n code shown as '#'); redraws on a change */
/* the text view for the host golden tests (one line per piece) */
unsigned gnp_text(const struct gnp *p, char (*line)[56], unsigned max);
#endif
