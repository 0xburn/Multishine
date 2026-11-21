/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The in-match pause policy of OUR modes (pure, host-tested).
 *
 * A real Slippi client (the bridge route: Unranked / Direct against desktop Slippi) decides pause and LRAS by itself, in the game logic, from the synced pads:
 *   Direct: the setup block leaves the pause-off bit (0x8046DB6A bit 0x08) clear -> Slippi's InitPause installs nothing -> Melee's NATIVE pause: Start pauses BOTH
 *              consoles (every console reads every port's pad), L+R+A+Start in the pause ends the game.
 *   Unranked: the block has the bit set -> InitPause installs ClientPause (0x8046DB68 + 0x40) and clears the bit; ClientPause reads BOTH ports' held pads, so an LRAS
 *              from either side ends the game on both.
 *   Ranked: InitPause skips it: no pause, no LRAS.
 * Whatever the mode, the two consoles must run the same rules byte for byte.  Since the policy is one CONSUMER of the per-match rules record (ms_match_rules.h): it reads nothing else.  Cube the policy below ran for the
 * Slippi route too, with od_o.mode = 0 = MNC_MODE_RANKED (the bridge never goes through od_request, the field stays 0) and put the pause-off bit back and zeroed the
 * callback every frame: the desktop paused (Start) / ended the game (LRAS), our console did neither -> "Desync Risk", then a hard desync.  Our modes only. */
#ifndef PAUSE_POLICY_H
#define PAUSE_POLICY_H
#include "ms_match_rules.h"

#define PP_BIT_PAUSE_OFF 0x08u

/* rules2 = byte 0x8046DB6A (StartMeleeRules +2), cb = word 0x8046DB68 + 0x40 (the pause callback); room_cb = our Game night callback.  Called every frame in a match.
   r = the match's rules record: only a record of OUR route with a pause policy touches anything (NULL, cleared, never written, the Slippi route = untouched). */
static __inline__ void ms_pause_policy(const struct ms_match_rules *r, volatile unsigned char *rules2, volatile unsigned int *cb, unsigned int room_cb)
{
    int p = ms_rules_pause(r);
    if (p == MR_PAUSE_OFF_NO_QUIT) { *rules2 |= (unsigned char)PP_BIT_PAUSE_OFF; *cb = 0u; }    /* Grandmaster = Slippi Ranked: no pause, no quit */
    if (p == MR_PAUSE_OFF_LRAS_ONLY && *cb != room_cb) { *rules2 &= (unsigned char)~PP_BIT_PAUSE_OFF; *cb = room_cb; }   /* Game night: no pause screen, L+R+A+Start still quits */
}
#endif
