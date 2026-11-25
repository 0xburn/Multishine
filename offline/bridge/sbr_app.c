/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr_app: see sbr_app.h.  Project-owned C99 (no libc, no heap).  Behaviour (not code) of the EXI side follows Slippi's
 * emulator device as read (prepareOnlineMatchState, handleConnectionCleanup)
 * and the ASM callers (Online/Online.s layouts), the same reading used. */
#include "sbr_app.h"
#include "sbr_card.h"
#include "sbr_ident.h"
#include "sbr_netcfg.h"

typedef sbr_u32 u32;
typedef sbr_u16 u16;
typedef sbr_u8 u8;

/* MSRB offsets (Online/Online.s:308-348) */
#define M_STATE 0
#define M_LREADY 1
#define M_RREADY 2
#define M_LIDX 3
#define M_RIDX 4
#define M_RNG 5
#define M_DELAY 9
#define M_LNAME 15
#define M_PNAME 46            /* 4 x 31 */
#define M_OPPNAME 170
#define M_PCODE 201           /* 4 x 10 */
#define M_PUID 241            /* 4 x 29 (LOCAL FORK: the recording's game info carries both players' ids, as Slippi's does) */
#define M_ERR 357             /* 241 */
#define M_MATCH 598           /* 0x138 */
#define M_MATCHID 910         /* 51 */
#define M_ALT 961
#define NAME_LEN 31
#define CODE_LEN 10
#define ERR_CHARS 120

#define CARD_ERROR_TRIES 3
#define PEER_STUB_MS 500u

/* 0x138-byte StartMelee block captured from a plain offline VS in our own patched ISO (the same capture uses; not
   taken from any Slippi source).  Stock 4, 8:00, items off. */
#ifdef SBRA_MATCH_FROM_S7   /* the engine (offline/multishine MANIFEST, profiles onb / onbc) links the S7 core, whose s3_match_struct is this same block word for word: one copy (engine size, 312 bytes) */
extern const u32 s3_match_struct[78];
#define MATCH_TEMPLATE s3_match_struct
#else
static const u32 MATCH_TEMPLATE[78] = {
    0x3201864Cu, 0xC3000000u, 0x000000FFu, 0xFF6E0009u, 0x000001E0u, 0x00000000u,
    0x00000000u, 0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0x3F800000u,
    0x3F800000u, 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x02000400u, 0x00FF0000u, 0x09007800u, 0xC0000401u, 0x00000000u, 0x00000000u,
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x14000400u, 0x00FF0000u, 0x09007800u,
    0xC0000401u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x3F800000u, 0x3F800000u,
    0x1A030400u, 0x00FF0000u, 0x09007800u, 0x40000401u, 0x00000000u, 0x00000000u,
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x1A030400u, 0x00FF0000u, 0x09007800u,
    0x40000401u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x3F800000u, 0x3F800000u,
    0x21030400u, 0x00FF0000u, 0x09007800u, 0x40000400u, 0x00000000u, 0x00000000u,
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x21030400u, 0x00FF0000u, 0x09007800u,
    0x40000400u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x3F800000u, 0x3F800000u
};
#endif

