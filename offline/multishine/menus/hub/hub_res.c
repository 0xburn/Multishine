/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* hub_res: the RESIDENT side of the hub overlay (hub_ovl.h).  #included into on_backend.c in the HUB_OVL_RES build only
 * (it uses that file's state, put_text / put_code and the mode-byte switch).  What it does:
 *   - hub_res_tick (from bh_tick, the first hub call of every engine pass, outside the network lock): in a hub scene (main menu, Slippi's online character select)
 *     with no overlay it loads "ms/hub.ovl" from the card pack (df_ovl_produce) into ONE block of the HSD heap, checks the header (sgo_check, kind, ABI, layout sizes),
 *     relocates it (sgo_relocate), flushes the caches and enters it; anywhere else it gives the block back;
 *   - hub_scene_change (from om_heap_policy, the lbHeap_80015900 hook) gives the block back at every scene transition, only while it is still on the heap's list;
 *   - the thunks of the overlay's entry points: od_tick, B9, B4, BE, BD, the BA tail, mnc_set_net, the Update row.  Without an overlay each answers from the resident
 *     state (see hub_ovl.h); BD and the BA tail are kept and given to the overlay at its next load.
 * The load is synchronous (the card read inside df_ovl_produce): it starts only when the card pack table is armed, Melee's card layer and the DVD are idle and the shared
 * CARD work area is free, so it never waits inside the call; otherwise it is tried again on the next pass (HUB_WAIT_MAX passes per scene, then "failed for this scene").
 * Every failure leaves the retail menus untouched; only the Multishine rows say "Online files missing on the memory card". */
#include "sg_ovl.h"
#include "mw2_crypto.h"
#include "sddns.h"
#include "hub_gate.h"

#ifndef HUB_HOST
#define H_SCENE (*(volatile unsigned int *)0x80479D30u)                 /* [major, pending, prev, minor] */
#define H_LOADING (*(volatile unsigned int *)0x80479D64u)               /* 0 when the game runs (no scene load) */
#define H_W(a) (*(volatile unsigned int *)(a))
#define H_OS_ALLOC(n) (((void *(*)(int, unsigned int))0x80343EF0u)(((int (*)(void))0x80375404u)(), (n)))   /* OSAllocFromHeap(HSD_GetHeap, n): 0 when it does not fit */
#define H_OS_FREE(p) (((void (*)(int, void *))0x80343FECu)(((int (*)(void))0x80375404u)(), (p)))
#define H_HEAP_ID() (((int (*)(void))0x80375404u)())
#define H_HEAP_ARRAY 0x804D7360u                                         /* HeapArray: 12-byte descriptors {size, free list, allocated list}; cell {prev, next, size} + 32 */
#define H_FLUSH(p, n) do { ((void (*)(void *, unsigned int))0x80344840u)((p), (n)); ((void (*)(void *, unsigned int))0x803448D4u)((p), (n)); } while (0)   /* DCStoreRange, ICInvalidateRange */
#define H_DVD_IDLE() (((int (*)(void))0x80339B4Cu)() <= 0)                /* DVDGetDriveStatus */
#define H_TB(t) __asm__ volatile("mftb %0" : "=r"(t))
#endif

extern unsigned int df_st[] __attribute__((weak));                      /* df_glue.c RAM proof block: [1] = table state (0 unread, 1 ok, 2 absent, 3 bad) */
extern int df_path(const char *path) __attribute__((weak));              /* pack name -> synthetic entry >= 0x4000 (DF_SYN_BASE) */
extern int df_ovl_size(int entry) __attribute__((weak));
extern int df_ovl_produce(int entry, unsigned char *dest, unsigned int cap) __attribute__((weak));
extern unsigned char *ms_card_wa_p;                                      /* on_hist.c: the shared CARD work area, 0 = free */
extern void od_ob_arm(void);
extern int od_send_cb(void *ctx, const mnc_u8 *p, mnc_u32 len);   /* od_glue.c (HUB_OVL_RES: not static) */
extern int od_recv_cb(void *ctx, mnc_u8 *buf, mnc_u32 cap);

#define HUB_WAIT_MAX 600u              /* passes (about 10 s) a load may wait for the pack table / an idle card in one scene */
#define HUB_BSS_EXTRA 256u             /* block bytes beyond the file for an overlay whose bss exceeds its relocation table (the packer checks the bound) */
#define HUB_FILE_MAX 0x20000u
#define HUB_BD_LEN 369u

volatile unsigned int hub_st[HB_N] OD_DATA = { 0x48425354u };          /* 'HBST' */
static struct {
    const struct hub_api *api;         /* non-zero only while the block is ours and entered */
    unsigned char *blk;
    unsigned int key, tries, gave_up, rearm, inited, seed, pend_bd, pend_ba, pend_cfg;
    unsigned char bd[HUB_BD_LEN + 3];
#ifdef UPD_TITLE
    void (*tchk)(struct hub_tchk *t, unsigned int now_ms);    /* the overlay's title check (0 = an overlay without it) */
#endif
} H OD_DATA;

#ifdef UPD_TITLE
static struct hub_tchk UT OD_DATA = { .magic = HUB_TCHK_MAGIC };
static struct fr_offer FR OD_DATA;                                      /* the first-run offer (hub_ovl.h fr_step) */
#endif

#ifdef UPD_TITLE
__attribute__((noinline))                                              /* two callers (hub_load, ut_menu_frame): one copy */
#endif
static unsigned int hub_card_idle(void)
{
#ifdef HUB_HOST
    return hub_host_idle();
#else
    unsigned int i;
    if (H_W(0x80432A68u + 0x80u) || H_W(0x80432A68u + 0x8ACu)) return 0;             /* Melee's card layer (the same check as df_glue.c / on_hist.c) */
    for (i = 0; i < 11u; i++)
        if (H_W(0x80432A68u + 0x510u + 0x54u * i) != 14u) return 0;
    if (H_W(0x80433318u + 0xCu) || H_W(0x80433318u + 0x10u)) return 0;
    if (H_W(0x804A8B38u) || H_W(0x804A8B38u + 0x110u)) return 0;
    return H_DVD_IDLE() && !ms_card_wa_p;
#endif
}

#if !defined(HUB_HOST) && (defined(MS_RB_READOUT) || defined(MS_HUB_DIAG))
/* the first failing check of hub_card_idle as a hang-locator event (0xB1..0xB8 = layer words, lb words, 804A8B38 words, DVD, shared CARD area; 0xC0+i = card slot i not idle) */
static unsigned int hub_idle_why(void)
{
    unsigned int i;
    if (H_W(0x80432A68u + 0x80u)) return 0xB1u;
    if (H_W(0x80432A68u + 0x8ACu)) return 0xB2u;
    for (i = 0; i < 11u; i++)
        if (H_W(0x80432A68u + 0x510u + 0x54u * i) != 14u) return 0xC0u + i;
    if (H_W(0x80433318u + 0xCu)) return 0xB3u;
    if (H_W(0x80433318u + 0x10u)) return 0xB4u;
    if (H_W(0x804A8B38u)) return 0xB5u;
    if (H_W(0x804A8B38u + 0x110u)) return 0xB6u;
    if (!H_DVD_IDLE()) return 0xB7u;
    if (ms_card_wa_p) return 0xB8u;
    return 0xB0u;
}
#define HUB_WHY_LOG() do { static unsigned int lw; unsigned int w_ = hub_idle_why(); HD_SET(0, (df_st[1] == 0u ? 2u : 3u) | (w_ << 8)); if (w_ != lw) { lw = w_; MS_BC(w_); } } while (0)
#else
#define HUB_WHY_LOG() ((void)0)
#endif
static void gns_release_for_hub(void);                                 /* gns_res.c (included after this file) */

#ifndef HUB_HOST
/* the block is still on the HSD heap's allocated list (a heap rebuilt under us is never touched) */
static int hub_cell_live(const unsigned char *p)
{
    unsigned int a = (unsigned int)p, hd = H_W(H_HEAP_ARRAY), c;
    if (!a || !hd) return 0;
    hd += 12u * (unsigned int)H_HEAP_ID();
    for (c = H_W(hd + 8u); c; c = H_W(c + 4u))
        if (c + 32u == a) return 1;
    return 0;
}
static unsigned int hub_hsd_largest(void)
{
    unsigned int hd = H_W(H_HEAP_ARRAY), c, m = 0;
    if (!hd) return 0;
    hd += 12u * (unsigned int)H_HEAP_ID();
    for (c = H_W(hd + 4u); c; c = H_W(c + 4u))
        if (H_W(c + 8u) > m) m = H_W(c + 8u);
    return m;
}
#endif

extern void od_core_stats(unsigned int *w);                            /* s12_gc.c (ON_BUILD) */
extern void od_core_io(unsigned char *p);                              /* s12_gc.c (ON_BUILD): the record's IO block */
extern void od_core_go(unsigned char *p);                              /* s12_gc.c (ON_BUILD): the record's game-over block */
extern void od_core_pq(unsigned char *p);                              /* s12_gc.c (ON_BUILD): the record's PQ block (pad-queue drops, guard stalls, slip, checksum shift) */
extern unsigned int upd_boot_seq(unsigned int *tried) __attribute__((weak));
static unsigned char hub_gr_q[HUB_GR_X3BYTES] OD_DATA;    /* the GAME_RECORD queue of the hub overlay + one IO block per slot (hub_ovl.h HUB_GR_IOX); zero = empty */
static void hub_core_stats(unsigned int *w)                            /* word 9 = the running engine's release number (0 = not known); the record's build field */
{
    unsigned int tried = 0;
    od_core_stats(w);
    w[9] = upd_boot_seq ? upd_boot_seq(&tried) : 0u;
    od_core_io(hub_gr_io_slot(hub_gr_q));                              /* the IO block of the record the overlay queues next, in that slot's place (an older overlay never sends it) */
    od_core_go(hub_gr_go_slot(hub_gr_q));                              /* the game-over block (frame the game ended at), same slot */
    od_core_pq(hub_gr_pq_slot(hub_gr_q));                              /* the PQ block, same slot */
}
#if defined(MS_RB_READOUT) || defined(MM_FC)
extern const char *bh_diag_lines(unsigned int *gen, unsigned int *n);   /* on_bhold.c */
#define HUB_LD_FN bh_diag_lines
#define HUB_LD_MAG HUB_LD_MAGIC
#else
#define HUB_LD_FN 0
#define HUB_LD_MAG 0u
#endif
#ifndef HUB_HOST
extern volatile unsigned int s3_cfg[];                                        /* s12_gc.c: word 3 = the online delay (ml_glue.c ENGINE_DELAY) */
extern int ons_change_delay(unsigned int v, unsigned int frame);       /* on_set.c: the Settings record */
#define HUB_CH_MAG HUB_CH_MAGIC
#define HUB_CH_DELAY (&s3_cfg[3])
#define HUB_CH_FN ons_change_delay
extern volatile unsigned int ms_ad_word __attribute__((weak));        /* s12_gc.c: the Auto delay word; absent in an engine without it */
#define HUB_AD_WORD (&ms_ad_word)
extern const struct ms_settings *ons_cur(void);                        /* on_set.c: the settings record (Wired opponents only) */
#define HUB_WF_MAG HUB_WF_MAGIC
#define HUB_WF_FN ons_cur
#else
#define HUB_WF_MAG 0u
#define HUB_WF_FN 0
#define HUB_AD_WORD 0
#define HUB_CH_MAG 0u
#define HUB_CH_DELAY 0
#define HUB_CH_FN 0
#endif
static unsigned char hub_ch_region OD_DATA;                            /* the CSS region chip's value (0 Auto): resident, so it survives the overlay's reloads */
static struct hub_env hub_env_tab OD_DATA = {
    HUB_ABI, (unsigned int)sizeof(struct od), (unsigned int)sizeof(mnc), (unsigned int)sizeof(struct od_cfg), (unsigned int)sizeof(struct od_status),
    &od_o, &od_cfg, &od_st, &on_st, &onc_st, 0u,
    od_send_cb, od_recv_cb, od_clock_ms, od_core_final, od_ob_arm,
    onc_on_status, onc_final, onc_backup, onc_save_ident, omc_on_status, omc_on_code, omc_answer, ch_from_melee,
    mw2_sha512, mw2_hmac, mw2_ed25519_verify, mw2_ct_equal,
    sddns_set_servers, sddns_cache_flush, sddns_start, sddns_poll, sddns_abort, sddns_parse_dotted, sddns_normalize_host,
    on_ranked_as_unranked,
    HUB_GS_MAGIC, hub_core_stats, hub_gr_q, (unsigned int)sizeof hub_gr_q,
    HUB_LD_MAG, HUB_LD_FN,
    HUB_CH_MAG, HUB_CH_DELAY, HUB_CH_FN, &hub_ch_region,
    HUB_AD_MAGIC, HUB_AD_WORD,
    HUB_WF_MAG, HUB_WF_FN
};

static unsigned int hub_rd32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3]; }

