/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of toast_pill.h: the fixed point <-> float bit conversions against the host's floats, the rounded-rectangle fan (count, closure, symmetry,
   containment, the corner cut, the area against the analytic one) for the real plates of the project, and the show-for-a-while rule.  Build: cc -DTP_HOST (the GX part is console only). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TP_HOST 1
#include "../toast_pill.h"
#include "../../qc/qc_win.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static unsigned int bits_of(float f) { unsigned int u; memcpy(&u, &f, 4); return u; }
static float float_of(unsigned int u) { float f; memcpy(&f, &u, 4); return f; }

static double poly_area(const int *px, const int *py, unsigned n)       /* the fan: the sum of its triangles about the hub (Q16 -> units) */
{
    double a = 0;
    unsigned i;
    for (i = 1; i + 1 < n; i++) {
        double x1 = px[i] - px[0], y1 = py[i] - py[0], x2 = px[i + 1] - px[0], y2 = py[i + 1] - py[0];
        a += fabs(x1 * y2 - x2 * y1) * 0.5;
    }
    return a / 65536.0 / 65536.0;
}

int main(void)
{
    static const float vals[] = { 0.0f, 1.0f, -1.0f, 0.1f, -28.6f, -15.0f, 230.0f, 30.0f, 0.028f, 3.3f, 17.0f, 100.0f, 0.5f, 6.0f, -0.0625f };
    unsigned i;
    int px[TP_NV], py[TP_NV];
    /* q <-> float: the Q16 value is the float truncated to 1/65536, and the bits come back exact for what fits 24 bits */
    for (i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        int q = tp_q(bits_of(vals[i]), 16);
        double ref = floor((double)vals[i] * 65536.0 + (vals[i] < 0 ? 0.999999 : 0.0));       /* truncation toward zero */
        CHECK(fabs((double)q - (vals[i] < 0 ? ceil((double)vals[i] * 65536.0) : floor((double)vals[i] * 65536.0))) < 1.0);
        (void)ref;
        CHECK(fabsf(float_of(tp_f2(q, 16)) - (float)q / 65536.0f) < 1e-6f * (fabsf((float)q / 65536.0f) + 1.0f));
    }
    CHECK(tp_f2(0, 16) == 0u);
    CHECK(tp_f2(65536, 16) == bits_of(1.0f));
    CHECK(tp_f2(-65536 * 3, 16) == bits_of(-3.0f));
    CHECK(tp_q(bits_of(1e30f), 16) > 0 && tp_q(bits_of(-1e30f), 16) < 0);                      /* saturates, no wrap */
    {   /* the plates of the project: the in-match watcher toast (30 x 0.1 high), the menu toast */
        static const struct { float x, ytop, w, h; } plate[] = { { -28.6f, 15.0f, 21.4f, 3.0f }, { -13.6f, 12.9f, 27.2f, 3.3f }, { 0.0f, 5.0f, 3.0f, 3.0f }, { -5.0f, 5.0f, 40.0f, 0.5f } };
        unsigned k, j;
        for (k = 0; k < sizeof plate / sizeof plate[0]; k++) {
            int x0 = tp_q(bits_of(plate[k].x), 16), x1 = tp_q(bits_of(plate[k].x + plate[k].w), 16), yt = tp_q(bits_of(plate[k].ytop), 16), yb = tp_q(bits_of(plate[k].ytop - plate[k].h), 16);
            unsigned n = tp_points(x0, yb, x1, yt, px, py);
            double r = (double)(yt - yb) * TP_RADIUS_PCT / 100.0 / 65536.0, w = (double)(x1 - x0) / 65536.0, h = (double)(yt - yb) / 65536.0, ana;
            CHECK(n == TP_NV);
            CHECK(px[1] == px[n - 1] && py[1] == py[n - 1]);                                     /* the fan closes */
            CHECK(abs(px[0] - (x0 + x1) / 2) <= 1 && abs(py[0] - (yb + yt) / 2) <= 1);           /* the hub is the centre */
            for (j = 0; j < n; j++) CHECK(px[j] >= x0 - 2 && px[j] <= x1 + 2 && py[j] >= yb - 2 && py[j] <= yt + 2);   /* nothing outside the box (rounding of the last bit) */
            if (r > w / 2) r = w / 2;
            ana = w * h - (4.0 - 3.14159265) * r * r;                                            /* rounded rectangle */
            { double a = poly_area(px, py, n); if (fabs(a - ana) >= 0.012 * w * h + 0.002) printf("plate %u area %f analytic %f w %f h %f\n", k, a, ana, w, h); CHECK(fabs(a - ana) < 0.012 * w * h + 0.002); CHECK(a < w * h); }   /* the 4-segment arcs sit inside the true arc: a little less area */
            /* left right and top bottom mirror: every point has a partner (x mirrored about the centre line, y the same) within rounding */
            for (j = 1; j + 1 < n; j++) {
                unsigned m, ok = 0;
                for (m = 1; m + 1 < n; m++) if (abs(px[m] - ((x0 + x1) - px[j])) <= 256 && abs(py[m] - py[j]) <= 256) ok = 1;
                CHECK(ok);
            }
            CHECK(px[1] <= x1 && px[1] > x1 - 1 * 65536 * 2);                                    /* the first point is on the right edge near the top */
        }
    }
    /* the corner is really cut: the point on the box corner is not part of the plate; the nearest vertex is r x (1 - cos 45) inside on each axis */
    {
        int x0 = 0, x1 = 20 * 65536, yb = 0, yt = 3 * 65536;
        unsigned n = tp_points(x0, yb, x1, yt, px, py), j, near_corner = 0;
        for (j = 1; j < n; j++) if (px[j] > x1 - 65536 / 10 && py[j] > yt - 65536 / 10) near_corner = 1;
        CHECK(n == TP_NV && !near_corner);
    }
    /* show for a while after the last input */
    CHECK(tp_visible(1000, 1000, TP_ON_TIME_MS, 0) == 1);
    CHECK(tp_visible(1000 + TP_ON_TIME_MS - 1, 1000, TP_ON_TIME_MS, 0) == 1);
    CHECK(tp_visible(1000 + TP_ON_TIME_MS, 1000, TP_ON_TIME_MS, 0) == 0);
    CHECK(tp_visible(1000 + 10 * TP_ON_TIME_MS, 1000, TP_ON_TIME_MS, 1) == 1);                   /* sticky: a prompt waits for its answer */
    CHECK(tp_visible(5, 0xFFFFFFF0u, TP_ON_TIME_MS, 0) == 1);                                     /* the millisecond clock wraps */
    /* the ONE glyph table: classes, the list estimates the old est_w gave (so the budgets of gnb_ev.c keep their meaning), the quick chat cut rule */
    CHECK(tp_gw('i') == 70 && tp_gw(' ') == 70 && tp_gw('a') == 100 && tp_gw('7') == 110 && tp_gw('C') == 130 && tp_gw('m') == 150 && tp_gw('W') == 160 && tp_gw(0x81) == 100 && tp_gw('(') == 100);
    {
        static const struct { const char *s; unsigned px; } L[] = { { "Cliff Hanger", 111 }, { "Fair Fiona", 97 }, { "Pocket Fox", 100 }, { "Shine Spike", 107 }, { "Wobble Pilot", 114 }, { "Grab Gus", 83 }, { "WWWWWWW", 112 }, { "", 0 } };
        unsigned i;
        for (i = 0; i < sizeof L / sizeof *L; i++) CHECK(TP_PX_OF(tp_text_gw(L[i].s), TP_LIST_PX * 100u) == L[i].px);
        CHECK(tp_text_gw("abc") == 300 && tp_text_gw("il") == 140);
    }
    {
        unsigned char a[64], b[64], c[64];
        int w;
        strcpy((char *)a, "Good game!"); w = qw_fit_a(a, 356, 0);
        CHECK(w == 78 && !strcmp((char *)a, "Good game!"));                                            /* fits: untouched, drawn width of the table x 7.56 px a letter */
        strcpy((char *)b, "A very long quick text that cannot possibly fit into the column"); w = qw_fit_a(b, 180 - 16, 0);
        CHECK(w <= 180 - 16 && strlen((char *)b) > 3 && !strcmp((char *)b + strlen((char *)b) - 2, ".."));    /* cut inside the column, ends in two dots, the result is its width */
        strcpy((char *)c, "\x81\x48 ok"); w = qw_fit_a(c, 356, 0);
        CHECK(w == 28);                                                                                 /* a Shift-JIS pair counts one letter: 756 + 529 + 756 + 756 = 2797 -> 28 px */
        {   /* the window plan: two columns, the plate never wider than QW_MAX_W */
            unsigned char h[64] = "Chat: Up", r0[64] = "Up: gg", r1[64] = "Left: nice one", r2[64] = "Right: thanks a lot", r3[64] = "Down: WWWWWWWWWWWWWWWWWWWWWWWWWWWW";
            unsigned char *rp[4];
            struct qw_plan p;
            rp[0] = r0; rp[1] = r1; rp[2] = r2; rp[3] = r3;
            qw_plan(&p, h, rp, 0);
            CHECK(p.w > QW_COL && p.w <= QW_MAX_W && p.h == QW_WIN_H && p.x[0] == QW_PAD_X && p.x[1] == QW_PAD_X + QW_COL);
        }
    }
    if (!fails) printf("OK toast_pill\n");
    return fails != 0;
}
