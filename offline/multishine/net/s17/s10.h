/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : copy of for stage S11 (fingerprint capture); S11: the S11 config block (image
 * +0xA40, "S11CFG01") and telemetry (+0xA80, 'S11T') are appended; code starts at +0xB80.
 * stage S10 (rollback depth) config block, telemetry layout and pure helpers. Original work.
 *
 * The S10 image is the S7 pair-rig image plus:
 *   - a GObj-proc invoke wrapper (04 `bl s10_invoke` at 0x80390DFC, the `blrl` of HSD_GObj_RunProcs that calls
 *     proc->on_invoke(gobj)): per-proc time and RNG-step attribution (TEST ONLY profile) and the trimmed re-simulation
 *     (cfg trim: during re-simulated frames, procs whose GObj p_link is in the trim mask, or whose function is in the
 *     trim list, are not called);
 *   - post-frame capture (capture at the end of the logic of frame F, labelled F+1) and the KEEP / RSET restore of
 *     ;
 *   - TEST ONLY profiles printed with OSReport ("S10" lines) at global frame prof_frame: outer-loop writes into the
 *     manifest (W profile), per-proc cost / RNG, never-written ranges, input-equivalence of rollbacks.
 * Everything is off by default (flags 0, rmode 0, trim 0): the image then behaves as the S7 image.
 */
#ifndef S10_H
#define S10_H

#ifndef S10_U32
#define S10_U32
typedef unsigned int s10_u32;
typedef int s10_s32;
#endif

struct s10_cfg {                    /* image +0x900, 0x40 bytes, magic "S10CFG01" */
    char magic[8];                  /* +0x00 */
    s10_u32 flags;                  /* +0x08 S10F_* */
    s10_u32 trim_lo;                /* +0x0C p_link bits 0..31: procs of these GObj classes are skipped in re-simulated frames */
    s10_u32 trim_hi;                /* +0x10 p_link bits 32..63 */
    s10_u32 prof_frame;             /* +0x14 global frame at which the TEST ONLY profiles are printed (0 = never) */
    s10_u32 rmode;                  /* +0x18 restore mode: 0 S6c (FULL + release), 1 KEEP, 2 RSET (needs S10F_POSTCAP) */
    s10_u32 rs_sel;                 /* +0x1C render-set selection (sd_melee.c SDM_RS_*) */
    s10_u32 nwx;                    /* +0x20 never-written exclusion list: 0 off, 1 on (sd_melee.c, step 3) */
    s10_u32 trim_fn[4];             /* +0x24 extra proc functions skipped in re-simulated frames (0 = unused) */
    s10_u32 res[3];                 /* +0x34 */
};
#define S10F_TRIM 1u                /* trimmed re-simulation (trim_lo/trim_hi/trim_fn) */
#define S10F_PPROF 2u               /* TEST ONLY: per-proc time and RNG steps (normal and re-simulated frames) */
#define S10F_POSTCAP 4u             /* post-frame capture */
#define S10F_RSVERIFY 8u            /* TEST: RSET also compares every non-R range (sd_rs_verify) */
#define S10F_WPROF 0x10u            /* TEST ONLY: outer-loop write profile (live vs M at every logic start after a
                                       post-frame capture); forces a post-frame capture every frame */
#define S10F_NWPROF 0x20u           /* TEST ONLY: never-written profile (per-range change counters) at prof_frame */
#define S10F_EQPROF 0x40u           /* TEST ONLY: input-equivalence profile of rollbacks */
#define S10F_FORCECAP 0x80u         /* TEST: post-frame capture every frame even without prediction (loopback profiles) */
#define S10F_RBCAP_LAST 0x100u      /* inside a rollback capture only the final frame (needs rmode >= 1: the restored
                                       capture G stays as the fallback for a later load of an intermediate frame) */

