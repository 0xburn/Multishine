/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of the Mockups hub (sg_mockup.c, sg_mocklist.c): scripted pad input through sg_frame opens every hub entry, works the
   keys of every new screen (list scrolling, every mock state, refresh, join / create / invite / watch, ready, phases, recorder setting), leaves it and checks that
   the hub cursor is back on the entry; every drawn screen: the "MOCKUP: example data" tag on the new screens, glyphs of the menu font only, rows of at most 49
   characters, nothing cut.  Existing screens reached from the hub return to the hub (hold B where the screen asks for it).
   usage: sg_mk_test [FLOW.txt]   (FLOW.txt: the screens in the format of tests/sg_mock.c, for tests/sg_png.py) */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static struct sg_view V;
static unsigned tbv = 1000, dt = 1350000u;      /* 30 frames/s at 40.5 MHz: the overlay's console rate */
static int fails, shots;
static FILE *flow;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void fr(unsigned btn)
{
    struct sdent_pad p;
    memset(&p, 0, sizeof p);
    p.buttons = (unsigned short)btn;
    tbv += dt;
    sg_frame(&G, &p, tbv);
}
static void press(unsigned btn) { unsigned k; for (k = 0; k <= SG_ACT_GAP; k++) fr(0); fr(btn); for (k = 0; k <= SG_ACT_GAP; k++) fr(0); }   /* released first: the Friends list ignores a button already down when it opens */
static void hold(unsigned btn, unsigned n) { unsigned i; for (i = 0; i < n; i++) fr(btn); fr(0); }
static int has(const char *s)
{
    unsigned r;
    sg_draw(&G, &V);
    for (r = 0; r < SG_ROWS; r++) if (strstr(V.t[r], s)) return 1;
    return 0;
}
static void shot(const char *name)
{
    unsigned r, c;
    sg_cut_count = 0;
    sg_draw(&G, &V);
    if (sg_cut_count) { printf("FAIL %s: %u characters cut\n", name, sg_cut_count); fails++; }
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(V.t[r]) > 49u) { printf("FAIL %s: row %u too long (%u): %s\n", name, r, (unsigned)strlen(V.t[r]), V.t[r]); fails++; }
        for (c = 0; V.t[r][c]; c++) {
            char ch = V.t[r][c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ')) {
                printf("FAIL %s: glyph %c not in the menu font: %s\n", name, ch, V.t[r]); fails++;
            }
        }
    }
    if (G.scr >= SG_MOCK && !strstr(V.t[0], "MOCKUP: example data")) { printf("FAIL %s: no MOCKUP tag: %s\n", name, V.t[0]); fails++; }
    if (G.scr >= SG_MOCK && strstr(V.t[0], "  ") && strstr(V.t[0], "  ") - V.t[0] > 18) { printf("FAIL %s: title longer than 18 (proportional font: the tag would leave the screen): %s\n", name, V.t[0]); fails++; }
    shots++;
    if (flow) {
        fprintf(flow, "SCREEN %s 0 0 0\n", name);
        for (r = 0; r < SG_ROWS; r++) fprintf(flow, "%u|%s\n", V.attr[r], V.t[r]);
    }
}
static void to_row(unsigned r)        /* move the hub cursor to row r with the d-pad */
{
    unsigned guard = 0;
    while (G.mk.hub != r && guard++ < 40u) press(SG_B_DOWN);
    CHECK(G.mk.hub == r);
}

enum { H_ROOMS, H_ROOMSG, H_FRIENDS, H_LB, H_TIERS, H_ROOM, H_SPEC, H_REC, H_IDENT, H_LINK, H_PROV, H_ENT };
#if SG_LAB
#define H_TLS 12
#define H_UPD 13
#else
#define H_UPD 12
#endif
#define H_CTL (H_UPD + 1)
#define H_BACK (H_CTL + 1)

