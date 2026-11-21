/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* od_glue: see od_glue.h and.  Strict C89, no libc, no heap, no globals, never blocks. */
#include "od_glue.h"
#include "sddns.h"          /* the DNS client (offline/net): in the combined image it is the bridge's, already linked */

#define OD_FIND_MS_DEFAULT 60000u
#define OD_CONNECT_MS_DEFAULT 20000u
#define OD_NET_MS_DEFAULT 20000u
#define OD_DEFAULT_PORT 43150u

extern void ms_sha256(const unsigned char *data, unsigned int n, unsigned char out[32]);   /* offline/payload_pack/ms_sha256.c */

/* Hub overlay split (menus/hub/hub_ovl.h): one source, three builds.  Neither macro = everything (the flat image, host tests:
   byte for byte the code before the split).  HUB_OVL_RES = the resident remainder (hand-off, inbox, B3 answer, the error texts); HUB_OVL = the overlay part (search,
   reports, server name lookup, config, the mnc client calls).  Every function is in exactly one of the two, except the few small helpers both need (own static copies). */
#if defined(HUB_OVL)
#define OD_RES 0
#define OD_OVL 1
#elif defined(HUB_OVL_RES)
#define OD_RES 1
#define OD_OVL 0
#else
#define OD_RES 1
#define OD_OVL 1
#endif
#ifdef HUB_OVL_RES
extern void hub_set_net(struct od *o);   /* hub_res.c: mnc_set_net through the overlay when it is loaded; else at its next enter */
#endif

#if OD_RES
static void od_zero(void *p, unsigned int n)
{
    volatile unsigned char *b = (volatile unsigned char *)p;
    while (n--) *b++ = 0;
}
#endif
#if OD_OVL
static void od_scopy(char *d, unsigned int cap, const char *s)
{
    unsigned int i = 0;
    while (s[i] && i + 1 < cap) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static unsigned int od_slen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}
#endif

/* ---- mnc io ---------------------------------------------------------------------------------------------------------------------- */
#if OD_RES
#ifdef HUB_OVL_RES
#define OD_IO_STATIC                 /* the overlay's mnc client gets these two through struct hub_env (hub_res.c) */
#else
#define OD_IO_STATIC static
#endif
OD_IO_STATIC int od_send_cb(void *ctx, const mnc_u8 *p, mnc_u32 len)
{
    struct od *o = (struct od *)ctx;
    if (!o->send || !o->net_up) return -1;
    return o->send(o->send_ctx, p, len);
}
OD_IO_STATIC int od_recv_cb(void *ctx, mnc_u8 *buf, mnc_u32 cap)
{
    struct od *o = (struct od *)ctx;
    unsigned int n, i;
    if (!o->in_n) return 0;
    n = o->in_len[o->in_head];
    if (n > cap) n = cap;
    for (i = 0; i < n; i++) buf[i] = o->in_buf[o->in_head][i];
    o->in_head = (o->in_head + 1u) % OD_INBOX;
    o->in_n--;
    return (int)n;
}
#endif

static void od_pub(struct od *o)
{
    struct od_status *s = o->st;
    if (!s) return;
    s->phase = (unsigned int)o->phase;
    s->err = (unsigned int)o->err;
    s->mnc_state = (unsigned int)o->m.state;
    s->mnc_err = (unsigned int)o->m.err;
    s->mnc_status = (unsigned int)o->m.status;
    s->peer_ip = o->peer_ip; s->peer_port = o->peer_port;
    s->side = (unsigned int)o->side; s->my_slot = (unsigned int)o->my_slot;
    s->st_sent = o->m.st_sent; s->st_resent = o->m.st_resent; s->st_rx = o->m.st_rx; s->st_ignored = o->m.st_ignored; s->st_bad = o->m.st_bad;
    s->net_ip = o->own_ip;
    s->core = (unsigned int)o->core_state;
    s->cfg_why = (unsigned int)o->cfg_why;
    s->rep_try = o->rep_tries;
    s->srv_ip = o->srv_ip;
    s->rt_n = o->rt_n; s->rt_why = o->rt_why; s->on_bk = o->on_bk;
}

static void od_fail(struct od *o, int err)
{
    o->phase = OD_ERR;
    o->err = err;
    od_pub(o);
}

#if OD_OVL
int od_host_ok(const char *s)
{
    unsigned int i, n = 0, lab = 0;
    char prev = '.';
    if (!s) return 0;
    while (s[n] && n < OD_HOST_MAX) n++;
    if (n == 0 || n >= OD_HOST_MAX) return 0;                      /* 1..47 characters, NUL inside the field */
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '.') {
            if (prev == '.' || prev == '-' || lab == 0) return 0;  /* empty label, or a label ending in a hyphen */
            lab = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            if (c == '-' && prev == '.') return 0;                 /* a label starting with a hyphen */
            if (++lab > 63u) return 0;
        } else return 0;
        prev = c;
    }
    return prev != '.' && prev != '-';
}
#endif

unsigned int od_server_ip(const struct od *o) { return o->srv_ip; }

void od_set_dns(struct od *o, struct sddns *d, unsigned int dhcp_dns) { o->dns = d; o->dhcp_dns = dhcp_dns; }
void od_set_backup(struct od *o, unsigned int ip) { o->bk_ip = ip; }

#if OD_OVL
/* DNS fallback: a release build patches the address AND the name (play.example.org).  The address stays first, exactly as before; when it does not
   answer OD_BK_AFTER times in a row and there is no backup address, the name is resolved (the DHCP resolver, then 1.1.1.1) and used like a backup server.  The pinned key still decides: an answer
   that is not our server times out like a dead backup and the console swaps back to the address.  A resolved address that answered stays for the session (OD_BK_PROVEN), so the other hub requests
   (friends, updates) follow it.  A failed lookup ends the search with today's "Cannot reach the server" and the address in force again. */
#define OD_BK_IP 1u                                   /* on_bk: the backup address (card record +0xA8) */
#define OD_BK_NAME 2u                                 /* on_bk: the patched name, resolved */
#define OD_BK_PROVEN 4u                               /* with OD_BK_NAME: the resolved address answered in this session */
static int od_name_fb(const struct od *o)
{
    return !o->bk_ip && o->cfg->server_ip && (o->cfg->flags & OD_CFGF_HOST) && o->dns && od_host_ok(o->cfg->host);
}
#ifdef MS_LAB_DNS_PRIVATE_OK
#define OD_FB_ANY_ANSWER 1                            /* LAB ONLY (the Dolphin proof, an internal design note): the lab server lives on a private network; never in a release build */
#else
#define OD_FB_ANY_ANSWER 0
#endif
static int od_ip_public(unsigned int ip)
{
    unsigned int a = ip >> 24, b = (ip >> 16) & 255u;
    if (a == 0 || a == 10 || a == 127 || a >= 224) return 0;
    if ((a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127)) return 0;
    return 1;
}
/* the primary's address is looked up again (its name may point elsewhere now) */
static void od_rediscover(struct od *o)
{
    o->srv_ip = o->cfg->server_ip; o->dns_state = OD_DNS_NONE;
    if (o->use_host && o->dns) sddns_cache_flush(o->dns);
    if (o->net_up) mnc_set_net(&o->m, o->own_ip, o->mask, o->srv_ip, OD_LAXV(o->cfg->flags));
}
/* primary <-> backup: a new session is needed with the other server (same pinned key) */
static void od_swap(struct od *o)
{
    o->fails = 0;
    mw2_drop(&o->m.w);
    if (o->on_bk) { o->on_bk = 0; od_rediscover(o); return; }
    if (!o->bk_ip) {                                   /* od_name_fb: the name, looked up at the next WAIT_NET tick (od_resolve_step) */
        o->on_bk = OD_BK_NAME; o->srv_ip = 0; o->dns_state = OD_DNS_NONE;
        sddns_cache_flush(o->dns);
        return;
    }
    o->on_bk = OD_BK_IP; o->srv_ip = o->bk_ip;
    if (o->net_up) mnc_set_net(&o->m, o->own_ip, o->mask, o->srv_ip, OD_LAXV(o->cfg->flags));
}

