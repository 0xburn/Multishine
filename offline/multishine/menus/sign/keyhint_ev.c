/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * keyhint_ev: the ONE copy of the key-badge text maker of the Event-view screens (keyhint_ev.h).  Until OPTIMIZATION_PASS_2 item 1 the bodies lived in the header
 * and every unit (gnb_ev.c, gnt_ev.c, frd_ev.c) carried its own instantiation of key_tok / key_line and of keyhint.h's parser and layout (3 x 2 KB + 3 x 0.9 KB of
 * sign.ovl).  The unit's slot access goes through struct kev_ops.  Soft-float: every float add goes through gnbev_madd (gnb_sis.S), as in the units.
 */
typedef unsigned int u32;
#include "keyhint.h"
#define KEV_IMPL
#include "keyhint_ev.h"

void gnbev_madd(float *out, const float *a, const float *b, const float *c);
static const float ONE = 1.0f;
static float add(float a, float b) { float o; gnbev_madd(&o, &a, &ONE, &b); return o; }
static float kev_fq(int v) { union { u32 u; float f; } c; c.u = tp_f2(v, 16); return c.f; }               /* Q16 -> float (integer arithmetic only) */
static float kev_fe(unsigned e4) { return kev_fq((int)(e4 * 6554u / 1000u)); }                           /* a font x 10000 -> the float */
static u32 kev_ink(unsigned k) { return (kh_ink_rgb(k) << 8) | 0xFFu; }

/* one token of a key line: cx = the pair's centre (align 1) or left edge (align 0) in text units; tag = a position stamp (0 is none: use any non-zero); ty = the text y; fe = the action's font x 10000 */
void kev_key_tok(const struct kev_ops *o, unsigned act, unsigned lb, const char *tok, float cx, int tag, int align, float ty, unsigned fe, u32 col)
{
    struct kh_seg g;
    struct kh_pair p;
    float font = kev_fe(fe);
    unsigned i, pl = 0, ok = 1;
    int left;
    if (!tok[0]) { o->xs(act, cx, ty, font, col, 0, ""); for (i = 0; i < 3u; i++) o->xs(lb + i, cx, ty, font, col, 0, ""); return; }
    if (!kh_parse(tok, &g)) { for (i = 0; i < 3u; i++) o->xs(lb + i, cx, ty, font, col, 0, ""); o->xs(act, cx, ty, font, col, align ? 1 : 0, tok); return; }
    kh_pair_build(&g, fe, &p);
    left = align ? -(p.w / 2) : 0;
    for (i = 0; i < p.n; i++) {
        const struct kh_item *it = &p.it[i];
        if (it->kind == KH_I_PLATE) {
            unsigned sl = lb + pl++;
            *o->made = 0; *o->tag = tag + left + it->x;
            o->xs(sl, add(cx, kev_fq(left + it->x)), add(ty, kev_fq(it->dy)), kev_fe(it->fe4), kev_ink(it->key), 1, it->s);
            *o->tag = 0;
            if (!o->slot_t(sl)) ok = 0;
            else if (*o->made) kh_attach(o->slot_t(sl), it);                                           /* a fresh text: its plate (a kept one has it already) */
        }
    }
    if (!ok) {                                                                                         /* a letter could not be made: the old text, no half badge */
        for (i = 0; i < 3u; i++) o->xs(lb + i, cx, ty, font, col, 0, "");
        o->xs(act, cx, ty, font, col, align ? 1 : 0, tok);
        return;
    }
    for (; pl < 2u; pl++) o->xs(lb + pl, cx, ty, font, col, 0, "");
    o->xs(lb + 2u, cx, ty, font, col, 0, "");
    for (i = 0; i < p.n; i++) {
        const struct kh_item *it = &p.it[i];
        if (it->kind == KH_I_PLATE) continue;
        *o->tag = tag + left + it->x;
        if (it->kind == KH_I_PRE) o->xs(lb + 2u, add(cx, kev_fq(left + it->x)), ty, font, col, 0, it->s);
        else o->xs(act, add(cx, kev_fq(left + it->x)), ty, font, col, 0, it->s);
        *o->tag = 0;
    }
}
/* a whole line of keys spread in equal shares of the width wq (Q16 text units) from x0; slots: acts[k], letters from lb + 3 k.  An empty line clears every slot. */
void kev_key_line(const struct kev_ops *o, const unsigned *acts, unsigned lb, const char *line, float x0, int wq, float ty, unsigned fe, u32 col)
{
    char k[KH_MAXTOK][KH_TOK];
    unsigned n = kh_split(line, k), i;
    union { float f; u32 u; } yb;                                                                       /* the position stamp also carries the line's y: a kept badge text never keeps the OLD line's height (the More popup's B stayed at the list footer height) */
    int ytag;
    yb.f = ty; ytag = (int)(yb.u >> 10) * 4096;
    for (i = 0; i < KH_MAXTOK; i++) {
        if (i < n) { int rel = (wq / (int)(2u * n)) * (int)(2u * i + 1u); kev_key_tok(o, acts[i], lb + 3u * i, k[i], add(x0, kev_fq(rel)), rel + 1 + ytag, 1, ty, fe, col); }
        else kev_key_tok(o, acts[i], lb + 3u * i, "", x0, 0, 1, ty, fe, col);
    }
}
