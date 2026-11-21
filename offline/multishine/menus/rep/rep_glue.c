/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_glue: see rep_glue.h.  Everything the report client needs from the surrounding image arrives through struct rep_glue_host; this file only adapts it to
   struct rep_env and drives rep_poll once per menu frame.  The ONLY gate is our server's signed HELLO (capability bit 15, per console on the server, default off):
   there is no console setting.  Two ways to run it:
   - the product: the resident image owns the `struct rep_keep` (queue, counters, HELLO clock: rep_res.c) and passes it in host->keep; ms/rep.ovl (rep_ovl.c) calls
     rep_glue_init / rep_glue_frame while it is loaded;
   - the host tests: host->keep is NULL, this file owns a private keep, notes the clock from host->hello and receives games through rep_game_hook (no -DREP_OVL). */
#include "rep_glue.h"
#include "sdtcp.h"
#include "sddns.h"

static struct rep_glue_host H;
static struct rep R;
static struct rep_dns RD;
static int g_up;
#ifndef REP_OVL
static struct rep_keep K_OWN;
#endif

#define GLUE_RX 4096u
#define GLUE_TX 2048u
static rep_u8 g_rx[GLUE_RX], g_tx[GLUE_TX];
static struct sdtcp g_tcp;
static struct sdtcp_conn g_conn[1];

struct sdtcp *rep_glue_tcp_up(struct sdnet *net, const rep_u8 seed[16])
{
    if (sdtcp_conn_init(&g_conn[0], g_rx, GLUE_RX, g_tx, GLUE_TX)) return 0;
    if (sdtcp_init(&g_tcp, net, g_conn, 1, seed)) return 0;
    sdtcp_set_window_cap(&g_tcp, 2920);         /*: the BBA ring holds two full frames; the menu frame polls once per frame */
    return &g_tcp;
}

static rep_u32 e_ms(void *c) { (void)c; return H.now_ms(); }
static rep_u32 e_unix(void *c) { (void)c; return rep_clock_unix(&R.k->clk, H.now_ms()); }
static int e_rand(void *c, rep_u8 *o, rep_u32 n) { (void)c; return H.rand(H.rand_ctx, o, n); }
static int e_dns(void *c, const char *host, rep_u32 *ip, rep_u32 now) { (void)c; if (H.dns_server) RD.server = H.dns_server(); return rep_dns_step(&RD, host, ip, now); }
static int e_ent(void *c) { (void)c; return H.entropy_ready(); }
static int e_scene(void *c) { (void)c; return H.scene_ok(); }
static int e_ident(void *c, char *uid, rep_u32 uc, char *key, rep_u32 kc) { (void)c; return H.ident && H.ident(uid, uc, key, kc); }
static int e_iso(void *c, char *o) { (void)c; return H.iso_hash && H.iso_hash(o); }
static void *e_get(void *c, rep_u32 n) { (void)c; return H.alloc ? H.alloc(n) : 0; }
static void e_put(void *c, void *p) { (void)c; if (H.release) H.release(p); }

#ifndef REP_OVL
static int k_server(void)
{
    rep_u32 caps = 0, t = 0;
    if (!H.hello || !H.hello(&caps, &t)) return -1;                     /* no HELLO answer yet */
    return (caps & REP_CAP_SLIPPI_REPORTS) != 0;
}
static rep_u32 k_ms(void) { return H.now_ms(); }
#endif

void rep_glue_init(const struct rep_glue_host *h)
{
    struct rep_env env;
    rep_u8 *b = (rep_u8 *)&env;
    rep_u32 i;
    struct rep_keep *keep = h->keep;
    H = *h;
#ifndef REP_OVL
    if (!keep) { rep_keep_init(&K_OWN, k_ms, k_server); keep = &K_OWN; }
#endif
    for (i = 0; i < sizeof env; i++) b[i] = 0;
    RD.d = H.dns; RD.busy = 0; RD.server = 0;
    env.ctx = 0; env.ms = e_ms; env.unix_time = e_unix; env.rand = e_rand; env.dns = e_dns; env.scene_ok = e_scene; env.entropy_ready = H.entropy_ready ? e_ent : 0;
    env.ident = e_ident; env.iso_hash = e_iso; env.arena_get = e_get; env.arena_put = e_put;
    env.tcp = H.tcp; env.anchors = H.anchors; env.nanchors = H.nanchors; env.build = H.build; env.platform = H.platform;
    rep_init(&R, &env, keep);
    g_up = 1;
}

struct rep *rep_glue_state(void) { return g_up ? &R : 0; }

void rep_glue_frame(int allow_heavy)
{
    rep_u32 caps = 0, t = 0, now;
    if (!g_up) return;
    now = H.now_ms();
    if (H.hello && H.hello(&caps, &t)) rep_clock_note(&R.k->clk, 1, t, now);
    rep_poll(&R, allow_heavy);
}

void rep_glue_match_start(void)
{
    if (g_up) rep_abort(&R);                             /* a match is about to start: no session survives it (the report stays queued) */
}

void rep_glue_set_tcp(struct sdtcp *t) { if (g_up) R.e.tcp = t; }

void rep_glue_leave(void)
{
    if (g_up) rep_abort(&R);
    g_up = 0;
}

#ifndef REP_OVL
void rep_game_hook(const rep_u8 *rgb, const char *match_id, const char *opp_uid, rep_u32 local_idx)
{
    if (!g_up || local_idx > 1u) return;
    (void)rep_enqueue(R.k, rgb, match_id, opp_uid, (int)local_idx, (int)(1u - local_idx));
}
void rep_menu_hook(void) { rep_glue_frame(1); }          /* menus: a crypto step may run on any frame */
#endif
