/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* cred: see cred.h.  C89, no libc, no heap. */
#include "cred.h"

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }
static void fail(cred *s, int err, int status) { s->state = CRED_FAILED; s->err = err; s->status = status; }

void cred_init(cred *s, mnc *c)
{
    mnc_u8 *b = (mnc_u8 *) s;
    mnc_u32 i;
    for (i = 0; i < sizeof *s; i++) b[i] = 0;
    s->c = c; s->state = CRED_IDLE; s->timeout_ms = 700u;
}

int cred_begin(cred *s, mnc_u8 op, const mnc_u8 *req, mnc_u32 n, mnc_u32 now)
{
    mnc *c = s->c;
    mnc_u32 i, len;
    if (s->state == CRED_BUSY) return CRED_E_BUSY;
    if (n > CRED_REQ_MAX || (op != 0x40u && (op < 0x50u || op > 0x54u))) { fail(s, CRED_E_ARG, 0); return CRED_E_ARG; }
    if (!mw2_ready(&c->w, now)) { fail(s, CRED_E_NOSESSION, 0); return CRED_E_NOSESSION; }
    for (i = 0; i < n; i++) c->req[MW2_HDR + i] = req[i];
    len = mw2_seal(&c->w, c->req, op, n);
    if (len > sizeof s->pkt) { fail(s, CRED_E_ARG, 0); return CRED_E_ARG; }
    for (i = 0; i < len; i++) s->pkt[i] = c->req[i];
    s->pkt_len = len; s->ctr = c->w.ctr; s->op = op; s->sent_ms = now; s->tries = 1;
    s->state = CRED_BUSY; s->err = CRED_E_NONE; s->status = 0; s->reply = 0; s->reply_n = 0;
    (void) c->io.send(c->io.ctx, s->pkt, len);
    return 0;
}

int cred_poll(cred *s, mnc_u32 now)
{
    mnc *c = s->c;
    int i;
    if (s->state != CRED_BUSY) return s->state;
    for (i = 0; i < MNC_RX_BUDGET; i++) {
        mnc_u8 st = 0, op;
        mw2_u32 bl = 0;
        int n = c->io.recv(c->io.ctx, c->rx, MNC_MAX_PACKET), r;
        if (n <= 0) break;
        if (n < (int) MW2_HDR || n > MNC_MAX_PACKET) continue;
        op = (mnc_u8) (c->rx[2] & 0x7Fu);
        if (op != s->op || g32(c->rx + 8) != s->ctr) continue;
        r = mw2_open(&c->w, now, c->rx, (mnc_u32) n, op, s->ctr, &st, &bl);
        if (r == 2) { fail(s, CRED_E_NOSESSION, 0); return s->state; }
        if (r != 1) continue;
        if (st != 0) { fail(s, CRED_E_SERVER, st); return s->state; }
        s->reply = c->rx + MW2_HDR; s->reply_n = (mnc_u32) bl;
        s->state = CRED_DONE_;
        return s->state;
    }
    if (now - s->sent_ms >= s->timeout_ms) {
        if (s->tries >= CRED_TRIES) { fail(s, CRED_E_TIMEOUT, 0); return s->state; }
        s->tries++; s->sent_ms = now;
        (void) c->io.send(c->io.ctx, s->pkt, s->pkt_len);
    }
    return s->state;
}

void cred_abort(cred *s) { s->state = CRED_IDLE; }
