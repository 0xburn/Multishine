/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Grandmaster set on the browser (gnb_rk.c, 14.2, piece G2): a whole best of 3 as the console sees it - the set views from the first strike to
   the result - with the decisions that are pure: when the next game starts (go), when the screen waits for the server to count the game that was just played (wait), when the same game
   is played again (a tie or a void game: GNRK_REPLAY_FRAMES without a change), the pick grid on this console's character step, the prep screen from the set view, hold B, the end
   of the set and its texts.  usage: gnb_rk_test */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"
#include "../../../../client_net/mnc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define STREQ(a, b) do { if (strcmp((a), (b))) { printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); fails++; } } while (0)

static const unsigned char G1[5] = { 0x02, 0x1C, 0x08, 0x20, 0x1F };
static const unsigned char G2[6] = { 0x02, 0x1C, 0x08, 0x20, 0x1F, 0x03 };
static const unsigned char NOCH[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

/* the set view: phase, game, scores, step (1 strike 2 ban 3 pick stage 4 character), owner slot, avail mask, stage once ready */
static struct gnp_gm sv(unsigned phase, unsigned game, unsigned s1, unsigned s2, unsigned step, unsigned owner, unsigned avail, unsigned stage, const unsigned char *lst, unsigned n)
{
    struct gnp_gm s;
    unsigned i;
    memset(&s, 0, sizeof s);
    s.have = 1; s.phase = (unsigned char)phase; s.my_slot = 1; s.best_of = 3; s.game = (unsigned char)game; s.score[0] = (unsigned char)s1; s.score[1] = (unsigned char)s2;
    s.step = (unsigned char)step; s.owner = (unsigned char)(step ? owner : 0xFF); s.secs = 25; s.avail = (unsigned char)avail; s.n = (unsigned char)n; s.stage = (unsigned char)stage;
    for (i = 0; i < n; i++) s.id[i] = lst[i];
    s.remaining = 1;
    return s;
}

static struct gnb G;

static void begin(unsigned played)
{
    gnb_enter(&G, 1);
    G.req = GNB_REQ_NONE;
    gnrk_start(&G, played);
}

static void game1(void)
{
    struct gnp_gm s;
    char a[40];
    unsigned i;
    begin(0);
    /* game 1: the other slot strikes first (slot 2), then this console (slot 1) strikes twice */
    s = sv(0, 1, 0, 0, 1, 2, 0, 0xFF, G1, 5);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.scr == GNB_LOBBY && G.rk.on && !G.rk.go && !G.rk.wait && !G.rk.fin);
    CHECK(G.prep.open && !G.prep.mine && G.prep.step == GNP_STRIKE && G.prep.kind == 1);
    CHECK(G.lob.v6 && G.lob.best_of == 3 && G.lob.game == 1 && G.lob.side == 0 && G.lob.flags == (GNB_V_CALLED | GNB_V_ONSIDE));
    STREQ(G.lob.side_name[0], "Ann"); STREQ(G.lob.side_name[1], "Bob"); STREQ(G.lob.rname, "Grandmaster");
    CHECK(G.lob.turn == 0xFF && G.lob.step == GNP_RV_STRIKE && G.lob.step_owner == 1);
    CHECK(G.play == 0);                                                    /* a ranked set never takes the room's "start the search" route */
    gnp_title(&G.prep, a); STREQ(a, "Your opponent strikes");
    s = sv(0, 1, 0, 0, 1, 1, 0x1E, 0xFF, G1, 5); s.state[0] = 1;
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.prep.open && G.prep.mine && G.prep.cur == 1);
    gnb_frame(&G, GNB_B_A, 0, 0); gnb_frame(&G, 0, 0, 0); gnb_frame(&G, GNB_B_A, 0, 0);                  /* A marks, A on OK sends */
    CHECK(G.req == GNB_REQ_ACTION && G.act == 5 && G.act_val == 1 && G.act_cos == 0xFF);                 /* RK_SET_ACTION 5 (strike) with the index into the list */
    G.req = GNB_REQ_NONE;
    /* the stage is fixed: READY for game 1, never started: go */
    s = sv(1, 1, 0, 0, 0, 0, 0, 0x1F, G1, 5); s.state[0] = s.state[1] = s.state[2] = s.state[3] = 1;
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.rk.go && !G.rk.wait && G.rk.game == 1 && G.rk.stage == 0x1F && G.rk.ch == 0xFF);
    CHECK(G.prep.open && G.prep.step == GNP_PLAY && G.prep.chosen == 4);
    gnp_title(&G.prep, a); STREQ(a, "Stage: Battlefield");
    for (i = 0; i < 4; i++) CHECK(G.prep.state[i] == GNP_S_STRUCK);
}

