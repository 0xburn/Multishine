/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* CANONICAL sdenet.c (offline/common/sdenet): see sdenet.h for what was merged from the enet, chat and S9 copies.
   Tests: (libenet interop, fragments, clock, differential against every copy, fuzz).
   sdenet.c: see sdenet.h.  Project-owned; behaviour derived from the ENet 1.3 protocol definitions and checked
   against the real library in tests/ (no ENet code copied). */
#include "sdenet.h"

#ifdef SDENET_S9_CLOCK_GLOBAL
sde_u32 sdenet9_clock_back;          /* S9 compatibility counter */
#endif

#define PING_INTERVAL 500u
#define TIMEOUT_LIMIT 32u
#define CONNECT_PUNCH_RTO 200u        /* CONNECT retransmit interval while punching (ms) */
#define CONNECT_PUNCH_MS 20000u       /* ... for this long after the first CONNECT */
#define TIMEOUT_MIN 5000u
#define TIMEOUT_MAX 30000u
#define DEFAULT_RTT 500u
#define MAX_CMDS 32u
#define UNSEQ_WINDOW 1024u
#define UNSEQ_FREE_WINDOWS 32u
#define REL_WINDOW 0x1000u
#define REL_WINDOWS 16u
#define REL_FREE_WINDOWS 8u

/* fragments are on for a host that set a buffer; -DSDENET_NO_FRAGMENTS compiles them out (netplay images: about 1.2 KB
   less code, behaviour identical to a host without a buffer, sdenet_set_frag_buf is then a no-op) */
#ifdef SDENET_NO_FRAGMENTS
#define FRAG_ON(h) 0
#else
#define FRAG_ON(h) ((h)->frag.buf != 0)
#endif

static const sde_u8 cmd_size[SDE_CMD_COUNT] = { 0, 8, 48, 44, 8, 4, 6, 8, 24, 8, 12, 16, 24 };

static void put16(sde_u8 *p, sde_u32 v) { p[0] = (sde_u8)(v >> 8); p[1] = (sde_u8)v; }
static void put32(sde_u8 *p, sde_u32 v) { p[0] = (sde_u8)(v >> 24); p[1] = (sde_u8)(v >> 16); p[2] = (sde_u8)(v >> 8); p[3] = (sde_u8)v; }
static sde_u32 get16(const sde_u8 *p) { return ((sde_u32)p[0] << 8) | p[1]; }
static sde_u32 get32(const sde_u8 *p) { return ((sde_u32)p[0] << 24) | ((sde_u32)p[1] << 16) | ((sde_u32)p[2] << 8) | p[3]; }
static void zero(void *p, sde_u32 n) { sde_u8 *b = (sde_u8 *)p; while (n--) *b++ = 0; }
static void copy(sde_u8 *d, const sde_u8 *s, sde_u32 n) { while (n--) *d++ = *s++; }
static int before(sde_u32 a, sde_u32 b) { return (sde_s32)(a - b) < 0; }

/* The host clock never goes back.  The console reads its clock once at the start of a scene-loop slice, drains the
   socket (sdenet_rx with a NEWER reading) and only then calls sdenet_service with the slice's older reading.  A reliable
   command that went out during the drain carries sent_ms newer than that `now`; `now - sent_ms` wrapped to ~4e9 ms >=
   TIMEOUT_MAX and the peer was dropped at once (chat run p2_nodraw), and a stale h->now made handle_ack discard
   acknowledgements as "from the future".  Fix from the chat copy (also in S9's copy); the clamp starts after the first
   reading, because h->now is 0 after sdenet_init and a first `now` >= 2^31 would otherwise be "before" 0 for ever. */
static sde_u32 mono(struct sdenet_host *h, sde_u32 now)
{
    if (h->clock_set && before(now, h->now)) {
        now = h->now;
        h->st.clock_back++;
#ifdef SDENET_S9_CLOCK_GLOBAL
        sdenet9_clock_back++;
#endif
    }
    h->clock_set = 1;
    return now;
}

static void peer_clear(struct sdenet_peer *p)
{
    sde_u8 in = p->in_session, out = p->out_session;     /* ENet keeps the session ids of a slot across resets */
    zero(p, sizeof *p);
    p->in_session = in; p->out_session = out;
    p->out_peer_id = SDE_PEER_ID_NONE;
    p->rtt = DEFAULT_RTT;
}

void sdenet_init(struct sdenet_host *h, const struct sdenet_ops *ops, void *ctx, sde_u32 seed)
{
    int i;
    zero(h, sizeof *h);
    h->ops = ops; h->ctx = ctx; h->seed = seed;
    for (i = 0; i < SDENET_MAX_PEERS; i++) {
        h->peer[i].in_session = h->peer[i].out_session = 0xFF;
        peer_clear(&h->peer[i]);
    }
    h->frag.peer = -1;
}

int sdenet_state(const struct sdenet_host *h, int p)
{
    return (p >= 0 && p < SDENET_MAX_PEERS) ? h->peer[p].state : -1;
}

void sdenet_reset_peer(struct sdenet_host *h, int p)
{
    if (p >= 0 && p < SDENET_MAX_PEERS) peer_clear(&h->peer[p]);
    if (h->frag.peer == p) h->frag.active = 0;
}

