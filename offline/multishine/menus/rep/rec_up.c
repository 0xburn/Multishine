/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LOCAL FORK: see rec_up.h.  Strict C89 style, no libc, no heap. */
#include "rec_up.h"

typedef rep_u32 u32;
typedef rep_u8 u8;
enum { U_IDLE = 0, U_HASH, U_DNS, U_OPEN, U_HEAD, U_BODY, U_RECV, U_CLOSE };
static struct up_env E;
static struct up_arena *A;
static struct sdhttp_rsp HR;
static struct sdc_sha SH;
static int st;
static u32 ip, t0, pos, piece_end, buf_off, buf_n, hlen, hsent, blen, seq;
rep_u32 up_st[16];

static void zero(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }
static int before(u32 now, u32 t) { return (int)(now - t) < 0; }
static void state(int s, u32 now) { st = s; t0 = now; up_st[9] = (u32)s; }

void up_init(const struct up_env *e) { E = *e; A = 0; st = U_IDLE; }
int up_busy(void) { return st != U_IDLE; }
static void down(void)
{
    u32 i;
    if (st == U_DNS) (void)E.e->dns(E.e->ctx, 0, &i, 0);
    if (A) {
        struct sdsec *s = &A->sec;
        if (s->state == SDSEC_TCP || s->state == SDSEC_TLS || s->state == SDSEC_UP || s->state == SDSEC_CLOSING) sdtcp_abort(E.e->tcp, s->h);
        zero(A->head, sizeof A->head); zero(s, sizeof *s);
        E.put(A); A = 0;
    }
    st = U_IDLE; up_st[9] = 0;
}
void up_abort(void) { down(); }
static void fail(u32 now)                               /* transport: the next session later, the same recording, the confirmed bytes stay confirmed */
{
    struct rec_pub *r = E.pending();
    u32 f = E.k->fails_tls;
    up_st[7]++;
    if (A) { E.k->st.last_tls_err = (u8)A->sec.tls_err; E.k->st.last_sec_err = (u8)A->sec.err; }
    if (E.k->fails_tls < 250) E.k->fails_tls++;
    E.k->next_ms = now + (f == 0 ? 5000u : f == 1 ? REP_RECONNECT_MS_1 : REP_RECONNECT_MS_2);
    if (r && r->seq == seq && ++r->fails >= UP_FAILS_MAX) { up_st[10]++; E.forget(seq, 1); }
    down();
}
static u8 *o_s(u8 *p, u8 *e, const char *s) { while (*s && p < e) *p++ = (u8)*s++; return p; }
static u8 *o_u(u8 *p, u8 *e, u32 v) { char t[11]; int i = 10; t[10] = 0; do { t[--i] = (char)('0' + v % 10u); v /= 10u; } while (v); return o_s(p, e, t + i); }

static int build(const struct rec_pub *r, u32 now)
{
    char uid[REP_ID_MAX + 2], key[REP_KEY_MAX + 2];
    u8 *p = A->head, *e = A->head + UP_HEAD;
    u32 i;
    zero(uid, sizeof uid); zero(key, sizeof key);
    if (!E.e->ident(E.e->ctx, uid, sizeof uid - 1, key, sizeof key - 1)) return 0;
    piece_end = r->sent + UP_PIECE < r->len ? r->sent + UP_PIECE : r->len;
    p = o_s(p, e, "POST " UP_PATH " HTTP/1.1\r\nHost: " REP_HOST "\r\nUser-Agent: " REP_CLIENT_NAME "\r\nX-App-Id: multishine\r\nAuthorization: Bearer ");
    p = o_s(p, e, key);
    p = o_s(p, e, "\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\nContent-Type: application/octet-stream\r\nX-Recording: ");
    for (i = 0; i < 32u && p + 2 < e; i++) { *p++ = (u8)"0123456789abcdef"[r->sha[i] >> 4]; *p++ = (u8)"0123456789abcdef"[r->sha[i] & 15]; }
    p = o_s(p, e, "\r\nX-Recording-Length: "); p = o_u(p, e, r->len);
    p = o_s(p, e, "\r\nX-Recording-Offset: "); p = o_u(p, e, r->sent);
    p = o_s(p, e, "\r\nX-Recording-Age: "); p = o_u(p, e, (now - r->t_ms) / 1000u);
    p = o_s(p, e, "\r\nContent-Length: "); p = o_u(p, e, piece_end - r->sent);
    p = o_s(p, e, "\r\n\r\n");
    zero(key, sizeof key);
    if (p >= e) { zero(A->head, sizeof A->head); return 0; }
    hlen = (u32)(p - A->head); hsent = 0; pos = r->sent; blen = 0;
    up_st[1]++;
    return 1;
}
/* 4 KB at `off` (multiple of the chunk) into A->buf: 1 there, 0 not yet, -1 gone */
static int chunk(u32 off)
{
    int n;
    if (buf_n && buf_off == off) return 1;
    buf_n = 0;
    n = E.read(seq, off, A->buf);
    if (n <= 0) return n;
    buf_off = off; buf_n = (u32)n;
    return 1;
}
static void lost(void) { up_st[8]++; E.forget(seq, 2); down(); }