/* a failed search step that is worth another try (no answer, or a busy status): wait with exponential backoff, +-20% jitter, at least the server's hint; 1 = scheduled */
static int od_retry(struct od *o, unsigned int now_ms, unsigned int find_ms)
{
    int e = o->m.err, hint = mnc_busy_hint(o->m.status);
    unsigned int d;
    if (e == MNC_E_TIMEOUT) o->rt_why = 2;
    else if (e == MNC_E_SERVER && hint) o->rt_why = 1;
    else return 0;
    if (o->rt_n >= OD_RT_MAX) return 0;
    d = OD_RT_BASE_MS << o->rt_n;
    if (d > OD_RT_CAP_MS) d = OD_RT_CAP_MS;
    if (hint && d < (unsigned int)hint * 1000u) d = (unsigned int)hint * 1000u;
    d = mnc_jitter(&o->m, d);
    if (now_ms + d - o->t_req >= find_ms) return 0;                  /* the search window ends first: show the failure now */
    if (o->rt_why == 2) {
        if (++o->fails >= OD_BK_AFTER && (o->bk_ip || o->on_bk || od_name_fb(o))) od_swap(o);
        else if (o->use_host && !o->on_bk) od_rediscover(o);
    }
    o->rt_n++; o->rt_at = now_ms + d; o->phase = OD_WAIT_NET;
    if (o->st) { o->st->rt_wait_ms = d; o->st->rt_n = o->rt_n; }
    return 1;
}

#endif /* OD_OVL */

#if !defined(HUB_OVL) && !defined(HUB_OVL_RES)
int od_init(struct od *o, const struct od_cfg *cfg, struct od_status *st,
            int (*send)(void *ctx, const unsigned char *p, unsigned int len), void *send_ctx, unsigned int seed, unsigned int now_ms)
{
    mnc_io io;
    od_zero(o, sizeof *o);
    o->cfg = cfg; o->st = st; o->send = send; o->send_ctx = send_ctx;
    o->now = now_ms; o->side = -1; o->my_slot = 0;
    if (st) {
        od_zero(st, sizeof *st);
        st->magic = 0x4F445354u;                                 /* 'ODST' */
    }
    io.send = od_send_cb; io.recv = od_recv_cb; io.ctx = o;
    mnc_init(&o->m, &io, 0, cfg->build_id, cfg->codeset, cfg->rules, seed ^ cfg->seed);
    od_pub(o);
    return od_apply_config(o);
}
#elif defined(HUB_OVL_RES)
/* resident half: the state the hand-off and the B3 answer need exists from the first engine pass on (od_o.cfg gates DHCP in s12_gc.c); the mnc client is set up by the
   overlay's first enter (od_init_mnc).  Returns OD_E_NOCFG until then (the caller ignores it, as before). */
int od_init(struct od *o, const struct od_cfg *cfg, struct od_status *st,
            int (*send)(void *ctx, const unsigned char *p, unsigned int len), void *send_ctx, unsigned int seed, unsigned int now_ms)
{
    (void)seed;
    od_zero(o, sizeof *o);
    o->cfg = cfg; o->st = st; o->send = send; o->send_ctx = send_ctx;
    o->now = now_ms; o->side = -1; o->my_slot = 0;
    if (st) {
        od_zero(st, sizeof *st);
        st->magic = 0x4F445354u;                                 /* 'ODST' */
    }
    o->srv_ip = cfg->server_ip;
    od_pub(o);
    return OD_E_NOCFG;
}
#else
/* overlay half (first load since boot): the mnc client with the resident io callbacks, then the config */
void od_init_mnc(struct od *o, int (*send_cb)(void *, const mnc_u8 *, mnc_u32), int (*recv_cb)(void *, mnc_u8 *, mnc_u32), unsigned int seed)
{
    mnc_io io;
    io.send = send_cb; io.recv = recv_cb; io.ctx = o;
    mnc_init(&o->m, &io, 0, o->cfg->build_id, o->cfg->codeset, o->cfg->rules, seed ^ o->cfg->seed);
    od_pub(o);
    (void)od_apply_config(o);
}
#endif

#if OD_OVL

/* guest tier: a card setup whose device id is all zero has a server and a pinned key but no account; the Unranked search (quick match)
   first asks the server for a short-lived token, kept in RAM only (never written to the card); every other mode needs an account */
static int od_guest(const struct od *o)
{
    unsigned int i, any = 0;
    for (i = 0; i < 8; i++) any |= o->cfg->dev[i];
    return !any;
}

int od_apply_config(struct od *o)
{
    const struct od_cfg *cfg = o->cfg;
    o->cfg_ok = 0;
    {   /* pinned keys: the second one only when it is not all zero */
        mnc_u8 k[64];
        int i, any = 0, n = 1;
        for (i = 0; i < 32; i++) { k[i] = cfg->pin[0][i]; k[32 + i] = cfg->pin[1][i]; if (cfg->pin[1][i]) any = 1; }
        if (any) n = 2;
        mnc_set_server_keys(&o->m, k, n);
    }
    mnc_clear_identity(&o->m);
    /* the server: a numeric address wins; else a valid host name is resolved at menu time (od_tick); the resolved address is cached for the session in o->srv_ip */
    o->srv_ip = cfg->server_ip;
    o->use_host = (!cfg->server_ip && (cfg->flags & OD_CFGF_HOST) && od_host_ok(cfg->host)) ? 1 : 0;
    o->dns_state = OD_DNS_NONE;
    if (cfg->magic0 == OD_CFG_MAGIC0 && cfg->magic1 == OD_CFG_MAGIC1 && (cfg->flags & OD_CFGF_ID) && (cfg->server_ip || o->use_host)) {
        int i, any = 0;
        for (i = 0; i < 32; i++) if (cfg->pin[0][i]) any = 1;
        if (any) {
            if (!od_guest(o)) mnc_set_identity(&o->m, cfg->dev, cfg->tok, cfg->code, cfg->name);   /* an all-zero device id = no linked account: a guest (od_tick mints a token) */
            o->cfg_ok = 1;
        }
    }
    if (o->net_up) mnc_set_net(&o->m, o->own_ip, o->mask, o->srv_ip, OD_LAXV(cfg->flags));   /* the address may have come from a later config */
    od_pub(o);
    return o->cfg_ok ? 0 : OD_E_NOCFG;
}

/* a typed connect code: letters/digits, exactly one '#' (Slippi universe) or '@' (Multishine universe) that is neither first nor last, at most 13 characters (the server's
   own limit; the Melee keyboard itself stops at 8) */
static int od_code_ok(const char *s)
{
    unsigned int i, n = od_slen(s), hash = 0, at = 0;
    if (n < 3 || n > 13) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '#' || c == '@') { hash++; at = i; }
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return 0;
    }
    return hash == 1 && at != 0 && at != n - 1;
}

