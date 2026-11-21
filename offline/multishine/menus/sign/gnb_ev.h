/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Night browser on the retail Event Match screen ("hybrid").  CONSOLE ONLY: compiled with
 * -DGNB_MELEE by sg_ovl_pack.py into ms/sign.ovl; host builds get the stubs below (the browser then shows its character-cell text view).
 *
 * How (no retail code is patched, nothing resident is added):
 *   1. gnbev_open (from the overlay's frame): the menu think gobj of the screen we come from is found by its callback (the menu table's think of the current
 *      kind) and its callback is swapped for ours, so the next proc pass runs our launcher IN PROC CONTEXT.
 *   2. the launcher does what the retail 1P menu does for Event Match (mnEvent_8024E838(0, 0) builds the screen with the retail art, cursor, arrows, then the old
 *      menu think gobj is freed) and swaps the callback of the new retail Event think (fn_8024D864, the one that would start an event match on A) for our think.
 *   3. the overlay draws our rows into the retail slots (MnEventData.texts / icons: the retail teardown frees them), our panel lines, the badge in the retail score
 *      box, moves the retail cursor joint and the scroll arrows, hides the event type label and empties the portrait (frame 25).
 *   4. leaving: our think calls mn_80229894(kind, row, 3) like the retail B handler, back to the screen we came from, cursor on the row we came from; the retail
 *      Event procs see cur_menu != 7 and tear the screen down.
 * The overlay never starts a scene: GM_TOURNAMENT (0x1B) and GM_EVENT are never touched.
 */
#ifndef GNB_EV_H
#define GNB_EV_H
#include "gnb.h"

enum { GNBEV_IDLE, GNBEV_LAUNCH, GNBEV_UP, GNBEV_CLOSING, GNBEV_CLOSED, GNBEV_FAILED };

int gnbev_open(struct gnb *g);          /* 0 = launching (the next menu proc pass builds the screen), < 0 = cannot (no menu think found: use the text view) */
/* the same screen for the Grandmaster standings (stb.h): always the Event Match look of release 385 (rows, cursor, arrows,
   description box, score box), rank / name / rating / tier per row; one of the two at a time (the state machine is shared) */
struct stb;
int gnbev_open_st(struct stb *sd);
void gnbev_frame(void);                 /* once per overlay frame while open: draws what the model marked dirty, plays its sound */
void gnbev_close(void);                 /* leave: our think goes back to the screen we came from at its next run */
int gnbev_state(void);
void gnbev_reset(void);                 /* CLOSED / FAILED -> IDLE (the overlay took note) */
void gnbev_abort(void);                 /* the overlay is being freed: nothing of ours may stay reachable (texts freed, our think callback replaced by a retail no-op) */
unsigned gnbev_err(void);               /* why it failed (0 none): 1 no menu think, 2 no Event think, 3 no screen */
#endif
