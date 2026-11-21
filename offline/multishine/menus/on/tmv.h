/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tmv: the retail Tourney scene as a view of a server bracket.  Engine side, called from bh_tick (once per scene-loop pass, outside
 * matches).  Pure state machine here (host tested, tests/tmv_test.c): given what the scene looks like (major / minor scene, TmData.cur_option, a request) it says what to do; tmv.c
 * does it with the retail functions.  The request and the data come from RAM words (tmv_st / tmv_in) that the hub entry fills; nothing else of ours runs in the Tourney scenes.
 * Sequence: request -> the SETUP scene (minor 0) appears -> we write the setup answers into TmData, run the retail finish (fn_80192938), overwrite the entrants, and end the setup the
 * way its own "Yes" does (state 1) -> the BRACKET scene (minor 1) builds itself -> once it idles at the match prompt (cur_option 0x1D, steady) the snapshot of the server bracket is written, the scene rebuilds
 * (cur_option 0x13) -> HOLD: the view stays; the scene's A / B / START are blocked and X leaves (back to the hub); a result animates one box (cur_option 0x1F).
 */
#ifndef TMV_H
#define TMV_H

#define TMV_MAGIC 0x544D5653u            /* 'TMVS' */
enum { TMV_IDLE, TMV_SETUP, TMV_BRACKET, TMV_APPLY, TMV_HOLD, TMV_LEAVE, TMV_FAIL };
enum { TMV_E_NONE, TMV_E_SIZE, TMV_E_TIMEOUT, TMV_E_BUILD, TMV_E_SCENE };
/* tmv_st words */
enum { TMV_W_MAGIC, TMV_W_REQ, TMV_W_PHASE, TMV_W_FRAMES, TMV_W_ERR, TMV_W_APPLIED, TMV_W_LEAVE, TMV_W_N };

struct tmv_world {
    unsigned major, minor, loading;      /* the running major / minor scene, 0 = no scene load in progress */
    unsigned cur_option;                 /* TmData.cur_option (valid in the bracket scene) */
    unsigned req;                        /* a view is requested (the hub entry / harness) */
    unsigned leave_key;                  /* X pressed (a fresh edge) */
    unsigned page_key;                   /* Y pressed (a fresh edge): the other page (winners / losers) */
    unsigned want_applied;               /* the data version asked for, 0 none: a higher number than the applied one = write the snapshot again */
    unsigned applied;
};
enum { TMV_A_NONE, TMV_A_SETUP_GO, TMV_A_APPLY, TMV_A_HOLD_PIN, TMV_A_LEAVE, TMV_A_ABORT, TMV_A_PAGE };
#define TMV_SCENE_MAJOR 0x1Bu            /* GM_TOURNAMENT */
#define TMV_SETUP_MINOR 0u
#define TMV_BRACKET_MINOR 1u
#define TMV_WAIT_FRAMES 1800u            /* passes a phase may wait (about 30 s of 60) */
#define TMV_SETTLE 30u                   /* passes the bracket scene must have shown cur_option 0x1D (the first match prompt, a steady state) before the first write */

struct tmv_m { unsigned phase, frames, settle, err; };     /* the machine's own state (the caller keeps it) */
/* one step: returns TMV_A_* */
unsigned tmv_step(struct tmv_m *m, const struct tmv_world *w);

/* what the menu overlay gets through struct mlo_env: the request word block and the data block (struct tmb_pack, menus/sign/gnt_tmb.h) */
#ifndef TMV_PUB_DEFINED
#define TMV_PUB_DEFINED
struct tmv_pub { volatile unsigned *st; void *in; unsigned in_size; unsigned char *hs; };     /* hs: 0xBA8 bytes + a magic word, the Gecko codehandler saved before tn_restore overwrites it (ml_glue.c); then 0x60 bytes + a magic word: the r13 slots the online codeset took from Tournament mode */
#endif

#endif
