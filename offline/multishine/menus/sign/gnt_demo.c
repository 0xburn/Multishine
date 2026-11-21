/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Demo cups (gnt_demo.h).  Strict C89, no libc. */
#include "gnt_demo.h"

static const char *const NAMES[16] = { "Nigzel", "Falcon", "Marth", "Puff", "Wizzrobe", "Mango", "Zain", "Hungrybox", "Axe", "Plup", "Cody", "Aklo", "Soonsay", "Moky", "Ginger", "Trif" };

static void put_name(char *d, unsigned i)          /* entrant i (0 based) -> at most 12 characters */
{
    const char *s = NAMES[i & 15u];
    unsigned k = 0;
    while (s[k] && k < 9u) { d[k] = s[k]; k++; }
    if (i >= 16u) { d[k++] = (char)('0' + (i / 16u)); }
    d[k] = 0;
}
static unsigned hash(unsigned x) { x = x * 2654435761u + 0x9E3779B9u; x ^= x >> 13; x *= 0x85EBCA6Bu; x ^= x >> 16; return x; }

unsigned gnt_demo_size(unsigned id) { return id == 1u ? 4u : id == 2u ? 16u : id == 3u ? 64u : 4u; }
unsigned gnt_demo_done(unsigned id) { return id == 1u ? 2u : id == 2u ? 18u : id == 3u ? 76u : 2u; }
unsigned gnt_demo_list(struct gnt_ent *e)
{
    static const char *const NM[GNT_DEMO_CUPS] = { "Demo 4", "Demo 16", "Demo 64"
#ifdef GNT_DEMO_MANY
        , "Cup 4", "Cup 5", "Cup 6", "Cup 7", "Cup 8", "Cup 9", "Cup 10", "Cup 11", "Cup 12"
#endif
    };
    unsigned i, k;
    for (i = 0; i < GNT_DEMO_CUPS; i++) {
        unsigned char *b = (unsigned char *)&e[i];
        for (k = 0; k < sizeof e[i]; k++) b[k] = 0;
        for (k = 0; NM[i][k] && k < GNT_NAME; k++) e[i].name[k] = NM[i][k];
        e[i].id = (unsigned short)(i + 1u); e[i].state = GNT_S_RUNNING; e[i].format = GNT_FMT_DOUBLE; e[i].cap = (unsigned char)gnt_demo_size(i + 1u); e[i].joined = e[i].cap;
        e[i].flags = (unsigned char)(GNT_F_JOINED | GNT_F_CHECKIN_ON); e[i].you = GNT_Y_IN; e[i].round = 1; e[i].rounds = (unsigned char)gnt_round_count(e[i].cap);
    }
    return GNT_DEMO_CUPS;
}