static void hub_free(void)
{
    MS_BCF(0x30, 0x31);
    if (H.blk) {
        if (hub_cell_live(H.blk)) {
            if (H.api) H.api->leave();
            H_OS_FREE(H.blk);
            hub_st[HB_FREES]++;
        } else hub_st[HB_LEAK_SKIP]++;
        hub_st[HB_FSCENE] = H_SCENE;
    }
    H.blk = 0; H.api = 0;
#ifdef UPD_TITLE
    H.tchk = 0;
#endif
    hub_st[HB_STATE] = 0; hub_st[HB_BLOCK] = 0; hub_st[HB_ADDR] = 0;
}

static void hub_fail(unsigned int err)
{
    hub_st[HB_FAILS]++; hub_st[HB_ERR] = err; hub_st[HB_STATE] = 2;
    MS_BC(0x34); MS_BC(0xD0u + (err & 15u)); HD_SET(0, 0x40u + err);
    H.gave_up = 1;
}

/* one load attempt; returns 1 when the overlay is entered */
static int hub_load(void)
{
    MS_BCF(0x32, 0x33);
    struct sgo_hdr h;
    unsigned char *p, *img, *area;
    unsigned int n, cap, rel, bss, clr, i, t0, t1;
    int e, rc;
    const struct hub_api *api;
    HD_INC(1);
    if (!df_st || !df_path || !df_ovl_size || !df_ovl_produce) { hub_fail(HB_E_NOPACK); return 0; }
    if (df_st[1] == 0u || !hub_card_idle()) {                       /* table not read yet / card or DVD busy: next pass */
        hub_st[HB_STATE] = 3; hub_st[HB_WAITS]++;
        HUB_WHY_LOG();
        if (H.tries == 240u && (H.pend_bd | H.pend_ba | H.pend_cfg)) gns_release_for_hub();   /* a kept report waits for the card: the uploader block goes back first */
        if (++H.tries > HUB_WAIT_MAX) hub_fail(df_st[1] == 0u ? HB_E_NOPACK : HB_E_BUSY);
        return 0;
    }
    if (df_st[1] != 1u) { hub_fail(HB_E_NOPACK); return 0; }
    e = df_path(HUB_PATH);
    if (e < 0x4000) { hub_fail(HB_E_NOFILE); return 0; }
    rc = df_ovl_size(e);
    if (rc < 64 || (unsigned int)rc > HUB_FILE_MAX || (rc & 31)) { hub_fail(HB_E_SIZE); return 0; }
    n = (unsigned int)rc; cap = n + 32u;                            /* the block has HUB_BSS_EXTRA spare bytes; a compressed entry needs gap + stream >= n + 1 (DF_OVL_SLACK) */
    hub_st[HB_HSD_FREE] = hub_hsd_largest();
    H_TB(t0);
    H.blk = (unsigned char *)H_OS_ALLOC(32u + n + HUB_BSS_EXTRA);
    if (!H.blk) { hub_fail(HB_E_NOMEM); return 0; }
    p = (unsigned char *)(((unsigned int)H.blk + 31u) & ~31u);
    rc = df_ovl_produce(e, p, cap);
    if (rc <= -100) { HD_SET(0, 8); MS_BC(0xE0u + ((unsigned int)(-rc - 100) & 15u)); H_OS_FREE(H.blk); H.blk = 0; hub_st[HB_WAITS]++; if (++H.tries > HUB_WAIT_MAX) hub_fail(HB_E_BUSY); return 0; }   /* the card session was refused: later */
    if (rc != 0) { MS_BC(0xF0u + ((unsigned int)(-rc) & 15u)); H_OS_FREE(H.blk); H.blk = 0; hub_fail(HB_E_READ); return 0; }
    h.magic = hub_rd32(p); h.ver = hub_rd32(p + 4); h.image_size = hub_rd32(p + 8); h.bss_size = hub_rd32(p + 12);
    h.nreloc = hub_rd32(p + 16); h.api_off = hub_rd32(p + 20); h.link_base = hub_rd32(p + 24); h.state_hint = hub_rd32(p + 28);
    rel = (h.nreloc * SGO_RELOC_BYTES + 31u) & ~31u;
    bss = (h.bss_size + 31u) & ~31u;
    if (sgo_check(&h, n) != SGO_E_NONE || h.state_hint != HUB_KIND || bss > rel + HUB_BSS_EXTRA || h.api_off + HUB_API_BASE_SIZE > h.image_size) {
        H_OS_FREE(H.blk); H.blk = 0; hub_fail(HB_E_FORMAT); return 0;
    }
    img = p + 32u; area = img + h.image_size;
    sgo_relocate(img, area, h.nreloc, (unsigned int)img, h.link_base);
    clr = bss > rel ? bss : rel;
    for (i = 0; i < clr; i++) area[i] = 0;                         /* the relocation table is gone, the bss reads zero */
    H_FLUSH(img, h.image_size);
    api = (const struct hub_api *)(img + h.api_off);
    hub_env_tab.seed = H.seed;
    if (api->abi != HUB_ABI) { H_OS_FREE(H.blk); H.blk = 0; hub_fail(HB_E_ABI); return 0; }
    if (api->enter(&hub_env_tab, !H.inited) != 0) { H_OS_FREE(H.blk); H.blk = 0; hub_fail(HB_E_ENTER); return 0; }
    H.inited = 1;
    H.api = api;
#ifdef UPD_TITLE
    H.tchk = (h.api_off + (unsigned int)sizeof(struct hub_api) <= h.image_size && api->tchk_magic == HUB_TCHK_MAGIC) ? api->tchk : 0;   /* an older overlay: no title check */
    UT.gen++;
#endif
    H_TB(t1);
    MS_BC_LOADED(); HD_SET(0, 13);
    hub_st[HB_STATE] = 1; hub_st[HB_LOADS]++; hub_st[HB_ERR] = 0; hub_st[HB_BLOCK] = 32u + n + HUB_BSS_EXTRA; hub_st[HB_ADDR] = (unsigned int)img;
    hub_st[HB_TICKS] = t1 - t0; hub_st[HB_SCENE] = H_SCENE;
    if (H.pend_cfg) { H.pend_cfg = 0; api->cfg_apply(); }             /* the setup first: a kept report or cancel may need it */
    if (H.pend_bd) { H.pend_bd = 0; on_st.bd--; api->bd(0, H.bd); }             /* what arrived while the overlay was absent, in arrival-independent order: the report, then the cancel */
    if (H.pend_ba) { H.pend_ba = 0; api->ba_tail(); }
    return 1;
}

