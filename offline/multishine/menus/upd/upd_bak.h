/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_bak: the full backup copy of the Multishine card to the memory card in physical SLOT B, made before an update is written.
 * Reads every file of slot A that this disc can see (the exploit save, Melee's own save, MultishineCode / B / Ovl / Pack / State / Online / Identity / Options / Codes ...), writes
 * the same names and card status to slot B, verifies each chunk by reading it back, and brackets the copy with a one-block marker file (written 'incomplete' first, 'complete' last) so a half-made backup is never taken for a good one.
 * Slot A is only read.  Slot B is never formatted, and a file of another origin with one of our names is never touched.  The result never blocks the update. */
#ifndef UPD_BAK_H
#define UPD_BAK_H
#include "upd_card.h"
#include "../sign/sg_updv.h"

#define BAK_MARKER "MultishineBackup"
#define BAK_MAXE 48                       /* files copied at most (the card has 127 directory entries; ours are about 15) */
struct bak_cfg {
    void *(*alloc)(unsigned n);           /* heap, 0 when it does not fit (never halts) */
    void (*free)(void *p);
    unsigned seq;                         /* build_seq of the running engine, written into the marker for the restore tools */
    unsigned mode;                        /* BAK_M_UPDATE (0): the full copy made before an update.  BAK_M_COPY (1): the player's "Copy card to slot B": the same copy WITHOUT the two files that hold a login
                                             (MultishineOnline = device secret, MultishineIdentity = Slippi login), so a second card never carries the first card's identity (the engine makes a guest one on it) */
};
#define BAK_M_UPDATE 0u
#define BAK_M_COPY 1u
#define BAK_NAME_ONLINE "MultishineOnline"
#define BAK_NAME_IDENT "MultishineIdentity"
/* SGU_BAK_DONE, or why nothing (complete) was written: NOCARD, BADCARD, SMALL, INWAY, FAIL, MEM, BUSY.  On any non-DONE result every file this run created in slot B is removed again. */
int bak_run(const struct bak_cfg *cfg);
#endif
