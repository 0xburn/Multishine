/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Combined image 'Multishine + Slippi bridge': the Slippi bridge (sbr_app, our own code) living inside the
 * ONLINE image (s12_gc.c with -DBR_BUILD) instead of next to it. What does in the stand-alone bridge image (BBA bring-up, sockets, the
 * network slice, the EXI entry, the unlock override) is done here on top of the online glue's ONE network stack:
 *
 *   - the BBA, the sdnet instance and the DHCP client belong to s12_gc.c; it calls br_link_up once the link is up (the bridge binds its three
 *     sockets there: matchmaker + peer on cfg.local_port, DNS), br_slice from its network slice (EXI lock held, interrupts off:
 *     the only place the bridge may send) and br_tick once per scene-loop iteration (card poll, Slippi's r13 app-state byte);
 *   - the EXI commands of Slippi's online menus (B0..BE) go to ONE of two backends: the Multishine backend (on_backend.c: our own lab server
 *     and the rollback engine; rows of the Multishine hub) or the bridge (sbra_backend: Slippi's matchmaker, rows of the Slippi entry).  The choice
 *     is made when the search starts (B4) from the screen the player started it on: MenuFlow.cur_menu 8 = Slippi's own online screen = bridge,
 *     anything else (the layout's hub / More) = Multishine.  It stays until Slippi's cleanup (BA).  B9 (status) and BE (code suggestions) before
 *     a search follow the same rule, B9 runs both backends (each one reads its own card file) and answers with the chosen one.
 *   - the unlock hook (mn_80229938): kind 8 (Slippi's screen) is decided by the bridge rule (Ranked, Teams, Party do not exist), every other kind by
 *     the layout (ml_unlock_override).  ml_stub.S is compiled with -Dml_unlock_override=br_unlock_override for this.
 *
 * Nothing here talks to a host that is not in the cfg block (the resolver, the matchmaker address or the name, the opponent
 * the matchmaker assigned).  The Multishine backend never sees a Slippi datagram and the other way round: they use different sockets.
 */
#include "../net/sdnet.h"
#include "../exi_shim/sdshim.h"
#include "sbr_app.h"
#include "sbr_card.h"
#include "rep_scene.h"
#include "tmm_core.h"
/* the loader's status block: list base + 0x3600 of the card layout the engine is built for (stock 0x817F7600, CL_LAYOUT=generic 0x817E3600, low 0x817C3600); MS_CARD_WA_ADDR is the layout's loader work area (release.sh WA_ADDR, card_payload.py) */
#if defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817D4000u
#define MS_CL_STATUS_ADDR 0x817E3600u
#elif defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817B4000u
#define MS_CL_STATUS_ADDR 0x817C3600u
#else
#define MS_CL_STATUS_ADDR 0x817F7600u
#endif

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef BR_DATA
#define BR_DATA __attribute__((section(".data")))
#endif

#define R13_APP_STATE (*(volatile u8 *)0x804D6641)     /* r13-0x505F OFST_R13_APP_STATE (Slippi Online/Online.s), what B9 fills */
#define R13_NAME_ENTRY (*(volatile u8 *)0x804D6643)    /* r13-0x505D OFST_R13_NAME_ENTRY_MODE: 0 normal, 1 connect code keyboard (see br_tick) */
#ifdef MS_LAB_POISON_NE
const char sbr_ne_lab_marker[] = "MS-NE-LAB";
#endif
#define SCENE_WORD (*(volatile u32 *)0x80479D30)
#define MENU_CUR (*(volatile u8 *)0x804A04F0)          /* MenuFlow.cur_menu (8 = Slippi's online submenu) */

extern void df_poll(void) __attribute__((weak));
#ifdef SD_CARD_HOLE
extern void tt_frame(void) __attribute__((weak));     /* menus/tt/tt_glue.c (card route): the title text band */
#endif
extern int df_pack_state(void) __attribute__((weak));   /* 2 = pack damaged */       /* menus/df/df_glue.c, linked in the card-route image only */
extern int omc_slippi_sync(const char *add, char out[][9], int max) __attribute__((weak));   /* menus/on/on_hist.c (card-route / hub images): the '#' direct code history in MultishineCodes */
/* Slippi game reports (offline/multishine/menus/rep/rep_glue.c, linked ONLY in images that carry the report client; absent = nothing happens) */
extern void rep_game_hook(const u8 *rgb, const char *match_id, const char *opp_uid, u32 local_idx) __attribute__((weak));
extern void rep_menu_hook(void) __attribute__((weak));
extern void rep_search_hook(void) __attribute__((weak));   /* a search / lock-in starts: the report overlay (if loaded) is closed and freed right now */
extern volatile u32 od_end_req;                      /* on_backend.c: set by BA, consumed by s12_gc.c peer_watch (ends the engine session) */
extern int ms_slippi_mm_off(void) __attribute__((weak));   /* menus/on/on_backend.c: 1 = our server switched Slippi matchmaking off for this console (HELLO caps2 bit 2, admin slippi-mm) */
#define MM_OFF() (ms_slippi_mm_off && ms_slippi_mm_off())
extern u32 od_clock_ms(void);                          /* s12_gc.c: the one clock of the image */
extern int ml_unlock_override(u32 kind, u32 sel);      /* menu layout: 0 no opinion, 1 hidden, 2 shown */

/* s3 state numbers of the online glue */
#define BR_S3_WAIT 0u
#define BR_S3_INIT 1u
#define BR_S3_LINK 2u
#define BR_S3_DHCP 3u
#define BR_S3_NOBBA 0x80u

struct sbra_cfg sbr_cfg BR_DATA = {
    { 'S', 'B', 'R', 'C', 'F', 'G', '0', '1' }, SBRA_F_ENGINE_PEER, 0u, 443u, 0u, 41400u, 41402u, 120u,   /* mm_port 443: the 3.7.0 WebSocket matchmaker is wss (was 43113, the ENet matchmaker) */
    10000u, 5000u, 8000u, 10u, SBR_PLAT_GC, 2u, 2500u, 0x14000000u, 0u,
    SBRA_APP_VERSION, "", { "", "", "", "" } };
struct sbra_status sbr_status BR_DATA;
/* PATCHED IDENTITY slot (build_sbr.py patch-ident): magic + the 0x12C-byte identity payload (sbr_ident.h); all zero = none patched.  Precedence: lab/dev builds
   (SBRA_BAKED_FIRST 1, the default for now) read the patched identity first and the card's MultishineIdentity second; a release build compiles with
   -DSBRA_BAKED_FIRST=0: card first, the patched identity only as the fallback.  A Melee save file is never an identity source. */
#ifndef SBRA_BAKED_FIRST
#define SBRA_BAKED_FIRST 1
#endif
struct { u8 magic[8]; u8 payload[0x12C]; } sbr_baked BR_DATA = { { 'S', 'B', 'R', 'B', 'I', 'D', '0', '1' }, { 0 } };
/* RAM proof words of the combination: [0] 'BRG1', [1] backend of the last search (0 none, 1 Multishine, 2 bridge),
   [2] B4 routed to Multishine, [3] B4 routed to the bridge, [4] B9 asked, [5] last origin (MenuFlow.cur_menu at the last B4), [6] BA routed,
   [7] hub kinds poked, [8] link-ups, [9] slices */
volatile u32 br_st[16] BR_DATA = { 0x42524731u };   /* [12] tiered plan (first << 4 | second), [13] tiered phase | held << 8 | note << 16 | why << 24, [14] first-side leaves, [15] second-side starts (tmm) */
#define BRS(i) (br_st[i])

#define ST sbr_status

static struct sbra A BR_DATA;
static struct sddns_sdnet_ctx g_dctx BR_DATA;
static struct sdnet *g_S;
static int g_sock = -1, g_dsock = -1, g_locked, g_init, g_slice_done;
static u32 g_nst;
static const struct sdshim_backend *g_ms;              /* the Multishine backend (on_backend.c's composition with the engine) */
static const struct sdshim_backend *g_br;              /* the bridge backend */
static char g_fd[10];                                  /* SLIPPI FRIEND (Friends view): the armed Slippi connect code; the character screen's B4 searches it in Direct mode */
static int g_fd_css;                                   /* 1 once the armed flow reached the character screen: leaving it ends the arming */
static int g_flow;                                     /* the bridge owns the flow after Slippi's screen (see origin()) */
static int g_active;                                   /* 0 none, 1 Multishine, 2 bridge */
static struct sdshim_backend DISP BR_DATA;
#if MS_TMM
static struct tmm TM BR_DATA;                         /* tiered matchmaking state (the block further down) */
static void tm_clear(void);
#endif

static u32 tbl(void)
{
    u32 t;
#if defined(__powerpc__) || defined(__PPC__)
    __asm__ volatile("mftbl %0" : "=r"(t));
#else
    t = 0;
#endif
    return t;
}
static u32 clock_ms_void(void) { return od_clock_ms() + 1000u; }

/* DIRECT CODE HISTORY ON THE CARD.  Menus only, outside the interrupt-off window (the card SDK needs
   interrupts), never inside Slippi's online submenu, at most every 2 s: the codes B4 queued are written ('#' namespace, Mano a mano's '@' entries untouched)
   and the RAM list that answers 0xBE is replaced by the card's list, so the Direct prompt shows the history again after a power cycle. */
static void hist_poll(void)
{
    static u32 t;
    char codes[SBRA_HISTN][9];
    u32 now;
    int n, k;
    if (!omc_slippi_sync) return;
    now = clock_ms_void();
    if (t && now - t < 2000u) return;
    t = now ? now : 1u;
    n = omc_slippi_sync(sbra_hist_pending(&A), codes, SBRA_HISTN);
    if (sbra_hist_pending(&A)) sbra_hist_done(&A);
    for (k = 0; k < SBRA_HISTP && sbra_hist_pending(&A); k++) {   /* more than one search since the last poll: the rest, same call */
        n = omc_slippi_sync(sbra_hist_pending(&A), codes, SBRA_HISTN);
        sbra_hist_done(&A);
    }
    if (n >= 0) sbra_hist_set(&A, (const char (*)[9])codes, n);
}


static u32 irq_off(void)
{
    u32 m;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    __asm__ volatile("mtmsr %0" : : "r"(m & ~0x8000u) : "memory");
    return m;
}
static void irq_restore(u32 m) { __asm__ volatile("mtmsr %0" : : "r"(m) : "memory"); }

/* ---- transport (valid only inside br_slice, where the online glue holds the EXI lock) ---- */
static int send_on(int sock, u32 ip, u16 port, const u8 *d, u32 len)
{
    int rc;
    if (!g_locked || sock < 0 || !g_S) return -1;
    rc = sdnet_sendto(g_S, sock, ip, port, d, (u16)len);
    return rc >= 0 ? 0 : -1;
}
static int send_main(void *ctx, sbr_u32 ip, sbr_u16 port, const sbr_u8 *d, sbr_u32 len) { (void)ctx; return send_on(g_sock, ip, port, d, len); }
/* ENGINE HAND-OFF (; only used with cfg flag SBRA_F_ENGINE_PEER): the online glue's engine plays the match against the real Slippi
   client.  s12_gc.c (BR_BUILD) provides br_engine_*; the engine sends and receives through THIS file's matchmaker/peer socket (same UDP port). */
extern int br_engine_begin(const void *m, int sock, u32 opp_ip);
extern int br_engine_state(void);
extern int br_engine_left_silence(void);
extern u32 br_engine_ready(void);
extern int br_engine_b0(void *ctx, const unsigned char *txb, unsigned char *rxb);
extern int br_engine_b3(void *ctx, unsigned char *msrb);
extern int br_engine_b5(void *ctx, const unsigned char *pstb);
struct br_slp_match {                                           /* byte-identical to struct s7_slippi_match */
    u32 is_decider, opp_ip, opp_port, fixed_rules, n_stages;
    u16 stages[16];
    u32 seed, char_color;
    u32 items;                                                  /* MATCH SETUP: the ticket's items (Slippi m_mmResult.items) */
};
static int e_begin(void *ctx, const struct sbr_match *m, sbr_u32 mode)
{
    struct br_slp_match sm;
    u32 i;
    (void)ctx;
    sm.is_decider = m->is_host ? 1u : 0u;                       /* : the conversion, written out (this file does not include s7_core.h) */
    sm.opp_ip = m->opp_ip; sm.opp_port = m->opp_port;
    sm.fixed_rules = mode == SBR_MODE_UNRANKED ? 1u : 0u;
    sm.n_stages = m->nstages > 16u ? 16u : m->nstages;
    for (i = 0; i < 16u; i++) sm.stages[i] = i < sm.n_stages ? m->stages[i] : 0;
    sm.seed = tbl() ^ (od_clock_ms() * 2654435761u);            /* any non-secret changing words */
    sm.char_color = 0;                                          /* B5 has locked the character in */
    sm.items = m->items;                                        /* : Slippi applies the ticket's items to the match block */
    return br_engine_begin(&sm, g_sock, m->opp_ip);
}
static int e_state(void *ctx) { (void)ctx; return br_engine_state(); }
static int e_b3(void *ctx, sbr_u8 *msrb) { return br_engine_b3(ctx, msrb); }
static int e_b5(void *ctx, const sbr_u8 *pstb) { return br_engine_b5(ctx, pstb); }
static sbr_u32 e_ready(void *ctx) { (void)ctx; return br_engine_ready(); }
extern void br_engine_end(void);
static void e_end(void *ctx) { (void)ctx; br_engine_end(); }
static const struct sbra_engine_ops ENG = { e_begin, e_state, e_b3, e_b5, e_ready, e_end };
static const struct sbra_io IO = { send_main, 0, &ENG };
static int dns_none_send(void *c, sddns_u32 ip, sddns_u16 p, const sddns_u8 *d, sddns_u32 l) { (void)c; (void)ip; (void)p; (void)d; (void)l; return -1; }
static int dns_none_recv(void *c, sddns_u8 *b, sddns_u32 cap, sddns_u32 *ip, sddns_u16 *p) { (void)c; (void)b; (void)cap; (void)ip; (void)p; return 0; }
static const sddns_io DNS_NONE = { dns_none_send, dns_none_recv, 0 };
static int dns_recv(void *c, sddns_u8 *b, sddns_u32 cap, sddns_u32 *ip, sddns_u16 *port)   /* records the source for the status block */
{
    int n;
    (void)c;
    if (g_dsock < 0 || !g_S) return 0;
    n = sdnet_recvfrom(g_S, g_dsock, b, (u16)(cap > 0xFFFFu ? 0xFFFFu : cap), ip, port);
    if (n < 0) return 0;
    ST.dns_last_src = *ip;
    if ((sbr_cfg.flags & SBRA_F_DNS_ANYSRC) && *port == 53u) *ip = A.dns.server[0];   /* LAB/Dolphin only, see sbr_app.h */
    else if (*port == 53u && A.dns.server[0] && *ip != A.dns.server[0] && g_S &&   /* a home router or a forwarder may answer from another of its own addresses: accept it when it is on our subnet or the gateway (txid + question still have to match) */
             (*ip == g_S->gw || (g_S->mask && ((*ip ^ g_S->ip) & g_S->mask) == 0))) *ip = A.dns.server[0];
    return n;
}

/* The old drain (read the bridge's UDP socket and hand the datagrams to the ENet matchmaker) is gone with the ENet matchmaker: the 3.7.0 matchmaker is a TCP WebSocket in the overlay, which reads this
   socket itself for STUN (br_udp_recv); while the engine plays, the engine reads it. */
int br_udp_send(u32 ip, u16 port, const u8 *d, u32 len)      /* STUN from the NETPLAY socket; only inside the overlay's net window (ms_tls_begin) */
{
    int rc;
    if (!g_S || g_sock < 0) return -1;
    rc = sdnet_sendto(g_S, g_sock, ip, port, d, (u16)len);
    return rc >= 0 ? 0 : -1;
}
int br_udp_recv(u8 *b, u32 cap, u32 *ip, u16 *port)
{
    int n;
    if (!g_S || g_sock < 0) return -1;
    n = sdnet_recvfrom(g_S, g_sock, b, (u16)(cap > 0xFFFFu ? 0xFFFFu : cap), ip, port);
    return n;
}
/* the matchmaker mailbox for the report/net overlay (rep_res.c): wanted while a bridge search's command is out or the search is queued; the identity copy for ONE search (R7) */
int br_mm_want(void) { return A.B.owned != 0; }
struct sbr_mbx *br_mm_mbx(void) { return &A.B.mbx; }
int br_mm_ident(struct sbr_ident *out)
{
    const struct sbr_ident *id = A.B.id;
    unsigned char *d = (unsigned char *)out;
    const unsigned char *r = (const unsigned char *)id;
    u32 i;
    if (!A.B.id_ok || !id) return 0;
    for (i = 0; i < (u32)sizeof *out; i++) d[i] = r[i];      /* a byte loop: no memcpy in this image */
    return 1;
}

static u32 running_release(void);
extern u32 sbr_build_seq;
static void core_init(void)
{
    if (g_init) return;
    g_init = 1;
    sbr_build_seq = running_release();      /* the ticket's client "multishine N" (appVersion stays plain 3.6.4) */
    sbra_init(&A, &sbr_cfg, &sbr_status, &IO, &DNS_NONE, tbl());
    sbra_set_baked_ident(&A, sbr_baked.payload[0] == 'S' ? sbr_baked.payload : 0, SBRA_BAKED_FIRST);
    if (sbr_cfg.flags & SBRA_F_NO_NET) { g_nst = SBRA_N_DISABLED; sbra_set_net(&A, 0, g_nst, 0, 0); }
    g_br = sbra_backend(&A, clock_ms_void);
}

/* ---- called by s12_gc.c (BR_BUILD) ---- */
/* HUB-DNS: the hub (od_glue.c) resolves its configured server NAME with the bridge's resolver: same sddns object, same bound UDP socket, same
   DHCP-provided server (the hub sets it before each lookup); zero extra code or data.  Only used at menu time. */
struct sddns *br_dns(void) { return g_dsock >= 0 ? &A.dns : (struct sddns *)0; }

void br_link_up(struct sdnet *s)                       /* the link is up, sdnet_init done, the engine's socket bound, DHCP about to start */
{
    core_init();
    g_S = s;
    g_sock = sdnet_bind(s, (u16)sbr_cfg.local_port, 0, 0);
    g_dsock = sddns_sdnet_bind(&A.dns.io, &g_dctx, s, (u16)sbr_cfg.dns_lport);
    A.dns.io.recv = dns_recv;
    BRS(8)++;
}

static void net_state(u32 s3)                          /* the online glue's state -> the bridge's network state */
{
    switch (s3) {
    case BR_S3_WAIT: g_nst = SBRA_N_WAIT; break;
    case BR_S3_INIT: g_nst = SBRA_N_INIT; break;
    case BR_S3_LINK: g_nst = SBRA_N_LINK; break;
    case BR_S3_DHCP: g_nst = SBRA_N_DHCP; break;
    case 0x80u: g_nst = SBRA_N_NOBBA; break;
    case 0x81u: g_nst = SBRA_N_FAIL_INIT; break;
    case 0x82u: g_nst = SBRA_N_FAIL_LINK; break;
    case 0x91u: g_nst = SBRA_N_UP; break;                 /* the engine's session is LOST (opponent gone): the NETWORK is fine; the bridge shows "Opponent left" (br_slice) */
    default: g_nst = (s3 < BR_S3_NOBBA) ? SBRA_N_UP : SBRA_N_FAIL_INIT; break;   /* the server step and the session states: the network is up */
    }
    if (sbr_cfg.flags & SBRA_F_NO_NET) g_nst = SBRA_N_DISABLED;
}

/* inside the online glue's network slice (EXI lock held, interrupts off); s3 = its state, after the state machine ran */
void br_slice(u32 s3)
{
    core_init();
    net_state(s3);
    g_locked = 1;
    sbra_set_net(&A, g_nst == SBRA_N_UP, g_nst, g_S && g_nst >= SBRA_N_DHCP ? g_S->ip : 0, g_S && g_nst >= SBRA_N_DHCP ? g_S->dns : 0);
    sbra_tick(&A, clock_ms_void());
    {   /* SILENT RE-SEARCH after a failed opponent connection (cfg reticket_max): the engine session that was started for the failed assignment is still open (CONNECTING / LOST) and
           holds g_slp_sock; its drain reads the bridge's socket and DROPS everything that is not the old opponent, i.e. the NEW matchmaker connection's replies (same bug class as the
           second search of a boot). Console "Connecting to opponent" then "Couldn't reach the matchmaking server." End it at every re-ticket. */
        static u32 s_retick BR_DATA;
        if (A.B.st.retickets != s_retick) { s_retick = A.B.st.retickets; od_end_req = 1; }
    }
    /* POST-GAME PEER GONE (hand-off): the engine's session is lost (the opponent left after a game, or s12_gc.c's silence rule fired) while the bridge still reads
       CONNECTED: show the error box on the CSS ("Opponent left"; B cancels, BA) instead of waiting for ever */
    if (sbra_engine_live(&A) && br_engine_state() < 0 && sbr_state(&A.B) == SBR_CONNECTED) {
        /* the box carries a code (a console has no log): cause (s7_core.h S7W_*), B0 frame, pad packets received / refused, stalled calls, other message ids, ENet data.  Digits and capitals only (the text encoder eats brackets). */
        static char t[72] BR_DATA;
        static const char tag[8] = { 'W', 'F', 'D', 'P', 'R', 'S', 'O', 'M' };
        extern void br_engine_why(u32 *o8);
        u32 w[8], k, n = 0, v, d;
        const char *h = "Your opponent left. Code";
        br_engine_why(w);
        while (h[n]) { t[n] = h[n]; n++; }
        for (k = 0; k < 8; k++) {
            char dg[10]; d = 0; v = w[k];
            if (k >= 6 && !v) continue;
            t[n++] = ' '; t[n++] = tag[k];
            do { dg[d++] = (char)('0' + v % 10u); v /= 10u; } while (v && d < 10u);
            while (d) t[n++] = dg[--d];
        }
        t[n] = 0;
        sbr_fail(&A.B, clock_ms_void(), SBR_E_PEER, t);
    }
    g_locked = 0;
    g_slice_done = 1;
    BRS(9)++;
}

/* ---- on-screen status line (the reason line, see sbra_login_words).  One SisLib text on the front menu canvas, same calls as the layout's own text lines
   (ml_glue.c text_new_at / pg_line: the layout is not touched).  Shown while the Slippi row waits for the identity read and on Slippi's screen while logged out. ---- */
#define TXT_NEW ((void *(*)(int, int))0x803A6754u)
#define TXT_FREE ((void (*)(void *))0x803A5CC4u)
#define MENU_CANVAS_FRONT (*(volatile u8 *)0x804D6BB4u)
extern int ml_sis_add(void *text, const double *xy, const char *fmt, const char *s);   /* ml_stub.S (the combined image links both) */
static const double g_zero_xy[2] = { 0.0, 0.0 };
static void *g_line_t;
static const char *g_line_s;
static u32 g_wait_seen, g_msg_until, waited;
static const char *g_msg;
static int g_nodata;                                   /* 1 = the Slippi data files are not on the disc (or served by the card): br_online_ready refused */
static void line_set(const char *s)
{
    u32 *t;
    if (s == g_line_s && (s == 0 || g_line_t)) return;
    if (g_line_t) { TXT_FREE(g_line_t); g_line_t = 0; }
    g_line_s = s;
    if (!s) return;
    t = (u32 *)TXT_NEW(0, (int)MENU_CANVAS_FRONT);
    if (!t) return;
    t[0] = 0x00000000u;                                  /* x 0.0: centred */
    t[1] = 0xC0F9999Au;                                  /* y -7.8: ONE line just under the page header, above the first row (the notice was drawn over the bar's own description; the bar region belongs to the row's description alone) */
    t[2] = 0x41880000u;                                  /* z 17.0 */
    t[0x24 / 4] = t[0x28 / 4] = 0x3CE56042u;             /* size 0.028 */
    ((u8 *)t)[0x49] = 1;
    ((u8 *)t)[0x4A] = 1;                                 /* centred */
    ((u8 *)t)[0x30] = 0xFF; ((u8 *)t)[0x31] = 0xD7; ((u8 *)t)[0x32] = 0x00; ((u8 *)t)[0x33] = 0xFF;
    ml_sis_add(t, g_zero_xy, "%s", s);
    g_line_t = t;
}

#ifdef MS_MM_SELFTEST
/* LAB / DOLPHIN PROOF ONLY (never in a release: tools/release.sh does not set it and the release shape check refuses the marker): the Slippi row of the Main Menu cannot be walked reliably in the
   headless rig, so 70 s into the menus (identity read, network up, the scenario has fed the entropy pool) the bridge starts ONE Unranked search by itself, exactly what B4 would do (sbra_b4), and with MS_MM_SELFTEST 2 cancels it
   (sbra_ba, the cleanup B4's screen sends) 3 s after it is queued.  The match is the stand-in's; with cfg flag 0x10 the bridge stops at the assignment (no engine session on the Main Menu). */
const char sbr_mm_selftest_marker[] = "MS-MM-SELFTEST";
static void mm_selftest(void)
{
    static u32 t0, tq;
    static int st;
    u32 now = clock_ms_void();
    unsigned char code[18];
    u32 i;
    if ((SCENE_WORD >> 24) != 1u || st >= 9) return;
    if (!t0) { t0 = now ? now : 1u; return; }
    if (st == 0 && sbra_app_state(&A) && g_nst == SBRA_N_UP && now - t0 > 70000u) {
        for (i = 0; i < 18u; i++) code[i] = 0;
        (void)sbra_b4(&A, now, SBR_MODE_UNRANKED, code);
        st = 1;
    } else if (st == 1 && MS_MM_SELFTEST == 2 && sbr_state(&A.B) == SBR_MM) {
        if (!tq) tq = now ? now : 1u;
        else if (now - tq > 3000u) { sbra_ba(&A); st = 9; }
    }
}
#endif

/* once per scene-loop iteration, outside the lock, before the online glue's own tick body */
void br_tick(u32 s3)
{
    u32 t0, dt, m;
    m = irq_off();
    core_init();
    t0 = tbl();
    net_state(s3);
    if (!g_slice_done && g_nst != SBRA_N_UP) {        /* no slice this iteration and the network is not up (waiting, failed adapter): the core still ticks, it cannot send */
        sbra_set_net(&A, 0, g_nst, 0, 0);
        sbra_tick(&A, clock_ms_void());
    }
    g_slice_done = 0;
    {
        u32 cls = SCENE_WORD >> 24;
        if (cls == 8u && g_fd[0]) g_fd_css = 1;
        if (cls == 1u && g_fd[0] && g_fd_css) { g_fd[0] = 0; g_fd_css = 0; }        /* back in the menus after the character screen: the arming is over */
        if (cls == 1u) g_flow = MENU_CUR == 8u;
        if (cls == 1u && g_active) { g_active = 0; BRS(1) = 0; }
#if MS_TMM
        if (cls == 1u && TM.phase != TMM_P_OFF) tm_clear();            /* the menus: a tiered search that was never cleaned up is over */
#endif          /* on the menus nothing searches: a backend binding left over from an earlier hub or Slippi match (BA never came) must not route this visit's B9 / CSS identity */
    }
    if ((SCENE_WORD >> 24) == 1u && MENU_CUR != 8u) (void)sbra_card_poll(&A, clock_ms_void());   /* menus only, no EXI lock held */
    /* Slippi asks B9 at boot, before the identity was read from the card: while the player is on the menus but not inside Slippi's
       online submenu, keep its app state byte equal to what the bridge's B9 would answer now (the submenu reads it when it is built) */
    if ((SCENE_WORD >> 24) == 1u && MENU_CUR != 8u) {
        u8 want = (u8)sbra_app_state(&A);
        if (R13_APP_STATE != want && R13_APP_STATE <= 1u) { R13_APP_STATE = want; ST.app_state = want; }
    }
    /* DOJO SOFTLOCK ON THE WII (game.log: scene 08080100 loading, OS exception srr0 = the C2 body of Slippi's SkipReturnToCssSound at 0x80264118, `lwz r3,6(r31)`, dar 6): that body runs when
       the character screen loads; with the name entry byte (r13 - 0x505D = 0x804D6643, a byte of retail's lbl_804D6640 that Slippi took over) != 0 it takes the code keyboard's buffer address from its data table
       (still 0 when no keyboard ran) and reads through it: a load from address 6 = DSI on the console, the game thread dies, the music goes on.  The keyboard exists only inside the character screen, so on
       the menus the byte is always dead: whatever left it set (Tournament's own variable, an earlier keyboard, a reset that kept RAM) is cleared here, every menu tick, before the next character screen loads. */
    if ((SCENE_WORD >> 24) == 1u) {
#ifdef MS_LAB_POISON_NE
        R13_NAME_ENTRY = 1u;                           /* LAB ONLY (the release shape check refuses the marker below): the polluted byte, to prove the freeze and the clear in Dolphin */
#endif
#if !defined(MS_LAB_NO_NE_CLEAR)
        if (R13_NAME_ENTRY) R13_NAME_ENTRY = 0u;
#endif
    }
    irq_restore(m);
    dt = tbl() - t0;
    if (dt > ST.tick_max) ST.tick_max = dt;
    if (((SCENE_WORD >> 24) == 0u || ((SCENE_WORD >> 24) == 1u && MENU_CUR != 8u)) && df_poll) df_poll();   /* title and menus: the table of contents is read as early as possible, before the first main-menu entry */
      /* outside the interrupt-off window (the card SDK needs interrupts): the card route's disc-file pack (menus/df, absent from the other images) reads its table of contents once */
    if ((SCENE_WORD >> 24) == 1u && MENU_CUR != 8u) hist_poll();
#ifdef MS_MM_SELFTEST
    mm_selftest();
#endif
    if (rep_menu_hook) rep_menu_hook();               /* the Slippi report client's frame (it decides by br_menu_scene_ok whether anything may run) */
#ifdef SD_CARD_HOLE
    if (tt_frame) tt_frame();   /* the title screen's text band and logo (menus/tt; card route only): a byte compare outside the title */
#endif
#ifdef UPD_BOOT
    /* OTA automatic rollback: the build counts its own start from the first title / menu frame whose card layer is idle (a build that hangs after this is
       tried 3 times, then the loader loads the previous build); upd_host.c upd_boot_frame is one call and a return once the count is done.  Interrupts are on here (the card SDK needs them). */
    if ((SCENE_WORD >> 24) <= 1u) { extern void upd_boot_frame(void); upd_boot_frame(); }
#endif
    {   /* the status line (outside the interrupt-off window): menus only; a scene change killed the old text with its heap, so forget it without freeing */
        static u32 last_cls = 1;
        u32 cls = SCENE_WORD >> 24;
        const char *want = 0;
        if (cls != last_cls && last_cls == 1u) { g_line_t = 0; g_line_s = 0; }
        last_cls = cls;
        if (cls == 1u) {
            if (g_msg && clock_ms_void() < g_msg_until) want = g_msg;      /* (the Slippi row's reasons are on the Main Menu's right panel now) */
            else if (MENU_CUR == 8u) want = !sbra_app_state(&A) ? sbra_login_words(&A) : 0;   /* logged out: why; logged in: nothing */
        }
        if (!g_wait_seen) waited = 0;
        g_wait_seen = 0;
        line_set(want);
    }
}

/* The layout's Slippi row (ml_glue.c, -Dml_online_hook=br_online_ready): called when the row is confirmed and every frame until it returns 1.  The FIRST call is
   the moment the player enters Slippi's screen.  Nothing is checked against a server any more: the only thing awaited is the identity read from
   the card (normally long final, the menu poll reads it every 2 s).  1 = identity known and usable, the screen may be built; 0 = the card read is still running
   (the layout gives up after ONLINE_WAIT_MAX frames); -1 = logged out: the screen is not opened, the reason line is shown. */
/* DATA GATE: Slippi's online CSS loads "slpCSS.dat" and asserts ("file isn't exist") when the disc has no such file, and the Slippi
   art lives in modified menu archives.  The probe is the SDK's name -> entry lookup (FST in RAM, no disc access, safe here), cached for the session; the card delta
   pack (menus/df) hooks the same lookup, so a file served from the card counts as present.  Missing = the screen is not entered. */
#define BR_DVD_PATH ((int (*)(const char *))0x8033796Cu)   /* DVDConvertPathToEntrynum */
static int data_files_present(void)
{
    static int known;                                   /* 1 = seen present (kept); a miss is asked again every time: the card pack's table of contents may arrive a moment later */
    if (!known && BR_DVD_PATH("slpCSS.dat") >= 0) known = 1;
    return known == 1;
}

/* OFFLINE GUARD: the BBA link is not usable (no adapter, init or link failed, still waiting for the link, networking disabled).  DHCP pending counts as linked. */
unsigned int br_tick_max_us(void) { return ST.tick_max / 40u; }       /* NET COST probe: the longest br_tick (interrupts-off window) since boot, about microseconds */
int br_busy(void) { int st = sbr_state(&A.B); return st >= SBR_MM && st <= SBR_CONNECTED; }     /* a Slippi search or match is under way: Settings > Network status > Reconnect refuses (s12_gc.c ms_net_reconnect) */
int br_link_down(void) { return !(g_nst == SBRA_N_DHCP || g_nst == SBRA_N_UP); }
void br_say(const char *msg)                          /* a message on the status line for 6 s (the same line as "Slippi: not logged in"); msg is a static string */
{
    g_msg = msg;
    g_msg_until = clock_ms_void() + 6000u;
}
int br_line_up(void) { return g_line_t != 0; }        /* ml_glue.c: the debug block stacks under the notice */
static u32 running_release(void)                      /* the running release (the loader's status block, as the title's "Rel N" reads it: tt_glue.c); 0 = unknown */
{
    volatile u32 *st = (volatile u32 *) MS_CL_STATUS_ADDR;
    u32 n = st[18];
    return (st[0] == 0x434C0001u && st[1] == 7u && st[2] == 0u && (st[16] & 1u) && n != 0u && n < 100000u) ? n : 0u;
}
void br_say_offline(void) { }                         /*: no yellow line; the Main Menu's right panel says 'Console is offline' (br_panel_reason) */
/* the reason the Slippi row cannot be entered, for the Main Menu's right panel (the panel, not a status line): 0 none, 1 the console is offline,
   2 the Slippi data files are missing, 3 the pack is damaged, 4 not logged in to Slippi (only once the identity read has finished) */
unsigned br_panel_reason(void)
{
    if (br_link_down()) return 1u;
    if (!data_files_present()) return (df_pack_state && df_pack_state() == 2) ? 3u : 2u;
    if (sbra_ident_done(&A) && !sbra_app_state(&A)) return 4u;
    return 0u;
}

/* SLIPPI FRIEND: arm a Direct search of `code` (the Friends view's S row).  0 = armed (the caller leaves for Slippi's character screen in Unranked mode; the lock-in's
   B4 becomes a Direct search of the code).  < 0 = refused, the reason is for the caller to show: -10 the console is offline (BBA link down), -11 logged out (never Direct without an
   identity), -12 the Slippi data files are missing, -13 not a Slippi connect code.  Nothing is sent to our server for these players. */
int br_friend_direct(const char *code)
{
    unsigned i;
    core_init();
    if (!data_files_present()) return -12;
    if (br_link_down()) { br_say_offline(); return -10; }
    if (MM_OFF()) return -14;                          /* our switched Slippi matchmaking off (server): no Direct through mm.slippi.gg */
    if (!sbra_ident_done(&A) || !sbra_app_state(&A)) return -11;
    if (!code || !sbr_code_valid(code)) return -13;
    for (i = 0; code[i] && i < 9u; i++) g_fd[i] = code[i];
    g_fd[i] = 0;
    g_fd_css = 0;
    return 0;
}

int br_online_ready(void)
{
    core_init();
    BRS(11)++;                                                 /* proof words: [10] why the last call answered what it did (1 no data files, 2 mm off, 3 link down, 4 waiting for identity, 5 logged out, 6 ready), [11] calls */
    g_wait_seen = 1;
    if (!data_files_present()) {
        g_nodata = 1;
        waited = 0;
        BRS(10) = 1;
        return -1;
    }
    g_nodata = 0;
    if (MM_OFF()) { waited = 0; BRS(10) = 2; return -1; }                    /* server switch (admin slippi-mm): the Slippi screen stays closed, the right panel says why (ms_net_cost 107 reason 5).  Since MM_OFF is also true while no HELLO has arrived (fail closed, on_backend.c): confirming the row while the console is still connecting does nothing; no wait and no text here, the engine file has no room for them (payload at the 0x7A000 hole limit) */
    if (br_link_down()) { br_say_offline(); waited = 0; BRS(10) = 3; return -1; }     /* the Slippi screen talks to Slippi: only the link counts; the existing refusal path keeps the Main Menu working */
    /* no card access here: br_tick already polls the identity every 2 s on the menus, and a card read with interrupts
       off is what this used to repeat; this function only waits for that verdict (230 frames cover the 2 s gap) */
    if (!sbra_ident_done(&A) && ++waited < 230u) { BRS(10) = 4; return 0; }       /* the layout's own cap is 240 frames: we answer first */
    waited = 0;
    {   /* the screen reads Slippi's app-state byte when it is built (br_tick only syncs it outside the screen): put the verdict there now */
        u8 want = (u8)sbra_app_state(&A);
        if (R13_APP_STATE != want && R13_APP_STATE <= 1u) { R13_APP_STATE = want; ST.app_state = want; }
        if (!want) {   /* LOGGED OUT (no identity): Slippi's logged-out form draws its Log-in row with the wrong highlight art and lets
                          the cursor reach Direct, so the screen is not opened at all: the player stays on the Main Menu and reads why (rule: never Direct) */
            BRS(10) = 5;
            return -1;                                       /* the right panel says why (br_panel_reason 4) */
        }
    }
    BRS(10) = 6;
    return 1;
}

/* mn_80229938(kind, sel) via ml_unlock_stub: 1 = the row does not exist (Ranked, Teams, Party on Slippi's online submenu) */
int br_unlock_override(u32 kind, u32 sel)
{
    int h = sbra_row_hidden(kind, sel, R13_APP_STATE);
    /* LOGGED OUT (state 0: no identity): only Slippi's log-in row (index 5) exists; Ranked, Unranked, Direct, Teams, Party, Log out and
       Update are hidden explicitly, so Direct / Unranked can never be entered from the logged-out form */
    if (kind == 8u && R13_APP_STATE == 0u) {
        ST.hidden_rows++;
        return sel == 5u ? 2 : 1;
    }
    if (h) ST.hidden_rows++;
    /* Slippi's online submenu (kind 8) belongs to the bridge alone: its rows depend on the logged-in form (hiding the single log-in row of
       the logged-out form loops the menu), so the layout's own hide list is not asked for kind 8 */
    if (!h && kind != 8u) return ml_unlock_override(kind, sel);
    return h;
}

/* ---- the dispatcher ---- */
/* The backend of a flow is decided by the MENU the player is on: Slippi's own screen (cur_menu 8) = bridge, any other menu screen = Multishine.  Once the player has
   been on Slippi's screen the bridge OWNS the whole flow that follows (Direct code entry and CSS, search, match, post-game) although those screens are no longer
   cur_menu 8.  g_flow is refreshed on every
   menu frame (br_tick) and kept while a scene runs. */
static int origin(void) { if (g_fd[0] && (SCENE_WORD >> 24) != 1u) return 2; return ((SCENE_WORD >> 24) == 1u ? MENU_CUR == 8u : g_flow != 0) ? 2 : 1; }
static const struct sdshim_backend *cur(void) { return g_active == 2 ? g_br : g_ms; }
static const struct sdshim_backend *by_origin(void) { return origin() == 2 ? g_br : g_ms; }

#if MS_TMM
/* ---- TIERED MATCHMAKING in Dojo and Mano a mano (decisions in tmm_core.h, host test tests/tmm_test.c) -------------------------------------------------------
   The dispatcher already sends a search to ONE backend.  For a search that starts on a Multishine hub row (origin 1: Dojo = Unranked, Mano a mano = Direct) and that the hub's hand-off blocks do not own
   (Game Night, Grandmaster sets, Friends: ms_tmm_plain), this block may choose the Slippi bridge instead (Dojo: Slippi login AND a positively wired link; Mano a mano: a Slippi-form code AND a Slippi login),
   or search both ONE AFTER THE OTHER (leave the first side, wait for its confirmation, start the second).  Everything else (guest, account only, Slippi login on Wi-Fi, '@' codes, Game Night ...) returns 1 =
   not handled and runs the code that was here before, untouched.  No Slippi contact happens unless the card holds a Slippi login. */
extern int ms_tmm_plain(void) __attribute__((weak));          /* on_backend.c: no Game Night / Grandmaster-set / Friends hand-off owns this search */
extern int ms_tmm_account(void) __attribute__((weak));        /* on_backend.c: 1 = a linked Multishine account on the card */
extern void ms_tmm_note(unsigned n) __attribute__((weak));    /* on_backend.c: the player-facing note for the character screen (hub_soc.c css_frame) */
extern int ms_slippi_srv_off(void) __attribute__((weak));     /* on_backend.c: the server's Slippi switch / no HELLO yet, WITHOUT the Wii Wi-Fi term (Mano a mano may use Slippi on Wi-Fi) */
extern int ms_link_kind(void) __attribute__((weak));          /* s12_gc.c: the wired filter's link class: 1 wired, 2 Wi-Fi, 0 unknown */
static u8 TM_held BR_DATA;                                    /* bit 0: a Multishine search is open, bit 1: a Slippi search is open (exactly one at a time, except for the instant of a leave) */
static u8 TM_poss BR_DATA;                                    /* Slippi was in play for this search: the note is shown */
static u8 TM_mode BR_DATA;
static u8 TM_code[18] BR_DATA;
static int tm_link(void)
{
#ifdef MS_TMM_LAB_LINK
    return MS_TMM_LAB_LINK;                                   /* LAB ONLY (marker below; the release shape check refuses it): fakes the link class in Dolphin, which is always a wired GameCube */
#else
    return ms_link_kind ? ms_link_kind() : TMM_LINK_UNKNOWN;
#endif
}
#ifdef MS_TMM_LAB_LINK
const char sbr_tmm_lab_marker[] = "MS-TMM-LAB";
#endif
static int tm_slippi_login(void) { return sbra_ident_done(&A) && sbra_app_state(&A); }
/* the gates of the Slippi rows (offline, data files, the server's switch) minus the Wii Wi-Fi term: the tier rules decide Wi-Fi themselves */
static int tm_slippi_usable(void)
{
    return tm_slippi_login() && !br_link_down() && data_files_present() && !(ms_slippi_srv_off ? ms_slippi_srv_off() : MM_OFF());
}
static void tm_note_set(void)
{
    unsigned n = (unsigned)tmm_note(&TM, TM_poss);
    BRS(13) = (u32)TM.phase | ((u32)TM_held << 8) | ((u32)n << 16) | ((u32)TM.why << 24);
    if (ms_tmm_note) ms_tmm_note(n);
}
static void tm_clear(void) { tmm_reset(&TM); TM_held = 0; TM_poss = 0; if (ms_tmm_note) ms_tmm_note(0); }
static int tm_start(int net)                                  /* start `net` with the lock-in's arguments */
{
    const struct sdshim_backend *b = net == TMM_NET_SLIPPI ? g_br : g_ms;
    if (!b || !b->find_opponent) return -1;
    if (net == TMM_NET_SLIPPI) { if (rep_search_hook) rep_search_hook(); od_end_req = 1; }
    g_active = net == TMM_NET_SLIPPI ? 2 : 1;
    BRS(1) = (u32)g_active;
    BRS(g_active == 2 ? 3 : 2)++;
    TM_held |= net == TMM_NET_SLIPPI ? 2u : 1u;
    return b->find_opponent(b->ctx, TM_mode, TM_code);
}
static void tm_leave(int net)                                 /* a proper leave on that side: Slippi's cleanup (cancel to the matchmaker) / our cancel */
{
    const struct sdshim_backend *b = net == TMM_NET_SLIPPI ? g_br : g_ms;
    if (net == TMM_NET_SLIPPI) od_end_req = 1;
    BRS(14)++;
    if (b && b->cleanup) b->cleanup(b->ctx);
    TM_held &= (u8)~(net == TMM_NET_SLIPPI ? 2u : 1u);
}
/* B4 of a hub-origin search.  Returns 1 = not ours to handle (the old path runs), else the find_opponent result of the side started. */
static int tm_b4(u8 mode, const u8 *code)
{
    int first, second, usable, login = tm_slippi_login();
    u32 i;
    tmm_reset(&TM);
    TM_mode = mode;
    for (i = 0; i < 18u; i++) TM_code[i] = code ? code[i] : 0;
    usable = login && tm_slippi_usable() && ST.local_ready;     /* local_ready: the lock-in (B5) reached the bridge too */
    if (mode == SBR_MODE_DIRECT) { first = tmm_route_code(TM_code, login, usable); second = TMM_NET_NONE; }
    else tmm_dojo_plan(tmm_tier(ms_tmm_account ? ms_tmm_account() : 0, login), tm_link(), &first, &second);
    if ((first == TMM_NET_SLIPPI || second == TMM_NET_SLIPPI) && !usable) { first = TMM_NET_OURS; second = TMM_NET_NONE; }
    if (first == TMM_NET_OURS && second == TMM_NET_NONE) return 1;   /* nothing tiered about it */
    TM_poss = 1;
    TM_held = 0;
    tmm_begin(&TM, clock_ms_void(), first, second);
    BRS(12) = ((u32)first << 4) | (u32)second;                      /* proof: the plan of the last tiered search */
    return tm_start(first);
}
/* every B3 of a tiered search: the clock of the machine.  Returns the (possibly replaced) answer. */
static int tm_b3(int rc, sdshim_u8 *msrb)
{
    u32 now = clock_ms_void();
    int act, k;
    if (TM.phase == TMM_P_LEAVING) {
        act = tmm_step(&TM, now, TMM_O_NONE, TM.first == TMM_NET_SLIPPI ? !br_mm_want() : 1);
        if (act == TMM_A_START_SECOND) { BRS(15)++; (void)tm_start(TM.net); }
        tm_note_set();
        for (k = 0; k < 600; k++) msrb[k] = 0;
        msrb[0] = 2; msrb[1] = 1;                                 /* the screen keeps showing a search while the first side leaves */
        return 0;
    }
    act = tmm_step(&TM, now, tmm_obs(rc, msrb[0]), 0);
    if (act == TMM_A_LEAVE_FIRST) {
        tm_leave(TM.first);
        for (k = 0; k < 600; k++) msrb[k] = 0;
        msrb[0] = 2; msrb[1] = 1;
        rc = 0;
    }
    tm_note_set();
    return rc;
}
#endif

static int d_b0(void *c, const sdshim_u8 *tx, sdshim_u8 *rx)
{
    const struct sdshim_backend *b = cur();
    (void)c;
    if (g_active == 2 && sbra_engine_live(&A)) return br_engine_b0(0, tx, rx);   /* ENGINE HAND-OFF */
    return b && b->online_inputs ? b->online_inputs(b->ctx, tx, rx) : -1;
}
static int d_b3(void *c, sdshim_u8 *msrb)
{
    const struct sdshim_backend *b = g_active ? cur() : by_origin();
    int rc;
    (void)c;
#if MS_TMM
    if (TM.phase == TMM_P_LEAVING) return tm_b3(0, msrb);        /* the first side has left or is leaving: the machine answers */
#endif
    rc = b && b->match_state ? b->match_state(b->ctx, msrb) : -1;
#if MS_TMM
    if (TM.phase != TMM_P_OFF) rc = tm_b3(rc, msrb);
#endif
    return rc;
}
static unsigned to_sjis(char ch)                    /* the code-entry buffer's full-width characters (sbr_app.c from_sjis) */
{
    if (ch >= '0' && ch <= '9') return (0x824Fu + (unsigned)(ch - '0'));
    if (ch >= 'A' && ch <= 'Z') return (0x8260u + (unsigned)(ch - 'A'));
    if (ch >= 'a' && ch <= 'z') return (0x8281u + (unsigned)(ch - 'a'));
    return ch == '#' ? 0x8194u : 0x8140u;
}
static int d_b4(void *c, sdshim_u8 mode, const sdshim_u8 *code)
{
    const struct sdshim_backend *b;
    sdshim_u8 fd18[18];
    (void)c;
    if (g_fd[0]) {                                     /* SLIPPI FRIEND: whatever the character screen says, this search is a Direct one for the armed code, to Slippi (never Ranked) */
        unsigned i;
        for (i = 0; i < 18u; i++) fd18[i] = 0;
        for (i = 0; i < 9u && g_fd[i]; i++) { unsigned v = to_sjis(g_fd[i]); fd18[2u * i] = (sdshim_u8)(v >> 8); fd18[2u * i + 1u] = (sdshim_u8)v; }
        mode = SBR_MODE_DIRECT;
        code = fd18;
    }
#if MS_TMM
    if (!g_fd[0] && origin() == 1 && (mode == SBR_MODE_UNRANKED || mode == SBR_MODE_DIRECT) && ms_tmm_plain && ms_tmm_plain()) {   /* Dojo / Mano a mano of the hub: tiered matchmaking */
        int r = tm_b4(mode, code);
        if (r != 1) return r;
    }
#endif
    if (MM_OFF() && (g_fd[0] || origin() == 2)) {      /* the switch came on while the Slippi screen was open: no search through mm.slippi.gg (our own queue, origin 1, is not touched) */
        br_say("Slippi matchmaking is switched off for now.");
        g_fd[0] = 0;
        return -1;
    }
    if (rep_search_hook) rep_search_hook();            /* before the bridge needs the network: the report session (idle character screen) ends, the report stays queued */
    g_active = origin();
    BRS(1) = (u32)g_active;
    BRS(5) = MENU_CUR;
    BRS(g_active == 2 ? 3 : 2)++;
    if (g_active == 2) od_end_req = 1;                 /* a new bridge search: an engine session of an earlier match is over (s12_gc.c peer_watch ends it; normally BA did already) */
    b = cur();
    return b && b->find_opponent ? b->find_opponent(b->ctx, mode, code) : -1;
}
static int d_b5(void *c, const sdshim_u8 *pstb)
{
    /* The lock-in (B5) comes BEFORE the search (B4) in Slippi's CSS (FN_LOCK_IN_AND_SEARCH), so g_active is still 0 here: ask the origin like B3 does.  With cur alone a Direct
       lock-in (made from the code-entry scene's callback) went to the Multishine backend, the bridge never saw it (local ready 0), the CSS asked for START again and Slippi's
       HANDLE_CONNECTED_DIRECT ran into its "b 0x0 # stall" (ISWINNER is NULL in the first match): frozen CSS with the music playing. */
    const struct sdshim_backend *b = g_active ? cur() : by_origin();
    (void)c;
    if (rep_search_hook) rep_search_hook();            /* the lock-in comes before the search: same rule as d_b4 */
#if MS_TMM
    if (!g_active && b == g_ms && g_br && g_br->set_selections && ms_tmm_plain && ms_tmm_plain() && tm_slippi_login() && !br_link_down())
        (void)g_br->set_selections(g_br->ctx, pstb);    /* a hub lock-in also reaches the bridge's selections (local state only, no network): the search may go to Slippi */
#endif
    return b && b->set_selections ? b->set_selections(b->ctx, pstb) : -1;
}
static int d_b9(void *c, sdshim_u8 *osb)
{
    static sdshim_u32 scratch[16] BR_DATA;                 /* the answer of the backend that is not asked for (aligned, 64 bytes >= 42) */
    const struct sdshim_backend *mine = by_origin(), *other = mine == g_br ? g_ms : g_br;
    int rc;
    (void)c;
    BRS(4)++;
    if (other && other->online_status) { scratch[0] = 0; (void)other->online_status(other->ctx, (sdshim_u8 *)scratch); }
    rc = mine && mine->online_status ? mine->online_status(mine->ctx, osb) : -1;
    return rc;
}
static void d_ba(void *c)
{
    const struct sdshim_backend *b = g_active ? cur() : by_origin();
    (void)c;
    BRS(6)++;
    od_end_req = 1;                                    /* the engine session ends with the cleanup, whichever backend ran it (s12_gc.c peer_watch) */
#if MS_TMM
    if (TM.phase != TMM_P_OFF) {                       /* a tiered search: whichever side still holds a search is cleaned (B at any point); a side already left is not touched again */
        if ((TM_held & 2u) && g_br && g_br->cleanup) g_br->cleanup(g_br->ctx);
        if ((TM_held & 1u) && g_ms && g_ms->cleanup) g_ms->cleanup(g_ms->ctx);
        tm_clear();
        g_active = 0;
        BRS(1) = 0;
        return;
    }
#endif
    if (b && b->cleanup) b->cleanup(b->ctx);
    g_active = 0;
    BRS(1) = 0;
}
static void d_ui(void *c, sdshim_u8 cmd)
{
    const struct sdshim_backend *b = g_active ? cur() : by_origin();
    (void)c;
    if (cmd == 0xB6u && MENU_CUR == 8u) {              /* the Log-in row (CONST_SlippiCmdOpenLogIn): no sign-in here yet; say why, start nothing */
        const char *w = sbra_login_words(&A);
        g_msg = w ? w : "Log in: not available yet";
        g_msg_until = clock_ms_void() + 6000u;
        return;
    }
    if (b && b->ui_action) b->ui_action(b->ctx, cmd);
}
static int d_be(void *c, const sdshim_u8 *req, sdshim_u8 *rsp)
{
    const struct sdshim_backend *b = g_active ? cur() : by_origin();
    (void)c;
    return b && b->fetch_suggestion ? b->fetch_suggestion(b->ctx, req, rsp) : -1;
}
static void d_bd(void *c, const sdshim_u8 *rgb)
{
    const struct sdshim_backend *b = cur();
    (void)c;
    if (b && b->report_game) b->report_game(b->ctx, rgb);
    if (g_active == 2 && rep_game_hook) rep_game_hook(rgb, A.B.match.match_id, A.B.match.opp_uid, A.B.match.local_idx);   /* a bridge (Slippi opponent) game ended: queue its report, sent later in a menu */
}
#ifdef MS_REP_OVL
/* the report overlay's view of the card identity (menus/rep/rep_res.c): the Slippi uid and play key, copied for ONE request by the overlay (R7: never shown, never logged, wiped by it) */
int br_rep_ident(char *uid, u32 uc, char *key, u32 kc)
{
    const struct sbr_ident *id = A.B.id;
    u32 i;
    if (!A.B.id_ok || !id) return 0;
    for (i = 0; id->uid[i]; i++) { if (i + 1u >= uc) return 0; uid[i] = id->uid[i]; }
    uid[i] = 0;
    for (i = 0; id->play_key[i]; i++) { if (i + 1u >= kc) { uid[0] = 0; return 0; } key[i] = id->play_key[i]; }
    key[i] = 0;
    return 1;
}
const char *br_rep_platform(void) { return "console"; }      /*: one card build runs on GameCube, Wii, Wii U and Dolphin and cannot tell them apart honestly, so the User-Agent says only "console" */
u32 br_rep_build(void) { return sbr_build_seq; }
#endif
/* The report client's scene test (rep_scene.h): the main menus (not Slippi's online submenu), or Slippi's online character screen while it is idle (after a
   game, before the next search: no search, ticket, matchmaker or opponent connection, no engine session).  Never in a match.  A search that starts turns this 0 at once (d_b4/d_b5
   also call rep_search_hook, so the overlay is gone before the bridge needs the network). */
int br_menu_scene_ok(void)
{
    return rep_scene_class(SCENE_WORD, MENU_CUR, origin() == 2, g_active, sbra_engine_live(&A), sbra_idle(&A)) != REP_SC_NONE;
}
static void d_rs(void *c, sdshim_u8 cmd, sdshim_u8 arg)
{
    const struct sdshim_backend *b = cur();
    (void)c;
    if (b && b->report_status) b->report_status(b->ctx, cmd, arg);
}
static void d_ss(void *c, sdshim_u8 cmd, const sdshim_u8 *ssrb)
{
    const struct sdshim_backend *b = (g_active == 2 && sbra_engine_live(&A)) ? g_ms : cur();   /* ENGINE HAND-OFF: B1/B2 are the engine's */
    (void)c;
    if (b && b->savestate) b->savestate(b->ctx, cmd, ssrb);
}
static void d_log(void *c, sdshim_u8 level, const char *text)
{
    (void)c;
    if (g_ms && g_ms->log) g_ms->log(g_ms->ctx, level, text);
}

/* on_backend.c is compiled with -Dsdshim_set_backend=br_set_backend: its od_set_backend ends here with the finished Multishine backend (the engine's
   B0/B1/B2/B5 slots wrapped by the online menus' B3/B4/B9/BA/BE/BD) */
void br_set_backend(struct sdshim *s, const struct sdshim_backend *b)
{
    core_init();
    g_ms = b;
    DISP.ctx = 0;
    DISP.online_inputs = d_b0;
    DISP.match_state = d_b3;
    DISP.find_opponent = d_b4;
    DISP.set_selections = d_b5;
    DISP.online_status = d_b9;
    DISP.cleanup = d_ba;
    DISP.ui_action = d_ui;
    DISP.fetch_suggestion = d_be;
    DISP.report_game = d_bd;
    DISP.report_status = d_rs;
    DISP.savestate = d_ss;
    DISP.log = d_log;
    sdshim_set_backend(s, &DISP);
}
