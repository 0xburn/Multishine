/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbrw: see sbr_ws.h.  C89-compatible (builds -std=c99 -pedantic like the neighbours), no libc, no heap. */
#include "sbr_ws.h"

typedef sbr_u32 u32;
typedef sbr_u16 u16;
typedef sbr_u8 u8;

enum { P_IDLE = 0, P_WAIT, P_CONN, P_JOIN, P_QUEUED, P_MATCHED, P_CONNECTED, P_FAILED };

/* ------------------------------------------------------------------ small helpers */
static void zero(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }
static void bcopy_(void *d, const void *s, u32 n) { u8 *a = (u8 *)d; const u8 *b = (const u8 *)s; while (n--) *a++ = *b++; }   /* no memcpy (freestanding) */
static u32 slen(const char *s) { u32 n = 0; if (!s) return 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, u32 cap) { u32 i = 0; if (!cap) return; if (s) for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ------------------------------------------------------------------ JSON writer */
struct jw { u8 *b; u32 cap, n; int ovf; };
static void jw_c(struct jw *w, char c) { if (w->n + 1 >= w->cap) { w->ovf = 1; return; } w->b[w->n++] = (u8)c; }
static void jw_raw(struct jw *w, const char *s) { while (*s) jw_c(w, *s++); }
static void jw_uint(struct jw *w, u32 v)
{
    char t[10];
    int k = 0;
    if (!v) { jw_c(w, '0'); return; }
    while (v) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k) jw_c(w, t[--k]);
}
static void jw_str(struct jw *w, const char *s)        /* escapes " \ and control characters; UTF-8 as is */
{
    static const char hx[] = "0123456789abcdef";
    jw_c(w, '"');
    for (; *s; s++) {
        u8 c = (u8)*s;
        if (c == '"') jw_raw(w, "\\\"");
        else if (c == '\\') jw_raw(w, "\\\\");
        else if (c == '\n') jw_raw(w, "\\n");
        else if (c == '\r') jw_raw(w, "\\r");
        else if (c == '\t') jw_raw(w, "\\t");
        else if (c < 0x20) { jw_raw(w, "\\u00"); jw_c(w, hx[c >> 4]); jw_c(w, hx[c & 15]); }
        else jw_c(w, (char)c);
    }
    jw_c(w, '"');
}
static void jw_addr(struct jw *w, u32 ip, u16 port)
{
    jw_c(w, '"');
    jw_uint(w, ip >> 24); jw_c(w, '.'); jw_uint(w, (ip >> 16) & 255u); jw_c(w, '.');
    jw_uint(w, (ip >> 8) & 255u); jw_c(w, '.'); jw_uint(w, ip & 255u); jw_c(w, ':'); jw_uint(w, port);
    jw_c(w, '"');
}
static int jw_end(struct jw *w) { if (w->ovf) return -1; return (int)w->n; }   /* no NUL: a text frame carries length bytes */
static void jw_open(struct jw *w, u8 *out, u32 cap) { w->b = out; w->cap = cap; w->n = 0; w->ovf = 0; }

int sbrw_write_simple(const char *type, u8 *out, u32 cap)
{
    struct jw w;
    jw_open(&w, out, cap);
    jw_raw(&w, "{\"type\":"); jw_str(&w, type); jw_c(&w, '}');
    return jw_end(&w);
}

int sbrw_write_resume(const char *ticket, const char *token, u8 *out, u32 cap)
{
    struct jw w;
    jw_open(&w, out, cap);
    jw_raw(&w, "{\"type\":\"" SBRW_T_RESUME "\",\"ticketId\":"); jw_str(&w, ticket);
    jw_raw(&w, ",\"token\":"); jw_str(&w, token); jw_c(&w, '}');
    return jw_end(&w);
}

