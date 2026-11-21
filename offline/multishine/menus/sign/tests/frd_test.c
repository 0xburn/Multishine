/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Friends screen model (frd.c): LIVE requests and answers (requests first, then 8 friends per page until all are cached), the sort as the server sends
   it, paging by 9 with X / Y, the rows (the Requests section, friends; no Add row: Z adds), the status dots, A accept and X decline of a request, the action list (Invite with the wait and its two
   outcomes, Message, Remove and Block confirmed by a second A, Back), the code entry, the empty and offline states, failures, texts and widths. */
#include <stdio.h>
#include <string.h>
#include "../frd.h"
#include "../sg_ui.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static unsigned held;
static void press(struct frd *f, unsigned b) { frd_frame(f, 0, 0, 0); frd_frame(f, b, 0, 0); frd_frame(f, 0, 0, 0); }   /* released first: a button already down when the list opens counts only after a release */
static void row(const struct frd *f, unsigned s, char *num, char *t) { (void)frd_row(f, s, num, t); }
static int title_has(const struct frd *f, const char *needle)        /* the panel's title bar (a friend's status, not the name) */
{
    char t[32];
    frd_req_badge(f, t);
    return strstr(t, needle) != 0;
}
static int panel_has(const struct frd *f, const char *needle)
{
    char ln[64];
    unsigned i;
    for (i = 0; i < FRD_PANEL; i++) { frd_panel(f, i, ln); if (strstr(ln, needle)) return 1; }
    return 0;
}
static void mk(struct frd_ent *e, unsigned i, unsigned pres, unsigned seen)
{
    memset(e, 0, sizeof *e);
    snprintf(e->code, sizeof e->code, "F%u#%u", i % 10u, i);
    snprintf(e->name, sizeof e->name, "Friend %u", i);
    e->presence = (unsigned char)pres; e->seen = (unsigned char)seen;
}

/* the body geometry (three lines touched): the lines of every count are inside the box, with a gap, and one line is centred */
static void body_geometry(void)
{
    static const float R[4][4] = FRD_BODY_ROWS;
    const float top = 10.30f, bot = 12.55f, big_h = 0.60f, small_h = 0.50f;
    unsigned m, i;
    for (m = 1; m <= 4; m++) {
        float h = m <= 3 ? big_h : small_h;
        CHECK(R[m - 1][0] >= top - 0.001f);
        CHECK(R[m - 1][m - 1] + h <= bot + 0.001f);
        for (i = 1; i < m; i++) CHECK(R[m - 1][i] - R[m - 1][i - 1] >= h + 0.05f);       /* never touching */
    }
    CHECK((R[0][0] + big_h / 2.0f) > (top + bot) / 2.0f - 0.05f && (R[0][0] + big_h / 2.0f) < (top + bot) / 2.0f + 0.05f);   /* one line: centred */
    CHECK(R[2][1] - R[2][0] >= 0.74f - 0.001f);                                       /* three lines: the pitch of the status messages */
}

