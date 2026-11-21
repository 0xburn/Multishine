/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Retail Tourney bracket scene, snapshot model ( and 8): from the server's bracket (who plays whom, who won) compute the
 * per-box state the retail scene keeps in RAM (BracketEntry[] at 0x80473AB8, TmData.x37[] at 0x804771C4), so that a rebuild draws the bracket as if the finished matches had been played in
 * any order. Pure C89, no libc, no heap, no Melee address: the console glue copies the result into RAM; this file is the model and is tested on the host against.
 * The box table (successor box and slot, initial slot occupancy) comes from the caller: on the console it is read back from the retail scene's own BracketEntry[] after its build, so no
 * retail data is carried here.  A box is a match; boxes are numbered as the retail table has them (round by round, a final last).
 */
#ifndef GNT_TMB_H
#define GNT_TMB_H

#define TMB_BOXES 64u
#define TMB_NONE 0xFFu

struct tmb_box {
    unsigned char nxt, slot;      /* successor box and its slot (nxt >= nbox: the terminal box) */
    unsigned char occ0, occ1;     /* an entrant starts in slot 0 / 1 (the table's initial occupancy) */
};
struct tmb_ent {                  /* one entrant, in leaf order: the k-th occupied leaf slot (box order, slot 0 then 1) is entrant k */
    unsigned char kind, level, handicap, ch, costume;   /* kind 0 human, 1 CPU (retail x4E); ch character icon id (x4D); level x51; handicap x52 */
    unsigned short label;         /* retail name label (x54): CPU 0x384 + n, Player 0x320 + n, or a server-name id the glue maps */
};
struct tmb_slot {
    unsigned char occ;            /* x30: a plate is in this slot */
    unsigned char place;          /* x4C: 0 the winner of the box, 1 the loser, 3 not played */
    unsigned char ch, kind, costume, ent, level, handicap;   /* x4D x4E x4F x50 x51 x52 */
    unsigned short label;         /* x54 */
};
struct tmb_state {
    struct tmb_slot s[TMB_BOXES][2];
    unsigned char pending[TMB_BOXES];   /* x1: the match is still to be played */
    unsigned char nbox, nleaf;
};
struct tmb_res { unsigned char box, winner; };     /* a finished box and the winning entrant (index into the entrant list) */

/* the data block the engine's tmv module reads (tmv_in) and the sign overlay writes: size index 0..8 = retail sizes 4 6 8 12 16 24 32 48 64; ent[] in leaf order, res[] finished boxes (feeders first) */
struct tmb_pack {
    unsigned char size_idx, nent, nres, ver;
    struct tmb_ent ent[64];
    struct tmb_res res[63];
    char name[64][9];                 /* the server names by entrant (leaf order), 8 characters + NUL; empty = none */
    unsigned char dbg;                /* harness only: 1 = name texts cycle through four sizes (calibration) */
    unsigned char lb_ok;              /* 1 = the losers page below is valid (double elimination cup of 8 / 16 / 32 / 64) */
    unsigned char lbs[TMB_BOXES][2];  /* losers page: the entrant (leaf index into ent[] / name[]) shown in each slot of each box (box order of tmb_lb_table), 0xFF = none */
    unsigned char lbp[TMB_BOXES][2];  /* its placing: 0 winner, 1 loser, 3 not played */
    unsigned char lbx[TMB_BOXES];     /* the match of the box is still to be played */
};
/* the retail 1v1 table of a power-of-two size (4 8 16 32 64): successors and initial occupancy as the retail TmBox has them (boxes = matches + the trophy box); returns the box count */
unsigned tmb_pow2_table(struct tmb_box *box, unsigned size);

/* the losers page: the boxes of the losers bracket of a power-of-two cup (8 16 32 64) as gen_lb makes them (port of that generator, same numbers): matches round by round, then grand final 1 and 2;
   layout in the retail scene's 640x480 pixel space.  Returns the box count (<= 64), 0 for another size. */
struct tmb_lbox { short left, width, top, height; unsigned char nxt, slot, occ0, occ1; };
unsigned tmb_lb_table(struct tmb_lbox *out, unsigned size);

/* 0 on success; 1 bad sizes; 2 a result for a box whose feeders are unfinished or that is done twice; 3 the winner is not in that box */
unsigned tmb_build(struct tmb_state *st, const struct tmb_box *box, unsigned nbox, const struct tmb_ent *ent, unsigned nent, const struct tmb_res *res, unsigned nres);
/* the lowest pending box (the match the retail scene shows as the current one), TMB_NONE when none */
unsigned tmb_current(const struct tmb_state *st);

#endif