void sdenet_set_frag_buf(struct sdenet_host *h, sde_u8 *buf, sde_u32 cap)
{
#ifdef SDENET_NO_FRAGMENTS
    buf = 0;
#endif
    h->frag.buf = buf;
    h->frag.cap = buf ? cap : 0;
    h->frag.active = 0;
    h->frag.peer = -1;
}

void sdenet_set_allow(struct sdenet_host *h, sde_u32 ip, sde_u16 port, int enable)
{
    h->allow_on = enable ? 1 : 0; h->allow_ip = ip; h->allow_port = port;
}

static void drop_peer(struct sdenet_host *h, int p, sde_u32 data, int reason)
{
    int was = h->peer[p].state;
    peer_clear(&h->peer[p]);
    if (h->frag.peer == p) h->frag.active = 0;
    /* like ENet: a peer that never reached CONNECTED from the acceptor side vanishes without an event */
    if (was == SDE_ACK_CONNECT && reason != SDE_DISC_LOCAL) return;
    if (h->ops->on_disconnect) h->ops->on_disconnect(h->ctx, p, data, reason);
}

/* ---- outgoing reliable queue ---- */
static struct sdenet_rel *rel_alloc(struct sdenet_host *h, struct sdenet_peer *P, sde_u8 channel, sde_u8 cmd,
                                    sde_u32 cmdlen, const sde_u8 *data, sde_u32 len)
{
    int i;
    struct sdenet_rel *r = 0;
    if (len > SDENET_REL_DATA || cmdlen > sizeof r->cmd) return 0;
    for (i = 0; i < SDENET_REL_SLOTS; i++) if (!P->rel[i].used) { r = &P->rel[i]; break; }
    if (!r) { h->st.rel_full++; return 0; }
    zero(r, sizeof *r);
    r->used = 1; r->channel = channel; r->cmdlen = (sde_u8)cmdlen; r->datalen = (sde_u16)len;
    if (channel == 0xFF) r->seq = ++P->out_rel_seq;
    else r->seq = ++P->ch_out_rel[channel];
    r->cmd[0] = (sde_u8)(cmd | SDE_FLAG_ACK);
    r->cmd[1] = channel;
    put16(r->cmd + 2, r->seq);
    if (data) copy(r->data, data, len);
    r->order = ++h->order;
    return r;
}

static int rel_used(const struct sdenet_peer *P)
{
    int i, n = 0;
    for (i = 0; i < SDENET_REL_SLOTS; i++) n += P->rel[i].used;
    return n;
}

/* remove the outgoing reliable command acknowledged by (seq, channel); returns its command number or 0 */
static int rel_remove(struct sdenet_peer *P, sde_u32 seq, sde_u8 channel)
{
    int i, c;
    for (i = 0; i < SDENET_REL_SLOTS; i++) {
        struct sdenet_rel *r = &P->rel[i];
        if (r->used && r->seq == seq && r->channel == channel) {
            c = r->cmd[0] & 0x0F;
            r->used = 0;
            return c;
        }
    }
    return 0;
}

/* ---- datagram builder ---- */
static void tx_begin(struct sdenet_host *h) { h->txlen = 4; h->txcmds = 0; h->txflags = 0; }

static int tx_room(const struct sdenet_host *h, const struct sdenet_peer *P, sde_u32 n)
{
    sde_u32 mtu = P->mtu ? P->mtu : SDENET_MTU;
    if (mtu > SDENET_MTU) mtu = SDENET_MTU;
    return h->txcmds < MAX_CMDS && h->txlen + n <= mtu;
}

static void tx_add(struct sdenet_host *h, const sde_u8 *p, sde_u32 n) { copy(h->tx + h->txlen, p, n); h->txlen += n; }

static void tx_finish(struct sdenet_host *h, struct sdenet_peer *P)
{
    sde_u32 id = P->out_peer_id, len = h->txlen, i;
    if (h->txcmds == 0) return;
    if (id < SDE_PEER_ID_NONE) id |= ((sde_u32)P->out_session << SDE_HDR_SESSION_SHIFT) & SDE_HDR_SESSION_MASK;
    id |= h->txflags;
    put16(h->tx, id);
    if (h->txflags & SDE_HDR_SENT_TIME) put16(h->tx + 2, h->now & 0xFFFFu);
    else { for (i = 4; i < len; i++) h->tx[i - 2] = h->tx[i]; len -= 2; }
    if (h->ops->send(h->ctx, P->ip, P->port, h->tx, len) >= 0) h->st.tx_dgrams++;
    else h->st.tx_fail++;
    h->txcmds = 0;
}

static void tx_acks(struct sdenet_host *h, struct sdenet_peer *P)
{
    sde_u8 c[8];
    sde_u32 i, k = 0;
    for (i = 0; i < P->nacks; i++) {
        if (!tx_room(h, P, 8)) { P->acks[k++] = P->acks[i]; continue; }   /* keep for the next datagram */
        c[0] = SDE_CMD_ACK; c[1] = P->acks[i].channel;
        put16(c + 2, P->acks[i].seq);
        put16(c + 4, P->acks[i].seq);
        put16(c + 6, P->acks[i].sent_time);
        tx_add(h, c, 8);
        h->txcmds++;
        h->st.tx_acks++;
    }
    P->nacks = k;
}

