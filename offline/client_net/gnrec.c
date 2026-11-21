/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gnrec: spectate S1 uploader, see gnrec.h.  C89, no libc, no heap, never blocks. */
#include "gnrec.h"

static void p8(mnc_u8 **p, unsigned v) { *(*p)++ = (mnc_u8) v; }
static void p32(mnc_u8 **p, mnc_u32 v) { p8(p, v >> 24); p8(p, v >> 16); p8(p, v >> 8); p8(p, v); }

void gnrec_init(gnrec *r)
{
    mnc_u8 *b = (mnc_u8 *) r;
    mnc_u32 i;
    for (i = 0; i < sizeof *r; i++) b[i] = 0;
}

void gnrec_arm(gnrec *r, mnc_u32 set_id, unsigned game, const mnc_u8 *sel, unsigned sel_len, mnc_s32 frame)
{
    unsigned i;
    gnrec_init(r);
    r->set_id = set_id; r->game = (mnc_u8) game;
    if (sel && sel_len && sel_len <= GNREC_SEL_MAX) {
        for (i = 0; i < sel_len; i++) r->sel[i] = sel[i];
        r->sel_len = (mnc_u8) sel_len; r->sel_left = GNREC_SEL_REPEAT;
    }
    r->last_frame = frame;
    r->state = GNREC_ON;
}

int gnrec_close(gnrec *r, mw2 *w, const mnc_io *io, mnc_u32 now_ms)
{
    mnc_u8 *p = r->buf + MW2_HDR;
    mnc_u32 n;
    if (r->state != GNREC_ON || !w || !mw2_ready(w, now_ms)) return 0;
    p8(&p, GNREC_VER); p32(&p, r->set_id); p8(&p, r->game); p8(&p, GNREC_F_END);
    p32(&p, 0); p8(&p, 0); p32(&p, 0); p8(&p, 0); p8(&p, 0);
    n = mw2_seal(w, r->buf, (mnc_u8) GNREC_OP, (mnc_u32) (p - (r->buf + MW2_HDR)));
    r->state = GNREC_OFF;
    r->st_sent++;
    (void) io->send(io->ctx, r->buf, n);
    return 1;
}

void gnrec_set_ondemand(gnrec *r, mnc_u8 *hist, mnc_u32 hist_cap) { r->on_demand = 1; r->hist = hist; r->hist_cap = hist ? hist_cap : 0u; }

void gnrec_kill(gnrec *r, unsigned why) { r->state = GNREC_DEAD; r->st_dead_why = why; }

void gnrec_finish(gnrec *r)
{
    if (r->state != GNREC_ON) return;
    r->end_req = 1;
    if (r->on_demand && r->archive && !r->watched && r->hist_n) { r->flushing = 1; r->bl_next = 1u; }      /* wanted afterwards and not streamed so far: flush the whole game before the closing datagram */
}

/* the newest run of valid ring entries ending at `top`: returns how many (<= GNREC_WIN), *first = the oldest tag of the run */
static unsigned run(const mnc_s32 *tag, mnc_u32 mask, mnc_s32 top, mnc_s32 *first)
{
    unsigned n = 0;
    mnc_s32 t = top;
    while (t >= 1 && n < GNREC_WIN && tag[(mnc_u32) t & mask] == t) { n++; t--; }
    *first = top - (mnc_s32) n + 1;
    return n;
}

mnc_u32 gnrec_build(gnrec *r, const struct gnrec_rings *v, mnc_u8 *out)
{
    mnc_u8 *p = out;
    mnc_s32 of, pf, fr;
    unsigned on = run(v->ltag, v->mask, v->lnewest, &of), pn = run(v->rtag, v->mask, v->rlatest, &pf), i, j, nc = 0;
    mnc_s32 cf[GNREC_CHK];
    mnc_u32 cc[GNREC_CHK];
    unsigned fl = (r->sel_left ? GNREC_F_SEL : 0u) | (r->end_req ? GNREC_F_END : 0u);

    for (fr = v->fin & ~15; fr >= 16 && fr > v->fin - 64 && nc < GNREC_CHK; fr -= 16) {      /* every 16th finalized frame still in the history */
        if (v->hframe[(mnc_u32) fr & v->hmask] == fr) { cf[nc] = fr; cc[nc] = v->hcks[(mnc_u32) fr & v->hmask]; nc++; }
    }
    if (on == 0 && pn == 0 && nc == 0 && !fl) return 0;
    p8(&p, GNREC_VER); p32(&p, r->set_id); p8(&p, r->game); p8(&p, fl);
    if (fl & GNREC_F_SEL) { p8(&p, r->sel_len); for (i = 0; i < r->sel_len; i++) p8(&p, r->sel[i]); }
    p32(&p, (mnc_u32) (on ? of : 0)); p8(&p, on); p32(&p, (mnc_u32) (pn ? pf : 0)); p8(&p, pn); p8(&p, nc);
    for (i = 0; i < on; i++) for (j = 0; j < GNREC_PAD; j++) p8(&p, v->lpad[(((mnc_u32) (of + (mnc_s32) i)) & v->mask) * GNREC_PAD + j]);
    for (i = 0; i < pn; i++) for (j = 0; j < GNREC_PAD; j++) p8(&p, v->rpad[(((mnc_u32) (pf + (mnc_s32) i)) & v->mask) * GNREC_PAD + j]);
    for (i = 0; i < nc; i++) { p32(&p, (mnc_u32) cf[i]); p32(&p, cc[i]); }
    return (mnc_u32) (p - out);
}