static void between_games(void)
{
    struct gnp_gm s;
    unsigned i;
    char a[40];
    /* back from game 1 (the hand-off block says game 1 was started): the server has not counted it yet - READY for game 1 again */
    begin(1);
    s = sv(1, 1, 0, 0, 0, 0, 0, 0x1F, G1, 5);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(!G.rk.go && G.rk.wait && G.prep.open && G.prep.step == GNP_WAIT && !G.prep.mine);
    gnp_title(&G.prep, a); STREQ(a, "Waiting for your opponent");
    CHECK(!strcmp(gnb_foot(&G), "Hold B: Leave"));
    /* the wait counts frames; the server moves on (game 2, the winner bans) before the replay rule fires */
    for (i = 0; i < 100u; i++) gnb_frame(&G, 0, 0, 0);
    CHECK(G.rk.waited == 100u);
    s = sv(0, 2, 1, 0, 2, 1, 0x3F, 0xFF, G2, 6);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(!G.rk.go && !G.rk.wait && G.rk.waited == 0 && G.prep.step == GNP_BAN && G.prep.mine);
    /* the loser picks; the characters come with the winner first; this console (slot 1, the winner) picks the character: the grid */
    s = sv(0, 2, 1, 0, 4, 1, 0, 0xFF, G2, 6);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.prep.open && G.prep.step == GNP_CHAR && G.prep.mine && G.lob.step == GNB_STEP_PICK_CHAR && G.lob.step_owner == 0 && G.lob.turn == 0);      /* the character step stays on the prep screen: OK / Change */
    {   /* the set panel: no queue, streak, crowns or room type */
        char l[GNB_LINE + 1];
        gnb_panel(&G, 0, l); STREQ(l, "Grandmaster");
        gnb_panel(&G, 1, l); CHECK(strstr(l, "Your pick") != 0);
        gnb_panel(&G, 2, l); STREQ(l, "Game 2 of 3");
        gnb_panel(&G, 3, l); STREQ(l, "Score: 1 - 0");
        gnb_panel(&G, 4, l); STREQ(l, "You: Ann");
        gnb_panel(&G, 5, l); STREQ(l, "Opponent: Bob");
    }
    G.timer = 0;
    gnb_frame(&G, GNB_B_A, 0, 0); gnb_frame(&G, 0, 0, 0);
    CHECK(G.pick_open);                                                    /* nothing picked before: A is Change, the room's grid opens */
    gnb_frame(&G, GNB_B_A, 0, 0);
    CHECK(G.req == GNB_REQ_ACTION && G.act == 4);                          /* RK_SET_ACTION 4 (character), value = the cell, costume in act_cos */
    G.req = GNB_REQ_NONE; G.pick_open = 0;
    /* the other console's character step: no grid for this one */
    s = sv(0, 2, 1, 0, 4, 2, 0, 0xFF, G2, 6);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.prep.open && G.prep.step == GNP_CHAR && !G.prep.mine && G.lob.step_owner == 1 && G.lob.turn == 1);
    gnb_frame(&G, GNB_B_A, 0, 0); gnb_frame(&G, 0, 0, 0);
    CHECK(!G.pick_open);
    /* READY for game 2: go, with the character and costume the server holds for this slot (Fox 2 costume 3 as slot 1, Pikachu 13 as slot 2) */
    {
        unsigned char ch[4];
        ch[0] = 2; ch[1] = 3; ch[2] = 13; ch[3] = 0;
        s = sv(1, 2, 1, 0, 0, 0, 0, 0x08, G2, 6);
        gnrk_view(&G, &s, ch, "Ann", "Bob");
        CHECK(G.rk.go && G.rk.game == 2 && G.rk.stage == 0x08 && G.rk.ch == 2 && G.rk.cos == 3);
        CHECK(G.lob.side_char[0] == 2 && G.lob.side_char[1] == 13);
    }
}

