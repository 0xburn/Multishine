/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Auto input delay.  The player's explicit choice "Online delay: Auto".
 * Standalone, C89, integers only (no FPU types: the console build is checked for FPU use), no libc, no heap, no globals.
 * Python twin: (must agree bit for bit).
 *
 * Rule:  D = max(floor, ceil((RTT/2) / 16.683 ms) + 1)  =  ceil(RTT / 33.366 ms) + 1,
 * then + 1 on a jittery link, then clamped to [floor, cap].  The delay is LOCAL: never negotiated, never on the wire.
 * Estimator: the RTT samples (microseconds) of a small ring; zero and absurd (> 2 s) samples are junk and ignored; the top
 * quarter of the rest is dropped (ENet pings only err upward: queueing, retransmits, the peer's poll grid); the estimate is the
 * lower median of what is left.  Fewer than DA_MIN_N valid samples: NOT confident, the result is the manual fallback.
 */
#ifndef MS_DELAY_AUTO_H
#define MS_DELAY_AUTO_H

typedef int da_s32;
typedef unsigned int da_u32;

#define DA_FRAME_US     16683u       /* Slippi frame time constant (= S7_FRAME_US) */
#define DA_STEP_US      (2u * DA_FRAME_US)
#define DA_RING         16           /* samples kept */
#define DA_MIN_N        5            /* valid samples needed for a confident estimate */
#define DA_MAX_US       2000000u     /* a sample above this is a lost ack, ignored */
#define DA_JITTER_US    30000u       /* (75th percentile - median) above this: one more frame ("one frame more if jitter exceeds 30 ms") */
#define DA_FLOOR        2            /* online floor (Slippi default) */
#define DA_CAP_DEFAULT  5            /* our maximum, default 5 */
#define DA_MAX          15           /* the options range */
#define DA_OFFLINE_FALLBACK 2        /* Auto where no probe can exist (offline "Use delay offline") */

enum { DA_R_FEW = 0,                 /* too few samples: fallback (not confident) */
       DA_R_FLOOR = 1,               /* the rule asked for <= floor: low ping */
       DA_R_RULE = 2,                /* the rule */
       DA_R_JITTER = 3,              /* the rule plus one frame for a jittery link */
       DA_R_CAP = 4 };               /* the rule asked for more than the cap */

struct da_state {
    da_u32 ring[DA_RING];            /* raw samples in microseconds (junk too: it is counted, not used) */
    da_u32 n, head;                  /* samples stored (<= DA_RING), next write index */
};

struct da_result {
    da_s32 delay;                    /* frames, floor..cap (the fallback, clamped, when not confident) */
    da_s32 raw;                      /* what the rule asked for before the cap (0 when not confident) */
    da_u32 rtt_us;                   /* estimate (0 when not confident) */
    da_u32 spread_us;                /* 75th percentile - median of the kept samples */
    da_u32 ping_ms;                  /* estimate rounded to ms, for the display */
    da_u32 n_valid;                  /* non-junk samples */
    da_s32 reason;                   /* DA_R_* */
    da_s32 confident;                /* 1 when n_valid >= DA_MIN_N */
};

da_s32 da_rule(da_u32 rtt_us);                         /* ceil(rtt/33.366 ms) + 1, no floor/cap (>= 1) */
da_s32 da_clamp(da_s32 d, da_s32 floor_, da_s32 cap);  /* floor_ 1..15, cap floor_..15 */
void   da_init(struct da_state *s);
void   da_reset(struct da_state *s);
void   da_sample(struct da_state *s, da_u32 rtt_us);
/* floor_/cap/fallback as the player set them (cap 0 = DA_CAP_DEFAULT; fallback = the manual default, normally 2) */
void   da_decide(const struct da_state *s, da_s32 floor_, da_s32 cap, da_s32 fallback, struct da_result *r);

/* Options byte (id 7, OPT_DELAY): 1..15 manual; 0 = Auto (an old build reads 0 as out of range and uses its default 2).
   The cap is the byte at payload +0x1C of SDOF v1 (one of the four reserved bytes): 0 or invalid = DA_CAP_DEFAULT. */
#define DA_OPT_AUTO 0u
da_s32 da_opt_is_auto(da_u32 byte);                    /* 1 for the Auto value */
da_s32 da_opt_cap(da_u32 byte);                        /* 2..15 -> itself, else DA_CAP_DEFAULT */
da_s32 da_opt_manual(da_u32 byte, da_s32 dflt);        /* the manual delay of a byte (1..15), dflt for Auto/invalid */

/* Display text (menu-font characters only: no parentheses).  Returns the length; buf is NUL terminated.
   Confident:      "Delay 3 auto, ping 62 ms"   (+ ", capped" | ", jittery link")
   Not confident:  "Delay 2 auto, no ping yet" */
da_s32 da_text(char *buf, da_s32 n, const struct da_result *r);
da_s32 da_text_manual(char *buf, da_s32 n, da_s32 delay);   /* "Delay 3" */

#endif
