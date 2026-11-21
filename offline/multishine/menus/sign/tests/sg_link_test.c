/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of Link this console (sg_link.c): the entropy pass hands over to the link BY ITSELF at 100 percent, A / B are
   pool input there and only a held B leaves; the QR is exactly the user-code URL (version 3, 29 modules); polling every interval, SLOW_DOWN and LINK_PENDING; expiry and a new
   code; the sealed poll's result -> card write -> verify -> Linked; every error line and what A / B do there (a card failure: B keeps the link in RAM); already linked: A twice
   links again; opened from the title (direct): Done leaves the overlay.  Every drawn row: at most 49 characters, menu-font glyphs ('#' / '@' of a code are expanded by the glue). */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static struct sg_view V;
static int fails;
static unsigned tbv = 5000;
static unsigned long lcg = 7;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static unsigned rnd(void) { lcg = lcg * 6364136223846793005ul + 1442695040888963407ul; return (unsigned)(lcg >> 33); }

static void draw_check(void)
{
    unsigned r, c;
    sg_draw(&G, &V);
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(V.t[r]) > 49u) { printf("row longer than 49: %s\n", V.t[r]); fails++; }
        for (c = 0; V.t[r][c]; c++) {
            char ch = V.t[r][c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ' || ch == '#' || ch == '@')) {
                printf("glyph %c not in the menu font: %s\n", ch, V.t[r]); fails++;
            }
        }
    }
}
static int has(const char *s) { unsigned r; sg_draw(&G, &V); for (r = 0; r < SG_ROWS; r++) if (strstr(V.t[r], s)) return 1; return 0; }
static void frame(unsigned btn, int sx, int sy, int err)
{
    struct sdent_pad p;
    p.buttons = (unsigned short)btn; p.sx = (signed char)sx; p.sy = (signed char)sy; p.cx = 0; p.cy = 0; p.tl = 0; p.tr = 0; p.err = (unsigned char)err;
    tbv += 675000u + rnd() % 400u;
    G.lk.now += 17u;
    sg_frame(&G, &p, tbv);
}
static void tap(unsigned b);
static void settle(void) { unsigned k; for (k = 0; k < SG_ACT_GAP + 1u; k++) frame(0, 0, 0, 0); }      /* a press right after the random pool input would fall in the fresh-press gap */
static void tap(unsigned b) { unsigned k; frame(b, 0, 0, 0); for (k = 0; k < SG_ACT_GAP; k++) frame(0, 0, 0, 0); }
static void wiggle(void) { frame(rnd() % 9u ? 0 : SG_B_A, (int)(rnd() % 161u) - 80, (int)(rnd() % 161u) - 80, 0); }
static void fresh(unsigned linked)
{
    sg_init(&G);
    G.linked = (unsigned char)linked;
    G.lk.now = 1000u;
}
static unsigned fill_pool(void) { unsigned i; for (i = 0; i < 6000u && G.lk.st == SGL_ENT; i++) wiggle(); frame(0, 0, 0, 0); return i; }   /* ends with no button held */

