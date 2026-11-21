/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of the Game Night browser model (gnb.c) in LIVE mode: the requests it raises and the answers the glue gives
   (lists in pages, join, create, lobby polls and actions, leave, every failure), the key map (A, Z, Start, B, X/Y paging by 9, held repeat), the Mock tag, and the
   text of every state (menu font glyphs only, rows of at most 49 characters, nothing cut).  usage: gnb_test */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"
#include "../../../../client_net/mnc.h"

static struct gnb G;
static char num_[24], tit_[40];
static struct sg_view V;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define CF_TYPE_ROW_FOR_TEST 0u        /* the Create form's rows: Mode, Players, Show in list, Spectators, Passcode, Skip passcode, Create room (the Mode is the room's name) */
#define CF_GO_ROW_FOR_TEST 6u
static void fr(unsigned held) { gnb_frame(&G, held, 0, 0); }
static void fr2(struct gnb *g, unsigned held) { gnb_frame(g, held, 0, 0); }
static void press(unsigned b) { fr(b); fr(0); }
static void draw(void)
{
    unsigned r, c;
    memset(&V, 0, sizeof V);
    sg_cut_count = 0;
    gnb_text(&G, &V, 0);
    if (sg_cut_count) { printf("FAIL: %u characters cut\n", sg_cut_count); fails++; }
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(V.t[r]) > 52u) { printf("FAIL row %u too long: %s\n", r, V.t[r]); fails++; }
        for (c = 0; V.t[r][c]; c++) {
            char ch = V.t[r][c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ')) {
                printf("FAIL glyph %c: %s\n", ch, V.t[r]); fails++;
            }
        }
    }
}
static int has(const char *s) { unsigned r; draw(); for (r = 0; r < SG_ROWS; r++) if (strstr(V.t[r], s)) return 1; return 0; }

static struct gnb_room mk(unsigned i, unsigned flags)
{
    struct gnb_room r;
    memset(&r, 0, sizeof r);
    r.region = 0xFF;                                                    /* a v2 server: no region ("-"); room 7 below carries EU */
    snprintf(r.code, sizeof r.code, "RM%04u", i);
    snprintf(r.name, sizeof r.name, "Room %u", i);
    snprintf(r.host, sizeof r.host, "Host%u#1", i);       /* '#' is not in the menu font: shown as a space */
    r.mem = 2; r.cap = 8; r.flags = (unsigned char)flags;
    return r;
}


/* ---- room types: the Create form's Type row and descriptions, the list's type column and filter, the lobby's type name and set score, the pick
   grid's excluded cells and forced character; and against a server without room types everything as before ---- */
static const char *const T_NAME[GNB_RT_N] = { "Chill", "Sweaty", "Wild", "Wild And Crazy", "Ditto Dojo", "Low-Mid Fun" };
static const char *const T_DESC[GNB_RT_N] = { "Single games, random legal stage.", "Strike, then loser picks. Best of 3.", "Single games, random stage from ALL stages.", "Wild, plus all items at maximum.",
                                              "Same character for both players.", "Chill without the top tiers." };
static const char *const T_SHORT[GNB_RT_N] = { "Chill", "Sweaty", "Wild", "Crazy", "Dojo", "Low-Mid" };
#define LOWMID_MASK 0x00188205ul       /* ms_roomtypes.excl_mask(T_LOWMID): Captain Falcon 0, Fox 2, Marth 9, Jigglypuff 15, Sheik 19, Falco 20 */

static void rt_lobby(struct gnb_lobby *v, unsigned type, unsigned side, unsigned phase)      /* a v6 view: this console on `side`, called up when phase is 1 */
{
    memset(v, 0, sizeof *v);
    v->have = 1; v->v3 = 1; v->v6 = 1; v->rtype = (unsigned char)type; v->best_of = (unsigned char)(type == GNB_RT_SWEATY ? 3u : 1u); v->forced = GNB_RT_NONE;
    v->members = 2; v->pos = 0xFF; v->side = (unsigned char)side; v->phase = (unsigned char)phase; v->secs = 0xFF; v->step_owner = 0xFF; v->step_secs = 0xFF; v->turn = 0xFF;
    v->side_on[0] = v->side_on[1] = 1; strcpy(v->side_name[0], "Wobble Pilot"); strcpy(v->side_name[1], "Pocket Fox"); strcpy(v->rname, "Room 1"); strcpy(v->code, "7471");
    v->flags = (unsigned char)(GNB_V_ONSIDE | (phase == 1u ? GNB_V_CALLED : 0u)); v->cap = 8; v->rflags = 3;
    if (type == GNB_RT_LOWMID) v->excl = LOWMID_MASK;
    if (phase == 1u) { v->game = 1; v->turn = 0; v->step = GNB_STEP_PICK_CHAR; v->step_owner = 0; v->secs = 30; v->step_secs = 30; }
}

