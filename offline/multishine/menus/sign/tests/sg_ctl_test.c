/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of the Controller test screen (sg_ui.c): scripted pad structs go through sg_frame, the
   drawn rows and the flags are asserted: rest value, OFF CENTRE beyond +/-8, NOT CHANGING after 5 s of identical non-zero input, min/max,
   Z-hold freeze, B-hold leave, the port hint, glyphs of the menu font, no truncated row. */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static unsigned tbv = 1000, dt = 675000u;       /* 60 Hz at the GameCube's 40.5 MHz */
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void fr(unsigned btn, int sx, int sy, int cx, int cy, unsigned tl, unsigned tr)
{
    struct sdent_pad p;
    p.buttons = (unsigned short)btn; p.sx = (signed char)sx; p.sy = (signed char)sy; p.cx = (signed char)cx; p.cy = (signed char)cy;
    p.tl = (sdc_u8)tl; p.tr = (sdc_u8)tr; p.err = 0;
    tbv += dt;
    sg_frame(&G, &p, tbv);
}
static void rep(unsigned n, unsigned btn, int sx, int sy) { unsigned i; for (i = 0; i < n; i++) fr(btn, sx, sy, 0, 0, 0, 0); }
static struct sg_view V;
static void draw(void) { sg_draw(&G, &V); }
static int has(const char *s)
{
    unsigned r;
    draw();
    for (r = 0; r < SG_ROWS; r++) if (strstr(V.t[r], s) || strstr(V.val[r], s)) return 1;
    return 0;
}
/* row numbers below are the pre- ones (header on row 0 only); the Settings look moved the body down two grid rows (the last two one) */
static unsigned nr(unsigned r) { return r == 0 ? 0u : r >= 22u ? r + 1u : r + 2u; }
static const char *row(unsigned r) { draw(); return V.t[nr(r)]; }
static unsigned bad;
static void fits(void)
{
    unsigned r, c;
    draw();
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(V.val[r]) > SG_VAL) { bad++; printf("value too long: %s\n", V.val[r]); }
        if (strlen(V.t[r]) > 49u) { bad++; printf("row too long: %s\n", V.t[r]); }
        for (c = 0; V.t[r][c]; c++) {
            char ch = V.t[r][c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ')) {
                bad++; printf("glyph %c not in the menu font: %s\n", ch, V.t[r]);
            }
        }
    }
}
static void open_ctl(void) { sg_init(&G); sg_goto(&G, SG_CTL); G.direct = 1; tbv = 1000; }

