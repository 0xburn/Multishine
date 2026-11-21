/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MS_WS_H
#define MS_WS_H
/*
 * ms_ws: RFC 6455 WebSocket CLIENT core.
 * Strict C89, no libc (not even memcpy), NO heap: the caller supplies one transmit buffer and one receive buffer.  Transport agnostic: the
 * caller gives two callbacks (send / recv) with the semantics of sdsec_write / sdsec_read (offline/net/tls/sdsec.h), so the same core runs over
 * the engine's TLS stream and over a test transport.  Poll driven and non-blocking; nothing here waits.
 *
 * What it does: the HTTP/1.1 upgrade request, the 101 response with Sec-WebSocket-Accept verified (SHA-1 + base64 inside), text and binary
 * frames out (client masked, the 4 mask bytes come from the caller), unfragmented text / binary / ping / pong / close frames in, ping answered with
 * pong (same payload), close handshake (the echo is queued).
 * What it refuses (error, state FAILED, the caller closes the transport): fragmentation (FIN = 0 or a continuation frame), reserved bits,
 * a masked server frame, reserved opcodes, a control frame above 125 bytes, a non-minimal length, a frame whose payload does not fit the receive
 * buffer (MS_WS_E_TOOBIG), a response that is not "101", lacks Upgrade / Connection tokens, carries a wrong Sec-WebSocket-Accept or any
 * Sec-WebSocket-Extensions / Sec-WebSocket-Protocol header (we offered none).  Text is NOT checked for UTF-8 (the JSON layer above rejects garbage).
 */
typedef unsigned char ws_u8;
typedef unsigned int ws_u32;

#define MS_WS_HDR_MAX 14u               /* 2 + 8 (length) + 4 (mask): the largest header we build */
#define MS_WS_RX_HDR 10u                /* the largest server header (no mask) */
#define MS_WS_CTRL_MAX 125u

/* states */
enum { MS_WS_IDLE = 0, MS_WS_HS_SEND, MS_WS_HS_RECV, MS_WS_OPEN, MS_WS_CLOSING, MS_WS_CLOSED, MS_WS_FAILED };
/* poll results: >= 0 events, < 0 errors */
enum { MS_WS_EV_NONE = 0, MS_WS_EV_OPEN, MS_WS_EV_TEXT, MS_WS_EV_BINARY, MS_WS_EV_PONG, MS_WS_EV_CLOSE };
enum {
    MS_WS_OK = 0, MS_WS_BUSY = -1,      /* BUSY: an earlier frame is still being sent, try again later */
    MS_WS_E_IO = -2,                    /* the transport failed or closed */
    MS_WS_E_HS = -3,                    /* the upgrade answer is not a valid 101 (status, missing header, too long) */
    MS_WS_E_ACCEPT = -4,                /* Sec-WebSocket-Accept is wrong or missing */
    MS_WS_E_PROTO = -5,                 /* a malformed or unsupported frame (see above) */
    MS_WS_E_FRAG = -6,                  /* fragmentation refused */
    MS_WS_E_TOOBIG = -7,                /* a frame above the buffer (in), or a message above the transmit buffer (out) */
    MS_WS_E_STATE = -8,                 /* call not allowed in this state */
    MS_WS_E_ARG = -9
};

struct ms_ws_io {
    int (*send)(void *ctx, const ws_u8 *buf, ws_u32 n);    /* bytes accepted (<= n), 0 = busy (try again), < 0 = closed or failed */
    int (*recv)(void *ctx, ws_u8 *buf, ws_u32 cap);        /* bytes read, 0 = none yet, < 0 = closed or failed */
    void *ctx;
};

struct ms_ws {
    struct ms_ws_io io;
    ws_u8 *tx, *rx;
    ws_u32 txcap, rxcap;
    ws_u32 tx_len, tx_sent;             /* the frame (or the upgrade request) being sent */
    ws_u32 rx_have, consume;            /* bytes in rx; bytes of the last delivered frame to drop at the next poll */
    ws_u8 state, pong_due, close_due, close_sent, got_close;
    ws_u8 pong_len, close_pl;
    ws_u8 pong_buf[MS_WS_CTRL_MAX];
    ws_u8 close_buf[2];
    char accept[29];                    /* expected Sec-WebSocket-Accept */
    /* the last delivered message / frame (valid until the next ms_ws_poll call) */
    const ws_u8 *msg;
    ws_u32 msg_len;
    ws_u32 close_code;                  /* of the peer's close frame (1005 when none), valid after MS_WS_EV_CLOSE */
    int err;                            /* the error that failed the connection */
};

/* tx must hold at least the upgrade request (about 200 bytes + host + path) and the largest message + MS_WS_HDR_MAX; the largest frame the
   receiver accepts has rxcap - MS_WS_RX_HDR bytes of payload (and the 101 answer must fit rxcap). */
void ms_ws_init(struct ms_ws *w, const struct ms_ws_io *io, ws_u8 *tx, ws_u32 txcap, ws_u8 *rx, ws_u32 rxcap);
/* Build the upgrade request (GET path, Host, Upgrade, Connection, Sec-WebSocket-Key from the 16 caller bytes, Version 13) and start sending it.  0 or < 0. */
int ms_ws_start(struct ms_ws *w, const char *host, const char *path, const ws_u8 key16[16]);
/* One pump: flush the transmit side, read at most one frame step.  `mask4` = fresh random bytes the core may use for a control frame it generates
   in this call (pong, close echo); the caller passes new bytes every call.  Returns an event or an error; after an event other than NONE the
   message (w->msg / w->msg_len) stays valid until the next call. */
int ms_ws_poll(struct ms_ws *w, const ws_u8 mask4[4]);
/* Queue one text / binary frame (state OPEN only).  0 = queued (poll flushes it), MS_WS_BUSY = the previous frame is not out yet. */
int ms_ws_send_text(struct ms_ws *w, const ws_u8 *msg, ws_u32 len, const ws_u8 mask4[4]);
int ms_ws_send_binary(struct ms_ws *w, const ws_u8 *msg, ws_u32 len, const ws_u8 mask4[4]);
/* Start the close handshake (no payload when code == 0).  Does not wait for the peer. */
int ms_ws_close(struct ms_ws *w, ws_u32 code, const ws_u8 mask4[4]);
/* 1 when nothing is waiting to be sent (idle transmit side) */
int ms_ws_tx_idle(const struct ms_ws *w);

/* exported for the tests: Sec-WebSocket-Accept for a 24 character key, NUL terminated into out[29] */
void ms_ws_accept_of(const char key24[24], char out29[29]);

#endif