/* main menu (major 1) and Slippi's online character select (major 8, minor 0) */
static unsigned int hub_scene_key(unsigned int sc)
{
    unsigned int major = sc >> 24, minor = sc & 0xFFu;
    if (major == 1u || (major == 8u && minor == 0u)) return (major << 8) | minor | 0x10000u;
    return 0;
}

#ifdef UPD_TITLE
/* ---- update check on the title.  The resident half: the inputs of struct hub_tchk, the hub overlay on the
   title while a check is wanted, one api->tchk call per title / Main Menu frame, the proof words, and the decision at the Main Menu entry (ut_menu_frame, called by ml_glue.c).
   Card route only (HUB_OVL_RES): a target without the hub client has none of this, the menus behave as before. ---- */
extern int ons_title_updchk(unsigned int delay_default, unsigned int frame, int menu);     /* on_set.c: the start-check flag, no longer a setting (-1 = not read yet) */
extern volatile unsigned int s3_cfg[];                                                       /* word 3 = the engine's default delay (the settings layer's default, as ml_glue.c passes it) */
extern unsigned int upd_boot_seq(unsigned int *tried) __attribute__((weak));                /* upd_host.c: the running engine build from the loader's status block (0 = not known) */
extern void ms_toast_update(unsigned int release) __attribute__((weak));                    /* the console's toast, when an image has one (not in this build): a late answer */
#define DF_S_OVLSEQ 28u                                                                     /* df_glue.c S_OVLSEQ: the MultishineOvl build_seq read with the pack table */
extern void s3_config(struct sdshim *s);                                                     /* s12_gc.c: the shim's config hook (SDSHIM_CONFIG_HOOK) */
static unsigned int ut_frames OD_DATA;
extern int ons_title_final(void);                                                            /* on_set.c */
extern void ml_prog_early(void) __attribute__((weak));                                       /* ml_glue.c: the stored progressive-scan setting, applied once (picture mode only) */
extern void ml_music_early(void) __attribute__((weak));                                      /* ml_glue.c: the stored Music Off, applied once */
static unsigned int ut_prog_done OD_DATA;
#ifdef UPD_TITLE
volatile unsigned int *const hub_ut_word = &hub_st[HB_UT_STATE];     /* tt_glue.c (title text): the check word, "Checking for update..." while it says running (tt_core.h tt_check_running) */
#endif