/* telemetry block (image +0x940, 64 words, 'S10T', canary at 63) */
enum {
    X_MAGIC, X_FLAGS, X_INV_N, X_INV_RS_N, X_INV_SKIP, X_INV_T_NORM, X_INV_T_RS, X_INV_T_SKIPPED_EST,
    X_RNG_NORM, X_RNG_RS, X_RNG_OUTER, X_RNG_TRIMCLS,
    X_POSTCAP, X_B1_FALLBACK, X_B1_HAVE, X_BASE_AFTER_LOAD,
    X_RES_N, X_RES_KEEP, X_RES_RSET, X_RES_DENIED, X_CMP_KB, X_COPY_KB, X_UNDO_KB, X_M_KB, X_RS_LINES, X_RS_MISS_LINES,
    X_RS_MISS_RANGES, X_RS_MISS_RES, X_RS_MISS_A0, X_M_NOMAP, X_KEPT_SUM, X_COPY_MAX,
    X_T_FULL_SUM, X_T_FULL_N, X_T_RSET_SUM, X_T_RSET_N,
    X_W_OUT_N, X_W_OUT_LINES, X_W_OUT_MAX, X_W_IN_N, X_W_IN_LINES, X_W_IN_MAX,
    X_EQ_RB, X_EQ_A, X_EQ_B, X_EQ_C, X_EQ_FRAMES, X_EQ_NOOLD, X_EQ_IN_ONLY_FR,
    X_PROF_STATE, X_PROF_LINES, X_ERR, X_LAST_RMODE, X_PRESEED_DRAWS, X_RS_BYTES_LAST, X_UNSORTED,
    X_NW_STATIC_KB, X_NW_CELL_KB, X_NW_TOTAL_KB,
    X_CANARY = 63
};

/* ---- S11: fingerprint capture config (image +0xA40, 0x40 bytes) and telemetry (+0xA80, 64 words) ---- */
struct s11_cfg {
    char magic[8];                  /* +0x00 "S11CFG01" */
    s10_u32 fp;                     /* +0x08 bit0: fingerprint capture on; bit1: random key per match (timebase) */
    s10_u32 fp_bits;                /* +0x0C 0 or 64 = full; TEST ONLY 1..63 = weakened */
    s10_u32 fpv;                    /* +0x10 verify every N-th delta capture (0 off) */
    s10_u32 fpv_fix;                /* +0x14 verify repairs missed lines */
    s10_u32 cold;                   /* +0x18 cold-site mode (S11 part 2, sd_melee.c) */
    s10_u32 cold_n;                 /* +0x1C cold cells are fingerprinted every cold_n-th capture */
    s10_u32 site_prof;              /* +0x20 TEST ONLY: allocation-site profile printed at global frame site_frame */
    s10_u32 site_frame;             /* +0x24 */
    s10_u32 s16_flags;              /* +0x28 S16: S16F_* (strict engine / gate switches) */
    s10_u32 s16_arena_kb;           /* +0x2C S16 TEST: cap the mirror+table arena to this many KB (0 = no cap) */
    s10_u32 s16_log_pct;            /* +0x30 S16: log share of the store in percent (0 = 32) */
    s10_u32 s17_flags;              /* +0x34 S17: sd_s17 engine kernels (net/s17.h): 1 G, 2 H, 4 U, 8 LOGZ; 0 = the S16 engine */
    s10_u32 res[2];                 /* +0x38 */
};
#define S16F_NOSTRICT 0x1u          /* engine strict off (S12 behaviour: incomplete captures are restored); also disables the gate */
#define S16F_NOFP_FORCE 0x2u        /* TEST: fingerprint table off (every range compared in full) */
#define S16F_REBUILD 0x20u          /* OPT-IN, UNRESOLVED: while gated, ask for a forced rebuild every 30 frames.  A rebuild in the game (build mid-match) made
                                       the two sides diverge in Dolphin even in lockstep and with a healthy engine (README S16, runs r2/r2c): off by default */
#define S16F_PROBE 0x40u            /* OPT-IN, UNRESOLVED: probe capture at the first in-game frame end (then forgotten).  Made YS/DL Marth/Sheik diverge at
                                       frame ~258 in Dolphin although the engine was healthy (runs v3-v5 f1/f2): off by default */
