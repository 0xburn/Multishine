/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of the stage prep screen model (gnb_prep.c) and of its place in the Game Night lobby (gnb.c): a Sweaty set from the first strike to the decided stage, the wait
   between two games, the loser's stage pick, a Grandmaster set (ban, Dave's rule, the strike order), the cursor and the busy guard, the clock, the texts (menu font safe
   characters only), the ticket name of each game, and the golden screen of every step.  usage: gnb_prep_test */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"
#include "../../../../client_net/mnc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
#define STREQ(a, b) do { if (strcmp((a), (b))) { printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, (a), (b)); fails++; } } while (0)

static const unsigned char G1[5] = { 0x02, 0x1C, 0x08, 0x20, 0x1F };           /* Fountain, Dream Land, Yoshi's, Final Destination, Battlefield */
static const unsigned char G2[6] = { 0x02, 0x1C, 0x08, 0x20, 0x1F, 0x03 };     /* + Pokemon Stadium */

static void press(struct gnp *p, unsigned b) { gnp_frame(p, b, 0, 0); gnp_frame(p, 0, 0, 0); }          /* one press and one idle frame */
static void mark_ok(struct gnp *p) { press(p, 0x0100u); press(p, 0x0100u); }                          /* A marks the row, A on OK sends it */

static void glyphs_ok(const char *s)
{
    unsigned i;
    for (i = 0; s[i]; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '\'' || c == ',' || c == '-' || c == '.' || c == ':' || c == ' ')) { printf("FAIL glyph '%c' in \"%s\"\n", c, s); fails++; }
    }
}
static void all_text_ok(const struct gnp *p)
{
    char l[16][56], a[40], b[40];
    unsigned i, n = gnp_text(p, l, 16u);
    for (i = 0; i < n; i++) { glyphs_ok(l[i]); CHECK(strlen(l[i]) <= 52u); }
    gnp_title(p, a); glyphs_ok(a); gnp_sub(p, a); glyphs_ok(a); gnp_score(p, a); glyphs_ok(a); gnp_clock(p, a); glyphs_ok(a);
    for (i = 0; i < p->n; i++) { gnp_row(p, i, a, b); glyphs_ok(a); glyphs_ok(b); }
    glyphs_ok(gnp_foot(p));
}

static struct gnp_t6 t6(unsigned game, unsigned sw, unsigned sc, unsigned step, unsigned owner, unsigned secs, unsigned avail, unsigned struck, const unsigned char *lst, unsigned n)
{
    struct gnp_t6 t;
    unsigned i;
    memset(&t, 0, sizeof t);
    t.have = 1; t.rtype = 1; t.best_of = 3; t.game = (unsigned char)game; t.sc[0] = (unsigned char)sw; t.sc[1] = (unsigned char)sc; t.step = (unsigned char)step; t.owner = (unsigned char)owner;
    t.secs = (unsigned char)secs; t.avail = (unsigned char)avail; t.struck = (unsigned char)struck; t.n = (unsigned char)n; t.forced = 0xFF;
    for (i = 0; i < n; i++) t.st[i] = lst[i];
    return t;
}

static void put_t6(struct gnb_lobby *v, struct gnp_t6 t)       /* the trailer fields of the lobby view (what sg_entry.c copies from the parsed view) */
{
    unsigned i;
    v->v6 = t.have; v->rtype = t.rtype; v->best_of = t.best_of; v->game = t.game; v->score_w = t.sc[0]; v->score_c = t.sc[1]; v->step = t.owner == 0xFFu && !t.step ? 0 : t.step; v->step_owner = t.owner;
    v->step_secs = t.secs; v->stage_avail = t.avail; v->stage_n = t.n; v->forced = t.forced; v->struck = t.struck;
    for (i = 0; i < 8u; i++) v->stage_list[i] = t.st[i];
}