static void ut_proof(void)
{
    hub_st[HB_UT_STATE] = (unsigned int)UT.state | ((unsigned int)UT.why << 8) | ((unsigned int)(UT.enabled & 3u) << 16) | ((unsigned int)UT.found << 20) | ((unsigned int)UT.prompted << 24)
                          | ((unsigned int)UT.late << 25) | ((unsigned int)UT.toasted << 26) | ((unsigned int)(UT.scene & 3u) << 28)
                          | (od_o.net_up ? 1u << 27 : 0u) | (H.tchk ? 1u << 23 : 0u) | (df_st && df_st[1] == 1u ? 1u << 22 : 0u);
    hub_st[HB_UT_LATEST] = UT.latest;
    hub_st[HB_UT_SEQ] = (UT.eng_seq << 16) | (UT.ovl_seq & 0xFFFFu);
    hub_st[HB_UT_COUNT] = (UT.checks & 0xFFu) | ((UT.fails & 0xFFu) << 8) | (hub_st[HB_UT_COUNT] & 0x10000u) | ((unsigned int)UT.gen << 24);
}

/* the inputs, once per engine pass; returns 1 when the hub overlay should be loaded on the title for the check */
static int ut_inputs(unsigned int major)
{
    int e;
    ut_frames++;
    if (major == 0u && UT.scene != 0u) ut_rearm(&UT, od_clock_ms());                 /* the title again (backed out of the menus): a check that found nothing is asked again (hub_ovl.h ut_rearm) */
    if (major == 1u && UT.scene == 0u) { UT.entry = 1; UT.wait = 0; fr_arm(&FR); }   /* START on the title: the Main Menu scene's first settled frame decides (ut_menu_frame), then the first-run offer */
    if (major > 1u) UT.entry = 0;
    UT.scene = (unsigned char)(major <= 1u ? major : 2u);
    if (major == 0x28u && !ut_prog_done && ml_prog_early) {                 /* the boot scene (logos, card scene): the stored progressive-scan setting is read now, so the picture mode is right before the title appears (Melee's own order: video first).  Melee busy with the card = later; same read rules as the title's */
        (void) ons_title_updchk(0u /* Auto, */, ut_frames, 2);   /* 2 = the boot scene: the first read comes ONS_BOOT_WAIT passes in, not 120 ( 7) */
        if (ons_title_final()) { ut_prog_done = 1; ml_prog_early(); if (ml_music_early) ml_music_early(); }
        return 0;
    }
    if (!ut_prog_done) ut_prog_done = 1;                                     /* the boot scene is over without a final read: no picture switch on the title or later; the menus' own pass handles GameCube and Dolphin */
    if (major > 1u) return 0;                                                /* a match, the character select, other modes: nothing runs */
    e = ons_title_updchk(0u /* Auto, */, ut_frames, major == 1u);
    if (ml_music_early && ons_title_final()) ml_music_early();               /* the record was read on the title (the boot scene gave no final read): the stored Music setting now */
    if (e >= 0) UT.enabled = (unsigned char)(e ? 1u : 0u);
    else if (UT.enabled != 1u && UT.enabled != 0u) UT.enabled = 2u;
    if (!UT.eng_seq && upd_boot_seq) { unsigned int tr = 0; UT.eng_seq = upd_boot_seq(&tr); UT.tried_seq = tr; }
    if (!UT.ovl_seq && df_st && df_st[1] == 1u) UT.ovl_seq = df_st[DF_S_OVLSEQ];
    if (major == 0u && UT.enabled == 1u && UT.state != UT_DONE && sdshim_g.magic != 0x53445348u) {     /* SDSHIM_MAGIC (sdshim.c) */
        /* the online backend (od_o.cfg) is normally made by the first EXI command of Slippi's code, in the Main Menu; until then the slice keeps the DHCP lease unused
           (s12_gc.c S3_NET_DHCP waits for od_o.cfg).  The check needs the address on the title: do here what that first command does (sdshim.c sdshim_transfer) */
        sdshim_init(&sdshim_g);
        s3_config(&sdshim_g);
        hub_st[HB_UT_COUNT] |= 1u << 16;
    }
    return major == 0u && UT.enabled == 1u && UT.state != UT_DONE && od_o.net_up && df_st && df_st[1] == 1u;   /* only once the BBA has an address: no card read for nothing */
}

