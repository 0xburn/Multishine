/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of Device identity and Delete This Console's Identity (sg_delid.c): the three clear levels (Up / Down; 1
   Multishine only, 2 and the Slippi login, 3 everything incl. the console key), A then a NEW press of Z held 1 s (letting go early does nothing; B steps back), the order
   server -> card -> Slippi files -> key with skipped steps, every result text, Device identity for a guest and a linked console (no secret on any row). */
#include <stdio.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static struct sg_view V;
static int fails;
static unsigned tbv = 1000;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void draw_check(void)
{
    unsigned r, c;
    sg_draw(&G, &V);
    for (r = 0; r < SG_ROWS; r++) {
        if (strlen(V.val[r]) > SG_VAL) { printf("value longer than %u: %s\n", (unsigned)SG_VAL, V.val[r]); fails++; }
        if (strlen(V.t[r]) > 49u) { printf("row longer than 49: %s\n", V.t[r]); fails++; }
        for (c = 0; V.t[r][c]; c++) {
            char ch = V.t[r][c];
            if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '\'' || ch == ',' || ch == '-' || ch == '.' || ch == ':' || ch == ' ' || ch == '#' || ch == '@')) {
                printf("glyph %c not in the menu font: %s\n", ch, V.t[r]); fails++;
            }
        }
    }
}
/* a row with a right aligned value is found as "label: value" too (the Settings look draws them as two objects) */
static int has(const char *s)
{
    unsigned r;
    sg_draw(&G, &V);
    for (r = 0; r < SG_ROWS; r++) {
        char both[128];
        if (strstr(V.t[r], s)) return 1;
        if (V.val[r][0]) { strcpy(both, V.t[r]); strcat(both, ": "); strcat(both, V.val[r]); if (strstr(both, s)) return 1; }
    }
    return 0;
}
static void frame(unsigned btn)
{
    struct sdent_pad p;
    p.buttons = (unsigned short)btn; p.sx = 0; p.sy = 0; p.cx = 0; p.cy = 0; p.tl = 0; p.tr = 0; p.err = 0;
    tbv += 675000u;
    G.lk.now += 17u;
    sg_frame(&G, &p, tbv);
}
static void tap(unsigned b) { unsigned k; frame(b); for (k = 0; k < SG_ACT_GAP; k++) frame(0); }
static void linked(int slippi)
{
    sg_init(&G);
    G.linked = SG_ID_LINKED; G.id_slippi = (unsigned char)(slippi ? 2 : 1); G.lk.now = 1000u;
    memcpy(G.id_name, "Nigzel", 7); memcpy(G.id_code, "TEST#1", 7);
    G.id_dev[0] = 0x3F; G.id_dev[1] = 0x9A; G.id_dev[2] = 0x21; G.id_dev[3] = 0xC0; G.id_dev[4] = 0x77;
}
static void hold_z(unsigned ms) { unsigned t; frame(SG_B_Z); for (t = 0; t < ms; t += 17u) frame(SG_B_Z); }