#define S16F_NOREBUILD 0x8u         /* (obsolete, rebuild is off by default) */
#define S16F_TESTREB 0x10u          /* TEST: forced rebuild every 30 frames in game even when not gated (rebuild in the normal flow) */
#define S16F_NOGATE 0x4u            /* TEST: strict engine but prediction is not stopped (negative control: refusals show) */
#define S16F_B2MISS_CONT 0x80u      /* TEST ONLY: a refused restore (b2_miss) does NOT end the match (the behaviour before review fix 3: the ASM re-simulates on un-restored memory = silent desync; negative control for the pair-rig injector) */
enum {                              /* S11 telemetry words */
    Y_MAGIC, Y_FP, Y_BITS, Y_FPV, Y_KEYA, Y_KEYB,
    Y_LINES_HI, Y_LINES, Y_HIT, Y_HOLED, Y_VCAPS, Y_VLINES, Y_VCHG, Y_VMISS, Y_VMISS_CAPS, Y_VMISS_A0, Y_TAB_BAD, Y_FALSE,
    Y_SH8, Y_SH12, Y_SH16, Y_SH20, Y_SH24, Y_BASE_LINES, Y_TABLE_B, Y_LFP_B, Y_MIRROR_B, Y_LOG_ENTS,
    Y_TCAP_SUM, Y_TCAP_N, Y_TCAPV_SUM, Y_TCAPV_N, Y_LINES_SUM, Y_ERR,
    Y_COLD_SKIP_LINES, Y_COLD_CELLS, Y_COLD_SCANS, Y_COLD_INVAL, Y_COLD_MISS, Y_COLD_VLINES, Y_SITES, Y_COLD_BYTES,
    Y_COLD_RESCANS, Y_COLD_MISS_CAPS, Y_COLD_SKIP_CAPS, Y_COLD_TOUCH,
    /* S16 (words 46..62) */
    Y_S16_FLAGS = 46, Y_S16_LOST_RANGES, Y_S16_LOST_BYTES, Y_S16_REFUSED, Y_S16_DEEP, Y_S16_REB_OK, Y_S16_REB_FAIL,
    Y_S16_ARENA_HI, Y_S16_FP_LIM, Y_S16_FP_COV, Y_S16_INC_CAPS, Y_S16_INC_ONSETS, Y_S16_GATE_FRAMES, Y_S16_GATE_INC,
    Y_S16_GATE_LOW, Y_S16_UNSAFE_RB, Y_S16_GATE_REB,
    Y_CANARY = 63
};

/* number of LCG steps (HSD_Rand: s' = s * 214013 + 2531011) from a to b, or `limit` + 1 if more */
static inline s10_u32 s10_lcg_steps(s10_u32 a, s10_u32 b, s10_u32 limit)
{
    s10_u32 n = 0;
    while (a != b && n <= limit) {
        a = a * 214013u + 2531011u;
        n++;
    }
    return n;
}

/* p_link membership in the 64-bit trim mask */
static inline int s10_in_mask(s10_u32 lo, s10_u32 hi, s10_u32 p_link)
{
    if (p_link < 32) return (lo >> p_link) & 1u;
    if (p_link < 64) return (hi >> (p_link - 32)) & 1u;
    return 0;
}

/* input components that differ between two copies of a fighter's current-frame input (fp+0x620 block, ft/types.h:1403:
   lstick[0] +0x620/+0x624, cstick[0] +0x638/+0x63C, triggers[0] +0x650, held_buttons[0] +0x65C, pressed +0x668,
   released +0x66C).  Bits: 1 lstick, 2 cstick, 4 trigger, 8 buttons held, 16 pressed/released.  `a`, `b` point at
   fp+0x620 (20 words). */
static inline s10_u32 s10_inp_diff(const volatile s10_u32 *a, const volatile s10_u32 *b)
{
    s10_u32 m = 0;
    if (a[0] != b[0] || a[1] != b[1]) m |= 1u;
    if (a[6] != b[6] || a[7] != b[7]) m |= 2u;
    if (a[12] != b[12]) m |= 4u;
    if (a[15] != b[15]) m |= 8u;
    if (a[18] != b[18] || a[19] != b[19]) m |= 16u;
    return m;
}
#endif
