/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ld_diag.c: the console glue of the live-stall diagnostic (pure logic and the long description: livediag.h; investigation:
 * ).  Opt-in: the whole file is empty unless the image is built with `livediag=on` (-DMS_LIVE_DIAG=1).
 *
 * Install: sbra_init (offline/bridge/sbr_app.c, one call under the same define) calls ld_install once at the first menu frame.  It chains
 * ld_retrace onto HSD's user pre-retrace callback (HSD_VIData.pre_cb, +0x1D8): nothing else in this engine uses it, the game only ever sets
 * the POST callback, and HSD_VIInit clears it once at boot, before the first menu frame.  The callback runs in the VI interrupt (interrupts off),
 * so it only reads RAM words and writes frame-buffer pixels: no lock, no EXI, no socket, no allocation.
 */
#ifdef MS_LIVE_DIAG
typedef unsigned int u32;
typedef unsigned short u16;
#include "livediag.h"
#include "sbr_app.h"

extern volatile u32 s3_status[64];        /* struct s3_status (section .s3status), as a word array */
extern volatile u32 s7_status[48];        /* struct s7_status */
extern volatile u32 s6_rb[80];            /* struct s6_rb (RB[] words) */
extern struct sbra_status sbr_status;
extern volatile u32 s7_setup_info[4];    /* MATCH SETUP CONFORMANCE: s7_core.c */

#define SCENE_WORD (*(volatile u32 *)0x80479D30)
#define LOADING_W (*(volatile u32 *)0x80479D64)
#define VI_PRE_CB (LDG_VIDATA + 0x1D8u)

static struct ldg_s g_prev, g_base, g_snap, g_now;
static u32 g_have_prev, g_have_snap, g_why, g_frame, g_retr, g_retr_base, g_load_n, g_prev_cb, g_installed, g_last_match;
static struct ldg_th g_th_snap;
static char g_rows[LDG_ROWS][LDG_COLS];

static void thread_ctx(struct ldg_th *t)                  /* the interrupted game thread: __gCurrentThread (0x800000E4) -> OSContext at +0 */
{
    u32 th = LDG_R32(0x800000E4u), sp, i;
    t->srr0 = t->lr = t->bc[0] = t->bc[1] = t->bc[2] = 0;
    if (!ldg_in_mem1(th)) return;
    t->srr0 = LDG_R32(th + 0x198u);
    t->lr = LDG_R32(th + 0x84u);
    sp = LDG_R32(th + 4u);
    for (i = 0; i < 3u && ldg_in_mem1(sp); i++) {
        u32 back = LDG_R32(sp);
        t->bc[i] = LDG_R32(sp + 4u);
        sp = back;
    }
}

static void sample(struct ldg_s *o)
{
    ldg_fill(o, s3_status, s7_status, s6_rb);
    ldg_fill_setup(o, s7_setup_info);
    o->scene = SCENE_WORD;
    o->loading = LOADING_W;
    o->sbr_state = sbr_status.state;
    o->sbr_err = sbr_status.err;
    o->sbr_peer = sbr_status.peer;
}

static void ld_retrace(u32 count)
{
    struct ldg_s *n = &g_now;
    int in_match, ev;
    u32 sc;
    sample(n);
    sc = n->scene;
    in_match = (sc >> 24) == 8u && (sc & 0xFFu) == 2u && n->loading == 0;
    g_retr++;
    if (!g_have_prev || n->match_no != g_last_match || n->e_connects != g_prev.e_connects) {   /* a new match or a new ENet connection (= a new session): fresh baseline, forget the old verdict */
        if (g_have_prev) { g_have_snap = 0; g_why = 0; }
        g_last_match = n->match_no;
        g_base = *n;
        g_retr_base = g_retr;
    }
    ev = g_have_prev && !g_have_snap && (sc >> 24) == 8u && (sc & 0xFFu) == 2u && ldg_event(n, &g_prev);   /* only in the match scene (its load included): what ends a match is the question */
    if (ev) {                                            /* the first thing that ends a match or a session: freeze the facts */
        g_snap = *n;
        g_why = ldg_why(n, &g_base, &g_prev);
        g_frame = n->b0_frame;
        g_have_snap = 1;
        g_th_snap.srr0 = 0;
        g_th_snap.lr = 0;
        ldg_rows(g_rows, &g_snap, &g_base, g_why, g_frame, g_retr - g_retr_base, &g_th_snap);
    }
    if (n->loading != 0) { if (g_load_n < 100000u) g_load_n++; } else g_load_n = 0;
    if (ldg_want_paint(in_match, g_have_snap && (sc >> 24) == 8u, g_load_n)) {   /* a verdict is shown on the online scenes only, never over the menus */
        struct ldg_th th;
        u32 i;
        if (g_have_snap) {
            ldg_rows(g_rows, &g_snap, &g_base, g_why, g_frame, g_retr - g_retr_base, 0);   /* frozen verdict rows 0..4 */
        } else {
            ldg_rows(g_rows, n, &g_base, 0u, n->b0_frame, g_retr - g_retr_base, 0);        /* nothing ended: the live facts */
        }
        if (g_load_n > LDG_LOAD_RETRACES) {                /* a scene loads for more than 10 s: where is the game thread */
            thread_ctx(&th);
            ldg_rows(g_rows, g_have_snap ? &g_snap : n, &g_base, g_why, g_frame, g_retr - g_retr_base, &th);
        }
        for (i = 0; i < LDG_ROWS; i++) if (g_rows[i][0]) (void)ldg_paint_row(i, g_rows[i]);
    }
    g_prev = *n;
    g_have_prev = 1;
    if (g_prev_cb && g_prev_cb != (u32)ld_retrace) ((void (*)(u32))g_prev_cb)(count);
}

void ld_install(void)
{
    u32 cur;
    if (g_installed) return;
    cur = LDG_R32(VI_PRE_CB);
    if (cur == (u32)ld_retrace) return;
    g_prev_cb = cur;
    LDG_W32(VI_PRE_CB, (u32)ld_retrace);
    g_installed = 1;
}
#endif