int main(void)
{
    body_geometry();
    static struct frd F;
    struct frd_ent page[8];
    struct frd_rq rq[2];
    char num[16], t[40], ln[64];
    unsigned i, k, idx;
    (void)held;
    /* ---- LIVE: enter asks for the list; requests then 3 pages (8 + 8 + 4 = 20 friends) ---- */
    frd_enter(&F, 1);
    CHECK(F.req == FRD_REQ_LIST && F.lpage == 0 && F.net == FRD_N_LOADING);
    CHECK(panel_has(&F, "Loading Friends"));
    F.req = FRD_REQ_NONE;
    memset(rq, 0, sizeof rq);
    strcpy(rq[0].code, "RQ#1"); strcpy(rq[0].name, "Req \"One\"");                  /* a quote never reaches the screen */
    strcpy(rq[1].code, "RQ#2"); strcpy(rq[1].name, "");
    frd_net_requests(&F, 2, 2, rq);
    CHECK(F.nrq == 2 && !strcmp(F.r[0].name, "Req  One ") && !strcmp(F.r[1].name, "RQ#2"));
    for (i = 0; i < 8u; i++) mk(&page[i], i, i < 5u ? 2u : 0u, (unsigned)i);
    frd_net_friends(&F, 20, 0, 8, page);
    CHECK(F.nf == 8 && F.req == FRD_REQ_LIST && F.lpage == 8 && F.net == FRD_N_OK);
    F.req = FRD_REQ_NONE;
    for (i = 0; i < 8u; i++) mk(&page[i], 8u + i, 0, 30u);
    frd_net_friends(&F, 20, 8, 8, page);
    CHECK(F.nf == 16 && F.req == FRD_REQ_LIST && F.lpage == 16);
    F.req = FRD_REQ_NONE;
    for (i = 0; i < 4u; i++) mk(&page[i], 16u + i, 0, 255u);
    frd_net_friends(&F, 20, 16, 4, page);
    CHECK(F.nf == 20 && F.req == FRD_REQ_NONE);
    CHECK(frd_rows(&F) == 2u + 20u && frd_max_first(&F) == 22u - 9u);
    /* rows: the Requests section first (a white dot, "Request" in the title), then the friends in the server's order; the left column is a dot, never a word */
    CHECK(frd_row(&F, 0, num, t) == 2u && !num[0] && !strcmp(t, "Req  One   RQ#1") && frd_dot(&F, 0) == FRD_DOT_REQ && !strcmp(frd_dot_label(FRD_DOT_REQ), "Request"));
    CHECK(frd_row(&F, 2, num, t) == 2u && !num[0] && !strcmp(t, "Friend 0  F0#0") && frd_dot(&F, 2) == FRD_DOT_GREEN);   /* in the menus: green, highlighted */
    CHECK(frd_row(&F, 7, num, t) == 1u && frd_dot(&F, 7) == FRD_DOT_GREY);                                              /* offline: grey, dim */
    CHECK(!strcmp(frd_dot_word(FRD_DOT_GREEN), "Ready") && !strcmp(frd_dot_word(FRD_DOT_RED), "In match") && !strcmp(frd_dot_label(FRD_DOT_SEARCH), "In queue") && strlen(frd_dot_label(FRD_DOT_SEARCH)) <= FRD_NUM);
    frd_badge(&F, ln); CHECK(!strcmp(ln, "2 req  5 online"));
    frd_req_badge(&F, ln); CHECK(!strcmp(ln, "Friend request"));                                                          /* the panel's title bar: always a title; on a request row "Friend request" */
    CHECK(frd_online(&F) == 5u && frd_box(&F, 0, ln) == 1u && !strcmp(ln, "5"));                                           /* the small box: the same count, big */
    CHECK(frd_box(&F, 1, ln) == 0u && !strcmp(ln, "online") && frd_box(&F, 2, ln) == 0u && !ln[0]);          /* one label line: "online" */
    /* a request row: its own panel, no legend; a friend row: the legend line in the panel, the keys in the hint */
    CHECK(panel_has(&F, "wants to be friends") && panel_has(&F, "Connect code RQ#1") && !frd_legend(&F));
    F.page = 2; frd_frame(&F, 0, 0, 0);
    CHECK(!panel_has(&F, "Add a friend") && !panel_has(&F, "No friends yet") && !frd_legend(&F) && !panel_has(&F, "Dots:"));   /* no legend line any more: the status words say it */
    CHECK(!strcmp(frd_dot_label(FRD_DOT_GREEN), "Ready") && !strcmp(frd_dot_label(FRD_DOT_YELLOW), "Busy") && !strcmp(frd_dot_label(FRD_DOT_RED), "In match") && !strcmp(frd_dot_label(FRD_DOT_GREY), "Offline") && !strcmp(frd_dot_label(FRD_DOT_REQ), "Request"));
    F.page = 0; frd_frame(&F, 0, 0, 0);
    CHECK(!strcmp(frd_hint(&F), "A: Accept  X: Decline  Z: Add a friend  B: Back"));                  /* on a request row: A accepts, X declines */
    /* paging by 9 with L and R, and the cursor */
    press(&F, FRD_B_R); CHECK(F.first == 9);
    press(&F, FRD_B_R); CHECK(F.first == 13);
    press(&F, FRD_B_L); CHECK(F.first == 4);
    press(&F, FRD_B_L); CHECK(F.first == 0);
    F.page = 0;
    for (i = 0; i < 12u; i++) press(&F, FRD_B_DOWN);
    CHECK(F.page == 8 && F.first == 4);
    for (i = 0; i < 30u; i++) press(&F, FRD_B_DOWN);
    CHECK((unsigned)F.first + F.page == 21u);                                                        /* stops at the last row */
    k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 2u && idx == 19u);
    CHECK(panel_has(&F, "Seen") == 0 && title_has(&F, "Offline"));                                   /* seen 255: no day */
    for (i = 0; i < 40u; i++) press(&F, FRD_B_UP);
    CHECK(F.first == 0 && F.page == 0);
    /* a request: A accepts (FRIEND_ANSWER yes), X declines; both are taps */
    CHECK(panel_has(&F, "wants to be friends") && panel_has(&F, "Connect code RQ#1") && (frd_req_badge(&F, ln), !strcmp(ln, "Friend request")));
    press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_ANSWER && F.arg_yes == 1 && !strcmp(F.arg_code, "RQ#1") && F.scr == FRD_BUSY);
    F.req = FRD_REQ_NONE;
    frd_net_done(&F, FRD_REQ_ANSWER, 0);
    CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_LIST && !strcmp(F.msg, "Friend added"));
    F.req = FRD_REQ_NONE;
    F.first = 0; F.page = 1;                                                                          /* the answered request stays in this model until the refresh lands: RQ#2 is row 1 */
    press(&F, FRD_B_X);
    CHECK(F.req == FRD_REQ_ANSWER && F.arg_yes == 0 && !strcmp(F.arg_code, "RQ#2"));
    F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_ANSWER, FRD_W_NOTFOUND);
    CHECK(F.scr == FRD_LIST && F.msg && !strcmp(F.msg, "Not found") && F.msg_warn);
    /* a friend: there is no action page.  A = the obvious action: Mano a mano (online) -> INVITE_SEND, the wait, accepted -> play */
    F.first = 0; F.page = 2;                                                                          /* Friend 0 (row 2), in the menus */
    CHECK(!strcmp(frd_hint(&F), "A: Mano a mano  X: Remove  Z: Add a friend  B: Back"));
    press(&F, FRD_B_A);
    CHECK(F.scr == FRD_BUSY && F.req == FRD_REQ_INVITE && !strcmp(F.arg_code, "F0#0"));
    {   /*: the list is not blanked while a request runs or a challenge waits: rows, dots, the cursor row and the online box stay */
        char nm[40], num[16], cd[16], bx[8];
        CHECK(frd_rows(&F) > 2u && frd_cur_page(&F) == 2u && frd_dot(&F, 2) != FRD_DOT_NONE && frd_row_parts(&F, 2, num, nm, cd) && !strcmp(nm, "Friend 0") && frd_box(&F, 0, bx) == 1u && bx[0]);
    }
    {   /* "Sending the challenge" never ended and B did nothing.  B cancels at once; 12 s of silence ends it with a plain message; both tell the glue to drop the request */
        static struct frd G;
        unsigned n;
        G = F;
        press(&G, FRD_B_B);
        CHECK(G.scr == FRD_LIST && G.req == FRD_REQ_NONE && G.abort == 1u && G.msg && !strcmp(G.msg, "Cancelled"));
        G = F; G.req = FRD_REQ_NONE;                                                                  /* taken by the glue, unanswered */
        for (n = 0; n < FRD_BUSY_FRAMES - 2u; n++) frd_frame(&G, 0, 0, 0);
        CHECK(G.scr == FRD_BUSY && !G.abort);
        for (n = 0; n < 4u; n++) frd_frame(&G, 0, 0, 0);
        CHECK(G.scr == FRD_LIST && G.abort == 1u && G.req == FRD_REQ_NONE && G.msg && !strcmp(G.msg, "No answer from the server") && strlen(G.msg) <= 25u && G.msg_warn);
        for (n = 0; n < 4u; n++) frd_frame(&G, 0, 0, 0);
        CHECK(G.scr == FRD_LIST);                                                                     /* stays out of BUSY */
    }
    F.req = FRD_REQ_NONE;
    frd_net_invite(&F, 4242);
    {
        char nm[40], num[16], cd[16], bx[8];
        CHECK(frd_rows(&F) > 2u && frd_cur_page(&F) == 2u && frd_dot(&F, 2) != FRD_DOT_NONE && frd_row_parts(&F, 2, num, nm, cd) && !strcmp(nm, "Friend 0") && frd_box(&F, 0, bx) == 1u && bx[0]);
    }
    CHECK(F.scr == FRD_WAIT && F.inv_id == 4242 && panel_has(&F, "Waiting for an answer") && (frd_req_badge(&F, ln), !strcmp(ln, "Friend 0")));
    for (i = 0; i < FRD_POLL_FRAMES; i++) frd_frame(&F, 0, 0, 0);
    CHECK(F.req == FRD_REQ_STATUS);
    F.req = FRD_REQ_NONE;
    frd_net_status(&F, 0, "");
    CHECK(F.scr == FRD_WAIT && !F.play);
    frd_net_status(&F, 1, "F0#0");
    CHECK(F.play_after == 1 && F.exit && !F.play && !strcmp(F.play_code, "F0#0") && F.answered == 1);   /* the Event screen closes first */
    frd_net_status(&F, 2, 0); CHECK(F.play_after == 1 && F.scr == FRD_WAIT);          /* a late poll does not undo it */
    frd_handoff(&F); CHECK(F.play == 1 && !F.play_after);
    /* not answered: back to the list with the text */
    F.play = 0; F.scr = FRD_LIST; press(&F, FRD_B_A); F.req = FRD_REQ_NONE; frd_net_invite(&F, 4243);
    frd_net_status(&F, 2, "");
    CHECK(F.scr == FRD_LIST && !strcmp(F.msg, "Not answered"));
    /* waiting gives up by itself after FRD_WAIT_FRAMES */
    press(&F, FRD_B_A); F.req = FRD_REQ_NONE; frd_net_invite(&F, 4244);
    for (i = 0; i < FRD_WAIT_FRAMES + 2u; i++) { frd_frame(&F, 0, 0, 0); F.req = FRD_REQ_NONE; }
    CHECK(F.scr == FRD_LIST && !strcmp(F.msg, "Not answered"));
    /* an offline friend: A does nothing (the footer has no A) */
    F.first = 0; F.page = 7;                                                                          /* Friend 5: offline */
    CHECK(!strstr(frd_hint(&F), "A:") && strstr(frd_hint(&F), "X: Remove"));
    press(&F, FRD_B_A);
    CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_NONE && F.sfx == FRD_SFX_DENY);
    /* X: Remove or block.  The panel asks (A Remove, Y Block, B Cancel); nothing else opens */
    press(&F, FRD_B_X);
    CHECK(F.scr == FRD_LIST && F.rm && (frd_req_badge(&F, ln), !strcmp(ln, "Remove Friend 5?")) && panel_has(&F, "Remove: you are no") && panel_has(&F, "Block: they can't"));
    CHECK(!strcmp(frd_hint(&F), "A: Remove  Y: Block  B: Cancel"));
    F.exit = 0;
    press(&F, FRD_B_B); CHECK(!F.rm && !F.exit);                                                      /* B cancels, it does not leave */
    press(&F, FRD_B_X); press(&F, FRD_B_DOWN); CHECK(!F.rm);                                          /* moving away cancels */
    press(&F, FRD_B_UP); press(&F, FRD_B_X);
    press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_REMOVE && !strcmp(F.arg_code, "F5#5") && !F.rm);
    F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_REMOVE, 0);
    CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_LIST && !strcmp(F.msg, "Friend removed"));
    F.req = FRD_REQ_NONE;
    F.first = 0; F.page = 7;
    press(&F, FRD_B_X); press(&F, FRD_B_Y);
    CHECK(F.req == FRD_REQ_BLOCK && !strcmp(F.arg_code, "F5#5"));
    F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_BLOCK, FRD_W_RATE);
    CHECK(F.scr == FRD_LIST && !strcmp(F.msg, "Too many requests, wait"));
    /* the code entry: letters, #, digits; a bad code is refused; A sends FRIEND_ADD */
    F.first = 0; F.page = 2;                                          /* a friend row: Z adds (on a request row Z is the hold) */
    F.rm = 0; press(&F, FRD_B_Z);
    CHECK(F.scr == FRD_CODE_ENTRY && !strcmp(F.cbuf, "A"));
    press(&F, FRD_B_UP); press(&F, FRD_B_UP);                       /* A -> C */
    press(&F, FRD_B_RIGHT);                                          /* "CA" */
    press(&F, FRD_B_DOWN);                                           /* A -> 9 (wraps backwards) */
    CHECK(!strcmp(F.cbuf, "C9"));
    press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_NONE && F.msg_warn && F.scr == FRD_CODE_ENTRY);
    for (i = 0; i < 9u; i++) press(&F, FRD_B_UP);                    /* 9 -> ... -> # (the set: A..Z # 0..9, upward from 9 wraps to A ... ) */
    F.cbuf[1] = '#';
    press(&F, FRD_B_RIGHT);
    CHECK(!strcmp(F.cbuf, "C#1"));
    CHECK(({ char n_[8], t_[32]; row(&F, 0, n_, t_); !strcmp(t_, "Code  C#[1]"); }));
    press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_ADD && !strcmp(F.arg_code, "C#1") && F.scr == FRD_BUSY);
    F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_ADD, FRD_W_NOTFOUND);
    CHECK(F.scr == FRD_LIST && frd_toast(&F, ln) == 2u && !strcmp(ln, "No player found for that code"));
    press(&F, FRD_B_Z); press(&F, FRD_B_RIGHT); press(&F, FRD_B_B);
    CHECK(F.scr == FRD_CODE_ENTRY && !strcmp(F.cbuf, "A"));          /* B deletes the last character first */
    press(&F, FRD_B_B);
    CHECK(F.scr == FRD_LIST);
    /* refresh failing keeps the list; B on the list leaves */
    frd_net_fail(&F, FRD_REQ_LIST, FRD_W_UNREACH);
    CHECK(F.net == FRD_N_OK && F.nf);
    press(&F, FRD_B_B);
    CHECK(F.exit == 1);
    /* ---- empty and offline states ---- */
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_net_requests(&F, 0, 0, rq);
    frd_net_friends(&F, 0, 0, 0, page);
    CHECK(F.net == FRD_N_OK && frd_rows(&F) == 0 && !frd_legend(&F));
    CHECK((frd_req_badge(&F, ln), !strcmp(ln, "No friends yet")) && panel_has(&F, "Press Z to add one") && panel_has(&F, "a fight") && strstr(frd_hint(&F), "Z: Add a friend"));
    press(&F, FRD_B_Z);                                                                               /* Z adds a friend even with nothing in the list */
    CHECK(F.scr == FRD_CODE_ENTRY);
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_net_fail(&F, FRD_REQ_LIST, FRD_W_UNREACH);
    CHECK(F.net == FRD_N_UNREACH && panel_has(&F, "Server not available") && frd_rows(&F) == 0 && !strcmp(frd_hint(&F), "A: Try again  B: Back"));
    CHECK(frd_status_view(&F) == 1u);                                    /* the message is a status view: one line, drawn centred in the box (frd_ev.c) */
    { char ln[64]; CHECK(frd_panel(&F, 0, ln) == 3u && !strcmp(ln, "Server not available")); }     /* the warning attribute (the console uses its lighter coral) */
    press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_LIST && F.net == FRD_N_LOADING);
    /* ----: '@' codes, a friend on both networks = two adjacent rows, the full code on every row ---- */
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_net_requests(&F, 0, 0, rq);
    for (i = 0; i < 4u; i++) mk(&page[i], i, i == 0u ? 2u : 0u, 1u);
    strcpy(page[0].code, "TEST@123"); strcpy(page[0].name, "Nigzel Long");
    strcpy(page[0].scode, "TEST#1"); strcpy(page[0].sname, "Nigzel On Slipp");      /* 15 characters: the longest Slippi name */
    strcpy(page[2].code, "ABCD@999"); strcpy(page[2].name, "Twelve Chars");
    strcpy(page[2].scode, "ABCD#9999"); strcpy(page[2].sname, "");                   /* 9 characters: cut to the 8 the entry keeps */
    frd_net_friends(&F, 4, 0, 4, page);
    CHECK(F.nf == 4 && frd_rows(&F) == 4u);                                              /* one row per person: a friend with a Slippi code gets no second row */
    CHECK(frd_row_kind(&F, 0, &idx) == 2u && idx == 0u);
    CHECK(frd_row_kind(&F, 1, &idx) == 2u && idx == 1u);
    CHECK(frd_row_kind(&F, 2, &idx) == 2u && idx == 2u);
    CHECK(frd_row_kind(&F, 3, &idx) == 2u && idx == 3u);
    CHECK(frd_row(&F, 0, num, t) == 2u && !strcmp(t, "Nigzel Long  TEST@123") && frd_dot(&F, 0) == FRD_DOT_GREEN);
    {
        char nm[32], cd[16];
        CHECK(frd_row_parts(&F, 0, num, nm, cd) == 2u && !strcmp(nm, "Nigzel Long") && !strcmp(cd, "TEST@123"));   /* the console draws the code apart, dimmed */
    }
    {
        char o[32];
        frd_fit(o, "Wobble Pilot Long", "WOBB@123", 20); CHECK(!strcmp(o, "Wobble Pi.  WOBB@123") && strlen(o) == 20u);   /* the name is cut, the code never */
        frd_fit(o, "Ann", "ANN@1", 20); CHECK(!strcmp(o, "Ann  ANN@1"));
        frd_fit(o, "Somebody", "ABCD@123", 10); CHECK(!strcmp(o, "ABCD@123"));   /* no room for a name: the code alone */
    }
    F.first = 0; F.page = 0;                                                             /* the friend TEST@123 who also has a Slippi code */
    {
        char tb[32];
        frd_req_badge(&F, tb); CHECK(strcmp(tb, "Nigzel Long") != 0 && tb[0]);           /* the title bar says what the friend is doing, not the name */
        CHECK(panel_has(&F, "Connect code TEST@123") && panel_has(&F, "Slippi: Nigzel On Slipp TEST#1") && !panel_has(&F, "Not Verified") && !panel_has(&F, "not verified") && !frd_legend(&F));
        F.page = 1; CHECK(!panel_has(&F, "Slippi:") && !panel_has(&F, "No Slippi name"));   /* a friend without a Slippi name: no Slippi line at all */
        F.page = 0;
    }
    CHECK(!strcmp(frd_hint(&F), "A: Mano a mano  Y: Slippi  X: Remove  B: Back"));          /* Y only for a friend with a Slippi code; Z waits for a free slot */
    F.exit = 0;
    press(&F, FRD_B_Y);                                                                  /* Y: the glue arms the Slippi Direct search (play 3) once the Event screen is gone */
    CHECK(F.scr == FRD_LIST && F.play_after == 3 && F.exit && !F.play && !strcmp(F.play_code, "TEST#1") && F.req == FRD_REQ_NONE);
    frd_handoff(&F); CHECK(F.play == 3);
    F.play = 0; F.exit = 0;
    press(&F, FRD_B_A);                                                                  /* A: its Multishine invite, TAG@n */
    CHECK(F.req == FRD_REQ_INVITE && !strcmp(F.arg_code, "TEST@123"));
    F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_INVITE, FRD_W_RATE);
    F.scr = FRD_LIST; F.page = 1; press(&F, FRD_B_Y);                                    /* a friend without a Slippi code: Y does nothing */
    CHECK(!F.play_after && !F.exit && !strstr(frd_hint(&F), "Slippi"));
    F.scr = FRD_LIST; F.first = 0; F.page = 0;
    /* the code entry offers '@' right after Z, then '#' */
    press(&F, FRD_B_Z);
    CHECK(F.scr == FRD_CODE_ENTRY && !strcmp(F.cbuf, "A"));
    press(&F, FRD_B_DOWN); CHECK(F.cbuf[0] == '9');
    F.cbuf[0] = 'Z'; press(&F, FRD_B_UP); CHECK(F.cbuf[0] == '@');
    press(&F, FRD_B_UP); CHECK(F.cbuf[0] == '#');
    strcpy(F.cbuf, "TEST@"); F.cpos = 4;
    press(&F, FRD_B_RIGHT); CHECK(!strcmp(F.cbuf, "TEST@1"));
    CHECK(({ char n_[8], t_[32]; row(&F, 0, n_, t_); !strcmp(t_, "Code  NIGZ[@]1") || !strcmp(t_, "Code  TEST@[1]"); }));
    press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_ADD && !strcmp(F.arg_code, "TEST@1"));
    F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_ADD, FRD_W_NOTFOUND);
    press(&F, FRD_B_Z); strcpy(F.cbuf, "AB@"); F.cpos = 2; press(&F, FRD_B_A);
    CHECK(F.req == FRD_REQ_NONE && F.msg && !strcmp(F.msg, "A connect code is letters, then @ or #, then digits"));
    press(&F, FRD_B_B); press(&F, FRD_B_B); press(&F, FRD_B_B); press(&F, FRD_B_B);
    CHECK(F.scr == FRD_LIST);
    /* ---- a friend in a Game Night room: the Room status, Y joins, nothing else changes ---- */
    frd_enter(&F, 1);
    F.req = FRD_REQ_NONE;
    for (i = 0; i < 4u; i++) mk(&page[i], 40u + i, i == 3u ? 0u : 2u, 0);
    page[0].flags = 4; strcpy(page[0].room, "4821"); page[0].rflags = 2;       /* in a room, may skip the passcode */
    page[1].flags = 4; strcpy(page[1].room, "1357"); page[1].rflags = 3;       /* in a room, the passcode is asked */
    page[2].presence = 4;                                                        /* playing a set in a room this viewer may not see: Match and no room */
    page[3].flags = 4; strcpy(page[3].room, "9999"); page[3].rflags = 9;        /* a bad room flags byte: the tail is dropped */
    frd_net_friends(&F, 4, 0, 4, page);
    CHECK(F.f[0].room[0] && F.f[1].room[0] && !F.f[3].room[0] && !(F.f[3].flags & 4u));
    CHECK(frd_dot(&F, 0) == FRD_DOT_ROOM && frd_dot(&F, 1) == FRD_DOT_ROOM && frd_dot(&F, 2) == FRD_DOT_RED && frd_dot(&F, 3) == FRD_DOT_GREY);
    CHECK(!strcmp(frd_dot_label(FRD_DOT_ROOM), "Room") && !strcmp(frd_dot_word(FRD_DOT_ROOM), "Room"));
    CHECK(!strcmp(frd_dot_label(FRD_DOT_RED), "In match"));
    CHECK(!strcmp(frd_hint(&F), "A: Join room  X: Remove  Z: Add a friend  B: Back"));
    CHECK(title_has(&F, "In a Game Night room"));
    press(&F, FRD_B_DOWN); press(&F, FRD_B_DOWN);                               /* on the Match row: no A, no join */
    CHECK(!strcmp(frd_hint(&F), "X: Remove  Z: Add a friend  B: Back"));
    press(&F, FRD_B_A);
    CHECK(!F.join && !F.exit);
    press(&F, FRD_B_UP); press(&F, FRD_B_UP);                                   /* back on the first row */
    press(&F, FRD_B_A);
    CHECK(F.join == 1 && F.exit == 1 && !strcmp(F.join_code, "4821") && F.join_rf == 2u);
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_net_friends(&F, 4, 0, 4, page);
    press(&F, FRD_B_DOWN); press(&F, FRD_B_A);
    CHECK(F.join == 1 && !strcmp(F.join_code, "1357") && F.join_rf == 3u);     /* the passcode screen comes first in the browser */
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;                                   /* back from a room left with a B hold: the held B does not close the list */
    frd_net_friends(&F, 4, 0, 4, page);
    frd_frame(&F, FRD_B_B, 0, 0); frd_frame(&F, FRD_B_B, 0, 0);
    CHECK(!F.exit && F.scr == FRD_LIST);
    frd_frame(&F, 0, 0, 0); frd_frame(&F, FRD_B_B, 0, 0);
    CHECK(F.exit);
    page[2].flags = 4; strcpy(page[2].room, "2468"); page[2].rflags = 2;       /* playing a set in a room this viewer may see: Match, A joins, watch from the room */
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_net_friends(&F, 4, 0, 4, page);
    CHECK(frd_dot(&F, 2) == FRD_DOT_RED);
    press(&F, FRD_B_DOWN); press(&F, FRD_B_DOWN);
    CHECK(!strcmp(frd_hint(&F), "A: Join room  X: Remove  Z: Add a friend  B: Back"));
    CHECK(title_has(&F, "In a Game Night match"));
    press(&F, FRD_B_A);
    CHECK(F.join == 1 && !strcmp(F.join_code, "2468"));
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;                                      /* a friend in a watchable match: A watches */
    mk(&page[0], 70, 4, 0); page[0].flags = 32; page[0].gmode = 1; page[0].gflags = 1;
    frd_net_friends(&F, 1, 0, 1, page);
    CHECK(!strcmp(frd_hint(&F), "A: Watch  X: Remove  Z: Add a friend  B: Back"));
    press(&F, FRD_B_A);
    CHECK(F.wchk && F.req == FRD_REQ_LIST && !F.exit && !F.play_after && !strcmp(F.play_code, "F0#70"));      /* the server is asked first: nothing is left yet */
    F.req = FRD_REQ_NONE; frd_net_friends(&F, 1, 0, 1, page);                                                /* still watchable in the fresh answer */
    CHECK(F.play_after == 4 && F.exit && !F.wchk);
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;                                      /* a searching friend: A offers nothing; amber with its own word */
    mk(&page[0], 60, 3, 0); mk(&page[1], 61, 4, 0);
    frd_net_friends(&F, 2, 0, 2, page);
    CHECK(frd_dot(&F, 0) == FRD_DOT_SEARCH && frd_dot(&F, 1) == FRD_DOT_RED && !strcmp(frd_dot_label(FRD_DOT_SEARCH), "In queue") && title_has(&F, "Searching for a match") && !strstr(frd_hint(&F), "A:"));
    frd_enter(&F, 0);                                                            /* example data has no rooms: Y on any row only pages */
    press(&F, FRD_B_Y);
    CHECK(!F.join);
    /* ---- example data and the text view: every line fits ---- */
    frd_enter(&F, 0);
    {
        struct sg_view v;
        unsigned scr;
        memset(&v, 0, sizeof v);
        sg_cut_count = 0;
        for (scr = 0; scr < 4u; scr++) {
            frd_text(&F, &v);
            for (i = 0; i < FRD_PANEL; i++) { frd_panel(&F, i, ln); CHECK(strlen(ln) <= FRD_LINE); }
            for (i = 0; i < FRD_VIS; i++) { frd_row(&F, i, num, t); CHECK(strlen(t) <= FRD_TITLE); }
            press(&F, FRD_B_DOWN);
        }
        CHECK(sg_cut_count == 0);
        CHECK(!strcmp(v.t[0], "Friends"));
    }
    /* ---- the online box: example (mock) friends count, singular, S rows and requests never count, empty until the list is loaded ---- */
    frd_enter(&F, 0);
    CHECK(frd_online(&F) >= 1u && frd_box(&F, 0, ln) == 1u && ln[0] >= '1' && ln[0] <= '9');
    F.nf = 1; F.f[0].presence = 2; F.nrq = 3;
    CHECK(frd_online(&F) == 1u && !strcmp((frd_box(&F, 0, ln), ln), "1") && !strcmp((frd_box(&F, 1, ln), ln), "online") && !(frd_box(&F, 2, ln), ln)[0]);
    F.f[0].presence = 0; F.f[0].scode[0] = 'A';
    CHECK(frd_online(&F) == 0u && !strcmp((frd_box(&F, 0, ln), ln), "0") && !strcmp((frd_box(&F, 1, ln), ln), "online"));
    F.f[0].presence = 1; F.f[0].flags = 4; strcpy(F.f[0].room, "1234");
    CHECK(frd_online(&F) == 1u);                                                  /* a friend in a Game Night room is online */
    frd_enter(&F, 1);
    CHECK(frd_box(&F, 0, ln) == 0u && !ln[0] && !frd_box(&F, 1, ln) && !ln[0]);   /* loading: nothing */
    /* ---- recent players: below the friends, one dim separator row "Recent players", no presence word, an action list of their own ---- */
    {
        struct frd_rc rc[3];
        unsigned sepi;
        frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        for (i = 0; i < 3u; i++) mk(&page[i], i, 2u, 0u);
        frd_net_friends(&F, 3, 0, 3, page);
        CHECK(frd_rows(&F) == 3u && F.nrc == 0);                                                   /* no recent players: no separator row at all */
        memset(rc, 0, sizeof rc);
        strcpy(rc[0].code, "RC1@1"); strcpy(rc[0].name, "Recent One");
        strcpy(rc[1].code, "RC2@2"); strcpy(rc[1].name, "Recent \"Two\"");
        strcpy(rc[2].code, "RC3@3"); strcpy(rc[2].name, "");
        frd_net_recent(&F, 3, 0, 3, rc);
        CHECK(F.nrc == 3 && !strcmp(F.rc[1].name, "Recent  Two ") && !strcmp(F.rc[2].name, "RC3@3"));
        CHECK(frd_rows(&F) == 3u + 1u + 3u);
        sepi = 3u;
        CHECK(frd_row_kind(&F, 2, &idx) == 2u && frd_row_kind(&F, sepi, &idx) == 4u && frd_row_kind(&F, sepi + 1u, &idx) == 5u && idx == 0u && frd_row_kind(&F, sepi + 3u, &idx) == 5u && idx == 2u);
        CHECK(frd_row(&F, (unsigned)sepi, num, t) == 1u && !strcmp(t, "Recent players") && !num[0] && frd_dot(&F, sepi) == FRD_DOT_NONE);
        CHECK(frd_row(&F, sepi + 1u, num, t) == 0u && !strcmp(t, "Recent One  RC1@1") && !num[0] && frd_dot(&F, sepi + 1u) == FRD_DOT_NONE && frd_mark(&F, sepi + 1u) == 0u);   /* no dot, no presence word, no friend marker */
        CHECK(frd_mark(&F, 0) == 1u);                                                              /* a friend row keeps its ring */
        CHECK(frd_online(&F) == 3u);                                                               /* recent players never count as online */
        F.page = 2;                                                                                /* the last friend */
        press(&F, FRD_B_DOWN);
        CHECK((unsigned)F.first + F.page == sepi + 1u);                                            /* the cursor steps over the separator onto the first recent player */
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 5u && idx == 0u);
        CHECK(panel_has(&F, "Recent player, not a friend yet") && !panel_has(&F, "Online") && !panel_has(&F, "Offline") && !panel_has(&F, "Seen"));
        press(&F, FRD_B_UP);
        CHECK((unsigned)F.first + F.page == 2u);                                                   /* and back over it */
        for (i = 0; i < 20u; i++) press(&F, FRD_B_DOWN);
        CHECK((unsigned)F.first + F.page == sepi + 3u);                                            /* stops at the last recent player */
        /* paging by 9 never leaves the cursor on the separator */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        for (i = 0; i < 8u; i++) mk(&page[i], i, 0u, 5u);
        frd_net_friends(&F, 9, 0, 8, page); F.req = FRD_REQ_NONE;
        mk(&page[0], 8u, 0u, 5u); frd_net_friends(&F, 9, 8, 1, page);
        frd_net_recent(&F, 3, 0, 3, rc);
        CHECK(frd_rows(&F) == 9u + 1u + 3u);
        for (i = 0; i < 9u; i++) { press(&F, FRD_B_R); k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k != 4u); press(&F, FRD_B_L); k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k != 4u); }
        for (F.first = 0, F.page = 0, i = 0; i < 8u; i++) press(&F, FRD_B_DOWN);
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 2u && idx == 8u);
        press(&F, FRD_B_DOWN);
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 5u && idx == 0u);
        /* A opens the recent list: Add friend first, no invite, no message, no presence */
        press(&F, FRD_B_A);
        CHECK(F.scr == FRD_ACT && F.a_rec == 1 && F.sel == 0 && F.a_cur == 0 && frd_rows(&F) == FRD_AR_N);
        row(&F, 0, num, t); CHECK(!strcmp(t, "Add friend"));
        row(&F, 1, num, t); CHECK(!strcmp(t, "Remove from recent"));
        row(&F, 2, num, t); CHECK(!strcmp(t, "Block"));
        row(&F, 3, num, t); CHECK(!strcmp(t, "Back"));
        row(&F, 4, num, t); CHECK(!t[0]);
        frd_req_badge(&F, ln); CHECK(!strcmp(ln, "Recent One"));
        CHECK(panel_has(&F, "Recent player, not a friend yet") && panel_has(&F, "Send them a friend request"));
        press(&F, FRD_B_A);                                                                        /* Add friend: the ordinary FRIEND_ADD */
        CHECK(F.req == FRD_REQ_ADD && !strcmp(F.arg_code, "RC1@1") && F.scr == FRD_BUSY && F.back_to == FRD_LIST);
        F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_ADD, 0);
        CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_LIST && frd_toast(&F, ln) == 1u && !strcmp(ln, "Friend request sent to RC1@1") && !F.msg);
        F.req = FRD_REQ_NONE;
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 5u);
        press(&F, FRD_B_A); press(&F, FRD_B_DOWN);                                                 /* Remove from recent: no confirmation, my entry only */
        press(&F, FRD_B_A);
        CHECK(F.req == FRD_REQ_RMREC && !strcmp(F.arg_code, "RC1@1") && F.scr == FRD_BUSY);
        F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_RMREC, 0);
        CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_LIST && !strcmp(F.msg, "Removed from recent"));
        F.req = FRD_REQ_NONE;
        press(&F, FRD_B_A); press(&F, FRD_B_DOWN); press(&F, FRD_B_DOWN);                          /* Block: a second A confirms */
        press(&F, FRD_B_A);
        CHECK(F.req == FRD_REQ_NONE && F.a_confirm == 1 && panel_has(&F, "Press A again to block"));
        row(&F, 2, num, t); CHECK(frd_row(&F, 2, num, t) == 3u);
        press(&F, FRD_B_A);
        CHECK(F.req == FRD_REQ_BLOCK && !strcmp(F.arg_code, "RC1@1"));
        F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_BLOCK, 0);
        CHECK(F.scr == FRD_LIST && !strcmp(F.msg, "Blocked") && F.req == FRD_REQ_LIST);
        F.req = FRD_REQ_NONE;
        press(&F, FRD_B_A); press(&F, FRD_B_B);                                                    /* B backs out of the recent list; Back too */
        CHECK(F.scr == FRD_LIST && F.a_rec == 0);
        press(&F, FRD_B_A); press(&F, FRD_B_DOWN); press(&F, FRD_B_DOWN); press(&F, FRD_B_DOWN); press(&F, FRD_B_A);
        CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_NONE);
        /* only recent players, no friends and no requests: the separator is row 0, the cursor starts on the first recent player */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        frd_net_friends(&F, 0, 0, 0, page);
        frd_net_recent(&F, 3, 0, 3, rc);
        CHECK(frd_rows(&F) == 4u);
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 5u && idx == 0u);
        press(&F, FRD_B_UP);
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 5u && idx == 0u);     /* up from the first recent player stays (the separator cannot hold the cursor) */
        press(&F, FRD_B_Z);                                                                        /* Z is still "add by code" on every row */
        CHECK(F.scr == FRD_CODE_ENTRY);
        /* a refresh that fails keeps the list but a total failure clears the recent rows too */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        frd_net_friends(&F, 0, 0, 0, page); frd_net_recent(&F, 3, 0, 3, rc);
        frd_net_fail(&F, FRD_REQ_LIST, FRD_W_UNREACH);
        CHECK(F.nrc == 0 && F.net == FRD_N_UNREACH);
        /* a second page of recent players appends (offset 16), offset 0 replaces; more than the cache holds is cut */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        frd_net_friends(&F, 0, 0, 0, page);
        { struct frd_rc big[16]; memset(big, 0, sizeof big); for (i = 0; i < 16u; i++) { big[i].code[0] = 'R'; big[i].code[1] = (char)('A' + i); big[i].name[0] = 'n'; }
          frd_net_recent(&F, 20, 0, 16, big); CHECK(F.nrc == 16);
          frd_net_recent(&F, 20, 16, 16, big); CHECK(F.nrc == FRD_RC);
          frd_net_recent(&F, 1, 0, 1, big); CHECK(F.nrc == 1); }
        /* example data (no server): two recent players; add, remove and block are local */
        frd_enter(&F, 0);
        CHECK(F.nrc == 2 && frd_rows(&F) == 1u + 6u + 1u + 2u);               /* a request, six friends, the separator, two recent players */
        for (i = 0; i < 20u; i++) press(&F, FRD_B_DOWN);
        press(&F, FRD_B_A); press(&F, FRD_B_A);
        CHECK(F.scr == FRD_LIST && frd_toast(&F, ln) == 1u && !strncmp(ln, "Friend request sent to ", 23) && F.nrc == 2);
        press(&F, FRD_B_A); press(&F, FRD_B_DOWN); press(&F, FRD_B_A);
        CHECK(F.scr == FRD_LIST && F.nrc == 1 && !strcmp(F.msg, "Removed from recent"));
        press(&F, FRD_B_A); press(&F, FRD_B_DOWN); press(&F, FRD_B_DOWN); press(&F, FRD_B_A); press(&F, FRD_B_A);
        CHECK(F.scr == FRD_LIST && F.nrc == 0 && !strcmp(F.msg, "Blocked") && frd_rows(&F) == 1u + 6u);   /* the last recent player gone: the separator goes too */
        k = frd_row_kind(&F, (unsigned)F.first + F.page, &idx); CHECK(k == 2u);
        /* the text view and every line width, with recent players */
        frd_enter(&F, 0);
        {
            struct sg_view v;
            unsigned scr;
            memset(&v, 0, sizeof v);
            sg_cut_count = 0;
            for (scr = 0; scr < 14u; scr++) {
                frd_text(&F, &v);
                for (i = 0; i < FRD_PANEL; i++) { frd_panel(&F, i, ln); CHECK(strlen(ln) <= FRD_LINE); }
                for (i = 0; i < FRD_VIS; i++) { frd_row(&F, i, num, t); CHECK(strlen(t) <= FRD_TITLE); }
                press(&F, FRD_B_DOWN);
            }
            CHECK(sg_cut_count == 0);
            press(&F, FRD_B_A);
            for (scr = 0; scr < 4u; scr++) {
                frd_text(&F, &v);
                for (i = 0; i < FRD_PANEL; i++) { frd_panel(&F, i, ln); CHECK(strlen(ln) <= FRD_LINE); }
                for (i = 0; i < FRD_AR_N; i++) { frd_row(&F, i, num, t); CHECK(strlen(t) <= FRD_TITLE); }
                press(&F, FRD_B_DOWN);
            }
            CHECK(sg_cut_count == 0);
            frd_enter(&F, 0);
            for (i = 0; i < 9u; i++) press(&F, FRD_B_DOWN);
            CHECK(frd_rows(&F) == 10u);
            frd_text(&F, &v);
            { char all[2048]; all[0] = 0; for (i = 3; i < 14u; i++) { strcat(all, v.t[i]); strcat(all, "\n"); } CHECK(strstr(all, "Recent players") && strstr(all, "Edge Guard  PX8@88")); }
        }
    }
    /* ---- Watch a friend: the game tail decides the state line and the Watch action ---- */
    {
        struct frd_ent g[4];
        frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        memset(g, 0, sizeof g);
        mk(&g[0], 1, 4, 0); g[0].flags = 32; g[0].gmode = 0; g[0].gflags = 1;      /* Grandmaster, watchable */
        mk(&g[1], 2, 4, 0); g[1].flags = 32; g[1].gmode = 1; g[1].gflags = 0;      /* Dojo, setting says no */
        mk(&g[2], 3, 4, 0); g[2].flags = 32; g[2].gmode = 2; g[2].gflags = 1;      /* Mano a mano, watchable */
        mk(&g[3], 4, 4, 0); g[3].flags = 32; g[3].gmode = 9; g[3].gflags = 1;      /* an unknown mode: ignored, plain "In a match" */
        frd_net_friends(&F, 4, 0, 4, g);
        CHECK(F.f[0].flags & 32u && frd_watchable(&F.f[0]) && !frd_watchable(&F.f[1]) && frd_watchable(&F.f[2]) && !frd_watchable(&F.f[3]) && !(F.f[3].flags & 32u));
        CHECK(!strcmp(frd_game_name(0), "Grandmaster") && !strcmp(frd_game_name(1), "Dojo") && !strcmp(frd_game_name(2), "Mano a mano") && !frd_game_name(5)[0]);
        k = frd_row_kind(&F, 0, &idx); CHECK(k == 2u && idx == 0u);
        CHECK(title_has(&F, "In a game: Grandmaster"));
        CHECK(!strcmp(frd_hint(&F), "A: Watch  X: Remove  Z: Add a friend  B: Back"));
        press(&F, FRD_B_A);                                                                      /* A on a watchable friend: Watch (no action page): the server is asked before anything is left */
        CHECK(F.wchk == 1 && F.exit == 0 && F.play_after == 0 && F.req == FRD_REQ_LIST && F.scr == FRD_LIST && !strcmp(F.play_code, "F1#1"));
        { struct frd_ent g2[4]; memcpy(g2, g, sizeof g2); g2[0].gflags = 0;                      /* the game ended / has no stream any more: stay in Friends with the plain line */
          F.req = FRD_REQ_NONE; frd_net_friends(&F, 4, 0, 4, g2);
          CHECK(!F.wchk && !F.exit && !F.play_after && F.scr == FRD_LIST && F.msg && !strcmp(F.msg, "No game to watch right now") && !F.msg_warn); }
        frd_net_friends(&F, 4, 0, 4, g);
        press(&F, FRD_B_A); F.req = FRD_REQ_NONE;
        frd_net_fail(&F, FRD_REQ_LIST, FRD_W_NOSOC);                                             /* the check could not ask: nothing was left */
        CHECK(!F.wchk && !F.exit && !F.play_after && F.scr == FRD_LIST && F.msg_warn);
        frd_net_friends(&F, 4, 0, 4, g);
        press(&F, FRD_B_A); F.req = FRD_REQ_NONE; frd_net_friends(&F, 4, 0, 4, g);               /* watchable in the fresh answer */
        CHECK(F.exit == 1 && F.play_after == 4 && !strcmp(F.play_code, "F1#1") && F.play == 0 && F.req == FRD_REQ_NONE && F.scr == FRD_LIST);
        frd_handoff(&F);
        CHECK(F.play == 4 && F.play_after == 0);
        /* a friend whose setting says no: the state shows, A offers nothing */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE; frd_net_friends(&F, 4, 0, 4, g);
        press(&F, FRD_B_DOWN); press(&F, FRD_B_A);
        CHECK(F.scr == FRD_LIST && !F.exit && !F.play_after && title_has(&F, "In a game: Dojo") && !strstr(frd_hint(&F), "A:"));
        /* Mano a mano in a game: the state line names it; the unknown mode stays "In a match" */
        press(&F, FRD_B_DOWN);
        CHECK(title_has(&F, "In a game: Mano a mano"));
        press(&F, FRD_B_DOWN);
        CHECK(title_has(&F, "In a match"));
        /* a game tail only counts with the presence "in a match": a stale flag on an offline entry is dropped */
        mk(&g[0], 1, 0, 3); g[0].flags = 32; g[0].gmode = 0; g[0].gflags = 1;
        frd_enter(&F, 1); F.req = FRD_REQ_NONE; frd_net_friends(&F, 1, 0, 1, g);
        CHECK(!frd_watchable(&F.f[0]) && !(F.f[0].flags & 32u));
        /* example data: no game state, no Watch */
        frd_enter(&F, 0);
        for (i = 0; i < 6u; i++) CHECK(!frd_watchable(&F.f[i]));
    }
    {   /* quick chat on the list: the D-Pad picks a text for the friend under the cursor (qc_pick.h), messages in the panel */
        static struct frd Q;
        struct frd_ent fe[2];
        char tx[FRD_NTXT][FRD_TXT], hint[96];
        unsigned s;
        memset(&Q, 0, sizeof Q);
        mk(&fe[0], 0, 2, 0); mk(&fe[1], 1, 0, 3);
        for (s = 0; s < FRD_NTXT; s++) snprintf(tx[s], FRD_TXT, "T%u", s);
        tx[5][0] = 0;                                                                     /* an empty slot stays IN PLACE */
        strcpy(tx[9], "A text longer than this");
        frd_enter(&Q, 1); Q.req = FRD_REQ_NONE;
        frd_net_friends(&Q, 2, 0, 2, fe);
        frd_set_texts(&Q, (const char (*)[FRD_TXT])tx, FRD_NTXT);
        CHECK(Q.ntxt == 15u && !Q.txt[5][0] && !strcmp(Q.txt[6], "T6"));
        Q.hold_lim = 1000u; Q.tb = 10000u;
        /* off (the glue has not enabled it): the D-Pad scrolls as always, no hint */
        press(&Q, FRD_B_DOWN); CHECK(Q.page == 1u && !frd_qc_open(&Q));
        press(&Q, FRD_B_UP); CHECK(Q.page == 0u);
        strcpy(hint, frd_hint(&Q)); CHECK(!strstr(hint, "D-Pad"));
        Q.qc_on = 1;
        /* the hint says so, before Z */
        strcpy(hint, frd_hint(&Q)); CHECK(strstr(hint, "D-Pad: Chat") && strstr(hint, "B: Back") && strstr(hint, "X: Remove"));        /* A, X, the chat key, B: the line holds four */
        /* first press opens the group of that direction; the cursor does not move; the cross is the group's four texts */
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_DOWN, 0, 0);
        CHECK(frd_qc_open(&Q) && Q.page == 0u && Q.qc.group == 3u && !strcmp(Q.qc_code, "F0#0"));
        frd_qc_head(&Q, t); CHECK(!strcmp(t, "Chat: Down")); CHECK(title_has(&Q, "Chat: Down"));
        frd_qc_cell(&Q, 0, t); CHECK(!strcmp(t, "T12"));
        frd_qc_cell(&Q, 1, t); CHECK(!strcmp(t, "T13"));
        frd_qc_cell(&Q, 3, t); CHECK(!strcmp(t, "T15"));
        CHECK(panel_has(&Q, "Left: T13") && panel_has(&Q, "Up: T12"));                   /* the text view's list of the same cells */
        strcpy(hint, frd_hint(&Q)); CHECK(!strcmp(hint, "D-Pad: Send  B: Close"));
        /* the second press sends the text at group * 4 + position */
        Q.tb += 20u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_LEFT, 0, 0);
        CHECK(!frd_qc_open(&Q) && Q.req == FRD_REQ_MSG && Q.arg_txt == 13u && !strcmp(Q.arg_code, "F0#0") && Q.msg_busy && Q.scr == FRD_LIST);
        CHECK(panel_has(&Q, "Sending"));
        /* a message is in flight: a second send is refused, the picker stays open */
        Q.req = FRD_REQ_NONE;                                                             /* the glue took it */
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_UP, 0, 0); CHECK(frd_qc_open(&Q));
        Q.tb += 300u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_UP, 0, 0);
        CHECK(frd_qc_open(&Q) && Q.req == FRD_REQ_NONE && Q.sfx == FRD_SFX_DENY);
        Q.tb += 10u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_B, 0, 0);       /* B closes the picker, not the list */
        CHECK(!frd_qc_open(&Q) && !Q.exit);
        /* the answer: "Sent", and the message is "You: ..." in that friend's panel */
        frd_net_done(&Q, FRD_REQ_MSG, 0);
        CHECK(!Q.msg_busy && Q.scr == FRD_LIST && panel_has(&Q, "Sent"));
        Q.msg = 0;
        /* a failure: the reason in the panel, nothing kept */
        Q.msg_busy = 1; frd_net_fail(&Q, FRD_REQ_MSG, FRD_W_RATE);
        CHECK(!Q.msg_busy && panel_has(&Q, "Too many requests"));
        Q.msg = 0;
        /* a long text is cut to 15 with a dot; the open group times out after 3 s */
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_RIGHT, 0, 0);   /* group Right: texts 8..11 */
        frd_qc_cell(&Q, 1, t); CHECK(t[14] == '.' && strlen(t) == 15u && !strncmp(t, "A text longer ", 14));
        Q.tb += 2999u; frd_frame(&Q, 0, 0, 0); CHECK(frd_qc_open(&Q));
        Q.tb += 2u; frd_frame(&Q, 0, 0, 0); CHECK(!frd_qc_open(&Q));
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_LEFT, 0, 0);     /* group Left: texts 4..7; slot 5 is empty */
        Q.tb += 300u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_RIGHT, 0, 0);      /* Left, Right = 4 + 2 = 6: ok */
        CHECK(Q.req == FRD_REQ_MSG && Q.arg_txt == 6u); Q.req = FRD_REQ_NONE; Q.msg_busy = 0;
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_LEFT, 0, 0);
        Q.tb += 300u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_LEFT, 0, 0);       /* Left, Left = 4 + 1 = 5: empty */
        CHECK(Q.req == FRD_REQ_NONE && !frd_qc_open(&Q) && panel_has(&Q, "No text there"));
        /* the stick scrolls and the D-Pad Up Down no longer do (the list keeps Left Right free too) */
        Q.msg = 0;
        frd_frame(&Q, 0, 0, 1); CHECK(Q.page == 1u);
        frd_frame(&Q, 0, 0, 0); frd_frame(&Q, 0, 1, 0); CHECK(Q.page == 0u);
        /* only a friend's row opens it: a request row does not */
        {
            struct frd_rq rq1[1];
            memset(rq1, 0, sizeof rq1); strcpy(rq1[0].code, "RQ#1"); strcpy(rq1[0].name, "Req");
            frd_net_requests(&Q, 1, 1, rq1);
            Q.first = 0; Q.page = 0;
            Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_UP, 0, 0);
            CHECK(!frd_qc_open(&Q));
        }
        /* no texts: a line, no picker */
        Q.nrq = 0; Q.nrc = 0; Q.first = 0; Q.page = 0;
        { char none[FRD_NTXT][FRD_TXT]; memset(none, 0, sizeof none); frd_set_texts(&Q, (const char (*)[FRD_TXT])none, FRD_NTXT); }
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_UP, 0, 0);
        CHECK(!frd_qc_open(&Q) && panel_has(&Q, "No quick chat texts"));
        /* example data (no server): the send is local, "Sent", and the line shows */
        frd_enter(&Q, 0); Q.qc_on = 1; Q.hold_lim = 1000u; Q.tb = 50000u;
        frd_frame(&Q, 0, 0, 1);                                                           /* off the request row onto the first friend */
        frd_set_texts(&Q, (const char (*)[FRD_TXT])tx, FRD_NTXT);
        Q.tb += 1000u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_UP, 0, 0);
        Q.tb += 300u; frd_frame(&Q, 0, 0, 0); Q.tb += 10u; frd_frame(&Q, FRD_B_UP, 0, 0);
        CHECK(Q.req == FRD_REQ_NONE && panel_has(&Q, "Sent"));
    }
    if (fails) { printf("%d failures\n", fails); return 1; }
    printf("friends model: all checks passed\n");
    {   /* Z with Slippi's keyboard: a hand-off, play 5; the typed code comes back through frd_add_code (sent as typed) */
        memset(&F, 0, sizeof F); frd_enter(&F, 0); F.live = 1; F.kbd = 1; F.net = FRD_N_OK; F.req = FRD_REQ_NONE;
        press(&F, FRD_B_Z); CHECK(F.exit && F.play_after == 5u && F.scr == FRD_LIST);
        memset(&F, 0, sizeof F); frd_enter(&F, 0); F.kbd = 0; F.req = FRD_REQ_NONE;
        press(&F, FRD_B_Z); CHECK(F.scr == FRD_CODE_ENTRY && !F.play_after);                 /* no hand-off available: our own entry */
        memset(&F, 0, sizeof F); frd_enter(&F, 0);
        frd_add_code(&F, "bot#1001"); CHECK(F.scr == FRD_LIST && !strcmp(F.cbuf, "BOT#1001") && !F.req);      /* not converted (offline model: says sent) */
        memset(&F, 0, sizeof F); frd_enter(&F, 0); F.live = 1; F.net = FRD_N_OK; F.req = FRD_REQ_NONE;
        frd_add_code(&F, "BOT@1001"); CHECK(F.req == FRD_REQ_ADD && !strcmp(F.arg_code, "BOT@1001"));
        F.req = FRD_REQ_NONE;
        frd_add_code(&F, "TESTER#1"); CHECK(F.req == FRD_REQ_ADD && !strcmp(F.arg_code, "TESTER#1"));       /* a Slippi code of 6 letters */
        F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_ADD, 3); CHECK(frd_toast(&F, ln) == 2u && !strcmp(ln, "No player found for that Slippi code"));
        frd_net_done(&F, FRD_REQ_ADD, 7); CHECK(frd_toast(&F, ln) == 2u && !strcmp(ln, "No player found for that code"));
        F.req = FRD_REQ_NONE; frd_add_code(&F, "BAD"); CHECK(F.req == FRD_REQ_NONE && F.msg && !strcmp(F.msg, "A connect code is letters, then @ or #, then digits"));
    }
    {   /* the Add-a-friend result is a toast, the cursor goes to the player the code belongs to */
        char tl[64];
        memset(&F, 0, sizeof F); frd_enter(&F, 0); F.live = 1; F.net = FRD_N_OK; F.req = FRD_REQ_NONE;
        { struct frd_ent e[12]; unsigned i; for (i = 0; i < 12u; i++) { mk(&e[i], i, 2, 0); snprintf(e[i].code, sizeof e[i].code, "ZZ@%u", i); } frd_net_friends(&F, 12, 0, 12, e); F.req = FRD_REQ_NONE; }
        { char cd[16]; unsigned k; strcpy(cd, "zz@9"); frd_add_code(&F, cd); F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_ADD, 2);
          k = frd_row_kind(&F, (unsigned)F.first + F.page, &k);
          CHECK(frd_toast(&F, tl) == 1u && !strcmp(tl, "Friend 9 is already on your list") && !F.msg && F.f[9].code[0] && (unsigned)F.first + F.page == (unsigned)F.nrq + 9u);
          press(&F, FRD_B_UP); CHECK(!frd_toast(&F, tl) && !tl[0]); }                         /* moving the cursor clears it */
        frd_add_code(&F, "NEW@77"); F.req = FRD_REQ_NONE; frd_net_done(&F, FRD_REQ_ADD, 0); CHECK(frd_toast(&F, tl) == 1u && !strcmp(tl, "Friend request sent to NEW@77"));
        { unsigned i; for (i = 0; i < FRD_TOAST_FRAMES; i++) frd_frame(&F, 0, 0, 0); CHECK(!frd_toast(&F, tl)); }      /* and it ends by itself */
        { char pb[64]; frd_panel(&F, 0, pb); CHECK(1); }
    }
    {   /* the Invites: rows after the requests, A accepts (Mano a mano: the glue's data check first), X declines / dismisses, a room invite opens the room, 10 s re-ask */
        struct frd_in iv[3];
        struct frd_ent e[2];
        struct frd_rq rq1[1];
        char nm[32], cd[16], pn[64], hint[80];
        unsigned i, idx;
        memset(&F, 0, sizeof F); frd_enter(&F, 1); F.req = FRD_REQ_NONE;
        memset(rq1, 0, sizeof rq1); strcpy(rq1[0].code, "RQ#1"); strcpy(rq1[0].name, "Req");
        frd_net_requests(&F, 1, 1, rq1);
        mk(&e[0], 0, 2, 0); mk(&e[1], 1, 0, 3);
        frd_net_friends(&F, 2, 0, 2, e); F.req = FRD_REQ_NONE;
        memset(iv, 0, sizeof iv);
        iv[0].type = 1; iv[0].id = 4242ul; strcpy(iv[0].code, "TEST@1"); strcpy(iv[0].name, "Nigzel"); iv[0].age = 125; iv[0].left = 175;
        iv[1].type = 2; iv[1].id = 77ul; strcpy(iv[1].code, "4242"); strcpy(iv[1].name, "Hosty"); iv[1].age = 20; iv[1].left = 280;
        frd_net_invites(&F, 2, iv);
        CHECK(F.nin == 2 && frd_rows(&F) == 1u + 2u + 2u);
        CHECK(frd_row_kind(&F, 0, &idx) == 1u && frd_row_kind(&F, 1, &idx) == 6u && idx == 0u && frd_row_kind(&F, 2, &idx) == 6u && idx == 1u && frd_row_kind(&F, 3, &idx) == 2u && idx == 0u);
        CHECK(frd_dot(&F, 1) == FRD_DOT_INV && !strcmp(frd_dot_label(FRD_DOT_INV), "Invite") && !strcmp(frd_dot_word(FRD_DOT_INV), "Invite"));
        CHECK(frd_row_parts(&F, 1, pn, nm, cd) == 2u && !strcmp(nm, "Nigzel") && !strcmp(cd, "2m"));
        CHECK(frd_row_parts(&F, 2, pn, nm, cd) == 2u && !strcmp(nm, "Hosty") && !strcmp(cd, "20s"));
        press(&F, FRD_B_DOWN);                                                              /* the cursor on the first invite */
        CHECK(frd_row_kind(&F, (unsigned)F.first + F.page, &idx) == 6u && title_has(&F, "Mano a mano") && panel_has(&F, "Invite from Nigzel") && panel_has(&F, "2m left"));
        strcpy(hint, frd_hint(&F)); CHECK(strstr(hint, "A: Accept") && strstr(hint, "X: Decline"));
        press(&F, FRD_B_DOWN);
        CHECK(title_has(&F, "Room invite") && panel_has(&F, "Invite from Hosty") && panel_has(&F, "Room 4242, ") && strstr(frd_hint(&F), "A: Join room") && strstr(frd_hint(&F), "X: Dismiss"));
        press(&F, FRD_B_UP);
        strcpy(F.inv_name, "Nigzel"); frd_net_fail(&F, FRD_REQ_INVITE, FRD_W_PENDING);                      /* one short line naming the open invite's target */
        CHECK(F.msg && !strcmp(F.msg, "Already invited Nigzel") && strlen(F.msg) <= 30u);
        F.inv_name[0] = 0; frd_net_fail(&F, FRD_REQ_INVITE, FRD_W_PENDING);
        CHECK(F.msg && !strcmp(F.msg, "Already invited a friend"));
        { unsigned w; for (w = 0; w <= FRD_W_TIMEOUT; w++) { frd_net_fail(&F, FRD_REQ_INVITE, w); CHECK(F.msg && strlen(F.msg) <= 30u); } }   /* every error line fits the box */
        /* A on a Mano a mano invite: nothing goes to the server until the glue has checked the data files */
        press(&F, FRD_B_A);
        CHECK(F.inv_chk == 1u && F.req == FRD_REQ_NONE && F.scr == FRD_LIST);
        frd_inv_checked(&F, 1);                                                               /* no Multishine system files: refused with a plain line, nothing sent */
        CHECK(!F.inv_chk && F.req == FRD_REQ_NONE && F.msg && strstr(F.msg, "system files not set up") && F.nin == 2);
        press(&F, FRD_B_A); frd_inv_checked(&F, 0);
        CHECK(F.req == FRD_REQ_INVRESP && F.arg_id == 4242ul && F.arg_yes == 1u && F.scr == FRD_BUSY);
        F.req = FRD_REQ_NONE; frd_net_invresp(&F, "TEST@1");
        CHECK(F.play_after == 1u && F.exit == 1u && !strcmp(F.play_code, "TEST@1") && F.msg && strstr(F.msg, "Accepted"));
        frd_handoff(&F); CHECK(F.play == 1u);
        /* gone: a plain line, the list is asked again */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE; frd_net_requests(&F, 0, 0, 0); frd_net_friends(&F, 1, 0, 1, e); F.req = FRD_REQ_NONE; frd_net_invites(&F, 2, iv);
        press(&F, FRD_B_DOWN); press(&F, FRD_B_X);
        CHECK(F.req == FRD_REQ_INVRESP && F.arg_yes == 0u && !F.inv_chk);                    /* a decline needs no data check */
        F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_INVRESP, FRD_W_GONE);
        CHECK(F.scr == FRD_LIST && F.req == FRD_REQ_LIST && F.msg && !strcmp(F.msg, "That invite is over"));
        F.req = FRD_REQ_NONE; frd_net_invites(&F, 1, iv + 1); CHECK(F.nin == 1);
        press(&F, FRD_B_UP);
        /* a room invite: accepted at the server first, then the list closes and the Game Night browser joins the room */
        press(&F, FRD_B_A);
        CHECK(F.req == FRD_REQ_INVRESP && F.arg_type == 2u && !F.inv_chk);
        F.req = FRD_REQ_NONE; frd_net_invresp(&F, "4242");
        CHECK(F.join == 1u && F.exit == 1u && !strcmp(F.join_code, "4242") && F.join_rf == 0u);
        /* the dismissed one: the list is asked again */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE; frd_net_requests(&F, 0, 0, 0); frd_net_friends(&F, 1, 0, 1, e); F.req = FRD_REQ_NONE; frd_net_invites(&F, 1, iv + 1);
        press(&F, FRD_B_X); F.req = FRD_REQ_NONE; frd_net_invresp(&F, "");
        CHECK(F.msg && !strcmp(F.msg, "Invite dismissed") && F.req == FRD_REQ_LIST);
        /* the Invites alone are asked every 10 s while the list is open; never while another request runs */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE; frd_net_requests(&F, 0, 0, 0); frd_net_friends(&F, 1, 0, 1, e); F.req = FRD_REQ_NONE;
        for (i = 0; i < FRD_INV_FRAMES - 2u; i++) frd_frame(&F, 0, 0, 0);
        CHECK(F.req == FRD_REQ_NONE);
        for (i = 0; i < 4u; i++) frd_frame(&F, 0, 0, 0);
        CHECK(F.req == FRD_REQ_INVITES);
        F.req = FRD_REQ_NONE; frd_net_fail(&F, FRD_REQ_INVITES, FRD_W_UNREACH); CHECK(F.scr == FRD_LIST && !F.msg);          /* a lost ask is silent */
        frd_net_invites(&F, 0, 0); CHECK(F.nin == 0 && frd_rows(&F) == 1u);
        /* offline example data: the invite row goes with a plain line */
        frd_enter(&F, 0); frd_net_invites(&F, 1, iv + 1);
        for (i = 0; i < 8u; i++) {
            if (frd_row_kind(&F, (unsigned)F.first + F.page, &idx) == 6u) break;
            press(&F, FRD_B_DOWN);
        }
        CHECK(frd_row_kind(&F, (unsigned)F.first + F.page, &idx) == 6u);
        press(&F, FRD_B_X); CHECK(F.nin == 0 && F.msg && !strcmp(F.msg, "Invite dismissed"));
        /* every text of the rows and the panel is drawable and fits */
        frd_enter(&F, 1); F.req = FRD_REQ_NONE; frd_net_friends(&F, 1, 0, 1, e); F.req = FRD_REQ_NONE;
        strcpy(iv[0].name, "A very long name!"); iv[0].age = 5999; iv[0].left = 65535;
        frd_net_invites(&F, 2, iv);
        for (i = 0; i < 3u; i++) {
            unsigned c, j;
            char ln[64], tit[FRD_TITLE + 1];
            if (i) press(&F, FRD_B_DOWN);
            frd_req_badge(&F, ln); CHECK(strlen(ln) <= (i < 2u ? 18u : 24u));
            for (c = 0; c < FRD_PANEL; c++) { frd_panel(&F, c, ln); CHECK(strlen(ln) <= FRD_LINE); for (j = 0; ln[j]; j++) CHECK(ln[j] >= 32 && ln[j] < 127); }
            for (c = 0; c < FRD_VIS; c++) { (void)frd_row(&F, c, ln, tit); CHECK(strlen(tit) <= FRD_TITLE); }
        }
    }
    return 0;
}
