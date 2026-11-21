/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * keyhint: button badges for the key hints.  Original work.
 *
 * A footer, a toast line or a hint is a string of key tokens separated by TWO spaces ("A: Ready  Hold B: Leave", "L R: Costume  A: Lock  B: Back").  At render time a token that
 * starts with [Hold ][Double ]<KEY>[ <KEY>]: (KEY = A B X Y Z L R Start) is drawn as the badge(s) in the GameCube colours, then the action text; any other token stays plain text.
 * "A:" in the strings of the code stays exactly as it is (another helper rewords the actions); the old text is also the fallback of every renderer that cannot draw a badge.
 *
 * A badge is ONE text object: the key's letter, centred, with the rounded plate of toast_pill.h behind it in the key's colour (tp_attach_rgb_at).  This file holds the parts every
 * renderer shares and nothing that touches Melee: the parser, the colours, and the layout of one pair "[Hold] (badge)(badge) action" in Q16 text units (1 unit = 18.6 px of the
 * 640 x 528 frame across, the unit of every Melee text), measured at the font of the action text.  A renderer turns the items into its own coordinates and makes the texts.
 *   plate height  35.2 x font units (21 px at the font 0.03; a text unit is about 19.9 px)   the letter font 0.8 x font (Start 0.6 x, at least 0.022)   glyph centre 15 x font below the text y
 * Pure integers (the overlays have no float arithmetic), C89.  Host test: tests/keyhint_test.c.
 */
#ifndef KEYHINT_H
#define KEYHINT_H

#define KH_FN static __attribute__((unused))
#ifdef KH_HOST
#ifndef TP_HOST
#define TP_HOST 1                          /* the host tests need only the pure part of the plate header (the glyph table) */
#endif
#endif
#include "toast_pill.h"

enum { KH_A, KH_B, KH_X, KH_Y, KH_Z, KH_L, KH_R, KH_START, KH_NK };
enum { KH_P_NONE, KH_P_HOLD, KH_P_DOUBLE };
#define KH_TOK 44u                         /* characters of one token of a line (with its end) at most */
#define KH_MAXTOK 4u                       /* keys of one line at most (the footer bar holds four) */
#define KH_MAXIT 5u                        /* prefix, two badges, action */

struct kh_seg { unsigned char pre, n, key[2]; const char *act; };

KH_FN const char *kh_letter(unsigned k)
{
    static const char *const L[KH_NK] = { "A", "B", "X", "Y", "Z", "L", "R", "Start" };
    return k < KH_NK ? L[k] : "";
}
/* the GameCube colours our: A green, B red, X and Y light grey, Z purple, Start / L / R grey */
KH_FN unsigned kh_plate_rgb(unsigned k)
{
    static const unsigned P[KH_NK] = { 0x2FA84Fu, 0xD23A32u, 0xC9CCD6u, 0xC9CCD6u, 0x6B4FC8u, 0x8D93A3u, 0x8D93A3u, 0x8D93A3u };
    return k < KH_NK ? P[k] : 0x8D93A3u;
}
KH_FN unsigned kh_ink_is_white(unsigned k) { return k == KH_B || k == KH_Z; }                      /* white letter on red and purple, dark on the rest */
KH_FN unsigned kh_ink_rgb(unsigned k) { return kh_ink_is_white(k) ? 0xFFFFFFu : 0x15171Fu; }

KH_FN int kh_key_at(const char *s, unsigned *len)           /* the key name at s (followed by a space or ':'), or -1 */
{
    unsigned k, n;
    for (k = 0; k < KH_NK; k++) {
        const char *nm = kh_letter(k);
        for (n = 0; nm[n] && s[n] == nm[n]; n++) {}
        if (!nm[n] && (s[n] == ' ' || s[n] == ':')) { *len = n; return (int)k; }
    }
    return -1;
}
/* parse one token; 1 = a key token (g filled), 0 = plain text */
KH_FN unsigned kh_parse(const char *s, struct kh_seg *g)
{
    unsigned len, i;
    int k;
    g->pre = KH_P_NONE; g->n = 0; g->key[0] = g->key[1] = 0; g->act = s;
    if (s[0] == 'H' && s[1] == 'o' && s[2] == 'l' && s[3] == 'd' && s[4] == ' ') { g->pre = KH_P_HOLD; s += 5; }
    else if (s[0] == 'D' && s[1] == 'o' && s[2] == 'u' && s[3] == 'b' && s[4] == 'l' && s[5] == 'e' && s[6] == ' ') { g->pre = KH_P_DOUBLE; s += 7; }
    for (i = 0; i < 2u; i++) {
        k = kh_key_at(s, &len);
        if (k < 0) return 0;
        g->key[g->n++] = (unsigned char)k;
        s += len;
        if (*s == ':') break;
        s++;                                                 /* the space between two keys: "L R:" */
    }
    if (*s != ':') return 0;
    s++;
    while (*s == ' ') s++;
    g->act = s;
    return 1;
}
/* split a line at its double spaces into tokens (the footer's keys); returns their number (at most KH_MAXTOK); a token longer than KH_TOK - 1 characters is cut */
KH_FN unsigned kh_split(const char *f, char (*k)[KH_TOK])
{
    unsigned n = 0, j = 0;
    while (*f && n < KH_MAXTOK) {
        if (f[0] == ' ' && f[1] == ' ') { while (*f == ' ') f++; if (j) { k[n][j] = 0; n++; j = 0; } continue; }
        if (j < KH_TOK - 1u) k[n][j++] = *f;
        f++;
    }
    if (j && n < KH_MAXTOK) { k[n][j] = 0; n++; }
    return n;
}
KH_FN unsigned kh_has_key(char (*k)[KH_TOK], unsigned n)
{
    struct kh_seg g;
    unsigned i;
    for (i = 0; i < n; i++) if (kh_parse(k[i], &g)) return 1;
    return 0;
}