/* a room name (ticket mode room; Game Night: the call-up name GN<code>S<set>): 3-18 characters A-Z 0-9 _ - (the server's RE_ROOM) */
static int od_room_ok(const char *s)
{
    unsigned int i, n = od_slen(s);
    if (n < 3 || n > 18) return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return 0;
    }
    return 1;
}

int od_request(struct od *o, unsigned int now_ms, int mode, const char *code)
{
    o->now = now_ms;
    if (!o->cfg_ok) { od_fail(o, OD_E_NOCFG); return OD_E_NOCFG; }
    if (mode != MNC_MODE_DIRECT && mode != MNC_MODE_ROOM && !(mode == MNC_MODE_UNRANKED && (o->cfg->flags & OD_CFGF_UNRANKED)) &&
        !(mode == MNC_MODE_RANKED && (o->cfg->flags & OD_CFGF_RANKED))) { od_fail(o, OD_E_MODE); return OD_E_MODE; }
    if (mode != MNC_MODE_UNRANKED && od_guest(o)) { o->m.status = MNC_ST_ACCOUNT_NEEDED; od_fail(o, OD_E_REFUSED); return OD_E_REFUSED; }   /* a guest only quick-matches */
    if (mode == MNC_MODE_UNRANKED || mode == MNC_MODE_RANKED) code = "";
    else if (mode == MNC_MODE_ROOM) { if (!od_room_ok(code)) { od_fail(o, OD_E_BADCODE); return OD_E_BADCODE; } }   /* Game Night: the call-up name */
    else if (!od_code_ok(code)) { od_fail(o, OD_E_BADCODE); return OD_E_BADCODE; }
    if (o->m.state == MNC_BUSY) { mnc_abort(&o->m); o->rep_started = 0; }     /* a report in flight is sent again later (rep_kind stays) */
    if (o->on_bk && !(o->on_bk & OD_BK_PROVEN)) { o->on_bk = 0; mw2_drop(&o->m.w); od_rediscover(o); }       /* the backup serves one search: a new one tries the primary first again (a resolved name that answered stays) */
    else if (o->use_host && o->err == OD_E_SERVER) od_rediscover(o);          /* the server did not answer: the name may point elsewhere now, look it up again on this entry */
    o->rt_n = 0; o->rt_why = 0; o->fails = 0; o->cn_n = 0;
    o->mode = mode;
    if (mode == MNC_MODE_UNRANKED && mnc_link_pref && !o->m.have_hello) o->why_hs = 0;      /* a failed HELLO of an earlier search is tried again for Wired opponents only */
    od_scopy(o->target, sizeof o->target, code);
    o->err = OD_E_NONE; o->started = 0; o->peer_taken = 0;
    o->peer_ip = 0; o->peer_port = 0; o->side = -1; o->my_slot = 0;
    o->core_state = 0;
    o->mid_ok = 0;
    o->reenrol = 0;                                                            /* one silent re-enrolment per search (od_tick) */
    o->t_req = now_ms; o->t_phase = now_ms;
    o->phase = OD_WAIT_NET;
    if (o->st) o->st->requests++;
    od_pub(o);
    return 0;
}

void od_cancel(struct od *o, unsigned int now_ms)
{
    o->now = now_ms;
    if (o->reenrol == 1 || o->reenrol >= 3) { mnc_abort(&o->m); o->reenrol = 2; }   /* the silent re-enrolment (or guest token request) is dropped; the identity on the card is untouched */
    if (o->phase == OD_FIND && o->started == 1) {                         /* (started 2 = a Grandmaster set is held, od_rk_hold: the server's ticket result must stay readable and nothing is sent) */
        mnc_abort(&o->m);
        if (o->cfg_ok && o->net_up) (void)mnc_cancel(&o->m, now_ms);          /* best effort: a lost reply is covered by the server's 60 s ticket timeout */
        if (o->st) o->st->cancels++;
    } else if (o->phase == OD_PEER || o->phase == OD_CONNECT || o->phase == OD_UP) {
        if (o->st) o->st->cancels++;
    }
    if (o->dns_state == OD_DNS_BUSY) { if (o->dns) sddns_abort(o->dns); o->dns_state = OD_DNS_NONE; }
    o->rt_why = 0;
    o->phase = OD_IDLE; o->err = OD_E_NONE; o->started = 0;
    od_pub(o);
}

#endif /* OD_OVL */

#if OD_RES
const char *od_err_text(const struct od *o)
{
    switch (o->err) {
    case OD_E_NOCFG:
        switch (o->cfg_why) {
        case OD_WHY_NOCARD: return "No memory card. Online needs one.";
        case OD_WHY_NOFILE: return "No online setup on the memory card";
        case OD_WHY_DAMAGED: return "Card online setup is damaged";
        case OD_WHY_NEWER: return "Card setup is too new";
        case OD_WHY_INCOMPLETE: return "Card online setup is incomplete";
        case OD_WHY_ERROR: case OD_WHY_BUSY: return "Couldn't read the online setup. Try again.";
        default: return "Online is not set up here";
        }
    case OD_E_NONET: return "No network. Check the cable.";
    case OD_E_SERVER:
        return o->m.w.st_bad ? "Couldn't verify the server" : o->rt_n ? "Server unavailable. Try again later." : "Couldn't reach the server. Try again.";
    case OD_E_NOOPP: return o->mode == MNC_MODE_DIRECT ? "Nobody entered your code in time" : o->mode == MNC_MODE_ROOM ? "Your opponent did not show up" : "No opponent found. Try again.";   /* cube Dojo showed the Direct text */
    case OD_E_REFUSED:
        switch (o->m.status) {
        case 6: return "Build too old for the server";
        case 32: return "Update required to play online";   /* UPDATE_REQUIRED: below the server's minimum build, or protocols differ (letters only: Melee's text drawing swallows / and |) */
        case 7: case 8: case 9: return "Server and game versions differ";
        case 4: case 26: return "Login refused by the server";
        case 5: return "This identity is blocked";
        case 3: return "Too many requests, wait a moment";
        case 18: case 25: return "This mode is not available";
        case 1: case 16: return "Connect code not found";   /* BAD_REQUEST (code does not parse, or is your own) and NOT_FOUND: couch feedback 20 */
        case 14: return "A search is already running";
        case 17: case 40: case 41: case 42: return "Server busy. Try again soon.";
        case 43: return "Server maintenance. Try again later.";
        default: return "The server refused the request";
        }
    case OD_E_PEER: return "Couldn't use your opponent's address";
    case OD_E_NOCONN: return "Couldn't connect to your opponent. Try again.";
    case OD_E_LOST: return "Connection to the opponent was lost";
    case OD_E_BADCODE: return "That is not a valid connect code";
    case OD_E_MODE: return "Online play is not available yet";
    case OD_E_SESSION: return "Restart the game to play someone else";
    case OD_E_LEFT: return "Opponent left";
    case OD_E_DNS: return o->dns_text[0] ? o->dns_text : "Couldn't find the server name";
#ifdef HUB_OVL_RES
    case OD_E_HUB: return "Online files missing on the memory card";
#endif
    default: return "Online play failed";
    }
}

