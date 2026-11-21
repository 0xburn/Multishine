/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of the sign-in screens (sg_ui): navigation, no truncated row on any screen, the entropy test (pass with varied input, fail with an idle
   stick, A/B are input while collecting, a tap of B does not leave, a hold does), the empty-pool refusal, the TLS verdict table, the QR view. */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static unsigned long lcg = 99;
static unsigned rnd(void) { lcg = lcg * 6364136223846793005ul + 1442695040888963407ul; return (unsigned)(lcg >> 33); }
static unsigned tbv = 5000, dt_ticks = 675000u;
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void frame(unsigned btn, int sx, int sy)
{
    struct sdent_pad p;
    p.buttons = (unsigned short)btn; p.sx = (signed char)sx; p.sy = (signed char)sy; p.cx = 0; p.cy = 0; p.tl = 0; p.tr = 0; p.err = 0;
    tbv += dt_ticks + (rnd() % 400u);
    sg_frame(&G, &p, tbv);
}
static void tap(unsigned b) { unsigned k; frame(b, 0, 0); for (k = 0; k < SG_ACT_GAP; k++) frame(0, 0, 0); }      /* (the gate of sg_act_filter wants SG_ACT_GAP idle frames between two presses) */
static void wiggle(unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) frame(rnd() % 9u ? 0 : SG_B_A, (int)(rnd() % 161u) - 80, (int)(rnd() % 161u) - 80);
}
static unsigned bad_glyphs;
/* live text objects of a drawn screen: every row with text, every value and the selection bar (the mark is a sprite, the backdrop one more text); the Settings look needs the crumb, the values and the bar on top of
   the old rows, so the old cap of 8 became 12 on the Update screen (cap about 20, unmeasured on a console, O-8) */
static unsigned live(const struct sg_view *v)
{
    unsigned r, n = 0;
    for (r = 0; r < SG_ROWS; r++) { if (v->t[r][0]) n++; if (v->val[r][0]) n++; }
    return n + (v->bar ? 1u : 0u);
}
#define UPD_CAP 12u
static void draw_all(void)
{
    static struct sg_view v;
    unsigned r, c;
    sg_draw(&G, &v);
    for (r = 0; r < SG_ROWS; r++) {        /* a value must fit beside its label: measured in Dolphin: 11.7 to 12.7 px a character in the list font, 10.3 in the text font, 483 px between the margins; 38 characters of the list font leave a gap */
        if (v.val[r][0] && strlen(v.t[r]) + 2u + strlen(v.val[r]) > ((v.attr[r] & SG_A_BIG) ? 38u : 42u)) { bad_glyphs++; printf("label and value too wide together: %s | %s\n", v.t[r], v.val[r]); }
        if (strlen(v.val[r]) > SG_VAL) { bad_glyphs++; printf("value longer than %u characters: %s\n", (unsigned)SG_VAL, v.val[r]); }
        for (c = 0; v.val[r][c]; c++) {
            char ch = v.val[r][c];
            int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ';
            if (!ok) { bad_glyphs++; printf("glyph %c (0x%02x) not in the menu font: value of row %u of screen %u: %s\n", ch, (unsigned char)ch, r, G.scr, v.val[r]); }
        }
    }
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(v.t[r]) > 49u) { bad_glyphs++; printf("row longer than 49 characters (a longer row was cut in Dolphin): %s\n", v.t[r]); }
        for (c = 0; v.t[r][c]; c++) {
            char ch = v.t[r][c];
            int ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ' || ch == '#' || ch == '@';   /* '#' '@': connect codes, expanded to full width by the glue (ml_sis_expand) */
            if (!ok) { bad_glyphs++; printf("glyph %c (0x%02x) not in the menu font: row %u of screen %u: %s\n", ch, (unsigned char)ch, r, G.scr, v.t[r]); }
#if !SG_LAB
            if ((ch == 'l' || ch == 'L') && !strncmp(v.t[r] + c, "lab ", 4)) { bad_glyphs++; printf("release screen names the lab: %s\n", v.t[r]); }
            if (!strncmp(v.t[r] + c, "TLS", 3) || !strncmp(v.t[r] + c, "Lab server", 10)) { bad_glyphs++; printf("release screen mentions TLS or a lab server: %s\n", v.t[r]); }
#endif
        }
    }
}

static int any_row(const struct sg_view *v, const char *needle)
{
    unsigned r;
    for (r = 0; r < SG_ROWS; r++) if (strstr(v->t[r], needle)) return 1;
    return 0;
}

