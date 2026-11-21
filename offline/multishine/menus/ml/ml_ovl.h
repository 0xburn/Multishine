/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Menu layout code as an on-demand overlay ( step 3, "MENU OVERLAY").  Shared by the three ways ml_glue.c is built:
 *   flat    (default, no define)  everything resident, as before this change (and as the card route still ships it)
 *   ML_OVL_RES                    the RESIDENT part of the overlay variant: ml_glue.c keeps only the state the hooks and the next scene need, the settings plumbing,
 *                                 the SisLib text hook and the DVD calls; ml_res.c is the dispatcher / loader / scene hook
 *   ML_OVL                        the OVERLAY: ml_glue.c + ml_core.c + sg_hook.c + sg_ovl.c linked flat and packed by ml_ovl_pack.py into the disc file ms/menu.ovl
 * The file format and the loader are the sign-in overlay's (sg_ovl.h: struct sgo_hdr, sgo_begin / sgo_step / sgo_relocate); only hdr.state_hint differs
 * (MLO_KIND 1 = menu, 0 = sign-in) and the entry table behind hdr.api_off is `struct mlo_api` instead of `struct sgo_api`.  The overlay has no import
 * relocations: everything it needs from the resident image comes through `struct mlo_env`.
 * Pure declarations, C89, no libc.
 */
#ifndef ML_OVL_H
#define ML_OVL_H

#define MLO_ABI 2u                     /* struct mlo_env / mlo_api / mlo_carry / mlo_tabsave layout; an overlay of another ABI is refused and the retail menu runs */
#define MLO_KIND 1u                     /* sgo_hdr.state_hint of a menu overlay file */
#define MLO_PATH "/ms/menu.ovl"

/* ---- the RAM proof block ml_st[] (symbol ml_st, ml_status_addr): indices shared by the resident and the overlay side ---- */
enum { S_MAGIC, S_ENTER, S_FRAME, S_LABEL, S_LABEL_EMPTY, S_BUILD_REDIR, S_THINK, S_CONFIRM, S_BACK, S_TEXTS, S_ERR, S_LAST_KIND, S_LAST_SEL, S_LAST_ACT,
       S_INSTALLED, S_COVER, S_COVER_HOV, S_COVER_NULL, S_HOVER, S_MODE, S_RETURN, S_PAGE, S_PAGE_ROW, S_NA, S_PAGE_OPENS, S_LABEL_ART, S_TEX, S_TAB, S_BAR, S_COUCH_BACK, S_OPT, S_DEBUG, S_SLIPPI_BACK, S_TOURN, S_LANG, S_UNLOCK, S_UNLOCK_N,
       /* settings wiring (ML_SETTINGS; names) */
       S_SET_LOAD, S_SET_SAVE, S_SET_DELAY, S_SET_POOL, S_SET_SAVES, S_SET_SLOT, S_SET_FLAGS, S_SET_TRIES, S_SET_APPLIED, S_ENG_DELAY, S_SG_STATE, S_SG_ERR,
       S_SG_LOADS, S_SG_FREES, S_SG_BLOCK, S_SG_SCR, S_SG_REQ, S_PG_IDX, S_SET_CHANGES, S_PG_CUR, S_WS_ON, S_WS_W0, S_WS_N, S_WS_CHANGED };

/* ---- overlay counters: the words MLO_ST_BASE.. of ml_st[] in the overlay variant (the flat ml_st has 72 words in ML_SETTINGS builds, the overlay variant 72 + MO_N), so the existing
   RAM name lists and every runner that watches ml_st see them without a new symbol ---- */
