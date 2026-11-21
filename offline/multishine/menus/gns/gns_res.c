/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gns_res: the RESIDENT side of the Game Night match-time block ms/gns.ovl (gns_ovl.h).  #included into on_backend.c in the HUB_OVL_RES build
 * only, right after hub_res.c (it uses that file's H_* macros, hub_card_idle, df_* imports and od_o / od_send).  What it does:
 *   - gns_res_tick (from hub_res_tick, the first hub call of every engine pass, menu thread): while a Game Night set of a SPECTATABLE room is searched or played (mnc gn_state
 *     SEARCH, gn_room[GNS_ROOM_FLAGS] bit 0) and the "Spectate upload" switch is on, it loads "ms/gns.ovl" from the card pack into ONE block of AllM (lbHeap heap 4, persistent for
 *     the online major scene) at the character / stage select, never inside the match; it gives the block back when the set is over, and forgets it when the major scene ends
 *     (the heap is gone then: the block is never touched again, never freed unless the heap still lists it);
 *   - gns_b0 (from be_inputs in s12_gc.c, under the engine lock, only while gns_hot): arms the uploader on the first call of a match (frame <= GNS_ARM_MAX so the first
 *     datagrams still hold tag 1), hands it the engine's rings, and gets one call per B0;
 *   - gns_rx (from od_server_rx): the server's GN_REC replies, consumed here so they do not pile up in the hub client's inbox while the overlay is absent.
 * Resident cost: this file plus one word (gns_hot) and a hook call in be_inputs; nothing else of Game Night is resident.  Every failure leaves the match as it is. */
#include "gns_ovl.h"
#include "ms_match_rules.h"
typedef char gns_rf_chars_matches_od[(GNS_RF_CHARS == OD_GN_RF_CHARS && GNS_ROOM_FLAGS == OD_GN_ROOM_FLAGS) ? 1 : -1];     /* the hub overlay (on_backend.c) reads the hand-off flag with its own copies of the numbers (the two builds include different headers) */
typedef char gns_zflags_match_mnc[(GNS_RF_ZON == MNC_GN_ZON && GNS_RF_ZERR == MNC_GN_ZERR && GNS_ROOM_FLAGS == MNC_GN_FLAGS_AT) ? 1 : -1];     /* mnc.c (the hub's poll) reads the patch bits with its own copies of the numbers */
typedef char gns_rf_items_matches_glue[GNS_RF_ITEMS == 0x40u ? 1 : -1];     /* s12_gc.c od_rules reads the bit by its number */
extern struct ms_match_rules ms_rules;                                 /* s12_gc.c: the per-match rules record: every match-time question below asks it, never od_o.mode */

#define GN_LB_HEAP(i) (0x80431FA0u + 0x10u + 0x1Cu * (unsigned int)(i))   /* lbHeap_80431FA0.heap_array[i]: id +0, handle +4, start +8, size +C, type +10, transient +14, status +18 (0 = created) */
#define GN_LBMEM_FREE_HEAD (H_W(0x804318B0u + 0x62Cu))
#define GN_LBMEM_ALLOC ((unsigned int (*)(unsigned int, unsigned int))0x80014FC8u)
#define GN_LBMEM_FREE ((void (*)(unsigned int, unsigned int))0x800150F0u)
#define GN_PTR(v) ((v) >= 0x80000000u && (v) < 0x81800000u && !((v) & 3u))
#define GNS_ARM_MAX 40                 /* arm only while the match is this young (frames): the first datagram must still cover tag 1 */
#define GNS_WAIT_MAX 900u              /* passes (about 15 s) a load may wait for the pack table / an idle card / AllM, per set */
#define GNS_KEEP_FREE 0x80000u         /* AllM bytes left free after the block: the Stadium cache and the store come first */
#define GNS_FILE_MAX 0x7000u
#define GNS_BSS_RESERVE 17408u         /* bytes after the file for the block's bss (the uploader's 8 KB on-demand history, the watcher's tag ring about 6.5 KB) or its relocation table, whichever is larger */

volatile unsigned int gns_st[GS_N] OD_DATA = { 0x474E5354u };        /* 'GNST' */
volatile unsigned int gns_hot OD_DATA;                                  /* 1 while the block is loaded and the uploader's B0 hook should run */
volatile unsigned int gns_whot OD_DATA;                                 /* 1 while a WATCH session is in force (the engine runs the loopback core, be_inputs / be_state rewrite their blocks) */
static struct { unsigned char sess, active, saw, wait, leave, autoleave, b_on, b_fired, failed; unsigned int b_t0, svc_t; } GW OD_DATA;          /* sess: a watch session is in force this hand-off (BD must not report it); active: B4 started it; saw: a match was seen; b_*: the hold-B clock */
extern void *bh_toast_make(const char *line) __attribute__((weak));  /* on_bhold.c: the in-match text maker (the Game Night watcher's hints are drawn by the block, w_toast) */
static void gns_watch_tick(unsigned int major, int ingame);
static void gns_watch_off(void);
extern int gns_engine_watch(int on);                                    /* s12_gc.c: the core in / out of the loopback state (no session, delay-only, no store) */
static struct {
    const struct gns_api *api;
    unsigned int lo, handle, size, tries, gave_up, armed, games, sc_ok, chk_n;
    unsigned int idle;                                  /* the block stays in AllM between sets of the online scene (never freed at the post-match character screen: a free there hung the cube, 489); 1 = left (uploader off), re-entered by the next set */
} GN OD_DATA;
static void gns_toast(const char *line, unsigned int sticky) { if (GN.api) GN.api->w_toast(line, bh_toast_make, od_clock_ms(), H_SCENE, sticky); }     /* line: a string literal or 0 */


/* the Diagnostics switch "Spectate upload" (lab / dev builds, Settings > Diagnostics): default ON again, a stored or chosen "off" sets gns_off; the settings layer (on_set.c) writes it */
volatile unsigned int gns_off OD_DATA = 0u;                              /* ON until the settings say otherwise */
static unsigned int gns_switch_on(void) { return gns_off == 0u; }

/* 1 = a Game Night set of a spectatable room is searched / played and the uploader may run; 2 = "X: Last game" armed the WATCH hand-off (the watcher needs no switch); 3 = Friends "Add a friend" (the block only does the keyboard's text / tab) */
static volatile unsigned char gns_zwant OD_DATA;                       /* 1 while the Game Night set being searched / played is of a room type with the Zelda no-transform patch (Low-Mid Fun; the PLAY hand-off flag GNS_RF_ZELDA): the block applies the patch from it (env->zd_want) */
static unsigned int gns_wanted_(unsigned int *zw)
{
    unsigned int f;
#ifdef GNS_ZELDA_LAB                                                    /* LAB proof only: the block loads and arms in the Slippi-route rig; never in a shipping build */
    return 1u;
#endif
    if (od_o.m.gn_magic != MNC_GN_MAGIC) return 0;
    if (od_o.m.gn_state == MNC_GN_ADDFR || od_o.m.gn_state == MNC_GN_ADDFR_DONE) return 3u;     /* Friends "Add a friend": the block carries the keyboard's text / tab rewrite (ad_prep), the uploader stays off (gns_b0 arms only for 1) */
    if (!od_o.m.gn_room[0]) {                                           /*  : a Dojo / Grandmaster / Mano a mano match the server said to upload (TICKET_POLL watch byte, mnc.c MNC_UPLOAD_AT) */
        if (!gns_switch_on() || ms_rules_slippi(&ms_rules) || !ms_search_flag(od_search_mode(&od_o), MR_F_SPECTATE)) return 0;   /* our search / session live (od_search_mode: -1 outside) and its mode uploads; never a Slippi-route match */
        return od_o.m.gn_room[MNC_UPLOAD_AT] == MNC_UPLOAD_MARK ? 1u : 0u;      /* only a match the server said to upload loads the block (no AllM use for every search; if the load misses the match start, that match simply is not uploaded) */
    }
    f = od_o.m.gn_room[GNS_ROOM_FLAGS];
    if (od_o.m.gn_state == MNC_GN_SEARCH && (f & GNS_RF_ZELDA)) *zw = 1u;       /* Low-Mid Fun: the block is wanted for the patch alone (4) when the room is not spectatable or the upload switch is off: a console without the patch desyncs the pair at the first Zelda down-B */
    if (!(f & GNS_RF_SPEC)) return *zw ? 4u : 0u;                       /* the room is not spectatable: the server would refuse every datagram / watcher */
    if (od_o.m.gn_state == MNC_GN_WATCH) return 2u;
    if (od_o.m.gn_state != MNC_GN_SEARCH) return 0;
    return gns_switch_on() ? 1u : (*zw ? 4u : 0u);
}
static unsigned int gns_wanted(void)                                   /* the byte is stored once, whole: the game thread (arm) and the menu thread both call this */
{
    unsigned int zw = 0, r = gns_wanted_(&zw);
    gns_zwant = (unsigned char)zw;
    return r;
}

/* AllM (heap 4): free bytes in the gaps and the largest gap; 0 = not usable (not created, not an lbMemory heap, no handle left) */
static int gns_heap_room(unsigned int idx, unsigned int *total, unsigned int *big)
{
    unsigned int d = GN_LB_HEAP(idx), h, cur, it, n = 0, t = 0, b = 0;
    *total = 0; *big = 0;
    if (H_W(d + 0x18u) != 0u || (H_W(d + 0x10u) != 2u && !(idx != 4u && H_W(d + 0x10u) == 3u))) return 0;
    h = H_W(d + 4u);
    if (!GN_PTR(h) || !GN_LBMEM_FREE_HEAD) return 0;
    cur = H_W(h + 4u);
    it = H_W(h + 0xCu);
    for (;;) {
        unsigned int end = GN_PTR(it) ? H_W(it + 4u) : H_W(h + 8u), gap = end > cur ? end - cur : 0u;
        t += gap;
        if (gap > b) b = gap;
        if (!GN_PTR(it)) break;
        cur = H_W(it + 4u) + H_W(it + 8u);
        it = H_W(it);
        if (++n > 4096u) return 0;
    }
    *total = t; *big = b;
    return 1;
}
static int gns_allm_room(unsigned int *total, unsigned int *big) { return gns_heap_room(4u, total, big); }
static int gns_allm_lists(unsigned int h, unsigned int lo)
{
    unsigned int it = H_W(h + 0xCu), n = 0;
    while (GN_PTR(it) && n++ < 4096u) { if (H_W(it + 4u) == lo) return 1; it = H_W(it); }
    return 0;
}

/* The block is ours only while the heap still lists it.  AllM is NOT stable between the scenes of the online major scene: the first Dolphin run showed the block
   alive through the character select and the match and overwritten by the next character select (heap content reused); an unchecked pointer into it froze the game thread
   (a data access to garbage).  So: validated at every scene word change and every 64th pass, before ANY use of the table, and the server-reply entry refuses to run block code
   while the scene word differs from the one that was validated. */
static int gns_alive(void)
{
    unsigned int d = GN_LB_HEAP(4), it, n = 0;
    if (!GN.api || !GN.lo) return 0;
    if (H_W(d + 0x18u) != 0u || H_W(d + 4u) != GN.handle) return 0;
    it = H_W(GN.handle + 0xCu);
    while (GN_PTR(it) && n++ < 4096u) {
        if (H_W(it + 4u) == GN.lo) return H_W(it + 8u) >= GN.size;       /* the record of OUR allocation: same start, at least our size */
        it = H_W(it);
    }
    return 0;
}

/* The Zelda no-transform patch: the block keeps the two original words in gns_st[GS_ZD0..1]; whenever the block goes away without its own leave (the online scene ended, AllM gone, the block found
   overwritten) they are put back here, so Zelda's down-B can never stay dead outside the room type.  0 = not patched. */
static void gns_zd_restore(void)
{
    unsigned int i;
    for (i = 0; i < 2u; i++) {
        if (gns_st[GS_ZD0 + i]) {
            volatile unsigned int *p = (volatile unsigned int *)(i ? 0x8013AFA4u : 0x8013AEE0u);
            *p = gns_st[GS_ZD0 + i]; gns_st[GS_ZD0 + i] = 0u;
            H_FLUSH((void *)p, 4u);
        }
    }
}

static void gns_drop(int free_it)                   /* the block goes: back to AllM only while the heap exists and still lists it */
{
    MS_BCF(0x40, 0x41);
    gns_zd_restore();
    if (GN.api && free_it) GN.api->leave();
    gns_hot = 0;
    if (GN.lo && free_it) {
        unsigned int d = GN_LB_HEAP(4);
        if (H_W(d + 0x18u) == 0u && H_W(d + 4u) == GN.handle && gns_allm_lists(GN.handle, GN.lo)) { GN_LBMEM_FREE(GN.handle, GN.lo); gns_st[GS_FREES]++; }
    }
    GN.api = 0; GN.lo = 0; GN.armed = 0; GN.idle = 0;
    gns_st[GS_STATE] = 0; gns_st[GS_BLOCK] = 0; gns_st[GS_ADDR] = 0;
}

static void gns_release_for_hub(void)                   /* the hub loader has waited a long time with a kept report: give the AllM block back and do not load it again this scene */
{
    if (!GN.api) return;
    MS_BC(0x35);
    gns_drop(1);
    GN.gave_up = 1;
}

static void gns_fail(unsigned int err)
{
    gns_st[GS_FAILS]++; gns_st[GS_ERR] = err; gns_st[GS_STATE] = 3;
    GN.gave_up = 1;
}

static struct gns_env gns_env_tab OD_DATA;
#if defined(MS_RB_READOUT) || defined(MS_HUB_DIAG)
static int gns_send_bc(void *ctx, const unsigned char *p, unsigned int len) { int rc; MS_BC(0x56); rc = od_send(ctx, p, len); MS_BC(0x57); return rc; }    /* the uploader's datagram through the engine's socket (hang locator) */
#define GNS_SEND gns_send_bc
#else
#define GNS_SEND od_send
#endif

/* one load attempt; returns 1 when the block is entered */
static int gns_load(unsigned int want)
{
    MS_BCF(0x42, 0x43);
    struct sgo_hdr h;
    unsigned char *p, *img, *area;
    unsigned int n, cap, rel, bss, clr, i, t0, t1, tot = 0, big = 0, need, blk, lo;
    int e, rc;
    const struct gns_api *api;
    if (!df_st || !df_path || !df_ovl_size || !df_ovl_produce) { gns_fail(GE_NOPACK); return 0; }
    if (df_st[1] == 0u || !hub_card_idle()) {
        gns_st[GS_STATE] = 4; gns_st[GS_WAITS]++;
        if (++GN.tries > GNS_WAIT_MAX) gns_fail(df_st[1] == 0u ? GE_NOPACK : GE_BUSY);
        return 0;
    }
    if (df_st[1] != 1u) { gns_fail(GE_NOPACK); return 0; }
    e = df_path(GNS_PATH);
    if (e < 0x4000) { gns_fail(GE_NOFILE); return 0; }
    rc = df_ovl_size(e);
    if (rc < 64 || (unsigned int)rc > GNS_FILE_MAX || (rc & 31)) { gns_fail(GE_SIZE); return 0; }
    n = (unsigned int)rc; cap = n + 32u;
    if (!gns_allm_room(&tot, &big)) { gns_st[GS_STATE] = 4; gns_st[GS_WAITS]++; if (++GN.tries > GNS_WAIT_MAX) gns_fail(GE_NOALLM); return 0; }
    gns_st[GS_ALLM_TOT] = tot; gns_st[GS_ALLM_BIG] = big;
    need = 32u + n + GNS_BSS_RESERVE + (want == 3u ? GNS_AD_EXTRA : 0u);                                 /* file + the bss / relocation area (the header is read below; the reserve covers this block, rechecked) */
    if (big < need + GNS_KEEP_FREE) { gns_st[GS_STATE] = 4; gns_st[GS_WAITS]++; if (++GN.tries > GNS_WAIT_MAX) gns_fail(GE_NOALLM); return 0; }
    H_TB(t0);
    GN.handle = H_W(GN_LB_HEAP(4) + 4u);
    blk = GN_LBMEM_ALLOC(GN.handle, need);
    if (!GN_PTR(blk)) { gns_fail(GE_NOMEM); return 0; }
    lo = H_W(blk + 4u);
    GN.lo = lo; GN.size = need;
    p = (unsigned char *)((lo + 31u) & ~31u);
    rc = df_ovl_produce(e, p, cap);
    if (rc <= -100) { gns_drop(1); gns_st[GS_WAITS]++; if (++GN.tries > GNS_WAIT_MAX) gns_fail(GE_BUSY); return 0; }
    if (rc != 0) { gns_drop(1); gns_fail(GE_READ); return 0; }
    h.magic = hub_rd32(p); h.ver = hub_rd32(p + 4); h.image_size = hub_rd32(p + 8); h.bss_size = hub_rd32(p + 12);
    h.nreloc = hub_rd32(p + 16); h.api_off = hub_rd32(p + 20); h.link_base = hub_rd32(p + 24); h.state_hint = hub_rd32(p + 28);
    rel = (h.nreloc * SGO_RELOC_BYTES + 31u) & ~31u;
    bss = (h.bss_size + 31u) & ~31u;
    clr = bss > rel ? bss : rel;
    if (h.magic != SGO_MAGIC || h.ver != SGO_VER || h.state_hint != GNS_KIND || h.image_size + clr + 64u > need || (h.image_size & 31u) || (h.api_off & 3u)
        || h.api_off + (unsigned int)sizeof(struct gns_api) > h.image_size || 32u + h.image_size + rel != n) { gns_drop(1); gns_fail(GE_FORMAT); return 0; }
    img = p + 32u; area = img + h.image_size;
    sgo_relocate(img, area, h.nreloc, (unsigned int)img, h.link_base);
    for (i = 0; i < clr; i++) area[i] = 0;
    gns_env_tab.ad_buf = 0;
    if (want == 3u) {                                                 /* the Add-a-friend backups: GNS_AD_EXTRA zeroed bytes behind the block's own bss / relocation area */
        unsigned char *ad = (unsigned char *)(((unsigned int)area + clr + 31u) & ~31u);
        if ((unsigned int)ad + GNS_AD_EXTRA > lo + need) { gns_drop(1); gns_fail(GE_NOMEM); return 0; }
        for (i = 0; i < GNS_AD_EXTRA; i++) ad[i] = 0;
        gns_env_tab.ad_buf = ad;
    }
    H_FLUSH(img, h.image_size);
    api = (const struct gns_api *)(img + h.api_off);
    if (api->abi != GNS_ABI) { gns_drop(1); gns_fail(GE_ABI); return 0; }
    gns_env_tab.abi = GNS_ABI; gns_env_tab.sz_view = (unsigned int)sizeof(struct gns_view); gns_env_tab.sz_mw2 = (unsigned int)sizeof(mw2);
    gns_env_tab.sz_gnrec_buf = GNS_BUF_BYTES;
    gns_env_tab.w = &od_o.m.w; gns_env_tab.send = GNS_SEND; gns_env_tab.ctx = 0;
    gns_env_tab.hmac = mw2_hmac; gns_env_tab.sha512 = mw2_sha512; gns_env_tab.ed_verify = mw2_ed25519_verify; gns_env_tab.ct_equal = mw2_ct_equal;
    gns_env_tab.zd_keep = &gns_st[GS_ZD0]; gns_env_tab.zd_want = &gns_zwant;
    if (api->enter(&gns_env_tab) != 0) { gns_drop(1); gns_fail(GE_ENTER); return 0; }
    GN.api = api; GN.armed = 0; GN.size = need;
    H_TB(t1);
    gns_st[GS_STATE] = 1; gns_st[GS_LOADS]++; gns_st[GS_ERR] = 0; gns_st[GS_BLOCK] = need; gns_st[GS_ADDR] = (unsigned int)img;
    gns_st[GS_TICKS] = t1 - t0; gns_st[GS_SCENE] = H_SCENE;
    GN.sc_ok = H_SCENE; GN.chk_n = 0;
    gns_hot = 1;
    return 1;
}

static unsigned int gns_set_id(void)                                  /* "GN<code>S<set>": the digits after the last 'S' */
{
    const char *r = od_o.m.gn_room;
    unsigned int i, last = 0, v = 0;
    if (!r[0]) return 1u;                                                /* a Dojo / Grandmaster / Mano a mano match: one set (the server keys it by the match) */
    for (i = 0; i < 19u && r[i]; i++) if (r[i] == 'S') last = i + 1u;
    if (!last) return 0;
    for (i = last; i < 19u && r[i] >= '0' && r[i] <= '9'; i++) v = v * 10u + (unsigned int)(r[i] - '0');
    return v;
}

/* the game number the uploader records: a Bo3 room's ticket name carries the game ("GN<code>S<set>G<game>", game 1..9): game - 1, the same on both consoles and the same as the room's own game index, so each game of a Sweaty set is its own record;
   every other name (a one-game set, Dojo / Grandmaster / Mano a mano) keeps the arm counter */
static unsigned int gns_game_idx(void)
{
    const char *r = od_o.m.gn_room;
    unsigned int i, last = 0, g = 0;
    for (i = 0; i < 19u && r[i]; i++) if (r[i] == 'S') last = i + 1u;
    if (last) {
        for (i = last; i < 19u && r[i] >= '0' && r[i] <= '9'; i++) { }
        if (i > last && i + 1u < 19u && r[i] == 'G') {
            for (i++; i < 19u && r[i] >= '0' && r[i] <= '9'; i++) g = g * 10u + (unsigned int)(r[i] - '0');
            if (g) return g - 1u;
        }
    }
    return GN.games++;
}

static void gns_proof_copy(void)
{
    const struct gns_proof *p = GN.api ? GN.api->proof : 0;
    if (!p) return;
    gns_st[GS_SENT] = p->sent; gns_st[GS_ROLLBACK] = p->rollback; gns_st[GS_NOTREADY] = p->notready; gns_st[GS_REPLIES] = p->replies;
    gns_st[GS_REFUSED] = p->refused; gns_st[GS_MERGED] = p->merged; gns_st[GS_WHY] = p->why; gns_st[GS_BADRX] = p->bad_rx;
    if (GN.armed) gns_st[GS_STATE] = p->state == 2u ? 3u : 2u;
#ifdef MS_MATCH_MSG
    if (GN.api->mmproof) {                                                                   /* friend messages in the match: the block's counters, for the harness and the cube readout */
        const struct gns_mmproof *m = GN.api->mmproof;
        gns_st[GS_MM_POLLS] = m->polls; gns_st[GS_MM_RX] = m->rx; gns_st[GS_MM_EMPTY] = m->rx_empty; gns_st[GS_MM_SHOWN] = m->shown; gns_st[GS_MM_BAD] = m->bad; gns_st[GS_MM_LOST] = m->lost;
        gns_st[GS_MM_NOSESS] = m->nosess; gns_st[GS_MM_TXB] = m->txb; gns_st[GS_MM_RXB] = m->rxb; gns_st[GS_MM_STATE] = m->state;
        gns_st[GS_MM_PN] = m->pn; gns_st[GS_MM_PSUM] = m->psum; gns_st[GS_MM_PMAX] = m->pmax; gns_st[GS_MM_DN] = m->dn; gns_st[GS_MM_DSUM] = m->dsum; gns_st[GS_MM_DMAX] = m->dmax;
        gns_st[GS_MM_CN] = m->cn; gns_st[GS_MM_CSUM] = m->csum; gns_st[GS_MM_CMAX] = m->cmax; gns_st[GS_MM_P50] = m->per_p50; gns_st[GS_MM_P99] = m->per_p99; gns_st[GS_MM_PERMAX] = m->per_max; gns_st[GS_MM_PERN] = m->per_n;
    }
#endif
}

#ifdef MS_MATCH_MSG
/* friend messages during the match: one call per menu-thread pass.  `on` = a match of an UNRANKED kind runs (Game Night, Unranked, Direct; the allow list keeps Grandmaster
   = Ranked out whatever else is added later), the uploader is armed (the block is loaded for this match: Game Night of a spectatable room, or a match the server asked to upload) and no watch session runs
   (a watcher has its own plates).  Off: the block hides its plate and stops asking.  The check itself runs in the block's step, on the uploader's thread (be_inputs, under the engine lock): one seal at a time on the session. */
static void gns_msg_tick(int ingame)
{
    unsigned int on;
    if (!GN.api || GN.idle || H_SCENE != GN.sc_ok || H_LOADING || !GN.api->w_msg) return;
    on = ingame && GN.armed && !GW.active && (od_o.m.srv_caps & 16u) && ms_rules_flag(&ms_rules, MR_F_FRIEND_MSG);
    gns_st[GS_MM_ON] = on;
    GN.api->w_msg(od_clock_ms(), bh_toast_make, H_SCENE, on);
}
#endif

/* the black cover of the character screen (hub_soc.c hs_pick sets gn_room[GNS_ROOM_COVER] while it holds the screen black): lifted HERE, every engine pass, in any scene (the hub overlay is gone outside
   the hub scenes: the cube run 486 stayed black for 15 minutes when only the overlay lifted it): when the scene leaves the character screen, or after GNS_COVER_MAX passes at the latest, with VIFlush
   (VISetBlack alone is taken at a later retrace the flush forces).  gn_room[GNS_ROOM_CAP] = GNS_CAP_MAGIC tells the overlay this engine lifts the cover (an older engine would leave it black). */
#define GNS_COVER_MAX 900u            /* passes (15 s): long enough for a slow overlay load and the opponent's arrival, short enough that a failed search shows the screen with its status */
#define GNS_HOLD_MAX 1500u            /* (the hold now starts at the Z press: the Main Menu's scene change comes before the character screen) */
static unsigned short gns_cover_n OD_DATA, gns_hold_n OD_DATA;
static void gns_cover_black(int on)
{
    ((void (*)(int))0x80350100u)(on);                                       /* VISetBlack */
    ((void (*)(void))0x8034FF78u)();                                        /* VIFlush (VISetBlack alone is taken at a later retrace the flush forces) */
    ((volatile unsigned char *)od_o.m.gn_room)[GNS_ROOM_COVER] = (unsigned char)on;
}
/* EARLY COVER: the overlay can only black the screen once it is loaded, which on the cube comes
   seconds after the screen is up (card reads).  The engine now blacks it on the screen's first pass when the hand-off carries the room pick (ARMED + gn_room[GNS_ROOM_PICK]: the same case the overlay
   plays through), so no part of the character screen is ever drawn; it stays black until the scene leaves the screen (the match loads) or GNS_COVER_MAX passes (the search got nowhere: the screen with its status) */
static void gns_cover_tick(unsigned int major, unsigned int minor)
{
    volatile unsigned char *r = (volatile unsigned char *)od_o.m.gn_room;
    r[GNS_ROOM_CAP] = GNS_CAP_MAGIC;
    if (r[GNS_ROOM_COVER] == 3u) { gns_cover_black(0); gns_hold_n = 0; }                               /* lift request (the Friends list is up) */
    if (r[GNS_ROOM_COVER] == 4u) {                                           /* the prep screen handed a game over (sg_entry.c gn_cover_hold): black through the Main Menu, the usual cover from the character screen on */
        if (major == 8u && minor == 0u) { r[GNS_ROOM_COVER] = 1u; gns_cover_n = 0; gns_hold_n = 0; }
        else { if (++gns_hold_n > GNS_HOLD_MAX) { gns_cover_black(0); gns_hold_n = 0; } return; }
    }
    if (r[GNS_ROOM_COVER] == 2u) {                                           /* held across scenes (Friends Z, gns_addfr_tick) until the list is drawn */
        if (++gns_hold_n > GNS_HOLD_MAX) { gns_cover_black(0); gns_hold_n = 0; }
        return;
    }
    gns_hold_n = 0;
    if (major != 8u || minor != 0u) { if (r[GNS_ROOM_COVER]) gns_cover_black(0); gns_cover_n = 0; return; }
    if (gns_cover_n <= GNS_COVER_MAX && ++gns_cover_n == 1u && od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_ARMED && r[GNS_ROOM_PICK]) gns_cover_black(1);
    else if (gns_cover_n >= GNS_COVER_MAX && r[GNS_ROOM_COVER]) gns_cover_black(0);
}

/* after a finished Game Night set the console leaves the character screen by itself: once the set is
   over (mnc gn_state DONE: the BD report was taken) and the character screen has been up for GNS_LEAVE_PASSES passes (about 2 s: the result is visible, the report is on its way), the same
   exit byte Melee's own hold-B block stores is set; the menu then returns to the hub and the browser resumes the room (a pending next call-up is handled there).  Hold B still works at any time. */
#define GNS_LEAVE_PASSES 120u
#define GNS_CSS_EXITKIND (*(volatile unsigned char *)0x804D6CF6u)        /* mnCharSel_804D6CF6: 2 = the CSS leaves "back" (see the watch code below) */
static unsigned short gns_pl_n OD_DATA, gns_pk_n OD_DATA;
/* THE KEPT-REPORT FALLBACK (cube 501, Spectate Upload ON, Stadium: the hub overlay never loaded after the match, the result report stayed kept in the resident (H.pend_bd), the Game Night state stayed SEARCH, so the
   DONE rule above never fired and the character screen stayed up with no way out).  The leave must not depend on the overlay: a Game Night set whose result report is kept for the overlay's next load, with the
   character screen up for GNS_LEAVE_KEPT_PASSES passes (about 7 s: longer than a normal overlay load plus the 2 s above), takes the same exit byte; the kept report is delivered when the hub overlay loads in the menu
   (hub_res.c hub_load: pend_bd -> api->bd -> od_report_game_cb sets DONE and sends the report from there).  Needs the engine passes to run: a hard hang of the game thread cannot be left by software. */
#define GNS_LEAVE_KEPT_PASSES 420u
/* FRIENDS Z "ADD A FRIEND" (Slippi's own Direct code keyboard, no character pick).  The Friends list armed MNC_GN_ADDFR and entered the Direct character
   screen (ml_glue.c go_slippi_friend, mode 2).  Slippi opens its code keyboard from HandleInputsOnCSS.asm FN_LOAD_CODE_ENTRY (START on the idle Direct CSS with a character selected); this tick does what that
   function does, without the character and the START: name entry mode 1 (r13 - 0x505D), the callback (the address of FN_LOCK_IN_AND_SEARCH, r13 - 0x5018), the controlling player (r13 - 0x49B0 -> -0x49A7) and the
   exit byte 4 (r13 - 0x49AA, "load the name entry").  The callback address is found from the codeset's own hook word at 0x80263258 (a branch into the module; FN_LOCK_IN_AND_SEARCH is GNS_AD_CBOFF bytes into it,
   the word before it is the module's blrl and it starts with mflr r0): any other layout and the keyboard is not opened (the player leaves with B).  The confirmed code is caught in od_find (ADDFR_DONE,
   no search); this tick then leaves the CSS "back" (the Friends return mark brings the list up and sends the request); B out of the keyboard (the CSS again, state still ADDFR) leaves the same way. */
#define GNS_CSS_PLAYER (*(volatile unsigned char *)0x804D6CF0u)          /* r13 - 0x49B0: the player index in control of the CSS */
#define GNS_AD_CBOFF 0x514u
static unsigned char gns_ad_ph OD_DATA;
static unsigned short gns_ad_n OD_DATA;
static int gns_ad_open(void)
{
    unsigned int hk = *(volatile unsigned int *)0x80263258u, cb;
    if ((hk & 0xFC000003u) != 0x48000000u) return 0;
    hk &= 0x03FFFFFCu;
    if (hk & 0x02000000u) hk |= 0xFC000000u;
    cb = 0x80263258u + hk + GNS_AD_CBOFF;
    if (cb < 0x80400000u || cb > 0x81800000u || *(volatile unsigned int *)(cb - 4u) != 0x4E800021u || *(volatile unsigned int *)cb != 0x7C0802A6u) return 0;
    *(volatile unsigned char *)0x804D6669u = 0xFFu;                      /* ISWINNER_NULL, CHOSESTAGE 0: what the START path sets first */
    *(volatile unsigned char *)0x804D666Au = 0u;
    *(volatile unsigned char *)0x804D6643u = 1u;                         /* name entry in connect code mode */
    *(volatile unsigned int *)0x804D6688u = cb;                          /* the callback on a confirmed code */
    *(volatile unsigned char *)0x804D6CF9u = GNS_CSS_PLAYER;             /* the player who types */
    GNS_CSS_EXITKIND = 4u;                                               /* the CSS loads the name entry */
    return 1;
}
/* THE COVER ("skip the CSS"): the character screen is never shown.  The screen goes black (the cover of gns_cover_tick, flag value 2 = "held") on the first pass in the character scene,
   is lifted once the keyboard has been up for GNS_AD_LIT passes, goes black again the moment the keyboard closes (or the code is taken), and stays black through the scene change and the Main Menu until the Friends
   list is on screen (sg_entry.c writes 3 = "lift" then); the cover lifts by itself after GNS_HOLD_MAX passes (the list never opened) */
#define GNS_AD_LIT 12u
static unsigned char gns_ad_cov OD_DATA, gns_ad_lit OD_DATA;
static unsigned short gns_ad_k OD_DATA;
static void gns_ad_cover(void)
{
    gns_cover_black(1);
    ((volatile unsigned char *)od_o.m.gn_room)[GNS_ROOM_COVER] = 2u;
}
/* THE HELP TEXT AND THE TOP-LEFT TAB ("Friends" / "Name Entry", "the other player gets a friend invite, nobody enters my code"): rewritten in the loaded game files for this hand-off only
   and put back when the keyboard closes.  The work (the fingerprint scan, the Friends tab strip, the backups, the text) is NOT resident since (the engine had no room left for it): it lives in the
   Game Night block ms/gns.ovl (gns_ovl.c ad_prep / ad_off), which this hand-off loads like a Game Night set does (gns_wanted 3, with GNS_AD_EXTRA bytes behind it for the backups).  If the block
   cannot be loaded (no card pack, no AllM room) the keyboard opens with Slippi's own text and tab. */
static unsigned char gns_ad_on OD_DATA;                                 /* ad_prep was called: the block may hold a patched copy that must be put back */
static int gns_ad_ready(void)                                           /* 1 = the text / tab work is done or cannot be done, 0 = ask again next pass */
{
    int r;
    if (!GN.api) return GN.gave_up != 0u;                               /* still loading (the load is at the end of every gns_res_tick) unless it gave up */
    if (!gns_alive()) return 1;
    r = GN.api->ad_prep();
    if (r >= 0) gns_ad_on = 1;
    return r != 0;
}
static void gns_ad_end(int reset)                                       /* the original bytes go back (reset: and the scan is forgotten) */
{
    if (gns_ad_on && GN.api && gns_alive()) GN.api->ad_off(reset);
    if (reset) gns_ad_on = 0;
}

static void gns_addfr_tick(unsigned int major, unsigned int minor, int ingame)
{
    int css = major == 8u && minor == 0u && !ingame && !H_LOADING;
    unsigned int st = od_o.m.gn_magic == MNC_GN_MAGIC ? od_o.m.gn_state : 0u;
    if (!(st == MNC_GN_ADDFR || st == MNC_GN_ADDFR_DONE) || major != 8u) { gns_ad_end(1); gns_ad_ph = 0; gns_ad_n = 0; gns_ad_cov = 0; gns_ad_lit = 0; gns_ad_k = 0; return; }
    if (!gns_ad_cov && minor == 0u) { gns_ad_cover(); gns_ad_cov = 1; }
    if (st == MNC_GN_ADDFR_DONE) { gns_ad_end(0); if (gns_ad_lit) { gns_ad_cover(); gns_ad_lit = 0; } }              /* the code was taken: nothing of the character screen is shown */
    switch (gns_ad_ph) {
    case 0:                                                           /* the CSS is up (black): let it settle, then open the keyboard */
        if (!css || st != MNC_GN_ADDFR) { gns_ad_n = 0; break; }
        if (!gns_ad_ready()) break;
        if (++gns_ad_n > 20u && GNS_CSS_EXITKIND == 0u) {
            if (gns_ad_open()) { gns_ad_ph = 1; gns_ad_n = 0; }
            else { gns_ad_ph = 3; gns_ad_n = 40u; }                   /* not the layout this was written for: no keyboard, leave */
        }
        break;
    case 1:                                                           /* wait for the CSS to take the exit byte (the keyboard loads) */
        if (GNS_CSS_EXITKIND != 4u) { gns_ad_ph = 2; gns_ad_n = 0; gns_ad_k = 0; }
        else if (++gns_ad_n > 120u) gns_ad_ph = 0;
        break;
    case 2:                                                           /* the keyboard is up; the exit byte back at 0 = it closed */
        if (GNS_CSS_EXITKIND != 0u) (void)gns_ad_ready();                  /* ad_prep again: the shown suggestion becomes the typed code (gns_ovl.c ad_commit), once */
        if (GNS_CSS_EXITKIND != 0u) {
            gns_ad_n = 0;
            if (!gns_ad_lit && ++gns_ad_k > GNS_AD_LIT) { gns_cover_black(0); gns_ad_lit = 1; }       /* the keyboard is drawn: show it */
        } else if (css) {
            if (gns_ad_lit && gns_ad_n >= 3u) { gns_ad_cover(); gns_ad_lit = 0; }
            if (++gns_ad_n > 8u) {
                gns_ad_ph = 3; gns_ad_n = 0; gns_ad_end(0);
                if (st == MNC_GN_ADDFR) { od_o.m.gn_state = MNC_GN_NONE; od_o.m.gn_room[0] = 0; GNS_CSS_EXITKIND = 2u; gns_ad_ph = 4; }   /* cancelled (B in the keyboard): nothing to send, back to the Friends list */
            }
        }
        break;
    default:                                                          /* the code was taken (hidden): short moment, then back to the Friends list */
        if (css && GNS_CSS_EXITKIND == 0u && ++gns_ad_n > 12u) { GNS_CSS_EXITKIND = 2u; gns_ad_ph = 4; }
        break;
    }
}

static void gns_playleave_tick(unsigned int major, unsigned int minor, int ingame)
{
    int css = major == 8u && minor == 0u && !ingame && !H_LOADING && od_o.m.gn_magic == MNC_GN_MAGIC && !GW.active && !GW.sess && !GW.leave;
    if (css && od_o.m.gn_state == MNC_GN_DONE) {
        gns_pk_n = 0;
        if (++gns_pl_n > GNS_LEAVE_PASSES) {
            if (GNS_CSS_EXITKIND == 0u) { GNS_CSS_EXITKIND = 2u; gns_st[GS_AUTOLEAVE]++; }
            gns_pl_n = GNS_LEAVE_PASSES;
        }                                                    /* ("Back To The Room..." toast removed it could never draw, the character screen has no match HUD)  */
        return;
    }
    gns_pl_n = 0;
    if (css && od_o.m.gn_state == MNC_GN_SEARCH && !H.pend_bd && !ms_rules_slippi(&ms_rules) && od_req_mode(&od_o) == MNC_MODE_ROOM && od_o.phase == OD_ERR) {   /* the set's search or connection failed before any match"Couldn't reach your opponent", then Z searched nothing and "Starting the match..." stayed up for minutes): the set is over for this console,
                                                             so the DONE leave above takes it back to the room after the error was up for its 2 s; the room voids the set on the rejoin (ms_rooms back) */
        od_o.m.gn_state = MNC_GN_DONE;
        return;
    }
    if (css && od_o.m.gn_state == MNC_GN_SEARCH && H.pend_bd) {                              /* the match ended (BD seen) but the overlay has not taken the report */
        if (++gns_pk_n > GNS_LEAVE_KEPT_PASSES) {
            if (GNS_CSS_EXITKIND == 0u) { MS_BC(0x36); GNS_CSS_EXITKIND = 2u; gns_st[GS_AUTOLEAVE]++; }
            gns_pk_n = GNS_LEAVE_KEPT_PASSES;
        }
    } else gns_pk_n = 0;
}

/* NO POST-MATCH CHARACTER SCREEN.  When the match of a Game Night set has ended (its result report was kept: H.pend_bd, the hub overlay is
   absent in the match; a watch session never keeps one) the console asks the game-mode loop for the menu mode right away: gm_ChangeGameModeAfterCurrentScene(GM_MENU) (0x801A42F8; the call Melee's own character
   select makes when it is left "back"). The loop checks the flag after every scene, so the scene after the match is the menu and the character select is never loaded; the menu is
   entered the way it is from the character select (Melee derives the page from the previous game MODE, not the scene) and Slippi's own cleanup (BA) comes with the menu entry as before.  The report is delivered by
   the hub overlay that loads in the menu scene (hub_load takes pend_bd, the path of the 7 s fallback below) and the browser waits for that (ml_glue.c g_gn_open, ms_net_cost 102).  The fallbacks stay for a character
   select that is reached anyway (a match without BD): the DONE leave (2 s), the kept-report leave (7 s) and Hold B.  Timeline.  The flag is spent once the scene is not loading
   and not the match (the menu is up). */
/* THE SPECTATOR'S MATCH TOO.  A watch session (GW.active, the replay is running: GW.saw) has no report to wait for, so the
   request goes in with the first replayed frame (any end of the match scene, the replay's own end or Hold B / Hold Z, then takes the same way) and the state is set to DONE once the scene is left, as the character select
   branch of gns_watch_tick does (the browser resumes the room from any state but NONE/DIRECT/LEFT, sg_entry.c gn_handoff).  gns_skipcss: 1 = a played set, 2 = a watch. */
static unsigned char gns_skipcss OD_DATA;
static void gns_skipcss_tick(int ingame)
{
    if (ingame) {      /* (GW.saw is set by the replay's first B0 and cleared by gns_watch_off: it implies an active session) */
        if (!gns_skipcss && (GW.saw || (H.pend_bd && od_o.m.gn_state == MNC_GN_SEARCH))) { gns_skipcss = 1u + GW.saw; ((void (*)(int))0x801A42F8u)(1); }      /* gm_ChangeGameModeAfterCurrentScene(GM_MENU) */
    } else if (!H_LOADING) {
        if (gns_skipcss == 2u) { od_o.m.gn_state = MNC_GN_DONE; GW.sess = 0; }      /* (no character screen, no BA: the watch session flag must not outlive the replay: gns_watch_sess drops the NEXT game's result report while it is set) */
        gns_skipcss = 0;
    }
}

void gns_res_tick(void)
{
    MS_BCF1(0x44, 0x45);
    if (GW.active) gns_st[GS_W_TK] = 10;
    unsigned int sc = H_SCENE, major = sc >> 24, minor = sc & 0xFFu, want;
    int ingame = major == 8u && minor == 2u && !H_LOADING;
    gns_cover_tick(major, minor);                                              /* first: also when the block code below returns early */
    gns_playleave_tick(major, minor, ingame);
    gns_addfr_tick(major, minor, ingame);
    gns_skipcss_tick(ingame);
    if (GN.api && major != 8u) { if (GW.active) gns_watch_off(); gns_drop(0); GN.tries = 0; GN.gave_up = 0; return; }   /* the online major scene ended: AllM is gone, the block is forgotten */
    if (GN.api && ++GN.chk_n) {                                                         /* EVERY pass: is the block still ours?  AllM compaction (the free of the match's Stadium cache / store blocks at the post-match scene, lbMemory_8001529C) MOVES the blocks above a gap and leaves the relocated image behind at its old address as garbage; a look only every 64th pass let the proof copy below (api->proof, a pointer read from the stale image) and the reply path run on it */
        if (!gns_alive()) { gns_drop(0); gns_st[GS_LOST]++; GN.tries = 0; GN.gave_up = 0; }
        else GN.sc_ok = sc;
    }
    if (od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_NONE && !od_o.m.gn_room[0]) od_o.m.gn_room[GNS_ROOM_FLAGS] = GNS_RF_CAP_WATCH;     /* this engine can run MNC_GN_WATCH: the lobby may offer "X: Last game" */
    gns_watch_tick(major, ingame);
#ifdef MS_MATCH_MSG
    gns_msg_tick(ingame);
#endif
    if (GN.api && !(GN.chk_n & 3u)) {                                                        /* AllM free space while the block lives (what the match load had to work with): the minimum seen */
        unsigned int at, ab;
        if (gns_allm_room(&at, &ab)) {
            if (!gns_st[GS_AMIN] || at < gns_st[GS_AMIN]) { gns_st[GS_AMIN] = at; gns_st[GS_ASC] = sc; }
            if (!gns_st[GS_ABIG] || ab < gns_st[GS_ABIG]) gns_st[GS_ABIG] = ab;
        }
        gns_st[GS_ALO] = GN.lo;
    }
    want = gns_wanted();
    if (!want) {
        GN.tries = 0; GN.gave_up = 0;
        if (GN.api && !ingame) {                                                           /* the set is over: the uploader stops, the block STAYS (no AllM free / compaction at the post-match character screen); the online scene's end forgets it */
            if (!GN.idle) { MS_BC(0x54); GN.api->leave(); MS_BC(0x55); gns_hot = 0; GN.armed = 0; GN.idle = 1; gns_st[GS_STATE] = 0; }
        }
        else if (!GN.api && gns_st[GS_STATE] == 4u) gns_st[GS_STATE] = 0;
        if ((od_o.m.gn_state == MNC_GN_SEARCH || od_o.m.gn_state == MNC_GN_WATCH) && !(od_o.m.gn_room[GNS_ROOM_FLAGS] & GNS_RF_SPEC)) gns_st[GS_NOWANT] = 1;
        return;
    }
    if (GN.api && GN.idle) {                                                                 /* the next set of a spectatable room: enter the kept block again */
        if (ingame || H_LOADING) return;
        if (!gns_alive() || GN.api->enter(&gns_env_tab) != 0) { gns_drop(0); gns_st[GS_LOST]++; GN.tries = 0; GN.gave_up = 0; return; }
        GN.idle = 0; GN.armed = 0; GN.sc_ok = H_SCENE; GN.chk_n = 0; gns_hot = 1; gns_st[GS_STATE] = 1;
        return;
    }
    if (GN.api) {
        gns_proof_copy();
        if (gns_zwant && !GN.idle) {                                                         /* a patched room type: tell the server whether the patch is on; a refusal is shown in the room */
            if (GN.api->proof->zd == 1u) ((volatile unsigned char *)od_o.m.gn_room)[GNS_ROOM_FLAGS] |= GNS_RF_ZON;       /* the hub's poll carries it (mnc.c) */
            else if (GN.api->proof->zd == 2u) ((volatile unsigned char *)od_o.m.gn_room)[GNS_ROOM_FLAGS] |= GNS_RF_ZERR;
        }
        if (!ingame && GN.armed) { GN.armed = 0; }                                          /* the match ended: the next one arms again */
        return;
    }
    if (major != 8u || ingame || H_LOADING || GN.gave_up) return;                          /* load at the character / stage select, never inside the match */
    (void)gns_load(want);
}

/* from be_inputs (s12_gc.c), under the engine lock, only while gns_hot: v is built by the caller */
void gns_b0(const struct gns_view *v)
{
    MS_BCF1(0x46, 0x47);
    unsigned int now = od_clock_ms();
    if (!GN.api) { gns_hot = 0; return; }
    if (H_SCENE != GN.sc_ok) return;                                         /* the scene changed and the next tick has not validated the block yet: not this frame */
    gns_st[GS_FRAME] = (unsigned int)v->frame;
    if (!GN.armed) {
        if (v->frame > GNS_ARM_MAX || gns_wanted() != 1u || !gns_alive()) return;
        MS_BC(0x52);
        GN.api->arm(gns_set_id(), gns_game_idx(), v->lsel, v->lsel ? 28u : 0u, v->frame);
        MS_BC(0x53);
        GN.armed = 1; gns_st[GS_ARMS]++; gns_st[GS_SET] = gns_set_id();
    }
    MS_BC1(0x50);
    (void)GN.api->step(v, now);
    MS_BC1(0x51);
}

/* from od_server_rx: 1 = a GN_REC reply, consumed */
int gns_rx(const unsigned char *d, unsigned int n)
{
    MS_BCF(0x48, 0x49);
#ifdef MS_MATCH_MSG
    if (GN.api && n >= 3u && d[2] == 0xAFu) {                              /* MSG_FETCH reply (op 0x2F | 0x80): the block takes it only when it answers its own check; any other one (the hub client's) goes on */
        if (GN.idle || H_SCENE != GN.sc_ok || H_LOADING || !gns_alive()) return 0;
        return GN.api->rx(d, n, od_clock_ms());
    }
#endif
    if (!GN.api || n < 3u || d[2] < 0xBBu || d[2] > 0xBFu) return 0;
    if (GN.idle) return 1;                                                 /* the uploader is stopped (block kept): a late reply is swallowed, never run through the block */
    if (H_SCENE != GN.sc_ok || H_LOADING || !gns_alive()) return 1;       /* the scene changed since the block was validated, or AllM moved / freed it since: do not run block code, just swallow the reply */
    return GN.api->rx(d, n, od_clock_ms());
}

/* ---- WATCH ("X: Last game") ---------------------------------------------------------------------------------------------------------------------
   The lobby armed MNC_GN_WATCH; the character screen opened; the first lock-in's B4 (hub_b4 thunk) comes here instead of the hub overlay: the engine core goes into the
   loopback state (gns_engine_watch), the watcher asks the server for the room's last game, and until it holds the descriptor and a first second of tags the CSS shows
   "searching" (B3 state 2) or the reason (state 5 + text).  Then the engine's own B3 answers (state 4); the block rewrites its match block (selections of the real game, or the
   demo words) and, in the match, B0's receive block (gnw_b0).  The session ends at the match's end, at BA (B on the character screen) or when the major scene is left. */
static void gns_watch_off(void)
{
    if (GW.active) (void)gns_engine_watch(0);
    GW.active = 0; GW.saw = 0; gns_whot = 0;
}

/* B4 (FIND_OPPONENT) while a watch hand-off is armed: 1 = taken (the hub overlay must not search) */
int gns_watch_b4(void)
{
    MS_BCF(0x4A, 0x4B);
    if (od_o.m.gn_magic != MNC_GN_MAGIC || od_o.m.gn_state != MNC_GN_WATCH) { GW.sess = 0; return 0; }
    od_st.b4++; od_st.latch = 0; on_st.rk_idle = 0;
    gns_st[GS_W_BEGINS]++;
    gns_watch_off();
    if (!GN.api || !gns_alive() || H_SCENE != GN.sc_ok || gns_engine_watch(1) != 0) {      /* no block (no card file, no AllM room) or the core is busy: the CSS says so */
        od_o.phase = OD_ERR; od_o.err = OD_E_HUB; od_st.phase = OD_ERR; od_st.err = OD_E_HUB;
        return 1;
    }
    od_ob_arm();                                                    /* the watcher's requests come from the menu thread: they go through the resident send queue (od_send refuses them unarmed: first Dolphin watch run, 5 sends rc -1) */
    GW.sess = 1; GW.active = 1; GW.saw = 0;
    gns_whot = 1;
    if (od_o.m.gn_room[0] == 'G' && od_o.m.gn_room[1] == 'N' && od_o.m.gn_room[2] == 'F' && od_o.m.gn_room[3]) (void)GN.api->w_begin_friend(od_clock_ms(), (const char *)&od_o.m.gn_room[3]);      /* the Friends list's Watch: GN_SPEC A_FRIEND with the friend's code */
    else if (od_o.m.gn_room[GNS_ROOM_FLAGS] & GNS_RF_LIVE) (void)GN.api->w_begin_live(od_clock_ms());      /* X on a RUNNING set: watch it live, from its first frame */
    else (void)GN.api->w_begin(od_clock_ms());
    return 1;
}

/* BD (the game's result): in a watch session it is dropped (a spectator reports nothing); in an uploading set the uploader's closing datagram goes out now (no B0 call follows the game's end),
   through the menu-thread queue (42 bytes) so Watch Last Game is available at once */
static int gns_watch_sess(void)
{
    if (GW.sess) return 1;
    if (GN.api && GN.armed && gns_alive()) { od_ob_arm(); if (GN.api->close(od_clock_ms())) gns_st[GS_CLOSED]++; }
    return 0;
}

/* BA (cancel / B on the character screen) */
#define GNS_CSS_EXITKIND (*(volatile unsigned char *)0x804D6CF6u)        /* mnCharSel_804D6CF6: 2 = the CSS leaves "back" (the byte Melee's own hold-B block stores, on_bhold.c) */
void gns_watch_ba(void)
{
    MS_BCF(0x4C, 0x4D);
    if (GW.active) gns_watch_off();
    /* hold B on the watch's character screen, or our own exit after the replay: either way the menu returns to the ROOM LOBBY (ml_glue g_gn_back: B / hold B always
       leaves the watch to the lobby, a pending call-up stays pending there).  The state is left as it is (no MNC_GN_LEFT). */
    GW.sess = 0; GW.autoleave = 0; GW.failed = 0;
    if (od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_room[0] == 'G' && od_o.m.gn_room[1] == 'N' && od_o.m.gn_room[2] == 'F') {      /* a friend's match (the Friends list's Watch) has no room to return to: the hand-off is spent, the next lock-in searches again */
        od_o.m.gn_state = MNC_GN_NONE; od_o.m.gn_room[0] = 0; od_o.m.gn_room[GNS_ROOM_FLAGS] = GNS_RF_CAP_WATCH;
    }
}

/* od_state: the CSS / match answer of a watch session.  1 = msrb is final (*rc), 0 = not a watch session (the normal path) */
static int gns_watch_state(sdshim_u8 *msrb, int *rc)
{
    const char *n0, *n1;
    if (!GW.active || !GN.api) return 0;
    if (H_SCENE != GN.sc_ok) {                                                                     /* the first B3 of a new scene comes BEFORE the next menu pass: validate here.  Answering "searching" instead made the VS scene
                                                                                                      start see no connection (no match data copied: Player[] / StartMeleeRules zero, the spawn took Fox) */
        if (!gns_alive()) { *rc = 0; msrb[0] = 2; return 1; }
        GN.sc_ok = H_SCENE;
    } else if (!gns_alive()) { *rc = 0; msrb[0] = 2; return 1; }
    if (GN.api->w_css(msrb)) { *rc = 0; return 1; }
    *rc = eng && eng->match_state ? eng->match_state(eng->ctx, msrb) : -1;                          /* the engine: state 4 and the block (be_state swaps the real game's selections in) */
    if (*rc == 0 && msrb[0] == 4) {
        unsigned int i;
        GN.api->w_names(&n0, &n1);
        for (i = 0; i < 31; i++) { msrb[15 + i] = 0; msrb[46 + i] = 0; msrb[77 + i] = 0; msrb[170 + i] = 0; }
        for (i = 0; i < 20; i++) msrb[201 + i] = 0;
        put_name(msrb + 46, n0, 31);
        put_name(msrb + 77, n1, 31);
        put_name(msrb + 15, n0, 31);
    }
    return 1;
}

/* s12_gc.c be_state: the post-processing of the real B3 of a watch session (markers, demo words) */
int gns_w_b3(sdshim_u8 *msrb)
{
    gns_st[GS_W_B3N]++;
    if (!GN.api) return -1;
    if (H_SCENE != GN.sc_ok) { if (!gns_alive()) return -1; GN.sc_ok = H_SCENE; }      /* (the match scene's first B3 comes before the next tick, as for B0) */
    {   int rc = GN.api->w_b3(msrb, (H_SCENE & 0xFFu) != 0u);
        if ((H_SCENE & 0xFFu) == 4u) msrb[3] = 0;                 /* the VS splash (scene 0x0408 = SCENE_ONLINE_VS, minor 4; InitVsSplash.asm) puts the LOCAL player on the left and everyone else on the right: the watcher's local index 2 is an empty port, so both fighters stood on the right as a team ("0 vs 2P").  Index 0 for the splash only (fighter 0 left, fighter 1 right); the match keeps 2 (both ports remote, the pads come from the feed) */
        gns_st[GS_W_BLK] = ((unsigned int)msrb[598 + 0x0E] << 24) | ((unsigned int)msrb[598 + 0x0F] << 16) | ((unsigned int)msrb[598 + 0x60] << 8) | msrb[598 + 0x84];
        gns_st[GS_W_BLK2] = ((unsigned int)msrb[0] << 24) | ((unsigned int)msrb[1] << 16) | ((unsigned int)msrb[2] << 8) | msrb[961]; gns_st[GS_W_BLKSC] = H_SCENE;
        { unsigned int q; for (q = 0; q < 4u; q++) { const sdshim_u8 *s4 = msrb + 598 + 0x60 + 0x24 * q; gns_st[GS_W_P0 + q] = ((unsigned int)s4[0] << 24) | ((unsigned int)s4[1] << 16) | ((unsigned int)s4[2] << 8) | s4[3]; } }
        return rc;
    }                 /* the character select reads msrb[3] as ITS local player index (markers there sent a Dolphin run to the results scene): descriptor fields always, markers only past the CSS */
}
/* ... and the two selections of a real game for the engine to build the block from (0 = a words descriptor) */
int gns_w_sels(const unsigned char **s0, const unsigned char **s1)
{
    if (!GN.api) return 0;
    if (H_SCENE != GN.sc_ok) { if (!gns_alive()) return 0; GN.sc_ok = H_SCENE; }
    return GN.api->w_sels(s0, s1);
}
/* Hold B in the watched match leaves the watch AND ends the auto-watch ("Hold B... leaving the watch's character screen... ends it"): the sign overlay carries the auto-watch in the hand-off's flag byte (sg_entry.c GN_RF_AUTO
   bit 2, chain bits 3-5, the engine reads none of them otherwise) and resumes it when the lobby returns, which restarted the live watch of a set that was still running right after a leave. */
#define GNS_RF_AUTO_BITS 0x3Cu
static void gns_hold_b_leave(void)
{
    GN.api->w_leave();
    od_o.m.gn_room[GNS_ROOM_FLAGS] = (unsigned char)(od_o.m.gn_room[GNS_ROOM_FLAGS] & ~GNS_RF_AUTO_BITS);
    gns_st[GS_W_LEAVEKEY]++;
}
/* the PHYSICAL pads: the SDK's own copy of the last SI poll (SI.c InputBuffer[4][2] = INBUFH / INBUFL a port, 0x804A7F78 in the NTSC 1.02 DOL; INBUFH: Start Y X B A | 1 L R Z U D R L in bits 28..16,
       the top two bits are the error flags).  Plain RAM written only by the SDK, no register is read, and a replay never touches it.
       NOT Melee's raw pad queue (HSD_PadLibData) nor HSD_PadMasterStatus: both carry the RECORDED players' inputs during a replay, so a recorded B held 1 s left and a recorded Z started the call-up
       gestures (same read as gns_ovl.c ts_pressed).  A console whose SDK leaves the buffer at zero (Nintendont's patched PADRead) falls back to the raw queue, as before. */
static void gns_pads(unsigned int *bd, unsigned int *zd)
{
    unsigned int pt, bdown = 0, zdown = 0;
    const volatile unsigned int *ib = (const volatile unsigned int *)0x804A7F78u;
    unsigned int any = 0;
    for (pt = 0; pt < 4u; pt++) {
        unsigned int h = ib[2u * pt];
        any |= h | ib[2u * pt + 1u];
        if (!(h & 0xC0000000u)) {
            if (h & 0x02000000u) bdown = 1u;
            if (h & 0x00100000u) zdown = 1u;
        }
    }
    if (!any) {
        const unsigned char *lib = (const unsigned char *)0x804C1F78u;
        const unsigned char *q = (const unsigned char *)*(const unsigned int *)(lib + 8u);
        unsigned int qn = lib[0], qw = lib[2];
        if (q && qn) {
            q += 0x30u * (qw ? qw - 1u : qn - 1u);
            for (pt = 0; pt < 4u; pt++, q += 12) if (q[10] == 0u) {
                unsigned int btn = ((unsigned int)q[0] << 8) | q[1];
                if (btn & 0x0200u) bdown = 1u;
                if (btn & 0x0010u) zdown = 1u;
            }
        }
    }
    *bd = bdown; *zd = zdown;
}

/* s12_gc.c be_inputs: after the real s3_core_b0, under the lock.  A frame the block cannot serve is a stall (result 2), never a frame run on the loopback script's pads. */
void gns_w_b0(const unsigned char *txb, unsigned char *rxb)
{
    gns_st[GS_W_B0N]++; gns_st[GS_W_B0F] = ((unsigned int)txb[1] << 24) | ((unsigned int)txb[2] << 16) | ((unsigned int)txb[3] << 8) | txb[4];
    if (!GW.active) return;
    if (!GN.api) { rxb[0] = 3; return; }                                    /* the block vanished under a running replay: leave the match, never run it on the loopback script's pads */
    if (H_SCENE != GN.sc_ok) {                                              /* the first B0 after a scene change comes BEFORE the next tick (a stalled B0 never returns to the scene loop): validate here, else this would stall for ever */
        if (!gns_alive()) { gns_st[GS_LOST]++; rxb[0] = 3; return; }
        GN.sc_ok = H_SCENE;
    }
    GW.saw = 1;
    (void)GN.api->w_b0(txb, rxb);
    if (rxb[0] == 2u) {                                                     /* a stalled frame: the scene loop does not run while the match is stalled (s12_gc.c), so the pass that keeps the watcher alive (its requests, the silence rule, hold B) runs from here, every 40 ms (the send needs the engine lock: without it it fails and is retried) */
        unsigned int now = od_clock_ms(), bdown, zdown;
        if (now - GW.svc_t >= 40u) {
            GW.svc_t = now;
            (void)GN.api->w_tick(now);
            gns_pads(&bdown, &zdown);
            if (bdown) {
                if (!GW.b_on) { GW.b_on = 1; GW.b_fired = 0; GW.b_t0 = now; }
                else if (!GW.b_fired && now - GW.b_t0 >= 1000u) { GW.b_fired = 1; gns_hold_b_leave(); }
            } else GW.b_on = 0;
            (void)GN.api->w_b0(txb, rxb);                                   /* a leave or a dead stream ends the match now (result 3) */
        }
    }
}

/* one menu pass of a watch session: resends, HEAD, RANGE; the end of the session; the proof words */
#define minor_of(x) ((x) & 0xFFu)
static void gns_watch_tick(unsigned int major, int ingame)
{
    const struct gns_wproof *p;
    gns_st[GS_W_HOT] = gns_whot | (GW.active << 1);
#ifdef GNS_H1_DIAG        /* (off in every build: the heap investigation of is closed; the early cover needed the bytes, engine headroom was 56 B) */
    if (major == 8u) {      /* lbHeap 1 (fighter files, 432 KB): minor 0 (character select) -> h1_tot / h1_big, minor 1 / 2 (load, match) -> fl_load / fl_spawn (free bytes, largest gap) */
    unsigned int t1 = 0, b1 = 0, d1 = GN_LB_HEAP(1);
      (void)gns_heap_room(1u, &t1, &b1);
      gns_st[GS_H1_START] = H_W(d1 + 8u); gns_st[GS_H1_SIZE] = H_W(d1 + 0xCu);
      if (minor_of(H_SCENE) == 0u) { gns_st[GS_H1_TOT] = t1; gns_st[GS_H1_BIG] = b1; }
      else if (minor_of(H_SCENE) == 1u) { gns_st[GS_FL_LOAD] = t1; gns_st[GS_FL_CSS] = b1; }
      else { gns_st[GS_FL_SPAWN] = t1; }
      gns_st[GS_H1_SC] = (H_W(d1 + 0x10u) << 16) | (H_W(d1 + 0x18u) << 8) | H_W(d1 + 0x14u);
    }
#endif
    if (!GW.active && !GW.failed && major == 8u && minor_of(H_SCENE) == 0u && od_o.m.gn_magic == MNC_GN_MAGIC && od_o.m.gn_state == MNC_GN_WATCH && GN.api && H_SCENE == GN.sc_ok && gns_alive()) {
        /* "X: Last game" needs no pick and no START: the watcher begins by itself once the character screen has been up for a moment (the CSS then sees the search states and, at READY, the connected
           state with both sides ready, and leaves for the splash scene on its own: HandleInputsOnCSS.asm HANDLE_CONNECTED / CHECK_SHOULD_START_MATCH) */
        if (++GW.wait > 45u) { GW.wait = 0; (void)gns_watch_b4(); }
    } else if (!GW.active) GW.wait = 0;
    if (GW.active && major == 8u && minor_of(H_SCENE) == 0u)       /* the online CSS starts the match only when a character counts as selected: the CSS recomputes r13 - 0x49A9 every frame from the persisted character byte at 0x803F0E0A
                                                                      (80262FF8: < 25 = selected; Dolphin diag 4g: the flag itself is overwritten each frame).  The spectator picks nothing: a valid id keeps it set. */
        *(volatile unsigned char *)0x803F0E0Au = 2u;
    if (GW.leave) {                                                  /* after the replay: wait for the character screen to settle, then leave it "back" like hold B does; the BA that follows is ours (autoleave) */
        if (major != 8u) GW.leave = 0;
        else if (minor_of(H_SCENE) == 0u && !ingame && --GW.leave == 0u && GNS_CSS_EXITKIND == 0u) { GW.autoleave = 1; GNS_CSS_EXITKIND = 2u; gns_st[GS_AUTOLEAVE]++; }
    }
    if (!GW.active) return;
    if (!GN.api || major != 8u) {                                             /* the block is gone (AllM reused) or the scene left: the session cannot go on */
        int saw = GW.saw;
        gns_watch_off();
        if (saw && major == 8u) { on_st.rk_idle = 1; od_o.m.gn_state = MNC_GN_DONE; GW.leave = 30u; }   /* a match was watched and the block went before the end was seen (a leave or a dead stream): the session is over too, the character screen must not start the next watch by itself */
        return;
    }
    if (GW.saw && !ingame) {                                         /* the match is over: the session ends, the character select answers "idle" like after a reported game */
        gns_watch_off();
        on_st.rk_idle = 1;
        od_o.m.gn_state = MNC_GN_DONE;
        GW.leave = 30u;                                                  /* the replay is over: go straight back to the room lobby instead of waiting on the character screen */
        return;
    }
    {   /* the in-match hints and keys: B held 1.0 s leaves (the next B0 ends the match), a call-up shows the Z toast: hold Z 1.0 s accepts (= leave to the lobby, where the
           call-up prompt is still running), double tap declines (the server's missed-call-up rule); a single tap does nothing */
        unsigned int now = od_clock_ms(), bdown, zdown;
        gns_pads(&bdown, &zdown);
        if (ingame && GW.saw) {
            int called = GN.api->w_called();
            if (bdown) {
                if (!GW.b_on) { GW.b_on = 1; GW.b_fired = 0; GW.b_t0 = now; }
                else if (!GW.b_fired && now - GW.b_t0 >= 1000u) { GW.b_fired = 1; gns_hold_b_leave(); }
            } else GW.b_on = 0;
            if (called && !GW.b_fired) { GW.b_fired = 1; GN.api->w_leave(); gns_st[GS_W_ACCEPT]++; }      /* called up while watching (no Z gesture; a watcher is a few seconds behind the game whose end called it): the watch ends here, the lobby shows the call-up prompt */
            gns_toast(GW.b_fired ? 0 : "Hold B: Leave", 0u);
        } else gns_toast(0, 0u);
    }
    gns_st[GS_W_TK] = 1; gns_st[GS_W_TKN]++; gns_st[GS_W_TKSC] = H_SCENE;
    (void)GN.api->w_tick(od_clock_ms());
    gns_st[GS_W_TK] = 2;
    p = GN.api->wproof;
    gns_st[GS_W_STATE] = p->state; gns_st[GS_W_WHY] = p->why; gns_st[GS_W_HAVE] = p->have; gns_st[GS_W_FRAMES] = p->frames; gns_st[GS_W_FEED] = p->feed; gns_st[GS_W_RANGE] = p->range;
    gns_st[GS_W_STALLS] = p->stalls; gns_st[GS_W_FED] = p->fed; gns_st[GS_W_CMP] = p->cmp; gns_st[GS_W_BAD] = p->cmp_bad; gns_st[GS_W_FIRSTBAD] = p->first_bad; gns_st[GS_W_LEAVE] = p->leave;
    if (GW.active && !GW.saw && p->state == 6u && minor_of(H_SCENE) == 0u && !ingame && od_o.m.gn_room[0] == 'G' && od_o.m.gn_room[1] == 'N' && od_o.m.gn_room[2] == 'F') {
        /* a friend's watch that failed before any match:
           the character screen is covered black and left "back" at once (the autoleave path of a finished replay); the hand-off becomes the mark "GNX" + the reason, which the Friends list reads
           when it opens again (sg_entry.c) to say why.  No search is started again (GW.failed). */
        unsigned int why = p->why;
        gns_watch_off();
        gns_ad_cover();
        GW.failed = 1;
        od_o.m.gn_room[0] = 'G'; od_o.m.gn_room[1] = 'N'; od_o.m.gn_room[2] = 'X'; od_o.m.gn_room[3] = (unsigned char)(why < 200u ? why + 1u : 1u); od_o.m.gn_room[4] = 0;
        od_o.m.gn_state = MNC_GN_DONE; on_st.rk_idle = 1;
        GW.leave = 12u;
        gns_st[GS_AUTOLEAVE]++;
        return;
    }
    gns_st[GS_W_BADRX] = p->bad; gns_st[GS_W_SENDS] = p->sends; gns_st[GS_W_SENDRC] = p->send_rc; gns_st[GS_W_RXN] = p->rx_n; gns_st[GS_W_RXOP] = p->rx_op;
}