static void model(void)
{
    struct gnp p;
    struct gnp_t6 t;
    char a[48], b[48], l[16][56];
    unsigned n;
    gnp_clear(&p);
    CHECK(!p.open);

    /* game 1, my step (side 0): five stages free, the clock at 30 s */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 30, 0x1F, 0, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    CHECK(p.open && p.step == GNP_STRIKE && p.mine && p.n == 5 && p.cur == 0 && !p.busy);
    gnp_title(&p, a); STREQ(a, "Strike a stage");
    gnp_sub(&p, a); STREQ(a, "Strike 1 of 4");
    gnp_score(&p, a); STREQ(a, "Game 1 of 3   0 - 0");
    gnp_clock(&p, a); STREQ(a, "0:30");
    STREQ(gnp_foot(&p), "A: Select  Hold B: Leave");
    n = gnp_text(&p, l, 16u);
    CHECK(n == 9);
    STREQ(l[0], "Strike a stage"); STREQ(l[3], "- Fountain of Dreams"); STREQ(l[4], "  Dream Land"); STREQ(l[7], "  Battlefield"); STREQ(l[8], "A: Select  Hold B: Leave");
    all_text_ok(&p);

    /* the cursor: down twice, a press sends GN_ACTION strike with the index; a second press while the answer is out is refused */
    gnp_frame(&p, 0x0004u, 0, 0); gnp_frame(&p, 0, 0, 0); gnp_frame(&p, 0, 0, 1);
    CHECK(p.cur == 2);
    gnp_frame(&p, 0x0100u, 0, 0);                                        /* A marks the stage: nothing is sent yet, OK / Redo take the prompt's place */
    CHECK(p.act == 0 && p.nsel == 1 && p.sel[2] && p.confirm && p.btn == 0 && !p.busy);
    gnp_msg(&p, a); STREQ(a, ""); STREQ(gnp_foot(&p), "A: Confirm  B: Redo  Hold B: Leave");
    gnp_frame(&p, 0, 0, 0); gnp_frame(&p, 0x0002u, 0, 0); CHECK(p.btn == 1);                   /* right: Redo; A on Redo takes the mark back */
    press(&p, 0x0100u); CHECK(p.nsel == 0 && !p.confirm && p.act == 0);
    press(&p, 0x0100u); CHECK(p.confirm); press(&p, 0x0200u); CHECK(!p.confirm && p.nsel == 0);   /* B in the confirm state is Redo too */
    press(&p, 0x0100u); press(&p, 0x0100u);                              /* mark, then A on OK */
    CHECK(p.act == GNP_A_STRIKE && p.val == 2 && p.busy);
    p.act = 0; p.sfx = 0;
    gnp_frame(&p, 0x0100u, 0, 0);
    CHECK(p.act == 0 && p.sfx == 4);

    /* the answer: Yoshi's is struck, our is still me (the second side strikes two, the first side one): the busy guard ends, the cursor skips the struck row */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 28, 0x1B, 0x04, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    CHECK(!p.busy && p.cur == 0 && p.state[2] == GNP_S_STRUCK && p.struck_n == 1);
    gnp_sub(&p, a); STREQ(a, "Strike 2 of 4");
    gnp_row(&p, 2, a, b); STREQ(a, "Yoshi's Story"); STREQ(b, "Struck"); CHECK(gnp_row(&p, 2, a, b) == 1);
    gnp_frame(&p, 0x0004u, 0, 0); gnp_frame(&p, 0, 0, 0); gnp_frame(&p, 0x0004u, 0, 0); gnp_frame(&p, 0, 0, 0);
    CHECK(p.cur == 3);                                                  /* 0 -> 1 -> 3 (row 2 is struck) */
    press(&p, 0x0100u); press(&p, 0x0004u); press(&p, 0x0100u); CHECK(p.confirm && p.nsel == 2);     /* the second player's first strike asks for two marks */
    press(&p, 0x0100u); CHECK(p.act == GNP_A_STRIKE && p.val == 3 && p.pend == 4); p.act = 0;
    all_text_ok(&p);

    /* the opponent's step (side 1 owns it): no cursor, A does nothing, the title names them */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 25, 0x13, 0x0C, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    CHECK(p.open && !p.mine && p.struck_n == 2);
    gnp_title(&p, a); STREQ(a, "Your opponent strikes");
    STREQ(gnp_foot(&p), "Hold B: Leave");
    gnp_frame(&p, 0x0100u, 0, 0); CHECK(p.act == 0 && !p.busy);
    n = gnp_text(&p, l, 16u); STREQ(l[3], "  Fountain of Dreams");      /* no cursor mark on the other side's step */
    all_text_ok(&p);

    /* the clock counts down between two views (30 frames a second) and never wraps */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 3, 0x13, 0x0C, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    gnp_clock(&p, a); STREQ(a, "0:03");
    { unsigned i; for (i = 0; i < 31u; i++) gnp_frame(&p, 0, 0, 0); }
    gnp_clock(&p, a); STREQ(a, "0:02");
    { unsigned i; for (i = 0; i < 300u; i++) gnp_frame(&p, 0, 0, 0); }
    gnp_clock(&p, a); STREQ(a, "0:00");
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 0xFF, 0x13, 0x0C, G1, 5);        /* no clock */
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    gnp_clock(&p, a); STREQ(a, "");

    /* the stage is fixed (turn GO): the one row left is selected, the rest struck */
    t = t6(1, 0, 0, 0, 0xFF, 0xFF, 0, 0x0E, G1, 5);
    t.struck = 0x0F | 0x00; t.struck = 0x0F;
    gnp_from_room(&p, &t, 0, 1, 1, GNP_TURN_GO, 0x1F, "Ann", "Bob");
    CHECK(p.open && p.step == GNP_PLAY && p.chosen == 4 && p.state[4] == GNP_S_CHOSEN);
    gnp_title(&p, a); STREQ(a, "Stage: Battlefield"); gnp_sub(&p, a); STREQ(a, "Starting the game");
    CHECK(gnp_row(&p, 4, a, b) == 2); STREQ(b, "Selected");
    all_text_ok(&p);

    /* between two games: the score counts the game just won, nothing to press */
    t = t6(2, 1, 0, 0, 0xFF, 0xFF, 0, 0, G2, 6);
    gnp_from_room(&p, &t, 1, 1, 1, GNP_TURN_WAIT, 0xFF, "Ann", "Bob");
    CHECK(p.open && p.step == GNP_WAIT && !p.mine);
    gnp_title(&p, a); STREQ(a, "Waiting for your opponent");
    gnp_score(&p, a); STREQ(a, "Game 2 of 3   0 - 1");                   /* I am side 1: they lead 1 - 0 */
    gnp_sub(&p, a); STREQ(a, "Both must be in the room");
    all_text_ok(&p);

    /* game 2: the loser (side 1) picks among six */
    t = t6(2, 1, 0, GNP_RV_PICK_STAGE, 1, 30, 0x3F, 0, G2, 6);
    gnp_from_room(&p, &t, 1, 1, 1, 1, 0xFF, "Ann", "Bob");
    CHECK(p.step == GNP_PICK && p.mine && p.n == 6);
    gnp_title(&p, a); STREQ(a, "Pick a stage");
    STREQ(gnp_foot(&p), "A: Select  Hold B: Leave");
    gnp_frame(&p, 0, 0, 1); gnp_frame(&p, 0, 0, 0);
    mark_ok(&p);
    CHECK(p.act == GNP_A_PICK && p.val == 1);
    p.act = 0;
    t = t6(2, 1, 0, GNP_RV_PICK_STAGE, 1, 30, 0x3F, 0, G2, 6);
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");              /* the other side of the same step */
    gnp_title(&p, a); STREQ(a, "Your opponent picks a stage");
    all_text_ok(&p);

    /* a character step: the prep screen gives way to the lobby's own grid */
    t = t6(1, 0, 0, GNP_RV_PICK_CHAR, 0, 30, 0, 0, G1, 5);                /* game 1: the lobby's own grid */
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    CHECK(!p.open);
    /* game 2: the character step stays on this screen: OK keeps last game's fighter, Change (or B) opens the grid */
    t = t6(2, 1, 0, GNP_RV_PICK_CHAR, 0, 30, 0, 0, G2, 6);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0x1F, "Ann", "Bob");
    CHECK(p.open && p.step == GNP_CHAR && p.mine && p.confirm && p.btn == 0 && p.chosen == 3);
    gnp_msg(&p, a); STREQ(a, ""); STREQ(gnp_btn_label(&p, 1), "Change"); STREQ(gnp_foot(&p), "A: Confirm  B: Change  Hold B: Leave");
    p.prevc = 12; p.cos = 3;
    press(&p, 0x0100u); CHECK(p.act == GNP_A_CHAR && p.val == 12 && p.busy && !p.grid); p.act = 0; p.busy = 0;
    press(&p, 0x0200u); CHECK(p.grid); p.grid = 0;
    press(&p, 0x0002u); CHECK(p.btn == 1); press(&p, 0x0100u); CHECK(p.grid); p.grid = 0;
    p.prevc = 0xFF; p.btn = 0; press(&p, 0x0100u); CHECK(p.grid && p.act == 0);                              /* nothing picked before: A is Change */
    gnp_from_room(&p, &t, 1, 1, 1, 0, 0x1F, "Ann", "Bob");                                             /* the other player's step */
    CHECK(p.open && p.step == GNP_CHAR && !p.mine && !p.confirm);
    gnp_msg(&p, a); STREQ(a, "Waiting for opponent to choose character");
    { struct gnp_tl tl[GNP_TL_MAX]; unsigned n; p.me_first = 1; n = gnp_timeline(&p, tl); CHECK(n == 3 && tl[0].state == 2 && tl[1].state == 2 && tl[2].state == 1 && tl[2].id[0] == 0xFF); }       /* the winner (first) is done, the loser's fighter step is lit */
    /* not called up, not on a side, a one-game room, a malformed list: closed */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 30, 0x1F, 0, G1, 5);
    gnp_from_room(&p, &t, 0xFF, 1, 1, 0, 0xFF, "Ann", "Bob"); CHECK(!p.open);
    gnp_from_room(&p, &t, 0, 1, 0, 0, 0xFF, "Ann", "Bob"); CHECK(!p.open);
    gnp_from_room(&p, &t, 0, 2, 1, 0xFF, 0xFF, "Ann", "Bob"); CHECK(!p.open);
    t.best_of = 1; gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob"); CHECK(!p.open);
}