static u32 find_num(const char *key, u32 *ok)           /* "key":number in the answer */
{
    u32 i, k, v = 0;
    *ok = 0;
    for (i = 0; i < blen; i++) {
        for (k = 0; key[k] && i + k < blen && A->body[i + k] == (u8)key[k]; k++) {}
        if (key[k]) continue;
        for (i += k; i < blen && A->body[i] >= '0' && A->body[i] <= '9' && v < 100000000u; i++) { v = v * 10u + (u32)(A->body[i] - '0'); *ok = 1; }
        return v;
    }
    return 0;
}
static void answer(u32 now)
{
    struct rec_pub *r = E.pending();
    u32 ok, next = find_num("\"nextOffset\":", &ok);
    int s = HR.status;
    up_st[3] = (u32)s; E.k->st.last_http = (rep_u16)s;
    if (!r || r->seq != seq) { down(); return; }
    if ((s == 200 || s == 409) && ok && next <= r->len && (next == r->len || !(next % REC_CHUNK))) {
        E.k->fails_tls = 0;
        if (s == 200) { up_st[2]++; if (next > r->sent) r->fails = 0; } else if (++r->fails >= UP_FAILS_MAX) { up_st[10]++; E.forget(seq, 1); sdsec_close(&A->sec); state(U_CLOSE, now); return; }
        r->sent = next;
        if (next == r->len) { up_st[5]++; E.forget(seq, 0); sdsec_close(&A->sec); state(U_CLOSE, now); return; }
        if (HR.close || !build(r, now)) { sdsec_close(&A->sec); state(U_CLOSE, now); return; }
        state(U_HEAD, now);
        return;
    }
    if (s >= 400 && s < 500 && s != 408 && s != 409 && s != 425 && s != 429) {
        up_st[6]++; E.forget(seq, 1);
        E.k->t_cooldown_until = now + (s == 401 || s == 403 ? REP_WEB_COOLDOWN_MS : 2000u);     /* a refused key or account: nothing else would pass either */
        sdsec_close(&A->sec); state(U_CLOSE, now);
        return;
    }
    if (++r->fails >= UP_FAILS_MAX) { up_st[10]++; E.forget(seq, 1); }
    E.k->next_ms = now + (s == 429 ? 60000u : 15000u);
    sdsec_close(&A->sec); state(U_CLOSE, now);
}

