/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * THE HOOK: the one resident entry into the sign-in screens.  The menu layout runtime (offline/multishine/menus/ml,
 * owned by another module) calls these four functions from ONE place: the confirm of the first row ("Accounts") of the Settings page, which today
 * opens the stub text screen.  Everything else of the sign-in screens is the on-demand overlay behind this file.
 *
 *   confirm on "Accounts"   ->  sg_hook_open(&H, &env, "/ms/sign.ovl")     starts the load (disc read), returns at once
 *   every menu frame        ->  sg_hook_poll(&H)                            LOADING -> ACTIVE (the overlay is relocated and entered)
 *                               sg_hook_frame(&H, &pad, tb)                 ACTIVE: input, tests, network steps (inside the resident network slice: EXI lock held)
 *                               sg_hook_present(&H)                         draws this frame with the resident ml helpers in env.gfx (text rows, backdrop, QR sprite)
 *   player left / scene exit ->  sg_hook_close(&H)                          leaves the overlay, clears the network hooks, frees the block
 * While state != IDLE the ml code holds the retail menu off exactly as it does for its stub text screens (the menu input cooldown).
 * Pure C89, no libc, no Melee address (the SDK calls come in through struct sgo_sdk). Host-tested through the Dolphin test image.
 */
#ifndef SG_HOOK_H
#define SG_HOOK_H
#include "sg_ovl.h"

#define SG_H_IDLE 0
#define SG_H_LOADING 1
#define SG_H_ACTIVE 2
#define SG_H_FAILED 3      /* the screen shows "not available" and a B press closes it; the menu never halts on a failed load */
#define SG_H_PARKED 4      /* loaded and relocated, NOT entered: a prefetch that finished, or a screen left with sg_hook_park; the
                              next sg_hook_open enters it without reading the file again when the image is unchanged, else it is read again (never a stale image) */
#define SG_H_PREFETCH 5    /* loading for a later open (sg_hook_prefetch): sg_hook_poll parks it when done; an sg_hook_open meanwhile turns it into a normal load */
#define SG_H_TAIL 128u     /* the image's last bytes (.data sits at its end, sg_ovl.ld) are kept at load time and put back before a parked block is entered again */

struct sg_hook {
    struct sgo_loader ld;
    struct sgo_env env;
    const struct sgo_api *api;
    const struct sg *st;
    unsigned char state, hooks_clear, reused;
    int err;
    sgo_u32 loads, frees, last_block, last_addr;
    sgo_u32 sum, tail_n, reuses, parks, prefetches;                  /* the parked image's checksum (all but the tail), its tail length; counters (RAM proof) */
    unsigned char tail[SG_H_TAIL];
};

void sg_hook_init(struct sg_hook *h, const struct sgo_sdk *sdk);
int sg_hook_open(struct sg_hook *h, const struct sgo_env *env, const char *path);   /* 0 started, <0 SGO_E_*: state FAILED */
int sg_hook_poll(struct sg_hook *h);                                                 /* returns the state */
int sg_hook_frame(struct sg_hook *h, const struct sdent_pad *p, sgo_u32 tb);        /* ACTIVE only; 1 = the player left the hub (call sg_hook_close) */
void sg_hook_present(struct sg_hook *h);                                             /* ACTIVE: draw this frame through env.gfx (the ml helpers) */
void sg_hook_close(struct sg_hook *h);                                               /* any state -> IDLE: leaves an ACTIVE overlay, frees the block (also a parked one) */
/* keeping the overlay between visits: the block costs block_bytes while parked, nothing else is kept */
int sg_hook_prefetch(struct sg_hook *h, const char *path);   /* IDLE: start loading without entering (0 started); sg_hook_poll parks it; a failure goes back to IDLE quietly */
void sg_hook_park(struct sg_hook *h);                        /* ACTIVE: leave the overlay (as close) but keep its block: PARKED */
int sg_hook_parked(const struct sg_hook *h);                 /* 1 while PARKED or PREFETCH (the glue frees it with sg_hook_close when the menu goes elsewhere) */
#endif