int main(void)
{
    unsigned i;

    /* a resting healthy pad: rest value after 60 frames, centre ok, nothing flagged */
    open_ctl();
    CHECK(G.scr == SG_CTL);
    rep(59, 0, 3, -2); CHECK(has("Rest value   moving"));
    rep(2, 0, 3, -2);
    CHECK(strstr(row(10), "Rest value") && strstr(row(10), "X  003") && strstr(row(10), "Y -002"));
    CHECK(strstr(row(4), "raw 03 FE"));                                   /* signed byte as the SI sends it */
    CHECK(!has("OFF CENTRE") && has("Centre ok") && !has("NOT CHANGING"));
    CHECK(!G.ctl.off_centre && !G.ctl.not_changing && !G.ctl.moving);
    fits();

    /* OFF CENTRE beyond +/-8 in either axis of the main stick (rest only) */
    open_ctl(); rep(70, 0, 0, 8); CHECK(!G.ctl.off_centre && !has("OFF CENTRE"));
    open_ctl(); rep(70, 0, 0, 9); CHECK(G.ctl.off_centre && has("OFF CENTRE"));
    open_ctl(); rep(70, 0, 0, -9); CHECK(G.ctl.off_centre);
    open_ctl(); rep(70, 0, 9, 0); CHECK(G.ctl.off_centre);
    open_ctl(); rep(70, 0, -9, 0); CHECK(G.ctl.off_centre);
    open_ctl(); rep(70, 0, -8, 8); CHECK(!G.ctl.off_centre);
    open_ctl(); for (i = 0; i < 70; i++) fr(0, 0, -41 + (int)(i & 1u), 0, 0, 0, 0);      /* a steady offset with one count of jitter */
    CHECK(G.ctl.off_centre && has("OFF CENTRE") && !G.ctl.not_changing && !G.ctl.moving);
    CHECK(strstr(row(11), "Rest range") && strstr(row(11), "Y -041...-040"));
    /* a stick that is moved is "moving" and never flagged OFF CENTRE */
    open_ctl(); for (i = 0; i < 200; i++) fr(0, (int)(i % 40) * 3 - 60, 0, 0, 0, 0, 0);
    CHECK(G.ctl.moving && !G.ctl.off_centre && has("Rest value   moving"));
    fits();

    /* a drift of 1 count per 40 frames never trips the 3-count window as long as the window restarts */
    open_ctl(); for (i = 0; i < 400; i++) fr(0, 0, (int)(i / 40u), 0, 0, 0, 0);
    CHECK(!G.ctl.not_changing);

    /* NOT CHANGING: bit-identical input for 5 s with a non-zero buttons word or an axis beyond 8 */
    open_ctl(); rep(1, SG_B_A, 0, 0); rep(299, SG_B_A, 0, 0);
    CHECK(!G.ctl.not_changing);
    rep(2, SG_B_A, 0, 0); CHECK(G.ctl.not_changing && has("NOT CHANGING"));
    fr(0, 0, 0, 0, 0, 0, 0); CHECK(!G.ctl.not_changing && !has("NOT CHANGING"));        /* any change clears it */
    open_ctl(); rep(400, 0, 0, -41); CHECK(G.ctl.not_changing && G.ctl.off_centre);    /* the stuck-down signature */
    open_ctl(); rep(700, 0, 0, 0); CHECK(!G.ctl.not_changing && !has("NOT CHANGING") && !G.ctl.off_centre);   /* all zero and unchanging is normal */
    open_ctl(); rep(700, 0, 8, -8); CHECK(!G.ctl.not_changing);                         /* inside 8: normal */
    open_ctl(); for (i = 0; i < 400; i++) fr(0, 0, 0, 0, 0, 0, 120); CHECK(G.ctl.not_changing);       /* a stuck trigger counts as an axis */
    open_ctl(); for (i = 0; i < 400; i++) fr(0, 0, 0, 0, 0, 0, 5); CHECK(!G.ctl.not_changing);
    open_ctl(); for (i = 0; i < 400; i++) fr(0, 0, 0, 0, 20, 0, 0); CHECK(G.ctl.not_changing);       /* C stick */
    /* the time base decides too: 5 s of ticks in far fewer frames */
    open_ctl(); dt = 40500000u / 10u; rep(60, SG_B_A, 0, 0); CHECK(G.ctl.not_changing); dt = 675000u;
    open_ctl(); dt = 40500000u / 10u; rep(40, SG_B_A, 0, 0); CHECK(!G.ctl.not_changing); dt = 675000u;
    /* and a time base that does not tick falls back to the frame count */
    open_ctl(); dt = 0; rep(299, SG_B_A, 0, 0); CHECK(!G.ctl.not_changing); rep(3, SG_B_A, 0, 0); CHECK(G.ctl.not_changing); dt = 675000u;
    /* a Wii-rate lim carried by hold_lim scales the seconds */
    open_ctl(); G.hold_lim = 60750000u; dt = 1012500u; rep(310, SG_B_A, 0, 0); CHECK(G.ctl.not_changing); G.hold_lim = 0; dt = 675000u;

    /* the numbers: all fields, buttons by name, d-pad by name */
    open_ctl();
    fr(SG_B_A | SG_B_X | SG_B_START | SG_B_L | SG_B_UP | SG_B_RIGHT, -12, 99, 3, -128, 200, 7);
    CHECK(strstr(row(4), "X -012") && strstr(row(4), "Y  099") && strstr(row(4), "raw F4 63"));
    CHECK(strstr(row(5), "X  003") && strstr(row(5), "Y -128") && strstr(row(5), "raw 03 80"));
    CHECK(strstr(row(6), "L 200") && strstr(row(6), "R 007"));
    CHECK(strstr(row(7), "1548") == 0 && strstr(row(7), "Buttons") && strstr(row(7), "A X L Start"));
    CHECK(strstr(row(8), "Up Right"));
    CHECK(strstr(row(7), "100A") == 0);
    { char exp[8]; snprintf(exp, sizeof exp, "%04X", SG_B_A | SG_B_X | SG_B_START | SG_B_L | SG_B_UP | SG_B_RIGHT); CHECK(strstr(row(7), exp)); }
    fr(0, 0, 0, 0, 0, 0, 0); CHECK(strstr(row(7), "none") && strstr(row(8), "none"));
    fits();

    /* min and max since the page opened, all four axes; they stay after the stick returns */
    open_ctl();
    fr(0, 0, 0, 0, 0, 0, 0); fr(0, -50, 20, 5, -7, 0, 0); fr(0, 60, -30, -9, 70, 0, 0); rep(80, 0, 1, 0);
    CHECK(strstr(row(17), "Main X") && strstr(row(17), "-050") && strstr(row(17), " 060"));
    CHECK(strstr(row(18), "Main Y") && strstr(row(18), "-030") && strstr(row(18), " 020"));
    CHECK(strstr(row(19), "C X") && strstr(row(19), "-009") && strstr(row(19), " 005"));
    CHECK(strstr(row(20), "C Y") && strstr(row(20), "-007") && strstr(row(20), " 070"));
    CHECK(strstr(row(4), "X  001"));
    /* a fresh screen restarts the extremes */
    sg_goto(&G, SG_CTL); fr(0, 2, 2, 2, 2, 0, 0); CHECK(strstr(row(17), "002") && !strstr(row(17), "060"));

    /* Z held for 1 s freezes the display (once), another hold unfreezes; the pad keeps its own counters off while frozen */
    open_ctl();
    rep(10, 0, 4, 4);
    for (i = 0; i < 59; i++) fr(SG_B_Z, 4, 4, 0, 0, 0, 0);
    CHECK(!G.ctl.frozen);                                                  /* 59 frames of Z: not yet */
    fr(SG_B_Z, 4, 4, 0, 0, 0, 0); CHECK(G.ctl.frozen && has("FROZEN") && has("Display frozen"));
    for (i = 0; i < 200; i++) fr(SG_B_Z, 4, 4, 0, 0, 0, 0);
    CHECK(G.ctl.frozen);                                                   /* one toggle per hold */
    { char a[SG_COLS + 1]; unsigned fc = G.ctl.frames;
      strcpy(a, row(4));
      rep(30, 0, -77, 55);                                                /* the pad changes while frozen: the screen does not */
      CHECK(strcmp(row(4), a) == 0 && G.ctl.frames == fc && strstr(row(17), "-077") == 0);
      for (i = 0; i < 60; i++) fr(SG_B_Z, -77, 55, 0, 0, 0, 0);
      CHECK(!G.ctl.frozen && !has("FROZEN"));
      fr(0, -77, 55, 0, 0, 0, 0); CHECK(strstr(row(4), "X  -077") == 0 && strstr(row(4), "-077") && strstr(row(17), "-077"));
      CHECK(!G.ctl.not_changing);
    }
    /* a short tap of Z does not freeze */
    open_ctl(); rep(5, 0, 0, 0); fr(SG_B_Z, 0, 0, 0, 0, 0, 0); fr(0, 0, 0, 0, 0, 0, 0); fr(SG_B_Z, 0, 0, 0, 0, 0, 0); fr(0, 0, 0, 0, 0, 0, 0); CHECK(!G.ctl.frozen);
    /* the freeze by the time base: 1 s of ticks in a few frames */
    open_ctl(); dt = 40500000u / 4u; fr(SG_B_Z, 0, 0, 0, 0, 0, 0); fr(SG_B_Z, 0, 0, 0, 0, 0, 0); CHECK(!G.ctl.frozen);
    fr(SG_B_Z, 0, 0, 0, 0, 0, 0); fr(SG_B_Z, 0, 0, 0, 0, 0, 0); fr(SG_B_Z, 0, 0, 0, 0, 0, 0); CHECK(G.ctl.frozen); dt = 675000u;

    /* the stick and A do nothing here; a tap of B is shown as a button, only a hold leaves */
    open_ctl(); G.cur = 0;
    fr(0, 0, 90, 0, 0, 0, 0); fr(0, 0, -90, 0, 0, 0, 0); fr(SG_B_A, 0, 0, 0, 0, 0, 0); fr(0, 0, 0, 0, 0, 0, 0);
    CHECK(G.scr == SG_CTL && !G.quit && G.cur == 0);
    fr(SG_B_B, 0, 0, 0, 0, 0, 0); CHECK(strstr(row(7), "B")); fr(0, 0, 0, 0, 0, 0, 0); CHECK(G.scr == SG_CTL && !G.quit);
    for (i = 0; i < SG_HOLD_B_FRAMES + 2 && !G.quit; i++) fr(SG_B_B, 0, 0, 0, 0, 0, 0);
    CHECK(G.quit && G.scr == SG_CTL);
    /* opened from the hub (not direct), B held returns to the hub */
    open_ctl(); G.direct = 0; for (i = 0; i < SG_HOLD_B_FRAMES + 2 && G.scr == SG_CTL; i++) fr(SG_B_B, 0, 0, 0, 0, 0, 0);
    CHECK(G.scr == SG_HUB && !G.quit);
    /* B held on a stuck-Down pad still leaves (Up/Down are not needed) */
    open_ctl(); rep(10, 0, 0, -90); for (i = 0; i < SG_HOLD_B_FRAMES + 2 && !G.quit; i++) fr(SG_B_B, 0, 0, -90, 0, 0, 0);
    CHECK(G.quit);

    /* the Online page (ex Accounts hub) does not list the Controller test; the screen stays for the Mockups hub and Settings > Diagnostics */
    sg_init(&G);
    for (i = 0; i < 8u; i++) { fr(0, 0, 0, 0, 0, 0, 0); fr(SG_B_DOWN, 0, 0, 0, 0, 0, 0); }
    CHECK(G.cur < SG_ON_N);   /* (was < 5: the page has ten rows now, the cursor ends on a later enum id) */
    for (i = 2; i < 12; i++) CHECK(!strstr(row(i), "Controller test") && !strstr(row(i), "Entropy test") && !strstr(row(i), "Sign-In Providers"));
    sg_init(&G); fr(0, 0, 0, 0, 0, 0, 0); fr(SG_B_DOWN, 0, 0, 0, 0, 0, 0); fr(0, 0, 0, 0, 0, 0, 0); fr(SG_B_A, 0, 0, 0, 0, 0, 0);      /* the Friends and privacy page opens on Who can invite me */
    draw(); CHECK(G.cur == SG_ON_POL && V.bar < SG_ROWS && strstr(V.t[V.bar], "Who can invite me"));
    fr(0, 0, 0, 0, 0, 0, 0); fr(SG_B_B, 0, 0, 0, 0, 0, 0); CHECK(!G.quit && !G.onp); fr(0, 0, 0, 0, 0, 0, 0); fr(SG_B_B, 0, 0, 0, 0, 0, 0); CHECK(G.quit);     /* B: the list of pages, B again leaves */
    fits();

    /* ports with a pad: err byte 0 on another port is listed */
    open_ctl();
    { unsigned char e[4] = { 0, 0xFF, 0, 0xFF }; fr(0, 0, 0, 0, 0, 0, 0); sg_ctl_ports(&G.ctl, e); }
    CHECK(strstr(row(2), "Port 1") && strstr(row(2), "with a pad: 3"));
    { unsigned char e[4] = { 0, 0, 0, 0 }; sg_ctl_ports(&G.ctl, e); }
    CHECK(strstr(row(2), "with a pad: 2 3 4"));
    { unsigned char e[4] = { 0, 0xFF, 0xFF, 0xFF }; sg_ctl_ports(&G.ctl, e); }
    CHECK(strstr(row(2), "with a pad: none"));

    /* err byte shown */
    open_ctl(); { struct sdent_pad p; p.buttons = 0; p.sx = p.sy = p.cx = p.cy = 0; p.tl = p.tr = 0; p.err = 0xFF; sg_frame(&G, &p, 5); } CHECK(strstr(row(22), "err FF"));

    /* every state fits the rows and the font */
    open_ctl(); rep(400, SG_B_Z | SG_B_START | SG_B_L | SG_B_R, -128, 127); fits();
    rep(70, SG_B_Z, -128, 127); fits();
    CHECK(sg_cut_count == 0);
    CHECK(bad == 0);
    printf(fails ? "sg_ctl_test: %d FAILED\n" : "sg_ctl_test: all passed\n", fails);
    return fails ? 1 : 0;
}
