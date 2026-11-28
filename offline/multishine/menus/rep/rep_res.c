/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_res: the RESIDENT side of the report overlay ms/rep.ovl (rep_ovl.h).  #included into on_backend.c in the MS_REP_OVL build only (it
 * shares hub_res.c's heap, card and cache helpers); the queue code (rep_q.c) is included here too, so the engine gains no manifest unit.  What it does:
 *   - rep_game_hook (the strong definition of the weak hook in br_glue.c): a bridge (Slippi Unranked) game ended: queue its report in the RESIDENT `struct rep_keep`.  Nothing is
 *     queued unless OUR server announced capability bit 15 to this console (the signed HELLO: the only gate, no setting).  No TCP, no secrets, a few hundred bytes of RAM;
 *   - rep_menu_hook (br_glue.c br_tick, once per scene-loop pass, outside the network lock): notes the HELLO clock, and while the gate is open (server bit, a report due, a trusted
 *     time, a main-menu scene with no bridge or engine session) loads "ms/rep.ovl" into ONE block of the HSD heap and runs it once per pass inside the EXI-lock window; it gives the
 *     block back the moment nothing is due and no session is open, or the scene is not a menu;
 *   - rep_search_hook (br_glue.c d_b4/d_b5): a search or lock-in starts on the character screen: the session is aborted and the block freed at once, the report stays queued;
 *   - rep_scene_change (hub_scene_change, the lbHeap_80015900 hook, every scene transition): the block goes back BEFORE the HSD heap is rebuilt, and the sdnet TCP hooks are cleared
 *     by the resident whether or not the block is still live: nothing of the overlay can be called in a match, and none stays on the online character screen past its idle window.
 * Every failure leaves the game untouched; the queue stays (the report waits for the next menu visit, 30 minutes at most). */
#include "rep_ovl.h"
#include "rep_scene.h"
#include "rec_gc.h"
#include "rep_q.c"

extern int br_menu_scene_ok(void);                                       /* br_glue.c: main menus, not Slippi's online submenu, no bridge or engine session */
extern struct sddns *br_dns(void);                                       /* br_glue.c: the bridge's resolver, shared with the hub (menus only) */
extern int br_rep_ident(char *uid, unsigned int uc, char *key, unsigned int kc);   /* br_glue.c: the Slippi uid and play key of the card identity (R7: copied for one request) */
extern const char *br_rep_platform(void);
/* the Slippi 3.7.0 matchmaker (br_glue.c): mailbox, full identity copy for one search, UDP on the netplay socket (inside the net window only) */
extern int br_mm_want(void);                                             /* 1 while a bridge search needs the matchmaker client (a command is out or the search is queued) */
extern struct sbr_mbx *br_mm_mbx(void);
extern int br_mm_ident(struct sbr_ident *out);
extern int br_udp_send(unsigned int ip, unsigned short port, const unsigned char *b, unsigned int n);
extern int br_udp_recv(unsigned char *b, unsigned int cap, unsigned int *ip, unsigned short *port);
extern unsigned int br_rep_build(void);
extern struct sdnet *ms_tls_net(void);                                   /* s12_gc.c: the engine's sdnet (built with SDNET_ENABLE_TCP=1) and the lock window helpers */
extern unsigned int ms_tls_net_size(void);
extern unsigned int ms_tls_dns(void);
extern void ms_tls_detach(void);
extern int ms_tls_begin(void);
extern void ms_tls_end(void);
extern void ms_tls_poll(struct sdnet *s);
extern unsigned int sdnet_csum_add(unsigned int sum, const unsigned char *p, unsigned int len);
extern unsigned short sdnet_csum_fold(unsigned int sum);
extern int sdnet_tx_ip(struct sdnet *s, unsigned char proto, unsigned int dst_ip, unsigned short plen);
extern int ons_seed(unsigned char *rec, unsigned int frame, int write);  /* on_set.c: the stored entropy seed of MultishineOptions */
extern volatile unsigned int ml_st[];                                    /* ml_glue.c / ml_res.c: word 2 (S_FRAME) is the menu frame counter the settings layer schedules its card writes on */

