/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#include "sdsec.h"

static int io_recv(void *c, sdc_u8 *b, sdc_u32 max)
{
    struct sdsec *s = (struct sdsec *)c;
    int n = sdtcp_recv(s->tcp, s->h, b, max);
    return n >= 0 ? n : -1;                        /* EOF or error: the transport is gone */
}
static int io_send(void *c, const sdc_u8 *b, sdc_u32 n)
{
    struct sdsec *s = (struct sdsec *)c;
    int r = sdtcp_send(s->tcp, s->h, b, n);
    return r >= 0 ? r : -1;
}

static int open_common(struct sdsec *s, struct sdtcp *tcp, sdc_u32 ip, unsigned short port, sdtls_rand_fn rnd, void *rand_ctx,
                       const sdc_u8 *pins, sdc_u32 npins, const char *sni, sdc_u32 timeout_ms, struct sdtls_web *web)

{
    struct sdtls_io io;
    int h;
    sdc_u8 *b = (sdc_u8 *)s;
    sdc_u32 i;

    for (i = 0; i < sizeof(*s); i++) b[i] = 0;
#ifndef SDTLS_WEBPKI
    (void)web;
#endif
    h = sdtcp_connect(tcp, ip, port);
    if (h < 0) { s->state = SDSEC_FAILED; s->err = SDSEC_E_TCP; s->tcp_err = h; return h; }
    s->tcp = tcp; s->h = h;
    io.recv = io_recv; io.send = io_send; io.ctx = s;
#ifdef SDTLS_WEBPKI
    if (web) { if (sdtls_begin_web(&s->tls, &io, rnd, rand_ctx, web, sni)) { sdtcp_abort(tcp, h); s->state = SDSEC_FAILED; s->err = SDSEC_E_TLS; return -1; } }
    else
#endif
    sdtls_begin(&s->tls, &io, rnd, rand_ctx, pins, npins, sni);
    s->t_open = tcp->net->now;
    s->deadline_ms = timeout_ms;
    s->state = SDSEC_TCP;
    return 0;
}

int sdsec_open(struct sdsec *s, struct sdtcp *tcp, sdc_u32 ip, unsigned short port, sdtls_rand_fn rnd, void *rand_ctx,
               const sdc_u8 *pins, sdc_u32 npins, const char *sni, sdc_u32 timeout_ms)
{
    return open_common(s, tcp, ip, port, rnd, rand_ctx, pins, npins, sni, timeout_ms, 0);
}
#ifdef SDTLS_WEBPKI
int sdsec_open_web(struct sdsec *s, struct sdtcp *tcp, sdc_u32 ip, unsigned short port, sdtls_rand_fn rnd, void *rand_ctx,
                   struct sdtls_web *web, const char *host, sdc_u32 timeout_ms)
{
    return open_common(s, tcp, ip, port, rnd, rand_ctx, 0, 0, host, timeout_ms, web);
}
#endif

int sdsec_poll(struct sdsec *s, int allow_heavy)
{
    int st;
    if (s->state == SDSEC_IDLE || s->state == SDSEC_CLOSED || s->state == SDSEC_FAILED) return s->state;
    if (s->deadline_ms && (sdc_u32)(s->tcp->net->now - s->t_open) > s->deadline_ms && s->state < SDSEC_UP) {
        sdtcp_abort(s->tcp, s->h);
        s->state = SDSEC_FAILED; s->err = SDSEC_E_TIMEOUT;
        return s->state;
    }
    s->allow_heavy = (sdc_u8)allow_heavy;
    if (s->state == SDSEC_TCP) {
        if (sdtcp_established(s->tcp, s->h)) { s->state = SDSEC_TLS; s->t_tcp_up = s->tcp->net->now; }
        else if (sdtcp_state(s->tcp, s->h) == SDTCP_CLOSED || sdtcp_error(s->tcp, s->h)) {
            s->state = SDSEC_FAILED; s->err = SDSEC_E_TCP; s->tcp_err = sdtcp_error(s->tcp, s->h);
            return s->state;
        } else return s->state;
    }
    if (s->state == SDSEC_TLS || s->state == SDSEC_UP || s->state == SDSEC_CLOSING) {
        if (sdtls_heavy_next(&s->tls) && !allow_heavy) { s->heavy_skipped++; return s->state; }
        st = sdtls_poll(&s->tls);
        if (st == SDTLS_FAILED) {
            s->state = SDSEC_FAILED; s->err = SDSEC_E_TLS; s->tls_err = s->tls.err;
            sdtcp_abort(s->tcp, s->h);
        } else if (st == SDTLS_CONNECTED && s->state == SDSEC_TLS) {
            s->state = SDSEC_UP; s->t_tls_up = s->tcp->net->now;
        } else if (st == SDTLS_CLOSED && s->state != SDSEC_CLOSING) {
            sdtcp_close(s->tcp, s->h);
            s->state = SDSEC_CLOSING;
        }
        if (s->state == SDSEC_CLOSING) {
            if (!sdtls_out_pending(&s->tls) && sdtcp_tx_pending(s->tcp, s->h) == 0) sdtcp_close(s->tcp, s->h);
            if (sdtcp_state(s->tcp, s->h) == SDTCP_CLOSED) s->state = SDSEC_CLOSED;
        }
    }
    return s->state;
}

int sdsec_write(struct sdsec *s, const void *buf, sdc_u32 n)
{
    if (s->state != SDSEC_UP) return s->state == SDSEC_TCP || s->state == SDSEC_TLS ? 0 : -1;
    return sdtls_write(&s->tls, (const sdc_u8 *)buf, n);
}
int sdsec_read(struct sdsec *s, void *buf, sdc_u32 max)
{
    if (s->state == SDSEC_UP || s->state == SDSEC_CLOSING) return sdtls_read(&s->tls, (sdc_u8 *)buf, max);
    if (s->state == SDSEC_CLOSED) return SDTLS_READ_EOF;
    return s->state == SDSEC_FAILED ? SDTLS_READ_ERR : 0;
}
void sdsec_close(struct sdsec *s)
{
    if (s->state == SDSEC_UP) { sdtls_close(&s->tls); s->state = SDSEC_CLOSING; }
    else if (s->state == SDSEC_TCP || s->state == SDSEC_TLS) { sdtcp_abort(s->tcp, s->h); s->state = SDSEC_CLOSED; }
}
