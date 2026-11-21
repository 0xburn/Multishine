/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* lnk: Link This Console, see lnk.h.  C89, no libc, no heap. */
#include "lnk.h"
#include "../net/tls/sdc25519.h"
#include "../net/tls/sdcrypto.h"

#define LNK_PROTO 1u                /* client_recog.h CR_PROTO (the client block's protocol byte, as mnc.c put_client) */

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }
static void zero(void *p, mnc_u32 n) { volatile mnc_u8 *b = (volatile mnc_u8 *) p; while (n--) *b++ = 0; }
static void copy(void *d, const void *s, mnc_u32 n) { mnc_u8 *a = (mnc_u8 *) d; const mnc_u8 *b = (const mnc_u8 *) s; while (n--) *a++ = *b++; }

void lnk_init(lnk *l, mnc *c, const mnc_u8 *seed, mnc_u32 n)
{
    zero(l, sizeof *l);
    l->c = c;
    l->timeout_ms = 700u;
    mw2_init(&l->w);
    l->sw = &l->w;
    if (c) { copy(l->w.pin, c->w.pin, sizeof l->w.pin); l->w.npin = c->w.npin; }
    if (seed && n) mw2_seed(&l->w, seed, n);
}

void lnk_set_key(lnk *l, const mnc_u8 priv[32])
{
    static const mnc_u8 nine[32] = { 9 };
    copy(l->c_priv, priv, 32);
    sdc_x25519(l->c_pub, l->c_priv, nine);
}

static void fail(lnk *l, int err, int status) { l->state = LNK_FAILED; l->err = err; l->status = status; l->phase = 0; }

static void transmit(lnk *l)
{
    if (l->phase == 1) (void) l->c->io.send(l->c->io.ctx, l->w.hs1, MW2_HS1_LEN);
    else (void) l->c->io.send(l->c->io.ctx, l->pkt, l->pkt_len);
    l->st_sent++;
}
static void seal_and_send(lnk *l, mnc_u32 now)
{
    copy(l->pkt + MW2_HDR, l->req, l->req_len);
    l->pkt_len = mw2_seal(l->sw, l->pkt, l->op, l->req_len);
    l->ctr = l->sw->ctr;
    l->phase = 2; l->tries = 1; l->sent_ms = now;
    transmit(l);
}
static int begin(lnk *l, mnc_u8 op, mnc_u32 now)
{
    if (!l->c || l->sw->npin == 0) { fail(l, LNK_E_ARG, 0); return LNK_E_ARG; }
    if (l->state == LNK_BUSY) return LNK_E_BUSY;
    l->op = op; l->state = LNK_BUSY; l->err = LNK_E_NONE; l->status = 0;
    if (mw2_ready(l->sw, now)) seal_and_send(l, now);
    else if (l->sw != &l->w) { fail(l, LNK_E_ARG, 0); return LNK_E_ARG; }          /* a borrowed session is never handshaken here */
    else { (void) mw2_hs1(&l->w, now); l->phase = 1; l->tries = 1; l->sent_ms = now; transmit(l); }
    return 0;
}

int lnk_start(lnk *l, mnc_u32 now)
{
    mnc *c = l->c;
    mnc_u8 *p = l->req;
    if (!c) return LNK_E_ARG;
    p[0] = c->version[0]; p[1] = c->version[1]; p[2] = c->version[2]; p[3] = LNK_PROTO; p[4] = c->platform;
    p[5] = (mnc_u8) (c->caps >> 8); p[6] = (mnc_u8) c->caps;
    p[7] = (mnc_u8) (c->codeset >> 24); p[8] = (mnc_u8) (c->codeset >> 16); p[9] = (mnc_u8) (c->codeset >> 8); p[10] = (mnc_u8) c->codeset;
    p[11] = (mnc_u8) (c->rules >> 24); p[12] = (mnc_u8) (c->rules >> 16); p[13] = (mnc_u8) (c->rules >> 8); p[14] = (mnc_u8) c->rules;
    l->req_len = 15;
    l->have_start = 0;
    l->sw = &l->w;
    return begin(l, LNK_OP_START, now);
}