#define RP_ML_FRAME 2u
#define RP_FILE_MAX 0x28000u                                             /* the file in the pack (packer budget 120 KB) */
#define RP_BSS_BLK 0xC000u                                               /* bss room beyond the file in the block (the packer refuses a larger bss) */
#define RP_WAIT_MAX 600u                                                 /* passes (about 10 s) a load may wait for the card pack / an idle card in one menu visit */

volatile unsigned int rep_st[RP_N] OD_DATA = { 0x52455053u };            /* 'REPS' */
static struct rep_keep REPK OD_DATA;
static struct rep_ovl_env RENV OD_DATA;
static struct {
    const struct rep_ovl_api *api;     /* non-zero only while the block is ours and entered */
    unsigned char *blk;
    unsigned int inited, tries, gave_up, key, load_n, selftest, noheap_seen;
} RP OD_DATA;

static unsigned int rp_k_ms(void) { return od_clock_ms(); }
/* the only gate: our server's signed HELLO announced capability bit 15 to this console */
/* 1 announced; 0 final: answered without it ON A DEVICE SESSION (or the console has no device identity, which no 'only' list can match); -1 not known yet: no HELLO, or only an anonymous one so far (the
   title's update check runs before the card identity is read; in the server's 'only' mode the bit comes only on a device session): the hub overlay sends ONE refresh HELLO once the identity exists (hub_gr_q[HUB_GR_HELLO] = 2) */
/* LOCAL FORK: the gate is the Lucky Stats key on the card (1 there, 0 definitely not, -1 not read yet); the Multishine server has no say */
extern int sbrc_read_lucky(char *key);
static char LK_key[72] OD_DATA;
static int LK_have OD_DATA = -1;
static unsigned int LK_next OD_DATA, LK_tries OD_DATA;
static int rp_k_server(void) { return LK_have; }
int rep_rec_wanted(void) { return LK_have == 1; }                 /* LOCAL FORK: rec_gc.c records only when the game could be uploaded */
static void rp_lucky_read(unsigned int now)
{
    int r;
    if (LK_have != -1 || (int)(now - LK_next) < 0) return;
    LK_next = now + 1000u;
    r = sbrc_read_lucky(LK_key);
    if (r == 1) LK_have = 1;                                        /* SBRC_L_OK */
    else if (r == 2 || r == 4 || r == 5) LK_have = 0;               /* NOFILE, DAMAGED, NEWER */
    else if (r != 7 && ++LK_tries > 30u) LK_have = 0;               /* no card / errors: give up after 30 tries (a busy card is asked again for ever) */
}
static int rp_lucky_ident(char *uid, unsigned int uc, char *key, unsigned int kc)
{
    unsigned int i;
    if (LK_have != 1 || uc < 2u || kc < 69u) return 0;
    uid[0] = '-'; uid[1] = 0;
    for (i = 0; i < 68u; i++) key[i] = LK_key[i];
    key[68] = 0;
    return 1;
}
/* LOCAL FORK: trusted time = the console's own clock (the Multishine HELLO is gone).  It only decides certificate validity dates; chain, host name and signatures are checked as before.
   Melee's OSGetTime: ticks of 40.5 MHz since 2000-01-01.  A clock before the build's year is no clock (0 = none: no TLS session, the search says the matchmaker cannot be reached). */
