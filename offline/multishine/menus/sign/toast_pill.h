/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * toast_pill: the rounded plate behind the small texts that come up over a screen.  Original work.
 *
 * Melee's text renderer (HSD_SisLib_803A84BC) can only draw a plain rectangle behind a text (bg_color). It also calls HSD_Text.render_callback (+0x58) once per frame
 * AFTER it has loaded the view matrix and the vertex format of the text (position XYZ f32 + tex coord ST f32, 5 words a vertex) and BEFORE it draws the background and the glyphs.
 * tp_draw is such a callback: it reads the text's own box (pos, box_size x font_size), draws a rounded translucent plate there with one triangle fan, and the text is made with
 * bg_color alpha 0 so the renderer adds no rectangle of its own.  The plate is the text's box, so a caller changes the size of a plate by changing the box of its text, nothing else.
 *
 * Use:  t = <make the text>;  tp_attach(t);          (bg_color <- 0, render_callback <- tp_draw)
 * Rules: radius = TP_RADIUS_PCT percent of the plate height (at most half the width), TP_SEG segments a corner, fill TP_FILL (RGBA).
 * Soft-float build: the arithmetic below is the compiler's libgcc soft-float (no FPU instruction); the vertices go to the GX FIFO as float bit patterns.
 * Host test: -DTP_HOST builds only the pure part (tp_points), tests/toast_pill_test.c.
 */
#ifndef TOAST_PILL_H
#define TOAST_PILL_H

#define TP_FN static __attribute__((unused))      /* a unit may not use every function */
#define TP_SEG 4u                              /* line segments a corner */
#define TP_NV (4u * (TP_SEG + 1u) + 2u)        /* triangle fan: the centre, four arcs of TP_SEG + 1 points, the first arc point again */
#define TP_RADIUS_PCT 32u                      /* corner radius as a percent of the plate height */
#ifdef TP_FILL_MENU                            /* the menu profile (hub.ovl toasts: near black, a touch of blue, nearly opaque: the room header showed through) */
#define TP_FILL_R 0x06u
#define TP_FILL_G 0x08u
#define TP_FILL_B 0x0Eu
#define TP_FILL_A 0xF0u
#endif
#ifndef TP_FILL_R                              /* the match / quick chat profile: the dark blue 0x0C1428, alpha 0xF4 (was 0xE0); a unit may still set its own before the include */
#define TP_FILL_R 0x0Cu
#define TP_FILL_G 0x14u
#define TP_FILL_B 0x28u
#define TP_FILL_A 0xF4u
#endif
#define TP_ON_TIME_MS 4000u                    /* a hint that explains a key stays this long after the last button press */

/* ---- fixed point <-> float bits.  The overlays link no floating point library (libgcc is hard-float, the build is soft-float): the plate is computed in Q16 integers and written to the FIFO as
   float bit patterns.  tp_q(bits, q): the float with these bits as a signed value x 2^q (saturated, truncated); tp_f2(v, q): the float bits of v / 2^q (exact up to 24 bits). ---- */
TP_FN int tp_q(unsigned int bits, int q)
{
    int e = (int)((bits >> 23) & 0xFFu) - 127 - 23 + q, v;
    unsigned int m = (bits & 0x7FFFFFu) | 0x800000u;
    if (((bits >> 23) & 0xFFu) == 0u) return 0;                      /* zero / denormal */
    if (e >= 8) v = 0x7FFFFFFF;
    else if (e >= 0) v = (int)(m << e);
    else if (e > -32) v = (int)(m >> -e);
    else v = 0;
    return (bits >> 31) ? -v : v;
}
TP_FN unsigned int tp_f2(int v, int q)
{
    unsigned int sign = v < 0 ? 0x80000000u : 0u, a = v < 0 ? (unsigned int)-v : (unsigned int)v, m;
    int e = 31;
    if (!a) return 0;
    while (!(a & 0x80000000u)) { a <<= 1; e--; }                     /* a has its top bit at 31: value = a x 2^(e - 31) */
    m = (a >> 8) & 0x7FFFFFu;
    return sign | ((unsigned int)(e - q + 127) << 23) | m;
}