/* A new reliable command is held back while it is SDENET_HOLD_SLOTS or more sequence numbers ahead of the oldest
   unacknowledged one of its channel: a receiver with our hold buffer can then always keep (and acknowledge) it, so a
   lost command never makes the receiver drop the ones behind it (which would back them off into a timeout). */
static int span_blocked(const struct sdenet_peer *P, const struct sdenet_rel *r)
{
    int i;
    for (i = 0; i < SDENET_REL_SLOTS; i++) {
        const struct sdenet_rel *o = &P->rel[i];
        if (o->used && o->channel == r->channel && o->sends && (sde_u16)(r->seq - o->seq) >= SDENET_HOLD_SLOTS &&
            (sde_u16)(r->seq - o->seq) < 0x8000u) return 1;
    }
    return 0;
}

/* add due reliable commands in queue order; only_new: never-sent ones only (piggyback on an unsequenced send) */
static void tx_rel(struct sdenet_host *h, struct sdenet_peer *P, int only_new, sde_u32 reserve)
{
    for (;;) {
        struct sdenet_rel *best = 0;
        int i;
        for (i = 0; i < SDENET_REL_SLOTS; i++) {
            struct sdenet_rel *r = &P->rel[i];
            if (!r->used || !(r->sends == 0 || (!only_new && r->retx))) continue;
            if (r->sends == 0 && r->channel != 0xFF && span_blocked(P, r)) continue;
            if (!best || before(r->order, best->order)) best = r;
        }
        if (!best || !tx_room(h, P, best->cmdlen + best->datalen + reserve)) return;
        if (best->sends == 0) {
            best->rto = P->rtt + 4u * P->rtt_var;
            best->rto_limit = TIMEOUT_LIMIT * best->rto;
            h->st.tx_rel++;
        } else h->st.tx_retx++;
        best->retx = 0;
        best->sends++;
        best->sent_ms = h->now;
        tx_add(h, best->cmd, best->cmdlen);
        if (best->datalen) tx_add(h, best->data, best->datalen);
        h->txcmds++;
        h->txflags |= SDE_HDR_SENT_TIME;
    }
}

static void flush_peer(struct sdenet_host *h, struct sdenet_peer *P)
{
    int guard;
    for (guard = 0; guard < 8; guard++) {
        tx_begin(h);
        tx_acks(h, P);
        tx_rel(h, P, 0, 0);
        if (!h->txcmds) return;
        tx_finish(h, P);
    }
}

/* ---- public API ---- */
int sdenet_connect(struct sdenet_host *h, sde_u32 ip, sde_u16 port, sde_u8 channels, sde_u32 data, sde_u32 now)
{
    int i;
    struct sdenet_peer *P = 0;
    struct sdenet_rel *r;
    if (channels < 1) channels = 1;
    if (channels > SDENET_CHANNELS) channels = SDENET_CHANNELS;
    for (i = 0; i < SDENET_MAX_PEERS; i++) if (h->peer[i].state == SDE_FREE) { P = &h->peer[i]; break; }
    if (!P) return -1;
    now = mono(h, now);
    h->now = now;
    peer_clear(P);
    P->state = SDE_CONNECTING; P->initiator = 1;
    P->ip = ip; P->port = port; P->channels = channels; P->mtu = SDENET_MTU;
    h->seed = h->seed * 1664525u + 1013904223u;
    P->connect_id = h->seed ^ (now << 16) ^ (sde_u32)i;
    P->connect_data = data;
    P->throttle[0] = 5000; P->throttle[1] = 2; P->throttle[2] = 2;
    P->last_ack_ms = now;
    r = rel_alloc(h, P, 0xFF, SDE_CMD_CONNECT, 48, 0, 0);
    if (!r) { peer_clear(P); return -1; }
    put16(r->cmd + 4, (sde_u32)i);                 /* our incoming peer id = slot index */
    r->cmd[6] = P->in_session; r->cmd[7] = P->out_session;
    put32(r->cmd + 8, SDENET_MTU);
    put32(r->cmd + 12, 65536u);                    /* window: unlimited bandwidth */
    put32(r->cmd + 16, channels);
    put32(r->cmd + 20, 0); put32(r->cmd + 24, 0);
    put32(r->cmd + 28, P->throttle[0]); put32(r->cmd + 32, P->throttle[1]); put32(r->cmd + 36, P->throttle[2]);
    put32(r->cmd + 40, P->connect_id);
    put32(r->cmd + 44, data);
    h->st.connects_out++;
    return i;
}

