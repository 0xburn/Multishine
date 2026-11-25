/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The entry code of ms/rep.ovl (rep_ovl.h): enter / frame / leave / busy on top of rep_glue.c and the TCP + TLS stack.  The resident image
   (rep_res.c) calls it, one `frame` per menu pass, inside the EXI lock / interrupts-off window it opens with env->net_begin (the same rule as the lab TLS probe: a TCP send from
   the menu thread outside that window is lost).  No allocation: the session block is this overlay's own bss.  Strict C99 here (the overlay's flags), no libc. */
#include "rep_glue.h"
#include "rep_ovl.h"
#include "sdtcp.h"
#include "sddns.h"
#include "sdent.h"
#include "rep_mm.h"

extern const struct sdx_anchor lucky_roots[];
extern const sdx_u32 lucky_nroots;
static const struct rep_ovl_env *E;
static struct sdent POOL;
/* ONE session block, two users that never overlap (a report waits until a search ends, the resident frees this block when a search starts on an idle online character screen): the report
   client's `struct rep_arena`, or the matchmaker session below (the same TLS session + records plus the WebSocket and message buffers). */
struct mm_arena {
    struct sdsec sec;
    struct sdtls_web web;
    rep_u8 rx[SDTLS_WEB_RX];
    rep_u8 wsrx[RM_WSRX], wstx[RM_WSTX], jtx[RM_JTX];
};
static union { struct rep_arena a; struct mm_arena m; } ARENA;
static struct rep_mm MM;
static struct rep_dns MD;                           /* the matchmaker's own lookup state on the shared resolver (the report client has its own in rep_glue.c) */
static int g_mm_up;
static int g_arena_out, g_entered, g_seed_state, g_seed_saved, g_frames;
static struct sdtcp *g_tcp;

/* the one libc function the compiler may emit for struct copies (the overlay may not import it) */
void *memcpy(void *d, const void *s, rep_u32 n)
{
    unsigned char *a = (unsigned char *)d;
    const unsigned char *b = (const unsigned char *)s;
    while (n--) *a++ = *b++;
    return d;
}

/* the resident sdnet functions the TCP code calls, through the environment (sdtcp.c); the symbols exist in the engine too, so the overlay carries these names itself */
sdnet_u32 sdnet_csum_add(sdnet_u32 sum, const sdnet_u8 *p, sdnet_u32 len) { return E->csum_add(sum, p, len); }
sdnet_u16 sdnet_csum_fold(sdnet_u32 sum) { return E->csum_fold(sum); }
int sdnet_tx_ip(struct sdnet *s, sdnet_u8 proto, sdnet_u32 dst_ip, sdnet_u16 plen) { return E->tx_ip(s, proto, dst_ip, plen); }

static void *o_alloc(rep_u32 n)
{
    if (g_arena_out || n > sizeof ARENA.a || MM.phase != RM_IDLE || MM.tp_started) return 0;      /* a report session never starts over a matchmaker session */
    g_arena_out = 1;
    return &ARENA;
}
static void o_release(void *p) { if (p == &ARENA) g_arena_out = 0; }
static rep_u32 o_ms(void) { return E->now_ms(); }
static int o_scene(void) { return E->scene_ok(); }
static int o_ident(char *uid, rep_u32 uc, char *key, rep_u32 kc) { return E->ident(uid, uc, key, kc); }
static int o_iso(char *out) { return E->iso_hash ? E->iso_hash(out) : 0; }
static rep_u32 o_dns_server(void) { return E->dns_server ? E->dns_server() : 0u; }
static int o_ready(void) { return sdent_ready(&POOL, SDENT_L_SESSION); }

static void zero(void *p, rep_u32 n) { volatile rep_u8 *b = (volatile rep_u8 *)p; rep_u32 i; for (i = 0; i < n; i++) b[i] = 0; }

/* the stored entropy seed of MultishineOptions (as the first-run flow reads it, sg_entry.c lk_seed): credited once, then REPLACED by a fresh domain-separated output of the pool,
   so a stored seed is never credited twice.  Until the options record is read (-1) we try again next frame; with no seed (0) the pool fills from the player's stick and buttons
   (sdent_feed_pad) and the report waits (held_ent). */