#define RTC_2000 946684800u
#define RTC_MIN 1790000000u
static unsigned int rp_rtc(void)
{
    unsigned long long t = ((unsigned long long (*)(void))0x8034C3F0u)();
    unsigned int bus = *(volatile unsigned int *)0x800000F8u, div = bus ? (bus >> 2) : 40500000u;
    unsigned int hi = (unsigned int)(t >> 32), lo = (unsigned int)t, q = 0, r = 0, i;
    if (!div || hi >= div) return 0;
    r = hi;                                                         /* 64 / 32 long division without libgcc: the quotient fits in 32 bits because hi < divisor */
    for (i = 0; i < 32u; i++) { r = (r << 1) | (lo >> 31); lo <<= 1; q <<= 1; if (r >= div) { r -= div; q |= 1u; } }
    q += RTC_2000;
    return q >= RTC_MIN ? q : 0;
}    /* 1 announced, 0 answered without it, -1 no HELLO yet */
static void rp_init(void)
{
    if (RP.inited) return;
    RP.inited = 1;
    rep_keep_init(&REPK, rp_k_ms, rp_k_server);
}

#ifdef MS_REP_LINE
/* TEMPORARY (only consoles the server lets report ever see it).  Was LAB / CUBE READOUT ONLY ( owner question 4 "show a dropped-report line or stay silent" is open, the default is silent): when a counter of the report client changes, one status line on the Main Menu for
   6 s (br_say, the line of the Slippi row's reasons): "REP ok:1 try:2 dns:0 tcp:0 tls:0 sf:0 gq:0 id:0 e:TTCCSS" (colons: the menu font has no equals sign) (e = last TLS error, certificate error, sec error as hex bytes).  The first live report of our console reads it. */
#include "rep_line.h"
extern void br_say(const char *msg);
static char rp_line[176];
static unsigned int rp_line_key;
/* EVERY counter that can say why a report did not go out (a line that hides the reason is useless).  Letters, numerals, space., ' -: only (the menu font). */
static void rp_say(void)
{
    const struct rep_stats *s = &REPK.st;
    unsigned int srv = (unsigned int)(rp_k_server() + 1), drops = s->dropped_max + s->dropped_age + s->dropped_cert + s->dropped_off, i;
    unsigned int key = s->ok * 1000003u + s->attempts * 10007u + s->dns_fail * 997u + s->tcp_fail * 331u + s->tls_fail * 127u + s->success_false * 31u + s->graphql_errors * 7u + s->no_ident * 3u + s->enq * 101u
                       + s->off * 13u + drops * 17u + s->mode_refused * 19u + s->bad * 23u + srv * 29u + s->http_status_bad * 37u + s->parse_bad * 41u + s->rsp_timeout * 43u + s->no_mem * 47u
                       + s->held_scene * 53u + s->held_time * 59u + s->held_ent * 61u + s->held_cooldown * 67u + s->scene_aborts * 71u + s->sessions * 73u + (unsigned int)s->last_http * 79u + s->full_dropped * 83u;
    for (i = 0; i < sizeof s->last_msg && s->last_msg[i]; i++) key = key * 31u + (unsigned char)s->last_msg[i];
    if (key == rp_line_key) return;
    rp_line_key = key;
    rep_line_fmt(rp_line, (unsigned int)sizeof rp_line, s, srv);                 /* bounded: cut at sizeof rp_line - 1 (review M6) */
    br_say(rp_line);
}
#endif

static void rp_mm_proof(void)
{
    const struct sbr_mbx *m = br_mm_mbx();
    rep_st[RP_MM_STATE] = m->state; rep_st[RP_MM_ALIVE] = m->alive; rep_st[RP_MM_ERR] = m->err;
    rep_st[RP_MM_STUN] = m->stun_state | (m->nat << 8);
    rep_st[RP_MM_JOINS] = (m->n_joins & 0xFFu) | ((m->n_resumes & 0xFFu) << 8) | ((m->n_drops & 0xFFu) << 16);
    rep_st[RP_MM_SEC] = m->sec_err;
}

