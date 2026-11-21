/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_card: the bridge's memory-card access on the console.  Same retail CARD SDK sequence as
 * offline/multishine/menus/om/om_card.c (Dolphin-proven): CARDProbeEx (BUSY retried) -> sector size 0x2000 -> CARDMountAsync +
 * __CARDSync -> open/read -> close -> CARDUnmount on EVERY path after a mount attempt.  Called only from the EXI shim's
 * menu commands (B9 menu load) and the menu-time card poll, never from a match, never at boot, never while Melee's own card layer
 * is busy (same idle check as om_card), never from the network slice (the BBA holds EXI channel 0 there).  THIS MODULE NEVER WRITES THE CARD.
 *
 *  - identity: "MultishineIdentity" (sbr_ident.h), slot A then slot B, the first file found decides.  READ ONLY.  The block
 *    buffer is wiped right after the parse on every path (R7: the play key lives only in the caller's struct sbr_ident).
 *  - dev/lab builds only (-DSBR_NETCFG): "MultishineBridge", the net-config section (sbr_netcfg.h).  The status-document record that used to live there is gone.
 * Soft-float, no heap, no libc.  Host tests: -DSBRC_HOST with tests/sbrc_host.h (a fake card behind the same entry points).
 */
#ifndef SBR_CARD_H
#define SBR_CARD_H

#include "sbr_ident.h"
#include "sbr_netcfg.h"

/* IDENT SETTLE: the Slippi row and the Multishine identity decision stop waiting for the memory card at the SAME moment.  Both count from their first card attempt:
   after SBR_IDENT_SETTLE_MS without a definite answer (card busy, read errors; a missing card is never final: it may be inserted later) the identity is 'not found on card'.
   Must equal ONC_SETTLE_MS in menus/on/on_card.h (host test bridge/tests/app_unit.c).  Before: the bridge needed 3 failed reads 2 s apart (about 4 to 6 s) or never when the card
   stayed busy; the Multishine side counted 3 tries per menu command and never ended on a busy card.  SBR_CARD_POLL_MS: the bridge's gap between card attempts (was 2000). */
#define SBR_IDENT_SETTLE_MS 3000u
#define SBR_CARD_POLL_MS 1000u
enum { SBRC_L_NONE = 0, SBRC_L_OK, SBRC_L_NOFILE, SBRC_L_NOCARD, SBRC_L_DAMAGED, SBRC_L_NEWER, SBRC_L_ERROR, SBRC_L_BUSY };

struct sbrc_status {               /* RAM proof block: numbers only, never a secret */
    sbr_u32 magic;                 /* 'SBCD' */
    sbr_u32 id_tries, id_status, id_slot, id_res, id_parse;
    sbr_u32 c_tries, c_status, c_slot, c_res;
    sbr_u32 w_tries, w_status, w_slot, w_res, writes;
    sbr_u32 ticks;                 /* time-base ticks of the last card operation */
};
extern struct sbrc_status sbrc_st;

/* SBRC_L_*; OK fills *id (otherwise *id is wiped); DAMAGED = a file whose payload failed sbr_ident_parse (id_parse says why) */
int sbrc_read_ident(struct sbr_ident *id);
#ifdef SBR_NETCFG
/* SBRC_L_*; dev/lab builds only: reads "MultishineBridge" (slot A then B) and feeds its net-config section to sbr_nc_parse_block.  Nothing is ever written. */
int sbrc_read_netcfg(void);
#endif

#endif
