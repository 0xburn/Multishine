/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Key hints with button badges on the Event-view screens (gnb_ev.c Game Night / Standings, gnt_ev.c Tournament, frd_ev.c Friends): the part of keyhint.h that makes texts.  Original work.
 * A token "Hold B: Leave" becomes  Hold (B) Leave: the action text, the letter text of each key (centred, with its coloured rounded plate of toast_pill.h behind it) and the prefix text.  A line
 * of keys is spread in equal shares of a width, each pair centred in its share.  A badge whose letter text cannot be made (the SIS pool guard) falls back
 * to the old text "B: Leave" in the key's action slot.
 *
 * The bodies are ONE unit, keyhint_ev.c (before, every unit instantiated them).  A unit includes this header AFTER it has (all static): u32, the slot type with ->t, and
 *   KEV_SLOT(i)                          the slot i (a struct tslot) of the unit's key texts
 *   kev_xs(i, x, y, font, col, align, str)   set slot i to this text (made again when the string, colour or kev_tag changed; "" frees it); a fresh text raises kev_made
 *   kev_made, kev_tag                    the unit's counters (a position stamp: a key that moves is made again)
 * and then calls key_tok / key_line as before (macros over the shared bodies with the unit's ops table).  Slots of a key: its action slot and three from `lb` (letter 0, letter 1, prefix).
 */
#ifndef KEYHINT_EV_H
#define KEYHINT_EV_H

/* the Settings family's footer span (ml_glue.c FT_W: 26 units = x 78..562 px, Q16 text units) and its left edge from the number column (x 78 px): the ONE footer x span of every Event-view screen (one footer style) */
#define FOOT_WQ 1703936
#define FOOT_DX -3.441f

struct kev_ops {
    void (*xs)(unsigned i, float x, float y, float font, u32 col, int align, const char *str);     /* the unit's kev_xs */
    void *(*slot_t)(unsigned i);                                                                 /* KEV_SLOT(i).t (0 = no text) */
    u32 *made;                                                                                   /* &kev_made */
    int *tag;                                                                                    /* &kev_tag */
};
void kev_key_tok(const struct kev_ops *o, unsigned act, unsigned lb, const char *tok, float cx, int tag, int align, float ty, unsigned fe, u32 col);
void kev_key_line(const struct kev_ops *o, const unsigned *acts, unsigned lb, const char *line, float x0, int wq, float ty, unsigned fe, u32 col);

#ifndef KEV_IMPL
static float kev_fq(int v) { union { u32 u; float f; } c; c.u = tp_f2(v, 16); return c.f; }               /* Q16 -> float (integer arithmetic only); gnb_ev.c TXI uses it */
static void *kev_slot_t_(unsigned i) { return KEV_SLOT(i).t; }
static const struct kev_ops kev_ops_ = { kev_xs, kev_slot_t_, &kev_made, &kev_tag };
#define key_tok(act, lb, tok, cx, tag, align, ty, fe, col) kev_key_tok(&kev_ops_, (act), (lb), (tok), (cx), (tag), (align), (ty), (fe), (col))
#define key_line(acts, lb, line, x0, wq, ty, fe, col) kev_key_line(&kev_ops_, (acts), (lb), (line), (x0), (wq), (ty), (fe), (col))
#endif
#endif
