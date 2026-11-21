/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Grandmaster standings model (stb.c) with a LOCAL MOCK LADDER (the field shapes of STANDINGS_LIST 0x33;
   the offline fallback of the tests only, never in the overlay): requests, paging by 9 with Up / Down across pages and L / R, your own row, placement rows,
   every state text, the dates and ordinals, glyphs and widths, the character-cell view.  usage: stb_test [OUT.txt] */
#include <stdio.h>
#include <string.h>
#include "sign/sg_ui.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* the local mock ladder: 23 players (the server's --standings-mock shapes: names <= 12, codes <= 8, ties, a placement tail with rank 0 / tier 255) */
#define LN 23u
static struct stb_row L[LN];
static void ladder(void)
{
    static const char *const NM[LN] = { "Wobble Pilot", "Pocket Fox", "Lylat Lou", "Cliff Hanger", "Tipper Tom", "Ledge Dash", "Shine Spike", "Wave Lander",
        "Dair Dan", "Fair Fiona", "Up B Ben", "Tech Chase", "Crouch Cee", "Grab Gabe", "Drift Dora", "Rest Rex", "Sweet Spot", "Moon Walker", "Short Hop",
        "Fast Fall", "Nigzel", "Ice Climb", "Laser Lee" };
    unsigned i, rank = 1, prev = 0;
    for (i = 0; i < LN; i++) {
        struct stb_row *r = &L[i];
        unsigned rating = 2240u - 60u * i;
        memset(r, 0, sizeof *r);
        if (i == 5u) rating = prev;                                /* a tie */
        strcpy(r->name, NM[i]);
        r->you = (unsigned char)(i == 20u);                          /* the server marks the requesting player's own row; no connect code exists on the console */
        r->rating = (unsigned short)rating;
        r->games = (unsigned short)(i >= 21u ? 3u : 40u + i);
        if (i >= 21u) { r->rank = 0; r->tier = STB_TIER_PLACEMENT; }
        else { if (rating != prev) rank = i + 1u; r->rank = (unsigned short)rank; r->tier = (unsigned char)(rating >= 2150u ? 5u : rating >= 1950u ? 4u : rating >= 1700u ? 3u : rating >= 1400u ? 2u : rating >= 1100u ? 1u : 0u); }      /* the server's tier byte, small-field rating limits */
        prev = rating;
    }
}
/* answer the model's request like the server would (page size 9) */
static void serve(struct stb *g, unsigned flags, const struct stb_row *you)
{
    unsigned p = g->req_page, off = p * STB_VIS, n = off < LN ? LN - off : 0u;
    CHECK(g->req == STB_REQ_LIST);
    g->req = STB_REQ_NONE;
    if (n > STB_VIS) n = STB_VIS;
    stb_net_list(g, flags, 1, 1792002600ul, LN, p, n, L + off, you);
}
static int glyphs_ok(const char *s)
{
    for (; *s; s++) if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || strchr(" .,'-:#", *s))) return 0;
    return 1;
}
static void all_texts_ok(const struct stb *g)
{
    char a[64], b[64], c[64], d[64];
    unsigned i;
    for (i = 0; i < STB_VIS; i++) { stb_cols(g, i, a, b, c, d); CHECK(glyphs_ok(a) && glyphs_ok(b) && glyphs_ok(c) && glyphs_ok(d) && strlen(b) <= 12 && strlen(d) <= 11); }
    for (i = 0; i < 3u; i++) { stb_panel(g, i, a); CHECK(glyphs_ok(a) && strlen(a) <= STB_LINE); }
    stb_head(g, a); CHECK(glyphs_ok(a) && strlen(a) <= 15);
    stb_badge(g, a); CHECK(glyphs_ok(a) && strlen(a) <= 12);
    CHECK(glyphs_ok(stb_title(g)) && glyphs_ok(stb_hint(g)) && strlen(stb_hint(g)) <= 60);
    if (stb_state_text(g)) CHECK(glyphs_ok(stb_state_text(g)));
}
static void press(struct stb *g, unsigned b) { stb_frame(g, b, 0, 0); stb_frame(g, 0, 0, 0); }