/* join: the only place that reads the play key (R7) */
int sbrw_write_join(const struct sbrw *W, u8 *out, u32 cap)
{
    struct jw w;
    u32 i;
    jw_open(&w, out, cap);
    jw_raw(&w, "{\"type\":\"" SBRW_T_JOIN "\",\"user\":{\"uid\":"); jw_str(&w, W->id->uid);
    jw_raw(&w, ",\"playKey\":"); jw_str(&w, W->id->play_key);
    jw_raw(&w, ",\"connectCode\":"); jw_str(&w, W->id->code);
    jw_raw(&w, ",\"displayName\":"); jw_str(&w, W->id->name);
    jw_raw(&w, "},\"search\":{\"mode\":"); jw_uint(&w, W->mode);
    jw_raw(&w, ",\"connectCode\":[");
    for (i = 0; i < W->code_len; i++) { if (i) jw_c(&w, ','); jw_uint(&w, W->code_sjis[i]); }
    jw_raw(&w, "]},\"appVersion\":"); jw_str(&w, W->app_version);
    jw_raw(&w, ",\"port\":"); jw_uint(&w, W->cfg.mapped_port ? W->cfg.mapped_port : W->cfg.local_port);
    jw_raw(&w, ",\"ipAddressLan\":");
    if (W->cfg.lan_ip) jw_addr(&w, W->cfg.lan_ip, W->cfg.local_port);
    else jw_raw(&w, "\"\"");
    if (W->cfg.ncand) {
        jw_raw(&w, ",\"candidates\":[");
        for (i = 0; i < W->cfg.ncand && i < SBRW_CANDS_MAX; i++) {
            if (i) jw_c(&w, ',');
            jw_raw(&w, "{\"ip\":"); jw_str(&w, W->cfg.cand[i].ip ? W->cfg.cand[i].ip : "");
            jw_raw(&w, ",\"port\":"); jw_uint(&w, W->cfg.cand[i].port);
            jw_raw(&w, ",\"type\":"); jw_str(&w, W->cfg.cand[i].type ? W->cfg.cand[i].type : ""); jw_c(&w, '}');   /* serde field order */
        }
        jw_c(&w, ']');
    }
    jw_raw(&w, ",\"natType\":"); jw_str(&w, W->cfg.nat_type && W->cfg.nat_type[0] ? W->cfg.nat_type : SBRW_NAT_UNKNOWN);   /* the stock client always sends one */
    jw_c(&w, '}');                                     /* "debug" is never sent (only a dev service with auth off honours it) */
    return jw_end(&w);
}

/* ------------------------------------------------------------------ state helpers */
static int elapsed(u32 now, u32 t, u32 ms) { return (u32)(now - t) >= ms; }
static int visible(const struct sbrw *W)
{
    switch (W->ph) {
    case P_IDLE: return SBR_IDLE;
    case P_WAIT: case P_CONN: return W->resuming ? SBR_MM : SBR_INIT;
    case P_JOIN: return W->resuming ? SBR_MM : SBR_INIT;
    case P_QUEUED: return SBR_MM;
    case P_MATCHED: return SBR_OPP_CONN;
    case P_CONNECTED: return SBR_CONNECTED;
    default: return SBR_ERROR;
    }
}
static void setph(struct sbrw *W, int ph) { W->ph = ph; W->vis = visible(W); W->st.state = (u32)W->vis; }
static void wipe_tx(struct sbrw *W) { if (W->cfg.tx) zero(W->cfg.tx, W->cfg.tx_cap); }
static void drop_conn(struct sbrw *W) { if (W->conn_open) { W->ops->close(W->ctx); W->conn_open = 0; } wipe_tx(W); }
static void fail(struct sbrw *W, int e, const char *text)
{
    drop_conn(W);
    scpy(W->err_text, text, sizeof W->err_text);
    W->err = e;
    W->st.err = (u32)e;
    W->st.errors++;
    zero(W->token, sizeof W->token);
    setph(W, P_FAILED);
}
static void wait_then(struct sbrw *W, u32 ms, int next)
{
    drop_conn(W);
    W->t_wait = W->now; W->wait_ms = ms; W->next_ph = next;
    setph(W, P_WAIT);
}
static void start_attempt(struct sbrw *W)
{
    W->t_attempt = W->now;
    W->shake_seen = 0;
    W->conn_open = 1;
    setph(W, P_CONN);
}
static int send_msg(struct sbrw *W, int n)
{
    int r;
    if (n < 0) return -1;
    r = W->ops->send_text(W->ctx, W->cfg.tx, (u32)n);
    wipe_tx(W);
    return r < 0 ? -1 : 0;
}

