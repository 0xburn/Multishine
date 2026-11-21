/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the lab/dev Online rows Server / Slippi / In Use (sg_ui.c built with -DSG_NETSEL=1): labels, values, A / Left-Right, the requests the glue answers,
   the one-time Real Slippi question (A yes, B no; B does not leave the page), the read-only row, every drawn glyph in the menu font and no row wider than 49 columns.  Mocks only. */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static unsigned tbv = 5000;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void frame(unsigned btn)
{
    struct sdent_pad p;
    memset(&p, 0, sizeof p);
    p.buttons = (unsigned short)btn;
    tbv += 675000u;
    sg_frame(&G, &p, tbv);
}
static void tap(unsigned b) { unsigned k; frame(b); for (k = 0; k < SG_ACT_GAP; k++) frame(0); }
static unsigned bad;
static const struct sg_view *draw(void)
{
    static struct sg_view v;
    unsigned r, c;
    sg_draw(&G, &v);
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(v.t[r]) > 49u) { bad++; printf("row longer than 49: %s\n", v.t[r]); }
        for (c = 0; v.t[r][c]; c++) {
            char ch = v.t[r][c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ')) { bad++; printf("glyph %c: %s\n", ch, v.t[r]); }
        }
    }
    return &v;
}
static void base(void) { sg_init(&G); G.linked = SG_ID_LINKED; G.fight = 2; G.pol = SG_POL_FRIENDS; G.srv = SG_SRV_VPS; G.mmr = SG_MMR_MOCK; G.srv_lock = 2; }
/* the lab rows live on the dev screen (Settings > Diagnostics > Server and Slippi: SG_GOTO_DEV); lines 2, 3, 4 */
static void to_row(unsigned row) { base(); G.dev = 1; G.cur = SG_ON_SRV; while (G.cur != row) tap(SG_B_DOWN); }

