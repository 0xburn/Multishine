/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * evo: the ONE open sequence of the Event-view screens (Friends frd_ev.c, Grandmaster standings and Game Night gnb_ev.c).
 * CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  The Friends screen's order is the reference and every screen now runs it:
 *   0. the request goes out in the frame of the A press (the glue's net step runs before the screen's, sg_entry.c o_frame);
 *   1. the launcher builds the retail Event screen (its own open), then evo_prep: the retail row marks and texts go, the portrait box is empty, the event type
 *      label, the "Select Character" box and the High Score strip are hidden, the reveal starts;
 *   2. the first frame is the frame with the panel's loading line (no blank wait, nothing blocks on the data);
 *   3. the title and the key line follow after EVR_CHROME frames, the rows one after another from EVR_OPEN (evr.h), rows that arrive later get their own stagger;
 *   4. a page turn replays the reveal quickly, a return from a sub screen shows everything at once.
 * One screen is up at a time, so the reveal state is shared here.
 */
#ifndef EVO_H
#define EVO_H
#include "evr.h"

void evo_prep(void *scr, unsigned nvis);    /* step 1 on the retail Event screen gobj (mnEvent_804D6C60) right after mnEvent_8024E838 */
int evo_frame(unsigned n_rows);             /* once per overlay frame BEFORE the screen draws, with the rows its model has: 1 = what may be shown changed (redraw) */
unsigned evo_rows(void);                    /* rows that may be drawn now (EVR_ALL = all) */
int evo_chrome(void);                       /* 1 = the title and the key line may be drawn */
void evo_page(void);                        /* a page turn: quick replay */
void evo_skip(void);                        /* back from a sub screen / an action list: all at once */
unsigned evo_proof(void);                   /* rows shown (0..15) for the RAM proof words */
#endif
