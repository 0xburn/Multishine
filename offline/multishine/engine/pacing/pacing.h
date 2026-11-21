/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Runtime pacing logic (rules ).
 * Standalone, C89, integers only (no float/double: the console build is soft-float-checked), no libc, no heap, no globals.
 * Three parts: (1) auto input delay from the measured RTT, (2) the "offset does not move" guard around the S7 mode 2/3
 * post-frame-120 correction, (3) mode selection from the recognition result. Python twin: (must agree bit for bit).
 */
#ifndef MS_PACING_H
#define MS_PACING_H

typedef int pc_s32;
typedef unsigned int pc_u32;

#define PC_FRAME_US   16683          /* Slippi frame time constant (= S7_FRAME_US) */
#define PC_RING       32             /* RTT samples kept */
#define PC_MIN_N      4              /* samples needed before an estimate exists */
#define PC_DELAY_STEP_US 33400       /* delay = ceil(rtt / 33.4 ms) + 1 */
#define PC_DELAY_FLOOR 2
#define PC_DELAY_CAP   7
#define PC_HYST_US     6000          /* deadband below a boundary before the delay may go down */
#define PC_JITTER_BUMP_US 30000      /* p90 - p50 above this: one more delay frame */
#define PC_DOWN_VOTES  2             /* safe moments (games) in a row that ask for a lower delay */

/* ---- (1) delay ---- */
struct pc_delay {
    pc_u32 ring[PC_RING];            /* RTT samples in microseconds, newest overwrites oldest */
    pc_u32 n, head;                  /* count (<= PC_RING), next write index */
    pc_s32 cur;                      /* delay in effect (frames); changes only in pc_delay_commit with safe != 0 */
    pc_s32 votes;                    /* consecutive safe moments that asked for a lower delay */
    pc_s32 floor_, cap;
};

pc_s32 pc_delay_raw(pc_u32 rtt_us);                                   /* max(2, ceil(rtt/33.4ms)+1), cap 7 */
void   pc_delay_init(struct pc_delay *d, pc_s32 floor_, pc_s32 cap);   /* cur = floor */
void   pc_delay_sample(struct pc_delay *d, pc_u32 rtt_us);
/* median and spread (p90 - p50) of the window; returns the number of samples, 0 when fewer than PC_MIN_N (outputs untouched) */
pc_u32 pc_delay_estimate(const struct pc_delay *d, pc_u32 *p50_us, pc_u32 *spread_us);
pc_s32 pc_delay_target(const struct pc_delay *d);                     /* delay the estimate asks for (cur when no estimate) */
/* Call at a safe moment (safe=1: between games / at the CSS) or any time (safe=0: returns cur unchanged).
   Up: at once.  Down: one step, only after PC_DOWN_VOTES safe moments in a row with target(rtt + PC_HYST_US) < cur. */
pc_s32 pc_delay_commit(struct pc_delay *d, int safe);
pc_s32 pc_delay_pair(pc_s32 own, pc_s32 peer);       /* two consoles: the larger one */
pc_u32 pc_rtt_pair(pc_u32 own_us, pc_u32 peer_us);                     /* ... or the larger RTT (feed to pc_delay_raw) */

/* ---- (2) time sync after frame 120 with the guard ---- */
struct pc_ts {
    pc_s32 over, last_corr;          /* as s7 ts_over / ts_last_corr */
    pc_s32 pend, pend_frame, pend_off, pend_k;   /* last correction waiting to be judged */
    pc_s32 bad;                      /* consecutive corrections that did not move the offset */
    pc_s32 tripped, trip_off;        /* 1: guard fired (offset trip_off is read as a bias): no corrections until the offset moves by a frame */
    pc_s32 guard;                    /* 0: behave exactly like s7_core.c time_sync (for the differential test) */
};
void   pc_ts_init(struct pc_ts *g, int guard);
/* One sync point after frame 120 (frame % 30 == 0, not skipping).  mode 2 or 3 (others: 0).  thr_us/gap: 0 = default (12.5 ms/60, 25 ms/150).
   Returns the number of frames to stall (0 = none).  The mean offset off_us is s7_time_offset_us. */
pc_s32 pc_ts_step(struct pc_ts *g, int mode, pc_s32 frame, pc_s32 off_us, pc_s32 thr_us, pc_s32 gap);

/* ---- (3) mode selection ---- */
enum { PC_PEER_NONE = 0, PC_PEER_TIMEOUT, PC_PEER_REJECTED, PC_PEER_SLIPPI, PC_PEER_CONSOLE };
#define PC_PAIR_MODE2 1              /* cfg bit: allow mode 2 (with guard) between recognised consoles (default off until E1/E2) */
struct pc_cfg { pc_s32 ts_mode, delay_floor, delay_cap, use_guard, equal_delay, peer_level; };
/* peer: recognition result, flags: PC_PAIR_MODE2.  in_match != 0: recognition is never upgraded or downgraded mid-game: *prev is returned. */
void   pc_select(struct pc_cfg *out, int peer, pc_u32 flags, int in_match, const struct pc_cfg *prev);

#endif
