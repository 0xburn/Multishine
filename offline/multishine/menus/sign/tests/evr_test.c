/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Event-view row reveal (evr.c): row i visible from frame EVR_OPEN + i * EVR_STEP, the title and key line
   from EVR_CHROME, late rows staggered from their arrival, a page turn replays EVR_PAGE_PER rows a frame, skip shows all, EV_STAGGER=0 shows all at once. */
#include <stdio.h>
#include "../evr.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static unsigned shown(const struct evr *r, unsigned n) { unsigned k = evr_rows(r); return k > n ? n : k; }

int main(void)
{
    struct evr r;
    unsigned t, i;
#if EV_STAGGER
    /* open with the data already there (a kept list, a fast server) */
    evr_open(&r);
    for (t = 0; t < 80u; t++) {
        for (i = 0; i < 9u; i++) CHECK((i < shown(&r, 9u)) == (t >= EVR_OPEN + i * EVR_STEP));
        CHECK(evr_chrome(&r) == (t >= EVR_CHROME));
        CHECK(evr_busy(&r, 9u) == (t < EVR_OPEN + 8u * EVR_STEP));
        evr_tick(&r, 9u);
    }
    /* open, the rows arrive at frame L = EVR_OPEN + 10 (after their slots): their own stagger from L */
    evr_open(&r);
    for (t = 0; t < 80u; t++) {
        unsigned n = t >= EVR_OPEN + 10u ? 7u : 0u;
        evr_tick(&r, n);                                  /* (the front end: tick with the rows of this frame, then draw) */
        if (n) for (i = 0; i < 7u; i++) CHECK((i < shown(&r, n)) == (t + 1u >= EVR_OPEN + 10u + i * EVR_STEP));
        else CHECK(shown(&r, n) == 0u);
    }
    /* rows that arrive before their slot keep the open schedule */
    evr_open(&r);
    evr_tick(&r, 0u); evr_tick(&r, 5u);
    CHECK(shown(&r, 5u) == 0u && r.start == EVR_OPEN);
    /* a page turn: EVR_PAGE_PER rows a frame from now, no wait */
    for (t = 0; t < 80u; t++) evr_tick(&r, 9u);
    evr_page(&r);
    CHECK(shown(&r, 9u) == EVR_PAGE_PER);
    evr_tick(&r, 9u); CHECK(shown(&r, 9u) == 2u * EVR_PAGE_PER);
    evr_tick(&r, 9u); CHECK(shown(&r, 9u) == 9u);
    CHECK(!evr_busy(&r, 9u));
    /* back from a sub screen / action list: everything now */
    evr_open(&r);
    evr_skip(&r);
    CHECK(evr_rows(&r) == EVR_ALL && evr_chrome(&r) && !evr_busy(&r, 9u));
#else
    evr_open(&r);
    CHECK(evr_rows(&r) == EVR_ALL && evr_chrome(&r) && !evr_busy(&r, 9u));
    evr_page(&r);
    CHECK(evr_rows(&r) == EVR_ALL);
    (void)t; (void)i; (void)shown;
#endif
    printf("evr_test (EV_STAGGER=%d): %s (%d failures)\n", EV_STAGGER, fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