/* the history: every own pad and every opponent pad (as received) the rings still hold, contiguous from tag 1 per side; hist_n = the tags both sides have */
static void record(gnrec *r, const struct gnrec_rings *v)
{
    mnc_s32 t;
    unsigned j;
    if (!r->hist) return;
    for (t = (mnc_s32) r->hist_own + 1; t <= v->lnewest && (mnc_u32) t <= r->hist_cap && v->ltag[(mnc_u32) t & v->mask] == t; t++) {
        for (j = 0; j < GNREC_PAD; j++) r->hist[(mnc_u32) (t - 1) * 2u * GNREC_PAD + j] = v->lpad[((mnc_u32) t & v->mask) * GNREC_PAD + j];
        r->hist_own = (mnc_u32) t;
    }
    for (t = (mnc_s32) r->hist_opp + 1; t <= v->rlatest && (mnc_u32) t <= r->hist_cap && v->rtag[(mnc_u32) t & v->mask] == t; t++) {
        for (j = 0; j < GNREC_PAD; j++) r->hist[(mnc_u32) (t - 1) * 2u * GNREC_PAD + GNREC_PAD + j] = v->rpad[((mnc_u32) t & v->mask) * GNREC_PAD + j];
        r->hist_opp = (mnc_u32) t;
    }
    r->hist_n = r->hist_own < r->hist_opp ? r->hist_own : r->hist_opp;
}

/* a backlog datagram: up to GNREC_BL_TAGS tags from bl_next, the own pads AND the opponent's (the server merges them with the other console's like any other upload; a single uploader is enough) */
static mnc_u32 build_backlog(gnrec *r, mnc_u8 *out)
{
    mnc_u8 *p = out;
    mnc_u32 n = r->hist_n - r->bl_next + 1u, i;
    unsigned fl = (r->sel_left ? GNREC_F_SEL : 0u);
    if (n > GNREC_BL_TAGS) n = GNREC_BL_TAGS;
    p8(&p, GNREC_VER); p32(&p, r->set_id); p8(&p, r->game); p8(&p, fl);
    if (fl & GNREC_F_SEL) { p8(&p, r->sel_len); for (i = 0; i < r->sel_len; i++) p8(&p, r->sel[i]); }
    p32(&p, r->bl_next); p8(&p, n); p32(&p, r->bl_next); p8(&p, n); p8(&p, 0);
    for (i = 0; i < n; i++) { unsigned j; for (j = 0; j < GNREC_PAD; j++) p8(&p, r->hist[(r->bl_next - 1u + i) * 2u * GNREC_PAD + j]); }                       /* own */
    for (i = 0; i < n; i++) { unsigned j; for (j = 0; j < GNREC_PAD; j++) p8(&p, r->hist[(r->bl_next - 1u + i) * 2u * GNREC_PAD + GNREC_PAD + j]); }          /* opponent */
    r->bl_next += n;
    return (mnc_u32) (p - out);
}

/* a repair datagram from the engine's own rings: the server's merged log stops at srv_complete while the game has run on (a datagram it refused or lost, or the pause after
   RATE_LIMITED); the history is frozen at its capacity, so the missing tags come from the rings the engine still holds (S3_RING 256 tags, 4 s).  Own AND opponent pads, like the backlog; 0 = a tag is not in the rings any more */