/* The points of the rounded rectangle in Q16 text units, counter clockwise: [0] = the centre (the fan hub), then TP_SEG + 1 points a corner (top right, top left, bottom left, bottom
   right), then the first corner point again.  ybot < ytop (Melee's text space: y up, the renderer draws -pos_y).  Returns the vertex count (TP_NV). */
TP_FN unsigned int tp_points_r(int x0, int ybot, int x1, int ytop, int *px, int *py, unsigned int pct)
{
    static const int C[TP_SEG + 1u] = { 65536, 60547, 46341, 25080, 0 };         /* cos of 0, 22.5, 45, 67.5, 90 degrees x 65536 */
    static const int S[TP_SEG + 1u] = { 0, 25080, 46341, 60547, 65536 };
    int h = ytop - ybot, w = x1 - x0, r = (int)((h * pct) / 100), cl, cr, cb, ct, rr;
    unsigned int n = 0, i;
    if (r > w / 2) r = w / 2;
    if (r < 0) r = 0;
    rr = r >> 6;                                                      /* r x C: (r / 64) x C stays inside 32 bits for plates up to 30 units high */
    cl = x0 + r; cr = x1 - r; cb = ybot + r; ct = ytop - r;
    px[n] = x0 + w / 2; py[n] = ybot + h / 2; n++;
    for (i = 0; i <= TP_SEG; i++) { px[n] = cr + ((rr * C[i]) >> 10); py[n] = ct + ((rr * S[i]) >> 10); n++; }              /* top right: 0 .. 90 degrees */
    for (i = 0; i <= TP_SEG; i++) { px[n] = cl - ((rr * S[i]) >> 10); py[n] = ct + ((rr * C[i]) >> 10); n++; }              /* top left: 90 .. 180 */
    for (i = 0; i <= TP_SEG; i++) { px[n] = cl - ((rr * C[i]) >> 10); py[n] = cb - ((rr * S[i]) >> 10); n++; }              /* bottom left: 180 .. 270 */
    for (i = 0; i <= TP_SEG; i++) { px[n] = cr + ((rr * S[i]) >> 10); py[n] = cb - ((rr * C[i]) >> 10); n++; }              /* bottom right: 270 .. 360 */
    px[n] = px[1]; py[n] = py[1]; n++;
    return n;
}

TP_FN unsigned int tp_points(int x0, int ybot, int x1, int ytop, int *px, int *py)
{
    return tp_points_r(x0, ybot, x1, ytop, px, py, TP_RADIUS_PCT);
}

/* "show for a while after the last input": 1 while now - last_in < window (wrap safe), 0 after.  sticky = always 1 (a prompt that waits for an answer). */
TP_FN int tp_visible(unsigned now, unsigned last_in, unsigned window_ms, int sticky)
{
    return sticky || (unsigned)(now - last_in) < window_ms;
}

/* ---- the ONE glyph-width table of the menu font.  Every shrink-wrap of a plate (hub_soc.c toasts, qc_win.h quick chat window and message block, the lobby picker,
   gnb_ev.c / keyhint.h fit_name / est_w / kh_w) measures a text with tp_text_gw and converts with the one calibration of its own font; there is no second table.
   Width of a glyph in 1/100 of a lower case letter (the face is proportional; the class widths are the fit of the measured frames: the Rooms list at font 0.03, the Mano A Mano toast at 0.028,
   the quick chat window in Dolphin: narrow 0.70, capitals 1.30, M W 1.60, m w 1.50 of a lower case letter).  Calibrations (px of the 640 x 528 frame for a lower case letter):
   list / key hint font 0.03 = 10 px, hub toast font 0.028 = 9.3 px (1.06 of the 8.8 px "unit" of its table), quick chat (CSS font 0.1, x scale 0.38) = 7.56 px. ---- */