int sdenet_send(struct sdenet_host *h, int p, sde_u8 channel, int flags, const sde_u8 *data, sde_u32 len)
{
    struct sdenet_peer *P;
    if (p < 0 || p >= SDENET_MAX_PEERS) return -1;
    P = &h->peer[p];
    if (P->state != SDE_CONNECTED || channel >= P->channels) return -2;
    if ((flags & SDENET_RELIABLE) && len > SDENET_REL_DATA && FRAG_ON(h)) {   /* ENet SEND_FRAGMENT, as enet_peer_send;
                                                                     only with fragments enabled (else -3 like the base) */
        sde_u32 count = (len + SDENET_REL_DATA - 1) / SDENET_REL_DATA, k, off, start;
        if (count > SDENET_FRAG_COUNT_MAX || count > (sde_u32)(SDENET_REL_SLOTS - rel_used(P))) { h->st.rel_full++; return -3; }
        start = (sde_u16)(P->ch_out_rel[channel] + 1);      /* the first fragment's own sequence number */
        for (k = 0, off = 0; k < count; k++, off += SDENET_REL_DATA) {
            sde_u32 n = len - off < SDENET_REL_DATA ? len - off : SDENET_REL_DATA;
            struct sdenet_rel *r = rel_alloc(h, P, channel, SDE_CMD_SEND_FRAGMENT, 24, data + off, n);
            if (!r) return -3;                               /* cannot happen: the slots were counted above */
            put16(r->cmd + 4, start);
            put16(r->cmd + 6, n);
            put32(r->cmd + 8, count);
            put32(r->cmd + 12, k);
            put32(r->cmd + 16, len);
            put32(r->cmd + 20, off);
            h->st.tx_frag++;
        }
        return 0;
    }
    if (flags & SDENET_RELIABLE) {
        struct sdenet_rel *r = rel_alloc(h, P, channel, SDE_CMD_SEND_RELIABLE, 6, data, len);
        if (!r) return -3;
        put16(r->cmd + 4, len);
        return 0;
    }
    /* unsequenced: one datagram now, with pending acks and new reliable commands in front */
    {
        sde_u8 c[8];
        sde_u32 mtu = P->mtu < SDENET_MTU ? P->mtu : SDENET_MTU;
        if (len + 8 + 4 > mtu) return -4;
        tx_begin(h);
        tx_acks(h, P);
        tx_rel(h, P, 1, 8 + len);
        if (!tx_room(h, P, 8 + len)) { tx_finish(h, P); tx_begin(h); }
        P->out_unseq_group++;
        c[0] = SDE_CMD_SEND_UNSEQUENCED | SDE_FLAG_UNSEQ; c[1] = channel;
        put16(c + 2, 0);
        put16(c + 4, P->out_unseq_group);
        put16(c + 6, len);
        tx_add(h, c, 8);
        tx_add(h, data, len);
        h->txcmds++;
        h->st.tx_unseq++;
        tx_finish(h, P);
    }
    return 0;
}

void sdenet_disconnect(struct sdenet_host *h, int p, sde_u32 data)
{
    struct sdenet_peer *P;
    if (p < 0 || p >= SDENET_MAX_PEERS) return;
    P = &h->peer[p];
    if (P->state == SDE_FREE || P->state == SDE_DISCONNECTING) return;
    if (P->state == SDE_CONNECTED) {
        struct sdenet_rel *r;
        int i;
        for (i = 0; i < SDENET_REL_SLOTS; i++) P->rel[i].used = 0;    /* like enet_peer_reset_queues */
        P->nacks = 0;
        r = rel_alloc(h, P, 0xFF, SDE_CMD_DISCONNECT, 8, 0, 0);
        if (r) { put32(r->cmd + 4, data); P->state = SDE_DISCONNECTING; flush_peer(h, P); return; }
    }
    {   /* not connected: unsequenced DISCONNECT, then forget the peer */
        sde_u8 c[8];
        tx_begin(h);
        c[0] = SDE_CMD_DISCONNECT | SDE_FLAG_UNSEQ; c[1] = 0xFF;
        put16(c + 2, ++P->out_rel_seq);
        put32(c + 4, data);
        tx_add(h, c, 8);
        h->txcmds++;
        tx_finish(h, P);
        peer_clear(P);
    }
}

/* ---- receive ---- */
static void queue_ack(struct sdenet_host *h, struct sdenet_peer *P, sde_u8 cmd, sde_u8 channel, sde_u32 seq, sde_u32 sent_time)
{
    if (channel < P->channels) {      /* ENet does not acknowledge commands just past the free reliable windows */
        sde_u32 rw = seq / REL_WINDOW, cw = P->ch_in_rel[channel] / REL_WINDOW;
        if (seq < P->ch_in_rel[channel]) rw += REL_WINDOWS;
        if (rw >= cw + REL_FREE_WINDOWS - 1 && rw <= cw + REL_FREE_WINDOWS) return;
    }
    if (P->nacks >= SDENET_ACK_SLOTS) { h->st.ack_full++; return; }
    P->acks[P->nacks].channel = channel;
    P->acks[P->nacks].cmd = cmd;
    P->acks[P->nacks].seq = (sde_u16)seq;
    P->acks[P->nacks].sent_time = (sde_u16)sent_time;
    P->nacks++;
}

