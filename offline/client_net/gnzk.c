/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gnzk: Z key gestures of the watcher's call-up toast, see gnzk.h.  C89, no libc. */
#include "gnzk.h"

void gnzk_init(gnzk *k)
{
    unsigned char *b = (unsigned char *) k;
    unsigned int i;
    for (i = 0; i < sizeof *k; i++) b[i] = 0;
}

int gnzk_step(gnzk *k, int z, unsigned int now)
{
    if (k->tap && !k->down && now - k->t_rel > GNZK_DOUBLE_MS) k->tap = 0;          /* a lone tap expires: nothing happens */
    if (z && !k->down) {                                                              /* press edge */
        k->down = 1; k->fired = 0; k->t_press = now;
        k->second = (unsigned char) (k->tap && now - k->t_rel <= GNZK_DOUBLE_MS);
        k->tap = 0;
        return GNZK_NONE;
    }
    if (z && k->down) {
        if (!k->fired && now - k->t_press >= GNZK_HOLD_MS) { k->fired = 1; k->second = 0; return GNZK_ACCEPT; }
        return GNZK_NONE;
    }
    if (!z && k->down) {                                                              /* release edge */
        k->down = 0;
        if (k->fired) { k->fired = 0; return GNZK_NONE; }
        if (k->second) { k->second = 0; k->tap = 0; return GNZK_DECLINE; }
        k->tap = 1; k->t_rel = now;
        return GNZK_NONE;
    }
    return GNZK_NONE;
}

unsigned int gnzk_hold_permille(const gnzk *k, unsigned int now)
{
    unsigned int el;
    if (!k->down || k->fired) return 0u;
    el = now - k->t_press;
    return el >= GNZK_HOLD_MS ? 1000u : el * 1000u / GNZK_HOLD_MS;
}
