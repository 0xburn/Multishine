/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* ml_font.h: OUR font on the overlay text pages (option 2).  Included by ml_glue.c in the overlay part when ML_OWNFONT.
 * The page code records lines (mlf_add: pixel position, size, colour, right alignment, text) instead of making Melee HSD_Text objects; ONE GObj in the front camera's text link
 * draws them all: one quad per glyph out of ml_font_tex.h (I4, 256 x 256), positions as s16 pixels and texel coordinates as u16 with 8 fraction bits, so there is no float
 * maths in this soft-float build.  The callback sets its own orthographic projection (one unit = one EFB pixel: texel = pixel) and puts the menu camera's back afterwards.
 * Host: the layout helpers (mlf_width, mlf_add's clipping) build with -DMLF_HOST and no Melee addresses (tests/mlf_test.c). */
#ifndef ML_FONT_H
#define ML_FONT_H
#include "ml_font_tex.h"

#define MLF_ROWS 128                /* lines alive at once: the Settings page under an overlay page, the overlay page, and a redraw makes the new lines before it frees the old  */
#define MLF_LEN 63                  /* characters a line keeps (the page is 640 px wide: 63 glyphs of the body size are wider than that) */
#define MLF_W 640
#define MLF_H 480
#define MLF_BAR_W 240u            /* a drawn progress bar's width in pixels */
struct mlf_line { unsigned char used, size, right, n, shape, grp, bar_n, bar_fill; short x, y; unsigned int rgba; char s[MLF_LEN + 1]; };     /* bar_n: a drawn progress bar of bar_n cells (bar_fill full) before the text (the I/. bars) */     /* shape: 0 text, MLF_S_* one quad; grp: lines freed together (a footer) */
static struct mlf_line mlf_l[MLF_ROWS];
static unsigned int mlf_used;
static unsigned char mlf_grp_next = 1;

/* the width in pixels of s in size sz (what the pen moves) */
static unsigned int mlf_width(const char *s, unsigned int sz)
{
    unsigned int w = 0;
    for (; *s; s++) {
        unsigned int c = (unsigned char)*s;
        w += mlf_glyph[sz][c >= 32u && c <= 126u ? c - 32u : '?' - 32u].adv;
    }
    return w;
}

/* the top of grid row r (26 rows, the Settings look's 0.9 unit pitch from -12.4): 9.36 + 16.74 r pixels */
static int mlf_row_top(unsigned int r) { return (int)((936u + 1674u * r + 50u) / 100u); }

/* a line: x is the left edge (right = 0) or the right edge (right = 1); y the top of the line box; returns the slot or 0 when full */
static struct mlf_line *mlf_add(int x, int y, unsigned int sz, unsigned int rgba, int right, const char *s)
{
    unsigned int i, n = 0;
    struct mlf_line *l = 0;
    if (sz >= MLF_NSIZES) sz = MLF_BODY;
    for (i = 0; i < MLF_ROWS; i++)
        if (!mlf_l[i].used) { l = &mlf_l[i]; break; }
    if (!l) return 0;
    {                                                          /* a progress bar written as a run of 'I' (full) and '.' (empty), 8 or more, then a space or the end (sg_ui.c bar): drawn, not typed */
        unsigned k = 0, f = 0;
        while ((s[k] == 'I' || s[k] == '.') && k < 255u) { if (s[k] == 'I') f++; k++; }
        if (k >= 8u && (s[k] == ' ' || !s[k]) && !right) { l->bar_n = (unsigned char)k; l->bar_fill = (unsigned char)f; s += k; while (*s == ' ') s++; }
        else { l->bar_n = 0; l->bar_fill = 0; }
    }
    while (s[n] && n < MLF_LEN) { l->s[n] = s[n]; n++; }
    l->s[n] = 0;
    l->n = (unsigned char)n;
    l->size = (unsigned char)sz; l->right = (unsigned char)(right != 0); l->rgba = rgba;
    l->x = (short)(right ? x - (int)mlf_width(l->s, sz) : x);
    l->y = (short)y;
    l->shape = 0; l->grp = 0;
    l->used = 1;
    mlf_used++;
    return l;
}
/* one quad of a shape (MLF_S_DISC / MLF_S_PILL) with its top left at x, y */
static struct mlf_line *mlf_add_shape(int x, int y, unsigned int shape, unsigned int rgba)
{
    struct mlf_line *l = mlf_add(x, y, MLF_BODY, rgba, 0, "");
    if (l) { l->shape = (unsigned char)shape; l->bar_n = 0; }
    return l;
}
/* THE SELECTED ROW'S PILL ("shrinkwrapped, pill shaped like the toast and leave indicators, the same yellow", centred on the text): a line with shape MLF_ROWPILL
   and the row's top as y; it has no width of its own: at draw time it wraps the left-aligned text line of the same row (the label) and, when the row has one, its value at the right margin, its
   middle on that line's capitals.  Two half discs (MLF_S_CAP) and a solid middle. */
#define MLF_ROWPILL 0xFEu
#define MLF_PILL_PAD 8              /* pixels between the label and the start of the rounded end */
static struct mlf_line *mlf_add_rowpill(int row_top, unsigned int rgba)
{
    struct mlf_line *l = mlf_add(0, row_top, MLF_BODY, rgba, 0, "");
    if (l) { l->shape = (unsigned char)MLF_ROWPILL; l->bar_n = 0; }
    return l;
}
/* the pill's box (x0 y0 x1 y1, ends included) for a row-pill line; 0 when its row has no label yet */
static int mlf_rowpill_box(const struct mlf_line *p, int *x0, int *y0, int *x1, int *y1)
{
    const struct mlf_line *t = 0, *v = 0;
    unsigned int i;
    for (i = 0; i < MLF_ROWS; i++) {
        const struct mlf_line *l = &mlf_l[i];
        if (!l->used || l->shape || !l->n || l->y != p->y) continue;
        if (!l->right && (!t || l->x < t->x)) t = l;
        else if (l->right && (!v || l->x > v->x)) v = l;                     /* the value at the right margin (the pill covers the setting being changed too) */
    }
    if (!t) return 0;
    {
        const struct mlf_g *h = &mlf_glyph[t->size]['H' - 32];
        const struct mlf_g *c = &mlf_shape[MLF_S_CAP - 1];
        int mid2 = 2 * (t->y + (int)h->y0) + (int)h->h;                      /* twice the capitals' middle */
        int r = (int)c->w / 2;
        *y0 = (mid2 - (int)c->h + 1) / 2; *y1 = *y0 + (int)c->h;
        *x0 = t->x - MLF_PILL_PAD - r; *x1 = (v ? v->x + (int)mlf_width(v->s, v->size) : t->x + (int)mlf_width(t->s, t->size)) + MLF_PILL_PAD + r;
    }
    return 1;
}
static unsigned int mlf_group(void) { unsigned int g = mlf_grp_next++; if (!mlf_grp_next) mlf_grp_next = 1; return g; }
static void mlf_free(struct mlf_line *l)
{
    unsigned int i, g;
    if (!l || !l->used) return;
    g = l->grp;
    l->used = 0; if (mlf_used) mlf_used--;
    if (g)
        for (i = 0; i < MLF_ROWS; i++)
            if (mlf_l[i].used && mlf_l[i].grp == g) { mlf_l[i].used = 0; if (mlf_used) mlf_used--; }
}

/* the footer of the Settings look (ui_foot): "A: Select  Hold B: Back" -> per key token a disc / pill in the GameCube colour with its letter, then the action; the tokens in
   equal shares of x 78..562, each pair centred in its share; plain tokens centred.  y is the row top.  Returns the first line (all of them share one group) or 0. */
#ifdef KH_FN
static struct mlf_line *mlf_footer(const char *s, int y, unsigned int dim_rgba)
{
    char k[KH_MAXTOK][KH_TOK];
    unsigned int n = kh_split(s, k), i, j, g;
    struct mlf_line *first = 0, *l;
    if (!n || !kh_has_key(k, n)) return 0;
    g = mlf_group();
    for (i = 0; i < n; i++) {
        int cx = 78 + (int)(((2u * i + 1u) * 484u) / (2u * n)), w = 0, x;
        struct kh_seg sg;
        const char *pre = 0;
        if (!kh_parse(k[i], &sg)) {
            l = mlf_add(cx - (int)mlf_width(k[i], MLF_BODY) / 2, y, MLF_BODY, dim_rgba, 0, k[i]);
            if (!l) goto fail;
            l->grp = (unsigned char)g; if (!first) first = l;
            continue;
        }
        if (sg.pre == KH_P_HOLD) pre = "Hold "; else if (sg.pre == KH_P_DOUBLE) pre = "Double ";
        if (pre) w += (int)mlf_width(pre, MLF_BODY);
        for (j = 0; j < sg.n; j++) w += (j ? 6 : 0) + (sg.key[j] == KH_START ? mlf_shape[MLF_S_PILL - 1].w : mlf_shape[MLF_S_DISC - 1].w);
        w += 8 + (int)mlf_width(sg.act, MLF_BODY);
        x = cx - w / 2;
        if (pre) {
            l = mlf_add(x, y, MLF_BODY, dim_rgba, 0, pre);
            if (!l) goto fail;
            l->grp = (unsigned char)g; if (!first) first = l;
            x += (int)mlf_width(pre, MLF_BODY);
        }
        for (j = 0; j < sg.n; j++) {
            unsigned int key = sg.key[j], sh = key == KH_START ? MLF_S_PILL : MLF_S_DISC, ink = kh_ink_rgb(key), sw = mlf_shape[sh - 1].w;
            const char *lt = kh_letter(key);
            if (j) x += 6;
            l = mlf_add_shape(x, y + 1, sh, (kh_plate_rgb(key) << 8) | 0xFFu);
            if (!l) goto fail;
            l->grp = (unsigned char)g; if (!first) first = l;
            l = mlf_add(x + (int)(sw - mlf_width(lt, MLF_SMALL)) / 2, y + 2, MLF_SMALL, (ink << 8) | 0xFFu, 0, lt);       /* the letter centred on the badge (14 px caps centre 9 below the line top, the badge's centre 11) */
            if (!l) goto fail;
            l->grp = (unsigned char)g;
            x += (int)sw;
        }
        l = mlf_add(x + 8, y, MLF_BODY, dim_rgba, 0, sg.act);
        if (!l) goto fail;
        l->grp = (unsigned char)g;
    }
    return first;
fail:
    if (first) mlf_free(first);
    return 0;
}
#endif

#ifndef MLF_HOST
static unsigned char mlf_tobj[32] __attribute__((aligned(32)));
static const unsigned int mlf_ident[12] = { 0x3F800000u, 0, 0, 0,  0, 0x3F800000u, 0, 0,  0, 0, 0x3F800000u, 0 };
/* GX orthographic projection: x 0..640 -> -1..1, y 0..480 (down) -> 1..-1, z: near 0, far 2 (the quads sit at z = -1: clip z -0.5) */
static const unsigned int mlf_ortho[16] = { 0x3B4CCCCDu, 0, 0, 0xBF800000u,   0, 0xBB888889u, 0, 0x3F800000u,   0, 0, 0xBF000000u, 0xBF800000u,   0, 0, 0, 0x3F800000u };

/* a pill bar_h (MLF_S_BCAP) high from x0 to x1 with its top at y: two half discs and a solid middle, in colour rgba (inside mlf_draw: the GX state is set) */
static void mlf_pill(int x0, int y, int x1, unsigned int rgba)
{
    volatile unsigned short *f16 = (volatile unsigned short *)0xCC008000u;
    const struct mlf_g *c = &mlf_shape[MLF_S_BCAP - 1], *so = &mlf_shape[MLF_S_SOLID - 1];
    int r = (int)c->w / 2, y1 = y + (int)c->h;
    unsigned int su = (unsigned int)so->u + 2u, sv = (unsigned int)so->v_lo + 2u;
    if (x1 - x0 < 2 * r) x1 = x0 + 2 * r;
    ((void (*)(int, const void *))0x803401EC)(1, &rgba);
    ((void (*)(int, int, int))0x8033D0DC)(0x80, 0, 12);
    f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y; f16[0] = 0xFFFFu; f16[0] = c->u; f16[0] = c->v_lo;
    f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = c->v_lo;
    f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = (unsigned short)(c->v_lo + c->h);
    f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = c->u; f16[0] = (unsigned short)(c->v_lo + c->h);
    f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y; f16[0] = 0xFFFFu; f16[0] = (unsigned short)su; f16[0] = (unsigned short)sv;
    f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(su + 4u); f16[0] = (unsigned short)sv;
    f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(su + 4u); f16[0] = (unsigned short)(sv + 4u);
    f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)su; f16[0] = (unsigned short)(sv + 4u);
    f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = c->v_lo;
    f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + c->w); f16[0] = c->v_lo;
    f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + c->w); f16[0] = (unsigned short)(c->v_lo + c->h);
    f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = (unsigned short)(c->v_lo + c->h);
}
static void mlf_draw(void *gobj, int pass)
{
    volatile unsigned short *f16 = (volatile unsigned short *)0xCC008000u;
    volatile unsigned int *f32 = (volatile unsigned int *)0xCC008000u;
    unsigned int pv[7], m[16], i;
    (void)gobj;
    if (pass != 2 || !mlf_used) return;
    ((void (*)(void *))0x80341390)(pv);                                        /* GXGetProjectionv: the menu camera's, put back below */
    ((void (*)(const void *, int))0x803412BC)(mlf_ortho, 1);                   /* GXSetProjection(ORTHOGRAPHIC) */
    ((void (*)(int))0x80341510)(0);                                            /* GXSetCurrentMtx(PNMTX0) */
    ((void (*)(const void *, int))0x80341494)(mlf_ident, 0);                   /* GXLoadPosMtxImm(identity) */
    ((void (*)(int, int, int, int, int, int))0x8033C8A8)(0, 1, 4, 60, 0, 125); /* the rest as spr_emit (ml_glue.c): tex gen, channels, cull, vertex descriptors ... */
    ((void (*)(int))0x803623D0)(0);
    ((void (*)(void))0x8033C3C8)();
    ((void (*)(int))0x8033D350)(0);
    ((void (*)(int))0x8033CB78)(1);
    ((void (*)(int))0x80340790)(1);
    ((void (*)(int, int, int, int, int))0x8033C414)(0, 9, 1, 3, 0);            /* POS: XYZ, s16, no fraction: pixels */
    ((void (*)(int, int))0x8033BF00)(9, 1);
    ((void (*)(int, int, int, int, int))0x8033C414)(0, 13, 1, 2, 8);           /* TEX0: ST, u16, 8 fraction bits: texel / 256 */
    ((void (*)(int, int))0x8033BF00)(13, 1);
    ((void (*)(int, int, int, int))0x803405F0)(0, 0, 0, 255);
    ((void (*)(int, int, int, int, int))0x8033FF68)(0, 15, 15, 15, 2);         /* colour: C0 (the line's) */
    ((void (*)(int, int, int, int, int))0x8033FFE8)(0, 7, 4, 1, 7);            /* alpha: TEXA x A0 */
    ((void (*)(int, int, int, int, int, int))0x8034006C)(0, 0, 0, 0, 1, 0);
    ((void (*)(int, int, int, int, int, int))0x8034012C)(0, 0, 0, 0, 1, 0);
    ((void (*)(int, int, int, int))0x80340C3C)(1, 4, 5, 0);                    /* BLEND, SRCALPHA, INVSRCALPHA */
    ((void (*)(int, int, int))0x80340DC0)(0, 7, 0);                            /* no Z */
    ((void (*)(int))0x80340D40)(1);
    ((void (*)(int))0x80340D80)(0);
    ((void (*)(void *, const void *, int, int, int, int, int, int))0x8033E9B0)(mlf_tobj, mlf_tex, (int)MLF_TEX_W, (int)MLF_TEX_H, 0, 0, 0, 0);
    ml_initlod(mlf_tobj);                                                      /* NEAREST: texel = pixel */
    ((void (*)(void *, int))0x8033EFD0)(mlf_tobj, 0);
    for (i = 0; i < 2u * MLF_ROWS; i++) {                                      /* two passes: the row pills first (under every text), then the rest */
        const struct mlf_line *l = &mlf_l[i % MLF_ROWS];
        unsigned int k, q = 0;
        int x;
        if (!l->used || ((l->shape == MLF_ROWPILL) != (i < MLF_ROWS))) continue;
        ((void (*)(int, const void *))0x803401EC)(1, &l->rgba);                 /* the line's colour (a bar changed it) */
        if (l->shape == MLF_ROWPILL) {                                         /* left half disc, solid middle, right half disc */
            const struct mlf_g *c = &mlf_shape[MLF_S_CAP - 1], *so = &mlf_shape[MLF_S_SOLID - 1];
            int x0, y0, x1, y1, r = (int)c->w / 2;
            unsigned int su = (unsigned int)so->u + 2u, sv = (unsigned int)so->v_lo + 2u;
            if (!mlf_rowpill_box(l, &x0, &y0, &x1, &y1)) continue;
            ((void (*)(int, int, int))0x8033D0DC)(0x80, 0, 12);
            f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = c->u; f16[0] = c->v_lo;
            f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = c->v_lo;
            f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = (unsigned short)(c->v_lo + c->h);
            f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = c->u; f16[0] = (unsigned short)(c->v_lo + c->h);
            f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)su; f16[0] = (unsigned short)sv;
            f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(su + 4u); f16[0] = (unsigned short)sv;
            f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(su + 4u); f16[0] = (unsigned short)(sv + 4u);
            f16[0] = (unsigned short)(x0 + r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)su; f16[0] = (unsigned short)(sv + 4u);
            f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = c->v_lo;
            f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + c->w); f16[0] = c->v_lo;
            f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + c->w); f16[0] = (unsigned short)(c->v_lo + c->h);
            f16[0] = (unsigned short)(x1 - r); f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(c->u + r); f16[0] = (unsigned short)(c->v_lo + c->h);
            continue;
        }
        if (l->shape) {                                                        /* a badge: one quad */
            const struct mlf_g *g = &mlf_shape[l->shape - 1];
            int x0 = l->x, y0 = l->y, x1 = x0 + g->w, y1 = y0 + g->h;
            ((void (*)(int, const void *))0x803401EC)(1, &l->rgba);
            ((void (*)(int, int, int))0x8033D0DC)(0x80, 0, 4);
            f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = g->u; f16[0] = g->v_lo;
            f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(g->u + g->w); f16[0] = g->v_lo;
            f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(g->u + g->w); f16[0] = (unsigned short)(g->v_lo + g->h);
            f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = g->u; f16[0] = (unsigned short)(g->v_lo + g->h);
            continue;
        }
        x = l->x;
        if (l->bar_n) {                                                      /* the drawn bar (pill shaped, centred on the text's capitals): a dim pill track, the full part a pill in the line's colour */
            const struct mlf_g *hg = &mlf_glyph[l->size]['H' - 32];
            unsigned int track = (l->rgba & 0xFFFFFF00u) | 0x48u, w = MLF_BAR_W, fw = (MLF_BAR_W * (unsigned int)l->bar_fill) / l->bar_n;
            int bh = (int)mlf_shape[MLF_S_BCAP - 1].h, by = (2 * (l->y + (int)hg->y0) + (int)hg->h - bh + 1) / 2;
            mlf_pill(x, by, x + (int)w, track);
            if (fw) mlf_pill(x, by, x + ((int)fw < bh ? bh : (int)fw), l->rgba);
            x += (int)w + 12;
        }
        for (k = 0; k < l->n; k++) { unsigned int c = (unsigned char)l->s[k]; if (c > 32u && c <= 126u && mlf_glyph[l->size][c - 32u].w) q++; }
        if (!q) continue;
        ((void (*)(int, const void *))0x803401EC)(1, &l->rgba);                 /* GXSetTevColor(TEVREG0) */
        ((void (*)(int, int, int))0x8033D0DC)(0x80, 0, (int)(q * 4u));         /* GXBegin(QUADS) */
        for (k = 0; k < l->n; k++) {
            unsigned int c = (unsigned char)l->s[k];
            const struct mlf_g *g = &mlf_glyph[l->size][c >= 32u && c <= 126u ? c - 32u : '?' - 32u];
            if (c > 32u && c <= 126u && g->w) {
                int x0 = x + g->x0, y0 = l->y + g->y0, x1 = x0 + g->w, y1 = y0 + g->h;
                unsigned int u0 = (unsigned int)g->u << 8, v0 = ((unsigned int)g->v_lo | (unsigned int)g->v_hi << 8) << 8, u1 = u0 + ((unsigned int)g->w << 8), v1 = v0 + ((unsigned int)g->h << 8);
                f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(u0 >> 8); f16[0] = (unsigned short)(v0 >> 8);
                f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y0; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(u1 >> 8); f16[0] = (unsigned short)(v0 >> 8);
                f16[0] = (unsigned short)x1; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(u1 >> 8); f16[0] = (unsigned short)(v1 >> 8);
                f16[0] = (unsigned short)x0; f16[0] = (unsigned short)y1; f16[0] = 0xFFFFu; f16[0] = (unsigned short)(u0 >> 8); f16[0] = (unsigned short)(v1 >> 8);
            }
            x += g->adv;
        }
    }
    (void)f32;
    for (i = 0; i < 16; i++) m[i] = 0;                                          /* the menu camera's projection back (GXGetProjectionv: type, then six values) */
    if (pv[0] == 0) {                                                          /* PERSPECTIVE: [0][0] [0][2] [1][1] [1][2] [2][2] [2][3], [3][2] = -1 */
        m[0] = pv[1]; m[2] = pv[2]; m[5] = pv[3]; m[6] = pv[4]; m[10] = pv[5]; m[11] = pv[6]; m[14] = 0xBF800000u;
        ((void (*)(const void *, int))0x803412BC)(m, 0);
    } else {                                                                   /* ORTHOGRAPHIC: [0][0] [0][3] [1][1] [1][3] [2][2] [2][3], [3][3] = 1 */
        m[0] = pv[1]; m[3] = pv[2]; m[5] = pv[3]; m[7] = pv[4]; m[10] = pv[5]; m[11] = pv[6]; m[15] = 0x3F800000u;
        ((void (*)(const void *, int))0x803412BC)(m, 1);
    }
}
#endif
#endif
