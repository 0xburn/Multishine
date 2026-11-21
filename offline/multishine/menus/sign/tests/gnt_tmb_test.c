/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the retail bracket snapshot model; the 8 entrant case equals (boxes 2, 3, 1 finished, 0 pending). */
#include <stdio.h>
#include "../gnt_tmb.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    /* the retail 8 table: boxes 0..3 round one (0,1 -> 4; 2,3 -> 5), 4,5 -> 6, 6 -> 7 (trophy), 7 terminal */
    static const struct tmb_box b8[8] = {
        { 4, 0, 1, 1 }, { 4, 1, 1, 1 }, { 5, 0, 1, 1 }, { 5, 1, 1, 1 },
        { 6, 0, 0, 0 }, { 6, 1, 0, 0 }, { 7, 0, 0, 0 }, { 8, 0, 0, 0 } };
    struct tmb_ent e[8];
    struct tmb_state st;
    struct tmb_res r1[3], r2[1], r3[1];
    unsigned i;
    for (i = 0; i < 8u; i++) { e[i].kind = 1; e[i].level = 5; e[i].handicap = 0; e[i].ch = (unsigned char)i; e[i].costume = 0; e[i].label = (unsigned short)(0x384u + i); }
    CHECK(tmb_build(&st, b8, 8, e, 8, 0, 0) == 0);
    CHECK(tmb_current(&st) == 0u);
    CHECK(st.nleaf == 8u && st.s[0][0].ent == 0u && st.s[0][1].ent == 1u && st.s[3][1].ent == 7u && !st.s[4][0].occ);
    r1[0].box = 2; r1[0].winner = 4; r1[1].box = 3; r1[1].winner = 7; r1[2].box = 1; r1[2].winner = 2;     /* entrants 4 (box 2 slot 0), 7 (box 3 slot 1), 2 would lose box 1: use the real feeders */
    r1[2].winner = 3;                                                                                       /* box 1 holds entrants 2 and 3 */
    CHECK(tmb_build(&st, b8, 8, e, 8, r1, 3) == 0);
    CHECK(!st.pending[1] && !st.pending[2] && !st.pending[3] && st.pending[0] && st.pending[4]);
    CHECK(tmb_current(&st) == 0u);
    CHECK(st.s[4][1].occ && st.s[4][1].ent == 3u);                  /* box 1's winner went to box 4 slot 1 */
    CHECK(st.s[5][0].occ && st.s[5][0].ent == 4u && st.s[5][1].occ && st.s[5][1].ent == 7u);
    CHECK(st.s[1][1].place == 0u && st.s[1][0].place == 1u && !st.s[1][1].occ && st.s[1][0].occ);   /* the winner left its leaf, the loser stays with place 1 */
    CHECK(st.s[0][0].occ && st.s[0][1].occ && st.s[0][0].place == 3u);
    r2[0].box = 4; r2[0].winner = 0;                                  /* feeders unfinished (box 0 pending): refused */
    CHECK(tmb_build(&st, b8, 8, e, 8, r2, 1) == 2u);
    r3[0].box = 0; r3[0].winner = 5;                                  /* entrant 5 is not in box 0 */
    CHECK(tmb_build(&st, b8, 8, e, 8, r3, 1) == 3u);
    CHECK(tmb_build(&st, b8, 8, e, 7, 0, 0) == 1u);
    {   /* the losers page: the numbers of gen_lb (tests/test_snapshot.py checks the same hash) */
        struct tmb_lbox lb[TMB_BOXES];
        unsigned sz[4] = { 8, 16, 32, 64 }, k, i, n;
        unsigned long hh[4], want[4] = { 2444351686ul, 1653298366ul, 1304890438ul, 1101120498ul };
        for (k = 0; k < 4; k++) {
            unsigned long h = 2166136261ul;
            n = tmb_lb_table(lb, sz[k]);
            CHECK(n == (sz[k] == 8 ? 8u : sz[k] == 16 ? 16u : sz[k] == 32 ? 32u : 64u));
            for (i = 0; i < n; i++) {
                int v[8]; int j;
                v[0] = lb[i].left; v[1] = lb[i].width; v[2] = lb[i].top; v[3] = lb[i].height; v[4] = lb[i].nxt; v[5] = lb[i].slot; v[6] = lb[i].occ0; v[7] = lb[i].occ1;
                for (j = 0; j < 8; j++) { h ^= (unsigned long)(v[j] & 0xFFFF); h = (h * 16777619ul) & 0xFFFFFFFFul; }
            }
            hh[k] = h;
        }
                for (k = 0; k < 4; k++) CHECK(hh[k] == want[k]);
        CHECK(tmb_lb_table(lb, 12) == 0);
    }
    if (fails) return 1;
    printf("gnt_tmb: ok\n");
    return 0;
}