static void list_screen(unsigned row, unsigned scr, const char *name, unsigned nstates, unsigned n)
{
    char nm[64];
    unsigned s, i;
    to_row(row);
    press(SG_B_A);
    CHECK(G.scr == scr);
    snprintf(nm, sizeof nm, "%s_live", name); shot(nm);
    for (i = 0; i < n + 1u; i++) press(SG_B_DOWN);                  /* wraps: cursor 1 again, the window followed it */
    CHECK(G.mk.cur == 1u);
    for (i = 0; i < 2u; i++) press(SG_B_UP);
    CHECK(G.mk.cur == n - 1u);
    if (n > 9u && scr != SG_MK_TIERS) { CHECK(G.mk.top == n - 9u); snprintf(nm, sizeof nm, "%s_scrolled", name); shot(nm); }
    for (s = 1; s < nstates; s++) {
        press(SG_B_X);
        CHECK(G.mk.state == s);
        snprintf(nm, sizeof nm, "%s_state%u", name, s); shot(nm);
    }
    if (nstates > 1u) {
        press(SG_B_X);
        CHECK(G.mk.state == SGM_LIVE);
        press(SG_B_START);
        CHECK(G.mk.timer > 0u && has("Loading"));
        snprintf(nm, sizeof nm, "%s_refresh", name); shot(nm);
        hold(0, SGM_LOAD_FRAMES);
        CHECK(G.mk.timer == 0u && has("Updated just now"));
        press(SG_B_X); press(SG_B_X);                                   /* UNREACHABLE: Start tries again and comes back live */
        CHECK(G.mk.state == SGM_UNREACH && has("not"));
        press(SG_B_START);
        CHECK(G.mk.state == SGM_LIVE && G.mk.timer > 0u);
        hold(0, SGM_LOAD_FRAMES);
    }
    press(SG_B_B);
    CHECK(G.scr == SG_MOCK && G.mk.hub == row);
}

static void existing(unsigned row, unsigned scr, const char *name, int hold_b)
{
    to_row(row);
    press(SG_B_A);
    CHECK(G.scr == scr || (scr == SG_IDENT && G.scr == SG_IDENT));
    shot(name);
    if (hold_b) hold(SG_B_B, SG_HOLD_B_FRAMES + 1u); else press(SG_B_B);
    CHECK(G.scr == SG_MOCK);
    CHECK(G.mk.hub == row);
    CHECK(!G.quit);
}

