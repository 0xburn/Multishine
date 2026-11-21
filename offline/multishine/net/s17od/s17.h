/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17/net: stage S17 config word and telemetry layout.  Original work.
 *
 * Config: s11_cfg.s17_flags (S11 config block, image +0xA40 + 0x34; patch key s17_flags) = the engine's sd_s17 bits (sd_snap.h
 *   SD_S17_*): 1 G (DMA capture compare, kbench4 G64), 2 H (streamed restore, H0), 4 U (undo with dcbz, U1), 8 LOGZ (log lines
 *   zeroed ahead); TEST ONLY: 0x100 NEG_G, 0x200 NEG_H, 0x400 NEG_U (deliberately faulty kernels, the match must diverge),
 *   0x800 NODCBST (G without the dcbst of the live lines: console control), 0x1000 FORCE_BUSY (every pass behaves as if the locked cache
 *   were in use: the CPU fallback path runs, the forced-fallback run), 0x10 DD (deduplicated undo chain), 0x20 PE (pass-end capture in the idle
 *   gap), 0x40 CENSUS (live against the newest mirror at every load), 0x80 CHAIN (restore by the undo chain only when the newest capture is fresh),
 *   0x2000 CHAIN_VERIFY (every chain-only restore first verifies live == M and falls back to the full restore, counted), 0x800000 NOGUARD (opt-out of the capture-gap guard, which is on by default: RXB latest capped at capture + 6, rollback captures at most 3 apart; 0x400000 is a no-op), 0x1000000 CHAIN_PROD (production chain-only, default OFF; needs PE 0x20), 0x4000 RELAX (relaxed stick
 *   compare, step 3).  0 = the S16 engine.  G replaces the fingerprint
 *   (the engine forces fp off); the log takes the S16 share (32 %) and the mirror the rest.
 * Telemetry: the global s17_tel (128 words, magic 'S17T', canary 0x5A5AC0DE at word 127), address in the build json "x17_addr"
 *   (from the symbol table: the block is ordinary data, not one of the fixed offsets).  Counters are cumulative since the match
 *   start; ticks are Time Base ticks.  Depth d = steps undone by a restore (0 = the
 *   newest capture, 7 = 7 or more); the per-depth sums are the engine's own cost of a rollback: restore (rest_t), step 1
 *   live := M (step1_t), step 2 undo (undo_t); per undo step = undo_t[d] / (d * rest_n[d]).
 */
#ifndef S17_H
#define S17_H
enum {
    Q_MAGIC, Q_FLAGS, Q_FLAGS_EFF,
    Q_G_PASSES, Q_G_BUSY, Q_G_FAIL, Q_G_TICKS_LAST, Q_G_TICKS_SUM, Q_G_TICKS_MAX, Q_G_LINES, Q_G_CMDS, Q_G_BATCHES, Q_G_CHG,
    Q_H_PASSES, Q_H_BUSY, Q_H_FAIL, Q_H_TICKS_LAST, Q_H_TICKS_SUM, Q_H_TICKS_MAX, Q_H_PLAN_LAST, Q_H_PLAN_SUM, Q_H_BYTES, Q_H_SEGS, Q_H_CMDS,
    Q_U_WHOLE, Q_U_PART, Q_RF_HIT, Q_RF_MISS, Q_LOGZ, Q_DMA_ERR, Q_MFLUSH,
    Q_TCAP_SUM, Q_TCAP_N, Q_TCAP_MAX, Q_TRES_MAX, Q_CAPTURES, Q_BASE_CAPS, Q_RESTORES, Q_LINES_SUM, Q_UNDO_KB, Q_FP_EFF,
    Q_PACE_DELAY = 125, Q_PACE_N = 124, Q_PACE_MODE = 123,   /* S18 pacing: delay committed at the last match setup, RTT samples, ts_mode chosen (these overlap the top of the relax block; PACE and RELAX are not run together) */
    Q_CH_LATCH = 42,                    /* production chain-only latched OFF (a residual line outside the learned set) */
    Q_CH_NEG = 41,                      /* injected unlogged writes (CHAIN_NEG, TEST ONLY) */
    Q_DD_USED = 43, Q_DD_FB, Q_DD_ENTS, Q_DD_APPL, Q_DD_LINES,   /* 43..47  S17 DD: deduplicated restores / pre-pass fallbacks / entries seen / entries applied / distinct lines */
    Q_REST_N = 48,                      /* 48..55  restores by depth */
    Q_REST_T = 56,                      /* 56..63  total ticks by depth */
    Q_UNDO_T = 64,                      /* 64..71  ticks of the undo (step 2) by depth */
    Q_STEP1_T = 72,                     /* 72..79  ticks of step 1 (live := M) by depth */
    Q_PE_N = 80, Q_PE_FAIL, Q_PE_SKIP,                 /* 80..82  pass-end captures taken / failed / idle passes without one (not predicted, stride, gate) */
    Q_C_LOADS = 83, Q_C_VALID, Q_C_ZERO, Q_C_LINES, Q_C_MAX,   /* 83..87  census: loads seen / with a fresh newest capture (census taken) / of those with 0 residual lines / sum of residual lines / max */
    Q_C_H0 = 88,                        /* 88..92  histogram of residual lines per load: 0, 1-2, 3-8, 9-32, 33+ */
    Q_C_PRES = 93, Q_C_STATIC, Q_C_CELL, Q_C_NOTFRESH,  /* 93..96  lines inside the preserve blocks (excluded) / residual static lines / residual cell lines / loads without a fresh capture */
    Q_CH_N = 97, Q_CH_FB, Q_CH_DENY,    /* 97..99  chain-only restores run / requested but the verify found residual lines (full restore ran) / requested but not fresh */
    Q_C_TAB = 100,                      /* 100..113 census table: 7 x (line address, loads in which it differed) */
    Q_CH_FIX = 126,                     /* chain-only restores that first repaired residual lines from the mirror (CHAIN_FIX) */
    Q_RX = 114,                         /* 114..125 relaxed stick compare (s17_relax.h RXT_*): calls, accepts, denied: other field / dead zone / no context / guard / port / ring patch, patch #1, patch #2, patch #2 skipped, busy */
    Q_DA_DELAY = 100, Q_DA_PING, Q_DA_REASON, Q_DA_NVALID, Q_DA_CONF, Q_DA_SPREAD, Q_DA_RAW, Q_DA_SAMPLES, Q_DA_DECISIONS,   /* 100..108  AUTO DELAY (S7F_DELAY_AUTO), overlaps the census table (lab block, not run together): the delay this side plays, estimate ms, reason (DA_R_*), valid samples, confident, spread us, what the rule asked before the cap, raw samples fed, decisions made */
    Q_DA_RING = 109,                    /* 109..112  the first 4 ring slots (microseconds) at the last decision */
    Q_CANARY = 127
};
struct s17_tel { volatile unsigned int w[128]; };
#endif
