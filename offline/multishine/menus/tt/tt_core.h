/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tt: OUR LINE on the title screen.  Plain C89, no Melee address, no SDK call: the host test (tests/tt_test.c) runs it.
 *
 * tt_compose draws the Multishine mark and one line of text into a 496 x 48 I4 image (the shape of the retail notice's image, so a copy of the notice's quad shows it
 * at the notice's texel density), the group (mark, gap, text) centred on a column.  The glyphs are Melee's own menu font read from RAM (the glyph atlas, as
 * ml_font_label).  The console glue (tt_glue.c) puts the image on a quad of its own; the notice is never touched.
 * I4 layout (GX): 8 x 8 texel tiles of 32 bytes, row major; inside a tile 4 bytes per row, two texels per byte, the left one in the high nibble.  The texture is
 * drawn with alpha = intensity (TObj flags 0x00340010: colour and alpha MODULATE), so a texel's nibble is the coverage of white.
 */
#ifndef TT_CORE_H
#define TT_CORE_H

#define TT_W 496u                       /* width of our image (texels): the same as the retail notice's, so the quad copy has the notice's shape */
#define TT_IH 48u                       /* its rows */
#define TT_BYTES (TT_W * TT_IH / 2u)    /* I4: two texels per byte */
#define TT_PLANE_H 24u                  /* rows of the scratch plane one text line is composed in */
#define TT_PLANE_BYTES (TT_W * TT_PLANE_H)
#define TT_MAXCH 96u                    /* characters per line */
#define TT_FONT_ATLAS 0x8040CD40u       /* HSD_SisLib_FontAtlas (main.dol data), I4 32 x 32 glyphs, 512 bytes each: 0-9, A-Z, a-z, punctuation from 230 (same as ML_FONT_ATLAS) */

struct tt_line {
    const char *text;
    unsigned char cap;                  /* cap height in texels (glyph rows 2..28 of the atlas cell), 5..16 */
    unsigned char y;                    /* row of the cap top inside the image */
};

struct tt_layout {
    unsigned text_x, text_w;            /* left edge of the lines and the widest a line may get (wider ones are squeezed horizontally) */
    const struct tt_line *lines;
    unsigned nlines;
    const unsigned char *logo;          /* 4 bit coverage, logo_w / 2 bytes per row, left texel in the high nibble; 0 = no logo */
    unsigned logo_w, logo_h, logo_x, logo_y, logo_gap;
    const char *right;                  /* 0 = none; else a short dim string right aligned to column right_edge (the build number, 'b475'): beside the first line's group on its baseline when it fits, else on row right_y */
    unsigned right_cap, right_y, right_edge;
    unsigned centre;                    /* nonzero: logo + gap + first line are centred on this column (text_x and logo_x are then computed) */
    unsigned part;                      /* 0 both (default); 1 the text only (the logo's room is kept); 2 the logo only: the mark is its own quad on the title (tinted white / cyan) */
};

/* dst (TT_BYTES) <- our image: the logo and the lines, composed from the menu font read from RAM.  plane: TT_PLANE_BYTES of scratch.
   Returns 0 on success, -1 bad layout, -2 a character the font path cannot draw, -3 a line with no glyph. */
int tt_compose(unsigned char *dst, const unsigned char *atlas, const struct tt_layout *L, unsigned char *plane);

/* the 4 bit texel at (x, y) of an I4 image of width w (a multiple of 8), and the matching store */
unsigned tt_get4(const unsigned char *img, unsigned w, unsigned x, unsigned y);
void tt_put4(unsigned char *img, unsigned w, unsigned x, unsigned y, unsigned v);

/* FNV-1a 32 of n bytes (host test goldens, console proof word) */
unsigned tt_fnv(const unsigned char *p, unsigned n);

/* "Checking for update..." on the title: the hub overlay's update check word (hub_st[HB_UT_STATE] of hub_res.c: state | enabled << 16 | scene << 28) says the check is RUNNING:
   enabled == 1, scene 0 (title), state 1..3 (hello or one of the two UPD_CHECKs; 0 = waiting for link / setup / a retry, 4 = done: no text).  The text takes the place of the link line while it is true. */
static __inline__ unsigned tt_check_running(unsigned w)
{
    unsigned st = w & 0xFFu;
    return ((w >> 16) & 3u) == 1u && (w >> 28) == 0u && st >= 1u && st <= 3u;
}
#define TT_CHK_TEXT "Checking for update..."
#define TT_CHK_CAP 10
#define TT_CHK_Y 29
#endif