static void rp_proof(void)
{
    const struct rep_stats *s = &REPK.st;
    rp_mm_proof();
    rep_st[RP_ENQ] = s->enq; rep_st[RP_OFF] = s->off; rep_st[RP_MODE] = s->mode_refused; rep_st[RP_BAD] = s->bad; rep_st[RP_FULL] = s->full_dropped;
    rep_st[RP_ATTEMPTS] = s->attempts; rep_st[RP_OK] = s->ok; rep_st[RP_SUCCESS_FALSE] = s->success_false; rep_st[RP_GQL_ERR] = s->graphql_errors;
    rep_st[RP_HTTP_BAD] = s->http_status_bad; rep_st[RP_PARSE_BAD] = s->parse_bad; rep_st[RP_RSP_TIMEOUT] = s->rsp_timeout;
    rep_st[RP_TCP_FAIL] = s->tcp_fail; rep_st[RP_TLS_FAIL] = s->tls_fail; rep_st[RP_DNS_FAIL] = s->dns_fail; rep_st[RP_NO_IDENT] = s->no_ident; rep_st[RP_NO_MEM] = s->no_mem;
    rep_st[RP_SESSIONS] = s->sessions;
    rep_st[RP_DROP_MAX] = s->dropped_max; rep_st[RP_DROP_AGE] = s->dropped_age; rep_st[RP_DROP_CERT] = s->dropped_cert; rep_st[RP_DROP_OFF] = s->dropped_off;
    rep_st[RP_HELD_SCENE] = s->held_scene; rep_st[RP_HELD_TIME] = s->held_time; rep_st[RP_HELD_COOLDOWN] = s->held_cooldown; rep_st[RP_HELD_ENT] = s->held_ent;
    rep_st[RP_LAST_ERRS] = (unsigned int)s->last_tls_err | ((unsigned int)s->last_cert_err << 8) | ((unsigned int)s->last_sec_err << 16) | ((unsigned int)s->last_tcp_err << 24);
    rep_st[RP_PENDING] = (unsigned int)rep_pending(&REPK);
    rep_st[RP_LAST_HTTP] = s->last_http;
#ifdef MS_REP_LINE
    rp_say();
#endif
}

/* the pad of all four ports, for the entropy pool (the same read as the sign-in overlay's: ml_glue.c sign_pad) */
static int rp_mag(int x, int y) { return (x < 0 ? -x : x) + (y < 0 ? -y : y); }
static void rp_pad(struct rep_pad *p)
{
    unsigned int i, b = 0, any = 0;
    int sx = 0, sy = 0, cx = 0, cy = 0, tl = 0, tr = 0;
    for (i = 0; i < 4u; i++) {
        unsigned int base = 0x804C1FACu + i * 0x44u, s = H_W(base + 0x18u), a = H_W(base + 0x1Cu);
        int x, y;
        if (*(volatile unsigned char *)(base + 0x41u)) continue;
        any = 1;
        b |= H_W(base) & 0xFFFFu;
        x = (signed char)(s >> 24); y = (signed char)(s >> 16);
        if (rp_mag(x, y) > rp_mag(sx, sy)) { sx = x; sy = y; }
        x = (signed char)(s >> 8); y = (signed char)s;
        if (rp_mag(x, y) > rp_mag(cx, cy)) { cx = x; cy = y; }
        if ((int)((a >> 24) & 0xFFu) > tl) tl = (int)((a >> 24) & 0xFFu);
        if ((int)((a >> 16) & 0xFFu) > tr) tr = (int)((a >> 16) & 0xFFu);
    }
    p->buttons = (unsigned short)b; p->sx = (signed char)sx; p->sy = (signed char)sy; p->cx = (signed char)cx; p->cy = (signed char)cy;
    p->tl = (unsigned char)tl; p->tr = (unsigned char)tr;
    p->err = any ? 0u : *(volatile unsigned char *)(0x804C1FACu + 0x41u);
}
static unsigned int rp_tb(void) { unsigned int t; H_TB(t); return t; }
static int rp_seed(unsigned char *rec, int write) { return ons_seed(rec, ml_st[RP_ML_FRAME], write); }