/* FORCED UPDATE gate (on_backend.c ms_offline_server): 1 = the last signed answer of the title check asked for a build above the running engine; 0 = fine, unknown or no answer (fail open) */
int ut_gate_required(void) { return ut_req_needed(&UT); }

/* ml_glue.c, every Main Menu scene frame: kind = the menu screen (0 = the Main Menu's top level), settled = its rows are built, sign_state = the sign overlay (0 = closed).
   1 = open the sign overlay on the Update screen now (the offer), 0 = nothing (no screen unless an update IS available,: START during the check is a silent wait,
   hub_ovl.h ut_menu_step; 2 and 3, the old hold page, are no longer returned), sign_state 0xFF = an older ml overlay's B on its hold page.  Never more than once per boot; never while Melee writes its save. */
int ut_menu_frame(unsigned int kind, int settled, unsigned int sign_state)
{
    int r;
    if (sign_state == 0xFFu) { ut_hold_skip(&UT); ut_proof(); return 0; }   /* B during the hold (ml_glue.c page_leave) */
    r = ut_menu_step(&UT, kind, settled, sign_state, (int)hub_card_idle());
    if (r < 0 && ms_toast_update) ms_toast_update(UT.latest);              /* the answer came after the menu opened: a toast at most, never a screen */
    ut_proof();
    return r < 0 ? 0 : r;                                                   /* 1 offer, 2 start the hold page, 3 end it, 0 nothing */
}

