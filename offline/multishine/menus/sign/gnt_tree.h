/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The bracket as a TREE: the window of the cached server bracket that the Tournament screen draws when Z shows the tree.  Pure C89, no libc, no heap,
 * no Melee address, no floats: it only decides WHICH matches are on screen and WHERE (rows in eighths of the plate area), the console view (gnt_ev.c) turns that into plates and lines and the
 * host test (tests/gnt_tree_test.c) draws the same layout as text.
 *
 * The window is a "pod": column 0 is up to four matches of the round the model shows (g->b_side, g->b_round; the pod is the block of four that holds g->b_sel), column 1 the matches
 * their winners go to, column 2 the next ones (so a winners pod is 4 + 2 + 1, a losers pod follows next_win and shows its 1:1 and 2:1 steps, the winners final continues into the grand
 * final).  Left Right (the model's round) slide the window along the rounds, Up Down move the cursor through the matches of column 0 (a new pod when it crosses a block of four).
 * A match is a plate with two name lines; the line from it goes to the plate of its winner.
 */
#ifndef GNT_TREE_H
#define GNT_TREE_H
#include "gnt.h"

#define GNT_TW_COLS 3u              /* columns on screen */
#define GNT_TW_ROWS 4u              /* matches of column 0 (one pod); the later columns have at most this many too */
#define GNT_TW_EIGHTHS 8u           /* the plate area is 8 units tall: a column of four matches sits at 1 3 5 7, two at 2 6, one at 4 */

struct gnt_tw {
    unsigned char nc;                              /* columns used (1..GNT_TW_COLS) */
    unsigned char n[GNT_TW_COLS];                  /* matches in the column */
    unsigned char mi[GNT_TW_COLS][GNT_TW_ROWS];    /* index of the match in g->b_m (mid - 1) */
    unsigned char y[GNT_TW_COLS][GNT_TW_ROWS];     /* the plate's centre, in eighths of the plate area (1..7) */
    unsigned char lk[GNT_TW_COLS][GNT_TW_ROWS];    /* row of the winner's match in the next column, 0xFF none (or the next column is not shown) */
    unsigned char side[GNT_TW_COLS], rnd[GNT_TW_COLS];   /* the column's side and round (its title) */
    unsigned char cur;                             /* cursor row in column 0 (g->b_sel inside the pod) */
    unsigned char pod, pods;                       /* the pod shown (1..) and how many the round has */
    unsigned char more;                            /* 1: the last column's matches go on to a match that is not shown */
};

/* fills `w` from the model's bracket; 0 = nothing to draw (no bracket, no matches in that round) */
unsigned gnt_tree_layout(const struct gnt *g, struct gnt_tw *w);

#endif
