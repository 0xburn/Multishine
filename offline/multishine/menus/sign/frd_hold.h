/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The deliberate hold (ONE hold length for the whole product, 1.0 s): hold Z accepts a friend request (menu toast and Friends screen), the same
 * length as every hold B (sg_ui.h SG_HOLD_B_TICKS, gnb.h, on_bhold.h).  TIME based, never a frame count (the menu loop does not run at a fixed rate): the caller
 * passes a 32-bit clock and the length in the same unit (FRD_HOLD_TICKS for the time base at 40.5 MHz, FRD_HOLD_MS for the milliseconds of the toast glue).
 * Pure C89, wrap safe.  Rules: a hold starts when the button goes DOWN (a button already held when the toast or the row appears does not count), releasing
 * early cancels it, completion fires ONCE per hold (release and press again to fire again), a tap alone never fires.
 */
#ifndef FRD_HOLD_H
#define FRD_HOLD_H

#define FRD_HOLD_MS 1000u
#define FRD_HOLD_TICKS 40500000u        /* 1.0 s of GameCube time base (40.5 MHz); a Wii scales it (sg_entry.c: bus / 4) */

#ifdef __GNUC__
#define FRD_HOLD_FN static __attribute__((unused))
#else
#define FRD_HOLD_FN static
#endif

struct frd_hold {
    unsigned long t0;
    unsigned char on;               /* a hold is running (the button went down while armed) */
    unsigned char fired;            /* this hold has completed */
    unsigned char prev;             /* the button's state at the last call */
    unsigned short pm;              /* progress, permille 0..1000 */
};

/* not armed (nothing to hold for): only remember the button, so that a press that began before arming never counts */
FRD_HOLD_FN void frd_hold_note(struct frd_hold *h, int held) { h->on = 0; h->fired = 0; h->pm = 0; h->prev = (unsigned char)(held != 0); }

/* one step while armed: held = the button is down now; returns 1 exactly once, on the step the hold reaches `lim` (same unit as `now`) */
FRD_HOLD_FN int frd_hold_step(struct frd_hold *h, unsigned long now, int held, unsigned long lim)
{
    unsigned long el, q;
    int rising = held && !h->prev;
    h->prev = (unsigned char)(held != 0);
    if (!held) { h->on = 0; h->fired = 0; h->pm = 0; return 0; }
    if (rising) { h->on = 1; h->fired = 0; h->t0 = now; h->pm = 0; }
    if (!h->on) return 0;
    if (lim == 0ul) lim = 1ul;
    el = (now - h->t0) & 0xFFFFFFFFul;
    if (el >= lim) {
        h->pm = 1000u;
        if (h->fired) return 0;
        h->fired = 1;
        return 1;
    }
    q = lim / 1000ul; if (q == 0ul) q = 1ul;
    el /= q;
    h->pm = (unsigned short)(el > 999ul ? 999ul : el);
    return 0;
}
#endif