static void handle_ack(struct sdenet_host *h, int p, const sde_u8 *c, int *stop)
{
    struct sdenet_peer *P = &h->peer[p];
    sde_u32 seq = get16(c + 4), st = get16(c + 6), r;
    int cmd;
    st |= h->now & 0xFFFF0000u;
    if ((st & 0x8000u) > (h->now & 0x8000u)) st -= 0x10000u;
    if (before(h->now, st)) return;
    cmd = rel_remove(P, seq, c[1]);
    if (cmd) {                       /* only an ACK that matches a command in flight counts (forged ACKs must not move the RTT) */
        h->st.rx_acks++;
        P->last_ack_ms = h->now;
        P->earliest_timeout = 0;
        r = h->now - st;
        P->rtt_var -= P->rtt_var / 4;
        if (r >= P->rtt) { P->rtt += (r - P->rtt) / 8; P->rtt_var += (r - P->rtt) / 4; }
        else { P->rtt -= (P->rtt - r) / 8; P->rtt_var += (P->rtt - r) / 4; }
    } else h->st.rx_ack_nomatch++;
    if (P->state == SDE_ACK_CONNECT) {
        if (cmd != SDE_CMD_VERIFY_CONNECT) { *stop = 1; return; }
        P->state = SDE_CONNECTED;
        if (h->ops->on_connect) h->ops->on_connect(h->ctx, p);
    } else if (P->state == SDE_DISCONNECTING) {
        if (cmd != SDE_CMD_DISCONNECT) { *stop = 1; return; }
        drop_peer(h, p, P->disc_data, SDE_DISC_LOCAL);
        *stop = 1;
    }
}

static sde_u32 clamp(sde_u32 v, sde_u32 lo, sde_u32 hi) { return v < lo ? lo : v > hi ? hi : v; }

static int handle_connect(struct sdenet_host *h, sde_u32 ip, sde_u16 port, const sde_u8 *c)
{
    sde_u32 nch = get32(c + 16), cid = get32(c + 40), win, s;
    int i, p = -1;
    struct sdenet_peer *P;
    struct sdenet_rel *r;
    if (nch < 1 || nch > 255) return -1;
#ifdef SDENET_ALLOW_POOL          /* CARRIER POOL: the engine builds define it; the canonical default and the copies it is compared with keep the exact address */
    if (h->allow_on && (((ip ^ h->allow_ip) & 0xFFFFFF00u) || (h->allow_port && port != h->allow_port))) { h->st.rx_drop_allow++; h->st.last_drop_ip = ip; h->st.last_drop_port = port; return -1; }
#else
    if (h->allow_on && (ip != h->allow_ip || (h->allow_port && port != h->allow_port))) { h->st.rx_drop_allow++; h->st.last_drop_ip = ip; h->st.last_drop_port = port; return -1; }
#endif
    if (h->allow_on && ip != h->allow_ip) { h->st.rx_accept_pool++; h->st.last_drop_ip = ip; h->st.last_drop_port = port; }   /* CARRIER POOL: the same /24 (a carrier NAT pool); the peer is answered at its real source.  last_drop_* names it for the record */
    for (i = 0; i < SDENET_MAX_PEERS; i++) {
        struct sdenet_peer *q = &h->peer[i];
        if (q->state == SDE_FREE) { if (p < 0) p = i; }
        else if (q->state != SDE_CONNECTING && q->ip == ip && q->port == port && q->connect_id == cid) return -1;  /* retransmit */
    }
    if (p < 0) { h->st.rx_drop_peer++; return -1; }
    P = &h->peer[p];
    peer_clear(P);
    P->state = SDE_ACK_CONNECT;
    P->ip = ip; P->port = port;
    P->channels = (sde_u8)(nch > SDENET_CHANNELS ? SDENET_CHANNELS : nch);
    P->connect_id = cid;
    P->out_peer_id = (sde_u16)get16(c + 4);
    P->throttle[0] = get32(c + 28); P->throttle[1] = get32(c + 32); P->throttle[2] = get32(c + 36);
    P->connect_data = get32(c + 44);
    /* session ids: pick values different from the previous ones of this slot (ENet's rule) */
    s = c[6] == 0xFF ? P->out_session : c[6];
    s = (s + 1) & 3u; if (s == P->out_session) s = (s + 1) & 3u;
    P->out_session = (sde_u8)s;
    s = c[7] == 0xFF ? P->in_session : c[7];
    s = (s + 1) & 3u; if (s == P->in_session) s = (s + 1) & 3u;
    P->in_session = (sde_u8)s;
    P->mtu = (sde_u16)clamp(get32(c + 8), 576, 4096);
    if (P->mtu > SDENET_MTU) P->mtu = SDENET_MTU;
    win = get32(c + 12); if (win > 65536u) win = 65536u;
    win = clamp(win, 4096, 65536);
    P->last_ack_ms = h->now;
    r = rel_alloc(h, P, 0xFF, SDE_CMD_VERIFY_CONNECT, 44, 0, 0);
    if (!r) { peer_clear(P); return -1; }
    put16(r->cmd + 4, (sde_u32)p);
    r->cmd[6] = P->out_session; r->cmd[7] = P->in_session;
    put32(r->cmd + 8, P->mtu);
    put32(r->cmd + 12, win);
    put32(r->cmd + 16, P->channels);
    put32(r->cmd + 20, 0); put32(r->cmd + 24, 0);
    put32(r->cmd + 28, P->throttle[0]); put32(r->cmd + 32, P->throttle[1]); put32(r->cmd + 36, P->throttle[2]);
    put32(r->cmd + 40, cid);
    h->st.connects_in++;
    return p;
}