static struct gnp_gm gm(unsigned phase, unsigned slot, unsigned game, unsigned s1, unsigned s2, unsigned step, unsigned owner, unsigned avail, const unsigned char *st, const unsigned char *id, unsigned n)
{
    struct gnp_gm s;
    unsigned i;
    memset(&s, 0, sizeof s);
    s.have = 1; s.phase = (unsigned char)phase; s.my_slot = (unsigned char)slot; s.best_of = 3; s.game = (unsigned char)game; s.score[0] = (unsigned char)s1; s.score[1] = (unsigned char)s2;
    s.step = (unsigned char)step; s.owner = (unsigned char)owner; s.avail = (unsigned char)avail; s.n = (unsigned char)n; s.stage = 0xFF; s.secs = 30;
    for (i = 0; i < n; i++) { s.id[i] = id[i]; s.state[i] = st[i]; }
    return s;
}

static void grandmaster(void)
{
    struct gnp p;
    struct gnp_gm s;
    char a[40], b[40];
    static const unsigned char free5[5] = { 0, 0, 0, 0, 0 };
    static const unsigned char ban6[6] = { 0, 0, 0, 0, 0, 0 };
    static const unsigned char dave6[6] = { 0, 0, 3, 0, 2, 0 };            /* Yoshi's: not allowed (Dave's rule), Battlefield: banned */
    gnp_clear(&p);
    /* game 1: strikes, slot 1 strikes first */
    s = gm(0, 1, 1, 0, 0, 1, 1, 0x1F, free5, G1, 5); s.remaining = 1;
    gnp_from_gm(&p, &s, "Ann", "Bob");
    CHECK(p.open && p.kind == 1 && p.step == GNP_STRIKE && p.mine);
    gnp_sub(&p, a); STREQ(a, "Last stage left is played");
    s.remaining = 2; gnp_from_gm(&p, &s, "Ann", "Bob"); gnp_sub(&p, a); STREQ(a, "2 strikes left");
    press(&p, 0x0100u); CHECK(p.act == 0 && p.nsel == 1 && !p.confirm);          /* two strikes asked: the first mark alone does not confirm */
    press(&p, 0x0004u); press(&p, 0x0100u); CHECK(p.confirm && p.nsel == 2);
    press(&p, 0x0100u); CHECK(p.act == GNP_A_STRIKE && p.val == 0 && p.pend == 1 && p.busy); p.act = 0;       /* the first goes now, the second when the view with the first struck is back */
    s.state[0] = 1; s.remaining = 1; gnp_from_gm(&p, &s, "Ann", "Bob");
    CHECK(p.act == GNP_A_STRIKE && p.val == 1 && p.pend == 0xFF && p.busy); p.act = 0;
    s.state[1] = 1; s.remaining = 0; s.step = 1; s.owner = 2; gnp_from_gm(&p, &s, "Ann", "Bob"); CHECK(!p.mine && p.nsel == 0 && p.act == 0);
    /* game 2: I won game 1 (slot 1): I ban, then they pick */
    s = gm(0, 1, 2, 1, 0, 2, 1, 0x3F, ban6, G2, 6);
    gnp_from_gm(&p, &s, "Ann", "Bob");
    gnp_title(&p, a); STREQ(a, "Ban a stage"); STREQ(gnp_foot(&p), "A: Select  Hold B: Leave");
    gnp_frame(&p, 0, 0, 1); gnp_frame(&p, 0, 0, 0); mark_ok(&p); CHECK(p.act == GNP_A_BAN && p.val == 1); p.act = 0;
    gnp_score(&p, a); STREQ(a, "Game 2 of 3   1 - 0");
    /* the loser's pick with Dave's rule: the disabled and the banned row cannot be chosen; the cursor never lands on them */
    s = gm(0, 2, 2, 1, 0, 3, 2, 0x29, dave6, G2, 6);
    gnp_from_gm(&p, &s, "Bob", "Ann");
    CHECK(p.step == GNP_PICK && p.mine && p.state[2] == GNP_S_DAVE && p.state[3] == GNP_S_BANNED);      /* shown in Slippi's order: Battlefield (server row 4) is the fourth, Final Destination the fifth */
    CHECK(p.id[3] == 0x1F && p.srv[3] == 4 && p.id[4] == 0x20 && p.srv[4] == 3);
    CHECK(gnp_row(&p, 2, a, b) == 1); STREQ(b, "Not allowed"); CHECK(gnp_row(&p, 3, a, b) == 1); STREQ(b, "Banned");
    gnp_frame(&p, 0x0004u, 0, 0); gnp_frame(&p, 0, 0, 0);
    gnp_frame(&p, 0x0004u, 0, 0); gnp_frame(&p, 0, 0, 0);
    CHECK(p.cur == 5);                                                     /* free rows: 0, 1, 4, 5 (the avail mask is the server's; the model trusts the states) */
    mark_ok(&p); CHECK(p.act == GNP_A_PICK && p.val == 5);
    all_text_ok(&p);
    /* the game is decided */
    s = gm(1, 1, 2, 1, 0, 0, 0xFF, 0, ban6, G2, 6); s.stage = 0x20; s.state[0] = 1; s.state[1] = 1; s.state[2] = 1; s.state[4] = 1; s.state[5] = 1;
    gnp_from_gm(&p, &s, "Ann", "Bob");
    CHECK(p.step == GNP_PLAY && p.chosen == 4);
    gnp_title(&p, a); STREQ(a, "Stage: Final Destination");
    /* a later game's character step is on the prep screen (OK / Change), game 1's is not; the finished set has no screen */
    s = gm(0, 1, 2, 1, 0, 4, 1, 0, ban6, G2, 6); gnp_from_gm(&p, &s, "Ann", "Bob"); CHECK(p.open && p.step == GNP_CHAR && p.mine && p.confirm);
    s = gm(0, 1, 1, 0, 0, 4, 1, 0, ban6, G2, 6); gnp_from_gm(&p, &s, "Ann", "Bob"); CHECK(!p.open);
    s = gm(2, 1, 2, 2, 0, 0, 0xFF, 0, ban6, G2, 6); gnp_from_gm(&p, &s, "Ann", "Bob"); CHECK(!p.open);
}

