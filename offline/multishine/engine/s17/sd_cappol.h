/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17: capture policy "skip own capture in rollback frames" (SD_S17_SKIPCAP, opt-in, default OFF).  Original work.
 *
 * Why (item 1): after every load the engine owes one capture (the S6c rule: the first predicted
 * frame after a load is the earliest frame a later rollback can target), and that capture (about 5.5 ms on the console) lands in the rollback frame, the
 * frame that already carries restore + replay.  85 % of the rollbacks are depth 1-2, and a depth-1 fight rollback frame is 23.7 ms against 16.1: the own
 * capture is the largest single term.  The policy defers it: the debt stays (g_loadcap), the NEXT normal frame pays it, and the capture of that frame is a
 * delta over the (older) restore state, so nothing is lost but one restore point.
 *
 * The rule (exactly what models, policy s2skip1g3: `if want_cap and k >= skip_k(1) and gapnow < maxgap(3)`):
 *     in a rollback frame, skip the frame's own capture while   frame - newest_capture  <  sd_cp.limit (3)
 *   - `frame` is the label of the capture that would be taken (the state before that frame runs), `newest_capture` the newest capture AFTER the load (the
 *     restore point, or a capture the gap guard took inside the replay);
 *   - at most ONE skip per load (a second load before the debt is paid is a new rollback frame and may skip again, but the capture of the normal frame
 *     in between is never skipped: `sd_cp_skip_id`);
 *   - everything else is untouched: stride (2 in the product), the capture-gap guard inside a replay (capture when the newest is >= 4 frames behind;
 *     the S6c gap path restores the newest capture <= target and re-simulates the gap), the RXB window (latest_cap_cb).
 * Why it is exact: a restore picks the newest COMPLETE capture <= the target frame (sd_newest_ok_le) and the caller re-simulates target - that frame
 * + the depth; a missing capture only lengthens the replay (S6c/S17 mechanism, proven by wire_strict_guard and the host stride schedules).  The cost is
 * the extra replay of the next rollback when the debt is still unpaid (+0.1..0.7 frames per rollback in the model).
 *
 * This header is pure: it holds the decision and the counters, no engine state.  Users define `struct sd_cp sd_cp = { .limit = SD_CP_LIMIT };` once. */
#ifndef SD_CAPPOL_H_
#define SD_CAPPOL_H_
#include "sd_snap.h"

#define SD_CP_LIMIT 3u                       /* skip while the newest capture is fewer than this many frames behind (the model's g3; 4 = the guard distance, 6 = g6) */

struct sd_cp {
    u32 limit;                               /* A/B cfg word: SD_CP_LIMIT; 0 or 1 = the policy never skips (a capture at the label itself leaves a distance of 1) */
    u32 skipped;                             /* own captures skipped */
    u32 own_taken;                           /* own captures taken (policy off, limit reached, or the debt of a skipped one) */
    u32 guard_caps;                          /* gap-guard captures inside replays */
    u32 gap_frames;                          /* frames re-simulated beyond the requested depth because the restore point was older than the target */
    u32 rollbacks;                           /* loads seen by the policy */
};
extern struct sd_cp sd_cp;

/* 1 = skip the frame's own capture.  flags: the S17 flags word of the user (glue s17_flags, rbtest sd_s17, host test argument) */
static inline int sd_cp_skip_own(u32 flags, s32 frame, s32 newest)
{
    return (flags & SD_S17_SKIPCAP) && (s32)(frame - newest) < (s32)sd_cp.limit && frame > newest;
}
#endif
