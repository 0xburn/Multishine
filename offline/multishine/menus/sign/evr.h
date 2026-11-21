/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * evr: the row reveal of the Event-view screens (Friends, Grandmaster standings, Game Night;, "stagger when
 * text enters").  Pure C89, no libc, no Melee address: the schedule only; the front ends (frd_ev.c, gnb_ev.c) ask it which rows may be drawn this frame.
 *   open:   the frame comes first (the retail Event screen: its own open), then the title and the key line after EVR_CHROME frames, then row i from
 *           EVR_OPEN + i * EVR_STEP frames (top to bottom); a list that is still loading shows its loading line in the panel meanwhile;
 *   late:   rows that arrive after their slot was due start their own stagger at the frame they arrive (they never pop in all at once);
 *   page:   a page turn (L/R, cursor across a page) replays it quickly (EVR_PAGE_PER rows per frame, no wait);
 *   return: back from a sub screen or an action list: everything at once (evr_skip).
 * EV_STAGGER=0 builds turn it off (every row at once, as before).
 */
#ifndef EVR_H
#define EVR_H

#ifndef EV_STAGGER
#define EV_STAGGER 1
#endif
/* overlay frames (the sign overlay's frame runs once per menu frame, 60 a second).  The open plays the retail Event zoom-in of the main menu panel (frames
   1400..1449 of its joint 0x29: 50 frames, evo.c); the retail screen builds its list 20 frames into it (mnEvent_804D6C64 = 0x14), so the title and key line come
   there and the rows follow, the last one about when the zoom ends */
#ifndef EVR_CHROME
#define EVR_CHROME 18u           /* frames after the open before the title / key line (the frame is up first) */
#endif
#ifndef EVR_OPEN
#define EVR_OPEN 22u             /* frames after the open before row 0 */
#endif
#ifndef EVR_STEP
#define EVR_STEP 3u              /* frames between two rows (open and late rows): row 8 at 46 */
#endif
#define EVR_PAGE_PER 3u          /* a page turn: rows per frame (9 rows in 3 frames) */
#define EVR_ALL 0xFFu

struct evr {
    unsigned short t;            /* frames since the open */
    unsigned short start;        /* frame row 0 is due */
    unsigned char step;          /* frames between two reveals (0: all at once) */
    unsigned char per;           /* rows per reveal */
    unsigned char had;           /* rows that existed at the last evr_tick (0 -> n starts a late stagger) */
    unsigned char chrome;        /* frame the title and key line are due */
};

void evr_open(struct evr *r);                     /* the screen opens: chrome after EVR_CHROME, rows from EVR_OPEN, EVR_STEP apart */
void evr_page(struct evr *r);                     /* a page turn: rows again, EVR_PAGE_STEP apart, from now */
void evr_skip(struct evr *r);                     /* everything at once (back from a sub screen / action list) */
void evr_tick(struct evr *r, unsigned n_rows);    /* once per frame, with the rows the model has now */
unsigned evr_rows(const struct evr *r);           /* rows that may be drawn now (EVR_ALL = no limit) */
int evr_chrome(const struct evr *r);              /* 1 = the title and the key line may be drawn */
int evr_busy(const struct evr *r, unsigned n_rows);   /* 1 while a reveal is still running (the front end redraws each frame) */
#endif
