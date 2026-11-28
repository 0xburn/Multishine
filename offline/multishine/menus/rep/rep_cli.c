/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_cli: queue + session state machine of the Slippi game-report client (2.5, 9).
   Rules enforced here, each with a counter in struct rep_stats:
   - THE ONLY GATE IS OUR SERVER (Slippi reporting is not a setting): nothing is queued and nothing is sent unless keep->server (the signed HELLO's
     capability bit 15, default off, per console on the server) says so.  The bit going away flushes the queue and tears the session down on the next poll.
   - MENUS ONLY: a session exists only while env->scene_ok says "menu, no search, no match, TCP in RUN mode".  The moment it turns 0 the session is aborted (RST)
     and the arena is returned; the report stays queued for later.
   - NO TRUSTED TIME, NO SESSION (the report waits).  UNTRUSTED / HOST / CERT / SIG errors: the report is dropped and the web is left alone for 10 minutes.
   - STOCK RETRY: at most 5 attempts per report; after an HTTP-level failure (status, success:false, GraphQL errors, timeout) wait n x 100 ms and retry on the
     same keep-alive session when possible; after a TCP/TLS-level failure open a new session no sooner than 30 s, 2 min, 10 min.  A report older than 30 minutes
     is dropped.  Failure is silent.
   - The play key never lives in the queue: env->ident is called per attempt into stack buffers, written into the request, and the request buffer is wiped as soon
     as it has been sent or the attempt fails. */
#include "rep.h"

#define REP_WAIT 6
#ifdef REP_TRACE                                    /* host debugging only: -DREP_TRACE, TR(("fmt", args)) */
#include <stdio.h>
#define TR(x) printf x
#else
#define TR(x)
#endif

static void zero(void *p, rep_u32 n) { volatile rep_u8 *b = (volatile rep_u8 *)p; rep_u32 i; for (i = 0; i < n; i++) b[i] = 0; }   /* volatile: a wipe of a dead stack buffer must not be optimised away (review L4) */
static int before(rep_u32 now, rep_u32 t) { return (int)(now - t) < 0; }

void rep_init(struct rep *r, const struct rep_env *env, struct rep_keep *keep)
{
    zero(r, sizeof *r);
    r->e = *env;
    r->k = keep;
    r->k->cur = -1;
}

/* ------------------------------------------------------------------ session handling */
static void dns_abort(struct rep *r) { rep_u32 ip; if (r->e.dns) (void)r->e.dns(r->e.ctx, 0, &ip, 0); }

static void teardown(struct rep *r)
{
    if (r->a) {
        struct sdsec *s = &r->a->sec;
        if (s->state == SDSEC_TCP || s->state == SDSEC_TLS || s->state == SDSEC_UP || s->state == SDSEC_CLOSING) sdtcp_abort(r->e.tcp, s->h);
        r->k->st.heavy_skipped += s->heavy_skipped;
        zero(r->a->req, sizeof r->a->req);                  /* the play key was in here */
        zero(s, sizeof *s);                                 /* the TLS traffic keys live in the session (review L5) */
        r->e.arena_put(r->e.ctx, r->a);
        r->a = 0;
    }
    if (r->state == REP_DNS) dns_abort(r);
    r->state = REP_IDLE; r->k->cur = -1;
    r->hr.state = SDHTTP_HEAD_STATUS;
}
static void drop_cur(struct rep *r)
{
    if (r->k->cur >= 0) zero(&r->k->q[r->k->cur], sizeof r->k->q[0]);
    r->k->cur = -1;
}
static void flush_all(struct rep *r, int count_off)
{
    int i;
    for (i = 0; i < REP_QUEUE; i++) if (r->k->q[i].used) { if (count_off) r->k->st.dropped_off++; zero(&r->k->q[i], sizeof r->k->q[0]); }
    teardown(r);
}
int rep_busy(const struct rep *r) { return r->a != 0 || r->state != REP_IDLE; }
void rep_abort(struct rep *r) { if (r->a || r->state != REP_IDLE) r->k->st.scene_aborts++; teardown(r); }
void rep_flush(struct rep *r) { flush_all(r, 0); r->k->next_ms = 0; r->k->t_cooldown_until = 0; r->k->fails_tls = 0; }

