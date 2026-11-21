/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gnchat: the Game Night room chat client, see gnchat.h.  C89, no libc, no heap, never blocks. */
#include "gnchat.h"

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }
static void p32(mnc_u8 *p, mnc_u32 v) { p[0] = (mnc_u8) (v >> 24); p[1] = (mnc_u8) (v >> 16); p[2] = (mnc_u8) (v >> 8); p[3] = (mnc_u8) v; }

void gnchat_init(gnchat *g)
{
    mnc_u8 *b = (mnc_u8 *) g;
    mnc_u32 i;
    for (i = 0; i < sizeof *g; i++) b[i] = 0;
}

static int fail(gnchat *g, int err, int status) { g->state = GNCHAT_FAILED; g->err = (mnc_u8) err; g->status = status; return err; }

static int start(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now, mnc_u8 kind, const mnc_u8 *pl, mnc_u32 plen)
{
    mnc_u32 i, n;
    if (g->state == GNCHAT_BUSY) return GNCHAT_E_BUSY;
    gnchat_init(g);
    if (!w || !io || !mw2_ready(w, now)) return fail(g, GNCHAT_E_NOSESSION, 0);
    for (i = 0; i < plen; i++) g->pkt[MW2_HDR + i] = pl[i];
    n = mw2_seal(w, g->pkt, GNCHAT_OP, plen);
    if (n > sizeof g->pkt) return fail(g, GNCHAT_E_ARG, 0);
    g->pkt_len = n; g->ctr = w->ctr; g->sent_ms = now; g->tries = 1; g->kind = kind; g->state = GNCHAT_BUSY;
    (void) io->send(io->ctx, g->pkt, n);
    return 0;
}

int gnchat_fetch(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now, mnc_u32 after)
{
    mnc_u8 pl[5];
    pl[0] = GNCHAT_K_FETCH; p32(pl + 1, after);
    return start(g, w, io, now, GNCHAT_K_FETCH, pl, 5u);
}

int gnchat_preset(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now, unsigned idx)
{
    mnc_u8 pl[2];
    if (idx > 15u) { gnchat_init(g); return fail(g, GNCHAT_E_ARG, 0); }
    pl[0] = GNCHAT_K_PRESET; pl[1] = (mnc_u8) idx;
    return start(g, w, io, now, GNCHAT_K_PRESET, pl, 2u);
}

int gnchat_parse_fetch(const mnc_u8 *b, mnc_u32 n, mnc_u8 *count, struct gnchat_msg *out)
{
    mnc_u32 i = 1, k, j, len;
    unsigned cnt;
    if (n < 1u || n > 500u) return -1;
    cnt = b[0];
    if (cnt > GNCHAT_MAX) return -1;
    for (k = 0; k < cnt; k++) {
        struct gnchat_msg *m = &out[k];
        if (i + 6u > n) return -1;
        m->id = g32(b + i); m->age = (mnc_u16) ((b[i + 4u] << 8) | b[i + 5u]); i += 6u;
        if (m->id == 0u) return -1;
        if (i >= n) return -1;
        len = b[i++];
        if (len == 0u || len > 12u || i + len > n) return -1;
        for (j = 0; j < len; j++) { mnc_u8 ch = b[i + j]; if (ch < 0x20u || ch > 0x7Eu) return -1; m->name[j] = (char) ch; }
        m->name[len] = 0; i += len;
        if (i >= n) return -1;
        len = b[i++];
        if (len == 0u || len > 60u || i + len > n) return -1;
        for (j = 0; j < len; j++) { mnc_u8 ch = b[i + j]; if (ch < 0x20u || ch > 0x7Eu) return -1; m->text[j] = (char) ch; }
        m->text[len] = 0; i += len;
    }
    if (i != n) return -1;
    *count = (mnc_u8) cnt;
    return 0;
}

int gnchat_rx(gnchat *g, mw2 *w, const mnc_u8 *d, mnc_u32 n, mnc_u32 now)
{
    mw2_u8 st = 0;
    mw2_u32 bl = 0;
    int r;
    if (g->state != GNCHAT_BUSY || n < MW2_HDR + MW2_MAC || d[0] != 0x6D || (d[2] & 0x7Fu) != GNCHAT_OP || (d[2] & 0x80u) == 0u || g32(d + 8) != g->ctr) return 0;
    r = mw2_open(w, (mw2_u32) now, d, (mw2_u32) n, (mw2_u8) GNCHAT_OP, (mw2_u32) g->ctr, &st, &bl);
    if (r == 2) { fail(g, GNCHAT_E_NOSESSION, 0); return 1; }          /* the server forgot the session */
    if (r != 1) { g->st_bad++; return 1; }
    if (st != 0) { fail(g, GNCHAT_E_SERVER, st); return 1; }
    if (g->kind == GNCHAT_K_FETCH) {
        mnc_u8 cnt = 0;
        if (gnchat_parse_fetch(d + MW2_HDR, (mnc_u32) bl, &cnt, g->m)) { g->st_bad++; fail(g, GNCHAT_E_BAD_REPLY, 0); return 1; }
        g->n = cnt;
    } else {
        if (bl != 4u) { g->st_bad++; fail(g, GNCHAT_E_BAD_REPLY, 0); return 1; }
        g->post_id = g32(d + MW2_HDR);
    }
    g->state = GNCHAT_DONE;
    return 1;
}

int gnchat_tick(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now)
{
    (void) w;
    if (g->state != GNCHAT_BUSY) return g->state;
    if (now - g->sent_ms >= GNCHAT_RESEND_MS) {
        if (g->tries >= GNCHAT_TRIES) { fail(g, GNCHAT_E_TIMEOUT, 0); return g->state; }
        g->tries++; g->sent_ms = now; g->st_resent++;
        (void) io->send(io->ctx, g->pkt, g->pkt_len);                 /* the same counter: the server's reply cache answers a repeat */
    }
    return g->state;
}

int gnchat_pump(gnchat *g, mw2 *w, const mnc_io *io, mnc_u32 now)
{
    int i;
    if (g->state != GNCHAT_BUSY) return g->state;
    for (i = 0; i < MNC_RX_BUDGET && g->state == GNCHAT_BUSY; i++) {
        mnc_u8 buf[MNC_MAX_PACKET];
        int n = io->recv(io->ctx, buf, MNC_MAX_PACKET);
        if (n <= 0) break;
        (void) gnchat_rx(g, w, buf, (mnc_u32) n, now);
    }
    return gnchat_tick(g, w, io, now);
}

void gnchat_abort(gnchat *g) { g->state = GNCHAT_IDLE; }

int gnchat_rate_ok(struct gnchat_rate *r, mnc_u32 now, int take)
{
    mnc_u8 i, live = 0;
    mnc_u32 keep[GNCHAT_RATE_N];
    for (i = 0; i < r->n; i++) {                                       /* the sends of the last 10 s */
        mnc_u32 t = r->t[(mnc_u8) ((r->head + GNCHAT_RATE_N - r->n + i) % GNCHAT_RATE_N)];
        if (now - t < GNCHAT_RATE_MS) keep[live++] = t;
    }
    if (live >= GNCHAT_RATE_N) { for (i = 0; i < live; i++) r->t[i] = keep[i]; r->n = live; r->head = (mnc_u8) (live % GNCHAT_RATE_N); return 0; }
    if (take) keep[live++] = now;
    for (i = 0; i < live; i++) r->t[i] = keep[i];
    r->n = live; r->head = (mnc_u8) (live % GNCHAT_RATE_N);
    return 1;
}