int od_b3(struct od *o, unsigned char *msrb)
{
    if ((o->phase == OD_WAIT_NET || o->phase == OD_FIND || o->phase == OD_PEER || o->phase == OD_CONNECT) && o->mode != MNC_MODE_ROOM)
        msrb[1] = 1;                                 /* MSRB_IS_LOCAL_PLAYER_READY: Slippi's CSS then shows "Locked in" / "Searching for opponent" / "Connecting to ..." (cube it kept
                                                        "Press START to search" through the whole search); Game Night keeps its own "Starting the match..." line (codeset MS_NM_LINE*) */
    switch (o->phase) {
    case OD_IDLE:
        if (o->st) o->st->b3_state = 0;
        return 1;                                    /* all zero = Slippi's state 0 (idle) */
    case OD_WAIT_NET: msrb[0] = o->rt_why ? 2 : 1; break;   /* INITIALIZING; between retries: MATCHMAKING (the player sees a search, not an error; B cancels as usual) */
    case OD_FIND: msrb[0] = 2; break;                /* MATCHMAKING */
    case OD_PEER: case OD_CONNECT: msrb[0] = 3; break;   /* OPPONENT_CONNECTING */
    case OD_ERR: {
        const char *t = od_err_text(o);
        unsigned int i = 0;
        msrb[0] = 5;                                 /* ERROR_ENCOUNTERED */
        while (t[i] && i < 240u) { msrb[357 + i] = (unsigned char)t[i]; i++; }
        break;
    }
    default:
        if (o->st) o->st->b3_state = 0;
        return 0;                                    /* UP: the engine's own answer (state 4, selections) */
    }
    if (o->st) o->st->b3_state = msrb[0];
    return 1;
}

void od_net_up(struct od *o, unsigned int own_ip, unsigned int mask)
{
    o->net_up = 1; o->own_ip = own_ip; o->mask = mask;
#ifdef HUB_OVL_RES
    hub_set_net(o);
#else
    mnc_set_net(&o->m, own_ip, mask, o->srv_ip, OD_LAXV(o->cfg->flags));
#endif
    if (o->st) o->st->net_ip = own_ip;
}
void od_net_down(struct od *o) { o->net_up = 0; }

void od_rx(struct od *o, const unsigned char *d, unsigned int len)
{
    unsigned int slot, i;
    if (len == 0 || len > MNC_MAX_PACKET) return;
    if (o->in_n >= OD_INBOX) { if (o->st) o->st->rx_drop++; return; }
    slot = (o->in_head + o->in_n) % OD_INBOX;
    for (i = 0; i < len; i++) o->in_buf[slot][i] = d[i];
    o->in_len[slot] = len;
    o->in_n++;
    if (o->st) o->st->rx_in++;
}
#endif /* OD_RES */

#ifdef OD_LAN_PORT_VAR                 /* host tests: the driver's real socket port (a console always binds 43200) */
extern unsigned short OD_LAN_PORT_VAR;
#define OD_LAN_PORT OD_LAN_PORT_VAR
#endif
#ifndef OD_LAN_PORT
#define OD_LAN_PORT 43200u   /* the engine's game socket port (s3_cfg.local_port, "local_port"): the server and the opponent talk to this one socket */
#endif

#if OD_OVL
/* the assignment of the last paired ticket into the controller: the peer, our side, the match id, the opponent; 0 = ok, else OD_E_PEER (sets nothing then) */
static int od_pair(struct od *o)
{
    int i, me = -1, other = -1;
    const mnc_player *a, *b;
    for (i = 0; i < (int)o->m.nplayers; i++) {
        if (o->m.pl[i].slot == o->m.my_slot) me = i; else if (other < 0) other = i;
    }
    if (o->m.nplayers != 2 || me < 0 || other < 0 || o->m.my_slot < 1 || o->m.my_slot > 2) return OD_E_PEER;
    a = &o->m.pl[me]; b = &o->m.pl[other];
    o->peer_ip = b->ip; o->peer_port = b->port;
    if (b->lan_ip && b->lan_port && a->ip == b->ip) { o->peer_ip = b->lan_ip; o->peer_port = b->lan_port; }   /* Slippi's rule: same public address -> the LAN one */
    o->side = (int)o->m.my_slot - 1;
    o->my_slot = (int)o->m.my_slot;
    for (i = 0; i < 8; i++) o->mid[i] = o->m.match_id[i];
    o->mid_ok = 1;
    od_scopy(o->opp_name, sizeof o->opp_name, b->name);
    od_scopy(o->opp_code, sizeof o->opp_code, b->code);
    return 0;
}

#ifdef MNC_RKSET
/* GRANDMASTER SETS. The pairing of a ranked ticket whose reply announced a set (both consoles said they can act in one) is HELD: the controller stays in OD_FIND with
   started = 2 (od_tick does nothing, od_b3 keeps saying "searching"), the hand-off block says MNC_GN_RKSET and the console leaves the character screen for the menu scene, where the sign overlay runs the
   set's prep (strikes, bans, picks) from RK_SET_STATE.  The assignment stays in the resident mnc / controller fields (the match id, the players, our slot).  Each game is then armed by the sign overlay
   (hand-off ARMED + MNC_RK_FLAG) and started from the character screen's lock-in by od_rk_go: the same peer, a fresh engine session, no new ticket. */
static void od_rk_hold(struct od *o)
{
    mnc *m = &o->m;
    unsigned i;
    m->gn_state = MNC_GN_RKSET;
    for (i = 0; i < 15u; i++) m->gn_room[i] = 0;                      /* not a Game Night room name: gn_room[0] stays 0 */
    m->gn_room[MNC_RK_LEN_AT] = mnc_rk_set;
    m->gn_room[MNC_RK_FLAGS_AT] = (unsigned char)((m->gn_room[MNC_RK_FLAGS_AT] & 0x80u) | MNC_RK_FLAG);   /* (the engine's watch capability bit stays) */
    o->started = 2;
    o->t_phase = o->now;
    if (o->st) o->st->finds_done++;
}
#endif

static void od_find_done(struct od *o)
{
    if (od_pair(o)) { od_fail(o, OD_E_PEER); return; }
#ifdef MNC_RKSET
    if (o->mode == MNC_MODE_RANKED && mnc_rk_set > 1u && o->m.gn_magic == MNC_GN_MAGIC) { od_rk_hold(o); return; }
#endif
    o->phase = OD_PEER;
    o->t_phase = o->now;
    if (o->st) { o->st->finds_done++; o->st->t_find_ms = o->now - o->t_req; }
}

#ifdef MNC_RKSET
/* one game of a held Grandmaster set starts: the character screen's lock-in (on_backend.c od_find, hand-off ARMED + MNC_RK_FLAG) comes here.  The stage of the game (the sign overlay put it in the hand-off block, from
   the set view) becomes the match setup's one stage; the rest of the setup is the one the first ticket reply carried.  0 = started (phase OD_PEER: the resident engine takes the peer), else OD_E_*. */
int od_rk_go(struct od *o, unsigned int now_ms)
{
    mnc *m = &o->m;
    if (m->gn_magic != MNC_GN_MAGIC || !m->nplayers) return OD_E_PEER;
    o->now = now_ms;
    if (od_pair(o)) { od_fail(o, OD_E_PEER); return OD_E_PEER; }
    o->mode = MNC_MODE_RANKED;
    o->started = 0; o->peer_taken = 0; o->core_state = 0; o->err = OD_E_NONE; o->rt_why = 0;
    o->cn_n = 0xFFu;                                                  /* a failed connection is an error box, never a new ticket (the pairing is the set's) */
    o->t_req = now_ms; o->t_phase = now_ms;
#ifdef MNC_SETUP_BLOCK
    if (m->su_have) {
        m->su_n = 1; m->su_stages[0] = m->gn_room[MNC_RK_STAGE_AT];
        m->su_flags |= 1u;                                            /* pause off, as the ranked rules say (the first reply's setup block already carries it) */
    }
#endif
    o->phase = OD_PEER;
    if (o->st) o->st->finds_done++;
    od_pub(o);
    return 0;
}
#endif