int main(void)
{
    const struct sg_view *v;
    unsigned r;
    CHECK(SG_ON_SRV == SG_ON_POL + 1 && SG_ON_MMR == SG_ON_SRV + 1 && SG_ON_NET == SG_ON_MMR + 1 && SG_ON_SLOGIN == SG_ON_NET + 1);
    CHECK(!strcmp(sg_on_label(SG_ON_SRV), "Server") && !strcmp(sg_on_label(SG_ON_MMR), "Slippi") && !strcmp(sg_on_label(SG_ON_NET), "In use"));
    for (r = 0; r < SG_ON_N; r++) CHECK(strlen(sg_on_desc(r)) <= 44u && strlen(sg_on_desc(r)) > 0);

    /* values on the page, defaults first */
    to_row(SG_ON_SRV);
    v = draw();
    CHECK(!strcmp(v->t[4u], "Server") && !strcmp(v->val[4u], "Production") && !strcmp(v->t[6u], "Slippi") && !strcmp(v->val[6u], "Our mock") && !strcmp(v->t[8u], "In use") && v->bar == 4u);
    G.srv = 0; G.mmr = 0; v = draw();
    CHECK(!strcmp(v->val[4u], "...") && !strcmp(v->val[6u], "..."));
    G.srv = SG_SRV_NA; G.mmr = SG_MMR_NA; v = draw();
    CHECK(strstr(v->val[4u], "Not available") && strstr(v->val[6u], "Not available"));

    /* Server: A and Left / Right flip the server and Lab, the glue writes (request 2); no patched server: a hint, nothing changes; unknown: read again */
    to_row(SG_ON_SRV);
    CHECK(G.cur == SG_ON_SRV);
    tap(SG_B_A); CHECK(G.srv == SG_SRV_LAB && G.srv_req == 2); G.srv_req = 0;
    v = draw(); CHECK(!strcmp(v->t[4u], "Server") && !strcmp(v->val[4u], "Dev") && v->bar == 4u);
#if SG_SRV_COUNT == 3                                          /* lab builds: Production, Dev, Lab; A and Right go on, Left goes back */
    tap(SG_B_RIGHT); CHECK(G.srv == SG_SRV_LAN && G.srv_req == 2); G.srv_req = 0;
    v = draw(); CHECK(!strcmp(v->val[4u], "Lab"));
    tap(SG_B_RIGHT); CHECK(G.srv == SG_SRV_VPS && G.srv_req == 2); G.srv_req = 0;
    tap(SG_B_LEFT); CHECK(G.srv == SG_SRV_LAN && G.srv_req == 2); G.srv_req = 0;
    tap(SG_B_LEFT); CHECK(G.srv == SG_SRV_LAB && G.srv_req == 2); G.srv_req = 0;
    tap(SG_B_A); CHECK(G.srv == SG_SRV_LAN && G.srv_req == 2); G.srv_req = 0;
    tap(SG_B_A); CHECK(G.srv == SG_SRV_VPS && G.srv_req == 2); G.srv_req = 0; tap(SG_B_A); CHECK(G.srv == SG_SRV_LAB); G.srv_req = 0;
#else                                                          /* a release build that shows the row: Production and Dev only */
    tap(SG_B_RIGHT); CHECK(G.srv == SG_SRV_VPS && G.srv_req == 2); G.srv_req = 0;
    tap(SG_B_LEFT); CHECK(G.srv == SG_SRV_LAB && G.srv_req == 2); G.srv_req = 0;
    tap(SG_B_A); tap(SG_B_A); CHECK(G.srv == SG_SRV_LAB); G.srv_req = 0;
#endif
    G.srv_lock = 1; G.srv = SG_SRV_LAB; tap(SG_B_A); CHECK(G.srv == SG_SRV_LAB && G.srv_req == 0 && G.hint && strstr(G.hint, "no baked server"));
    v = draw(); CHECK(strstr(v->t[UI_R_NOTE], "no baked server"));
    G.srv_lock = 2; G.srv = 0; tap(SG_B_A); CHECK(G.srv == 0 && G.srv_req == 1); G.srv_req = 0;
    G.srv = SG_SRV_NA; tap(SG_B_A); CHECK(G.srv == SG_SRV_NA && G.srv_req == 0 && G.hint);

    /* Slippi: the first Real asks (A yes, B no); B does not leave the page; the answer is remembered */
    to_row(SG_ON_MMR);
    tap(SG_B_A);
    CHECK(G.nconf == 1 && G.mmr == SG_MMR_MOCK && G.mmr_req == 0);
    v = draw();
    CHECK(strstr(v->t[11u], "Unranked and Direct will contact Slippi's real") && strstr(v->t[12u], "This client is not Slippi") && strstr(v->t[13u], "Switch it on"));
    CHECK(strstr(v->t[SG_ROWS - 2u], "A: Yes") != 0 && strstr(v->t[SG_ROWS - 2u], "B: No") != 0);
    tap(SG_B_DOWN); tap(SG_B_RIGHT); tap(SG_B_UP); CHECK(G.nconf == 1 && G.mmr == SG_MMR_MOCK && G.cur == SG_ON_MMR);        /* nothing else moves while the question is up */
    tap(SG_B_B); CHECK(G.nconf == 0 && G.mmr == SG_MMR_MOCK && G.mmr_req == 0 && !G.quit && G.scr == SG_HUB && !G.mmk);          /* no: unchanged, still on the page */
    tap(SG_B_RIGHT); CHECK(G.nconf == 1);                                                                                        /* Left / Right ask as well */
    tap(SG_B_A); CHECK(G.nconf == 0 && G.mmr == SG_MMR_REAL && G.mmk == 1 && G.mmr_req == 2);                                   /* yes: switched on, answer to be stored */
    G.mmr_req = 0;
    v = draw(); CHECK(!strcmp(v->t[6u], "Slippi") && !strcmp(v->val[6u], "Real Slippi") && v->bar == 6u);
    tap(SG_B_A); CHECK(G.mmr == SG_MMR_MOCK && G.mmr_req == 2 && G.nconf == 0 && G.mmk == 1); G.mmr_req = 0;                      /* off: at once */
    tap(SG_B_A); CHECK(G.mmr == SG_MMR_REAL && G.mmr_req == 2 && G.nconf == 0); G.mmr_req = 0;                                   /* on again: the resident remembers, no question */
    /* a resident that already remembers the answer (a later boot): no question at all */
    to_row(SG_ON_MMR); G.mmk = 1; tap(SG_B_A);
    CHECK(G.nconf == 0 && G.mmr == SG_MMR_REAL && G.mmr_req == 2);
    G.mmr = SG_MMR_NA; G.mmr_req = 0; tap(SG_B_A); CHECK(G.mmr == SG_MMR_NA && G.mmr_req == 0 && G.hint);

    /* In Use: read only, A does nothing, the two lines name what the console uses */
    to_row(SG_ON_NET);
    G.srv_ip = (198u << 24) | (51u << 16) | (100u << 8) | 214u; G.mm_ip = (10u << 24) | (213u << 16) | (90u << 8) | 21u;
    v = draw();
    CHECK(!strcmp(v->t[11u], "Server: 198.51.100.214 Production") && !strcmp(v->t[12u], "Slippi: Our mock at 10.213.90.21"));
    tap(SG_B_A); CHECK(G.scr == SG_HUB && G.cur == SG_ON_NET && G.srv_req == 0 && G.mmr_req == 0 && !G.nconf);
    G.mmr = SG_MMR_REAL; v = draw(); CHECK(!strcmp(v->t[12u], "Slippi: Real, mm.slippi.gg"));
    G.mmr = SG_MMR_MOCK; G.mm_ip = 0; G.srv_ip = 0; strcpy(G.srv_host, "play.example"); G.srv = SG_SRV_LAB; v = draw();
    CHECK(!strcmp(v->t[11u], "Server: play.example Dev") && !strcmp(v->t[12u], "Slippi: no override, baked name"));

    /* the footer, the other rows and Delete are unchanged; Up from the first row still stays */
    /* the Online page itself: no lab rows, Down skips them, Delete follows the invite rows, Up from the first row still stays; the dev screen's cursor stays among its three rows */
    base(); v = draw();
    { unsigned k; for (k = 4; k < 18; k++) CHECK(!strstr(v->t[k], "Server") && !strstr(v->t[k], "In use") && strcmp(v->t[k], "Slippi")); }
    tap(SG_B_DOWN); tap(SG_B_A);                                                     /* the Friends and privacy page */
    CHECK(G.cur == SG_ON_POL); tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.cur == SG_ON_WATCH); tap(SG_B_DOWN); CHECK(G.cur == SG_ON_POL);      /* no lab row on a page: Down wraps inside it */
    base(); tap(SG_B_A); tap(SG_B_UP); CHECK(G.cur == SG_ON_IDENT);
    to_row(SG_ON_SRV); tap(SG_B_UP); CHECK(G.cur == SG_ON_SRV); tap(SG_B_DOWN); tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.cur == SG_ON_SRV); /* wraps inside the three rows */
    CHECK(bad == 0 && sg_cut_count == 0);
    if (fails) { printf("sg_netsel_test: %d FAILED\n", fails); return 1; }
    printf("sg_netsel_test: all checks passed\n");
    return 0;
}
