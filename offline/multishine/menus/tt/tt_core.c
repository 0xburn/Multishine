/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* tt_core: the title text band composer (tt_core.h).  Integer only (the console build is soft-float), C89, no libc. */
#include "tt_core.h"

typedef unsigned char u8;

#define TT_GAP 4u                /* glyph columns between two glyphs */
#define TT_SPACE 12u             /* glyph columns of a space */
#define TT_CAPROWS 26u           /* glyph rows from the cap top (row 2) to the baseline (row 28) */
#define TT_GAIN16 22u            /* fill gain in sixteenths: small text needs a little weight */
#define TT_BIAS 12u
#define TT_TAPS 6u

unsigned tt_fnv(const unsigned char *p, unsigned n)
{
    unsigned h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static unsigned t4_off(unsigned w, unsigned x, unsigned y)
{
    return (((y >> 3) * (w >> 3) + (x >> 3)) << 5) + ((y & 7u) << 2) + ((x & 7u) >> 1);
}

unsigned tt_get4(const unsigned char *img, unsigned w, unsigned x, unsigned y)
{
    unsigned b = img[t4_off(w, x, y)];
    return (x & 1u) ? (b & 15u) : (b >> 4);
}

void tt_put4(unsigned char *img, unsigned w, unsigned x, unsigned y, unsigned v)
{
    unsigned o = t4_off(w, x, y);
    if (x & 1u)
        img[o] = (u8) ((img[o] & 0xF0u) | (v & 15u));
    else
        img[o] = (u8) ((img[o] & 0x0Fu) | ((v & 15u) << 4));
}

/* atlas glyph texel (32 x 32 I4) */
static unsigned g_nib(const u8 *g, unsigned x, unsigned y)
{
    return tt_get4(g, 32u, x, y);
}

static const char tt_pun[] = ",.:;?!^_/~|'\"()[]{}+-=<>$%#&*@";
static const u8 tt_pun_off[] = { 0, 1, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 28, 29, 30, 31, 32, 33 };

static int g_index(char c)
{
    unsigned i;
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'Z')
        return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z')
        return 36 + (c - 'a');
    for (i = 0; i < sizeof tt_pun - 1u; i++)
        if (tt_pun[i] == c)
            return 230 + (int) tt_pun_off[i];
    return -1;
}

