/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host driver of the sign-in screens: plays a scripted pad through sg_frame / sg_draw and dumps selected views as text for sg_png.py.
   Pad input of the entropy screens is a deterministic pseudo-random stick (a host stand-in for a person); the TCP/TLS screen is NOT dumped here
   (its real run is the Dolphin test).
   usage: sg_mock OUTFILE  (writes every screen of the mock flow) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sg_ui.h"

static struct sg G;
static unsigned long lcg = 12345;
static unsigned rnd(void) { lcg = lcg * 6364136223846793005ul + 1442695040888963407ul; return (unsigned)(lcg >> 33); }
static unsigned tbv = 1000;

static void frame(unsigned btn, int sx, int sy)
{
    struct sdent_pad p;
    p.buttons = (unsigned short)btn; p.sx = (signed char)sx; p.sy = (signed char)sy; p.cx = 0; p.cy = 0; p.tl = 0; p.tr = 0; p.err = 0;
    tbv += 675000u + (rnd() % 512u);
    sg_frame(&G, &p, tbv);
}
static void tap(unsigned btn) { unsigned k; frame(btn, 0, 0); for (k = 0; k < SG_ACT_GAP; k++) frame(0, 0, 0); }
static void wiggle(unsigned frames)           /* varied circles with changing speed */
{
    unsigned i;
    for (i = 0; i < frames; i++) frame(rnd() % 7u ? 0 : (rnd() % 3u ? SG_B_A : SG_B_B), (int)(rnd() % 161u) - 80, (int)(rnd() % 161u) - 80);
}

static FILE *out;
static void dump(const char *name)
{
    static struct sg_view v;
    unsigned r, i;
    sg_draw(&G, &v);
    fprintf(out, "SCREEN %s %u %u %u\n", name, v.qr_n, v.qr_row, v.qr_rows);
    for (r = 0; r < SG_ROWS; r++) {     /* the monospace mock: a right aligned value goes to the end of the 52 columns; the Settings look's attributes map to the old ones (title = header, crumb and section = dim) */
        char row[SG_COLS + SG_VAL + 8];
        unsigned a = v.attr[r] & 15u, n = (unsigned)strlen(v.t[r]), m = (unsigned)strlen(v.val[r]);
        strcpy(row, v.t[r]);
        if (m) { while (n + m < SG_COLS) row[n++] = ' '; row[n] = 0; strcat(row, v.val[r]); }
        if (a == SG_A_TITLE) a = SG_A_HEAD; else if (a == SG_A_CRUMB || a == SG_A_SEC) a = SG_A_DIM;
        if (v.bar == r) a = SG_A_HI;
        fprintf(out, "%u|%s\n", a, row);
    }
    if (v.qr_n) {
        unsigned stride = (v.qr_n + 7u) / 8u;
        for (i = 0; i < v.qr_n * stride; i++) fprintf(out, "%02x", v.qr[i]);
        fprintf(out, "\n");
    }
}

#define BOUNDED(cond, what) do { unsigned guard_ = 0; while ((cond) && guard_++ < 20000u) wiggle(1); if (guard_ >= 20000u) { fprintf(stderr, "stuck: %s (credit %u mbits)\n", what, G.ent.e.mtotal); return 1; } } while (0)
static void down(unsigned n) { while (n--) tap(SG_B_DOWN); }

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    out = fopen(argv[1], "w");
    if (!out) return 2;
    /* the Online page and the identity screens, example data only */
    sg_init(&G);
    G.linked = SG_ID_GUEST; G.fight = 3; G.pol = SG_POL_NA; G.id_slippi = 1;
    dump("01_online_guest");
    tap(SG_B_DOWN); tap(SG_B_A);                    /* Device identity */
    dump("02_identity_guest");
    tap(SG_B_A);                                    /* a guest's A: Link this console (the entropy pass first) */
    wiggle(60);
    dump("03_link_entropy");
    BOUNDED(G.lk.st == SGL_ENT, "the entropy pass");
    G.lk.req = SGL_R_NONE;
    sgl_started(&G.lk, "K7QF-39XM", 600, 5);
    G.lk.now += 19000u;
    dump("04_link_code_qr");
    sgl_linked(&G.lk, "TEST#1", "Nigzel");
    dump("05_link_saving");
    sgl_saved(&G.lk, 0); sgl_verified(&G.lk, 1);
    dump("06_link_done");
    G.linked = SG_ID_LINKED; G.fight = 2; G.pol = SG_POL_FRIENDS; G.id_sync = 1;
    { const char *c = "TEST#1", *n = "Nigzel"; unsigned i; for (i = 0; c[i]; i++) G.id_code[i] = c[i]; for (i = 0; n[i]; i++) G.id_name[i] = n[i]; }
    G.id_dev[0] = 0x3F; G.id_dev[1] = 0x9A; G.id_dev[2] = 0x21; G.id_dev[3] = 0xC0; G.id_slippi = 2;
    tap(SG_B_A);                                    /* Done: the Online page, cursor on Device identity */
    dump("07_online_linked");
    tap(SG_B_A);
    dump("08_identity_linked");
    tap(SG_B_B);
    { unsigned k; for (k = 0; k < 4u; k++) tap(SG_B_DOWN); }
    tap(SG_B_A);                                    /* Delete This Console's Identity */
    tap(SG_B_RIGHT);
    dump("09_delete_ask");
    tap(SG_B_A);
    G.lk.now = 50000u; frame(SG_B_A, 0, 0); G.lk.now = 51000u; frame(SG_B_A, 0, 0);
    dump("10_delete_hold");
    G.del.st = SGD_DONE; G.del.server_ok = 1; G.del.card_ok = 1; G.del.slippi_ok = 1;
    dump("11_delete_done");
    sg_goto(&G, SG_PROV);                           /* Providers: no row any more, screen kept */
    dump("12_providers");
    tap(SG_B_A); dump("13_provider_passkey");
    tap(SG_B_B); down(1); tap(SG_B_A); dump("14_provider_discord");
    tap(SG_B_B);
    sg_goto(&G, SG_ENT);                            /* Entropy test: no row any more, screen kept */
    wiggle(60);
    dump("15_entropy_running");
    BOUNDED(G.ent.state == SG_ET_RUN, "entropy pass");
    dump("16_entropy_pass");
    fclose(out);
    return 0;
}
