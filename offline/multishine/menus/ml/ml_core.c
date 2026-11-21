/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Menu layout runtime, pure part.  See ml_core.h. */
#include "ml_core.h"
#include "ml_table.h"
#include "ml_tex.h"

const char *const *const ml_tex_text = ml_tex_names;
const unsigned ml_tex_n = ML_NTEX;
const char *const *const ml_tab_text = ml_tab_names;

const struct ml_screen *const ml_tab = ml_screens;
const unsigned ml_tab_n = ML_NSCREEN;
const struct ml_page *const ml_page_tab = ml_pages;
const unsigned ml_page_n = ML_NPAGE;

const struct ml_screen *ml_screen_for_kind(unsigned kind)
{
    unsigned i;
    for (i = 0; i < ML_NSCREEN; i++)
        if (ml_screens[i].kind == kind)
            return &ml_screens[i];
    return 0;
}

int ml_visible_rows(const struct ml_screen *s)
{
    int i, n = 0;
    for (i = 0; i < (int) s->nrows; i++)
        if (!s->rows[i].hidden)
            n++;
    return n;
}

int ml_first_visible(const struct ml_screen *s)
{
    int i;
    for (i = 0; i < (int) s->nrows; i++)
        if (!s->rows[i].hidden)
            return i;
    return 0;
}

int ml_row_of_mode(const struct ml_screen *s, unsigned mode)
{
    int i;
    for (i = 0; i < (int) s->nrows; i++)
        if (s->rows[i].act == ML_A_MODE && s->rows[i].arg == mode)
            return i;
    return -1;
}

int ml_step(const struct ml_screen *s, int sel, int dir)
{
    int n = (int) s->nrows, i = sel, guard;
    if (ml_visible_rows(s) == 0 || n <= 0)
        return sel;
    for (guard = 0; guard < n; guard++) {
        i += dir;
        if (i < 0)
            i = n - 1;
        else if (i >= n)
            i = 0;
        if (!s->rows[i].hidden)
            return i;
    }
    return sel;
}

const struct ml_row *ml_row_of(unsigned kind, unsigned sel)
{
    const struct ml_screen *s = ml_screen_for_kind(kind);
    if (!s || sel >= s->nrows)
        return 0;
    return &s->rows[sel];
}

int ml_label_empty(unsigned kind, unsigned sel)
{
    const struct ml_screen *s = ml_screen_for_kind(kind);
    if (!s || sel >= s->nrows)
        return 0;
    return s->rows[sel].text != 0;
}

int ml_unlock(unsigned kind, unsigned sel)
{
    const struct ml_screen *s;
    unsigned i;
    for (i = 0; i < ML_NHIDE; i++)
        if (ml_hide_tab[i][0] == kind && ml_hide_tab[i][1] == sel)
            return 1;
    s = ml_screen_for_kind(kind);
    if (!s)
        return 0;
    if (sel >= s->nrows)
        return 1;
    return s->rows[sel].hidden ? 1 : 2;
}

int ml_size_class(const char *label)
{
    int n = 0;
    while (label[n])
        n++;
    if (n <= 11)
        return 0;
    if (n <= 13)
        return 1;
    if (n <= 15)
        return 2;
    if (n <= 17)
        return 3;
    return 4;
}

unsigned ml_css_marker(unsigned mode)
{
    return mode < 3u ? mode + 1u : 0u;
}

/* DATA GATE for the hub's online modes: panel = ms_net_cost(107), bits 4..7 are br_panel_reason (2 the Multishine system
   files are not served by the disc or the card pack, 3 the pack is damaged).  Slippi's online character screen asserts on the missing slpCSS.dat, so those modes must not be entered. */
unsigned ml_online_data_missing(unsigned panel)
{
    unsigned why = (panel >> 4) & 15u;
    return why == 2u || why == 3u;
}

unsigned ml_css_body(unsigned hook_addr, unsigned hook_word)
{
    unsigned off, body;
    if ((hook_word >> 26) != 18u || (hook_word & 3u) != 0u)      /* opcode 18 with AA = 0 and LK = 0: a relative `b` */
        return 0;
    off = hook_word & 0x03FFFFFCu;
    if (off & 0x02000000u)
        off |= 0xFC000000u;                                         /* sign extension of the 26 bit displacement */
    body = hook_addr + off;
    if (body < 0x80000000u || body >= 0x81800000u - 16u || (body & 3u) != 0u)
        return 0;
    return body;
}