static int handle_verify(struct sdenet_host *h, int p, const sde_u8 *c)
{
    struct sdenet_peer *P = &h->peer[p];
    sde_u32 nch = get32(c + 16), mtu;
    if (P->state != SDE_CONNECTING) return 0;
    if (nch < 1 || nch > 255 || get32(c + 28) != P->throttle[0] || get32(c + 32) != P->throttle[1] ||
        get32(c + 36) != P->throttle[2] || get32(c + 40) != P->connect_id) {
        drop_peer(h, p, 0, SDE_DISC_REFUSED);
        return -1;
    }
    rel_remove(P, 1, 0xFF);
    if (nch < P->channels) P->channels = (sde_u8)nch;
    P->out_peer_id = (sde_u16)(get16(c + 4) & 0xFFFu);
    P->in_session = c[6];
    P->out_session = c[7];
    mtu = clamp(get32(c + 8), 576, 4096);
    if (mtu < P->mtu) P->mtu = (sde_u16)mtu;
    P->state = SDE_CONNECTED;
    P->last_ack_ms = h->now;
    if (h->ops->on_connect) h->ops->on_connect(h->ctx, p);
    return 0;
}

static int release_held(struct sdenet_host *h, int p, sde_u8 ch);

/* returns 1 if the command should be acknowledged */
static int handle_reliable(struct sdenet_host *h, int p, const sde_u8 *c, const sde_u8 *data, sde_u32 len)
{
    struct sdenet_peer *P = &h->peer[p];
    sde_u8 ch = c[1];
    sde_u32 seq = get16(c + 2), in = P->ch_in_rel[ch], rw = seq / REL_WINDOW, cw = in / REL_WINDOW, d;
    int i, free_slot = -1;
    if (seq < in) rw += REL_WINDOWS;
    if (rw < cw || rw >= cw + REL_FREE_WINDOWS - 1) { h->st.rx_rel_dup++; return 1; }   /* ENet discards, still acks */
    d = (seq - in) & 0xFFFFu;
    if (d == 0 || d >= 0x8000u) { h->st.rx_rel_dup++; return 1; }
    if (d > 1) {                     /* future command: hold it if there is room, else let the sender retransmit */
        for (i = 0; i < SDENET_HOLD_SLOTS; i++) {
            struct sdenet_hold *x = &P->hold[i];
            if (x->used && x->channel == ch && x->seq == seq) { h->st.rx_rel_dup++; return 1; }
            if (!x->used && free_slot < 0) free_slot = i;
        }
        if (free_slot < 0 || len > SDENET_REL_DATA) { h->st.rx_rel_ooo++; return 0; }
        P->hold[free_slot].used = 1; P->hold[free_slot].channel = ch; P->hold[free_slot].seq = (sde_u16)seq;
        P->hold[free_slot].len = (sde_u16)len;
        copy(P->hold[free_slot].data, data, len);
        h->st.rx_rel_held++;
        return 1;
    }
    P->ch_in_rel[ch] = (sde_u16)seq;
    h->st.rx_rel++;
    if (h->ops->on_receive) h->ops->on_receive(h->ctx, p, ch, data, len);
    return release_held(h, p, ch);
}

/* the held-command loop of handle_reliable as a function (also run after a reassembled packet) */
static int release_held(struct sdenet_host *h, int p, sde_u8 ch)
{
    struct sdenet_peer *P = &h->peer[p];
    int i;
    for (;;) {                       /* release held commands that are now in order */
        int found = 0;
        if (P->state != SDE_CONNECTED) return 0;           /* a callback dropped the peer */
        for (i = 0; i < SDENET_HOLD_SLOTS; i++) {
            struct sdenet_hold *x = &P->hold[i];
            if (x->used && x->channel == ch && x->seq == (sde_u16)(P->ch_in_rel[ch] + 1)) {
                x->used = 0;
                P->ch_in_rel[ch] = x->seq;
                h->st.rx_rel++;
                if (h->ops->on_receive) h->ops->on_receive(h->ctx, p, ch, x->data, x->len);
                found = 1;
                break;
            }
        }
        if (!found) return 1;
    }
}

static void handle_unseq(struct sdenet_host *h, int p, const sde_u8 *c, const sde_u8 *data, sde_u32 len)
{
    struct sdenet_peer *P = &h->peer[p];
    sde_u32 g = get16(c + 4), idx = g % UNSEQ_WINDOW, i;
    if (g < P->in_unseq_group) g += 0x10000u;
    if (g >= (sde_u32)P->in_unseq_group + UNSEQ_FREE_WINDOWS * UNSEQ_WINDOW) { h->st.rx_unseq_dup++; return; }
    g &= 0xFFFFu;
    if (g - idx != P->in_unseq_group) {
        P->in_unseq_group = (sde_u16)(g - idx);
        for (i = 0; i < UNSEQ_WINDOW / 32; i++) P->unseq_window[i] = 0;
    } else if (P->unseq_window[idx / 32] & (1u << (idx % 32))) { h->st.rx_unseq_dup++; return; }
    P->unseq_window[idx / 32] |= 1u << (idx % 32);
    h->st.rx_unseq++;
    if (h->ops->on_receive) h->ops->on_receive(h->ctx, p, c[1], data, len);
}

#ifndef SDENET_NO_FRAGMENTS
/* one reliable fragment (ENet SEND_FRAGMENT: +4 startSequenceNumber u16, +6 dataLength u16, +8 fragmentCount,
   +12 fragmentNumber, +16 totalLength, +20 fragmentOffset, all u32; the receiver rules of libenet's
   enet_protocol_handle_send_fragment, behaviour only).  Returns 1 if the fragment must be acknowledged. */