int main(int argc, char **argv)
{
    unsigned i;
    if (argc > 1) flow = fopen(argv[1], "w");
    sg_init(&G);
    sg_goto(&G, SG_MOCK);                                              /* what the engine's Main Menu row does (o_goto) */
    CHECK(G.scr == SG_MOCK && G.mk.on && G.mk.hub == 0u);
    shot("mock_hub");
    CHECK(has("Rooms Browser") && has("Recorder Indicator") && has("Controller test") && has("Back"));

    list_screen(H_ROOMS, SG_MK_ROOMS, "mk_rooms", SGM_NSTATE, 8);
    /* Rooms Browser: join a room (Game Night Room with its name, Back returns to the same row), full, passcode, create */
    to_row(H_ROOMS); press(SG_B_A);
    press(SG_B_DOWN);
    press(SG_B_A);
    CHECK(G.scr == SG_MK_ROOM && G.mk.room == 1u && G.mk.ret == 1u && has("Wobble Club"));
    shot("mk_room_joined");
    press(SG_B_B);
    CHECK(G.scr == SG_MK_ROOMS && G.mk.cur == 1u);
    press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_A);
    CHECK(G.scr == SG_MK_ROOMS && has("Room is full. Spectators allowed"));
    shot("mk_rooms_full_msg");
    press(SG_B_DOWN); press(SG_B_A);
    CHECK(has("Passcode required"));
    press(SG_B_Z);
    CHECK(has("Create room: mock"));
    shot("mk_rooms_create");
    press(SG_B_X); press(SG_B_X); press(SG_B_X); press(SG_B_X);         /* FULL: no spectators, A says full */
    CHECK(G.mk.state == SGM_FULL);
    press(SG_B_A);
    CHECK(G.scr == SG_MK_ROOMS && has("Room is full.") && has("8 of 8"));
    press(SG_B_X);
    CHECK(G.mk.state == SGM_SPECONLY);
    press(SG_B_A);
    CHECK(has("Spectators allowed") && has("Spectators: allowed"));
    press(SG_B_B);
    CHECK(G.scr == SG_MOCK && G.mk.hub == H_ROOMS);

    /* Rooms Browser, Graphic (gnb.c; the host has no Event screen: the text view of the same model): example rooms, paging, join, lobby, passcode, create, states */
    to_row(H_ROOMSG); press(SG_B_A);
    CHECK(G.scr == SG_MK_GNG && has("Game Night") && has("Example data") && has("Friday Night") && has("5 of 8") && has("Host: Wobble Pilot"));
    shot("gng_list");
    press(SG_B_DOWN); press(SG_B_A);                                    /* Wobble Club: join, lobby */
    CHECK(G.gn.scr == GNB_LOBBY && has("Room lobby") && has("Wobble Club") && has("Pocket Fox"));
    shot("gng_lobby");
    press(SG_B_A); CHECK(has("You Ready"));
    press(SG_B_B); CHECK(G.gn.scr == GNB_LOBBY); hold(SG_B_B, GNB_HOLD_B_FRAMES + 2u); CHECK(G.gn.scr == GNB_LIST && !has("You left the room"));
    press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_A);                  /* Ditto Dojo: full, spectators allowed */
    CHECK(G.gn.scr == GNB_LIST && has("Room is full"));
    shot("gng_full");
    press(SG_B_DOWN); press(SG_B_A);                                    /* Mirror Match: passcode (example passcode 1234) */
    CHECK(G.gn.scr == GNB_PASS && has("Passcode required"));
    press(SG_B_UP); press(SG_B_A); press(SG_B_UP); press(SG_B_UP); press(SG_B_A); press(SG_B_UP); press(SG_B_UP); press(SG_B_UP); press(SG_B_A);
    press(SG_B_UP); press(SG_B_UP); press(SG_B_UP); press(SG_B_UP);
    shot("gng_passcode");
    press(SG_B_A);
    CHECK(G.gn.scr == GNB_LOBBY && has("Mirror Match"));
    hold(SG_B_B, GNB_HOLD_B_FRAMES + 2u);
    press(SG_B_Z); CHECK(G.gn.scr == GNB_CREATE && has("Create room") && has("Show in list: yes"));
    press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_DOWN);                                               /* (players), list, spectators */
    CHECK(has("Spectators: allowed") && has("Mode: Chill") && has("Single games, random legal stage.") && !has("Name:"));      /* the Mode row replaces the Name row (Chill first) and shows its description */
    shot("gng_create");
    press(SG_B_UP); press(SG_B_UP); press(SG_B_UP); press(SG_B_RIGHT);                               /* the Mode row: Sweaty */
    CHECK(has("Mode: Sweaty") && has("Strike, then loser picks. Best of 3."));
    shot("gng_create_type");
    press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_DOWN); press(SG_B_A);       /* to Create room */
    CHECK(G.gn.scr == GNB_LOBBY && has("You: host"));
    hold(SG_B_B, GNB_HOLD_B_FRAMES + 2u); CHECK(G.gn.n == 9u && has("9 rooms"));
    for (i = 0; i < 12u; i++) press(SG_B_DOWN);
    CHECK(G.gn.first == 0u && G.gn.page == 8u);
    press(SG_B_X); CHECK(G.gn.first == 0u);                              /* 9 rows: nothing to page */
    for (i = 1; i < GNB_X_N; i++) { char nm[32]; press(SG_B_R); CHECK(G.gn.xst == i); snprintf(nm, sizeof nm, "gng_state%u", i); shot(nm); }
    press(SG_B_R); CHECK(G.gn.xst == GNB_X_LIVE);
    press(SG_B_START); CHECK(has("Loading")); hold(0, GNB_LOAD_FRAMES); CHECK(has("Updated just now"));
    press(SG_B_B);         /*: a tap of B on the room list goes back */
    CHECK(G.scr == SG_MOCK && G.mk.hub == H_ROOMSG);

    list_screen(H_FRIENDS, SG_MK_FRIENDS, "mk_friends", 4, 6);
    to_row(H_FRIENDS); press(SG_B_A);
    CHECK(has("Code: XMPL 1") && has("Wobble Pilot"));
    press(SG_B_A); CHECK(has("Invite sent. Mock"));
    for (i = 0; i < 4u; i++) press(SG_B_DOWN);
    press(SG_B_A); CHECK(has("Tipper Tom is offline"));
    shot("mk_friends_offline");
    press(SG_B_B);

    list_screen(H_LB, SG_MK_LB, "mk_lb", 4, 12);
    to_row(H_LB); press(SG_B_A);
    CHECK(has("Grandmaster: Kestrel") && has("You: Rank 312") && has("1 - 9 of 12"));
    press(SG_B_X); press(SG_B_X); press(SG_B_X);
    CHECK(G.mk.state == SGM_EMPTY && has("Vacant") && !has("You: Rank"));
    press(SG_B_B);

    list_screen(H_TIERS, SG_MK_TIERS, "mk_tiers", 1, 12);
    to_row(H_TIERS); press(SG_B_A);
    CHECK(has("Grandmaster") && has("Copper") && has("Below 900"));
    press(SG_B_X); CHECK(G.mk.state == SGM_LIVE);                      /* static page: X changes nothing */
    press(SG_B_B);

    list_screen(H_SPEC, SG_MK_SPEC, "mk_spec", 4, 4);
    to_row(H_SPEC); press(SG_B_A);
    press(SG_B_A); CHECK(has("Watch: mock"));
    shot("mk_spec_watch");
    press(SG_B_B);

    /* Game Night Room from the hub: ready toggle, phases, Back to the hub */
    to_row(H_ROOM); press(SG_B_A);
    CHECK(G.scr == SG_MK_ROOM && G.mk.ret == 0u && has("Friday Night"));
    shot("mk_room");
    CHECK(has("Challenger    Not Ready"));
    press(SG_B_A); CHECK(has("Challenger    Ready"));
    for (i = 1; i < 3u; i++) { char nm[32]; press(SG_B_X); CHECK(G.mk.phase == i); snprintf(nm, sizeof nm, "mk_room_phase%u", i); shot(nm); }
    press(SG_B_B);
    CHECK(G.scr == SG_MOCK && G.mk.hub == H_ROOM);

    /* Recorder Indicator: the setting cycles with A / right / left, the mode with X; Grandmaster always shows REC */
    to_row(H_REC); press(SG_B_A);
    CHECK(G.scr == SG_MK_REC && has("Ranked Only") && has("REC"));
    shot("mk_rec");
    press(SG_B_X); CHECK(has("Preview Mode: Dojo") && !has("REC "));
    shot("mk_rec_dojo_off");
    press(SG_B_A); CHECK(has("Send Match Inputs: On") && has("REC "));
    press(SG_B_LEFT); press(SG_B_LEFT); CHECK(has("Send Match Inputs: Off"));
    press(SG_B_X); shot("mk_rec_gamenight");
    press(SG_B_B);
    CHECK(G.scr == SG_MOCK && G.mk.hub == H_REC);

    /* the existing screens, linked not copied: each opens and comes back to the Mockups hub on its row */
    existing(H_IDENT, SG_IDENT, "ex_ident", 0);
    to_row(H_IDENT); press(SG_B_A); press(SG_B_DOWN); press(SG_B_B);    /* the real Device identity (its actions are rows now): B goes back */
    CHECK(G.scr == SG_MOCK && G.mk.hub == H_IDENT);
    existing(H_LINK, SG_LINK, "ex_link", 1);                             /* the real Link screen opens on its entropy pass (a fresh pool): hold B leaves */
    existing(H_PROV, SG_PROV, "ex_prov", 0);
    to_row(H_PROV); press(SG_B_A); press(SG_B_A); CHECK(G.scr == SG_PROV_DETAIL); press(SG_B_B); CHECK(G.scr == SG_PROV); press(SG_B_B);
    CHECK(G.scr == SG_MOCK && G.mk.hub == H_PROV);
    existing(H_ENT, SG_ENT, "ex_entropy", 1);