/* ml_glue.c, every Main Menu scene frame AFTER ut_menu_frame: 1 = open the sign overlay on the first-run offer now (hub_ovl.h fr_step) */
int fr_menu_frame(unsigned int kind, int settled, unsigned int sign_state)
{
    unsigned int i, any = 0;
    int r;
    for (i = 0; i < 8u; i++) any |= od_cfg.dev[i];
    r = fr_step(&FR, kind, settled, sign_state != 0u, UT.entry != 0u, any != 0u, od_o.cfg_ok && od_o.net_up && od_o.srv_ip);
    hub_st[HB_FR] = (unsigned int)FR.armed | ((unsigned int)FR.done << 8) | ((unsigned int)FR.why << 16) | ((unsigned int)FR.offered << 24);
    return r;
}

/* ml_glue.c, before fr_menu_frame and the system files set-up: 1 = open the progressive scan question now (the first step of the first run, hub_ovl.h fr_prog_step; due: unset setting, cable, interlaced picture) */
int fr_prog_menu_frame(unsigned int kind, int settled, unsigned int sign_state, int due)
{
    return fr_prog_step(&FR, kind, settled, sign_state != 0u, UT.entry != 0u, due);
}
#endif

/* LAGLESS FoD (Slippi's "Recommended: Lagless FoD": no reflection, no particles; modules/CUBE_CODESET_MODULES_2026-10-23.md group 6) belongs to the online scene only (offline modes look like the stock disc).  The group is built
   with its retail words (payload.py fod_fill: patched ISO and card route leave the retail word at every site) and this table, written into the image by the same step, switches it: {count, count x (address, word when on, word when off)}.
   Major scene 8 is the online scene (Unranked, Direct, Game Night, spectate); the table is switched on its first pass, before the character screen's stage load, and back off on the first pass outside it. */
