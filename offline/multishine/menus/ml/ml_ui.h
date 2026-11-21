/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Settings look: the numbers both presenters share.  The Melee-text pages (ml_glue.c: Settings,
 * Game, Diagnostics, the loading page) and the sign overlay (sg_ui.c rows drawn through the resident gfx table) put their text on the same 26-row grid
 * (0.9 screen units a row, ml_glue.c row_y) with the same attributes, so one resident helper (ml_glue.c ui_text, ui_bar, ui_mark) draws every page.
 * Pure macros, no code: included by ml_glue.c, sg_ui.h and the host tests.
 *
 *   rows 0..2   header: the small mark top-left, the title (big, white) on row 0, the breadcrumb (dim, small) on row 2
 *   rows 4..    body: list rows are UI_A_BIG (a larger font) two grid rows apart with the selection bar behind the row under the cursor; section headings
 *               (UI_A_SEC) take one grid row; plain text rows use one grid row each
 *   row 20..    note (dim), 22 description of the selected row, 25 footer "Key: Action"
 * A row may carry a VALUE right aligned at the right margin (a second text object: the retail font is proportional, so a column of spaces cannot align it).
 */
#ifndef ML_UI_H
#define ML_UI_H

/* row attributes (the values of sg_ui.h SG_A_*; the flag UI_A_BIG makes a list row) */
#define UI_A_BODY 0
#define UI_A_HEAD 1                /* gold text (the old header and emphasis) */
#define UI_A_HI 2                  /* the selected row: gold */
#define UI_A_DIM 3
#define UI_A_GOOD 4
#define UI_A_BAD 5
#define UI_A_TITLE 6               /* the page title: big, white, next to the mark */
#define UI_A_CRUMB 7               /* the breadcrumb under the title: small, dim */
#define UI_A_SEC 8                 /* a section heading: small, dim (one text object each) */
#define UI_A_BIG 0x10u             /* flag: list row font instead of the text font */
#define UI_A_MASK 0x0Fu

/* the grid */
#define UI_R_TITLE 0
#define UI_R_CRUMB 2
#define UI_R_BODY 4
#define UI_R_NOTE 20
#define UI_R_DESC 22
#define UI_R_STAT 23
#define UI_R_FOOT 24          /* one grid row above the last: the footer was clipped at the bottom edge on the cube */
#define UI_VAL 28                  /* characters of a right aligned value at most */

#endif
