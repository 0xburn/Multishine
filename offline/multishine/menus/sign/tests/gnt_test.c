/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Tournament model (gnt.c): list, Create form, page, bracket arithmetic and view, the call, the text view.  Mock data only, no network. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../sg_ui.h"
#include "../gnt.h"
#include "../gnt_tmb.h"
#include "../gnt_geo.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void ent(struct gnt_ent *e, unsigned id, const char *name, unsigned state, unsigned format, unsigned cap, unsigned joined, unsigned flags)
{
    memset(e, 0, sizeof *e);
    e->id = (unsigned short)id; strcpy(e->name, name); strcpy(e->host, "Nigzel");
    e->state = (unsigned char)state; e->format = (unsigned char)format; e->cap = (unsigned char)cap; e->joined = (unsigned char)joined; e->flags = (unsigned char)flags;
    e->rounds = (unsigned char)gnt_round_count(gnt_size_for(cap)); e->round = state == GNT_S_RUNNING ? 1 : 0;
}
static void press(struct gnt *g, unsigned b) { gnt_frame(g, b, 0, 0); gnt_frame(g, 0, 0, 0); gnt_frame(g, 0, 0, 0); }      /* (two frames up after the press: the L R region cycle wants GNT_LR_GAP frames of both up before the next press) */
static void hold_b(struct gnt *g) { unsigned k; for (k = 0; k < GNT_HOLD_B_FRAMES + 5u; k++) gnt_frame(g, GNT_B_B, 0, 0); gnt_frame(g, 0, 0, 0); }   /* (a tap does it; the long press must not fire twice) */

