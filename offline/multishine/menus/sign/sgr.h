/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sgr: "Send Slippi login" (console half).  The screen's state machine, the mailbox polling and the
 * identity record write.  Pure C89, no libc, no heap of its own (one block from host->alloc while a session runs), no clock (the glue passes `now` in ms), no Melee address:
 * everything outside comes through `struct sgr_host`, so the same file runs in the overlay and in the host test (tests/sgr_test.c with a fake mailbox and a fake card).
 *
 *   ENT    waiting for the entropy pool (SESSION level, the Link screen's pool)         OFFER  CRED_OFFER 0x50 (public key out; ttl and session code back)
 *   SHOW   QR + fingerprint + countdown; CRED_GET 0x51 every 3 s, chunks of 384          CONFIRM the opened login's name and connect code: A save, B decline
 *   SAVE   the card write (stage, read back, swap)                                       DONE / ERR / EXPIRED
 * CRED_DONE 0x52 (1 stored, 0 bad blob, 2 declined) is sent best effort after every end that follows a delivery, and on a cancel while waiting.
 * The play key lives only in the heap block (plaintext) and the card buffer; both are wiped.  Nothing here is ever logged or drawn except the display name and connect code.
 */
#ifndef SGR_H
#define SGR_H
#include "sgx.h"
#include "../../../qr/qr.h"

#define SGR_OP_OFFER 0x50u
#define SGR_OP_GET 0x51u
#define SGR_OP_DONE 0x52u
#define SGR_CHUNK 384u
#define SGR_POLL_MS 3000u
#define SGR_NET_FAILS 3u                /* consecutive transport failures while waiting before the screen gives up */
#define SGR_ID_NAME "MultishineIdentity"
#define SGR_ID_NEW "MultishineIdentityN"    /* staging */
#define SGR_ID_OLD "MultishineIdentityB"    /* the previous record while the swap runs */
#define SGR_BLOCK 0x2000u

/* the card: the layout of struct ota_card_ops (cardloader/ota/ota_core.h) so the glue can pass upc_ops through; 0 = success, create 1 = no room, 2 = exists */
struct sgr_card {
    void *ctx;
    int (*create)(void *ctx, const char *name, unsigned nblocks);
    int (*write)(void *ctx, const char *name, unsigned off, const unsigned char *buf, unsigned n);
    int (*read)(void *ctx, const char *name, unsigned off, unsigned char *buf, unsigned n);
    int (*del)(void *ctx, const char *name);
    int (*rename)(void *ctx, const char *from, const char *to);
    int (*exists)(void *ctx, const char *name);
};

struct sgr_host {
    void *ctx;
    const char *site;                                   /* host in the address, e.g. "multishine.link" */
    int (*rand)(void *ctx, unsigned char *out, unsigned n);                 /* 0 = filled from a pool that is ready at the SESSION level; nonzero = not yet */
    unsigned (*progress)(void *ctx);                    /* or 0: entropy pool percent for the bar */
    int (*net_up)(void *ctx);
    int (*net_begin)(void *ctx, unsigned op, const unsigned char *req, unsigned n, unsigned now);     /* in the hub client's device session (HELLO first when needed); 0 = sent */
    int (*net_poll)(void *ctx, unsigned now, const unsigned char **reply, unsigned *n, int *status);    /* 0 busy, 1 done (reply valid until the next begin), -1 failed: *status = the server's status or 0 */
    void (*net_abort)(void *ctx);
    int (*card_ready)(void *ctx);                       /* 1: a formatted card is in the slot and nothing else uses it (mounts nothing) */
    const struct sgr_card *card;
    unsigned char *(*card_buf)(void *ctx);              /* the 8 KiB block buffer, valid after the first card call until card_done (upc_block); 0 = none */
    void (*card_done)(void *ctx);
    void *(*alloc)(unsigned n);                         /* 0 when it cannot be given */
    void (*free)(void *p);
};

enum { SGR_OFF = 0, SGR_ENT, SGR_OFFER, SGR_SHOW, SGR_CONFIRM, SGR_SAVE, SGR_DONE, SGR_EXPIRED, SGR_ERR };
enum { SGR_E_NONE = 0, SGR_E_OFFLINE, SGR_E_UNREACH, SGR_E_OLD, SGR_E_LINK, SGR_E_RATE, SGR_E_BAD, SGR_E_MEM, SGR_E_NOCARD, SGR_E_NOROOM, SGR_E_CARD, SGR_E_SERVER };
enum { SGR_C_OK = 0, SGR_C_NOCARD, SGR_C_NOROOM, SGR_C_IO, SGR_C_VERIFY };
#define SGR_QR_ROWS 21u                 /* grid rows the code square covers on the SHOW screen (rows 1 to 21; the sprite is that many rows square) */
#define SGR_SETTLE 3u                   /* frames the SAVE screen is drawn before the (blocking) card write */

struct sgr {
    const struct sgr_host *h;
    unsigned char st, err, leave, op, busy, fails, settle, done_res;        /* op: the network op in flight (0 none); done_res: 0xFF none, else the CRED_DONE result still to send */
    unsigned char priv[32], pub[32];
    char code[SGX_CODE_LEN + 1], fp[SGX_FP_LEN];
    char url[SGX_URL_MAX];
    unsigned char qr[QR_OUT_BYTES], qr_n, qr_ok, saved, ent_pct;
    unsigned ttl, have, total, now, t0, next_poll;
    unsigned char *buf;                 /* heap: SGX_BLOB_MAX bytes of blob / plaintext, then a struct sgx_ident */
    struct sgx_ident *id;
    char name[SGX_NAME_LEN], cc[SGX_CC_LEN];     /* what the screen may show of the opened login */
};

void sgr_init(struct sgr *s, const struct sgr_host *h);
void sgr_enter(struct sgr *s, unsigned now);                     /* start a session (the screen was opened) */
void sgr_frame(struct sgr *s, unsigned edge, unsigned now);       /* one call per rendered frame; edge = newly pressed buttons (SG_B_*) */
void sgr_close(struct sgr *s);                                   /* leave: abort, wipe, free */
struct sg_view;
void sgr_draw(const struct sgr *s, struct sg_view *v);
/* the card write by itself (host-tested with cuts): installs `id` as the MultishineIdentity record keeping everything else of an existing file */
/* Recovery of an interrupted identity swap (see sgr.c); the glue runs it when the overlay first looks at the card and sgr_card_write runs it first.  1 put the old record back,
   0 nothing to do, -1 no card or card error. */
int sgr_recover(const struct sgr_host *h);
int sgr_card_write(const struct sgr_host *h, const struct sgx_ident *id);
#endif
