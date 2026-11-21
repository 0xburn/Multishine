/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Tournament screens: the row / cursor geometry in ONE place (the text rows drifted against the select box, 'Create Tournament' low in the first box,
 * the last row high).  The text rows sit on the retail Event slots (base ny0, pitch dy text units = 26.8 picture px); the select box is an art box placed in
 * picture px (TY).  Measured on the cube: the box pitch came out 1.053 times the text pitch, the two agreeing at row 4, so the box pitch is
 * 26.8 / 1.053 and its first row moves down by 4 rows of the difference.  Every screen (list, page, Create, bracket) uses these and nothing else.
 * Plain macros: the console code feeds them to the soft-float helpers, the host test checks the relations.  All px are in the picture frame of gnt_ev.c (TX / TY).
 */
#ifndef GNT_GEO_H
#define GNT_GEO_H

#define GNT_GEO_TEXT_PITCH_PX 26.8f        /* one text row = PYROW^-1 px (the retail slot pitch dy) */
#define GNT_GEO_CUR_PITCH_PX 25.46f        /* one select box step */
#define GNT_GEO_CUR_Y0_PX 157.36f          /* the select box's centre on row 0 (= 152 + 4 * (26.8 - 25.46): both pitches meet at row 4) */
#define GNT_GEO_CUR_X_PX 322.0f
#define GNT_GEO_CUR_HALF_W_PX 213.0f       /* the box spans X - HALF_W .. X + HALF_W: its left edge is 109 */
/* the Create / Sign up by ID box: 9 rows, equal margins, in the list's own box (centre 254, half height 123 + 15: visible 116..392, 8 px gaps to the title bar and the info box).
   Row k cursor centre = Y0 + k * PITCH; rows 0..8 span (Y0 - PITCH/2) .. (Y0 + 8.5 * PITCH) = 144.63 .. 373.69, centre 259.16; the block (text rows and select box) moves up FORM_SHIFT
   so its centre is the box's 254: margins 23.4 px top and bottom */
#define GNT_GEO_FORM_CY_PX 254.0f
#define GNT_GEO_FORM_BH_PX 123.0f
#define GNT_GEO_FORM_SHIFT_PX 5.16f
#define GNT_GEO_PAD_UNITS 0.0f             /* (reserved) */

/* the columns, in text units from the retail number column x (nx): the entrants count starts a small padding inside the box's left edge, the name after the count */
#define GNT_GEO_NUM_DX (-2.5f)             /* count column x: the box left edge + 8 px (capture 960 wide: box 245..720, 18.8 px per unit) */
#define GNT_GEO_NAME_DX 5.0f               /* name / item column x (394 px): after the widest count "64 of max 64" (128 px at the name font) + 12 px; the page, form and Create rows start here too */

#define GNT_GEO_ROW_FONT 0.0288f           /* name and state word: 4% under the old 0.030 so count + gap + name + gap + state fit the 459 px between the paddings */
#define GNT_GEO_NUM_FONT GNT_GEO_ROW_FONT  /* the count is as big as the name */
#define GNT_GEO_STATE_DX 21.9f             /* right edge of the state word / score, flush with the box right edge - 8 px; the Create form values use it too */

#define GNT_GEO_STAR_DAILY_DX 16.6f        /* the star on the dailies row: "Multishine dailies" ends at about 15.3 and "33m 1/5" starts at about 17.9, so the star sits between them (the other rows keep 15.3) */
#define GNT_GEO_STAR_DX 15.3f              /* centre of the enrolled star (gold '*'): 122 px left of the state word's right edge, right of the longest name (12 characters ends about px 560) and left of the widest state word ("Called off", about 105 px) */

#define GNT_GEO_ARR_L_DX (-3.16f)          /* the dailies row's cycle arrows (Left Right) OUTSIDE the yellow select box (before: -2.55 / 21.95 inside it): '<' is RIGHT aligned ending 0.23 unit (4 px) left of the box's left edge (-2.93), in the gap between the select box and the list box (list edge about -4.65) */
#define GNT_GEO_ARR_R_DX 22.56f            /* '>' is LEFT aligned starting 0.23 unit (4 px) right of the box's right edge; ends about 23.5, list edge about 24.05 */
#define GNT_GEO_ARR_FONT 0.026f            /* twice the old 0.013 (about 18 px wide at the menu font), drawn in the select box's yellow (TV_CUR) */
#define GNT_GEO_ARR_DY 0.035f              /* down from the row's text y (1 px): the bigger glyph's cell is 1 px less than the row font's, so its centre stays on the box centre (text y is the top of the cell; estimate, not yet seen on a console) */

#define GNT_GEO_SCROLL_X_PX 178.0f         /* the retail scroll triangles (joints 3 up / 2 down, Game Night list): x of their parent joint, the box's left inside edge (box 163..480 in picture px) */
#define GNT_GEO_SCROLL_DY (152.0f / 79.0f * 138.0f)   /* their vertical offset from the box centre: the Game Night list puts them 152 units out for a box 79 px half high; this box is 138 px half high, so top / bottom corner */

#endif