/* the width of a text in pixels at the font 0.03: toast_pill.h's one glyph table (the menu font is proportional; within about 8 percent of the console frames; the same as gnb_ev.c est_w) */
KH_FN unsigned kh_w(const char *s) { return TP_PX_OF(tp_text_gw(s), TP_LIST_PX * 100u); }
/* Q16 text units */
KH_FN int kh_wq(const char *s, unsigned f_e4) { return (int)((kh_w(s) * f_e4 * 1174u) / 100u); }          /* the width of s at the font f_e4 (the font x 10000: 0.028 = 280) */
KH_FN int kh_hq(unsigned f_e4) { return (int)(f_e4 * 230u); }                                           /* the plate height: 21 px at the font 0.03 (a text unit is about 19 px) */
KH_FN int kh_cq(unsigned f_e4) { return (int)(f_e4 * 98u); }                                           /* the glyph centre below the text's y */
KH_FN unsigned kh_lfont(unsigned k, unsigned f_e4)                                                      /* the letter's font: 0.8 x (Start 0.6 x), never below 0.022 (the menu font garbles smaller) */
{
    unsigned v = (k == KH_START ? f_e4 * 6u : f_e4 * 8u) / 10u;
    if (v < 220u) v = f_e4 < 220u ? f_e4 : 220u;
    return v;
}

struct kh_item {
    unsigned char kind, key;
    unsigned short fe4;       /* a plate's letter font (x 10000) */
    int x, w;                 /* PRE / ACT: left edge; PLATE: centre; Q16 units from the pair's left edge.  w = plate width / text width */
    int dy, py, ph;           /* a plate: the letter text's y below the action's y, the plate's top below the letter's y, the plate height */
    const char *s;
};
enum { KH_I_PRE, KH_I_PLATE, KH_I_ACT };
struct kh_pair { struct kh_item it[KH_MAXIT]; unsigned n; int w; };

/* lay out "[Hold] (key)[(key)] action" for the font f_e4; every x is from the pair's left edge, w = the whole width */
KH_FN void kh_pair_build(const struct kh_seg *g, unsigned f_e4, struct kh_pair *o)
{
    int h = kh_hq(f_e4), gap = h / 3, x = 0, i;
    o->n = 0;
    if (g->pre) {
        struct kh_item *p = &o->it[o->n++];
        p->kind = KH_I_PRE; p->key = 0; p->fe4 = 0; p->x = x; p->s = g->pre == KH_P_HOLD ? "Hold" : "Double"; p->w = kh_wq(p->s, f_e4); p->dy = p->py = p->ph = 0;
        x += p->w + h / 3;
    }
    for (i = 0; i < (int)g->n; i++) {
        struct kh_item *p = &o->it[o->n++];
        unsigned fl = kh_lfont(g->key[i], f_e4);
        int w = h;
        if (g->key[i] == KH_START) w = kh_wq("Start", fl) + (h * 55) / 100;
        p->kind = KH_I_PLATE; p->key = g->key[i]; p->fe4 = (unsigned short)fl; p->s = kh_letter(g->key[i]);
        p->w = w; p->x = x + w / 2; p->ph = h;
        p->dy = kh_cq(f_e4) - kh_cq(fl);                       /* the letter's glyph centre on the action's: its text sits this far lower */
        p->py = kh_cq(fl) - h / 2;                             /* the plate's top, from the letter's y: its centre on the glyph centre */
        x += w + (i + 1 < (int)g->n ? h / 6 : gap);
    }
    {
        struct kh_item *p = &o->it[o->n++];
        p->kind = KH_I_ACT; p->key = 0; p->fe4 = 0; p->x = x; p->s = g->act; p->w = kh_wq(g->act, f_e4); p->dy = p->py = p->ph = 0;
        x += p->w;
    }
    o->w = x;
}

#ifndef KH_HOST
/* the plate behind a badge's letter text (the text is centred at the plate's centre): call after the text is made */
KH_FN void kh_attach(void *t, const struct kh_item *p)
{
    tp_attach_rgb_at(t, kh_plate_rgb(p->key), -(p->w / 2), p->py, p->w, p->ph);
}
#endif
#endif