/* ------------------------------------------------------------------ API */
void sbrw_init(struct sbrw *W, const struct sbrw_cfg *cfg, const struct sbrw_ops *ops, void *ctx)
{
    zero(W, sizeof *W);
    bcopy_(&W->cfg, cfg, sizeof W->cfg);
    W->ops = ops;
    W->ctx = ctx;
    scpy(W->app_version, cfg->app_version[0] ? cfg->app_version : SBRW_APP_VERSION, sizeof W->app_version);
    W->st.magic = 0x53425731u;                         /* 'SBW1' */
    wipe_tx(W);
}

void sbrw_identity(struct sbrw *W, const struct sbr_ident *id)
{
    W->id = id;
    W->id_ok = id && id->uid[0] && id->play_key[0] && id->code[0];
}
void sbrw_set_net(struct sbrw *W, int up, u32 lan_ip) { W->net_up = up; W->cfg.lan_ip = lan_ip; }
int sbrw_state(const struct sbrw *W) { return W->vis; }
int sbrw_err(const struct sbrw *W) { return W->vis == SBR_ERROR ? W->err : 0; }
const char *sbrw_err_text(const struct sbrw *W) { return W->vis == SBR_ERROR ? W->err_text : ""; }
const struct sbr_match *sbrw_match(const struct sbrw *W) { return &W->match; }
u32 sbrw_queue_s(const struct sbrw *W) { return W->st.queue_s; }

static int refuse(struct sbrw *W, int e, const char *text) { fail(W, e, text); return -e; }

int sbrw_search(struct sbrw *W, u32 mode, const char *code)
{
    if (W->ph != P_IDLE) return -SBR_E_BUSY;
    W->err = 0; W->err_text[0] = 0; W->latest[0] = 0;
    W->st.err = 0;
    W->st.searches++;
    W->reticket = 0;
    W->resuming = 0;
    W->now = W->ops->now_ms(W->ctx);
    if (W->outdated) return refuse(W, SBR_E_OUTDATED, SBRW_TXT_OUTDATED);      /* Slippi told us this session: no second search */
    if (!W->app_version[0]) return refuse(W, SBR_E_GATE, "No Slippi version configured");
    if (mode != SBR_MODE_UNRANKED && mode != SBR_MODE_DIRECT) return refuse(W, SBR_E_MODE, "Only Unranked and Direct are available on the Slippi bridge");
    if (!W->id_ok) return refuse(W, SBR_E_NO_ID, "No Slippi identity on the memory card");
    W->mode = mode;
    W->st.mode = mode;
    W->code[0] = 0;
    W->code_len = 0;
    zero(W->code_sjis, sizeof W->code_sjis);
    if (mode == SBR_MODE_DIRECT) {
        if (!sbr_code_valid(code)) return refuse(W, SBR_E_CODE, "That is not a valid connect code");
        if (seq(code, W->id->code)) return refuse(W, SBR_E_CODE, "That is your own connect code");
        scpy(W->code, code, sizeof W->code);
        W->code_len = sbr_code_to_sjis(code, W->code_sjis);
    }
    if (!W->net_up) return refuse(W, SBR_E_NO_NET, "No network. Check the cable.");
    W->ticket[0] = 0; W->token[0] = 0; W->token_ok = 0;
    W->attempts = 0;
    start_attempt(W);
    return 0;
}

void sbrw_cancel(struct sbrw *W)
{
    W->now = W->ops->now_ms(W->ctx);
    if (W->ph != P_IDLE) W->st.cancels++;
    if ((W->ph == P_JOIN || W->ph == P_QUEUED) && W->conn_open)
        (void)send_msg(W, sbrw_write_simple(SBRW_T_CANCEL, W->cfg.tx, W->cfg.tx_cap));   /* then close without waiting (stock) */
    drop_conn(W);
    zero(&W->match, sizeof W->match);
    zero(W->ticket, sizeof W->ticket);
    zero(W->token, sizeof W->token);
    W->token_ok = 0; W->resuming = 0;
    W->err = 0; W->st.err = 0; W->err_text[0] = 0;
    setph(W, P_IDLE);
}