/* the lobby: a view that carries the trailer opens the screen, input goes to it, the request goes out as GN_ACTION, the ticket name carries the game */
static void lobby(void)
{
    static struct gnb g;
    struct gnb_lobby v;
    struct sg_view V;
    unsigned r, found = 0;
    memset(&v, 0, sizeof v);
    gnb_enter(&g, 1);
    g.req = GNB_REQ_NONE;
    v.have = 1; v.phase = 1; v.flags = GNB_V_CALLED | GNB_V_ONSIDE; v.members = 2; v.side = 0; v.v3 = 1; v.cap = 8; v.turn = 0; v.stage = 0xFF; v.secs = 28;
    strcpy(v.code, "4242"); strcpy(v.rname, "Sweaty"); v.side_on[0] = v.side_on[1] = 1; strcpy(v.side_name[0], "Ann"); strcpy(v.side_name[1], "Bob");
    put_t6(&v, t6(1, 0, 0, GNP_RV_STRIKE, 0, 28, 0x1F, 0, G1, 5));
    gnb_net_view(&g, &v);
    CHECK(g.scr == GNB_LOBBY && g.prep.open && g.prep.mine && !g.pick_open);
    CHECK(!strcmp(gnb_foot(&g), "A: Select  Hold B: Leave"));
    memset(&V, 0, sizeof V);
    gnb_text(&g, &V, 0);
    for (r = 0; r < SG_ROWS; r++) if (strstr(V.t[r], "Strike a stage")) found = 1;
    CHECK(found);
    /* a press of A on stage 0 raises the request; stick down first */
    gnb_frame(&g, 0, 0, 1); gnb_frame(&g, 0, 0, 0);
    gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0); gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0);
    CHECK(g.req == GNB_REQ_ACTION && g.act == 5 && g.act_val == 1 && g.act_cos == 0xFF);
    g.req = GNB_REQ_NONE;
    /* the lobby's A on a character step still opens the grid: my_turn needs the character step in a v6 view */
    put_t6(&v, t6(1, 0, 0, GNP_RV_PICK_CHAR, 0, 28, 0, 0, G1, 5));
    gnb_net_view(&g, &v);
    CHECK(!g.prep.open);
    gnb_frame(&g, 0, 0, 0);
    gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0);
    CHECK(g.pick_open);
    g.pick_open = 0;
    /* a later game's character step on the prep screen: A with nothing picked before is Change (the grid opens); the grid's A sends the pick; back on the prep screen A (OK) sends the same fighter again */
    gnb_enter(&g, 1); g.req = GNB_REQ_NONE;
    v.set_id = 0; v.turn = 0; v.stage = 0x1F;
    put_t6(&v, t6(2, 1, 0, GNP_RV_PICK_CHAR, 0, 28, 0, 0, G2, 6)); v.step_owner = 0; v.step = GNP_RV_PICK_CHAR;
    gnb_net_view(&g, &v);
    CHECK(g.prep.open && g.prep.step == GNP_CHAR && g.prep.mine && !g.pick_open);
    gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0);
    CHECK(g.pick_open);                                                       /* nothing picked before: Change */
    gnb_net_view(&g, &v);
    CHECK(g.pick_open);                                                       /* a poll's view does not close the grid */
    gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0);
    CHECK(!g.pick_open && g.req == GNB_REQ_ACTION && g.act == 4 && g.prep.busy);
    g.req = GNB_REQ_NONE;
    gnb_net_view(&g, &v);                                                     /* the next view: still this console's step (the server has not applied it yet) */
    CHECK(g.prep.open && g.prep.step == GNP_CHAR && !g.prep.busy && g.prep.prevc < 26u);
    gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0);
    CHECK(g.req == GNB_REQ_ACTION && g.act == 4 && g.act_val == g.prep.prevc);   /* OK */
    g.req = GNB_REQ_NONE;
    /* a failed action frees the busy guard */
    put_t6(&v, t6(1, 0, 0, GNP_RV_STRIKE, 0, 28, 0x1F, 0, G1, 5));
    gnb_net_view(&g, &v);
    gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0); gnb_frame(&g, GNB_B_A, 0, 0); gnb_frame(&g, 0, 0, 0);
    CHECK(g.prep.busy);
    g.req = GNB_REQ_NONE;
    gnb_net_fail(&g, GNB_REQ_ACTION, GNB_W_BAD);
    CHECK(!g.prep.busy);
    /* the poll is faster while the screen is up (30 frames) */
    g.req = GNB_REQ_NONE; g.timer = 0;
    { unsigned i; for (i = 0; i < GNB_PREP_POLL; i++) gnb_frame(&g, 0, 0, 0); }
    CHECK(g.req == GNB_REQ_STATE);
    g.req = GNB_REQ_NONE;
    /* hold B leaves the room from the prep screen too */
    { unsigned i; for (i = 0; i < GNB_HOLD_B_FRAMES + 2u; i++) gnb_frame(&g, GNB_B_B, 0, 0); }
    CHECK(g.req == GNB_REQ_LEAVE);
    g.req = GNB_REQ_NONE;

    /* the match starts when the stage is fixed: turn GO; the ticket name carries the game of a Bo3 set (and none for a one-game room) */
    gnb_enter(&g, 1); g.req = GNB_REQ_NONE;
    v.set_id = 12; v.turn = GNB_TURN_GO; v.stage = 0x1F;
    put_t6(&v, t6(2, 1, 0, 0, 0xFF, 0xFF, 0, 0x2F, G2, 6));
    gnb_net_view(&g, &v);
    CHECK(g.play == 1 && !strcmp(g.play_name, "GN4242S12G2"));
    gnb_enter(&g, 1); g.req = GNB_REQ_NONE;
    v.v6 = 0; v.best_of = 0;
    gnb_net_view(&g, &v);
    CHECK(g.play == 1 && !strcmp(g.play_name, "GN4242S12") && !g.prep.open);
    /* 0xFD: both consoles must be back; the screen says so and the match does not start */
    gnb_enter(&g, 1); g.req = GNB_REQ_NONE;
    v.turn = GNP_TURN_WAIT; v.stage = 0xFF; put_t6(&v, t6(2, 1, 0, 0, 0xFF, 0xFF, 0, 0, G2, 6));
    gnb_net_view(&g, &v);
    CHECK(g.play == 0 && g.prep.open && g.prep.step == GNP_WAIT);
    CHECK(!strcmp(gnb_foot(&g), "Hold B: Leave"));
    /* a one-game room view with the v6 trailer (Chill) never opens the screen */
    gnb_enter(&g, 1); g.req = GNB_REQ_NONE;
    v.turn = 0; put_t6(&v, t6(1, 0, 0, GNP_RV_PICK_CHAR, 0, 30, 0, 0, G1, 0)); v.best_of = 1; v.stage_n = 0; v.rtype = 0;
    gnb_net_view(&g, &v);
    CHECK(!g.prep.open);
}