/* ---- result reports (Ranked) ---------------------------------------------------------------------------------------------------------- */
static void put32b(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

#endif /* OD_OVL */
#if OD_RES
int od_report_busy(const struct od *o) { return o->rep_kind != 0; }
#endif
#if OD_OVL

static int od_rep_skip(struct od *o, int why)
{
    if (o->st) { o->st->rep_skipped++; o->st->rep_why = (unsigned int)why; }
    return why;
}

int od_report_game(struct od *o, unsigned int now_ms, const struct od_game *g)
{
    unsigned char ev[20], h[32], *b;
    unsigned int i;
    int s1 = g->stocks1, s2 = g->stocks2;
    (void)now_ms;
    if (!o->cfg_ok || !((o->mode == MNC_MODE_RANKED && (o->cfg->flags & OD_CFGF_RANKED)) || o->mode == MNC_MODE_ROOM)) return od_rep_skip(o, OD_RS_MODE);   /* Ranked, or a Game Night set (room) */
    if (!o->mid_ok) return od_rep_skip(o, OD_RS_NOTICKET);
    if (g->winner != 1 && g->winner != 2) return od_rep_skip(o, OD_RS_NOWINNER);
    if (o->rep_kind) return od_rep_skip(o, OD_RS_BUSY);
    if (s1 < 0) s1 = 0;
    if (s1 > 4) s1 = 4;
    if (s2 < 0) s2 = 0;
    if (s2 > 4) s2 = 4;
    for (i = 0; i < 8; i++) { o->rep_mid[i] = o->mid[i]; ev[i] = o->mid[i]; }
    ev[8] = (unsigned char)g->game;
    put32b(ev + 9, g->final_frame); put32b(ev + 13, g->cks);
    ev[17] = (unsigned char)g->winner; ev[18] = (unsigned char)s1; ev[19] = (unsigned char)s2;
    ms_sha256(ev, 20u, h);                                           /* ms_ranked.evidence_hash: SHA-256(match id, game, final frame, checksum, winner, stocks) */
    b = o->rep_blob;
    b[0] = 1; b[1] = (unsigned char)g->game; b[2] = (unsigned char)g->tie; b[3] = (unsigned char)g->winner; b[4] = (unsigned char)g->endm; b[5] = (unsigned char)g->stage;
    put32b(b + 6, g->frames); put32b(b + 10, g->final_frame); put32b(b + 14, g->cks);
    b[18] = (unsigned char)s1; b[19] = (unsigned char)s2;
    for (i = 0; i < 32; i++) b[20 + i] = h[i];
    o->rep_len = OD_REP_LEN; o->rep_kind = 1; o->rep_tries = 0; o->rep_next = 0; o->rep_started = 0;
    o->rep_code = (g->chars_ok && o->mode == MNC_MODE_ROOM && g->char1 >= 0 && g->char1 <= 25 && g->char2 >= 0 && g->char2 <= 25) ? (OD_REP_CHARS | ((unsigned int)g->char1 << 8) | (unsigned int)g->char2) : 0u;
    if (o->st) {
        o->st->rep_queued++; o->st->rep_winner = (unsigned int)g->winner; o->st->rep_frames = g->frames; o->st->rep_ff = g->final_frame;
        o->st->rep_ck = g->cks; o->st->rep_stocks = ((unsigned int)s1 << 8) | (unsigned int)s2;
    }
    od_pub(o);
    return 0;
}

int od_report_status(struct od *o, unsigned int now_ms, int code)
{
    unsigned int i;
    (void)now_ms;
    if (!o->cfg_ok || !((o->mode == MNC_MODE_RANKED && (o->cfg->flags & OD_CFGF_RANKED)) || o->mode == MNC_MODE_ROOM)) return od_rep_skip(o, OD_RS_MODE);   /* Ranked, or a Game Night set (room) */
    if (!o->mid_ok) return od_rep_skip(o, OD_RS_NOTICKET);
    if (o->rep_kind) return od_rep_skip(o, OD_RS_BUSY);
    for (i = 0; i < 8; i++) o->rep_mid[i] = o->mid[i];
    o->rep_code = (unsigned int)code; o->rep_kind = 2; o->rep_len = 0; o->rep_tries = 0; o->rep_next = 0; o->rep_started = 0;
    if (o->st) { o->st->rep_queued++; o->st->rep_status_code = (unsigned int)code; }
    od_pub(o);
    return 0;
}

static void od_rep_done(struct od *o, int ok)
{
    if (ok) {
        if (o->st) {
            o->st->rep_acked++;
            if (o->rep_kind == 1) o->st->rep_game_state = o->m.rep_game;
            o->st->rep_match_state = o->m.rep_match;
        }
    } else if (o->st) {
        o->st->rep_failed++; o->st->rep_err = (unsigned int)o->m.err;
    }
    o->rep_kind = 0; o->rep_started = 0;
}

/* the GAME_REPORT payload after the match id: the 52-byte blob, plus (Game Night on a server with room types) the characters as played, slot 1 then slot 2; out holds OD_REP_LEN + 2 bytes */
unsigned int od_rep_payload(const struct od *o, unsigned char *out)
{
    unsigned int i, n = o->rep_len;
    for (i = 0; i < n && i < OD_REP_LEN; i++) out[i] = o->rep_blob[i];
    if ((o->rep_code & OD_REP_CHARS) && n == OD_REP_LEN) { out[n++] = (unsigned char)(o->rep_code >> 8); out[n++] = (unsigned char)o->rep_code; }
    return n;
}

/* WHY-IT-FAILED record: the glue's end_session hands over the last peer session's counters; sent with the next ticket after a failed connect */

/* called from od_tick while no search runs: finish or (re)start the pending report */
static void od_rep_step(struct od *o, unsigned int now_ms)
{
    unsigned int i;
    if (!o->rep_kind) return;
    if (o->rep_started) {
        if (o->m.state == MNC_DONE) od_rep_done(o, 1);
        else if (o->m.state == MNC_FAILED) {
            o->rep_started = 0;
            if (o->m.err == MNC_E_SERVER || o->rep_tries >= OD_REP_TRIES) od_rep_done(o, 0);          /* refused by the server, or out of tries: dropped */
            else o->rep_next = now_ms + OD_REP_RETRY_MS;
        } else if (o->m.state == MNC_IDLE) o->rep_started = 0;
        return;
    }
    if (!o->net_up || !o->srv_ip || o->m.state == MNC_BUSY || (unsigned int)(now_ms - o->rep_next) > 0x7FFFFFFFu) return;   /* wrap-safe: not before rep_next */
    for (i = 0; i < 8; i++) o->m.match_id[i] = o->rep_mid[i];
    if (o->rep_kind == 1) {                                          /* the blob, and with room types the two "characters as played" bytes after it (copied into the request at once: a stack copy is enough) */
        unsigned char xb[OD_REP_LEN + 2];
        i = (unsigned int)mnc_game_report(&o->m, now_ms, xb, od_rep_payload(o, xb));
    } else i = (unsigned int)mnc_match_status(&o->m, now_ms, (mnc_u8)o->rep_code);
    if (i != 0) {
        od_rep_done(o, 0);
        return;
    }
    o->rep_started = 1; o->rep_tries++;
}

/* ---- the server NAME: resolved once per session, only here (phase WAIT_NET = the player asked for a search), never in a match ---- */
static void od_dec(char *d, unsigned int cap, unsigned int v)
{
    char t[11];
    unsigned int k = 0, n = od_slen(d);
    if (v == 0) t[k++] = '0';
    while (v && k < sizeof t) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k && n + 1 < cap) d[n++] = t[--k];
    d[n] = 0;
}
static void od_cat(char *d, unsigned int cap, const char *s)
{
    unsigned int n = od_slen(d), i = 0;
    while (s[i] && n + 1 < cap) d[n++] = s[i++];
    d[n] = 0;
}
static void od_cat_ip(char *d, unsigned int cap, unsigned int ip)
{
    int i;
    for (i = 3; i >= 0; i--) { od_dec(d, cap, (ip >> (8 * i)) & 255u); if (i) od_cat(d, cap, "."); }
}
/* LINK QUALITY cooldown: the server refused the ticket with status 50..53 (COOLDOWN_15 / 60 / 240 / 1440 = the minutes still to wait, rounded up).  The text is built
   here, in the overlay, into the resident error text buffer and shown through OD_E_DNS (the box prints dns_text): no engine byte.  15, 60 minutes; 4, 24 hours. */
