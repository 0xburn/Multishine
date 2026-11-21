/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sgc: commands from the website (server half: the server's ). The console asks for its
 * account state now and then (ACCT_STATE 0x40); flag bit 5 says a command waits; CMD_GET 0x53 returns it (kind 1 = clear the Slippi login) and CMD_DONE 0x54 confirms it
 * (1 done, 0 failed: the command stays and the console backs off).  Pure C89, no libc, no heap, no clock (the glue passes `now` in ms), no Melee address: the transport and the
 * clear come through `struct sgc_host`, so the same file runs in the overlay and in the host tests (tests/sgc_test.c with a fake mailbox, tests/test_sgc_wire.py against the real
 * server).  Nothing here is logged; a command carries no secret.
 *
 *   IDLE  wait for the next poll (SGC_FIRST_MS after the screen opened, then SGC_POLL_MS; a failure backs off up to SGC_MAX_BACKOFF_MS)
 *   ASK   ACCT_STATE in flight         GET   CMD_GET in flight         DONE  CMD_DONE in flight (an owed confirmation is sent before anything else is asked)
 * `allowed` (the glue) is 0 while anything else uses the network or the card: a running op is dropped (the owed confirmation survives) and nothing starts.
 */
#ifndef SGC_H
#define SGC_H

#define SGC_OP_STATE 0x40u
#define SGC_OP_GET 0x53u
#define SGC_OP_DONE 0x54u
#define SGC_FLAG_PENDING 32u            /* ACCT_STATE flags bit 5 */
#define SGC_KIND_CLEAR_SLIPPI 1u
#define SGC_KIND_WIPE 2u                /* a console REMOVED from its account erases its data; a build that only knows kind 1 leaves it alone */
#define SGC_ST_TOKEN_REVOKED 26         /* the server's status for a removed console: ACCT_STATE answers it, CMD_GET / CMD_DONE are still answered for a pending wipe */
#define SGC_ID_RETRY_MS 2000u           /* the identity record is busy: try again */
#define SGC_ID_TRIES 8u
#define SGC_FIRST_MS 4000u
#define SGC_POLL_MS 60000u
#define SGC_MIN_BACKOFF_MS 30000u
#define SGC_MAX_BACKOFF_MS 600000u
#define SGC_MSG_MS 10000u               /* the status line stays this long */
#define SGC_MSG_CLEARED "Slippi login removed by the website."
#define SGC_MSG_WIPED "This console was removed and its data erased."          /* 49 characters: one hint row (SG_COLS 52) */

struct sgc_host {
    void *ctx;
    int (*net_up)(void *ctx);
    int (*net_begin)(void *ctx, unsigned op, const unsigned char *req, unsigned n, unsigned now);    /* 0 = sent */
    int (*net_poll)(void *ctx, unsigned now, const unsigned char **reply, unsigned *n, int *status);   /* 0 busy, 1 done, -1 failed (*status: the server's status or 0) */
    void (*net_abort)(void *ctx);
    int (*clear)(void *ctx);            /* delete the Slippi login from the card (the Slippi part of Delete Identity only): 0 = gone (or was not there), else failed */
    int (*wipe_data)(void *ctx);        /* kind 2, step 1: the Slippi login, the code history, the settings and the stored seed gone from the card, each read back: 0 = all gone, else failed (idempotent) */
    int (*wipe_identity)(void *ctx);    /* kind 2, step 2, only after the server confirmed step 1: the identity record becomes the guest placeholder and the RAM forgets it: 0 done, 1 busy (call again), else failed; 0 = not built */
};

enum { SGC_IDLE = 0, SGC_ASK, SGC_GET, SGC_DONE, SGC_WIPEID };

struct sgc {
    const struct sgc_host *h;
    unsigned char st, owe, res, started;     /* owe: a CMD_DONE is still to be sent (res 1 done, 0 failed) for cmd id */
    unsigned id, next_at, backoff, msg_until;
    unsigned char msg;                       /* the status line is up: 1 Slippi login removed, 2 console wiped */
    unsigned char cleared, failed, polls;    /* counters (host tests, RAM proof) */
    unsigned char kind, revoked, idtries, wiped;     /* kind of the command in hand; revoked: the server called this device removed (ACCT_STATE answers 26: go straight to CMD_GET); identity retries; wipes finished */
};

void sgc_init(struct sgc *s, const struct sgc_host *h);
void sgc_frame(struct sgc *s, unsigned now, int allowed);    /* once per rendered frame */
const char *sgc_status(struct sgc *s, unsigned now);         /* the status line, or 0 */
#endif