TP_FN unsigned int tp_gw(unsigned char c)
{
    /* the class of each character 32..127, a nibble each (the even character in the low nibble): 0 narrow  i l j t f I. , ' space: ; ! |   1 lower case and punctuation   2 digit   3 capital   4 m w @ % & #   5 M W */
    static const unsigned char CLS[48] = { 0x00, 0x41, 0x41, 0x04, 0x11, 0x11, 0x10, 0x10, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x11, 0x11, 0x34, 0x33, 0x33, 0x33, 0x03, 0x33, 0x53, 0x33, 0x33, 0x33, 0x33, 0x53, 0x33, 0x13, 0x11, 0x11, 0x11, 0x11, 0x11, 0x10, 0x01, 0x10, 0x40, 0x11, 0x11, 0x11, 0x10, 0x41, 0x11, 0x11, 0x10, 0x11 };
    static const unsigned char GW[6] = { 70u, 100u, 110u, 130u, 150u, 160u };
    if (c < 32u || c > 127u) return 100u;                              /* the second byte of a Shift-JIS pair and the rest */
    return GW[(CLS[(c - 32u) >> 1] >> (((c - 32u) & 1u) * 4u)) & 15u];
}
/* the width of a whole text (plain characters; qc_win.h measures its Shift-JIS pairs itself) */
TP_FN unsigned int tp_text_gw(const char *s)
{
    unsigned int w = 0;
    for (; *s; s++) w += tp_gw((unsigned char)*s);
    return w;
}
#define TP_LIST_PX 10u                                                /* a lower case letter of the list font 0.03 (and the key hints), px */
#define TP_PX_OF(gw, lower_px_x100) (((gw) * (lower_px_x100) + 5000u) / 10000u)    /* gw (1/100 letters) x px a letter x 100 -> whole px, rounded */

#ifndef TP_HOST
typedef unsigned int tp_u32;

/* HSD_Text.render_callback: the plate.  gobj = the text's GObj (user data at +0x2C = the HSD_Text).  Calls into Melee's GX (addresses of the NTSC 1.02 disc, as ml_glue.c spr_emit).
   The plate is the text's box, unless tp_attach_at stored its own rectangle in the four words the renderer leaves alone (x14: only read when x4E is set, which no toast sets):
   +0x14 offset x, +0x18 offset y (down), +0x1C width, +0x20 height, Q16 text units from the box's top left; width 0 = the box. */