int lnk_poll_req(lnk *l, mnc_u32 now)
{
    if (!l->have_start) { fail(l, LNK_E_ARG, 0); return LNK_E_ARG; }
    copy(l->req, l->dc, 16);
    copy(l->req + 16, l->c_pub, 32);
    l->req_len = 48;
    l->sw = &l->w;
    return begin(l, LNK_OP_POLL, now);
}

int lnk_forget(lnk *l, mnc_u32 now, unsigned flags)
{
    if (!l->c || !l->c->w.dev_sess || !mw2_ready(&l->c->w, now)) { fail(l, LNK_E_ARG, 0); return LNK_E_ARG; }
    l->sw = &l->c->w;
    l->req[0] = 0xA5u; l->req[1] = (mnc_u8) flags;
    l->req_len = 2;
    return begin(l, LNK_OP_FORGET, now);
}

int lnk_cancel(lnk *l, mnc_u32 now)
{
    if (!l->have_start || !mw2_ready(&l->w, now)) { l->state = LNK_IDLE; return 0; }
    copy(l->req, l->dc, 16);
    l->req_len = 16;
    l->op = LNK_OP_CANCEL;
    copy(l->pkt + MW2_HDR, l->req, l->req_len);
    l->pkt_len = mw2_seal(&l->w, l->pkt, l->op, l->req_len);
    (void) l->c->io.send(l->c->io.ctx, l->pkt, l->pkt_len);
    l->st_sent++;
    l->state = LNK_IDLE; l->phase = 0;
    return 0;
}

void lnk_abort(lnk *l) { l->state = LNK_IDLE; l->phase = 0; }
void lnk_wipe(lnk *l) { zero(l, sizeof *l); }

/* ---- replies ---- */
typedef struct { const mnc_u8 *b; mnc_u32 n, i; } rd;
static int pstr(rd *r, char *out, unsigned max)
{
    unsigned k, len;
    if (r->i >= r->n) return -1;
    len = r->b[r->i];
    if (len == 0 || len > max || r->i + 1u + len > r->n) return -1;
    for (k = 0; k < len; k++) {
        mnc_u8 ch = r->b[r->i + 1u + k];
        if (ch < 0x20u || ch > 0x7Eu) return -1;
        out[k] = (char) ch;
    }
    out[len] = 0;
    r->i += 1u + len;
    return 0;
}

int lnk_open_reply(const mnc_u8 *b, mnc_u32 n, const mnc_u8 dc[16], const mnc_u8 c_priv[32], const mnc_u8 c_pub[32],
                   char code[24], char name[24], mnc_u8 dev[8], mnc_u8 secret[16], mnc_u8 sid[8])
{
    static const mnc_u8 zsalt[32] = { 0 };
    static const char info[] = "ms-link-v1";
    rd r;
    const mnc_u8 *ct, *s_pub, *tag;
    mnc_u8 shared[32], prk[32], key[32], msg[10 + 16 + 32 + 32 + 1], aad[24], nonce[12], ctt[32];
    mnc_u32 i, any = 0;
    int ok;
    r.b = b; r.n = n; r.i = 0;
    if (pstr(&r, code, 23) || pstr(&r, name, 23)) return -1;
    if (r.i + 8u + 16u + 8u + 32u + 16u != n) return -1;
    copy(dev, b + r.i, 8); ct = b + r.i + 8u; copy(sid, b + r.i + 24u, 8); s_pub = b + r.i + 32u; tag = b + r.i + 64u;
    sdc_x25519(shared, c_priv, s_pub);
    for (i = 0; i < 32u; i++) any |= shared[i];
    if (!any) return -2;
    sdc_hkdf_extract(zsalt, shared, 32, prk);
    for (i = 0; i < 10u; i++) msg[i] = (mnc_u8) info[i];
    copy(msg + 10, dc, 16); copy(msg + 26, c_pub, 32); copy(msg + 58, s_pub, 32); msg[90] = 1;
    sdc_hmac(prk, 32, msg, sizeof msg, key);                 /* HKDF-Expand, one block: T(1) = HMAC(prk, info | 0x01) */
    copy(aad, dev, 8); copy(aad + 8, dc, 16);
    zero(nonce, 12);
    copy(ctt, ct, 16); copy(ctt + 16, tag, 16);
    ok = sdc_aead_open(key, nonce, aad, 24, ctt, 16, secret);
    zero(shared, 32); zero(prk, 32); zero(key, 32);
    return ok ? 0 : -2;
}