static void replay_and_set_end(void)
{
    struct gnp_gm s;
    unsigned i;
    /* a tie or a void game: the server shows the SAME game READY again; the console waits GNRK_REPLAY_FRAMES, then plays it again with the same stage */
    begin(2);
    s = sv(1, 2, 1, 0, 0, 0, 0, 0x08, G2, 6);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.rk.wait && !G.rk.go);
    for (i = 0; i < GNRK_REPLAY_FRAMES; i++) gnb_frame(&G, 0, 0, 0);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.rk.go && !G.rk.wait && G.rk.game == 2 && G.rk.stage == 0x08);
    /* the next game is READY at once when its number is above the last started one (the server counted the game and the selection finished while this console was away) */
    begin(2);
    s = sv(1, 3, 1, 1, 0, 0, 0, 0x20, G2, 6);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.rk.go && G.rk.game == 3);
    /* the set is over: the result replaces the prep screen; A (or B) leaves */
    begin(3);
    s = sv(2, 3, 1, 2, 0, 0, 0, 0xFF, G2, 6); s.result = 2; s.score[0] = 1; s.score[1] = 2;
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    CHECK(G.rk.fin && !G.rk.go && !G.prep.open && !G.exit);
    STREQ(G.msg, "You lost the set 1 - 2");
    gnb_frame(&G, GNB_B_A, 0, 0);
    CHECK(G.exit);
    begin(3);
    s = sv(2, 3, 2, 1, 0, 0, 0, 0xFF, G2, 6); s.result = 1; s.score[0] = 2; s.score[1] = 1;
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    STREQ(G.msg, "You won the set 2 - 1");
    s = sv(2, 2, 1, 0, 0, 0, 0, 0xFF, G2, 6); s.result = 0;
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    STREQ(G.msg, "The set is over");
    /* the header's rank icons: this console's tier first (slot 2 here), 0xFF = placement */
    begin(0);
    s = sv(0, 1, 0, 0, 1, 1, 0x1F, 0xFF, G1, 5); s.my_slot = 2; s.tier[0] = 4; s.tier[1] = 0xFF;
    gnrk_view(&G, &s, NOCH, "Bob", "Ann");
    CHECK(G.prep.tier[0] == 0xFF && G.prep.tier[1] == 4);
    /* this console is slot 2: the scores and the sides are read from its side */
    begin(0);
    s = sv(0, 2, 1, 0, 3, 2, 0x3E, 0xFF, G2, 6); s.my_slot = 2;
    gnrk_view(&G, &s, NOCH, "Bob", "Ann");
    CHECK(G.lob.side == 1 && G.prep.sc_me == 0 && G.prep.sc_opp == 1 && G.prep.mine && G.prep.step == GNP_PICK);
    STREQ(G.lob.side_name[1], "Bob"); STREQ(G.lob.side_name[0], "Ann");
}

static void leave_and_guards(void)
{
    struct gnp_gm s;
    unsigned i;
    begin(0);
    s = sv(0, 1, 0, 0, 1, 2, 0, 0xFF, G1, 5);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    /* hold B 1.0 s forfeits: the request is a leave (the glue sends MATCH_STATUS abandoned) */
    for (i = 0; i < GNB_HOLD_B_FRAMES + 2u; i++) gnb_frame(&G, GNB_B_B, 0, 0);
    CHECK(G.req == GNB_REQ_LEAVE && G.scr == GNB_BUSY);
    /* cube the first view has not come (scr BUSY on the state request): hold B still forfeits */
    begin(0);
    G.scr = GNB_BUSY; G.busy_what = GNB_REQ_STATE; G.req = GNB_REQ_NONE; G.hold_b = 0;
    for (i = 0; i < GNB_HOLD_B_FRAMES + 2u; i++) gnb_frame(&G, GNB_B_B, 0, 0);
    CHECK(G.req == GNB_REQ_LEAVE && G.busy_what == GNB_REQ_LEAVE);
    /* nothing of the room works: Y (crowns), X (watch), Z (leave queue) do nothing, no pad touches the room actions */
    begin(0);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    gnb_frame(&G, GNB_B_Y, 0, 0); gnb_frame(&G, 0, 0, 0);
    gnb_frame(&G, GNB_B_X, 0, 0); gnb_frame(&G, 0, 0, 0);
    gnb_frame(&G, GNB_B_Z, 0, 0); gnb_frame(&G, 0, 0, 0);
    CHECK(!G.ros_open && !G.watch && G.req == GNB_REQ_NONE);
    /* the view of a room is not touched by the prep of the room model: a wrong or missing view changes nothing */
    {
        struct gnp_gm bad;
        memset(&bad, 0, sizeof bad);
        begin(1);
        gnrk_view(&G, &bad, NOCH, "Ann", "Bob");
        CHECK(!G.lob.have && !G.rk.go);
        bad.have = 1; bad.my_slot = 3;
        gnrk_view(&G, &bad, NOCH, "Ann", "Bob");
        CHECK(!G.lob.have);
    }
    /* the wait counter stops at its limit and an ordinary room (rk.on = 0) does not count */
    begin(1);
    s = sv(1, 1, 0, 0, 0, 0, 0, 0x1F, G1, 5);
    gnrk_view(&G, &s, NOCH, "Ann", "Bob");
    G.rk.waited = 0xFFFFu; gnb_frame(&G, 0, 0, 0);
    CHECK(G.rk.waited == 0xFFFFu);
    gnb_enter(&G, 1);
    CHECK(!G.rk.on);
    for (i = 0; i < 10u; i++) gnb_frame(&G, 0, 0, 0);
    CHECK(G.rk.waited == 0);
}

int main(void)
{
    game1();
    between_games();
    replay_and_set_end();
    leave_and_guards();
    printf("gnb rk host test ok: %d failures\n", fails);
    return fails ? 1 : 0;
}