#define FOD_MAX 8u
unsigned int ms_fod_tab[1 + 3 * FOD_MAX] OD_DATA;
static unsigned int fod_on OD_DATA;                                              /* 0 at load = the retail words */
/* The verifying card loader (cl_loader.c CL_MUTE) silenced the Name Entry track it interrupted and handed the restore to this engine:
   its status words 32/33 = 'MUTE' and the live stream volume (lbl_804D387C, 0..127) it found.  The first pass outside the menu scene of the exploit (the title, exit mode 0)
   puts that value back (fn_80024654 applies it at once; the title's track starts at the retail start volume) and clears the mark.  A stored Music Off is applied later as
   before (ml_music_early).  No mark (an older loader, a lab loader): nothing is written.  Once per load. */
#ifdef MS_CARD_WA_ADDR
#define LM_MARK (MS_CARD_WA_ADDR == 0x817E4000u ? 0x817F7680u : MS_CARD_WA_ADDR + 0xC000u + 0x3680u)      /* CL_STATUS + 0x80 (mscf.h: stock 0x817F7600, generic/low list base + 0x3600) */
static unsigned int lm_done OD_DATA;
static void lm_tick(void)
{
    volatile unsigned int *m = (volatile unsigned int *)LM_MARK;
    if (lm_done || (H_SCENE >> 24) == 1u) return;
    lm_done = 1;
    if (m[0] != 0x4D555445u) return;
    m[0] = 0;
    *(volatile int *)0x804D387Cu = 0x7F;                              /* Melee's own value (lbAudioAx_80028690); Music Off no longer uses this word (ml_glue.c music_word: synth channel 1), so a 0 the loader found (a 1284..1297 Off) is not kept */
}
#else
#define lm_tick() ((void)0)
#endif
static void fod_tick(void)
{
    unsigned int want = (H_SCENE >> 24) == 8u, i, n = ms_fod_tab[0];
    if (want == fod_on || !n || n > FOD_MAX) return;
    for (i = 0; i < n; i++) {
        volatile unsigned int *a = (volatile unsigned int *)ms_fod_tab[1 + 3 * i];
        *a = ms_fod_tab[2 + 3 * i + (want ? 0u : 1u)];
        H_FLUSH((void *)a, 4);                                          /* the words are code: both caches */
    }
    fod_on = want;
}

void gns_res_tick(void);
int gns_watch_b4(void);                                                /* gns_res.c: "X: Last game" armed MNC_GN_WATCH: the lock-in watches instead of searching */
void gns_watch_ba(void);
static int gns_watch_sess(void);                                               /* gns_res.c (included after this file): the Game Night match-time block */
void hub_res_tick(void)
{
    MS_BCF1(0x20, 0x21);
    MS_BC_PASS();
    unsigned int key = hub_scene_key(H_SCENE);
    fod_tick();
    lm_tick();
    {   /* MUSIC RANDOM, "a match was played": an in-game pass of a local VS (major 2) or online (major 8)
           match (minor 2) marks the free word ml_st[45] (S_SET_APPLIED, ml_ovl.h; checked by a static assert in ml_glue.c); the menu overlay draws a new song at the next menu
           entry only when it sees the mark (ml_glue.c mlo_enter), then clears it */
        extern volatile unsigned int ml_st[] __attribute__((weak));
        unsigned int sc = H_SCENE;
        if (ml_st && ((sc >> 24) == 2u || (sc >> 24) == 8u) && (sc & 0xFFu) == 2u)
            ml_st[45] = 0x4D504C59u;               /* 'MPLY' */
    }
    gns_res_tick();                                                    /* loads / gives back ms/gns.ovl in AllM; a no-op outside a Game Night set of a spectatable room */
#ifdef UPD_TITLE
    if (ut_inputs(H_SCENE >> 24) && !key) key = 0x10000u | (H_SCENE & 0xFFu);     /* the title, while the check is wanted: load the overlay here too */
    if ((H_SCENE >> 24) <= 1u) ut_proof();
#endif
    if (!key) { if (H.blk) hub_free(); H.key = 0; return; }        /* match, stage select, offline modes: nothing of the hub is resident beyond the thunks */
#ifdef UPD_TITLE
    if (H.api && H.tchk && !H_LOADING && (H_SCENE >> 24) <= 1u) H.tchk(&UT, od_clock_ms());
#endif
    if (H.api) return;
    if (H_LOADING) { HD_INC(3); return; }
    if (key != H.key) { H.key = key; H.tries = 0; H.gave_up = 0; }
    if (H.gave_up && hub_gate_rearm(H.pend_bd | H.pend_ba | H.pend_cfg, hub_st[HB_ERR] == HB_E_BUSY || hub_st[HB_ERR] == HB_E_READ || hub_st[HB_ERR] == HB_E_NOMEM || (hub_st[HB_ERR] == HB_E_NOPACK && df_st && df_st[1] == 0u), &H.rearm)) {   /* a failed card read or a full HSD heap are transient too */
        H.gave_up = 0; H.tries = 0; hub_st[HB_REARMS]++;               /* a kept report or cancel is not given up while the card is only busy */
    }
    if (!H.gave_up) (void)hub_load();
    else HD_INC(2);
}

