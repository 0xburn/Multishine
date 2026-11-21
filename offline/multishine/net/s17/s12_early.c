/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : port of TriggerSendInput.asm section 9 (see s12_early.h). Original work; the logic follows
   third_party/slippi-ssbm-asm Online/Core/TriggerSendInput.asm lines 384-676 (an earlier revision), cited per block. */
#include "s12_early.h"

static s12_s32 rd32(const s12_u8 *p) { return (s12_s32)s12_be32(p); }

/* TriggerSendInput.asm 482-530: buttons (& 0x1F, & 0x7F), both sticks as one word (+2..+5), triggers +6/+7 with the
   42 deadzone (both <= 42: equal) */
static int inputs_match(const s12_u8 *a, const s12_u8 *p)
{
    int k;
    if ((a[0] & 0x1Fu) != (p[0] & 0x1Fu)) return 0;
    if ((a[1] & 0x7Fu) != (p[1] & 0x7Fu)) return 0;
    if (a[2] != p[2] || a[3] != p[3] || a[4] != p[4] || a[5] != p[5]) return 0;
    for (k = 6; k < 8; k++) {
        if (a[k] <= 42 && p[k] <= 42) continue;
        if (a[k] != p[k]) return 0;
    }
    return 1;
}

/* S17 RELAX: set by the glue when the relaxed stick compare is on (net/s17_relax.c); the stick word is the only thing it may relax */
int (*s12_relax_hook)(const s12_u8 *a, const s12_u8 *p, const s12_u8 *odb, const s12_u8 *rxb, s12_u32 count, s12_u32 off, s12_s32 frame, s12_u32 rd);

/* inputs_match with the relaxed main-stick rule: identical to inputs_match unless only the stick word differs (buttons and triggers strict) */
static int inputs_match_rx(const s12_u8 *a, const s12_u8 *p, const s12_u8 *odb, const s12_u8 *rxb, s12_u32 count, s12_u32 off, s12_s32 frame, s12_u32 rd)
{
    int k;
    if (inputs_match(a, p)) return 1;
    if (!s12_relax_hook) return 0;
    if ((a[0] & 0x1Fu) != (p[0] & 0x1Fu) || (a[1] & 0x7Fu) != (p[1] & 0x7Fu)) return 0;
    for (k = 6; k < 8; k++) {
        if (a[k] <= 42 && p[k] <= 42) continue;
        if (a[k] != p[k]) return 0;
    }
    return s12_relax_hook(a, p, odb, rxb, count, off, frame, rd);                /* only the stick word differs */
}