/* one line of text into dst (rows rows): cap height `cap`, cap top at row `ytop`, left edge x0, at most `wmax` texels wide.  dry: only measure, return the width in texels */
static int draw_line(u8 *dst, unsigned rows, const u8 *atlas, const char *text, unsigned cap, unsigned x0, unsigned wmax, unsigned ytop, u8 *plane, int dry)
{
    int gix[TT_MAXCH];
    unsigned gx0[TT_MAXCH], gw[TT_MAXCH];
    unsigned tj0[TT_PLANE_H], tn[TT_PLANE_H], tw[TT_PLANE_H][TT_TAPS];
    unsigned n = 0, i, k, r, x, y, sumw = 0, nl = 0, nsp = 0, cols, inv, sv, sx, pos, band, yb;
    while (text[n]) {
        if (n >= TT_MAXCH)
            return -2;
        if (text[n] == ' ') {
            gix[n] = -1;
            nsp++;
        } else {
            const u8 *g;
            unsigned lo = 32, hi = 0;
            gix[n] = g_index(text[n]);
            if (gix[n] < 0)
                return -2;
            g = atlas + (unsigned) gix[n] * 512u;
            for (x = 0; x < 32u; x++)
                for (y = 0; y < 32u; y++)
                    if (g_nib(g, x, y) >= 3u) {
                        if (x < lo)
                            lo = x;
                        hi = x;
                        break;
                    }
            if (hi < lo)
                return -3;
            if (text[n] == '-') {           /* the atlas minus is full width: a short hyphen, as the menu plates */
                lo = 11;
                hi = 21;
            }
            gx0[n] = lo;
            gw[n] = hi - lo + 1u;
            sumw += gw[n];
            nl++;
        }
        n++;
    }
    if (nl == 0)
        return -3;
    sv = (cap << 16) / TT_CAPROWS;                 /* texels per glyph row */
    inv = (TT_CAPROWS << 16) / cap;                /* glyph rows per texel */
    cols = sumw + TT_GAP * (nl - 1u) + TT_SPACE * nsp;
    sx = sv;
    if (((cols * (sx >> 4)) >> 12) > wmax)         /* too wide: squeeze horizontally only */
        sx = (wmax << 16) / cols;
    if (dry)
        return (int) ((cols * sx) >> 16);
    band = ((32u * sv) >> 16) + 2u;                /* plane rows the glyph cell covers */
    if (band > TT_PLANE_H)
        return -1;
    /* vertical taps: plane row r averages the glyph rows [r * inv, (r + 1) * inv) */
    for (r = 0; r < band; r++) {
        unsigned lo = r * inv, hi = lo + inv, j;
        tn[r] = 0;
        tj0[r] = lo >> 16;
        if (hi > (32u << 16))
            hi = 32u << 16;
        for (j = lo >> 16; (j << 16) < hi && j < 32u && tn[r] < TT_TAPS; j++) {
            unsigned v0 = (j << 16) > lo ? (j << 16) : lo, v1 = ((j + 1u) << 16) < hi ? ((j + 1u) << 16) : hi;
            tw[r][tn[r]++] = ((v1 - v0) << 8) / (inv >> 8);     /* 16.16 share of the texel */
        }
    }
    for (i = 0; i < TT_W * band; i++)
        plane[i] = 0;
    pos = x0 << 16;
    for (i = 0; i < n; i++) {
        const u8 *g;
        if (gix[i] < 0) {
            pos += TT_SPACE * sx;
            continue;
        }
        g = atlas + (unsigned) gix[i] * 512u;
        for (k = 0; k < gw[i]; k++) {
            unsigned start = pos + k * sx, end = start + sx, p0 = start >> 16, w0, c[32], any = 0, v, j;
            if (p0 >= TT_W)
                break;
            for (y = 0; y < 32u; y++) {
                c[y] = (text[i] == '.' && y > 28u) ? 0u : g_nib(g, gx0[i] + k, y) * 17u;   /* the atlas period (231) hangs one row below the baseline: cut there so it is a dot on the line, not a comma-like tail */
                any |= c[y];
            }
            if (!any)
                continue;
            w0 = ((p0 + 1u) << 16) < end ? ((p0 + 1u) << 16) - start : sx;     /* the column's width inside texel p0, the rest in p0 + 1 */
            for (r = 0; r < band; r++) {
                v = 0;
                for (j = 0; j < tn[r]; j++)
                    v += c[tj0[r] + j] * tw[r][j];
                v >>= 16;
                if (v) {
                    unsigned t = plane[r * TT_W + p0] + ((v * w0 + 0x8000u) >> 16);
                    plane[r * TT_W + p0] = (u8) (t > 255u ? 255u : t);
                    if (w0 < sx && p0 + 1u < TT_W) {
                        t = plane[r * TT_W + p0 + 1u] + ((v * (sx - w0) + 0x8000u) >> 16);
                        plane[r * TT_W + p0 + 1u] = (u8) (t > 255u ? 255u : t);
                    }
                }
            }
        }
        pos += gw[i] * sx + TT_GAP * sx;
    }
    /* plane row r holds glyph rows from r * inv: glyph row 2 (cap top) is at plane row 2 / inv; place it on ytop */
    yb = (2u << 16) / inv;                          /* plane row of the cap top (whole rows) */
    for (r = 0; r < band; r++) {
        unsigned yy = ytop + r - yb;
        if (ytop + r < yb || yy >= rows)
            continue;
        for (x = 0; x < TT_W; x++) {
            int f = (int) ((plane[r * TT_W + x] * TT_GAIN16) >> 4) - (int) TT_BIAS;
            unsigned q, old;
            if (f <= 0)
                continue;
            if (f > 255)
                f = 255;
            q = ((unsigned) f * 15u + 127u) / 255u;
            old = tt_get4(dst, TT_W, x, yy);
            if (q > old)
                tt_put4(dst, TT_W, x, yy, q);
        }
    }
    return 0;
}