/* the connection died while queued: resume the ticket inside the window, else the session fails */
static void lost(struct sbrw *W)
{
    W->st.drops++;
    if ((W->ph == P_QUEUED || (W->ph == P_JOIN && W->resuming)) && W->token_ok) {
        if (!W->resuming) W->t_lost = W->now;          /* the window runs from the FIRST loss and is cleared only by a `joined` */
        W->resuming = 1;
        W->attempts = 0;
        wait_then(W, SBRW_RECONNECT_MS, P_CONN);
    } else fail(W, SBR_E_LOST, SBRW_TXT_LOST);
}

static void got_match(struct sbrw *W)
{
    const char *text = "";
    int e = sbr_assign_parse(&W->reply, W->cfg.local_port, W->st.assigned + 1, &W->match, &text);
    if (e) { zero(&W->match, sizeof W->match); fail(W, e, text); return; }
    W->st.assigned++;
    W->match.ready = 1;                                /* the socket is closed right here: the netplay layer may bind its port now */
    drop_conn(W);                                      /* stock: close without waiting for the server */
    zero(W->token, sizeof W->token);
    W->token_ok = 0;
    setph(W, P_MATCHED);
}

static void handle(struct sbrw *W, const u8 *js, u32 n)
{
    const char *t;
    if (sbr_parse_reply(js, n, &W->reply) < 0) { W->st.rx_bad++; return; }      /* malformed JSON is ignored (stock) */
    W->st.rx_msgs++;
    t = W->reply.type;
    if (seq(t, SBRW_T_JOINED)) {
        if ((W->ph != P_JOIN && W->ph != P_QUEUED) || !W->reply.ticket[0]) { W->st.rx_ignored++; return; }
        scpy(W->ticket, W->reply.ticket, sizeof W->ticket);
        scpy(W->token, W->reply.token, sizeof W->token);
        /* a field that filled its buffer was cut: then no resume (it would be refused) */
        W->token_ok = W->token[0] && slen(W->ticket) < SBRW_TICKET_MAX - 1 && slen(W->token) < SBRW_TOKEN_MAX - 1;
        if (W->ph == P_JOIN) { if (W->resuming) { W->resuming = 0; W->st.resumes++; } setph(W, P_QUEUED); }
    } else if (seq(t, SBRW_T_STATUS)) {
        if (W->ph == P_QUEUED) W->st.queue_s = W->reply.queue_s; else W->st.rx_ignored++;
    } else if (seq(t, SBRW_T_MATCH)) {
        if (W->ph == P_QUEUED) got_match(W); else W->st.rx_ignored++;
    } else if (seq(t, SBRW_T_ERROR)) {
        if (W->reply.latest[0]) {                      /* outdated appVersion: no further search this session */
            W->outdated = 1;
            scpy(W->latest, W->reply.latest, sizeof W->latest);
            fail(W, SBR_E_OUTDATED, SBRW_TXT_OUTDATED);
        } else fail(W, SBR_E_SERVER, W->reply.error[0] ? W->reply.error : SBRW_TXT_SERVER);   /* no retry (R11) */
    } else W->st.rx_ignored++;                         /* unknown types are ignored (stock) */
}

static void pump(struct sbrw *W)
{
    u32 n;
    for (n = 0; n < SBRW_RX_BURST && (W->ph == P_JOIN || W->ph == P_QUEUED); n++) {
        int r = W->ops->recv_text(W->ctx, W->cfg.rx, W->cfg.rx_cap);
        if (r == 0) break;
        if (r == SBRW_IO_CLOSED || r < SBRW_IO_OVERSIZE || (r > 0 && (u32)r > W->cfg.rx_cap)) { lost(W); return; }
        W->t_rx = W->now; W->ping_sent = 0;            /* any frame is a sign of life and re-arms the ping */
        if (r == SBRW_IO_OVERSIZE) { W->st.rx_oversize++; continue; }
        handle(W, W->cfg.rx, (u32)r);
    }
}

static void attempt_failed(struct sbrw *W)
{
    W->st.connects_fail++;
    W->attempts++;
    if (!W->resuming && W->attempts >= SBRW_CONNECT_TRIES) fail(W, SBR_E_CONNECT, SBRW_TXT_CONNECT);
    else wait_then(W, SBRW_RECONNECT_MS, P_CONN);
}