int main(void)
{
    static struct sg_view v;
    unsigned i, s;
    struct sg_tls_view t;

    /* every screen, in every mock state, fits its rows */
    sg_init(&G);
    for (s = 0; s < SG_NSCREEN; s++) {
        unsigned sub, prov, st;
        for (sub = 0; sub < SG_LK_N; sub++) for (prov = 0; prov < 4; prov++) for (st = 0; st < 2; st++) {
            sg_init(&G);
            sg_goto(&G, s);
            G.sub = (unsigned char)sub; G.prov = (unsigned char)prov; G.id_stage = (unsigned char)st; G.signed_in = (unsigned char)st; G.confirm = (unsigned char)st;
            G.tls.started = 1; G.tls.done = 1; G.tls.want_down = 20000; G.tls.down_bytes = 20000; G.tls.echo_ok = 1; G.tls.hello_ok = 1; G.tls.https_status = 200; G.tls.https_bytes = 4000;
            G.tls.heavy_max_us = 123456; G.tls.elapsed_ms = 4000000u; G.tls.err = -9; G.tls.err_phase = 4;
            { const char *sv = "255.255.255.255:65535"; for (i = 0; sv[i]; i++) G.tls.server[i] = sv[i]; }
            draw_all();
        }
    }
    CHECK(sg_cut_count == 0);
    CHECK(bad_glyphs == 0);

    /* navigation: Settings > Account IS the identity page (Status, Name, Connect code, Slippi login, then Link this console when not linked, Region, Send Slippi login,
       Delete); Delete opens the page of the three levels, B there returns to the Delete row; B on Account leaves to the Settings list.  Settings > Friends: the three settings. */
    sg_init(&G); G.linked = SG_ID_LINKED;
    sg_hub_page(&G, 1); CHECK(G.scr == SG_IDENT && G.cur == 0 && G.id_top);
    { unsigned char it[4]; CHECK(sg_acct_items(&G, it) == 3u && it[0] == SG_AI_REGION && it[1] == SG_AI_SLOGIN && it[2] == SG_AI_DELETE); }   /* linked: no Link row */
    tap(SG_B_DOWN); CHECK(G.cur == 1); tap(SG_B_A); CHECK(G.scr == SG_SLOGIN);
    tap(SG_B_B); CHECK(G.scr == SG_IDENT && G.cur == 1);
    tap(SG_B_DOWN); tap(SG_B_A); CHECK(G.scr == SG_HUB && G.onp && G.cur == SG_ON_DEL && G.del_acct);    /* Delete: the three levels */
    tap(SG_B_A); CHECK(G.scr == SG_DELID && G.del.st == SGD_HOLD && G.del.level == 1 && G.del.from_ident == 2);
    tap(SG_B_B); CHECK(G.scr == SG_HUB && G.onp && G.cur == SG_ON_DEL && G.del_acct);
    tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.cur == SG_ON_DEL3); tap(SG_B_A); CHECK(G.scr == SG_DELID && G.del.level == 3);
    tap(SG_B_B); CHECK(G.scr == SG_HUB && G.cur == SG_ON_DEL3);
    tap(SG_B_B); CHECK(G.scr == SG_IDENT && G.cur == 2 && !G.del_acct);                  /* back on Account, on Delete */
    tap(SG_B_B); CHECK(G.quit);
    sg_init(&G); G.linked = SG_ID_GUEST; sg_hub_page(&G, 1);
    { unsigned char it[4]; CHECK(sg_acct_items(&G, it) == 4u && it[0] == SG_AI_LINK); }
    tap(SG_B_A); CHECK(G.scr == SG_LINK);
    sg_goto(&G, SG_HUB); CHECK(G.scr == SG_IDENT && G.cur == 0);                         /* the link screen's leave (sg_link.c, Hold B on its entropy pass) returns to Account */
    sg_init(&G); G.linked = SG_ID_LINKED; sg_hub_page(&G, 2); CHECK(G.scr == SG_HUB && G.onp == 2 && G.cur == SG_ON_POL && !G.id_top);
    tap(SG_B_DOWN); CHECK(G.cur == SG_ON_FIGHT); tap(SG_B_DOWN); CHECK(G.cur == SG_ON_WATCH); tap(SG_B_DOWN); CHECK(G.cur == SG_ON_POL);
    tap(SG_B_B); CHECK(G.quit);
    {   /* every label and description: capitalised labels, descriptions of at most 44 characters (the menu font: checked by draw_all) */
        unsigned r;
        for (r = 0; r < SG_ON_N; r++) { CHECK(strlen(sg_on_desc(r)) <= 44u && strlen(sg_on_desc(r)) > 0); CHECK(sg_on_label(r)[0] >= 'A' && sg_on_label(r)[0] <= 'Z'); }
        CHECK(!strcmp(sg_on_label(SG_ON_LINK), "Link this console") && !strcmp(sg_on_label(SG_ON_DEL), "Delete: Multishine only") );
    }
    {   /* the values on the pages: link state, the settings (guest: grey, A explains) */
        sg_init(&G); G.linked = SG_ID_LINKED; G.fight = 2; G.pol = SG_POL_ANYONE; G.rgn = 3; strcpy(G.id_name, "Nigzel"); strcpy(G.id_code, "TEST@966"); G.id_slippi = 2;
        sg_hub_page(&G, 1); sg_draw(&G, &v);                                              /* the Account page: identity on top, then Region (value), Send Slippi login, Delete */
        CHECK(!strcmp(v.t[0], "Account") && !strcmp(v.t[4], "Status") && !strcmp(v.val[4], "Linked") && !strcmp(v.t[6], "Name") && !strcmp(v.t[8], "Connect code") && !strcmp(v.t[10], "Slippi login"));
        CHECK(!strcmp(v.t[12], "Region") && !strcmp(v.val[12], "Europe") && !strcmp(v.t[14], "Delete") && v.bar == 12 && v.attr[12] == (SG_A_HI | SG_A_BIG) && v.attr[0] == SG_A_TITLE);   /* a login on the card: no Send row */
        G.id_slippi = 1; sg_draw(&G, &v);
        CHECK(!strcmp(v.t[14], "Send Slippi login") && !strcmp(v.t[16], "Delete"));
        { int n = 0, r; for (r = 0; r < SG_ROWS; r++) if (!strcmp(v.t[r], "Send Slippi login")) n++; CHECK(n == 1); }      /* once (it was on two pages) */
        G.scr = SG_HUB; G.onp = 1; G.cur = SG_ON_POL; sg_draw(&G, &v); CHECK(!strcmp(v.t[0], "Friends") && !strcmp(v.t[4], "Who can invite me") && !strcmp(v.val[4], "Anyone with my code") && !strcmp(v.t[6], "Friend requests after a fight") && !strcmp(v.val[6], "On") && !strcmp(v.t[8], "Who can watch my games"));
        G.cur = SG_ON_DEL; sg_draw(&G, &v); CHECK(!strcmp(v.t[0], "Delete") && !strcmp(v.t[4], "Delete: Multishine only") && !strcmp(v.t[8], "Delete: everything"));
        sg_init(&G); G.linked = SG_ID_GUEST; G.onp = 1; G.cur = SG_ON_POL; sg_draw(&G, &v);
        CHECK(!strcmp(v.t[4], "Who can invite me") && !v.val[4][0] && v.bar == 4 && v.attr[6] == (SG_A_DIM | SG_A_BIG) && v.attr[4] == (SG_A_HI | SG_A_BIG) && strstr(v.t[UI_R_DESC], "Needs an account") != 0);
        G.fight_req = 0; tap(SG_B_A); CHECK(G.fight_req == 0 && G.hint && !strcmp(G.hint, "Link this console first."));
        tap(SG_B_DOWN); CHECK(G.hint == 0);
        sg_init(&G); tap(SG_B_DOWN); tap(SG_B_DOWN); tap(SG_B_RIGHT); CHECK(G.upd.req == 0);      /* Left / Right on the list of pages change nothing */
    }
    {   /* every value of the Online page fits beside its label (draw_all measures label + value) */
        unsigned lk, fi, po;
        for (lk = 0; lk < 4; lk++) for (fi = 0; fi < 4; fi++) for (po = 0; po <= SG_POL_NA; po++) {
            sg_init(&G); G.linked = (unsigned char)lk; G.fight = (unsigned char)fi; G.pol = (unsigned char)po;
            for (i = 0; i < SG_ON_N; i++) { G.cur = (unsigned char)i; draw_all(); }
        }
    }
    {   /* the policy: Left / Right cycle Friends -> Anyone -> Nobody, the glue writes it (request 3); unknown is read again */
        sg_init(&G); G.linked = SG_ID_LINKED; G.onp = 1; G.cur = SG_ON_POL; G.pol = SG_POL_FRIENDS; G.fight = 2; G.fight_req = 0;
        tap(SG_B_RIGHT); CHECK(G.pol == SG_POL_ANYONE && G.fight_req == 3);
        G.fight_req = 0; tap(SG_B_RIGHT); CHECK(G.pol == SG_POL_NOBODY);
        G.fight_req = 0; tap(SG_B_RIGHT); CHECK(G.pol == SG_POL_FRIENDS);
        G.fight_req = 0; tap(SG_B_LEFT); CHECK(G.pol == SG_POL_NOBODY && G.fight_req == 3);
        G.fight_req = 0; G.pol = SG_POL_NA; tap(SG_B_A); CHECK(G.pol == SG_POL_UNKNOWN && G.fight_req == 1);
    }
    {   /* Region and Who can watch my games: cycle with Left / Right, the glue writes (requests 4 / 5), guests grey, an unavailable value is read again */
        sg_init(&G); G.linked = SG_ID_LINKED; G.onp = 1; G.cur = SG_ON_REGION; G.rgn = 3; G.wch = SG_WCH_FRIENDS; G.fight = 2; G.pol = SG_POL_FRIENDS; G.fight_req = 0;
        sg_draw(&G, &v); CHECK(!strcmp(v.t[v.bar], "Region") && !strcmp(v.val[v.bar], "Europe") && strstr(v.t[UI_R_DESC], "Where you play") != 0);
        tap(SG_B_RIGHT); CHECK(G.rgn == 4 && G.fight_req == 4);
        G.fight_req = 0; tap(SG_B_LEFT); tap(SG_B_LEFT); CHECK(G.rgn == 1 && G.fight_req == 4);      /* EU, Left to North America East, then Not set past the west end */
        G.rgn = 2; tap(SG_B_LEFT); CHECK(G.rgn == SG_RGN_NONE);                    /* Left of North America West (first, west to east): Not set */
        sg_draw(&G, &v); CHECK(!strcmp(v.val[v.bar], "Not set"));
        G.fight_req = 0; tap(SG_B_RIGHT); CHECK(G.rgn == 2 && G.fight_req == 4);   /* and round again */
        G.cur = SG_ON_WATCH; G.fight_req = 0; sg_draw(&G, &v); CHECK(!strcmp(v.t[v.bar], "Who can watch my games") && !strcmp(v.val[v.bar], "Friends"));
        tap(SG_B_RIGHT); CHECK(G.wch == SG_WCH_NOBODY && G.fight_req == 5);
        G.fight_req = 0; tap(SG_B_A); CHECK(G.wch == SG_WCH_FRIENDS && G.fight_req == 5);
        G.fight_req = 0; G.wch = SG_WCH_NA; G.rgn = SG_RGN_NA; tap(SG_B_A); CHECK(G.wch == 0 && G.fight_req == 1);       /* offline: A reads again */
        sg_init(&G); G.linked = SG_ID_GUEST; G.onp = 1; G.cur = SG_ON_WATCH; G.fight_req = 0; sg_draw(&G, &v);
        CHECK(!v.val[v.bar][0] && strstr(v.t[UI_R_DESC], "Needs an account") != 0);
        tap(SG_B_RIGHT); CHECK(G.fight_req == 0 && G.wch == 0 && G.hint && !strcmp(G.hint, "Link this console first."));
        G.cur = SG_ON_REGION; G.hint = 0; tap(SG_B_RIGHT); CHECK(G.fight_req == 0 && G.rgn == 0);
        sg_init(&G); sg_goto(&G, SG_HUB); CHECK(G.fight_req == 1);
        G.fight_req = 0; G.fight = 2; G.pol = SG_POL_FRIENDS; G.wch = 2; sg_goto(&G, SG_HUB); CHECK(G.fight_req == 1);     /* the region is still unknown: read on opening */
        G.fight_req = 0; G.rgn = 5; sg_goto(&G, SG_HUB); CHECK(G.fight_req == 0);
    }
    {   /* Update: the start check is always on, so no row for it; Back up before updating is the only option row, below the text (rows two apart) */
        unsigned n, st;
        sg_init(&G); sg_goto(&G, SG_UPD); sg_draw(&G, &v); CHECK(!any_row(&v, "Automatically search for updates") && !any_row(&v, "Backup to slot B"));   /* backup unknown: not shown; the start check never has a row (mandatory) */
        G.bak = 2; sg_draw(&G, &v); CHECK(!strcmp(v.t[19], "Back up before updating") && !strcmp(v.val[19], "On") && v.bar == 0); tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.upd_cur == 1); tap(SG_B_UP); tap(SG_B_UP); CHECK(G.upd_cur == 0);
        G.bak = 2; sg_draw(&G, &v); CHECK(!strcmp(v.t[19], "Back up before updating") && !strcmp(v.val[19], "On") && !v.bar);
        tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.upd_cur == 1);                                          /* stops at the last row */
        tap(SG_B_A); CHECK(G.bak == 1 && G.bak_req == 1 && G.upd.req == 0);
        sg_draw(&G, &v); CHECK(!strcmp(v.t[19], "Back up before updating") && !strcmp(v.val[19], "Off") && v.bar == 19 && v.attr[19] == (SG_A_HI | SG_A_BIG));
        G.bak_req = 0; tap(SG_B_RIGHT); CHECK(G.bak == 2 && G.bak_req == 1); G.bak_req = 0; tap(SG_B_LEFT); CHECK(G.bak == 1 && G.bak_req == 1);
        tap(SG_B_UP); CHECK(G.upd_cur == 0); tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_CHECK);
        G.upd.state = SGU_DOWNLOADING; sg_draw(&G, &v); CHECK(!any_row(&v, "Back up before"));
        {   /* Copy card to slot B: an action row below the switches, only where the host can (cpy_ok), never during an offer */
            unsigned st, no, ns;
            sg_init(&G); sg_goto(&G, SG_UPD); G.bak = 2; G.keep = 2; G.upd.state = SGU_UPTODATE; G.upd.installed = 1000; G.upd.run_rel = 1000; sg_draw(&G, &v);
            CHECK(!any_row(&v, "Set up Multishine on card slot B"));                                                  /* no host support: no row */
            G.cpy_ok = 1; sg_draw(&G, &v);
            CHECK(!strcmp(v.t[15], "Back up before updating") && !strcmp(v.t[17], "Keep previous build as backup") && !strcmp(v.t[19], "Set up Multishine on card slot B") && v.val[19][0] == 0);
            tap(SG_B_DOWN); tap(SG_B_DOWN); tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.upd_cur == 3);       /* three rows: the last one stops the cursor */
            sg_draw(&G, &v); CHECK(v.bar == 19 && strstr(v.t[UI_R_DESC], "Logins stay behind") != 0 && strstr(v.t[SG_ROWS - 2], "A: Copy") != 0);
            tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_COPYB && G.bak_req == 0 && G.keep_req == 0); G.upd.req = 0;
            tap(SG_B_LEFT); tap(SG_B_RIGHT); CHECK(G.upd.req == 0 && G.bak == 2 && G.keep == 2);         /* Left / Right do not copy */
            G.upd.copyb = SGU_CPY_WORK; sg_draw(&G, &v); CHECK(!strcmp(v.val[19], "Copying...") && strstr(v.t[UI_R_DESC], "Do not turn the power off") != 0);
            tap(SG_B_A); CHECK(G.upd.req == 0);                                                         /* a copy that runs is not asked twice */
            for (st = 1; st < SGU_BAK_N; st++) { G.upd.copyb = (unsigned char)st; sg_draw(&G, &v); CHECK(v.val[19][0] != 0 && v.t[UI_R_DESC][0] != 0); }
            G.upd.copyb = SGU_BAK_DONE; sg_draw(&G, &v); CHECK(!strcmp(v.val[19], "Done") && strstr(v.t[UI_R_DESC], "No logins were copied") != 0);
            G.upd.copyb = 0; G.upd.state = SGU_AVAILABLE; sg_draw(&G, &v); CHECK(!any_row(&v, "Set up Multishine on card slot B"));      /* an offer waits: no copy row */
            for (st = 0; st < SGU_NSTATE; st++) for (no = 0; no < 3; no++) for (ns = 0; ns < 3; ns += 2) {      /* every state x note: the page fits its rows (draw_all-style: no row twice, none past the footer) */
                sg_init(&G); sg_goto(&G, SG_UPD); G.bak = 2; G.keep = 2; G.cpy_ok = 1; G.upd.state = (unsigned char)st; G.upd.installed = 1000; G.upd.run_rel = 1000; G.upd.latest = 1001; G.upd.size_kb = 112;
                G.upd.note_ours = (unsigned char)no; G.upd.note_slippi = (unsigned char)ns; G.upd.slippi_new[0] = 3; G.upd.slippi_new[1] = 7; G.upd.slippi_new[2] = 255;
                sg_draw(&G, &v);
                if (st == SGU_AVAILABLE) CHECK(!any_row(&v, "Set up Multishine on card slot B"));
                if ((st == SGU_IDLE || st == SGU_UPTODATE || st == SGU_FAILED || st == SGU_OFFLINE) && !no && !ns) CHECK(any_row(&v, "Set up Multishine on card slot B"));
                if (st == SGU_DOWNLOADING || st == SGU_INSTALLING || st == SGU_VERIFYING || st == SGU_CHECKING || st == SGU_RESTART) CHECK(!any_row(&v, "Set up Multishine on card slot B"));
                if (no || ns) CHECK(!any_row(&v, "Set up Multishine on card slot B"));                                    /* any note: the row steps aside (the rows would reach the hint line) */
            }
        }
        sg_init(&G); sg_goto(&G, SG_UPD); G.bak = 2; G.upd.state = SGU_DOWNLOADING; sg_draw(&G, &v);
        {   /* an update on offer (also a required one) keeps every option row, so the switches are reachable BEFORE the install; the third row is Keep previous build as backup */
            unsigned no, tr;
            for (st = 0; st < SGU_NSTATE; st++) for (no = 0; no < 3; no++) for (tr = 0; tr < 5; tr++) {
                sg_init(&G); sg_goto(&G, SG_UPD); G.bak = 2; G.keep = 2;
                G.upd.state = (unsigned char)st; G.upd.installed = 1000; G.upd.run_rel = 1000; G.upd.latest = 1001; G.upd.size_kb = 112; G.upd.tier = (unsigned char)tr; G.upd.armed = (unsigned char)(tr == 4u);
                G.upd.note_ours = (unsigned char)no;
                sg_draw(&G, &v); CHECK(live(&v) <= UPD_CAP);
                if (st == SGU_AVAILABLE && no == 0 && tr == 0) CHECK(!any_row(&v, "Automatically search for updates") && any_row(&v, "Back up before updating") && any_row(&v, "Keep previous build as backup"));
                if (st == SGU_DOWNLOADING || st == SGU_INSTALLING || st == SGU_VERIFYING) CHECK(!any_row(&v, "Keep previous build"));
            }
            sg_init(&G); sg_goto(&G, SG_UPD); G.bak = 2; G.keep = 2; G.upd.state = SGU_AVAILABLE; G.upd.installed = 1000; G.upd.run_rel = 1000; G.upd.latest = 1001; G.upd.size_kb = 112;
            tap(SG_B_DOWN); tap(SG_B_DOWN); CHECK(G.upd_cur == 2);
            sg_draw(&G, &v); CHECK(v.bar && !strcmp(v.t[v.bar], "Keep previous build as backup") && !strcmp(v.val[v.bar], "On") && strstr(v.t[UI_R_DESC], "fallback") != 0);
            tap(SG_B_A); CHECK(G.keep == 1 && G.keep_req == 1 && G.upd.req == 0 && G.bak == 2);                  /* A changes the switch and does not start the download */
            sg_draw(&G, &v); CHECK(!strcmp(v.val[v.bar], "Off") && strstr(v.t[UI_R_DESC], "no fallback") != 0 && strlen(v.t[UI_R_DESC]) <= 49u);
            CHECK((unsigned)v.bar + 2u < UI_R_DESC);   /* the highlight of the last option row (2 grid rows) ends above the hint line (it covered the hint at row 21) */
            tap(SG_B_RIGHT); CHECK(G.keep == 2 && G.keep_req == 1); tap(SG_B_UP); tap(SG_B_UP); CHECK(G.upd_cur == 0);
            tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_DOWNLOAD || G.upd.req != 0);                                                     /* the first row still starts the install */
            sg_init(&G); sg_goto(&G, SG_UPD); G.keep = 0; G.bak = 2; G.upd.state = SGU_AVAILABLE; sg_draw(&G, &v); CHECK(!any_row(&v, "Keep previous build"));       /* unknown (older resident): no row */
        }
        /* the status line after an install, every result, within the row length and the object cap */
        for (st = 0; st < SGU_BAK_N; st++) {
            G.bak = 2; G.upd.state = SGU_RESTART; G.upd.latest = 5; G.upd.backup = (unsigned char)st; G.upd.noreset = (st & 1u) ? 1 : 0; G.upd.restart_s = 5;
            sg_draw(&G, &v);
            n = live(&v);
            CHECK(n <= UPD_CAP);
            if (st == SGU_BAK_DONE) { CHECK(any_row(&v, "Backup to slot B: done")); }
            if (st == SGU_BAK_OFF) { CHECK(any_row(&v, "Backup to slot B: off")); }
            if (st == SGU_BAK_NOCARD) { CHECK(any_row(&v, "no card in B, skipped")); }
        }
        G.upd.state = SGU_INSTALLING; G.upd.pct = 100; G.upd.kind = 0; G.bak = 2; sg_draw(&G, &v); CHECK(strstr(v.t[19], "slot B") != 0);
        G.bak = 1; sg_draw(&G, &v); CHECK(!v.t[19][0]);                                                   /* switched off: no promise on screen */
        { /* worst case on the idle screens: both options, an advisory and a Slippi note */
            G.bak = 2; G.upd.state = SGU_UPTODATE; G.upd.note_ours = SGU_NOTE_NEWER; G.upd.note_slippi = 2; G.upd.backup = 0; G.upd.kind = 0;
            sg_draw(&G, &v); n = live(&v); CHECK(n <= UPD_CAP && !any_row(&v, "Back up before"));                                  /* the Backup row steps aside when both notes are up */
        }
    }

    /* entropy test: varied input passes, and A/B are input rather than commands while collecting */
    sg_init(&G); sg_goto(&G, SG_ENT);
    CHECK(G.ent.state == SG_ET_RUN && G.ent.starved_ok);
    for (i = 0; i < 40; i++) { frame(SG_B_B, (int)(rnd() % 161u) - 80, (int)(rnd() % 161u) - 80); frame(0, 20, 20); }
    CHECK(G.scr == SG_ENT);                       /* short B presses do not leave */
    for (i = 0; i < 4000 && G.ent.state == SG_ET_RUN; i++) wiggle(1);
    CHECK(G.ent.state == SG_ET_PASS && G.ent.out_ok && G.ent.out_checked);
    CHECK(sdent_ready(&G.ent.e, SDENT_L_SESSION));
    CHECK(G.ent.ones > 924u && G.ent.ones < 1124u);
    frame(0, 0, 0); tap(SG_B_A); CHECK(G.ent.state == SG_ET_RUN && G.ent.e.mtotal == 0);     /* run again: a fresh pool */

    /* holding B leaves a collecting screen */
    for (i = 0; i < SG_HOLD_B_FRAMES + 2 && G.scr == SG_ENT; i++) frame(SG_B_B, 0, 0);
    CHECK(G.scr == SG_HUB);

    /* an idle stick times out and fails; nothing is credited, no bytes come out */
    sg_goto(&G, SG_ENT);
    for (i = 0; i < SG_ET_TIMEOUT_FRAMES + 5 && G.ent.state == SG_ET_RUN; i++) frame(0, 0, 0);
    CHECK(G.ent.state == SG_ET_FAIL && G.ent.why == 1 && !G.ent.out_checked && G.ent.e.mtotal == 0);
    { sdc_u8 b[8]; CHECK(sdent_bytes(&G.ent.e, SDENT_L_SESSION, b, 8) == SDENT_E_STARVED); }

    /* a digital-only pad (box controller): sticks and triggers at rest, irregular presses and holds of buttons only: the test passes within its time limit */
    sg_goto(&G, SG_ENT);
    for (i = 0; i < SG_ET_TIMEOUT_FRAMES && G.ent.state == SG_ET_RUN; i++) {
        static const unsigned short bt[7] = { SG_B_A, 0x400u, 0x800u, 0x10u, 0x20u, 0x40u, 0x1u };
        unsigned k, b = bt[rnd() % 7u], idle = 2u + rnd() % 13u, hold = 2u + rnd() % 8u;
        for (k = 0; k < idle; k++) frame(0, 0, 0);
        for (k = 0; k < hold; k++) frame(b, 0, 0);
    }
    CHECK(G.ent.state == SG_ET_PASS && G.ent.out_ok && G.ent.e.mbits[SDENT_STICK] == 0 && G.ent.e.mbits[SDENT_DIGI] > 0);
    frame(0, 0, 0);

    /* a perfectly regular sweep earns nothing (second difference zero) and fails */
    sg_goto(&G, SG_ENT);
    for (i = 0; i < SG_ET_TIMEOUT_FRAMES + 5 && G.ent.state == SG_ET_RUN; i++) frame(0, (int)((i * 3u) % 100u), (int)((i * 5u) % 100u));
    CHECK(G.ent.state == SG_ET_FAIL);

    /* make-key mock (kept for the Mockups hub; Device identity is the real read-only screen now): progress to the long-term level */
    sg_init(&G); sg_goto(&G, SG_MAKEKEY); CHECK(G.scr == SG_MAKEKEY);
    for (i = 0; i < 6000 && !G.sub; i++) wiggle(1);
    CHECK(G.sub == 1 && G.id_stage == 1 && sdent_ready(&G.ent.e, SDENT_L_LONGTERM));