static void rp_env_init(void)
{
    if (RENV.abi) return;
    RENV.abi = REP_OVL_ABI; RENV.sz_sdnet = ms_tls_net_size(); RENV.sz_keep = (unsigned int)sizeof REPK; RENV.sz_rep_ovl_env = (unsigned int)sizeof RENV; RENV.sz_mbx = (unsigned int)sizeof(struct sbr_mbx);
    RENV.net = ms_tls_net(); RENV.keep = &REPK;
    RENV.dns_server = ms_tls_dns; RENV.now_ms = od_clock_ms; RENV.tb = rp_tb; RENV.scene_ok = br_menu_scene_ok;
    RENV.ident = rp_lucky_ident; RENV.iso_hash = 0;                   /* no card-file MD5 of the image yet: isoHash is the empty string (what the stock client sends while its hasher is unfinished) */
    RENV.seed = rp_seed; RENV.pad = rp_pad;
    RENV.net_begin = ms_tls_begin; RENV.net_end = ms_tls_end; RENV.net_poll = ms_tls_poll;
    RENV.csum_add = sdnet_csum_add; RENV.csum_fold = sdnet_csum_fold; RENV.tx_ip = sdnet_tx_ip;
    RENV.build = br_rep_build(); RENV.platform = br_rep_platform();
    RENV.rec_pending = ms_rec_pending; RENV.rec_read = ms_rec_read; RENV.rec_forget = ms_rec_forget;
    RENV.mbx = br_mm_mbx(); RENV.ident_full = br_mm_ident; RENV.udp_send = br_udp_send; RENV.udp_recv = br_udp_recv;
}

/* give the block back: the overlay first aborts its session (an RST needs the lock window), then the block is freed, both only while the block is still on the heap's list */
static void rp_free(void)
{
    if (RP.blk) {
        int live = hub_cell_live(RP.blk);
        if (live && RP.api) {
            int own = ms_tls_begin();
            RP.api->leave();
            if (own) ms_tls_end();
        } else rep_st[RP_LEAK]++;
        ms_tls_detach();                                            /* the stack's hooks into the block go away whether or not the block can still be called */
        { struct sddns *d = br_dns(); if (d && d->state == SDDNS_BUSY) sddns_abort(d); }   /* a lookup of ours must not outlive the block (the scene is ending: the hub's would end anyway) */
        if (live) { H_OS_FREE(RP.blk); rep_st[RP_FREES]++; }
        REPK.cur = -1;                                              /* nothing is in flight any more; the queue entries and their attempt counts stay */
    }
    RP.blk = 0; RP.api = 0;
    rep_st[RP_STATE] = 0; rep_st[RP_BLOCK] = 0;
}

/* free bytes in the HSD heap (the sum of the free list; hub_hsd_largest is the biggest single cell) */
static unsigned int rp_hsd_free(void)
{
    unsigned int hd = H_W(H_HEAP_ARRAY), c, t = 0;
    if (!hd) return 0;
    hd += 12u * (unsigned int)H_HEAP_ID();
    for (c = H_W(hd + 4u); c; c = H_W(c + 4u)) t += H_W(c + 8u);
    return t;
}

static void rp_fail(unsigned int err)
{
    rep_st[RP_FAILS]++; rep_st[RP_ERR] = err; rep_st[RP_STATE] = 3;
    RP.gave_up = 1;
}