static void do_conn(struct sbrw *W)
{
    int r;
    if (W->resuming && elapsed(W->now, W->t_lost, SBRW_RESUME_MS)) { fail(W, SBR_E_LOST, SBRW_TXT_LOST); return; }
    r = W->ops->connect(W->ctx);
    if (r == SBRW_CONN_UP) {
        int n = W->resuming ? sbrw_write_resume(W->ticket, W->token, W->cfg.tx, W->cfg.tx_cap) : sbrw_write_join(W, W->cfg.tx, W->cfg.tx_cap);
        W->st.connects_ok++;
        if (send_msg(W, n) < 0) { fail(W, SBR_E_SEND, "Couldn't send the search. Try again."); return; }
        W->t_rx = W->now; W->ping_sent = 0;
        W->t_join = W->now;                            /* `joined` is due SBRW_JOINED_MS after the socket opened, on EVERY connection (resume too) */
        if (!W->resuming) W->st.joins++;
        setph(W, P_JOIN);
    } else if (r < 0) attempt_failed(W);
    else if (r == SBRW_CONN_TCP) { if (elapsed(W->now, W->t_attempt, SBRW_CONNECT_MS)) attempt_failed(W); }
    else {
        if (!W->shake_seen) { W->shake_seen = 1; W->t_shake = W->now; }
        if (elapsed(W->now, W->t_shake, SBRW_HANDSHAKE_MS)) attempt_failed(W);
    }
}

void sbrw_tick(struct sbrw *W)
{
    W->now = W->ops->now_ms(W->ctx);
    switch (W->ph) {
    case P_WAIT:
        if (W->resuming && elapsed(W->now, W->t_lost, SBRW_RESUME_MS)) { fail(W, SBR_E_LOST, SBRW_TXT_LOST); break; }
        if (elapsed(W->now, W->t_wait, W->wait_ms)) start_attempt(W);
        break;
    case P_CONN: do_conn(W); break;
    case P_JOIN:
        pump(W);
        if (W->ph == P_JOIN && W->resuming && elapsed(W->now, W->t_lost, SBRW_RESUME_MS)) { fail(W, SBR_E_LOST, SBRW_TXT_LOST); break; }
        if (W->ph == P_JOIN && elapsed(W->now, W->t_join, SBRW_JOINED_MS)) {
            if (W->resuming) { drop_conn(W); lost(W); }    /* a resume answered by anything but `joined` is a lost connection again */
            else fail(W, SBR_E_JOIN, SBRW_TXT_JOIN);
        }
        break;
    case P_QUEUED:
        pump(W);
        if (W->ph != P_QUEUED) break;
        if (elapsed(W->now, W->t_rx, SBRW_LOST_MS)) { drop_conn(W); lost(W); }
        else if (!W->ping_sent && elapsed(W->now, W->t_rx, SBRW_PING_IDLE_MS)) {      /* ONE ping per silence; any frame re-arms it */
            W->ping_sent = 1;
            W->st.pings++;
            if (send_msg(W, sbrw_write_simple(SBRW_T_PING, W->cfg.tx, W->cfg.tx_cap)) < 0) { drop_conn(W); lost(W); }
        }
        break;
    default: break;
    }
}

void sbrw_peer_result(struct sbrw *W, int ok)
{
    W->now = W->ops->now_ms(W->ctx);
    if (W->ph != P_MATCHED) return;
    if (ok) { W->st.peer_ok++; setph(W, P_CONNECTED); return; }
    W->st.peer_fail++;
    if (W->reticket < W->cfg.reticket_max) {           /* a fresh search, silently, bounded (R11) */
        W->reticket++;
        W->st.retickets++;
        zero(&W->match, sizeof W->match);
        W->ticket[0] = 0; W->token[0] = 0; W->token_ok = 0;
        W->attempts = 0;
        W->resuming = 0;
        wait_then(W, SBRW_RETICKET_MS, P_CONN);
        return;
    }
    fail(W, SBR_E_PEER, "Couldn't connect to your opponent. Try again.");
}