/* ---- plate label from Melee's own menu font  ----
   The glyph atlas HSD_SisLib_FontAtlas (main.dol, RAM 0x8040CD40, I4 32 x 32, 512 bytes per glyph: 0-9, A-Z, a-z, punctuation from 230) is read from the running
   game; nothing of it is stored here.  The text is laid out with the glyphs' ink widths (squeezed to fit 168 texels), area-averaged into a 176 x 30 plane
   (piecewise linear row map, 0.763 texel per glyph column, box filter of one texel), emboldened (gain 1.625) and given a soft dilated outline (radius 2.52), then written as the IA4 tiles of the retail plates
   (fill = intensity, outline = alpha).  The constants are the fits against 46 retail plates (doc sections 3 and 6). */
#define MF_W 176u
#define MF_H 30u
#ifndef MF_OK0
#define MF_OK0 219546         /* output rows of the glyph rows 2 / 10 / 28 / 32: cap top, x-height top, baseline, cell bottom (piecewise linear row map, fitted on the retail plates) */
#define MF_OK1 653492
#define MF_OK2 1584955
#define MF_OK3 1751122
#define MF_SL0 79180          /* glyph rows per output row between the knots, 16.16 */
#define MF_SL1 82998
#define MF_SL2 103389
#endif
#ifndef MF_SX0
#define MF_SX0 50004u         /* texels per glyph column, 16.16 */
#endif
#ifndef MF_GAP
#define MF_GAP 160563u        /* gap between glyphs, texels 16.16 */
#endif
#ifndef MF_SPACE
#define MF_SPACE 618496u      /* a space, texels 16.16 */
#endif
#ifndef MF_MAXW
#define MF_MAXW 11075584u     /* widest text, texels 16.16 (169) */
#endif
#define MF_MAXCH 24u
#ifndef MF_FILL_GAIN16
#define MF_FILL_GAIN16 26     /* fill gain in sixteenths */
#endif
#ifndef MF_FILL_BIAS
#define MF_FILL_BIAS 36
#endif

static const char mf_pun[] = ",.:;?!^_/~|'\"()[]{}+-=<>$%#&*@";
static const unsigned char mf_pun_off[] = { 0, 1, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 26, 28, 29, 30, 31, 32, 33 };

/* outline weights, 7 x 7, 256 = full: clip((3.02 - distance) / 0.775, 0, 1) */
#ifndef MF_DWTAB
#define MF_DWTAB \
      0,   0,   0,   6,   0,   0,   0, \
      0,  63, 256, 256, 256,  63,   0, \
      0, 256, 256, 256, 256, 256,   0, \
      6, 256, 256, 256, 256, 256,   6, \
      0, 256, 256, 256, 256, 256,   0, \
      0,  63, 256, 256, 256,  63,   0, \
      0,   0,   0,   6,   0,   0,   0
#endif
static const unsigned short mf_dw[49] = { MF_DWTAB };

static unsigned mf_nib(const unsigned char *g, unsigned x, unsigned y)
{
    unsigned char b = g[(((y >> 3) * 4u + (x >> 3)) * 32u) + (y & 7u) * 4u + ((x & 7u) >> 1)];
    return (x & 1u) ? (unsigned) (b & 15u) : (unsigned) (b >> 4);
}

static int mf_index(char c)
{
    unsigned i;
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'Z')
        return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z')
        return 36 + (c - 'a');
    for (i = 0; i < sizeof mf_pun - 1u; i++)
        if (mf_pun[i] == c)
            return 230 + (int) mf_pun_off[i];
    return -1;
}

/* FNV-1a of the glyphs '0' 'A' 'a' '.' '-': ML_FONT_SUM when the atlas is the retail one */
unsigned ml_font_sum(const unsigned char *atlas)
{
    static const unsigned short gl[5] = { 0, 10, 36, 231, 252 };
    unsigned h = 2166136261u, i, k;
    for (k = 0; k < 5u; k++)
        for (i = 0; i < 512u; i++)
            h = (h ^ atlas[gl[k] * 512u + i]) * 16777619u;
    return h;
}

