/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Release shape of the sign overlay (SG_MOCKUPS 0: the Mockups hub is compiled out): the Game Night browser must still open, take input and draw (the Game Night row, a toast's Open
 * Game Night, the Friends list's Join Room), and a Mockups number must land on the Accounts hub.  Regression of with the hub compiled out, sg_goto sent SG_GN to the hub. */
#include <stdio.h>
#include <string.h>
#include "sg_ui.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static struct sg G;
static struct sg_view V;
int main(void)
{
    struct sdent_pad pad;
    memset(&pad, 0, sizeof pad);
    sg_init(&G);
    CHECK(SG_MOCKUPS == 0);
    sg_goto(&G, SG_GN);
    CHECK(G.scr == SG_GN);                                   /* the Game Night browser opens */
    sg_frame(&G, &pad, 0);
    CHECK(G.scr == SG_GN);                                   /* and stays on its own input */
    memset(&V, 0, sizeof V);
    sg_draw(&G, &V);                                         /* draws without the hub */
    sg_init(&G);
    G.fr.join = 1; G.fr.join_code[0] = 'A'; G.fr.join_code[1] = 'B'; G.fr.join_code[2] = 'C'; G.fr.join_code[3] = 'D';
    sgm_frd_join(&G);
    CHECK(G.scr == SG_GN && G.gn.frd == 1);                  /* Friends > Join Room goes to the browser, not to the Online page */
    sg_init(&G);
    sg_goto(&G, SG_MOCK);
    CHECK(G.scr == SG_HUB);                                  /* the Mockups hub itself is gone in this build */
    printf(fails ? "sg_rel: FAILED\n" : "sg_rel: ok\n");
    return fails != 0;
}