/* one load attempt; returns 1 when the overlay is entered */
static int rp_load(void)
{
    struct sgo_hdr h;
    unsigned char *p, *img, *area;
    unsigned int n, cap, rel, bss, clr, i, t0, t1;
    int e, rc;
    const struct rep_ovl_api *api;
    if (!df_st || !df_path || !df_ovl_size || !df_ovl_produce) { rp_fail(RE_NOPACK); return 0; }
    if (!br_dns()) { rep_st[RP_WHY] = 1u << 3; return 0; }          /* the resolver is bound at link up: no network yet, nothing is read from the card for nothing */
    if (df_st[1] == 0u || !hub_card_idle() || H_LOADING) {          /* table not read yet / card or DVD busy / the scene is still loading: next pass */
        rep_st[RP_STATE] = 1; rep_st[RP_WHY] = 1u << 4;
        if (++RP.tries > RP_WAIT_MAX) rp_fail(df_st[1] == 0u ? RE_NOPACK : RE_BUSY);
        return 0;
    }
    if (df_st[1] != 1u) { rp_fail(RE_NOPACK); return 0; }
    e = df_path(REP_OVL_PATH + 1);                                  /* "ms/rep.ovl" */
    if (e < 0x4000) { rp_fail(RE_NOFILE); return 0; }
    rc = df_ovl_size(e);
    if (rc < 64 || (unsigned int)rc > RP_FILE_MAX || (rc & 31)) { rp_fail(RE_SIZE); return 0; }
    n = (unsigned int)rc; cap = n + 32u;
    {   unsigned int fr = rp_hsd_free();                                 /* the margin the Slippi 3.7.0 doc asks to measure: free HSD bytes just before the block is taken */
        rep_st[RP_HSD_FREE] = fr;
        if (!rep_st[RP_HSD_MIN] || fr < rep_st[RP_HSD_MIN]) rep_st[RP_HSD_MIN] = fr;
    }
    if (!rep_heap_room(hub_hsd_largest(), rp_hsd_free(), 32u + n + RP_BSS_BLK)) {   /* the online character screen has 318 KB free at its minimum: never load into less than the block plus a reserve */
        if (!RP.noheap_seen) { RP.noheap_seen = 1; rep_st[RP_NOHEAP]++; }
        rep_st[RP_WHY] = 1u << 7; rep_st[RP_STATE] = 0;
        return 0;
    }
    H_TB(t0);
    RP.blk = (unsigned char *)H_OS_ALLOC(32u + n + RP_BSS_BLK);
    if (!RP.blk) { rp_fail(RE_NOMEM); return 0; }
    p = (unsigned char *)(((unsigned int)RP.blk + 31u) & ~31u);
    rc = df_ovl_produce(e, p, cap);
    if (rc <= -100) { H_OS_FREE(RP.blk); RP.blk = 0; rep_st[RP_WHY] = 1u << 4; if (++RP.tries > RP_WAIT_MAX) rp_fail(RE_BUSY); return 0; }   /* the card session was refused: later */
    if (rc != 0) { H_OS_FREE(RP.blk); RP.blk = 0; rp_fail(RE_READ); return 0; }
    h.magic = hub_rd32(p); h.ver = hub_rd32(p + 4); h.image_size = hub_rd32(p + 8); h.bss_size = hub_rd32(p + 12);
    h.nreloc = hub_rd32(p + 16); h.api_off = hub_rd32(p + 20); h.link_base = hub_rd32(p + 24); h.state_hint = hub_rd32(p + 28);
    rel = (h.nreloc * SGO_RELOC_BYTES + 31u) & ~31u;
    bss = (h.bss_size + 31u) & ~31u;
    if (sgo_check(&h, n) != SGO_E_NONE || h.state_hint != REP_OVL_KIND || bss > rel + RP_BSS_BLK || h.api_off + (unsigned int)sizeof(struct rep_ovl_api) > h.image_size) {
        H_OS_FREE(RP.blk); RP.blk = 0; rp_fail(RE_FORMAT); return 0;
    }
    img = p + 32u; area = img + h.image_size;
    sgo_relocate(img, area, h.nreloc, (unsigned int)img, h.link_base);
    clr = bss > rel ? bss : rel;
    for (i = 0; i < clr; i++) area[i] = 0;                         /* the relocation table is gone, the bss reads zero */
    H_FLUSH(img, h.image_size);
    api = (const struct rep_ovl_api *)(img + h.api_off);
    rp_env_init();
    RENV.dns = br_dns();
    if (api->abi != REP_OVL_ABI) { H_OS_FREE(RP.blk); RP.blk = 0; rp_fail(RE_ABI); return 0; }
    if (api->enter(&RENV) != 0) { H_OS_FREE(RP.blk); RP.blk = 0; rp_fail(RE_ENTER); return 0; }
    RP.api = api;
    H_TB(t1);
    if ((H_SCENE >> 24) == 8u) rep_st[RP_CSS_LOADS]++;
    rep_st[RP_STATE] = 2; rep_st[RP_LOADS]++; rep_st[RP_ERR] = 0; rep_st[RP_BLOCK] = 32u + n + RP_BSS_BLK; rep_st[RP_TICKS] = t1 - t0;
    return 1;
}