#define MLO_ST_BASE 72u
/* settings words (ML_SETTINGS builds): words ML_CFG_BASE.. of ml_st[] (names cfg_*); the overlay counters follow them */
#define ML_CFG_BASE 64u
enum { CFG_PROG_PLAN,    /* low nibble: the last ms_prog_plan bits (MP_*); 0x100: the console flag is cleared as soon as the stored On is on the card */
       CFG_PROG_TRIAL,   /* progressive scan test from the Game page: frames left (0 = none); 0xFFFFFFFF = the test ended, put the picture back */
       CFG_PROG_STAT,    /* bit0 cable (VIGetDTVStatus), bit1 console flag, bit2 live mode, bits 4.. stored setting (0 off, 1 on, 2 follow) */
       CFG_RBR,          /* rollback readout flag as applied (needs the rbread=rt build; 0 in others) */
       CFG_PROG_LIVE,    /* times the picture mode was switched by the settings layer */
       CFG_SPARE5,
       CFG_REC,          /* Melee records seeding: bits 0..3 state (0 wait, 1 saving, 2 done, 3 gave up), 4..7 save requests so far, 8..15 Target Test slots written, 16..23 Home-Run slots written, bit 24 the save was seen busy */
       CFG_SPARE7 };
#define CFG_N 8u
enum { MO_MAGIC,        /* 'MLOV' */
       MO_STATE,        /* 0 none, 1 loaded and entered, 2 last load failed */
       MO_LOADS,        /* overlays loaded and entered */
       MO_FREES,        /* blocks freed (scene change or stale) */
       MO_FAILS,        /* loads that failed (any reason) */
       MO_ERR,          /* SGO_E_* (negated) or MO_E_* of the last failure, 0 = none */
       MO_BLOCK,        /* bytes of the HSD block while loaded (sgo_loader.peak_bytes) */
       MO_ADDR,         /* load address of the image while loaded */
       MO_UNPATCH,      /* retail table restored after a failure */
       MO_SCENES,       /* ml_scene_change calls (lbHeap_80015900) */
       MO_STALE,        /* a block of an earlier scene was still held at ml_enter */
       MO_LEAK_SKIP,    /* a block that is no longer on the HSD heap's allocated list was forgotten, not freed */
       MO_SPINS,        /* polls of the last blocking load */
       MO_TICKS,        /* time base ticks of the last load (begin to ready) */
       MO_N };
#define MO_E_ABI 100u   /* mlo_api.abi / hdr.state_hint / env size differ */
#define MO_E_ENTER 101u /* api->enter refused */
#define MO_E_TIMEOUT 102u
#define MO_E_NOHEAP 103u

/* state that must outlive the overlay (it is set in one menu scene and read in the next) */
struct mlo_carry {
    unsigned from_hub, ret_kind, ret_row, couch;     /* ml_glue.c g_from_hub, g_ret_kind, g_ret_row, g_couch */
    unsigned dbg_on, dbg_set, dbg_saved, dbg_seed;   /* the Debug menu's language bookkeeping */
    unsigned slip_back;                              /* a Slippi friend search was started from the Friends view: the next menu entry from its character screen lands on the Main Menu, Friends row */
    unsigned gn_back;                                /* card route: a Game Night set left for the character screen: the next menu entry from it returns to the hub's Game Night row (g_gn_back) */
};

/* the retail menu table words the overlay patches, as found (saved once, at the first install) so that a failed load later can put them back exactly */
struct mlo_tabsave {
    unsigned valid;
    unsigned ent[5][5];                              /* kinds 0, 4, 10, 11, 14: the 20-byte MenuKindData entry (anim, start, desc, count word, think) */
    unsigned th1p;                                   /* kind 1 think */
    unsigned desc[2];                                /* description pointer words of kinds 2 and 8 */
};
#define MLO_SAVE_KINDS { 0u, 4u, 10u, 11u, 14u }

struct ms_settings;
struct ons_status;
struct sgo_sdk;
struct sgu_host;

/* the CARD-ROUTE services of the resident image (SD_CARD_HOLE builds): everything the menu code reached by a direct symbol in the flat build (the Update row's host, the identity writer,
   the Game Night hand-off, the title's update check, the engine clock, the wait breadcrumbs).  The overlay has no import relocations, so it calls them through this table; the resident
   (ml_res.c) fills it with the same functions the flat build links.  A member is 0 when the image has no such function (weak symbols); the overlay treats 0 as the flat build treats an
   absent weak symbol. */