static void od_cooldown(struct od *o)
{
    static const unsigned short mn[4] = { 15, 60, 240, 1440 };
    unsigned int m = mn[(unsigned int)o->m.status - 50u];
    char *t = o->dns_text;
    t[0] = 0;
    od_cat(t, sizeof o->dns_text, "Matchmaking paused for up to ");
    od_dec(t, sizeof o->dns_text, m > 60u ? m / 60u : m);
    od_cat(t, sizeof o->dns_text, m > 60u ? " hours" : " min");
    od_cat(t, sizeof o->dns_text, " after leaving games");
    od_fail(o, OD_E_DNS);
}
/* WIRED OPPONENTS ONLY: the server dropped a wired-only search after 60 s without a wired opponent (status 54, NO_LINK_MATCH).  The text is built here, in the
   overlay, and shown through OD_E_DNS like the cooldown text: no engine byte.  An older server never sends the status, an older console shows it as the plain refusal. */
static void od_nowired(struct od *o)
{
    char *t = o->dns_text;
    t[0] = 0;
    od_cat(t, sizeof o->dns_text, "No wired opponent found right now. Turn off Wired only to search wider.");
    od_fail(o, OD_E_DNS);
}
/* same compact diagnostics as the Slippi DNS box: code (4 no such name, 5 no A record, 6 server error, 7 no answer, 8 only malformed answers), sends, replies, ignored, malformed, resolver */
static void od_dns_failed(struct od *o, const struct sddns *d)
{
    char *t = o->dns_text;
    o->dns_state = OD_DNS_FAILED;
    t[0] = 0;
    if (!d) {
        od_cat(t, sizeof o->dns_text, "Server name needs DNS, not in this build");
    } else if (d->err == SDDNS_E_NOSERVER) {
        od_cat(t, sizeof o->dns_text, "No DNS server");
    } else {
        od_cat(t, sizeof o->dns_text, "Server name not found (DNS e"); od_dec(t, sizeof o->dns_text, (unsigned int)d->err);
        od_cat(t, sizeof o->dns_text, " t"); od_dec(t, sizeof o->dns_text, d->st_sent);
        od_cat(t, sizeof o->dns_text, " r"); od_dec(t, sizeof o->dns_text, d->st_rx);
        od_cat(t, sizeof o->dns_text, " i"); od_dec(t, sizeof o->dns_text, d->st_ignored);
        od_cat(t, sizeof o->dns_text, " b"); od_dec(t, sizeof o->dns_text, d->st_bad);
        od_cat(t, sizeof o->dns_text, " "); od_cat_ip(t, sizeof o->dns_text, d->server[0]);
        od_cat(t, sizeof o->dns_text, ")");
    }
    if (o->st) {
        o->st->dns_state = OD_DNS_FAILED;
        if (d) {
            o->st->dns_err = (unsigned int)d->err; o->st->dns_sent = d->st_sent; o->st->dns_rx = d->st_rx;
            o->st->dns_ignored = d->st_ignored; o->st->dns_bad = d->st_bad; o->st->dns_server = d->server[0];
        }
    }
    if (o->on_bk & OD_BK_NAME) {                       /* the DNS fallback found nothing: the address in force again, and today's box */
        o->on_bk = 0; o->srv_ip = o->cfg->server_ip; o->fails = 0;
        if (o->net_up) mnc_set_net(&o->m, o->own_ip, o->mask, o->srv_ip, OD_LAXV(o->cfg->flags));
        od_fail(o, OD_E_SERVER);
        return;
    }
    od_fail(o, OD_E_DNS);
}

/* one step of the lookup: 1 once o->srv_ip is known, else 0 (still running, or the search failed with OD_E_DNS) */
static int od_resolve_step(struct od *o, unsigned int now_ms)
{
    struct sddns *d = o->dns;
    int st;
    if (o->srv_ip) return 1;
    if (!d) { od_dns_failed(o, 0); return 0; }
    if (o->dns_state != OD_DNS_BUSY) {
        int rc;
        o->dns_text[0] = 0;
        sddns_set_servers(d, o->dhcp_dns, (o->on_bk & OD_BK_NAME) ? 0x01010101u : 0u);   /* a name-only setup: the DHCP-provided resolver only; the release fallback: then 1.1.1.1 */
        rc = sddns_start(d, o->cfg->host, now_ms);
        if (rc == SDDNS_E_BUSY) {                                  /* the shared resolver is serving someone else (the bridge): wait a little */
            if ((unsigned int)(now_ms - o->t_phase) > 10000u) { od_dns_failed(o, d); }
            return 0;
        }
        if (rc != 0) { d->err = SDDNS_E_ARG; od_dns_failed(o, d); return 0; }
        o->dns_state = OD_DNS_BUSY; o->dns_t0 = now_ms;
        if (o->st) { o->st->dns_lookups++; o->st->dns_state = OD_DNS_BUSY; o->st->dns_server = d->server[0]; }
    }
    st = sddns_poll(d, now_ms);
    if (st == SDDNS_DONE) {
        char norm[OD_HOST_MAX + 1];
        unsigned int lit;
        if (!sddns_parse_dotted(o->cfg->host, &lit)) {              /* a literal address in the host field has no name to compare */
            unsigned int i;
            if (sddns_normalize_host(o->cfg->host, norm) < 0) { d->err = SDDNS_E_BAD; od_dns_failed(o, d); return 0; }
            for (i = 0; norm[i] || d->name[i]; i++) if (norm[i] != d->name[i]) { d->err = SDDNS_E_BAD; od_dns_failed(o, d); return 0; }   /* the result is for another name (someone else restarted the shared resolver) */
        }
        if ((o->on_bk & OD_BK_NAME) && !OD_FB_ANY_ANSWER && !od_ip_public(d->result_ip)) { d->err = SDDNS_E_BAD; od_dns_failed(o, d); return 0; }   /* a resolver on the LAN cannot steer a release console into the LAN */
        o->srv_ip = d->result_ip;
        o->dns_state = OD_DNS_OK;
        mnc_set_net(&o->m, o->own_ip, o->mask, o->srv_ip, OD_LAXV(o->cfg->flags));   /* the "opponent must not be the server" rule uses the resolved address */
        if (o->st) {
            o->st->dns_state = OD_DNS_OK; o->st->srv_ip = o->srv_ip; o->st->dns_ms = now_ms - o->dns_t0;
            o->st->dns_sent = d->st_sent; o->st->dns_rx = d->st_rx; o->st->dns_ignored = d->st_ignored; o->st->dns_bad = d->st_bad;
        }
        return 1;
    }
    if (st == SDDNS_FAILED) { od_dns_failed(o, d); return 0; }
    if (st == SDDNS_IDLE) o->dns_state = OD_DNS_NONE;              /* aborted by the other user of the resolver: start again at the next tick */
    return 0;
}

