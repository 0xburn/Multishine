/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GNZK_H
#define GNZK_H
/*
 * gnzk: the Z key gestures of the watcher's call-up toast: HOLD Z for 1.0 s = accept, DOUBLE tap = decline, a single tap does nothing.
 * Rules: the hold timer starts at EVERY press (a hold that begins inside the double-tap window counts as a hold only after its own 1.0 s); a press shorter than GNZK_HOLD_MS is a tap;
 * the second tap of a pair must PRESS within GNZK_DOUBLE_MS of the first tap's release; the decline fires when the second tap is released (still shorter than the hold); a lone tap
 * expires silently.  Pure, C89, no libc; the caller feeds the Z state once per pass with a millisecond clock (wrap safe).
 */
#define GNZK_HOLD_MS 1000u
#define GNZK_DOUBLE_MS 400u
enum { GNZK_NONE = 0, GNZK_ACCEPT = 1, GNZK_DECLINE = 2 };
typedef struct gnzk {
    unsigned char down, tap, second, fired;
    unsigned int t_press, t_rel;
} gnzk;
void gnzk_init(gnzk *k);
/* z = Z is down now.  Returns GNZK_NONE / GNZK_ACCEPT / GNZK_DECLINE (each at most once per gesture). */
int gnzk_step(gnzk *k, int z, unsigned int now_ms);
/* 0..1000 permille of the hold in progress (for a progress bar), 0 when no hold is running */
unsigned int gnzk_hold_permille(const gnzk *k, unsigned int now_ms);
#endif
