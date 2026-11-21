/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : stage S12 config block and telemetry layout. Original work.
 *
 * The S12 image is the S11 image (copied to, changes marked S12:) plus:
 *   - more re-simulation trim classes: a list of 8 more proc functions skipped in re-simulated frames (cfg trim_fn[],
 *     S12F_TRIMX), and pre-hooks on the functions Slippi's rollback loop calls between re-simulated frames
 *     (LoopEngineForRollback.asm FN_ExecCameraTasks) so they can be skipped there (S12F_TRIMCAM);
 *   - early rollback start (S12F_EARLY): the scene loop's wait loop (04 `bl s12_idle_stub` at 0x801A4DA8, main thread)
 *     polls the network; when remote inputs arrived since the last check (flag set by drain, which also runs from the
 *     B0 interrupt), it runs a port of TriggerSendInput section 9 (s12_early.c) and, when a rollback is needed and the
 *     measured cost fits the time left before the next VI, starts it right there (Slippi's ForceEngineOnRollback runs
 *     it from the same wait loop).  Optional render skip after such a rollback (S12F_EARLY_NORENDER, 04 `bl
 *     s12_render_stub` at 0x801A5030), with Slippi's camera tasks run instead;
 *   - per-frame re-simulation timing (frames 1..8 of a rollback separately) and a Dolphin tick breakdown (S12F_RSTIME);
 *   - TEST ONLY divergence locator (S12F_LOC chunk hashes, S12F_LOCW raw words), OSReport "S12C"/"S12W" lines;
 *   - gameplay-hash scope fix for render-written fighter bits (S12F_GPMASK, trace hash only, see README).
 * Everything is off by default (flags 0): the image then behaves as the S11 image.  Telemetry names are Z_* / ZL
 * (S11 owns Y_* / YL).
 */
#ifndef S12_H
#define S12_H

struct s12_cfg {                    /* image +0xB80 (S12 on S11; +0xA40 in the S10-based WIP), 0x40 bytes, magic "S12CFG01" */
    char magic[8];                  /* +0x00 */
    unsigned int flags;             /* +0x08 S12F_* */
    unsigned int trim_fn[8];        /* +0x0C more proc functions skipped in re-simulated frames (S12F_TRIMX) */
    unsigned int early_guard;       /* +0x2C ticks kept free before the expected VI (0 = 1 ms) */
    unsigned int early_pct;         /* +0x30 % of the time left before the VI an early rollback may use (0 = 90) */
    unsigned int early_poll_us;     /* +0x34 network poll interval in the idle gap (0 = 500 us) */
    unsigned int loc_sel;           /* +0x38 LOCW: (player << 12) | (secondary << 8) | chunk; also NEG_EARLY frame */
    unsigned int loc_lo;            /* +0x3C locator: first frame printed (S12C/S12W), lines up to loc_lo + 4095 */
};
#define S12F_TRIMX 0x1u             /* skip trim_fn[] in re-simulated frames */
#define S12F_EARLY 0x2u             /* early rollback start in the idle gap */
#define S12F_EARLY_NORENDER 0x4u    /* no render pass after an early rollback; Slippi's camera tasks instead */
#define S12F_RSTIME 0x8u            /* per-frame re-simulation timing and tick breakdown */
#define S12F_LOC 0x10u              /* TEST ONLY: per finalized frame chunk hashes of the gameplay-hash scope */
#define S12F_LOCW 0x20u             /* TEST ONLY: per finalized frame raw words of chunk loc_sel */
#define S12F_GPMASK 0x40u           /* gameplay trace hash: also mask the render-written fighter bits (README S12) */
#define S12F_TRIMCAM 0x80u          /* skip Slippi's between-frame camera tasks inside a rollback (except the last) */
#define S12F_NEG_EARLY 0x100u       /* NEGATIVE CONTROL: the first early rollback at or after frame loc_sel flips one
                                       remote button bit in the RXB it re-simulates with (must diverge) */
#define S12F_EARLY_DRY 0x200u       /* early check only (count what would have started), never commit */
/* S12 built-in trim classes (proc functions skipped in re-simulated frames; static review in README "Trim classes") */
#define S12F_C_DYN 0x400u           /* fighter / item dynamic bones: Fighter_8006D9AC (prio 0x10), Item_8026A788 */
#define S12F_C_CAM 0x800u           /* camera: fn_8002F360 (p_link 18), Fighter_UnkCallCameraCallback_8006D9EC */
#define S12F_C_COSM 0x1000u         /* small cosmetic procs: Ground_801C461C, ftCo_8009F480, lbBgFlash_Proc 80021B04,
                                       fn_800204C8, fn_802FED14, fn_801A0E34 */