/* the GameSetup look: the big line, the bottom timeline, the order the strikes were seen in */
static void gamesetup(void)
{
    struct gnp p;
    struct gnp_t6 t;
    struct gnp_tl tl[GNP_TL_MAX];
    char a[48];
    unsigned n;
    gnp_clear(&p);
    CHECK(gnp_timeline(&p, tl) == 0 && p.ch[0] == 0xFF && p.tier[1] == 0xFF);
    /* game 1, nothing struck, my step: five entries, the fighters unknown, STRIKE 1 is the step now */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 30, 0x1F, 0, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    n = gnp_timeline(&p, tl);
    CHECK(n == 5 && tl[0].label == GNP_L_YOUR && tl[1].label == GNP_L_OPP && tl[2].label == GNP_L_STRIKE1 && tl[3].label == GNP_L_STRIKE23 && tl[4].label == GNP_L_STRIKE4);
    CHECK(tl[0].id[0] == 0xFF && tl[0].state == 0 && tl[2].state == 1 && tl[3].state == 0 && tl[3].n == 2 && tl[2].id[0] == 0xFF);
    STREQ(gnp_label(GNP_L_STRIKE23), "STRIKE 2/3"); STREQ(gnp_label(GNP_L_YOUR), "YOUR CHAR");
    gnp_msg(&p, a); STREQ(a, "Select a stage you do not want to play");
    gnp_sub_name(&p, 0, a); STREQ(a, "");
    gnp_codes(&p, "LUCK@0", "PMPT#810"); gnp_sub_name(&p, 0, a); STREQ(a, "LUCK#0"); gnp_sub_name(&p, 1, a); STREQ(a, "PMPT#810");
    /* the fighters, known to the lobby, show in the first two entries */
    p.ch[0] = 2; p.ch[1] = 12;
    n = gnp_timeline(&p, tl); CHECK(tl[0].id[0] == 2 && tl[0].state == 2 && tl[1].id[0] == 12);
    /* I struck Yoshi's (list index 2); the opponent strikes next, two stages: Fountain (0) and Final Destination (3) appear in ONE view, taken in list order */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 30, 0x1B, 0x04, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    gnp_msg(&p, a); STREQ(a, "Waiting for opponent to strike");
    n = gnp_timeline(&p, tl); CHECK(tl[2].id[0] == 0x08 && tl[2].state == 2 && tl[3].state == 1 && tl[3].id[0] == 0xFF);
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 28, 0x12, 0x0D, G1, 5);           /* free: Dream Land (1) and Battlefield (4); struck: 0, 2, 3 */
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    n = gnp_timeline(&p, tl);
    CHECK(tl[2].id[0] == 0x08 && tl[3].id[0] == 0x02 && tl[3].id[1] == 0x20 && tl[3].state == 2 && tl[4].state == 1 && tl[4].id[0] == 0xFF);
    /* a later view keeps the order (a refreshed view with the same states changes nothing) */
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    n = gnp_timeline(&p, tl); CHECK(tl[2].id[0] == 0x08 && tl[3].id[0] == 0x02 && p.order[2] == 1 && p.order[0] == 2 && p.order[3] == 3 && p.nord == 3);
    /* my last strike (the 4th), Dream Land struck, Battlefield left: the stage is played */
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 28, 0x12, 0x0D, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    gnp_msg(&p, a); STREQ(a, "Select a stage you do not want to play");
    n = gnp_timeline(&p, tl); CHECK(tl[4].state == 1);
    t = t6(1, 0, 0, 0, 0xFF, 0xFF, 0, 0x0F, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, GNP_TURN_GO, 0x1F, "Ann", "Bob");
    gnp_msg(&p, a); STREQ(a, "Get ready!");
    n = gnp_timeline(&p, tl); CHECK(n == 5 && tl[2].state == 2 && tl[3].state == 2 && tl[4].state == 2 && tl[4].id[0] == 0x1C);
    /* the second player's first strike asks for two */
    gnp_clear(&p);
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 30, 0x1B, 0x04, G1, 5);
    gnp_from_room(&p, &t, 1, 1, 1, 1, 0xFF, "Ann", "Bob");
    CHECK(p.mine && p.struck_n == 1);
    gnp_msg(&p, a); STREQ(a, "Select two stages you do not want to play");
    /* game 2: the loser picks: STAGE, YOUR, OPP (the fighters "?" until the stage is fixed); the stage fills when it is fixed */
    t = t6(2, 1, 0, GNP_RV_PICK_STAGE, 1, 30, 0x3F, 0, G2, 6);
    gnp_from_room(&p, &t, 1, 1, 1, 1, 0xFF, "Ann", "Bob");
    n = gnp_timeline(&p, tl); CHECK(n == 3 && tl[0].label == GNP_L_STAGE && tl[0].state == 1 && tl[0].id[0] == 0xFF && tl[1].label == GNP_L_OPP && tl[1].id[0] == 0xFF && tl[2].label == GNP_L_YOUR);      /* I lost game 1: the previous winner's fighter (OPP) comes first */
    gnp_msg(&p, a); STREQ(a, "Select a stage to play");
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob");
    gnp_msg(&p, a); STREQ(a, "Waiting for opponent to choose stage");
    t = t6(2, 1, 0, 0, 0xFF, 0xFF, 0, 0, G2, 6);
    gnp_from_room(&p, &t, 1, 1, 1, GNP_TURN_GO, 0x03, "Ann", "Bob");
    n = gnp_timeline(&p, tl); CHECK(tl[0].state == 2 && tl[0].id[0] == 0x03);
    t = t6(2, 1, 0, 0, 0xFF, 0xFF, 0, 0, G2, 6);
    gnp_from_room(&p, &t, 1, 1, 1, GNP_TURN_WAIT, 0xFF, "Ann", "Bob");
    gnp_msg(&p, a); STREQ(a, "Waiting for opponent");
    CHECK(p.sc_me == 0 && p.sc_opp == 1);
    all_text_ok(&p);
}

