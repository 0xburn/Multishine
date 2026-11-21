/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * S6D: the adaptive rollback depth cap, as pure integer logic so the same
 * code runs in the GameCube glue and in the host test (tests/s6d_cap_test.c).
 *
 * Why (/ finding 4): the S6c cap `(idle x budget - restore) / (logic + capture / stride)` with
 * idle = frame - max(base work of the last 8 frames) only allowed rollbacks that fit in ONE frame's idle time and
 * assumed every re-simulated frame pays a full capture.  It picked cap 1-2, so the game stalled on ~60 % of frames at
 * 120 ms RTT.  S6d changes four things:
 *   1. idle is measured from the Time Base as frame - base work, with base work = EMA + half the gap to the max of the
 *      last 8 frames (between the mean and the worst case, not the worst case);
 *   2. the per-frame cost of a re-simulated frame is MEASURED per rollback ((rollback ticks - restore) / frames
 *      re-simulated, EMA), not modelled as logic + a full capture / stride (most re-simulated frames are confirmed and
 *      are not captured again);
 *   3. planned overruns: an overrun token bucket allows a rollback to use the idle time of the NEXT frame too
 *      (spread over two frames: Melee's scene loop does not drop a logic frame when one pass is late, the next pass
 *      starts at once and its idle time absorbs the rest).  Tokens refill by (ovr_pct % of a frame) / ovr_every per
 *      frame that was not late and are spent by the measured lateness (work - frame), so at most one full-frame
 *      overrun per `ovr_every` frames is planned: occasional, never chronic.  The extra budget is also bounded by one
 *      frame's idle (the next frame must be able to absorb it);
 *   4. hysteresis: the cap rises one step only after `up` consecutive frames in which the wanted cap was higher, and
 *      drops to the wanted value after `dn` consecutive frames in which it was lower, or at once in a frame that was
 *      late (work > one frame) while the token bucket is below half (chronic overrun).  Defaults up 4, dn 30: rise fast, ignore estimate noise,
 *      react to real overruns.
 * All values are Time Base ticks (40.5 MHz; 675,000 per 60 Hz frame).  No floating point.
 */
#ifndef S6D_CAP_H
#define S6D_CAP_H

typedef unsigned int s6d_u32;

struct s6d_cap_cfg {
    s6d_u32 frame;          /* ticks per frame (675000) */
    s6d_u32 budget_pct;     /* share of the idle time of a frame a rollback may plan to use */
    s6d_u32 ovr_pct;        /* planned overrun: token bucket size, % of a frame (0 = no planned overrun = S6c-like) */
    s6d_u32 ovr_every;      /* frames to refill a full bucket (>= 1) */
    s6d_u32 up, dn;         /* hysteresis: frames before a raise (one step) / a drop */
    s6d_u32 cap_min, cap_max;
};

struct s6d_cap {
    s6d_u32 cap;            /* current cap (applied by the glue at the next B0 outside a rollback) */
    s6d_u32 want;           /* cap the budget of this frame would allow */
    s6d_u32 up_cnt, dn_cnt;
    s6d_u32 tokens;         /* planned-overrun budget left, ticks */
    s6d_u32 idle;           /* idle estimate of the last step, ticks */
    s6d_u32 budget;         /* rollback budget of the last step (strict + overrun), ticks */
    s6d_u32 late, late_ticks, raises, drops;
};

static inline void s6d_cap_init(struct s6d_cap *s, const struct s6d_cap_cfg *c, s6d_u32 cap0)
{
    s->cap = cap0 < c->cap_min ? c->cap_min : cap0 > c->cap_max ? c->cap_max : cap0;
    s->want = s->cap;
    s->up_cnt = s->dn_cnt = 0;
    s->tokens = c->frame / 100u * c->ovr_pct;             /* start with a full bucket */
    s->idle = s->budget = 0;
    s->late = s->late_ticks = s->raises = s->drops = 0;
}

/* One rendered frame.  work = this frame's measured work (period - idle wait), base_ema / base_max8 = work without
   rollback (EMA and max of the last 8 frames), restore = B2 cost (EMA), per = cost of one re-simulated frame (EMA). */
static inline void s6d_cap_step(struct s6d_cap *s, const struct s6d_cap_cfg *c, s6d_u32 work, s6d_u32 base_ema,
                                s6d_u32 base_max8, s6d_u32 restore, s6d_u32 per)
{
    s6d_u32 P = c->frame, tokmax = P / 100u * c->ovr_pct, every = c->ovr_every ? c->ovr_every : 1u;
    s6d_u32 wb, strict, extra, want;
    if (work > P) {                                        /* late: spend what the overrun used */
        s6d_u32 o = work - P;
        s->late++; s->late_ticks += o;
        s->tokens = s->tokens > o ? s->tokens - o : 0;
    } else {
        s->tokens += tokmax / every;
        if (s->tokens > tokmax) s->tokens = tokmax;
    }
    wb = base_ema + (base_max8 > base_ema ? (base_max8 - base_ema) / 2u : 0u);
    s->idle = wb < P ? P - wb : 0;
    strict = s->idle / 100u * c->budget_pct;
    extra = s->tokens < strict ? s->tokens : strict;       /* the next frame's idle absorbs at most one frame's worth */
    s->budget = strict + extra;
    if (!per) per = 1;
    want = s->budget > restore ? (s->budget - restore) / per : 0;
    if (want < c->cap_min) want = c->cap_min;
    if (want > c->cap_max) want = c->cap_max;
    s->want = want;
    if (want > s->cap) {
        s->dn_cnt = 0;
        if (++s->up_cnt >= (c->up ? c->up : 1u)) { s->cap++; s->up_cnt = 0; s->raises++; }
    } else if (want < s->cap) {
        s->up_cnt = 0;
        /* drop after `dn` frames, or at once when this frame was late AND the overrun budget is below half (overruns
           are becoming chronic, not noise in the estimate) */
        if (++s->dn_cnt >= (c->dn ? c->dn : 1u) || (work > P && s->tokens < tokmax / 2u)) { s->cap = want; s->dn_cnt = 0; s->drops++; }
    } else {
        s->up_cnt = s->dn_cnt = 0;
    }
}

#endif
