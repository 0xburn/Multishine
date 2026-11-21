/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CRED_H
#define CRED_H
/*
 * cred: the console's transport for the Slippi login relay: CRED_OFFER 0x50, CRED_GET 0x51, CRED_DONE 0x52, and for the website's commands (menus/sign/sgc.c) ACCT_STATE 0x40, CMD_GET 0x53, CMD_DONE 0x54, in the hub client's DEVICE
 * session (run mnc_hello first).  One generic request at a time: the caller (menus/sign/sgr.c) builds the request payload and parses the reply, so this file knows no field.
 * Same style as socc.c: C89, no libc, no heap, never blocks, a lost reply is asked again with the SAME sealed datagram (the server answers a repeat; CRED_GET and CRED_DONE are
 * idempotent, a repeated OFFER with the same key returns the same code).  The reply points into the client's receive buffer and is valid until the next cred_poll or mnc call.
 */
#include "mnc.h"
#define CRED_CAP 8192u              /* HELLO capability bit 13: the relay ops are served (only with the server's --slippi-relay) */
#define CRED_TRIES 5u
#define CRED_REQ_MAX 40u
enum { CRED_IDLE = 0, CRED_BUSY = 1, CRED_DONE_ = 2, CRED_FAILED = 3 };
#define CRED_E_NONE 0
#define CRED_E_BUSY 1
#define CRED_E_ARG 2
#define CRED_E_NOSESSION 3          /* no READY session, or the server forgot it: hello again */
#define CRED_E_TIMEOUT 4
#define CRED_E_SERVER 5             /* status != 0 */
typedef struct cred {
    mnc *c;
    int state, err, status;
    mnc_u8 op, tries;
    mnc_u32 timeout_ms, sent_ms, ctr, pkt_len, reply_n;
    const mnc_u8 *reply;
    mnc_u8 pkt[96];
} cred;
void cred_init(cred *s, mnc *c);
int cred_begin(cred *s, mnc_u8 op, const mnc_u8 *req, mnc_u32 n, mnc_u32 now);    /* 0 = sent */
int cred_poll(cred *s, mnc_u32 now);                                               /* -> CRED_BUSY / CRED_DONE_ / CRED_FAILED */
void cred_abort(cred *s);
#endif