struct dm { unsigned char side, round, a, b, state, wa, wb, win, bo, wt, wts, lt, lts; };
void gnt_demo_bracket(struct gnt *g, unsigned size, unsigned done, unsigned me)
{
    static struct dm M[GNT_MATCHES + 1u];
    static unsigned char order[64];
    unsigned k, r, i, j, n = 0, mid, rr, S = size, nl, cnt, played = 0, pass, gf1, gf2, W[8], L[16];
    char nm[GNT_NAME + 1];
    if (S < 4u) S = 4u;
    if (S > 64u) S = 64u;
    k = gnt_round_count(S);
    for (i = 0; i <= GNT_MATCHES; i++) { M[i].side = 0; M[i].round = 0; M[i].a = M[i].b = GNT_NONE; M[i].state = GNT_M_WAIT; M[i].wa = M[i].wb = M[i].win = 0; M[i].bo = 3; M[i].wt = M[i].lt = 0; M[i].wts = M[i].lts = 0; }
    /* ids: winners rounds, then the losers rounds (round pairs), then the grand final and its reset: the server's order */
    mid = 1;
    for (r = 1; r <= k; r++) { W[r] = mid; for (i = 0; i < (S >> r); i++, mid++) { M[mid].side = GNT_SIDE_W; M[mid].round = (unsigned char)r; } }
    nl = 2u * (k - 1u);
    for (j = 1; j < k; j++) {
        cnt = S >> (j + 1u);
        for (rr = 2u * j - 1u; rr <= 2u * j; rr++) { L[rr] = mid; for (i = 0; i < cnt; i++, mid++) { M[mid].side = GNT_SIDE_L; M[mid].round = (unsigned char)rr; } }
    }
    gf1 = mid++; gf2 = mid++;
    M[gf1].side = GNT_SIDE_F; M[gf1].round = 1; M[gf2].side = GNT_SIDE_F; M[gf2].round = 2;
    /* routing */
    for (r = 1; r <= k; r++)
        for (i = 0; i < (S >> r); i++) {
            struct dm *m = &M[W[r] + i];
            if (r < k) { m->wt = (unsigned char)(W[r + 1u] + i / 2u); m->wts = (unsigned char)(i % 2u); } else { m->wt = (unsigned char)gf1; m->wts = 0; }
            if (k == 1u) { m->lt = (unsigned char)gf1; m->lts = 1; }
            else if (r == 1u) { m->lt = (unsigned char)(L[1] + i / 2u); m->lts = (unsigned char)(i % 2u); }
            else { m->lt = (unsigned char)(L[2u * (r - 1u)] + i); m->lts = 1; }
        }
    for (rr = 1; rr <= nl; rr++) {
        cnt = S >> ((rr + 1u) / 2u + 1u);
        for (i = 0; i < cnt; i++) {
            struct dm *m = &M[L[rr] + i];
            if (rr % 2u == 1u) { m->wt = (unsigned char)(L[rr + 1u] + i); m->wts = 0; }
            else if (rr < nl) { m->wt = (unsigned char)(L[rr + 1u] + i / 2u); m->wts = (unsigned char)(i % 2u); }
            else { m->wt = (unsigned char)gf1; m->wts = 1; }
        }
    }
    /* set lengths: best of 3, best of 5 from the winners round before the semifinal on and the finals */
    for (i = 1; i < mid; i++) if ((M[i].side == GNT_SIDE_W && M[i].round + 2u >= k) || M[i].side == GNT_SIDE_F || (M[i].side == GNT_SIDE_L && M[i].round + 1u >= nl)) M[i].bo = 5;
    /* seeds into round 1 */
    order[0] = 1;
    for (n = 1; n < S; n *= 2u) for (i = n; i-- > 0; ) { order[2u * i + 1u] = (unsigned char)(2u * n + 1u - order[i]); order[2u * i] = order[i]; }
    for (i = 0; i < S / 2u; i++) { M[W[1] + i].a = (unsigned char)(order[2u * i] - 1u); M[W[1] + i].b = (unsigned char)(order[2u * i + 1u] - 1u); M[W[1] + i].state = GNT_M_READY; }
    /* results: the better seed wins, an upset in one match of four; play `done` matches in id order passes */
    for (pass = 0; pass < 24u && played < done; pass++) {
        for (i = 1; i < mid && played < done; i++) {
            struct dm *m = &M[i];
            unsigned h, hi_a, up;
            if (m->state == GNT_M_DONE || m->a == GNT_NONE || m->b == GNT_NONE || i == gf2) continue;
            h = hash(i); hi_a = m->a < m->b; up = (h & 3u) == 0u;
            m->win = (unsigned char)((hi_a ^ up) ? 1u : 2u);
            m->wa = (unsigned char)(m->win == 1u ? m->bo / 2u + 1u : (h >> 4 & 1u) && m->bo == 3u ? 1u : (h >> 4 & 1u) ? 2u : 0u);
            m->wb = (unsigned char)(m->win == 2u ? m->bo / 2u + 1u : (h >> 4 & 1u) && m->bo == 3u ? 1u : (h >> 4 & 1u) ? 2u : 0u);
            m->state = GNT_M_DONE; played++;
            if (m->wt) { struct dm *d = &M[m->wt]; unsigned char w = m->win == 1u ? m->a : m->b; if (m->wts) d->b = w; else d->a = w; }
            if (m->lt) { struct dm *d = &M[m->lt]; unsigned char l = m->win == 1u ? m->b : m->a; if (m->lts) d->b = l; else d->a = l; }
            if (m->wt == 0u) break;
        }
    }
    /* the matches both players are known for: one is called (yours when you have one), one is live, the rest are ready */
    {
        unsigned called = 0, live = 0;
        for (i = 1; i < mid; i++) {
            struct dm *m = &M[i];
            if (m->state == GNT_M_DONE) continue;
            m->state = (m->a != GNT_NONE && m->b != GNT_NONE) ? GNT_M_READY : GNT_M_WAIT;
        }
        for (i = 1; i < mid && me != 0xFFu; i++) if (M[i].state == GNT_M_READY && (M[i].a == me || M[i].b == me) && !called) { M[i].state = GNT_M_CALLED; called = 1; }
        for (i = 1; i < mid; i++) if (M[i].state == GNT_M_READY && !live) { M[i].state = GNT_M_LIVE; live = 1; M[i].wa = 1; }
    }
    gnt_br_begin(g, S, S, GNT_FMT_DOUBLE);
    for (i = 0; i < S; i++) { put_name(nm, i); gnt_br_name(g, i, nm, 0); }
    for (i = 1; i < mid; i++) {
        struct gnt_match m;
        m.mid = (unsigned char)i; m.side = M[i].side; m.round = M[i].round; m.state = M[i].state; m.a = M[i].a; m.b = M[i].b; m.wa = M[i].wa; m.wb = M[i].wb; m.win = M[i].win; m.bo = M[i].bo;
        m.next_win = (i == gf1 || i == gf2) ? 0u : M[i].wt; m.next_lose = M[i].lt;
        gnt_br_match(g, i, &m);
    }
    gnt_br_end(g, me);
}