/* The hub's own requests outside a search (HELLO, WHOAMI: hub_soc.c net_step_run).  With a dead address these fail first and the menu's offline guard then closes every server
   row, so no search would ever reach od_retry: the same swap rule is applied here. Only while no search runs. */
static int od_idle(const struct od *o) { return o->phase == OD_IDLE || o->phase == OD_ERR; }
void od_idle_noanswer(struct od *o)
{
    if (!o->cfg || !od_idle(o)) return;
    if (++o->fails >= OD_BK_AFTER && (o->bk_ip || o->on_bk || od_name_fb(o))) od_swap(o);
    od_pub(o);
}
void od_idle_ok(struct od *o)
{
    if (!od_idle(o)) return;
    o->fails = 0;
    if ((o->on_bk & OD_BK_NAME) && o->srv_ip) o->on_bk |= OD_BK_PROVEN;
    od_pub(o);
}
/* 1 = an address is in force; 0 = the fallback lookup is running (the caller sends nothing to the server this tick).  A failed lookup puts the address back without an error box. */
int od_idle_resolve(struct od *o, unsigned int now_ms)
{
    int ph, er, r;
    if (o->srv_ip || !(o->on_bk & OD_BK_NAME) || !o->net_up || !od_idle(o)) return o->srv_ip != 0;
    ph = o->phase; er = o->err;
    r = od_resolve_step(o, now_ms);
    if (!(o->on_bk & OD_BK_NAME)) { o->phase = ph; o->err = er; od_pub(o); return 1; }   /* the lookup failed: od_dns_failed put the address back; no box outside a search */
    return r;
}

/* An anonymous (Sandbag) connect code: the tag before '@' holds a digit (player-chosen tags are letters only),  6.1. */
static int od_anon_code(const char *c)
{
    unsigned int i = 0, digit = 0;
    while (c[i] && c[i] != '@') { if (c[i] >= '0' && c[i] <= '9') digit = 1; i++; }
    return c[i] == '@' && i >= 1 && i <= 4 && digit;
}

/* the fresh identity from the server becomes the one in force (the config block is the console's copy); the card record follows (ident_dirty, on_backend.c) */
static void od_adopt(struct od *o)
{
    struct od_cfg *c = (struct od_cfg *)o->cfg;
    unsigned int i;
    for (i = 0; i < 8; i++) c->dev[i] = o->m.dev[i];
    for (i = 0; i < 16; i++) c->tok[i] = o->m.tok[i];
    od_scopy(c->code, sizeof c->code, o->m.code);
    od_scopy(c->name, sizeof c->name, o->m.name);
    o->ident_dirty = 1;
}