void up_poll(int heavy)
{
    struct rep_env *e = E.e;
    u32 now = e->ms(e->ctx), t, n;
    struct rec_pub *r = E.pending();
    int rc, i;
    if (st == U_IDLE && !r) return;
    if (!e->scene_ok(e->ctx)) { down(); return; }
    if (st != U_IDLE && st != U_CLOSE && (!r || r->seq != seq)) { down(); return; }
    switch (st) {
    case U_IDLE:
        if (before(now, E.k->next_ms) || before(now, E.k->t_cooldown_until) || !e->unix_time(e->ctx) || (e->entropy_ready && !e->entropy_ready(e->ctx))) return;
        A = (struct up_arena *)E.get((u32)sizeof *A);
        if (!A) return;
        zero(A, sizeof *A);
        seq = r->seq; buf_n = 0;
        if (r->sha_ok) state(U_DNS, now);
        else { sdc_sha_init(&SH); pos = 0; state(U_HASH, now); }
        return;
    case U_HASH:
        for (i = 0; i < 2; i++) {
            if ((rc = chunk(pos)) < 0) { lost(); return; }
            if (!rc) return;
            sdc_sha_update(&SH, A->buf, buf_n);
            pos += buf_n;
            if (pos >= r->len) { sdc_sha_final(&SH, r->sha); r->sha_ok = 1; state(U_DNS, now); return; }
        }
        return;
    case U_DNS:
        rc = e->dns(e->ctx, REP_HOST, &ip, now);
        if (rc == 0) { if ((u32)(now - t0) > 15000u) fail(now); return; }
        if (rc < 0) { fail(now); return; }
        if (!(t = e->unix_time(e->ctx))) { down(); return; }
        A->web.anchors = e->anchors; A->web.nanchors = e->nanchors; A->web.now = t; A->web.rx = A->rx; A->web.rx_cap = sizeof A->rx;
        up_st[0]++;
        if (sdsec_open_web(&A->sec, e->tcp, ip, REP_PORT, (sdtls_rand_fn)e->rand, e->ctx, &A->web, REP_HOST, REP_SESSION_TIMEOUT_MS) < 0) { fail(now); return; }
        state(U_OPEN, now);
        return;
    case U_OPEN:
        rc = sdsec_poll(&A->sec, heavy);
        if (rc == SDSEC_FAILED) {
            if (A->sec.err == SDSEC_E_TLS && (A->sec.tls_err == SDTLS_E_UNTRUSTED || A->sec.tls_err == SDTLS_E_HOST || A->sec.tls_err == SDTLS_E_CERT || A->sec.tls_err == SDTLS_E_SIG)) E.k->t_cooldown_until = now + REP_WEB_COOLDOWN_MS;   /* never sent to a server we cannot authenticate; the recording waits */
            fail(now);
            return;
        }
        if (rc == SDSEC_UP) { if (!build(r, now)) { sdsec_close(&A->sec); state(U_CLOSE, now); E.k->next_ms = now + 60000u; return; } state(U_HEAD, now); }
        return;
    case U_HEAD:
        (void)sdsec_poll(&A->sec, heavy);
        if (A->sec.state != SDSEC_UP) { fail(now); return; }
        while (hsent < hlen) {
            if ((rc = sdsec_write(&A->sec, A->head + hsent, hlen - hsent)) < 0) { fail(now); return; }
            if (!rc) { if ((u32)(now - t0) > UP_ANSWER_MS) fail(now); return; }
            hsent += (u32)rc;
        }
        zero(A->head, sizeof A->head);                  /* the key was in here */
        state(U_BODY, now);
        return;
    case U_BODY:
        (void)sdsec_poll(&A->sec, heavy);
        if (A->sec.state != SDSEC_UP) { fail(now); return; }
        for (i = 0; i < 6 && pos < piece_end; i++) {
            u32 at = pos & ~(REC_CHUNK - 1u);
            if ((rc = chunk(at)) < 0) { lost(); return; }
            if (!rc) break;
            n = at + buf_n - pos; if (n > piece_end - pos) n = piece_end - pos; if (n > 1400u) n = 1400u;
            if ((rc = sdsec_write(&A->sec, A->buf + (pos - at), n)) < 0) { fail(now); return; }
            if (!rc) break;
            pos += (u32)rc; up_st[4] += (u32)rc; t0 = now;
        }
        if (pos >= piece_end) { sdhttp_rsp_init(&HR); blen = 0; state(U_RECV, now); }
        else if ((u32)(now - t0) > UP_ANSWER_MS) fail(now);
        return;
    case U_RECV: {
        u8 tmp[256];
        (void)sdsec_poll(&A->sec, heavy);
        for (i = 0; i < 12; i++) {
            u32 used = 0, k, bl;
            const u8 *bd;
            rc = sdsec_read(&A->sec, tmp, sizeof tmp);
            if (rc == 0) break;
            if (rc < 0) { fail(now); return; }
            while (used < (u32)rc) {
                int u = sdhttp_feed(&HR, tmp + used, (u32)rc - used, &bd, &bl);
                if (u < 0) { fail(now); return; }
                for (k = 0; k < bl; k++) if (blen < UP_RSP) A->body[blen++] = bd[k];
                used += (u32)u;
                if (HR.state == SDHTTP_DONE || !u) break;
            }
            if (HR.state == SDHTTP_DONE) { answer(now); return; }
        }
        if ((u32)(now - t0) > UP_ANSWER_MS) fail(now);
        return; }
    case U_CLOSE:
        (void)sdsec_poll(&A->sec, heavy);
        if (A->sec.state == SDSEC_CLOSED || A->sec.state == SDSEC_FAILED || (u32)(now - t0) > 2000u) down();
        return;
    default: down(); return;
    }
}