static void gamesetup_gm(void)
{
    struct gnp p;
    struct gnp_gm s;
    struct gnp_tl tl[GNP_TL_MAX];
    char a[48];
    static const unsigned char free5[5] = { 0, 0, 0, 0, 0 };
    static const unsigned char ban6[6] = { 0, 0, 0, 0, 0, 0 };
    static const unsigned char dave6[6] = { 0, 0, 3, 0, 2, 0 };
    unsigned n;
    gnp_clear(&p);
    s = gm(0, 1, 1, 0, 0, 1, 1, 0x1F, free5, G1, 5); s.remaining = 2;
    gnp_from_gm(&p, &s, "Ann", "Bob");
    gnp_msg(&p, a); STREQ(a, "Select two stages you do not want to play");
    n = gnp_timeline(&p, tl); CHECK(n == 5);
    /* game 2, the winner bans: BAN, STAGE, YOUR, OPP */
    s = gm(0, 1, 2, 1, 0, 2, 1, 0x3F, ban6, G2, 6);
    gnp_from_gm(&p, &s, "Ann", "Bob");
    n = gnp_timeline(&p, tl); CHECK(n == 4 && tl[0].label == GNP_L_BAN && tl[0].state == 1 && tl[1].label == GNP_L_STAGE && tl[1].state == 0 && tl[2].label == GNP_L_OPP && tl[3].label == GNP_L_YOUR);     /* (the view names no previous winner: not me) */
    s.prev_winner = 1; gnp_from_gm(&p, &s, "Ann", "Bob");
    n = gnp_timeline(&p, tl); CHECK(n == 4 && tl[2].label == GNP_L_YOUR && tl[3].label == GNP_L_OPP);      /* I won: my fighter first */
    gnp_msg(&p, a); STREQ(a, "Select a stage to ban");
    /* the loser picks: the ban is on the bar */
    s = gm(0, 2, 2, 1, 0, 3, 2, 0x29, dave6, G2, 6);
    gnp_from_gm(&p, &s, "Bob", "Ann");
    n = gnp_timeline(&p, tl); CHECK(n == 4 && tl[0].state == 2 && tl[0].id[0] == 0x1F && tl[1].state == 1);
    gnp_msg(&p, a); STREQ(a, "Select a stage to play");
    gnp_from_gm(&p, &s, "Bob", "Ann");
    gnp_msg(&p, a); STREQ(a, "Select a stage to play");
}


