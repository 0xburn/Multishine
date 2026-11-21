/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : early rollback start (stage S12), the decision part. Original work.
 *
 * Slippi decides a rollback only in TriggerSendInput.asm (third_party/slippi-ssbm-asm/Online/Core, hook 0x80376A28 in
 * HSD_PadRenewRawStatus, VI pad alarm = interrupt context), once per new frame, section 9 (lines 384-676): for every
 * remote player whose inputs were predicted, it walks the frames from that player's savestate frame up to the newest
 * received remote frame, compares the received input with the predicted one (buttons & 0x1F / 0x7F, both sticks as one
 * word, triggers with the 42 deadzone), advances the player's savestate frame and predicted-input read index over every
 * frame that matched, then sets ODB_SAVESTATE_FRAME (= min over predicting players, starting at the stable finalized
 * frame) and ODB_FINALIZED_FRAME (capped at RXB_SMALLEST_LATEST_FRAME), and triggers the rollback when any frame did
 * not match (ODB_ROLLBACK_IS_ACTIVE = ODB_ROLLBACK_SHOULD_LOAD_STATE = 1, ODB_ROLLBACK_END_FRAME = frame).
 * The engine then runs the rollback from the scene loop's wait loop without a new pad (ForceEngineOnRollback.asm,
 * 0x801A4DB4) and stops at the end frame (LoopEngineForRollback.asm, 0x801A5014).
 *
 * s12_early_check is a byte-exact port of that section 9 over the ODB and an RXB, run from the MAIN LOOP in the idle
 * gap (never from an interrupt): with `commit` 0 it only reports; with `commit` 1 and a mismatch it writes exactly the
 * fields section 9 + TRIGGER_ROLLBACK write, with end frame = the last frame already simulated (ODB_FRAME - 1) and
 * without the ODB_FRAME increment (no new input was taken).  Nothing else is written; when no rollback is needed nothing
 * is written at all (the next B0 repeats the walk with at least the same data).  Pure function of the two buffers:
 * host-tested in tests/s12_unit_test.c.
 */
#ifndef S12_EARLY_H
#define S12_EARLY_H

typedef unsigned int s12_u32;
typedef int s12_s32;
typedef unsigned char s12_u8;

/* Online.s offsets (third_party/slippi-ssbm-asm/Online/Online.s:233-302; MAX_DELAY_FRAMES 15 from ODB_RXB_ADDR = 0xDB) */
#define S12_ODB_FRAME 0x003
#define S12_ODB_IS_GAME_OVER 0x00F
#define S12_ODB_IS_DISCONNECTED 0x010
#define S12_ODB_IS_FRAME_ADVANCE 0x014
#define S12_ODB_RXB_ADDR 0x0DB
#define S12_ODB_ROLLBACK_IS_ACTIVE 0x0DF
#define S12_ODB_ROLLBACK_SHOULD_LOAD_STATE 0x0E0
#define S12_ODB_ROLLBACK_END_FRAME 0x0E1
#define S12_ODB_PRED_READ_IDXS 0x18E
#define S12_ODB_PRED_WRITE_IDXS 0x191
#define S12_ODB_PRED_INPUTS 0x194
#define S12_ODB_SAVESTATE_IS_PREDICTING 0x38C
#define S12_ODB_SAVESTATE_FRAME 0x38D
#define S12_ODB_PLAYER_SAVESTATE_FRAME 0x391
#define S12_ODB_PLAYER_SAVESTATE_IS_PREDICTING 0x39D
#define S12_ODB_STABLE_ROLLBACK_IS_ACTIVE 0xABF
#define S12_ODB_STABLE_FINALIZED_FRAME 0xAC9
#define S12_ODB_SHOULD_FORCE_PAD_RENEW 0xACD
#define S12_ODB_FINALIZED_FRAME 0xADA
#define S12_ODB_SIZE_MIN 0xADE
#define S12_RXB_OPNT_FRAME_NUMS 26
#define S12_RXB_SMALLEST_LATEST_FRAME 38
#define S12_RXB_OPNT_INPUTS 42
#define S12_RXB_SIZE 559                    /* Slippi 3.7.0 (d0ce23a): + despawn frames, predicted inputs, next pad RNG bits (3.6.4: 297) */
#define S12_PAD_RNG_MARKER 0xB               /* PAD_RNG_ROLLBACK_MARKER in a backed-up prediction (ODB_ROLLBACK_PREDICTED_INPUTS) */
#define S12_PAD 12                          /* PAD_REPORT_SIZE */
#define S12_PRED_N 14                       /* PREDICTED_INPUTS_COUNT = 2 * ROLLBACK_MAX_FRAME_COUNT */
#define S12_RXB_IN_N 7                      /* RXB_INPUTS_COUNT */

/* result of one check */
struct s12_eck {
    s12_u32 need;                           /* 1: section 9 would trigger a rollback */
    s12_s32 target;                         /* ODB_SAVESTATE_FRAME it would set (the frame the load restores) */
    s12_s32 fin;                            /* ODB_FINALIZED_FRAME it would set */
    s12_u32 matched;                        /* frames whose received input matched the prediction (all players) */
    s12_u32 players;                        /* remote players with a pending prediction */
    s12_u32 skip;                           /* 0 ran; else why not (S12_ESKIP_*) */
};
enum { S12_ESKIP_NONE, S12_ESKIP_ACTIVE, S12_ESKIP_NOTPRED, S12_ESKIP_OVER, S12_ESKIP_ADVANCE, S12_ESKIP_DISC };

extern int (*s12_relax_hook)(const s12_u8 *a, const s12_u8 *p, const s12_u8 *odb, const s12_u8 *rxb, s12_u32 count, s12_u32 off, s12_s32 frame, s12_u32 rd);   /* S17 RELAX (NULL = strict) */
int s12_early_check(s12_u8 *odb, const s12_u8 *rxb, s12_s32 end_frame, int commit, struct s12_eck *r);

static inline s12_u32 s12_be32(const volatile s12_u8 *p)
{
    return ((s12_u32)p[0] << 24) | ((s12_u32)p[1] << 16) | ((s12_u32)p[2] << 8) | p[3];
}
static inline void s12_put32(volatile s12_u8 *p, s12_u32 v)
{
    p[0] = (s12_u8)(v >> 24); p[1] = (s12_u8)(v >> 16); p[2] = (s12_u8)(v >> 8); p[3] = (s12_u8)v;
}
#endif
