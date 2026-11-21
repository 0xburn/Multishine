/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The row reveal of the Event-view screens: see evr.h.  Strict C89, no libc. */
#include "evr.h"

void evr_open(struct evr *r)
{
    r->t = 0; r->had = 0;
    r->chrome = EV_STAGGER ? (unsigned char)EVR_CHROME : 0u;
    r->start = EV_STAGGER ? (unsigned short)EVR_OPEN : 0u;
    r->step = EV_STAGGER ? (unsigned char)EVR_STEP : 0u;
    r->per = 1u;
}

void evr_page(struct evr *r)
{
    r->start = r->t;
    r->step = EV_STAGGER ? 1u : 0u;                                       /* every frame, EVR_PAGE_PER rows at a time */
    r->per = EVR_PAGE_PER;
}

void evr_skip(struct evr *r)
{
    r->chrome = 0; r->start = 0; r->step = 0; r->per = 1u;
    if (r->t < 1u) r->t = 1u;
}

void evr_tick(struct evr *r, unsigned n_rows)
{
    if (r->had == 0u && n_rows > 0u && r->t > r->start && r->step) r->start = r->t;    /* the rows came after their slot: their own stagger from now */
    r->had = (unsigned char)(n_rows > 255u ? 255u : n_rows);
    if (r->t < 0xFFF0u) r->t++;
}

unsigned evr_rows(const struct evr *r)
{
    unsigned k;
    if (!r->step) return r->t >= r->start ? EVR_ALL : 0u;
    if (r->t < r->start) return 0u;
    k = ((unsigned)(r->t - r->start) / r->step + 1u) * r->per;
    return k > 254u ? EVR_ALL : k;
}

int evr_chrome(const struct evr *r) { return r->t >= r->chrome; }

int evr_busy(const struct evr *r, unsigned n_rows)
{
    unsigned k = evr_rows(r);
    return !evr_chrome(r) || (k != EVR_ALL && k < n_rows);
}
