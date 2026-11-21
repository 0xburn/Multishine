/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* ut_check: see ut_check.h.  Pure C, no libc, no heap; every time difference is unsigned (wraps safely). */
#include "ut_check.h"

static void done(struct hub_tchk *t, unsigned why, unsigned now)
{
    t->state = UT_DONE; t->why = (unsigned char)why; t->t_done = now;
}

/* a failed hello or check: again later, only while the player is still on the title (cheap retry); in the menus it ends silently */
static void fail(struct hub_tchk *t, unsigned now)
{
    t->fails++;
    if (t->scene == 0u && t->retries < UT_RETRIES) {
        t->hold = 1; t->t_hold = now - (UT_RETRY_MS - (t->retries == 0u ? UT_RETRY1_MS : UT_RETRY_MS));      /* 2 s before the second attempt, 5 s before the third */
        t->retries++;
        t->state = UT_WAIT; t->link_seen = 0;
        return;
    }
    done(t, UT_W_FAILED, now);
}

/* start the first part at or after `from` whose installed build is known (files first, then the engine, as the Update screen), or finish */
static void next(struct hub_tchk *t, const struct ut_io *io, unsigned now, unsigned from)
{
    unsigned st;
    for (st = from; st <= UT_CHK_ENG; st++) {
        unsigned have = st == UT_CHK_FILES ? t->ovl_seq : t->eng_seq;
        if (!have) continue;
        if (io->check(now, st == UT_CHK_FILES ? UT_ABI_FILES : UT_ABI_ENGINE, have) != 0) { fail(t, now); return; }
        t->state = (unsigned char)st; t->checks++;
        return;
    }
    done(t, t->found ? UT_W_FOUND : UT_W_UPTODATE, now);
}

void ut_step(struct hub_tchk *t, const struct ut_io *io, unsigned now)
{
    int r;
    if (t->state == UT_DONE || t->scene > 1u) return;
    if (t->enabled == 0u) {                                           /* the setting is retired; 0 only comes from a PC bot card's skip byte: nothing is sent, nothing is offered */
        if (t->state >= UT_CHK_FILES && t->kgen == t->gen) io->abort();
        done(t, UT_W_OFF, now);
        return;
    }
    if (t->enabled != 1u) return;                                     /* the setting is not read yet: wait (the resident gives the default after its tries) */
    if (!t->t0_set) { t->t0_set = 1; t->t0 = now; }
    if (t->kgen != t->gen) {                                          /* a fresh overlay load: the UPD_CHECK of the old block is gone, redo it (the hello lives in the resident client) */
        t->kgen = t->gen;
        if (t->state >= UT_CHK_FILES) t->state = UT_WAIT;
    }
    if (t->pause || io->net_busy()) {                                 /* the Update screen or a search uses the client: step aside, come back later */
        if (t->state >= UT_CHK_FILES) { io->abort(); t->state = UT_WAIT; }
        return;
    }
    switch (t->state) {
    case UT_WAIT:
        if (!t->eng_seq && !t->ovl_seq) { done(t, UT_W_NOBUILD, now); return; }       /* neither installed build is known: nothing to compare */
        if (t->hold) { if (now - t->t_hold < UT_RETRY_MS) return; t->hold = 0; }
        r = io->cfg(now);
        if (r < 0) { done(t, UT_W_NOCFG, now); return; }             /* no online setup on the card: no server to ask */
        if (r == 0 || !io->link_up()) {
            t->link_seen = 0;
            if (now - t->t0 >= UT_GIVE_UP_MS) done(t, r == 0 ? UT_W_NOCFG : UT_W_NOLINK, now);   /* no cable, no DHCP: no check this boot and no text */
            return;
        }
        if (!t->link_seen) {
            t->link_seen = 1; t->t_link = now;
            t->jit = (t->checks == 0u && t->fails == 0u) ? 0u : io->rand() % (UT_JITTER_MS + 1u);     /* the first check of the boot goes at once; re-checks and retries spread */
        }
        if (now - t->t_link < ((t->checks == 0u && t->fails == 0u) ? UT_SETTLE_BOOT_MS : UT_SETTLE_MS) + t->jit) return;
        if (io->sess_ready(now)) { next(t, io, now, UT_CHK_FILES); return; }
        r = io->hello(now);
        if (r == 0) t->state = UT_HELLO;
        else if (r < 0) fail(t, now);                                 /* r > 0: the client is busy with something else, try at a later frame */
        return;
    case UT_HELLO:
        r = io->hello_poll(now);
        if (r > 0) next(t, io, now, UT_CHK_FILES);
        else if (r < 0) fail(t, now);
        return;
    case UT_CHK_FILES:
    case UT_CHK_ENG: {
        unsigned latest = 0;
        r = io->check_poll(now, &latest);
        if (r == 0) return;
        if ((r == 1 || r == 2) && io->req_min) t->req_min = io->req_min();         /* FORCED UPDATE: the advisory rides on the same signed answer; a server without it says 0 = fail open */
        if (r == 1 && !(t->state == UT_CHK_ENG && latest == t->tried_seq)) {     /* the engine build the loader already gave up on (boot record) is not offered again */
            t->found = 1;
            if (latest > t->latest) t->latest = latest;
        }
        if (r == -1) { fail(t, now); return; }
        if (r == -2) { done(t, t->found ? UT_W_FOUND : UT_W_FAILED, now); return; }   /* a server without updates, a refused request: silent */
        next(t, io, now, (unsigned)t->state + 1u);
        return;
    }
    default:
        return;
    }
}