static int handle_fragment(struct sdenet_host *h, int p, const sde_u8 *c, const sde_u8 *data, sde_u32 len)
{
    struct sdenet_peer *P = &h->peer[p];
    struct sdenet_frag *F = &h->frag;
    sde_u8 ch = c[1];
    sde_u32 start = get16(c + 4), in = P->ch_in_rel[ch], rw = start / REL_WINDOW, cw = in / REL_WINDOW, d;
    sde_u32 count = get32(c + 8), num = get32(c + 12), total = get32(c + 16), off = get32(c + 20), i;
    if (!F->buf) { h->st.rx_drop_frag++; return 0; }                    /* no buffer: the original behaviour */
    if (start < in) rw += REL_WINDOWS;
    if (rw < cw || rw >= cw + REL_FREE_WINDOWS - 1) { h->st.rx_rel_dup++; return 1; }   /* ENet ignores, still acks */
    d = (start - in) & 0xFFFFu;
    if (d == 0 || d >= 0x8000u) { h->st.rx_rel_dup++; return 1; }    /* a packet already delivered */
    if (d > 1 || (F->active && (F->peer != p || F->channel != ch))) { h->st.rx_frag_later++; return 0; }
    if (count < 1 || count > SDENET_FRAG_COUNT_MAX || num >= count || total > F->cap || off >= total || len > total - off ||
        (F->active && (F->count != count || F->total != total))) {
        h->st.rx_frag_bad++;                                             /* libenet drops the datagram as malformed */
        return 0;
    }
    if (!F->active) {
        F->active = 1; F->peer = p; F->channel = ch; F->start = (sde_u16)start;
        F->count = count; F->total = total; F->have_n = 0; F->have[0] = F->have[1] = 0;
    }
    if (F->have[num / 32] & (1u << (num % 32))) { h->st.rx_rel_dup++; return 1; }
    F->have[num / 32] |= 1u << (num % 32);
    F->have_n++;
    for (i = 0; i < len; i++) F->buf[off + i] = data[i];
    h->st.rx_frag++;
    if (F->have_n < F->count) return 1;
    F->active = 0;                                                       /* complete: deliver in order */
    P->ch_in_rel[ch] = (sde_u16)(start + count - 1);
    h->st.rx_frag_done++;
    h->st.rx_rel++;
    if (h->ops->on_receive) h->ops->on_receive(h->ctx, p, ch, F->buf, total);
    if (P->state != SDE_CONNECTED) return 0;
    release_held(h, p, ch);
    return 1;
}
#endif