TP_FN void tp_draw(void *gobj)
{
    const tp_u32 *t = *(const tp_u32 *const *)((const unsigned char *)gobj + 0x2C);
    int px[TP_NV], py[TP_NV], x0 = tp_q(t[0], 16), yt = -tp_q(t[1], 16), fx = tp_q(t[0x24 / 4], 16), fy = tp_q(t[0x28 / 4], 16), bx = tp_q(t[3], 4), by = tp_q(t[4], 4), w, h;
    volatile tp_u32 *fifo = (volatile tp_u32 *)0xCC008000u;
    unsigned int n, i;
    tp_u32 own = t[0x2C / 4], rp = t[0x1C / 4] >> 24;      /* corner radius percent in the top byte of the plate width (0 = TP_RADIUS_PCT; tp_attach_round) */                                                                 /* bg_color: 0 = the default fill; R G B 0 (alpha 0: the renderer still draws no rectangle) = this plate's own colour (keyhint.h key badges) */
    tp_u32 col = ((tp_u32)TP_FILL_R << 24) | ((tp_u32)TP_FILL_G << 16) | ((tp_u32)TP_FILL_B << 8) | TP_FILL_A;           /* RGBA as bytes in memory (big endian): a word, aligned */
    if (own) col = (own & 0xFFFFFF00u) | TP_FILL_A;
    w = (int)((bx * fx) >> 4); h = (int)((by * fy) >> 4);                                    /* box (Q4 units) x font (Q16) = Q20 -> Q16 */
    if (t[0x1C / 4]) { x0 += (int)t[0x14 / 4]; yt -= (int)t[0x18 / 4]; w = (int)(t[0x1C / 4] & 0xFFFFFFu); h = (int)t[0x20 / 4]; }
    n = tp_points_r(x0, yt - h, x0 + w, yt, px, py, rp ? rp : TP_RADIUS_PCT);
    ((void (*)(int, int, int, int, int))0x8033FF68)(0, 15, 15, 15, 2);                        /* GXSetTevColorIn(STAGE0, ZERO, ZERO, ZERO, C0) */
    ((void (*)(int, int, int, int, int))0x8033FFE8)(0, 7, 7, 7, 1);                           /* GXSetTevAlphaIn(STAGE0, ZERO, ZERO, ZERO, A0) */
    ((void (*)(int, int, int, int, int, int))0x8034006C)(0, 0, 0, 0, 0, 0);                   /* GXSetTevColorOp(STAGE0, ADD, bias 0, scale 1, no clamp, TEVPREV) */
    ((void (*)(int, int, int, int, int, int))0x8034012C)(0, 0, 0, 0, 0, 0);                   /* GXSetTevAlphaOp, the same */
    ((void (*)(int, const void *))0x803401EC)(1, &col);                                    /* GXSetTevColor(TEVREG0, &rgba): by pointer (the SDK takes the GXColor by reference) */
    ((void (*)(int, int, int))0x8033D0DC)(0xA0, 0, (int)n);                                   /* GXBegin(TRIANGLEFAN, FMT0, n) */
    for (i = 0; i < n; i++) {
        fifo[0] = tp_f2(px[i], 16); fifo[0] = tp_f2(py[i], 16); fifo[0] = t[2]; fifo[0] = 0; fifo[0] = 0;       /* x, y, z (the text's), s, t */
    }
}

/* the text's box becomes the plate: no rectangle of its own, the callback draws */
TP_FN void tp_attach(void *text)
{
    tp_u32 *t = (tp_u32 *)text;
    t[0x2C / 4] = 0;                       /* bg_color: alpha 0 = the renderer draws no background quad */
    t[0x58 / 4] = (tp_u32)tp_draw;         /* render_callback */
}
/* the plate is its own rectangle, offset / size in Q16 text units from the box's top left (see tp_draw), for a text whose line sits at an offset inside its box */
TP_FN void tp_attach_at(void *text, int off_x, int off_y, int w, int h)
{
    tp_u32 *t = (tp_u32 *)text;
    t[0x14 / 4] = (tp_u32)off_x; t[0x18 / 4] = (tp_u32)off_y; t[0x1C / 4] = (tp_u32)w; t[0x20 / 4] = (tp_u32)h;
    tp_attach(text);
}
/* a plate in its own colour 0xRRGGBB (the key badges of keyhint.h): the colour goes in the text's bg_color with alpha 0 (the renderer draws no rectangle for alpha 0; tp_draw reads the RGB) */
TP_FN void tp_attach_rgb_at(void *text, unsigned int rgb, int off_x, int off_y, int w, int h)
{
    tp_attach_at(text, off_x, off_y, w, h);
    ((tp_u32 *)text)[0x2C / 4] = (rgb & 0xFFFFFFu) << 8;
}
/* a round badge: as tp_attach_rgb_at with the corner radius pct percent of the height (50 = a circle on a square plate) */
TP_FN void tp_attach_rgb_round(void *text, unsigned int rgb, int off_x, int off_y, int w, int h, unsigned int pct)
{
    tp_attach_rgb_at(text, rgb, off_x, off_y, w, h);
    ((tp_u32 *)text)[0x1C / 4] = (tp_u32)w | (pct << 24);
}
#endif
#endif