#ifdef MS_REP_OVL
void rep_scene_change(void);                                           /* rep_res.c (included after this file): the Slippi report overlay goes back first, before the HSD heap is rebuilt */
#endif
void hub_scene_change(void)
{
    MS_BCF(0x22, 0x23);
#ifdef MS_REP_OVL
    rep_scene_change();
#endif
    if (H.blk) hub_free();
    H.key = 0;
}

/* ---- thunks ---- */
#define hub_ready() (H.api != 0)

void hub_set_net(struct od *o)
{
    if (H.api) H.api->set_net(o);                                   /* else the overlay's next enter applies it */
}

void od_tick(struct od *o, unsigned int now_ms)
{
    MS_BCF1(0x24, 0x25);
    if (H.api) H.api->tick(o, now_ms);                              /* absent (a match, the stage select): no search runs, nothing is due */
}

static int hub_b9(void *ctx, sdshim_u8 *osb)
{
    MS_BCF(0x26, 0x27);
    const char *name = OD_FALLBACK_NAME, *code = OD_FALLBACK_CODE;
    unsigned int i;
    if (H.api) return H.api->b9(ctx, osb);
    od_st.b9++; hub_st[HB_ABSENT_B9]++;
    if (!od_o.cfg_ok && !H.pend_cfg && !onc_final()) {             /* Slippi's CSS asks B9 in its OnEnter, before the first engine pass can load the overlay: the card read */
        int why = onc_on_status(&od_cfg);                           /* (resident, on_card.c) runs here as before; od_apply_config follows at the overlay's next enter */
        od_o.cfg_why = why;
        if (why == OD_WHY_NONE) H.pend_cfg = 1;
    }
    omc_on_status();                                                /* the code history: first read from the card, retry of a pending save (resident) */
    if (od_o.cfg_ok || H.pend_cfg) { name = od_cfg.name; code = od_cfg.code; }
    for (i = 0; i < 42; i++) osb[i] = 0;
    osb[0] = 1;
    put_name(osb + 1, name, 31);
    put_code(osb + 32, code, 10);
    return 0;
}

static int hub_b4(void *ctx, sdshim_u8 mode, const sdshim_u8 *code)
{
    MS_BCF(0x28, 0x29);
    if (gns_watch_b4()) return 0;                                   /* a watch hand-off: the watcher takes the lock-in (the hub overlay is not asked) */
    if (H.api) return H.api->b4(ctx, mode, code);
    od_st.b4++; od_st.last_mode = mode; od_st.latch = 0; on_st.rk_idle = 0;
    od_o.phase = OD_ERR; od_o.err = OD_E_HUB;                       /* the next B3 shows "Online files missing on the memory card"; B cancels (BA) */
    od_st.phase = OD_ERR; od_st.err = OD_E_HUB;
    hub_st[HB_ABSENT_B4]++;
    return 0;
}

static int hub_be(void *ctx, const sdshim_u8 *req, sdshim_u8 *rsp)
{
    MS_BCF(0x2A, 0x2B);
    unsigned int i;
    if (H.api) return H.api->be(ctx, req, rsp);
    od_st.be++;
    for (i = 0; i < 30; i++) rsp[i] = 0;                            /* no suggestion */
    return 0;
}

static void hub_bd(void *ctx, const sdshim_u8 *rgb)
{
    MS_BCF(0x2C, 0x2D);
    unsigned int i;
    if (gns_watch_sess()) return;                                   /* a spectator reports nothing (the replayed game is not ours) */
    if (H.api) { H.api->bd(ctx, rgb); return; }
    for (i = 0; i < HUB_BD_LEN; i++) H.bd[i] = rgb[i];              /* the result report waits for the next load (the character select after the game) */
    H.pend_bd = 1; hub_st[HB_PEND_BD]++;
    on_st.bd++;                                                     /* "BD seen" counts at arrival (the scenarios wait for it in the match); the replay takes it back first */
}

static void hub_ba_tail(void)
{
    MS_BCF(0x2E, 0x2F);
    gns_watch_ba();                                                 /* B on the character screen ends a watch session */
    if (H.api) { H.api->ba_tail(); return; }
    if (od_o.dns_state == OD_DNS_BUSY && od_o.dns) sddns_abort(od_o.dns);
    od_o.dns_state = OD_DNS_NONE;
    od_o.phase = OD_IDLE; od_o.err = OD_E_NONE; od_o.started = 0; od_o.rt_why = 0;   /* od_cancel's state part now; its mnc part and the identity flush at the next load */
    od_st.phase = OD_IDLE; od_st.err = OD_E_NONE;
    H.pend_ba = 1; hub_st[HB_PEND_BA]++;
}