/* ------------------------------------------------------------------ helpers */
static void zero(void *p, u32 n) { sbr_wipe(p, n); }
static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, u32 cap) { u32 i = 0; if (!cap) return; for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static void scat(char *d, const char *s, u32 cap) { u32 n = slen(d); scpy(d + n, s, cap - n); }
static int is_mm2(const char *n)     /* Slippi's internal test environments (mm2.slippi.gg, matchmaking-dev.slippi.gg) in any case, with or without a trailing dot: never a target */
{
    static const char *const t[2] = { "mm2.slippi.gg", "matchmaking-dev.slippi.gg" };
    u32 i, k;
    for (k = 0; k < 2; k++) {
        for (i = 0; t[k][i]; i++) { char c = n[i]; if (c >= 'A' && c <= 'Z') c = (char)(c + 32); if (c != t[k][i]) break; }
        if (!t[k][i] && (n[i] == 0 || (n[i] == '.' && n[i + 1] == 0))) return 1;
    }
    return 0;
}
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static void words(u32 *w, u32 nw, const char *s)
{
    u32 i, n = slen(s);
    for (i = 0; i < nw * 4; i++) {
        u32 c = i < n ? (u8)s[i] : 0, k = i / 4, sh = 24 - 8 * (i % 4);
        w[k] = (w[k] & ~(0xFFu << sh)) | (c << sh);
    }
}
static u32 rnd(struct sbra *A)
{
    u32 x = A->rng ? A->rng : 0x2545F491u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    A->rng = x;
    return x;
}

static u16 to_sjis(char c)
{
    if (c >= '0' && c <= '9') return (u16)(0x824F + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (u16)(0x8260 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (u16)(0x8281 + (c - 'a'));
    if (c == '#') return 0x8194;
    if (c == ' ') return 0x8140;
    return 0;
}
static char from_sjis(u16 v)
{
    if (v >= 0x824F && v <= 0x8258) return (char)('0' + (v - 0x824F));
    if (v >= 0x8260 && v <= 0x8279) return (char)('A' + (v - 0x8260));
    if (v >= 0x8281 && v <= 0x829A) return (char)('a' + (v - 0x8281));
    if (v == 0x8194) return '#';
    if (v == 0x8140) return ' ';
    return '?';
}
/* the code-entry buffer: 9 full-width characters (18 bytes), cut at the first 0 */
static int code_from_game(const u8 *sjis, char *out, u32 cap)
{
    u32 i, n = 0;
    for (i = 0; i < 9; i++) {
        u16 v = (u16)((sjis[2 * i] << 8) | sjis[2 * i + 1]);
        if (v == 0) break;
        if (n + 1 >= cap) { out[0] = 0; return -1; }
        out[n++] = from_sjis(v);
    }
    out[n] = 0;
    return (int)n;
}
static void code_for_game(u8 *dst, const char *code)   /* "#" -> 0x81 0x94, the rest as is, 10 bytes */
{
    u32 i, n = 0;
    zero(dst, CODE_LEN);
    for (i = 0; code[i] && n + 1 < CODE_LEN; i++) {
        if (code[i] == '#') {
            if (n + 2 >= CODE_LEN) break;
            dst[n++] = 0x81; dst[n++] = 0x94;
        } else dst[n++] = (u8)code[i];
    }
}
/* A display name into Melee's name field the way Slippi's ConvertNarrowSpecialSHIFTJIS does it (same table as put_name in menus/on/on_backend.c): Melee's SIS encoder takes any ASCII punctuation byte it does
   not know as the lead byte of a two byte Shift-JIS code and SWALLOWS the next character, so punctuation becomes the full-width code (0x81 xx).  Letters, digits, space stay single bytes; at most 15 characters,
   a two byte code is never cut at the cap (NUL kept); control and non-ASCII (UTF-8 sequences) become '?'. */
static const char nm_chr[] = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
static const u8 nm_lo[] = { 0x49, 0x68, 0x94, 0x90, 0x93, 0x95, 0x66, 0x69, 0x6A, 0x96, 0x7B, 0x43, 0x7C, 0x44, 0x5E, 0x46, 0x47, 0x83, 0x81, 0x84, 0x48, 0x97, 0x6D, 0x5F, 0x6E, 0x4F, 0x51, 0x4D, 0x6F, 0x62, 0x70, 0x60 };
void sbra_name_for_game(u8 *dst, const char *name, u32 cap)
{
    u32 i, n = 0, ch = 0, k;
    zero(dst, cap);
    for (i = 0; name[i] && ch < 15; i++) {
        u8 c = (u8)name[i];
        if (c >= 0x80) { while ((((u8)name[i + 1]) & 0xC0) == 0x80) i++; c = '?'; }
        else if (c < 0x20 || c == 0x7F) c = '?';
        for (k = 0; k < sizeof nm_lo && (u8)nm_chr[k] != c; k++) {}
        if (n + (k < sizeof nm_lo ? 2u : 1u) + 1u > cap) break;
        if (k < sizeof nm_lo) { dst[n++] = 0x81; dst[n++] = nm_lo[k]; } else dst[n++] = c;
        ch++;
    }
}
#define name_for_game sbra_name_for_game

/* ------------------------------------------------------------------ autocomplete history (RAM; the card file MultishineCodes is written by the glue, see sbra_hist_pending) */
static void hist_add(struct sbra *A, const char *code)
{
    int i, j;
    for (i = 0; i < A->nhist; i++) if (seq(A->hist[i], code)) break;
    if (i == A->nhist) { if (A->nhist < SBRA_HISTN) A->nhist++; i = A->nhist - 1; }
    for (j = i; j > 0; j--) scpy(A->hist[j], A->hist[j - 1], SBR_CODE_MAX);
    scpy(A->hist[0], code, SBR_CODE_MAX);
}
const char *sbra_hist_pending(const struct sbra *A) { return A->npend > 0 ? A->hist_pend[0] : 0; }
void sbra_hist_done(struct sbra *A)
{
    int i;
    if (A->npend <= 0) return;
    for (i = 1; i < A->npend; i++) scpy(A->hist_pend[i - 1], A->hist_pend[i], SBR_CODE_MAX);
    A->npend--;
}
void sbra_hist_set(struct sbra *A, const char (*codes)[9], int n)
{
    int i;
    char keep[SBRA_HISTP][SBR_CODE_MAX];
    int nk = A->npend;
    for (i = 0; i < nk; i++) scpy(keep[i], A->hist_pend[i], SBR_CODE_MAX);
    A->nhist = 0;
    for (i = n - 1; i >= 0; i--) if (sbr_code_valid(codes[i])) hist_add(A, codes[i]);   /* oldest first, so the newest ends up first */
    for (i = 0; i < nk; i++) hist_add(A, keep[i]);                                     /* unwritten codes are newer than anything on the card */
    A->hist_seeded = 1;
}
static int tag_matches(const u8 *input, u32 n, const char *code)
{
    u32 i, cl = slen(code);
    for (i = 0; i < n; i++) {
        u16 s = i < cl ? to_sjis(code[i]) : 0;
        if (input[i * 3] != (u8)(s >> 8) || input[i * 3 + 1] != (u8)s) return 0;
    }
    return 1;
}
int sbra_be(struct sbra *A, const u8 *req, u8 *rsp)
{
    const u8 *input = req + 1;
    u32 n = req[25], init = get32(req + 26), cur, i, cl;
    u8 scroll = req[30];
    int found = -1;
    A->st->be++;
    if (n > 8) n = 8;
    cur = init;
    if (scroll == 1) cur++;
    else if (scroll == 2) cur = cur > 0 ? cur - 1 : cur;
    else if (scroll == 3) cur = 0;
    while (cur < (u32)A->nhist) {
        if (tag_matches(input, n, A->hist[cur])) { found = (int)cur; break; }
        cur = scroll == 2 ? cur - 1 : cur + 1;
    }
    if (found < 0 && init < (u32)A->nhist && tag_matches(input, n, A->hist[init])) found = (int)init;
    zero(rsp, 30);
    if (found < 0) {
        for (i = 0; i < 3 * n; i++) rsp[1 + i] = input[i];
        rsp[25] = (u8)n;
        put32(rsp + 26, init);
        return 0;
    }
    rsp[0] = 1;
    cl = slen(A->hist[found]);
    for (i = 0; i < 8; i++) {
        u16 s = i < cl ? to_sjis(A->hist[found][i]) : 0;
        rsp[1 + 3 * i] = (u8)(s >> 8);
        rsp[2 + 3 * i] = (u8)s;
    }
    rsp[25] = (u8)(cl > 8 ? 8 : cl);
    put32(rsp + 26, (u32)found);
    return 0;
}

/* ------------------------------------------------------------------ core callbacks */
static int c_send(void *ctx, u32 ip, u16 port, const u8 *d, u32 len)
{
    struct sbra *A = (struct sbra *)ctx;
    return A->io && A->io->send ? A->io->send(A->io->ctx, ip, port, d, len) : -1;
}
static const struct sbr_ops CORE_OPS = { c_send };

#ifndef SBRA_NO_PEER_PROBE
/* ------------------------------------------------------------------ peer probe (same local port) */
static int p_send(void *ctx, sde_u32 ip, sde_u16 port, const sde_u8 *d, sde_u32 len) { return c_send(ctx, ip, port, d, len); }
static void p_connect(void *ctx, int peer)
{
    struct sbra *A = (struct sbra *)ctx;
    if (!A->p_active) return;
    if (peer != A->p_peer && A->p_peer >= 0 && sdenet_state(&A->P, A->p_peer) != SDE_CONNECTED) {
        sdenet_reset_peer(&A->P, A->p_peer);      /* the opponent's own connect won the race: keep that one */
    }
    A->p_peer = peer;
    A->p_state = SBRA_P_CONNECTED;
    A->st->peer = SBRA_P_CONNECTED;
    if (sbr_state(&A->B) == SBR_OPP_CONN) sbr_peer_result(&A->B, A->now, 1);
}
static void p_disconnect(void *ctx, int peer, sde_u32 data, int reason)
{
    struct sbra *A = (struct sbra *)ctx;
    (void)data; (void)reason;
    if (!A->p_active || peer != A->p_peer) return;
    A->p_state = SBRA_P_FAILED;
    A->st->peer = SBRA_P_FAILED;
}
static void p_receive(void *ctx, int peer, sde_u8 ch, const sde_u8 *d, sde_u32 len) { (void)ctx; (void)peer; (void)ch; (void)d; (void)len; }
static const struct sdenet_ops PEER_OPS = { p_send, p_connect, p_disconnect, p_receive };
#endif

static int engine_mode(const struct sbra *A) { return (A->cfg->flags & SBRA_F_ENGINE_PEER) && A->io && A->io->eng; }
int sbra_engine_live(const struct sbra *A) { return A->eng_live; }

static void peer_close(struct sbra *A)
{
    if (A->eng_live && A->io && A->io->eng && A->io->eng->end) A->io->eng->end(A->io->ctx);   /* M1: the failed attempt's engine session must not outlive the assignment */
    A->eng_live = 0;
#ifndef SBRA_NO_PEER_PROBE
    if (A->p_active) {
        if (A->p_peer >= 0 && sdenet_state(&A->P, A->p_peer) == SDE_CONNECTED) {
            sdenet_disconnect(&A->P, A->p_peer, 0);
            if (A->net_up) sdenet_service(&A->P, A->now);
        }
        zero(&A->P, sizeof A->P);
    }
#endif
    A->p_active = 0;
    A->p_peer = -1;
    A->p_state = SBRA_P_IDLE;
    A->st->peer = SBRA_P_IDLE;
}

static void peer_start(struct sbra *A)
{
    const struct sbr_match *m = &A->B.match;
    A->st->peer_ip = m->opp_ip;
    A->st->peer_port = m->opp_port;
    A->p_t = A->now;
    if (A->cfg->flags & SBRA_F_PEER_STUB) {
        A->p_active = 0;
        A->p_state = SBRA_P_STUB;
        A->st->peer = SBRA_P_STUB;
        return;
    }
#ifndef SBRA_NO_PEER_PROBE
    zero(&A->P, sizeof A->P);
    A->seed = A->seed * 1664525u + 1013904223u;
    sdenet_init(&A->P, &PEER_OPS, A, A->seed ^ A->now);
    sdenet_set_allow(&A->P, m->opp_ip, m->opp_port, 1);
    A->p_active = 1;
    A->p_peer = sdenet_connect(&A->P, m->opp_ip, m->opp_port, 3, 0, A->now);
    A->p_state = A->p_peer >= 0 ? SBRA_P_CONNECTING : SBRA_P_FAILED;
    A->st->peer = (u32)A->p_state;
    if (A->p_peer >= 0) sdenet_service(&A->P, A->now);
#else
    /* product image (SBRA_NO_PEER_PROBE,  D1): no probe host exists; the engine hand-off is the only real peer path */
    A->p_state = SBRA_P_FAILED;
    A->st->peer = SBRA_P_FAILED;
#endif
}

static void bcopy_(void *d, const void *s, u32 n) { u8 *dd = (u8 *)d; const u8 *ss = (const u8 *)s; while (n--) *dd++ = *ss++; }
static void use_baked(struct sbra *A)
{
    bcopy_(&A->id, &A->bid, (u32)sizeof A->id);
    sbr_identity(&A->B, &A->id);
    A->ident_final = 1;
    A->ident_src = 2;
    A->st->ident_ok = (u32)A->B.id_ok;
}
void sbra_set_baked_ident(struct sbra *A, const sbr_u8 *payload, int baked_first)
{
    A->baked_first = baked_first != 0;
    A->bid_ok = 0;
    if (payload && sbr_ident_parse(payload, SBR_ID_PAYLOAD_LEN, &A->bid) == SBR_ID_OK) A->bid_ok = 1;
    if (A->bid_ok && A->baked_first) use_baked(A);
}
const char *sbra_ident_words(const struct sbra *A)
{
    return A->ident_src == 2 ? "Identity: baked in the image" : A->ident_src == 1 ? "Identity: memory card" : 0;
}

/* Why the Slippi rows do not exist: the only reason left is a missing identity (the player can fix that one).  0 = logged in (or nothing to say yet). */
const char *sbra_login_words(const struct sbra *A)
{
    if (A->B.id_ok) return 0;
    switch (A->st->ident) {
    case SBRC_L_NOFILE: return A->bid_ok ? "No identity (card A, image)" : "No identity on card A";
    case SBRC_L_NOCARD: return "No memory card (identity)";
    case SBRC_L_DAMAGED: return "Identity file damaged";
    case SBRC_L_NEWER: return "Identity file too new";
    default: return "Identity not found on card";            /* not read yet, card busy or a read error: the same words, the wait is bounded by SBR_IDENT_SETTLE_MS */
    }
}
int sbra_ident_done(const struct sbra *A) { return A->ident_final; }

/* ------------------------------------------------------------------ init / net */
void sbra_init(struct sbra *A, const struct sbra_cfg *cfg, struct sbra_status *st, const struct sbra_io *io,
               const sddns_io *dns_io, u32 seed)
{
    struct sbr_cfg c;
    int i;
    zero(A, sizeof *A);
    zero(st, sizeof *st);
    A->cfg = cfg;
    A->st = st;
    A->io = io;
    A->seed = seed | 1u;
    A->rng = seed | 1u;
    A->p_peer = -1;
    st->magic = 0x53425241u;
    st->nc_state = SBR_NC_PENDING_INIT;            /* 0 in a release build, forever */
#ifdef MS_LIVE_DIAG
    { extern void ld_install(void); ld_install(); }  /* LIVE DIAGNOSTIC (opt-in build option livediag=on, offline/bridge/ld_diag.c): status rows on the screen outside a match */
#endif
    zero(&c, sizeof c);
    c.server_ip = cfg->mm_ip;
    c.server_port = (u16)cfg->mm_port;
    c.local_port = (u16)cfg->local_port;
    c.dns_ip = cfg->dns_ip;
    c.dns_port = (u16)cfg->dns_port;
    c.host = cfg->mm_name;                         /* static cfg block: stays valid; the overlay resolves it (the resident does no matchmaker DNS any more) */
    c.connect_ms = cfg->connect_ms;
    c.create_ms = cfg->create_ms;
    c.peer_ms = cfg->peer_ms;
    c.reticket_max = cfg->reticket_max;
    c.platform = cfg->platform;
    scpy(c.app_version, cfg->app_version, sizeof c.app_version);
    sbr_init(&A->B, &c, &CORE_OPS, A);
    sddns_init(&A->dns, dns_io, seed ^ 0x444E53u);
#ifndef SBR_NETCFG
    A->nc_final = 1;                                /* release build: there is no card file but the identity */
#endif
    for (i = SBRA_HIST - 1; i >= 0; i--) if (cfg->hist[i][0] && sbr_code_valid(cfg->hist[i])) hist_add(A, cfg->hist[i]);
}

void sbra_set_net(struct sbra *A, int up, u32 net_state, u32 lan_ip, u32 dhcp_dns)
{
    A->net_up = (u32)(up != 0);
    A->dhcp_dns = dhcp_dns;
    A->st->net = net_state;
    A->st->ip = lan_ip;
    sbr_set_net(&A->B, up, up ? lan_ip : 0);
}

/* ------------------------------------------------------------------ card work (EXI context only) */
static void card_work(struct sbra *A)
{
    int r;
    if (!A->ident_final) {   /* (patched-first with a valid patched identity is final from sbra_set_baked_ident on: the card file is not read at all) */
        r = sbrc_read_ident(&A->id);
        A->st->ident = (u32)r;
        A->st->ident_parse = sbrc_st.id_parse;
        A->st->ident_slot = sbrc_st.id_slot;
        if (r == SBRC_L_OK) { A->ident_final = 1; A->ident_src = 1; sbr_identity(&A->B, &A->id); }
        else if (r == SBRC_L_NOFILE || r == SBRC_L_DAMAGED || r == SBRC_L_NEWER) A->ident_final = 1;
        else if (r == SBRC_L_ERROR && ++A->ident_err_tries >= CARD_ERROR_TRIES) A->ident_final = 1;
        if (A->ident_final && !A->B.id_ok && A->bid_ok) use_baked(A);   /* release order: the card had none, the patched identity is the fallback */
        A->st->ident_ok = (u32)A->B.id_ok;
    }
#ifdef SBR_NETCFG   /* dev/lab builds only (sbr_netcfg.h): read the card's address override (the only other card file) */
    if (!A->nc_final) {
        r = sbrc_read_netcfg();
        A->st->cache = (u32)r;
        A->st->cache_slot = sbrc_st.c_slot;
        if (r == SBRC_L_NOFILE) sbr_nc_no_file();
        A->st->nc_state = (u32)sbr_nc_state();
        A->st->nc_mm_ip = sbr_nc_mm_ip(0);
        if (r == SBRC_L_OK || r == SBRC_L_NOFILE || r == SBRC_L_DAMAGED || r == SBRC_L_NEWER) A->nc_final = 1;
        else if (r == SBRC_L_ERROR && ++A->cache_err_tries >= CARD_ERROR_TRIES) A->nc_final = 1;
    }
#endif
}

int sbra_card_poll(struct sbra *A, u32 now)
{
    if (!A->ident_final) {                         /* IDENT SETTLE: no definite answer in SBR_IDENT_SETTLE_MS (busy card, read errors; never for a missing card): not found */
        if (!A->t_first) A->t_first = now ? now : 1;
        else if (now - A->t_first >= SBR_IDENT_SETTLE_MS && A->st->ident != SBRC_L_NOCARD) {
            A->ident_final = 1;
            if (!A->B.id_ok && A->bid_ok) use_baked(A);
            A->st->ident_ok = (u32)A->B.id_ok;
        }
    }
    if (A->ident_final && A->nc_final) return 0;
    if (A->t_card && now - A->t_card < SBR_CARD_POLL_MS) return 0;
    A->t_card = now ? now : 1;
    card_work(A);
    return 1;
}

/* ------------------------------------------------------------------ EXI commands */
int sbra_b9(struct sbra *A, u8 *osb)
{
    A->st->b9++;
    card_work(A);
    zero(osb, 42);
    /* "logged in" (the search rows exist) only with a usable identity: otherwise Slippi's logged-out form,
       i.e. no Unranked / Direct row at all (hidden, not greyed).  Never 2 ("update required" would offer an update). */
    osb[0] = (u8)sbra_app_state(A);
    if (osb[0]) {
        name_for_game(osb + 1, A->id.name, NAME_LEN);
        code_for_game(osb + 32, A->id.code);
    }
    A->st->app_state = osb[0];
    return 0;
}

int sbra_b4(struct sbra *A, u32 now, u8 mode, const u8 *code18)
{
    char code[SBR_CODE_MAX + 2];
    A->now = now;
    A->st->b4++;
    if (mode != SBR_MODE_UNRANKED && mode != SBR_MODE_DIRECT) A->st->refused++;
    if (code_from_game(code18, code, sizeof code) < 0 || slen(code) >= SBR_CODE_MAX) code[0] = 0;
    A->want_mode = mode;
    scpy(A->want_code, mode == SBR_MODE_DIRECT ? code : "", sizeof A->want_code);
    if (mode == SBR_MODE_DIRECT && sbr_code_valid(code)) {
        hist_add(A, code);                         /* the list the Direct prompt completes from, at once */
        if (A->npend < SBRA_HISTP) scpy(A->hist_pend[A->npend++], code, SBR_CODE_MAX);   /* and the card file, written by the glue outside the interrupt-off window */
    }
    A->want_search = 1;                            /* the network part runs in the next slice (EXI lock held there) */
    A->stub_ready = 0; A->t_both = 0;            /* selections stay (Slippi sends B5 before B4) */
    return 0;
}

int sbra_b5(struct sbra *A, const u8 *p)
{
    static const u16 STAGES[6] = { 0x2, 0x3, 0x8, 0x1C, 0x1F, 0x20 };
    u8 char_opt = p[4], stage_opt = p[7];
    A->st->b5++;
    A->rng_offset = rnd(A) % 0xFFFFu;
    if (stage_opt == 1 || stage_opt == 3) {
        A->sel_stage = stage_opt == 3 ? STAGES[rnd(A) % 6u] : (u16)((p[5] << 8) | p[6]);
        A->sel_stage_ok = 1;
        A->sel_alt = p[9];
    }
    if (char_opt) {
        A->sel_char = p[2];
        A->sel_color = p[3];
        A->sel_team = p[1];
        A->sel_char_ok = 1;
    }
    A->st->local_ready = (u32)A->sel_char_ok;
    if (engine_mode(A)) (void)A->io->eng->b5(A->io->ctx, p);   /* ENGINE HAND-OFF: the engine keeps its own copy of the lock-in (merged into the selections it sends) */
    return 0;
}

static void cleanup(struct sbra *A)                /* Slippi's handleConnectionCleanup (behaviour) */
{
    A->want_search = 0;
    A->dns_pending = 0;
    A->want_cancel = 1;                            /* the network part (core cancel, peer close) runs in the next slice */
    A->sel_char_ok = A->sel_stage_ok = 0;
    A->sel_char = A->sel_color = A->sel_team = A->sel_alt = 0;
    A->sel_stage = 0;
    A->rng_offset = 0;
    A->stub_ready = 0;
    A->t_both = 0;
    A->st->local_ready = A->st->remote_ready = 0;
}

void sbra_ba(struct sbra *A)
{
    A->st->ba++;
    cleanup(A);
    /* the MSRB must read idle at once (the CSS asks right after BA): the core state is reset here, its sockets in the slice.
       sbr_cancel itself never sends (the matchmaker close is an unsequenced best effort, then the host is wiped). */
    sbr_cancel(&A->B);
}

static u8 mm_state(struct sbra *A)
{
    int s = sbr_state(&A->B);
    if (s == SBR_IDLE && (A->want_search || A->dns_pending)) return 1;   /* waiting for the slice / DNS: "initializing" */
    return (u8)s;
}

/* 1 when no search, ticket, matchmaker or opponent connection is open or being started (the report client may use the network on the idle online character screen) */
int sbra_idle(const struct sbra *A)
{
    int s = sbr_state(&A->B);
    return (s == SBR_IDLE || s == SBR_ERROR) && !A->want_search && !A->dns_pending;
}

int sbra_b3(struct sbra *A, u32 now, u8 *m)
{
    u8 mm;
    int conn, li, ri;
    u32 i;
    const char *et;
    (void)now;
    A->st->b3++;
    mm = mm_state(A);
    conn = mm == SBR_CONNECTED;
    zero(m, SBRA_MSRB_SIZE);
    m[M_STATE] = mm;
    A->st->state = mm;
    A->st->mm_seen |= 1u << mm;
    m[M_LREADY] = (u8)A->sel_char_ok;
    m[M_RREADY] = (u8)(conn && A->stub_ready);
    li = (mm == SBR_OPP_CONN || conn) ? (int)(A->B.match.local_idx & 1u) : 0;
    ri = li ^ 1;
    m[M_LIDX] = (u8)li;
    m[M_RIDX] = (u8)ri;
    A->st->local_idx = (u32)li;
    A->st->remote_ready = m[M_RREADY];
    m[M_DELAY] = (u8)(A->cfg->delay < 1 ? 1 : A->cfg->delay > 15 ? 15 : A->cfg->delay);
    if (mm == SBR_OPP_CONN || conn) {
        const struct sbr_match *x = &A->B.match;
        name_for_game(m + M_LNAME, A->id.name, NAME_LEN);
        name_for_game(m + M_PNAME + NAME_LEN * li, A->id.name, NAME_LEN);
        name_for_game(m + M_PNAME + NAME_LEN * ri, x->opp_name, NAME_LEN);
        name_for_game(m + M_OPPNAME, x->opp_name, NAME_LEN);
        code_for_game(m + M_PCODE + CODE_LEN * li, A->id.code);
        code_for_game(m + M_PCODE + CODE_LEN * ri, x->opp_code);
        for (i = 0; x->match_id[i] && i < 50; i++) m[M_MATCHID + i] = (u8)x->match_id[i];
        for (i = 0; A->id.uid[i] && i < 28; i++) m[M_PUID + 29 * li + i] = (u8)A->id.uid[i];
        for (i = 0; x->opp_uid[i] && i < 28; i++) m[M_PUID + 29 * ri + i] = (u8)x->opp_uid[i];
    }
    if (mm == SBR_ERROR) {
        et = sbr_error_text(&A->B);
        for (i = 0; et[i] && i < ERR_CHARS; i++) m[M_ERR + i] = (u8)et[i];
        words(A->st->err_text, 32, et);
    }
    for (i = 0; i < 78; i++) put32(m + M_MATCH + 4 * i, MATCH_TEMPLATE[i]);
    if (A->eng_live && A->io->eng) {               /* ENGINE HAND-OFF: match block, rngOffset and alt-stage mode from the engine (selections of both players) */
        u8 scr[SBRA_MSRB_SIZE];
        zero(scr, sizeof scr);
        if (A->io->eng->b3(A->io->ctx, scr) == 0) {
            for (i = 0; i < 0x138; i++) m[M_MATCH + i] = scr[M_MATCH + i];
            for (i = 0; i < 4; i++) m[M_RNG + i] = scr[M_RNG + i];
            m[M_ALT] = scr[M_ALT];
            if (scr[M_DELAY] >= 1u && scr[M_DELAY] <= 15u) m[M_DELAY] = scr[M_DELAY];   /* the engine's input delay (the Settings / CSS word s3_cfg[3] through its delay hook, s7_core b3 msrb[9]): the bridge's own value is the patched cfg->delay (2), which made every Slippi match 2f whatever the player set */
            if (conn && scr[M_LIDX] <= 1u) {       /* : Slippi, once connected 1v1: localPlayerIndex = isDecider ? 0: 1 (not the ticket's port); the engine's side is the same rule */
                m[M_LIDX] = scr[M_LIDX]; m[M_RIDX] = (u8)(scr[M_LIDX] ^ 1u);
                A->st->local_idx = scr[M_LIDX];
            }
        }
        {
            sbr_u32 r = A->io->eng->ready(A->io->ctx);
            if (r & SBRA_ENG_FLAGS) {              /* the engine owns both lock-in flags (cleared when a game starts): a finished game must not read "both locked in" */
                m[M_LREADY] = (u8)((r & SBRA_ENG_LOCAL) ? 1 : 0);
                m[M_RREADY] = (u8)(conn && (r & SBRA_ENG_REMOTE) ? 1 : 0);
                A->st->local_ready = m[M_LREADY];
            } else m[M_RREADY] = (u8)(conn && r);
        }
        A->st->remote_ready = m[M_RREADY];
    } else if (m[M_LREADY] && m[M_RREADY]) {       /* LAB stub opponent only (SBRA_F_STUB_MATCH) */
        u8 *mb = m + M_MATCH;
        u8 rc = (u8)(A->cfg->stub_sel >> 24), rcol = (u8)(A->cfg->stub_sel >> 16);
        u32 rng = A->B.match.is_host ? A->rng_offset : (get32((const u8 *)A->B.match.match_id) % 0xFFFFu);
        u16 stage = A->sel_stage_ok ? A->sel_stage : 0x1F;
        int p;
        put32(m + M_RNG, rng);
        mb[0x60 + li * 0x24] = A->sel_char;  mb[0x63 + li * 0x24] = A->sel_color;
        mb[0x60 + ri * 0x24] = rc;           mb[0x63 + ri * 0x24] = rcol;
        for (p = 0; p < 2; p++) { mb[0x61 + p * 0x24] = 0; mb[0x67 + p * 0x24] = 0; mb[0x69 + p * 0x24] = 0; }
        mb[0x61 + 2 * 0x24] = 3;             mb[0x61 + 3 * 0x24] = 3;
        if (mb[0x60] == mb[0x60 + 0x24] && mb[0x63] == mb[0x63 + 0x24]) mb[0x67 + 0x24] = 1;
        mb[0xE] = (u8)(stage >> 8); mb[0xF] = (u8)stage;
        mb[0x8] = 0;
        mb[2] &= 0xF7u;
        m[M_ALT] = A->sel_stage_ok ? A->sel_alt : 0;
    }
    return 0;
}

int sbra_b0(struct sbra *A, const u8 *txb, u8 *rxb)  /* LAB stub opponent: neutral pads, never desyncs */
{
    u32 frame = get32(txb + 1);
    A->st->b0++;
    if (!(A->cfg->flags & SBRA_F_STUB_MATCH) || sbr_state(&A->B) != SBR_CONNECTED) return -1;
    rxb[0] = 1;
    rxb[1] = 1;
    put32(rxb + 2, get32(txb + 5));
    put32(rxb + 6, get32(txb + 9));
    put32(rxb + 26, frame);
    put32(rxb + 30, frame);
    put32(rxb + 34, frame);
    put32(rxb + 38, frame);
    return 0;
}

int sbra_row_hidden(u32 kind, u32 sel, u32 logged_in)
{
    return logged_in == 1u && kind == SBRA_KIND_ONLINE && (sel == 0u || sel == 3u || sel == 4u);   /* Ranked, Teams, Party (R6) */
}

int sbra_app_state(const struct sbra *A) { return A->B.id_ok; }

/* ------------------------------------------------------------------ network side */
void sbra_rx(struct sbra *A, u32 now, u32 ip, u16 port, const u8 *d, u32 len)
{
    A->now = now;
    if (!A->net_up) return;
#ifndef SBRA_NO_PEER_PROBE
    if (A->p_active && ip == A->B.match.opp_ip && port == A->B.match.opp_port) { sdenet_rx(&A->P, now, ip, port, d, len); return; }
#endif
    (void)ip; (void)port; (void)d; (void)len;      /* the 3.7.0 matchmaker is a TCP WebSocket in the overlay: nothing UDP belongs to the core */
}

static void do_search(struct sbra *A)
{
    (void)sbr_search(&A->B, A->now, A->want_mode, A->want_code);
    A->st->searches = A->B.st.searches;
}

void sbra_tick(struct sbra *A, u32 now)
{
    int s;
    A->now = now;
    if (A->want_cancel) {
        A->want_cancel = 0;
        peer_close(A);
        sddns_abort(&A->dns);
        A->dns_pending = 0;
    }
    /* a search asked for by B4 */
    if (A->want_search) {
        A->want_search = 0;
        if (sbr_state(&A->B) != SBR_IDLE) { sbr_cancel(&A->B); peer_close(A); }
        /* the matchmaker's name is resolved by the overlay's client: this layer only hands it the name and the lab overrides */
        A->B.cfg.server_ip = sbr_nc_mm_ip(A->cfg->mm_ip);
        A->B.cfg.server_port = (u16)A->cfg->mm_port;
        A->B.cfg.dns_ip = sbr_nc_dns_ip(A->cfg->dns_ip) ? sbr_nc_dns_ip(A->cfg->dns_ip) : A->dhcp_dns;
        A->B.cfg.dns_port = (u16)A->cfg->dns_port;
        if (is_mm2(A->cfg->mm_name)) sbr_fail_fault(&A->B, now, SBR_E_DNS, "Matchmaker name refused");
        else do_search(A);                         /* refusals (mode, identity, code, network) happen in the core */
    }
    sbr_tick(&A->B, now);
    s = sbr_state(&A->B);
    if (s == SBR_OPP_CONN && A->B.match.ready && (A->cfg->flags & SBRA_F_STOP_AT_ASSIGN)) {
        char t[80];                                /* "Opponent found: CODE Name, stopped - test mode": name cut so the tail survives the 84-char room */
        char nm_[17];
        scpy(t, "Opponent found: ", sizeof t);
        scat(t, A->B.match.opp_code, sizeof t);
        scpy(nm_, A->B.match.opp_name, sizeof nm_);
        if (nm_[0]) { scat(t, " ", sizeof t); scat(t, nm_, sizeof t); }
        scat(t, ", stopped - test mode", sizeof t);
        sbr_fail_fault(&A->B, now, SBR_E_PEER, t);
        s = sbr_state(&A->B);
    }
    if (s == SBR_OPP_CONN && A->B.match.ready && A->p_state == SBRA_P_IDLE && (A->cfg->flags & SBRA_F_ENGINE_PEER)) {
        /* ENGINE HAND-OFF: the rollback engine connects to the opponent from the SAME local port and plays the match */
        if (!engine_mode(A) || A->io->eng->begin(A->io->ctx, &A->B.match, A->B.st.mode) != 0) {
            sbr_fail_fault(&A->B, now, SBR_E_PEER, "Engine session not available");
            s = sbr_state(&A->B);
        } else {
            A->eng_live = 1;
            A->p_t = now;
            A->p_state = SBRA_P_CONNECTING;
            A->st->peer = SBRA_P_CONNECTING;
            A->st->peer_ip = A->B.match.opp_ip;
            A->st->peer_port = A->B.match.opp_port;
        }
    } else if (s == SBR_OPP_CONN && A->B.match.ready && A->p_state == SBRA_P_IDLE) peer_start(A);
    if (s == SBR_OPP_CONN && A->eng_live) {         /* the engine's ENet session is the peer step now */
        int es = A->io->eng->state(A->io->ctx);
        if (es > 0) { A->p_state = SBRA_P_CONNECTED; A->st->peer = SBRA_P_CONNECTED; sbr_peer_result(&A->B, now, 1); s = sbr_state(&A->B); }
        else if (es < 0) { A->p_state = SBRA_P_FAILED; A->st->peer = SBRA_P_FAILED; }
    }
    if (s == SBR_OPP_CONN && A->p_state == SBRA_P_STUB && now - A->p_t >= PEER_STUB_MS) sbr_peer_result(&A->B, now, 1);
    if (s != SBR_OPP_CONN && s != SBR_CONNECTED && A->p_state != SBRA_P_IDLE) peer_close(A);   /* re-ticket, error, cancel */
#ifndef SBRA_NO_PEER_PROBE
    if (A->p_active && A->net_up) sdenet_service(&A->P, now);
#endif
    if (s == SBR_CONNECTED && (A->cfg->flags & SBRA_F_STUB_MATCH) && !A->stub_ready && A->sel_char_ok) {
        if (!A->t_both) A->t_both = now ? now : 1;
        else if (now - A->t_both >= A->cfg->stub_ready_ms) A->stub_ready = 1;
    }
    /* numbers for the harness (never a secret) */
    A->st->state = (u32)mm_state(A);
    A->st->err = A->B.st.err;
    A->st->mode = A->B.st.mode;
    A->st->searches = A->B.st.searches;
    A->st->tickets = A->B.st.tickets;
    A->st->assigned = A->B.st.assigned;
    A->st->retickets = A->B.st.retickets;
    A->st->cancels = A->B.st.cancels;
    A->st->wipes = A->B.st.wipes;
    A->st->peer_ok = A->B.st.peer_ok;
    A->st->peer_fail = A->B.st.peer_fail;
    A->st->is_host = A->B.match.is_host;
}

/* ------------------------------------------------------------------ sdshim vtable */
static struct sbra *g_A;
static u32 (*g_clock)(void);
static int v_b0(void *c, const sdshim_u8 *txb, sdshim_u8 *rxb) { (void)c; return sbra_b0(g_A, txb, rxb); }
static int v_b3(void *c, sdshim_u8 *msrb) { (void)c; return sbra_b3(g_A, g_clock(), msrb); }
static int v_b4(void *c, sdshim_u8 mode, const sdshim_u8 *code) { (void)c; return sbra_b4(g_A, g_clock(), mode, code); }
static int v_b5(void *c, const sdshim_u8 *pstb) { (void)c; return sbra_b5(g_A, pstb); }
static int v_b9(void *c, sdshim_u8 *osb) { (void)c; return sbra_b9(g_A, osb); }
static void v_ba(void *c) { (void)c; sbra_ba(g_A); }
static int v_be(void *c, const sdshim_u8 *req, sdshim_u8 *rsp) { (void)c; return sbra_be(g_A, req, rsp); }
static void v_ui(void *c, sdshim_u8 cmd) { (void)c; (void)cmd; g_A->st->other++; }     /* B6/B7/B8: no login, logout or update here */
static void v_rep(void *c, const sdshim_u8 *rgb) { (void)c; (void)rgb; g_A->st->other++; }   /* BD: no reports to Slippi (no TLS) */
static void v_rst(void *c, sdshim_u8 cmd, sdshim_u8 arg) { (void)c; (void)cmd; (void)arg; g_A->st->other++; }
static const struct sdshim_backend BACKEND = { 0, v_b0, v_b3, v_b4, v_b5, v_b9, v_ba, v_ui, v_be, v_rep, v_rst, 0, 0 };

const struct sdshim_backend *sbra_backend(struct sbra *A, u32 (*clock_ms)(void))
{
    g_A = A;
    g_clock = clock_ms;
    return &BACKEND;
}
