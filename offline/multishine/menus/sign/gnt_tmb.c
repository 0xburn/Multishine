/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Retail Tourney bracket snapshot model: see gnt_tmb.h.  The rules are the retail advance (fn_8018B090_inline5): the winner's data is copied into the successor's slot, its own slot is
 * emptied, the loser stays put with place 1 (the retail scene shows no 'beaten' look, the overlay draws it). */
#include "gnt_tmb.h"

static void tmb_zero(void *p, unsigned n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }

unsigned tmb_build(struct tmb_state *st, const struct tmb_box *box, unsigned nbox, const struct tmb_ent *ent, unsigned nent, const struct tmb_res *res, unsigned nres)
{
    unsigned i, k = 0, r, sl;
    if (nbox == 0u || nbox > TMB_BOXES) return 1u;
    tmb_zero(st, (unsigned)sizeof *st);
    st->nbox = (unsigned char)nbox;
    for (i = 0; i < nbox; i++) {
        st->pending[i] = 1u;
        for (sl = 0; sl < 2u; sl++) {
            struct tmb_slot *s = &st->s[i][sl];
            s->place = 3u; s->kind = 3u; s->label = 0x3E7u;                    /* an empty slot: retail's 'removed' type and the empty label */
            if (sl ? box[i].occ1 : box[i].occ0) {
                if (k >= nent) return 1u;
                s->occ = 1u; s->place = 3u; s->ch = ent[k].ch; s->kind = ent[k].kind; s->costume = ent[k].costume; s->ent = (unsigned char)k;
                s->level = ent[k].level; s->handicap = ent[k].handicap; s->label = ent[k].label;
                k++;
            }
        }
    }
    if (k != nent) return 1u;
    st->nleaf = (unsigned char)k;
    for (r = 0; r < nres; r++) {
        unsigned b = res[r].box, w = 0xFFu, l = 0xFFu;
        if (b >= nbox || !st->pending[b] || !st->s[b][0].occ || !st->s[b][1].occ) return 2u;
        if (st->s[b][0].ent == res[r].winner) { w = 0u; l = 1u; }
        else if (st->s[b][1].ent == res[r].winner) { w = 1u; l = 0u; }
        else return 3u;
        st->pending[b] = 0u;
        st->s[b][l].place = 1u;
        st->s[b][w].place = 0u;
        if (box[b].nxt < nbox) {
            struct tmb_slot *d = &st->s[box[b].nxt][box[b].slot ? 1 : 0];
            *d = st->s[b][w];
            d->occ = 1u; d->place = 3u;
        }
        st->s[b][w].occ = 0u; st->s[b][w].kind = 3u;
    }
    return 0u;
}

unsigned tmb_current(const struct tmb_state *st)
{
    unsigned i;
    for (i = 0; i < st->nbox; i++) if (st->pending[i]) return i;
    return TMB_NONE;
}

unsigned tmb_pow2_table(struct tmb_box *box, unsigned size)
{
    unsigned n, i, start = 0, cnt, r = 0;
    if (size < 4u || size > 64u || (size & (size - 1u))) return 0;
    for (cnt = size >> 1; cnt >= 1u; cnt >>= 1) {                  /* round r holds `cnt` boxes starting at `start`; box i of the round feeds box i / 2 of the next, slot i & 1 */
        for (i = 0; i < cnt; i++) {
            box[start + i].nxt = (unsigned char)(cnt > 1u ? start + cnt + (i >> 1) : start + 1u);
            box[start + i].slot = (unsigned char)(cnt > 1u ? (i & 1u) : 0u);
            box[start + i].occ0 = box[start + i].occ1 = (unsigned char)(r == 0u);
        }
        start += cnt; r++;
    }
    n = start + 1u;                                                 /* the trophy box: the final's successor, nothing in it, terminal */
    box[n - 1u].nxt = (unsigned char)n; box[n - 1u].slot = 0; box[n - 1u].occ0 = box[n - 1u].occ1 = 0;
    return n;
}

