/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The card layout notice of the Update screen: built with -DEXPECT_NOTICE=1 only together with MS_CARD_WA_ADDR=0x817E4000u (stock layout); every other work area (generic 0x817D4000u, low, none) must not show it. */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static struct sg_view v;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static int any_row(const char *needle) { unsigned r; for (r = 0; r < SG_ROWS; r++) if (strstr(v.t[r], needle)) return 1; return 0; }
static void draw(unsigned st, unsigned no, unsigned ns)
{
    sg_init(&G); sg_goto(&G, SG_UPD); G.bak = 2; G.keep = 2; G.cpy_ok = 1;
    G.upd.state = (unsigned char)st; G.upd.installed = 1282; G.upd.run_rel = 1282; G.upd.latest = 1283; G.upd.size_kb = 112;
    G.upd.note_ours = (unsigned char)no; G.upd.note_slippi = (unsigned char)ns; G.upd.slippi_new[0] = 3; G.upd.slippi_new[1] = 7; G.upd.slippi_new[2] = 255;
    sg_draw(&G, &v);
}
int main(void)
{
    unsigned st, no, ns, r;
    for (st = 0; st < SGU_NSTATE; st++) for (no = 0; no < 3; no++) for (ns = 0; ns < 3; ns += 2) {
        int want = EXPECT_NOTICE && st == SGU_UPTODATE && no == 0 && ns == 0;
        draw(st, no, ns);
        CHECK(any_row("A new card layout is out") == want);
        CHECK(any_row("or GameCube app, or set it up again.") == want);
        if (want) for (r = 0; r < SG_ROWS; r++) if (strstr(v.t[r], "layout") || strstr(v.t[r], "updates, update") || strstr(v.t[r], "or GameCube")) CHECK(strlen(v.t[r]) <= 44u);
        if (want) { CHECK(!any_row("Back up before updating") && !any_row("Set up Multishine on card slot B")); CHECK(v.t[SG_ROWS - 2][0] != 0); }
    }
    draw(SGU_UPTODATE, 0, 0);
    CHECK(any_row("Up to date"));
    if (fails) printf("sg_notice_test FAILED (%d)\n", fails); else printf("sg_notice_test ok (expect notice %d)\n", EXPECT_NOTICE);
    return fails != 0;
}