void sdenet_rx(struct sdenet_host *h, sde_u32 now, sde_u32 ip, sde_u16 port, const sde_u8 *d, sde_u32 len)
{
    sde_u32 pid, flags, session, off, sent_time = 0;
    int p = -1, stop = 0;
    struct sdenet_peer *P = 0;
    now = mono(h, now);
    h->now = now;
    h->st.rx_dgrams++;
    if (len < 2) { h->st.rx_bad++; return; }
    pid = get16(d);
    flags = pid & (SDE_HDR_SENT_TIME | SDE_HDR_COMPRESSED);
    session = (pid & SDE_HDR_SESSION_MASK) >> SDE_HDR_SESSION_SHIFT;
    pid &= 0x0FFFu;
    off = (flags & SDE_HDR_SENT_TIME) ? 4u : 2u;
    if (len < off) { h->st.rx_bad++; return; }
    if (off == 4) sent_time = get16(d + 2);
    if (flags & SDE_HDR_COMPRESSED) { h->st.rx_drop_compressed++; return; }
    if (pid != SDE_PEER_ID_NONE) {
        if (pid >= SDENET_MAX_PEERS) { h->st.rx_drop_peer++; return; }
        p = (int)pid; P = &h->peer[p];
        if (P->state == SDE_FREE || P->ip != ip || P->port != port) { h->st.rx_drop_peer++; return; }
        if (P->out_peer_id < SDE_PEER_ID_NONE && session != P->in_session) { h->st.rx_drop_session++; return; }
        P->last_rx_ms = now;
    }
    while (!stop && off + 4 <= len) {
        const sde_u8 *c = d + off;
        sde_u32 num = c[0] & 0x0Fu, dl = 0;
        int ack = 0;
        if (num == 0 || num >= SDE_CMD_COUNT || off + cmd_size[num] > len) break;
        off += cmd_size[num];
        if (p < 0 && num != SDE_CMD_CONNECT) break;
        h->st.rx_cmds++;
        if (num == SDE_CMD_SEND_RELIABLE) dl = get16(c + 4);
        else if (num == SDE_CMD_SEND_UNRELIABLE || num == SDE_CMD_SEND_UNSEQUENCED || num == SDE_CMD_SEND_FRAGMENT ||
                 num == SDE_CMD_SEND_UNRELIABLE_FRAGMENT) dl = get16(c + 6);
        if (dl > len - off) { h->st.rx_bad++; break; }
        switch (num) {
        case SDE_CMD_ACK:
            handle_ack(h, p, c, &stop);
            break;
        case SDE_CMD_CONNECT:
            if (p >= 0) { stop = 1; break; }
            p = handle_connect(h, ip, port, c);
            if (p < 0) { stop = 1; break; }
            P = &h->peer[p];
            break;
        case SDE_CMD_VERIFY_CONNECT:
            if (handle_verify(h, p, c)) { stop = 1; break; }
            ack = 1;
            break;
        case SDE_CMD_DISCONNECT:
            h->st.disc_rx++;
            if (P->state == SDE_CONNECTED && (c[0] & SDE_FLAG_ACK) && (flags & SDE_HDR_SENT_TIME)) {
                queue_ack(h, P, (sde_u8)num, 0xFF, get16(c + 2), sent_time);
                tx_begin(h); tx_acks(h, P); tx_finish(h, P);
            }
            drop_peer(h, p, get32(c + 4), SDE_DISC_REMOTE);
            stop = 1;
            break;
        case SDE_CMD_PING:
        case SDE_CMD_BANDWIDTH_LIMIT:
        case SDE_CMD_THROTTLE_CONFIGURE:
            if (P->state != SDE_CONNECTED) { stop = 1; break; }
            ack = 1;
            break;
        case SDE_CMD_SEND_RELIABLE:
            if (P->state != SDE_CONNECTED || c[1] >= P->channels) { stop = 1; break; }
            ack = handle_reliable(h, p, c, d + off, dl);
            break;
        case SDE_CMD_SEND_UNSEQUENCED:
            if (P->state != SDE_CONNECTED || c[1] >= P->channels) { stop = 1; break; }
            handle_unseq(h, p, c, d + off, dl);
            break;
        case SDE_CMD_SEND_UNRELIABLE:
            if (P->state != SDE_CONNECTED || c[1] >= P->channels) { stop = 1; break; }
            h->st.rx_unrel++;
            if (h->ops->on_receive) h->ops->on_receive(h->ctx, p, c[1], d + off, dl);
            break;
        case SDE_CMD_SEND_FRAGMENT:  /* reliable fragments (lab lobby replies with quick-chat texts) */
            if (!FRAG_ON(h)) { h->st.rx_drop_frag++; break; }      /* fragments off: exactly the base behaviour */
#ifndef SDENET_NO_FRAGMENTS
            if (P->state != SDE_CONNECTED || c[1] >= P->channels) { stop = 1; break; }
            ack = handle_fragment(h, p, c, d + off, dl);
#endif
            break;
        default:                     /* unreliable fragments: unsupported (Slippi payloads fit one datagram) */
            h->st.rx_drop_frag++;
            break;
        }
        off += dl;
        if (stop) break;
        if (P->state == SDE_FREE) break;                /* a callback dropped the peer */
        if (ack && (c[0] & SDE_FLAG_ACK) && (flags & SDE_HDR_SENT_TIME) &&
            P->state != SDE_ACK_CONNECT && P->state != SDE_DISCONNECTING)
            queue_ack(h, P, (sde_u8)num, c[1], get16(c + 2), sent_time);
    }
}

void sdenet_service(struct sdenet_host *h, sde_u32 now)
{
    int p, i;
    now = mono(h, now);
    h->now = now;
    for (p = 0; p < SDENET_MAX_PEERS; p++) {
        struct sdenet_peer *P = &h->peer[p];
        int dead = 0;
        if (P->state == SDE_FREE) continue;
        for (i = 0; i < SDENET_REL_SLOTS; i++) {           /* retransmission timeouts (ENet's check_timeouts) */
            struct sdenet_rel *r = &P->rel[i];
            if (!r->used || !r->sends || r->retx || (sde_u32)(now - r->sent_ms) < r->rto) continue;
            if (P->earliest_timeout == 0 || before(r->sent_ms, P->earliest_timeout)) P->earliest_timeout = r->sent_ms;
            if (P->earliest_timeout && ((sde_u32)(now - P->earliest_timeout) >= TIMEOUT_MAX ||
                                        (r->rto >= r->rto_limit && (sde_u32)(now - P->earliest_timeout) >= TIMEOUT_MIN))) {
                dead = 1;
                break;
            }
#ifdef SDENET_CONNECT_PUNCH     /* NAT hole punching (the pairing misses): the engine builds define it; the canonical default and the copies it is compared with keep the plain ENet backoff */
            if (P->state == SDE_CONNECTING && P->earliest_timeout && (sde_u32)(now - P->earliest_timeout) < CONNECT_PUNCH_MS) r->rto = CONNECT_PUNCH_RTO;     /* NAT hole punching (the pairing misses): both consoles' first CONNECTs often meet no open pinhole on a carrier-grade NAT, and ENet's 0.5 / 1 / 2 / 4 / 8 s doubling then sends too few packets for the two sides to overlap; keep trying every CONNECT_PUNCH_RTO for the first CONNECT_PUNCH_MS, then the normal backoff */
            else r->rto *= 2;
#else
            r->rto *= 2;
#endif
            r->retx = 1;
        }
        if (dead) { h->st.timeouts++; drop_peer(h, p, 0, SDE_DISC_TIMEOUT); continue; }
        if (P->state == SDE_CONNECTED && !rel_used(P) && (sde_u32)(now - P->last_ack_ms) >= PING_INTERVAL) {
            if (rel_alloc(h, P, 0xFF, SDE_CMD_PING, 4, 0, 0)) h->st.tx_pings++;
        }
        flush_peer(h, P);
    }
}
