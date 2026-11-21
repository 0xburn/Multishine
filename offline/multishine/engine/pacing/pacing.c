/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Runtime pacing logic, see pacing.h.  C89, integers only. */
#include "pacing.h"

typedef char pc_assert_int32[(sizeof(pc_s32) == 4 && sizeof(pc_u32) == 4) ? 1 : -1];

pc_s32 pc_delay_raw(pc_u32 rtt_us)
{
    pc_u32 q = rtt_us / PC_DELAY_STEP_US + (rtt_us % PC_DELAY_STEP_US != 0);   /* no overflow near 2^32 */
    pc_s32 d = (q > 100 ? 100 : (pc_s32)q) + 1;
    return d < PC_DELAY_FLOOR ? PC_DELAY_FLOOR : d > PC_DELAY_CAP ? PC_DELAY_CAP : d;
}

void pc_delay_init(struct pc_delay *d, pc_s32 floor_, pc_s32 cap)
{
    pc_u32 i;
    for (i = 0; i < PC_RING; i++) d->ring[i] = 0;
    d->n = d->head = 0; d->votes = 0;
    d->floor_ = floor_ < 1 ? 1 : floor_;
    d->cap = cap < d->floor_ ? d->floor_ : cap > 15 ? 15 : cap;
    d->cur = d->floor_;
}

void pc_delay_sample(struct pc_delay *d, pc_u32 rtt_us)
{
    if (rtt_us > 4000000u) rtt_us = 4000000u;      /* one lost ack must not dominate any statistic */
    d->ring[d->head] = rtt_us;
    d->head = (d->head + 1) % PC_RING;
    if (d->n < PC_RING) d->n++;
}

/* element of rank k (0-based) among the n samples, by insertion sort of a copy (n <= 32: no heap, 128 bytes of stack) */
static void sorted_copy(const struct pc_delay *d, pc_u32 *s)
{
    pc_u32 i, j, v;
    for (i = 0; i < d->n; i++) {
        v = d->ring[i];                           /* order of the ring does not matter for order statistics */
        for (j = i; j > 0 && s[j - 1] > v; j--) s[j] = s[j - 1];
        s[j] = v;
    }
}

pc_u32 pc_delay_estimate(const struct pc_delay *d, pc_u32 *p50_us, pc_u32 *spread_us)
{
    pc_u32 s[PC_RING], n = d->n, a, b;
    if (n < PC_MIN_N) return 0;
    sorted_copy(d, s);
    a = s[n / 2];                                  /* median (upper median for even n) */
    b = s[(n * 9) / 10 >= n ? n - 1 : (n * 9) / 10];
    *p50_us = a; *spread_us = b - a;
    return n;
}

static pc_s32 target_for(const struct pc_delay *d, pc_u32 extra_us)
{
    pc_u32 p50, sp;
    pc_s32 t;
    if (!pc_delay_estimate(d, &p50, &sp)) return d->cur;
    t = pc_delay_raw(p50 > 0xFFFFFFFFu - extra_us ? 0xFFFFFFFFu : p50 + extra_us);
    if (sp > PC_JITTER_BUMP_US) t++;
    if (t < d->floor_) t = d->floor_;
    if (t > d->cap) t = d->cap;
    return t;
}

pc_s32 pc_delay_target(const struct pc_delay *d) { return target_for(d, 0); }

pc_s32 pc_delay_commit(struct pc_delay *d, int safe)
{
    pc_s32 t;
    if (!safe) return d->cur;
    t = target_for(d, 0);
    if (t > d->cur) { d->cur = t; d->votes = 0; }
    else if (target_for(d, PC_HYST_US) < d->cur) {
        if (++d->votes >= PC_DOWN_VOTES) { d->cur--; d->votes = 0; }
    } else d->votes = 0;
    return d->cur;
}

pc_s32 pc_delay_pair(pc_s32 a, pc_s32 b) { return a > b ? a : b; }
pc_u32 pc_rtt_pair(pc_u32 a, pc_u32 b) { return a > b ? a : b; }

/* ---- time sync ---- */
void pc_ts_init(struct pc_ts *g, int guard)
{
    g->over = g->last_corr = 0;
    g->pend = g->pend_frame = g->pend_off = g->pend_k = 0;
    g->bad = g->tripped = g->trip_off = 0;
    g->guard = guard ? 1 : 0;
}

pc_s32 pc_ts_step(struct pc_ts *g, int mode, pc_s32 frame, pc_s32 off, pc_s32 thr_us, pc_s32 gap)
{
    pc_s32 thr, k, mx;
    if (mode != 2 && mode != 3) return 0;
    thr = thr_us ? thr_us : mode == 2 ? 12500 : 25000;
    gap = gap ? gap : mode == 2 ? 60 : 150;
    if (g->guard && g->pend && frame - g->pend_frame >= (gap < 60 ? 60 : gap)) {   /* the ring has been renewed: judge the last correction */
        pc_s32 drop = g->pend_off - off;
        if (drop * 4 < g->pend_k * PC_FRAME_US) g->bad++; else g->bad = 0;          /* moved by less than 1/4 of what was skipped */
        g->pend = 0;
        if (g->bad >= 2) { g->tripped = 1; g->trip_off = off; }
    }
    if (g->tripped && (off - g->trip_off >= PC_FRAME_US || g->trip_off - off >= PC_FRAME_US)) {   /* the offset moved by itself: a real lead, re-arm */
        g->tripped = 0; g->bad = 0; g->over = 0;
    }
    if (off >= thr) g->over++; else g->over = 0;
    if (g->tripped) return 0;
    if (off >= thr && (mode == 2 || g->over >= 2) && (!g->last_corr || frame - g->last_corr >= gap)) {
        k = (off + PC_FRAME_US / 2) / PC_FRAME_US - (mode == 3 ? 1 : 0);
        mx = mode == 2 ? 3 : 2;
        if (k > mx) k = mx;
        if (k >= 1) {
            g->last_corr = frame; g->over = 0;
            g->pend = 1; g->pend_frame = frame; g->pend_off = off; g->pend_k = k;
            return k;
        }
    }
    return 0;
}

/* ---- mode selection ---- */
void pc_select(struct pc_cfg *o, int peer, pc_u32 flags, int in_match, const struct pc_cfg *prev)
{
    if (in_match && prev) { *o = *prev; return; }
    o->delay_floor = PC_DELAY_FLOOR; o->delay_cap = PC_DELAY_CAP;     /* auto delay from the RTT table, same table for both peer types */
    if (peer == PC_PEER_CONSOLE) {
        o->peer_level = 1;
        o->equal_delay = 1;                                            /* both sides use the larger RTT (hello field) */
        if (flags & PC_PAIR_MODE2) { o->ts_mode = 2; o->use_guard = 1; }
        else { o->ts_mode = 3; o->use_guard = 0; }
    } else {                                                           /* Slippi PC, unknown, timeout, rejected, none: Slippi-compatible */
        o->peer_level = 0; o->equal_delay = 0; o->ts_mode = 3; o->use_guard = 0;
    }
}
