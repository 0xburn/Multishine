/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* hub_gate: when the resident hub loader may try again after it gave up in a scene (hub_res.c).  Pure function, host-tested (tests/hub_gate_test.c).
 * Cause of the lost result report after a long Game Night game: the report (BD) is kept until the hub overlay loads, the load waits for an idle Melee card layer,
 * and after HUB_WAIT_MAX passes (~10 s) it gave up for the rest of the scene, so a card layer busy longer than that (Melee's own "SAVING" after a long match) meant the
 * report was never queued while the character select stayed up.  Now: while a BD / BA tail / setup is kept, a give-up caused by a BUSY card, a failed card read, a full HSD heap (or a pack table not read yet)
 * is lifted every HUB_REARM_PASSES passes; permanent failures (no file, bad format, ABI, enter) stay given up. */
#ifndef HUB_GATE_H
#define HUB_GATE_H
#define HUB_REARM_PASSES 120u            /* about 2 s between the retries of a kept report */
#define HUB_REARM_IDLE_PASSES 300u       /* about 5 s between the retries when nothing is kept: a transient give-up (card busy for more than HUB_WAIT_MAX passes: a Wii's 60 s card load, Melee's save scan) must not leave
                                            the scene without the hub overlay for good: no net_step, no WHOAMI, Settings > Network status > Reconnect asking nobody */

/* returns 1 when the give-up is lifted this pass (the caller clears gave_up and tries) ; *wait counts the passes since the give-up */
static unsigned int hub_gate_rearm(unsigned int pending, unsigned int err_busy_or_table, unsigned int *wait)
{
    if (!err_busy_or_table) { *wait = 0; return 0; }
    if (++*wait < (pending ? HUB_REARM_PASSES : HUB_REARM_IDLE_PASSES)) return 0;
    *wait = 0;
    return 1;
}
#endif