static void seed_step(void)
{
    unsigned char rec[SDENT_SEED_REC_LEN], seed[32];
    sdc_u32 bits = 0;
    int r;
    if (!E->seed) { g_seed_state = 2; return; }
    if (g_seed_state == 0) {
        r = E->seed(rec, 0);
        if (r < 0) return;
        g_seed_state = 1;
        if (r == 1 && sdent_seed_parse(rec, sizeof rec, seed, &bits) == 0) sdent_load_seed(&POOL, SDENT_STORED, seed, bits);
    } else if (g_seed_state == 1 && !g_seed_saved && sdent_ready(&POOL, SDENT_L_SESSION)) {
        g_seed_saved = 1;
        if (sdent_export_seed(&POOL, seed, &bits) == 0) {
            sdent_seed_pack(seed, bits, rec);
            (void)E->seed(rec, 1);
        }
    } else return;
    zero(seed, sizeof seed);
    zero(rec, sizeof rec);
}

/* ---- the matchmaker's bindings (struct rep_mm_env) over the overlay's TLS stack, the shared resolver, the netplay socket and the mailbox ---- */
static int m_ready(void *c) { (void)c; return sdent_ready(&POOL, SDENT_L_SESSION); }
static rep_u32 m_ms(void *c) { (void)c; return E->now_ms(); }
static int m_rand(void *c, unsigned char *o, rep_u32 n) { (void)c; return sdent_rand_session(&POOL, o, n); }
static int m_dns(void *c, const char *host, rep_u32 *ip, rep_u32 now)
{
    (void)c;
    if (!host) return rep_dns_step(&MD, 0, ip, now);
    return rep_dns_step(&MD, host, ip, now);
}
static void m_dns_cfg(void *c, rep_u32 server, rep_u32 port)
{
    (void)c;
    MD.server = server ? server : o_dns_server();
    MD.port = port ? port : 53u;
}
static int m_ident(void *c, struct sbr_ident *o) { (void)c; return E->ident_full && E->ident_full(o); }
static rep_u32 m_unix(void) { return rep_clock_unix(&E->keep->clk, E->now_ms()); }
static int m_tp_open(void *c, rep_u32 ip, unsigned short port, const char *host)
{
    struct mm_arena *a = &ARENA.m;
    rep_u32 t;
    (void)c;
    if (g_arena_out || !g_tcp) return -1;
    t = m_unix();
    if (!t) return -1;                                              /* no trusted time: the certificate dates cannot be checked */
    zero(&a->sec, sizeof a->sec); zero(&a->web, sizeof a->web);
    a->web.anchors = sdx_roots; a->web.nanchors = sdx_nroots; a->web.now = t; a->web.flags = 0;
    a->web.rx = a->rx; a->web.rx_cap = sizeof a->rx;
    return sdsec_open_web(&a->sec, g_tcp, ip, port, (sdtls_rand_fn)sdent_rand_session, &POOL, &a->web, host, SBRW_CONNECT_MS + SBRW_HANDSHAKE_MS) < 0 ? -1 : 0;
}
static rep_u32 g_mm_t_close;
static int m_tp_poll(void *c, int heavy)
{
    struct sdsec *s = &ARENA.m.sec;
    int st;
    (void)c;
    st = sdsec_poll(s, heavy);
    if (st == SDSEC_CLOSING && g_mm_t_close && (rep_u32)(E->now_ms() - g_mm_t_close) > 2000u) { sdtcp_abort(g_tcp, s->h); s->state = SDSEC_FAILED; st = SDSEC_FAILED; }   /* a peer that never answers our close */
    switch (st) {
    case SDSEC_TCP: return RM_TP_TCP;
    case SDSEC_TLS: return RM_TP_TLS;
    case SDSEC_UP: return RM_TP_UP;
    case SDSEC_FAILED: return RM_TP_FAILED;
    default: return RM_TP_IDLE;
    }
}
static int m_tp_write(void *c, const unsigned char *b, rep_u32 n) { (void)c; return sdsec_write(&ARENA.m.sec, b, n); }
static int m_tp_read(void *c, unsigned char *b, rep_u32 cap)
{
    int r;
    (void)c;
    r = sdsec_read(&ARENA.m.sec, b, cap);
    return r < 0 ? -1 : r;                                          /* EOF and ERR are both "closed" */
}
static void m_tp_close(void *c)
{
    struct sdsec *s = &ARENA.m.sec;
    (void)c;
    if (s->state == SDSEC_UP) { sdsec_close(s); g_mm_t_close = E->now_ms() ? E->now_ms() : 1u; }
    else if (s->state == SDSEC_TCP || s->state == SDSEC_TLS) { sdtcp_abort(g_tcp, s->h); s->state = SDSEC_FAILED; }
}
static int m_tp_busy(void *c)                                       /* 1 while the previous session still holds the TCP slot; wipes the session (traffic keys) when it is over */
{
    struct sdsec *s = &ARENA.m.sec;
    (void)c;
    if (s->state == SDSEC_IDLE) return 0;
    if (s->state == SDSEC_CLOSED || s->state == SDSEC_FAILED) { zero(s, sizeof *s); zero(&ARENA.m.web, sizeof ARENA.m.web); g_mm_t_close = 0; return 0; }
    return 1;
}
static int m_udp_send(void *c, rep_u32 ip, unsigned short port, const unsigned char *b, rep_u32 n) { (void)c; return E->udp_send ? E->udp_send(ip, port, b, n) : -1; }
static int m_udp_recv(void *c, unsigned char *b, rep_u32 cap, rep_u32 *ip, unsigned short *port) { (void)c; return E->udp_recv ? E->udp_recv(b, cap, ip, port) : -1; }