struct mlo_card {
    void *(*upd_net)(void);                          /* on_backend.c ml_upd_net: the hub client's mnc, the Update row's network */
    int (*upd_hello)(unsigned now_ms);
    int (*upd_poll)(unsigned now_ms);
    int (*upd_link)(void);
    unsigned char *(*wa_acquire)(unsigned owner);    /* on_hist.c: the shared CARD work area */
    void (*wa_release)(void);
    void *(*ident_cfg)(void);                        /* the Online page: the identity in force and its card writer */
    int (*ident_save)(const void *c);
    unsigned (*clock_ms)(void);                      /* od_clock_ms of the engine */
    void (*df_note)(unsigned kind, unsigned a, unsigned count);     /* df_glue.c: the lab's step word (0 if absent) */
    volatile unsigned *df_dbg;                       /* df_glue.c wait breadcrumbs, 4 words (0 if absent) */
    void (*net_dbg)(unsigned *w);                    /* ms_net_dbg of s12_gc.c (netcfg builds; 0 if absent) */
    int (*line_up)(void);                            /* br_line_up of the bridge (0 if absent) */
    unsigned (*nc_card_mm_ip)(void);                 /* sbr_nc_card_mm_ip (netcfg builds) */
    unsigned (*gn_left_ack)(void);                   /* on_backend.c: the Game Night hand-off */
    unsigned (*gn_state)(void);
    int (*ut_menu_frame)(unsigned kind, int settled, unsigned sign_state);    /* hub_res.c: the update check on the title and the first-run offer (UPD_TITLE) */
    int (*fr_menu_frame)(unsigned kind, int settled, unsigned sign_state);
    int (*ons_seed)(unsigned char *rec, unsigned frame, int write);          /* on_set.c, the stored entropy seed */
    int (*ons_change_srv)(unsigned v, unsigned frame);                       /* netcfg builds: Settings > Online > Server */
    int (*ons_change_mmr)(unsigned v, unsigned frame);
    int (*ons_change_mmk)(unsigned frame);
    unsigned (*gn_rk)(void);                         /* on_backend.c: 1 = the Game Night hand-off is a Grandmaster set (the character screen header says Grandmaster, not Game Night; appended last) */
    int (*df_reload)(void);                          /* df_glue.c: read the pack's table of contents again after the system files were written (0 if absent; appended last) */
    int (*fr_prog_menu_frame)(unsigned kind, int settled, unsigned sign_state, int due);   /* hub_res.c: the first-run progressive scan question, the first step of the first run (0 if absent; appended last) */
    int (*odl_save)(const unsigned *w);              /* on_dlog.c: the network report file MultishineDiag (0 saved, 1 busy, -1 failed; 0 pointer if absent; appended last) */
};