#if SG_LAB
    existing(H_TLS, SG_TLS, "ex_tls", 0);
#endif
    existing(H_UPD, SG_UPD, "ex_update", 0);
    existing(H_CTL, SG_CTL, "ex_ctl", 1);
    CHECK(!G.direct);

    /* leaving: the Back row and B both quit the overlay (the engine returns to the Main Menu) */
    to_row(H_BACK);
    shot("mock_hub_back");
    press(SG_B_A);
    CHECK(G.quit);
    G.quit = 0;
    press(SG_B_B);
    CHECK(G.quit);

    /* the Online page (ex Accounts hub): no Mockups session, Back from a screen lands on the Online page */
    sg_init(&G);
    CHECK(G.scr == SG_HUB && !G.mk.on);
    press(SG_B_A); CHECK(G.scr == SG_HUB && G.onp); press(SG_B_A); CHECK(G.scr == SG_IDENT); press(SG_B_B); CHECK(G.scr == SG_HUB);      /* Account is the first page, Device identity its first row */
    /* an unknown screen number is refused (the hub) */
    sg_goto(&G, SG_NSCREEN + 3u); CHECK(G.scr == SG_HUB);

    /* Friends > Y on a Room row (text view, no Event screen): the list closes and the Game Night browser joins that room */
    sg_init(&G);
    sg_goto(&G, SG_FRD);
    CHECK(G.scr == SG_FRD && G.fr.live && G.fr.req == FRD_REQ_LIST);
    G.fr.req = FRD_REQ_NONE;
    {
        struct frd_ent e[2];
        memset(e, 0, sizeof e);
        strcpy(e[0].code, "ANN@1"); strcpy(e[0].name, "Ann"); e[0].presence = 1; e[0].flags = 4; strcpy(e[0].room, "4821"); e[0].rflags = 2;
        strcpy(e[1].code, "BOB@2"); strcpy(e[1].name, "Bob"); e[1].presence = 1; e[1].flags = 4; strcpy(e[1].room, "1357"); e[1].rflags = 3;
        frd_net_friends(&G.fr, 2, 0, 2, e);
    }
    CHECK(has("Room"));
    press(SG_B_A);
    CHECK(G.scr == SG_GN && G.gn.live && G.gn.scr == GNB_BUSY && G.gn.req == GNB_REQ_JOIN && !strcmp(gnb_join_code(&G.gn), "4821") && !G.fr.join && !G.quit);
    CHECK(G.gn.frd == 1);                                               /*: leaving that room returns to the Friends list, not the Game Night list */
    G.gn.req = GNB_REQ_NONE; gnb_net_left(&G.gn);
    CHECK(G.gn.exit && G.gn.back_frd);
    press(SG_B_UP);
    CHECK(G.scr == SG_FRD && !G.quit && !G.gn.back_frd && G.fr.live);
    sg_goto(&G, SG_FRD);                                                /* the passcode friend: the passcode screen comes first */
    G.fr.req = FRD_REQ_NONE;
    {
        struct frd_ent e[2];
        memset(e, 0, sizeof e);
        strcpy(e[0].code, "BOB@2"); strcpy(e[0].name, "Bob"); e[0].presence = 1; e[0].flags = 4; strcpy(e[0].room, "1357"); e[0].rflags = 3;
        frd_net_friends(&G.fr, 1, 0, 1, e);
    }
    press(SG_B_A);
    CHECK(G.scr == SG_GN && G.gn.scr == GNB_PASS && has("Passcode required"));
    press(SG_B_B);
    CHECK(G.scr == SG_GN && G.gn.scr == GNB_LIST);                      /* B: the Game Night list, as if the row had been opened */

    if (flow) fclose(flow);
    printf("%s: %d failures, %d screens checked\n", fails ? "FAIL" : "mockups hub host test ok", fails, shots);
    return fails ? 1 : 0;
}