/* a failure of the transport (DNS, TCP, TLS, reset): count the attempt, back off 30 s / 2 min / 10 min, new session later */
static void transport_fail(struct rep *r, rep_u32 now)
{
    struct rep_rec *q = r->k->cur >= 0 ? &r->k->q[r->k->cur] : 0;
    rep_u32 wait = r->k->fails_tls == 0 ? REP_RECONNECT_MS_1 : r->k->fails_tls == 1 ? REP_RECONNECT_MS_2 : REP_RECONNECT_MS_3;
    if (r->a) { r->k->st.last_tls_err = r->a->sec.tls_err; r->k->st.last_sec_err = (rep_u8)r->a->sec.err; r->k->st.last_tcp_err = (rep_u8)(r->a->sec.tcp_err < 0 ? -r->a->sec.tcp_err : r->a->sec.tcp_err); }
    if (r->k->fails_tls < 250) r->k->fails_tls++;
    if (q) {
        if (q->attempts == 0) q->attempts = 1;            /* a failed connection attempt is an attempt */
        else if (q->attempts < 250) q->attempts++;
        if (q->attempts >= REP_MAX_ATTEMPTS) { r->k->st.dropped_max++; drop_cur(r); }
    }
    teardown(r);
    r->k->next_ms = now + wait;
}
/* the first ~20 characters of what the server said, for the lab line: the first "message":"..." of the body when there is one, else the body's own first characters; every character outside the
   menu font's safe set (letters, numerals, space. , ' - :) becomes a dot, a quote or backslash ends a message; at most 20 */