#define S12F_C_EFLIB 0x2000u        /* efLib_Update 8005BC50 (frees effects: not heap-neutral) */
#define S12F_C_QUAKE 0x4000u        /* grLib quake procs 801C9C40 / 801C9BC8 (free themselves) */
#define S12F_C_DYNB 0x8000u         /* S12 (on S11): dynamic-bone simulation only: lb_8001044C called from inside
                                       Fighter_8006D9AC / Item_8026A788 returns at once in re-simulated frames, after the
                                       parent-bone matrix update it would have done (dirty + setup); the hurt/collider
                                       update, Marth/Roy's ftCo_8009E614 and Slippi's DynamicsFix (C2 0x8009E090, matrix
                                       setup of every dirty fighter bone) still run.  04 `b s12_dyn_stub` at 0x8001044C. */
#define S12F_EARLY_OVR 0x10000u      /* S12 (on S11): an early rollback whose estimate exceeds the idle budget by at most half
                                       a frame starts anyway (the rest would run at the next frame start; the overrun now
                                       lands in this idle gap) */
#define S12F_C_ANY (S12F_C_DYN | S12F_C_CAM | S12F_C_COSM | S12F_C_EFLIB | S12F_C_QUAKE | S12F_C_DYNB)

/* telemetry block (image +0xBC0, 128 words, 'S12T', canary at 127) */
enum {
    Z_MAGIC, Z_FLAGS,
    Z_E_POLLS, Z_E_RXNEW, Z_E_CHECKS, Z_E_NEED, Z_E_START, Z_E_DENY_BUDGET, Z_E_DENY_OTHER, Z_E_SKIP_ACTIVE,
    Z_E_SKIP_NOTPRED, Z_E_SKIP_FRAME, Z_E_DONE, Z_E_TICKS, Z_E_EST, Z_RB_NORMAL, Z_RB_NORMAL_TICKS, Z_E_VI_DURING,
    Z_E_NORENDER, Z_E_DEPTH_H, /* 19..26 */
    Z_E_LEFT_SUM = Z_E_DEPTH_H + 8, Z_E_LATE, Z_E_MATCHED, Z_E_NETPOLLS, Z_E_CAMTASKS,
    Z_RS_SUM = 32, /* 32..39 logic ticks of re-simulated frame 1..8+ of a rollback */
    Z_RS_N = 40, Z_RS_MAX = 48,
    Z_T_RESTORE = 56, Z_T_GAP, Z_T_GAP_N, Z_T_TRACE_RB, Z_T_CAP_RB, Z_E_SHOULD_RENEW, Z_E_SKIP_OVER, Z_E_SKIP_PADQ,
    Z_X_SKIP = 64, /* 64..71 skipped calls per trim_fn[] entry */
    Z_CAM_SKIP = 72, /* 72..73 skipped camera-task calls per hooked function */
    Z_DYN_SKIP = 74, /* S12 (on S11): lb_8001044C calls skipped (S12F_C_DYNB) */ Z_DYN_RUN, /* lb_8001044C calls run inside a DYNB proc */
    Z_S16_ARENA_B = 76, Z_S16_FREE, Z_S16_STEP_NEW_MAX, Z_S16_EARLY_DENY, /* 76..79: S16 (arena bytes, free bytes, largest new-cell step, early rollbacks denied: no complete capture) */
    Z_LOC_LINES = 80, Z_LOC_FULL, Z_E_NEG, Z_E_EST_ERR_SUM, Z_E_EST_UNDER, Z_E_RB_NORM_DEPTH_H, /* 85..92 */
    Z_E_FIRST_FRAME = 93, Z_RS_FIRST_SUM, Z_RS_REST_SUM, Z_RS_REST_N, Z_E_IDLE_LEFT_MIN, Z_RENDER_EMA, Z_E_ERR,
    Z_C_SKIP = 100, /* 100..111 skipped calls per built-in class entry (s12_gc.c S12_CLS) */
    Z_TIMER_KEEP = 112, /* ifTime_UpdateTimers kept in a re-simulated frame near the 5-second countdown */
    Z_VI_IRQ_LINE, Z_VI_VMAX,
    Z_STEAL_KEEP, /* 115: S12 (on S11): fn_802F9410 (stock HUD) kept in a re-simulated frame while a team stock-steal slot
                     animates (590 -> ifStock_802F7EFC reads those HUD slots, :70-76) */
    Z_HEAP_FREE_MIN, Z_HEAP_BIG_MIN, Z_HEAP_FREE_LAST, Z_HEAP_MIN_FRAME, /* 116..119: HSD heap headroom (S12F_RSTIME) */
    Z_E_OVR, /* 120: early starts over budget (S12F_EARLY_OVR) */ Z_REST_K_USED, /* 121: estimates with a per-depth restore */
    Z_REST_K, /* 122..126: restore ticks EMA for depth 1..5 */
    Z_CANARY = 127
};
#endif
