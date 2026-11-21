/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * qc_win: the ONE layout of the quick chat window, shared by the two screens that draw it ("reuse code if you can").
 *   - the online character screen (qc_gc.c, the CSS text canvas, font 0.1, 1 text unit = 1 px), and
 *   - the Game Night lobby picker (sign/gnb_ev.c, the menu canvas, TX()/TY px mapping).
 * Everything here is in px of the 640 x 528 frame and integer only (the overlays are soft-float): the pitch, the paddings, the column, the width estimate and the cut rule of a long text,
 * and qw_plan, which turns the header + the four texts (order Up, Left / Right, Down) into the plate's width and height and the offsets of the title and the four cells from the plate's
 * top left.  The canvases only convert px to their own text units (and add their own glyph-top offset), nothing else: there is no second copy of the numbers.
 */
#ifndef QC_WIN_H
#define QC_WIN_H

#define QW_FN static __attribute__((unused))
#include "../sign/toast_pill.h"                    /* the one plate header: the plate and the glyph table (tp_gw) */
#define QW_PITCH 20                                     /* one pitch for the message block and the window: even spacing */
#define QW_PAD_X 12                                     /* text left of the plate's edge; the plate's right padding is the same (width = widest line + 2 x this) */
#define QW_PAD_Y 4                                      /* extra plate height over n lines (n x pitch + 2 x this): the glyph cell has its own air */
#define QW_MAX_W 380                                    /* the widest plate */
#define QW_COL 180                                      /* the second column starts 180 px after the first */
#define QW_DOTS 1060                                    /* the two dots of a cut line: 2 x 0.70 x 7.56 px x 100 */
#define QW_LOWER_PX 756                                 /* a lower case letter drawn at the CSS font 0.1 with x scale 0.38, kerning on: 7.56 px x 100 (Dolphin: 180 / 108 / 91 px for texts the old 9 px estimate had at 212 / 128 / 111 = x 0.84) */
#define QW_BLK_H(n) ((n) * QW_PITCH + 2 * QW_PAD_Y)
#define QW_WIN_H QW_BLK_H(3)                            /* header + two rows */
#define QW_ROW_Y(i) (QW_PAD_Y + (i) * QW_PITCH)         /* row i (0 = the header) top inside the plate, px */

/* drawn width of a raw line in 1/100 px: toast_pill.h's ONE glyph table (tp_gw, 1/100 of a lower case letter) x QW_LOWER_PX.  The text is the CSS text, which arrives already as Shift-JIS pairs (0x81 + byte, one
   letter wide); the lobby's plain ASCII punctuation the canvas turns into pairs is also one letter wide in the table, so the picker and the character screen measure the same text the same. */
QW_FN int qw_gw_x100(const unsigned char *raw, int *len)
{
    *len = (raw[0] == 0x81 && raw[1]) ? 2 : 1;
    return *len == 2 ? QW_LOWER_PX : (int)(tp_gw(raw[0]) * QW_LOWER_PX / 100u);
}

/* the line is cut (ending "..") so that its drawn width stays within max_px, the result is its drawn width in px (what the plates are wrapped to).  sj is kept for the callers (the table does not need it). */
QW_FN int qw_fit_a(unsigned char *raw, int max_px, int sj)
{
    int i = 0, px = 0, last_ok = 0, px_ok = 0, mx = max_px * 100;
    (void)sj;
    while (raw[i]) {
        int w, len;
        w = qw_gw_x100(raw + i, &len);
        if (px + w > mx - QW_DOTS) { if (!last_ok) { last_ok = i; px_ok = px; } }          /* QW_DOTS = the ".." (two dots) */
        if (px + w > mx) {                                                          /* does not fit: keep what fits before "..", add ".." */
            raw[last_ok] = '.'; raw[last_ok + 1] = '.'; raw[last_ok + 2] = 0;
            return (px_ok + QW_DOTS + 99) / 100;
        }
        px += w;
        i += len;
    }
    return (px + 99) / 100;
}

/* the plan of the window: a w x QW_WIN_H plate; the title at (x[0], row 0), the cells i = 0 Up, 1 Left (row 1, column 0 / 1), 2 Right, 3 Down (row 2): cell_x(i) = x[i & 1], cell_row(i) = 1 + (i >> 1).
   hdr and the four raw lines are cut in place (a long own text ends in ".." inside its column). */
struct qw_plan { int w, h, x[2]; };
QW_FN void qw_plan(struct qw_plan *p, unsigned char *hdr, unsigned char *const raw[4], int sj)
{
    int i, wh, wr = 0, w;
    wh = qw_fit_a(hdr, QW_MAX_W - 2 * QW_PAD_X, sj);
    for (i = 0; i < 4; i++) {
        int px = qw_fit_a(raw[i], (i & 1) ? QW_MAX_W - 2 * QW_PAD_X - QW_COL : QW_COL - 16, sj);   /* a long own text is cut inside its column */
        if ((i & 1) && px > wr) wr = px;
    }
    w = QW_COL + wr + 2 * QW_PAD_X;
    if (wh + 2 * QW_PAD_X > w) w = wh + 2 * QW_PAD_X;
    if (w > QW_MAX_W) w = QW_MAX_W;
    p->w = w; p->h = QW_WIN_H;
    p->x[0] = QW_PAD_X; p->x[1] = QW_PAD_X + QW_COL;
}
#endif