static mnc_u32 build_ring_repair(gnrec *r, const struct gnrec_rings *v, mnc_u8 *out)
{
    mnc_u8 *p = out;
    mnc_s32 from = (mnc_s32) r->srv_complete + 1, t, hi = v->lnewest < v->rlatest ? v->lnewest : v->rlatest;
    mnc_u32 n = 0, i, j;
    unsigned fl = (r->sel_left ? GNREC_F_SEL : 0u);
    for (t = from; t <= hi && n < GNREC_BL_TAGS; t++, n++)
        if (v->ltag[(mnc_u32) t & v->mask] != t || v->rtag[(mnc_u32) t & v->mask] != t) break;
    if (n == 0) return 0;
    p8(&p, GNREC_VER); p32(&p, r->set_id); p8(&p, r->game); p8(&p, fl);
    if (fl & GNREC_F_SEL) { p8(&p, r->sel_len); for (i = 0; i < r->sel_len; i++) p8(&p, r->sel[i]); }
    p32(&p, (mnc_u32) from); p8(&p, n); p32(&p, (mnc_u32) from); p8(&p, n); p8(&p, 0);
    for (i = 0; i < n; i++) for (j = 0; j < GNREC_PAD; j++) p8(&p, v->lpad[(((mnc_u32) (from + (mnc_s32) i)) & v->mask) * GNREC_PAD + j]);
    for (i = 0; i < n; i++) for (j = 0; j < GNREC_PAD; j++) p8(&p, v->rpad[(((mnc_u32) (from + (mnc_s32) i)) & v->mask) * GNREC_PAD + j]);
    return (mnc_u32) (p - out);
}

/* the heartbeat: header, selections while they are still being repeated, the newest checksums, no pads (18 + 8 * n bytes) */
static mnc_u32 build_beat(gnrec *r, const struct gnrec_rings *v, mnc_u8 *out)
{
    mnc_u8 *p = out;
    mnc_s32 fr;
    unsigned nc = 0, i;
    mnc_s32 cf[GNREC_CHK];
    mnc_u32 cc[GNREC_CHK];
    unsigned fl = (r->sel_left ? GNREC_F_SEL : 0u);
    for (fr = v->fin & ~15; fr >= 16 && fr > v->fin - 64 && nc < GNREC_CHK; fr -= 16)
        if (v->hframe[(mnc_u32) fr & v->hmask] == fr) { cf[nc] = fr; cc[nc] = v->hcks[(mnc_u32) fr & v->hmask]; nc++; }
    p8(&p, GNREC_VER); p32(&p, r->set_id); p8(&p, r->game); p8(&p, fl);
    if (fl & GNREC_F_SEL) { p8(&p, r->sel_len); for (i = 0; i < r->sel_len; i++) p8(&p, r->sel[i]); }
    p32(&p, 0); p8(&p, 0); p32(&p, 0); p8(&p, 0); p8(&p, nc);
    for (i = 0; i < nc; i++) { p32(&p, (mnc_u32) cf[i]); p32(&p, cc[i]); }
    return (mnc_u32) (p - out);
}

static int xmit(gnrec *r, mw2 *w, const mnc_io *io, mnc_u32 n)
{
    n = mw2_seal(w, r->buf, (mnc_u8) GNREC_OP, n);
    if (io->send(io->ctx, r->buf, n) < 0) {
        if (++r->fails >= GNREC_FAIL_MAX) { gnrec_kill(r, 1u); return -1; }
    } else r->fails = 0;
    r->st_sent++;
    return 0;
}