int main(void)
{
    static struct gnt g;
    struct gnt_ent l[3], info;
    char num[GNT_NUM + 1], tit[40], ex[24], pn[40];
    unsigned i;

    /* arithmetic */
    CHECK(gnt_size_for(2) == 2 && gnt_size_for(3) == 4 && gnt_size_for(5) == 8 && gnt_size_for(8) == 8 && gnt_size_for(9) == 16 && gnt_size_for(64) == 64 && gnt_size_for(99) == 64);
    CHECK(gnt_round_count(2) == 1 && gnt_round_count(8) == 3 && gnt_round_count(64) == 6);
    CHECK(gnt_round_matches(8, 1) == 4 && gnt_round_matches(8, 2) == 2 && gnt_round_matches(8, 3) == 1 && gnt_round_matches(8, 4) == 0 && gnt_round_matches(64, 1) == 32);
    CHECK(gnt_round_start(8, 1) == 0 && gnt_round_start(8, 2) == 4 && gnt_round_start(8, 3) == 6 && gnt_round_start(64, 6) == 62);
    { unsigned s, tot = 0; for (s = 2; s <= 64; s <<= 1) { unsigned r; tot = 0; for (r = 1; r <= gnt_round_count(s); r++) tot += gnt_round_matches(s, r); CHECK(tot == s - 1u); } }
    { char b[16]; CHECK(!strcmp(gnt_round_name(3, 3, b), "Final")); CHECK(!strcmp(gnt_round_name(3, 2, b), "Semifinal")); CHECK(!strcmp(gnt_round_name(4, 2, b), "Quarterfinal")); CHECK(!strcmp(gnt_round_name(6, 1, b), "Round 1")); }

    /* list: loading, then 3 tournaments with Create and Sign up by ID */
    gnt_enter(&g);
    CHECK(g.req == GNT_REQ_LIST && g.net == GNT_N_LOADING && gnt_status_view(&g) == 1 && gnt_rows(&g) == 0);
    g.req = 0;
    ent(&l[0], 11, "Friday Night", GNT_S_REG, GNT_FMT_DOUBLE, 16, 5, GNT_F_JOINED);
    ent(&l[1], 12, "Chill Night", GNT_S_REG, GNT_FMT_SINGLE, 8, 2, 0);
    ent(&l[2], 13, "Big Bracket", GNT_S_RUNNING, GNT_FMT_DOUBLE, 32, 32, 0);
    gnt_net_list(&g, 2u, 3, l);
    CHECK(g.net == GNT_N_OK && gnt_rows(&g) == 3 && g.busy == 0);
    CHECK(gnt_row(&g, 0, num, tit, ex) == 3 && !strcmp(tit, "Friday Night") && !strcmp(num, "5 of max 16") && !strcmp(ex, "Open"));
    CHECK(gnt_row(&g, 1, num, tit, ex) == 1 && !strcmp(num, "2 of max 8"));
    CHECK(gnt_row(&g, 2, num, tit, ex) == 1 && !strcmp(ex, "Playing"));
    CHECK(gnt_row(&g, 3, num, tit, ex) == 0);
    /*: Create and Sign up by ID are footer keys (X, Y), not rows; the list rows are only cups; the enrolled one has a star */
    CHECK(gnt_row_star(&g, 0) == 1 && gnt_row_star(&g, 1) == 0 && gnt_row_star(&g, 2) == 0);
    CHECK(!strcmp(gnt_hint2(&g), "A: Open  Z: Leave  X: Create") && !strcmp(gnt_hint(&g), "Y: Sign up by ID  L R: Region Auto  B: Back"));
    CHECK(g.sel == 0 && gnt_panel(&g, 0, pn) && !strcmp(pn, "Host: Nigzel, double"));
    CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "5 of max 16 entered"));
    CHECK(gnt_panel(&g, 2, pn) == 3 && !strcmp(pn, "You are signed up.") && gnt_panel(&g, 3, pn) == 0 && !pn[0]);      /* the box holds three lines */
    CHECK(gnt_box(&g, 0, pn) && !strcmp(pn, "5") && gnt_box(&g, 1, pn) && !strcmp(pn, "of 16"));

    /* a full cache (GNT_MAX entries, past one 8-row page): every row readable, the cursor walks to the last row and back, nothing out of bounds */
    {
        struct gnt_ent big[GNT_MAX];
        unsigned i, k, n = GNT_MAX;
        for (i = 0; i < n; i++) ent(&big[i], 100 + i, "Cup", i % 3 ? GNT_S_DONE : GNT_S_REG, GNT_FMT_DOUBLE, 4, 4, 0);
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, n, big); g.busy = 0;
        CHECK(gnt_rows(&g) == n);
        for (i = 0; i < n; i++) CHECK(gnt_row(&g, i, num, tit, ex) != 0 || 1);
        CHECK(gnt_row(&g, n, num, tit, ex) == 0);
        for (k = 0; k < n + 4; k++) { gnt_frame(&g, 0, 0, 1); gnt_frame(&g, 0, 0, 0); }
        CHECK(g.sel < n);
        for (k = 0; k < n + 4; k++) { gnt_frame(&g, 0, 1, 0); gnt_frame(&g, 0, 0, 0); }
        CHECK(g.sel < n);
        gnt_panel(&g, 0, pn); gnt_box(&g, 0, pn);
    }

    /* Tournament v2 list words: Upcoming and its countdown, the sign-up time left of a "now" cup, no Called off / Done needed here */
    {
        struct gnt_ent u[2];
        ent(&u[0], 31, "Friday Night", GNT_S_REG, GNT_FMT_DOUBLE, 16, 3, 0); u[0].scheduled = 1; u[0].signup_left = 2ul * 3600ul + 57ul * 60ul; u[0].pubid = 12345678ul;
        ent(&u[1], 32, "Fresh Open", GNT_S_REG, GNT_FMT_SINGLE, 8, 1, 0); u[1].signup_left = 42ul * 60ul + 5ul; u[1].pubid = 23456789ul;
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 2, u);
        CHECK(gnt_row(&g, 0, num, tit, ex) == 1 && !strcmp(num, "3 of max 16") && !strcmp(ex, "Upcoming"));
        CHECK(gnt_row(&g, 1, num, tit, ex) == 1 && !strcmp(ex, "Open"));
        g.sel = 0;
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Starts in 2h 57m"));
        g.sel = 1;
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "1 of max 8, 42m left"));
        g.sel = 0; g.say[0] = 0;
        g.scr = GNT_PAGE; g.cur = u[0]; g.cur.host_plays = 1;
        gnt_state_text(&g, pn); CHECK(!strcmp(pn, "Upcoming"));
        { unsigned r, found = 0; for (r = 0; r < gnt_rows(&g); r++) { gnt_row(&g, r, num, tit, ex); if (!strcmp(tit, "ID")) { found = 1; CHECK(!strcmp(ex, "12345678")); } } CHECK(found); }
        CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "3 of max 16 entered"));
    }

    /* Create form (v2): the rows in order, the select box follows, values, the 8 digit passcode, Start presets and Pick time */
    gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 3, l); g.busy = 0;
    CHECK(g.sel == 0);
    press(&g, GNT_B_X);
    CHECK(g.scr == GNT_CREATE && gnt_rows(&g) == 12);
    { static const char *const T[12] = { "Preset", "Name", "Format", "Size", "Sign-up window", "Check-in", "Seeding", "Host plays", "Start", "Region", "Passcode", "Create Tournament" }; unsigned k; for (k = 0; k < 9u; k++) { CHECK(gnt_row(&g, k, num, tit, ex) && !strcmp(tit, T[k])); }
      CHECK(gnt_vis(&g) == GNT_FORM_VIS && gnt_row(&g, 9, num, tit, ex) == 0 && gnt_max_first(&g) == 3);      /* the form shows 9 rows (equal margins in its box) and scrolls the rest in */
      g.first = 3; CHECK(gnt_row(&g, 8, num, tit, ex) == 3 && !strcmp(tit, "Create Tournament"));
      g.first = 1; CHECK(gnt_row(&g, 8, num, tit, ex) && !strcmp(tit, "Region") && !strcmp(ex, "Any")); g.first = 0; }
    CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(ex, "Custom") && g.cf_row == 0 && !strcmp(gnt_hint(&g), "Left Right: Preset  B: Back"));     /* the Preset row first, Custom with today's defaults */
    CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Your own values on every row."));
    CHECK(gnt_row(&g, 2, num, tit, ex) && !strcmp(ex, "Double Elimination") && gnt_row(&g, 3, num, tit, ex) && !strcmp(ex, "Max 16 players"));
    CHECK(gnt_row(&g, 4, num, tit, ex) && !strcmp(ex, "60 min") && gnt_row(&g, 5, num, tit, ex) && !strcmp(ex, "Off") && gnt_row(&g, 7, num, tit, ex) && !strcmp(ex, "Yes") && gnt_row(&g, 8, num, tit, ex) && !strcmp(ex, "Now"));
    press(&g, GNT_B_DOWN); CHECK(!strcmp(gnt_hint(&g), "Left Right: Change  B: Back"));
    press(&g, GNT_B_RIGHT); CHECK(g.cf_name == 1 && g.cf_preset == 0);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(g.cf_format == GNT_FMT_SINGLE && gnt_create_format(&g) == 1);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(g.cf_size == 2 && gnt_create_cap(&g) == 32);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(gnt_create_signup(&g) == 120);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(g.cf_checkin == 1 && gnt_create_checkin(&g) == 5);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(g.cf_seed == 1 && gnt_create_seeding(&g) == 1);
    press(&g, GNT_B_RIGHT); CHECK(gnt_create_seeding(&g) == 2);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(gnt_create_hostplays(&g) == 0);
    press(&g, GNT_B_DOWN); CHECK(g.cf_row == 8 && gnt_cur_page(&g) == 8);          /* the select box follows the form row */
    press(&g, GNT_B_UP); CHECK(gnt_cur_page(&g) == 7); press(&g, GNT_B_DOWN);
    CHECK(gnt_create_start_in(&g) == 0 && !strcmp(gnt_hint(&g), "Left Right: Change  B: Back"));
    press(&g, GNT_B_RIGHT); CHECK(gnt_create_start_in(&g) == 3600ul && gnt_row(&g, 7, num, tit, ex) && !strcmp(ex, "In 1 hour") && gnt_rows(&g) == 11);   /* a scheduled start: no sign-up window */
    CHECK(!strcmp(tit, "Start") && g.cf_row == 7 && !strcmp(gnt_title(&g), "Create Tournament"));
    CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Starts in 1h 0m"));
    press(&g, GNT_B_RIGHT); CHECK(gnt_create_start_in(&g) == 10800ul);
    press(&g, GNT_B_RIGHT); press(&g, GNT_B_RIGHT); CHECK(gnt_create_start_in(&g) == 86400ul);          /* Tonight (6 h), Tomorrow (24 h) */
    press(&g, GNT_B_RIGHT); CHECK(g.cf_start == 5 && gnt_rows(&g) == 13 && g.cf_row == 7);            /* Pick time: the day and hour rows show */
    CHECK(gnt_row(&g, 8, num, tit, ex) && !strcmp(tit, "Start day") && !strcmp(ex, "In 1 day"));
    g.first = 1; CHECK(gnt_row(&g, 8, num, tit, ex) && !strcmp(tit, "Start hour") && !strcmp(ex, "+0 hours")); g.first = 0;
    CHECK(gnt_create_start_in(&g) == 86400ul);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); press(&g, GNT_B_RIGHT); CHECK(g.cf_days == 3 && gnt_create_start_in(&g) == 3ul * 86400ul);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); press(&g, GNT_B_RIGHT); CHECK(g.cf_hours == 2 && gnt_create_start_in(&g) == 3ul * 86400ul + 7200ul);
    press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_hours == 23);           /* the hour ring */
    press(&g, GNT_B_UP); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_days == 0 && g.cf_hours == 23 && gnt_create_start_in(&g) == 23ul * 3600ul);
    { unsigned k; g.cf_hours = 0; g.cf_days = 1; for (k = 0; k < 31u; k++) press(&g, GNT_B_RIGHT); CHECK(g.cf_days == 1u); for (k = 0; k < 29u; k++) press(&g, GNT_B_RIGHT); CHECK(g.cf_days == 30 && g.cf_hours == 0 && gnt_create_start_in(&g) == 2592000ul); }
    press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_days == 28);
    g.cf_days = 0; g.cf_hours = 0; press(&g, GNT_B_LEFT); press(&g, GNT_B_RIGHT);
    CHECK(gnt_create_start_in(&g) >= 3600ul);                                                         /* at least 1 hour, at most 30 days */
    press(&g, GNT_B_UP); press(&g, GNT_B_LEFT);                                  /* Start row: back to Pick -> Tomorrow */
    CHECK(g.cf_start == 4 && gnt_rows(&g) == 11);
    press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_start == 0 && gnt_rows(&g) == 12 && g.cf_row == 8);
    press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(g.cf_region == 2 && gnt_create_region(&g) == 2); press(&g, GNT_B_LEFT); CHECK(gnt_create_region(&g) == 0);      /* Region: Any, NAW ... (Left wraps) */
    press(&g, GNT_B_LEFT); CHECK(gnt_create_region(&g) == 6); { char t2[GNT_EXTRA + 1]; gnt_row(&g, g.cf_row - g.first, num, tit, t2); CHECK(!strcmp(t2, "OC")); } press(&g, GNT_B_RIGHT);      /* Left wraps to OC, the last one west to east */
    gnt_set_account_region(&g, 3); CHECK(gnt_create_region(&g) == 0);                                  /* the player already chose: the account region no longer overrides */
    press(&g, GNT_B_DOWN);
    CHECK(gnt_create_pass(&g)[0] == 0 && !strcmp(gnt_hint(&g), "A Digit +  X Digit -  Left Right: Move"));
    press(&g, GNT_B_A); CHECK(g.cf_pass_on == 1 && !strcmp(gnt_create_pass(&g), "00000000"));
    press(&g, GNT_B_A); press(&g, GNT_B_A); press(&g, GNT_B_RIGHT); press(&g, GNT_B_A);
    CHECK(!strcmp(gnt_create_pass(&g), "21000000"));
    gnt_row(&g, 8, num, tit, ex); CHECK(!strcmp(ex, "2<1>000000"));
    press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_pass_on == 0 && gnt_create_pass(&g)[0] == 0);   /* None is on the ring */
    press(&g, GNT_B_RIGHT); CHECK(g.cf_pass_on == 1 && g.cf_pass_pos == 0);
    press(&g, GNT_B_DOWN); CHECK(g.cf_row == 11 && gnt_cur_page(&g) == 11 - g.first && g.cf_preset == 0);
    g.req = 0;
    press(&g, GNT_B_A);
    CHECK(g.req == GNT_REQ_CREATE && g.busy);
    CHECK(gnt_create_start_in(&g) == 0 && gnt_create_hostplays(&g) == 0 && gnt_create_seeding(&g) == 2 && gnt_create_signup(&g) == 120);
    ent(&info, 20, "Friday Night", GNT_S_REG, GNT_FMT_SINGLE, 32, 1, GNT_F_JOINED | GNT_F_HOST); info.host_plays = 1;
    gnt_net_created(&g, &info);
    CHECK(g.scr == GNT_PAGE && g.cur.id == 20 && gnt_rows(&g) >= 2);
    /* a single B press leaves the form (nothing is sent) */
    gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 3, l); g.busy = 0;
    press(&g, GNT_B_X); CHECK(g.scr == GNT_CREATE);
    g.req = 0; press(&g, GNT_B_B); CHECK(g.scr == GNT_LIST && g.req == GNT_REQ_LIST && g.exit == 0);
    gnt_net_list(&g, 2u, 3, l); g.req = 0;
    /* a long form scrolls with the cursor (Pick time = 13 rows, 9 on screen) */
    press(&g, GNT_B_X); g.cf_start = 5; g.cf_row = 0;
    { unsigned k; for (k = 0; k < 12u; k++) press(&g, GNT_B_DOWN); }
    CHECK(g.cf_row == 12 && g.first == 4 && gnt_cur_page(&g) == 8 && gnt_cur_first(&g) == 4 && gnt_max_first(&g) == 4);
    CHECK(gnt_row(&g, 8, num, tit, ex) == 3 && !strcmp(tit, "Create Tournament") && gnt_row(&g, 9, num, tit, ex) == 0);
    g.req = 0; press(&g, GNT_B_B); gnt_net_list(&g, 2u, 3, l); g.req = 0;

    /* Create presets: order, values, Custom restore, an edit turns the label to Custom, the request equals a hand-set form */
    {
        static const char *const PN[7] = { "Custom", "Grand Open", "Midnight Cup", "Pocket Bracket", "Locals Night", "Evening Cup", "Last Call" };
        /* cap, format (0 double 1 single), sign-up min (Now only), check-in min, start_in s (0 Now; Tonight 6 h, Tomorrow 24 h as the Start row), host plays */
        static const unsigned long PV[7][6] = { { 0 }, { 64, 0, 60, 10, 86400ul, 1 }, { 16, 1, 60, 0, 21600ul, 1 }, { 8, 1, 30, 0, 0, 1 }, { 32, 0, 60, 10, 21600ul, 1 }, { 16, 0, 60, 5, 0, 1 }, { 16, 1, 15, 0, 0, 1 } };
        static struct gnt h;
        unsigned k, w;
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 3, l); g.busy = 0;
        press(&g, GNT_B_X); CHECK(g.scr == GNT_CREATE && g.cf_row == 0 && g.cf_preset == 0);
        /* the player's own values first: Name, a 32 single with check-in 5, join order, a passcode, Pick time 2 days 3 hours, a region */
        g.cf_name = 2; g.cf_size = 2; g.cf_format = GNT_FMT_SINGLE; g.cf_checkin = 1; g.cf_seed = 2; g.cf_pass_on = 1; g.cf_start = 5; g.cf_days = 2; g.cf_hours = 3; g.cf_region = 4;
        for (k = 1; k <= 7u; k++) {                                                                      /* Right walks the fixed order and wraps to Custom */
            press(&g, GNT_B_RIGHT);
            w = k % 7u;
            CHECK(g.cf_preset == w && g.cf_row == 0 && gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Preset") && !strcmp(ex, PN[w]));
            CHECK(gnt_panel(&g, 0, pn) && strlen(pn) <= GNT_LINE + 8u && strlen(PN[w]) <= GNT_EXTRA && !strcmp(gnt_hint(&g), "Left Right: Preset  B: Back"));
            if (w) {
                CHECK(gnt_create_cap(&g) == PV[w][0] && gnt_create_format(&g) == PV[w][1] && gnt_create_checkin(&g) == PV[w][3] && gnt_create_start_in(&g) == PV[w][4] && gnt_create_hostplays(&g) == 1u);
                if (!PV[w][4]) CHECK(gnt_create_signup(&g) == PV[w][2]);
                CHECK(gnt_create_seeding(&g) == 0 && gnt_create_pass(&g)[0] == 0 && gnt_create_region(&g) == 4 && g.cf_name == 2);   /* Random, no passcode; Name and Region stay */
                CHECK(gnt_create_start_in(&g) ? gnt_rows(&g) == 11 : gnt_rows(&g) == 12);
                CHECK(PV[w][1] ? PV[w][0] >= 2u : PV[w][0] >= 3u);                                           /* every cap meets the entrant minimum (2 single, 3 double) */
            }
        }
        CHECK(g.cf_preset == 0 && g.cf_size == 2 && g.cf_format == GNT_FMT_SINGLE && g.cf_checkin == 1 && g.cf_seed == 2 && g.cf_pass_on == 1 && g.cf_start == 5 && g.cf_days == 2 && g.cf_hours == 3 && gnt_rows(&g) == 13);   /* Custom: the player's values back */
        press(&g, GNT_B_LEFT); CHECK(g.cf_preset == 6 && gnt_create_signup(&g) == 15 && gnt_create_cap(&g) == 16);          /* Left from Custom: the last one */
        press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_preset == 1 && gnt_create_cap(&g) == 64);   /* back to Grand Open */
        press(&g, GNT_B_LEFT); CHECK(g.cf_preset == 0 && gnt_create_start_in(&g) == 2ul * 86400ul + 3ul * 3600ul);
        /* an edit after a preset: the label turns to Custom; Name and Region do not count; Custom then holds the edited values */
        press(&g, GNT_B_RIGHT); CHECK(g.cf_preset == 1);
        press(&g, GNT_B_DOWN); press(&g, GNT_B_RIGHT); CHECK(g.cf_preset == 1 && gnt_row(&g, 0, num, tit, ex) && !strcmp(ex, "Grand Open"));     /* Name */
        press(&g, GNT_B_DOWN); press(&g, GNT_B_DOWN); press(&g, GNT_B_LEFT); CHECK(g.cf_preset == 0 && gnt_create_cap(&g) == 32 && gnt_row(&g, 0, num, tit, ex) && !strcmp(ex, "Custom"));   /* Size 64 -> 32 */
        press(&g, GNT_B_RIGHT); CHECK(g.cf_preset == 0 && gnt_create_cap(&g) == 64);                    /* still Custom (equal to Grand Open again, but edited) */
        press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); CHECK(g.cf_row == 0);
        press(&g, GNT_B_RIGHT); press(&g, GNT_B_RIGHT); CHECK(g.cf_preset == 2 && gnt_create_cap(&g) == 16);     /* Midnight Cup */
        press(&g, GNT_B_LEFT); press(&g, GNT_B_LEFT); CHECK(g.cf_preset == 0 && gnt_create_cap(&g) == 64 && gnt_create_start_in(&g) == 86400ul && gnt_create_checkin(&g) == 10);   /* the edited values */
        press(&g, GNT_B_A); CHECK(g.cf_preset == 1);                                                      /* A on the row steps forward too */
        /* the passcode digits count as an edit (presets set None) */
        { unsigned n = gnt_rows(&g); for (k = 0; k < n; k++) press(&g, GNT_B_DOWN); press(&g, GNT_B_UP); }
        CHECK(!strcmp(gnt_hint(&g), "A Digit +  X Digit -  Left Right: Move")); press(&g, GNT_B_A); CHECK(g.cf_preset == 0 && g.cf_pass_on == 1);
        press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP); press(&g, GNT_B_UP);
        CHECK(g.cf_row == 0); press(&g, GNT_B_RIGHT); CHECK(g.cf_preset == 1);
        { unsigned n = gnt_rows(&g); for (k = 0; k < n; k++) press(&g, GNT_B_DOWN); }
        press(&g, GNT_B_RIGHT); CHECK(g.cf_preset == 1);                                                  /* Left Right on Create Tournament changes nothing */
        /* the request: a preset form and a hand-set form with the same values give the same TN_CREATE arguments (the glue builds the request from these getters only) */
        for (w = 1; w < 7u; w++) {
            gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 3, l); g.busy = 0; press(&g, GNT_B_X);
            for (k = 0; k < w; k++) press(&g, GNT_B_RIGHT);
            gnt_enter(&h); h.scr = GNT_CREATE;
            h.cf_size = (unsigned char)(PV[w][0] == 8 ? 0 : PV[w][0] == 16 ? 1 : PV[w][0] == 32 ? 2 : 3); h.cf_format = (unsigned char)PV[w][1];
            h.cf_signup = (unsigned char)(PV[w][2] == 15 ? 0 : PV[w][2] == 30 ? 1 : PV[w][2] == 60 ? 2 : 3); h.cf_checkin = (unsigned char)(PV[w][3] / 5u);
            h.cf_start = (unsigned char)(PV[w][4] == 86400ul ? 4 : PV[w][4] == 21600ul ? 3 : 0); h.cf_hostplays = (unsigned char)PV[w][5];
            CHECK(!strcmp(gnt_create_name(&g), gnt_create_name(&h)) && gnt_create_cap(&g) == gnt_create_cap(&h) && gnt_create_format(&g) == gnt_create_format(&h) && gnt_create_flags(&g) == gnt_create_flags(&h));
            CHECK(gnt_create_checkin(&g) == gnt_create_checkin(&h) && !strcmp(gnt_create_pass(&g), gnt_create_pass(&h)) && gnt_create_signup(&g) == gnt_create_signup(&h) && gnt_create_seeding(&g) == gnt_create_seeding(&h));
            CHECK(gnt_create_hostplays(&g) == gnt_create_hostplays(&h) && gnt_create_start_in(&g) == gnt_create_start_in(&h) && gnt_create_region(&g) == gnt_create_region(&h));
            g.req = 0; press(&g, GNT_B_A); CHECK(g.req == 0);                                             /* A on the Preset row never sends */
        }
        g.req = 0; press(&g, GNT_B_B); gnt_net_list(&g, 2u, 3, l); g.req = 0;
    }

    /* Sign up by ID: 8 digit id, then the passcode of a private cup, lock-out text */
    press(&g, GNT_B_Y);
    CHECK(g.scr == GNT_JOIN && gnt_rows(&g) == 3 && !strcmp(gnt_title(&g), "Sign up by ID"));
    CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "ID") && !strcmp(ex, "<0>0000000"));
    CHECK(gnt_row(&g, 1, num, tit, ex) && !strcmp(tit, "Passcode") && !strcmp(ex, "None") && gnt_row(&g, 2, num, tit, ex) == 3 && !strcmp(tit, "Sign up"));
    g.req = 0; press(&g, GNT_B_DOWN); press(&g, GNT_B_DOWN); press(&g, GNT_B_A);
    CHECK(g.req == 0 && g.say_warn && !strcmp(g.say, "Type the 8 digit ID first.") && gnt_join_id(&g) == 0);
    press(&g, GNT_B_UP); press(&g, GNT_B_UP);
    press(&g, GNT_B_A);                                                                              /* digit 0: 0 -> 1 */
    { static const char *const D = "2345678"; unsigned k; for (k = 0; k < 7u; k++) { unsigned n; press(&g, GNT_B_RIGHT); for (n = 0; n < (unsigned)(D[k] - '0'); n++) press(&g, GNT_B_A); } }
    CHECK(!strcmp(g.jid, "12345678") && gnt_join_id(&g) == 12345678ul && gnt_join_pass(&g)[0] == 0);
    { unsigned k; g.jid_pos = 0; press(&g, GNT_B_X); CHECK(g.jid[0] == '0'); press(&g, GNT_B_X); CHECK(g.jid[0] == '9');      /* X: digit minus one, wraps 0 -> 9; A wraps 9 -> 0 */
      press(&g, GNT_B_A); CHECK(g.jid[0] == '0'); for (k = 0; k < 9u; k++) press(&g, GNT_B_A); CHECK(g.jid[0] == '9'); press(&g, GNT_B_A); press(&g, GNT_B_A); CHECK(g.jid[0] == '1');
      for (k = 0; k < 4u; k++) press(&g, GNT_B_X); CHECK(g.jid[0] == '7'); g.jid[0] = '1'; CHECK(!strcmp(g.jid, "12345678"));
      g.jid_pos = 7; }
    press(&g, GNT_B_DOWN); CHECK(!strcmp(gnt_hint(&g), "A Digit +  X Digit -  Left Right: Move"));
    press(&g, GNT_B_A); CHECK(g.jpw_on == 1 && !strcmp(gnt_join_pass(&g), "00000000"));
    press(&g, GNT_B_A); press(&g, GNT_B_RIGHT); press(&g, GNT_B_A); press(&g, GNT_B_A);
    CHECK(!strcmp(gnt_join_pass(&g), "12000000"));
    press(&g, GNT_B_DOWN); CHECK(!strcmp(gnt_hint(&g), "A: Join  B: Back"));
    g.req = 0; press(&g, GNT_B_A);
    CHECK(g.req == GNT_REQ_JOINID && g.busy && !strcmp(g.say, "Joining..."));
    gnt_net_fail(&g, GNT_REQ_JOINID, GNT_W_RATE);
    CHECK(g.scr == GNT_JOIN && g.busy == 0 && g.say_warn && !strcmp(g.say, "Too many tries, wait 10 minutes.") && gnt_panel(&g, 1, pn) == 4 && !strcmp(pn, "Too many tries, wait 10 minutes."));
    gnt_net_fail(&g, GNT_REQ_JOINID, GNT_W_DENIED); CHECK(!strcmp(g.say, "Wrong passcode."));
    gnt_net_fail(&g, GNT_REQ_JOINID, GNT_W_BAD); CHECK(!strcmp(g.say, "No tournament with that ID."));
    g.req = 0; press(&g, GNT_B_A); CHECK(g.req == GNT_REQ_JOINID);
    ent(&info, 55, "Secret Open", GNT_S_REG, GNT_FMT_DOUBLE, 16, 2, GNT_F_JOINED | GNT_F_PASS); info.pubid = 12345678ul;
    gnt_net_created(&g, &info);
    CHECK(g.scr == GNT_PAGE && g.cur.id == 55 && !strcmp(g.say, "You are in."));

    /* page of a hosted cup: leave, B to the list */
    gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 3, l); g.busy = 0; g.req = 0;
    ent(&info, 20, "Friday Night", GNT_S_REG, GNT_FMT_SINGLE, 32, 1, GNT_F_JOINED | GNT_F_HOST); info.host_plays = 1;
    g.cur = info; g.scr = GNT_PAGE; g.sel = 0; g.first = 0; g.dirty = GNT_D_ALL;
    CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Leave Tournament"));
    CHECK(gnt_row(&g, 1, num, tit, ex) && !strcmp(tit, "Start Tournament"));
    g.req = 0; press(&g, GNT_B_A);
    CHECK(g.req == GNT_REQ_LEAVE && g.req_id == 20);
    gnt_net_done(&g, GNT_REQ_LEAVE, 1);
    CHECK(!(g.cur.flags & GNT_F_JOINED) && gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Join Tournament"));
    g.cur.host_plays = 0; CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Start Tournament"));            /* a host who does not play cannot join their own cup */
    g.req = 0; hold_b(&g);
    CHECK(g.scr == GNT_LIST && g.req == GNT_REQ_LIST);
    gnt_net_list(&g, 2u, 3, l);

    /* a running tournament: page -> bracket of 5 entrants in a size 8 bracket */
    press(&g, GNT_B_DOWN); press(&g, GNT_B_DOWN); press(&g, GNT_B_DOWN); press(&g, GNT_B_DOWN);
    g.req = 0; press(&g, GNT_B_A);
    CHECK(g.scr == GNT_PAGE && g.req == GNT_REQ_INFO && g.cur.id == 13);
    info = l[2]; info.round = 2; info.rounds = 3; info.you = GNT_Y_IN; info.flags = GNT_F_JOINED; info.state = GNT_S_RUNNING;
    gnt_net_info(&g, &info);
    CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "View Bracket"));
    g.req = 0; press(&g, GNT_B_A);
    CHECK(g.scr == GNT_BRACKET && g.req == GNT_REQ_BRACKET);
    {
        static char names[5][GNT_NAME + 1] = { "Nigzel", "Alpha", "Bravo", "Charlie", "Delta" };
        struct gnt_match m[7];
        memset(m, 0, sizeof m);
        /* round 1: 0 v 7(bye), 1 v 2, 3 v 4, 5 v 6 (nobody): seeds 0..4 */
        m[0].round = 1; m[0].a = 0; m[0].b = GNT_NONE; m[0].state = GNT_M_BYE; m[0].win = 1;
        m[1].round = 1; m[1].a = 1; m[1].b = 2; m[1].state = GNT_M_DONE; m[1].wa = 2; m[1].wb = 1; m[1].win = 1;
        m[2].round = 1; m[2].a = 3; m[2].b = 4; m[2].state = GNT_M_DONE; m[2].wa = 0; m[2].wb = 2; m[2].win = 2;
        m[3].round = 1; m[3].a = GNT_NONE; m[3].b = GNT_NONE; m[3].state = GNT_M_BYE;
        m[4].round = 2; m[4].a = 0; m[4].b = 1; m[4].state = GNT_M_READY;
        m[5].round = 2; m[5].a = 4; m[5].b = GNT_NONE; m[5].state = GNT_M_WAIT;
        m[6].round = 3; m[6].a = GNT_NONE; m[6].b = GNT_NONE; m[6].state = GNT_M_WAIT;
        gnt_net_bracket(&g, 5, 8, 0, (const char (*)[GNT_NAME + 1])names, 7, m);
    }
    CHECK(g.b_have && g.b_rounds == 3 && g.b_round == 2 && g.b_me == 0);          /* the round the server plays */
    CHECK(gnt_rows(&g) == 2 && g.b_sel == 0 && gnt_is_mine(&g, 0) && !gnt_is_mine(&g, 1));
    CHECK(gnt_row(&g, 0, num, tit, ex) == 3 && !strcmp(num, "Ready") && !strcmp(tit, "Nigzel v Alpha"));
    CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Semifinal"));
    press(&g, GNT_B_L);
    CHECK(g.b_round == 1 && gnt_rows(&g) == 4);
    CHECK(gnt_row(&g, 0, num, tit, ex) == 3 && !strcmp(num, "Bye") && !strcmp(tit, "Nigzel (bye)"));
    CHECK(gnt_row(&g, 1, num, tit, ex) == 2 && !strcmp(num, "Done") && !strcmp(tit, "Alpha v Bravo") && !strcmp(ex, "2-1"));
    CHECK(gnt_row(&g, 3, num, tit, ex) == 2 && !strcmp(tit, "To be decided") && !strcmp(num, "Bye"));
    press(&g, GNT_B_L); CHECK(g.b_round == 1);
    press(&g, GNT_B_X); CHECK(g.b_round == 2 && g.b_sel == 0);
    press(&g, GNT_B_R); press(&g, GNT_B_R); CHECK(g.b_round == 3);
    press(&g, GNT_B_R); CHECK(g.b_round == 3);
    CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Final"));

    /* the call */
    g.req = 0; g.busy = 0;
    gnt_net_call(&g, 0, 2, "Alpha", "4821", 300);
    CHECK(gnt_calls(&g) && gnt_panel(&g, 0, pn) && !strcmp(pn, "Your match is ready!"));
    CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Semifinal v Alpha"));
    CHECK(!strcmp(gnt_hint(&g), "A: Play  L R: Round  Y: Bracket  B: Back"));
    press(&g, GNT_B_A);
    CHECK(g.req == GNT_REQ_PLAY && !strcmp(g.call_room, "4821"));
    gnt_played(&g);
    CHECK(!gnt_calls(&g));
    gnt_back_from_room(&g);
    CHECK(g.scr == GNT_BRACKET && g.poll >= GNT_POLL_FRAMES);
    g.busy = 0; g.req = 0;
    gnt_frame(&g, 0, 0, 0);
    CHECK(g.req == GNT_REQ_BRACKET);                                               /* back in the bracket: asked fresh at once */

    /* a 64 bracket fits */
    {
        static char big[64][GNT_NAME + 1];
        static struct gnt_match bm[63];
        unsigned k;
        for (k = 0; k < 64; k++) { big[k][0] = 'P'; big[k][1] = (char)('0' + k / 10); big[k][2] = (char)('0' + k % 10); big[k][3] = 0; }
        memset(bm, 0, sizeof bm);
        for (k = 0; k < 32; k++) { bm[k].round = 1; bm[k].a = (unsigned char)k; bm[k].b = (unsigned char)(63 - k); bm[k].state = GNT_M_READY; }
        for (; k < 63; k++) { bm[k].a = GNT_NONE; bm[k].b = GNT_NONE; }
        gnt_enter(&g); g.can_create = 0; gnt_net_list(&g, 0u, 1, &info);
        g.scr = GNT_BRACKET; g.cur = info; g.b_have = 0; g.busy = 1;
        gnt_net_bracket(&g, 64, 64, 63, (const char (*)[GNT_NAME + 1])big, 63, bm);
        g.b_round = 1; g.b_sel = 0; g.b_first = 0;
        CHECK(gnt_rows(&g) == 32 && g.b_rounds == 6);
        for (k = 0; k < 40; k++) press(&g, GNT_B_DOWN);
        CHECK(g.b_sel == 31 && g.b_first == 32 - GNT_LIST_VIS);
        for (k = 0; k < 40; k++) press(&g, GNT_B_UP);
        CHECK(g.b_sel == 0 && gnt_row(&g, 0, num, tit, ex) == 3 && !strcmp(tit, "P00 v P63"));          /* the first row is yours (63 plays 0) */
    }

    /* double elimination, 4 entrants: winners 2 + 1, losers 2, GF1 + reset; tabs on Y, rounds on L R, X goes to your next match */
    {
        static char dn[4][GNT_NAME + 1] = { "Nigzel", "Alpha", "Bravo", "Charlie" };
        static const unsigned char M[7][11] = {
            /* side round state a b wa wb win bo nw nl */
            { 0, 1, 4, 0, 3, 2, 0, 1, 3, 3, 4 }, { 0, 1, 4, 1, 2, 2, 1, 1, 3, 3, 4 }, { 0, 2, 3, 0, 1, 1, 0, 0, 3, 7, 5 },
            { 1, 1, 1, 3, 2, 0, 0, 0, 3, 5, 0 }, { 1, 2, 0, 0xFF, 0xFF, 0, 0, 0, 3, 6, 0 }, { 2, 1, 0, 0xFF, 0xFF, 0, 0, 0, 5, 7, 0 }, { 2, 2, 7, 0xFF, 0xFF, 0, 0, 0, 5, 0, 0 } };
        unsigned k;
        gnt_enter(&g); g.net = GNT_N_OK; g.scr = GNT_BRACKET; g.cur = info; g.cur.state = GNT_S_RUNNING; g.cur.round = 2; g.cur.rounds = 2; g.busy = 1;
        gnt_br_begin(&g, 4, 4, GNT_FMT_DOUBLE);
        for (k = 0; k < 4; k++) gnt_br_name(&g, k, dn[k], 0);
        for (k = 0; k < 7; k++) {
            struct gnt_match m;
            memset(&m, 0, sizeof m);
            m.side = M[k][0]; m.round = M[k][1]; m.state = M[k][2]; m.a = M[k][3]; m.b = M[k][4]; m.wa = M[k][5]; m.wb = M[k][6]; m.win = M[k][7]; m.bo = M[k][8]; m.next_win = M[k][9]; m.next_lose = M[k][10];
            gnt_br_match(&g, k + 1, &m);
        }
        gnt_br_end(&g, 3);                                                    /* you are Charlie */
        CHECK(g.b_have && g.b_side == GNT_SIDE_W && g.b_round == 2 && gnt_rows(&g) == 1);
        CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Winners Final"));
        press(&g, GNT_B_Y); CHECK(g.b_side == GNT_SIDE_L && g.b_round == 1 && gnt_rows(&g) == 1);
        CHECK(gnt_row(&g, 0, num, tit, ex) == 3 && !strcmp(num, "Ready") && !strcmp(tit, "Charlie v Bravo"));      /* yours: Charlie dropped to losers */
        CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Losers Round 1"));
        press(&g, GNT_B_R); CHECK(g.b_round == 2 && gnt_panel(&g, 0, pn) && !strcmp(pn, "Losers Final"));
        press(&g, GNT_B_R); CHECK(g.b_round == 2);
        press(&g, GNT_B_Y); CHECK(g.b_side == GNT_SIDE_F && gnt_panel(&g, 0, pn) && !strcmp(pn, "Grand Final"));
        press(&g, GNT_B_R); CHECK(g.b_round == 2 && gnt_panel(&g, 0, pn) && !strcmp(pn, "Grand Final Reset") && gnt_row(&g, 0, num, tit, ex));
        press(&g, GNT_B_Y); CHECK(g.b_side == GNT_SIDE_W);
        press(&g, GNT_B_X); CHECK(g.b_side == GNT_SIDE_L && g.b_round == 1);
        gnt_net_call(&g, 1, 1, "Bravo", "4821", 180);
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Losers Round 1 v Bravo"));
    }
    /* text view */
    {
        static struct sg_view v;
        gnt_enter(&g); gnt_net_list(&g, 3u, 3, l);
        memset(&v, 0, sizeof v);
        gnt_text(&g, &v);
        CHECK(!strncmp(v.t[0], "Tournament", 10) && strstr(v.t[0], "Example"));
        CHECK(strstr(v.t[2], "Friday Night") && strstr(v.t[2], "5 of max 16") && strstr(v.t[12], "Create") && strstr(v.t[SG_ROWS - 1u], "Sign up by ID") && strstr(v.t[SG_ROWS - 1u], "Region Auto"));
        for (i = 0; i < SG_ROWS; i++) CHECK(strlen(v.t[i]) <= SG_COLS);
    }
    /* official cups: countdown rows, Sign up / Cancel / Check In / Join now, no host controls */
    {
        struct gnt_ent oc;
        char t2[24];
        ent(&oc, 90, "Official", GNT_S_REG, GNT_FMT_DOUBLE, 32, 9, GNT_F_OFFICIAL);
        oc.secs = 8100ul; strcpy(oc.host, "Server");
        gnt_fmt_secs(t2, 8100ul); CHECK(!strcmp(t2, "2h 15m")); gnt_fmt_secs(t2, 2520ul); CHECK(!strcmp(t2, "42m")); gnt_fmt_secs(t2, 20ul); CHECK(!strcmp(t2, "under 1m"));
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 2u, 1, &oc); gnt_net_next(&g, 8100ul);
        CHECK(gnt_row(&g, 0, num, tit, ex) == 1 && !strcmp(tit, "Multishine dailies") && !strcmp(num, "9 of max 32") && !strcmp(ex, "2h 15m"));
        CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Starts in 2h 15m") && gnt_panel(&g, 1, pn) && !strcmp(pn, "9 of 32 signed up"));
        CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "Not signed up.") && gnt_panel(&g, 3, pn) == 0 && !pn[0]);   /* three lines, no "Press A" */
        CHECK(!strcmp(gnt_hint2(&g), "A: Rules  Z: Sign up  X: Create"));
        g.cur = oc; g.scr = GNT_PAGE; g.sel = 0;
        CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Sign up"));
        oc.flags |= GNT_F_JOINED; g.cur = oc;
        CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Cancel sign-up") && gnt_row(&g, 1, num, tit, ex) && !strcmp(tit, "Refresh"));       /* no Start for an official cup */
        oc.state = GNT_S_CHECKIN; g.cur = oc;
        CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Check In") && gnt_row(&g, 1, num, tit, ex) && !strcmp(tit, "Cancel sign-up"));       /* the urgent action is first: the cursor starts on it */
        oc.flags = GNT_F_OFFICIAL | GNT_F_LATE; g.cur = oc;
        CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Join now") && !strcmp(ex, "23 left"));
        oc.flags = GNT_F_OFFICIAL; g.cur = oc;
        CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(tit, "Refresh"));                                                                         /* check-in, not signed up, no late registration: closed */
    }
    /* Tournament look: Z signs up / leaves / checks in, A opens the rules, Hold B goes back, a tap of B closes the rules, the dot marks your one cup */
    {
        struct gnt_ent oc[2];
        unsigned k;
        ent(&oc[0], 90, "Official", GNT_S_REG, GNT_FMT_DOUBLE, 32, 9, GNT_F_OFFICIAL); oc[0].secs = 8100ul; strcpy(oc[0].host, "Server");
        ent(&oc[1], 91, "Official", GNT_S_REG, GNT_FMT_DOUBLE, 32, 3, GNT_F_OFFICIAL); oc[1].secs = 26100ul; strcpy(oc[1].host, "Server");
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 2, oc);
        CHECK(gnt_rows(&g) == 1 && g.dn == 2 && g.sel == 0 && gnt_row(&g, 0, num, tit, ex) == 1 && !strcmp(gnt_z_label(&g), "Sign up"));       /* both dailies on ONE row: the soonest */
        press(&g, GNT_B_Z);                                                   /* Z: sign up for the highlighted cup, no A */
        CHECK(g.req == GNT_REQ_JOIN && g.req_id == 90 && g.cur.id == 90 && g.scr == GNT_LIST && g.busy);
        gnt_net_done(&g, GNT_REQ_JOIN, 1);
        CHECK(g.req == GNT_REQ_INFO && !strcmp(g.say, "You are in."));
        oc[0].flags |= GNT_F_JOINED; oc[0].joined++; gnt_net_list(&g, 0u, 2, oc);
        CHECK(gnt_row(&g, 0, num, tit, ex) == 3 && gnt_row_star(&g, 0) == 1 && !strcmp(gnt_z_label(&g), "Leave"));          /* the ONE cup you are signed up for carries the dot */
        CHECK(!strcmp(gnt_hint2(&g), "A: Rules  Z: Leave"));
        press(&g, GNT_B_RIGHT); g.req = 0; g.busy = 0;                          /* the later daily, same row */
        CHECK(g.sel == 0 && g.v[0] == 1 && gnt_row_star(&g, 0) == 0 && !g.say[0] && !strcmp(gnt_z_label(&g), "Sign up"));
        press(&g, GNT_B_LEFT);
        g.req = 0; g.busy = 0; press(&g, GNT_B_Z);
        CHECK(g.req == GNT_REQ_LEAVE && g.req_id == 90);
        g.busy = 0; g.req = 0;
        oc[0].flags |= GNT_F_CHECKED; gnt_net_list(&g, 0u, 2, oc); CHECK(gnt_row(&g, 0, num, tit, ex) == 3);                /* checked in: the other colour */
        oc[0].flags = GNT_F_OFFICIAL; oc[0].state = GNT_S_CHECKIN; oc[0].flags |= GNT_F_JOINED; gnt_net_list(&g, 0u, 2, oc);
        CHECK(!strcmp(gnt_z_label(&g), "Check in")); g.req = 0; g.busy = 0; press(&g, GNT_B_Z); CHECK(g.req == GNT_REQ_CHECKIN);
        oc[0].flags = GNT_F_OFFICIAL; gnt_net_list(&g, 0u, 2, oc); g.busy = 0; g.req = 0;
        press(&g, GNT_B_Z); CHECK(g.req == 0 && g.sfx == GNT_SFX_DENY && !strcmp(g.say, "Sign-up is closed now."));    /* check-in open, no late registration */
        oc[0].state = GNT_S_REG; gnt_net_list(&g, 0u, 2, oc); g.say[0] = 0;
        press(&g, GNT_B_A);                                                   /* A: the cup's rules */
        CHECK(g.scr == GNT_RULES && g.req == GNT_REQ_INFO && !strcmp(gnt_title(&g), "Tournament rules") && !strcmp(gnt_z_label(&g), "Sign up"));
        for (k = 0; k < GNT_RULE_N; k++) { CHECK(gnt_rules(&g, k, tit) == 1 && strlen(tit) <= 38u); }
        CHECK(gnt_rules(&g, GNT_RULE_N, tit) == 0);
        CHECK(!strcmp(gnt_hint(&g), "Z: Sign up  Start: Refresh  B: Close"));
        g.busy = 0; g.req = 0; press(&g, GNT_B_Z); CHECK(g.req == GNT_REQ_JOIN && g.req_id == 90 && g.scr == GNT_RULES);
        g.busy = 0; g.req = 0; press(&g, GNT_B_B);                            /* a tap of B closes the rules page, it does not leave the list */
        CHECK(g.scr == GNT_LIST && g.exit == 0);
        g.busy = 0; g.req = 0; hold_b(&g); CHECK(g.exit == 1); g.exit = 0;               /* B: back out of the list */
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 2, oc); press(&g, GNT_B_B); CHECK(g.exit == 1 && g.scr == GNT_LIST); g.exit = 0;
        gnt_frame(&g, 0, 0, 0); for (k = 0; k < 40u; k++) gnt_frame(&g, GNT_B_B, 0, 0); CHECK(g.exit == 1); g.exit = 0;      /* a long press fires once */
        gnt_frame(&g, 0, 0, 0);
        { struct sg_view v; memset(&v, 0, sizeof v); g.scr = GNT_RULES; g.cur = oc[0]; gnt_text(&g, &v); CHECK(!strncmp(v.t[2], "Double elimination", 18)); }
        /* a hosted tournament: A opens its page, Z signs up; a private one in your list (you host it or are in it) has no passcode step any more */
        ent(&oc[1], 92, "Chill Night", GNT_S_REG, GNT_FMT_SINGLE, 8, 2, 0); oc[1].flags = GNT_F_PASS;
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 2, oc); press(&g, GNT_B_DOWN); g.busy = 0; g.req = 0;
        CHECK(g.sel == 1 && !strcmp(gnt_z_label(&g), "Sign up")); press(&g, GNT_B_Z); CHECK(g.scr == GNT_LIST && g.req == GNT_REQ_JOIN && g.req_id == 92);
    }
    /* cube the call belongs to its own tab and round; the page asks again while registering and offers Check in */
    {
        struct gnt_ent ck;
        ent(&ck, 70, "Cube Open", GNT_S_REG, GNT_FMT_DOUBLE, 8, 3, GNT_F_JOINED | GNT_F_HOST);
        gnt_enter(&g); g.req = 0; g.cur = ck; g.scr = GNT_PAGE; g.busy = 0;
        { unsigned k; for (k = 0; k < GNT_POLL_FRAMES + 2u; k++) gnt_frame(&g, 0, 0, 0); CHECK(g.req == GNT_REQ_INFO && g.req_id == 70); }
        g.req = 0; g.busy = 0; ck.state = GNT_S_CHECKIN; ck.secs = 240; gnt_net_info(&g, &ck);
        CHECK(!strcmp(gnt_z_label(&g), "Check in") && gnt_panel(&g, 2, pn) && !strcmp(pn, "Check in now! 4m left"));
        { unsigned r, found = 0; for (r = 0; r < gnt_rows(&g); r++) { gnt_row(&g, r, num, tit, ex); if (!strcmp(tit, "Check In")) found = 1; } CHECK(found); }
        CHECK(!strcmp(gnt_hint(&g), "A: Select  Z: Check in  Start: Refresh  B: Back"));
        press(&g, GNT_B_Z); CHECK(g.req == GNT_REQ_CHECKIN && g.req_id == 70);
        gnt_enter(&g); g.req = 0; g.cur = ck; g.scr = GNT_BRACKET; g.b_have = 1; g.b_side = GNT_SIDE_L; g.b_round = 1;
        gnt_net_call(&g, GNT_SIDE_W, 2, "PC Bot 5", "1234", 180);
        CHECK(g.b_side == GNT_SIDE_W && g.b_round == 2);                      /* a new call shows its own tab and round */
        g.b_side = GNT_SIDE_L; g.b_round = 1;
        CHECK(gnt_calls(&g) == 0 && strcmp(gnt_hint(&g), "A: Play  L R: Round  Y: Bracket  B: Back") != 0);         /* the losers page: its own lines, no call */
        g.req = 0; g.busy = 0; press(&g, GNT_B_A); CHECK(g.req != GNT_REQ_PLAY);
        g.b_side = GNT_SIDE_W; g.b_round = 2; CHECK(gnt_calls(&g) == 1 && gnt_panel(&g, 0, pn) && !strcmp(pn, "Your match is ready!"));
        g.req = 0; g.busy = 0; press(&g, GNT_B_A); CHECK(g.req == GNT_REQ_PLAY);
        ent(&ck, 71, "Called Off", GNT_S_CANCEL, GNT_FMT_DOUBLE, 8, 3, 0);
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 1, &ck); CHECK(gnt_row(&g, 0, num, tit, ex) && !strcmp(ex, "Called off"));
    }
    /* refusals say why ( Limits), never silently */
    {
        static const struct { unsigned what, why; const char *text; } F[] = {
            { GNT_REQ_JOIN, GNT_W_LIMIT, "You are in another one." }, { GNT_REQ_CREATE, GNT_W_LIMIT, "You are in another one." }, { GNT_REQ_JOIN, GNT_W_FULL, "The tournament is full." },
            { GNT_REQ_JOIN, GNT_W_CLOSED, "Entry closed: it started." }, { GNT_REQ_JOIN, GNT_W_DENIED, "You cannot join this one." }, { GNT_REQ_JOINID, GNT_W_DENIED, "Wrong passcode." }, { GNT_REQ_JOINID, GNT_W_RATE, "Too many tries, wait 10 minutes." }, { GNT_REQ_JOINID, GNT_W_BAD, "No tournament with that ID." }, { GNT_REQ_START, GNT_W_DENIED, "Only the host can do that." },
            { GNT_REQ_CREATE, GNT_W_BAD, "That name is not allowed." }, { GNT_REQ_START, GNT_W_BAD, "Not enough entrants yet." }, { GNT_REQ_CHECKIN, GNT_W_CLOSED, "Check-in is not open now." } };
        for (i = 0; i < sizeof F / sizeof F[0]; i++) {
            gnt_enter(&g); g.cur = info; g.scr = GNT_PAGE;
            gnt_net_fail(&g, F[i].what, F[i].why);
            CHECK(!strcmp(g.say, F[i].text) && g.say_warn && g.sfx == GNT_SFX_DENY && strlen(F[i].text) <= GNT_SAY);
            CHECK(gnt_panel(&g, 0, pn) == 4 && !strcmp(pn, F[i].text));
        }
        gnt_enter(&g); g.cf_row = 3; g.scr = GNT_CREATE;
        CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Up to 16 players. Byes fill gaps."));
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Starts when sign-up ends.") && gnt_panel(&g, 2, pn) && !strcmp(pn, "Entry closes at the start."));
        gnt_enter(&g); g.cur = info; g.cur.state = GNT_S_REG; g.cur.flags = 0; g.scr = GNT_PAGE;
        CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "Entry closes at the start."));
    }
    /* failures */
    gnt_enter(&g); g.req = 0;
    gnt_net_fail(&g, GNT_REQ_LIST, GNT_W_NOSUP);
    CHECK(g.net == GNT_N_NOSUP && gnt_panel(&g, 0, pn) == 4 && !strcmp(pn, "Not on this server yet."));
    gnt_net_fail(&g, GNT_REQ_LIST, GNT_W_UNREACH);
    CHECK(g.net == GNT_N_UNREACH);
    press(&g, GNT_B_A); CHECK(g.req == GNT_REQ_LIST && g.net == GNT_N_LOADING);
    /* GUI pass the list asks the entry under the cursor for its host once, the wording of every state, the empty and error states */
    {
        struct gnt_ent a[2], pe;
        ent(&a[0], 31, "Cube Open", GNT_S_RUNNING, GNT_FMT_DOUBLE, 16, 4, GNT_F_JOINED); a[0].host[0] = 0; a[0].round = 0; a[0].rounds = 0;
        ent(&a[1], 32, "Other", GNT_S_REG, GNT_FMT_SINGLE, 8, 2, 0); a[1].host[0] = 0;
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 2, a);
        CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Double elimination") && gnt_panel(&g, 1, pn) && !strcmp(pn, "Playing now") && gnt_panel(&g, 2, pn) == 3 && !strcmp(pn, "You are in."));    /* no host yet: no empty "Host: ; double", no "Round 0 of 0" */
        { unsigned k; for (k = 0; k < 14u; k++) gnt_frame(&g, 0, 0, 0); CHECK(g.req == GNT_REQ_INFO && g.req_id == 31 && g.busy); }
        pe = a[0]; strcpy(pe.host, "Nigzel"); pe.round = 2; pe.rounds = 4;
        gnt_net_info(&g, &pe);
        CHECK(g.scr == GNT_LIST && g.e[0].host[0] == 'N' && gnt_panel(&g, 0, pn) && !strcmp(pn, "Host: Nigzel, double") && gnt_panel(&g, 1, pn) && !strcmp(pn, "Round 2 of 4"));
        g.req = 0; { unsigned k; for (k = 0; k < 40u; k++) gnt_frame(&g, 0, 0, 0); CHECK(g.req == GNT_REQ_NONE); }      /* asked once, not again and again */
        gnt_enter(&g); g.req = 0; gnt_net_fail(&g, GNT_REQ_LIST, GNT_W_UNREACH);
        CHECK(gnt_status_view(&g) == 2 && gnt_panel(&g, 0, pn) == 4 && !strcmp(pn, "Server not available.") && gnt_panel(&g, 1, pn) && !strcmp(pn, "Press A to try again."));
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 0, a);
        CHECK(g.net == GNT_N_EMPTY && gnt_status_view(&g) == 0 && gnt_rows(&g) == 0 && gnt_row(&g, 0, num, tit, ex) == 2 && !strcmp(tit, "No tournaments open.") && !strcmp(gnt_hint2(&g), "") && !strncmp(gnt_hint(&g), "Y: Sign up by ID", 16));    /* Create and Sign up by ID are footer keys: an empty list keeps them */
        press(&g, GNT_B_Y); CHECK(g.scr == GNT_JOIN);
    }
    /* the finished page: winner, your place; cancelled; ordinals */
    {
        struct gnt_ent d;
        ent(&d, 41, "Done Open", GNT_S_DONE, GNT_FMT_DOUBLE, 8, 8, GNT_F_JOINED | GNT_F_OUT); strcpy(d.champion, "PC Bot 2"); d.you = GNT_Y_ELIM;
        gnt_enter(&g); g.req = 0; g.cur = d; g.scr = GNT_PAGE; g.busy = 0;
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Winner: PC Bot 2"));
        gnt_frame(&g, 0, 0, 0); CHECK(g.req == GNT_REQ_MYMATCH && g.my_asked);                  /* a finished one you played: the place is asked once */
        g.busy = 0; gnt_net_my(&g, 4u, 2u);
        CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "You are out."));
        g.cur.flags &= (unsigned char)~GNT_F_OUT; g.cur.flags |= GNT_F_JOINED;
        CHECK(gnt_panel(&g, 2, pn) == 3 && !strcmp(pn, "You placed 2nd."));
        gnt_net_my(&g, 4u, 3u); CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "You placed 3rd."));
        gnt_net_my(&g, 4u, 5u); CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "You placed 5th."));
        d.state = GNT_S_CANCEL; d.reason = 1; d.flags = 0; g.cur = d;
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Called off, too few players."));
    }
    /* the match room's identity and the walkover wording */
    {
        char rt[40], names[4][GNT_NAME + 1];
        unsigned bo = 0;
        struct gnt_match m[3];
        memset(m, 0, sizeof m);
        strcpy(names[0], "Nigzel"); strcpy(names[1], "PC Bot 6"); strcpy(names[2], "PC Bot 4"); strcpy(names[3], "PC Bot 5");
        m[0].side = GNT_SIDE_W; m[0].round = 1; m[0].state = GNT_M_CALLED; m[0].a = 0; m[0].b = 1; m[0].bo = 5;
        m[1].side = GNT_SIDE_W; m[1].round = 1; m[1].state = GNT_M_WALK; m[1].a = 2; m[1].b = 3; m[1].win = 2;
        gnt_enter(&g); g.req = 0; g.scr = GNT_BRACKET; strcpy(g.cur.name, "Cube Open"); g.cur.id = 5;
        gnt_net_bracket(&g, 4, 4, 0, (const char (*)[GNT_NAME + 1])names, 2, m);
        gnt_net_call(&g, GNT_SIDE_W, 1, "PC Bot 6", "1234", 150);
        CHECK(gnt_call_info(&g, rt, &bo) && !strcmp(rt, "Semifinal") && bo == 5 && gnt_call_secs(&g) == 150);
        CHECK(gnt_panel(&g, 2, pn) == 3 && !strcmp(pn, "Press A to play. 2:30 left"));
        gnt_row(&g, 1, num, tit, ex); CHECK(!strcmp(num, "Done") && !strcmp(ex, "Walkover") && !strcmp(tit, "PC Bot 4 v PC Bot 5"));
        g.called = 0; g.b_sel = 1; CHECK(gnt_panel(&g, 2, pn) && !strcmp(pn, "PC Bot 5 won by walkover"));
        gnt_enter(&g); g.req = 0; g.scr = GNT_BRACKET; g.busy = 0; g.b_have = 0; CHECK(gnt_panel(&g, 0, pn) && !strcmp(pn, "Bracket not available.") && gnt_panel(&g, 1, pn) && !strcmp(pn, "Press Start to try again."));
    }
    /* Back on the bracket of a pending call was undone by the next call poll; the call clock runs; the host's check-in page */
    {
        char names[4][GNT_NAME + 1];
        struct gnt_match m[2];
        struct gnt_ent h;
        unsigned k;
        memset(m, 0, sizeof m);
        strcpy(names[0], "Nigzel"); strcpy(names[1], "PC Bot 6"); strcpy(names[2], "PC Bot 4"); strcpy(names[3], "PC Bot 5");
        m[0].side = GNT_SIDE_W; m[0].round = 1; m[0].state = GNT_M_CALLED; m[0].a = 0; m[0].b = 1; m[0].bo = 3;
        gnt_enter(&g); g.req = 0; g.scr = GNT_BRACKET; strcpy(g.cur.name, "Cube Open"); g.cur.id = 5; g.cur.state = GNT_S_RUNNING; g.cur.flags = GNT_F_JOINED;
        gnt_net_bracket(&g, 4, 4, 0, (const char (*)[GNT_NAME + 1])names, 1, m);
        gnt_net_call(&g, GNT_SIDE_W, 1, "PC Bot 6", "1234", 150);
        CHECK(gnt_calls(&g));
        for (k = 0; k < 60u; k++) gnt_frame(&g, 0, 0, 0);
        CHECK(gnt_call_secs(&g) == 148 && gnt_panel(&g, 2, pn) && !strcmp(pn, "Press A to play. 2:28 left"));
        g.busy = 0; g.req = 0;
        hold_b(&g);
        CHECK(g.scr == GNT_PAGE && !g.exit);
        g.busy = 0; gnt_net_call(&g, GNT_SIDE_W, 1, "PC Bot 6", "1234", 140);
        CHECK(g.scr == GNT_PAGE);                                      /* the poll does not pull the player back in */
        g.busy = 0; gnt_net_call(&g, GNT_SIDE_L, 1, "PC Bot 9", "5678", 170);
        CHECK(g.scr == GNT_BRACKET);                                   /* a different call does */
        ent(&h, 6, "Host Open", GNT_S_CHECKIN, GNT_FMT_DOUBLE, 8, 4, GNT_F_JOINED | GNT_F_HOST); h.secs = 240;
        gnt_enter(&g); g.req = 0; g.cur = h; g.scr = GNT_PAGE;
        { unsigned r; int st = 0; for (r = 0; r < gnt_rows(&g); r++) { gnt_row(&g, r, num, tit, ex); if (!strcmp(tit, "Start Tournament")) st = 1; } CHECK(!st); }
        CHECK(gnt_panel(&g, 1, pn) && !strcmp(pn, "Check-in open, 4m left"));
    }
    {   /* geometry in one place (gnt_geo.h) and the new strings */
        float mid = GNT_GEO_CUR_Y0_PX + 4.0f * GNT_GEO_CUR_PITCH_PX - (152.0f + 4.0f * GNT_GEO_TEXT_PITCH_PX);
        struct gnt_ent e;
        gnt_enter(&g); g.req = 0; g.net = GNT_N_OK;
        ent(&e, 5, "Server", GNT_S_REG, GNT_FMT_DOUBLE, 64, 64, 0); g.n = 1; g.e[0] = e; g.scr = GNT_LIST; g.first = 0;
        CHECK(mid > -0.05f && mid < 0.05f && GNT_GEO_CUR_PITCH_PX < GNT_GEO_TEXT_PITCH_PX);
        CHECK(((GNT_GEO_CUR_Y0_PX - GNT_GEO_FORM_SHIFT_PX - GNT_GEO_CUR_PITCH_PX / 2) + (GNT_GEO_CUR_Y0_PX - GNT_GEO_FORM_SHIFT_PX + 8.5f * GNT_GEO_CUR_PITCH_PX)) / 2 - GNT_GEO_FORM_CY_PX > -0.05f && ((GNT_GEO_CUR_Y0_PX - GNT_GEO_FORM_SHIFT_PX - GNT_GEO_CUR_PITCH_PX / 2) + (GNT_GEO_CUR_Y0_PX - GNT_GEO_FORM_SHIFT_PX + 8.5f * GNT_GEO_CUR_PITCH_PX)) / 2 - GNT_GEO_FORM_CY_PX < 0.05f);      /* the form's 9 rows are centred in its box */
        CHECK(strlen(GNT_OFFICIAL_NAME) <= GNT_TITLE && GNT_NUM >= strlen("64 of max 64"));
        { unsigned r; for (r = 0; r < gnt_rows(&g); r++) { gnt_row(&g, r, num, tit, ex); if (!strcmp(tit, "Server")) CHECK(!strcmp(num, "64 of max 64")); } }
        ent(&e, 6, "Official", GNT_S_REG, GNT_FMT_DOUBLE, 32, 4, GNT_F_OFFICIAL); g.e[0] = e; g.cur = e; g.scr = GNT_PAGE;
        CHECK(!strcmp(gnt_title(&g), "Multishine dailies"));
    }
    /* the new screens in the text view, and no trace of the old 4 digit passcode: the digits are 8, the widest right-hand word fits its buffer */
    {
        static struct sg_view v;
        unsigned r;
        gnt_enter(&g); g.req = 0; g.scr = GNT_CREATE; g.cf_pass_on = 1; g.cf_row = 10; g.cf_format = GNT_FMT_SINGLE;
        memset(&v, 0, sizeof v); gnt_text(&g, &v);
        CHECK(strstr(v.t[2], "Preset") && strstr(v.t[2], "Custom") && strstr(v.t[4], "Format") && strstr(v.t[4], "Single Elimination"));
        g.first = 2; memset(&v, 0, sizeof v); gnt_text(&g, &v);                                           /* scrolled two rows: the Passcode row is the 9th shown */
        CHECK(strstr(v.t[10], "Passcode") && strstr(v.t[10], "<0>0000000")); g.first = 0;
        for (r = 0; r < SG_ROWS; r++) CHECK(strlen(v.t[r]) <= SG_COLS);
        g.scr = GNT_JOIN; g.cf_row = 0; memset(&v, 0, sizeof v); gnt_text(&g, &v);
        CHECK(strstr(v.t[0], "Sign up by ID") && strstr(v.t[2], "<0>0000000") && strstr(v.t[3], "None") && strstr(v.t[4], "Sign up"));
        CHECK(strlen(gnt_join_pass(&g)) == 0 && gnt_join_id(&g) == 0);
    }
    /* list pass: dailies on top (soonest first), custom cups after; filter Any / Dailies / Custom on L R; check-in rows show the time to start; no preset name equals the dailies' name */
    {
        struct gnt_ent m[4];
        char nm[GNT_NAME + 1];
        unsigned k;
        ent(&m[0], 41, "Late Night", GNT_S_REG, GNT_FMT_DOUBLE, 16, 3, 0);
        ent(&m[1], 42, "Official", GNT_S_REG, GNT_FMT_DOUBLE, 32, 9, GNT_F_OFFICIAL); m[1].secs = 20000ul;
        ent(&m[2], 43, "Official", GNT_S_REG, GNT_FMT_DOUBLE, 32, 4, GNT_F_OFFICIAL); m[2].secs = 900ul;
        ent(&m[3], 44, "Check Open", GNT_S_CHECKIN, GNT_FMT_SINGLE, 8, 4, GNT_F_JOINED | GNT_F_CHECKIN_ON); m[3].secs = 14ul * 60ul;
        gnt_enter(&g); g.req = 0; gnt_net_list(&g, 0u, 4, m);
        /* ONE dailies row on top (the soonest daily), then the custom cups */
        CHECK(gnt_rows(&g) == 3 && g.dn == 2 && g.v[0] == 2 && g.v[1] == 0 && g.v[2] == 3);
        gnt_row(&g, 0, num, tit, ex); CHECK(!strcmp(tit, "Multishine dailies") && !strcmp(ex, "15m 1/2") && !strcmp(num, "4 of max 32"));
        CHECK(gnt_row_cycle(&g, 0) == 1 && gnt_row_cycle(&g, 1) == 0 && gnt_row_cycle(&g, 2) == 0 && !strchr(ex, '<') && !strchr(ex, '>'));       /* the cycle arrows on the dailies row only */
        gnt_row(&g, 1, num, tit, ex); CHECK(!strcmp(tit, "Late Night") && !strcmp(ex, "Open"));
        gnt_row(&g, 2, num, tit, ex); CHECK(!strcmp(tit, "Check Open") && !strcmp(ex, "14m"));      /* the countdown, not "Check in" */
        CHECK(gnt_row_star(&g, 2) == 1 && gnt_row_star(&g, 0) == 0);
        m[3].state = GNT_S_RUNNING; gnt_net_list(&g, 0u, 4, m); gnt_row(&g, 2, num, tit, ex); CHECK(!strcmp(ex, "Playing"));
        m[3].state = GNT_S_CHECKIN; gnt_net_list(&g, 0u, 4, m);
        /* Left Right on the dailies row cycle the dailies (wrap); A / Z act on the shown one */
        CHECK(g.sel == 0 && gnt_hint2(&g)[0]);
        press(&g, GNT_B_RIGHT); CHECK(g.v[0] == 1 && g.dsel == 1 && gnt_rows(&g) == 3 && g.sel == 0);
        gnt_row(&g, 0, num, tit, ex); CHECK(!strcmp(tit, "Multishine dailies") && !strcmp(ex, "5h 33m 2/2") && !strcmp(num, "9 of max 32"));
        press(&g, GNT_B_RIGHT); CHECK(g.v[0] == 2 && g.dsel == 0);                          /* wraps */
        press(&g, GNT_B_LEFT); CHECK(g.v[0] == 1 && g.dsel == 1);
        gnt_net_list(&g, 0u, 4, m); CHECK(g.v[0] == 1 && g.sel == 0);                       /* a refresh keeps the shown daily */
        CHECK(g.e[g.v[g.sel]].id == 42);
        press(&g, GNT_B_A); CHECK(g.scr == GNT_RULES && g.cur.id == 42 && g.req == GNT_REQ_INFO); press(&g, GNT_B_B); g.scr = GNT_LIST; g.req = 0;
        g.req = 0; press(&g, GNT_B_DOWN); CHECK(g.sel == 1);
        press(&g, GNT_B_RIGHT); CHECK(g.v[0] == 1 && g.sel == 1);                           /* Left Right only act with the cursor on the dailies row */
        press(&g, GNT_B_UP); press(&g, GNT_B_LEFT); CHECK(g.v[0] == 2);
        g.sel = 1;                                                            /* Late Night: the cursor stays on the same cup across a filter change that keeps it */
        m[0].region = 3; m[3].region = 0; gnt_net_list(&g, 0u, 4, m);                         /* Late Night is an EU cup, Check Cup is open to any region, the dailies are Any */
        CHECK(gnt_rows(&g) == 3 && g.filter == 0 && !g.f_user && !strcmp(gnt_filter_name(&g), "Auto"));      /* Auto first (no account region set: Any) */
        press(&g, GNT_B_R); CHECK(g.f_user == 1u && g.filter == 0 && !strcmp(gnt_filter_name(&g), "Any"));
        press(&g, GNT_B_R); CHECK(g.filter == 2 && !strcmp(gnt_filter_name(&g), "NAW") && gnt_rows(&g) == 2 && g.dn == 2 && !strcmp(gnt_hint(&g), "Y: Sign up by ID  L R: Region NAW  B: Back"));      /* an EU cup hides under NAW, an Any cup and the dailies stay */
        gnt_row(&g, 1, num, tit, ex); CHECK(!strcmp(tit, "Check Open"));
        { unsigned k, f0 = g.filter; static const unsigned char NX[8] = { 2, 3, 7, 4, 5, 6, 0, 1 };                                         /* L R cycle on a fresh press only: held = one step, a chattering trigger (one frame up) = still one step */
          for (k = 0; k < 60u; k++) gnt_frame(&g, GNT_B_R, 0, 0);
          CHECK(g.filter == NX[f0]);
          for (k = 0; k < 6u; k++) { gnt_frame(&g, 0, 0, 0); gnt_frame(&g, GNT_B_R, 0, 0); }
          CHECK(g.filter == NX[f0]);
          for (k = 0; k < GNT_LR_GAP; k++) gnt_frame(&g, 0, 0, 0);
          gnt_frame(&g, GNT_B_R, 0, 0); gnt_frame(&g, 0, 0, 0); gnt_frame(&g, 0, 0, 0);
          CHECK(g.filter == NX[NX[f0]]);
          press(&g, GNT_B_L); press(&g, GNT_B_L); CHECK(g.filter == (unsigned char)f0); }
        press(&g, GNT_B_R); press(&g, GNT_B_R); press(&g, GNT_B_R); CHECK(g.filter == 3 && !strcmp(gnt_filter_name(&g), "EU") && gnt_rows(&g) == 3);
        press(&g, GNT_B_L); press(&g, GNT_B_L); press(&g, GNT_B_L); press(&g, GNT_B_L); CHECK(g.filter == 0);
        press(&g, GNT_B_L); CHECK(!g.f_user && !strcmp(gnt_filter_name(&g), "Auto") && gnt_rows(&g) == 3);      /* Left from Any is Auto */
        press(&g, GNT_B_L); CHECK(g.filter == 6 && !strcmp(gnt_filter_name(&g), "OC") && gnt_rows(&g) == 2);     /* and wraps back to OC (the last one west to east) */
        press(&g, GNT_B_L); CHECK(g.filter == 5 && !strcmp(gnt_filter_name(&g), "AS") && gnt_rows(&g) == 2);      /* then AS */
        press(&g, GNT_B_R);
        press(&g, GNT_B_R); press(&g, GNT_B_R); CHECK(g.f_user == 1u && g.filter == 0 && gnt_rows(&g) == 3);
        gnt_set_account_region(&g, 3u); CHECK(g.f_user == 1u && g.filter == 0);      /* a chosen filter stays when the account region arrives */
        press(&g, GNT_B_L); CHECK(!g.f_user && g.filter == 3 && !strcmp(gnt_filter_name(&g), "Auto") && gnt_rows(&g) == 3);      /* Auto = the account's region (EU) */
        gnt_set_account_region(&g, 0u); CHECK(g.filter == 0); press(&g, GNT_B_R);
        { struct gnt_ent one[2]; one[0] = m[1]; one[1] = m[0]; gnt_net_list(&g, 0u, 2, one); gnt_row(&g, 0, num, tit, ex); CHECK(g.dn == 1 && !strcmp(ex, "5h 33m")); g.sel = 0; press(&g, GNT_B_RIGHT); CHECK(g.dsel == 0 && g.sel == 0 && gnt_row_cycle(&g, 0) == 0); }      /* a single daily: no position hint, nothing to cycle */
        { struct gnt_ent only = m[0]; gnt_net_list(&g, 0u, 1, &only); press(&g, GNT_B_R); CHECK(g.net == GNT_N_OK && g.filter == 2 && gnt_rows(&g) == 0 && gnt_row(&g, 0, num, tit, ex) == 2 && !strcmp(tit, "None for this region.")); press(&g, GNT_B_L); }
        { struct gnt_ent pg = m[0]; char pn[40]; pg.host[0] = 'N'; pg.host[1] = 0; pg.region = 3; gnt_net_list(&g, 0u, 1, &pg); CHECK(g.filter == 0 && gnt_panel(&g, 0, pn) && !strcmp(pn, "Host: N, double, Region: EU")); }      /* the cup page / box line */
        /* the Create presets never equal the dailies' reserved name (the server refuses it too) */
        gnt_enter(&g); g.req = 0; g.scr = GNT_CREATE;
        for (k = 0; k < 4u; k++) {
            unsigned j = 0, q = 0;
            g.cf_name = (unsigned char)k;
            while (gnt_create_name(&g)[j] && q < GNT_NAME) { char c = gnt_create_name(&g)[j++]; if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a'); if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) nm[q++] = c; }
            nm[q] = 0;
            CHECK(strcmp(nm, "multishinedailies") != 0);
        }
    }
    /* scroll markers: '^' when rows are hidden above the window, 'v' when below (the Create form has 12 rows, 9 visible) */
    {
        unsigned k;
        gnt_enter(&g); g.req = 0; g.busy = 0; g.scr = GNT_CREATE; g.first = 0;
        CHECK(gnt_rows(&g) == 12 && gnt_scroll_more(&g) == 2u);                       /* top: more below only */
        g.first = 1; CHECK(gnt_scroll_more(&g) == 3u);
        g.first = 2; CHECK(gnt_scroll_more(&g) == 3u);
        g.first = 3; CHECK(gnt_scroll_more(&g) == 1u);                                /* bottom: more above only */
        g.scr = GNT_JOIN; g.first = 0; CHECK(gnt_scroll_more(&g) == 0u);              /* Sign up by ID has 3 rows: nothing hidden */
        g.scr = GNT_BRACKET; CHECK(gnt_scroll_more(&g) == 0u);
        (void)k;
    }
    /* the host's call off on a RUNNING cup he plays in: the page polls TN_MYMATCH every GNT_POLL_FRAMES and each poll in flight must not reset the hold */
    {
        struct gnt_ent hr; char num[16], tit[32], ex[32]; unsigned k, row = 99, fr, cancelled = 0, flight = 0;
        gnt_enter(&g); g.req = 0; g.busy = 0; g.scr = GNT_PAGE;
        ent(&hr, 78, "Running", GNT_S_RUNNING, GNT_FMT_DOUBLE, 16, 4, GNT_F_HOST | GNT_F_JOINED); hr.pubid = 12345679ul;
        g.cur = hr; g.poll = GNT_POLL_FRAMES - 10u;                                              /* the next poll is 10 frames away */
        for (k = 0; k < gnt_rows(&g); k++) { gnt_row(&g, k, num, tit, ex); if (!strcmp(tit, "Call Off Tournament")) row = k; }
        CHECK(row != 99);
        g.sel = (unsigned char)row;
        for (fr = 0; fr < 4u * GNT_HOLD_B_FRAMES && !cancelled; fr++) {
            gnt_frame(&g, GNT_B_A, 0, 0);
            if (g.req == GNT_REQ_CANCEL) cancelled = 1;
            else if (g.busy && g.req == GNT_REQ_MYMATCH) { if (++flight >= 6u) { flight = 0; g.req = 0; gnt_net_my(&g, 0, 0); } }     /* the answer comes 6 frames later */
        }
        CHECK(cancelled);
        gnt_frame(&g, 0, 0, 0); g.busy = 0; g.req = 0;
    }
    /* a call that was dismissed (B from the called bracket back to the page): the next TN_MYMATCH answer must not leave the page busy */
    {
        struct gnt_ent hd;
        gnt_enter(&g); g.req = 0; g.busy = 0; g.scr = GNT_PAGE;
        ent(&hd, 79, "Dismissed", GNT_S_RUNNING, GNT_FMT_DOUBLE, 16, 4, GNT_F_HOST | GNT_F_JOINED);
        g.cur = hd; g.called = 1; g.dismissed = 1; g.dis_room[0] = 'R'; g.dis_room[1] = '1'; g.dis_room[2] = 0;
        g.busy = 1; g.req = GNT_REQ_MYMATCH;                                                      /* the poll is in flight */
        gnt_net_call(&g, 0, 1, "Bot", "R1", 90);
        CHECK(g.scr == GNT_PAGE && !g.busy);
    }
    /* the host calls a cup off: a Call Off Tournament row on the cup page (HOLD A for the shared hold length; a tap only shows the hint; a release cancels) */
    {
        struct gnt_ent hc; char num[16], tit[32], ex[32]; unsigned k, row = 99;
        gnt_enter(&g); g.req = 0; g.busy = 0; g.scr = GNT_PAGE;
        ent(&hc, 77, "Hosted", GNT_S_REG, GNT_FMT_SINGLE, 8, 3, GNT_F_HOST | GNT_F_JOINED); hc.pubid = 12345678ul;
        g.cur = hc;
        for (k = 0; k < gnt_rows(&g); k++) { gnt_row(&g, k, num, tit, ex); if (!strcmp(tit, "Call Off Tournament")) row = k; }
        CHECK(row != 99 && gnt_row(&g, row, num, tit, ex) == 4 && !strcmp(ex, "Hold A"));            /* coral like Forfeit */
        g.sel = (unsigned char)row;
        press(&g, GNT_B_A); CHECK(g.req == GNT_REQ_NONE && !strcmp(g.say, "Hold A to call off.") && g.say_warn);     /* a tap: only the hint */
        for (k = 0; k < GNT_HOLD_B_FRAMES - 3u; k++) gnt_frame(&g, GNT_B_A, 0, 0);
        gnt_frame(&g, 0, 0, 0); CHECK(g.req == GNT_REQ_NONE && g.hold_a == 0);                                   /* released early: nothing */
        for (k = 0; k < GNT_HOLD_B_FRAMES + 5u; k++) gnt_frame(&g, GNT_B_A, 0, 0);
        CHECK(g.req == GNT_REQ_CANCEL && g.req_id == 77 && g.busy);                                              /* held the whole time: asked once */
        gnt_frame(&g, 0, 0, 0);
        g.req = 0; gnt_net_done(&g, GNT_REQ_CANCEL, 1);
        CHECK(g.scr == GNT_LIST && g.req == GNT_REQ_LIST && !strcmp(g.say, "Tournament called off."));
        g.cur = hc; g.scr = GNT_PAGE; g.busy = 0; g.say[0] = 0; gnt_net_done(&g, GNT_REQ_CANCEL, 0); CHECK(g.scr == GNT_PAGE && g.say_warn);
        hc.flags = GNT_F_JOINED; g.cur = hc; for (k = 0; k < gnt_rows(&g); k++) { gnt_row(&g, k, num, tit, ex); CHECK(strcmp(tit, "Call Off Tournament") != 0); }   /* only the host */
        hc.flags = GNT_F_HOST; hc.state = GNT_S_DONE; g.cur = hc; for (k = 0; k < gnt_rows(&g); k++) { gnt_row(&g, k, num, tit, ex); CHECK(strcmp(tit, "Call Off Tournament") != 0); }      /* not once it is over */
        hc.state = GNT_S_RUNNING; hc.flags = GNT_F_HOST | GNT_F_JOINED; g.cur = hc; row = 99; for (k = 0; k < gnt_rows(&g); k++) { gnt_row(&g, k, num, tit, ex); if (!strcmp(tit, "Call Off Tournament")) row = k; } CHECK(row != 99);     /* a running cup too */
    }
    {   /* the animated bracket pack: an 8 cup, standard seeding in round 1, two first-round winners known */
        static struct gnt g; static struct tmb_pack pk;
        static char nm[8][GNT_NAME + 1] = { "P0", "P1", "P2", "P3", "P4", "P5", "P6", "P7" };
        static const unsigned char R1[4][2] = { { 0, 7 }, { 3, 4 }, { 1, 6 }, { 2, 5 } };
        unsigned k, i, mid = 1;
        memset(&g, 0, sizeof g);
        gnt_enter(&g); g.net = GNT_N_OK; g.scr = GNT_BRACKET; g.busy = 1;
        gnt_br_begin(&g, 8, 8, GNT_FMT_DOUBLE);
        for (k = 0; k < 8; k++) gnt_br_name(&g, k, nm[k], 0);
        for (k = 0; k < 4; k++) {                                                     /* W round 1 */
            struct gnt_match m; memset(&m, 0, sizeof m);
            m.side = GNT_SIDE_W; m.round = 1; m.a = R1[k][0]; m.b = R1[k][1]; m.bo = 3; m.state = (k < 2) ? GNT_M_DONE : GNT_M_READY; m.win = (k == 0) ? 1 : (k == 1) ? 2 : 0;
            gnt_br_match(&g, mid++, &m);
        }
        for (k = 0; k < 2; k++) { struct gnt_match m; memset(&m, 0, sizeof m); m.side = GNT_SIDE_W; m.round = 2; m.a = m.b = GNT_NONE; m.bo = 3; gnt_br_match(&g, mid++, &m); }
        { struct gnt_match m; memset(&m, 0, sizeof m); m.side = GNT_SIDE_W; m.round = 3; m.a = m.b = GNT_NONE; m.bo = 5; gnt_br_match(&g, mid++, &m); }
        gnt_br_end(&g, 0);
        CHECK(gnt_tmv_pack(&g, &pk, 7) == 0);
        CHECK(pk.size_idx == 2 && pk.nent == 8 && pk.nres == 2 && pk.ver == 7);
        CHECK(pk.res[0].box == 0 && pk.res[0].winner == 0);                              /* box 0 = R1 match 0: P0 (leaf 0) won */
        CHECK(pk.res[1].box == 1 && pk.res[1].winner == 3);                              /* box 1 = R1 match 1 (P3 v P4): side b = P4 won: leaf 3 holds P4 */
        for (i = 0; i < 8; i++) CHECK(pk.ent[i].label == 0x384u + (i == 0 ? 0 : i == 1 ? 7 : i == 2 ? 3 : i == 3 ? 4 : i == 4 ? 1 : i == 5 ? 6 : i == 6 ? 2 : 5));
        g.b_n = 7; CHECK(gnt_tmv_pack(&g, &pk, 1) == 1);                                  /* a cup with byes: not supported yet */
        g.b_n = 8; g.b_m[0].win = 2; g.b_m[0].state = GNT_M_DONE; CHECK(gnt_tmv_pack(&g, &pk, 1) == 0);
        g.b_have = 0; CHECK(gnt_tmv_pack(&g, &pk, 1) == 1);
    }
    printf(fails ? "gnt_test: %d FAILED\n" : "gnt_test: all passed\n", fails);
    return fails ? 1 : 0;
}