static FILE *dump;
static void view(const struct stb *g, const char *tag)
{
    static struct sg_view v;
    unsigned r;
    memset(&v, 0, sizeof v);
    stb_text(g, &v);
    for (r = 0; r < SG_ROWS; r++) CHECK(strlen(v.t[r]) <= SG_COLS);
    if (!dump) return;
    fprintf(dump, "SCREEN %s\n", tag);
    for (r = 0; r < SG_ROWS; r++) fprintf(dump, "%u|%s\n", v.attr[r], v.t[r]);
}

int main(int argc, char **argv)
{
    static struct stb g;
    char a[64], b[64], c[64], d[64];
    unsigned i;
    dump = argc > 1 ? fopen(argv[1], "w") : 0;
    ladder();

    /* dates and ordinals */
    stb_day(a, 0); CHECK(!strcmp(a, "Jan 1"));
    stb_day(a, 951782400ul); CHECK(!strcmp(a, "Feb 29"));
    stb_day(a, 1792002600ul); CHECK(!strcmp(a, "Oct 14"));
    stb_day(a, 1798761599ul); CHECK(!strcmp(a, "Dec 31"));
    { static const unsigned n[] = { 1, 2, 3, 4, 11, 12, 13, 21, 22, 23, 101, 111, 112 }; static const char *const w[] = { "1st", "2nd", "3rd", "4th", "11th", "12th", "13th", "21st", "22nd", "23rd", "101st", "111th", "112th" };
      for (i = 0; i < sizeof n / sizeof n[0]; i++) { stb_ordinal(a, n[i]); CHECK(!strcmp(a, w[i])); } }

    /* enter: Loading, page 0 asked */
    stb_enter(&g);
    CHECK(g.req == STB_REQ_LIST && g.req_page == 0 && g.net == STB_N_LOADING && stb_rows(&g) == 0);
    stb_badge(&g, a); CHECK(!strcmp(a, "Loading"));
    CHECK(!strcmp(stb_state_text(&g), "Loading") && !strcmp(stb_title(&g), "Grandmaster"));
    stb_head(&g, a); CHECK(a[0] == 0);
    view(&g, "loading");
    /* example data from the server (flag bit 0): Example tag, no 'you' even if one were sent */
    serve(&g, 1, 0);                                                      /* (the server sends no you row for a mock ladder unless the lab flag is on) */
    CHECK(g.net == STB_N_OK && stb_rows(&g) == 9 && g.mock && !g.have_you && stb_pages(&g) == 3);
    stb_badge(&g, a); CHECK(!strcmp(a, "Example"));
    stb_head(&g, a); CHECK(!strcmp(a, "Updated Oct 14"));
    CHECK(stb_cols(&g, 0, a, b, c, d) == 1u && !strcmp(a, "1") && !strcmp(b, "Wobble Pilot") && !strcmp(c, "2240") && !strcmp(d, "Grandmaster"));
    /* tie display (two players on the same rating share the rank, competition ranking 1 2 3 4 5 5 7): same number, same badge, the next rank skips */
    CHECK(stb_cols(&g, 4, a, b, c, d) == 1u && !strcmp(a, "5"));
    CHECK(stb_cols(&g, 5, a, b, c, d) == 1u && !strcmp(a, "5"));
    CHECK(stb_cols(&g, 6, a, b, c, d) == 1u && !strcmp(a, "7"));
    { unsigned long r4, r5; CHECK(stb_mark(&g, 4, &r4) == stb_mark(&g, 5, &r5) && r4 == r5 && !strcmp(stb_mark_name(&g, 4), stb_mark_name(&g, 5))); }
    stb_panel(&g, 0, a); CHECK(!strcmp(a, "Wobble Pilot"));   /* name only: no connect code */
    stb_panel(&g, 1, a); CHECK(!strcmp(a, "40 games   Grandmaster"));
    CHECK(stb_panel(&g, 2, a) == 1u && !strcmp(a, "Example data from the server"));
    all_texts_ok(&g);
    /* badges: the Grandmaster alone on rank 1 wears the crown icon (gold); the others step down by rating, the tier icon id never rising */
    {
        unsigned long rgb, rgb0;
        unsigned sh = stb_mark(&g, 0, &rgb), prev_sh = STB_SH_TMAX;
        CHECK(sh == STB_SH_GM && rgb == 0xFFC83Cul && !strcmp(stb_mark_name(&g, 0), "Grandmaster"));
        CHECK(stb_mark(&g, 9, &rgb) == STB_SH_NONE);                              /* no row there */
        CHECK(stb_mark(&g, 1, &rgb0) <= STB_SH_TMAX && rgb0 != 0xFFC83Cul);       /* second place: a tier icon, not the gold crown */
        for (i = 1; i < 9u; i++) { sh = stb_mark(&g, i, &rgb); CHECK(sh <= prev_sh && sh >= STB_SH_T1); prev_sh = sh; CHECK(stb_mark_name(&g, i)[0] != 0); }
        CHECK(stb_mark(&g, 8, &rgb) == STB_SH_T1 + 3u);                           /* 1760: Master, the third step of six (limit 1700, shape 3; the six-tier ladder of) */
        { const char *nm; unsigned long c2;
          CHECK(stb_rank_icon(2, 2150, STB_TIER_NONE, 0, 0, &c2, &nm) == STB_SH_TMAX && !strcmp(nm, "Virtuoso"));
          CHECK(stb_rank_icon(3, 2149, STB_TIER_NONE, 0, 0, 0, &nm) == STB_SH_TMAX - 1u && !strcmp(nm, "Sensei"));
          CHECK(stb_rank_icon(1, 2400, STB_TIER_NONE, 0, 0, 0, &nm) == STB_SH_GM && !strcmp(nm, "Grandmaster"));
          CHECK(stb_rank_icon(1, 2400, STB_TIER_NONE, 1, 0, 0, &nm) == STB_SH_TMAX);                  /* `tied` = an earlier row already is the Grandmaster */
          CHECK(stb_rank_icon(40, 1500, STB_TIER_NONE, 0, 1, 0, &nm) == STB_SH_PLACEMENT && !strcmp(nm, "Placement"));
          CHECK(stb_rank_icon(0, 0, STB_TIER_NONE, 0, 0, 0, 0) == STB_SH_PLACEMENT);
          CHECK(stb_rank_icon(7, 1000, 5, 0, 0, 0, &nm) == STB_SH_TMAX && !strcmp(nm, "Virtuoso"));              /* the server's percentile band beats the rating limits */
          CHECK(stb_rank_icon(1, 1000, 5, 0, 0, 0, &nm) == STB_SH_GM);                                           /* rank 1 alone stays the Grandmaster */
          CHECK(stb_rank_icon(7, 2400, 0, 0, 0, 0, &nm) == STB_SH_T1 && !strcmp(nm, "Novice"));
          CHECK(stb_rank_icon(90, 100, STB_TIER_NONE, 0, 0, 0, &nm) == STB_SH_T1 && !strcmp(nm, "Novice")); }
    }
    view(&g, "example_page1");
    /* cursor: down 8 times, then past the last row: page 2 asked, cursor lands on its first row */
    for (i = 0; i < 8u; i++) press(&g, STB_B_DOWN);
    CHECK(g.sel == 8 && g.req == STB_REQ_NONE);
    press(&g, STB_B_DOWN);
    CHECK(g.req == STB_REQ_LIST && g.req_page == 1 && g.busy);
    stb_badge(&g, a); CHECK(!strcmp(a, "Loading"));
    CHECK(stb_rows(&g) == 9);                                              /* the old rows stay until the answer */
    press(&g, STB_B_DOWN); CHECK(g.sel == 8);                              /* no move while the page is loading */
    serve(&g, 1, 0);
    CHECK(g.page == 1 && g.sel == 0 && stb_cols(&g, 0, a, b, c, d) && !strcmp(b, "Fair Fiona") && !strcmp(a, "10"));
    /* up above the first row: page 1 again, cursor on its last row */
    press(&g, STB_B_UP); CHECK(g.req_page == 0); serve(&g, 1, 0); CHECK(g.page == 0 && g.sel == 8);
    /* R twice: the last page (5 rows: two placed, ... the placement tail); R again refused */
    press(&g, STB_B_R); serve(&g, 1, 0); CHECK(g.page == 1 && g.sel == 8);
    press(&g, STB_B_R); serve(&g, 1, 0); CHECK(g.page == 2 && g.n == 5 && g.sel == 4);
    press(&g, STB_B_R); CHECK(g.req == STB_REQ_NONE && g.sfx == STB_SFX_DENY);
    CHECK(stb_cols(&g, 4, a, b, c, d) == 2u && !strcmp(a, "-") && !strcmp(d, "Placement"));     /* placement: dim, no rank */
    { unsigned long rgb; CHECK(stb_mark(&g, 4, &rgb) == STB_SH_PLACEMENT && !strcmp(stb_mark_name(&g, 4), "Placement")); }
    stb_panel(&g, 1, a); CHECK(!strcmp(a, "3 games   In placement"));
    press(&g, STB_B_DOWN); CHECK(g.req == STB_REQ_NONE && g.sel == 4);  /* the end of the ladder: no wrap */
    view(&g, "example_last_page");
    press(&g, STB_B_L); serve(&g, 1, 0); CHECK(g.page == 1);
    all_texts_ok(&g);
    /* held Down repeats */
    g.sel = 0;
    for (i = 0; i < STB_REP_FIRST + 2u * STB_REP_NEXT; i++) stb_frame(&g, STB_B_DOWN, 0, 0);
    stb_frame(&g, 0, 0, 0);
    CHECK(g.sel == 3);
    /* stick edges move like the D-pad */
    stb_frame(&g, 0, 1, 0); CHECK(g.sel == 2);
    stb_frame(&g, 0, 0, 1); CHECK(g.sel == 3);

    /* a real ladder with your own row (Nigzel, 21st) shown on page 3 and pinned in the panel */
    stb_enter(&g);
    serve(&g, 0, &L[20]);
    CHECK(!g.mock && g.have_you);
    stb_badge(&g, a); CHECK(!strcmp(a, "1 of 3"));
    CHECK(stb_panel(&g, 2, a) == 2u && !strcmp(a, "You: 21st, 1040"));
    press(&g, STB_B_R); serve(&g, 0, &L[20]); press(&g, STB_B_R); serve(&g, 0, &L[20]);
    CHECK(stb_cols(&g, 2, a, b, c, d) == 3u && !strcmp(b, "Nigzel"));     /* your row: highlighted */
    g.sel = 2; stb_panel(&g, 0, a); CHECK(!strcmp(a, "Nigzel   You"));   /* no connect code in the panel */
    view(&g, "you_row");
    all_texts_ok(&g);
    /* Start reloads the page, then the 2 s gap */
    press(&g, STB_B_START); CHECK(g.req == STB_REQ_LIST && g.req_page == 2 && g.sfx == STB_SFX_FWD); serve(&g, 0, &L[20]);
    press(&g, STB_B_START); CHECK(g.req == STB_REQ_NONE && g.sfx == STB_SFX_DENY);
    /* a refused reload (rate) keeps the page */
    for (i = 0; i < STB_REFRESH_GAP; i++) stb_frame(&g, 0, 0, 0);
    press(&g, STB_B_START); g.req = STB_REQ_NONE; stb_net_fail(&g, STB_W_RATE); CHECK(g.net == STB_N_OK && stb_rows(&g) == 5);
    /* B leaves */
    press(&g, STB_B_B); CHECK(g.exit && g.sfx == STB_SFX_BACK);

    /* states: empty ladder, offline, old server, bad data */
    stb_enter(&g); g.req = STB_REQ_NONE; stb_net_list(&g, 0, 1, 1792002600ul, 0, 0, 0, L, 0);
    { char pl[STB_LINE + 1]; CHECK(g.net == STB_N_EMPTY && stb_state_text(&g) == 0 && stb_rows(&g) == 0 && stb_panel(&g, 0, pl) == 2 && !strcmp(pl, "No ranked games yet")); }   /* the empty text sits in the panel */
    CHECK(stb_cols(&g, 0, a, b, c, d) == 0 && stb_panel(&g, 1, a) == 0 && !strcmp(a, "Play Grandmaster to get listed.") && stb_panel(&g, 2, a) == 0 && a[0] == 0);
    { char tl[STB_TOP_MAX + 1u], tr[16], bl[STB_BOTTOM_MAX + 1u], bm[16], br[16];          /* the screen draws stb_top / stb_bottom (gnb_ev.c st_draw): the empty text must be there */
      stb_top(&g, tl, tr); stb_bottom(&g, bl, bm, br);
      CHECK(!strcmp(tl, "No ranked games yet") && !tr[0] && !strcmp(bl, "Play Grandmaster to get listed.") && !bm[0] && !br[0]); }
    stb_badge(&g, a); CHECK(a[0] == 0);
    press(&g, STB_B_DOWN); press(&g, STB_B_R); CHECK(g.req == STB_REQ_NONE);
    all_texts_ok(&g);
    view(&g, "empty");
    stb_enter(&g); g.req = STB_REQ_NONE; stb_net_fail(&g, STB_W_UNREACH); CHECK(!strcmp(stb_state_text(&g), "Server not available"));
    stb_head(&g, a); CHECK(a[0] == 0);
    view(&g, "offline");
    press(&g, STB_B_START); CHECK(g.req == STB_REQ_LIST && g.net == STB_N_LOADING);       /* Start tries again */
    stb_enter(&g); g.req = STB_REQ_NONE; stb_net_fail(&g, STB_W_NOSUP); CHECK(!strcmp(stb_state_text(&g), "Needs a newer server"));
    stb_enter(&g); g.req = STB_REQ_NONE; stb_net_fail(&g, STB_W_BAD); CHECK(!strcmp(stb_state_text(&g), "The server sent bad data"));
    all_texts_ok(&g);
    /* two rows on rank 1 (an older server): only the FIRST is THE Grandmaster, never two */
    {
        struct stb_row t2[3];
        unsigned long rgb;
        memset(t2, 0, sizeof t2);
        t2[0] = L[0]; t2[1] = L[0]; t2[2] = L[1]; t2[2].rank = 3;
        strcpy(t2[1].name, "Twin");
        stb_enter(&g); g.req = STB_REQ_NONE; stb_net_list(&g, 0, 1, 1ul, 3, 0, 3, t2, 0);
        CHECK(stb_mark(&g, 0, &rgb) == STB_SH_GM && rgb == 0xFFC83Cul && stb_mark(&g, 1, &rgb) <= STB_SH_TMAX && rgb != 0xFFC83Cul);
        CHECK(stb_cols(&g, 0, a, b, c, d) && !strcmp(a, "1") && !strcmp(d, "Grandmaster"));
        CHECK(stb_cols(&g, 1, a, b, c, d) && strcmp(d, "Grandmaster") != 0);
        /* the rating limits: the step boundaries from the data file (2100 is in the top step, 0 in the bottom one) */
        t2[0].rank = 2; t2[0].rating = 2100; t2[0].tier = 99; t2[1].rank = 3; t2[1].rating = 2099; t2[1].tier = 99; t2[2].rank = 4; t2[2].rating = 5; t2[2].tier = 99;      /* an unknown tier byte: the rating limits (fallback) */
        stb_enter(&g); g.req = STB_REQ_NONE; stb_net_list(&g, 0, 1, 1ul, 3, 0, 3, t2, 0);
        CHECK(!strcmp(stb_mark_name(&g, 0), "Sensei") && !strcmp(stb_mark_name(&g, 1), "Sensei") && !strcmp(stb_mark_name(&g, 2), "Novice") && stb_mark(&g, 2, &rgb) == STB_SH_T1);
    }
    /* hostile strings: anything the font cannot draw becomes a space; n above 9 is cut */
    {
        struct stb_row h[12];
        memset(h, 0, sizeof h);
        for (i = 0; i < 12u; i++) { strcpy(h[i].name, "Bad\x7f<Name>!"); h[i].rank = (unsigned short)(i + 1u); h[i].tier = 3; }
        stb_enter(&g); g.req = STB_REQ_NONE; stb_net_list(&g, 0, 1, 1ul, 12, 0, 12, h, 0);
        CHECK(g.n == 9 && !strcmp(g.r[0].name, "Bad  Name  "));
        all_texts_ok(&g);
        /* the ladder shrank: an empty later page sends the model back to page 0 */
        g.page = 1; stb_net_list(&g, 0, 1, 1ul, 5, 1, 0, h, 0); CHECK(g.req == STB_REQ_LIST && g.req_page == 0);
    }
    if (dump) fclose(dump);
    {   /* the season box (the small box at the bottom right of the info panel): "24" / "days" / "left", "day" for 1, nothing when unknown */
        struct stb_row h[1];
        char dn[8], d1[8], d2[8];
        unsigned dd;
        memset(h, 0, sizeof h); strcpy(h[0].name, "Ann"); h[0].rank = 1; h[0].rating = 1500; h[0].tier = 3;
        stb_enter(&g); g.req = STB_REQ_NONE; stb_net_list(&g, 0, 1, 1ul, 1, 0, 1, h, 0);
        CHECK(!stb_season_box(&g, dn, d1, d2) && !dn[0] && !d1[0] && !d2[0]);               /* unknown: empty box */
        stb_net_days(&g, 24);
        CHECK(stb_season_box(&g, dn, d1, d2) && !strcmp(dn, "24") && !strcmp(d1, "days") && !strcmp(d2, "left"));
        stb_net_days(&g, 1);
        CHECK(stb_season_box(&g, dn, d1, d2) && !strcmp(dn, "1") && !strcmp(d1, "day") && !strcmp(d2, "left"));
        stb_net_days(&g, 31);
        CHECK(stb_season_box(&g, dn, d1, d2) && !strcmp(dn, "31"));
        stb_net_days(&g, 0); CHECK(!stb_season_box(&g, dn, d1, d2));                         /* 0 is never shown */
        stb_net_days(&g, 32); CHECK(!stb_season_box(&g, dn, d1, d2) && g.days_left == 0);    /* out of range = unknown */
        for (dd = 1; dd <= 31u; dd++) {                                                        /* widths: 2 digits at the big size (0.06: about 40 px), the words at 0.022 (about 30 px) in a 62 px box */
            stb_net_days(&g, dd);
            CHECK(stb_season_box(&g, dn, d1, d2) && strlen(dn) <= 2 && strlen(d1) <= 4 && strlen(d2) <= 4 && glyphs_ok(dn) && glyphs_ok(d1) && glyphs_ok(d2));
            CHECK(strlen(dn) * 10u * 60u / 30u <= 52u && 4u * 10u * 22u / 30u <= 52u);
        }
        g.net = STB_N_OFFLINE; CHECK(!stb_season_box(&g, dn, d1, d2));                       /* never over a state screen */
    }
    {   /* the info box layout and X: Me: top row name + tier title, bottom row Position / games / ELO; no example or update wording; X jumps to your own row by page */
        char l[32], m[32], r[32];
        unsigned pg, row;
        stb_me_page(1, &pg, &row); CHECK(pg == 0 && row == 0);
        stb_me_page(9, &pg, &row); CHECK(pg == 0 && row == 8);
        stb_me_page(10, &pg, &row); CHECK(pg == 1 && row == 0);
        stb_me_page(18, &pg, &row); CHECK(pg == 1 && row == 8);
        stb_me_page(21, &pg, &row); CHECK(pg == 2 && row == 2);
        stb_me_page(4000000u > 65535u ? 65535u : 0u, &pg, &row); CHECK(pg == 65534u / STB_VIS && row == 65534u % STB_VIS);      /* a huge ladder: the request is by page number */
        stb_enter(&g); serve(&g, 0, &L[20]);
        stb_top(&g, l, r); CHECK(!strcmp(l, "Wobble Pilot") && !strcmp(r, "Grandmaster") && glyphs_ok(l) && glyphs_ok(r));
        stb_bottom(&g, l, m, r); CHECK(!strcmp(l, "Position: 1") && !strcmp(m, "40 games") && !strcmp(r, "ELO 2240") && glyphs_ok(l) && glyphs_ok(m) && glyphs_ok(r));
        for (i = 0; i < STB_VIS; i++) { g.sel = (unsigned char)i; stb_top(&g, l, r); stb_bottom(&g, l, m, r); CHECK(strlen(l) <= 14 && strlen(m) <= 10 && strlen(r) <= 10); }
        { unsigned k; for (k = 0; k < 3u; k++) { stb_panel(&g, k, l); CHECK(!strstr(l, "Example")); } stb_head(&g, l); CHECK(!strstr(l, "Example")); }
        CHECK(stb_me_ok(&g));
        g.sel = 0; press(&g, STB_B_X);                                          /* your row is on page 3 (21st): the page is asked, the cursor lands on your row */
        CHECK(g.req == STB_REQ_LIST && g.req_page == 2 && g.find_me && g.sfx == STB_SFX_MOVE);
        serve(&g, 0, &L[20]);
        CHECK(g.page == 2 && g.sel == 2 && !g.find_me);
        stb_top(&g, l, r); CHECK(!strncmp(l, "Nigzel", 6));
        g.sel = 0; press(&g, STB_B_X); CHECK(g.req == STB_REQ_NONE && g.sel == 2);     /* already on that page: the cursor only moves */
        press(&g, STB_B_L); serve(&g, 0, &L[20]); CHECK(g.page == 1);
        press(&g, STB_B_X); CHECK(g.req == STB_REQ_LIST && g.req_page == 2);          /* from another page */
        serve(&g, 0, &L[20]);
        { struct stb_row pl = L[22]; pl.rank = 0; pl.tier = STB_TIER_PLACEMENT;      /* in placement: not on the board */
          stb_enter(&g); serve(&g, 0, &pl); CHECK(!stb_me_ok(&g)); press(&g, STB_B_X); CHECK(g.req == STB_REQ_NONE && g.sfx == STB_SFX_DENY); }
        stb_enter(&g); serve(&g, 0, 0); CHECK(!stb_me_ok(&g)); press(&g, STB_B_X); CHECK(g.sfx == STB_SFX_DENY && stb_note(&g));      /* no reply row: the note says so */
        stb_enter(&g); serve(&g, 1, &L[20]); CHECK(g.mock && g.have_you && stb_me_ok(&g));    /* a mock ladder may mark a row as yours (the lab's --standings-mock-you) */
    }
    if (fails) { printf("stb_test: %d FAILED\n", fails); return 1; }
    printf("stb_test: all checks passed\n");
    return 0;
}