/* is a report due now: the gate is open (server bit, trusted time) and neither the back-off nor the web cool-down holds it */
static int rp_due(void) { return rep_pending(&REPK) || (LK_have == 1 && ms_rec_pending() != 0); }   /* LOCAL FORK: a report or a finished recording waits */
static unsigned int rp_gate(unsigned int now)
{
    unsigned int why = 0;
    if (!rp_due()) why |= 1u << 0;
    if (rp_k_server() != 1) why |= 1u << 1;
    if (!rep_clock_unix(&REPK.clk, now)) why |= 1u << 2;
    if ((int)(now - REPK.next_ms) < 0 || (int)(now - REPK.t_cooldown_until) < 0) why |= 1u << 6;
    return why;
}

#ifdef MS_REP_SELFTEST
/* LAB / DOLPHIN PROOF ONLY (never in a release: and the engine option list refuse it): the first time the gate is open in a menu, one synthetic
   finished Unranked game goes into the queue, as if the bridge had just ended it, so the overlay's whole path (load, DNS, TLS to a LOCAL mock with a TEST root, request, answer, free) runs without a real
   game.  A real console never has this symbol; a real report is only ever a real game. */
#ifndef MS_REP_SELFTEST_MODE
#define MS_REP_SELFTEST_MODE 1          /* the synthetic game's online mode: 1 Unranked, 2 Direct (the Dolphin proof of Direct reports) */
#endif
#ifdef MS_REP_SELFTEST_CSS
#define RP_SELFTEST_MAJOR 8u            /* queue the game only while Slippi's online character screen is up and idle (the Dolphin proof of the send between games) */
#else
#define RP_SELFTEST_MAJOR 1u
#endif
static void rp_selftest(void)
{
    static unsigned char rgb[REP_RGB_LEN];
    unsigned int i;
    if (RP.selftest || rp_k_server() != 1 || !br_menu_scene_ok() || (H_SCENE >> 24) != RP_SELFTEST_MAJOR) return;
    RP.selftest = 1;
    for (i = 0; i < REP_RGB_LEN; i++) rgb[i] = 0;
    rgb[0] = 0xBD; rgb[REP_RGB_MODE] = MS_REP_SELFTEST_MODE;
    rgb[REP_RGB_FRAMES + 2] = 0x0E; rgb[REP_RGB_FRAMES + 3] = 0x1A;                     /* 3610 frames */
    rgb[REP_RGB_GAME + 3] = 1; rgb[REP_RGB_WINNER] = 1; rgb[REP_RGB_ENDM] = 2; rgb[REP_RGB_LRAS] = 0xFF;
    for (i = 0; i < 4; i++) {
        unsigned char *pr = rgb + REP_RGB_PLAYERS + 9 * i, *pb = rgb + REP_RGB_INFO + REP_INFO_PLAYER0 + REP_INFO_PLAYER_STEP * i;
        pr[0] = (unsigned char)(i < 2 ? 0 : 3); pr[1] = (unsigned char)(i == 1 ? 2 : 0);
        pb[0] = (unsigned char)(i == 0 ? 2 : 10); pb[2] = 4; pb[3] = (unsigned char)i;
    }
    rgb[REP_RGB_INFO + REP_INFO_STAGE + 1] = 31;
    (void)rep_enqueue(&REPK, rgb, MS_REP_SELFTEST_MODE == 2 ? "mode.direct.00000000-selftest" : "mode.unranked.00000000-selftest", "SELFTESTOPPUID000000000000000000", 0, 1);
}
#endif