static void note_answer(struct rep *r)
{
    const rep_u8 *b = r->a->body;
    rep_u32 n = r->body_len, i, j = 0, at = 0;
    static const char key[] = "\"message\":\"";
    r->k->st.last_http = (rep_u16)r->hr.status;
    for (i = 0; i + sizeof key - 1 <= n && !at; i++) {
        rep_u32 k = 0;
        while (k < sizeof key - 1 && b[i + k] == (rep_u8)key[k]) k++;
        if (k == sizeof key - 1) at = i + k;
    }
    for (i = at; i < n && j < 20; i++) {
        rep_u8 c = b[i];
        if (at && (c == '"' || c == '\\')) break;
        if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        r->k->st.last_msg[j++] = (char)((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == ',' || c == '\'' || c == '-' || c == ':' ? c : '.');
    }
    r->k->st.last_msg[j] = 0;
}

/* a failure after a complete exchange (status, success:false, errors, timeout): the stock n x 100 ms wait, same session if it is still good */
static void http_fail(struct rep *r, rep_u32 now, int keep)
{
    struct rep_rec *q = &r->k->q[r->k->cur];
    if (q->attempts >= REP_MAX_ATTEMPTS) {
        r->k->st.dropped_max++;
        drop_cur(r);
        if (r->a) { sdsec_close(&r->a->sec); r->state = REP_CLOSING; r->t_state = now; r->k->cur = -1; } else teardown(r);
        return;
    }
    r->k->next_ms = now + (rep_u32)q->attempts * REP_RETRY_STEP_MS;
    if (keep && r->a && r->a->sec.state == SDSEC_UP && !r->hr.close) { r->state = REP_WAIT; return; }
    teardown(r);
    /* teardown cleared cur; the report stays queued (attempts kept) */
}

static int oldest(const struct rep *r)
{
    int i, best = -1;
    for (i = 0; i < REP_QUEUE; i++) if (r->k->q[i].used && (best < 0 || (int)(r->k->q[best].t_enq - r->k->q[i].t_enq) > 0)) best = i;
    return best;
}

static int send_request(struct rep *r)
{
    char uid[REP_ID_MAX + 2], key[REP_KEY_MAX + 2], iso[40];
    const char *isop = "";
    rep_u32 n;
    zero(uid, sizeof uid); zero(key, sizeof key); iso[0] = 0;
    if (!r->e.ident || !r->e.ident(r->e.ctx, uid, sizeof uid - 1, key, sizeof key - 1)) { zero(uid, sizeof uid); zero(key, sizeof key); r->k->st.no_ident++; return 0; }
#if REP_ISOHASH_FROM_CARD
    if (r->e.iso_hash && r->e.iso_hash(r->e.ctx, iso)) isop = iso;
#endif
    n = rep_build_request(r->a->req, sizeof r->a->req, &r->k->q[r->k->cur], uid, key, isop, r->e.build, r->e.platform);
    zero(uid, sizeof uid); zero(key, sizeof key);
    if (!n) { r->k->st.bad++; return 0; }
    r->req_len = n; r->req_sent = 0;
    r->k->q[r->k->cur].attempts++;
    r->k->st.attempts++;
    return 1;
}

void rep_poll(struct rep *r, int allow_heavy)
{
    rep_u32 now = r->e.ms(r->e.ctx);
    int i, rc;
    if (!rep_pending(r->k) && !r->a) return;
    if (rep_pending(r->k) && !rep_enabled(r->k)) { flush_all(r, 1); return; }
    if (r->state != REP_CLOSING)
        for (i = 0; i < REP_QUEUE; i++)
            if (r->k->q[i].used && i != r->k->cur && (rep_u32)(now - r->k->q[i].t_enq) > REP_AGE_MAX_MS) { r->k->st.dropped_age++; zero(&r->k->q[i], sizeof r->k->q[0]); }
    if (r->k->cur >= 0 && r->k->q[r->k->cur].used && (rep_u32)(now - r->k->q[r->k->cur].t_enq) > REP_AGE_MAX_MS) { r->k->st.dropped_age++; drop_cur(r); teardown(r); return; }
    if (!r->e.scene_ok(r->e.ctx)) {
        if (r->a || r->state != REP_IDLE) { r->k->st.scene_aborts++; teardown(r); }
        r->k->st.held_scene++;
        return;
    }
    switch (r->state) {
    case REP_IDLE: {
        rep_u32 t;
        int k;
        if (!rep_pending(r->k)) return;
        if (before(now, r->k->next_ms)) return;
        if (before(now, r->k->t_cooldown_until)) { r->k->st.held_cooldown++; return; }
        t = r->e.unix_time(r->e.ctx);
        if (!t) { r->k->st.held_time++; return; }
        if (r->e.entropy_ready && !r->e.entropy_ready(r->e.ctx)) { r->k->st.held_ent++; return; }   /* the pool cannot serve a session yet (the player has not moved the stick; no stored seed): wait, a failed handshake would cost a 30 s back-off */
        k = oldest(r);
        if (k < 0) return;
        r->k->cur = k;
        r->state = REP_DNS;
        r->t_state = now;
        break;
    }
    case REP_DNS:
        rc = r->e.dns(r->e.ctx, REP_HOST, &r->ip, now);
        if (rc == 0) { if ((rep_u32)(now - r->t_state) > 15000u) { r->k->st.dns_fail++; transport_fail(r, now); } return; }
        if (rc < 0) { r->k->st.dns_fail++; transport_fail(r, now); return; }
        {
            rep_u32 t = r->e.unix_time(r->e.ctx);
            struct rep_arena *a;
            if (!t) { r->k->st.held_time++; r->state = REP_IDLE; r->k->cur = -1; return; }
            a = (struct rep_arena *)r->e.arena_get(r->e.ctx, (rep_u32)sizeof(struct rep_arena));
            if (!a) { r->k->st.no_mem++; r->state = REP_IDLE; r->k->cur = -1; r->k->next_ms = now + 1000u; return; }
            zero(a, sizeof *a);
            r->a = a;
            a->web.anchors = r->e.anchors; a->web.nanchors = r->e.nanchors; a->web.now = t; a->web.flags = 0;
            a->web.rx = a->rx; a->web.rx_cap = sizeof a->rx;
            rc = sdsec_open_web(&a->sec, r->e.tcp, r->ip, REP_PORT, (sdtls_rand_fn)r->e.rand, r->e.ctx, &a->web, REP_HOST, REP_SESSION_TIMEOUT_MS);
            r->k->st.sessions++;
            if (rc < 0) { r->k->st.tcp_fail++; transport_fail(r, now); return; }
            r->state = REP_OPENING; r->t_state = now;
        }
        break;
    case REP_OPENING:
        rc = sdsec_poll(&r->a->sec, allow_heavy);
        if (rc == SDSEC_FAILED) {
            struct sdsec *s = &r->a->sec;
            r->k->st.last_cert_err = (rep_u8)(r->a->web.cert_err < 0 ? -r->a->web.cert_err : r->a->web.cert_err);
            if (s->err == SDSEC_E_TLS && s->tls_err == SDTLS_E_TIME) {            /* our clock and their certificate disagree: wait, do not hammer */
                r->k->st.tls_fail++; r->k->st.held_time++;
                r->k->st.last_tls_err = (rep_u8)s->tls_err;
                transport_fail(r, now);                                           /* counts the attempt (5 max) and backs off 30 s / 2 min / 10 min (review L2) */
                return;
            }
            if (s->err == SDSEC_E_TLS && (s->tls_err == SDTLS_E_UNTRUSTED || s->tls_err == SDTLS_E_HOST || s->tls_err == SDTLS_E_CERT || s->tls_err == SDTLS_E_SIG)) {
                r->k->st.tls_fail++; r->k->st.dropped_cert++;
                r->k->st.last_tls_err = (rep_u8)s->tls_err;
                drop_cur(r);
                teardown(r);
                r->k->t_cooldown_until = now + REP_WEB_COOLDOWN_MS;
                return;
            }
            if (s->err == SDSEC_E_TLS) r->k->st.tls_fail++; else r->k->st.tcp_fail++;
            transport_fail(r, now);
            return;
        }
        if (rc == SDSEC_UP) {
            if (!send_request(r)) { drop_cur(r); sdsec_close(&r->a->sec); r->state = REP_CLOSING; r->t_state = now; return; }
            r->reused = 0;
            r->state = REP_SEND;
        }
        break;
    case REP_WAIT:
        TR(("WAIT now=%u next=%u sec=%d\n", now, r->k->next_ms, r->a ? r->a->sec.state : -1));
        (void)sdsec_poll(&r->a->sec, allow_heavy);
        {
            rep_u8 one[1];
            int n = r->a->sec.state == SDSEC_UP ? sdsec_read(&r->a->sec, one, 1) : -1;
            if (n != 0) { teardown(r); return; }                          /* the server closed the idle session (or sent junk): next time a new one */
        }
        if (before(now, r->k->next_ms)) return;
        if (!send_request(r)) { drop_cur(r); sdsec_close(&r->a->sec); r->state = REP_CLOSING; r->t_state = now; return; }
        r->reused = 1;
        r->state = REP_SEND;
        break;
    case REP_SEND:
        (void)sdsec_poll(&r->a->sec, allow_heavy);
        if (r->a->sec.state != SDSEC_UP) { r->k->st.tls_fail++; transport_fail(r, now); return; }
        while (r->req_sent < r->req_len) {
            rep_u32 chunk = r->req_len - r->req_sent;
            if (chunk > 1000u) chunk = 1000u;
            rc = sdsec_write(&r->a->sec, r->a->req + r->req_sent, chunk);
            if (rc < 0) { r->k->st.tls_fail++; transport_fail(r, now); return; }
            if (rc == 0) break;
            r->req_sent += (rep_u32)rc;
        }
        if (r->req_sent >= r->req_len) {
            zero(r->a->req, sizeof r->a->req);                           /* wipe the play key now */
            sdhttp_rsp_init(&r->hr);
            r->body_len = 0; r->body_overflow = 0;
            r->t_attempt_ms = now;
            r->state = REP_RECV;
        }
        break;
    case REP_RECV: {
        rep_u8 tmp[256];
        int guard = 0;
        (void)sdsec_poll(&r->a->sec, allow_heavy);
        for (;;) {
            const rep_u8 *bd;
            rep_u32 bl, used = 0, k;
            int n;
            if (++guard > 24) break;
            n = sdsec_read(&r->a->sec, tmp, sizeof tmp);
            if (n == 0) break;
            if (n < 0) {                                                 /* closed or failed before the answer was complete */
                if (r->hr.state == SDHTTP_DONE) break;
                if (r->reused && r->hr.state == SDHTTP_HEAD_STATUS && r->hr.lpos == 0) { r->k->st.stale_retry++; http_fail(r, now, 0); return; }     /* the server had closed the idle keep-alive session: not a network failure, stock wait, new session */
                r->k->st.tls_fail++; transport_fail(r, now); return;
            }
            while (used < (rep_u32)n) {
                int u = sdhttp_feed(&r->hr, tmp + used, (rep_u32)n - used, &bd, &bl);
                if (u < 0) { r->k->st.parse_bad++; http_fail(r, now, 0); return; }
                for (k = 0; k < bl; k++) { if (r->body_len < REP_RSP_MAX) r->a->body[r->body_len++] = bd[k]; else r->body_overflow = 1; }
                used += (rep_u32)u;
                if (r->hr.state == SDHTTP_DONE || u == 0) break;
            }
            if (r->hr.state == SDHTTP_DONE) break;
        }
        if (r->hr.state == SDHTTP_DONE) {
            note_answer(r);
            /* LOCAL FORK: the answer is the HTTP status.  200 stored or already there; 4xx other than 429 can never succeed on a retry (bad or revoked key, refused report): dropped, and the host left alone */
            if (r->hr.status >= 400 && r->hr.status < 500 && r->hr.status != 429) {
                r->k->st.http_status_bad++;
                drop_cur(r); sdsec_close(&r->a->sec);
                r->k->t_cooldown_until = now + (r->hr.status == 401 || r->hr.status == 403 ? REP_WEB_COOLDOWN_MS : 2000u);
                r->state = REP_CLOSING; r->t_state = now;
                return;
            }
            if (r->hr.status != 200) { r->k->st.http_status_bad++; http_fail(r, now, 1); return; }
            r->k->st.ok++; r->k->st.last_msg[0] = 0;
            r->k->fails_tls = 0;
            drop_cur(r);
            sdsec_close(&r->a->sec);
            r->state = REP_CLOSING; r->t_state = now;
            return;
        }
        if ((rep_u32)(now - r->t_attempt_ms) > REP_REQ_TIMEOUT_MS) { r->k->st.rsp_timeout++; http_fail(r, now, 0); return; }
        break;
    }
    case REP_CLOSING:
        if (r->a) (void)sdsec_poll(&r->a->sec, allow_heavy);
        if (!r->a || r->a->sec.state == SDSEC_CLOSED || r->a->sec.state == SDSEC_FAILED || (rep_u32)(now - r->t_state) > 2000u) teardown(r);
        break;
    default:
        teardown(r);
        break;
    }
}