int s12_early_check(s12_u8 *odb, const s12_u8 *rxb, s12_s32 end_frame, int commit, struct s12_eck *r)
{
    s12_s32 ssf[3], fin_st, ss, sl;
    s12_u8 rd[3], wr[3], pred[3];
    s12_u32 required, count, set = 0;

    r->need = 0; r->target = 0; r->fin = 0; r->matched = 0; r->players = 0; r->skip = S12_ESKIP_NONE;
    if (odb[S12_ODB_ROLLBACK_IS_ACTIVE] || odb[S12_ODB_ROLLBACK_SHOULD_LOAD_STATE]) { r->skip = S12_ESKIP_ACTIVE; return 0; }
    if (odb[S12_ODB_IS_GAME_OVER]) { r->skip = S12_ESKIP_OVER; return 0; }
    if (odb[S12_ODB_IS_FRAME_ADVANCE]) { r->skip = S12_ESKIP_ADVANCE; return 0; }
    if (odb[S12_ODB_IS_DISCONNECTED]) { r->skip = S12_ESKIP_DISC; return 0; }
    /* 395: REG_ROLLBACK_REQUIRED starts as ODB_ROLLBACK_IS_ACTIVE (0 here) */
    required = 0;
    /* 401-411: no savestate predicting -> section 9 would only clear the per-player flags; nothing to do early */
    if (odb[S12_ODB_SAVESTATE_IS_PREDICTING] == 0) { r->skip = S12_ESKIP_NOTPRED; return 0; }
    fin_st = rd32(odb + S12_ODB_STABLE_FINALIZED_FRAME);
    for (count = 0; count < 3; count++) {
        ssf[count] = rd32(odb + S12_ODB_PLAYER_SAVESTATE_FRAME + 4 * count);
        rd[count] = odb[S12_ODB_PRED_READ_IDXS + count];
        wr[count] = odb[S12_ODB_PRED_WRITE_IDXS + count];
        pred[count] = odb[S12_ODB_PLAYER_SAVESTATE_IS_PREDICTING + count];
    }
    /* 415-604 COMPARE_PREDICTED_INPUTS / CHECK_WHETHER_TO_ROLL_BACK_LOOP */
    for (count = 0; count < 3; count++) {
        if (pred[count] != 1) continue;                                   /* 421-424 */
        r->players++;
        for (;;) {
            s12_s32 latest = rd32(rxb + S12_RXB_OPNT_FRAME_NUMS + 4 * count), off = latest - ssf[count];   /* 427-440 */
            if (off < 0) break;                                           /* 442 */
            if (ssf[count] > fin_st) {                                    /* 445-446 HAVE_PLAYER_INPUTS */
                s12_u32 ao = S12_RXB_OPNT_INPUTS + (s12_u32)off * S12_PAD + count * S12_RXB_IN_N * S12_PAD;
                s12_u32 po = S12_ODB_PRED_INPUTS + (s12_u32)rd[count] * S12_PAD + count * S12_PRED_N * S12_PAD;
                if (ao + S12_PAD > S12_RXB_SIZE || rd[count] >= S12_PRED_N) { r->skip = 100u + count; return 0; }   /* out of the ASM's buffers: leave it to B0 */
                /* Slippi 3.7.0 (TriggerSendInput.asm section 9, d0ce23a): section 11 marked this prediction because the frame's RNG used this player's bits before they were known: roll back even when the inputs match */
                if (odb[po + S12_PAD_RNG_MARKER]) { required = 1; break; }
                if (!inputs_match_rx(rxb + ao, odb + po, odb, rxb, count, (s12_u32)off, ssf[count], rd[count])) { required = 1; break; }   /* 565-571 INDICATE_ROLLBACK_REQUIRED */
            } else if (!(latest > ssf[count])) {                          /* 448-456: finalized, nothing newer */
                break;
            }
            /* 532-563 INPUTS_MATCH: advance the savestate frame and the read index; loop until caught up */
            ssf[count]++;
            r->matched++;
            rd[count] = (s12_u8)(rd[count] + 1u >= S12_PRED_N ? rd[count] + 1u - S12_PRED_N : rd[count] + 1u);
            if (wr[count] == rd[count]) break;
        }
    }
    /* 609-658 COMPUTE_SAVESTATE_FRAME_LOOP: min over predicting players, starting at the stable finalized frame */
    ss = fin_st;
    for (count = 0; count < 3; count++) {
        if (pred[count] != 1) continue;
        if (!set || ssf[count] < ss) ss = ssf[count];
        set = 1;
    }
    sl = rd32(rxb + S12_RXB_SMALLEST_LATEST_FRAME);                       /* 664-668 finalized capped by smallest latest */
    r->target = ss;
    r->fin = ss > sl ? sl : ss;
    r->need = required;
    if (!required || !commit) return (int)required;
    /* commit: what section 9 + TRIGGER_ROLLBACK (573-586) write; end frame = last simulated frame, no ODB_FRAME++ */
    for (count = 0; count < 3; count++) {
        s12_put32(odb + S12_ODB_PLAYER_SAVESTATE_FRAME + 4 * count, (s12_u32)ssf[count]);
        odb[S12_ODB_PRED_READ_IDXS + count] = rd[count];
    }
    s12_put32(odb + S12_ODB_SAVESTATE_FRAME, (s12_u32)ss);
    s12_put32(odb + S12_ODB_FINALIZED_FRAME, (s12_u32)r->fin);
    s12_put32(odb + S12_ODB_ROLLBACK_END_FRAME, (s12_u32)end_frame);
    odb[S12_ODB_ROLLBACK_SHOULD_LOAD_STATE] = 1;
    odb[S12_ODB_ROLLBACK_IS_ACTIVE] = 1;
    return 1;
}