#ifndef TMV_PUB_DEFINED
#define TMV_PUB_DEFINED
struct tmv_pub { volatile unsigned *st; void *in; unsigned in_size; unsigned char *hs; };     /* hs: 0xBA8 bytes + a magic word, the Gecko codehandler saved before tn_restore overwrites it (ml_glue.c) */      /* menus/on/tmv.h has the same: the engine's request word block and data block (a struct tmb_pack) */
#endif
struct mlo_env {
    unsigned abi, size;                             /* MLO_ABI, sizeof(struct mlo_env) */
    volatile unsigned *st;                           /* ml_st */
    int *ok;                                         /* ml_g_ok: the table was found as retail and patched */
    struct mlo_carry *carry;
    struct mlo_tabsave *save;
    unsigned *main_anim, *hub_anim, *more_anim, *misc_anim, *opt_anim;   /* preview range tables the patched menu table points at */
    unsigned short *opt_desc;
    const unsigned short *blank_desc;
    unsigned th_think, th_think_opt, th_think_1p;    /* resident thunks: the addresses the menu table gets (they stay valid when the overlay moves) */
    const struct ms_settings *(*ons_cur)(void);
    int (*ons_change_delay)(unsigned v, unsigned frame);
    int (*ons_change_pool)(unsigned v, unsigned frame);
    int (*ons_change_ws)(unsigned v, unsigned frame);
    void (*ons_flush)(unsigned frame);
    const struct ons_status *ons;
    void (*apply_settings)(void);                    /* settings_apply: widescreen words and the engine's delay from the settings in RAM */
    int (*online_hook)(void);                        /* br_online_ready or 0 (ready at once) */
    const struct sgo_sdk *sdk;                       /* the DVD / heap calls (also used by the nested sign-in overlay) */
    const struct sgu_host *upd;                      /* for sgo_env.upd (the Update row's host services), 0 until the resident host exists */
    void *reserved_updchk;                                    /* was ons_change_updchk: the slot stays so the later fields keep their offsets */
    int (*ons_change_prog)(unsigned v, unsigned frame);        /* Settings > Game > Progressive scan */
    int (*ons_change_rbr)(unsigned v, unsigned frame);         /* Settings > System > Rollback readout (rbread=rt builds) */
    void (*test_toast)(int kind);                              /* ms_test_toast of the resident hub client or 0 */
    unsigned (*net_cost)(int i);                               /* ms_net_cost of the resident hub client or 0: Settings > Diagnostics, Net cost rows */
    int (*friend_direct)(const char *code);                    /* br_friend_direct of the resident bridge or 0: arm a Direct search of a Slippi friend (the Friends view) */
    int (*offline_hook)(int need_account);                              /* ms_offline_server of the resident hub client or 0: 1 = the row must not open (message set); need_account 1 = a linked console is needed too */
    int (*ons_change_preload)(unsigned v, unsigned frame);     /* Settings > Game > Menu Preload (appended last; the size check refuses an overlay of another layout) */
    int (*ons_change_spec)(unsigned v, unsigned frame);        /* Settings > Diagnostics > Spectate Upload (appended last, same rule) */
    int (*ons_change_bak)(unsigned v, unsigned frame);         /* Update > Backup to slot B (appended last, same rule) */
    int (*ons_change_keep)(unsigned v, unsigned frame);        /* Update > Keep the previous build as backup (appended last, same rule) */
    int (*ons_change_dbg)(unsigned v, unsigned frame);         /* Settings > Diagnostics > Debug Lines (appended last, same rule) */
    int (*ons_change_ping)(unsigned v, unsigned frame);        /* Settings > Game > Show ping (appended last, same rule) */
    int (*ons_change_music)(unsigned v, unsigned frame);       /* Settings > Game > Music: 1 on, 0 off, 2 random (appended after Show ping, same rule) */
    int (*ons_change_hud)(unsigned which, unsigned v, unsigned frame);   /* Settings > Game > Ping and delay position / Raise HUD (appended before the card services, same rule) */
    const struct mlo_card *card;                               /* the card-route services (ABI 2); 0 in a build without SD_CARD_HOLE */
    const struct tmv_pub *tmv;                                 /* the engine's retail-Tourney view (menus/on/tmv.h: request word + data block); 0 in a build without it (appended last, same rule) */
};

struct mlo_api {
    unsigned abi;
    int (*enter)(const struct mlo_env *env, void *enter_data);   /* 0 = ready (the table is patched / still patched); called at every entry of the menu scene */
    void (*frame)(void);
    void (*think)(void *gobj);
    void (*think_opt)(void *gobj);
    void (*think_1p)(void *gobj);
    int (*label_override)(unsigned kind, unsigned sel);
    void (*pre_build)(unsigned state);
    int (*unlock_override)(unsigned kind, unsigned sel);
    void (*sign_request)(void);
    void (*leave)(void);                             /* scene change: close the nested sign-in overlay; the caller frees the block */
    unsigned spare[2];
};

/* ---- resident side (ml_res.c): the names the hook stubs and the scene hook call ---- */
extern int ml_g_ok;
extern struct mlo_carry ml_carry;
extern struct mlo_tabsave ml_tabsave;
#define mlo_st (ml_st + MLO_ST_BASE)
extern volatile unsigned ml_st[];
void ml_scene_change(void);        /* lbHeap_80015900 (om_heap_policy): the menu scene is over */
#endif