unsigned tmb_lb_table(struct tmb_lbox *out, unsigned size)
{
    unsigned rc[12], first[12], nr = 0, r, i, n = 0, step1, c, gf1, gf2, step;
    short prev2[32], cur2[32];                       /* twice the mid-points of the previous / current round */
    unsigned char fed[TMB_BOXES][2];
    if (size != 8u && size != 16u && size != 32u && size != 64u) return 0;
    for (c = size >> 2; c >= 1u; c >>= 1) { rc[nr++] = c; rc[nr++] = c; }
    step1 = 608u / rc[0];
    step = size == 64u ? 30u : (300u / (nr + 2u) < 70u ? 300u / (nr + 2u) : 70u);
    for (r = 0; r < nr; r++) {
        short top = (short)(r == 0u ? 372 : 372 - (int)step * (int)r), hgt = (short)(r == 0u ? 40 : (int)step);
        first[r] = n;
        for (i = 0; i < rc[r]; i++) {
            short left, width;
            if (r == 0u) { left = (short)(16u + step1 * i); width = (short)(size == 64u ? 20u : (step1 * 20u / 38u < 60u ? step1 * 20u / 38u : 60u)); }
            else if (r & 1u) { left = (short)(prev2[i] >> 1); width = (short)(size == 64u ? 16 : 40); }
            else { left = (short)(prev2[2u * i] >> 1); width = (short)((prev2[2u * i + 1u] >> 1) - left); }
            out[n].left = left; out[n].width = width; out[n].top = top; out[n].height = hgt;
            out[n].nxt = 0; out[n].slot = 0; out[n].occ0 = out[n].occ1 = 0;
            cur2[i] = (short)(2 * left + width);
            n++;
        }
        for (i = 0; i < rc[r]; i++) prev2[i] = cur2[i];
    }
    gf1 = n; gf2 = n + 1u;
    {   short cx = (short)(prev2[0] >> 1);
        out[gf1].left = (short)(cx - 80); out[gf1].width = 80; out[gf1].top = (short)(372 - (int)step * (int)nr); out[gf1].height = (short)step;
        out[gf2].left = (short)(cx - 40); out[gf2].width = 80; out[gf2].top = (short)(372 - (int)step * (int)(nr + 1u)); out[gf2].height = (short)step;
    }
    n += 2u;
    if (size != 64u) {                                              /* the small cups are centred on the 640 px screen */
        short lo = 32000, hi = -32000, sh;
        for (i = 0; i < n; i++) { if (out[i].left < lo) lo = out[i].left; if (out[i].left + out[i].width > hi) hi = (short)(out[i].left + out[i].width); }
        sh = (short)((640 - (hi - lo)) / 2 - lo);
        for (i = 0; i < n; i++) out[i].left = (short)(out[i].left + sh);
    }
    for (r = 0; r < nr; r++)
        for (i = 0; i < rc[r]; i++) {
            struct tmb_lbox *b = &out[first[r] + i];
            if (r == nr - 1u) { b->nxt = (unsigned char)gf1; b->slot = 1; }
            else if ((r & 1u) == 0u) { b->nxt = (unsigned char)(first[r + 1u] + i); b->slot = 0; }
            else { b->nxt = (unsigned char)(first[r + 1u] + i / 2u); b->slot = (unsigned char)(i & 1u); }
        }
    out[gf1].nxt = (unsigned char)gf2; out[gf1].slot = 0;
    out[gf2].nxt = (unsigned char)n; out[gf2].slot = 0;
    for (i = 0; i < n; i++) fed[i][0] = fed[i][1] = 0;
    for (i = 0; i < n; i++) if (out[i].nxt < n) fed[out[i].nxt][out[i].slot ? 1 : 0] = 1;
    for (i = 0; i < n; i++) {
        out[i].occ0 = (unsigned char)!fed[i][0];
        out[i].occ1 = (unsigned char)(!fed[i][1] && i != gf2);
    }
    return n;
}
