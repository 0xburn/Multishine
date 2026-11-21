/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* om_card: memory-card persistence of the direct-code history (codehist.h) for the 'Multishine online-menu' image
 *.  Called only from the EXI shim's menu commands:
 *   B9  GET_ONLINE_STATUS   (menu load / CSS)     omc_on_status   first read of the card file, retry of a pending save
 *   B4  FIND_OPPONENT       (code confirmed)     omc_on_code     MRU update in RAM, then one card write when the list changed
 *   BE  FETCH_CODE_SUGGESTION                      omc_answer      answered from RAM only (no card access while the player types)
 * Never from a match, never at boot, never with Melee's own card layer busy.  Every failure leaves the history in RAM only. */
#ifndef OM_CARD_H
#define OM_CARD_H

#include "codehist.h"

struct omc_status {                  /* RAM proof block (symbol omc_st, omc_status_addr) */
    unsigned int magic;              /* 'OMCH' */
    unsigned int be, be_hits;        /* BE requests, answers with a suggestion */
    unsigned int adds, add_changes;  /* codes confirmed, list changes */
    unsigned int load_tries, load_status, load_slot, load_res;
    unsigned int write_tries, write_status, write_slot, write_res, writes;
    unsigned int count;              /* codes in RAM */
    unsigned int flags;              /* bit0 loaded (verdict final), bit1 dirty, bit2 card file is from a newer version */
    unsigned int ticks;              /* time base ticks of the last card operation */
};
extern struct omc_status omc_st;

/* omc_st.load_status */
enum { OMC_L_NONE = 0, OMC_L_OK = 1, OMC_L_NOFILE = 2, OMC_L_NOCARD = 3, OMC_L_DAMAGED = 4, OMC_L_NEWER = 5, OMC_L_ERROR = 6, OMC_L_BUSY = 7 };
/* omc_st.write_status */
enum { OMC_W_NONE = 0, OMC_W_OK = 1, OMC_W_NOCARD = 2, OMC_W_ERROR = 3, OMC_W_NEWER = 4, OMC_W_FULL = 5, OMC_W_RESERVE = 6, OMC_W_BUSY = 7,
       OMC_W_VERIFY = 8, OMC_W_NOLOAD = 9 };

void omc_on_status(void);
void omc_on_code(const char *code);           /* a valid stored code (ch_valid) */
int omc_answer(const unsigned char *req, unsigned char *rsp);

#endif
