/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The one list core of the Mockups hub (sg_mockup.h): list left, detail right, cursor, scrolling and the mock states.  Strict C89, no libc. */
#include "sg_ui.h"

#if SG_MOCKUPS      /* compiled out of release overlays (sg_mockup.h) */
void sgm_init_ln(struct sgm_ln *l) { l->n = 0; l->b[0] = 0; }
struct sgm_ln *sgm_s(struct sgm_ln *l, const char *s)
{
    while (*s && l->n < SG_COLS) l->b[l->n++] = *s++;
    if (*s) sg_cut_count++;
    l->b[l->n] = 0;
    return l;
}
struct sgm_ln *sgm_at(struct sgm_ln *l, unsigned col)
{
    while (l->n < col && l->n < SG_COLS) l->b[l->n++] = ' ';
    l->b[l->n] = 0;
    return l;
}
struct sgm_ln *sgm_u(struct sgm_ln *l, unsigned v)
{
    char t[12], *e = sg_u(t, v);
    *e = 0;
    return sgm_s(l, t);
}

void sgm_put(struct sg_view *v, unsigned r, unsigned attr, const char *s)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (s[c] && c < SG_COLS) { v->t[r][c] = s[c]; c++; }
    if (s[c] || c > SGM_MAX_COL) sg_cut_count++;
    v->t[r][c] = 0;
    v->attr[r] = (unsigned char)attr;
}

void sgm_head(struct sg_view *v, const char *title)
{
    struct sgm_ln l;
    sgm_init_ln(&l);
    sgm_s(&l, title);
    if (l.n >= SGM_TAG_COL) sg_cut_count++;                 /* the tag must never be pushed off */
    sgm_at(&l, SGM_TAG_COL);
    sgm_s(&l, SGM_TAG);
    sgm_put(v, 0, SG_A_HEAD, l.b);
}

unsigned sgm_list_n(const struct sg *g, const struct sgm_kind *k)
{
    unsigned s = g->mk.state;
    if (g->mk.timer || s == SGM_LOADING || s == SGM_UNREACH || s == SGM_EMPTY) return 0;
    return k ? k->n : 1u;         /* k = 0: only asks whether the list is shown */
}

/* cursor up / down with wrap; the window follows the cursor */
void sgm_list_nav(struct sgm *m, unsigned n, unsigned vis, int up, int down)
{
    if (!n) { m->cur = 0; m->top = 0; return; }
    if (m->cur >= n) m->cur = (unsigned char)(n - 1u);
    if (down) m->cur = (unsigned char)((m->cur + 1u) % n);
    if (up) m->cur = (unsigned char)((m->cur + n - 1u) % n);
    if (m->cur < m->top) m->top = m->cur;
    if (m->cur >= m->top + vis) m->top = (unsigned char)(m->cur + 1u - vis);
}

static const char *const SGM_STATE_NAME[SGM_NSTATE] = { "Live", "Loading", "Server Unreachable", "Empty", "All Full", "Spectate Only" };

void sgm_list_draw(const struct sg *g, struct sg_view *v, const struct sgm_kind *k)
{
    const struct sgm *m = &g->mk;
    unsigned n = sgm_list_n(g, k), i, r;
    struct sgm_ln l;
    sgm_head(v, k->title);
    sgm_init_ln(&l);
    k->sub(g, &l);
    sgm_put(v, 1, SG_A_DIM, l.b);
    if (m->timer || m->state == SGM_LOADING) {
        sgm_put(v, SGM_LIST_ROW0 + 1u, SG_A_BODY, "  Loading ...");
        sgm_put(v, SGM_LIST_ROW0 + 3u, SG_A_DIM, "  Mock: nothing is fetched.");
    } else if (m->state == SGM_UNREACH) {
        sgm_put(v, SGM_LIST_ROW0 + 1u, SG_A_BAD, k->unreach);
        sgm_put(v, SGM_LIST_ROW0 + 3u, SG_A_DIM, "  Press Start to try again.");
    } else if (m->state == SGM_EMPTY) {
        sgm_put(v, SGM_LIST_ROW0 + 1u, SG_A_BODY, k->empty1);
        sgm_put(v, SGM_LIST_ROW0 + 3u, SG_A_DIM, k->empty2);
    } else {
        for (r = 0; r < k->vis; r++) {
            i = m->top + r;
            sgm_init_ln(&l);
            if (i < n) {
                sgm_s(&l, i == m->cur ? "- " : "  ");
                k->left(g, i, &l);
                if (l.n > SGM_DETAIL_COL - 2u) sg_cut_count++;          /* a list row may not run into the panel */
            }
            sgm_at(&l, SGM_DETAIL_COL);
            if (m->cur < n) {
                unsigned c0 = l.n;
                k->detail(g, m->cur, r, &l);
                if (l.n == c0) { while (l.n && l.b[l.n - 1u] == ' ') l.n--; l.b[l.n] = 0; }
            }
            if (l.n == SGM_DETAIL_COL) { l.n = 0; l.b[0] = 0; }
            sgm_put(v, SGM_LIST_ROW0 + r, i == m->cur && i < n ? SG_A_HI : SG_A_BODY, l.b);
        }
        if (n > k->vis) {
            sgm_init_ln(&l);
            sgm_s(&l, "  ");
            sgm_u(&l, m->top + 1u); sgm_s(&l, " - "); sgm_u(&l, m->top + k->vis < n ? m->top + k->vis : n);
            sgm_s(&l, " of "); sgm_u(&l, n);
            sgm_put(v, SGM_LIST_ROW0 + k->vis, SG_A_DIM, l.b);
        }
    }
    if (m->msg) sgm_put(v, SGM_MSG_ROW, SG_A_HI, m->msg);
    if (k->nstates < 2u) { sgm_put(v, SG_ROWS - 2u, SG_A_DIM, k->foot); return; }
    sgm_init_ln(&l);
    sgm_s(&l, "Mock state: "); sgm_s(&l, SGM_STATE_NAME[m->state < SGM_NSTATE ? m->state : 0]); sgm_s(&l, "  X: Next State");
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, l.b);
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, k->foot);
}
#endif /* SG_MOCKUPS */