static void mm_bind(struct rep_mm_env *me)
{
    zero(me, sizeof *me);
    me->ctx = 0; me->ms = m_ms; me->rand = m_rand; me->dns = m_dns; me->dns_cfg = m_dns_cfg; me->ident = m_ident;
    me->tp_open = m_tp_open; me->tp_poll = m_tp_poll; me->tp_write = m_tp_write; me->tp_read = m_tp_read; me->tp_close = m_tp_close; me->tp_busy = m_tp_busy;
    me->udp_send = m_udp_send; me->udp_recv = m_udp_recv; me->ready = m_ready; me->mbx = E->mbx;
    me->wsrx = ARENA.m.wsrx; me->wstx = ARENA.m.wstx; me->jtx = ARENA.m.jtx;
}

static int ov_enter(const struct rep_ovl_env *e)
{
    struct rep_glue_host h;
    rep_u32 i;
    rep_u8 *b = (rep_u8 *)&h;
    unsigned char mix[8];
    if (e->abi != REP_OVL_ABI || e->sz_rep_ovl_env != (rep_u32)sizeof *e || e->sz_sdnet != (rep_u32)sizeof(struct sdnet) || e->sz_keep != (rep_u32)sizeof(struct rep_keep) ||
        e->sz_mbx != (rep_u32)sizeof(struct sbr_mbx) || !e->mbx || e->mbx->magic != SBR_MBX_MAGIC || e->mbx->ver != SBR_MBX_VER || e->mbx->size != (rep_u32)sizeof(struct sbr_mbx)) return 1;
    E = e;
    g_arena_out = 0; g_seed_state = 0; g_seed_saved = 0; g_frames = 0; g_tcp = 0;
    sdent_init(&POOL);
    {   /* uncredited, but unique per load: the time base and the clock (the credit comes from the stored seed or the stick) */
        rep_u32 t = e->tb(), m = e->now_ms();
        mix[0] = (unsigned char)(t >> 24); mix[1] = (unsigned char)(t >> 16); mix[2] = (unsigned char)(t >> 8); mix[3] = (unsigned char)t;
        mix[4] = (unsigned char)(m >> 24); mix[5] = (unsigned char)(m >> 16); mix[6] = (unsigned char)(m >> 8); mix[7] = (unsigned char)m;
        sdent_mix(&POOL, mix, sizeof mix);
    }
    for (i = 0; i < sizeof h; i++) b[i] = 0;
    h.net = e->net; h.dns = e->dns; h.dns_server = o_dns_server; h.now_ms = o_ms;
    h.rand = sdent_rand_session; h.rand_ctx = &POOL; h.entropy_ready = o_ready;
    h.scene_ok = o_scene; h.ident = o_ident; h.iso_hash = o_iso; h.alloc = o_alloc; h.release = o_release;
    h.anchors = lucky_roots; h.nanchors = lucky_nroots;         /* LOCAL FORK: luckystats.gg anchors; the matchmaker keeps sdx_roots */ h.build = e->build; h.platform = e->platform;
    h.keep = e->keep;
    rep_glue_init(&h);
    {
        struct rep_mm_env me;
        zero(&MD, sizeof MD);
        MD.d = e->dns; MD.server = 0; MD.port = 0;
        g_mm_t_close = 0;
        mm_bind(&me);
        rep_mm_init(&MM, &me);
        g_mm_up = 1;
    }
    g_entered = 1;
    return 0;
}

