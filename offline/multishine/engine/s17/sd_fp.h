/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : S11: the per-line fingerprint of the fingerprint capture (experiment E3). Original work.
 *
 * fp(line) = the 64-bit state (a, b) of an ARX sponge over the line's words w0..w(n-1) (n = 8 for a full 32-byte
 * line, fewer for the tail of a range whose length is not a multiple of 32):
 *   (a, b) = key (sd_fp_key);  per word pair (x, y):  a ^= x;  b += y;  4 rounds R
 *   R(a, b):  a = rotl(a, 24) + b;  b = rotl(b, 3) ^ a          (the Speck64 round function without its key)
 * Each step is a bijection of the 64-bit state for a fixed input, and the injection is a bijection of the word pair for a
 * fixed state, so a change confined to ONE word pair (in particular one word) can never collide; a collision needs
 * changes in two or more pairs whose difference after the rounds in between is cancelled exactly by a later pair.
 * Model: a changed line collides with probability 2^-64 (2^-b for the b-bit truncation used by the TEST variants: the
 * low b bits of a after sd_fp_fin); validated empirically for b = 8..24 on structured synthetic changes (tests/fp_quality.c), on the
 * recorded real frames (tests/fp_replay.c) and in Dolphin (README); not provable for 64 bits.
 * Rejected designs (README, "Fingerprint design"): two independent 32-bit rotate-multiply lanes (single bits in two
 * words cancelled with probability ~2^-11 in one lane), 2 or 3 rounds per pair (16-bit windows of the state showed
 * excess collisions for bits flipped in two words: 1490 / 94 per 10^6 instead of 15), NH (UMAC) truncated to the high
 * halves (small arithmetic changes never reach the high word: 1-16 % collisions).
 * PowerPC cost per full line: 8 lwz + 8 injections + 16 rounds x 4 ops = 72 integer ops (one serial chain); counted on
 * the devkitPPC -O2 object in README.
 */
#ifndef SD_FP_H
#define SD_FP_H

#define SD_FP_ROTL(x, r) (((x) << (r)) | ((x) >> (32 - (r))))
/* one ARX round on the 64-bit state (a, b): the Speck64 round function without the round key */
#define SD_FP_R(a, b) do { a = SD_FP_ROTL(a, 24) + b; b = SD_FP_ROTL(b, 3) ^ a; } while (0)
/* absorb one word pair: x into a (xor), y into b (add), then 4 rounds */
#define SD_FP_P(a, b, x, y) do { a ^= (x); b += (y); SD_FP_R(a, b); SD_FP_R(a, b); SD_FP_R(a, b); SD_FP_R(a, b); } while (0)

/* full 32-byte line: out = (a, b) after the last pair (no finalisation) */
static inline __attribute__((always_inline)) void sd_fp8(const u32 *p, u32 ka, u32 kb, u32 *out)
{
    u32 a = ka, b = kb;
    SD_FP_P(a, b, p[0], p[1]);
    SD_FP_P(a, b, p[2], p[3]);
    SD_FP_P(a, b, p[4], p[5]);
    SD_FP_P(a, b, p[6], p[7]);
    out[0] = a;
    out[1] = b;
}

/* TEST variants only: 4 more rounds (a bijection of (a, b), so it changes no collision of the full 64 bits) spread the
   state into the low bits before a weakened fingerprint keeps only b of them */
static inline void sd_fp_fin(const u32 *in, u32 *out)
{
    u32 a = in[0], b = in[1];
    SD_FP_R(a, b); SD_FP_R(a, b); SD_FP_R(a, b); SD_FP_R(a, b);
    out[0] = a;
    out[1] = b;
}

/* n words (1..8, the tail of a range): pairs, an odd last word is paired with 0; same function as sd_fp8 for n = 8 */
static inline void sd_fpn(const u32 *p, u32 n, u32 ka, u32 kb, u32 *out)
{
    u32 a = ka, b = kb, i;
    for (i = 0; i + 1 < n; i += 2)
        SD_FP_P(a, b, p[i], p[i + 1]);
    if (i < n)
        SD_FP_P(a, b, p[i], 0u);
    out[0] = a;
    out[1] = b;
}
#endif
