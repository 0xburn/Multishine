/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_req: the forced-update decision.  Header-only, pure C89.
 * required = the server's advisory (live answer or the card cache) says the minimum build is above the running build.  Fail open: no advisory yet (no internet, server down,
 * check off and no cache) is NOT required; a running build of 0 (not known, dev image) is never required.  The Update screen and Settings never ask this question. */
#ifndef UPD_REQ_H
#define UPD_REQ_H
#include "../sign/sg_updv.h"
#define UPDREQ_BLOCK 2          /* the value ms_offline_server returns for 'online mode refused: update required' (1 = offline / link first) */
#define UPDREQ_QUERY 2          /* ms_offline_server(UPDREQ_QUERY): only the update question (the Slippi bridge row), no offline or account check */

static int updreq_required(const struct sgu_adv *a, unsigned running)
{
    return a && a->valid && running && a->ours_min > running;
}
#endif