/* ---- the entry points ---- */
void rep_game_hook(const unsigned char *rgb, const char *match_id, const char *opp_uid, unsigned int local_idx)
{
    rp_init();
    if (local_idx > 1u) return;
    (void)rep_enqueue(&REPK, rgb, match_id, opp_uid, (int)local_idx, (int)(1u - local_idx));
    rp_proof();
}

void rep_menu_hook(void)
{
    unsigned int now, why;
    rp_init();
    now = od_clock_ms();
    { unsigned int rtc = rp_rtc(); rep_clock_note(&REPK.clk, rtc != 0u, rtc, now); }
    if (br_menu_scene_ok()) rp_lucky_read(now);
#ifdef MS_REP_SELFTEST
    rp_selftest();
#endif
    if (br_mm_want()) {                                                                 /* a Slippi search needs the matchmaker client: the overlay is loaded on the menus or the online character screen, whatever the report gates say */
        unsigned int cls = H_SCENE >> 24;
        if (cls != 8u && cls != 1u) { if (RP.blk) rp_free(); rp_proof(); return; }       /* never in a match or a loading scene */
        if (RP.blk) { if (RP.api) RP.api->frame(); rp_proof(); return; }
        if (RP.gave_up) { rp_proof(); return; }
        if (rp_load()) rep_st[RP_MM_LOADS]++;
        rp_proof();
        return;
    }
    if (RP.blk && RP.api && (RP.api->busy() & 2)) { RP.api->frame(); rp_proof(); return; }   /* the search is over but the matchmaker's connection is still closing: the block stays until it is gone */
    if (!rp_due() && !RP.blk) { rep_st[RP_WHY] = 1u << 0; return; }          /* nothing to do: a few compares per pass */
    if (rp_k_server() == 0 && rep_pending(&REPK)) {                                      /* our server answered WITHOUT the bit (or took it away): the waiting reports are forgotten, nothing is ever sent */
        unsigned int i;
        for (i = 0; i < REP_QUEUE; i++) if (REPK.q[i].used) { { volatile unsigned char *z = (volatile unsigned char *)&REPK.q[i]; unsigned int k; REPK.st.dropped_off++; for (k = 0; k < sizeof REPK.q[i]; k++) z[k] = 0; } }   /* wipe uid / match id too (review L6) */
        if (RP.blk) rp_free();
        rp_proof();
        return;
    }
    if (!br_menu_scene_ok()) { if (RP.blk) rp_free(); rep_st[RP_WHY] = 1u << 3; rp_proof(); return; }   /* neither a main menu nor the idle online character screen (a match, a search, a session): never loaded, never kept */
    why = rp_gate(now);
    if (RP.blk) {
        if (why & ((1u << 1) | (1u << 3))) { rp_free(); rep_st[RP_WHY] = why; rp_proof(); return; }   /* the bit went away or the scene is no menu: out, the queue stays */
        if (RP.api) RP.api->frame();
        if (why && !RP.api->busy()) rp_free();                     /* nothing due and nothing open: the block goes back */
        rp_proof();
        return;
    }
    rep_st[RP_WHY] = why;
    if (why) { RP.tries = 0; RP.gave_up = 0; rp_proof(); return; }
    if (RP.gave_up) { rp_proof(); return; }
    (void)rp_load();
    rp_proof();
}

/* a search or lock-in starts (br_glue.c d_b4 / d_b5): the report session on the online character screen ends NOW (RST, wipe, block freed), the report stays queued for the next idle window */
void rep_search_hook(void)
{
    rp_init();
    if (RP.blk) { rp_free(); rep_st[RP_ABORTS]++; }
    RP.tries = 0;
}

void rep_scene_change(void)
{
    rp_init();
    if (RP.blk) rp_free();
    ms_tls_detach();                                                /* also when no block is ours: the hooks must never point into a freed block */
    RP.tries = 0; RP.gave_up = 0; RP.noheap_seen = 0;
    rep_st[RP_SCENES] = rep_st[RP_SCENES] + 1u;
}