int tt_compose(unsigned char *dst, const unsigned char *atlas, const struct tt_layout *L, unsigned char *plane)
{
    unsigned i, x, y, textx = L->text_x, logox = L->logo_x, gright = TT_W;
    int rc;
    if (!L || L->text_w == 0 || L->text_x >= TT_W || L->text_x + L->text_w > TT_W || L->nlines > 8u)
        return -1;
    if (L->logo && (L->logo_x + L->logo_w > TT_W || L->logo_y + L->logo_h > TT_IH || (L->logo_w & 1u)))
        return -1;
    for (i = 0; i < TT_BYTES; i++)
        dst[i] = 0;
    if (L->centre && L->nlines >= 1u) {   /* the group (logo, gap, first line) centred on column `centre` */
        int w = draw_line(dst, TT_IH, atlas, L->lines[0].text, L->lines[0].cap, 0u, L->text_w, 0u, plane, 1);
        unsigned group, left, corner;
        if (w < 0)
            return w;
        corner = L->logo && L->logo_gap == 0u;       /* gap 0: the logo is a corner mark at logo_x, outside the centred text */
        group = (L->logo && !corner ? L->logo_w + L->logo_gap : 0u) + (unsigned) w;
        if (group > TT_W || group / 2u > L->centre)
            return -1;
        left = L->centre - group / 2u;
        left &= ~1u;                                 /* even: the logo's texel pairs stay byte aligned */
        logox = corner ? L->logo_x : left;
        textx = left + (L->logo && !corner ? L->logo_w + L->logo_gap : 0u);
        gright = textx + (unsigned) w;
    }
    if (L->right && L->right[0] && L->part != 2u) {          /* the dim string on the right, drawn first (the dimming below touches only its own box) */
        unsigned rc2 = L->right_cap, ry = L->right_y, rx0;
        int wb;
        if (rc2 < 5u || rc2 > 16u || L->right_edge > TT_W || L->right_y + rc2 > TT_IH)
            return -1;
        wb = draw_line(dst, TT_IH, atlas, L->right, rc2, 0u, L->text_w, 0u, plane, 1);
        if (wb < 0)
            return wb;
        if ((unsigned) wb > L->right_edge)
            return -1;
        rx0 = L->right_edge - (unsigned) wb;
        if (L->nlines >= 1u && L->lines[0].y + L->lines[0].cap >= rc2 && rx0 >= gright + 8u)
            ry = L->lines[0].y + L->lines[0].cap - rc2;      /* it fits beside the group: same baseline */
        else if (rx0 < gright && ry < L->lines[0].y + L->lines[0].cap && L->nlines >= 1u)
            return -1;                                       /* the fallback row would collide with the first line */
        rc = draw_line(dst, TT_IH, atlas, L->right, rc2, rx0, L->text_w, ry, plane, 0);
        if (rc)
            return rc;
        for (y = ry; y < ry + rc2 + 2u && y < TT_IH; y++)
            for (x = rx0; x < L->right_edge + 1u && x < TT_W; x++) {
                unsigned v = tt_get4(dst, TT_W, x, y);
                if (v)
                    tt_put4(dst, TT_W, x, y, (v * 5u + 4u) >> 3);   /* about 60 % coverage: dimmer than the credit */
            }
    }
    for (i = 0; i < L->nlines && L->part != 2u; i++) {
        const struct tt_line *ln = &L->lines[i];
        unsigned x0 = i == 0u ? textx : L->text_x;
        int wi;
        if (ln->cap < 5u || ln->cap > 16u || ln->y + ln->cap > TT_IH)
            return -1;
        if (i > 0u && L->centre) {          /* the further lines are centred on the same column as the first group */
            wi = draw_line(dst, TT_IH, atlas, ln->text, ln->cap, 0u, L->text_w, 0u, plane, 1);
            if (wi < 0)
                return wi;
            if ((unsigned) wi / 2u > L->centre)
                return -1;
            x0 = L->centre - (unsigned) wi / 2u;
        }
        rc = draw_line(dst, TT_IH, atlas, ln->text, ln->cap, x0, L->text_w, ln->y, plane, 0);
        if (rc)
            return rc;
    }
    if (L->logo && L->part != 1u)
        for (y = 0; y < L->logo_h; y++)
            for (x = 0; x < L->logo_w; x++) {
                unsigned b = L->logo[y * (L->logo_w >> 1) + (x >> 1)], v = (x & 1u) ? (b & 15u) : (b >> 4);
                if (v > tt_get4(dst, TT_W, logox + x, L->logo_y + y))
                    tt_put4(dst, TT_W, logox + x, L->logo_y + y, v);
            }
    return 0;
}