static void ov_frame(void)
{
    struct rep_pad rp;
    struct sdent_pad p;
    int heavy;
    if (!g_entered) return;
    g_frames++;
    E->pad(&rp);
    p.buttons = rp.buttons; p.sx = rp.sx; p.sy = rp.sy; p.cx = rp.cx; p.cy = rp.cy; p.tl = rp.tl; p.tr = rp.tr; p.err = rp.err;
    sdent_feed_pad(&POOL, &p, E->tb());
    seed_step();
    if (!g_tcp && sdent_ready(&POOL, SDENT_L_SESSION)) {          /* the TCP initial sequence numbers need a few bytes of the pool: only once it can serve a session */
        unsigned char s16[16];
        if (sdent_bytes(&POOL, SDENT_L_SESSION, s16, sizeof s16) == 0) {
            g_tcp = rep_glue_tcp_up(E->net, s16);
            zero(s16, sizeof s16);
            if (g_tcp) rep_glue_set_tcp(g_tcp);
        }
    }
    if (!g_tcp) {                                                   /* nothing can start without TCP; while the pool fills the glue only counts the held frames (it reaches no TCP code) */
        if (g_mm_up) rep_mm_frame(&MM);                             /* the matchmaker still answers its mailbox (it waits for the entropy gate by itself) */
        if (!sdent_ready(&POOL, SDENT_L_SESSION)) rep_glue_frame(0);
        return;
    }
    heavy = (g_frames & 3) == 0;                                    /* a crypto step of up to ~70 ms (interrupts are off inside the window): every fourth frame at most */
    if (!E->net_begin()) return;
    E->net_poll(E->net);
    if (g_mm_up) rep_mm_frame(&MM);
    if (!g_mm_up || !rep_mm_busy(&MM)) rep_glue_frame(heavy);       /* a pending report waits until the search is over */
    E->net_end();
}

static void ov_leave(void)
{
    struct sdnet *n;
    if (!g_entered) return;
    if (g_mm_up) { rep_mm_leave(&MM); g_mm_up = 0; }
    rep_glue_leave();                                               /* session aborted (RST), request and key buffers wiped, arena returned */
    n = E->net;
    if (n && g_tcp) { n->tcp_rx = 0; n->tcp_poll = 0; n->tcp = 0; } /* the stack's hooks into this block go away before the block does */
    g_tcp = 0;
    zero(&POOL, sizeof POOL);
    zero(&ARENA, sizeof ARENA);
    zero(&MM, sizeof MM);
    g_entered = 0;
}

static int ov_busy(void)
{
    const struct rep *r = rep_glue_state();
    return (r && rep_busy(r) ? 1 : 0) | (g_mm_up && rep_mm_busy(&MM) ? 2 : 0);
}

static const struct rep *ov_state(void) { return rep_glue_state(); }

const struct rep_ovl_api rep_api = { REP_OVL_ABI, ov_enter, ov_frame, ov_leave, ov_busy, ov_state, { 0, 0, 0, 0, 0, 0 } };
