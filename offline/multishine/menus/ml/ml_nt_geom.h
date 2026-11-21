/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Test connection: where the notice and the result lines of the Network status page stand (the lines sat one grid row apart = 16.74 px, with a 19 px font
 * their boxes touched, and the last one ran into the footer).  Pure macros in screen pixels (the overlay's 640 x 480 EFB; a 16:9 TV stretches the same picture, so the vertical
 * numbers are the same for 4:3 and widescreen).  The grid: row top = (936 + 1674 r + 50) / 100 (ml_font.h mlf_row_top).
 *
 *   Status 4, then the three actions moved up: Reconnect at grid row 6, Test connection 8, Wired opponents only 10 (the Connection check row was removed on all platforms).
 *   The Network details are no longer an item: Console address, Gateway, Adapter and Public address stand in the footer area while the cursor is on Status (NTG_FACT_Y0).
 *   the selected row's pill spans NTG_PILL_H px from the row top; the first line starts NTG_GAP px under it; lines are NTG_PITCH px apart (BODY font 17 px + 3 px margin);
 *   the last line's box (NTG_BOX px = the BODY line height 21) ends above the footer's row top (grid row 24 = 411).
 */
#ifndef ML_NT_GEOM_H
#define ML_NT_GEOM_H
#define NTG_ROW_RECONNECT 6u
#define NTG_ROW_TEST 8u
#define NTG_ROWTOP(r) ((int) ((936u + 1674u * (unsigned) (r) + 50u) / 100u))
#define NTG_PILL_H 26                  /* the cap shape of a big row's pill */
#define NTG_GAP 5                      /* between the pill and the first line */
#define NTG_PX 17                      /* the BODY size (MLF_BODY) */
#define NTG_MARGIN 3
#define NTG_PITCH (NTG_PX + NTG_MARGIN)
#define NTG_BOX 21                     /* the BODY line box (ml_font_tex.h mlf_sizes[BODY].line) */
#define NTG_LINES 5                    /* NTX_RESULT_LINES; the notice has 4 and the refusal line takes the fifth place */
#define NTG_SAFE_BOTTOM 411            /* the footer's row top: nothing of the block may reach it (the 4:3 title-safe edge is 456) */
/* FOOTER AREA (Wii test: 'the details should be the footer on the Status row; a second footer in the middle covers the options'): below the last row (Wired opponents only, pill bottom 203) and above the help
   line (grid row 22, top 378) the page has ONE block area.  Its first line is NTG_FOOT_Y0; at most five lines of NTG_PITCH fit (last box ends 374 < 378).  Status shows the four facts there, Test connection its
   notice / progress / result; every other row shows nothing there (only its help line).  No block ever stands over a row any more. */
#define NTG_FOOT_Y0 273
#define NTG_FOOT_Y(k) (NTG_FOOT_Y0 + (int) (k) * NTG_PITCH)
#define NTG_Y(k) NTG_FOOT_Y(k)
/* "Wired opponents only": the last item, grid row 10, right under Test connection. */
#define NTG_ROW_WIRED 10u
#define NTG_YR(row, k) NTG_FOOT_Y(k)                 /* (the row argument is kept for the callers: the footer area does not depend on it) */
/* Network details ('show up as a footer text'): the four facts (Console address, Gateway, Adapter, Public address) are NOT an item any more; they stand as four lines, body font, pitch NTG_PITCH,
   at the bottom of the page just above the help line (grid row 22, top 378): first line at NTG_FACT_Y0 = 293, the last box ends at 374.  Every other block (Test connection's five lines end at 275) stays above it. */
#define NTG_FACT_LINES 4
#define NTG_FACT_Y0 NTG_FOOT_Y0
#define NTG_FACT_Y(k) (NTG_FACT_Y0 + (int) (k) * NTG_PITCH)
/* The item order of the page (card route: Test connection and Wired opponents only present): the cursor index of each item. */
#define NTG_I_STATUS 0u
#define NTG_I_RECONNECT 1u
#define NTG_I_TEST 2u
#define NTG_I_WIRED 3u
#define NTG_HIDDEN(i, cur) 0                       /* since no item is hidden: the blocks stand in the footer area, under every row */
#endif
