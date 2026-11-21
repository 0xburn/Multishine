/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SDENT_H
#define SDENT_H
/* sdent: the console entropy pool.  A credit account over the existing
   ChaCha20 fast-key-erasure generator of sdcrypto (sdc_rng): every raw event is mixed into the SHA-256 pool, but only what a source has EARNED is
   credited, in milli-bits, per source and with a per-source cap, so that no single source can make a key.  Live sources pass SP 800-90B style
   health tests (repetition count and adaptive proportion) before their credit is released.  Nothing is ever output below the requested level:
   a starved pool says so (SDENT_E_STARVED) and the caller shows the "move the stick" screen; it never falls back to weak output.
   Strict C89, no libc, no heap, no clock.  CREDIT RATES ARE PROVISIONAL until the console measurements of are in. */
#include "sdcrypto.h"
typedef unsigned short sdent_u16;

#define SDENT_L_SESSION 128u        /* ephemeral keys, nonces, TLS client random: credited bits (any source) */
#define SDENT_L_LONGTERM 192u       /* device identity key: credited bits from sources other than the stored seed (see docs: 256 considered, 192 chosen) */

enum { SDENT_STICK, SDENT_PRESS, SDENT_JITTER, SDENT_PROV, SDENT_STORED, SDENT_SERVER, SDENT_DIGI, SDENT_NSRC };

/* credit per qualifying sample, milli-bits (provisional, conservative: about half of what a first look at the measurements supports) */
#define SDENT_STICK_MBITS 250u      /* per frame in which the stick moves and the symbol passed the health tests */
#define SDENT_PRESS_MBITS 1000u     /* per button press whose spacing differs by 2 frames or more from the previous spacing */
#define SDENT_DIGI_MBITS 500u       /* per digital event (any button press OR release, or a trigger click crossing its threshold: what a box controller, which has no analog stick, produces) whose spacing from the previous event differs by 2 frames or more from the previous spacing; half a bit because a release follows its press */
#define SDENT_DIGI_TRIG 100u        /* an analog trigger counts as clicked from this value (the click of a digital-only pad's L / R maps to 255) */
#define SDENT_JITTER_MBITS 0u       /* time-base / VI phase jitter: mixed, not credited, until measured on the console */
#define SDENT_MOVE_MIN 3u           /* summed absolute change of the four stick axes between frames that counts as "moving" */
/* per-source caps, bits.  Every cap is below SDENT_L_LONGTERM, so a long-term key needs at least two kinds of source. */
#define SDENT_CAP_STICK 160u
#define SDENT_CAP_PRESS 48u
#define SDENT_CAP_JITTER 24u
#define SDENT_CAP_PROV 128u
#define SDENT_CAP_STORED 128u
#define SDENT_CAP_SERVER 0u
#define SDENT_CAP_DIGI 160u         /* press (48) + digital timing (160) = 208 >= SDENT_L_LONGTERM: a pad with ONLY digital inputs (box controller) can reach the device-key level by buttons and trigger clicks alone, from two kinds of source; an analog pad earns this source too, the stick cap does not change */

/* health tests on the 8-bit stick symbol (claimed min-entropy 0.5 bit/sample, alpha = 2^-20): repetition count cutoff 1 + ceil(20 / 0.5) = 41;
   adaptive proportion over a window of 64 samples with cutoff = 1 + CRITBINOM(64, 2^-0.5, 1 - 2^-20) = 61 (tests/test_sdent.py recomputes it) */
#define SDENT_RCT_CUTOFF 41u
#define SDENT_APT_WINDOW 64u
#define SDENT_APT_CUTOFF 61u

#define SDENT_E_STARVED (-1)
#define SDENT_BATCH 64u

struct sdent_health {
    sdc_u8 last, ref;
    sdc_u32 run, wn, wcnt, pend;        /* repetition run; samples in the window; occurrences of ref; credit held back until the window passes (milli-bits) */
    sdc_u32 rct_trips, apt_trips, windows_ok;
};

struct sdent {
    struct sdc_rng rng;
    sdc_u32 mbits[SDENT_NSRC];          /* credited milli-bits per source (<= cap) */
    sdc_u32 mtotal;
    struct sdent_health h[SDENT_NSRC];
    sdc_u8 batch[SDENT_BATCH];
    sdc_u32 bn;
    sdc_u8 prev[6], pd[4], pdt[2];      /* pdt: previous trigger step (the triggers count as two more moving axes) */
    sdc_u8 have_prev;
    sdent_u16 prev_buttons;
    sdc_u32 frame, last_press, last_gap, n_press, n_moving, n_samples, calls;
    sdc_u8 have_press, have_gap;
    sdc_u32 prev_dig, last_dev, last_dgap, last_dgap2, n_dig;     /* digital state (buttons + trigger clicks), frame of the last digital event, its spacing, events */
    sdc_u8 have_dev, have_dgap;
};

struct sdent_pad { sdent_u16 buttons; signed char sx, sy, cx, cy; sdc_u8 tl, tr, err; };   /* the player's port, as read by PADRead */

void sdent_init(struct sdent *e);
/* once per frame for the port being used.  tb = low 32 bits of the time base at the read.  Everything is mixed; credit follows the rules above. */
void sdent_feed_pad(struct sdent *e, const struct sdent_pad *p, sdc_u32 tb);
/* mix without credit: server seed over the wire, console-unique data (BBA MAC, boot time), anything an observer might know */
void sdent_mix(struct sdent *e, const sdc_u8 *p, sdc_u32 n);
/* a 32-byte seed from the card file (SDENT_STORED, credit min(bits,128) once) or from the PC card tool (SDENT_PROV) */
void sdent_load_seed(struct sdent *e, int kind, const sdc_u8 seed[32], sdc_u32 credited_bits);
/* credited bits at the level, 0 if not ready: returns whole credited bits relevant to the level */
sdc_u32 sdent_credit(const struct sdent *e, sdc_u32 level);
int sdent_ready(const struct sdent *e, sdc_u32 level);
/* random bytes if the pool is ready at `level`, else SDENT_E_STARVED and nothing is written */
int sdent_bytes(struct sdent *e, sdc_u32 level, sdc_u8 *out, sdc_u32 n);
/* adapter for sdtls_rand_fn: session level; nonzero = not ready */
int sdent_rand_session(void *ctx, sdc_u8 *out, sdc_u32 n);
/* a fresh seed for the card file (domain separated from every other output); *bits = credited bits to record with it, at most 128 */
int sdent_export_seed(struct sdent *e, sdc_u8 seed[32], sdc_u32 *bits);
/* the card file of the stored seed ("SDSD" ver flags bits(2) seed(32) crc32 = 44 bytes): written after a boot that earned credit, read by the next boot */
#define SDENT_SEED_REC_LEN 44
void sdent_seed_pack(const sdc_u8 seed[32], sdc_u32 bits, sdc_u8 out[SDENT_SEED_REC_LEN]);
int sdent_seed_parse(const sdc_u8 *in, sdc_u32 n, sdc_u8 seed[32], sdc_u32 *bits);   /* 0, or -1 short, -2 magic/version, -3 crc */
/* progress for the "move the stick" screen, 0..100 percent toward the level */
sdc_u32 sdent_progress(const struct sdent *e, sdc_u32 level);
#endif