static void on_reply(lnk *l, const mnc_u8 *b, mnc_u32 n)
{
    if (l->op == LNK_OP_START) {
        rd r;
        r.b = b; r.n = n; r.i = 0;
        if (pstr(&r, l->user, 9) || r.i + 16u + 3u != n) { l->st_bad++; fail(l, LNK_E_BAD_REPLY, 0); return; }
        copy(l->dc, b + r.i, 16);
        l->expires = (mnc_u16) ((b[r.i + 16u] << 8) | b[r.i + 17u]);
        l->interval = b[r.i + 18u];
        if (!l->interval) l->interval = 5;
        l->have_start = 1;
    } else if (l->op == LNK_OP_FORGET) {
        if (n) { l->st_bad++; fail(l, LNK_E_BAD_REPLY, 0); return; }
    } else if (l->op == LNK_OP_POLL) {
        if (lnk_open_reply(b, n, l->dc, l->c_priv, l->c_pub, l->code, l->name, l->dev, l->secret, l->server_id)) { l->st_bad++; fail(l, LNK_E_BAD_REPLY, 0); return; }
    }
    l->state = LNK_DONE; l->phase = 0;
}

int lnk_poll(lnk *l, mnc_u32 now)
{
    mnc *c = l->c;
    int i;
    if (l->state != LNK_BUSY) return l->state;
    for (i = 0; i < MNC_RX_BUDGET; i++) {
        int n = c->io.recv(c->io.ctx, l->rx, MNC_MAX_PACKET);
        if (n <= 0) break;
        l->st_rx++;
        if (l->phase == 1) {
            int h = mw2_hs2(&l->w, now, l->rx, (mnc_u32) n);
            if (h == 1) { seal_and_send(l, now); return l->state; }
            if (h < 0) { fail(l, LNK_E_HS, -h); return l->state; }
            l->st_bad++;
            continue;
        }
        if (n < (int) MW2_HDR || (l->rx[2] & 0x7Fu) != l->op || g32(l->rx + 8) != l->ctr) { l->st_bad++; continue; }
        {
            mnc_u8 st = 0;
            mw2_u32 bl = 0;
            int r = mw2_open(l->sw, now, l->rx, (mnc_u32) n, l->op, l->ctr, &st, &bl);
            if (r == 2) { mw2_drop(l->sw); fail(l, LNK_E_TIMEOUT, 0); return l->state; }      /* the server forgot the session: the caller asks again (a new handshake) */
            if (r != 1) { l->st_bad++; continue; }
            if (st != 0) { fail(l, LNK_E_SERVER, st); return l->state; }
            on_reply(l, l->rx + MW2_HDR, (mnc_u32) bl);
            return l->state;
        }
    }
    if (now - l->sent_ms >= l->timeout_ms) {
        if (l->tries >= LNK_TRIES) { if (l->phase == 1) mw2_drop(l->sw); fail(l, LNK_E_TIMEOUT, 0); return l->state; }
        l->tries++; l->sent_ms = now; l->st_resent++;
        transmit(l);                                          /* the same bytes: the server's caches answer a repeat, nothing runs twice */
    }
    return l->state;
}
