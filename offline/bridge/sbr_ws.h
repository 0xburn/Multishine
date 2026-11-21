/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbrw: the Slippi 3.7.0 matchmaker client LOGIC (WebSocket text-JSON queue protocol), transport
 * independent.  C89-compatible, no libc, no heap, caller buffers, not reentrant.  It talks to the network only through struct sbrw_ops
 * (the WebSocket core .[ch] in the product, an in-memory fake in the tests). Every constant lives in sbr_ws_const.h.
 *
 * Mapping to the bridge (menus unchanged): sbrw_state returns the same SBR_* values as sbr_state (IDLE, INIT = connecting/joining,
 * MM = queued, OPP_CONN = matched, CONNECTED, ERROR), sbrw_err the same SBR_E_* codes, sbrw_err_text the plain text (hand it to
 * sbr_fail, which adds the R3 notice and cleans characters), sbrw_match the same struct sbr_match.
 * Secrets (R7): the play key is read only by the join writer (and the identity check); the transmit buffer is wiped after every send and
 * on every terminal state; nothing in struct sbrw_status or any text contains it.  Modes: only SBR_MODE_UNRANKED / SBR_MODE_DIRECT.
 */
#ifndef SBR_WS_H
#define SBR_WS_H

#include "sbr.h"

#define SBRW_IO_CLOSED (-1)          /* recv_text: connection closed or failed */
#define SBRW_IO_OVERSIZE (-2)        /* recv_text: a message larger than cap was received and discarded */
#define SBRW_CONN_UP 0               /* connect: established (TCP, TLS and the WebSocket upgrade are done) */
#define SBRW_CONN_TCP 1              /* connect: still in the TCP connect phase (limit SBRW_CONNECT_MS) */
#define SBRW_CONN_SHAKE 2            /* connect: TCP is up, TLS + upgrade running (limit SBRW_HANDSHAKE_MS) */
                                     /* connect < 0: failed */

struct sbrw_ops {
    /* Called once to start an attempt (after close()) and again on every tick while it returns > 0.  Returns SBRW_CONN_*, or < 0. */
    int (*connect)(void *ctx);
    int (*send_text)(void *ctx, const sbr_u8 *buf, sbr_u32 n);    /* one text message; >= 0 ok, < 0 failed */
    int (*recv_text)(void *ctx, sbr_u8 *buf, sbr_u32 cap);        /* n > 0 one whole message, 0 none, SBRW_IO_CLOSED, SBRW_IO_OVERSIZE */
    void (*close)(void *ctx);                                     /* close without waiting (idempotent) */
    sbr_u32 (*now_ms)(void *ctx);
};

struct sbrw_cand { const char *type; const char *ip; sbr_u16 port; };    /* type: SBRW_CAND_*, ip dotted text */

struct sbrw_cfg {
    sbr_u32 lan_ip;              /* our DHCP address, host order (0 = ipAddressLan "") */
    sbr_u16 local_port;          /* the netplay layer's UDP port */
    sbr_u16 mapped_port;         /* STUN-mapped port, 0 = send local_port */
    sbr_u32 reticket_max;        /* fresh searches after a failed peer connection (bounded, R11) */
    sbr_u32 platform;            /* SBR_PLAT_* (informational, not sent) */
    char app_version[SBR_VER_LEN];   /* "" -> SBRW_APP_VERSION */
    const char *nat_type;        /* SBRW_NAT_* or 0 = omitted */
    struct sbrw_cand cand[SBRW_CANDS_MAX];
    sbr_u32 ncand;
    sbr_u8 *rx; sbr_u32 rx_cap;  /* caller buffers: >= SBRW_RX_MIN, >= SBRW_TX_MIN */
    sbr_u8 *tx; sbr_u32 tx_cap;
};

struct sbrw_status {             /* RAM words for DIAG / the harness: numbers only, never a secret */
    sbr_u32 magic;               /* 'SBW1' */
    sbr_u32 state, err, mode;
    sbr_u32 searches, joins, resumes, cancels, errors, assigned, retickets, peer_ok, peer_fail;
    sbr_u32 connects_ok, connects_fail, drops, pings;
    sbr_u32 rx_msgs, rx_bad, rx_ignored, rx_oversize;
    sbr_u32 queue_s;
};

struct sbrw {
    struct sbrw_cfg cfg;
    const struct sbrw_ops *ops;
    void *ctx;
    struct sbrw_status st;
    const struct sbr_ident *id;
    int id_ok, net_up, outdated;
    char app_version[SBR_VER_LEN];
    char latest[SBR_VER_LEN];            /* latestVersion of the last error ("" if none) */
    int ph, vis, err, resuming, conn_open, token_ok, next_ph;
    sbr_u32 now, mode, code_len, attempts, reticket;
    sbr_u32 t_attempt, t_shake, t_join, t_rx, t_lost, t_wait, wait_ms;
    int shake_seen, ping_sent;
    sbr_u8 code_sjis[18];
    char code[SBR_CODE_MAX];
    char err_text[SBR_ERR_MAX];
    char ticket[SBRW_TICKET_MAX], token[SBRW_TOKEN_MAX];
    struct sbr_match match;
    struct sbr_reply reply;
};

void sbrw_init(struct sbrw *W, const struct sbrw_cfg *cfg, const struct sbrw_ops *ops, void *ctx);
void sbrw_identity(struct sbrw *W, const struct sbr_ident *id);
void sbrw_set_net(struct sbrw *W, int up, sbr_u32 lan_ip);
/* 0 = search started; < 0 = -SBR_E_* (state ERROR with text, except SBR_E_BUSY which leaves it alone).  Same checks as sbr_search. */
int sbrw_search(struct sbrw *W, sbr_u32 mode, const char *code);
void sbrw_cancel(struct sbrw *W);               /* sends cancel when queued, closes, back to IDLE from any state */
void sbrw_tick(struct sbrw *W);                 /* the clock is ops->now_ms */
void sbrw_peer_result(struct sbrw *W, int ok);  /* OPP_CONN only: ok -> CONNECTED; fail -> a fresh search while reticket < reticket_max, else ERROR */
int sbrw_state(const struct sbrw *W);           /* SBR_IDLE .. SBR_ERROR */
int sbrw_err(const struct sbrw *W);             /* SBR_E_* */
const char *sbrw_err_text(const struct sbrw *W);    /* plain text, "" when none */
const struct sbr_match *sbrw_match(const struct sbrw *W);   /* valid in OPP_CONN / CONNECTED */
sbr_u32 sbrw_queue_s(const struct sbrw *W);

/* exported for the host tests: message writers into out (bytes written or < 0 on overflow) */
int sbrw_write_join(const struct sbrw *W, sbr_u8 *out, sbr_u32 cap);
int sbrw_write_resume(const char *ticket, const char *token, sbr_u8 *out, sbr_u32 cap);
int sbrw_write_simple(const char *type, sbr_u8 *out, sbr_u32 cap);   /* {"type":"cancel"} / {"type":"ping"} */

#endif
