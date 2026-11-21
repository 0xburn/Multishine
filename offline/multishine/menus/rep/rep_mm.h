/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REP_MM_H
#define REP_MM_H
/*
 * rep_mm: the Slippi 3.7.0 MATCHMAKER client of ms/rep.ovl.  It serves the resident bridge through the mailbox (offline/bridge/sbr_mbx.h):
 * on a SEARCH command it copies the identity for the join (R7: the play key lives only in this block's `struct sbr_ident`, wiped when the matchmaker connection ends), runs the STUN
 * step (sbr_stun, same UDP socket as the netplay layer), resolves the matchmaker name, then drives the client logic sbr_ws.c over a WebSocket (ms_ws.c) over a TLS stream, and
 * publishes state / error / queue seconds / the match record in the mailbox.  Everything it touches outside is behind `struct rep_mm_env`: in the overlay it is bound to sdsec,
 * the shared resolver and the netplay socket (rep_ovl.c); in the host tests to plain sockets.  C89, no libc, no heap; the caller gives the buffers.
 */
#include "sbr.h"
#include "sbr_ws.h"
#include "sbr_stun.h"
#include "ms_ws.h"

#define RM_TP_IDLE 0         /* tp_poll: no session */
#define RM_TP_TCP 1          /* connecting */
#define RM_TP_TLS 2          /* TCP is up, handshake running */
#define RM_TP_UP 3           /* secure stream usable */
#define RM_TP_FAILED 4

#define RM_STUN_BUDGET_MS 4000u      /* the whole STUN step (the stock client asks up to 4 servers x 3 tries x 500 ms; we bound it, a blocked network must not delay the search) */
#define RM_STUN_NSERV 4
#define RM_HEAVY_MASK 1u             /* a TLS crypto step (up to ~70 ms) may run on every second frame while connecting */
#define RM_WSRX 4112u                /* ms_ws receive buffer: a frame of up to 4102 bytes (a match is about 1.6 KB) */
#define RM_WSTX 1100u                /* ms_ws transmit buffer: the upgrade request and the largest message (join, SBRW_TX_MIN 1024) + header */
#define RM_JTX 1100u                 /* sbr_ws message buffer */

struct rep_mm_env {
    void *ctx;
    sbr_u32 (*ms)(void *ctx);
    int (*ready)(void *ctx);                                                /* 1 when the entropy pool can serve a session; NULL = always (host tests) */
    int (*rand)(void *ctx, unsigned char *out, sbr_u32 n);                  /* 0 ok, nonzero = entropy not ready (the frame is skipped) */
    int (*dns)(void *ctx, const char *host, sbr_u32 *ip, sbr_u32 now);      /* 1 = *ip, 0 = working, < 0 failed; host NULL = abort our lookup */
    void (*dns_cfg)(void *ctx, sbr_u32 server, sbr_u32 port);               /* lab override of the resolver (0 = DHCP resolver / 53), set before each search's lookups */
    int (*ident)(void *ctx, struct sbr_ident *out);                         /* 1 = the card identity (uid, play key, code, name) copied for this search */
    int (*tp_open)(void *ctx, sbr_u32 ip, unsigned short port, const char *host);   /* start TCP + TLS (host checked, SNI); 0 started, < 0 cannot */
    int (*tp_poll)(void *ctx, int heavy);                                   /* RM_TP_* */
    int (*tp_write)(void *ctx, const unsigned char *b, sbr_u32 n);          /* sdsec_write: bytes accepted, 0 busy, < 0 closed */
    int (*tp_read)(void *ctx, unsigned char *b, sbr_u32 cap);               /* sdsec_read: bytes, 0 none, < 0 closed */
    void (*tp_close)(void *ctx);                                            /* graceful when UP, abort otherwise; idempotent */
    int (*tp_busy)(void *ctx);                                              /* 1 while a previous session is not completely gone (a new tp_open must wait) */
    int (*udp_send)(void *ctx, sbr_u32 ip, unsigned short port, const unsigned char *b, sbr_u32 n);
    int (*udp_recv)(void *ctx, unsigned char *b, sbr_u32 cap, sbr_u32 *ip, unsigned short *port);   /* n >= 0 a datagram, < 0 none */
    struct sbr_mbx *mbx;
    unsigned char *wsrx, *wstx, *jtx;                                       /* RM_WSRX, RM_WSTX, RM_JTX bytes, owned by the caller */
};

enum { RM_IDLE = 0, RM_STUN, RM_DNS, RM_RUN, RM_DONE, RM_ENT };       /* RM_ENT: command taken, waiting for the entropy pool (no network yet) */
#define RM_ENT_WAIT_MS 25000u

struct rep_mm {
    struct rep_mm_env e;
    struct sbrw W;
    struct sbrw_ops ops;
    struct ms_ws ws;
    struct ms_ws_io wio;
    struct sbr_ident id;
    struct sbrw_status prev;             /* counters already added to the mailbox */
    int phase, tp_started, ws_started, ws_open, id_ok, heavy, si, tries, have[2], nresp, sent_any, mm_resolved, stun_dns;
    sbr_u32 seen, now, frames, t_phase, t_stun, t_try, t_ka, mm_ip, mm_port;
    sbr_u32 srv_ip[RM_STUN_NSERV], ka_ip;
    unsigned short ka_port;
    struct sbr_stun_addr ans[2];
    sbrs_u8 tid[SBR_STUN_TID_LEN], req[SBR_STUN_REQ_LEN];
    char host[SBR_MBX_HOST_MAX], cand_ip[2][16];
    char code[SBR_CODE_MAX];
    sbr_u32 mode, lan_ip, local_port;
};

void rep_mm_init(struct rep_mm *m, const struct rep_mm_env *env);
void rep_mm_frame(struct rep_mm *m);            /* once per menu frame, inside the network window, after the net poll */
void rep_mm_leave(struct rep_mm *m);            /* the block goes away: close, wipe the identity copy and every buffer */
int rep_mm_busy(const struct rep_mm *m);        /* 1 while a search is being served or a transport is still closing (the resident keeps the block) */
#endif
