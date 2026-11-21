/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of keyhint.h: the parser over the footer / hint / toast strings of the project, the split at double spaces, the layout of a pair. */
#include <stdio.h>
#include <string.h>
#define KH_HOST 1
#include "../keyhint.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void one(const char *tok, int ok, unsigned pre, unsigned n, int k0, int k1, const char *act)
{
    struct kh_seg g;
    unsigned r = kh_parse(tok, &g);
    CHECK(r == (unsigned)ok);
    if (!ok || r != (unsigned)ok) return;
    CHECK(g.pre == pre); CHECK(g.n == n); CHECK(g.key[0] == k0); if (n > 1u) CHECK(g.key[1] == k1);
    CHECK(strcmp(g.act, act) == 0);
}

int main(void)
{
    char k[KH_MAXTOK][KH_TOK];
    unsigned n, i, j;
    struct kh_seg g;
    struct kh_pair p;
    one("A: Ready", 1, 0, 1, KH_A, 0, "Ready");
    one("Hold B: Leave", 1, KH_P_HOLD, 1, KH_B, 0, "Leave");
    one("Double Z: Decline", 1, KH_P_DOUBLE, 1, KH_Z, 0, "Decline");
    one("L R: Costume", 1, 0, 2, KH_L, KH_R, "Costume");
    one("X Y: Page", 1, 0, 2, KH_X, KH_Y, "Page");
    one("Start: Join", 1, 0, 1, KH_START, 0, "Join");
    one("Z: Play Mano A Mano", 1, 0, 1, KH_Z, 0, "Play Mano A Mano");
    one("Left Right: Change", 0, 0, 0, 0, 0, "");
    one("Up Down: Digit", 0, 0, 0, 0, 0, "");
    one("A restart now", 0, 0, 0, 0, 0, "");
    one("Starting the match", 0, 0, 0, 0, 0, "");
    one("Keep Holding Z: Accepting", 0, 0, 0, 0, 0, "");
    one("Back: x", 0, 0, 0, 0, 0, "");
    one("Any input feeds the pool", 0, 0, 0, 0, 0, "");
    one("A:", 1, 0, 1, KH_A, 0, "");
    one("B:Back", 1, 0, 1, KH_B, 0, "Back");
    /* split */
    n = kh_split("L R: Costume  A: Lock  B: Back", k);
    CHECK(n == 3); CHECK(strcmp(k[0], "L R: Costume") == 0); CHECK(strcmp(k[2], "B: Back") == 0);
    n = kh_split("Y: Standings  Start: Join  Hold B: Leave", k);
    CHECK(n == 3); CHECK(kh_has_key(k, n));
    n = kh_split("Please wait", k);
    CHECK(n == 1); CHECK(!kh_has_key(k, n));
    n = kh_split("a  b  c  d  e", k);
    CHECK(n == 4);
    n = kh_split("", k);
    CHECK(n == 0);
    /* a pair: Hold (B) Leave at the footer font 0.03 */
    CHECK(kh_parse("Hold B: Leave", &g));
    kh_pair_build(&g, 300, &p);
    CHECK(p.n == 3); CHECK(p.it[0].kind == KH_I_PRE); CHECK(p.it[1].kind == KH_I_PLATE); CHECK(p.it[2].kind == KH_I_ACT);
    CHECK(p.it[0].x == 0); CHECK(p.it[1].x - p.it[1].w / 2 > p.it[0].x + p.it[0].w); CHECK(p.it[2].x > p.it[1].x + p.it[1].w / 2);
    CHECK(p.w == p.it[2].x + p.it[2].w);
    /* 21 px at the font 0.03: 0.0373 units a px */
    CHECK(p.it[1].ph == 300 * 230); CHECK(p.it[1].ph / 65536.0 > 1.0 && p.it[1].ph / 65536.0 < 1.1);
    /* the letter text and the plate share the glyph centre: letter y + plate top + h/2 = action y + centre */
    CHECK(p.it[1].dy + p.it[1].py + p.it[1].ph / 2 == kh_cq(300));
    CHECK(kh_parse("L R: Costume", &g));
    kh_pair_build(&g, 300, &p);
    CHECK(p.n == 3); CHECK(p.it[0].x < p.it[1].x); CHECK(p.it[0].x + p.it[0].w / 2 < p.it[1].x - p.it[1].w / 2);
    CHECK(kh_parse("Start: Join", &g));
    kh_pair_build(&g, 300, &p);
    CHECK(p.it[0].w > p.it[0].ph);                                   /* the Start plate is wider than high */
    /* widths scale with the font */
    CHECK(kh_wq("Join", 300) > kh_wq("Join", 200));
    /* every key has its colours; white ink only on the red and purple */
    for (i = 0; i < KH_NK; i++) { CHECK(kh_plate_rgb(i) != 0); CHECK(kh_ink_is_white(i) == (i == KH_B || i == KH_Z)); }
    CHECK(kh_plate_rgb(KH_A) == 0x2FA84Fu); CHECK(kh_plate_rgb(KH_B) == 0xD23A32u); CHECK(kh_plate_rgb(KH_Z) == 0x6B4FC8u); CHECK(kh_plate_rgb(KH_X) == 0xC9CCD6u);
    /* the widest real lines against an equal share of the footer box (x 29.5 .. 612.5 px = 31.3 units) */
    {
        static const char *const lines[] = { "Y: Standings  Start: Join  Hold B: Leave", "X: Replay  Y: Standings  Start: Join  Hold B: Leave", "L R: Costume  A: Lock  B: Back", "R: Auto Off  Start: Join  Hold B: Leave", "Y: Standings  Z: Leave Queue  Hold B: Leave" };
        for (i = 0; i < sizeof lines / sizeof lines[0]; i++) {
            char kk[KH_MAXTOK][KH_TOK];
            unsigned nn = kh_split(lines[i], kk);
            int share = (int)(31.3 * 65536.0 / nn);
            for (j = 0; j < nn; j++)
                if (kh_parse(kk[j], &g)) {
                    kh_pair_build(&g, 300, &p);
                    CHECK(p.w <= share);
                    if (p.w > share) printf("  %s: token %u is %.1f units in a %.1f share\n", lines[i], j, p.w / 65536.0, share / 65536.0);
                }
        }
    }
    if (fails) { printf("%d failed\n", fails); return 1; }
    printf("keyhint ok\n");
    return 0;
}