/* the rows are shown in Slippi's order whatever order the server lists them in; actions carry the server's index; the pips follow the set; the clock turns colour for the player to act */
static void slippi_order(void)
{
    static const unsigned char SRV5[5] = { 0x1F, 0x08, 0x02, 0x20, 0x1C };           /* Battlefield, Yoshi's, Fountain, Final Destination, Dream Land: the server's own order */
    static const unsigned char SRV6[6] = { 0x03, 0x20, 0x1F, 0x1C, 0x08, 0x02 };
    struct gnp p;
    struct gnp_t6 t;
    struct gnp_tl tl[GNP_TL_MAX];
    unsigned i;
    gnp_clear(&p);
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 30, 0x1F, 0, SRV5, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    CHECK(p.id[0] == 0x02 && p.id[1] == 0x1C && p.id[2] == 0x08 && p.id[3] == 0x20 && p.id[4] == 0x1F);           /* Fountain, Dream Land, Yoshi's, Final Destination, Battlefield */
    CHECK(p.srv[0] == 2 && p.srv[1] == 4 && p.srv[2] == 1 && p.srv[3] == 3 && p.srv[4] == 0);
    press(&p, 0x0004u); mark_ok(&p); CHECK(p.act == GNP_A_STRIKE && p.val == 4);                                  /* row 1 (Dream Land) is the server's index 4 */
    t = t6(2, 1, 0, GNP_RV_PICK_STAGE, 0, 30, 0x3F, 0, SRV6, 6);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    CHECK(p.id[0] == 0x02 && p.id[1] == 0x1C && p.id[2] == 0x08 && p.id[3] == 0x1F && p.id[4] == 0x20 && p.id[5] == 0x03);   /* the counterpick row puts Battlefield before Final Destination */
    /* B takes marks back; a mark on a stage that is not free is refused */
    gnp_clear(&p);
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 30, 0x1B, 0x04, G1, 5);
    gnp_from_room(&p, &t, 1, 1, 1, 1, 0xFF, "Ann", "Bob");
    press(&p, 0x0100u); CHECK(p.nsel == 1); press(&p, 0x0200u); CHECK(p.nsel == 0 && !p.confirm);
    press(&p, 0x0100u); press(&p, 0x0100u); CHECK(p.nsel == 0);                                                  /* A on a marked row takes the mark off */
    /* game 1: the first striker (seat 0) is the first on the bar whoever I am */
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob"); p.ch[0] = 1; p.ch[1] = 2; (void)gnp_timeline(&p, tl);
    CHECK(tl[0].label == GNP_L_YOUR && tl[0].id[0] == 1 && tl[1].label == GNP_L_OPP);
    gnp_from_room(&p, &t, 1, 1, 1, 1, 0xFF, "Ann", "Bob"); p.ch[0] = 2; p.ch[1] = 1; (void)gnp_timeline(&p, tl);
    CHECK(tl[0].label == GNP_L_OPP && tl[0].id[0] == 1 && tl[1].label == GNP_L_YOUR && tl[1].id[0] == 2);
    /* the pips: a set joined at 1 - 1 shows a win and a loss, a new score that grew by one fixes the game before it */
    gnp_clear(&p);
    memset(&p.res, 0, sizeof p.res); p.sc_seen = 0;
    t = t6(3, 1, 1, GNP_RV_PICK_STAGE, 0, 30, 0x3F, 0, G2, 6);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");
    CHECK(gnp_pip(&p, 0) == 1 && gnp_pip(&p, 1) == 2 && gnp_pip(&p, 2) == 3);
    t = t6(2, 0, 1, GNP_RV_PICK_STAGE, 0, 30, 0x3F, 0, G2, 6); t.best_of = 3;
    memset(&p.res, 0, sizeof p.res); p.sc_seen = 0;
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");              /* 0 - 1 for me: game 1 lost */
    CHECK(gnp_pip(&p, 0) == 2 && gnp_pip(&p, 1) == 3 && gnp_pip(&p, 2) == 0);
    t = t6(3, 1, 1, GNP_RV_PICK_STAGE, 0, 30, 0x3F, 0, G2, 6);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob");              /* I won game 2 */
    CHECK(gnp_pip(&p, 0) == 2 && gnp_pip(&p, 1) == 1 && gnp_pip(&p, 2) == 3);
    /* the clock: normal, yellow from 10 s, red from 3 s, only on the player's own step */
    gnp_clear(&p);
    t = t6(1, 0, 0, GNP_RV_STRIKE, 0, 30, 0x1F, 0, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob"); CHECK(gnp_clock_state(&p) == 0);
    t.secs = 10; gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob"); CHECK(gnp_clock_state(&p) == 1);
    t.secs = 3; gnp_from_room(&p, &t, 0, 1, 1, 0, 0xFF, "Ann", "Bob"); CHECK(gnp_clock_state(&p) == 2);
    t = t6(1, 0, 0, GNP_RV_STRIKE, 1, 3, 0x1F, 0, G1, 5);
    gnp_from_room(&p, &t, 0, 1, 1, 1, 0xFF, "Ann", "Bob"); CHECK(gnp_clock_state(&p) == 0);
    (void)i;
}

int main(void)
{
    slippi_order();
    model();
    grandmaster();
    gamesetup();
    gamesetup_gm();
    lobby();
    printf("gnb prep host test ok: %d failures\n", fails);
    return fails ? 1 : 0;
}