/* compose `text` into dst (5632 bytes, IA4 8 x 4 tiles, 176 x 30 padded to 32 rows); plane = 5280 scratch bytes.  Returns 0, or -1 for a character the atlas path does not have */
/* the glyph rows per output row below the baseline: MF_SL2 fits the retail plates; the small preview lists set 65536 (ml_glue.c) so a descender keeps its length
   when the list scales the label down to 0.72 */
unsigned ml_font_desc16 = MF_SL2;
int ml_font_label(unsigned char *dst, const unsigned char *atlas, const char *text, unsigned char *plane)
{
    int gix[MF_MAXCH];
    unsigned gx0[MF_MAXCH], gw[MF_MAXCH];
    int tj0[MF_H], tn[MF_H], sb[MF_H + 1u];
    unsigned tw[MF_H][4];
    unsigned n = 0, i, k, r, sumw = 0, nl = 0, nsp = 0, nat, s12, sx, gap, spc, total, pos, o, tx, ty, x, y;
    int j;
    while (text[n]) {
        if (n >= MF_MAXCH)
            return -1;
        if (text[n] == ' ') {
            gix[n] = -1;
            nsp++;
        } else {
            const unsigned char *g;
            unsigned lo = 32, hi = 0;
            gix[n] = mf_index(text[n]);
            if (gix[n] < 0)
                return -1;
            g = atlas + (unsigned) gix[n] * 512u;
            for (x = 0; x < 32u; x++)
                for (y = 0; y < 32u; y++)
                    if (mf_nib(g, x, y) >= 3u) {
                        if (x < lo)
                            lo = x;
                        hi = x;
                        break;
                    }
            if (hi < lo)
                return -1;
            if (text[n] == '-') {           /* the glyph is a full width minus: the plates use a short hyphen */
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
        return -1;
    nat = MF_SX0 * sumw + MF_GAP * (nl - 1u) + MF_SPACE * nsp;
    s12 = nat > MF_MAXW ? (MF_MAXW >> 4) * 4096u / (nat >> 4) : 4096u;
    sx = (MF_SX0 * s12) >> 12;
    gap = (MF_GAP * s12) >> 12;
    spc = (MF_SPACE * s12) >> 12;
    total = sumw * sx + (nl - 1u) * gap + nsp * spc;
    /* vertical taps: output row r averages the glyph rows [src(r), src(r + 1)); src is piecewise linear through the four knots (cap top, x-height top, baseline, cell bottom) */
    for (r = 0; r <= MF_H; r++) {
        int d, g0, sl;
        unsigned ro = r << 16;
        if ((int) ro < MF_OK1) {
            g0 = 2 << 16;
            d = (int) ro - MF_OK0;
            sl = MF_SL0;
        } else if ((int) ro < MF_OK2) {
            g0 = 10 << 16;
            d = (int) ro - MF_OK1;
            sl = MF_SL1;
        } else {
            g0 = 28 << 16;
            d = (int) ro - MF_OK2;
            sl = (int) ml_font_desc16;
        }
        sb[r] = g0 + (((d >> 6) * (sl >> 6)) >> 4);
    }
    for (r = 0; r < MF_H; r++) {
        int lo = sb[r], hi = sb[r + 1], a, b, v0, v1;
        tn[r] = 0;
        tj0[r] = 0;
        a = lo < 0 ? 0 : lo;
        b = hi > 32 * 65536 ? 32 * 65536 : hi;
        if (b <= a)
            continue;
        tj0[r] = a >> 16;
        for (j = a >> 16; j <= (b - 1) >> 16 && tn[r] < 4; j++) {
            v0 = j * 65536 > a ? j * 65536 : a;
            v1 = (j + 1) * 65536 < b ? (j + 1) * 65536 : b;
            tw[r][tn[r]++] = ((unsigned) (v1 - v0) << 8) / ((unsigned) (hi - lo) >> 8);
        }
    }
    for (i = 0; i < MF_W * MF_H; i++)
        plane[i] = 0;
    pos = (88u << 16) - total / 2u;
    for (i = 0; i < n; i++) {
        const unsigned char *g;
        if (gix[i] < 0) {
            pos += spc;
            continue;
        }
        g = atlas + (unsigned) gix[i] * 512u;
        for (k = 0; k < gw[i]; k++) {
            unsigned start = pos + k * sx, end = start + sx, p0 = start >> 16, ov0, w0, sw, c[32], any = 0, v;
            if (p0 >= MF_W)
                break;
            for (y = 0; y < 32u; y++) {
                c[y] = mf_nib(g, gx0[i] + k, y) * 17u;
                any |= c[y];
            }
            if (!any)
                continue;
            ov0 = ((p0 + 1u) << 16) < end ? ((p0 + 1u) << 16) - start : sx;
            w0 = ov0;                         /* mass of the column in texel p0 (box filter of width 1 texel) */
            sw = sx;
            for (r = 0; r < MF_H; r++) {
                v = 0;
                for (j = 0; j < tn[r]; j++)
                    v += c[(unsigned) tj0[r] + (unsigned) j] * tw[r][j];
                v >>= 16;
                if (v) {
                    unsigned t = plane[r * MF_W + p0] + ((v * w0 + 0x8000u) >> 16);
                    plane[r * MF_W + p0] = (unsigned char) (t > 255u ? 255u : t);
                    if (w0 < sw && p0 + 1u < MF_W) {
                        t = plane[r * MF_W + p0 + 1u] + ((v * (sw - w0) + 0x8000u) >> 16);
                        plane[r * MF_W + p0 + 1u] = (unsigned char) (t > 255u ? 255u : t);
                    }
                }
            }
        }
        pos += gw[i] * sx + gap;
    }
    for (i = 0; i < MF_W * MF_H; i++) {          /* emboldened fill */
        int t = (((int) plane[i] * MF_FILL_GAIN16) >> 4) - MF_FILL_BIAS;
        plane[i] = (unsigned char) (t < 0 ? 0 : t > 255 ? 255 : t);
    }
    o = 0;
    for (ty = 0; ty < 32u; ty += 4)
        for (tx = 0; tx < MF_W; tx += 8)
            for (y = ty; y < ty + 4u; y++)
                for (x = tx; x < tx + 8u; x++) {
                    unsigned f, a, dx, dy;
                    if (y >= MF_H) {
                        dst[o++] = 0;
                        continue;
                    }
                    f = plane[y * MF_W + x];
                    a = f;
                    for (dy = 0; dy < 7u && a < 255u; dy++) {
                        unsigned yy = y + dy;
                        if (yy < 3u || yy - 3u >= MF_H)
                            continue;
                        yy -= 3u;
                        for (dx = 0; dx < 7u; dx++) {
                            unsigned xx = x + dx, m;
                            if (xx < 3u || xx - 3u >= MF_W)
                                continue;
                            m = (plane[yy * MF_W + xx - 3u] * mf_dw[dy * 7u + dx]) >> 8;
                            if (m > a)
                                a = m;
                        }
                    }
                    dst[o++] = (unsigned char) ((((a * 15u + 127u) / 255u) << 4) | ((f * 15u + 127u) / 255u));
                }
    return 0;
}

/* ---- SIS text: punctuation the ASCII path of Melee's encoder does not have ----
   HSD_SisLib_803A67EC (the encoder behind HSD_SisLib_803A6B98) knows space " ' , - . 0-9 : A-Z a-z; any other byte is taken as the first byte of a two byte Shift-JIS code
   and swallows the next character, so "(" used to vanish together with its neighbour.  The font does hold the glyphs (atlas indices 228 to 263): they are reached with
   the full-width Shift-JIS codes.  Writes `in` to `out` (cap bytes) with those characters replaced; returns the length, or 0 when it does not fit. */
static const char mf_sj_chr[] = "!?;()[]+/%#&*@$=<>~|_^{}";
static const unsigned char mf_sj_lo[] = { 0x49, 0x48, 0x47, 0x69, 0x6A, 0x6D, 0x6E, 0x7B, 0x5E, 0x93, 0x94, 0x95, 0x96, 0x97, 0x90, 0x81, 0x83, 0x84, 0x60, 0x62, 0x51, 0x4F, 0x6F, 0x70 };

unsigned ml_sis_expand(char *out, unsigned cap, const char *in)
{
    unsigned n = 0, i, j;
    for (i = 0; in[i]; i++) {
        int k = -1;
        for (j = 0; j < sizeof mf_sj_chr - 1u; j++)
            if (mf_sj_chr[j] == in[i])
                k = (int) j;
        if (n + 3u > cap)
            return 0;
        if (k < 0) {
            out[n++] = in[i];
        } else {
            out[n++] = (char) 0x81;
            out[n++] = (char) mf_sj_lo[k];
        }
    }
    out[n] = 0;
    return n;
}

/* ---- the same menu font for everything else: title tabs (I4 168 x 28) and the preview lists (I4 128 x 104) ----
   `src` is a plate label made by ml_font_label (IA4 176 x 30, 8 x 4 tiles, fill = low nibble); the letters (fill only, no outline) are scaled by k16 (16.16, 65536 = plate size; reduced
   when the ink would be wider than maxw texels) with a box filter and written into the I4 image dst (8 x 8 tiles, two texels per byte, high nibble first, dw x dh, the caller clears it)
   with the ink's left edge at x0 (x0 < 0: centred in dw) and the top of the text rows at y0 (y0 < 0: centred in dh).  Returns the number of texels of ink width, or -1 when the label has no ink. */
static unsigned ia4_fill(const unsigned char *src, unsigned x, unsigned y)
{
    return src[((y >> 2) * 22u + (x >> 3)) * 32u + (y & 3u) * 8u + (x & 7u)] & 15u;
}

int ml_font_to_i4(unsigned char *dst, unsigned dw, unsigned dh, int x0, int y0, const unsigned char *src, unsigned k16, unsigned maxw, int shear8, unsigned xs16)
{
    unsigned l = MF_W, r = 0, x, y, ink, ow, oh, inv, invx, k, kx;
    for (y = 0; y < MF_H; y++)
        for (x = 0; x < MF_W; x++)
            if (ia4_fill(src, x, y) >= 2u) {
                if (x < l)
                    l = x;
                if (x > r)
                    r = x;
            }
    if (r < l)
        return -1;
    ink = r - l + 1u;
    k = k16;
    kx = ((k16 >> 4) * (xs16 >> 4)) >> 8;          /* the retail strips' letters are wider than the plates' (xs16 = 65536: as the plate) */
    if (((ink * kx) >> 16) > maxw) {                                /* too wide: squeezed in x first (the retail way), the height stays */
        kx = (maxw << 16) / ink;
        if (kx * 100u < k * 70u)
            k = (kx * 100u) / 70u;                                  /* never narrower than 0.7 of the height: then it shrinks in y as well */
    }
    ow = (ink * kx + 0xFFFFu) >> 16;                                /* rounded UP: the last column of a squeezed line held the right stroke of its last letter (the final 'o' of 'Mano a mano' read as a 'c') */
    oh = (MF_H * k) >> 16;
    if (ow == 0u || oh == 0u)
        return -1;
    inv = 0xFFFFFFFFu / k;                                       /* source texels per output texel, 16.16 */
    invx = 0xFFFFFFFFu / kx;
    {
        /* italic (the header strips look like the retail ones): a row y of the output is shifted right by (baseline - y) * shear8 / 256 texels, so the letters lean like
           the retail strips (measured on the retail "Main Menu" strip: 0.32 texel per row); y0 <= -the cap top of the letters stands on output row -(y0 + 1000), as in the retail strips */
        int by = (int) ((((MF_OK2 >> 4) * (k >> 4)) >> 24)), ct = (int) ((((MF_OK0 >> 4) * (k >> 4)) >> 24));
        int lo = ((by - ((int) oh - 1)) * shear8) / 256, hi = ((by - ct) * shear8) / 256;
        int ox = x0 < 0 ? ((int) dw - ((int) ow + hi - lo)) / 2 - lo : x0, oy = y0 <= -1000 ? -(y0 + 1000) - ct : y0 < 0 ? ((int) dh - (int) oh) / 2 : y0;
        for (y = 0; y < oh; y++)
            for (x = 0; x < ow; x++) {
                unsigned sx0 = l + ((x * invx) >> 16), sx1 = l + (((x + 1u) * invx + 0xFFFFu) >> 16), sy0 = (y * inv) >> 16, sy1 = ((y + 1u) * inv + 0xFFFFu) >> 16, sx, sy, sum = 0, n = 0, v;
                int px = ox + (int) x + ((by - (int) y) * shear8) / 256, py = oy + (int) y;
                unsigned o, tile;
                if (px < 0 || py < 0 || px >= (int) dw || py >= (int) dh)
                    continue;
                if (sx1 <= sx0)
                    sx1 = sx0 + 1u;
                if (sy1 <= sy0)
                    sy1 = sy0 + 1u;
                for (sy = sy0; sy < sy1 && sy < MF_H; sy++)
                    for (sx = sx0; sx < sx1 && sx < MF_W; sx++) {
                        sum += ia4_fill(src, sx, sy);
                        n++;
                    }
                if (!n)
                    continue;
                v = (sum * 13u) / (n * 8u);                  /* box average, gain 1.6: the thinner scaled strokes keep their weight */
                if (v > 15u)
                    v = 15u;
                tile = ((unsigned) py >> 3) * (dw >> 3) + ((unsigned) px >> 3);
                o = tile * 64u + (((unsigned) py & 7u) << 3) + ((unsigned) px & 7u);
                if (o & 1u) {                                       /* never darker than what is there: a list line's box overlaps the line above, whose descenders (j, g, p, y) it would clear */
                    if (v > (dst[o >> 1] & 0x0Fu))
                        dst[o >> 1] = (unsigned char) ((dst[o >> 1] & 0xF0u) | v);
                } else if (v > (unsigned) (dst[o >> 1] >> 4))
                    dst[o >> 1] = (unsigned char) ((dst[o >> 1] & 0x0Fu) | (v << 4));
            }
    }
    return (int) ow;
}

/* ---- Konami code (ml_core.h) ---- */
unsigned ml_konami_step(struct ml_konami *k, unsigned edge, unsigned now)
{
    static const unsigned short seq[10] = { 0x08, 0x08, 0x04, 0x04, 0x01, 0x02, 0x01, 0x02, 0x200, 0x100 };
    edge &= ML_KONAMI_MASK;
    if (!edge)
        return 0;
    if (k->idx && now - k->last > ML_KONAMI_GAP)
        k->idx = 0;
    k->last = now;
    if (edge & seq[k->idx]) {                      /* the expected button is in the edge (a diagonal, stick and D-pad together, a second button in the same frame: not a mistake) */
        k->idx++;
        if (k->idx == 10u) {
            k->idx = 0;
            return 1;
        }
        return 0;
    }
    if (edge & 0x300u)                             /* a wrong A or B starts over (a wrong direction is ignored: a stick flick between two presses must not cost the whole code) */
        k->idx = (edge & seq[0]) ? 1u : 0u;
    return 0;
}

/* the Main Menu's Friends panel: up to four short lines, each only when its count is non-zero: "3 online", "2 unread", "1 request", "1 invite" (sentence case, no full
   stops, singular / plural); the unread line is followed by "on website" (the messages are read there, not on the console:; the menu font has no parentheses).  A count over 99 reads "99+".  Letters, digits and spaces only.  Returns the number of lines (0 = an empty panel). */
unsigned ml_news_lines(unsigned online, unsigned unread, unsigned req, unsigned inv, char out[5][16])
{
    static const char *const word[4] = { "online", "unread", "request", "invite" };
    unsigned cnt[4], n = 0, i, k, o;
    cnt[0] = online; cnt[1] = unread; cnt[2] = req; cnt[3] = inv;
    for (i = 0; i < 4u; i++) {
        unsigned c = cnt[i];
        if (!c) continue;
        o = 0;
        if (c > 99u) { out[n][o++] = '9'; out[n][o++] = '9'; out[n][o++] = '+'; }
        else { if (c > 9u) out[n][o++] = (char) ('0' + c / 10u); out[n][o++] = (char) ('0' + c % 10u); }
        out[n][o++] = ' ';
        for (k = 0; word[i][k]; k++) out[n][o++] = word[i][k];
        if (i >= 2u && c != 1u) out[n][o++] = 's';
        out[n][o] = 0;
        n++;
        if (i == 1u) { for (k = 0; "on website"[k]; k++) out[n][k] = "on website"[k]; out[n][k] = 0; n++; }
    }
    return n;
}
