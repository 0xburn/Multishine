/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* ut_check: the network part of the update check on the title.  Pure C, no libc, no heap, no Melee address: the
 * console side (the UPD_TITLE part of menus/on/on_backend.c, inside ms/hub.ovl) supplies the io (struct ut_io, hub_ovl.h), the host test (upd/tests/ut_test.c) supplies fakes.  All state is in struct hub_tchk
 * (hub_ovl.h), which the resident keeps across the overlay's reloads, so a scene change in the middle of a step only restarts that step.
 *
 * One call per title / Main Menu frame.  Order: setting known and ON -> the online setup from the card (once) -> wait for the BBA address + UT_SETTLE_MS + 0..UT_JITTER_MS ->
 * hello when no session is ready -> UPD_CHECK for our files (ABI 2, have = the installed MultishineOvl build) -> UPD_CHECK for the engine (ABI 1, have = the running build) ->
 * DONE with found = 1 when either part is newer (one combined release, as the Update screen offers it).  Never blocks; never runs while the hub client is busy or the sign
 * overlay is open (the Update screen uses the same client: a running step is aborted and redone later); a failure is tried again UT_RETRIES times, UT_RETRY_MS apart, only
 * while still on the title; anything else ends silently (no text, no prompt).  No link / no setup within UT_GIVE_UP_MS: done, nothing sent. */
#ifndef UT_CHECK_H
#define UT_CHECK_H
#include "hub_ovl.h"

/* struct ut_io, UT_ABI_* and ut_step are declared in hub_ovl.h (that header is staged with the resident sources; this one is not) */
#endif