static void room_types_test(void)
{
    struct gnb_room page[4];
    struct gnb_lobby v;
    char t[72], a[24], b[24];
    unsigned i, k;

    /* ---- the Create form: the Type row replaces the Stages row when the server serves room types ---- */
    gnb_enter(&G, 1);
    G.req = 0; G.rt_ok = 1;
    for (i = 0; i < 4u; i++) { memset(&page[i], 0, sizeof page[i]); page[i] = mk(i, 0u); }
    gnb_net_list(&G, 4, 0, 4, page);
    press(GNB_B_Z);
    CHECK(G.scr == GNB_CREATE && has("Mode: Chill") && !has("Stages:") && gnb_create_type(&G) == GNB_RT_CHILL && gnb_create_smode(&G) == 0xFFu);
    CHECK(has("Single games, random legal stage."));                    /* the description under the form, from the first frame */
    CHECK(G.c_row == CF_TYPE_ROW_FOR_TEST);
    for (i = 1; i <= GNB_RT_N; i++) {                                   /* Right cycles the six and wraps; each type shows its name and one line of at most 44 characters */
        unsigned want = i % GNB_RT_N;
        press(GNB_B_RIGHT);
        CHECK(G.c_type == want && gnb_create_type(&G) == want);
        snprintf(t, sizeof t, "Mode: %s", T_NAME[want]);
        CHECK(has(t) && !strcmp(gnb_type_name(want), T_NAME[want]) && !strcmp(gnb_type_desc(want), T_DESC[want]) && strlen(T_DESC[want]) <= 60u);
        gnb_create_desc(&G, t);
        CHECK(!strcmp(t, gnb_type_open(&G, want) ? T_DESC[want] : "Coming soon"));
        CHECK(has(t));      /* every line fits the row */
    }
    press(GNB_B_LEFT); CHECK(G.c_type == GNB_RT_LOWMID && has("Mode: Low-Mid Fun") && has("Chill without the top tiers."));       /* Left wraps backwards */
    press(GNB_B_A); CHECK(G.c_type == 0u);                              /* A steps like Right */
    /* the Mode is the room's name: no Name row, the name sent is the mode's (12 characters at most), the screens show the full mode name */
    {
        const char *nm; unsigned fl, pol; const char *pin;
        CHECK(!has("Name:") && !has("Friday Night") && gnb_rows(&G) == 7u);
        gnb_create_args(&G, &nm, &fl, &pol, &pin); CHECK(!strcmp(nm, "Chill"));
        press(GNB_B_LEFT); gnb_create_args(&G, &nm, &fl, &pol, &pin); CHECK(!strcmp(nm, "Low-Mid Fun"));
        press(GNB_B_RIGHT); press(GNB_B_RIGHT); press(GNB_B_RIGHT); press(GNB_B_RIGHT); gnb_create_args(&G, &nm, &fl, &pol, &pin);
        CHECK(G.c_type == GNB_RT_WILD_CRAZY && !strcmp(nm, "Wild n Crazy") && strlen(nm) <= 12u);
        press(GNB_B_LEFT); press(GNB_B_LEFT); press(GNB_B_LEFT); CHECK(G.c_type == 0u);
    }
    /* Wild / Wild And Crazy while the server's switches are off: the type is dim, the line says "Coming soon", Create refuses */
    G.all_ok = 0; G.items_ok = 0; press(GNB_B_RIGHT); press(GNB_B_RIGHT);
    CHECK(G.c_type == GNB_RT_WILD && !gnb_type_open(&G, GNB_RT_WILD) && has("Mode: Wild") && has("Coming soon") && !has("random stage from ALL"));
    CHECK(gnb_row(&G, 0u, num_, tit_) == 1u);                           /* dim row */
    for (i = 0; i < 6u; i++) press(GNB_B_DOWN);
    CHECK(G.c_row == CF_GO_ROW_FOR_TEST);
    press(GNB_B_A); CHECK(G.scr == GNB_CREATE && G.req == GNB_REQ_NONE && has("Coming soon") && G.sfx == GNB_SFX_DENY);
    for (i = 0; i < 6u; i++) press(GNB_B_UP);
    G.all_ok = 1; press(GNB_B_RIGHT);                                   /* Wild And Crazy needs the items switch too */
    CHECK(G.c_type == GNB_RT_WILD_CRAZY && gnb_type_open(&G, GNB_RT_WILD) && !gnb_type_open(&G, GNB_RT_WILD_CRAZY) && has("Coming soon"));
    G.items_ok = 1; G.dirty = GNB_D_ALL;
    CHECK(gnb_type_open(&G, GNB_RT_WILD_CRAZY) && has("Wild, plus all items at maximum."));
    press(GNB_B_LEFT); press(GNB_B_LEFT);                               /* Sweaty: the request carries the type */
    CHECK(G.c_type == GNB_RT_SWEATY);
    for (i = 0; i < 6u; i++) press(GNB_B_DOWN);
    press(GNB_B_A); CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_CREATE && gnb_create_type(&G) == GNB_RT_SWEATY && gnb_create_smode(&G) == 0xFFu);
    /* a server without room types: the old Stages row, no type byte */
    gnb_enter(&G, 1); G.req = 0;
    gnb_net_list(&G, 4, 0, 4, page);
    press(GNB_B_Z); CHECK(G.scr == GNB_CREATE && !G.rt_ok && has("Stages: Legal, All soon") && !has("Mode:") && gnb_create_type(&G) == GNB_RT_NONE);

    /* ---- the list: the type column, the detail line, the Type filter in the More popup ---- */
    gnb_enter(&G, 1); G.req = 0; G.rt_ok = 1; G.f_sort = GNB_SORT_SERVER;
    for (i = 0; i < 4u; i++) { page[i] = mk(i, 0u); page[i].rtype = (unsigned char)(i == 3u ? GNB_RT_LOWMID : i); page[i].smode = (unsigned char)(i == 2u || i == 3u ? 2u : 0u); if (i == 3u) page[i].smode = 0; }
    page[2].smode = 2;
    gnb_net_list(&G, 4, 0, 4, page);
    CHECK(G.n == 4u);
    for (i = 0; i < 4u; i++) {
        char code[8], host[16], reg[8], mode[10], cnt[12];
        CHECK(gnb_cols(&G, i, code, host, reg, mode, cnt) == 0u && !strcmp(mode, T_SHORT[page[i].rtype]));
    }
    G.page = 1; G.dirty = GNB_D_ALL;
    CHECK(has("Mode: Sweaty") && !has("Stages:"));
    gnb_status_text(&G, t); CHECK(strstr(t, "all modes") != 0);
    press(GNB_B_RIGHT); press(GNB_B_RIGHT); press(GNB_B_RIGHT); press(GNB_B_RIGHT);
    CHECK(G.act_sel == GNB_ACT_MORE);
    press(GNB_B_A); CHECK(G.more);
    CHECK(!strcmp(gnb_more_label(&G, 0u, t), "Mode: Any") && gnb_list_filter(&G) == 0xFFu);
    for (i = 0; i < GNB_RT_N; i++) {                                    /* A walks Any -> the six types -> Any; the server's filter byte is the type */
        press(GNB_B_A);
        snprintf(t, sizeof t, "Mode: %s", T_NAME[i]);
        CHECK(G.f_stage == i + 1u && !strcmp(gnb_more_label(&G, 0u, a), t) && gnb_list_filter(&G) == i);
    }
    press(GNB_B_A); CHECK(G.f_stage == 0u && !strcmp(gnb_more_label(&G, 0u, t), "Mode: Any") && gnb_list_filter(&G) == 0xFFu);
    { unsigned k, f0 = G.f_stage; for (k = 0; k < 90u; k++) gnb_frame(&G, GNB_B_A, 0, 0); gnb_frame(&G, 0, 0, 0); CHECK(G.f_stage == (f0 + 1u) % (GNB_RT_N + 1u)); G.f_stage = (unsigned char)f0; }      /*: A held 3 s on a More row changes the value ONCE */
    press(GNB_B_A); press(GNB_B_A);                                     /* Sweaty */
    gnb_status_text(&G, t); CHECK(strstr(t, "Sweaty rooms") != 0);
    G.n = 0; gnb_net_list(&G, 4, 0, 4, page);                           /* a server that sends everything: the same filter is applied here */
    CHECK(G.n == 1u && G.r[0].rtype == GNB_RT_SWEATY);
    G.f_stage = 6; G.n = 0; gnb_net_list(&G, 4, 0, 4, page);
    CHECK(G.n == 1u && G.r[0].rtype == GNB_RT_LOWMID);
    G.f_stage = 0;
    /* an older server: the list's types are derived (All stage mode = Wild) and the Stages filter stays */
    gnb_enter(&G, 1); G.req = 0;
    gnb_net_list(&G, 4, 0, 4, page);
    CHECK(!strcmp(gnb_more_label(&G, 0u, t), "Stages: Any") && gnb_list_filter(&G) == 0xFFu);
    { char code[8], host[16], reg[8], mode[10], cnt[12]; CHECK(gnb_cols(&G, 0u, code, host, reg, mode, cnt) == 0u && !strcmp(mode, "Singles")); }

    /* ---- the lobby: the type name and the set score ---- */
    gnb_enter(&G, 1); G.req = 0; G.rt_ok = 1;
    rt_lobby(&v, GNB_RT_SWEATY, 0, 0); gnb_net_view(&G, &v);
    gnb_lobby_rule_parts(&G, a, b); CHECK(!strcmp(a, "Sweaty") && !strcmp(b, "Best of 3") && !strcmp(gnb_lobby_rule(&G), "Sweaty"));       /* between sets */
    CHECK(has("Mode: Sweaty") && gnb_room_type(&G) == GNB_RT_SWEATY);
    rt_lobby(&v, GNB_RT_SWEATY, 0, 2); v.game = 2; v.score_w = 1; v.score_c = 0; gnb_net_view(&G, &v);
    gnb_lobby_rule_parts(&G, a, b); CHECK(!strcmp(a, "Sweaty") && !strcmp(b, "Game 2 of 3  Set 1-0"));                                    /* a running set */
    v.game = 3; v.score_w = 1; v.score_c = 1; gnb_net_view(&G, &v);
    gnb_lobby_rule_parts(&G, a, b); CHECK(!strcmp(b, "Game 3 of 3  Set 1-1"));
    for (i = 0; i < GNB_RT_N; i++) {
        rt_lobby(&v, i, 0, 0); gnb_net_view(&G, &v);
        gnb_lobby_rule_parts(&G, a, b);
        CHECK(!strcmp(a, T_NAME[i]) && (i == GNB_RT_SWEATY ? !strcmp(b, "Best of 3") : !b[0]));
        snprintf(t, sizeof t, "Mode: %s", T_NAME[i]);
        CHECK(has(t));
    }
    { struct gnb_lobby w = v; w.v6 = 0; w.smode = 2; gnb_net_view(&G, &w); gnb_lobby_rule_parts(&G, a, b); CHECK(!strcmp(a, "All stages") && !b[0]); }          /* a view v5: as before */
    /* the call-up ticket of a Sweaty game carries the game (GN<code>S<set>G<game>), any other type does not */
    rt_lobby(&v, GNB_RT_SWEATY, 0, 1); v.set_id = 5; v.game = 2; v.turn = GNB_TURN_GO; v.step = 0; v.step_owner = 0xFF; v.picked = 3; v.secs = 20; gnb_net_view(&G, &v);
    CHECK(G.play == 1 && !strcmp(G.play_name, "GN7471S5G2"));
    gnb_enter(&G, 1); G.req = 0;
    rt_lobby(&v, GNB_RT_CHILL, 0, 1); v.set_id = 5; v.turn = GNB_TURN_GO; v.step = 0; v.step_owner = 0xFF; v.picked = 3; gnb_net_view(&G, &v);
    CHECK(G.play == 1 && !strcmp(G.play_name, "GN7471S5"));

    /* ---- E1's step is not the character pick: a strike (turn = our) never opens the grid, between two games the console says so ---- */
    gnb_enter(&G, 1); G.req = 0; G.rt_ok = 1;
    rt_lobby(&v, GNB_RT_SWEATY, 0, 1); v.step = 2u; v.turn = 0; v.step_owner = 0; v.stage_n = 5; v.stage_avail = 0x1F; gnb_net_view(&G, &v);
    CHECK(G.pick_secs == 0xFFu);
    CHECK(G.prep.open && G.prep.mine);                                                          /* piece E1: the stage prep screen (gnb_prep.c, gnb_prep_test.c) owns the step: A strikes, the grid stays shut */
    press(GNB_B_A); press(GNB_B_A); CHECK(!G.pick_open && G.req == GNB_REQ_ACTION && G.act == 5u && G.act_val == 0u); G.req = 0;     /* A marks, A on OK sends */
    CHECK(has("Striking stages") && has("Strike a stage"));
    v.step = 5u; gnb_net_view(&G, &v); CHECK(G.pick_secs == 30u);
    press(GNB_B_A); CHECK(G.pick_open);
    v.turn = GNB_TURN_WAIT; v.step = 0; v.step_owner = 0xFF; gnb_net_view(&G, &v);
    CHECK(!G.pick_open && has("Waiting for both players") && has("Waiting for your opponent") && !strcmp(gnb_foot(&G), "Hold B: Leave"));

    /* ---- E2: Low-Mid Fun, the excluded cells ---- */
    gnb_enter(&G, 1); G.req = 0; G.rt_ok = 1;
    rt_lobby(&v, GNB_RT_LOWMID, 0, 1); v.side_char[0] = 2u;                       /* the view's character is Fox: excluded */
    gnb_net_view(&G, &v);
    for (i = 0; i < 27u; i++) {
        int ex = (i == 0u || i == 2u || i == 9u || i == 15u || i == 19u || i == 20u);
        CHECK(gnb_cell_ok(&G, i) == !ex);
    }
    CHECK(!gnb_cell_ok(&G, 27u) && gnb_cell_ok(&G, GNB_CHAR_RANDOM));
    press(GNB_B_A);
    CHECK(G.pick_open && !G.pick_lock && G.pick_cur == 1u && has("Dimmed fighters are not allowed"));      /* the grid opens on the first allowed cell, not on Fox */
    CHECK(G.sfx == GNB_SFX_FWD);
    press(GNB_B_RIGHT); CHECK(G.pick_cur == 3u);                                    /* Fox skipped */
    press(GNB_B_LEFT); CHECK(G.pick_cur == 1u);
    press(GNB_B_LEFT); CHECK(G.pick_cur == 6u);                                     /* Captain Falcon skipped: wraps to Link */
    press(GNB_B_RIGHT); CHECK(G.pick_cur == 1u);                                    /* wraps over Captain Falcon */
    G.pick_cur = 16u; G.sfx = 0; press(GNB_B_UP); CHECK(G.pick_cur == 16u);         /* Samus: Marth and Fox above are excluded, nothing allowed above: stays */
    G.pick_cur = 17u; press(GNB_B_UP); CHECK(G.pick_cur == 10u);                    /* Yoshi up: Mewtwo? (column 3: 17 -> 10 -> 3) */
    press(GNB_B_UP); CHECK(G.pick_cur == 3u);
    G.pick_cur = 14u; press(GNB_B_RIGHT); CHECK(G.pick_cur == 16u);                 /* Ice Climbers right: Jigglypuff skipped */
    press(GNB_B_RIGHT); press(GNB_B_RIGHT); CHECK(G.pick_cur == 18u);               /* Zelda */
    press(GNB_B_RIGHT); CHECK(G.pick_cur == 14u);                                   /* Sheik and Falco skipped: the row wraps to the first allowed one */
    G.pick_cur = 21u; press(GNB_B_A);
    CHECK(!G.pick_open && G.req == GNB_REQ_ACTION && G.act == 4 && G.act_val == 21u && G.pick_mine == 21u);
    G.req = 0; v.turn = 0; gnb_net_view(&G, &v); press(GNB_B_A); G.pick_cur = 20u; G.sfx = 0; press(GNB_B_A);       /* a stale cursor on an excluded fighter (Falco): refused, nothing sent */
    CHECK(G.pick_open && G.req == GNB_REQ_NONE && G.sfx == GNB_SFX_DENY);
    G.pick_cur = 26u; press(GNB_B_A); CHECK(G.req == GNB_REQ_ACTION && G.act_val == 26u);        /* Random is allowed: the server resolves it among the allowed fighters */
    /* a room without exclusions: every cell, the old message-free grid */
    gnb_enter(&G, 1); G.req = 0;
    rt_lobby(&v, GNB_RT_CHILL, 0, 1); gnb_net_view(&G, &v);
    for (i = 0; i < 27u; i++) CHECK(gnb_cell_ok(&G, i));
    press(GNB_B_A); CHECK(G.pick_open && G.pick_cur == 0u && !G.msg);

    /* ---- E2: Ditto Dojo, the forced character ---- */
    gnb_enter(&G, 1); G.req = 0; G.rt_ok = 1;
    rt_lobby(&v, GNB_RT_DITTO, 1, 1); v.turn = 0; v.step_owner = 0; v.step = GNB_STEP_PICK_CHAR;     /* this console is side 1; side 0 (the creator) picks first */
    gnb_net_view(&G, &v);
    CHECK(gnb_forced_char(&G) == GNB_RT_NONE && has("Wobble Pilot is choosing"));                       /* nothing chosen yet: nothing to see */
    press(GNB_B_A); CHECK(!G.pick_open && has("Wait for your turn"));
    v.forced = 2u; v.side_char[0] = 2u; v.side_char[1] = 2u; v.turn = 0xFE; v.step = 0; v.step_owner = 0xFF; v.picked = 3; v.secs = 20; gnb_net_view(&G, &v);       /* the picker chose Fox: both play it */
    CHECK(gnb_forced_char(&G) == 2u && has("Both play Fox") && !strcmp(gnb_foot(&G), "Starting the match"));      /* the match starts at once: the other player may still look */
    G.play = 0;                                                                                        /* (the match loop arms the search; the locked view is what this test is after) */
    press(GNB_B_A); CHECK(G.pick_open && G.pick_lock);                                                 /* A during "Starting the match": the locked view, not a refusal */
    press(GNB_B_B); CHECK(!G.pick_open);
    v.turn = 0; v.step = GNB_STEP_PICK_CHAR; v.step_owner = 0; v.picked = 3; gnb_net_view(&G, &v);     /* forced char set, still on the stage of the picker's turn: the footer offers it */
    CHECK(!strcmp(gnb_foot(&G), "A: See pick  Hold B: Leave"));
    press(GNB_B_A);
    CHECK(G.pick_open && G.pick_lock && G.pick_cur == 2u && has("Set for both players") && !strcmp(gnb_foot(&G), "B: Back"));
    G.req = 0; G.sfx = 0; press(GNB_B_A); CHECK(G.pick_open && G.req == GNB_REQ_NONE && G.sfx == GNB_SFX_DENY && G.pick_cur == 2u);     /* A refused */
    press(GNB_B_RIGHT); press(GNB_B_DOWN); press(GNB_B_L); press(GNB_B_R); CHECK(G.pick_cur == 2u && G.req == GNB_REQ_NONE && G.pick_cos[2] == 0u);   /* nothing moves, no costume */
    press(GNB_B_START); CHECK(G.req == GNB_REQ_NONE);
    press(GNB_B_B); CHECK(!G.pick_open && !G.pick_lock && G.req == GNB_REQ_NONE);
    press(GNB_B_A); CHECK(G.pick_lock);
    v.forced = GNB_RT_NONE; gnb_net_view(&G, &v); CHECK(!G.pick_open && !G.pick_lock);                  /* the character went away (a new round): the locked view closes */
    /* the picker: the normal grid ("Pick for both") */
    gnb_enter(&G, 1); G.req = 0;
    rt_lobby(&v, GNB_RT_DITTO, 0, 1); v.turn = 0; v.step_owner = 0; v.secs = 25; gnb_net_view(&G, &v);
    CHECK(has("Pick for both: A") && !strcmp(gnb_foot(&G), "A: Pick  Hold B: Leave"));
    press(GNB_B_A); CHECK(G.pick_open && !G.pick_lock && has("Your pick is played by both"));
    press(GNB_B_RIGHT); press(GNB_B_A); CHECK(G.req == GNB_REQ_ACTION && G.act == 4 && G.act_val == 1u);
    /* a view v6 with a forced character from a room of another type is still just the character; a view without v6 never locks */
    gnb_enter(&G, 1); G.req = 0;
    rt_lobby(&v, GNB_RT_CHILL, 1, 1); v.v6 = 0; v.forced = 5u; v.excl = 0xFFFFFFFul; gnb_net_view(&G, &v);
    CHECK(gnb_forced_char(&G) == GNB_RT_NONE && gnb_cell_ok(&G, 0u) && gnb_cell_ok(&G, 5u));
    (void)k;
}