int gnrec_step(gnrec *r, mw2 *w, const mnc_io *io, const struct gnrec_rings *v, mnc_s32 frame, int in_rollback, mnc_u32 now_ms)
{
    mnc_u32 n;
    if (r->state != GNREC_ON) return 0;
    if (frame < r->last_frame) r->last_frame = frame;                       /* a new match counts from 1 again */
    if (r->on_demand) {
        if (frame <= 1) r->hist_n = r->hist_own = r->hist_opp = r->bl_next = 0;
        record(r, v);
        if (r->watched && r->bl_next && (r->overflow ? r->bl_next > r->hist_n : r->hist_n < r->bl_next + 36u)) r->bl_next = 0u;       /* the backlog reached the live window (which the regular stream covers): done.  After the history overflowed
                                                                                                                                       it is frozen at its capacity while the game runs on: the live window is far past hist_n, so the backlog is done only when it has sent the whole history
 */
        if (r->flushing || (r->watched && r->bl_next && !r->end_req)) {      /* backlog (a watcher joined mid-game) or the end-of-game flush */
            if (w && mw2_ready(w, now_ms) && now_ms - r->bl_ms >= GNREC_BL_MS) {
                if (r->bl_next > r->hist_n) {
                    if (r->flushing) { r->flushing = 0; r->watched = 0; }                  /* flushed: fall through to the closing datagram */
                } else {
                    r->bl_ms = now_ms;
                    n = build_backlog(r, r->buf + MW2_HDR);
                    if (r->sel_left) r->sel_left--;
                    r->st_backlog++;
                    if (xmit(r, w, io, n) < 0) return -1;
                    if (r->flushing) return 1;
                }
            }
            if (r->flushing) return 0;
        }
        if (r->overflow && !r->flushing && !r->end_req && r->srv_complete >= r->hist_n && v->lnewest - (mnc_s32) r->srv_complete > (mnc_s32) (GNREC_WIN + GNREC_EVERY) && frame >= r->pause_until) {
            /* the server's log has a hole past the history (not inside it): repair it from the engine's rings, one datagram per GNREC_BL_MS, until the live window joins up again */
            if (w && mw2_ready(w, now_ms) && now_ms - r->bl_ms >= GNREC_BL_MS) {
                n = build_ring_repair(r, v, r->buf + MW2_HDR);
                if (n) {
                    r->bl_ms = now_ms; r->st_ring++;
                    if (xmit(r, w, io, n) < 0) return -1;
                }
            }
        }
        if (r->hist && r->hist_n >= r->hist_cap && !r->overflow) { r->overflow = 1; r->watched = 1; r->bl_next = 1u; }      /* the history is full: from here on the game streams in full (and the server gets the start once), a long game is never lost */
        if (r->od_ok && !r->watched && !r->end_req && !r->flushing) {                    /* idle: a heartbeat every GNREC_IDLE_EVERY frames */
            if (frame - r->last_frame < GNREC_IDLE_EVERY || frame < r->pause_until || in_rollback) return 0;
            if (!w || !mw2_ready(w, now_ms)) { r->st_notready++; return 0; }
            n = build_beat(r, v, r->buf + MW2_HDR);
            r->last_frame = frame;
            if (r->sel_left) r->sel_left--;
            r->st_idle++;
            if (xmit(r, w, io, n) < 0) return -1;
            if (r->expect_replies && ++r->since_reply > GNREC_SILENT_MAX) { gnrec_kill(r, 2u); return -1; }
            return 1;
        }
    }
    if (!r->end_req && frame - r->last_frame < GNREC_EVERY) return 0;
    if (frame < r->pause_until) return 0;
    if (in_rollback) { r->st_rollback++; return 0; }                       /* the next window repeats these pads */
    if (!w || !mw2_ready(w, now_ms)) { r->st_notready++; return 0; }
    n = gnrec_build(r, v, r->buf + MW2_HDR);
    if (n == 0) return 0;
    n = mw2_seal(w, r->buf, (mnc_u8) GNREC_OP, n);
    r->last_frame = frame;
    if (r->sel_left) r->sel_left--;
    if (io->send(io->ctx, r->buf, n) < 0) {
        if (++r->fails >= GNREC_FAIL_MAX) { gnrec_kill(r, 1u); return -1; }
    } else r->fails = 0;
    r->st_sent++;
    if (r->end_req) { r->state = GNREC_OFF; return 1; }                    /* the closing datagram went out */
    if (r->expect_replies && ++r->since_reply > GNREC_SILENT_MAX) { gnrec_kill(r, 2u); return -1; }
    return 1;
}

void gnrec_reply2(gnrec *r, unsigned status, mnc_u32 complete, unsigned flags)
{
    gnrec_reply(r, status, complete);
    if (status == 0u) r->srv_complete = complete;
    if (status != 0u || !r->on_demand) return;
    if (flags & GNREC_RF_ONDEMAND) r->od_ok = 1;
    if (r->overflow) {                                                                     /* streaming in full from here on; the history is frozen: repair any hole the server still reports inside it (a flush datagram it refused, RATE_LIMITED, or that was lost) */
        if (r->hist_n && complete < r->hist_n && (r->bl_next == 0u || r->bl_next > r->hist_n || complete + 1u < r->bl_next)) r->bl_next = complete + 1u;
        return;
    }
    r->archive = (flags & GNREC_RF_ARCHIVE) ? 1u : 0u;
    if ((flags & GNREC_RF_WATCHED) && !r->watched) { r->watched = 1; r->bl_next = 1u; }      /* somebody started watching: send the game from its start */
    else if (!(flags & GNREC_RF_WATCHED)) { if (r->watched) r->bl_next = 0u; r->watched = 0; }
    else if (complete + 1u + 4u * GNREC_BL_TAGS < r->hist_n && (r->bl_next == 0u || complete + 1u + 4u * GNREC_BL_TAGS < r->bl_next)) r->bl_next = complete + 1u;      /* the merge is far behind (losses, or the other console is late): send again from the hole */
}

void gnrec_reply(gnrec *r, unsigned status, mnc_u32 complete)
{
    (void) complete;
    r->expect_replies = 1;
    if (status == 0u) { r->since_reply = 0; r->st_replies++; return; }
    if (status == 3u || status == 17u) { r->pause_until = r->last_frame + GNREC_BACKOFF; r->since_reply = 0; return; }   /* RATE_LIMITED, SERVER_FULL: pause */
    gnrec_kill(r, 16u + status);                                         /* NOT_SUPPORTED, NOT_FOUND, BAD_REQUEST, AUTH_FAILED ...: stop for good */
}
