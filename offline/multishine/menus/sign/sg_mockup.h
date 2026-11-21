/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Mockups hub: the screens we have designed but not built, drawn with built-in EXAMPLE data in the sign overlay's character-cell view
 * (sg_ui.h), plus links to the existing mock and test screens of the sign overlay (Device Identity ... Controller Test), which are reused, not copied.
 * Pure C89, no libc, no heap, no Melee address: everything here lives in ms/sign.ovl (OTA by the overlay container).  The engine only opens the overlay on SG_MOCK
 * (Main Menu row "Mockups").  Every new screen carries the header tag "MOCKUP: example data"; nothing is sent, nothing is stored.
 * sg_mocklist.c is the one list core (list left, detail right, cursor, scrolling, mock states) shared by Rooms Browser, Friend List, Grandmaster Leaderboard,
 * Rank Tiers and Spectate List; sg_mockup.c holds the hub, the example data, Game Night Room and Recorder Indicator.
 */
#ifndef SG_MOCKUP_H
#define SG_MOCKUP_H
/* SG_MOCKUPS 0: the hub, its example data and the list core are compiled out (release overlays, sg_ovl_pack.py: the Main Menu row "Mockups" is a DEV row of
   tools/menu_layout.py and no release layout has it; a request for a screen >= SG_MOCK lands on the Accounts hub).  Default 1: lab overlays and the host tests.
   sgm_frd_join (the Friends list's Join Room) and the Game Night browser's three dispatchers (sgm_enter / sgm_frame / sgm_draw, GN branch only) are real features and stay in every build. */
#ifndef SG_MOCKUPS
#define SG_MOCKUPS 1
#endif

#define SGM_TAG "MOCKUP: example data"
#define SGM_TAG_COL 29u            /* the tag starts here: titles are at most 28 characters, and short (the menu font is proportional: a 23-letter capital title pushed the tag off the screen in Dolphin) */
#define SGM_MAX_COL 49u            /* a drawn row is at most 49 characters (the width the console shows of the 52-cell view, tests/sg_ctl_test.c) */

/* mock states of the list screens (X cycles them; Start = refresh passes through LOADING) */
enum { SGM_LIVE, SGM_LOADING, SGM_UNREACH, SGM_EMPTY, SGM_FULL, SGM_SPECONLY, SGM_NSTATE };
#define SGM_LOAD_FRAMES 45u       /* a mock refresh shows "Loading" this long (1.5 s at the overlay's 30 frames/s) */

struct sgm {
    unsigned char on;              /* this overlay session was opened on the Mockups hub: every way back to the Accounts hub lands on the Mockups hub instead */
    unsigned char hub;             /* Mockups hub cursor (kept while a screen is open) */
    unsigned char cur, top;        /* list cursor and first shown row of the open list screen */
    unsigned char state;           /* SGM_* */
    unsigned char room;            /* Game Night Room: index of the example room shown */
    unsigned char ready;           /* Game Night Room: bit per member, 1 = Ready */
    unsigned char phase;           /* Game Night Room: 0 waiting, 1 call-up, 2 playing */
    unsigned char rec, recmode;    /* Recorder Indicator: setting 0 Off / 1 Ranked Only / 2 On; previewed mode 0 Grandmaster / 1 Dojo / 2 Game Night */
    unsigned char ret;             /* Game Night Room opened from the Rooms Browser: Back returns there */
    unsigned short timer;          /* frames left of a mock refresh */
    const char *msg;               /* status line, 0 = none */
};

/* line builder shared by the two files (no libc) */
struct sgm_ln { char b[52 + 1]; unsigned n; };
void sgm_init_ln(struct sgm_ln *l);
struct sgm_ln *sgm_s(struct sgm_ln *l, const char *s);
struct sgm_ln *sgm_at(struct sgm_ln *l, unsigned col);
struct sgm_ln *sgm_u(struct sgm_ln *l, unsigned v);

struct sg;
struct sg_view;

/* one list kind of the shared core */
struct sgm_kind {
    const char *title;                                  /* header, at most 28 characters */
    const char *empty1, *empty2;                        /* the two lines of the EMPTY state */
    const char *unreach;                                /* the line of the UNREACHABLE state */
    const char *foot;
    unsigned char n, vis;                               /* rows of example data; rows shown at once */
    unsigned char nstates;                              /* X cycles 0 .. nstates-1 (Rooms Browser: all six, the others four) */
    void (*sub)(const struct sg *g, struct sgm_ln *l);                              /* line under the header */
    void (*left)(const struct sg *g, unsigned i, struct sgm_ln *l);                 /* list row i (at most 25 columns) */
    void (*detail)(const struct sg *g, unsigned i, unsigned line, struct sgm_ln *l);  /* line `line` of the panel for row i (at most 22 columns) */
};
#define SGM_LIST_ROW0 3u           /* first list row */
#define SGM_DETAIL_COL 27u         /* the panel column: list rows at most 25 characters, panel lines at most 22 */
#define SGM_MSG_ROW 22u            /* status line */

void sgm_head(struct sg_view *v, const char *title);
void sgm_put(struct sg_view *v, unsigned r, unsigned attr, const char *s);
void sgm_list_nav(struct sgm *m, unsigned n, unsigned vis, int up, int down);
void sgm_list_draw(const struct sg *g, struct sg_view *v, const struct sgm_kind *k);
unsigned sgm_list_n(const struct sg *g, const struct sgm_kind *k);   /* rows in the current state (0 when empty, loading or unreachable) */

/* entry points used by sg_ui.c */
void sgm_enter(struct sg *g, unsigned from);                         /* g->scr is already the new mock screen; from = the screen left */
void sgm_frame(struct sg *g, unsigned edge, int up, int down);       /* one frame of input on a mock screen (edge = newly pressed SG_B_* bits) */
void sgm_draw(const struct sg *g, struct sg_view *v);
#endif