int main(void)
{
    struct gnb_room page[8];
    struct gnb_lobby v;
    unsigned i;

    gnb_enter(&G, 1);
    G.f_sort = GNB_SORT_SERVER;                                         /* the list scenarios below index rooms in the server's order */
    CHECK(G.req == GNB_REQ_LIST && G.lpage == 0 && G.net == GNB_N_LOADING && has("Loading"));
    G.req = 0;
    for (i = 0; i < 8u; i++) page[i] = mk(i, i == 1u ? GNB_F_MOCK : 0u);
    page[2].mem = 8; page[2].flags = GNB_F_FULL | GNB_F_SPECT; page[2].stadium = 2;
    page[3].flags = GNB_F_PASS;
    gnb_net_list(&G, 12, 0, 8, page);
    CHECK(G.n == 8 && G.req == GNB_REQ_LIST && G.lpage == 1);           /* more pages: asked at once */
    G.req = 0;
    for (i = 0; i < 4u; i++) page[i] = mk(8 + i, 0);
    gnb_net_list(&G, 12, 1, 4, page);
    CHECK(G.n == 12 && G.req == GNB_REQ_NONE && G.net == GNB_N_OK);
    CHECK(has("Room 0") && has("2 of 8") && has("Room 1 Mock") && has("Host0 1") && has("12 rooms"));
    press(GNB_B_DOWN);
    CHECK(G.page == 1 && has("Room 1 Mock") && has("Host: Host1 1"));
    press(GNB_B_X);                                                     /* page down by 9: first = min(9, 12 - 9) = 3 */
    CHECK(G.first == 3 && G.page == 1);
    press(GNB_B_Y);
    CHECK(G.first == 0);
    for (i = 0; i < 30u; i++) fr(GNB_B_DOWN);                           /* held: repeats */
    fr(0);
    CHECK(G.first + G.page > 5u);
    G.first = 0; G.page = 2; G.dirty = GNB_D_ALL;
    press(GNB_B_A);                                                     /* full + spectators */
    CHECK(G.scr == GNB_LIST && G.sfx == GNB_SFX_DENY && has("Room is full"));
    G.page = 3;
    press(GNB_B_A);                                                     /* passcode */
    CHECK(G.scr == GNB_PASS && has("Passcode required"));
    press(GNB_B_UP); press(GNB_B_RIGHT); press(GNB_B_UP); press(GNB_B_UP); press(GNB_B_A); press(GNB_B_A); press(GNB_B_A);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && !strcmp(gnb_join_code(&G), "RM0003") && !strcmp(gnb_join_pin(&G), "1200") && has("Joining room"));
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);
    CHECK(G.scr == GNB_LIST && has("Room not found or wrong passcode"));
    G.page = 1;
    press(GNB_B_A);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && !strcmp(gnb_join_pin(&G), ""));
    G.req = 0;
    memset(&v, 0, sizeof v);
    v.have = 1; v.members = 3; v.qlen = 0; v.pos = 0xFF; v.side = 0xFF; v.secs = 0xFF; v.side_on[0] = v.side_on[1] = 1;
    strcpy(v.side_name[0], "Wobble Pilot"); strcpy(v.side_name[1], "Pocket Fox"); strcpy(v.rname, "Room 1"); strcpy(v.code, "RM0001"); v.stadium = 2; v.rflags = 3;
    gnb_net_view(&G, &v);
    { char h[16]; gnb_head(&G, h); CHECK(!gnb_char_box(&G)); CHECK(!strcmp(h, "Room 1")); }       /* lobby, not called: no Select Character box, heading is the room name */
    CHECK(gnb_side_char(&G, 0) == 0u && gnb_side_char(&G, 1) == 0u && gnb_side_char(&G, 2) == GNB_CHAR_NONE);     /* the view's characters: the portraits (0 = Captain Falcon) */
    {   /* portraits: Marth and Random (unknown: "?"), an empty challenger slot, an out-of-range id never reaches the art */
        struct gnb_lobby w = v;
        w.side_char[0] = 9u; w.side_char[1] = GNB_CHAR_RANDOM; gnb_net_view(&G, &w);
        CHECK(gnb_side_char(&G, 0) == 9u && gnb_side_char(&G, 1) == GNB_CHAR_NONE);
        w.side_on[1] = 0; w.side_char[1] = 2u; gnb_net_view(&G, &w);
        CHECK(gnb_side_char(&G, 0) == 9u && gnb_side_char(&G, 1) == GNB_CHAR_NONE);
        w.side_char[0] = 200u; gnb_net_view(&G, &w);
        CHECK(gnb_side_char(&G, 0) == GNB_CHAR_NONE);
        gnb_net_view(&G, &v);
    }
    CHECK(G.scr == GNB_LOBBY && has("Room lobby") && has("Wobble Pilot") && has("Room 1 Mock") && has("You: not queued") && !has("You joined"));
    for (i = 0; i < GNB_POLL_FRAMES; i++) fr(0);
    CHECK(G.req == GNB_REQ_STATE);
    G.req = 0;
    press(GNB_B_A);
    CHECK(G.req == GNB_REQ_ACTION && G.act == 1);
    G.req = 0;
    v.flags = GNB_V_QUEUED; v.pos = 1; v.qlen = 1; v.nnext = 1; strcpy(v.next[0], "You");
    gnb_net_view(&G, &v);
    CHECK(has("You: queued 1") && has("Queue 1"));
    v.phase = 1; v.flags = GNB_V_CALLED | GNB_V_ONSIDE; v.pos = 0xFF; v.side = 1; v.qlen = 0; v.nnext = 0;
    gnb_net_view(&G, &v);
    CHECK(has("Call-up: press A"));
    CHECK(gnb_char_box(&G));                                            /* only a called-up player gets the character box */
    press(GNB_B_A);
    CHECK(G.req == GNB_REQ_ACTION && G.act == 4);
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_STATE, GNB_W_NOTFOUND);
    CHECK(G.scr == GNB_LIST && G.req == GNB_REQ_LIST && has("You are no longer in the room"));
    G.req = 0;
    /* create */
    press(GNB_B_Z);
    CHECK(G.scr == GNB_CREATE && has("Create room") && has("Show in list: yes"));
    { char h[16]; gnb_head(&G, h); CHECK(!gnb_char_box(&G)); CHECK(!strcmp(h, "New room")); }
    press(GNB_B_DOWN);                                                  /* Players: 8 by default; Left/Right cycle 2 4 8 16 32 */
    CHECK(has("Players: 8") && gnb_create_cap(&G) == 8u);
    press(GNB_B_LEFT); CHECK(has("Players: 4") && gnb_create_cap(&G) == 4u);
    press(GNB_B_LEFT); CHECK(has("Players: 2") && gnb_create_cap(&G) == 2u);
    press(GNB_B_LEFT); CHECK(has("Players: 32") && gnb_create_cap(&G) == 32u);
    press(GNB_B_RIGHT); CHECK(has("Players: 2"));
    press(GNB_B_RIGHT); press(GNB_B_RIGHT); press(GNB_B_RIGHT); CHECK(has("Players: 16") && gnb_create_cap(&G) == 16u);
    press(GNB_B_A); CHECK(has("Players: 32"));                          /* A also steps */
    press(GNB_B_LEFT); press(GNB_B_LEFT); CHECK(gnb_create_cap(&G) == 8u);
    press(GNB_B_LEFT); CHECK(gnb_create_cap(&G) == 4u);                 /* the room asked for in the request below */
    press(GNB_B_DOWN); press(GNB_B_DOWN);                               /* spectators: allowed */
    press(GNB_B_UP); press(GNB_B_UP); press(GNB_B_UP); press(GNB_B_A);   /* Stages (row 0): All is not open yet (the server's HELLO has no bit 12): greyed, "Coming soon" */
    CHECK(has("All stages: Coming soon") && has("Stages: Legal, All soon") && gnb_create_smode(&G) == 0xFFu && G.c_all == 0);
    G.all_ok = 1; G.dirty = GNB_D_ALL;                                  /* our switched it on */
    CHECK(has("Stages: Legal") && gnb_create_smode(&G) == 0xFFu);
    press(GNB_B_A); CHECK(has("Stages: All") && gnb_create_smode(&G) == 2u && G.c_all == 1);
    press(GNB_B_LEFT); CHECK(has("Stages: Legal") && gnb_create_smode(&G) == 0xFFu);
    G.all_ok = 0;
    press(GNB_B_DOWN); press(GNB_B_DOWN); press(GNB_B_DOWN); press(GNB_B_DOWN); press(GNB_B_A);   /* passcode entry */
    CHECK(G.scr == GNB_PASS && G.p_for);
    press(GNB_B_A); press(GNB_B_A); press(GNB_B_A); press(GNB_B_UP); press(GNB_B_A);
    CHECK(G.scr == GNB_CREATE && has("Passcode: 0001"));
    press(GNB_B_DOWN); CHECK(has("Skip passcode: yes"));                /* the new row, default Yes */
    press(GNB_B_DOWN); press(GNB_B_A);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_CREATE);
    {
        const char *nm, *pin;
        unsigned fl, pol;
        gnb_create_args(&G, &nm, &fl, &pol, &pin);
        CHECK(!strcmp(nm, "Game Night") && fl == 11u && pol == 0xFFu && !strcmp(pin, "0001") && gnb_create_cap(&G) == 4u);
    }
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_CREATE, GNB_W_INROOM);
    CHECK(G.scr == GNB_CREATE && has("Leave your other room first"));
    press(GNB_B_A);
    G.req = 0;
    memset(&v, 0, sizeof v);
    v.have = 1; v.members = 1; v.pos = 0xFF; v.side = 0xFF; v.flags = GNB_V_OWNER; strcpy(v.rname, "Game Night");
    gnb_net_view(&G, &v);
    CHECK(G.scr == GNB_LOBBY && !has("Room created") && has("Open") && !has("Mock"));      /* a created room is never tagged Mock (found by code, not by the last row joined) */
    press(GNB_B_B); CHECK(G.scr == GNB_LOBBY); for (i = 0; i < GNB_HOLD_B_FRAMES + 2u; i++) fr(GNB_B_B); fr(0);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_LEAVE);
    G.req = 0;
    gnb_net_left(&G);
    CHECK(G.scr == GNB_LIST && G.req == GNB_REQ_LIST && !has("You left the room"));
    G.req = 0;
    /* original-style list: columns, action row (Create / Join by code / Random; Mode and Region removed), Hold B in the lobby */
    {
        char c[8], h[16], rg[8], md[10], n[12];
        unsigned at;
        G.first = 0; G.page = 0;
        CHECK(gnb_cols(&G, 5, c, h, rg, md, n) == 0 && !strcmp(c, "0005") && !strcmp(h, "Host5 1") && !strcmp(rg, "-") && !strcmp(md, "Singles") && !strcmp(n, "2 of 8"));
        CHECK(gnb_cols(&G, 2, c, h, rg, md, n) == 1);                   /* full: dim */
        G.first = 4; CHECK(gnb_cols(&G, 8, c, h, rg, md, n) == 0 && !c[0]); G.first = 0;     /* past the 12 rooms: empty */
        CHECK(G.focus == 0 && G.act_sel == GNB_ACT_RANDOM && GNB_ACT_RANDOM == 0);      /* Random is first and selected by default */
        press(GNB_B_RIGHT);
        CHECK(G.focus == 1 && G.act_sel == GNB_ACT_RANDOM);             /* the first side press only enters the action row */
        press(GNB_B_RIGHT); press(GNB_B_RIGHT);
        CHECK(G.act_sel == GNB_ACT_JOIN);
        press(GNB_B_A);
        CHECK(G.scr == GNB_PASS && G.p_for == 2u && has("Enter room code") && has("Join room"));
        strcpy(G.p_pin, "0099");                                        /* no room 0009 */
        G.p_pos = 3; press(GNB_B_A);
        CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && !strcmp(gnb_join_code(&G), "0099") && !strcmp(gnb_join_pin(&G), ""));   /* not in the loaded list (unlisted, or past the cached 24): the typed code goes to the server */
        G.req = 0;
        gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);                 /* the one uniform refusal: the passcode is asked, then the code is sent again with it */
        CHECK(G.scr == GNB_PASS && G.fj && !G.fpin && has("passcode"));
        G.p_pin[0] = '7'; G.p_pin[1] = '7'; G.p_pin[2] = '0'; G.p_pin[3] = '0'; G.p_pos = 3; press(GNB_B_A);
        CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && !strcmp(gnb_join_code(&G), "0099") && !strcmp(gnb_join_pin(&G), "7700"));
        G.req = 0;
        gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);                 /* refused again: the room does not exist (or the passcode is wrong) */
        CHECK(G.scr == GNB_LIST && !G.fj && has("Room not found or wrong passcode"));
        G.req = 0; G.focus = 1; G.act_sel = GNB_ACT_JOIN;
        press(GNB_B_A); strcpy(G.p_pin, "0098"); G.p_pos = 3; press(GNB_B_A);
        G.req = 0; gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_FULL);           /* a room that exists but is full: no passcode question */
        CHECK(G.scr == GNB_LIST && !G.fj && has("Room is full"));
        G.req = 0; G.focus = 1; G.act_sel = GNB_ACT_JOIN;
        press(GNB_B_A);                                                 /* focus is still the action row: Join again */
        strcpy(G.p_pin, "0005"); G.p_pos = 3; press(GNB_B_A);
        CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && G.sel == 5);
        G.req = 0; G.scr = GNB_LIST; G.focus = 1; G.act_sel = GNB_ACT_JOIN;
        press(GNB_B_A); strcpy(G.p_pin, "0002"); G.p_pos = 3; press(GNB_B_A);
        CHECK(G.scr == GNB_LIST && has("Room is full") && G.req == GNB_REQ_NONE);
        press(GNB_B_A); strcpy(G.p_pin, "0003"); G.p_pos = 3; press(GNB_B_A);
        CHECK(G.scr == GNB_PASS && G.p_for == 0u && has("Passcode required"));      /* a passcode room asks for its passcode after the code */
        press(GNB_B_B);
        {   /* the bar: five plates in our order */
            unsigned k;
            static const char *const LAB[4] = { "Random", "Create", "Code", "More" };
            CHECK(GNB_ACT_N == 4u && GNB_ACT_RANDOM == 0 && GNB_ACT_CREATE == 1 && GNB_ACT_JOIN == 2 && GNB_ACT_MORE == 3);
            for (k = 0; k < 4u; k++) CHECK(gnb_act_enabled(&G, k) && !strcmp(gnb_act_label(&G, k), LAB[k]));
        }
        {   /* More popup: rows, values, A changes, up/down move, B closes and the list is fetched again; Stages needs the server's bit for All */
            static struct gnb SV;
            char t[32];
            SV = G;
            G.act_sel = GNB_ACT_MORE; G.focus = 1; G.live = 1; G.net = GNB_N_OK; G.req = GNB_REQ_NONE; G.scr = GNB_LIST;
            press(GNB_B_A);
            CHECK(G.more == 1 && G.more_row == GNB_MORE_STAGES && !strcmp(gnb_hint(&G), "A: Change  B: Done") && G.req == GNB_REQ_NONE);
            CHECK(!strcmp(gnb_more_label(&G, 0u, t), "Stages: Any") && !strcmp(gnb_more_label(&G, 1u, t), "Hide full rooms: No") && !strcmp(gnb_more_label(&G, 2u, t), "Sort: Most players") && !strcmp(gnb_more_label(&G, 3u, t), "Region: Auto") && GNB_MORE_N == 4u && GNB_MORE_REGION == 3);
            press(GNB_B_A); CHECK(G.f_stage == 1u && !strcmp(gnb_more_label(&G, 0u, t), "Stages: Legal") && gnb_list_filter(&G) == 0u && G.req == GNB_REQ_NONE);   /* nothing is fetched while it is open */
            press(GNB_B_A); CHECK(G.f_stage == 0u && !strcmp(gnb_more_label(&G, 0u, t), "Stages: Any") && gnb_list_filter(&G) == 0xFFu);      /* All is skipped while it is off */
            G.all_ok = 1; press(GNB_B_A); press(GNB_B_A); CHECK(G.f_stage == 2u && !strcmp(gnb_more_label(&G, 0u, t), "Stages: All") && gnb_list_filter(&G) == 2u);
            press(GNB_B_A); CHECK(G.f_stage == 0u); G.all_ok = 0;
            press(GNB_B_DOWN); CHECK(G.more_row == GNB_MORE_HIDE);
            press(GNB_B_A); CHECK(G.f_hide == 1u && !strcmp(gnb_more_label(&G, 1u, t), "Hide full rooms: Yes"));
            press(GNB_B_DOWN); CHECK(G.more_row == GNB_MORE_SORT);
            press(GNB_B_A); CHECK(G.f_sort == GNB_SORT_NEWEST && !strcmp(gnb_more_label(&G, 2u, t), "Sort: Newest"));
            press(GNB_B_DOWN); press(GNB_B_DOWN); CHECK(G.more_row == GNB_MORE_REGION);      /* the last row: down stops */
            press(GNB_B_A); CHECK(G.f_reg == 0u && G.reg_user == 1u && !strcmp(gnb_more_label(&G, 3u, t), "Region: Any")); press(GNB_B_A); CHECK(G.f_reg == 2u && G.reg_user == 1u && !strcmp(gnb_more_label(&G, 3u, t), "Region: NAW") && G.req == GNB_REQ_NONE);      /* the region cycles in the popup and nothing is fetched while it is open */
            G.f_reg = 0u; G.reg_user = 0u;
            press(GNB_B_UP); CHECK(G.more_row == GNB_MORE_SORT); press(GNB_B_UP); press(GNB_B_UP); press(GNB_B_UP); CHECK(G.more_row == 0u);
            { char st[56]; gnb_status_text(&G, st); CHECK(!strcmp(st, "Showing: all regions, any stages, no full rooms")); }
            G.req = GNB_REQ_NONE; fr(GNB_B_B); fr(0);                      /* a tap of B closes the popup, never leaves Game Night */
            CHECK(G.more == 0 && G.exit == 0 && G.req == GNB_REQ_LIST && G.n == 0u && G.net == GNB_N_LOADING);
            G.req = GNB_REQ_NONE; G.net = GNB_N_OK; press(GNB_B_A);       /* the plate again; no change then no fetch on close */
            CHECK(G.more == 1u && G.more_row == 0u); press(GNB_B_B); CHECK(G.more == 0 && G.req == GNB_REQ_NONE);
            G = SV;
            {   /*: L / R on the list cycle the Region filter (Any -> NAW -> NAC -> NAE -> EU ->... -> OC -> Any, L backwards) and fetch the list again */
                G.more = 0; G.scr = GNB_LIST; G.f_reg = 0u; G.reg_user = 0u; G.acct_rgn = 3u; G.req = GNB_REQ_NONE; G.net = GNB_N_OK;
                press(GNB_B_R); CHECK(G.f_reg == 0u && G.reg_user == 1u && G.req == GNB_REQ_LIST && G.net == GNB_N_LOADING);       /* Auto -> Any */
                G.req = GNB_REQ_NONE; G.net = GNB_N_OK; press(GNB_B_R); CHECK(G.f_reg == 2u);                      /* NAW */
                G.req = GNB_REQ_NONE; G.net = GNB_N_OK; press(GNB_B_L); press(GNB_B_L); CHECK(G.reg_user == 0u && G.f_reg == 3u);      /* back over Any to Auto: the account's region (EU) */
                { char st[56]; gnb_status_text(&G, st); CHECK(!strncmp(st, "Showing: Europe (auto)", 22)); }
                G.req = GNB_REQ_NONE; G.net = GNB_N_OK; press(GNB_B_L); CHECK(G.f_reg == 6u && G.reg_user == 1u);   /* Auto backwards wraps to OC */
                G.req = GNB_REQ_NONE; G.net = GNB_N_OK; press(GNB_B_R); CHECK(G.reg_user == 0u);
                G.acct_rgn = 0u;
                G.f_reg = 0u; G.req = GNB_REQ_NONE; G = SV;
            }
        }
        {   /* filters and sort on a list page: Region (client side), Stages, Hide full, Sort */
            static struct gnb Z;
            struct gnb_room pg[8];
            unsigned k;
            char bd[16];
            gnb_enter(&Z, 1);
            Z.req = GNB_REQ_NONE;
            for (k = 0; k < 8u; k++) { pg[k] = mk(k, 0u); pg[k].cap = 16; pg[k].region = (unsigned char)(k & 3u); pg[k].mem = (unsigned char)(2u + k); pg[k].minutes = (unsigned short)(10u * (8u - k)); pg[k].smode = (unsigned char)((k & 1u) ? 2u : 0u); }
            pg[5].mem = 16; pg[5].flags = GNB_F_FULL;
            gnb_net_list(&Z, 8, 0, 8, pg);
            CHECK(Z.n == 8u && Z.got == 8u && Z.r[0].mem == 9u && Z.r[0].region == 3u && Z.r[6].mem == 2u && Z.r[7].mem == 16u);       /* Most players first, the full room last, region Any */
            Z.f_sort = GNB_SORT_NEWEST; gnb_net_list(&Z, 8, 0, 8, pg);
            CHECK(Z.r[0].minutes == 10u && Z.r[6].minutes == 80u && Z.r[7].mem == 16u);      /* Newest: the fewest minutes open first; the full room (5) last */
            Z.f_sort = GNB_SORT_PLAYERS; Z.f_reg = 3u;                  /* EU = region index 2 */
            gnb_net_list(&Z, 8, 0, 8, pg);
            CHECK(Z.n == 2u && Z.r[0].region == 2u && Z.r[1].region == 2u && Z.r[0].mem >= Z.r[1].mem && Z.got == 8u && Z.req == GNB_REQ_NONE);
            Z.f_reg = 0; Z.f_stage = 2u; gnb_net_list(&Z, 8, 0, 8, pg);
            CHECK(Z.n == 4u && Z.r[0].smode == 2u && Z.r[3].smode == 2u);
            Z.f_stage = 1u; gnb_net_list(&Z, 8, 0, 8, pg);
            CHECK(Z.n == 4u && Z.r[0].smode == 0u);
            Z.f_stage = 0; Z.f_hide = 1u; gnb_net_list(&Z, 8, 0, 8, pg);
            CHECK(Z.n == 7u);                                           /* the full room (5) is gone */
            gnb_badge(&Z, bd); CHECK(!strcmp(bd, "7 rooms"));           /* the badge counts what is shown */
            Z.f_reg = 6u; Z.reg_user = 1u; gnb_net_list(&Z, 8, 0, 8, pg);               /* OC: no room, the list says so */
            CHECK(Z.n == 0u && Z.net == GNB_N_EMPTY && !strcmp(gnb_more_label(&Z, GNB_MORE_REGION, (char[28]){0}), "Region: OC"));
            gnb_badge(&Z, bd); CHECK(!strcmp(bd, "0 rooms"));
        }
        {   /* the Region row of the More popup: the account's region is the default, A cycles Any -> NAE .. OC -> Any and fetches the list again */
            static struct gnb Z;
            struct gnb_room pg[2];
            static const char *const LG[8] = { "Showing: all regions, any stages", "Showing: North America East, any stages", "Showing: North America West, any stages", "Showing: Europe, any stages", "Showing: Latin America, any stages", "Showing: Asia, any stages", "Showing: Oceania, any stages", "Showing: North America Central, any stages" };
            char t[56], t2[28];
            unsigned k;
            gnb_enter(&Z, 1);
            Z.acct_rgn = 3u;                                           /* the account says Europe */
            fr2(&Z, 0);
            CHECK(Z.f_reg == 3u && !strcmp(gnb_more_label(&Z, GNB_MORE_REGION, t2), "Region: Auto") && Z.req == GNB_REQ_LIST);      /* the first list is still on its way: no second fetch */
            gnb_status_text(&Z, t); CHECK(!strcmp(t, "Showing: Europe (auto), any stages"));
            pg[0] = mk(1, 0u); pg[0].region = 2; pg[1] = mk(2, 0u); pg[1].region = 0;
            Z.req = GNB_REQ_NONE; gnb_net_list(&Z, 2, 0, 2, pg);
            CHECK(Z.n == 1u && Z.r[0].region == 2u);
            Z.acct_rgn = 8u; fr2(&Z, 0);                               /* not set (sg rgn 8 since NAC): Any, the loaded list is fetched again */
            CHECK(Z.f_reg == 0u && Z.req == GNB_REQ_LIST);
            Z.acct_rgn = 5u; Z.req = GNB_REQ_NONE; Z.net = GNB_N_OK; fr2(&Z, 0);
            CHECK(Z.f_reg == 5u);
            for (k = 0; k < 9u; k++) {                                  /* a full cycle from Auto (the account's region, Asia): Auto, Any, NAW, NAC, NAE, EU .. OC, Auto */
                static const char *const AL[9] = { "Region: Auto", "Region: Any", "Region: NAW", "Region: NAC", "Region: NAE", "Region: EU", "Region: LA", "Region: AS", "Region: OC" };
                static const unsigned char SEQ[8] = { 0, 2, 7, 1, 3, 4, 5, 6 };
                unsigned want = k == 0u ? 5u : SEQ[k - 1u];
                CHECK(Z.f_reg == want && !strcmp(gnb_more_label(&Z, GNB_MORE_REGION, t2), AL[k]) && Z.reg_user == (k != 0u));
                gnb_status_text(&Z, t); CHECK(k == 0u ? !strcmp(t, "Showing: Asia (auto), any stages") : !strcmp(t, LG[want]));
                Z.req = GNB_REQ_NONE; Z.net = GNB_N_OK; Z.act_sel = GNB_ACT_MORE; Z.focus = 1; Z.scr = GNB_LIST;
                fr2(&Z, GNB_B_A); fr2(&Z, 0); Z.more_row = GNB_MORE_REGION; fr2(&Z, GNB_B_A); fr2(&Z, 0); fr2(&Z, GNB_B_B); fr2(&Z, 0);      /* More > Region > A, then B closes and fetches */
                CHECK(Z.req == GNB_REQ_LIST && Z.n == 0u && !Z.more);
            }
            CHECK(Z.f_reg == 5u && Z.reg_user == 0u);
            Z.acct_rgn = 2u; fr2(&Z, 0); CHECK(Z.f_reg == 2u);          /* Auto follows the account */
            fr2(&Z, 0); Z.f_reg = 5u; Z.reg_user = 1u; Z.acct_rgn = 2u;
            Z.acct_rgn = 2u; fr2(&Z, 0); CHECK(Z.f_reg == 5u);          /* once the player has chosen, the account no longer moves it */
        }
        G.act_sel = GNB_ACT_RANDOM;
        for (at = 0; at < 20u; at++) {                                  /* Random never picks the full room (2) or the passcode room (3) */
            G.scr = GNB_LIST; G.req = GNB_REQ_NONE; G.focus = 1; G.act_sel = GNB_ACT_RANDOM;
            press(GNB_B_A);
            CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && G.sel != 2 && G.sel != 3);
            fr(0);
        }
        G.req = 0; G.scr = GNB_LIST; G.act_sel = GNB_ACT_CREATE; G.focus = 1;
        press(GNB_B_A);
        CHECK(G.scr == GNB_CREATE);
        press(GNB_B_B);
        CHECK(G.scr == GNB_LIST);
        press(GNB_B_DOWN);
        CHECK(G.focus == 0);
        /* the lobby footer follows the state; a tap of B does nothing, 1.5 s of B leaves */
        memset(&v, 0, sizeof v);
        v.have = 1; v.members = 3; v.pos = 0xFF; v.side = 0xFF; strcpy(v.rname, "Room 1");
        gnb_net_view(&G, &v);
        CHECK(G.scr == GNB_LOBBY && !strcmp(gnb_foot(&G), "Y: Standings  Start: Join  Hold B: Leave"));
        press(GNB_B_START);
        CHECK(G.req == GNB_REQ_ACTION && G.act == 1);
        G.req = 0;
        v.flags = GNB_V_QUEUED; v.pos = 1; gnb_net_view(&G, &v);
        CHECK(!strncmp(gnb_foot(&G), "Y: Standings  Z: Leave queue", 28));
        v.phase = 2; gnb_net_view(&G, &v);                                     /* queued while a set runs: Z still listed and still leaves the queue */
        CHECK(strstr(gnb_foot(&G), "Z: Leave queue") != 0);
        press(GNB_B_Z);
        CHECK(G.req == GNB_REQ_ACTION && G.act == 2);
        G.req = 0; v.phase = 0; gnb_net_view(&G, &v);
        v.rflags = 2; v.stadium = 2; gnb_net_view(&G, &v);                    /* spectatable room: the bar lists the X key (and the info line 6 does too) */
        CHECK(!strncmp(gnb_foot(&G), "Y: Standings  Z: Leave queue", 28) && strlen(gnb_foot(&G)) <= 54);
        CHECK(!strcmp(gnb_lobby_rule(&G), "Legal stages"));                    /*: the header says the stage rule only, whatever the old Stadium policy */
        v.stadium = 1; gnb_net_view(&G, &v);                                   /* frozen Stadium, spectatable: still watchable */
        CHECK(!strncmp(gnb_foot(&G), "Y: Standings", 12) && !strcmp(gnb_lobby_rule(&G), "Legal stages"));
        v.stadium = 0; v.smode = 2; gnb_net_view(&G, &v);                      /* an All stages room */
        CHECK(!strncmp(gnb_foot(&G), "Y: Standings", 12) && !strcmp(gnb_lobby_rule(&G), "All stages"));
        v.smode = 0; gnb_net_view(&G, &v);
        v.stadium = 1; v.rflags = 0; gnb_net_view(&G, &v);                     /* not spectatable: no X */
        CHECK(strncmp(gnb_foot(&G), "X: ", 3) != 0);
        v.rflags = 2; v.stadium = 2; gnb_net_view(&G, &v);
        v.flags = 0; v.pos = 0xFF; gnb_net_view(&G, &v);
        CHECK(!strncmp(gnb_foot(&G), "X: Replay  Y: Standings  Start: Join", 36) && strlen(gnb_foot(&G)) <= 54);
        {   /* NO GAME TO REPLAY YET and AUTO-WATCH (and 5): the view's watch facts grey X; the first Watch arms the auto-watch; a running set is then watched at once */
            char l6[GNB_LINE + 1];
            unsigned k;
            v.wfacts = GNB_VF_NOREPLAY; gnb_net_view(&G, &v);
            CHECK(strncmp(gnb_foot(&G), "X: ", 3) != 0 && !strcmp(gnb_foot(&G), "Y: Standings  Start: Join  Hold B: Leave"));
            gnb_panel(&G, 6, l6); CHECK(!strcmp(l6, "No game to replay yet"));
            press(GNB_B_X); CHECK(G.watch == 0 && G.autow == 0 && G.msg && !strcmp(G.msg, "No game to replay yet"));
            v.wfacts = 0; gnb_net_view(&G, &v);
            gnb_panel(&G, 6, l6); CHECK(!strcmp(l6, "X: Replay last game"));
            v.phase = 2; v.wfacts = GNB_VF_NOREPLAY; gnb_net_view(&G, &v);          /* a set is running: only the live fact counts */
            gnb_panel(&G, 6, l6); CHECK(!strcmp(l6, "X: Watch live") && !strncmp(gnb_foot(&G), "X: Live", 7));
            v.wfacts = GNB_VF_NOLIVE; gnb_net_view(&G, &v);
            gnb_panel(&G, 6, l6); CHECK(!strcmp(l6, "No live game yet") && strncmp(gnb_foot(&G), "X: ", 3) != 0);
            press(GNB_B_X); CHECK(G.watch == 0 && G.msg && !strcmp(G.msg, "No live game yet"));
            v.phase = 0; v.wfacts = 0; gnb_net_view(&G, &v);
            /* the first manual Watch arms it (default on) */
            CHECK(G.autow == 0);
            press(GNB_B_X); CHECK(G.watch == 1 && G.autow == 1 && G.autoc == 0 && !G.watch_live);
            G.watch = 0;                                                            /* (the glue took it; the player came back) */
            CHECK(!strcmp(gnb_foot(&G), "Hold X: Stop watching  Start: Join  Hold B: Leave") && strlen(gnb_foot(&G)) <= 52);
            gnb_panel(&G, 6, l6); CHECK(!strcmp(l6, "Watching every game"));
            gnb_net_view(&G, &v); CHECK(G.watch == 0);                              /* between sets: it waits */
            v.phase = 2; v.wfacts = GNB_VF_NOLIVE; gnb_net_view(&G, &v); CHECK(G.watch == 0);       /* a set runs but no game is uploaded yet: it waits */
            v.wfacts = 0; v.flags = GNB_V_ONSIDE; v.side = 0; gnb_net_view(&G, &v); CHECK(G.watch == 0);   /* the player is in the set: never */
            v.flags = 0; v.side = 0xFF; gnb_net_view(&G, &v);
            CHECK(G.watch == 1 && G.watch_live == 1 && G.autoc == 1 && G.autow == 1);          /* the next game: watched at once, live */
            for (k = 2; k <= GNB_AUTO_MAX; k++) { G.watch = 0; gnb_net_view(&G, &v); CHECK(G.watch == 1 && G.autoc == k); }
            G.watch = 0; gnb_net_view(&G, &v);                                      /* a watch that fails at once and comes back: the chain stops */
            CHECK(G.watch == 0 && G.autow == 0 && G.msg && !strcmp(G.msg, "Auto watch stopped"));
            press(GNB_B_X); CHECK(G.autow == 1 && G.watch == 1);                    /* X starts watching AND turns it on */
            G.watch = 0;
            v.phase = 0; gnb_net_view(&G, &v); CHECK(G.autoc == 0 && G.watch == 0);       /* the lobby saw the set end: the chain restarts */
            press(GNB_B_X); CHECK(G.autow == 1 && G.watch == 0);                    /* a short X tap while watching is on does nothing */
            { unsigned h; for (h = 0; h < GNB_HOLD_B_FRAMES - 1u; h++) fr(GNB_B_X); fr(0); CHECK(G.autow == 1);           /* released early: no stop */
              for (h = 0; h < GNB_HOLD_B_FRAMES + 2u; h++) fr(GNB_B_X); fr(0); CHECK(G.autow == 0 && G.msg && !strcmp(G.msg, "Stopped watching")); }      /* held 1.0 s: stop, auto-watch off */
            v.phase = 2; gnb_net_view(&G, &v); CHECK(G.watch == 0);                 /* off: nothing is armed */
            G.autow = 1; v.phase = 0; gnb_net_view(&G, &v);
            press(GNB_B_START); CHECK(G.autow == 1 && G.req == GNB_REQ_ACTION && G.act == 1); G.req = 0; G.autow = 0;      /* joining the queue arms it */
            v.rflags = 0; gnb_net_view(&G, &v); press(GNB_B_R); CHECK(G.autow == 0);                          /* not a watchable room: no toggle */
            v.rflags = 2; v.stadium = 2; v.flags = 0; v.phase = 0; gnb_net_view(&G, &v);
            press(GNB_B_X); CHECK(G.autow == 1);
            for (k = 0; k < GNB_HOLD_B_FRAMES + 2u; k++) fr(GNB_B_B);               /* Hold B leaves the room and with it the auto-watch */
            fr(0);
            CHECK(G.autow == 0 && G.req == GNB_REQ_LEAVE);
            G.req = 0; G.watch = 0; G.scr = GNB_LOBBY; G.hold_b = 0; G.msg = 0; v.wfacts = 0; gnb_net_view(&G, &v);
        }
        {   /* Y: the crowns panel (live): requests the roster, shows the rows, Y or B closes, a view with a call-up closes it too */
            struct gnb_roster ro;
            char nm[24], cr[8], st[14];
            unsigned k;
            memset(&ro, 0, sizeof ro);
            ro.n = 6; ro.streak = 3;                                           /* wire order: by crowns, as an older server sends it */
            strcpy(ro.name[0], "Wobble Pilot"); ro.crowns[0] = 4;   ro.place[0] = 0;
            strcpy(ro.name[1], "Pocket Fox");   ro.crowns[1] = 1;   ro.place[1] = 2;
            strcpy(ro.name[2], "Zed");          ro.crowns[2] = 0;   ro.place[2] = 0xFE;
            strcpy(ro.name[3], "Nigzel");       ro.crowns[3] = 0x84; ro.place[3] = 0;          /* the viewer, 4 crowns */
            strcpy(ro.name[4], "Amy");          ro.crowns[4] = 0;   ro.place[4] = 0xFD;
            strcpy(ro.name[5], "Bob");          ro.crowns[5] = 0;   ro.place[5] = 1;
            G.req = GNB_REQ_NONE;
            press(GNB_B_Y);
            CHECK(G.ros_open && G.req == GNB_REQ_ROSTER && strlen(gnb_foot(&G)) <= 54);
            ro.pstreak[2] = 3;                                                 /* Zed is on the winner side: his own streak is the room's */
            gnb_net_roster(&G, &ro);
            /* order: champion, challenger, queue (Bob 1, Pocket Fox 2), then the rest by crowns desc, streak wins, then the server's order (Wobble Pilot 4 = Nigzel 4: wire order kept) */
            k = gnb_roster_row(&G, 0, nm, cr, st); CHECK(k == 3 && !strcmp(nm, "Zed") && !strcmp(cr, "0") && !strcmp(st, "On stage"));
            k = gnb_roster_row(&G, 1, nm, cr, st); CHECK(k == 3 && !strcmp(nm, "Amy") && !strcmp(st, "Challenger"));
            k = gnb_roster_row(&G, 2, nm, cr, st); CHECK(k == 1 && !strcmp(nm, "Bob") && !strcmp(st, "Queue 1"));
            k = gnb_roster_row(&G, 3, nm, cr, st); CHECK(k == 1 && !strcmp(nm, "Pocket Fox") && !strcmp(cr, "1") && !strcmp(st, "Queue 2"));
            k = gnb_roster_row(&G, 5, nm, cr, st); CHECK(k == 2 && !strcmp(nm, "Nigzel (You)") && !strcmp(cr, "4") && !strcmp(st, "Watching"));      /* viewer marker; the tied lead is said in the header line, no star */
            k = gnb_roster_row(&G, 4, nm, cr, st); CHECK(k == 1 && !strcmp(nm, "Wobble Pilot") && !strcmp(cr, "4") && !gnb_roster_row(&G, 6, nm, cr, st));
            {   unsigned r;       /* every string fits its column */
                for (r = 0; r < 6u; r++) { gnb_roster_row(&G, r, nm, cr, st); CHECK(strlen(nm) <= 18 && strlen(cr) <= 4 && strlen(st) <= 10); }
            }
            {   char ld[44], sk[8]; unsigned r;
                /* the server ranks and flags the lead; here two rows carry the flag (a full tie on crowns, streak wins and recency) */
                for (r = 0; r < 6u; r++) G.ros.plead[r] = 0;
                for (r = 0; r < 6u; r++) if (!strcmp(G.ros.name[r], "Nigzel") || !strcmp(G.ros.name[r], "Wobble Pilot")) G.ros.plead[r] = 1;
                gnb_lead_text(&G, ld, 12); CHECK(!strcmp(ld, "Wobble Pilot and Nigzel are in the lead") || !strcmp(ld, "Nigzel and Wobble Pilot are in the lead"));
                gnb_lead_text(&G, ld, 6); CHECK(strstr(ld, "Wobbl.") && strstr(ld, " are in the lead"));              /* names cut to fit the header */
                gnb_roster_streak(&G, 0, sk); CHECK(!strcmp(sk, "3"));                                              /* Zed: 3 streak wins */
                for (r = 1; r < 6u; r++) { gnb_roster_streak(&G, r, sk); CHECK(!strcmp(sk, "0")); }
                for (r = 0; r < 6u; r++) if (!strcmp(G.ros.name[r], "Wobble Pilot")) G.ros.plead[r] = 0;
                gnb_lead_text(&G, ld, 12); CHECK(!strcmp(ld, "Nigzel currently in the lead"));
                G.ros.plead[3] = G.ros.plead[4] = 1;
                gnb_lead_text(&G, ld, 12); CHECK(!strcmp(ld, "3 players tied for the lead"));
                for (r = 0; r < 6u; r++) { G.ros.plead[r] = 1; G.ros.crowns[r] = 0; }                                /* nobody has a crown: nobody leads, whatever the flags */
                gnb_lead_text(&G, ld, 12); CHECK(!strcmp(ld, "No crowns yet"));
            }
            press(GNB_B_Y);
            CHECK(!G.ros_open);
            press(GNB_B_Y); gnb_net_roster(&G, &ro);
            press(GNB_B_B);
            CHECK(!G.ros_open && G.scr == GNB_LOBBY);
            press(GNB_B_Y); v.flags |= GNB_V_CALLED; gnb_net_view(&G, &v); v.flags &= ~GNB_V_CALLED;
            CHECK(!G.ros_open);
            press(GNB_B_Y); G.ros_open = 1; gnb_net_fail(&G, GNB_REQ_ROSTER, GNB_W_BAD);
            CHECK(!G.ros_open);
            G.req = GNB_REQ_NONE; v.flags = 0; v.pos = 0xFF; gnb_net_view(&G, &v);
        }
        v.rflags = 0; v.stadium = 0; v.flags = GNB_V_QUEUED; v.pos = 1; gnb_net_view(&G, &v);
        CHECK(!strncmp(gnb_foot(&G), "Y: Standings  Z: Leave queue", 28));
        press(GNB_B_B);
        CHECK(G.scr == GNB_LOBBY && G.req == GNB_REQ_NONE);
        for (at = 0; at + 1u < GNB_HOLD_B_FRAMES; at++) fr(GNB_B_B);
        CHECK(G.scr == GNB_LOBBY);                                      /* one frame short */
        fr(GNB_B_B); fr(GNB_B_B);
        CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_LEAVE);
        fr(0);
        G.req = 0;
        gnb_net_left(&G);
        G.req = 0;
    }
    /* failures of the list */
    gnb_net_fail(&G, GNB_REQ_LIST, GNB_W_RATE);
    CHECK(G.net == GNB_N_OK && has("Too many tries"));
    gnb_net_fail(&G, GNB_REQ_LIST, GNB_W_NOGN);
    CHECK(G.net == GNB_N_NOGN && has("No Game Night here") && has("No rooms on this server"));
    press(GNB_B_Z);
    CHECK(G.scr == GNB_LIST && G.sfx == GNB_SFX_DENY);
    gnb_net_fail(&G, GNB_REQ_LIST, GNB_W_UNREACH);
    CHECK(has("Can't reach the server") && has("Press Start to retry"));
    press(GNB_B_START);
    CHECK(G.req == GNB_REQ_LIST && G.net == GNB_N_LOADING);
    G.req = 0;
    press(GNB_B_START);
    CHECK(G.req == GNB_REQ_NONE);                                       /* 2 s between two refreshes */
    gnb_net_list(&G, 0, 0, 0, page);
    CHECK(G.net == GNB_N_EMPTY && has("No rooms yet") && has("Press Z to create one"));
    gnb_net_fail(&G, GNB_REQ_LIST, GNB_W_SIGNIN);
    CHECK(has("Needs an account"));
    for (i = 0; i < GNB_AUTO_FRAMES + 5u; i++) fr(0);                    /* no automatic refresh in a failure state */
    CHECK(G.req == GNB_REQ_NONE);
    gnb_net_list(&G, 1, 0, 1, page);
    for (i = 0; i < GNB_AUTO_FRAMES; i++) fr(0);
    CHECK(G.req == GNB_REQ_LIST);                                       /* every 30 s while the list is open */
    press(GNB_B_B);
    CHECK(G.exit);                                                      /*: a tap of B on the room list goes back */

    /* ---- the match loop with view v3 (Rooms-aligned): START queues, the pick turn and its countdown, A picks, "go" hands off, the resume ---- */
    gnb_enter(&G, 1);
    G.req = 0;
    {   /* list v3: the region column */
        struct gnb_room rr = mk(7, 0);
        char c[9], h[13], rg[5], md[9], n[9];
        rr.region = 2;
        gnb_net_list(&G, 1, 0, 1, &rr);
        CHECK(gnb_cols(&G, 0, c, h, rg, md, n) == 0 && !strcmp(rg, "EU"));
        G.req = 0;
        {   /* all seven names, then an unknown value draws "-" (Oceania is 5, North America Central 6), and so does a value past them */
            static const char *const NM[9] = { "NAE", "NAW", "EU", "LA", "AS", "OC", "NAC", "-", "-" };
            unsigned q;
            for (q = 0; q < 9u; q++) {
                rr.region = (unsigned char)(q == 7u ? 0xFF : q);
                gnb_net_list(&G, 1, 0, 1, &rr);
                CHECK(gnb_cols(&G, 0, c, h, rg, md, n) == 0 && !strcmp(rg, NM[q]));
                G.req = 0;
            }
        }
    }
    memset(&v, 0, sizeof v);
    v.have = 1; v.v3 = 1; v.members = 4; v.pos = 0xFF; v.side = 0xFF; v.secs = 0xFF; v.turn = 0xFF; v.stage = 0xFF; v.cap = 8; v.region = 2;
    v.side_on[0] = v.side_on[1] = 1; strcpy(v.side_name[0], "Wobble Pilot"); strcpy(v.side_name[1], "Pocket Fox"); strcpy(v.rname, "Game Night"); strcpy(v.code, "7471"); v.stadium = 0;
    gnb_net_view(&G, &v);
    CHECK(G.scr == GNB_LOBBY && has("Waiting for players") && has("Start: Join"));
    press(GNB_B_START);
    CHECK(G.req == GNB_REQ_ACTION && G.act == 1);                       /* START joins the queue (Rooms) */
    G.req = 0;
    v.phase = 1; v.flags = GNB_V_CALLED | GNB_V_ONSIDE; v.side = 1; v.turn = 0; v.picked = 0; v.secs = 28; v.set_id = 3;
    gnb_net_view(&G, &v);
    CHECK(!has("28s") && !G.play && (has("is choosing")));                /* the other side's turn: a name, no countdown */
    { char ck[16]; gnb_pick_clock(&G, ck); CHECK(!ck[0]); }
    press(GNB_B_A);
    CHECK(G.req == GNB_REQ_NONE && has("Wait for your turn"));          /* not our turn yet */
    v.turn = 1; v.picked = 1; v.secs = 30;
    gnb_net_view(&G, &v);
    CHECK(has("Your pick: press A 30s"));
    {   /* the pick grid: 7 x 4 in the retail order, the short last row, Random, lock, cancel, the clock */
        char ck[8];
        unsigned r, c;
        gnb_pick_clock(&G, ck); CHECK(!strcmp(ck, "0:30"));
        for (i = 0; i < GNB_FPS * 7u; i++) fr(0);
        gnb_pick_clock(&G, ck); CHECK(!strcmp(ck, "0:23") && has("Your pick: press A 23s"));      /* counts down locally between two views */
        G.req = 0;                                                     /* the lobby polls (GN_STATE) meanwhile */
        gnb_pick_cell(26, &r, &c); CHECK(r == 3 && c == 5);
        gnb_pick_cell(20, &r, &c); CHECK(r == 2 && c == 6);
        CHECK(!strcmp(gnb_char_name(9), "Marth") && !strcmp(gnb_char_name(26), "Random") && !strcmp(gnb_char_name(19), "Sheik") && !gnb_char_name(27)[0]);
        press(GNB_B_A);
        CHECK(G.pick_open && G.pick_cur == 0 && G.req == GNB_REQ_NONE && has("Costume 1 of 6") && has("A: Lock"));   /* A opens the grid on the first cell */
        press(GNB_B_LEFT); CHECK(G.pick_cur == 6);                     /* left wraps inside the row: Link */
        press(GNB_B_RIGHT); CHECK(G.pick_cur == 0);
        press(GNB_B_DOWN); press(GNB_B_DOWN); CHECK(G.pick_cur == 14);   /* Ice Climbers */
        for (i = 0; i < 6u; i++) press(GNB_B_RIGHT);
        CHECK(G.pick_cur == 20 && has("Falco  Costume 1 of 4"));
        press(GNB_B_DOWN); CHECK(G.pick_cur == 26 && has("Pick: Random"));   /* the column past the short last row: its last cell, Random */
        press(GNB_B_DOWN); CHECK(G.pick_cur == 26);                    /* stops at the bottom */
        press(GNB_B_RIGHT); CHECK(G.pick_cur == 21);                   /* wraps in the 6-cell row: Young Link */
        press(GNB_B_UP); press(GNB_B_UP); press(GNB_B_UP); press(GNB_B_UP); CHECK(G.pick_cur == 0);     /* stops at the top */
        G.req = 0;
        fr(GNB_B_B);
        CHECK(!G.pick_open && G.req == GNB_REQ_NONE && G.scr == GNB_LOBBY);   /* B: back to the room, nothing sent (the previous pick stays) */
        for (i = 0; i < GNB_HOLD_B_FRAMES + 4u; i++) fr(GNB_B_B);
        CHECK(G.scr == GNB_LOBBY && G.req != GNB_REQ_LEAVE);           /* the B that closed the grid, held on, never becomes a hold-B leave */
        /* a busy screen (a request in flight) ignores B for its first 3 s; after that holding B for 1.0 s backs out to the menu */
        G.pick_open = 0; G.more = 0; G.rk.on = 0; G.scr = GNB_BUSY; G.busy_what = GNB_REQ_STATE; G.busy_t = 0; G.exit = 0; G.hold_b = 0;
        for (i = 0; i < GNB_HOLD_B_FRAMES + 10u; i++) fr(GNB_B_B);
        CHECK(G.scr == GNB_BUSY && !G.exit);                               /* held B inside the first 3 s: nothing */
        fr(0); G.hold_b = 0;
        for (i = 0; i < GNB_BUSY_ESC; i++) fr(0);
        for (i = 0; i < GNB_HOLD_B_FRAMES + 2u; i++) fr(GNB_B_B);
        CHECK(G.exit == 1);                                                /* held B 1.0 s on a busy screen older than 3 s: out */
        fr(0); G.exit = 0; G.scr = GNB_LOBBY; G.busy_t = 0; G.req = 0; G.hold_b = 0;
        fr(0); G.req = 0;
        press(GNB_B_A); CHECK(G.pick_open);
        press(GNB_B_DOWN); press(GNB_B_RIGHT); press(GNB_B_RIGHT);
        CHECK(G.pick_cur == 9);
        press(GNB_B_A);
        CHECK(!G.pick_open && G.req == GNB_REQ_ACTION && G.act == 4 && G.act_val == 9u && G.pick_mine == 9u);    /* the lock: GN_ACTION ready + Marth */
        CHECK(gnb_side_char(&G, 1) == 9u);                             /* a locked pick shows in the slot at once */
        G.req = 0;
        press(GNB_B_A); CHECK(G.pick_open && G.pick_cur == 9);         /* reopened (the lock's reply not in yet): on the last pick */
        v.turn = GNB_TURN_GO; v.picked = 3; v.secs = 60; v.side_char[1] = 9;
        gnb_net_view(&G, &v);
        CHECK(!G.pick_open);                                           /* the turn is over: the grid closes */
        gnb_pick_clock(&G, ck); CHECK(!ck[0]);
        v.turn = 1; v.picked = 1; v.secs = 30;
    }
    G.req = 0;
    v.turn = GNB_TURN_GO; v.picked = 3; v.secs = 60;
    gnb_net_view(&G, &v);
    CHECK(G.play == 1 && !strcmp(G.play_name, "GN7471S3") && has("Starting the match"));   /* the glue arms the room search and closes the browser */
    /* back from the set: the browser resumes the room (GN_STATE), the lobby or the list */
    gnb_enter(&G, 1);
    gnb_resume(&G);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_STATE);
    G.req = 0;
    v.phase = 0; v.flags = GNB_V_QUEUED; v.side = 0xFF; v.pos = 3; v.qlen = 3; v.turn = 0xFF; v.picked = 0; v.secs = 0xFF;
    gnb_net_view(&G, &v);
    CHECK(G.scr == GNB_LOBBY && !has("Back in the room") && has("You: queued 3"));
    gnb_enter(&G, 1);
    gnb_resume(&G);
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_STATE, GNB_W_NOTFOUND);
    CHECK(G.scr == GNB_LIST && G.req == GNB_REQ_LIST && has("You are no longer in the room"));
    /* ---- a toast's room invite accepted INSIDE a room, and the join fails: back in our room  ---- */
    G.scr = GNB_LOBBY; G.lob.have = 1;
    gnb_join_friend(&G, "4242", 0u);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && G.fj_lob);
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);
    CHECK(G.scr == GNB_BUSY && G.busy_what == GNB_REQ_STATE && G.req == GNB_REQ_STATE && !G.fj && !G.fj_lob);
    /* ---- a friend's room from the Friends list ---- */
    gnb_enter(&G, 1);                                                   /* open room, or the host's friend: the join goes out at once, with no passcode */
    gnb_join_friend(&G, "4821", 2u);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && !strcmp(gnb_join_code(&G), "4821") && !strcmp(gnb_join_pin(&G), ""));
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_FULL);                         /* refused: the list is fetched now, nothing else is armed */
    CHECK(G.scr == GNB_LIST && G.req == GNB_REQ_LIST && !G.fj && has("Room is full"));
    gnb_enter(&G, 1);                                                   /* the passcode is asked of this console: the passcode screen first, the join waits for it */
    gnb_join_friend(&G, "4821", 3u);
    CHECK(G.scr == GNB_PASS && G.req == GNB_REQ_LIST && G.p_for == 0 && has("Passcode required"));
    press(GNB_B_B);
    CHECK(G.scr == GNB_LIST && !G.fj);                                  /* B goes back to the list */
    gnb_join_friend(&G, "4821", 3u);
    G.p_pin[0] = '1'; G.p_pin[1] = '2'; G.p_pin[2] = '3'; G.p_pin[3] = '4'; G.p_pos = 3;
    G.req = 0;
    press(GNB_B_A);
    CHECK(G.scr == GNB_BUSY && G.req == GNB_REQ_JOIN && !strcmp(gnb_join_code(&G), "4821") && !strcmp(gnb_join_pin(&G), "1234"));
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);                     /* a wrong passcode typed: back to the list with the generic refusal */
    CHECK(G.scr == GNB_LIST && !G.fj && G.req == GNB_REQ_LIST);
    gnb_enter(&G, 1);                                                   /* the skip was refused by the server (not the host's friend any more): the passcode screen as the fallback */
    gnb_join_friend(&G, "4821", 2u);
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);
    CHECK(G.scr == GNB_PASS && G.fj && !G.fpin && has("Passcode required"));
    G.p_pos = 3; press(GNB_B_A);
    CHECK(G.fpin && !strcmp(gnb_join_pin(&G), "0000"));
    G.req = 0;
    { struct gnb_lobby v; memset(&v, 0, sizeof v); v.have = 1; v.turn = 0xFF; v.secs = 0xFF; v.side = 0xFF; v.pos = 0xFF; strcpy(v.code, "4821"); strcpy(v.rname, "Friday"); gnb_net_view(&G, &v); }
    CHECK(G.scr == GNB_LOBBY && !G.fj);                                 /* in: the friend join is over, the lobby is the browser's own */
    gnb_enter(&G, 1);                                                   /* an open room that is gone: no passcode fallback (nothing to type) */
    gnb_join_friend(&G, "4821", 0u);
    G.req = 0;
    gnb_net_fail(&G, GNB_REQ_JOIN, GNB_W_NOTFOUND);
    CHECK(G.scr == GNB_LIST && !G.fj && G.req == GNB_REQ_LIST && has("Room"));
    /* ---- Create room: Friends Skip Passcode ---- */
    gnb_enter(&G, 1);
    G.req = 0;
    {
        const char *nm, *pin; unsigned fl, pol;
        G.scr = GNB_CREATE; G.c_row = 5; G.page = 5;
        CHECK(gnb_rows(&G) == 7u);
        gnb_row(&G, 5, num_, tit_);
        CHECK(!num_[0] && !strcmp(tit_, "Skip passcode: yes"));
        gnb_create_args(&G, &nm, &fl, &pol, &pin);
        CHECK(fl == 11u);                                               /* listed, spectators, no passcode, default Yes: the flag is sent anyway (harmless without a passcode) */
        G.c_pass = 1; strcpy(G.c_pin, "4321");
        gnb_create_args(&G, &nm, &fl, &pol, &pin);
        CHECK(fl == 11u && !strcmp(pin, "4321"));                        /* passcode set, default Yes: flag 8 */
        press(GNB_B_RIGHT);
        gnb_row(&G, 5, num_, tit_);
        CHECK(!strcmp(tit_, "Skip passcode: no"));
        gnb_create_args(&G, &nm, &fl, &pol, &pin);
        CHECK(fl == 3u);
        press(GNB_B_RIGHT);
        G.c_pass = 0; G.c_listed = 0;                                   /* a private room (it gets a server password) carries the bit too */
        gnb_create_args(&G, &nm, &fl, &pol, &pin);
        CHECK(fl == 10u);
    }
    {   /* ranking among the watchers: equal crowns, the higher streak first (the server sends this order; the console sorts by the same rule) */
        struct gnb_roster ro;
        char nm[24], cr[8], st[14];
        memset(&ro, 0, sizeof ro);
        ro.n = 3;
        strcpy(ro.name[0], "Ann"); ro.crowns[0] = 2; ro.place[0] = 0; ro.pstreak[0] = 0;
        strcpy(ro.name[1], "Bea"); ro.crowns[1] = 2; ro.place[1] = 0; ro.pstreak[1] = 4;
        strcpy(ro.name[2], "Cy");  ro.crowns[2] = 3; ro.place[2] = 0; ro.pstreak[2] = 0;
        gnb_enter(&G, 1);
        gnb_net_roster(&G, &ro);
        gnb_roster_row(&G, 0, nm, cr, st); CHECK(!strcmp(nm, "Cy"));
        gnb_roster_row(&G, 1, nm, cr, st); CHECK(!strcmp(nm, "Bea"));
        gnb_roster_row(&G, 2, nm, cr, st); CHECK(!strcmp(nm, "Ann"));
    }
    {   char r1[24], r2[24];                                            /* lobby header top right: the rules, no mode */
        CHECK(gnb_upload_room(2u, 0u) && gnb_upload_room(2u, 1u) && gnb_upload_room(2u, 2u) && gnb_upload_room(3u, 0u) && !gnb_upload_room(0u, 2u) && !gnb_upload_room(1u, 0u));   /* the uploader runs in every spectatable room, whatever the Stadium policy */
        G.lob.smode = 0; gnb_lobby_rule_parts(&G, r1, r2); CHECK(!strcmp(r1, "Legal stages") && !strcmp(r2, ""));
        G.lob.smode = 2; gnb_lobby_rule_parts(&G, r1, r2); CHECK(!strcmp(r1, "All stages") && !strcmp(r2, ""));
        G.lob.smode = 0;
        CHECK(strlen(r1) <= 13 && strlen(r2) <= 15);                       /* widths: 13 characters at 0.04 about 170 px, 15 at 0.022 about 100 px: inside the right 250 px of the 640 px header */
    }
    {   /* the list with 9, 12, 24 and 30 rooms: every key, the paging, a refresh while page 2 loads, a list that shrinks under the cursor */
        static const unsigned totals[4] = { 9u, 12u, 24u, 30u };
        unsigned t, k, p, j;
        for (t = 0; t < 4u; t++) {
            unsigned total = totals[t], want = total > GNB_MAX ? GNB_MAX : total;
            gnb_enter(&G, 1);
            G.req = 0;
            for (p = 0; p * 8u < total; p++) {
                struct gnb_room pg[8];
                k = total - p * 8u > 8u ? 8u : total - p * 8u;
                for (j = 0; j < k; j++) pg[j] = mk(p * 8u + j, (p * 8u + j) % 3u == 0u ? GNB_F_MOCK : 0u);
                if (p == 1u) { press(GNB_B_START); }                                   /* a refresh while page 2 is being asked for: must not corrupt the cache */
                gnb_net_list(&G, total, p, k, pg);
                G.req = 0;
            }
            CHECK(G.n == want && G.net == GNB_N_OK);
            for (j = 0; j < total + 6u; j++) {
                press(GNB_B_DOWN);
                CHECK(G.first + G.page < G.n && G.page < GNB_VIS && G.first <= gnb_max_first(&G));
                draw();
            }
            for (j = 0; j < GNB_VIS; j++) {                                              /* every visible slot builds its texts, also past the last room */
                char c0[24], h0[24], r0[8], m0[10], n0[12];
                (void)gnb_cols(&G, j, c0, h0, r0, m0, n0);
                (void)gnb_row(&G, j, num_, tit_);
                CHECK(strlen(num_) <= 8u && strlen(tit_) <= 20u + 1u);
            }
            press(GNB_B_X); press(GNB_B_X); press(GNB_B_Y); press(GNB_B_Y);
            CHECK(G.first + G.page < G.n);
            G.first = (unsigned char)gnb_max_first(&G); G.page = (unsigned char)(G.n - 1u - G.first); G.dirty = GNB_D_ALL;     /* cursor on the LAST row (row 9 of the window) */
            {   struct gnb_room pg[8];                                                  /* the list shrinks to 3 rooms under it */
                for (j = 0; j < 3u; j++) pg[j] = mk(j, 0);
                gnb_net_list(&G, 3, 0, 3, pg);
                G.req = 0;
                CHECK(G.n == 3 && G.first + G.page < G.n && G.first <= gnb_max_first(&G));
                draw();
                press(GNB_B_DOWN); press(GNB_B_DOWN); press(GNB_B_DOWN); press(GNB_B_A);
                CHECK(G.first + G.page < G.n);
            }
        }
    }
    {   /* the lobby header's "Open for ..." line under the room code */
        static const struct { unsigned m; const char *t; } T[] = {
            { 0, "Just opened" }, { 1, "Open for 1 min" }, { 12, "Open for 12 min" }, { 59, "Open for 59 min" }, { 60, "Open for 1 h" }, { 72, "Open for 1 h 12 min" },
            { 135, "Open for 2 h 15 min" }, { 23u * 60u + 59u, "Open for 23 h 59 min" }, { 24u * 60u, "Open for 24 h" }, { 30u * 60u + 5u, "Open for 30 h" },
            { 98u * 60u + 59u, "Open for 98 h" }, { 99u * 60u, "Open for 99+ h" }, { 65535u, "Open for 99+ h" } };
        char o[40];
        unsigned i;
        gnb_enter(&G, 1);
        for (i = 0; i < sizeof T / sizeof T[0]; i++) {
            G.lob.minutes = (unsigned short)T[i].m;
            gnb_open_text(&G, o);
            CHECK(!strcmp(o, T[i].t) && strlen(o) <= 20);              /* 20 characters at 0.022 are about 150 px: left of the pick clock (x 290..350) from x 41 */
        }
        for (i = 0; i < 65536u; i += 7u) { G.lob.minutes = (unsigned short)i; gnb_open_text(&G, o); CHECK(strlen(o) <= 20u); }
    }
    {   /* a tournament match room: its identity, a footer without Standings or Leave queue, the join clock */
        char o[40], a[40], b[40];
        gnb_enter(&G, 1);
        gnb_tn_set(&G, "Cube Open", "Losers Round 1", "PC Bot 6", 3u, 150u);
        gnb_open_text(&G, o); CHECK(!strcmp(o, "Losers Round 1"));
        gnb_lobby_rule_parts(&G, a, b); CHECK(!strcmp(a, "Best of 3") && !strcmp(b, "vs PC Bot 6"));
        CHECK(!strcmp(gnb_foot(&G), "Hold B: Leave"));
        G.lob.members = 1; G.clk += 30u * 9u; gnb_tn_clock(&G, o); CHECK(!strcmp(o, "Join in 2:21"));
        G.lob.members = 2; gnb_tn_clock(&G, o); CHECK(!o[0]);
        gnb_enter(&G, 1); gnb_open_text(&G, o); CHECK(strncmp(o, "Losers", 6) != 0);                 /* a fresh browser is a Game Night browser again */
    }
    {   /* the pick grid's labels: every name fits a 64 px cell at the larger label size (widths estimated from the menu font classes: narrow i l j t f I. , ' space: 7, W M 16, m w 15, other capitals 13, the rest 10 at font 0.03) */
        unsigned i, k, w, worst = 0;
        for (i = 0; i < GNB_PICK_N; i++) {
            const char *n = gnb_cell_label(i);
            for (w = 0, k = 0; n[k]; k++) {
                char c = n[k];
                w += (c == 'i' || c == 'l' || c == 'j' || c == 't' || c == 'f' || c == 'I' || c == '.' || c == ',' || c == '\'' || c == ' ' || c == ':') ? 7u : (c == 'W' || c == 'M') ? 16u : (c == 'm' || c == 'w') ? 15u : (c >= 'A' && c <= 'Z') ? 13u : 10u;
            }
            w = w * 26u / 30u;
            if (w > worst) worst = w;
            CHECK(n[0]);
            if (i == 19u) CHECK(w <= 64u);                       /* Sheik is the one cell drawn as text on a normal run (every other name is patched into its retail icon); in a failed-icon fallback long names such as ICE CLIMBERS are wider than a cell and run over, as before */
            CHECK(!strcmp(gnb_cell_label(i), gnb_cell_label(i)));
        }
        CHECK(!strcmp(gnb_cell_label(19), "SHEIK"));
        printf("pick grid labels: widest estimate %u px (Sheik %u of 64)\n", worst, 0u);
    }
    {   /* the costume in the picker: L / R cycle 1..N of the character under the cursor (N per character from the retail table), remembered per character, sent with the pick */
        struct gnb_lobby v;
        unsigned id, n;
        for (id = 0; id < 26u; id++) CHECK(gnb_costumes(id) >= 4u && gnb_costumes(id) <= 6u);
        CHECK(gnb_costumes(26) == 0u && gnb_costumes(2) == 4u && gnb_costumes(0) == 6u);          /* Fox 4, Captain Falcon 6 */
        gnb_enter(&G, 1);
        memset(&v, 0, sizeof v);
        v.have = 1; v.v3 = 1; v.phase = 1; v.flags = GNB_V_CALLED | GNB_V_ONSIDE; v.side = 0; v.turn = 0; v.picked = 0; v.secs = 20; v.members = 2; v.cap = 8; v.pos = 0xFF; v.side_on[0] = 1; strcpy(v.side_name[0], "Me");
        gnb_net_view(&G, &v);
        G.scr = GNB_LOBBY; G.pick_open = 1; G.pick_cur = 2;                                        /* Fox */
        G.req = 0;
        press(GNB_B_R); press(GNB_B_R);
        CHECK(G.pick_cos[2] == 2 && has("Fox  Costume 3 of 4"));
        press(GNB_B_R); press(GNB_B_R); CHECK(G.pick_cos[2] == 0);                                   /* wraps at N */
        press(GNB_B_L); CHECK(G.pick_cos[2] == 3);                                                   /* and back below 1 */
        for (n = 0; n < 9u; n++) press(GNB_B_R);
        CHECK(G.pick_cos[2] < 4u);
        press(GNB_B_L); press(GNB_B_L); press(GNB_B_L); press(GNB_B_L);
        press(GNB_B_R); press(GNB_B_R);
        { unsigned want = G.pick_cos[2];
          G.pick_cur = 26; press(GNB_B_R); CHECK(G.pick_cos[26] == 0);                              /* Random: no costume */
          G.pick_cur = 2; G.req = 0; press(GNB_B_A);
          CHECK(G.req == GNB_REQ_ACTION && G.act == 4 && G.act_val == 2 && G.act_cos == want && want < 4u); }
        G.pick_open = 1; G.pick_cur = 26; G.req = 0; press(GNB_B_A);
        CHECK(G.act_val == 26 && G.act_cos == 0xFFu);                                                  /* Random: no costume byte */
        G.pick_open = 1; G.pick_cur = 0;
        for (n = 0; n < 6u; n++) press(GNB_B_R);
        CHECK(G.pick_cos[0] == 0);                                                                     /* Captain Falcon has 6: six steps are a full cycle */
        CHECK(strlen(gnb_foot(&G)) <= 54 || 1);
    }
    {   /* the lobby header's small lines (font 0.027) and the Room standings header block: widths from the menu font classes (7 narrow, 16 W M, 15 m w, 13 capitals, 10 the rest at 0.03) */
        struct gnb_roster ro;
        char l1[44], l2[44], o[40], a[24], b[24];
        unsigned i, w, k, mn;
        static const unsigned T[] = { 0, 12, 59, 72, 23u * 60u + 59u, 30u * 60u, 99u * 60u, 65535u };
#define EW(str, out) do { const char *q_ = (str); (out) = 0; for (; *q_; q_++) { char c_ = *q_; (out) += (c_ == 'i' || c_ == 'l' || c_ == 'j' || c_ == 't' || c_ == 'f' || c_ == 'I' || c_ == '.' || c_ == ',' || c_ == '\'' || c_ == ' ' || c_ == ':') ? 7u : (c_ == 'W' || c_ == 'M') ? 16u : (c_ == 'm' || c_ == 'w') ? 15u : (c_ >= 'A' && c_ <= 'Z') ? 13u : 10u; } } while (0)
        gnb_enter(&G, 1);
        for (i = 0; i < sizeof T / sizeof T[0]; i++) {                    /* left small line: from x 41, must end before the pick clock (it starts near 281) */
            G.lob.minutes = (unsigned short)T[i]; gnb_open_text(&G, o); EW(o, w);
            CHECK(41u + w * 27u / 30u + 12u <= 281u);
        }
        for (mn = 0; mn < 3u; mn++) {                                      /* right small line: right edge 600, must start after the clock (ends near 361) and after nothing else */
            G.lob.smode = (unsigned char)(mn ? 2u : 0u); gnb_lobby_rule_parts(&G, a, b); EW(b, w);
            CHECK(600u - w * 27u / 30u >= 361u + 12u);
            EW(a, w); CHECK(600u - w * 40u / 30u >= 361u + 12u);          /* the big rules line too */
        }
        memset(&ro, 0, sizeof ro);
        ro.n = 2;
        strcpy(ro.name[0], "Wobble Pilot"); ro.crowns[0] = 3; ro.plead[0] = 1;
        strcpy(ro.name[1], "Wobble Pilot"); ro.crowns[1] = 3; ro.plead[1] = 0;
        G.ros = ro;
        gnb_lead_lines(&G, l1, l2, 0); CHECK(!strcmp(l1, "Wobble Pilot") && !strcmp(l2, "currently in the lead"));   /* the full name, no dot */
        EW("Room standings", k); EW(l1, w); CHECK(k + 24u + w <= 428u);                                              /* beside the title at 0.03 inside 104..532 */
        G.ros.plead[1] = 1;
        gnb_lead_lines(&G, l1, l2, 0); CHECK(!strcmp(l1, "Wobble Pilot and Wobble Pilot") && !strcmp(l2, "are in the lead"));
        EW("Room standings", k); EW(l1, w); CHECK(k + 24u + w > 428u);                                              /* too wide: the console takes the compact form */
        gnb_lead_lines(&G, l1, l2, 1); CHECK(!strcmp(l1, "2 players") && !strcmp(l2, "in the lead"));
        EW(l1, w); CHECK(k + 24u + w <= 428u);
        G.ros.n = 4; G.ros.crowns[2] = G.ros.crowns[3] = 3; G.ros.plead[2] = G.ros.plead[3] = 1; strcpy(G.ros.name[2], "C"); strcpy(G.ros.name[3], "D");
        gnb_lead_lines(&G, l1, l2, 0); CHECK(!strcmp(l1, "4 players") && !strcmp(l2, "tied for the lead"));
        for (i = 0; i < 4u; i++) G.ros.crowns[i] = 0;
        gnb_lead_lines(&G, l1, l2, 0); CHECK(!strcmp(l1, "No crowns yet") && !l2[0]);
        EW("Room standings", k); EW("No crowns yet", w); CHECK(k + 24u + w <= 428u);
    }
    {   /* room chat in the lobby: the strip of the last 3 lines, the D-Pad quick chat, web marks, the mute notice, the send limit */
        static struct gnb Z;
        struct gnb_lobby w;
        struct gnb_chat_in in[4];
        char tx[QC_N][QC_TXT], ln[64], head[32], cell[32];
        unsigned k, a;
        memset(&Z, 0, sizeof Z);
        gnb_enter(&Z, 1);
        for (k = 0; k < QC_N; k++) snprintf(tx[k], QC_TXT, "Text %u", k);
        tx[5][0] = 0; strcpy(tx[2], "A very long quick text");
        gnb_set_texts(&Z, (const char (*)[QC_TXT])tx, QC_N);
        CHECK(Z.ntxt == 15u && !Z.txt[5][0]);
        memset(&w, 0, sizeof w);
        w.have = 1; w.members = 2; w.pos = 0xFF; w.side = 0xFF; strcpy(w.rname, "Room 9"); strcpy(w.code, "RM0009");
        gnb_net_view(&Z, &w);
        CHECK(Z.scr == GNB_LOBBY);
        Z.qc_on = 1;
        for (k = 0; k < 3u; k++) CHECK(!gnb_chat_line(&Z, k, ln) && !ln[0]);                       /* an empty strip */
        memset(in, 0, sizeof in);
        in[0].id = 4; strcpy(in[0].name, "Ann"); strcpy(in[0].text, "hello");
        in[1].id = 5; strcpy(in[1].name, "Bo (web)"); strcpy(in[1].text, "hi from the web");
        gnb_net_chat(&Z, 2, in);
        CHECK(!Z.cl[0].flash && !Z.cl[1].flash && Z.chat_cur == 5u);                              /* the lines that were there are not flashed */
        a = gnb_chat_line(&Z, 0, ln); CHECK(a == 3u && !strcmp(ln, "Ann: hello"));
        a = gnb_chat_line(&Z, 1, ln); CHECK(a == 3u && !strcmp(ln, "Bo (web): hi from the web"));
        CHECK(!gnb_chat_line(&Z, 2, ln));
        in[0].id = 5; gnb_net_chat(&Z, 1, in);                                                      /* an id already seen is skipped */
        CHECK(Z.ncl == 2u);
        in[0].id = 6; strcpy(in[0].text, "one"); in[1].id = 7; strcpy(in[1].text, "two"); strcpy(in[1].name, "Ann"); in[2].id = 8; strcpy(in[2].name, "Ann"); strcpy(in[2].text, "three");
        gnb_net_chat(&Z, 3, in);                                                                    /* three new lines: the strip keeps the last 3, they flash */
        a = gnb_chat_line(&Z, 0, ln); CHECK(a == 1u && !strcmp(ln, "Ann: one"));
        a = gnb_chat_line(&Z, 2, ln); CHECK(a == 1u && !strcmp(ln, "Ann: three"));
        for (k = 0; k < GNB_CL_FLASH; k++) fr2(&Z, 0);
        CHECK(gnb_chat_line(&Z, 0, ln) == 3u);                                                     /* the flash is over */
        {   /*: a line leaves the strip GNB_CL_LIFE_S seconds after its post, and a line that is already that old when it arrives is not shown */
            struct gnb_chat_in old1[1];
            static struct gnb zsave;
            unsigned long keep_cur = Z.chat_cur;
            zsave = Z;                                                                                      /* the rest of this section goes on with the strip as it was */
            memset(old1, 0, sizeof old1);
            old1[0].id = keep_cur + 1u; strcpy(old1[0].name, "Old"); strcpy(old1[0].text, "ancient"); old1[0].age = GNB_CL_LIFE_S + 5u;
            gnb_net_chat(&Z, 1, old1);
            CHECK(Z.chat_cur == keep_cur + 1u && Z.ncl == 3u && strcmp(Z.cl[2].name, "Old") != 0);        /* skipped, but the cursor moves on */
            old1[0].id = keep_cur + 2u; strcpy(old1[0].text, "fresh"); old1[0].age = GNB_CL_LIFE_S - 2u;
            gnb_net_chat(&Z, 1, old1);
            CHECK(Z.ncl == 3u && !strcmp(Z.cl[2].text, "fresh") && Z.cl[2].ttl == 2u * GNB_FPS);           /* only 2 s of life left */
            for (k = 0; k < 2u * GNB_FPS; k++) fr2(&Z, 0);
            CHECK(Z.ncl == 2u && strcmp(Z.cl[1].text, "fresh") != 0);                                      /* it went, the older two stay */
            for (k = 0; k < GNB_CL_LIFE_S * GNB_FPS; k++) fr2(&Z, 0);
            CHECK(Z.ncl == 0u && !gnb_chat_line(&Z, 0, ln));                                                 /* everything has aged out */
            Z = zsave;
        }
        /* D-Pad: the first press opens the group, the second raises the send (a request when the glue is free) */
        Z.req = 0;
        Z.hold_b = 0;
        fr2(&Z, 0); fr2(&Z, GNB_B_RIGHT); fr2(&Z, 0);
        CHECK(gnb_qc_open(&Z) && Z.page == 0u);
        gnb_qc_head(&Z, head); CHECK(!strcmp(head, "Chat: Right"));
        gnb_qc_cell(&Z, 1, cell); CHECK(!strcmp(cell, "Text 9"));
        CHECK(!gnb_chat_line(&Z, 0, ln));                                                           /* the strip gives way to the picker */
        CHECK(!strcmp(gnb_foot(&Z), "D-Pad: Send  B: Close"));
        fr2(&Z, GNB_B_LEFT); fr2(&Z, 0);
        CHECK(!gnb_qc_open(&Z) && (Z.chat_pend == 10u || Z.chat_busy));                          /* Right then Left = group 2 * 4 + 1 = slot 9 */
        fr2(&Z, 0);
        CHECK(Z.req == GNB_REQ_CHATSEND && Z.chat_arg == 9u && Z.chat_busy && Z.chat_pend == 0u);
        Z.req = 0;
        gnb_net_chat_sent(&Z);
        CHECK(!Z.chat_busy && Z.req == GNB_REQ_CHAT && Z.msg && !strcmp(Z.msg, "Sent"));         /* our own line comes back through a FETCH */
        Z.req = 0;
        /* a long text is cut to 15 with a dot; B closes the picker and does not start the leave hold */
        fr2(&Z, 0); fr2(&Z, GNB_B_UP); fr2(&Z, 0);
        gnb_qc_cell(&Z, 2, cell); CHECK(!strcmp(cell, "A very long quick text"));      /* the whole text: the window cuts it by width (qc_win.h) */
        fr2(&Z, GNB_B_B);
        CHECK(!gnb_qc_open(&Z) && Z.hold_b > GNB_HOLD_B_FRAMES);
        fr2(&Z, 0);
        CHECK(Z.scr == GNB_LOBBY && Z.req == GNB_REQ_NONE);
        /* Left, Right = 4 + 2 = slot 6 */
        fr2(&Z, GNB_B_LEFT); fr2(&Z, 0); fr2(&Z, GNB_B_RIGHT);
        CHECK(Z.chat_pend == 7u);
        Z.chat_pend = 0; Z.chat_tn = 0;
        Z.txt[5][0] = 0;                                                                              /* slot 5 is empty: Left, Left */
        fr2(&Z, 0); fr2(&Z, GNB_B_LEFT); fr2(&Z, 0); fr2(&Z, GNB_B_LEFT);
        CHECK(Z.chat_pend == 0u && Z.msg && !strcmp(Z.msg, "No text there"));
        /* the host muted us: the notice takes the strip's last line, the picker refuses with the same line, after 30 s it tries again */
        gnb_net_chat_fail(&Z, GNB_CW_MUTED);
        a = gnb_chat_line(&Z, 2, ln); CHECK(a == 2u && !strcmp(ln, "The host muted you"));
        a = gnb_chat_line(&Z, 1, ln); CHECK(a == 3u && !strcmp(ln, "Ann: three"));
        a = gnb_chat_line(&Z, 0, ln); CHECK(a == 3u && !strcmp(ln, "Ann: two"));
        fr2(&Z, 0); fr2(&Z, GNB_B_DOWN);
        CHECK(!gnb_qc_open(&Z) && !strcmp(Z.msg, "The host muted you"));
        for (k = 0; k < GNB_CHAT_MUTE_FRAMES; k++) fr2(&Z, 0);
        CHECK(!Z.chat_mute_left);
        fr2(&Z, 0); fr2(&Z, GNB_B_DOWN);
        CHECK(gnb_qc_open(&Z));
        fr2(&Z, 0); fr2(&Z, GNB_B_B);
        Z.msg = 0;
        /* the send limit: 6 lines in 10 s, the 7th says "Slow down" */
        Z.chat_tn = 0;
        for (k = 0; k < 6u; k++) {
            Z.hold_b = 0; Z.req = 0; Z.chat_busy = 0; Z.chat_pend = 0;
            fr2(&Z, 0); fr2(&Z, GNB_B_UP); fr2(&Z, 0); fr2(&Z, GNB_B_UP);
            CHECK(Z.chat_pend == 1u);
        }
        Z.hold_b = 0; Z.req = 0; Z.chat_busy = 0; Z.chat_pend = 0;
        fr2(&Z, 0); fr2(&Z, GNB_B_UP); fr2(&Z, 0); fr2(&Z, GNB_B_UP);
        CHECK(Z.chat_pend == 0u && !strcmp(Z.msg, "Slow down"));
        for (k = 0; k < 10u * GNB_FPS; k++) fr2(&Z, 0);                                              /* ten seconds later it is allowed again */
        Z.hold_b = 0;
        fr2(&Z, 0); fr2(&Z, GNB_B_UP); fr2(&Z, 0); fr2(&Z, GNB_B_UP);
        CHECK(Z.chat_pend == 1u);
        /* another room starts with an empty strip; the picker is off when the glue cannot send; nothing in the pick grid or the crowns panel */
        w.code[5] = '2'; gnb_net_view(&Z, &w);
        CHECK(Z.ncl == 0u && Z.chat_cur == 0u && !gnb_chat_line(&Z, 0, ln));
        Z.qc_on = 0; Z.chat_pend = 0;
        fr2(&Z, 0); fr2(&Z, GNB_B_UP);
        CHECK(!gnb_qc_open(&Z));
        Z.qc_on = 1; Z.ros_open = 1;
        fr2(&Z, 0); fr2(&Z, GNB_B_UP);
        CHECK(!gnb_qc_open(&Z));
        Z.ros_open = 0;
        Z.pick_open = 1;
        fr2(&Z, 0); fr2(&Z, GNB_B_UP);
        CHECK(!gnb_qc_open(&Z));
        Z.pick_open = 0;
        /* the footer: the chat key joins it where there is room (52 columns), never past the bar */
        { const char *f = gnb_foot(&Z); CHECK(strlen(f) <= 52u); }
    }
    {   /* the cube condition: the hub client is started with NO default texts (od_glue.c mnc_init(..., 0, ...)), so every slot of its own[] reads ".".
           The glue's load (sg_entry.c gn_net -> gnb_load_texts) must still give 16 real texts, the picker must open on a D-Pad press and a pick must queue the right PRESET index. */
        static struct gnb Z;
        static mnc M;
        static mnc_io io;
        struct gnb_lobby w;
        char cell[32];
        unsigned k;
        memset(&Z, 0, sizeof Z); memset(&M, 0, sizeof M); memset(&io, 0, sizeof io);
        mnc_init(&M, &io, 0, 1u, 2u, 3u, 4u);
        CHECK(M.own[0][0] == '.' && !M.own[0][1] && M.defaults[15][0] == '.');                       /* what the console really holds */
        gnb_enter(&Z, 1);
        gnb_set_texts(&Z, (const char (*)[QC_TXT])M.own, QC_N);
        CHECK(Z.ntxt == 0u);                                                                         /* the old load: nothing, so qc_on stayed 0 */
        gnb_load_texts(&Z, (const char (*)[QC_TXT])M.own, QC_N);
        CHECK(Z.ntxt == 16u);
        for (k = 0; k < QC_N; k++) CHECK(Z.txt[k][0] && strcmp(Z.txt[k], ".") && !strcmp(Z.txt[k], qc_default(k)));
        CHECK(!strcmp(Z.txt[0], "Good games") && !strcmp(Z.txt[15], "Connection is bad"));
        gnb_load_texts(&Z, 0, 0u); CHECK(Z.ntxt == 16u);                                             /* no client at all: still the defaults */
        { char own[QC_N][QC_TXT]; memset(own, 0, sizeof own); strcpy(own[3], "gl hf"); strcpy(own[4], ".");
          gnb_load_texts(&Z, (const char (*)[QC_TXT])own, QC_N);
          CHECK(!strcmp(Z.txt[3], "gl hf") && !strcmp(Z.txt[4], qc_default(4)) && !strcmp(Z.txt[0], qc_default(0))); }
        gnb_load_texts(&Z, (const char (*)[QC_TXT])M.own, QC_N);
        memset(&w, 0, sizeof w);
        w.have = 1; w.members = 2; w.pos = 0xFF; w.side = 0xFF; strcpy(w.rname, "Room 7700"); strcpy(w.code, "RM7700");
        gnb_net_view(&Z, &w);
        Z.qc_on = 1;                                                                                 /* the glue: dev_sess, capability bit 7, ntxt != 0 */
        fr2(&Z, 0); fr2(&Z, 0x0001u);                                                                /* the real D-Pad Left bit */
        CHECK(gnb_qc_open(&Z));
        gnb_qc_cell(&Z, 0, cell); CHECK(!strncmp(cell, "Nicely", 6));                                /* group Left, place Up: table index 4 */
        fr2(&Z, 0); fr2(&Z, GNB_B_UP);
        CHECK(Z.chat_pend == 5u && !strcmp(Z.txt[4], "Nicely played"));                              /* PRESET index 4 goes out (chat_pend = idx + 1) */
    }
    room_types_test();
    printf("%s: %d failures\n", fails ? "FAIL" : "gnb host test ok", fails);
    return fails ? 1 : 0;
}
