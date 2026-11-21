/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REP_H
#define REP_H
/* rep: the console's Slippi game-report client ( and 9).  After a bridge game (Slippi Unranked; Direct only
   behind REP_REPORT_DIRECT) the engine's 369-byte report buffer is turned into a small queue entry (rep_enqueue); in a MENU scene, with the
   server bit ON, the entry is sent as a `reportOnlineGame` GraphQL mutation to internal.slippi.gg over TLS 1.3 (sdsec_open_web: the public
   web roots of sdroots.c, host name checked, dates checked against OUR server's signed time).  Failure is silent (counters only).
   Everything the client touches is behind the `rep_env` callbacks, so the same code runs in the overlay and in the host tests against a local mock.
   Strict C89, no heap, no float, no libc.  The queue holds NO secrets: uid and play key are read from the card at send time (env->ident) and
   written straight into the request buffer, which is wiped when the attempt ends.
   Build with -DSDTLS_WEBPKI -DSDNET_ENABLE_TCP=1 and link sdsec, sdtls, sdtcp, sdhttp, sdpk, sdx509, sdroots, sdcrypto, sdc25519. */
#include "sdsec.h"
#include "sdhttp.h"
#include "rep_keep.h"

struct rep_env {
    void *ctx;
    rep_u32 (*ms)(void *ctx);                           /* monotonic milliseconds (the sdnet clock) */
    rep_u32 (*unix_time)(void *ctx);                    /* TRUSTED Unix time, 0 = none yet (rep_clock) */
    int (*rand)(void *ctx, rep_u8 *out, rep_u32 n);     /* 0 ok, nonzero = entropy not ready (sdtls_rand_fn) */
    int (*dns)(void *ctx, const char *host, rep_u32 *ip, rep_u32 now);   /* 1 = *ip set, 0 = still working, < 0 = failed */
    int (*entropy_ready)(void *ctx);                    /* 1 when the pool can serve a session (SDENT_L_SESSION credited bits); NULL = always ready (host tests) */
    int (*scene_ok)(void *ctx);                         /* 1 only in a menu scene with no match, no rollback session and no search: the TCP stack is RUN (never QUIESCE / PAUSED) */
    int (*ident)(void *ctx, char *uid, rep_u32 uidcap, char *key, rep_u32 keycap);   /* the Slippi uid and play key from the card, NUL terminated; 1 = ok.  Called per attempt; never stored by rep */
    int (*iso_hash)(void *ctx, char *out33);            /* 32 lower-case hex characters + NUL from the card file; 1 = ok, 0 = none (then ""); may be NULL */
    void *(*arena_get)(void *ctx, rep_u32 size);        /* a 4-aligned block for the session; NULL = no memory now */
    void (*arena_put)(void *ctx, void *p);
    struct sdtcp *tcp;
    const struct sdx_anchor *anchors; rep_u32 nanchors; /* sdx_roots / sdx_nroots in the product; a test root in the host tests */
    rep_u32 build;                                      /* our build number for the User-Agent */
    const char *platform;                               /* "console" (the build cannot tell GameCube, Wii, Wii U, Dolphin apart) */
};

#define REP_IDLE 0
#define REP_DNS 1
#define REP_OPENING 2
#define REP_SEND 3
#define REP_RECV 4
#define REP_CLOSING 5

struct rep_arena {                  /* allocated while a session lives: about 27 KB */
    struct sdsec sec;
    struct sdtls_web web;
    rep_u8 rx[SDTLS_WEB_RX];
    rep_u8 req[REP_REQ_MAX];
    rep_u8 body[REP_RSP_MAX];
};

struct rep {
    struct rep_env e;
    struct rep_keep *k;
    struct rep_arena *a;
    struct sdhttp_rsp hr;
    rep_u8 state, reused;
    rep_u32 ip, t_state, req_len, req_sent, body_len, t_attempt_ms;
    int body_overflow;
};

void rep_init(struct rep *r, const struct rep_env *env, struct rep_keep *keep);
/* One call per menu frame, AFTER sdnet_poll.  allow_heavy: a crypto step of up to 70 ms may run on this frame (sdsec_poll). */
void rep_poll(struct rep *r, int allow_heavy);
/* Forget everything in flight and in the queue (account change, flag turned off). */
void rep_flush(struct rep *r);
/* Tear the session down (RST, arena returned, key wiped) but keep the queue: a match is about to start. */
void rep_abort(struct rep *r);

/* 1 while a session exists or is being set up (the resident keeps the overlay loaded) */
int rep_busy(const struct rep *r);

/* ---- pieces exposed for the tests ---- */
rep_u32 rep_build_request(rep_u8 *out, rep_u32 cap, const struct rep_rec *rec, const char *uid, const char *key, const char *iso_hash,
                          rep_u32 build, const char *platform);                       /* HTTP request bytes (head + JSON body), 0 if too small or a field is not plain */
/* Parse the GraphQL answer: returns 1 = success true, 0 = success false or missing, -1 = "errors" present or not JSON.  *upload = 1 if uploadUrl is a non-null string. */
int rep_parse_answer(const rep_u8 *p, rep_u32 n, int *upload);

/* DNS through sddns (the overlay's resolver; the host tests use the real one over the simulated link) */
#ifndef REP_NO_SDDNS
struct sddns;
struct rep_dns { struct sddns *d; rep_u32 server, port; rep_u8 busy; char host[64]; };     /* port: 0 = leave the resolver's port as it is; the matchmaker driver sets 53 or a lab port */     /* server: the DHCP-provided resolver to set before each lookup (0 = leave the resolver as it is) */
int rep_dns_step(struct rep_dns *s, const char *host, rep_u32 *ip, rep_u32 now);    /* the rep_env.dns body: ctx = struct rep_dns */
#endif
#endif