int main(void)
{
    unsigned i;
    /* ---- Device identity ---- */
    linked(1); G.id_sync = 1; sg_goto(&G, SG_IDENT); draw_check();
    CHECK(has("Status: Linked") && has("Name: Nigzel") && has("Connect code: TEST#1") && has("Slippi login: On this card") && !has("Device:") && !has("3f9a"));      /* no device id on the page */
    /* the Account page: Region, Send Slippi login, Delete as rows (linked: no Link row); Delete opens the page of the three levels */
    CHECK(has("Region") && !has("Send Slippi login") && has("Delete") && !has("Delete: Multishine only") && !has("Consoles On Your Account"));   /* a login on the card: no Send row */
    CHECK(V.bar == 12 && has("Where you play from. Helps find matches.") && has("A: Change  B: Back") && !has("More - Settings"));
    tap(SG_B_DOWN); draw_check(); CHECK(V.bar == 14 && has("Remove the identity from this console."));
    G.id_slippi = 1; G.cur = 0; draw_check(); tap(SG_B_DOWN); draw_check(); CHECK(V.bar == 14 && has("Get your Slippi login from your phone."));
    tap(SG_B_A); CHECK(G.scr == SG_SLOGIN);
    tap(SG_B_B); CHECK(G.scr == SG_IDENT && G.cur == 1);
    sg_init(&G); G.linked = SG_ID_GUEST; G.id_slippi = 1; sg_goto(&G, SG_IDENT); draw_check();
    CHECK(has("Status: Guest") && has("Slippi login: None") && has("A: Select  B: Back"));
    /* ---- the three levels: Up / Down choose, default 1; A continues, then a NEW press of Z held 1 s; B steps back ---- */
    linked(1); sg_goto(&G, SG_DELID);                 /* the three-level question (the Device identity rows go straight to the hold: tested above and in sg_test) */
    CHECK(G.scr == SG_DELID && G.del.st == SGD_ASK && G.del.have_slippi && G.del.was_linked && G.del.level == 1 && !G.del.also_slippi);
    draw_check();
    CHECK(has("- 1  Multishine only") && has("  2  Multishine and Slippi") && has("  3  Everything, console key too") && has("Name, code and link go.") && has("Kept: Slippi login, console key, Melee save.") && has("Your account is not deleted."));
    tap(SG_B_DOWN); CHECK(G.del.level == 2 && G.del.also_slippi && has("- 2  Multishine and Slippi") && has("Kept: console key, Melee save and settings."));
    tap(SG_B_DOWN); CHECK(G.del.level == 3 && has("Everything goes, the console key too.") && has("The first-run setup comes back"));
    tap(SG_B_DOWN); CHECK(G.del.level == 1);                                   /* wraps */
    tap(SG_B_UP); CHECK(G.del.level == 3);
    tap(SG_B_B); CHECK(G.scr == SG_HUB && G.cur == SG_ON_IDENT);               /* B cancels: nothing asked of the glue */
    /* level 1, linked with a Slippi login: hold, server, card, done; the Slippi login and the key stay */
    sg_goto(&G, SG_DELID); tap(SG_B_A); CHECK(G.del.st == SGD_HOLD);
    for (i = 0; i < 200u; i++) frame(SG_B_A);                                  /* the A of the first step, kept down: only Z holds */
    CHECK(G.del.st == SGD_HOLD && G.del.req == SGD_R_NONE);
    frame(0);
    hold_z(900); CHECK(G.del.st == SGD_HOLD && G.del.held_ms >= 800u && G.del.req == SGD_R_NONE);       /* not yet 1.0 s */
    draw_check(); CHECK(has("Hold Z for 1 second to clear.") && has("Clearing: Multishine."));
    frame(0); CHECK(G.del.st == SGD_HOLD && G.del.held_ms == 0);              /* let go early: nothing */
    tap(SG_B_B); CHECK(G.del.st == SGD_ASK);                                   /* B steps back */
    tap(SG_B_A); hold_z(1100);
    CHECK(G.del.st == SGD_SERVER && G.del.req == SGD_R_FORGET);
    draw_check();
    G.del.req = SGD_R_NONE; sgd_server(&G.del, 1);
    CHECK(G.del.st == SGD_CARD && G.del.req == SGD_R_CARD);
    draw_check(); CHECK(has("Do not remove the memory card."));
    G.del.req = SGD_R_NONE; sgd_card(&G.del, 1);
    CHECK(G.del.st == SGD_DONE && G.del.req == SGD_R_NONE);                    /* level 1: no Slippi step, no key step */
    draw_check(); CHECK(has("Cleared. This console is now a guest.") && !has("Slippi login") && !has("console key"));
    frame(0); tap(SG_B_A); CHECK(G.scr == SG_HUB && G.cur == SG_ON_IDENT);
    /* level 2: ... then the Slippi login, no key */
    linked(1); sg_goto(&G, SG_DELID); tap(SG_B_DOWN); tap(SG_B_A); hold_z(1100);
    CHECK(G.del.st == SGD_SERVER); sgd_server(&G.del, 1); sgd_card(&G.del, 1);
    CHECK(G.del.st == SGD_SLIPPI && G.del.req == SGD_R_SLIPPI);
    G.del.req = SGD_R_NONE; sgd_slippi(&G.del, 1);
    CHECK(G.del.st == SGD_DONE && G.del.req == SGD_R_NONE);
    draw_check(); CHECK(has("The Slippi login is gone.") && !has("console key"));
    /* level 3: ... then the console key; every result has its line, no secret anywhere */
    linked(1); sg_goto(&G, SG_DELID); tap(SG_B_UP); CHECK(G.del.level == 3); tap(SG_B_A); hold_z(1100);
    CHECK(G.del.st == SGD_SERVER); sgd_server(&G.del, 1); sgd_card(&G.del, 1); sgd_slippi(&G.del, 1);
    CHECK(G.del.st == SGD_KEY && G.del.req == SGD_R_KEY);
    draw_check(); CHECK(has("Do not remove the memory card."));
    G.del.req = SGD_R_NONE; sgd_key(&G.del, 1);
    CHECK(G.del.st == SGD_DONE);
    draw_check(); CHECK(has("Cleared. This console is now a guest.") && has("The Slippi login is gone.") && has("The console key is gone. First-run setup returns."));
    linked(1); sg_goto(&G, SG_DELID); tap(SG_B_UP); tap(SG_B_A); hold_z(1100); sgd_server(&G.del, 1); sgd_card(&G.del, 1); sgd_slippi(&G.del, 0); sgd_key(&G.del, 0);
    draw_check(); CHECK(has("The Slippi login could not be cleared.") && has("The console key could not be cleared."));
    /* ---- the server could not be told; the card could not be changed ---- */
    linked(0); sg_goto(&G, SG_DELID); CHECK(G.del.st == SGD_ASK && !G.del.have_slippi);
    tap(SG_B_DOWN); draw_check(); CHECK(has("There is no Slippi login on this card."));          /* level 2 without one: said, and it does nothing extra */
    tap(SG_B_UP); tap(SG_B_A); hold_z(1100); CHECK(G.del.st == SGD_SERVER);
    sgd_server(&G.del, 0); sgd_card(&G.del, 1);
    CHECK(G.del.st == SGD_DONE);
    CHECK(has("Cleared on this card. The server could not") && has("be told: remove it on multishine.link, Consoles."));
    linked(0); sg_goto(&G, SG_DELID); tap(SG_B_A); hold_z(1100); sgd_server(&G.del, 1); sgd_card(&G.del, 0);
    CHECK(has("the memory card could not be changed."));
    linked(0); sg_goto(&G, SG_DELID); tap(SG_B_DOWN); tap(SG_B_A); hold_z(1100); sgd_server(&G.del, 1); sgd_card(&G.del, 1);
    CHECK(G.del.st == SGD_DONE && G.del.req == SGD_R_NONE);                                      /* level 2 without a Slippi login = level 1 */
    /* ---- a guest: level 1 has nothing to do, level 2 the Slippi login, level 3 also the key ---- */
    sg_init(&G); G.linked = SG_ID_GUEST; G.id_slippi = 2; sg_goto(&G, SG_DELID);
    CHECK(G.del.st == SGD_ASK && !G.del.was_linked);
    draw_check(); CHECK(has("This console is a guest already:"));
    tap(SG_B_A); hold_z(1100); CHECK(G.del.st == SGD_DONE && G.del.req == SGD_R_NONE);          /* level 1: nothing to do */
    draw_check(); CHECK(has("Multishine: nothing to clear, already a guest."));
    sg_init(&G); G.linked = SG_ID_GUEST; G.id_slippi = 2; sg_goto(&G, SG_DELID); tap(SG_B_DOWN);
    tap(SG_B_A); hold_z(1100); CHECK(G.del.st == SGD_SLIPPI && G.del.req == SGD_R_SLIPPI);
    sgd_slippi(&G.del, 1); CHECK(G.del.st == SGD_DONE); draw_check(); CHECK(has("The Slippi login is gone."));
    sg_init(&G); G.linked = SG_ID_GUEST; G.id_slippi = 1; sg_goto(&G, SG_DELID); tap(SG_B_UP);     /* a guest without Slippi: level 3 still clears the key */
    tap(SG_B_A); hold_z(1100); CHECK(G.del.st == SGD_KEY && G.del.req == SGD_R_KEY);
    sgd_key(&G.del, 1); draw_check(); CHECK(G.del.st == SGD_DONE && has("The console key is gone."));
    /* ---- late answers change nothing ---- */
    linked(1); sg_goto(&G, SG_DELID);
    sgd_server(&G.del, 1); sgd_card(&G.del, 1); sgd_slippi(&G.del, 1); sgd_key(&G.del, 1);
    CHECK(G.del.st == SGD_ASK);
    CHECK(sg_cut_count == 0);
    printf("%s: %d failures\n", fails ? "FAIL" : "delete identity host test ok", fails);
    return fails ? 1 : 0;
}