void od_tick(struct od *o, unsigned int now_ms)
{
    int queue = o->mode == MNC_MODE_UNRANKED || o->mode == MNC_MODE_RANKED;
    unsigned int find_ms = queue ? 0x7FFFFFFFu : o->cfg->find_ms ? o->cfg->find_ms : OD_FIND_MS_DEFAULT;     /* a queue search runs until the player cancels (Slippi's rule; the server has no cap) */
    unsigned int net_ms = o->cfg->net_ms ? o->cfg->net_ms : OD_NET_MS_DEFAULT;
    o->now = now_ms;
    if (o->m.state == MNC_BUSY) {
        (void)mnc_poll(&o->m, now_ms);
        if (o->m.state == MNC_DONE) {
            o->srv_ok_ms = now_ms | 1u;                    /* a real answer from the server (Settings > Network status) */
            if ((o->on_bk & OD_BK_NAME) && o->srv_ip) o->on_bk |= OD_BK_PROVEN;   /* the resolved name answered with the pinned key: it stays for the session */
        }
    }
    if (od_idle(o)) {                                  /* 1287 DNS fallback outside a search, counted here for every request of the one mnc (title update HELLO, hub HELLO / WHOAMI, ...), whoever polled
                                                          it; the lookup also runs here, in the network slice (the Dolphin lab: started from the menu thread its datagrams never left).  rt_at is free while
                                                          no search runs and marks the failed request already counted */
        if (o->m.state == MNC_DONE) od_idle_ok(o);
        else if (o->m.state == MNC_FAILED && (o->m.err == MNC_E_TIMEOUT || o->m.err == MNC_E_BAD_REPLY) && o->rt_at != (o->m.sent_ms | 1u)) { o->rt_at = o->m.sent_ms | 1u; od_idle_noanswer(o); }
        (void)od_idle_resolve(o, now_ms);
    }
    if (o->phase != OD_FIND) od_rep_step(o, now_ms);
    switch (o->phase) {
    case OD_WAIT_NET:
        if (o->net_up && o->rep_kind) {
            /* a result report is still queued or in flight on the one mnc: the search starts when it is done (od_rep_step) */
        } else if (o->rt_why && (unsigned int)(now_ms - o->rt_at) > 0x7FFFFFFFu) {
            /* backoff between two attempts of one search */
        } else if (o->reenrol == 1) {                               /* the server forgot this (anonymous) device: waiting for the fresh identity, then the search goes on */
            if (o->m.state == MNC_DONE) { od_adopt(o); o->reenrol = 2; }
            else if (o->m.state != MNC_BUSY) { o->reenrol = 2; od_fail(o, OD_E_REFUSED); }
        } else if (o->reenrol >= 3) {                               /* guest token request (3 first, 4 after an expired token): RAM only, then the search goes on */
            if (o->m.state == MNC_DONE) o->reenrol = o->reenrol == 3 ? 0 : 2;
            else if (o->m.state != MNC_BUSY) { o->reenrol = 2; od_fail(o, o->m.err == MNC_E_SERVER ? OD_E_REFUSED : OD_E_SERVER); }
        } else if (o->net_up && o->srv_ip && !o->m.have_id && od_guest(o)) {
            if (mnc_guest(&o->m, now_ms)) { od_fail(o, OD_E_SERVER); break; }
            o->reenrol = 3;
        } else if (o->net_up && !o->srv_ip) {
            (void)od_resolve_step(o, now_ms);                       /* the name of the server: one lookup, then the search starts at a following tick */
        } else if (o->net_up && !o->m.have_hello && o->why_hs != 2 && (((((o->cn_n && o->mid_ok) || o->why[8] == 2u) && o->why[8])) || (o->mode == MNC_MODE_UNRANKED && mnc_link_pref))) {
            /* the same one HELLO also serves Wired opponents only: the preference goes to a server whose HELLO announced caps2 bit 4, and a search alone never says HELLO */
            /* the why-it-failed record goes only to a server whose HELLO announced caps2 bit 3, and a search alone never says HELLO (the mw2 handshake is not one):
               a console whose boot did not HELLO on this client sent the short note only, and the server journal never showed a record.  One HELLO first, then the ticket. */
            if (o->why_hs == 0) o->why_hs = mnc_hello(&o->m, now_ms) == 0 ? 1 : 2;
            else if (o->m.state != MNC_BUSY) o->why_hs = 2;          /* answered (caps2 known now) or failed: the search goes on, with or without the record */
        } else if (o->net_up) {
            int rc;
            o->m.f_open = (mnc_u8)queue;
            if (o->why[8] == 2u) { mnc_note_connect_failed(o->why_mid); mnc_note_why(o->why); }   /* BLACKOUT: the dropped match's record, once */
            else if (o->cn_n && o->mid_ok && (queue || o->why[8])) { mnc_note_connect_failed(o->mid); if (o->why[8]) mnc_note_why(o->why); }   /* the last pairing could not connect: the server keeps that pair apart for a while (queues), and gets the why-it-failed record (all modes) */
            rc = mnc_find(&o->m, now_ms, o->mode, o->target, 0, o->own_ip, (mnc_u16)(o->lan_port ? o->lan_port : OD_LAN_PORT));   /* the LAN hint: two consoles behind one router meet on it (Slippi's rule, od_find_done) */
            mnc_note_connect_failed(0);
            if (rc) { od_fail(o, OD_E_SERVER); break; }
            if (o->why[8] == 2u) o->why[8] = 0;                       /* BLACKOUT: sent with this ticket */
            o->started = 1; o->phase = OD_FIND; o->t_phase = now_ms; o->rt_why = 0;
            o->nat_probe = 1;                                           /* NAT PROBE: the glue sends one datagram to the server's wire port + 1 right after this ticket */
        } else if ((unsigned int)(now_ms - o->t_req) > net_ms) od_fail(o, OD_E_NONET);
        break;
    case OD_FIND:
        if (o->started == 2) break;                                 /* a Grandmaster set is held (od_rk_hold): nothing to do here */
        if (o->m.state == MNC_DONE) od_find_done(o);
        else if (o->m.state == MNC_FAILED) {
            int e = o->m.err;
            o->started = 0;
            if (od_retry(o, now_ms, find_ms)) break;                  /* no answer or busy: another attempt after the backoff ( C2) */
            if (e == MNC_E_TIMEOUT || e == MNC_E_BAD_REPLY) od_fail(o, OD_E_SERVER);
            else if (e == MNC_E_TICKET) od_fail(o, OD_E_PEER);
            else if (e == MNC_E_DEADLINE) od_fail(o, OD_E_NOOPP);
            else if (e == MNC_E_SERVER && o->m.status == MNC_ST_TICKET_TIMEOUT) od_fail(o, OD_E_NOOPP);
            else if (e == MNC_E_SERVER && o->m.status == MNC_ST_AUTH_FAILED && !o->reenrol && od_guest(o) && mnc_guest(&o->m, now_ms) == 0) {
                o->reenrol = 4; o->phase = OD_WAIT_NET;             /* the guest token expired (or the server restarted): one fresh token per search, RAM only */
            } else if (e == MNC_E_SERVER && o->m.status == MNC_ST_AUTH_FAILED && !o->reenrol && od_anon_code(o->m.code) && mnc_enrol(&o->m, now_ms, "", "") == 0) {
                o->reenrol = 1; o->phase = OD_WAIT_NET;             /* purged anonymous device ( C8): enrol again with no name, silently; no screen */
            } else if (e == MNC_E_SERVER && o->m.status == MNC_ST_NO_LINK_MATCH) od_nowired(o);                   /* the wired-only search found nobody for 60 s */
            else if (e == MNC_E_SERVER && o->m.status >= MNC_ST_COOLDOWN_15 && o->m.status <= MNC_ST_COOLDOWN_1440) od_cooldown(o);   /* matchmaking paused after repeated mid-game leaving */
            else od_fail(o, OD_E_REFUSED);
        } else if ((unsigned int)(now_ms - o->t_req) > find_ms) {
            mnc_abort(&o->m);
            (void)mnc_cancel(&o->m, now_ms);                       /* the server drops our ticket at once instead of at its own timeout */
            o->started = 0;
            if (o->st) o->st->cancels++;
            od_fail(o, OD_E_NOOPP);
        }
        break;
    default:
        break;
    }
    od_pub(o);
}
#endif /* OD_OVL */

#if OD_RES
int od_take_peer(struct od *o, unsigned int *ip, unsigned int *port, int *side)
{
    if (o->phase != OD_PEER || o->peer_taken) return 0;
    o->peer_taken = 1;
    *ip = o->peer_ip; *port = o->peer_port; *side = o->side;
    o->phase = OD_CONNECT;
    o->t_phase = o->now;
    od_pub(o);
    return 1;
}

/* The paired opponent could not be reached (Slippi's rule, SlippiMatchmaking.cpp "Connection attempt failed, looking for someone else": no error box, a fresh ticket) - bounded by OD_CN_MAX.  Queue modes
   search the queue again (the next ticket names the failed match so the server does not hand us the same console at once); Direct searches the same code again.  Game Night rooms keep their own rule
   (a failed call-up ends the set for this console, gns_res.c) and so does everything else: 0 = give up, show the error.  The caller ends the engine session (s12_gc.c) when the phase returns to WAIT_NET. */
static int od_conn_retry(struct od *o, unsigned int now_ms)
{
    if (o->mode != MNC_MODE_UNRANKED && o->mode != MNC_MODE_RANKED && o->mode != MNC_MODE_DIRECT) return 0;
    if (o->cn_n >= (o->mode == MNC_MODE_DIRECT || OD_CN_MAX < OD_CN_MAX_Q ? OD_CN_MAX : OD_CN_MAX_Q) || !o->mid_ok) return 0;   /* M2: queues 4, Direct OD_CN_MAX */
    o->cn_n++;
    o->peer_taken = 0; o->peer_ip = 0; o->peer_port = 0; o->side = -1; o->my_slot = 0;
    o->started = 0; o->core_state = 0;
    o->t_req = now_ms; o->t_phase = now_ms;                       /* a code search gets its whole window again */
    o->rt_why = 3; o->rt_at = now_ms + OD_CN_PAUSE_MS;            /* the search screen keeps searching (od_b3: MATCHMAKING while rt_why is set) */
    o->phase = OD_WAIT_NET;
    return 1;
}

void od_core_report(struct od *o, unsigned int now_ms, int core_state)
{
    unsigned int connect_ms = o->cfg->connect_ms ? o->cfg->connect_ms : OD_CONNECT_MS_DEFAULT;
    o->now = now_ms;
    o->core_state = core_state;
    if (o->phase == OD_CONNECT) {
        if (core_state == OD_CORE_RUN) {
            o->phase = OD_UP;
            if (o->st) o->st->t_up_ms = now_ms - o->t_req;
        } else if (core_state == OD_CORE_LOST) { if (!od_conn_retry(o, now_ms)) od_fail(o, OD_E_LOST); }
        else if ((unsigned int)(now_ms - o->t_phase) > connect_ms) { if (!od_conn_retry(o, now_ms)) od_fail(o, OD_E_NOCONN); }
    } else if (o->phase == OD_UP && core_state == OD_CORE_LOST) od_fail(o, OD_E_LEFT);   /* after a game (or during one): the CSS shows the box, B cancels (BA) */
    od_pub(o);
}
#endif /* OD_RES */