int main(void)
{
    unsigned i, frames;
    /* ---- from a guest, empty pool: the entropy pass, then the link starts by itself ---- */
    fresh(SG_ID_GUEST);
    sg_goto(&G, SG_LINK);
    CHECK(G.scr == SG_LINK && G.lk.st == SGL_ENT && G.lk.req == SGL_R_NONE);
    draw_check();
    CHECK(has("Getting ready") && has("Input random stick and button events") && has("Hold B: Cancel"));
    for (i = 0; i < 40; i++) { frame(SG_B_B, (int)(rnd() % 161u) - 80, 0, 0); frame(0, 20, 20, 0); }     /* taps of B are pool input */
    CHECK(G.scr == SG_LINK && G.lk.st == SGL_ENT);
    frames = fill_pool();
    printf("entropy pass: %u frames of random stick input to the SESSION level\n", frames);
    CHECK(G.lk.st == SGL_START && G.lk.req == SGL_R_START && frames < 3000u);                 /* no key press needed */
    CHECK(sdent_ready(&G.lk.pool, SDENT_L_SESSION));
    draw_check();
    /* ---- the code arrives: QR of the user-code URL, the code text, the countdown ---- */
    G.lk.req = SGL_R_NONE;
    sgl_started(&G.lk, "K7QF-39XM", 600, 5);
    CHECK(G.lk.st == SGL_SHOW && G.lk.qr_ok && G.lk.qr_n == 29);
    {
        unsigned char ref[QR_OUT_BYTES], n = 0;
        const char *url = "https://multishine.link/qr?code=K7QF39XM";
        CHECK(strlen(url) == 40u && qr_encode(url, (unsigned)strlen(url), ref, &n) == QR_OK && n == 29 && !memcmp(ref, G.lk.qr, 29u * 4u));
    }
    draw_check();
    CHECK(V.qr_n == 29 && V.qr_row == 3 && V.qr_rows == 16 && ((unsigned)V.qr_n + 8u) * 7u <= (unsigned)V.qr_rows * 17u);
    CHECK(!strcmp(V.t[21], "K7QF-39XM") && !strcmp(V.t[22], "Works for 10:00 more.") && strstr(V.t[20], "multishine.link") && !strcmp(V.t[SG_ROWS - 2], "B: Cancel"));
    /* polling: every interval; LINK_PENDING waits one more interval, SLOW_DOWN five seconds more */
    for (i = 0; i < 400u && G.lk.req == SGL_R_NONE; i++) frame(0, 0, 0, 0);
    printf("poll at %u ms after the code (req %u)\n", (unsigned)(G.lk.now - G.lk.t0), G.lk.req);
    CHECK(G.lk.req == SGL_R_POLL && G.lk.now - G.lk.t0 >= 5000u && G.lk.now - G.lk.t0 < 5100u);
    G.lk.req = SGL_R_NONE; sgl_failed(&G.lk, 0, 21);
    CHECK(G.lk.st == SGL_SHOW && G.lk.next_poll == G.lk.now + 5000u);
    sgl_failed(&G.lk, 0, 23);
    CHECK(G.lk.st == SGL_SHOW && G.lk.next_poll == G.lk.now + 10000u);
    G.lk.now = G.lk.t0 + 61000u; frame(0, 0, 0, 0);
    CHECK(has("Works for 9:0") || has("Works for 8:5"));
    /* approved: the sealed poll opened -> SAVE -> VERIFY -> DONE */
    G.lk.req = SGL_R_NONE;
    sgl_linked(&G.lk, "TEST#1", "Nigzel");
    CHECK(G.lk.st == SGL_SAVE && G.lk.req == SGL_R_SAVE);
    draw_check();
    G.lk.req = SGL_R_NONE; sgl_saved(&G.lk, 0);
    CHECK(G.lk.st == SGL_VERIFY && G.lk.req == SGL_R_VERIFY && G.lk.saved == 1);
    G.lk.req = SGL_R_NONE; sgl_verified(&G.lk, 1);
    CHECK(G.lk.st == SGL_DONE);
    draw_check();
    CHECK(has("Name: Nigzel") && has("Connect code: TEST#1") && has("Saved to the memory card.") && has("A: Done"));
    tap(SG_B_A);
    CHECK(G.scr == SG_HUB && G.cur == SG_ON_IDENT && G.lk.st == SGL_OFF);        /* back on the Online page, cursor on Device identity */

    /* ---- B on the code: LINK_CANCEL, back on the Online page on the Link row ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "AAAA-BBBB", 600, 5);
    tap(SG_B_B);
    CHECK(G.scr == SG_HUB && G.cur == SG_ON_LINK && G.lk.req == SGL_R_CANCEL);
    /* ---- the code runs out: EXPIRED (cancel), A asks for a new code ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "AAAA-BBBB", 600, 5);
    G.lk.now = G.lk.t0 + 600000u; G.lk.next_poll = G.lk.now + 99999u; frame(0, 0, 0, 0);
    CHECK(G.lk.st == SGL_EXPIRED && G.lk.req == SGL_R_CANCEL);
    draw_check();
    CHECK(has("The code expired or was declined.") && has("A: New code"));
    G.lk.req = SGL_R_NONE; tap(SG_B_A);
    CHECK(G.lk.st == SGL_START && G.lk.req == SGL_R_START);
    /* LINK_EXPIRED from the server (Deny on the website) during the polls: the same screen */
    G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "CCCC-DDDD", 600, 5);
    sgl_failed(&G.lk, 0, 22);
    CHECK(G.lk.st == SGL_EXPIRED);

    /* ---- every error line; A retries the step that failed; B goes back ---- */
    {
        static const unsigned st[] = { 0, 3, 40, 41, 42, 43, 27, 5, 1, 99, 17 };
        for (i = 0; i < sizeof st / sizeof st[0]; i++) {
            fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); fill_pool(); G.lk.req = SGL_R_NONE;
            if (st[i] == 1u) sgl_started(&G.lk, "EEEE-FFFF", 600, 5);              /* BAD_REQUEST to the sealed poll: a server without it */
            sgl_failed(&G.lk, st[i] ? 0u : SGL_E_OFFLINE, st[i]);
            CHECK(G.lk.st == SGL_ERR);
            draw_check();
            if (st[i] == 0u) CHECK(has("No network. Check the cable, then press A."));
            if (st[i] == 3u) CHECK(has("Too many tries. Wait a minute, then press A."));
            if (st[i] == 41u || st[i] == 17u) CHECK(has("The server is busy. Try again in 30 s."));
            if (st[i] == 42u) CHECK(has("Try again in 120 s."));
            if (st[i] == 43u) CHECK(has("The server is down for maintenance."));
            if (st[i] == 27u) CHECK(has("Your account has the most consoles allowed.") && has("Remove one on multishine.link, then press A."));
            if (st[i] == 1u) CHECK(has("This server cannot link securely yet."));
            frame(0, 0, 0, 0); frame(0, 0, 0, 0); frame(0, 0, 0, 0); frame(SG_B_A, 0, 0, 0);      /* (the pool fill above pressed A: the fresh-press gate wants it up first) */
            if (!(st[i] == 1u ? (G.lk.st == SGL_SHOW) : (G.lk.st == SGL_START))) printf("status %u: st %u req %u from %u err %u\n", st[i], G.lk.st, G.lk.req, G.lk.err_from, G.lk.err);
            CHECK(st[i] == 1u ? (G.lk.st == SGL_SHOW && G.lk.req == SGL_R_NONE && G.lk.next_poll == G.lk.now) : (G.lk.st == SGL_START && G.lk.req == SGL_R_START));
        }
        fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); fill_pool(); G.lk.req = SGL_R_NONE; sgl_failed(&G.lk, SGL_E_UNREACH, 0);
        tap(SG_B_B); CHECK(G.scr == SG_HUB && G.cur == SG_ON_LINK);
    }
    /* ---- the card write fails: A writes again, B keeps the link until power off ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "GGGG-HHHH", 600, 5);
    sgl_linked(&G.lk, "AB@123", "Ann"); G.lk.req = SGL_R_NONE;
    sgl_saved(&G.lk, -2);
    CHECK(G.lk.st == SGL_ERR && G.lk.err == SGL_E_CARD);
    draw_check();
    CHECK(has("Linked, but not saved to the memory card.") && has("B: Use until power off"));
    tap(SG_B_A); CHECK(G.lk.st == SGL_SAVE && G.lk.req == SGL_R_SAVE);
    G.lk.req = SGL_R_NONE; sgl_saved(&G.lk, -1);
    CHECK(G.lk.st == SGL_ERR && G.lk.err == SGL_E_NOREC);
    draw_check();
    tap(SG_B_B); CHECK(G.lk.st == SGL_DONE && G.lk.saved == 2);
    CHECK(has("Not saved: works until power off.") && has("Connect code: AB@123"));
    /* ---- already linked: A shows the warning, A again links again (the pool is still full: straight to the code) ---- */
    fresh(SG_ID_LINKED);
    memcpy(G.id_name, "Nigzel", 7); memcpy(G.id_code, "TEST#1", 7);
    sg_goto(&G, SG_LINK);
    CHECK(G.lk.st == SGL_ALREADY);
    draw_check(); CHECK(has("Linked as Nigzel") && has("Connect code: TEST#1"));
    tap(SG_B_A); CHECK(G.lk.again == 1 && G.lk.st == SGL_ALREADY && has("This replaces the identity on this console."));
    tap(SG_B_A); CHECK(G.lk.st == SGL_ENT);
    fill_pool(); CHECK(G.lk.st == SGL_START);
    fresh(SG_ID_LINKED); sg_goto(&G, SG_LINK); tap(SG_B_B); CHECK(G.scr == SG_HUB && G.cur == SG_ON_LINK);
    /* ---- the entropy pass: hold B leaves (taps do not), no pad for 3 s shows the hint, a still stick asks for variety after 20 s ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK);
    for (i = 0; i < SGL_NOPAD_FRAMES + 2u; i++) frame(0, 0, 0, 0xFF);
    CHECK(has("Plug in a controller."));
    for (i = 0; i < SGL_SLOW_FRAMES + 2u; i++) frame(0, 0, 0, 0);
    CHECK(has("Vary your rhythm.") && !has("Plug the controller"));
    for (i = 0; i < SG_HOLD_B_FRAMES + 2u && G.scr == SG_LINK; i++) frame(SG_B_B, 0, 0, 0);
    CHECK(G.scr == SG_HUB && G.cur == SG_ON_LINK && !G.quit);
    /* ---- opened straight from the title (first-run offer): Done leaves the overlay ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "JJJJ-KKKK", 600, 5);
    sgl_linked(&G.lk, "AB@123", "Ann"); sgl_saved(&G.lk, 0); sgl_verified(&G.lk, 0);
    CHECK(G.lk.st == SGL_DONE && !G.lk.verified && has("The server check comes with the next search."));
    tap(SG_B_A); CHECK(G.quit);
    /* ---- the first-run offer after START (opened with SG_GOTO_OFFER, direct): a question first; Later leaves the overlay without the entropy screen ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);
    CHECK(G.lk.st == SGL_OFFER && G.lk.req == SGL_R_NONE);
    draw_check();
    CHECK(has("Welcome to Multishine") && has("Dojo works without linking.") && has("Other features require an account.") && has("A: Link now  B: Later"));
    tap(SG_B_B); CHECK(G.quit && G.lk.st == SGL_OFF && G.lk.req == SGL_R_NONE);      /* Later: the console is not linked, so no Slippi offer: the first run ends */
    /* ---- the progressive scan question: the FIRST step of the first run, a screen of its own (SG_GOTO_PROG without SG_GOTO_OFFER); the link offer does not carry it any more ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; G.prog_ask = 1; sgl_progq(&G);
    CHECK(G.lk.st == SGL_PROGQ && !G.quit && G.lk.req == SGL_R_NONE && !G.lk.chain); draw_check();
    CHECK(has("Sharper picture") && has("Does your TV support progressive scan") && has("We test it for 10 seconds first.") && has("A: Yes, try it  B: Not now"));
    /* Yes: the test runs INLINE (console the Settings page lost the first run).  The overlay stays open, asks the glue (YES), the glue starts the trial (RUN, seconds left in prog_wii) */
    tap(SG_B_A); CHECK(!G.quit && G.lk.st == SGL_PROGK && G.prog_try == SG_PT_YES);
    frame(0, 0, 0, 0); CHECK(!G.quit && G.prog_try == SG_PT_YES);                        /* waits for the glue */
    G.prog_try = SG_PT_RUN; G.prog_wii = 7; draw_check();
    CHECK(has("Press A to keep this picture.") && has("B or waiting goes back.") && has("Going back in 7 seconds.") && has("A: Keep  B: Go back"));
    tap(SG_B_A); CHECK(G.prog_try == SG_PT_KEEP && !G.quit);                              /* A keeps: the glue stores On and says DONE */
    G.prog_try = SG_PT_DONE; frame(0, 0, 0, 0); CHECK(G.quit && G.lk.st == SGL_OFF && G.prog_try == SG_PT_DONE);   /* the overlay leaves: the next first-run steps follow */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; G.prog_ask = 1; sgl_progq(&G);   /* Yes, then B: go back */
    tap(SG_B_A); G.prog_try = SG_PT_RUN; G.prog_wii = 3; tap(SG_B_B); CHECK(G.prog_try == SG_PT_BACK && !G.quit);
    G.prog_try = SG_PT_DONE; frame(0, 0, 0, 0); CHECK(G.quit);
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; G.prog_ask = 1; sgl_progq(&G);   /* Yes, no key press: the test times out (the glue says DONE), the overlay leaves */
    tap(SG_B_A); G.prog_try = SG_PT_RUN; frame(0, 0, 0, 0); CHECK(!G.quit);
    G.prog_try = SG_PT_DONE; frame(0, 0, 0, 0); CHECK(G.quit);
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; G.prog_ask = 1; sgl_progq(&G);
    tap(SG_B_B); CHECK(G.quit && G.prog_try == 2 && G.lk.st == SGL_OFF && G.lk.req == SGL_R_NONE);                   /* Not now: no entropy pass, no link started */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);               /* the link offer alone never shows the question */
    tap(SG_B_B); CHECK(G.quit && G.prog_try == 0 && G.lk.st == SGL_OFF);
    /* ---- the chain: Later or an error never leads to the Slippi offer (the console is not linked); only a link that completed does.  A on the Slippi offer opens the login screen, and Back from it goes on to the progressive scan question ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);
    tap(SG_B_B); CHECK(G.quit && G.lk.st == SGL_OFF && G.lk.req == SGL_R_NONE);      /* Later: no Slippi offer, the overlay leaves */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);        /* a link that completes: Done goes to the Slippi offer */
    tap(SG_B_A); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "MMMM-NNNN", 600, 5);
    sgl_linked(&G.lk, "AB@123", "Ann"); sgl_saved(&G.lk, 0); sgl_verified(&G.lk, 1); settle();
    tap(SG_B_A); CHECK(G.lk.st == SGL_SLCHK && !G.lk.noslp && !G.quit && G.lk.chain == 0); draw_check();
    G.lk.req = SGL_R_NONE; sgl_slchecked(&G, 1);
    CHECK(G.lk.st == SGL_SLOFFER && !G.quit); draw_check();
    CHECK(has("Set up Slippi") && has("Slippi login. It is optional.") && has("A: Set up now  B: Later"));
    tap(SG_B_A); CHECK(G.scr == SG_SLOGIN && G.lk.st == SGL_OFF && G.direct && !G.quit);
    G.sr.leave = 1; frame(0, 0, 0, 0); CHECK(G.quit && G.lk.req == SGL_R_NONE);                                /* Back from the login screen, no question asked: the overlay leaves */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);
    tap(SG_B_A); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "MMMM-NNNN", 600, 5);
    sgl_linked(&G.lk, "AB@123", "Ann"); sgl_saved(&G.lk, 0); sgl_verified(&G.lk, 1); settle();
    tap(SG_B_A); G.lk.req = SGL_R_NONE; sgl_slchecked(&G, 1); tap(SG_B_B); CHECK(G.quit && G.lk.st == SGL_OFF);                  /* Later on the Slippi offer leaves */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);        /* a link that completes: Done goes to the Slippi offer, not out */
    tap(SG_B_A); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "MMMM-NNNN", 600, 5);
    sgl_linked(&G.lk, "AB@123", "Ann"); sgl_saved(&G.lk, 0); sgl_verified(&G.lk, 1); settle();
    tap(SG_B_A); CHECK(G.lk.st == SGL_SLCHK && !G.quit && G.lk.chain == 0);
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);        /* the code on screen, B: the cancel of the code is sent first, then the Slippi check */
    tap(SG_B_A); fill_pool(); G.lk.req = SGL_R_NONE; sgl_started(&G.lk, "PPPP-QQQQ", 600, 5);
    frame(SG_B_B, 0, 0, 0); CHECK(G.lk.st == SGL_SLCHK && G.lk.req == SGL_R_CANCEL);
    frame(0, 0, 0, 0); CHECK(G.lk.req == SGL_R_CANCEL && !G.quit);          /* the glue has not taken it yet: the overlay stays until the cancel of the code was taken */
    G.lk.req = SGL_R_NONE; frame(0, 0, 0, 0); CHECK(G.quit && G.lk.req == SGL_R_NONE);      /* not linked: no card check for the Slippi offer, the first run ends */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); sgl_slchecked(&G, 1);               /* not in the chain (Settings): an answer out of turn changes nothing */
    CHECK(G.lk.st == SGL_ENT);
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);
    tap(SG_B_A); CHECK(G.lk.st == SGL_ENT && !G.quit);                   /* Link Now: the entropy pass (fresh pool) ... */
    fill_pool(); CHECK(G.lk.st == SGL_START && G.lk.req == SGL_R_START);  /* ... then the code by itself */
    fresh(SG_ID_LINKED); sg_goto(&G, SG_LINK); G.direct = 1; sgl_offer(&G);
    CHECK(G.lk.st == SGL_ALREADY);                                       /* linked after all: no offer */

    /* ---- answers that come too late (another state) change nothing ---- */
    fresh(SG_ID_GUEST); sg_goto(&G, SG_LINK);
    sgl_started(&G.lk, "LATE-LATE", 600, 5); sgl_linked(&G.lk, "X@1", "X"); sgl_saved(&G.lk, 0); sgl_verified(&G.lk, 1);
    CHECK(G.lk.st == SGL_ENT);
    CHECK(sg_cut_count == 0);
    printf("%s: %d failures\n", fails ? "FAIL" : "link screen host test ok", fails);
    return fails ? 1 : 0;
}
