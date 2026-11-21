/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The bracket tree window (gnt_tree.h).  Strict C89, no libc. */
#include "gnt_tree.h"

static void zero(void *p, unsigned n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }

/* the index (in g->b_m) of the k-th match of a side and round, in id order (the order the wire lists them); GNT_MATCHES = none */
static unsigned tw_at(const struct gnt *g, unsigned side, unsigned round, unsigned k)
{
    unsigned i, n = 0;
    for (i = 0; i < GNT_MATCHES; i++)
        if (g->b_m[i].mid && g->b_m[i].side == side && g->b_m[i].round == round) { if (n == k) return i; n++; }
    return GNT_MATCHES;
}
static unsigned tw_cnt(const struct gnt *g, unsigned side, unsigned round)
{
    unsigned i, n = 0;
    for (i = 0; i < GNT_MATCHES; i++) if (g->b_m[i].mid && g->b_m[i].side == side && g->b_m[i].round == round) n++;
    return n;
}

unsigned gnt_tree_layout(const struct gnt *g, struct gnt_tw *w)
{
    unsigned cnt, first, n0, pw, k, c;
    zero(w, (unsigned)sizeof *w);
    if (!g->b_have) return 0;
    cnt = tw_cnt(g, g->b_side, g->b_round);
    if (!cnt) return 0;
    first = (g->b_sel / GNT_TW_ROWS) * GNT_TW_ROWS;
    if (first >= cnt) first = ((cnt - 1u) / GNT_TW_ROWS) * GNT_TW_ROWS;
    n0 = cnt - first; if (n0 > GNT_TW_ROWS) n0 = GNT_TW_ROWS;
    pw = n0 <= 1u ? 1u : n0 <= 2u ? 2u : 4u;
    w->nc = 1; w->n[0] = (unsigned char)n0;
    w->side[0] = g->b_side; w->rnd[0] = g->b_round;
    for (k = 0; k < n0; k++) { w->mi[0][k] = (unsigned char)tw_at(g, g->b_side, g->b_round, first + k); w->y[0][k] = (unsigned char)((2u * k + 1u) * (GNT_TW_ROWS / pw)); w->lk[0][k] = 0xFFu; }
    w->cur = (unsigned char)(g->b_sel >= first && g->b_sel - first < n0 ? g->b_sel - first : 0u);
    w->pod = (unsigned char)(first / GNT_TW_ROWS + 1u); w->pods = (unsigned char)((cnt + GNT_TW_ROWS - 1u) / GNT_TW_ROWS);
    for (c = 0; c + 1u < GNT_TW_COLS; c++) {
        unsigned nn = 0, any = 0;
        for (k = 0; k < w->n[c]; k++) {
            const struct gnt_match *m = &g->b_m[w->mi[c][k]];
            unsigned d = m->next_win, j;
            if (!d || d > GNT_MATCHES || !g->b_m[d - 1u].mid) continue;
            any = 1;
            for (j = 0; j < nn; j++) if (w->mi[c + 1u][j] == d - 1u) break;
            if (j == nn) {
                if (nn >= GNT_TW_ROWS) continue;
                w->mi[c + 1u][nn] = (unsigned char)(d - 1u); w->y[c + 1u][nn] = 0; nn++;
            }
            w->lk[c][k] = (unsigned char)j;
        }
        if (!any || !nn) break;
        for (k = 0; k < nn; k++) {                                       /* a match sits half way between the matches that feed it (one feeder: level with it) */
            unsigned lo = 255u, hi = 0u, i;
            for (i = 0; i < w->n[c]; i++) if (w->lk[c][i] == k) { if (w->y[c][i] < lo) lo = w->y[c][i]; if (w->y[c][i] > hi) hi = w->y[c][i]; }
            w->y[c + 1u][k] = (unsigned char)((lo + hi) / 2u);
            w->lk[c + 1u][k] = 0xFFu;
        }
        w->n[c + 1u] = (unsigned char)nn; w->nc = (unsigned char)(c + 2u);
        w->side[c + 1u] = g->b_m[w->mi[c + 1u][0]].side; w->rnd[c + 1u] = g->b_m[w->mi[c + 1u][0]].round;
    }
    for (k = 0; k < w->n[w->nc - 1u]; k++) { const struct gnt_match *m = &g->b_m[w->mi[w->nc - 1u][k]]; if (m->next_win && m->next_win <= GNT_MATCHES) w->more = 1; }
    return 1;
}