#if !SG_LAB
    sg_init(&G); sg_goto(&G, SG_TLS); CHECK(G.scr == SG_HUB && !G.req_tls);        /* release: no TLS screen, no request */
    { unsigned k; for (k = 0; k < 4; k++) tap(SG_B_DOWN); CHECK(!G.onp && G.cur == 1); }   /* the list of three pages: 4 steps down wrap once and stop on the second */
    {   /* the fight setting row: read when the page opens, A toggles a known value (the glue writes it), an unknown one is read again */
        sg_init(&G); sg_goto(&G, SG_HUB); CHECK(G.fight_req == 1 && G.fight == 0);
        G.fight_req = 0; G.fight = 2; G.onp = 1; G.cur = SG_ON_FIGHT; tap(SG_B_A); CHECK(G.fight == 1 && G.fight_req == 2);
        G.fight_req = 0; G.fight = 3; tap(SG_B_A); CHECK(G.fight == 0 && G.fight_req == 1);
        G.fight = 2; sg_draw(&G, &v); CHECK(strstr(v.t[6], "Friend requests after a fight") != 0 && !strcmp(v.val[6], "On"));
    }
#endif
    /* the Update screen: every state fits its rows (draw_all checks length and glyphs), A and B raise the right requests, B never leaves a write */
    {
        unsigned st, rs, row_text = 0;
        for (st = 0; st < SGU_NSTATE; st++) for (rs = 0; rs < SGU_R_N; rs++) {
            sg_init(&G); sg_goto(&G, SG_UPD);
            G.upd.state = (unsigned char)st; G.upd.reason = (unsigned char)rs; G.upd.pct = (unsigned char)(st * 11u);
            G.upd.installed = 4000000000u; G.upd.latest = 4000000001u; G.upd.size_kb = 4000000u; G.upd.code = 4000000000u;
            draw_all();
            sg_draw(&G, &v);
            CHECK(live(&v) <= UPD_CAP); row_text++;
            CHECK(!strstr(v.t[SG_ROWS - 2u], "Up:") && !strstr(v.t[SG_ROWS - 2u], "Down:"));      /* no stick hint in a footer */
            if (st == SGU_FAILED && rs == SGU_R_UNREACH) {      /* the unreachable server is not 'busy' */
                unsigned r, found = 0, busy = 0;
                for (r = 0; r < SG_ROWS; r++) { if (strstr(v.t[r], "Could not reach the update server.")) found = 1; if (strstr(v.t[r], "busy")) busy = 1; }
                CHECK(found && !busy);
            }
        }
        /* with an advisory: the notes fit the 8 rows in every state and carry the right words */
        for (st = 0; st < SGU_NSTATE; st++) {
            unsigned no, ns;
            for (no = 0; no < 3; no++) for (ns = 0; ns < 3; ns++) {
                sg_init(&G); sg_goto(&G, SG_UPD);
                G.upd.state = (unsigned char)st; G.upd.installed = 1000; G.upd.run_rel = 1000; G.upd.latest = 1001; G.upd.size_kb = 112; G.upd.pct = (st == SGU_VERIFYING || st == SGU_INSTALLING) ? 100 : 50;      /* the core reports 100 there, except mid-way through a combined release (bar kept) */
                G.upd.note_ours = (unsigned char)no; G.upd.note_slippi = (unsigned char)ns; G.upd.slippi_new[0] = 3; G.upd.slippi_new[1] = 7; G.upd.slippi_new[2] = 255;
                draw_all();
                sg_draw(&G, &v);
                CHECK(live(&v) <= UPD_CAP);
                if (st == SGU_UPTODATE && ns == 1) CHECK(any_row(&v, "Slippi has a newer version, 3.7.255.") && any_row(&v, "may fail"));
                if (st == SGU_UPTODATE && ns == 2) CHECK(any_row(&v, "3.7.255 does not work with this build."));
                if (st == SGU_IDLE && no == 2) CHECK(strstr(v.t[16], "Update required") != 0 && strstr(v.t[16], "online") != 0);
                if (st == SGU_IDLE && no == 1) CHECK(strstr(v.t[16], "newer Multishine version") != 0);
                if (st == SGU_DOWNLOADING || st == SGU_INSTALLING) CHECK(!any_row(&v, "Slippi has") && !any_row(&v, "does not work with") && !any_row(&v, "newer Multishine") && !any_row(&v, "Update required"));
                { unsigned r; for (r = 5; r < UI_R_NOTE; r++) if (v.t[r][0] && r + 1u < UI_R_NOTE) CHECK(!v.t[r + 1u][0] || v.attr[r] == SG_A_DIM);   /* no two lines on back-to-back rows */ }          /* nothing extra while a download or a card write runs */
            }
        }
        CHECK(row_text > 0);
        CHECK(bad_glyphs == 0 && sg_cut_count == 0);
        sg_init(&G); G.upd.installed = 1000;                               /* the Update screen is a top-level Settings row (opened by goto) */
        sg_goto(&G, SG_UPD); CHECK(G.scr == SG_UPD);
        tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_CHECK); G.upd.req = 0;
        G.upd.state = SGU_CHECKING; tap(SG_B_A); CHECK(G.upd.req == 0);     /* A does nothing while checking */
        tap(SG_B_B); CHECK(G.scr == SG_UPD && G.upd.req == SGU_REQ_CANCEL); G.upd.req = 0;     /* B cancels, stays */
        G.upd.state = SGU_AVAILABLE; tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_DOWNLOAD); G.upd.req = 0;
        G.upd.state = SGU_DOWNLOADING; tap(SG_B_B); CHECK(G.scr == SG_UPD && G.upd.req == SGU_REQ_CANCEL); G.upd.req = 0;
        G.upd.state = SGU_VERIFYING; tap(SG_B_B); tap(SG_B_A); CHECK(G.scr == SG_UPD && G.upd.req == 0);
        G.upd.state = SGU_INSTALLING; tap(SG_B_B); tap(SG_B_A); CHECK(G.scr == SG_UPD && G.upd.req == 0);      /* the card is being written: no way out */
        G.upd.state = SGU_RESTART; tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_RESTART); G.upd.req = 0;
        tap(SG_B_B); CHECK(G.scr == SG_UPD && G.upd.req == 0);      /* the new build is on the card: no way back (the restart is automatic) */
        G.upd.noreset = 1; tap(SG_B_B); CHECK(G.scr == SG_HUB && G.upd.req == 0); G.upd.noreset = 0;      /* a host without a software reset: back to the hub on the Update row */
        G.upd.state = SGU_FAILED; sg_goto(&G, SG_UPD); tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_CHECK); G.upd.req = 0;
        G.upd.state = SGU_OFFLINE; tap(SG_B_A); CHECK(G.upd.req == SGU_REQ_CHECK); G.upd.req = 0;
        G.upd.state = SGU_UPTODATE; tap(SG_B_B); CHECK(G.scr == SG_HUB);
        /* the screen text names the numbers and the promises */
        sg_init(&G); sg_goto(&G, SG_UPD); G.upd.installed = 1000; G.upd.run_rel = 1000; G.upd.latest = 1001; G.upd.size_kb = 112;
        G.upd.state = SGU_AVAILABLE; sg_draw(&G, &v);
        CHECK(!strcmp(v.t[4], "Running: release 1000") && !strcmp(v.t[7], "Update available: release 1001") && strstr(v.t[10], "112 KB"));      /* three rows apart */
        CHECK(!strcmp(v.t[13], "Your account and settings stay as they are.") && !strcmp(v.t[SG_ROWS - 2], "A: Download  B: Back"));
        G.upd.state = SGU_RESTART; sg_draw(&G, &v);
        G.upd.restart_s = 5; sg_draw(&G, &v);
        CHECK(strstr(v.t[21], "Restarting in 5 seconds") && strstr(v.t[10], "account and settings stay") && !strcmp(v.t[SG_ROWS - 2], "A: Restart now"));
        G.upd.state = SGU_DOWNLOADING; G.upd.pct = 42; sg_draw(&G, &v);
        CHECK(strstr(v.t[13], " 42 percent") != 0);
        G.upd.state = SGU_INSTALLING; G.upd.pct = 100; sg_draw(&G, &v);
        CHECK(strstr(v.t[10], "Do not turn the power off") != 0);
        G.upd.state = SGU_INSTALLING; G.upd.pct = 42; sg_draw(&G, &v);      /* first part of a combined release: the heading and the bar do not change, the warning is added */
        CHECK(!strcmp(v.t[7], "Downloading...") && strstr(v.t[13], " 42 percent") && strstr(v.t[19], "Do not turn the power off"));
        G.upd.state = SGU_VERIFYING; sg_draw(&G, &v);
        CHECK(!strcmp(v.t[7], "Downloading...") && strstr(v.t[13], " 42 percent"));
    }
    /* hold B (MENU_SWEEP 5,: ONE hold threshold of 1.0 s): leaves after 1.0 s of REAL time (time base) whatever the frame rate, or after 30 frames if the time base does
       not tick.  frames_to_leave: frames with B held until the screen changes, at dt ticks per frame */
    CHECK(SG_HOLD_B_FRAMES == 90u && SG_HOLD_B_TICKS == 40500000u);     /*: at 59.94 Hz the old 30-frame guard ended the hold after 0.5 s; 90 is only a guard now */
    {
        unsigned k, dts[5], n;
        dts[0] = 675000u; dts[1] = 1350000u; dts[2] = 2000000u; dts[3] = 4000000u; dts[4] = 0u;
        for (n = 0; n < 5; n++) {
            unsigned scrs[2], s;
            scrs[0] = SG_ENT; scrs[1] = SG_MAKEKEY;
            for (s = 0; s < 2; s++) {
                unsigned left = 0, want;
                sg_init(&G); sg_goto(&G, scrs[s]);
                dt_ticks = dts[n];
                for (k = 1; k <= 120 && G.scr == scrs[s]; k++) { frame(SG_B_B, 0, 0); if (G.scr != scrs[s]) left = k; }
                dt_ticks = 675000u;
                CHECK(left != 0);
                /* elapsed time at frame k is (k - 1) * dt (the hold starts on frame 1): the first k with (k-1)*dt >= 40,500,000, at most 30 */
                want = 90u;
                if (dts[n]) { unsigned t = 1u + (SG_HOLD_B_TICKS + dts[n] - 1u) / dts[n]; if (t < want) want = t; }
                CHECK(left >= want && left <= want + 1u);                              /* +1: the small per-frame jitter of the test clock */
                CHECK(left <= 90u);
                if (dts[n] >= 2000000u) CHECK(left < 26u);                              /* slow frames: real time decides, long before the frame count */
            }
        }
        /* a time base that does not tick (dt 0): the 90-frame guard decides */
        dt_ticks = 0u;
        sg_init(&G); sg_goto(&G, SG_ENT);
        for (k = 0; k < SG_HOLD_B_FRAMES - 1u; k++) frame(SG_B_B, 0, 0);
        CHECK(G.scr == SG_ENT);                                                       /* 29 frames: still collecting */
        frame(SG_B_B, 0, 0); CHECK(G.scr == SG_HUB && G.cur == SG_ON_LINK);           /* the 30th leaves (not a direct open here) */
        sg_init(&G); sg_goto(&G, SG_MAKEKEY);
        for (k = 0; k < SG_HOLD_B_FRAMES - 1u; k++) frame(SG_B_B, 0, 0);
        CHECK(G.scr == SG_MAKEKEY);
        frame(SG_B_B, 0, 0); CHECK(G.scr == SG_IDENT);
        dt_ticks = 675000u;
        /* a console whose time base runs at 60.75 MHz (Wii): the overlay entry sets hold_lim; 3 M ticks per frame is 0.049 s there, so 1.0 s is 21 frames after the first */
        sg_init(&G); sg_goto(&G, SG_ENT); G.hold_lim = 60750000u;
        dt_ticks = 3000000u;
        for (k = 1; k <= 60 && G.scr == SG_ENT; k++) frame(SG_B_B, 0, 0);
        dt_ticks = 675000u;
        CHECK(G.scr == SG_HUB && k - 1u >= 20u && k - 1u <= 23u);
        /* a tap resets the hold (also after 29 frames, the longest hold that does not leave) */
        sg_init(&G); sg_goto(&G, SG_ENT);
        for (k = 0; k < 29; k++) frame(SG_B_B, 0, 0);
        frame(0, 0, 0);
        for (k = 0; k < 29; k++) frame(SG_B_B, 0, 0);
        CHECK(G.scr == SG_ENT);
        /* a long gap between two frames does not leave by itself on the first frame of a hold, and the clock restarts after a leave/return */
        sg_init(&G); sg_goto(&G, SG_ENT);
        dt_ticks = 100000000u; frame(SG_B_B, 0, 0); dt_ticks = 675000u;
        CHECK(G.scr == SG_ENT);
        /* time base wrap: the difference is taken in 32 bits */
        sg_init(&G); sg_goto(&G, SG_ENT);
        tbv = 0xFFFFFFFFu - 10000000u; dt_ticks = 2000000u;
        for (k = 1; k <= 60 && G.scr == SG_ENT; k++) frame(SG_B_B, 0, 0);
        dt_ticks = 675000u;
        CHECK(G.scr == SG_HUB && k - 1u <= 33u);
    }
    /* QR texture: every module decodes from the centre texel, the margin is white */
    {
        static unsigned char tex[SG_QR_TEX_BYTES], bits[QR_OUT_BYTES];
        unsigned char n = 0;
        unsigned r, c, errs = 0, off, stride;
        CHECK(qr_encode(SG_MOCK_URL, (unsigned)strlen(SG_MOCK_URL), bits, &n) == QR_OK);
        sg_qr_texture(bits, n, tex);
        stride = (n + 7u) / 8u; off = SG_QR_QUIET;                  /* quiet zone: 4 modules, the QR minimum */
        CHECK(sg_qr_dim(n) == 2u * (n + 2u * SG_QR_QUIET) && sg_qr_dim(n) <= SG_QR_TEX_DIM);
        for (r = 0; r < SG_QR_TEX_DIM; r++)
            for (c = 0; c < SG_QR_TEX_DIM; c++) {
                unsigned at = ((r >> 3) * (SG_QR_TEX_DIM / 8u) + (c >> 3)) * 32u + (r & 7u) * 4u + ((c & 7u) >> 1);
                unsigned nib = (c & 1u) ? (tex[at] & 15u) : (tex[at] >> 4), mx = c / 2u, my = r / 2u, want = 15u;
                if (mx >= off && mx < off + n && my >= off && my < off + n && ((bits[(my - off) * stride + ((mx - off) >> 3)] >> (7 - ((mx - off) & 7u))) & 1u)) want = 0u;
                if (nib != want) errs++;
            }
        CHECK(errs == 0);
    }
    /* TLS verdict */
    memset(&t, 0, sizeof t);
    CHECK(sg_tls_verdict(&t) == 0);
    t.started = 1; CHECK(sg_tls_verdict(&t) == 0);
    t.done = 1; t.want_down = 20000; t.down_bytes = 20000; t.echo_ok = 1; t.hello_ok = 1; t.https_status = 200; t.https_bytes = 100;
    CHECK(sg_tls_verdict(&t) == 1);
    t.heavy_max_us = SG_TLS_FRAME_US + 1; CHECK(sg_tls_verdict(&t) == 3);
    t.heavy_max_us = 0; t.https_status = 404; CHECK(sg_tls_verdict(&t) == 2);
    t.https_status = 200; t.down_bad = 1; CHECK(sg_tls_verdict(&t) == 2);
    t.down_bad = 0; t.failed = 1; CHECK(sg_tls_verdict(&t) == 2);
    CHECK(sg_cut_count == 0);

    /* About: every attribution, scrolled; down to the end shows the last line, up back to the top, B leaves */
    {
        unsigned k, found = 0;
        sg_init(&G); sg_goto(&G, SG_ABOUT);
        sg_draw(&G, &v);
        CHECK(!strcmp(v.t[8], "Licence") && !strcmp(v.t[22], "More below."));
        for (k = 0; k < 60u; k++) frame(SG_B_DOWN, 0, 0), frame(0, 0, 0);
        sg_draw(&G, &v);
        for (k = 8; k <= 20; k += 2) if (!strcmp(v.t[k], "Super Smash Bros. Melee: Nintendo, HAL.")) found = 1;
        CHECK(found && G.cur + 7u == sg_about_lines() && !strcmp(v.t[22], "End of the list."));
        for (k = 0; k < 60u; k++) frame(SG_B_UP, 0, 0), frame(0, 0, 0);
        sg_draw(&G, &v);
        CHECK(G.cur == 0 && !strcmp(v.t[8], "Licence"));
        tap(SG_B_B);
        CHECK(G.quit);
    }
    {   /*: an action button acts on a FRESH press only (sg_act_filter): held, a repeat-mask pulse train and a chattering contact give one press; Up Down and a steady hold are untouched */
        unsigned k, presses = 0, held = 0;
        struct sg F;
        memset(&F, 0, sizeof F);
        for (k = 0; k < 90u; k++) { unsigned o = sg_act_filter(&F, SG_B_A); if (o & SG_B_A) held++; }          /* A held 3 s: the held bit stays, as Hold A needs */
        CHECK(held == 90u);
        memset(&F, 0, sizeof F);
        for (k = 0; k < 90u; k++) { unsigned raw = (k < 12u || (k - 12u) % 3u == 0u) ? SG_B_A : 0u; unsigned o = sg_act_filter(&F, raw); static unsigned pr; if ((o & SG_B_A) && !(pr & SG_B_A)) presses++; pr = o; }
        CHECK(presses == 1u);                                                                                    /* the pulse train after the first 12 frames: still one press */
        memset(&F, 0, sizeof F); presses = 0;
        for (k = 0; k < 40u; k++) { unsigned o = sg_act_filter(&F, (k & 1u) ? SG_B_X : 0u); static unsigned px; if ((o & SG_B_X) && !(px & SG_B_X)) presses++; px = o; }
        CHECK(presses == 1u);                                                                                    /* chatter (one frame up between) */
        memset(&F, 0, sizeof F);
        CHECK((sg_act_filter(&F, SG_B_A) & SG_B_A) != 0u);
        for (k = 0; k < SG_ACT_GAP; k++) CHECK((sg_act_filter(&F, 0u) & SG_B_A) == 0u);
        CHECK((sg_act_filter(&F, SG_B_A) & SG_B_A) != 0u);                                                       /* a real second tap after the gap counts */
        CHECK((sg_act_filter(&F, SG_B_UP | SG_B_B) & (SG_B_UP | SG_B_B)) == (SG_B_UP | SG_B_B));                 /* not filtered */
    }
    printf(fails ? "sg_test: %d FAILED\n" : "sg_test: all passed\n", fails);
    return fails ? 1 : 0;
}
