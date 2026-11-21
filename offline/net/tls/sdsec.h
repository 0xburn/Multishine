/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SDSEC_H
#define SDSEC_H
/* sdsec: one secure client session = sdtcp connection + sdtls handshake + application stream, as a poll-driven state machine
   .  The caller owns the schedule: call sdsec_poll once per menu tick AFTER sdnet_poll, and pass
   allow_heavy = 1 only on ticks where a 4-10 ms crypto operation may run (menus: always; never inside the rollback match loop). */
#include "../sdtcp.h"
#include "sdtls.h"

#define SDSEC_IDLE 0
#define SDSEC_TCP 1
#define SDSEC_TLS 2
#define SDSEC_UP 3
#define SDSEC_CLOSING 4
#define SDSEC_CLOSED 5
#define SDSEC_FAILED 6

#define SDSEC_E_NONE 0
#define SDSEC_E_TCP 1           /* connect failed (refused, timeout); see tcp_err */
#define SDSEC_E_TLS 2           /* handshake failed; see tls_err */
#define SDSEC_E_TIMEOUT 3       /* whole-session deadline */
#define SDSEC_E_BUSY 4

struct sdsec {
    struct sdtcp *tcp;
    int h;
    struct sdtls tls;
    sdc_u8 state, allow_heavy;
    int err, tcp_err, tls_err;
    sdc_u32 t_open, t_tcp_up, t_tls_up, deadline_ms;    /* sdnet clock (ms) */
    sdc_u32 heavy_skipped;                              /* polls where a heavy step waited for allow_heavy */
};
int sdsec_open(struct sdsec *s, struct sdtcp *tcp, sdc_u32 ip, unsigned short port, sdtls_rand_fn rnd, void *rand_ctx,
               const sdc_u8 *pins, sdc_u32 npins, const char *sni, sdc_u32 timeout_ms);
#ifdef SDTLS_WEBPKI
/* Same, but authenticating a public web server (WebPKI: chain to the anchors in `web`, host name `host`, dates against web->now). */
int sdsec_open_web(struct sdsec *s, struct sdtcp *tcp, sdc_u32 ip, unsigned short port, sdtls_rand_fn rnd, void *rand_ctx,
                   struct sdtls_web *web, const char *host, sdc_u32 timeout_ms);
#endif
int sdsec_poll(struct sdsec *s, int allow_heavy);
int sdsec_write(struct sdsec *s, const void *buf, sdc_u32 n);     /* bytes accepted, 0 = busy, <0 closed/failed */
int sdsec_read(struct sdsec *s, void *buf, sdc_u32 max);          /* bytes, 0 = none yet, SDTLS_READ_EOF, SDTLS_READ_ERR */
void sdsec_close(struct sdsec *s);
#endif
