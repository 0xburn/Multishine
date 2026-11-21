/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_mm: see rep_mm.h.  The overlay's Slippi 3.7.0 matchmaker driver: mailbox commands -> identity copy -> STUN -> DNS -> sbr_ws over ms_ws over TLS -> mailbox results. */
#include "rep_mm.h"

typedef sbr_u32 u32;
typedef sbr_u8 u8;

static const struct { const char *name; unsigned short port; } STUN_SRV[RM_STUN_NSERV] = {
    { "stun.l.google.com", 19302 }, { "stun1.l.google.com", 19302 }, { "stun.cloudflare.com", 3478 }, { "global.stun.twilio.com", 3478 }
};

static void zero(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }
static void scpy(char *d, const char *s, u32 cap) { u32 i = 0; if (!cap) return; if (s) for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static void bcopy_(void *d, const void *s, u32 n) { u8 *a = (u8 *)d; const u8 *b = (const u8 *)s; while (n--) *a++ = *b++; }
static void ip_text(char *out, u32 ip)           /* dotted quad into out[16] */
{
    u32 k, n = 0;
    for (k = 0; k < 4; k++) {
        u32 v = (ip >> (24 - 8 * k)) & 255u, h = v / 100u, t = (v / 10u) % 10u;
        if (h) out[n++] = (char)('0' + h);
        if (h || t) out[n++] = (char)('0' + t);
        out[n++] = (char)('0' + v % 10u);
        if (k < 3) out[n++] = '.';
    }
    out[n] = 0;
}

/* ------------------------------------------------------------------ transport ops for sbr_ws (connect / send / recv / close / clock) */
static int ws_io_send(void *c, const ws_u8 *b, ws_u32 n) { struct rep_mm *m = (struct rep_mm *)c; return m->e.tp_write(m->e.ctx, b, n); }
static int ws_io_recv(void *c, ws_u8 *b, ws_u32 cap) { struct rep_mm *m = (struct rep_mm *)c; return m->e.tp_read(m->e.ctx, b, cap); }
static int mask4(struct rep_mm *m, u8 *k) { return m->e.rand(m->e.ctx, k, 4); }

static void reset_transport(struct rep_mm *m) { m->tp_started = 0; m->ws_started = 0; m->ws_open = 0; }

static int op_connect(void *c)
{
    struct rep_mm *m = (struct rep_mm *)c;
    int st;
    if (!m->tp_started) {
        if (m->e.tp_busy(m->e.ctx)) { (void)m->e.tp_poll(m->e.ctx, m->heavy); return SBRW_CONN_TCP; }   /* the previous session is still closing: wait (counts against the TCP budget) */
        if (m->e.tp_open(m->e.ctx, m->mm_ip, (unsigned short)m->mm_port, m->host) < 0) return -1;
        m->tp_started = 1;
    }
    st = m->e.tp_poll(m->e.ctx, m->heavy);
    if (st == RM_TP_FAILED || st == RM_TP_IDLE) { m->e.tp_close(m->e.ctx); reset_transport(m); return -1; }
    if (st == RM_TP_TCP) return SBRW_CONN_TCP;
    if (st == RM_TP_TLS) return SBRW_CONN_SHAKE;
    {   /* the secure stream is up: the WebSocket upgrade */
        u8 mk[4];
        int ev;
        if (mask4(m, mk)) return SBRW_CONN_SHAKE;
        if (!m->ws_started) {
            u8 key[16];
            if (m->e.rand(m->e.ctx, key, 16)) return SBRW_CONN_SHAKE;
            m->wio.send = ws_io_send; m->wio.recv = ws_io_recv; m->wio.ctx = m;
            ms_ws_init(&m->ws, &m->wio, m->e.wstx, RM_WSTX, m->e.wsrx, RM_WSRX);
            if (ms_ws_start(&m->ws, m->host, SBRW_PATH, key) < 0) { m->e.tp_close(m->e.ctx); reset_transport(m); return -1; }
            zero(key, sizeof key);
            m->ws_started = 1;
        }
        ev = ms_ws_poll(&m->ws, mk);
        if (ev == MS_WS_EV_OPEN) { m->ws_open = 1; return SBRW_CONN_UP; }
        if (ev < 0) { m->e.tp_close(m->e.ctx); reset_transport(m); return -1; }
        return SBRW_CONN_SHAKE;
    }
}

static int op_send(void *c, const u8 *buf, u32 n)
{
    struct rep_mm *m = (struct rep_mm *)c;
    u8 mk[4];
    int r;
    if (!m->ws_open || mask4(m, mk)) return -1;
    r = ms_ws_send_text(&m->ws, buf, n, mk);
    if (r < 0) return -1;
    (void)ms_ws_poll(&m->ws, mk);                       /* flush now (a cancel is followed by close at once); events of this poll are dropped, the next recv sees later frames */
    return 0;
}

/* ZERO COPY: the received message is parsed in place inside the WebSocket buffer (the match is about 1.6 KB; a second buffer would cost 4 KB of overlay bss).  sbr_ws.c reads the
   message from cfg.rx after recv_text returns, so this function points cfg.rx at it. */
static int op_recv(void *c, u8 *buf, u32 cap)
{
    struct rep_mm *m = (struct rep_mm *)c;
    u8 mk[4];
    int ev;
    (void)buf; (void)cap;
    if (!m->ws_open) return SBRW_IO_CLOSED;
    if (mask4(m, mk)) return 0;
    ev = ms_ws_poll(&m->ws, mk);
    if (ev == MS_WS_EV_TEXT) {
        m->W.cfg.rx = (u8 *)m->ws.msg;
        return m->ws.msg_len > 0 ? (int)m->ws.msg_len : 0;
    }
    if (ev == MS_WS_EV_CLOSE || ev < 0) return SBRW_IO_CLOSED;
    return 0;                                           /* none, binary, pong: nothing for the JSON layer */
}

static void op_close(void *c)
{
    struct rep_mm *m = (struct rep_mm *)c;
    u8 mk[4];
    if (m->ws_open && !mask4(m, mk)) {
        (void)ms_ws_poll(&m->ws, mk);                   /* flush a pending cancel */
        (void)ms_ws_close(&m->ws, 1000u, mk);
        (void)ms_ws_poll(&m->ws, mk);
    }
    m->e.tp_close(m->e.ctx);
    reset_transport(m);
}
static u32 op_now(void *c) { struct rep_mm *m = (struct rep_mm *)c; return m->e.ms(m->e.ctx); }

/* ------------------------------------------------------------------ mailbox helpers */
static void count(u32 *dst, u32 now, u32 *prev) { *dst += now - *prev; *prev = now; }

static void publish_counters(struct rep_mm *m)
{
    struct sbr_mbx *B = m->e.mbx;
    const struct sbrw_status *s = &m->W.st;
    struct sbrw_status *p = &m->prev;
    count(&B->n_joins, s->joins, &p->joins);
    count(&B->n_resumes, s->resumes, &p->resumes);
    count(&B->n_drops, s->drops, &p->drops);
    count(&B->n_assigned, s->assigned, &p->assigned);
    count(&B->n_errors, s->errors, &p->errors);
    count(&B->n_pings, s->pings, &p->pings);
    count(&B->n_rx_msgs, s->rx_msgs, &p->rx_msgs);
    count(&B->n_rx_bad, s->rx_bad, &p->rx_bad);
}

static void wipe_identity(struct rep_mm *m) { zero(&m->id, sizeof m->id); m->id_ok = 0; }

static void finish(struct rep_mm *m)                    /* the search is over: nothing of it stays (identity copy, token, buffers) */
{
    wipe_identity(m);
    zero(m->e.jtx, RM_JTX);
    m->phase = RM_DONE;
}

static void fail_now(struct rep_mm *m, int e, const char *text)
{
    struct sbr_mbx *B = m->e.mbx;
    B->err = (u32)e;
    scpy(B->err_text, text, sizeof B->err_text);
    B->latest[0] = 0;
    B->n_errors++;
    B->state = SBR_ERROR;
    m->e.dns(m->e.ctx, 0, 0, 0);
    op_close(m);
    finish(m);
}

static void stop_search(struct rep_mm *m)               /* CANCEL (or a new SEARCH over a running one) */
{
    if (m->phase == RM_ENT) { m->phase = RM_IDLE; m->e.mbx->state = SBR_IDLE; return; }
    if (m->phase == RM_RUN && m->W.ph != 0) sbrw_cancel(&m->W);   /* sends cancel when queued, closes */
    else op_close(m);
    m->e.dns(m->e.ctx, 0, 0, 0);
    finish(m);
    m->phase = RM_IDLE;
    m->e.mbx->state = SBR_IDLE;
}

/* ------------------------------------------------------------------ STUN step (sbr_stun) */
static void stun_begin_server(struct rep_mm *m)
{
    u32 i;
    m->e.rand(m->e.ctx, m->tid, SBR_STUN_TID_LEN);
    for (i = 0; i < SBR_STUN_TID_LEN; i++) m->tid[i] ^= (sbrs_u8)(m->frames >> (i & 3u));      /* never all-zero if the pool stalls */
    sbr_stun_build_request(m->req, m->tid);
    m->tries = 0; m->t_try = 0;
}

static void stun_finish(struct rep_mm *m)
{
    struct sbr_mbx *B = m->e.mbx;
    struct sbrw_cfg *c = &m->W.cfg;
    int nat = sbr_stun_nat_type(m->have[0], &m->ans[0], m->have[1], &m->ans[1]);
    B->stun_state = m->have[0] ? SBR_MS_DONE : SBR_MS_NONE;
    B->nat = (u32)nat;
    c->nat_type = sbr_stun_nat_name(nat);
    c->mapped_port = m->have[0] ? m->ans[0].port : 0;
    c->ncand = 0;
    if (m->have[0]) {
        ip_text(m->cand_ip[0], m->ans[0].ip);
        c->cand[c->ncand].type = SBRW_CAND_SRFLX; c->cand[c->ncand].ip = m->cand_ip[0]; c->cand[c->ncand].port = m->ans[0].port; c->ncand++;
    }
    if (m->lan_ip) {
        ip_text(m->cand_ip[1], m->lan_ip);
        c->cand[c->ncand].type = SBRW_CAND_HOST; c->cand[c->ncand].ip = m->cand_ip[1]; c->cand[c->ncand].port = (unsigned short)m->local_port; c->ncand++;
    }
    m->phase = RM_DNS;
    m->t_phase = m->now;
}

static void stun_step(struct rep_mm *m)
{
    unsigned char d[SBR_STUN_MAX_REPLY];
    sbr_u32 ip;
    unsigned short port;
    int n;
    while (m->sent_any && (n = m->e.udp_recv(m->e.ctx, d, sizeof d, &ip, &port)) >= 0) {   /* anything else on the shared socket is dropped while we search (the netplay host does not exist yet) */
        struct sbr_stun_addr a;
        if (sbr_stun_is_stun((const sbrs_u8 *)d, (u32)n) && sbr_stun_parse_response(m->req, (const sbrs_u8 *)d, (u32)n, &a) && m->nresp < 2) {
            if (!m->nresp) { m->ka_ip = m->srv_ip[m->si]; m->ka_port = STUN_SRV[m->si].port; }
            m->ans[m->nresp] = a; m->have[m->nresp] = 1; m->nresp++;
            m->sent_any = 0; m->tries = 0; m->si++; m->stun_dns = 0;     /* next server (the second answer must come from another one) */
            if (m->si < RM_STUN_NSERV && m->nresp < 2) stun_begin_server(m);
        }
    }
    if (m->nresp >= 2 || m->si >= RM_STUN_NSERV || (u32)(m->now - m->t_phase) >= RM_STUN_BUDGET_MS) { stun_finish(m); return; }
    if (!m->stun_dns) {                                   /* resolve this server */
        sbr_u32 r = 0;
        int rc = m->e.dns(m->e.ctx, STUN_SRV[m->si].name, &r, m->now);
        if (rc == 0) return;
        if (rc < 0) { m->si++; m->stun_dns = 0; if (m->si < RM_STUN_NSERV) stun_begin_server(m); return; }
        m->srv_ip[m->si] = r; m->stun_dns = 1; m->t_try = 0;
    }
    if (!m->t_try || (u32)(m->now - m->t_try) >= SBR_STUN_WAIT_MS) {
        if ((u32)m->tries >= SBR_STUN_TRIES) { m->si++; m->stun_dns = 0; m->sent_any = 0; if (m->si < RM_STUN_NSERV) stun_begin_server(m); return; }
        m->e.udp_send(m->e.ctx, m->srv_ip[m->si], STUN_SRV[m->si].port, m->req, SBR_STUN_REQ_LEN);
        m->tries++; m->sent_any = 1; m->t_try = m->now ? m->now : 1u;
    }
}

/* keepalive of the NAT mapping while queued: one request to the first server that answered, every 15 s (the stock client does this while Connecting / Queued); the reply is dropped */
static void stun_keepalive(struct rep_mm *m)
{
    unsigned char d[SBR_STUN_MAX_REPLY];
    sbr_u32 ip;
    unsigned short port;
    while (m->e.udp_recv(m->e.ctx, d, sizeof d, &ip, &port) >= 0) {}
    if (m->have[0] && (u32)(m->now - m->t_ka) >= SBR_STUN_KEEPALIVE_MS) {
        m->t_ka = m->now ? m->now : 1u;
        m->e.udp_send(m->e.ctx, m->ka_ip, m->ka_port, m->req, SBR_STUN_REQ_LEN);
    }
}

/* ------------------------------------------------------------------ search start */
static void begin_search(struct rep_mm *m)
{
    struct sbr_mbx *B = m->e.mbx;
    struct sbrw_cfg cfg;
    u32 i;
    zero(&cfg, sizeof cfg);
    zero(&m->W, sizeof m->W);
    zero(&m->prev, sizeof m->prev);
    m->mode = B->mode; m->lan_ip = B->lan_ip; m->local_port = B->local_port;
    m->mm_ip = B->mm_ip; m->mm_port = B->mm_port ? B->mm_port : 443u;
    scpy(m->host, B->host, sizeof m->host);
    scpy(m->code, B->code, sizeof m->code);
    cfg.lan_ip = B->lan_ip; cfg.local_port = (unsigned short)B->local_port; cfg.reticket_max = 0;
    scpy(cfg.app_version, B->app_version, sizeof cfg.app_version);
    cfg.rx = m->e.wsrx; cfg.rx_cap = RM_WSRX - MS_WS_RX_HDR; cfg.tx = m->e.jtx; cfg.tx_cap = RM_JTX;
    m->ops.connect = op_connect; m->ops.send_text = op_send; m->ops.recv_text = op_recv; m->ops.close = op_close; m->ops.now_ms = op_now;
    sbrw_init(&m->W, &cfg, &m->ops, m);
    reset_transport(m);
    B->state = SBR_INIT; B->err = 0; B->err_text[0] = 0; B->latest[0] = 0; B->queue_s = 0; B->stun_state = SBR_MS_OFF; B->nat = SBR_NAT_UNKNOWN;
    B->n_searches++;
    if (!m->e.ident(m->e.ctx, &m->id)) { fail_now(m, SBR_E_NO_ID, "No Slippi identity on the memory card"); return; }
    m->id_ok = 1;
    sbrw_identity(&m->W, &m->id);
    sbrw_set_net(&m->W, 1, B->lan_ip);
    m->e.dns_cfg(m->e.ctx, B->dns_ip, B->dns_port);
    for (i = 0; i < 2; i++) { m->have[i] = 0; }
    m->nresp = 0; m->si = 0; m->stun_dns = 0; m->sent_any = 0; m->mm_resolved = 0; m->t_ka = m->now ? m->now : 1u;
    m->t_phase = m->now;
    if (B->mm_ip) {                                       /* lab override: the matchmaker is a numeric address; STUN is skipped (no public servers in a lab) */
        stun_finish(m);
        B->stun_state = SBR_MS_OFF;
        m->phase = RM_DNS;
    } else {
        B->stun_state = SBR_MS_RUN;
        stun_begin_server(m);
        m->phase = RM_STUN;
    }
}

/* ------------------------------------------------------------------ the frame */
static void run_sbrw(struct rep_mm *m)
{
    struct sbr_mbx *B = m->e.mbx;
    int s;
    sbrw_tick(&m->W);
    publish_counters(m);
    s = sbrw_state(&m->W);
    B->queue_s = sbrw_queue_s(&m->W);
    if (s == SBR_ERROR) {
        B->err = (u32)sbrw_err(&m->W);
        scpy(B->err_text, sbrw_err_text(&m->W), sizeof B->err_text);
        scpy(B->latest, m->W.latest, sizeof B->latest);
        B->state = SBR_ERROR;
        finish(m);
    } else if (s == SBR_OPP_CONN) {
        bcopy_(&B->match, sbrw_match(&m->W), sizeof B->match);
        B->match_seq++;
        B->state = SBR_OPP_CONN;                          /* the socket was closed inside sbr_ws before the match is visible */
        finish(m);
    } else if (s == SBR_MM) {
        B->state = SBR_MM;
        stun_keepalive(m);
    } else B->state = SBR_INIT;
}

void rep_mm_init(struct rep_mm *m, const struct rep_mm_env *env)
{
    zero(m, sizeof *m);
    m->e = *env;
    m->phase = RM_IDLE;
    m->seen = env->mbx->ack_seq;                          /* a command issued before the block was loaded (cmd_seq != ack_seq) is ours: the first frame takes it */
}

void rep_mm_frame(struct rep_mm *m)
{
    struct sbr_mbx *B = m->e.mbx;
    m->now = m->e.ms(m->e.ctx);
    m->frames++;
    m->heavy = (m->frames & RM_HEAVY_MASK) == 0;
    B->alive++;
    if (B->cmd_seq != m->seen) {                          /* an edge: read the command and its arguments, then ack */
        u32 cmd = B->cmd, seq = B->cmd_seq;
        if (m->phase == RM_RUN || m->phase == RM_STUN || m->phase == RM_DNS || m->phase == RM_ENT) stop_search(m);
        m->seen = seq;
        if (cmd == SBR_MC_SEARCH) {
            if (m->e.ready && !m->e.ready(m->e.ctx)) { m->phase = RM_ENT; m->t_phase = m->now; B->state = SBR_INIT; B->err = 0; B->err_text[0] = 0; }
            else begin_search(m);
        } else { m->phase = RM_IDLE; B->state = SBR_IDLE; }
        B->ack_seq = seq;
    }
    switch (m->phase) {
    case RM_ENT:
        if (m->e.ready(m->e.ctx)) begin_search(m);
        else if ((u32)(m->now - m->t_phase) >= RM_ENT_WAIT_MS) fail_now(m, SBR_E_CONNECT, "Not enough randomness yet. Move the stick and try again");
        break;
    case RM_STUN: stun_step(m); break;
    case RM_DNS:
        if (m->mm_ip) { m->phase = RM_RUN; (void)sbrw_search(&m->W, m->mode, m->code); }
        else {
            sbr_u32 ip = 0;
            int rc = m->e.dns(m->e.ctx, m->host, &ip, m->now);
            if (rc < 0 || (u32)(m->now - m->t_phase) > 25000u) { fail_now(m, SBR_E_DNS, "Matchmaker name not found (DNS)"); break; }
            if (rc == 0) break;
            m->mm_ip = ip;
            m->phase = RM_RUN;
            (void)sbrw_search(&m->W, m->mode, m->code);
        }
        if (m->phase == RM_RUN) run_sbrw(m);
        break;
    case RM_RUN: run_sbrw(m); break;
    case RM_DONE:
        (void)m->e.tp_poll(m->e.ctx, m->heavy);          /* let a closing session finish */
        if (!m->e.tp_busy(m->e.ctx)) m->phase = RM_IDLE;
        break;
    default: (void)m->e.tp_poll(m->e.ctx, 0); break;
    }
}

void rep_mm_leave(struct rep_mm *m)
{
    if (m->phase == RM_RUN || m->phase == RM_STUN || m->phase == RM_DNS || m->phase == RM_ENT) m->e.mbx->state = SBR_IDLE;   /* the resident sees the search vanish (its ack timer or the scene logic ends it) */
    m->e.dns(m->e.ctx, 0, 0, 0);
    op_close(m);
    wipe_identity(m);
    zero(&m->W, sizeof m->W);
    zero(&m->ws, sizeof m->ws);
    zero(m->e.jtx, RM_JTX); zero(m->e.wstx, RM_WSTX); zero(m->e.wsrx, RM_WSRX);
    m->phase = RM_IDLE;
}

int rep_mm_busy(const struct rep_mm *m) { return m->phase != RM_IDLE || m->tp_started || m->e.mbx->cmd_seq != m->e.mbx->ack_seq; }
