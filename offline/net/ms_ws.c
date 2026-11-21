/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* ms_ws: RFC 6455 client core, see ms_ws.h.  Strict C89, no libc, no heap. */
#include "ms_ws.h"

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static void ws_cpy(ws_u8 *d, const ws_u8 *s, ws_u32 n) { ws_u32 i; for (i = 0; i < n; i++) d[i] = s[i]; }
static void ws_move(ws_u8 *d, const ws_u8 *s, ws_u32 n)   /* d <= s only */ { ws_u32 i; for (i = 0; i < n; i++) d[i] = s[i]; }
static ws_u32 ws_len(const char *s) { ws_u32 n = 0; while (s[n]) n++; return n; }
static ws_u8 ws_lc(ws_u8 c) { return (ws_u8)((c >= 'A' && c <= 'Z') ? c + 32 : c); }

/* ---- SHA-1 (FIPS 180-4), one shot over a short message ---- */
static ws_u32 rol(ws_u32 x, int n) { return (x << n) | (x >> (32 - n)); }
static void sha1_block(ws_u32 h[5], const ws_u8 *p)
{
    ws_u32 w[80], a, b, c, d, e, t;
    int i;
    for (i = 0; i < 16; i++) w[i] = ((ws_u32)p[4 * i] << 24) | ((ws_u32)p[4 * i + 1] << 16) | ((ws_u32)p[4 * i + 2] << 8) | (ws_u32)p[4 * i + 3];
    for (i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4];
    for (i = 0; i < 80; i++) {
        ws_u32 f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}
/* n <= 119 (two blocks) */
static void sha1_short(const ws_u8 *m, ws_u32 n, ws_u8 out[20])
{
    ws_u32 h[5];
    ws_u8 blk[128];
    ws_u32 i, total = (n + 9u + 63u) & ~63u, bits = n * 8u;
    h[0] = 0x67452301u; h[1] = 0xEFCDAB89u; h[2] = 0x98BADCFEu; h[3] = 0x10325476u; h[4] = 0xC3D2E1F0u;
    for (i = 0; i < 128; i++) blk[i] = 0;
    ws_cpy(blk, m, n);
    blk[n] = 0x80;
    blk[total - 4] = (ws_u8)(bits >> 24); blk[total - 3] = (ws_u8)(bits >> 16); blk[total - 2] = (ws_u8)(bits >> 8); blk[total - 1] = (ws_u8)bits;
    for (i = 0; i < total; i += 64) sha1_block(h, blk + i);
    for (i = 0; i < 5; i++) { out[4 * i] = (ws_u8)(h[i] >> 24); out[4 * i + 1] = (ws_u8)(h[i] >> 16); out[4 * i + 2] = (ws_u8)(h[i] >> 8); out[4 * i + 3] = (ws_u8)h[i]; }
}
static const char b64t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
/* n bytes -> 4*ceil(n/3) characters, padded with '='; returns the length */
static ws_u32 b64(const ws_u8 *s, ws_u32 n, char *o)
{
    ws_u32 i, k = 0;
    for (i = 0; i + 2 < n; i += 3) {
        o[k++] = b64t[s[i] >> 2]; o[k++] = b64t[((s[i] & 3) << 4) | (s[i + 1] >> 4)];
        o[k++] = b64t[((s[i + 1] & 15) << 2) | (s[i + 2] >> 6)]; o[k++] = b64t[s[i + 2] & 63];
    }
    if (n - i == 1) { o[k++] = b64t[s[i] >> 2]; o[k++] = b64t[(s[i] & 3) << 4]; o[k++] = '='; o[k++] = '='; }
    else if (n - i == 2) { o[k++] = b64t[s[i] >> 2]; o[k++] = b64t[((s[i] & 3) << 4) | (s[i + 1] >> 4)]; o[k++] = b64t[(s[i + 1] & 15) << 2]; o[k++] = '='; }
    return k;
}

void ms_ws_accept_of(const char key24[24], char out29[29])
{
    ws_u8 m[24 + 36], d[20];
    ws_u32 i;
    for (i = 0; i < 24; i++) m[i] = (ws_u8)key24[i];
    for (i = 0; i < 36; i++) m[24 + i] = (ws_u8)WS_GUID[i];
    sha1_short(m, 60, d);
    (void)b64(d, 20, out29);
    out29[28] = 0;
}

/* ---- core ---- */
void ms_ws_init(struct ms_ws *w, const struct ms_ws_io *io, ws_u8 *tx, ws_u32 txcap, ws_u8 *rx, ws_u32 rxcap)
{
    ws_u8 *p = (ws_u8 *)w;
    ws_u32 i;
    for (i = 0; i < (ws_u32)sizeof *w; i++) p[i] = 0;
    w->io = *io; w->tx = tx; w->txcap = txcap; w->rx = rx; w->rxcap = rxcap;
    w->close_code = 1005u;
}

static int fail(struct ms_ws *w, int e) { w->state = MS_WS_FAILED; w->err = e; return e; }

static int put(struct ms_ws *w, const char *s, ws_u32 *k)
{
    ws_u32 n = ws_len(s);
    if (*k + n > w->txcap) return 0;
    ws_cpy(w->tx + *k, (const ws_u8 *)s, n); *k += n;
    return 1;
}

int ms_ws_start(struct ms_ws *w, const char *host, const char *path, const ws_u8 key16[16])
{
    ws_u32 k = 0, kn;
    char key[25];
    if (!w || !host || !path || !key16 || w->state != MS_WS_IDLE) return MS_WS_E_STATE;
    if (w->rxcap < 64u || w->txcap < 64u) return MS_WS_E_ARG;
    kn = b64(key16, 16, key);
    key[kn] = 0;
    ms_ws_accept_of(key, w->accept);
    if (!put(w, "GET ", &k) || !put(w, path, &k) || !put(w, " HTTP/1.1\r\nHost: ", &k) || !put(w, host, &k)
        || !put(w, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ", &k) || !put(w, key, &k)
        || !put(w, "\r\nSec-WebSocket-Version: 13\r\n\r\n", &k)) return MS_WS_E_TOOBIG;
    w->tx_len = k; w->tx_sent = 0; w->state = MS_WS_HS_SEND;
    return MS_WS_OK;
}

/* a masked client frame into w->tx (tx must be idle) */
static int build(struct ms_ws *w, ws_u8 op, const ws_u8 *pl, ws_u32 n, const ws_u8 *mask)
{
    ws_u32 k = 0, i;
    if (n > 65535u || n + MS_WS_HDR_MAX > w->txcap) return MS_WS_E_TOOBIG;
    w->tx[k++] = (ws_u8)(0x80u | op);
    if (n < 126u) w->tx[k++] = (ws_u8)(0x80u | n);
    else { w->tx[k++] = 0x80u | 126u; w->tx[k++] = (ws_u8)(n >> 8); w->tx[k++] = (ws_u8)n; }
    for (i = 0; i < 4; i++) w->tx[k + i] = mask[i];
    k += 4;
    for (i = 0; i < n; i++) w->tx[k + i] = (ws_u8)(pl[i] ^ mask[i & 3u]);
    w->tx_len = k + n; w->tx_sent = 0;
    return MS_WS_OK;
}

int ms_ws_tx_idle(const struct ms_ws *w) { return w->tx_len == 0 && !w->pong_due && !w->close_due; }

/* flush what is queued; queue the next control frame when idle.  < 0: failed */
static int flush(struct ms_ws *w, const ws_u8 *mask)
{
    int guard;
    for (guard = 0; guard < 3; guard++) {
        while (w->tx_sent < w->tx_len) {
            int n = w->io.send(w->io.ctx, w->tx + w->tx_sent, w->tx_len - w->tx_sent);
            if (n < 0) return fail(w, MS_WS_E_IO);
            if (n == 0) return 0;
            if ((ws_u32)n > w->tx_len - w->tx_sent) return fail(w, MS_WS_E_IO);
            w->tx_sent += (ws_u32)n;
        }
        if (w->tx_len) {
            w->tx_len = w->tx_sent = 0;
            if (w->state == MS_WS_HS_SEND) w->state = MS_WS_HS_RECV;
        }
        if (w->state == MS_WS_HS_SEND || w->state == MS_WS_HS_RECV) return 0;
        if (w->pong_due) { w->pong_due = 0; if (build(w, 0xAu, w->pong_buf, w->pong_len, mask) < 0) return fail(w, MS_WS_E_TOOBIG); continue; }
        if (w->close_due) { w->close_due = 0; if (build(w, 0x8u, w->close_buf, w->close_pl, mask) < 0) return fail(w, MS_WS_E_TOOBIG); continue; }
        break;
    }
    if (w->got_close && w->close_sent && ms_ws_tx_idle(w) && w->state == MS_WS_CLOSING) w->state = MS_WS_CLOSED;
    return 0;
}

/* ---- the 101 answer ---- */
static int tok_has(const ws_u8 *v, ws_u32 n, const char *tok)   /* comma separated list contains tok (case-insensitive) */
{
    ws_u32 i = 0, tl = ws_len(tok);
    while (i < n) {
        ws_u32 s, e, j;
        while (i < n && (v[i] == ' ' || v[i] == '\t' || v[i] == ',')) i++;
        s = i;
        while (i < n && v[i] != ',') i++;
        e = i;
        while (e > s && (v[e - 1] == ' ' || v[e - 1] == '\t')) e--;
        if (e - s == tl) { for (j = 0; j < tl && ws_lc(v[s + j]) == (ws_u8)tok[j]; j++) {} if (j == tl) return 1; }
    }
    return 0;
}
static int name_is(const ws_u8 *l, ws_u32 n, const char *nm)
{
    ws_u32 i, k = ws_len(nm);
    if (n != k) return 0;
    for (i = 0; i < k; i++) if (ws_lc(l[i]) != (ws_u8)nm[i]) return 0;
    return 1;
}

static int parse_hs(struct ms_ws *w, ws_u32 end)   /* end = index of the first byte after the blank line */
{
    const ws_u8 *r = w->rx;
    ws_u32 p = 0, e;
    int up = 0, con = 0, acc = 0;
    static const char st[] = "HTTP/1.1 101";
    ws_u32 i;
    for (i = 0; i < 12; i++) if (r[i] != (ws_u8)st[i]) return MS_WS_E_HS;
    if (!(r[12] == ' ' || r[12] == '\r')) return MS_WS_E_HS;
    while (p < end && !(r[p] == '\r' && r[p + 1] == '\n')) p++;
    p += 2;
    while (p + 2 < end) {
        ws_u32 ls = p, c, vs, ve;
        while (p < end && !(r[p] == '\r' && r[p + 1] == '\n')) p++;
        e = p; p += 2;
        for (c = ls; c < e && r[c] != ':'; c++) {}
        if (c >= e) return MS_WS_E_HS;
        vs = c + 1; ve = e;
        while (vs < ve && (r[vs] == ' ' || r[vs] == '\t')) vs++;
        while (ve > vs && (r[ve - 1] == ' ' || r[ve - 1] == '\t')) ve--;
        if (name_is(r + ls, c - ls, "upgrade")) { if (ve - vs == 9 && tok_has(r + vs, ve - vs, "websocket")) up = 1; else return MS_WS_E_HS; }
        else if (name_is(r + ls, c - ls, "connection")) { if (tok_has(r + vs, ve - vs, "upgrade")) con = 1; }
        else if (name_is(r + ls, c - ls, "sec-websocket-accept")) {
            ws_u32 j;
            if (ve - vs != 28) return MS_WS_E_ACCEPT;
            for (j = 0; j < 28; j++) if (r[vs + j] != (ws_u8)w->accept[j]) return MS_WS_E_ACCEPT;
            acc = 1;
        }
        else if (name_is(r + ls, c - ls, "sec-websocket-extensions") || name_is(r + ls, c - ls, "sec-websocket-protocol")) return MS_WS_E_HS;
    }
    if (!up || !con) return MS_WS_E_HS;
    if (!acc) return MS_WS_E_ACCEPT;
    return MS_WS_OK;
}

static int hs_step(struct ms_ws *w)
{
    ws_u32 i;
    if (w->rx_have < w->rxcap) {
        int n = w->io.recv(w->io.ctx, w->rx + w->rx_have, w->rxcap - w->rx_have);
        if (n < 0) return fail(w, MS_WS_E_IO);
        if ((ws_u32)n > w->rxcap - w->rx_have) return fail(w, MS_WS_E_IO);
        w->rx_have += (ws_u32)n;
    }
    for (i = 0; i + 3 < w->rx_have; i++)
        if (w->rx[i] == '\r' && w->rx[i + 1] == '\n' && w->rx[i + 2] == '\r' && w->rx[i + 3] == '\n') {
            ws_u32 end = i + 4;
            int rc = w->rx_have >= 13u ? parse_hs(w, end) : MS_WS_E_HS;
            if (rc < 0) return fail(w, rc);
            ws_move(w->rx, w->rx + end, w->rx_have - end);
            w->rx_have -= end;
            w->state = MS_WS_OPEN;
            return MS_WS_EV_OPEN;
        }
    if (w->rx_have >= w->rxcap) return fail(w, MS_WS_E_HS);
    return MS_WS_EV_NONE;
}

/* ---- frames ---- */
/* returns event (> 0), 0 = need more bytes, < 0 error */
static int frame_step(struct ms_ws *w)
{
    ws_u8 b0, b1, op;
    ws_u32 hl, len;
    if (w->rx_have < 2u) return 0;
    b0 = w->rx[0]; b1 = w->rx[1];
    op = (ws_u8)(b0 & 0x0Fu);
    if (b0 & 0x70u) return fail(w, MS_WS_E_PROTO);
    if (b1 & 0x80u) return fail(w, MS_WS_E_PROTO);                       /* a server frame is never masked */
    if (op == 0u || !(b0 & 0x80u)) return fail(w, MS_WS_E_FRAG);        /* continuation or a non-final frame */
    if (!(op == 1u || op == 2u || op == 8u || op == 9u || op == 10u)) return fail(w, MS_WS_E_PROTO);
    len = b1 & 0x7Fu; hl = 2u;
    if (len == 126u) {
        hl = 4u;
        if (w->rx_have < hl) return 0;
        len = ((ws_u32)w->rx[2] << 8) | w->rx[3];
        if (len < 126u) return fail(w, MS_WS_E_PROTO);                  /* not minimal */
    } else if (len == 127u) {
        hl = 10u;
        if (w->rx_have < hl) return 0;
        if (w->rx[2] || w->rx[3] || w->rx[4] || w->rx[5]) return fail(w, MS_WS_E_TOOBIG);
        len = ((ws_u32)w->rx[6] << 24) | ((ws_u32)w->rx[7] << 16) | ((ws_u32)w->rx[8] << 8) | w->rx[9];
        if (len < 65536u && !(w->rx[6] & 0x80u)) return fail(w, MS_WS_E_PROTO);   /* not minimal */
        if (w->rx[6] & 0x80u) return fail(w, MS_WS_E_TOOBIG);
    }
    if (op >= 8u && len > MS_WS_CTRL_MAX) return fail(w, MS_WS_E_PROTO);
    if (len > w->rxcap - hl) return fail(w, MS_WS_E_TOOBIG);
    if (w->rx_have < hl + len) return 0;
    w->msg = w->rx + hl; w->msg_len = len; w->consume = hl + len;
    if (op == 1u) return MS_WS_EV_TEXT;
    if (op == 2u) return MS_WS_EV_BINARY;
    if (op == 10u) return MS_WS_EV_PONG;
    if (op == 9u) {
        ws_u32 i;
        for (i = 0; i < len; i++) w->pong_buf[i] = w->msg[i];
        w->pong_len = (ws_u8)len; w->pong_due = 1;
        return MS_WS_EV_NONE + 100;                                      /* internal: ping handled, keep going */
    }
    /* close */
    if (len == 1u) return fail(w, MS_WS_E_PROTO);
    if (len >= 2u) {
        ws_u32 code = ((ws_u32)w->msg[0] << 8) | w->msg[1];
        if (code < 1000u || code >= 5000u || code == 1004u || code == 1005u || code == 1006u || code == 1015u) return fail(w, MS_WS_E_PROTO);
        w->close_code = code;
        if (!w->close_sent) { w->close_buf[0] = w->msg[0]; w->close_buf[1] = w->msg[1]; w->close_pl = 2; }
    } else {
        w->close_code = 1005u;
        if (!w->close_sent) w->close_pl = 0;
    }
    w->got_close = 1;
    if (!w->close_sent) { w->close_sent = 1; w->close_due = 1; }
    w->state = MS_WS_CLOSING;
    return MS_WS_EV_CLOSE;
}

int ms_ws_poll(struct ms_ws *w, const ws_u8 mask4[4])
{
    int rc;
    if (!w || !mask4) return MS_WS_E_ARG;
    if (w->state == MS_WS_FAILED) return w->err;
    if (w->state == MS_WS_IDLE || w->state == MS_WS_CLOSED) return MS_WS_E_STATE;
    if (w->consume) { ws_move(w->rx, w->rx + w->consume, w->rx_have - w->consume); w->rx_have -= w->consume; w->consume = 0; w->msg = 0; w->msg_len = 0; }
    rc = flush(w, mask4);
    if (rc < 0) return rc;
    if (w->state == MS_WS_HS_SEND) return MS_WS_EV_NONE;
    if (w->state == MS_WS_HS_RECV) return hs_step(w);
    if (w->got_close) { rc = flush(w, mask4); return rc < 0 ? rc : MS_WS_EV_NONE; }
    for (;;) {
        rc = frame_step(w);
        if (rc < 0) return rc;
        if (rc == MS_WS_EV_NONE + 100) {                                  /* ping: drop it, answer, look at the next frame */
            ws_move(w->rx, w->rx + w->consume, w->rx_have - w->consume); w->rx_have -= w->consume; w->consume = 0; w->msg = 0; w->msg_len = 0;
            rc = flush(w, mask4);
            if (rc < 0) return rc;
            continue;
        }
        if (rc > 0) { int r2 = flush(w, mask4); return r2 < 0 ? r2 : rc; }
        if (w->rx_have >= w->rxcap) return fail(w, MS_WS_E_TOOBIG);
        {
            int n = w->io.recv(w->io.ctx, w->rx + w->rx_have, w->rxcap - w->rx_have);
            if (n < 0) return fail(w, MS_WS_E_IO);
            if (n == 0) return MS_WS_EV_NONE;
            if ((ws_u32)n > w->rxcap - w->rx_have) return fail(w, MS_WS_E_IO);
            w->rx_have += (ws_u32)n;
        }
    }
}

static int send_data(struct ms_ws *w, ws_u8 op, const ws_u8 *msg, ws_u32 len, const ws_u8 *mask)
{
    if (!w || !mask || (!msg && len)) return MS_WS_E_ARG;
    if (w->state != MS_WS_OPEN || w->close_sent) return MS_WS_E_STATE;
    if (w->tx_len || w->pong_due || w->close_due) return MS_WS_BUSY;
    return build(w, op, msg, len, mask);
}
int ms_ws_send_text(struct ms_ws *w, const ws_u8 *msg, ws_u32 len, const ws_u8 mask4[4]) { return send_data(w, 1u, msg, len, mask4); }
int ms_ws_send_binary(struct ms_ws *w, const ws_u8 *msg, ws_u32 len, const ws_u8 mask4[4]) { return send_data(w, 2u, msg, len, mask4); }

int ms_ws_close(struct ms_ws *w, ws_u32 code, const ws_u8 mask4[4])
{
    (void)mask4;
    if (!w) return MS_WS_E_ARG;
    if (w->state != MS_WS_OPEN || w->close_sent) return MS_WS_E_STATE;
    if (code && (code < 1000u || code >= 5000u || code == 1004u || code == 1005u || code == 1006u || code == 1015u)) return MS_WS_E_ARG;
    w->close_pl = code ? 2 : 0;
    w->close_buf[0] = (ws_u8)(code >> 8); w->close_buf[1] = (ws_u8)code;
    w->close_sent = 1; w->close_due = 1; w->state = MS_WS_CLOSING;
    return MS_WS_OK;
}
