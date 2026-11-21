/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the frozen Final Destination policy in ms_match_rules.h; run by run_tests.sh. */
#include <stdio.h>
#include "../ms_match_rules.h"

static int bad;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)

int main(void)
{
    unsigned int k;
    /* manual FD: the toggle decides */
    CHECK(mfd_decide(MFD_GRKIND_FD, 0, 0, 0) == MFD_WHY_OFF);
    CHECK(mfd_decide(MFD_GRKIND_FD, 1, 0, 0) == MFD_WHY_TOGGLE);
    /* random FD: frozen by default, whatever the toggle says */
    CHECK(mfd_decide(MFD_GRKIND_FD, 0, 1, 0) == MFD_WHY_RANDOM);
    CHECK(mfd_decide(MFD_GRKIND_FD, 1, 1, 0) == MFD_WHY_RANDOM);
    /* every other stage ground is never touched */
    for (k = 0; k < 0x60u; k++) if (k != MFD_GRKIND_FD) CHECK(mfd_decide(k, 1, 1, 0) == MFD_WHY_OFF);
    /* the Slippi route: nothing, even random */
    CHECK(mfd_decide(MFD_GRKIND_FD, 1, 1, 1) == MFD_WHY_OFF);
    CHECK(mfd_decide(MFD_GRKIND_FD, 1, 0, 1) == MFD_WHY_OFF);
    /* the cursor body address from the `b` at 0x8025A530: forward and backward branches, not a branch, absolute / link forms */
    CHECK(mfd_body_addr(MFD_INJ_CURSOR, 0x48000100u) == MFD_INJ_CURSOR + 0x100u);
    CHECK(mfd_body_addr(MFD_INJ_CURSOR, 0x4BFFFF00u) == MFD_INJ_CURSOR - 0x100u);
    CHECK(mfd_body_addr(MFD_INJ_CURSOR, 0x7FC3F378u) == 0u);
    CHECK(mfd_body_addr(MFD_INJ_CURSOR, 0x48000101u) == 0u && mfd_body_addr(MFD_INJ_CURSOR, 0x48000102u) == 0u);
    /* the notice kind: FD frozen = 1, PS with its toggle = 2 (never on the Slippi route), everything else 0 */
    CHECK(mfd_kind(MFD_GRKIND_FD, 1, 0, 0) == 1u && mfd_kind(MFD_GRKIND_FD, 0, 1, 0) == 0u);
    CHECK(mfd_kind(MFD_GRKIND_PS, 0, 1, 0) == 2u && mfd_kind(MFD_GRKIND_PS, 0, 0, 0) == 0u && mfd_kind(MFD_GRKIND_PS, 0, 1, 1) == 0u);
    for (k = 0; k < 0x60u; k++) if (k != MFD_GRKIND_FD && k != MFD_GRKIND_PS) CHECK(mfd_kind(k, 0, 1, 0) == 0u);
    CHECK(MFD_INJ_PS == 0x801D457Cu && MFD_GRKIND_PS == 0x10u);
    /* the match scene: online (major 8) and offline Vs Melee (major 2), minor 2, not loading; any other major or minor, or a load in progress, is not */
    CHECK(mfd_scene(0x08080002u, 0) && mfd_scene(0x02020102u, 0) && mfd_scene(0x02000002u, 0));
    CHECK(!mfd_scene(0x02020102u, 1) && !mfd_scene(0x02020101u, 0) && !mfd_scene(0x01010002u, 0) && !mfd_scene(0x08080100u, 0) && !mfd_scene(0x0E0E0002u, 0));
    /* the two words of the switch: the on word is `b +8` (skips two instructions), 4-byte aligned */
    CHECK(MFD_WORD_ON == 0x48000008u && (MFD_SWITCH_ADDR & 3u) == 0u);
    /* a Slippi bridge record is the Slippi route */
    {
        struct ms_match_rules r;
        ms_rules_begin_slippi(&r, 0);
        CHECK(ms_rules_slippi(&r));
        ms_rules_clear(&r);
        CHECK(!ms_rules_slippi(&r));
    }
    printf(bad ? "mfd_test: %d FAILED\n" : "mfd_test: OK\n", bad);
    return bad != 0;
}
