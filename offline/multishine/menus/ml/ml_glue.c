/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Menu layout runtime, Melee glue (steps 3 and 4).  Original work; Melee facts are cited from the
 * decompilation as file:line.
 *
 * What it does, in one paragraph: the table of Melee's menu kinds (mn_803EB6B0, 20 bytes per kind, :339) is patched at every entry of the
 * menu scene (ml_enter, hook 04 at mnMain_Scene_OnEnter): kind 0 (the Main Menu) gets our row count and our think function, kind 10 is the
 * Multishine Online screen (8 rows), kind 11 (Miscellaneous) becomes a copy of the retail main menu (same art, same descriptions) whose rows lead to
 * 1-P Mode, VS Mode, Trophies, Options and Data.  All three screens use ONE think function (ml_think) that follows the generated table (ml_table.h).
 * The labels of kinds 0 and 10 are drawn by us as HSD_Text over the empty plate of the retail art: hooks at the three label requests
 * (HSD_JObjReqAnim in mn_8022B3A0, mn_80229F60, mn_8022A440) ask ml_label_override and select the empty image (frame 86).
 * The five retail submenus go "back" to kind 0; a hook at mn_8022B3A0's entry turns that into kind 11, so Back from Options lands on Miscellaneous,
 * as in our tree.  Rows that open a stub text screen (Tournament, Account, Friends) draw it with SisLib text on the front canvas and hold the
 * retail menu off with the menu input cooldown, exactly as the options page does. No network is touched.
 *
 * Soft-float build: no FPU instruction in C; float arguments of the SisLib text calls go through ml_sis_add (ml_stub.S).
 *
 * THREE BUILDS of this file (ml_ovl.h,  step 3):
 *   flat (no define)   all of it resident: the build before the overlay existed, byte for byte
 *   -DML_OVL_RES       the resident remainder of the overlay variant: the regions marked [RES] below (ml_st, the tables the retail menu table points at, the
 *                      state that outlives a scene, the settings plumbing, the SisLib text hook, the DVD / heap calls); ml_res.c adds the dispatchers and the loader
 *   -DML_OVL           the overlay (ml_ovl_pack.py): the regions marked [OVL]; the resident state is reached through `E` (struct mlo_env)
 * Regions without a mark are shared declarations (macros, types).  A function appears in exactly one of the two builds of the overlay variant.
 */
#include "ml_core.h"
#include "ml_ovl.h"
#include "../ms_fmt.h"
#include "ml_mark_level.h"             /* the mark as an online indicator: two brightnesses of one artwork */
#include "ml_nt_geom.h"            /* Test connection: pixel rows of the notice and the result lines */
#include "ml_ui.h"                    /* the Settings look: row attributes and grid rows shared with the sign overlay */

#ifdef ML_OVL
#define ML_PART_RES 0
#define ML_PART_OVL 1
#elif defined(ML_OVL_RES)
#define ML_PART_RES 1
#define ML_PART_OVL 0
#else
#define ML_PART_RES 1
#define ML_PART_OVL 1
#endif

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef u32 __attribute__((may_alias)) u32a;    /* float tables are read as their bit patterns (soft-float build: no FPU instruction) */

/* ---- retail addresses (NTSC-U 1.02, vendor/melee/config/GALE01/symbols.txt) ---- */
#define MENU_FLOW 0x804A04F0u          /* MenuFlow: +0 cur_menu u8, +1 prev_menu, +2 hovered_selection u16, +0x11 entering_menu */
#define MENU_COOLDOWN 0x804D6BC8u      /* MenuInputState.cooldown u16 */
#define MENU_CANVAS_FRONT 0x804D6BB4u  /* SisLib canvas id of the front menu camera */
#define TAB 0x803EB6B0u                /* MenuKindData[0x22], 20 bytes: +0 anim_loop*, +4 start_frame f32, +8 descriptions*, +0xC count u8, +0x10 think */
#define PANEL 0x803EAE8Cu              /* AnimLoopSettings[0x22][3], 12 bytes each */
#define RETAIL_MAIN_ANIM 0x803EB3FCu   /* mn_803EB3FC: retail Main preview ranges, 5 x 12 bytes */
#define RETAIL_REG_ANIM 0x803EB57Cu    /* mn_803EB57C: Regular Match preview ranges (Slippi's online screen borrows them, OnMenuPrep.asm) */
#define RETAIL_MAIN_THINK 0x8022DB10u  /* mn_8022DB10 */
#define CTRL_ALL 0x80479CF0u           /* controller_map.x0[4] (all ports): +8 trigger u64, +0x20 repeat2 u64 */
#define R13_SWITCH_ONLINE (-0x49EC)    /* Slippi: r13 slot holding their "switch to online submenu" function (TitleMenu/OnMenuPrep.asm) */
#define R13_ONLINE_MODE (-0x5060)      /* Slippi: OFST_R13_ONLINE_MODE, the selected online mode (u8) */
#define R13_MENU_FLAG (-0x4AD8)        /* the u16 the retail menu thinks set to 5 when a screen is left for a scene (2554 `sth r0,-0x4AD8(r13)`) */
#define ONLINE_SCENE 8                 /* major scene of Slippi's online character screen (Event_StoreSceneNumber(8)) */

#define MI_UP 0x01u
#define MI_DOWN 0x02u
#define MI_CONFIRM 0x10u
#define MI_BACK 0x20u

#ifdef ML_SETTINGS
#include "on_card.h"                   /* ons_*: the settings record layer (on_set.c), same flat image */
#include "ml_set.h"                    /* ms_rec_seed (ml_set.c) */
#if ML_PART_OVL
#include "ml_music.h"                  /* Music = Random: the 38-song pool and the menu-slot draw */
#endif
/* Online delay Auto: ad_step, the engine's Auto word */
#if defined(__has_include) && __has_include("../../delay_auto/auto_delay_tab.h")
#include "../../delay_auto/auto_delay_tab.h"           /* staged build tree (offline/s17/net/...) */
#else
#include "../../../delay_auto/auto_delay_tab.h"        /* the source tree (offline/multishine/menus/...) */
#endif
extern volatile u32 s3_cfg[];          /* the engine's config block (s12_gc.c); word 3 (+0x0C) = input delay frames, read by the engine at every match setup */
#define ENGINE_DELAY (s3_cfg[3])
#define IC_INVAL ((void (*)(void*, u32)) 0x803448D4u)          /* ICInvalidateRange */
#ifdef ML_OVL
/* [OVL] the resident settings layer (on_set.c) reached through the env table; `E` is defined below */
#define ons_cur() (E->ons_cur())
#define ons_change_delay(v, f) (E->ons_change_delay((v), (f)))
#define ons_change_pool(v, f) (E->ons_change_pool((v), (f)))
#define ons_change_ws(v, f) (E->ons_change_ws((v), (f)))
#define ons_change_bak(v, f) (E->ons_change_bak((v), (f)))
#define ons_change_keep(v, f) (E->ons_change_keep((v), (f)))
#define ons_change_prog(v, f) (E->ons_change_prog((v), (f)))
#define ons_change_rbr(v, f) (E->ons_change_rbr((v), (f)))
#define ons_change_preload(v, f) (E->ons_change_preload((v), (f)))
#define ons_change_spec(v, f) (E->ons_change_spec((v), (f)))
#define ons_change_dbg(v, f) (E->ons_change_dbg((v), (f)))
#define ons_change_ping(v, f) (E->ons_change_ping((v), (f)))
#define ons_change_music(v, f) (E->ons_change_music((v), (f)))
#define ons_change_hud(w, v, f) (E->ons_change_hud((w), (v), (f)))
#define ons_flush(f) (E->ons_flush(f))
#define ons_st (*E->ons)
#endif
#endif
#ifdef ML_SIGN
#include "sg_hook.h"                   /* the sign-in overlay hook */
#endif
/* Game Night hand-off: card-route resident menu with the sign overlay only */
#if defined(SD_CARD_HOLE) && !defined(ML_OVL_RES) && defined(ML_SIGN) && defined(ML_SETTINGS)
#define ML_GN_HANDOFF 1
#ifndef ML_OVL
static u32 g_gn_back;                  /* a Game Night set left for the character screen: the next menu entry from it returns to the hub's Game Night row (the overlay variant: ml_carry.gn_back) */
#endif
static u32 g_gn_open;                  /* ... and the browser opens again (it resumes the room) once the hub is up (counts frames, opens after 90) */
static u32 g_gn_retry;                 /* reopen attempts left when the overlay file is not readable yet */
static u32 g_gn_scr;                   /* the page that opens instead of the Game Night browser, + 1 (0 = Game Night): Standings after R on the Grandmaster character screen */
static u32 g_st_back;                  /* Standings was opened from the Grandmaster character screen: its Back enters that screen again (sign_leave, go_mode 0) */
#ifndef ML_OVL
extern unsigned ml_gn_left_ack(void);  /* on_backend.c: the player left a watch on purpose */
extern unsigned ml_gn_state(void);     /* on_backend.c: the hand-off state (1 = the lobby armed a room search) */
extern unsigned ml_gn_rk(void);        /* on_backend.c: the hand-off is a Grandmaster set */
#endif
#else
#define ML_GN_HANDOFF 0
#endif

typedef u32 (*fn_buttons)(u32 slot);
typedef void (*fn_goto)(int kind, u32 hover, int state);   /* mn_80229894: cooldown, prev/cur/hover, build the screen, free this proc, add the kind's think */
typedef void (*fn_sfx)(int id);
typedef void* (*fn_exitdata)(void);
typedef void (*fn_void)(void);
typedef void (*fn_pending)(int mode);
typedef unsigned long long (*fn_trig)(int port);           /* gm_GetButtonsTriggered: r3 = high word, r4 = low word */
typedef void* (*fn_text_new)(int sis, int canvas);
typedef void (*fn_text_free)(void* t);

#define MN_BUTTONS ((fn_buttons) 0x80229624u)
#define MN_GOTO ((fn_goto) 0x80229894u)
#define MN_PENDING ((fn_pending) 0x80229860u)                   /* mn_80229860(pending_mode): exit the menu scene to that game mode (Event_StoreSceneNumber) */
#define GM_TRIGGERED ((fn_trig) 0x801A36A0u)
#define CSS_STORE_PORT ((fn_pending) 0x801677E8u)               /* gm_801677E8: the port that picks on the character screen (1P think, :2561) */
#define SFX ((fn_sfx) 0x80024030u)        /* 0 back, 1 forward, 2 move (mn/inlines.h:50-65) */
#define EXIT_DATA ((fn_exitdata) 0x801A4B9Cu)   /* gm_GetCurrentSceneExitData */
#define EXIT_GO ((fn_void) 0x801A4B60u)         /* gm_801A4B60 */
#define TEXT_NEW ((fn_text_new) 0x803A6754u)
#define TEXT_FREE ((fn_text_free) 0x803A5CC4u)

int ml_sis_add(void* text, const double* xy, const char* fmt, const char* s);   /* ml_stub.S: HSD_SisLib_803A6B98 with soft-float arguments */

#define SECT(x) __attribute__((section(x), used))
#define MLR (8 + ML_MOCKUPS)     /* rows we can draw: the eight rows of the flat Main Menu  + the development row of -DML_MOCKUPS=1 */
#define SETTLE 40                /* frames after a screen change before the labels are drawn (the plates slide in;) */
#define ML_NTEX_MAX 16            /* plate art bitmaps (generated, ml_tex.h: 13, the last one the development row Mockups of -DML_MOCKUPS=1); tex_install skips an index at or above this */
#define PG_ROWS 8                /* rows of a stub text screen */
#if defined(ML_SETTINGS) && defined(ML_SIGN)
#define ML_UI 1                  /* the Settings look (ml_ui.h): mark, title, breadcrumb, bar, right aligned values; its helpers are defined with the gfx table below */
#define PG_NR 12                 /* list rows of a Settings page (Diagnostics of a lab build has 9) */
#else
#define ML_UI 0
#define PG_NR PG_ROWS
#endif
#ifndef ML_OWNFONT
#define ML_OWNFONT 1             /* our own font on the Settings look pages (shipped): ml_font.h; 0 = Melee's menu text */
#endif
#if ML_UI
__attribute__((unused)) static void* g_mmark;                  /* the mark on the Melee menus (a gfx handle), made and freed by ml_frame */
#endif

/* RAM proof block, 32 words. The S_* indices are in ml_ovl.h. */
#ifdef ML_SETTINGS
#ifdef ML_OVL_RES
#define ML_ST_N (72 + MO_N)                /* 72 + the overlay loader's counters (ml_ovl.h MLO_ST_BASE) */
#else
#define ML_ST_N 72                         /* 64 + the settings words (ml_ovl.h ML_CFG_BASE) */
#endif
#else
#define ML_ST_N 40
#endif

#ifdef ML_OVL
/* [OVL] the resident state the overlay reads and writes: one pointer to the resident `struct mlo_env`, set by the first statement of mlo_enter */
static const struct mlo_env* E;
#define ML_TMV (E->tmv)             /* the engine's retail-Tourney view block; the flat build (no E: the hub-ovl host test's blob_flat) takes the symbol itself */
#define ST(i) (E->st[i])
#define g_ok (*E->ok)
#define ml_main_anim (E->main_anim)
#define ml_hub_anim (E->hub_anim)
#define ml_more_anim (E->more_anim)
#define ml_misc_anim (E->misc_anim)
#define ml_opt_anim (E->opt_anim)
#define ml_opt_desc (E->opt_desc)
#define ml_blank_desc (E->blank_desc)
#define g_from_hub (E->carry->from_hub)
#define g_ret_kind (E->carry->ret_kind)
#define g_ret_row (E->carry->ret_row)
#define g_couch (E->carry->couch)
#define g_slip_back (E->carry->slip_back)
#define g_dbg_on (E->carry->dbg_on)
#define g_dbg_set (E->carry->dbg_set)
#define g_dbg_saved (E->carry->dbg_saved)
#define g_dbg_seed (E->carry->dbg_seed)
#ifdef SD_CARD_HOLE
/* the card route's resident services (struct mlo_card, ml_ovl.h): the names the flat build links directly, here read from the env table (weak ones are 0 when the image lacks them) */
#define g_gn_back (E->carry->gn_back)
#define ml_upd_net (E->card->upd_net)
#define ml_upd_hello (E->card->upd_hello)
#define ml_upd_poll (E->card->upd_poll)
#define ml_upd_link (E->card->upd_link)
#define ms_card_wa_acquire (E->card->wa_acquire)
#define ms_card_wa_release (E->card->wa_release)
#define ml_ident_cfg (E->card->ident_cfg)
#define ml_ident_save (E->card->ident_save)
#define od_clock_ms (E->card->clock_ms)
#define df_note (E->card->df_note)
#define df_dbg (E->card->df_dbg)
#define ms_net_dbg (E->card->net_dbg)
#define br_line_up (E->card->line_up)
#define sbr_nc_card_mm_ip (E->card->nc_card_mm_ip)
#define ml_gn_left_ack (E->card->gn_left_ack)
#define ml_gn_state (E->card->gn_state)
#define ml_gn_rk (E->card->gn_rk)
#define df_reload (E->card->df_reload)
#define ut_menu_frame (E->card->ut_menu_frame)
#define fr_menu_frame (E->card->fr_menu_frame)
#define ons_seed (E->card->ons_seed)
#define ml_odl_save(w) (E->card && E->card->odl_save ? E->card->odl_save(w) : -2)
#define ons_change_srv (E->card->ons_change_srv)
#define ons_change_mmr (E->card->ons_change_mmr)
#define ons_change_mmk (E->card->ons_change_mmk)
#endif
#else
extern const struct tmv_pub tmv_pub __attribute__((weak));
#define ML_TMV (&tmv_pub ? &tmv_pub : (const struct tmv_pub*) 0)
/* [RES] */
volatile u32 ml_st[ML_ST_N] SECT(".data") = { 0x4D4C3031u /* "ML01" */ };
#define ST(i) (ml_st[i])
#ifdef ML_SETTINGS
/* Widescreen switch table (ml_set.h ms_ws_apply): {count, count x (address, word when on, word when off)}, written into the image by payload.py (ws_toggle);
   the patched DOL holds the off words, so with count 0 or no option set nothing here ever writes. */
u32 ml_ws_tab[1 + 3 * 16] SECT(".data");
#endif

/* preview ranges per row: row i shows retail Main range src[i] (0 1-P, 1 VS, 2 Trophies, 3 Options, 4 Data); 3 floats per row, filled at install */
u32 ml_main_anim[MLR * 3] SECT(".data");
u32 ml_hub_anim[MLR * 3] SECT(".data");
u32 ml_more_anim[MLR * 3] SECT(".data");
u32 ml_misc_anim[MLR * 3] SECT(".data");
#endif /* ML_OVL */
#if ML_PART_OVL
static const u8 ml_misc_src[MLR] = { 0, 1, 2, 3, 4, 0, 3, 4 };
#endif
#if ML_PART_RES
/* every description of our screens is the same blank string (SIS id 0x7A, 8 bytes: format codes and the end mark); no text under the panel */
#ifdef ML_OVL_RES
const u16 ml_blank_desc[10] = { 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A };
#else
static const u16 ml_blank_desc[10] = { 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A, 0x7A };
#endif
#endif

/* LAZY SIGNED CHECK (combined image): the Slippi row asks the bridge to run its one status query + signed-document check when the
   player ENTERS Slippi's screen, and the screen opens when it is done (the rows of that screen exist only if the check passed) or after ONLINE_WAIT_MAX frames
   (then the screen shows its log-in row).  Compiled with -Dml_online_hook=br_online_ready in the bridge image; every other image opens at once. */
#if ML_PART_OVL
#ifdef ML_OVL
#undef ml_online_hook                       /* the command line names the bridge's function: the overlay calls it through the env table */
#define ml_online_hook() (E->online_hook ? E->online_hook() : 1)          /* br_online_ready of the resident bridge, or ready at once */
#elif defined(ml_online_hook)
extern int ml_online_hook(void);
#else
#define ml_online_hook() 1                  /* every other image: ready at once */
#endif
#define ONLINE_WAIT_MAX 240u
#ifdef ML_OVL
#define ml_net_cost(i) (E->net_cost ? E->net_cost(i) : 0u)
#define ml_test_toast(k) do { if (E->test_toast) E->test_toast(k); } while (0)
#define ml_friend_direct(c) (E->friend_direct ? E->friend_direct(c) : -1)
#define ml_offline_hook(n) (E->offline_hook ? E->offline_hook(n) : 0)       /* OFFLINE GUARD: the rows that need our server (hub modes, Friends, Standings, Game Night) */
#else
extern unsigned ms_net_cost(int i) __attribute__((weak));
#define ml_net_cost(i) (ms_net_cost ? ms_net_cost(i) : 0u)
extern void ms_test_toast(int kind) __attribute__((weak));
#define ml_test_toast(k) do { if (ms_test_toast) ms_test_toast(k); } while (0)
extern int br_friend_direct(const char *code) __attribute__((weak));
#define ml_friend_direct(c) (br_friend_direct ? br_friend_direct(c) : -1)
extern int ms_offline_server(int need_account) __attribute__((weak));
#define ml_offline_hook(n) (ms_offline_server ? ms_offline_server(n) : 0)
#endif
static u32 g_online_wait;
#endif
#ifdef ML_SIGN
__attribute__((unused)) static u32 g_sign_state;               /* (defined with its comment further down: forward for ml_think) */
static u32 g_fr_go __attribute__((unused));              /* the Friends view armed a hand-off (g_slip_back == 3): counts frames on the settled Main Menu, then the character screen is entered from HERE (not from inside the Event overlay) */
static u32 g_fr_open __attribute__((unused));            /* back from a hand-off of the Friends view (g_slip_back == 2): the Friends list opens again once the Main Menu settled (counts frames, opens after 30) */
static u32 g_cov_wd __attribute__((unused));             /* COVER WATCHDOG: armed at a Friends return; see ml_frame */
#ifdef ML_LAB_NO_FR_REOPEN
#define ML_FR_REOPEN 0u                                    /* LAB ONLY (Dolphin z_kb_loop): the Friends list does not reopen after a hand-off, the path the watchdog covers */
#else
#define ML_FR_REOPEN 1u
#endif
#endif
#if !defined(ML_OVL) && !defined(ML_OVL_RES)
static u32 g_ret_kind, g_ret_row;  /* Tournament Melee started from the hub: Back lands on VS Mode in the stock flow, we go back to the hub row */
#ifdef ML_SIGN
static u32 g_slip_back;          /* a Slippi friend search was started from the Friends view: its character screen's menu entry lands on the Main Menu, Friends row (2: the Friends list reopens there too) */
#endif
static u32 g_couch;              /* the Couch row left the menu for game mode 2: the next menu entry on VS Mode returns to the Main Menu on that row */
static int g_ok;                 /* the table was found as retail and patched */
static u32 g_from_hub;           /* a hub row started online mode (g_from_hub - 1): the next menu entry returns to the hub on that row, see ml_enter */
#elif defined(ML_OVL_RES)
/* [RES] the same words, but global: ml_res.c hands their addresses to the overlay (struct mlo_env), and the next menu scene reads what this one left */
int ml_g_ok;
#define g_ok ml_g_ok
struct mlo_carry ml_carry;
#endif
#if ML_PART_OVL
static void* g_txt[MLR];
static void* g_bar;               /* the description line in the bar under the panel (our text, SIS font of the retail descriptions) */
static const char* g_bar_msg;      /* a refusal line for the bar (A on a row that cannot be entered: the player gets a reason); kept until the cursor moves */
static u32 g_bar_msg_hov;
static u32 g_bar_hov = 0xFFFFu, g_bar_seen, g_bar_sign, g_bar_arm;   /* what the bar was made for: hover, hook generation, sign state */
static u32 g_bar_gen_prev, g_bar_gen_cur;  /* the hook generation at the start of the last two ml_frame calls: drop_all arms on a hook that came after the frame before (the new screen's object is often hooked before ml_frame first sees its kind) */
static u32 main_lists(const struct ml_screen* m, u8* src);
#ifdef ML_SIGN
static u32 g_news;                /* the Friends panel's counts as of the last composition (news_now: unread | requests << 8 | invites << 16); the panel is composed again when it changes */
#endif
static u32 g_font_ok;             /* the retail menu font atlas is in RAM (checksum): every text of ours that wants the menu font is drawn only then */
static u32 g_list_ok;             /* bit j: the Main Menu list j is composed */
static u8 g_last[4];             /* cursor memory per menu (Main Menu, More, Original Menu; slot 3 spare), see go_kind */
static int mem_idx(u32 kind)
{
    return kind == 0u ? 0 : kind == (u32) ML_KIND_MORE ? 1 : kind == (u32) ML_KIND_MISC ? 2 : -1;
}
static u8 g_tex_ok[ML_NTEX_MAX];
static u32 g_tex_done, g_slippi_ok, g_lbl_tbl, g_lbl_cnt;      /* g_lbl_*: the label animation's image table, for tex_point */
static u32 g_tab_d[4];           /* title tab image descriptors: parent tab (joint 84, image 0: "1-P Mode"), child tab (joint 85, image 2: "Online Play") */
static u8* g_tab_save[4];        /* their retail contents, put back on every other screen */
static u32 g_tab_mode = 99;
static u16 g_pv_orig[3][10];                /* the first ten colour keys of each channel as the archive has them (pv_box) */
static u32 g_pv_red_ok, g_pv_hov = 0xFFFFFFFFu;
static u32 g_kind = 255u, g_settle, g_built = 0xFFFFu, g_cover_hover = 0xFFFFu, g_cover_due = 0xFFFFu;
#endif

#define DC_STORE ((void (*)(void*, u32)) 0x80344840u)      /* DCStoreRange */
#define W(a) (*(volatile u32*) (a))
#define H(a) (*(volatile u16*) (a))
#define B(a) (*(volatile u8*) (a))

#ifdef ML_SETTINGS
/* Progressive scan: Melee's own prompt is the only code that turns the mode on: on Yes it sets
   gmMainLib_8046B0F0.progressive (a 4 byte word at +8), calls gmMainLib_8015F500 (HSD_VISetConfigure with the Prog render mode) and OSSetProgressiveMode(1).  The settings layer
   does the same sequence from the menu scene.  The cable test is the prompt's own (VIGetDTVStatus). */
#define VI_DTV ((int (*)(void)) 0x803503A8u)                   /* VIGetDTVStatus */
#define OS_PROG_GET ((int (*)(void)) 0x80349278u)              /* OSGetProgressiveMode */
#define OS_PROG_SET ((void (*)(int)) 0x803492E8u)              /* OSSetProgressiveMode */
#define GM_VIDEO_CONF ((void (*)(void)) 0x8015F500u)           /* gmMainLib_8015F500: picks the render mode from the progressive word and the rumble/deflicker setting */
#define MELEE_PROG W(0x8046B0F8u)
#define CFG(i) ST(ML_CFG_BASE + (i))
#define PROG_TRIAL_FRAMES 600u                                 /* 10 s to press A on the test picture */
#define PROG_TRIAL_END 0xFFFFFFFFu
#define PROG_WII() (W(0x800000F8u) >= 200000000u)
#endif

#if ML_PART_OVL
static u32 r13_get(void)
{
    u32 v;
    __asm__ volatile("mr %0,13" : "=r"(v));
    return v;
}
#endif

/* ---- table patch ---- */
#if ML_PART_OVL
static void put_entry(u32 kind, u32 anim, u32 start_bits, u32 desc, u32 count, u32 think)
{
    u32 a = TAB + kind * 0x14u;
    W(a + 0x00u) = anim;
    W(a + 0x04u) = start_bits;
    W(a + 0x08u) = desc;
    W(a + 0x0Cu) = count << 24;        /* selection_count is the first byte of the word, three pad bytes zero */
    W(a + 0x10u) = think;
}

extern void ml_think(void* gobj);
extern void ml_think_opt(void* gobj);
extern void ml_think_1p(void* gobj);
#endif
#if ML_PART_RES && !defined(ML_OVL)
u32 ml_opt_anim[7 * 3] SECT(".data");
u16 ml_opt_desc[8] SECT(".data");
#endif
#ifdef ML_OVL
/* the think functions the retail menu table points at: resident thunks (ml_res.c) that forward into this overlay, wherever it was loaded */
#define TH_THINK (E->th_think)
#define TH_THINK_OPT (E->th_think_opt)
#define TH_THINK_1P (E->th_think_1p)
#else
#define TH_THINK ((u32) &ml_think)
#define TH_THINK_OPT ((u32) &ml_think_opt)
#define TH_THINK_1P ((u32) &ml_think_1p)
#endif
#define RETAIL_OPT_THINK 0x8022D104u   /* mn_8022D104: Options screen think */
#define RETAIL_1P_THINK 0x8022D7F4u    /* mn_8022D7F4: 1-P Mode think */
#define RETAIL_OPT_ANIM 0x803EB4F8u
#define RETAIL_OPT_DESC 0x803EB684u
#define KIND_OPT 4u
#define KIND_1P 1u

#if ML_PART_OVL
static void fill_anim(u32* dst, u32 base, const u8* src, u32 n)
{
    u32 i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 3; j++)
            dst[i * 3u + j] = W(base + (u32) src[i] * 12u + j * 4u);
}

/* Main Menu light colour: mn_8022C010 gives kind 0 the row number as the colour index and the colour switch of mn_8022BFBC
   has cases 0..4 only.  With a sixth Main Menu row (Mockups, row 5) the switch fell through: the out-of-line copy returned 5 as the GXColor pointer and the two
   copies inlined in fn_8022C128 (the light proc, every frame) and mn_8022C304 (light set-up at scene entry) left the pointer register unset (0): a DSI at
   0x8022C264 (lbz from address 0) the frame the cursor reached Mockups, from Miscellaneous downwards or from Multishine upwards.  The `index >= 5` branch of each
   copy is pointed at case 0 (rows 5.. get the colour of row 0).  Each site is written only when it holds the retail word. */
#ifndef IC_INVAL
#define IC_INVAL ((void (*)(void*, u32)) 0x803448D4u)          /* ICInvalidateRange */
#endif
#define LIGHT_CASE0 0x40800008u           /* bge +8: on to `addi rX,r13,-0x6B50` (colour 0) */
static void light_clamp(void)
{
    static const u32 site[3] = { 0x8022BFE0u, 0x8022C1D8u, 0x8022C400u };
    static const u32 retail[3] = { 0x4C800020u, 0x4080002Cu, 0x4080002Cu };   /* bgelr; bge to the end of the switch (twice) */
    u32 i;
    for (i = 0; i < 3u; i++)
        if (W(site[i]) == retail[i]) {
            W(site[i]) = LIGHT_CASE0;
            DC_STORE((void*) site[i], 4);
            IC_INVAL((void*) site[i], 4);
        }
}

/* Preview panel of our own screens (Main Menu, hub, More): EMPTY.  The retail ranges list the contents of Melee's own five rows, which are not what our rows are
.  MnMaAll's two list textures (MenMainConTop_Top_matanim_joint 22 and 40, key tables read host side) both show an 8 x 4 transparent
   image over frames 250..299, so every row gets the range {250, 299, 270} (start, end, loop point; floats as bit patterns).  The Miscellaneous screen keeps the retail
   ranges: its rows are Melee's own folders. */
static void fill_blank(u32* dst, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        dst[i * 3u] = 0x437A0000u;           /* 250.0f */
        dst[i * 3u + 1u] = 0x43958000u;      /* 299.0f */
        dst[i * 3u + 2u] = 0x43870000u;      /* 270.0f */
    }
}

static void copy_panel(u32 to, u32 from)
{
    u32 k;
    for (k = 0; k < 9; k++)
        W(PANEL + to * 36u + k * 4u) = W(PANEL + from * 36u + k * 4u);
}

static void page_forget(void);
#ifdef ML_SETTINGS
#ifdef ML_OVL
#define settings_apply() (E->apply_settings())      /* resident: the widescreen words and the engine's delay */
#else
static void settings_apply(void);
#endif
static void sign_reset(void);
#endif
#endif /* ML_PART_OVL */

/* Couch feedback 6 / 16 / 17: Melee's developer debug menu (Options > Debug, game mode 6) has a LANGUAGE item (un_803FA258.x0; un_80300218 writes it to
   the saved language) whose power-on value is 0 = GMLANGTYPEJP, whatever the player's language is; leaving the debug menu (its VERSUS MODE entry goes straight to a VS
   character screen) left the game Japanese, in the setting and in the saved preferences (which go to the card).: the user's language is respected, we never force one.
   So: when the Debug row is confirmed we remember the user's language setting and saved language and start the debug menu's item from the user's saved language; when the next
   Main Menu scene is entered we put both back exactly, unless the player changed the item inside the debug menu (then the explicit choice stays).
   : lbLang_SetLanguageSetting 0x8000AD98, lbLang_SetSavedLanguage 0x8000AE18; setting = *save (u8), saved language = save + 0x1CC6 (GamePrefs +0x16). */
#define LANG_SET ((void (*)(int)) 0x8000AD98u)
#define LANG_SET_SAVED ((void (*)(int)) 0x8000AE18u)
#define DBG_LANG_ITEM 0x803FA258u         /* un_803FA258.x0: value of the developer debug menu's LANGUAGE item (0 JP, 1 US) */
#define LANG_SETTING_PTR 0x804D3EE0u       /* gmMainLib_804D3EE0 -> language u8 at +0 */
#define LANG_SAVED_OFF 0x1CC6u
#if !defined(ML_OVL) && !defined(ML_OVL_RES)
static u32 g_dbg_on, g_dbg_set, g_dbg_saved, g_dbg_seed;
#endif

#if ML_PART_OVL
/* Online character select header names: the marker word in the head of the CSS text module's C2 body (codeset flag ms-names)
   tells it which Multishine mode name to print; 0 = Slippi's own flow.  The body is found through the hook word; nothing is written when it has no generated header. */
static void css_names_mark(u32 marker)
{
    u32 body = ml_css_body(ML_CSS_HOOK, W(ML_CSS_HOOK));
    if (body && W(body + 4u) == ML_CSS_BLRL && W(body + 12u) == ML_CSS_GRAN)
        B(body + 11u) = (u8) marker;
}
#endif

#if ML_PART_OVL
static u32 lang_save_ptr(void)
{
    u32 p = W(LANG_SETTING_PTR);
    return (p >= 0x80000000u && p < 0x81800000u) ? p : 0u;
}

static void lang_debug_enter(void)
{
    u32 p = lang_save_ptr();
    if (!p)
        return;
    g_dbg_set = B(p);
    g_dbg_saved = B(p + LANG_SAVED_OFF);
    g_dbg_seed = g_dbg_saved;
    W(DBG_LANG_ITEM) = g_dbg_seed;
    g_dbg_on = 1;
}

static void lang_debug_return(void)
{
    u32 p;
    if (!g_dbg_on)
        return;
    g_dbg_on = 0;
    p = lang_save_ptr();
    if (!p || W(DBG_LANG_ITEM) != g_dbg_seed)        /* the player picked a language inside the debug menu: keep it */
        return;
    if (B(p) != g_dbg_set || B(p + LANG_SAVED_OFF) != g_dbg_saved) {
        LANG_SET((int) g_dbg_set);
        LANG_SET_SAVED((int) g_dbg_saved);
        ST(S_LANG)++;
    }
}

/* Everything unlocked.  Melee's own debug unlock path
   (gmMainLib_8015FA34, the `db_804D6B20` branch) does: feature mask = 0xFF, all characters (gm_80164F18), all stages (gm_8016468C), all 66 unlock
   conditions met (gm_8017297C), all 300 notice flags (gm_801741FC); we run the same functions and add the trophies, which that path does not touch:
   every trophy flag word gets a copy count of at least 1 (the low byte is the copy count, 0x8000 is the 'new' mark, left alone) and the trophy count is
   the number of owned trophies.  The save struct is *0x804D3EE0, GmSaveData starts at +0x1868 (GamePrefs at +0x1CB0 = GmSaveData +0x448, proven by
   RulesAfterLoad.asm); trophy_count is +0x468 (s16), trophy_flags +0x46C (u16 x 293).  Runs at every Main Menu entry, which is after the boot card scene,
   so a loaded card is already in RAM; nothing here touches the card, Melee writes the (now complete) save at its next save, exactly as for an unlock
   earned by playing.  Idempotent. */
#define SAVE_PTR 0x804D3EE0u
#define GSD 0x1868u
#define TY_N 293u
static void ml_unlock_all(void)
{
    u32 save = W(SAVE_PTR), i, owned = 0;
    volatile u16* fl;
    if (save < 0x80000000u || save > 0x81800000u)
        return;
    B(save + GSD + 0x04u) = 0xFFu;                      /* x186C: unlocked features bitmask */
    ((fn_void) 0x80164F18u)();                          /* all characters */
    ((fn_void) 0x8016468Cu)();                          /* all stages */
    ((fn_void) 0x8017297Cu)();                          /* all 66 unlock conditions (records the time, once each) */
    ((fn_void) 0x801741FCu)();                          /* all 300 notice flags */
    fl = (volatile u16*) (save + GSD + 0x46Cu);
    for (i = 0; i < TY_N; i++) {
        if ((fl[i] & 0xFFu) == 0u)
            fl[i] = (u16) ((fl[i] & 0xFF00u) | 1u);
        owned++;
    }
    H(save + GSD + 0x468u) = (u16) owned;
    ST(S_UNLOCK)++;
    ST(S_UNLOCK_N) = owned;
}

#ifdef ML_OVL
/* the retail words the install overwrites, kept in the resident image: a later scene whose overlay cannot be loaded puts them back (ml_res.c mlo_unpatch) */
static void tab_save(void)
{
    static const u8 sk[5] = MLO_SAVE_KINDS;
    struct mlo_tabsave* t = E->save;
    u32 k, j;
    for (k = 0; k < 5; k++)
        for (j = 0; j < 5; j++)
            t->ent[k][j] = W(TAB + (u32) sk[k] * 0x14u + j * 4u);
    t->th1p = W(TAB + KIND_1P * 0x14u + 0x10u);
    t->desc[0] = W(TAB + 2u * 0x14u + 0x08u);
    t->desc[1] = W(TAB + 8u * 0x14u + 0x08u);
    t->valid = 1;
}
#endif

#if defined(ML_SETTINGS) && defined(ML_SIGN)
static void tn_r13_back(void);
#endif
static void mz_forget(void);
#ifdef ML_SETTINGS
static void music_pool_sync(void);
static void music_pool_keep(void) __attribute__((unused));
_Static_assert(S_SET_APPLIED == 45, "hub_res.c writes the match mark to ml_st[45]");
#endif
#ifdef ML_SIGN
/* ANNOUNCER ON THE ADD-FRIEND KEYBOARD.  The character screen's start plays its voice at 0x80264124
   (mnCharSel setup: `bl lbAudioAx_800237A8` with the mode's enter_sfx; Slippi's hook at 0x80264118 skips it only when coming back from name entry or when locked in).  The add-friend
   hand-off (go_slippi_friend, Direct mode 2) turns that one call into a nop just before it leaves; the next menu-scene entry (mlo_enter, every menu scene, i.e. before any real character
   screen of Dojo, Grandmaster, Mano a mano, Couch or a Game Night set can start) puts the retail word back.  Both writes are guarded by the exact word expected. */
#define ML_CSS_VOICE 0x80264124u
#define ML_CSS_VOICE_W 0x4BDBF685u        /* retail v1.02: bl 0x800237A8 */
static void css_voice_mute(int on)
{
    u32 have = on ? ML_CSS_VOICE_W : 0x60000000u;
    if (W(ML_CSS_VOICE) != have)
        return;
    W(ML_CSS_VOICE) = on ? 0x60000000u : ML_CSS_VOICE_W;
    ((void (*)(void*, u32)) 0x80344840u)((void*) ML_CSS_VOICE, 4);     /* DCStoreRange */
    ((void (*)(void*, u32)) 0x803448D4u)((void*) ML_CSS_VOICE, 4);     /* ICInvalidateRange */
}
#endif
#ifdef ML_OVL
static int mlo_enter(const struct mlo_env* env, void* enter_data)
#else
void ml_enter(void* enter_data)
#endif
{
    const struct ml_screen* m = ml_screen_for_kind(0);
    const struct ml_screen* mo = ml_screen_for_kind(ML_KIND_MORE);
    const struct ml_screen* x = ml_screen_for_kind(ML_KIND_MISC);
    u8* ed = (u8*) enter_data;
    u32 i;
#ifdef ML_OVL
    if (!env || env->abi != MLO_ABI || env->size != (u32) sizeof *env)
        return 1;
    E = env;
#else
    ST(S_ENTER)++;                          /* the overlay variant counts this in the resident ml_enter */
#endif
    css_names_mark(0);                      /* a menu scene: whatever comes next is Slippi's own flow until a hub row starts a mode */
#ifdef ML_SIGN
    css_voice_mute(0);                      /* the character screen's voice back (an add-friend hand-off silenced it for its own screen only) */
#endif
#if defined(ML_SETTINGS) && defined(ML_SIGN)
    tn_r13_back();                          /* back from the retail Tournament scene: the codeset's r13 slots before the first screen is built */
#endif
#if defined(SD_CARD_HOLE) && !defined(ML_OVL)
    {   /* card route: the disc-file pack's table of contents must be read before this scene reads its archive (a fast START at the title can beat the background poll); menus/df/df_glue.c */
        extern void df_arm(void) __attribute__((weak));
        if (df_arm) df_arm();
    }
#endif
    lang_debug_return();
    ml_unlock_all();
    for (i = 0; i < MLR; i++)               /* the previous menu scene's texts died with its text heap: forget them, never free */
        g_txt[i] = 0;
    g_bar = 0;
    g_bar_hov = 0xFFFFu;
    ST(S_DEBUG) = 0;                /* the description text object of the scene before died with its heap */
    g_bar_seen = g_bar_gen_prev = g_bar_gen_cur = ST(S_BAR);
    g_bar_arm = 0;
    g_list_ok = 0;
    g_kind = 255u;
    g_online_wait = 0;
    g_settle = 0;
    g_built = 0xFFFFu;
    for (i = 0; i < ML_NTEX_MAX; i++) {        /* a new menu scene: its heap and its archive are new */
        g_tex_ok[i] = 0;
    }
    g_tex_done = 0;
    mz_forget();                     /* VARIANT Z: the hint's texts died with the scene's heap */
#ifdef ML_SETTINGS
    {   /* MUSIC RANDOM, WHEN A SONG IS DRAWN:
           ONCE per power-on (the first menu-scene entry: bit 31 of the resident carried return row, kept by every writer), again only at the first menu entry AFTER a match
           was played (the engine marks ml_st[S_SET_APPLIED] 'MPLY' on an in-game pass, hub_res.c), and when Random is switched on (music_pool_sync from the frame, it starts the
           song at once).  Every other entry (menus, backing out of a mode, Friends / keyboard hand-offs, Game Night screens) keeps the slots, so every scene finds the playing name.
           The draw runs here, in the scene's OnEnter hook, before the scene's own music start.  On / Off: music_pool_sync keeps the canonical slots as before. */
        u32 draw = (u32) ml_music_entry_draws(!(g_ret_row & 0x80000000u), ST(S_SET_APPLIED));
        g_ret_row |= 0x80000000u;
        ST(S_SET_APPLIED) = 0;
        if (draw)
            music_pool_sync();
        else
            music_pool_keep();
    }
#endif
    g_pv_red_ok = 0;                 /* a new scene: the colour keys are the archive's again */
#if ML_UI
    g_mmark = 0;                               /* the scene's objects died with it (sign_reset clears the handle pool) */
#endif
    g_font_ok = 0;
    g_slippi_ok = 0;
    g_tab_d[0] = g_tab_d[1] = g_tab_d[2] = g_tab_d[3] = 0;
    g_tab_mode = 99;
    for (i = 0; i < 4; i++)
        g_last[i] = 0;              /* cursor memory: a new menu scene starts on the first rows */
    page_forget();
#ifdef ML_SETTINGS
    sign_reset();                           /* the scene changed: the overlay's block and the text objects died with the HSD heap */
#ifndef ML_OVL
    ons_menu_enter(MSET_DELAY_AUTO, ST(S_FRAME));   /* the default before the card is read: Auto; the overlay variant does this in the resident ml_enter, before the overlay is loaded */
    settings_apply();
#endif
#endif
    if (!g_ok && m && x) {           /* */
        /* install once, only on the retail table: kind 0 = Main with 5 rows, the retail think, the retail preview ranges; our kinds empty */
        if (W(TAB + 0x00u) != RETAIL_MAIN_ANIM || W(TAB + 0x10u) != RETAIL_MAIN_THINK || (W(TAB + 0x0Cu) >> 24) != 5u ||
            W(TAB + (u32) ML_KIND_MISC * 0x14u + 0x10u) != 0u ||
            W(TAB + (u32) ML_KIND_MORE * 0x14u + 0x10u) != 0u || m->nrows > MLR || (mo && mo->nrows > MLR) || x->nrows > MLR) {
            ST(S_ERR) |= 1u;
        } else {
            u32 desc = (u32) ml_blank_desc, start = W(TAB + 0x04u);
#ifdef ML_OVL
            tab_save();
#endif
            {
                u8 src[MLR];
                u32 i;
                (void) main_lists(m, src);
                for (i = 0; i < m->nrows; i++) {
                    if (src[i] == 255u)
                        fill_blank(&ml_main_anim[i * 3u], 1);
                    else
                        fill_anim(&ml_main_anim[i * 3u], RETAIL_MAIN_ANIM, &src[i], 1);     /* the retail range of list `src` (1-P Mode, VS. Mode, Trophies lists: images 0, 1, 2) */
                }
            }
            if (mo) {                    /* More's two rows list what is inside them on the right panel, as the Main Menu rows do */
                u8 src[MLR];
                u32 i;
                (void) main_lists(mo, src);
                for (i = 0; i < mo->nrows; i++) {
                    if (src[i] == 255u)
                        fill_blank(&ml_more_anim[i * 3u], 1);
                    else
                        fill_anim(&ml_more_anim[i * 3u], RETAIL_MAIN_ANIM, &src[i], 1);
                }
            }
            fill_anim(ml_misc_anim, RETAIL_MAIN_ANIM, ml_misc_src, x->nrows);
            copy_panel(ML_KIND_MISC, 0);     /* panel look of kind 11 = that of kind 0 (the code uses [2] for kinds other than 0, :896) */
            if (mo)
                copy_panel(ML_KIND_MORE, 0);     /* the Main Menu's panel look: its list images (the More rows' contents) */
            put_entry(ML_KIND_MISC, (u32) ml_misc_anim, start, W(TAB + 0x08u), x->nrows, TH_THINK);       /* the Original Menu keeps Melee's own five descriptions (the Main Menu's table, read before it is replaced below) */
            if (mo)
                put_entry(ML_KIND_MORE, (u32) ml_more_anim, start, desc, mo->nrows, TH_THINK);
            put_entry(0, (u32) ml_main_anim, start, desc, m->nrows, TH_THINK);
            light_clamp();                   /* six Main Menu rows: the retail light colour switch knows five */
            /* Options (kind 4) stays the retail screen with its retail think since the seventh row Debug is gone from the layout (menu finalization).  ml_table.h keeps the six retail rows of
               Options (only the unlock rule reads them: hidden slot 3), nothing of the Options entry is patched, and the only patch here is the 1-P think slot that lets Back from Slippi's
               screen land on the Main Menu.  The retail Options table is still verified first (the same words as before), so a foreign table never gets the slot patch. */
            if (W(TAB + KIND_OPT * 0x14u) == RETAIL_OPT_ANIM && W(TAB + KIND_OPT * 0x14u + 0x10u) == RETAIL_OPT_THINK && (W(TAB + KIND_OPT * 0x14u + 0xCu) >> 24) == 6u &&
                W(TAB + KIND_OPT * 0x14u + 0x8u) == RETAIL_OPT_DESC && W(TAB + KIND_1P * 0x14u + 0x10u) == RETAIL_1P_THINK) {
                W(TAB + KIND_1P * 0x14u + 0x10u) = TH_THINK_1P;     /* Back from Slippi's screen lands on the Main Menu through the 1-P think slot */
                ST(S_OPT) = 1;
            }
            g_ok = 1;
            ST(S_INSTALLED) = 1;
        }
    }
    if (g_ok) {                              /* no description under the panel on Main, VS, Slippi's screen, More and Miscellaneous (their prep rewrites kind 8 every scene) */
        static const u8 kinds[2] = { 0, ML_KIND_MORE };         /*: only OUR screens carry the blank string (our line is appended to Melee's text object); VS. Mode, Slippi's screen and the
                                                                                 Original Menu keep Melee's own descriptions */
        for (i = 0; i < 2; i++)
            W(TAB + (u32) kinds[i] * 0x14u + 0x08u) = (u32) ml_blank_desc;
    }
    /* coming back from the online character screen after a mode started from the hub or More: Slippi's handler chose their own screen (kind 8, and the row of
       the mode, or their first unlocked row when the mode is hidden there), we change the enter data before the scene reads it (MenuEnterData: +0 menu_kind,
       +1 hovered_selection) and take the mode from our own record */
#if ML_GN_HANDOFF
    g_gn_scr = 0;
    g_st_back = 0;
    if (g_gn_back || ml_gn_state() == 9u) {  /* back from a Game Night set's character screen: the hub, cursor on the Game Night row, then the browser resumes the room.  State 9 (MNC_GN_RKSET): a Grandmaster set was paired on the
                                                ranked character screen (hub_soc.c hs_rk_hold left it): the same way back, the browser then opens the set's prep screen (sg_entry.c gn_handoff) */
        g_gn_back = 0;
        if (ml_gn_left_ack()) goto gn_no_return;          /* a watch left with hold B: no return to the room; any other way out of the set (the quit combo lands on the Main Menu, not Slippi's screen) returns to the room too */
        if (g_ok && ed && m) {
            u32 r;
            for (r = 0; r < m->nrows; r++)
                if (m->rows[r].act == ML_A_SIGN && m->rows[r].arg == (u8) SG_GN) {
                    ed[0] = 0;
                    ed[1] = (u8) r;
                    g_gn_open = 1u;
                    g_gn_retry = 3u;
                    ST(S_RETURN)++;
                }
        }
    }
gn_no_return:
    if (ml_gn_state() == 10u && g_ok && ed && m) {   /* Y on the Grandmaster character screen (hub_soc.c hs_standings, MNC_GN_STAND): the hub on the Grandmaster row, then Standings opens; its Back leaves to that row.  The hub overlay clears the state */
        int r = ml_row_of_mode(m, 0);
        if (r >= 0) {
            ed[0] = 0;
            ed[1] = (u8) r;
            g_from_hub = 0;
            g_gn_open = 1u;
            g_gn_scr = (u32) SG_STAND + 1u;
            g_st_back = 1u;
            g_gn_retry = 3u;
            ST(S_RETURN)++;
        }
    }
#endif
#ifdef ML_SIGN
    if (g_slip_back) {                       /* back from a Slippi friend's character screen (Friends view): the screen of the Friends row, cursor on that row */
        u32 reopen = g_slip_back == 2u;
        g_slip_back = 0;                     /* a mark is used once: boot, a deliberate Main Menu exit or any later entry starts clean */
        if (g_ok && ed && ed[0] == 8u && m) {
            u32 r, k;
            for (k = 0; k < 2u; k++)
            for (r = 0; (k ? mo : m) && r < (k ? mo : m)->nrows && ed[0] == 8u; r++)
                if ((k ? mo : m)->rows[r].act == ML_A_SIGN && (k ? mo : m)->rows[r].arg == (u8) SG_FRD && !(k ? mo : m)->rows[r].hidden) {
                    ed[0] = k ? (u8) ML_KIND_MORE : 0u;
                    ed[1] = (u8) r;
                    ST(S_RETURN)++;
                    if (reopen) {
                        g_fr_open = ML_FR_REOPEN;
                        g_cov_wd = 1u;
                    }
                }
            if (ed[0] == 8u) {               /* VARIANT Z: no Friends row (a Z tap opens it): the screen it was opened from (Main Menu or More, bit 24), cursor where it was, and the Friends list again */
                u32 zr = (g_ret_row >> 16) & 0xFFu;
                const struct ml_screen* zs = (g_ret_row & 0x1000000u) && mo ? mo : m;
                ed[0] = zs == m ? 0u : (u8) ML_KIND_MORE;
                ed[1] = (u8) (zr && zr - 1u < zs->nrows ? zr - 1u : 0u);
                ST(S_RETURN)++;
                if (reopen) {
                    g_fr_open = ML_FR_REOPEN;
                    g_cov_wd = 1u;
                }
            }
        }
        g_ret_row &= 0x8000FFFFu;           /* (bits 16..24 used once) */
    }
#endif
#if ML_GN_HANDOFF
    if (!g_from_hub && ml_gn_state() == 4u)  /* MNC_GN_DIRECT: the character screen was reached by a Mano a mano hand-off (a toast accepted here, or our challenge accepted: hub_soc.c go_direct), not from a hub row; the hub overlay keeps the mark until this entry.  Back out lands on the hub's Mano a mano row, never on Slippi's submenu (tester without a Slippi login) */
        g_from_hub = 2u + 1u;
#endif
    if (g_from_hub) {
        u32 mode = g_from_hub - 1u;
        g_from_hub = 0;
        if (g_ok && ed && ed[0] == 8u) {
            int r = ml_row_of_mode(m, mode);
            if (r >= 0) {
                ed[0] = 0;
                ed[1] = (u8) r;
                ST(S_RETURN)++;
            } else if (mo && (r = ml_row_of_mode(mo, mode)) >= 0) {
                ed[0] = ML_KIND_MORE;
                ed[1] = (u8) r;
                ST(S_RETURN)++;
            }
        }
    }
    if (g_ok && ed && ed[0] == 2u && g_ret_kind) {
        ed[0] = (u8) (g_ret_kind - 1u);
        ed[1] = (u8) g_ret_row;
        ST(S_RETURN)++;
    }
    g_ret_kind = 0;
    /* back from the Couch character select: the stock path lands on VS Mode, we return to the screen of the Couch row on that row */
    if (g_ok && ed && ed[0] == 2u && m) {
        if (g_couch & 1u) {
            unsigned r, k;
            for (k = 0; k < 2u; k++) {
                const struct ml_screen* cs = k ? mo : m;
                for (r = 0; cs && r < cs->nrows && ed[0] == 2u; r++)
                    if (cs->rows[r].act == ML_A_SCENE) {
                        ed[0] = k ? (u8) ML_KIND_MORE : 0u;
                        ed[1] = (u8) r;
                        ST(S_COUCH_BACK)++;
                    }
            }
        }   /* else: VS. Mode > Melee is the retail row again: retail cursor, B from the character select returns to it */
    }
    g_couch = 2u;
#ifdef ML_OVL
    return 0;
#endif
}
#endif /* ML_PART_OVL (lang, unlock, enter) */

/* ---- hook bodies ---- */
#if ML_PART_OVL
/* does this row's plate carry art (ours in a placeholder image, or Slippi's own label image)? */
static int row_art(const struct ml_row* r)
{
    if (!r || !r->text || !r->art)
        return 0;
    return r->tex == 255 ? g_slippi_ok : g_tex_ok[r->tex];
}

/* the label stub asks for the frame to request for the plate of (kind, row): 0 keep the retail frame, else an index in the stub's frame table
   (1 = the empty plate, our text is drawn over it, 2.. = a frame whose image is the row's art) */
int ml_label_override(u32 kind, u32 sel)
{
    const struct ml_row* r;
#ifndef ML_OVL
    ST(S_LABEL)++;                          /* the overlay variant counts this in the resident dispatcher */
#endif
    if (!g_ok)
        return 0;
    r = ml_row_of(kind, sel);
    if (row_art(r)) {
        ST(S_LABEL_ART)++;
        return r->art;
    }
    return 0;           /* no art for the row: the retail plate art stays (never drawn text over it) */
}

/* ---- plate art: our own label bitmaps into the placeholder images of the label animation ---- */
#define TEX_PX_W 176u
#define TEX_PX_H 30u
#define TEX_BYTES (TEX_PX_W * 32u)             /* IA4: 1 byte per texel, 8 x 4 tiles, the height padded to 32 */
#define HSD_MEMALLOC ((void* (*) (u32)) 0x8037F1E4u)
#define GX_INV_TEX ((void (*)(void)) 0x8033F270u)          /* GXInvalidateTexAll */
#define MENCURSOR_MODEL 0x804A0538u                         /* MenMainCursor_Top (StaticModelDesc: joint, animjoint, matanim_joint, shapeanim_joint) */

static u32 mat_nth(u32 o, int* n)                           /* the n-th MatAnimJoint in depth first order (lb_80011E24 order) */
{
    while (o) {
        u32 c;
        if ((*n)-- == 0)
            return o;
        c = W(o);
        if (c) {
            u32 r = mat_nth(c, n);
            if (r)
                return r;
        }
        o = W(o + 4u);
    }
    return 0;
}

#define HSD_FREE ((void (*)(void*)) 0x8037F1B0u)           /* HSD_Free */
/* ---- title tab strips: the hub and Slippi's screen get their own names, and "Main Menu" as the parent tab ---- */
#define PANEL_MODEL 0x804A0518u                             /* MenMainPanel_Top */
#define TAB_PX_W 168u
#define TAB_PX_H 28u
#define TAB_BYTES (TAB_PX_W * 32u / 2u)                     /* I4: two texels per byte, 8 x 8 tiles, the height padded to 32 */

static u32 img_desc(u32 model, int joint, u32 idx, u32 w, u32 h)
{
    int n = joint;
    u32 m = mat_nth(W(model + 8u), &n), ma, ta, tbl, d;
    ma = m ? W(m + 8u) : 0;
    ta = ma ? W(ma + 8u) : 0;
    tbl = ta ? W(ta + 0xCu) : 0;
    if (!tbl || idx >= H(ta + 0x14u))
        return 0;
    d = W(tbl + 4u * idx);
    if (!d || H(d + 4u) != w || H(d + 6u) != h || W(d + 8u) != 0u)
        return 0;
    return d;
}

/* Title tabs (every name is OUR text, nothing of Slippi's or Nintendo's art): three places of the panel model hold a strip we replace at run time.
   joint 84 image 0 (parent tab, 168 x 28 I4, retail "1-P Mode"): "Main Menu" on More / the hub / Slippi's screen; joint 85 image 2 (child tab): on a retail archive an 8 x 4 transparent placeholder
   (Slippi's archive fills it with "Online Play"), so its descriptor is repointed to a buffer of ours (168 x 28) and put back afterwards; joint 86 (the static "Main Menu" title strip of the Main Menu
   screen, found through the joint descriptor tree, not a matanim table): "Original Menu" on kind 11, retail bytes put back on every other screen. */
#define TAB_PARENT 0
#define TAB_CHILD 1
#define TAB_TITLE 2
#define TAB_EVENT 3                  /* joint 85 image 1: the child tab "Event Match" of the Event-view screens (Standings, Friends), overwritten in place while the sign overlay shows one */
static u32 g_ph_ptr, g_ph_wh;        /* the child tab's retail placeholder: data pointer and (width << 16 | height) while we have repointed it */
static u8* g_ph_buf;
static u32 g_tab_ph;                 /* 1: the child tab image is the retail 8 x 4 placeholder, 0: a 168 x 28 strip we overwrite in place */

static u32 jobj_nth(u32 o, int* n)   /* the n-th joint descriptor in depth first order (child before next), the order of lb_80011E24 and of mat_nth */
{
    while (o) {
        u32 c;
        if ((*n)-- == 0)
            return o;
        c = W(o + 8u);
        if (c) {
            u32 r = jobj_nth(c, n);
            if (r)
                return r;
        }
        o = W(o + 0xCu);
    }
    return 0;
}

static u32 static_tab_desc(u32 joint)   /* image descriptor of the first texture of the joint's first material (the static title strips) */
{
    int n = (int) joint;
    u32 j = jobj_nth(W(PANEL_MODEL), &n), dobj, mobj, tobj, d;
    dobj = j ? W(j + 0x10u) : 0;
    mobj = dobj ? W(dobj + 8u) : 0;
    tobj = mobj ? W(mobj + 8u) : 0;
    d = tobj ? W(tobj + 0x4Cu) : 0;
    if (!d || !W(d) || H(d + 4u) != TAB_PX_W || H(d + 6u) != TAB_PX_H || W(d + 8u) != 0u)
        return 0;
    return d;
}


/* draw `text` (menu font, ml_font_label -> ml_font_to_i4) into the I4 strip dst (168 x 28 in 8 x 8 tiles), cleared first; 0 on success */
static u32 g_tab_k16 = 65536u;       /* the letters' scale of the next tab_draw; the Event-view parent tab draws larger (tab_event) */
static int tab_draw(u8* dst, const char* text)
{
    u8* plane = (u8*) HSD_MEMALLOC(TEX_PX_W * TEX_PX_H + 32u);
    u8* lab = (u8*) HSD_MEMALLOC(TEX_BYTES + 32u);
    u32 k;
    int r = -1;
    if (plane && lab && g_font_ok) {
        for (k = 0; k < TAB_BYTES; k++)
            dst[k] = 0;
        if (ml_font_label(lab, (const u8*) ML_FONT_ATLAS, text, plane) == 0)
            r = ml_font_to_i4(dst, TAB_PX_W, TAB_PX_H, -1, -1001, lab, g_tab_k16, 158u, 82, 91750u) > 0 ? 0 : -1;      /* the plate size (the retail strips' cap height is 21 rows), at most 150 texels wide (the slant adds 7), centred, cap top on row 1, italic 0.32 */
    }
    if (plane)
        HSD_FREE(plane);
    if (lab)
        HSD_FREE(lab);
    return r;
}

static void tab_put(u32 i, const char* text)     /* text 0: the retail strip again (also when the menu font is not there) */
{
    u32 d = g_tab_d[i];
    u8* dst;
    u32 k;
    if (!d)
        return;
    if (i == TAB_CHILD && g_tab_ph) {
        if (text && tab_draw(g_ph_buf, text) == 0) {
            DC_STORE(g_ph_buf, TAB_BYTES);
            W(d) = (u32) g_ph_buf;
            H(d + 4u) = (u16) TAB_PX_W;
            H(d + 6u) = (u16) TAB_PX_H;
        } else {
            W(d) = g_ph_ptr;
            H(d + 4u) = (u16) (g_ph_wh >> 16);
            H(d + 6u) = (u16) (g_ph_wh & 0xFFFFu);
        }
        return;
    }
    dst = (u8*) W(d);
    if (!text || tab_draw(dst, text) != 0) {
        for (k = 0; k < TAB_BYTES; k++)
            dst[k] = g_tab_save[i][k];
    }
    DC_STORE(dst, TAB_BYTES);
}

static void tab_apply(u32 kind)
{
    u32 mode = kind == 8u ? 1u : kind == (u32) ML_KIND_MORE ? 3u : kind == (u32) ML_KIND_MISC ? 4u : kind == 0u ? 5u : 0u;
    if (mode == g_tab_mode || !(g_tab_d[TAB_PARENT] || g_tab_d[TAB_CHILD] || g_tab_d[TAB_TITLE]))
        return;
    g_tab_mode = mode;
    tab_put(TAB_PARENT, mode == 1u ? ml_tab_text[ML_TAB_MORE] : (mode == 3u || mode == 5u) ? ml_tab_text[ML_TAB_MAIN] : 0);   /* Slippi's screen hangs under More since (header "More | Slippi", Back lands on More) */   /* 5 = the Main Menu: its parent tab also reads Main Menu (: the way back from Friends / the hub showed the retail "1-P Mode" strip until the menu settled) */
    tab_put(TAB_CHILD, mode == 1u ? ml_tab_text[ML_TAB_SLIPPI] : mode == 3u ? ml_tab_text[ML_TAB_MORE] : 0);
    tab_put(TAB_TITLE, mode == 4u ? ml_tab_text[ML_TAB_ORIG] : 0);
    GX_INV_TEX();
    ST(S_TAB) = mode + 1u;
}

#define ML_EV_TAB_K16 72400u
static u32 g_ev_hdr __attribute__((unused)), g_ev_cur __attribute__((unused));       /* the Event-view page the sign overlay shows (1 Standings, 2 Friends), the one whose header is set now */
static u32 g_ev_par __attribute__((unused));
#ifndef ML_EV_PARENT_MAIN
#define ML_EV_PARENT_MAIN 1        /* VARIANT Z: the Friends screen's parent tab reads Main Menu, from More too (the keyboard's tab says the same, gns_ovl.c GNS_KB_TAB) */
#endif
#define ML_ROOM_FRIENDS 0xFFFFu    /* VARIANT Z: od.room_note sentinel from the hub overlay (hub_soc.c HZ_OPEN): open Friends */
#define ML_ZROW_MARK 0x10000u      /* VARIANT Z: g_ret_row bits 16..23 = the Main Menu row + 1 a Friends hand-off left from (no Friends row to return to) */       /* the menu kind the Event-view page was opened from (mock_open): its parent tab reads More or Main Menu */

/* Event-view screens (the retail Event Match screen reused by the sign overlay): the header tabs read "Main Menu | Standings" (the Grandmaster standings, reached from the Main Menu) and "Main Menu | Friends";
   the retail tabs are put back when the overlay closes.  The Game Night browser has its own header art and is left alone. */
static void __attribute__((unused)) tab_event(u32 want, u32 kind)
{
    g_ev_cur = want;
    if (want && g_font_ok) {
        g_tab_k16 = ML_EV_TAB_K16;             /* the Event-view screens show this slot about 0.9 x the Main Menu's title strip: drawn 1.105 x so the letters match the Main Menu's */
        tab_put(TAB_PARENT, (g_ev_par == (u32) ML_KIND_MORE && !ML_EV_PARENT_MAIN) ? ml_tab_text[ML_TAB_MORE] : ml_tab_text[ML_TAB_MAIN]);      /* Friends opened from More: "More | Friends" */
        g_tab_k16 = 65536u;
        tab_put(TAB_EVENT, want == 1u ? ml_tab_text[ML_TAB_GRAND] : ml_tab_text[ML_TAB_FRIENDS]);
    } else {
        tab_put(TAB_EVENT, 0);
        g_tab_mode = 99;
        tab_apply(kind);
    }
    GX_INV_TEX();
}

static u8* tab_buf(void)
{
    u32 a = (u32) HSD_MEMALLOC(TAB_BYTES + 32u);
    return a ? (u8*) ((a + 31u) & ~31u) : 0;
}

static void tab_install(void)
{
    u32 i, k;
    g_tab_d[TAB_PARENT] = img_desc(PANEL_MODEL, 84, 0, TAB_PX_W, TAB_PX_H);
    g_tab_d[TAB_CHILD] = img_desc(PANEL_MODEL, 85, 2, TAB_PX_W, TAB_PX_H);
    g_tab_ph = 0;
    if (!g_tab_d[TAB_CHILD]) {            /* the retail archive: image 2 of the child tab is the transparent 8 x 4 placeholder */
        g_tab_d[TAB_CHILD] = img_desc(PANEL_MODEL, 85, 2, 8u, 4u);
        if (g_tab_d[TAB_CHILD]) {
            g_ph_buf = tab_buf();
            if (g_ph_buf) {
                g_tab_ph = 1;
                g_ph_ptr = W(g_tab_d[TAB_CHILD]);
                g_ph_wh = (u32) H(g_tab_d[TAB_CHILD] + 4u) << 16 | H(g_tab_d[TAB_CHILD] + 6u);
            } else {
                g_tab_d[TAB_CHILD] = 0;
            }
        }
    }
    g_tab_d[TAB_TITLE] = static_tab_desc(86u);
    g_tab_d[TAB_EVENT] = img_desc(PANEL_MODEL, 85, 1, TAB_PX_W, TAB_PX_H);
    for (i = 0; i < 4; i++) {
        g_tab_save[i] = 0;
        if (!g_tab_d[i] || (i == TAB_CHILD && g_tab_ph))
            continue;
        g_tab_save[i] = tab_buf();
        if (!g_tab_save[i]) {
            g_tab_d[i] = 0;
            continue;
        }
        for (k = 0; k < TAB_BYTES; k++)
            g_tab_save[i][k] = ((u8*) W(g_tab_d[i]))[k];
    }
    g_tab_mode = 0;
}

/* ---- the right-hand preview panel of the Main Menu  ----
   Melee shows what is inside the hovered row as an italic menu-font list (128 x 104 I4 images, MenMainConTop_Top_matanim_joint 22, images 0..4 = the lists of 1-P Mode, VS. Mode, Trophies,
   Options, Data).  Our rows that have sub-items (Multishine: the hub's rows, Slippi: Unranked / Direct, More: its rows) take the first lists in row order: their descriptors are pointed at
   buffers composed from the same menu font (ml_font_label -> ml_font_to_i4); the other rows keep the empty range.  Every other screen gets the retail pointers back (the Original Menu shows
   Melee's own lists again). */
#define CONTOP_MODEL 0x804A0528u                            /* MenMainConTop_Top */
/* The visible part of a list image (the right end of the longest lines was cut): the 128 texels fill a panel quad about 154 px wide, and the menu frame's vertical bar covers
   everything from texel ~111 on (measured on the Dolphin frames). A left aligned line starts at LIST_X0 and ends about LIST_X0 + width + 1, so a line is squeezed
   (ml_font_to_i4 maxw) to LIST_MAXW and its ink stays left of texel 106; a centred message is centred on LIST_CX, the middle of the visible part (not of the 128). */
#define LIST_X0 4u
#define LIST_MAXW 100u
#define LIST_CX 55
#define NLIST 3u                                            /* list buffers at once (retail has five list images, only the first NLIST are ever repointed).  First level: at most two distinct lists (a shared reason or the Friends news, and More's);
                                                               More: at most three (Mano a mano's reason, Settings, Original menu, the Slippi row is gone).  Rows with the same lines share one list image (main_lists), so six gated rows offline need one */
static u32 g_list_d[NLIST], g_list_orig[NLIST];

#ifdef ML_SIGN
static u32 g_news_now(void);
static char g_news_txt[5][16];                              /* the Friends panel's lines (ml_news_lines) */
static u32 is_friends_row(const struct ml_row* r)
{
    return r && r->text && !r->hidden && r->act == ML_A_SIGN && r->arg == (u8) SG_FRD;
}
#endif

static u32 g_panel_st;                                      /* ml_net_cost(107) as of the last composition: the panel is composed again when it changes */
/* FLAT MAIN MENU: the online modes are rows of the first level.  online_row = the rows that need our server (Dojo, Grandmaster, Mano a mano, Game Night, Tournament, Friends):
   offline / server silent they are refused (ml_think) and the right panel says 'Console is offline.'; hub_account_row = those of them that also need a linked console (everything but Dojo,
   which stays open to a guest): 'Link this console in Settings, Account.' */
static u32 online_row(const struct ml_row* r)
{
    return r && r->text && !r->hidden && (r->act == ML_A_MODE || (r->act == ML_A_SIGN && (r->arg == 18u || r->arg == 22u || r->arg == 25u)));
}
static u32 hub_account_row(const struct ml_row* r)
{
    return online_row(r) && !(r->act == ML_A_MODE && r->arg == 1u);
}
/* FREEZE GUARD: the rows that enter the online character screen (Dojo, Grandmaster, Mano a mano) assert without Slippi's CSS data on a card with no
   MultishinePack (a seeded card); refused until the system-files builder writes the pack.  Who may enter is the separate credentials rule (guest: Dojo only; hub_account_row). */
static void bar_refuse(u32 hov, u32 st)           /* the reason A did nothing, in the description bar (about 30 characters fit; st = ml_net_cost(107): 1 offline, 2 guest, 8 server silent) */
{
    g_bar_msg = (st & 1u) ? "Not online, check settings" : (st & 2u) ? "Link this console in Settings" : "Server not answering, wait";
    g_bar_msg_hov = hov;
    g_bar_hov = 0xFFFFu;                              /* the bar is made again at once */
    g_bar_arm = 1;
}
static u32 files_row(const struct ml_row* r)
{
    return r && r->text && !r->hidden && r->act == ML_A_MODE ? 1u : 0u;
}
static u32* panel_anim(u32 kind) { return kind == (u32) ML_KIND_MORE ? ml_more_anim : ml_main_anim; }
static u32 put_lines(const char** out, const char* a, const char* b, const char* c, const char* d)
{
    u32 n = 0;
    if (a) out[n++] = a;
    if (b) out[n++] = b;
    if (c) out[n++] = c;
    if (d) out[n++] = d;
    return n;
}
static u32 g_msg_panel;                                      /* the lines row_list just made are a message (centred in the panel), not a list of sub-items (left, like Melee's) */
#define MSGP(x) (g_msg_panel = 1u, (x))
static u32 row_list(const struct ml_row* r, const char** out)   /* the sub-items of a row, at most 6 (only the More > Settings panel has six) */
{
    static const char* const slippi_rows[2] = { "Unranked", "Direct" };      /* the rows of Slippi's own screen (Ranked, Teams and Party are hidden there) */
    u32 n = 0, i, st = g_panel_st;
    g_msg_panel = 0;
    if (!r || !r->text || r->hidden)
        return 0;
    /* REASONS IN THE PANEL (not a yellow status line): a row that needs the server or Slippi says why it cannot be entered now; the panel is composed
       again when the state changes (ml_frame), so it updates while the player is on the menu.  Lines of at most 11 characters (the panel's width). */
    if (online_row(r)) {                                        /* the first level's server rows: why the row stays closed (the panel, not a yellow line; the same words on every row) */
        if (st & 9u)                                            /* no network at all, or the server went silent (a cable pulled while running): one message */
            return MSGP(put_lines(out, "Console is", "offline.", 0, 0));
        if ((st & 2u) && hub_account_row(r))
            return MSGP(put_lines(out, "Link this", "console in", "Settings,", "Account."));
    }
    if (r->act == ML_A_ONLINE) {
        u32 why = (st >> 4) & 15u;
        if (why == 1u) return MSGP(put_lines(out, "Console is", "offline.", 0, 0));
        if (why == 2u) return MSGP(put_lines(out, "Multishine", "system", "files are", "missing."));
        if (why == 3u) return MSGP(put_lines(out, "The card's", "pack is", "damaged.", 0));
        if (why == 4u) return MSGP(put_lines(out, "Not logged", "in to", "Slippi.", 0));
        if (why == 5u) return MSGP(put_lines(out, "Slippi", "matchmaking", "is switched", "off for now."));
        if (why == 6u) return MSGP(put_lines(out, "Slippi", "needs a", "cable", "on a Wii."));      /* a Wii on Wi-Fi */
    }
#ifdef ML_SIGN
    if (is_friends_row(r)) {                                 /* the Friends row: what is new, only the non-zero counts; the online count is the menu tick's optional last byte (ms_net_cost 106) */
        u32 nw = g_news_now();
        n = ml_news_lines(ml_net_cost(106) & 0xFFu, nw & 0xFFu, (nw >> 8) & 0xFFu, (nw >> 16) & 0xFFu, g_news_txt);
        if (!n)                                              /* nothing new and nobody online: no list; the box shows the retail picture instead (main_previews) */
            return 0;
        for (i = 0; i < n; i++)
            out[i] = g_news_txt[i];
        return n;
    }
#endif
    if (r->act == ML_A_KIND) {
        const struct ml_screen* s = ml_screen_for_kind(r->arg);
        if (s && (s->text || r->arg == (u8) ML_KIND_MISC))      /* (the Original menu keeps Melee's own plates, text 0, but its row names are in the table: More's panel lists them) */
            for (i = 0; i < s->nrows && n < 5u; i++)
                if (!s->rows[i].hidden)
                    out[n++] = s->rows[i].label;
    } else if (r->act == ML_A_ONLINE) {
        out[0] = slippi_rows[0];
        out[1] = slippi_rows[1];
        n = 2;
    } else if (r->act == ML_A_PAGE && r->arg == 2u) {             /* More > Settings: its pages (all six rows) */
        n = put_lines(out, "Account", "Friends", "Game", "Update");
        out[n++] = "About";
        out[n++] = "Network status";                           /* six lines like the Settings page; the panel draws six smaller lines (tex_prepare) */
    }                                                             /* Couch (straight into VS. Melee, no sub-items): no list; the box shows the character screen picture */
    return n;
}

static u32 same_lines(const char** a, const char** b, u32 n)
{
    u32 i, k;
    for (i = 0; i < n; i++) {
        for (k = 0; a[i][k] && a[i][k] == b[i][k]; k++)
            ;
        if (a[i][k] != b[i][k])
            return 0;
    }
    return 1;
}
static u32 main_lists(const struct ml_screen* m, u8* src)      /* src[row] = list number or 255 (no list: the row shows its picture or nothing); returns the number of lists.
                                                                  Rows whose lines are the same share one list image: offline, all six server rows say 'Console is offline.' (one list) */
{
    const char *l[6], *q[6];
    u32 i, k, n, j = 0;
    for (i = 0; i < MLR; i++)
        src[i] = 255u;
    for (i = 0; m && i < m->nrows && i < MLR; i++) {
        n = row_list(&m->rows[i], l);
        if (!n)
            continue;
        for (k = 0; k < i && src[i] == 255u; k++)
            if (src[k] != 255u && row_list(&m->rows[k], q) == n && same_lines(l, q, n))
                src[i] = src[k];
        if (src[i] == 255u && j < NLIST)
            src[i] = (u8) j++;
    }
    return j;
}

/* Label art of one screen (heap-lean version): ONE pool of slots (HSD_MEMALLOC once per menu scene: plates 176 x 30 IA4 and preview lists 128 x 104 I4 both fit a slot of 6,656 bytes)
   and ONE composition per screen build: ml_pre_build calls tex_prepare(kind), which composes the labels of that screen's rows into the first slots (Melee's menu font, ml_font_label), points the
   retail 8 x 4 placeholder descriptors the rows were given by tools/menu_layout.py (donors per screen) at them, and, on the Main Menu, composes the preview lists into the last three slots and points
   the list images at them (every other screen gets the retail list pointers back).  Heap: 8 slots = 53,248 B (5 plates + 3 lists, Main Menu and More of); 11 slots = 73,216 B (8 plates + 3 lists, flat Main Menu) against 66,560 B (10 slots) at release 1284, 73 KB
   (13 plates) in build 481 and about 45 KB in 480; the temporary composer buffers (10.9 KB) are freed again at once. */
#define SLOT_BYTES 6656u
#define NPLATE (5u + ML_MOCKUPS)   /* five plates at most (Main Menu 5 rows, More 5 rows; was eight on the flat first level of) */
#define NSLOT (NPLATE + NLIST)
static u8* g_pool;

static u8* slot(u32 i)
{
    return g_pool + i * SLOT_BYTES;
}

#ifdef ML_SIGN
static u32 g_news_now(void)      /* the hub client's counts: unread messages (ms_net_cost 100, 0 offline / guest) | requests << 8 | invites << 16 (ms_net_cost 105) */
{
    u32 n = ml_net_cost(100), q = ml_net_cost(105);
    return (n > 255u ? 255u : n) | ((q & 0xFFu) << 8) | (((q >> 8) & 0xFFu) << 16);
}
#endif

/* The hub's preview box: the retail menu shows a picture of the next screen in the right-hand box per hovered row (the VS. Mode screen's Melee row: the character
   screen; its Tournament row: the bracket; the 1-P Mode screen's Event Match row: the event list).  Those pictures are animation ranges of the same MenMainConTop_Top model our lists use,
   so a hub row only needs the RANGE of the retail row that shows the picture it wants: read at run time from Melee's own range tables of the 1-P Mode and VS. Mode screens (nothing of the
   art is copied).  Dojo, Mano a mano and Grandmaster show the character screen, Game Night the bracket, Friends the event list (table + controller).  A row with a reason to show (the
   console needs a link) keeps its list range instead; the range is chosen again whenever the panel is composed again. */
#define RETAIL_1P_ANIM 0x803EB444u     /* mn_803EB444: 1-P Mode (kind 1) ranges, 5 x 12 bytes: Regular, Event, Stadium, Training */
#define RETAIL_VS_ANIM 0x803EB48Cu     /* mn_803EB48C: VS. Mode (kind 2) ranges, 5 x 12 bytes: Melee, Tournament, Special, Rules, Name Entry */
#define PV_START_VS0 0x442F0000u       /* 700.0f: start frame of the VS. Mode Melee range (the retail table is checked against it before it is used) */
#define PV_START_VS1 0x443B8000u       /* 750.0f: Tournament */
#define PV_START_1P1 0x43E10000u       /* 450.0f: Event Match */
static u32 hub_preview(const struct ml_row* r, u32* base, u8* idx, u32* start)
{
    if (!r || !r->text || r->hidden)
        return 0;
    if (r->act == ML_A_MODE) {                                  /* Dojo, Mano a mano, Grandmaster: the online character screen */
        *base = RETAIL_VS_ANIM; *idx = 0; *start = PV_START_VS0;
    } else if (r->act == ML_A_SIGN && (r->arg == 18u || r->arg == 25u)) {          /* Game Night (SG_GN) and Tournament (SG_TOURN): the bracket */
        *base = RETAIL_VS_ANIM; *idx = 1; *start = PV_START_VS1;
    } else if ((r->act == ML_A_SIGN && r->arg == 22u)) {   /* on the Main Menu the Friends row with nothing to show: the list with the controller */
        *base = RETAIL_1P_ANIM; *idx = 1; *start = PV_START_1P1;
    } else if (r->act == ML_A_SCENE && r->arg == 2u) {          /* Couch (Main Menu, straight into VS. Melee): the character screen like Dojo */
        *base = RETAIL_VS_ANIM; *idx = 0; *start = PV_START_VS0;
    } else
        return 0;
    return 1;
}

static void hub_previews(const struct ml_screen* sc, u32* anim)       /* anim = the Main Menu's range table: every row gets the range of its own preview (the lists are numbered by the state, so the table is made again whenever they are) */
{
    u8 src[MLR];
    u32 i;
    (void) main_lists(sc, src);
    for (i = 0; sc && i < sc->nrows && i < MLR; i++) {
        const struct ml_row* r = &sc->rows[i];
        const char* l[6];
        u32 base = 0, st = 0;
        u8 idx = 0;
        if (!hub_preview(r, &base, &idx, &st)) {                 /* More: its list (its image number follows the state) */
            if (src[i] != 255u && (g_list_ok & (1u << src[i])))
                fill_anim(&anim[i * 3u], RETAIL_MAIN_ANIM, &src[i], 1);
            else
                fill_blank(&anim[i * 3u], 1);
            continue;
        }
        if (row_list(r, l) == 0u) {                                /* no reason to show: the picture */
            if (W(base + (u32) idx * 12u) == st)
                fill_anim(&anim[i * 3u], base, &idx, 1);
            else
                fill_blank(&anim[i * 3u], 1);               /* not the retail table: the empty box as before */
        } else if (src[i] != 255u && (g_list_ok & (1u << src[i]))) /* the reason (list image `src`) */
            fill_anim(&anim[i * 3u], RETAIL_MAIN_ANIM, &src[i], 1);
        else
            fill_blank(&anim[i * 3u], 1);
    }
}

/* The box colour follows the animation frame, not the screen: joint 15 of MenMainConTop_Top (the box plane) has colour keys (three channels, 16-bit values, step keys) per range block:
   key 0..4 at frames 0, 50, 100, 150, 199 (the Main Menu block, one per row: blue, red, green, gold, purple, the frame colours of mn_8022C010's index 0..4), then key 5 at frame 400
   (1-P Mode: index 0), 6 at 700 (VS. Mode: 1), 7 at 1000 (Trophies: 2), 8 at 1300 (Options: 3), 9 at 1600 (Data: 4).  The FRAME colour is the light colour index of the screen and row: the Main Menu
   rows 0..4 their own number (further rows 0, light_clamp), the Multishine hub 0 (MENU_KIND_10 is not in Melee's switch but shows blue), More 1.  The box must be of the same colour family (owner
 More had a red frame and a teal box, Standings a blue-range picture in a red hub box): for the hovered row the key that owns the row's start frame takes the value of the retail key
   of that colour index (5 + index).  The archive's own values are kept (g_pv_orig) and put back first, so every other screen has the retail look, nothing needs saving elsewhere.  Streams are
   checked by their length (60 bytes), value format (16 bit) and type; a foreign archive is left alone.  The value of key j sits at byte 2 + 3 j (j < 5; the three-byte entries) or 18 + 4 (j - 5). */
static u32 pv_koff(u32 j) { return j < 5u ? 2u + 3u * j : 18u + 4u * (j - 5u); }
static u32 pv_frame(u32 bits)                /* a start frame as an integer (floats 0.0 .. 8e6) */
{
    int e = (int) ((bits >> 23) & 255u) - 127;
    u32 m = (bits & 0x7FFFFFu) | 0x800000u;
    if (e < 0) return 0;
    return e >= 23 ? m << (e - 23) : m >> (23 - e);
}
static void pv_box(u32 kind, u32 hover)
{
    int n = 15;
    u32 m = mat_nth(W(CONTOP_MODEL + 8u), &n), ma, ao, fo, k = 0, j;
    u32 d[3];
    g_pv_hov = 0xFFFFFFFFu;
    ma = m ? W(m + 8u) : 0;
    ao = ma ? W(ma + 4u) : 0;
    fo = ao ? W(ao + 8u) : 0;
    for (; fo && k < 3u; k++, fo = W(fo)) {
        d[k] = W(fo + 0x10u);
        if (!d[k] || W(fo + 4u) != 60u || B(fo + 0xDu) != 0x4Fu || B(fo + 0xCu) != (u8) (4u + k))
            return;
    }
    if (k != 3u)
        return;
    for (k = 0; k < 3u; k++) {
        if (!g_pv_red_ok)
            for (j = 0; j < 10u; j++)
                g_pv_orig[k][j] = H(d[k] + pv_koff(j));
        for (j = 0; j < 10u; j++)
            H(d[k] + pv_koff(j)) = g_pv_orig[k][j];
    }
    g_pv_red_ok = 1;
    if (kind == 0u || kind == (u32) ML_KIND_MORE) {
        u32 c = kind == 0u ? (hover < 5u ? hover : 0u) : kind == (u32) ML_KIND_MORE ? 1u : 0u;
        u32 f = pv_frame(panel_anim(kind)[hover * 3u]);
        j = f < 50u ? 0u : f < 100u ? 1u : f < 150u ? 2u : f < 199u ? 3u : f < 400u ? 4u : f < 700u ? 5u : f < 1000u ? 6u : f < 1300u ? 7u : f < 1600u ? 8u : 9u;
        if (hover < MLR)
            for (k = 0; k < 3u; k++)
                H(d[k] + pv_koff(j)) = g_pv_orig[k][5u + c];
    }
    for (k = 0; k < 3u; k++)
        DC_STORE((void*) d[k], 60);
    g_pv_hov = (kind << 16) | hover;
}

static void tex_prepare(u32 kind)
{
    const struct ml_screen* sc = ml_screen_for_kind(kind);
    u8 *tmp = 0, *plane = 0, *lab = 0;
    u32 i, j;
    if (!g_tex_done) {
        int n = 3, n2 = 22;
        u32 m, ma, ta, tbl, cnt, mj;
        m = mat_nth(W(MENCURSOR_MODEL + 8u), &n);
        ma = m ? W(m + 8u) : 0;
        ta = ma ? W(ma + 8u) : 0;
        tbl = ta ? W(ta + 0xCu) : 0;
        cnt = ta ? H(ta + 0x14u) : 0;
        if (!tbl || cnt < 53u || W(ta + 4u) != 0u) {
            ST(S_ERR) |= 4u;
            return;
        }
        g_tex_done = 1;
        g_lbl_tbl = tbl;
        g_lbl_cnt = cnt;
        g_slippi_ok = cnt >= 64u && H(W(tbl + 4u * 58u) + 4u) == TEX_PX_W;
        if (ml_font_sum((const u8*) ML_FONT_ATLAS) != ML_FONT_SUM) {      /* not the retail font atlas: the rows keep their retail plates */
            ST(S_ERR) |= 8u;
        } else {
            u32 a = (u32) HSD_MEMALLOC(NSLOT * SLOT_BYTES + 32u);
            if (a) {
                g_pool = (u8*) ((a + 31u) & ~31u);
                g_font_ok = 1;
            }
        }
        mj = mat_nth(W(CONTOP_MODEL + 8u), &n2);                         /* the list images of the Main Menu rows (MenMainConTop_Top, joint 22, images 0 .. 2) */
        ma = mj ? W(mj + 8u) : 0;
        ta = ma ? W(ma + 8u) : 0;
        tbl = ta ? W(ta + 0xCu) : 0;
        cnt = ta ? H(ta + 0x14u) : 0;
        for (j = 0; tbl && j < NLIST && j < cnt; j++) {
            u32 d = W(tbl + 4u * j);
            if (d && H(d + 4u) == 128u && H(d + 6u) == 104u && W(d + 8u) == 0u) {
                g_list_d[j] = d;
                g_list_orig[j] = W(d);
            }
        }
        tab_install();
    }
#ifdef ML_SIGN
    g_news = g_news_now();
#endif
    g_panel_st = ml_net_cost(107);
    for (i = 0; i < ML_NTEX_MAX; i++)
        g_tex_ok[i] = 0;
    for (j = 0; j < NLIST; j++)                                           /* retail list pointers everywhere but on the Main Menu */
        if (g_list_d[j])
            W(g_list_d[j]) = g_list_orig[j];
    g_list_ok = 0;
    if (g_font_ok && g_pool && sc && sc->text) {
        tmp = (u8*) HSD_MEMALLOC(TEX_PX_W * TEX_PX_H + TEX_BYTES + 64u);
        if (tmp) {
            plane = tmp;
            lab = (u8*) (((u32) tmp + TEX_PX_W * TEX_PX_H + 31u) & ~31u);
        }
    }
    if (plane) {
        for (i = 0; i < sc->nrows && i < NPLATE; i++) {
            const struct ml_row* r = &sc->rows[i];
            u32 d;
            if (!r->text || r->hidden || r->tex == 255 || r->tex >= ml_tex_n || r->tex >= ML_NTEX_MAX || r->didx >= g_lbl_cnt)
                continue;
            d = W(g_lbl_tbl + 4u * r->didx);
            if (!d || !((H(d + 4u) == 8u && H(d + 6u) == 4u) || (H(d + 4u) == TEX_PX_W && H(d + 6u) == TEX_PX_H)))
                continue;
            if (ml_font_label(slot(i), (const u8*) ML_FONT_ATLAS, ml_tex_text[r->tex], plane) != 0)
                continue;
            DC_STORE(slot(i), TEX_BYTES);
            W(d) = (u32) slot(i);
            H(d + 4u) = (u16) TEX_PX_W;
            H(d + 6u) = (u16) TEX_PX_H;
            g_tex_ok[r->tex] = 1;
            ST(S_TEX)++;
        }
        if (kind == 0u || kind == (u32) ML_KIND_MORE) {      /* the preview lists of the Main Menu: the reasons of the server rows, Friends' news, More's contents; More's own rows */
            u8 src[MLR];
            (void) main_lists(sc, src);
            for (i = 0; i < sc->nrows && i < MLR; i++) {
                const char* l[6];
                u32 k, nn, pitch, k16, first, msgp;
                u8* buf;
                if (src[i] == 255u || !g_list_d[src[i]] || (g_list_ok & (1u << src[i])))      /* (a list shared by several rows is composed once) */
                    continue;
                j = src[i];
                nn = row_list(&sc->rows[i], l);
                buf = slot(NPLATE + j);
                for (k = 0; k < 6656u; k++)
                    buf[k] = 0;
                pitch = nn <= 4u ? 24u : nn == 5u ? 20u : 16u;
                msgp = g_msg_panel;          /* */
                first = nn <= 4u ? 14u : nn == 5u ? 12u : 9u;
                k16 = nn <= 4u ? 55705u : nn == 5u ? 51118u : 40000u;      /* 0.85 / 0.78 of the plate size (five lines: pitch 20, the biggest that keeps the fifth line inside the 104 rows; was 0.72) */
                if (msgp)
                    first = 52u - (nn - 1u) * pitch / 2u;     /* a message: centred in the panel, up and down */
                ml_font_desc16 = 65536u;               /* full-length descenders in the small lists */
                for (k = 0; k < nn; k++)
                    if (ml_font_label(lab, (const u8*) ML_FONT_ATLAS, l[k], plane) == 0) {
                        int oh = (int) ((30u * k16) >> 16), x0 = (int) LIST_X0;
                        if (msgp) {              /* the ink width first (a dry run into an 8 x 8 scratch: every texel is clipped), then centred on the visible part */
                            u8 dry[64];
                            int w = ml_font_to_i4(dry, 8u, 8u, 0, 0, lab, k16, LIST_MAXW, 64, 65536u);
                            x0 = w > 0 ? LIST_CX - w / 2 : (int) LIST_X0;
                            if (x0 < 2) x0 = 2;
                        }
                        (void) ml_font_to_i4(buf, 128u, 104u, x0, (int) (first + k * pitch) - oh / 2, lab, k16, LIST_MAXW, 64, 65536u);
                    }
                ml_font_desc16 = 103389u;              /* MF_SL2: the plates and tabs keep the retail fit */
                DC_STORE(buf, 6656u);
                W(g_list_d[j]) = (u32) buf;
                g_list_ok |= 1u << j;
            }
        }
    }
    if (tmp)
        HSD_FREE(tmp);
    if (kind == 0u || kind == (u32) ML_KIND_MORE) {      /* a row whose list could not be made gets the empty range */
        u8 src[MLR];
        (void) main_lists(sc, src);
        for (i = 0; sc && i < sc->nrows && i < MLR; i++)
            if (src[i] != 255u && !(g_list_ok & (1u << src[i])))
                fill_blank(&panel_anim(kind)[i * 3u], 1);
    }
    if (kind == 0u || kind == (u32) ML_KIND_MORE)
        hub_previews(sc, panel_anim(kind));     /* the server rows: the retail pictures, or the reason; Friends with nothing to show, Couch: the retail pictures; More's own rows: the same; More's Settings / Original menu: their lists */
    pv_box(kind, 0u);                           /* the retail keys back; the hovered row's colour is set by ml_frame */
    g_pv_hov = 0xFFFFFFFFu;
    GX_INV_TEX();
}

/* HSD_SisLib_803A6368(text, id) fills a text with a SIS string (ml_text_stub, 04 hook at its entry): the description of our screens is the blank string, and the
   description text of the menu draws its own frame (the bar under the panel, a box of the text's size), so the blank text is moved off the screen: nothing is left at
   the bottom.  The retail code never moves the text again. */
#endif /* ML_PART_OVL */
#define BLANK_ID 0x7Au
#define F_OFFSCREEN 0x459C4000u        /* 5000.0 */
#if ML_PART_RES
void ml_text_hook(u32 text, u32 id)
{
    if (g_ok && text && id == BLANK_ID) {
        ST(S_DEBUG) = text;                  /* the retail description text object of our screen: the menu glue appends our line to it (ml_frame); it stays where Melee put it */
        ST(S_BAR)++;
    }
}
#endif
#if ML_PART_OVL

/* entry of mn_8022B3A0 (every screen build): the art goes in before the first plate is built; state 3 with cur_menu 0 and a parent kind 1 to 5 is the Back path
   of a retail submenu, which returns to Miscellaneous instead of the Main Menu */
void ml_pre_build(u32 state)
{
    if (!g_ok)
        return;
    if (state == 3u && B(MENU_FLOW) == 1u && B(MENU_FLOW + 1u) == 8u) {    /* Back from Slippi's screen (their handler sets 1-P Mode, row 2): More on the Slippi row */
        const struct ml_screen* mm = ml_screen_for_kind(ML_KIND_MORE);
        unsigned i;
        for (i = 0; mm && i < mm->nrows; i++)
            if (mm->rows[i].act == ML_A_ONLINE) {
                B(MENU_FLOW) = ML_KIND_MORE;
                H(MENU_FLOW + 2u) = (u16) i;
                ST(S_SLIPPI_BACK)++;
            }
    }
    if (state == 3u && B(MENU_FLOW) == 0u) {
        u32 prev = B(MENU_FLOW + 1u);
        if (prev >= 1u && prev <= 5u) {
            {
                B(MENU_FLOW) = ML_KIND_MISC;
                ST(S_BUILD_REDIR)++;
            }
        }
    }
    tex_prepare(B(MENU_FLOW));
    tab_apply(B(MENU_FLOW));
}

int ml_unlock_override(u32 kind, u32 sel)
{
    if (!g_ok)
        return 0;
    return ml_unlock(kind, sel);
}

/* ---- think ---- */
/* Cursor memory: each of our menus (Main Menu, the hub, More, the Original Menu) opens on the row it was last left on, within this menu scene (reset at every scene entry, so a
   fresh boot starts on the first row; the Couch / online hand-offs set their own return rows).  The row is recorded by the think, used when a screen is opened by a row (go_kind). */
#if defined(ML_SETTINGS) && defined(ML_SIGN)
static void tn_restore(void);
static void tn_unrestore(void);
#else
#define tn_restore() ((void) 0)
#endif
#if defined(ML_SIGN) && ML_PART_OVL
static int go_tmv(void);
static u8 g_tmv_go;                                  /* the sign overlay handed a bracket over (id 80): the scene is left once the overlay is gone (ml_think, go_tmv) */
#endif
static void go_kind(int kind, u32 hov)
{
    int mi = mem_idx((u32) kind);
    if (kind == (int) ML_KIND_MISC)
        tn_restore();                              /* the Original Menu: bring Melee's own Tournament back before the retail VS. Mode rows can be reached (see tn_restore) */
    if (mi >= 0) {
        const struct ml_screen* ts = ml_screen_for_kind((u32) kind);
        if (ts && g_last[mi] < ts->nrows && !ts->rows[g_last[mi]].hidden)
            hov = g_last[mi];
    }
    B(MENU_FLOW + 0x11u) = 1;                      /* entering_menu, as the retail think does */
    SFX(1);
    MN_GOTO(kind, hov, 1);
}

#ifdef ML_SIGN
static u32 g_sign_goto;                /* defined above in the sign block; declared here for open_update_required */
static u32 g_sign_req;                 /* (tentative: defined in the sign block below) */
static int sign_open(void);
/* FORCED UPDATE: an online row was refused because the server's minimum build is above ours (ms_offline_server returned 2): open the Update screen,
   which checks by itself and says 'Update required ... A Update now'.  Back returns to the Main Menu / hub; the row stays closed until the update is installed. */
static void open_update_required(void)
{
    SFX(1);
    g_sign_goto = SG_UPD + 1u;
    if (!sign_open())
        g_sign_goto = 0;
}
#else
#define open_update_required() SFX(3)
#endif

static void go_online(void)
{
    u32 f = *(volatile u32*) (r13_get() + (u32) R13_SWITCH_ONLINE);
    SFX(1);
    B(MENU_FLOW + 0x11u) = 1;
    if (f)
        ((fn_void) f)();
}

/* start an online mode the way Slippi's own think does (TitleMenu/OnMenuPrep.asm, FN_OnlineSubmenuThink A handler): the port that pressed picks on
   the character screen, the mode goes to their r13 byte, then the scene changes to the online major scene */
static u8 g_pad_port;                          /* the port that pressed a button last, + 1 (0 = none yet): ml_think notes it every frame */
static void pad_note(void)
{
    int i;
    for (i = 0; i < 4; i++)
        if ((u32) (GM_TRIGGERED(i) >> 32) || (u32) GM_TRIGGERED(i)) {
            g_pad_port = (u8) (i + 1);
            return;
        }
}

static void go_mode(u32 mode)
{
    int i, port = 0;
    if (ml_online_data_missing(ml_net_cost(107))) {            /* belt and braces: the online scene asserts without slpCSS.dat and the console freezes for good (the row gate in ml_think_main refuses first) */
        SFX(3);
        return;
    }
    ST(S_MODE) = mode + 1u;
    for (i = 0; i < 4; i++)
        if ((u32) (GM_TRIGGERED(i) >> 32) & 1u) {
            port = i;
            break;
        }
    B(MENU_FLOW + 0x11u) = 1;
    H(MENU_COOLDOWN) = 5;
    *(volatile u16*) (r13_get() + (u32) R13_MENU_FLAG) = 5;
    CSS_STORE_PORT(port);
    *(volatile u8*) (r13_get() + (u32) R13_ONLINE_MODE) = (u8) mode;
    css_names_mark(ml_css_marker(mode));           /* the online character screen of a hub row reads Grandmaster / Dojo / Mano a mano */
    g_from_hub = mode + 1u;
    SFX(1);
#if defined(ONS_NETSEL) && defined(SD_CARD_HOLE)
    {
#ifndef ML_OVL
        extern void df_note(u32 kind, u32 a, u32 count) __attribute__((weak));
#endif
        if (df_note)
            df_note(0xE0u, mode, 0u);                  /* lab: the last step before the scene change (a freeze names it: the on-screen step word, df_glue.c) */
    }
#endif
    MN_PENDING(ONLINE_SCENE);
}

#ifdef ML_SIGN
/* SLIPPI FRIEND (Friends view): the same leave as go_mode (Unranked character screen, no code keyboard); the armed code makes the lock-in's B4 a Direct search to Slippi (br_glue.c) */
static u8 g_fr_mode = 1u;                     /* the online mode of the Friends hand-off: 1 Unranked (the search is armed, nothing to type), 2 Direct (Z Add a friend: Slippi's code keyboard, env opt 71) */
/* MUSIC ACROSS A FRIENDS HAND-OFF.  Under Random the three menu slots of hps_files hold songs drawn at this
   Main Menu entry (music_pool_sync), so the online character screen's start, which asks for the menu index, plays a NEW song.  Before a Friends hand-off leaves the menu, the three
   menu slots are pointed at the song that is playing (found by its name in the track buffer 0x803BB300, "/audio/" + a slot's name): the character screen's start then finds the
   same name and does not restart (lbAudioAx_80023F28 compares), nor does the next Main Menu's own start; the next menu-overlay load draws again.  Random only: On / Off never touch it. */
#if defined(ML_SETTINGS) && defined(ML_MUSIC_TAB)
static void music_hold_current(void)
{
    const struct ms_settings* c = ons_cur();
    const volatile char* cur = (const volatile char*) 0x803BB300u;
    u32 i;
    if (!c || c->musicoff || !c->musicrand)
        return;
    for (i = 0; i < 101u; i++) {     /* the 98 slots, then the three canonical menu names (menu01 / menu02 / menu3 = the kraid slot + 12 / 24 / 36: a slot of the Random draw may no longer point at them) */
        u32 p = i < 98u ? W(ML_MUSIC_TAB + 4u * i) : W(ML_MUSIC_TAB + 4u * ML_MUSIC_ANCHOR) + (i - 97u) * ML_MUSIC_STEP, k;
        const char* pre = "/audio/";
        if (p < 0x80000000u || p >= 0x81800000u || (i >= 98u && W(ML_MUSIC_TAB + 4u * ML_MUSIC_ANCHOR_END) != W(ML_MUSIC_TAB + 4u * ML_MUSIC_ANCHOR) + 4u * ML_MUSIC_STEP))
            continue;                                          /* (the canonical names only when the table is the known layout, as ml_music_apply checks) */
        for (k = 0; pre[k] && cur[k] == pre[k]; k++)
            ;
        if (pre[k])
            return;                                            /* the buffer is not "/audio/...": leave everything */
        {
            u32 j = 0;
            while (j < 40u && cur[7u + j] && cur[7u + j] == ((const volatile char*) p)[j])
                j++;
            if (j < 40u && cur[7u + j] == 0 && ((const volatile char*) p)[j] == 0) {
                W(ML_MUSIC_TAB + 4u * 52u) = p;
                W(ML_MUSIC_TAB + 4u * 53u) = p;
                W(ML_MUSIC_TAB + 4u * 54u) = p;
                /* (the held index: i, 98..100 = the canonical menu01 / menu02 / menu3) */
                return;
            }
        }
    }
}
#else
static void music_hold_current(void) { }
#endif
static void go_slippi_friend(void)
{
    int i, port = 0;
    if (ml_online_data_missing(ml_net_cost(107))) {            /* no slpCSS.dat: the online scene would assert (the Mano a mano invite path has no br_friend_direct check); drop the hand-off */
        g_slip_back = 0;
        g_fr_mode = 1u;
        SFX(3);
        g_fr_open = 1u;                                        /* (a hand-off that blanked the screen first: the Friends list opens again, the cover lifts there) */
        return;
    }
    for (i = 0; i < 4; i++)
        if ((u32) (GM_TRIGGERED(i) >> 32) & 1u) {
            port = i;
            break;
        }
    if (i == 4 && g_pad_port)
        port = g_pad_port - 1;                                 /* this runs a dozen frames after the Event screen closed: nobody triggers A now; the pad that pressed last */
    B(MENU_FLOW + 0x11u) = 1;
    H(MENU_COOLDOWN) = 5;
    *(volatile u16*) (r13_get() + (u32) R13_MENU_FLAG) = 5;
    CSS_STORE_PORT(port);
    *(volatile u8*) (r13_get() + (u32) R13_ONLINE_MODE) = g_fr_mode;
#if ML_PART_OVL
    css_names_mark(g_fr_mode == 1u ? 3u : 0u);   /* (DNO): the armed Direct search (a Mano a mano invite, a Slippi friend) reads 'Mano a mano Mode', not Slippi's 'Unranked Mode' (R13_ONLINE_MODE stays 1: the armed code makes the lock-in's B4 a Direct search); the Direct-with-keyboard hand-off (2) keeps Slippi's own header */
#endif
    if (g_fr_mode == 2u)
        css_voice_mute(1);                    /* the add-friend keyboard: no "Choose your character" on its (covered) character screen */
    g_fr_mode = 1u;
    g_ret_row = (g_ret_row & 0x8000FFFFu) | (((u32) H(MENU_FLOW + 2u) + 1u) << 16) | (B(MENU_FLOW) == (u8) ML_KIND_MORE ? 0x1000000u : 0u);   /* VARIANT Z: the row (and bit 24: More) to come back to (the low half stays the Tourney return's) */
    music_hold_current();
    g_slip_back = 2;                          /* Friends return mark (the last hand-off that set a mark wins: the Game Night mark is dropped) */
#if ML_GN_HANDOFF
    g_gn_back = 0;
#endif
    SFX(1);
    MN_PENDING(ONLINE_SCENE);
}
#define SYSF_NEED 0x80000u                                   /* 512 KiB: the lean pack builder (offline/seed/seed_lean.c) peaks at about 0.47 MB and its largest block is 0.25 MB; Dolphin a 1 MiB probe fails at the Main Menu, 256 KiB passes and the set-up completes */
static u32 g_sysf_try __attribute__((unused));                                  /* the automatic system-files set-up was opened in this session */
static char g_fd_buf[10];
/* ids 60..68 give the 9 characters of a Slippi friend's code, id 69 arms the search and leaves: 0 done, -10 offline, -11 logged out, -12 data files missing, -13 bad code (plain ids of the
   overlay's env_opt: no change of the overlay's table layout) */
__attribute__((unused)) static int slippi_friend_opt(int id, int v) /* only env_opt (ML_PART_OVL) calls it */
{
    if (id == 73) {                               /* the system files (MultishinePack) were just written by the Update machine: read the pack's table of contents now, no restart (df_glue.c df_reload).  Answers like id 72 */
#ifndef ML_OVL
        extern int df_reload(void) __attribute__((weak));
#endif
#if !defined(ML_OVL) || defined(SD_CARD_HOLE)       /* the card services (df_reload) exist in the resident menu and in the card-route overlay only */
        if (df_reload) (void) df_reload();
#endif
        return ml_online_data_missing(ml_net_cost(107)) ? -12 : 0;
    }
    if (id == 72)                                 /* a question only (no return mark armed): can this console enter an online match?  The Friends Invites ask it before accepting a Mano a mano invite */
        return ml_online_data_missing(ml_net_cost(107)) ? -12 : 0;
    if ((id == 70 || id == 71) && ml_online_data_missing(ml_net_cost(107)))
        return -12;                               /* no Multishine system files (slpCSS.dat) / damaged pack: the online scene would assert; the Friends overlay refuses BEFORE it blanks the screen (Dolphin: Z on Friends > black screen) */
    if (id == 70 || id == 71) {                   /* the Friends view hands off to a character screen (Mano a mano invite; 71: Direct with the code keyboard): come back to the Friends list; the leave itself waits for the Main Menu (g_fr_go) */
        g_slip_back = 3;
        g_fr_mode = id == 71 ? 2u : 1u;
#if ML_GN_HANDOFF
        g_gn_back = 0;
#endif
        return 0;
    }
    if (id >= 60 && id <= 68) {
        g_fd_buf[id - 60] = (char) v;
        g_fd_buf[9] = 0;
        return 0;
    }
    {
        int rc = ml_friend_direct(g_fd_buf);
        if (rc == 0)
            g_slip_back = 3;                      /* armed: the character screen is entered once the Event overlay is gone and the Main Menu has settled (ml_frame, g_fr_go) */
        return rc;
    }
}

#endif
#if ML_GN_HANDOFF
/* Game Night: the lobby's call-up armed the room search (mnc gn_state, the sign overlay wrote the ticket name); the player goes to
   Slippi's Unranked character screen like the hub's Dojo row, the lock-in's B4 searches the call-up name instead (on_backend.c od_find), and the next menu entry
   returns to the hub and reopens the Game Night browser, which resumes the room (g_gn_back, ml_enter / ml_frame) */
static void go_gn_play(void)
{
    go_mode(1u);
    css_names_mark(ml_gn_rk() ? 1u : ML_CSS_GAME_NIGHT);   /* the header says Game Night, not Dojo (the call-up goes through the Unranked path); a Grandmaster set (the same hand-off) says Grandmaster (marker 1) */
    g_from_hub = 0;
    g_gn_back = 1u;
    g_slip_back = 0;                                 /* the last hand-off that set a mark wins: the Friends mark is dropped */
}
#endif

/* leave for a game mode like the stock VS. Mode > Melee row does: the character select */
static void go_scene(u32 mode)
{
    u8* d;
    if (mode == 0x1Bu) {                 /* GM_TOURNAMENT is never started: on the card route the exploit save's codehandler sits on the Tournament Melee code
                                            (0x801910E0..) and the scene jumps into garbage, a console freeze */
        ST(S_TOURN)++;
        SFX(0);
        return;
    }
    B(MENU_FLOW + 0x11u) = 1;
    H(MENU_COOLDOWN) = 5;
    g_couch = (mode == 2u ? 1u : 0u) | 2u;
    g_ret_kind = 0;
    if (mode == 0x1Bu && (B(MENU_FLOW + 0u) == 0u || B(MENU_FLOW + 0u) == (u8) ML_KIND_MORE)) {
        g_ret_kind = B(MENU_FLOW + 0u) + 1u;
        g_ret_row = (g_ret_row & 0x80000000u) | H(MENU_FLOW + 2u);   /* (bit 31: the first-Main-Menu music draw is done) */
    }
    SFX(1);
    d = (u8*) EXIT_DATA();
    d[0] = (u8) mode;
    EXIT_GO();
}

static void page_open(u32 idx);
static void mock_open(u32 scr);

void ml_think(void* gobj)
{
    u32 kind = B(MENU_FLOW + 0u), buttons, sel;
    const struct ml_screen* s = ml_screen_for_kind(kind);
    int n;
    (void) gobj;
    ST(S_THINK)++;
    pad_note();
    if (!s) {
        ST(S_ERR) |= 2u;
        return;
    }
#if defined(ML_SIGN) && ML_PART_OVL
    { static u8 unr_done; if (!unr_done) { unr_done = 1; tn_unrestore(); } }   /* back from the retail Tournament scene: the codehandler first, once per menu scene (a per-frame call undid the Original menu's own restore) */
    {   /* back from the animated bracket (tmv_st[TMV_W_LEAVE], set when B / X left the view): the Tournament screens open again by themselves on the bracket of the player's cup
           (leaving the view landed on the hub row and took three presses to get back) */
        const struct tmv_pub* tb = ML_TMV;
        if (tb && tb->st[6] && g_sign_state == 0 && g_ok && !g_tmv_go) {
            tb->st[6] = 0;
            g_sign_goto = (SG_TOURN | SG_GOTO_MINE) + 1u;
            g_sign_req = 1;
        }
    }
    if (g_tmv_go && g_sign_state == 0) {                                 /* the sign overlay is gone and handed an animated bracket over: restore the retail Tourney code and leave for the scene (go_tmv); a refusal stays in the menu */
        g_tmv_go = 0;
        if (go_tmv() == 0)
            return;
    }
#endif
#ifdef ML_SIGN
    if (g_slip_back == 3u && g_sign_state == 0 && (kind == 0u || kind == (u32) ML_KIND_MORE)) {   /* (More: the Friends row's screen since) */          /* a hand-off armed by the Friends view (Slippi friend, accepted Mano a mano): its Event screen has closed (frd_ev.c ev_think) and the Main Menu is back.
                                                                            The character screen is entered from this think, like the Main Menu's own rows; from inside the overlay the Event screen was left frozen under the scene change
                                                                             */
        if (++g_fr_go > 12u) {
            g_fr_go = 0;
            go_slippi_friend();                                          /* sets g_slip_back = 2 (the return mark) */
        }
        return;
    }
    g_fr_go = 0;
#endif
    buttons = MN_BUTTONS(4);
    *(volatile u32*) (MENU_FLOW + 0x08u) = 0;            /* MenuFlow.buttons (u64) */
    *(volatile u32*) (MENU_FLOW + 0x0Cu) = buttons;
    sel = H(MENU_FLOW + 2u);
    {
        int mi = mem_idx(kind);
        if (mi >= 0 && sel < s->nrows)
            g_last[mi] = (u8) sel;
    }
    if (g_online_wait) {                                 /* the Slippi row was confirmed; the bridge's check is running */
        if (buttons & MI_BACK)
            g_online_wait = 0;                           /* B cancels, the normal Back handling below */
        else {
            int r = ml_online_hook();                    /* > 0 ready, 0 still checking, < 0 refused (the reason is on the status line): stay on the Main Menu */
            if (r < 0)
                g_online_wait = 0;
            else if (r || ++g_online_wait > ONLINE_WAIT_MAX) {
                g_online_wait = 0;
                go_online();
            }
            return;
        }
    }
    if (buttons & MI_CONFIRM) {
        const struct ml_row* r;
        if (sel >= s->nrows)
            return;
        r = &s->rows[sel];
#ifdef ML_SIGN
#define ML_NEEDS_ACCOUNT(r) ((r)->act == ML_A_MODE ? (r)->arg != 1u : 1)
#define ML_SRV_SIGN_ROW(r) ((r)->act == ML_A_SIGN && ((r)->arg == (u8) SG_FRD || (r)->arg == (u8) SG_GN || (r)->arg == 25u))
#else
#define ML_NEEDS_ACCOUNT(r) ((r)->act == ML_A_MODE ? (r)->arg != 1u : 0)
#define ML_SRV_SIGN_ROW(r) 0
#endif
        /* rows that need our server: Grandmaster (mode 0), Dojo (1), Mano a mano (2), Friends, Game Night, Tournament (online_row).  Of these Dojo (guest quick match)
           stays open to a guest; the others also need a linked console (the hook's argument) */
        if ((r->act == ML_A_MODE) || ML_SRV_SIGN_ROW(r)) {
            int og = ml_offline_hook(ML_NEEDS_ACCOUNT(r));
            if (og == 2) {                             /* FORCED UPDATE: the Update screen instead of the mode */
                open_update_required();
                return;
            }
            if (og) {
                SFX(3);                                /* refused: the reason is in the description bar (bar_refuse), the Main Menu / hub keeps working */
                bar_refuse(sel, ml_net_cost(107));
                return;
            }
        }
        if (files_row(r) && ml_online_data_missing(ml_net_cost(107))) {      /* the rows that need our Multishine files (and Dojo, interim): refused with a buzz, the description line says why */
            SFX(3);
            g_bar_msg = "Multishine system files not set up: check network"; g_bar_msg_hov = sel; g_bar_hov = 0xFFFFu; g_bar_arm = 1;
            return;
        }
        if (online_row(r) && (ml_net_cost(107) & 9u)) {      /* the first level's server rows offline (what the old Online row did for every mode in it): refused, the bar says why and the right panel says 'Console is offline' */
            SFX(3);
            bar_refuse(sel, ml_net_cost(107));
            return;
        }
        if (r->act == ML_A_ONLINE && ml_offline_hook(2) == 2) {      /* the Slippi bridge row (UPDREQ_QUERY): the same rule */
            open_update_required();
            return;
        }
        ST(S_CONFIRM)++;
        ST(S_LAST_KIND) = kind;
        ST(S_LAST_SEL) = sel;
        ST(S_LAST_ACT) = r->act;
        if (r->act == ML_A_KIND)
            go_kind((int) r->arg, r->hov);
        else if (r->act == ML_A_ONLINE) {
            int r = ml_online_hook();
            if (r > 0)
                go_online();
            else if (r == 0)
                g_online_wait = 1;                     /* r < 0: refused, stay on the Main Menu */
        }
        else if (r->act == ML_A_MODE)
            go_mode(r->arg);
        else if (r->act == ML_A_SCENE)
            go_scene(r->arg);
        else if (r->act == ML_A_PAGE)
            page_open(r->arg);
        else if (r->act == ML_A_SIGN)
            mock_open(r->arg);
        return;
    }
    if (buttons & MI_BACK) {
        ST(S_BACK)++;
        SFX(0);
        B(MENU_FLOW + 0x11u) = 0;
        if (s->parent_kind == ML_KIND_NONE) {          /* the title screen: what the retail Main think does */
            u8* d;
            H(MENU_COOLDOWN) = 5;
            d = (u8*) EXIT_DATA();
            d[0] = 0;                                  /* pending_mode = GM_TITLE */
            EXIT_GO();
        } else {
            MN_GOTO((int) s->parent_kind, s->parent_pos, 3);
        }
        return;
    }
    if (buttons & MI_UP) {
        SFX(2);
        n = ml_step(s, (int) sel, -1);
        H(MENU_FLOW + 2u) = (u16) n;
    } else if (buttons & MI_DOWN) {
        SFX(2);
        n = ml_step(s, (int) sel, 1);
        H(MENU_FLOW + 2u) = (u16) n;
    }
}

/* Options screen think: dormant since (the Debug row was removed from the layout and the Options table entry is no longer patched, see ml_enter); the abi slot stays, and a row 6 can never be
   confirmed on the six-row retail screen.  Everything else is the retail think. */
void ml_think_opt(void* gobj)
{
    u32 b = MN_BUTTONS(4);
    if (!b)
        return;
    if ((b & MI_CONFIRM) && H(MENU_FLOW + 2u) == 6u) {
        ST(S_DEBUG)++;
        lang_debug_enter();
        go_scene(6);
        return;
    }
    ((void (*)(void*)) RETAIL_OPT_THINK)(gobj);
}

/* 1-P Mode think slot: with kind 0 or More on screen it is our think (Back from Slippi's screen lands on More), else the retail 1-P think */
void ml_think_1p(void* gobj)
{
    if (B(MENU_FLOW) == 0u || B(MENU_FLOW) == (u8) ML_KIND_MORE)
        ml_think(gobj);
    else
        ((void (*)(void*)) RETAIL_1P_THINK)(gobj);
}

/* ---- text drawing ---- */
static const u8 col_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 col_gold[4] = { 0xFF, 0xD7, 0x00, 0xFF };
static const u8 col_dim[4] = { 0xB4, 0xB4, 0xB4, 0xFF };
static const u8 col_bg[4] = { 0x00, 0x00, 0x10, 0xFF };    /* opaque: the plates must not show through */
#if ML_UI && defined(ML_OVL)
static const u8 col_bg_glass[4] = { 0x00, 0x00, 0x10, 0xC0 };   /* over the Settings globe (pgl_show): 75 percent, the text keeps its contrast, the globe shows through */
static int pgl_show(void);
#define PGL_BG() (pgl_show() ? col_bg_glass : col_bg)
#else
#define PGL_BG() col_bg
#endif
static const double zero_xy[2] = { 0.0, 0.0 };

#define F_POS_Z 0x41880000u    /* 17.0 */

static u32* text_new_at(u32 x_bits, u32 y_bits, u32 size_bits, const u8* c)
{
    u8 canvas = B(MENU_CANVAS_FRONT);
    u32* t = (u32*) TEXT_NEW(0, canvas);
    if (!t)
        return 0;
    t[0] = x_bits;
    t[1] = y_bits;
    t[2] = F_POS_Z;
    t[0x24 / 4] = size_bits;
    t[0x28 / 4] = size_bits;
    ((u8*) t)[0x49] = 1;
    ((u8*) t)[0x4A] = 0;
    ((u8*) t)[0x30] = c[0];
    ((u8*) t)[0x31] = c[1];
    ((u8*) t)[0x32] = c[2];
    ((u8*) t)[0x33] = c[3];
    return t;
}

#ifdef ONS_NETSEL
static u32 g_fps_on;                   /* lab/dev builds (Settings > Game > Frame Rate Readout, RAM only): the frames per second of the menu, from the time base every 60 frames */
static void* g_fps_txt;
static void* g_nd_txt;
static void* g_nd_txt2;                /* its second line: the last blocking step of the card / DVD hook (df_glue.c df_dbg) */                 /* the network line of the lab/dev menu (rebuilt once a second, like the readout above) */
#endif

static void drop_all(int do_free)
{
    int i;
#ifdef ONS_NETSEL
    if (g_fps_txt && do_free)
        TEXT_FREE(g_fps_txt);
    g_fps_txt = 0;
    if (g_nd_txt && do_free)
        TEXT_FREE(g_nd_txt);
    g_nd_txt = 0;
    if (g_nd_txt2 && do_free)
        TEXT_FREE(g_nd_txt2);
    g_nd_txt2 = 0;
#endif
    for (i = 0; i < MLR; i++) {
        if (g_txt[i] && do_free)
            TEXT_FREE(g_txt[i]);
        g_txt[i] = 0;
    }
    if (g_bar && do_free)
        TEXT_FREE(g_bar);
    g_bar = 0;
    g_bar_hov = 0xFFFFu;
    g_bar_msg = 0;
    g_bar_seen = g_bar_gen_prev;    /* the description text object of the screen before is gone: wait for the hook of the new one, which may already have come since the frame before */
    g_bar_arm = ST(S_BAR) != g_bar_gen_prev;   /* (back from a mode the bar stayed empty until the cursor moved) */
    g_built = 0xFFFFu;
    g_cover_hover = 0xFFFFu;
    g_cover_due = 0xFFFFu;
}

/* ---- stub text screens (Tournament, Account, Friends):  kind T, text only ----
   Lines: title, note, rows with a "- " cursor, status (the answer of a stub row), footer. Geometry of the options page (measured on the console
   and in Dolphin): x -13.0, size 0.028, backdrop -13.4 x -9.3 .. 26.8 x 16.8.  Input from the all-ports controller map. */
#ifndef F_BAR_X
#define F_BAR_X 0u               /* the description bar: centred at x 0 */
#define F_BAR_Y 0x410E6666u      /* 8.9 */
#define F_BAR_SIZE 0x3CA3D70Au   /* 0.02 */
#endif
#define F_PG_X 0xC1500000u     /* -13.0 */
#define F_PG_SIZE 0x3CE56042u  /* 0.028 */
#define F_BG_X 0xC18B3333u     /* -17.4: the backdrop covers the whole screen (nothing of the menu behind the stub screens, not even the description bar) */
#define F_BG_Y 0xC1566666u     /* -13.4 */
#define F_BG_W 0x449B5B6Eu     /* 34.8 / 0.028 */
#define F_BG_H 0x446BB6DBu     /* 26.4 / 0.028 */
static const float pg_y[PG_ROWS + 4] = { -8.5f, -7.2f, -5.8f, -4.7f, -3.6f, -2.5f, -1.4f, -0.3f, 0.8f, 1.9f, 5.2f, 6.5f };   /* title, note, 8 rows, status, footer */
static int g_page = -1;
static u32 g_page_kind;
static u32 g_pg_cur, g_pg_status;
static void* g_pg_back;
static void* g_pg_title;
static void* g_pg_note;
static void* g_pg_row[PG_NR];
static void* g_pg_stat;
static void* g_pg_foot;
static const char* g_pg_msg;           /* the status line now shown (a static string) */
#ifdef ML_SETTINGS
static void* g_pg_desc;                /* the description line of the row under the cursor */
#endif
#if ML_UI
static void* g_pg_crumb;               /* the breadcrumb */
static void* g_pg_bar;                 /* the selection bar behind the row under the cursor */
static void* g_pg_mark;                /* the small mark (a gfx handle) */
static void* g_pg_val[PG_NR];          /* the right aligned value of a list row */
static void* g_pg_sec[3];              /* section headings */
static u32* ui_text(unsigned row, unsigned attr, const char* s, int right);
static u32* ui_bar(unsigned row, unsigned big);
static void* ui_mark(void);
static void* ui_mark_menu(void);
static void ui_free(void* h);
static unsigned ft_n[4];               /* texts of the footers: group g owns sets 2g and 2g + 1 (group 0 the Settings pages, 1 the sign overlay's rows); forgotten with their handles when the canvas dies */
static unsigned ft_grp = 1;            /* the group ui_foot makes its footer in: the pages set 0 around their footer */
#endif

#ifdef ML_SETTINGS
/* Settings wiring (tree of).  The Settings page (generated table "mo")
   lists Game, Online, About; a lab build adds Diagnostics (rollback readout / controller test builds) after them.  Virtual pages built here, not in the generated table:
   PG_SIGN    the one-line screen shown while the sign-in overlay loads or when it is not available
   PG_GAME    Settings > Game: Widescreen, Progressive Scan, Online Delay, Menu Preload (the "MultishineOptions" card file, on_set.c)
   PG_SYS     Settings > Diagnostics (lab builds): Controller Test, Rollback Readout
   Settings > Online is the sign overlay's Online page (Link This Console, Device Identity, Update, the two invite settings, Delete), Settings > About its About screen;
   a build without the overlay answers both with "Not available in this build." */
#define PG_SIGN 17                    /* virtual page numbers above the generated table (ML_NPAGE) */
#define PG_GAME 18
#define PG_SYS 19
#define PG_NET 20                      /* Settings > Network status: read-only addresses and the server check */
#if defined(SD_CARD_HOLE) && defined(ML_SETTINGS) && ML_UI
#define ML_NETTEST 1                   /* card route: the row "Test connection" and its result lines; the engine side is s12_gc.c ms_net_test_q behind ms_net_cost 110..114 */
#else
#define ML_NETTEST 0
#endif
/* Status (0, a status line), then the actions (moved up); the Network details are no longer an item: the four facts stand as a block above the help line (ml_nt_geom.h NTG_FACT_Y0) */
#define RC_ROW 1u                       /* the item "Reconnect" (grid row 6) */
#define NT_ROW 2u                       /* the item "Test connection" (grid row 8) */
#define WF_ROW (2u + (u32) ML_NETTEST)    /* the item "Wired opponents only": after Test connection (grid row 10); the Connection check row was removed on all platforms */
#if ML_NETTEST
typedef char net_items_match_geom[(RC_ROW == NTG_I_RECONNECT && NT_ROW == NTG_I_TEST && WF_ROW == NTG_I_WIRED) ? 1 : -1];   /* the item order of ml_nt_geom.h */
#endif
#if ML_NETTEST
static u32* nt_text_at(unsigned k, unsigned cls, const char* s);        /* line k of the Test connection block at its pixel row: cls 0 dim, 1 good, 2 amber, 3 bad */
static u32* nt_text_row(unsigned row, unsigned k, unsigned cls, const char* s);   /* the same for the block under any item (grid row `row`) */
static u32* nt_text_fact(unsigned k, const char* s);                     /* fact line k of the Network details block above the help line (NTG_FACT_Y) */
#endif
#if defined(ML_SIGN) && (defined(ML_CTLTEST) || ML_UI)
#define SYS_CTL 1                      /* Settings > Diagnostics row 0 "Controller test" opens the Controller test screen of the sign overlay; on with the Settings look (Diagnostics is a visible page), else opt-in with -DML_CTLTEST */
#else
#define SYS_CTL 0
#endif
#define SYS_ENT ML_UI                  /* the next row "Entropy test" opens the sign overlay's entropy screen (a visible Diagnostics row, was a lab screen) */
#define SYS_FIRST (SYS_CTL + SYS_ENT)  /* rows before the rollback readout */
#ifdef MS_RB_RUNTIME
#define SYS_RB 1
#else
#define SYS_RB 0
#endif
#ifdef ONS_NETSEL
#define SYS_DEV 9                      /* lab/dev builds (netcfg=on): Menu Preload and Frame Rate Readout live here, after the controller test and the rollback readout; the last row is Spectate Upload */
#else
#define SYS_DEV 0
#endif
#define SYS_I_ENT (SYS_CTL)            /* row indexes of Settings > Diagnostics */
#define SYS_I_RB (SYS_FIRST)
#define SYS_I_PRE (SYS_FIRST + SYS_RB)
#define SYS_I_FPS (SYS_FIRST + SYS_RB + 1)
#define SYS_I_NETSET (SYS_FIRST + SYS_RB + 3)      /* Server And Slippi: opens the sign overlay's lab page (SG_GOTO_DEV) */
#define SYS_I_TT (SYS_FIRST + SYS_RB + 4)          /* Test Toast: Left-Right the kind, A raises it */
#define SYS_I_NET (SYS_FIRST + SYS_RB + 5)         /* two read-only Net cost rows (hub proc, bridge tick) */
#define SYS_I_SPEC (SYS_FIRST + SYS_RB + 7)        /* Spectate Upload (Game Night match-time uploader): default On, only inside a Game Night set of a spectatable room */
#define SYS_I_DBG (SYS_FIRST + SYS_RB + 8)         /* Debug Lines: the lab/dev on-screen debug text (network line, wait line) is hidden unless this is On (default Off, saved with the other options) */
#define SYS_I_RST (SYS_FIRST + SYS_RB + 2)         /* Restart (lab/dev builds): A twice within 3 s = the Update screen's soft reset */
#define SYS_ROWS (SYS_FIRST + SYS_RB + SYS_DEV)
#define PG_SETTINGS 2                  /* the generated page "mo" (Settings) */
#define PG_GROWS 9                     /* Settings > Game: widescreen, progressive scan, online delay, show technical info (next to the delay) */
#define SET_DIAG (SYS_ROWS ? 1u : 0u)  /* Settings > Diagnostics: a page of every build with the Settings look (Controller Test, Entropy Test) */
/* The Settings list, in this order: Online, Game, Update (its own top-level row), About, Diagnostics (hidden until the Konami code, this session only).  The generated table "mo"
   (Game, Online, About) is not used for the rows any more: the order and the new rows are here. */
#define SET_ROWS_BASE 6u
enum { SETK_ACC = 0, SETK_PRIV, SETK_GAME, SETK_UPDATE, SETK_ABOUT, SETK_NET, SETK_DIAG };   /*: Delete is a row of Account (no top-level row), Friends and privacy is "Friends" */    /*: Online dissolved, its three pages are top-level rows (the sign overlay's Online pages, SG_GOTO_PAGE) */
#ifdef ONS_NETSEL
#define DIAG_ALWAYS 1u                 /* lab/dev builds (the ones our tests): Diagnostics is always in the Settings list, no code needed; a release build keeps it behind the Konami code */
#else
#define DIAG_ALWAYS 0u
#endif
static u32 g_diag_open;                /* the Konami code was entered on the Settings list: Diagnostics shows until the next boot (never stored) */
static struct ml_konami g_konami;
static u32 g_kon_done;                 /* the code was completed on this frame */
#if defined(ONS_NETSEL) || defined(SD_CARD_HOLE)
/* the software reset behind "A: Restart now" after an install and behind Settings > Diagnostics > Restart: Melee's own reset-switch path (lb_8001955C, proven in Dolphin by
   ): audio off, finish the card task queue, drop the VI callbacks, black screen, two retraces, OSResetSystem(0,0,0) = __OSReboot (the apploader runs again, the loader re-injects at
   Name Entry).  The Update screen's host unmounted the card before; Diagnostics flushes its settings first. */
#if defined(ML_OVL) && defined(SD_CARD_HOLE) && defined(MS_CARD_WA_ADDR)
#include "ml_reinj.h"
#ifndef IC_INVAL
#define IC_INVAL ((void (*)(void*, u32)) 0x803448D4u)          /* ICInvalidateRange */
#endif
#ifndef DC_STORE
#define DC_STORE ((void (*)(void*, u32)) 0x80344840u)          /* DCStoreRange */
#endif
/* Wii / Nintendont, stock disc: the restart below reloads the DOL through the kernel, past the launcher that applied the Name Entry inject, so Melee would come back
   stock.  When the running game IS the injected one and the kernel's entry stub is the known one, a trampoline in the card work window re-applies the inject in the stub's window (after the
   reload, before the game starts) and puts the stub back.  Returns the stub's last-word address while armed, else 0.  Overlay code, no engine bytes; any doubt = nothing written. */
static void reinj_diag(u32 code, u32 word)                        /* Wii only (called once the kernel stub was found): the words the shutdump prints as 'FZ4: BC .. at tbl ..' */
{
    W(RJ_DIAG1) = word;
    W(RJ_DIAG0) = code;
}
static u32 reinj_arm(void)
{
    u32 blr, br, live = 0, d;
    u8* wa;
    blr = rj_find_stub_blr();
    if (!blr)
        return 0;                                                 /* no Nintendont stub: not a Wii, nothing to do and no Wii memory to write */
    d = rj_live_diff(&live);
    if (d) {
        reinj_diag(0x524A0001u | (d << 8), live);                 /* the live words are not the inject's: nothing armed (index in bits 8..15, the live word in the second word) */
        return 0;
    }
    wa = ms_card_wa_acquire(3u);                                  /* owner 3: never released on the way to the reset; refused = busy = no re-inject */
    if (!wa) {
        reinj_diag(0x524A0002u, 0);
        return 0;
    }
    br = rj_branch(blr, (u32) wa);
    if (!br || (u32) wa < 0x80800000u || (u32) wa + RJ_BYTES > 0x81800000u || (u32) wa < (u32) MS_CARD_WA_ADDR) {
        ms_card_wa_release();
        reinj_diag(0x524A0003u, (u32) wa);
        return 0;
    }
    rj_build((volatile u32*) wa, blr, (u32) wa);
    DC_STORE(wa, RJ_BYTES);
    IC_INVAL(wa, RJ_BYTES);
    W(blr) = br;                                                  /* the stub's last word: blr -> b trampoline */
    DC_STORE((void*) blr, 4);
    IC_INVAL((void*) blr, 4);
    reinj_diag(0x524A0004u, (u32) wa);
    return blr;
}
/* Nintendont keeps the card image in RAM and copies the changed range to the SD file from its main loop once no card write came for
   more than 2 s (kernel/main.c SaveCard, GCNCard_Save), holding all EXI service (card and vBBA) while it writes.  After an install that
   range is most of the card (up to ~2 MB), so an immediate restart put that SD write on top of the DOL reload and Melee's boot card scene.
   The black screen waits it out: 2.x s idle + the write, 360 fields = 6 s at 60 Hz (7.2 s at 50 Hz).  Wii only (the kernel stub was found).
   A mitigation for the one-off 'damaged memory card' after an in-game update's restart: the card itself was
   valid at that restart (the SDK cut sweep); not proven on a console. */
#ifndef RST_SETTLE_FIELDS
#define RST_SETTLE_FIELDS 360u
#endif
static void reinj_disarm(u32 blr)                                 /* the reset call came back: put the stub back and free the window */
{
    W(blr) = RJ_BLR;
    DC_STORE((void*) blr, 4);
    IC_INVAL((void*) blr, 4);
    ms_card_wa_release();
}
/* GameCube, stock disc: the same restart lands in plain Melee because nothing walks to Name Entry.  __OSReboot's call of the boot image is pointed at
   stage 1 in the card work window, which (boot image hash checked) hooks the boot image's step after the apploader loaded the DOL: the fresh DOL gets the BASE-MIN auto Name Entry words
   before it starts.  Returns 1 when armed.  Any 'no' = nothing written = the plain restart. */
static u32 reinj_arm_gc(void)
{
    u8* wa;
    u32 s1;
    if (!rjg_live_ok())
        return 0;
    wa = ms_card_wa_acquire(3u);
    if (!wa)
        return 0;
    if ((u32) wa < (u32) MS_CARD_WA_ADDR || (u32) wa + RJG_BYTES > 0x81800000u || !(s1 = rjg_build((volatile u32*) wa, (u32) wa))) {
        ms_card_wa_release();
        return 0;
    }
    DC_STORE(wa, RJG_BYTES);
    IC_INVAL(wa, RJG_BYTES);
    W(RJG_REBOOT_CALL) = rj_branch(RJG_REBOOT_CALL, s1) | 1u;    /* bl stage 1 */
    DC_STORE((void*) RJG_REBOOT_CALL, 4);
    IC_INVAL((void*) RJG_REBOOT_CALL, 4);
    return 1;
}
static void reinj_disarm_gc(void)
{
    W(RJG_REBOOT_CALL) = rj_branch(RJG_REBOOT_CALL, RJG_RUN) | 1u;
    DC_STORE((void*) RJG_REBOOT_CALL, 4);
    IC_INVAL((void*) RJG_REBOOT_CALL, 4);
    ms_card_wa_release();
}
#endif
static void soft_reset(void)
{
#if defined(ML_OVL) && defined(SD_CARD_HOLE) && defined(MS_CARD_WA_ADDR)
    u32 rj = 0, rg = 0;
#endif
    ((void (*)(void)) 0x80027DBC)();                        /* lbAudioAx_80027DBC */
    while (((int (*)(void)) 0x8001B6F8)() == 11) { }        /* lbCardNew_CompleteNextTask */
    ((void (*)(int)) 0x8034EBD0)(0);                        /* VISetPostRetraceCallback(0) */
    ((void (*)(int)) 0x8034EB8C)(0);                        /* VISetPreRetraceCallback(0) */
    ((void (*)(int)) 0x80350100)(1);                        /* VISetBlack */
    ((void (*)(void)) 0x8034FF78)();                        /* VIFlush */
    ((void (*)(void)) 0x8034F314)();                        /* VIWaitForRetrace */
    ((void (*)(void)) 0x8034F314)();
#if defined(ML_OVL) && defined(SD_CARD_HOLE) && defined(MS_CARD_WA_ADDR)
    if (rj_find_stub_blr()) {                               /* Wii / Nintendont: let the kernel write its card image to the SD before the game restarts (RST_SETTLE_FIELDS) */
        u32 f;
        for (f = 0; f < RST_SETTLE_FIELDS; f++)
            ((void (*)(void)) 0x8034F314)();
    }
    rj = reinj_arm();                                       /* last thing before the reset: the stub is hooked only for the reload that follows */
    if (!rj)
        rg = reinj_arm_gc();                                /* GameCube stock disc: the reload is hooked for this restart only */
#endif
    ((void (*)(int, int, int)) 0x8034844C)(0, 0, 0);        /* OSResetSystem */
#if defined(ML_OVL) && defined(SD_CARD_HOLE) && defined(MS_CARD_WA_ADDR)
    if (rj)
        reinj_disarm(rj);
    if (rg)
        reinj_disarm_gc();
#endif
}
#endif
#ifdef ONS_NETSEL
static u32 g_tt_kind;                  /* Test Toast kind: 0 Friend Request, 1 Invite, 2 Friends Added, 3 Slippi Direct, 4 Room Invite, 5 Room Join */
static u32 g_rst_until;                /* Restart armed until this menu frame (0 = not armed) */
#endif
static int g_pg_parent = -1;           /* page to return to on B (-1: the menu) */
static u32 g_pg_parent_cur;
static u32 g_pg_start_cur;
static const char* g_pg_note_txt;      /* PG_SIGN: the note line */
__attribute__((unused)) static u8 g_ut_hold;   /* 1 = PG_SIGN is the update check's hold ('Checking for updates.', hub_ovl.h ut_menu_step); B skips it */
static u32 g_sign_state;               /* 0 idle, 1 loading, 2 overlay active, 3 failed */
static u32 g_sign_req;                 /* ml_sign_request: open the overlay at the next frame (the Slippi screen's log-in row) */
#if SYS_CTL
static u32 g_sign_ctl;                 /* the overlay opens on the Controller test screen (Settings > System) */
#endif
#if SYS_FIRST
static u32 g_sys_gp_cur;               /* cursor of the Settings page while the Controller test or the Entropy test is up (the System page's parent) */
#endif
static u32 g_sign_from_online;         /* the overlay was requested from Slippi's screen: Back lands on the Main Menu */
static u32 g_ev_room;                  /* the room code + 1 of a toast the player accepted, handed to mock_open (SG_GOTO_ROOM) */
static u32 g_sign_goto;                /* a row that opens the sign overlay on a page (ML_A_SIGN: hub Game Night, hub Standings, the -DML_MOCKUPS=1 Main Menu row Mockups): overlay page + 1 to open once loaded; Back leaves to the screen of the row */
static int sign_open(void);
static void sign_shutdown(void);
#endif

__attribute__((unused)) static int str_eq(const char* a, const char* b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static u32 pg_nrows(int idx)
{
#ifdef ML_SETTINGS
    if (idx == PG_SIGN)
        return 0;
    if (idx == PG_GAME)
        return PG_GROWS;
#if SYS_ROWS
    if (idx == PG_SYS)
        return SYS_ROWS;
#endif
    if (idx == PG_SETTINGS)
        return SET_ROWS_BASE + (SET_DIAG && (g_diag_open || DIAG_ALWAYS) ? 1u : 0u);
    if (idx == PG_NET)
        return 2u + (u32) ML_NETTEST + 1u;   /* Status, Reconnect, Test connection, then Wired opponents only; the Network details are a footer block, not an item */
#endif
    return ml_page_tab[idx].nrows;
}

static void page_forget(void)
{
    int i;
    g_page = -1;
    g_pg_back = g_pg_title = g_pg_note = g_pg_stat = g_pg_foot = 0;
#if ML_UI
    ft_n[0] = ft_n[1] = 0;                         /* the page's footer texts die with the canvas */
#endif
    for (i = 0; i < PG_NR; i++)
        g_pg_row[i] = 0;
#ifdef ML_SETTINGS
    g_pg_msg = 0;
    g_pg_parent = -1;
    g_pg_desc = 0;
#endif
#if ML_UI
    g_pg_crumb = g_pg_bar = g_pg_mark = 0;
    for (i = 0; i < PG_NR; i++)
        g_pg_val[i] = 0;
    g_pg_sec[0] = g_pg_sec[1] = g_pg_sec[2] = 0;
#endif
}

static void* pg_line(u32 y_bits, const u8* col, const char* s)
{
    u32* t = text_new_at(F_PG_X, y_bits, F_PG_SIZE, col);
    char buf[120];
    if (t)
        ml_sis_add(t, zero_xy, "%s", ml_sis_expand(buf, sizeof buf, s) ? buf : s);
    return t;
}

#if ML_UI
static int ft_release(void* t);        /* the footer with button badges is several texts: its first text stands for all (ui_foot) */
#endif
static void pg_free(void** p)
{
    if (*p) {
#if ML_UI
        if (!ft_release(*p))
#endif
            TEXT_FREE(*p);
    }
    *p = 0;
}

static char* pg_put(char* d, const char* s)
{
    while (*s)
        *d++ = *s++;
    return d;
}

#ifdef ML_SETTINGS
static char* pg_put_u(char* d, u32 v)
{
    return d + ms_fmt_u(d, v, 11u);
}
#endif

/* the text of row i of page idx: the label in out, and the value of the settings rows in val ("" = none); at most 48 characters each.  The Settings look draws the value right
   aligned; the old layout (no overlay) joins them with a colon. */
#ifdef ML_SETTINGS
/* the network facts (s12_gc.c ms_net_dbg: 1 ip, 3 gateway, 10/11 MAC, 12 public address) and the server check (on_backend.c ms_net_cost 103: an answer within 45 s) */
#ifndef ML_OVL
extern void ms_net_dbg(unsigned* w) __attribute__((weak));
#endif
static int net_state(unsigned* w)                             /* 0 not available, 1 no network, 2 no answer from the server, 3 online, 4 reconnecting */
{
    u32 i;
    for (i = 0; i < 16u; i++)
        w[i] = 0;
#if defined(ML_OVL) && !defined(SD_CARD_HOLE)
    return 0;                                                 /* an overlay without the card route's resident table: no network facts */
#else
    if (!ms_net_dbg)
        return 0;
    ms_net_dbg(w);
#endif
    if (w[13])
        return 4;                                             /* a full reconnect is under way (s12_gc.c ms_net_reconnect: adapter, link, address, server) */
    if (!w[1])
        return 1;
    (void) ml_net_cost(104);                                  /* ask the hub overlay for a real server check while this page is up (on_backend.c) */
    return ml_net_cost(103) ? 3 : 2;
}
static const char* const net_word[5] = { "Not available", "No network", "No server", "Online", "Reconnecting..." };
/* what is missing while the state says "No server" (on_backend.c ms_net_cost 109: 1 the setup read waits for the memory card, 2 no online setup, 3 starting, 4 no server address; 0 = asked and no answer) */
static const char* const net_why_word[5] = { 0, "Waiting for the card", "No online setup", "Starting...", "No server address" };
static const char* const net_why_help[5] = { 0, "Waiting for the memory card.", "No online setup on the memory card.", "Starting: the network comes up.", "No server address yet." };
static u32 net_why(int st) { u32 r = st == 2 ? ml_net_cost(109) : 0u; return r < 5u ? r : 0u; }
static const char* net_word_of(int st) { u32 r = net_why(st); return r ? net_why_word[r] : net_word[st]; }
#if ML_NETTEST                         /* pg_put_ip / pg_put_hex2 are used by net_fact only: no unused statics in a build without the row (lab profile s17-compat-onb, -Werror) */
static char* pg_put_ip(char* d, u32 ip)
{
    u32 k;
    for (k = 0; k < 4u; k++) {
        u32 b = (ip >> (24u - 8u * k)) & 255u;
        if (k)
            *d++ = '.';
        if (b >= 100u)
            *d++ = (char) ('0' + b / 100u);
        if (b >= 10u)
            *d++ = (char) ('0' + (b / 10u) % 10u);
        *d++ = (char) ('0' + b % 10u);
    }
    *d = 0;
    return d;
}
static char* pg_put_hex2(char* d, u32 v)
{
    static const char hx[] = "0123456789ABCDEF";
    *d++ = hx[(v >> 4) & 15u];
    *d++ = hx[v & 15u];
    return d;
}
/* one of the four facts of Network details (k: 0 Console address, 1 Gateway, 2 Adapter, 3 Public address): the label in d and the value in dv (empty while the adapter is not up) */
static void net_fact(u32 k, int st, const unsigned* w, char* d, char* dv)
{
    static const char* const fl[4] = { "Console address", "Gateway", "Adapter", "Public address" };
    d = pg_put(d, fl[k & 3u]);
    if (k != 2u) {
        u32 ip = k == 0u ? w[1] : k == 1u ? w[3] : w[12];
        dv = ip ? pg_put_ip(dv, ip) : pg_put(dv, k == 3u ? (st <= 1 ? "No IP address" : "Not known yet") : "None");      /* offline: no public address to wait for */
    } else if (st) {
        u32 j;
        for (j = 0; j < 6u; j++) {
            u32 b = j < 4u ? (w[10] >> (24u - 8u * j)) & 255u : (w[11] >> (8u * (5u - j))) & 255u;
            if (j)
                dv = pg_put(dv, ":");
            dv = pg_put_hex2(dv, b);
        }
    }
    *d = 0;
    *dv = 0;
}
#endif
#if ML_NETTEST
#include "nettext.h"                                              /* verdict, texts and the result word of the connection test: pure C (offline/net), staged beside the sources */
#include "nettext.c"
static void nt_results(nt_leg_result* r, nt_leg_result* n)
{
    ntx_unpack(ml_net_cost(113), r);
    ntx_unpack(ml_net_cost(114), n);
}
#endif
#endif
static void pg_row_text(int idx, int i, char* out, char* val)
{
    char* d = out;
    char* dv = val;
    *val = 0;
#ifdef ML_SETTINGS
    if (idx == PG_GAME && i == 8) {                          /* Music: On is the default */
        d = pg_put(d, "Music");
        dv = pg_put(dv, ons_cur()->musicoff ? "Off" : ons_cur()->musicrand ? "Random" : "On");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_GAME && i == 0) {
        d = pg_put(d, "Widescreen 16:9");
        dv = pg_put(dv, ons_cur()->ws ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_GAME && i == 4) {                          /* one general switch for technical text: delay readout, error codes, network lines; the stored Debug Lines bit */
        d = pg_put(d, "Show frame delay");
        dv = pg_put(dv, ons_cur()->dbgl ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_GAME && i == 5) {                          /* the in-match ping line: its own switch, default Off */
        d = pg_put(d, "Show ping");
        dv = pg_put(dv, ons_cur()->pingon ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_GAME && (i == 6 || i == 7)) {              /* Ping and delay position / Raise HUD: one name, one value from a table */
        static const char* const nm[2] = {"Ping and delay position", "Raise HUD"};
        static const char* const vl[7] = {"Bottom", "Above timer", "Below timer", "Off", "Small", "Medium", "Large"};
        const struct ms_settings* c = ons_cur();
        d = pg_put(d, nm[i - 6]);
        dv = pg_put(dv, vl[i == 6 ? c->hudpos : 3u + (c->hudraise & 3u)]);      /* bit 2 of hudraise is Wired opponents only */
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_GAME && i == 2) {                          /* Auto delay: On = the engine picks the delay and raises it by itself when a match stalls; Off = the delay below */
        d = pg_put(d, "Auto delay");
        dv = pg_put(dv, ons_cur()->delay == MSET_DELAY_AUTO ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_GAME && i == 3) {
        const struct ms_settings* c = ons_cur();
        d = pg_put(d, "Online delay");
        if (c->delay == MSET_DELAY_AUTO) {                   /* Auto delay On: the row is grey and shows the remembered manual value; the frames come from the opponent at pairing (the CSS chip shows them then) */
            dv = pg_put_u(dv, c->dman);
            dv = pg_put(dv, c->dman == 1 ? " frame" : " frames");
        } else {
            dv = pg_put_u(dv, c->delay);
            dv = pg_put(dv, c->delay == 1 ? " frame" : " frames");
        }
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SETTINGS) {
        d = pg_put(d, i == SETK_ACC ? "Account" : i == SETK_PRIV ? "Friends" : i == SETK_GAME ? "Game" : i == SETK_UPDATE ? "Update" : i == SETK_ABOUT ? "About" : i == SETK_NET ? "Network status" : "Diagnostics");
        if (i == SETK_NET) {
            unsigned w[16];
            { int st2 = net_state(w); dv = pg_put(dv, net_word_of(st2)); }
        }
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_NET) {                                       /* Status, Reconnect, Test connection, Wired opponents only */
        unsigned w[16];
        int st = net_state(w);
        static const char* const lab[4] = { "Status", "Reconnect", "Test connection", "Wired opponents only" };
        d = pg_put(d, i == (int) WF_ROW ? lab[3] : (ML_NETTEST && i == (int) NT_ROW) ? lab[2] : lab[i < 2 ? i : 0]);
        if (i == (int) WF_ROW)
            dv = pg_put(dv, ons_cur()->hudraise & 4u ? "On" : "Off");      /* bit 2 of hudraise (ml_set.h, byte 17 of the options record) */
        if (i == 0)
            dv = pg_put(dv, net_word_of(st));
#if ML_NETTEST
        else if (i == (int) NT_ROW) {                          /* running: "Testing n of 21"; after a run: the verdict word */
            u32 w = ml_net_cost(112), ts = w & 15u;
            if (ts == 1u) {
                char pb[24];
                (void) ntx_progress(pb, (w >> 8) & 255u);
                dv = pg_put(dv, pb);
            } else if (ts == 2u || ts == 3u) {
                nt_leg_result r, n;
                nt_results(&r, &n);
                dv = pg_put(dv, ntx_word(ntx_verdict(&r, &n)));
            }
        }
#endif
        *d = 0;
        *dv = 0;
        return;
    }
#ifdef ONS_NETSEL
    if (idx == PG_SYS && i == SYS_I_FPS) {
        d = pg_put(d, "Frame rate readout");
        dv = pg_put(dv, g_fps_on ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SYS && i >= SYS_I_NET && i < SYS_I_NET + 2) {      /* NET COST probe: since boot, microseconds on the frame thread */
        int k = i - SYS_I_NET;
        d = pg_put(d, k == 0 ? "Hub proc" : "Bridge tick max");
        if (k < 1) {
            u32 n = ml_net_cost(k);
            dv = pg_put_u(dv, n);
            dv = pg_put(dv, " avg ");
            dv = pg_put_u(dv, n ? ml_net_cost(2 + k) / n : 0u);
            dv = pg_put(dv, " max ");
            dv = pg_put_u(dv, ml_net_cost(4 + k));
        } else
            dv = pg_put_u(dv, ml_net_cost(6));
        dv = pg_put(dv, " us");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SYS && i == SYS_I_TT) {
        d = pg_put(d, "Test toast");
        dv = pg_put(dv, g_tt_kind == 0u ? "Friend request" : g_tt_kind == 1u ? "Invite" : g_tt_kind == 2u ? "Friends added" : g_tt_kind == 3u ? "Slippi Direct" : g_tt_kind == 4u ? "Room invite" : "Room join");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SYS && i == SYS_I_NETSET) {
        d = pg_put(d, "Server and Slippi");
        *d = 0;
        return;
    }
    if (idx == PG_SYS && i == SYS_I_RST) {
        d = pg_put(d, "Restart");
        dv = pg_put(dv, g_rst_until ? "press A again" : "");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SYS && i == SYS_I_SPEC) {
        d = pg_put(d, "Spectate upload");
        dv = pg_put(dv, ons_cur()->specoff ? "Off" : "On");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SYS && i == SYS_I_DBG) {
        d = pg_put(d, "Debug lines");
        dv = pg_put(dv, ons_cur()->dbgl ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
    if (idx == PG_SYS && i == SYS_I_PRE) {
        d = pg_put(d, "Menu preload");
        dv = pg_put(dv, ons_cur()->preload ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
#endif
    if (idx == PG_GAME) {
        const struct ms_settings* c = ons_cur();
        d = pg_put(d, "Progressive scan");
        if (!VI_DTV())
            dv = pg_put(dv, "needs a component cable");
        else if (CFG(CFG_PROG_TRIAL) && CFG(CFG_PROG_TRIAL) != PROG_TRIAL_END)
            dv = pg_put(dv, "testing");
        else
            dv = pg_put(dv, c->prog == MSET_PROG_ON ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
#if SYS_CTL
    if (idx == PG_SYS && i == 0) {
        d = pg_put(d, "Controller test");
        *d = 0;
        return;
    }
#endif
#if SYS_ENT
    if (idx == PG_SYS && i == SYS_I_ENT) {
        d = pg_put(d, "Entropy test");
        *d = 0;
        return;
    }
#endif
#ifdef MS_RB_RUNTIME
    if (idx == PG_SYS && i == SYS_I_RB) {
        d = pg_put(d, "Rollback readout");
        dv = pg_put(dv, ons_cur()->rbr ? "On" : "Off");
        *d = 0;
        *dv = 0;
        return;
    }
#endif
#endif
    {
        const char* l = ml_page_tab[idx].rows[i];
        int k = 0;
        while (l[k] && k < 48) {
            *d++ = l[k];
            k++;
        }
        *d = 0;
    }
    (void) dv;
}

#if ML_UI
/* ---- the Settings look of the Melee-text pages (ml_ui.h): geometry of the list ---- */
static int pg_big(int idx)                                   /* list rows in the big font, two grid rows apart; a lab Diagnostics page with many rows keeps one grid row each */
{
    return idx != PG_SYS || pg_nrows(idx) <= 4u;
}

static u32 pg_item_row(int idx, u32 i)                       /* the grid row of list item i */
{
    if (idx == PG_GAME)
        return UI_R_BODY + 2u * i;    /* 9 rows at 4, 6 .. 20, no heading */
    //    /* PICTURE at 4: rows 5 and 7; ONLINE at 9: rows 10, 12 and 14; ON SCREEN at 16 (a blank row above the heading like the others): rows 17 and 19 */
    if (idx == PG_NET && i >= RC_ROW)
        return i == RC_ROW ? NTG_ROW_RECONNECT : i == WF_ROW ? NTG_ROW_TEST + 2u * (u32) ML_NETTEST : NTG_ROW_TEST;           /* the actions right under Status: rows 6, 8, 10 (ml_nt_geom.h; Connection check removed) */
    if (idx == PG_SETTINGS && i >= (u32) SETK_NET)
        return UI_R_BODY + 2u * i + 2u;                              /* Network status (and lab Diagnostics): one empty row below About */
    if (idx == PG_SYS && pg_big(idx))
        return UI_R_BODY + 1u + 2u * i;                              /* TESTS at 4 */
    if (pg_big(idx))
        return UI_R_BODY + 2u * i;
    return UI_R_BODY + i;
}

static void pg_sec_build(int idx)                            /* section headings (one text object each: where they fit) */
{
    if (idx == PG_GAME) {
        (void) g_pg_sec;                                     /* Settings > Game has no heading since (nine rows) */
    } else if (idx == PG_SYS && pg_big(idx))
        g_pg_sec[0] = ui_text(UI_R_BODY, UI_A_SEC, "Tests", 0);
}

static void pg_note_set(const char* note)                    /* the loading page's line (PG_SIGN) or the page's note */
{
    pg_free(&g_pg_note);
    if (note && note[0])
        g_pg_note = ui_text(g_page == PG_SIGN ? UI_R_BODY : UI_R_NOTE, g_page == PG_SIGN ? UI_A_BODY : UI_A_SEC, note, 0);
}
#else
#define pg_note_set(n) ((void) 0)
#endif

static void pg_row_build(int idx, int i)
{
    char buf[110];
    char line[52];
    char val[52];
    pg_free(&g_pg_row[i]);
    pg_row_text(idx, i, line, val);
#if ML_NETTEST
    if (idx == PG_NET && NTG_HIDDEN((u32) i, g_pg_cur)) {       /* Test connection has the cursor: its notice or result lines stand where this row is (ml_nt_geom.h NTG_HIDDEN) */
        pg_free(&g_pg_val[i]);
        return;
    }
#endif
#if ML_UI
    if (idx == PG_SETTINGS || idx == PG_GAME || idx == PG_SYS || idx == PG_NET) {      /* the Settings look: bar behind the selected row (made first, so the text is drawn over it), value at the right margin */
        u32 r = pg_item_row(idx, (u32) i), sel = (u32) i == g_pg_cur;
        unsigned big = pg_big(idx) ? UI_A_BIG : 0u;
        unsigned a = sel ? UI_A_HI : (idx == PG_GAME && ((i == 1 && !VI_DTV()) || (i == 3 && ons_cur()->delay == MSET_DELAY_AUTO))) ? UI_A_DIM : UI_A_BODY;     /* a row that cannot be used (no component cable; Online delay while Auto delay is On) is grey */
        unsigned av = a;
        if ((idx == PG_SETTINGS && (u32) i == SETK_NET) || (idx == PG_NET && i == 0)) {    /* the network word: green online, red otherwise; neutral while reconnecting */
            unsigned w[16];
            int ns = net_state(w);
            av = ns == 3 ? UI_A_GOOD : ns == 4 ? UI_A_BODY : UI_A_BAD;
        }
        pg_free(&g_pg_val[i]);
        if (sel) {
            pg_free(&g_pg_bar);
            g_pg_bar = ui_bar(r, big ? 1u : 0u);
        }
        g_pg_row[i] = ui_text(r, a | big, line, 0);
        if (val[0])
            g_pg_val[i] = ui_text(r, av | big, val, 1);
        return;
    }
#endif
    buf[0] = (u32) i == g_pg_cur ? '-' : ' ';
    buf[1] = ' ';
    {
        char* e = pg_put(buf + 2, line);
        if (val[0]) {
            e = pg_put(e, ": ");
            e = pg_put(e, val);
        }
        *e = 0;
    }
    g_pg_row[i] = pg_line(((const u32a*) pg_y)[2 + i], (u32) i == g_pg_cur ? col_gold : col_white, buf);
}

#ifdef ML_SETTINGS
/* the one-line description of the row under the cursor (at most 44 characters, menu-font glyphs only; host-checked by ) */
#if !ML_UI
static const float pg_desc_y = 3.6f;
#endif
static const char* g_net_rc_msg;                                                       /* why Reconnect was refused (shown on the help line until the cursor moves) */
static int net_state_now(void) { unsigned w[16]; return net_state(w); }      /* the Status help line follows the state ('Online:...' read wrong while offline) */
static const char* ml_pg_desc_text(int idx, u32 i)
{
    if (idx == PG_SETTINGS)
        return i == SETK_ACC ? "Name, code, region, Slippi login and delete." : i == SETK_PRIV ? "Invites, friend requests and watching." : i == SETK_GAME ? "Picture and online delay."
             : i == SETK_UPDATE ? "Look for a newer Multishine build." : i == SETK_ABOUT ? "Version, licences and credits."
             : i == SETK_NET ? "Your connection and the Multishine server." : "Controller test, entropy test and readouts.";
    if (idx == PG_NET && i == RC_ROW)
        return g_net_rc_msg ? g_net_rc_msg : "Restart the network link and the server.";
    if (idx == PG_NET && i == WF_ROW)
        return "Only meet wired players. May wait longer.";           /* Wired opponents only: the same text on a wired and on a Wi-Fi console (a Wi-Fi player is matched with wired opponents too) */
    if (idx == PG_NET && ML_NETTEST && i == NT_ROW)
        return 0;                                                    /* the notice or the result lines take the rows under the list (nt_block_build) */
    if (idx == PG_NET)
        return i == 0 ? (net_why(net_state_now()) ? net_why_help[net_why(net_state_now())] : net_state_now() == 3 ? (ml_net_cost(116) ? "Wi-Fi: unsteady. A cable is best." : "Online: the Multishine server answers.") : net_state_now() == 4 ? "Reconnecting: adapter, address, server." : "Multishine servers unreachable.") : 0;
    if (idx == PG_GAME)
        return i == 0 ? "Wider picture for a 16:9 TV." : i == 1 ? "480p picture. Needs a component cable."
             : i == 2 ? "On: the delay follows your connection." : i == 3 ? "Used when Auto delay is Off." : i == 4 ? "The delay on screen in online matches." : i == 6 ? "Where Delay and Ping show in a match." : i == 7 ? "Lifts the HUD off a TV's edge." : i == 8 ? "Menu music. Random: any song each visit." : "Live ping to your opponent during the match.";
    if (idx == PG_SYS)
        return (SYS_CTL && i == 0) ? "Check the buttons and sticks."
             : (SYS_ENT && (int) i == SYS_I_ENT) ? "Check the random numbers behind your keys."
#ifdef ONS_NETSEL
             : (int) i == SYS_I_FPS ? "Frames per second, top left. Lab only."
             : (int) i == SYS_I_PRE ? "Load Friends, Standings, Game Night early."
             : (int) i == SYS_I_TT ? "Left Right: Kind  A: Show on menu"
             : (int) i == SYS_I_NETSET ? "Game server, Slippi matchmaking, in use."
             : (int) i == SYS_I_SPEC ? "Game Night: share your game with watchers."
             : (int) i == SYS_I_DBG ? "Network and wait text under the header."
             : (int) i >= SYS_I_NET ? "Frame thread time since boot."
             : (int) i == SYS_I_RST ? "Restart the console. A twice, B cancels."
#endif
             : "Show rollback numbers during matches.";
    return 0;
}
#if ML_PART_OVL
static void pg_desc_build(void)
{
    const char* s = g_page >= 0 ? ml_pg_desc_text(g_page, g_pg_cur) : 0;
    pg_free(&g_pg_desc);
#if ML_UI
    if (s)
        g_pg_desc = ui_text(UI_R_DESC, UI_A_DIM | UI_A_BIG, s, 0);
#else
    if (s)
        g_pg_desc = pg_line(*(const u32a*) &pg_desc_y, col_dim, s);
#endif
}
#endif
#else
#define pg_desc_build() ((void) 0)
#endif

static void pg_status_set(const char* msg)
{
    if (msg == g_pg_msg && g_pg_stat)
        return;
    pg_free(&g_pg_stat);
    g_pg_msg = msg;
#if ML_UI
    if (msg && (g_page == PG_GAME || g_page == PG_SYS))
        g_pg_stat = ui_text(UI_R_STAT, UI_A_DIM | UI_A_BIG, msg, 0);
    else
#endif
    if (msg)
        g_pg_stat = pg_line(((const u32a*) pg_y)[2 + PG_ROWS], col_dim, msg);
}

static void pg_status_build(void)
{
    pg_status_set(g_pg_status ? "Not available in this build." : 0);   /* ml.na */
}

#if ML_NETTEST
/* Test connection: while the cursor is on the row the rows 19 to 23 show the privacy notice (before and during a test) or the result
   (router, internet, lost, verdict in two lines).  The block is rebuilt only when the engine's state word, the cursor or the refusal changes. */
static void* g_nt_ln[5];
static u32 g_nt_sig = 0xFFFFFFFFu;
static u32 g_nt_run;
static const char* g_nt_msg;                                      /* why A did nothing */
static void nt_block_free(void)
{
    u32 k;
    for (k = 0; k < 5u; k++)
        pg_free(&g_nt_ln[k]);
}
static void* g_nt_fl[NTG_FACT_LINES];                              /* the Network details block above the help line (footer text, no longer an item) */
static void nt_facts_free(void)
{
    u32 k;
    for (k = 0; k < (u32) NTG_FACT_LINES; k++)
        pg_free(&g_nt_fl[k]);
}
static void nt_facts_build(void)                                    /* the four facts follow the connection: built when the page opens and again every second */
{
    unsigned w[16];
    int st = net_state(w);
    u32 k;
    nt_facts_free();
    if (g_pg_cur != NTG_I_STATUS)
        return;                                                     /* the facts are the footer of the Status row only; every other row shows its own help text alone */
    for (k = 0; k < (u32) NTG_FACT_LINES; k++) {
        char fl[28], fv[28], ln[56];
        char* e = ln;
        net_fact(k, st, w, fl, fv);
        e = pg_put(e, fl);
        if (fv[0]) {
            e = pg_put(e, ": ");
            e = pg_put(e, fv);
        }
        *e = 0;
        g_nt_fl[k] = nt_text_fact(k, ln);
    }
}
static void nt_block_build(void)
{
    u32 w = ml_net_cost(112), ts = w & 15u, k;
    nt_block_free();
    if (g_page != PG_NET || g_pg_cur != NT_ROW)
        return;
    if (ts == 2u || ts == 3u) {
        nt_leg_result r, n;
        char l[NTX_RESULT_LINES][NTX_LINE_MAX];
        int vd;
        u32 vcls;
        nt_results(&r, &n);
        ntx_lines(&r, &n, l);
        vd = ntx_verdict(&r, &n);
        vcls = vd == NTX_GOOD ? 1u : (vd == NTX_NOINET || vd == NTX_NOTHING) ? 3u : 2u;       /* green Good; amber Unsteady, Slow, Loses packets, No router; red No answer, Nothing */
        for (k = 0; k < (u32) NTX_RESULT_LINES; k++)
            if (l[k][0])
                g_nt_ln[k] = nt_text_at(k, k >= 3u ? vcls : 0u, l[k]);
        return;
    }
    for (k = 0; k < (u32) NTX_NOTICE_LINES; k++)
        g_nt_ln[k] = nt_text_at(k, 0u, ntx_notice(k));
    if (g_nt_msg)
        g_nt_ln[4] = nt_text_at(4u, 3u, g_nt_msg);                  /* the refusal under the notice (the fifth place) */
}
static void nt_foot(u32 run)                                      /* the footer states the keys: B cancels while a test runs */
{
    pg_free(&g_pg_foot);
    ft_grp = 0;
    g_pg_foot = ui_text(UI_R_FOOT, UI_A_DIM, run ? "B: Cancel" : "A: Select  B: Back", 0);
    ft_grp = 1;
}
static void nt_ui_sync(void)                                      /* once a frame while the Network status page is up */
{
    u32 w, sig, run;
    w = ml_net_cost(112);
    sig = w ^ (g_pg_cur << 20) ^ (g_nt_msg ? 0x10000000u : 0u);
    run = (w & 15u) == 1u ? 1u : 0u;
    if (sig == g_nt_sig)
        return;
    g_nt_sig = sig;
    pg_row_build(PG_NET, (int) NT_ROW);
    pg_row_build(PG_NET, (int) WF_ROW);
    nt_block_build();
    if (run != g_nt_run) {
        g_nt_run = run;
        nt_foot(run);
    }
}
static void nt_rows_sync(void)                                    /* the cursor moved: the rows its block takes over are redrawn at once (not a frame later) */
{
    pg_row_build(PG_NET, (int) WF_ROW);
    nt_block_build();
    nt_facts_build();                                              /* the facts stand only while the cursor is on Status */
}
#ifndef ML_OVL
extern int odl_save(const unsigned* w) __attribute__((weak));       /* on_dlog.c (-DONS_DLOG only): the network report file */
#define ml_odl_save(w) (odl_save ? odl_save(w) : -2)
#endif
static int nt_report(void)                                        /* the network report to the card: 1 saved */
{
    unsigned w[16];
    u32 i;
    for (i = 0; i < 16u; i++)
        w[i] = 0;
#if defined(ML_OVL) && !defined(SD_CARD_HOLE)
    return 0;
#else
    if (!ms_net_dbg)
        return 0;
    ms_net_dbg(w);
    return ml_odl_save(w) == 0;
#endif
}
static void nt_press_a(void)                                      /* A on Test connection */
{
    u32 r;
    int saved;
    if ((ml_net_cost(112) & 15u) == 1u) {
        SFX(0);
        return;
    }
    r = ml_net_cost(110);
    saved = nt_report();                                          /* every press leaves the report on the card (MultishineDiag, no secrets) */
    g_nt_msg = r == 1u ? "Not now: a search or a match is active." : r == 5u ? "The network is starting. Try again soon." : r == 3u ? (saved ? "No network. Report saved to card." : "No network. Check the cable.") : r == 4u ? "The test could not start." : 0;
    SFX(r ? 0 : 1);
    g_nt_sig = 0xFFFFFFFFu;
}
#endif
static void page_texts_free(void)       /* everything of the page but its black backdrop (page_close frees that too; the cover that waits for an Event Match screen keeps only it: texts are scarce) */
{
    int i;
    pg_free(&g_pg_title);
    pg_free(&g_pg_note);
    pg_free(&g_pg_stat);
    pg_free(&g_pg_foot);
#ifdef ML_SETTINGS
    pg_free(&g_pg_desc);
#endif
#if ML_NETTEST
    nt_block_free();
    nt_facts_free();
#endif
    for (i = 0; i < PG_NR; i++)
        pg_free(&g_pg_row[i]);
#if ML_UI
    pg_free(&g_pg_crumb);
    pg_free(&g_pg_bar);
    for (i = 0; i < PG_NR; i++)
        pg_free(&g_pg_val[i]);
    for (i = 0; i < 3; i++)
        pg_free(&g_pg_sec[i]);
    if (g_pg_mark)
        ui_free(g_pg_mark);
    g_pg_mark = 0;
#endif
}

static void page_close(void)
{
    page_texts_free();
    pg_free(&g_pg_back);
    g_page = -1;
    g_pg_msg = 0;
    ST(S_PAGE) = 0;
}

#if ML_UI
/* the breadcrumb and, for the loading page, the title of the screen about to open (the sign overlay's own screens take over the same header) */
static const char* pg_crumb_text(int idx, const char** title)
{
    if (idx == PG_SETTINGS)
        return "More - Settings";
    if (idx == PG_GAME)
        return "More - Settings - Game";
    if (idx == PG_SYS)
        return "More - Settings - Diagnostics";
    if (idx == PG_NET) {
        *title = "Network status";
        return "More - Settings - Network status";
    }
    if (idx == PG_SIGN) {                                       /* loading page: names what is loading */
        if (g_ut_hold) { *title = "Update"; return ""; }
#if SYS_CTL
        if (g_sign_ctl) {
            *title = "Controller test";
            return "More - Settings - Diagnostics - Controller test";
        }
#endif
        if (g_sign_goto == SG_ABOUT + 1u) {
            *title = "About";
            return "More - Settings - About";
        }
#if SYS_ENT
        if (g_sign_goto == SG_ENT + 1u) {
            *title = "Entropy test";
            return "More - Settings - Diagnostics - Entropy test";
        }
#endif
#if ML_GN_HANDOFF
        if (g_gn_open && g_gn_scr) {
            *title = "Loading";
            return "";
        }
        if (g_gn_open || (g_sign_goto && ((g_sign_goto - 1u) & 0xFFu) == (u32) SG_GN)) {
#else
        if (g_sign_goto && ((g_sign_goto - 1u) & 0xFFu) == (u32) SG_GN) {
#endif      /* the cover while the browser reopens after a set */
            *title = "Loading";             /*: it read "Game Night" after a TOURNAMENT game (the same hand-off carries cup, Grandmaster and Game Night sets): no mode name on the cover */
            return "";
        }
        if (g_sign_goto == 0u || (g_sign_goto - 1u) & SG_GOTO_PAGE) {
            u32 p = g_sign_goto ? ((g_sign_goto - 1u) >> 16) : 1u;
            *title = p == 2u ? "Friends" : p == 3u ? "Delete" : "Account";
            return "";
        }
        *title = "Loading";
        return "";
    }
    return "";
}
#endif

static void page_open(u32 idx)
{
    u32* t;
    int i, k;
    const char* title;
    const char* note;
    const char* foot = "A: Select  B: Back";   /* ml.foot (no "Up Down: Row":) */
#ifdef ML_SETTINGS
    if ((int) idx != PG_SIGN && (int) idx != PG_GAME && (int) idx != PG_SYS && (int) idx != PG_NET)
#endif
    if (idx >= ml_page_n)
        return;
    if (g_page >= 0)
        return;
    title = "";
    note = "";
#ifdef ML_SETTINGS
    if ((int) idx == PG_NET) {
        title = "Network status";
        note = "";
    } else if ((int) idx == PG_GAME) {
        title = "Game";
        note = "";                                       /* no "Changes are saved on the memory card." line */
        foot = "A: Change  B: Back";
    } else if ((int) idx == PG_SYS) {
        title = "Diagnostics";
        note = "";
#if SYS_CTL
        foot = SYS_RB + SYS_DEV ? "A: Change  B: Back" : "A: Select  B: Back";
#else
        foot = "A: Change  B: Back";
#endif
    } else if ((int) idx == PG_SIGN) {
        title = "Settings";
        note = g_pg_note_txt ? g_pg_note_txt : "";
        foot = g_ut_hold ? "B: Skip" : "B: Back";
    } else
#endif
    {
        title = ml_page_tab[idx].title;
        note = ml_page_tab[idx].note;
    }
    drop_all(1);                                   /* the hub's labels and cover go: the page covers the menu */
    SFX(1);
    g_page = (int) idx;
    g_page_kind = B(MENU_FLOW);
    g_pg_cur = 0;
#ifdef ML_SETTINGS
    g_pg_cur = g_pg_start_cur < pg_nrows((int) idx) ? g_pg_start_cur : 0;
    g_pg_start_cur = 0;
    g_pg_msg = 0;
#endif
    g_pg_status = 0;
    ST(S_PAGE) = idx + 1u;
#ifdef ML_SETTINGS
    ST(S_PG_IDX) = idx;
#endif
    ST(S_PAGE_OPENS)++;
    t = text_new_at(F_BG_X, F_BG_Y, F_PG_SIZE, col_white);        /* backdrop: an empty text whose background box darkens the menu behind */
    if (t) {
        const u8* bg = PGL_BG();                                   /* see-through over the globe when it is up */
        t[3] = F_BG_W;
        t[4] = F_BG_H;
        for (k = 0; k < 4; k++)
            ((u8*) t)[0x2C + k] = bg[k];
        ml_sis_add(t, zero_xy, "%s", " ");
    }
    g_pg_back = t;
#if ML_UI
    if ((int) idx == PG_SETTINGS || (int) idx == PG_GAME || (int) idx == PG_SYS || (int) idx == PG_SIGN || (int) idx == PG_NET) {     /* the Settings look */
        const char* crumb = pg_crumb_text((int) idx, &title);
        if (crumb[0])                                    /* the mark only on Settings pages (a loading page for Game Night, Friends or Standings has none) */
            g_pg_mark = ui_mark();
        g_pg_title = ui_text(UI_R_TITLE, UI_A_TITLE, title, 0);
        (void) crumb;                                    /* no breadcrumb; only the loading page's title is taken from pg_crumb_text */
        pg_sec_build((int) idx);
        pg_note_set(note);
        for (i = 0; i < (int) pg_nrows((int) idx) && i < PG_NR; i++)
            pg_row_build((int) idx, i);
        pg_desc_build();
#if ML_NETTEST
        g_nt_sig = 0xFFFFFFFFu;
        g_nt_msg = 0;
        g_nt_run = 0;
        if (idx == PG_NET)
            nt_facts_build();                            /* the Network details block, always shown */
#endif
        ft_grp = 0;
        g_pg_foot = ui_text(UI_R_FOOT, UI_A_DIM, foot, 0);
        ft_grp = 1;
        H(MENU_COOLDOWN) = 2;
        return;
    }
#endif
    g_pg_title = pg_line(((const u32a*) pg_y)[0], col_white, title);
    g_pg_note = pg_line(((const u32a*) pg_y)[1], col_dim, note);
    for (i = 0; i < (int) pg_nrows((int) idx) && i < PG_ROWS; i++)
        pg_row_build((int) idx, i);
    pg_desc_build();
    g_pg_foot = pg_line(((const u32a*) pg_y)[PG_ROWS + 3], col_dim, foot);
    H(MENU_COOLDOWN) = 2;
}

#endif /* ML_PART_OVL */

#ifdef ML_SETTINGS
#if ML_PART_RES
/* ---- settings: apply, status text ---- [RES] */
static u32 ws_rd(void* ctx, u32 a)
{
    (void) ctx;
    return W(a);
}
static void ws_wr(void* ctx, u32 a, u32 w)
{
    (void) ctx;
    W(a) = w;
    DC_STORE((void*) a, 4);
    IC_INVAL((void*) a, 4);                        /* the words are code (hook branches) or float constants: flush both caches, harmless for data */
}

/* Progressive scan: the picture mode follows ms_prog_plan(stored setting, cable, console flag, live mode).  While a test from the Game page runs (CFG_PROG_TRIAL) the stored
   setting is not looked at; when it ends without A (PROG_TRIAL_END) the picture goes back to interlaced.  The console flag is cleared only when the On is safe on the card
   (loaded as 1, or saved after the adoption), so a failed card write never loses the choice silently into a state without prompt AND without card. */
static void prog_live(u32 on)
{
    MELEE_PROG = on;
    GM_VIDEO_CONF();
    CFG(CFG_PROG_LIVE)++;
}

/* A Wii (Nintendont's kernel writes 243 MHz into the OS bus clock word; GameCube and Dolphin 162 MHz) gets its one live picture switch (a stored On only) in the boot scene (ml_prog_early),
   never on the title or later: if the stored setting is not read in time the picture stays as it is; the Settings layer of the menus never switches the picture there again. */

static void prog_apply_ex(const struct ms_settings* c, u32 early)
{
    u32 trial = CFG(CFG_PROG_TRIAL), dtv = VI_DTV() != 0u, sram = dtv && OS_PROG_GET() == 1, live = MELEE_PROG != 0u, plan;
    CFG(CFG_PROG_STAT) = dtv | (sram << 1) | (live << 2) | ((u32) c->prog << 4);
    if (trial == PROG_TRIAL_END) {                 /* the test ended without A */
        CFG(CFG_PROG_TRIAL) = 0;
        if (live)
            prog_live(0u);
        return;
    }
    if (trial) {                                   /* a test is running: show the picture, decide nothing */
        if (dtv && !live)
            prog_live(1u);
        return;
    }
    plan = ms_prog_plan(c->prog, dtv, sram, live);
    if (early) {                                   /* the boot scene (hub_res.c, never later): only a stored On that is not live yet switches the picture, the way Melee's own prompt answered Yes does; Off, unset and "already as wanted" never touch the video.  The card write and the console flag stay with the menu's pass */
        if (plan & MP_LIVE_ON)
            prog_live(1u);
        return;
    }
    if (!PROG_WII()) {
        if (plan & MP_LIVE_ON)
            prog_live(1u);
        if (plan & MP_LIVE_OFF)
            prog_live(0u);
    } else if (plan & MP_LIVE_OFF)                 /* a Wii never switches ON here (boot scene only), but a stored Off with the picture live progressive goes back to Melee's own interlaced mode: Off restores the stock picture */
        prog_live(0u);
    if (plan & MP_ADOPT)
        (void) ons_change_prog(1u, ST(S_FRAME));
    if (plan & MP_CLEAR_SRAM)
        CFG(CFG_PROG_PLAN) |= 0x100u;              /* settings_tick clears the console flag once the card holds the On */
    CFG(CFG_PROG_PLAN) = (CFG(CFG_PROG_PLAN) & 0x100u) | plan;
}

#define prog_apply(c) prog_apply_ex((c), 0u)

/* MUSIC-ONLY MUTE (Dolphin audio dump of Music Off also silenced the sound effects).  Melee streams every music track (menus, title, stages) as ONE synth
   voice on synth channel 1 (lbAudioAx_80023F28 -> fn_80023ED4(stem, 127, 1) -> AXDriver_8038E8EC -> HSD_Synth_8038B5AC(.., channel 1)); every sound effect plays on channels 2..8
   (fn_80023750 callers), and lbAudioAx sets only channels 2..7 (fn_80024654).  Off = channel 1's volume 0 through the game's own HSD_SynthSFXUpdateAllVolume(vol, fade, channel)
   (0x8038AAB0: the AX callback applies it within one 5 ms frame, also to a track that is already playing, and to every later track); On / Random = 255.  The start-volume word
   0x80023FFC stays retail (`li r4,127`: nothing is restarted) and the live stream volume lbl_804D387C is no longer used for
   Music (it scales EVERY voice); a 0 left in it is put back to 127.  settings_apply and ml_music_early share this one copy. */
static void music_word(u32 off) __attribute__((noinline));
static void music_word(u32 off)
{
    ws_wr(0, 0x80023FFCu, MS_MUSIC_WORD(0));
    if (*(volatile int*) 0x804D387Cu == 0)
        *(volatile int*) 0x804D387Cu = 0x7F;
    ((void (*)(int, int, int)) 0x8038AAB0u)(off ? 0 : 255, 0, 1);
}

#ifdef ML_OVL_RES
/* hub_res.c, in the boot scene only, once the stored options record has been read (ons_title_final): a stored On is applied now, before the title appears; a late read is skipped.  Once per boot (the caller's ut_prog_done). */
void ml_prog_early(void)
{
    /* no 'done' flag: hub_res.c calls this once (ut_prog_done is set before the call, and 0x28 is the boot scene only) */
    prog_apply_ex(ons_cur(), 1u);
}

/* hub_res.c, once the options record is final, in the boot scene or (a late read) on the title: the stored Music Off before the next track starts (7):
   the start-volume word AND the live stream volume, so a track that already began (the title's) is muted at once.  Once per boot (the later settings_apply writes the same). */
void ml_music_early(void)
{
    static u32 done;
    const struct ms_settings* c = ons_cur();
    if (done)
        return;
    done = 1;
    music_word(c->musicoff);
}
#endif

/* In-match "Delay: %df" HUD text (codeset flag ms-hud-delay): Slippi's InitInGame C2 body (hook ML_IGD_HOOK) carries a flag word at body+8; 0 = no delay text (default), 1 = shown.
   It follows Settings > Show technical info (the Debug Lines bit) and is written at every settings_apply (menu scene entry, every change of the switch).  Slippi's own flow follows the
   same switch.  The body is found through the hook word and accepted only with the generated header (`blrl` at body+4, 'MSHD' at body+12); any other codeset: nothing is written. */
/* ML_IGD_POS_*: where Slippi's Delay text (and our Ping line, on_bhold.c ping_place) stand.  Canvas units (about pixels of 640x480, origin at the screen centre, y DOWN): Default = Slippi's own place
   (right edge x 270, y 207: bottom right); above / below the timer: centred on the timer's x, the Delay line left of the Ping line in one row (Delay right edge -7 with Ping on, +36 alone;
   Ping right edge +79 with Delay on, +44.5 alone; centre on the timer, was -20/+23/+66/+36), y -229 (just over the timer) or -171 (under the timer's digits; was -191, which overlapped them), both texts without a backing box at size 0.42 (Slippi's 0.33 before, ML_IGD_SIZE_BIG). */
static const u32 igd_xs[4] = {ML_IGD_X_STD, ML_IGD_X_MOVED, ML_IGD_XC_ALONE, ML_IGD_XC_PING};    /* [(pos ? 2 : 0) + Ping on] */
static const u32 igd_ys[3] = {ML_IGD_Y, ML_IGD_Y_ABOVE, ML_IGD_Y_BELOW};
static u32 igd_has(const u32* t, u32 n, u32 v)
{
    while (n--)
        if (t[n] == v)
            return 1u;
    return 0u;
}
static u32 igd_body(u32 hook, u32 magic)           /* the generated body of a codeset flag module (blrl at body+4, its magic at body+12), or 0 */
{
    u32 body = ml_css_body(hook, W(hook));
    return body && W(body + 4u) == ML_CSS_BLRL && W(body + 12u) == magic ? body : 0u;
}
extern u32 bh_note_pool[];                                           /* on_bhold_sis.S: the ping line's own (x, y) words [6], [7], read when the line is made */
static void igd_delay_mark(u32 on, u32 ping, u32 pos)
{
    bh_note_pool[6] = pos == 0u ? ML_IGD_X_STD : on ? ML_IGD_PX_PING : ML_IGD_PX_ALONE;     /* the ping line's right edge: 270.0 (Slippi's Delay place) / 79.0 right of the Delay text / 44.5 alone (centred on the timer, ml_core.h) */
    bh_note_pool[7] = igd_ys[pos];
    bh_note_pool[9] = ML_IGD_SIZE_BIG;                                  /* the ping line's size: 0.42 (was Slippi's 0.33, more readable without the backing box) */
    u32 body = igd_body(ML_IGD_HOOK, ML_IGD_MAGIC);
    if (body) {
        u32 a;
        B(body + 11u) = (u8) (on ? 1u : 0u);
        for (a = body + 16u; a < body + 16u + ML_IGD_SCAN; a += 4u) {      /* Slippi's data block: the float pair (x, y) of the delay text (right aligned at x); found by value, not by offset: any of our own values counts */
            if (igd_has(igd_xs, 4u, W(a))) {
                W(a) = igd_xs[(pos ? 2u : 0u) + (on && ping ? 1u : 0u)];               /* both lines on: Delay moves left to make room for the Ping line to its right, one row */
                W(a + 4u) = igd_ys[pos];
                break;
            }
        }
        for (a = body + 16u; a < body + 16u + ML_IGD_SCAN; a += 4u)        /* the delay text's size float (Slippi's): found by value, 0.42 afterwards (re-runs find it again) */
            if (W(a) == ML_IGD_SIZE_STD || W(a) == ML_IGD_SIZE_BIG) { W(a) = ML_IGD_SIZE_BIG; break; }
    }
}

/* Raise HUD (codeset flag ms-hud-raise, Offline/Hud/RaiseHud.asm, hook ML_HUR_HOOK = the last instruction of ifAll_802F343C): flag byte at body+11 = 0 Off, 1 Small, 2 Medium, 3 Large; blrl at body+4,
   'MSHR' at body+12.  Written at every settings_apply, read by the module each time the match fills the HUD position table. */

/* The HUD marks alone (Delay text flag + position pair, the Ping line's place, the Raise HUD level), from the settings in RAM; nothing else (no widescreen words, no delay, no progressive).
   settings_apply runs it, and the engine's per-frame tick re-runs it (on_bhold.c bh_ping_tick: every frame outside the match, once on its first pass), so the marks are written again right
   before every match whatever reset the module words or ran in between (tester ESTT@860 Delay text, Ping line and the HUD settings did nothing in Slippi Unranked / Direct
   games while they worked in our modes; the cause was not found without a console).  Idempotent: rewriting the same values is harmless (data words, read by loads only). */
void ml_hud_apply(void)
{
    const struct ms_settings* c = ons_cur();
    u32 hb = igd_body(ML_HUR_HOOK, ML_HUR_MAGIC);              /* Raise HUD (flag byte at body+11 = 0 Off, 1 Small, 2 Medium, 3 Large): the codeset module reads it at the next match */
    igd_delay_mark(c->dbgl, c->pingon, c->hudpos);             /* the in-match "Delay: %df" text only with Show technical info on, at the chosen position */
    if (hb)
        B(hb + 11u) = (u8) (c->hudraise & 3u);
}

#ifdef ML_OVL_RES
#define settings_apply ml_settings_apply            /* [RES] external: the dispatcher in ml_res.c and the overlay (through struct mlo_env) call it */
#define settings_mirror ml_settings_mirror
void settings_apply(void)
#else
static void settings_apply(void)
#endif
{
    const struct ms_settings* c = ons_cur();
    ms_ws_apply((const unsigned*) ml_ws_tab, c->ws, ws_rd, ws_wr, 0);     /* (the S_WS_CHANGED count was dropped nothing read it; engine bytes for the Music boot apply) */
    ST(S_WS_ON) = c->ws;
    music_word(c->musicoff);                                           /* Music: channel 1 (the music stream) 0 for Off, 255 for On / Random; the same value again is harmless */
    ENGINE_DELAY = c->delay ? c->delay : AD_SW;                       /* read by the engine at the next match setup (s7_core b3), both backends */
    ml_hud_apply();                                /* the Delay text, Ping line place and Raise HUD marks (also re-run by the engine's tick before every match) */
#ifdef MS_RB_RUNTIME
    {
        extern u32 rbr_enable;                     /* s12_gc.c: the rollback readout runs only while this is 1 (rbread=rt builds) */
        rbr_enable = c->rbr;
        CFG(CFG_RBR) = c->rbr;
    }
#endif
    prog_apply(c);
}

/* Melee single-player records, console side (the records go INTO Melee's own save, loaded once if they do not exist, then not
   touched).  Once per card: when the settings record is final (so the marker can be read) and Melee's card layer is idle, ms_rec_seed (ml_set.c) fills the empty slots of Melee's own save in
   RAM (*0x804D3EE0 + 0x1868 = GmSaveData, the same struct ml_unlock_all uses); Melee's own save is then requested the way its option pages do it (lbCardGame_UpdatePowerTime sets the
   save-requested word, lb_8001CC84 writes the whole save with Melee's own checksums), and only after that save was seen to run to the end without a failure the marker (bit 5 of the
   Multishine options byte 16, never Melee's save) is stored.  A failed or never-run save is retried (3 requests); a crash or a power cut in between leaves no marker, and the next
   boot seeds again, which is harmless: filled slots are never touched, so it finds nothing to write and just sets the marker. */
#define REC_SAVE_PTR 0x804D3EE0u
#define REC_GSD 0x1868u
#define REC_LBCN 0x80432A68u                   /* lbcardnew state (same offsets as on_hist.c omc_melee_idle) */
#define REC_LBCG 0x80433318u                   /* lbcardgame state: +8 card_status, +C save requested, +0x10 save running, +0x14 failed, +0x18 enable */
#define REC_CARD_BLOCK 0x804A8B38u
#define REC_REQUEST_SAVE ((fn_void) 0x8001CE00u)   /* lbCardGame_UpdatePowerTime: adds the play time and sets the save-requested word (asserts the card scene's enable word) */
#define REC_WAIT 0u
#define REC_SAVING 1u
#define REC_DONE 2u
#define REC_GAVE_UP 3u
#define REC_TIMEOUT 1500u                      /* menu frames a save may take before it counts as a failed attempt */
static u32 g_rec_t0;
static int rec_melee_idle(void)
{
    int i;
    if (W(REC_LBCN + 0x80u) || W(REC_LBCN + 0x8ACu))
        return 0;
    for (i = 0; i < 11; i++)
        if (W(REC_LBCN + 0x510u + 0x54u * (u32) i) != 14u)
            return 0;
    if (W(REC_LBCG + 0xCu) || W(REC_LBCG + 0x10u))
        return 0;
    if (W(REC_CARD_BLOCK) || W(REC_CARD_BLOCK + 0x110u))
        return 0;
    return 1;
}

static void rec_tick(void)
{
    u32 w = CFG(CFG_REC), st = w & 15u, save, f = ST(S_FRAME);
    const struct ms_settings* c = ons_cur();
    if (st >= REC_DONE || !ST(S_ENTER))
        return;
    if (!(ons_st.flags & 1u) || (ons_st.flags & 0x1Cu))            /* the record is read (final) and is neither newer, damaged nor given up on: the marker can be stored */
        return;
    if (ons_st.load_status != ONS_L_OK && ons_st.load_status != ONS_L_NOFILE)
        return;
    if (c->recseed) {
        CFG(CFG_REC) = (w & ~15u) | REC_DONE;
        return;
    }
    save = W(REC_SAVE_PTR);
    if (save < 0x80000000u || save > 0x81800000u)
        return;
    if (st == REC_WAIT) {
        u32 t = 0, h = 0;
        if ((ons_st.flags & 2u) || !rec_melee_idle() || W(REC_LBCG + 0x18u) != 1u || W(REC_LBCG + 8u) || W(REC_LBCG + 0x14u))
            return;
        if (!ms_rec_seed((unsigned char*) (save + REC_GSD), &t, &h)) {      /* nothing empty: the player (or an earlier run) has them all */
            (void) ons_change_rec(1u, f);
            CFG(CFG_REC) = REC_DONE;
            return;
        }
        REC_REQUEST_SAVE();
        g_rec_t0 = f;
        CFG(CFG_REC) = REC_SAVING | (1u << 4) | ((t & 0xFFu) << 8) | ((h & 0xFFu) << 16);
        return;
    }
    if (W(REC_LBCG + 0x10u))                                        /* the write is running */
        w |= 1u << 24;
    if (W(REC_LBCG + 0xCu) || W(REC_LBCG + 0x10u)) {
        if (f - g_rec_t0 < REC_TIMEOUT) {
            CFG(CFG_REC) = w;
            return;
        }
    } else if ((w >> 24 & 1u) && !W(REC_LBCG + 0x14u) && !W(REC_LBCG + 8u) && rec_melee_idle()) {
        (void) ons_change_rec(1u, f);                               /* the save ran to the end: now the marker */
        CFG(CFG_REC) = (w & ~15u) | REC_DONE;
        return;
    }
    if (((w >> 4) & 15u) >= 3u) {                                   /* three requests without a clean save: leave it to the next boot */
        CFG(CFG_REC) = (w & ~15u) | REC_GAVE_UP;
        return;
    }
    if (!W(REC_LBCG + 0xCu) && !W(REC_LBCG + 0x10u) && !W(REC_LBCG + 0x14u) && !W(REC_LBCG + 8u)) {
        REC_REQUEST_SAVE();
        g_rec_t0 = f;
        w = (w & ~(15u << 4 | 1u << 24)) | ((((w >> 4) & 15u) + 1u) << 4);
    } else {                                                        /* still running after the time-out: leave it alone, the next boot tries again */
        w = (w & ~15u) | REC_GAVE_UP;
    }
    CFG(CFG_REC) = w;
}

/* once per menu frame (after ons_poll): the progressive scan test countdown and the deferred clear of the console flag */
#ifdef ML_OVL_RES
#define settings_tick ml_settings_tick
void settings_tick(void)
#else
static void settings_tick(void)
#endif
{
    u32 t = CFG(CFG_PROG_TRIAL);
    if (t && t != PROG_TRIAL_END && --t == 0)
        t = PROG_TRIAL_END;
    if (t != CFG(CFG_PROG_TRIAL)) {
        CFG(CFG_PROG_TRIAL) = t;
        if (t == PROG_TRIAL_END)
            settings_apply();
    }
    if (CFG(CFG_PROG_PLAN) & 0x100u) {
        const struct ms_settings* c = ons_cur();
        if (c->prog == MSET_PROG_ON && !(c->touched & MSET_T_PROG) && VI_DTV() != 0) {     /* On is what the card holds (loaded, or saved: a save clears 'touched') */
            OS_PROG_SET(0);
            CFG(CFG_PROG_PLAN) &= ~0x100u;
        }
    }
    rec_tick();
}

#ifdef ML_OVL_RES
void settings_mirror(void)
#else
static void settings_mirror(void)
#endif
{
    ST(S_SET_LOAD) = ons_st.load_status;
    ST(S_SET_SAVE) = ons_st.save_status;
    ST(S_SET_DELAY) = ons_st.delay;
    ST(S_SET_POOL) = ons_st.pool;
    ST(S_SET_SAVES) = ons_st.saves;
    ST(S_SET_SLOT) = ons_st.slot;
    ST(S_SET_FLAGS) = ons_st.flags;
    ST(S_SET_TRIES) = ons_st.load_tries;
    ST(S_ENG_DELAY) = ENGINE_DELAY;
}
#endif /* ML_PART_RES */
#if ML_PART_OVL

/* the status line of the Online page, from the card layer's state */
static const char* online_status(void)
{
    if (g_page == PG_GAME) {
        u32 t = CFG(CFG_PROG_TRIAL);
        if (t && t != PROG_TRIAL_END)
            return "A keeps this picture. B or waiting goes back.";
    }
    if (ons_st.flags & 4u)
        return "Card file is from a newer version: not saved.";
    if (ons_st.flags & 16u)
        return "Memory card error: not saved.";
    if (ons_st.flags & 2u) {
        if (ons_st.save_status == ONS_W_NOCARD || (ons_st.load_status == ONS_L_NOCARD && !(ons_st.flags & 1u)))
            return "No memory card: changes last until power off.";
        if (ons_st.save_status == ONS_W_FULL || ons_st.save_status == ONS_W_RESERVE)
            return "Memory card full: not saved.";
        return 0;                                 /* "Will be saved on the memory card." is not said: only problems are */
    }
    if (ons_st.saves)
        return 0;                                 /* nor "Saved on the memory card in slot A." */
    if (ons_st.load_status == ONS_L_NOCARD)
        return "No memory card: changes last until power off.";
    return 0;
}

/* the 10 s progressive scan test (the Game page's row, and the first-run question's Try it): the test picture is shown, A on the Game page keeps it */
static void prog_trial_start(void)
{
    CFG(CFG_PROG_TRIAL) = PROG_TRIAL_FRAMES;
    settings_apply();
}

/* the first-run progressive scan question: only for an unset byte, a component cable and a picture that is still interlaced (a mode the player chose in Melee's own prompt is adopted instead) */
__attribute__((unused)) static u32 prog_first_run_ask(void)
{
    const struct ms_settings* c = ons_cur();
    return ms_prog_ask(ons_st.load_status == ONS_L_OK || ons_st.load_status == ONS_L_NOFILE, c->prog, VI_DTV() != 0u, MELEE_PROG != 0u);
}

#if defined(UPD_TITLE) && defined(HUB_OVL_RES) && defined(ML_SIGN)
#ifndef ML_OVL
extern int fr_prog_menu_frame(unsigned int kind, int settled, unsigned int sign_state, int due);
#endif
/* the first step of the first run: while the stored options record is not read yet the later first-run steps (system files, link offer) wait for it, so the question can
   come first; they stop waiting after 10 s (a card that never answers must not hold the first run back) */
static u32 g_pq_wait;
static u32 prog_load_pending(void)
{
    if (ons_st.load_status != ONS_L_NONE && ons_st.load_status != ONS_L_BUSY)
        return 0u;
    return g_pq_wait++ < 600u;
}

/* 1 = open the progressive scan question now (hub_res.c fr_prog_menu_frame: the same moment rules as the first-run offer, once per boot, no link or network conditions) */
static int pq_frame(unsigned int kind, int settled)
{
    int due = !prog_load_pending() && prog_first_run_ask() != 0u;
#ifdef ML_OVL
    if (!E->card || !E->card->fr_prog_menu_frame)
        return 0;                                 /* an older resident without the step: the question is simply not asked */
    return E->card->fr_prog_menu_frame(kind, settled, g_sign_state, due);
#else
    return fr_prog_menu_frame(kind, settled, g_sign_state, due);
#endif
}
#endif

static void online_change(int row, int dir)
{
    const struct ms_settings* c = ons_cur();
    u32 f = ST(S_FRAME);
    int ch;
    if (g_page == PG_GAME && row == 0) {           /* widescreen */
        ch = ons_change_ws(c->ws ? 0u : 1u, f);
        if (ch)
            settings_apply();
        (void) dir;
    } else if (g_page == PG_GAME && row == 4) {    /* show technical info: the Debug Lines bit, applies at once (nd_tick), saved with the other options */
        ch = ons_change_dbg(c->dbgl ? 0u : 1u, f);
        if (ch)
            settings_apply();                      /* the delay HUD text of a match follows this switch (igd_delay_mark) */
        (void) dir;
    } else if (g_page == PG_GAME && row == 8) {    /* Music On / Random / Off (shown and cycled in that order): Off is one word of the game's music start (settings_apply), Random gives the three menu slots (menu01/02/3) songs drawn from the 38 menu and stage tracks (music_pool_sync, next frame; the stage slots themselves are never written); the track that is playing ends at the next scene */
        u32 cur = c->musicoff ? 2u : c->musicrand ? 1u : 0u;                       /* the screen order */
        u32 nxt = (u32) ((int) cur + (dir < 0 ? -1 : 1) + 3) % 3u;
        ch = ons_change_music(nxt == 0u ? 1u : nxt == 1u ? 2u : 0u, f);            /* the setter's values: 1 On, 2 Random, 0 Off */
        if (ch)
            settings_apply();
    } else if (g_page == PG_GAME && row == 5) {    /* show ping: its own bit, applies at once (the match line follows ons_cur()->pingon; the delay text moves aside, igd_delay_mark) */
        ch = ons_change_ping(c->pingon ? 0u : 1u, f);
        if (ch)
            settings_apply();
        (void) dir;
    } else if (g_page == PG_GAME && (row == 6 || row == 7)) {   /* Ping and delay position / Raise HUD: cycle, applied at once (igd_delay_mark, hudr_mark) */
        u32 n = row == 6 ? 3u : 4u, cur = row == 6 ? c->hudpos : (c->hudraise & 3u);
        u32 nv = (u32) ((int) cur + (dir < 0 ? -1 : 1) + (int) n) % n;
        if (row == 7)
            nv |= c->hudraise & 4u;                     /* keep Wired opponents only (bit 2) */
        ch = ons_change_hud(row == 6 ? 0u : 1u, nv, f);
        if (ch)
            settings_apply();
    } else if (g_page == PG_GAME && row == 2) {    /* Auto delay On / Off: Off plays the remembered manual delay, On gives the choice back to the engine */
        (void) dir;
        ch = ons_change_delay(c->delay ? MSET_DELAY_AUTO : c->dman, f);
        if (ch)
            settings_apply();
    } else if (g_page == PG_GAME && row == 3) {    /* online delay (moved here from the old Online page): only while Auto delay is Off, grey and fixed otherwise */
        ch = c->delay ? ons_change_delay((u32) ((int) c->delay - 1 + (dir < 0 ? -1 : 1) + 15) % 15u + 1u, f) : 0;   /* 1f .. 15f, wraps; never Auto here (the switch above does that) */
        if (ch)
            settings_apply();
    } else if (g_page == PG_GAME) {                /* progressive scan: A / Left / Right on a test keeps the picture; On -> Off at once; Off -> a 10 s test first */
        u32 trial = CFG(CFG_PROG_TRIAL);
        (void) dir;
        ch = 0;
        if (!VI_DTV()) {
            ch = 0;                                 /* no component cable: nothing to switch (the row says so) */
        } else if (trial && trial != PROG_TRIAL_END) {
            CFG(CFG_PROG_TRIAL) = 0;                /* kept */
            ch = ons_change_prog(1u, f);
            settings_apply();
            ch = 1;
        } else if (c->prog == MSET_PROG_ON) {
            ch = ons_change_prog(0u, f);
            if (!ch)
                ch = 1;
            settings_apply();
        } else {
            prog_trial_start();                     /* the test picture */
            ch = 1;
        }
    } else {                                       /* Diagnostics: rollback readout (row SYS_CTL; row 0 is the Controller test, opened by A in page_frame) */
#if SYS_FIRST
        if (row < SYS_FIRST)
            return;                                /* Controller Test and Entropy Test open with A (page_frame) */
#endif
#ifdef ONS_NETSEL
        if (row == SYS_I_TT) {                     /* Left-Right: the kind of the test toast (A raises it, page_frame) */
            g_tt_kind = (g_tt_kind + (dir < 0 ? 5u : 1u)) % 6u;
            ch = 1;
        } else if (row == SYS_I_SPEC) {            /* Spectate Upload: applies at the next Game Night set (the resident loader reads gns_off), saved with the other options */
            ch = ons_change_spec(c->specoff ? 1u : 0u, f);
        } else if (row == SYS_I_DBG) {             /* Debug Lines: applies at once (nd_tick), saved with the other options */
            ch = ons_change_dbg(c->dbgl ? 0u : 1u, f);
            if (ch)
                settings_apply();                  /* the delay HUD text of a match follows this switch (igd_delay_mark) */
        } else if (row >= SYS_I_NETSET)
            return;                                /* Server And Slippi opens with A (page_frame); the Net cost rows are read-only */
        else if (row == SYS_I_RST) {                    /* Restart: the first A arms it for 3 s, the second restarts like the Update screen does */
            if (g_rst_until && ST(S_FRAME) < g_rst_until) {
                ons_flush(f);
                soft_reset();
            }
            g_rst_until = ST(S_FRAME) + 180u;
            ch = 1;
        } else if (row == SYS_I_FPS) {                    /* frame rate readout (lab/dev builds): RAM only, nothing to save */
            g_fps_on = g_fps_on ? 0u : 1u;
            ch = 1;
        } else if (row == SYS_I_PRE) {             /* menu preload: applies at once (ev_keep_tick reads it), saved with the other options */
            ch = ons_change_preload(c->preload ? 0u : 1u, f);
        } else
#endif
        {
            ch = ons_change_rbr(c->rbr ? 0u : 1u, f);
            if (ch)
                settings_apply();
        }
        (void) dir;
    }
    if (ch) {
        ST(S_SET_CHANGES)++;
        SFX(2);
        pg_row_build(g_page, row);
    }
}
#endif /* ML_PART_OVL */
#endif /* ML_SETTINGS */

#if ML_PART_OVL
/* leave the page: back to the parent page (cursor kept) or to the menu */
static void page_leave(void)
{
#ifdef ML_SETTINGS
    int idx = g_page;
#if ML_NETTEST
    if (idx == PG_NET)
        (void) ml_net_cost(111);                   /* B (or any way out) cancels a running connection test at once */
#endif
    if (idx == PG_GAME || idx == PG_SYS)
        ons_flush(ST(S_FRAME));
    if (idx == PG_GAME && CFG(CFG_PROG_TRIAL) && CFG(CFG_PROG_TRIAL) != PROG_TRIAL_END) {
        CFG(CFG_PROG_TRIAL) = PROG_TRIAL_END;      /* B during the test: not kept, the picture goes back */
        settings_apply();
    }
#if defined(UPD_TITLE) && defined(HUB_OVL_RES) && defined(ML_SIGN)
    if (idx == PG_SIGN && g_ut_hold) {         /* B in the update check's hold: straight to the menu (a later answer is still the toast) */
#ifndef ML_OVL
        extern int ut_menu_frame(unsigned int kind, int settled, unsigned int sign_state);
#endif
        g_ut_hold = 0;
        (void) ut_menu_frame(0u, 0, 0xFFu);
        g_pg_parent = -1;
        page_close();
        H(MENU_COOLDOWN) = 10;
        return;
    }
#endif
    if (idx == PG_SIGN)
        sign_shutdown();                       /* B while the overlay loads or after a failed load */
    if (g_pg_parent >= 0) {
        int par = g_pg_parent;
        g_pg_start_cur = g_pg_parent_cur;
        page_close();
        g_pg_parent = -1;
        page_open((u32) par);
        return;
    }
#endif
    page_close();
    H(MENU_COOLDOWN) = 10;
}

static void page_frame(void)
{
    u32 trig_lo = W(CTRL_ALL + 0xCu), rep_hi = W(CTRL_ALL + 0x20u), old = g_pg_cur;
    u32 n = pg_nrows(g_page);
    H(MENU_COOLDOWN) = 2;
#if ML_UI
    {   /* the network words follow the connection: rebuilt once a second (Settings' Network status row, the Network status page) */
        static u32 nt;
        if ((g_page == PG_SETTINGS || g_page == PG_NET) && ++nt >= 60u) {
            u32 k;
            nt = 0;
            if (g_page == PG_NET) {
                for (k = 0; k < n; k++) pg_row_build(g_page, (int) k);
                pg_desc_build();
#if ML_NETTEST
                nt_facts_build();                        /* the facts block follows the connection (an address arrives while the page is up) */
#endif
            }
            else pg_row_build(g_page, (int) SETK_NET);
        }
    }
#endif                           /* hold the retail menu off while the page is open */
    {   /* the retail menu camera turns the 3D menu with the C stick (HSD_PadCopyStatus[4] at 0x804C20BC, nml_subStickX/Y at +0x28/+0x2C): the page owns the stick, so the camera sees it centred (same as sg_entry.c cam_hold) */
        u32 i;
        for (i = 0; i < 4u; i++) { W(0x804C20BCu + i * 0x44u + 0x28u) = 0; W(0x804C20BCu + i * 0x44u + 0x2Cu) = 0; }
    }
#ifdef ML_SETTINGS
    ST(S_PG_CUR) = g_pg_cur;
#endif
    {
        u32 eat = 0;
#ifdef ML_SETTINGS
        if (g_page == PG_SETTINGS) {                /* the Konami code is read before B: its ninth press is a B that must not leave the list */
            u32 th = W(CTRL_ALL + 0x8u), ke = trig_lo;      /* the stick counts like the D-pad: the trigger word's high half has the stick directions in bits 4 to 7 (up, down, left, right) */
            if (th & 0x10u) ke |= 0x08u;
            if (th & 0x20u) ke |= 0x04u;
            if (th & 0x40u) ke |= 0x01u;
            if (th & 0x80u) ke |= 0x02u;
            g_kon_done = ml_konami_step(&g_konami, ke, ST(S_FRAME));
            eat = g_konami.idx == 9u && (trig_lo & 0x200u);
        }
#endif
        if ((trig_lo & 0x200u) && !eat) {           /* B */
            SFX(0);
            page_leave();
            return;
        }
    }
#ifdef ML_SETTINGS
    if (g_page == PG_SIGN) {
        if (g_sign_state == 3 || g_sign_state == 0) {
            /* nothing more to do here: only B leaves (above) */
        }
        return;
    }
    if (g_page == PG_NET) {                                  /* A on Reconnect: the full stack again (ms_net_cost 108 -> s12_gc.c ms_net_reconnect: adapter, link, new lease, caches, then the server); refused in a search or a match */
        if (g_net_rc_msg && g_net_rc_msg[0] == 'A') {         /*: "Already reconnecting." goes away by itself once the reconnect is over (it stayed until the cursor moved) */
            unsigned w[16];
            if (net_state(w) != 4) { g_net_rc_msg = 0; pg_desc_build(); }
        }
#if ML_NETTEST
        nt_ui_sync();
        if ((trig_lo & 0x100u) && g_pg_cur == NT_ROW)
            nt_press_a();
#endif
        if (g_pg_cur == WF_ROW && ((trig_lo & 0x100u) || (rep_hi & ((1u << 6) | (1u << 7))))) {      /* A / Left / Right on Wired opponents only: On <-> Off, stored with the other options (bit 2 of byte 17), read by the next Unranked search */
            const struct ms_settings* wc = ons_cur();
            if (ons_change_hud(1u, (wc->hudraise & 3u) | ((wc->hudraise & 4u) ^ 4u), ST(S_FRAME)))
                SFX(1);
            else
                SFX(0);
            pg_row_build(PG_NET, (int) WF_ROW);
        }
        if ((trig_lo & 0x100u) && g_pg_cur == RC_ROW) {
            u32 r;
#if ML_NETTEST
            nt_report();                                         /* the state of the failed bring-up goes to the card before the stack is torn down (MultishineDiag) */
#endif
            r = ml_net_cost(108);
            g_net_rc_msg = r == 1u ? "Not now: a search or a match is active." : r == 2u ? "Already reconnecting." : 0;
            SFX(r ? 0 : 1);
            pg_desc_build();
        }
    }
    if (g_page == PG_GAME || g_page == PG_SYS) {
        const char* m = online_status();
        if (m != g_pg_msg)
            pg_status_set(m);
#ifdef ONS_NETSEL
        if (g_page == PG_SYS && g_pg_cur == SYS_I_TT && (trig_lo & 0x100u)) {      /* A on Test Toast: the real toast model and draw path, raised locally; it shows once the page is left */
            ml_test_toast((int) g_tt_kind);
            SFX(1);
            pg_status_set("Test toast armed. Press B, it shows on the menu.");
        }
#endif
#if defined(ONS_NETSEL) && defined(ML_SIGN)
        if (g_page == PG_SYS && g_pg_cur == SYS_I_NETSET && (trig_lo & 0x100u)) {   /* A on Server And Slippi: the overlay's lab page; B returns here with the cursor on the row */
            g_pg_parent_cur = g_pg_cur;
            SFX(1);
            g_sign_goto = (SG_HUB | SG_GOTO_DEV) + 1u;
            if (sign_open())
                return;
            g_sign_goto = 0;
        }
#endif
#if SYS_CTL
        if (g_page == PG_SYS && g_pg_cur == 0 && (trig_lo & 0x100u)) {      /* A on Controller test: read-only screen of the sign overlay, straight to it */
            g_sys_gp_cur = g_pg_parent_cur;                                /* Settings row to come back to */
            g_pg_parent_cur = g_pg_cur;
            g_sign_ctl = 1;
            SFX(1);
            if (sign_open())
                return;
            g_sign_ctl = 0;
        }
#endif
#if SYS_ENT
        if (g_page == PG_SYS && g_pg_cur == SYS_I_ENT && (trig_lo & 0x100u)) {      /* A on Entropy Test: the overlay's entropy screen; Back leaves the overlay to this page */
            g_sys_gp_cur = g_pg_parent_cur;
            g_pg_parent_cur = g_pg_cur;
            g_sign_goto = SG_ENT + 1u;
            SFX(1);
            if (sign_open())
                return;
            g_sign_goto = 0;
        }
#endif
        if (rep_hi & (1u << 4)) {
            g_pg_cur = g_pg_cur ? g_pg_cur - 1u : n - 1u;
        } else if (rep_hi & (1u << 5)) {
            g_pg_cur = g_pg_cur + 1u < n ? g_pg_cur + 1u : 0u;
        } else if (rep_hi & (1u << 6)) {
            online_change((int) g_pg_cur, -1);
        } else if ((rep_hi & (1u << 7)) || (trig_lo & 0x100u)) {
            online_change((int) g_pg_cur, 1);
        }
        if (g_pg_cur != old) {
            SFX(2);
            pg_row_build(g_page, (int) old);
            pg_row_build(g_page, (int) g_pg_cur);
            pg_desc_build();
        }
        return;
    }
    if (g_page == PG_SETTINGS) {
        u32 k = g_pg_cur;                               /* SETK_*: Online, Game, Update, About, Diagnostics (after the Konami code) */
        if (g_kon_done && SET_DIAG && !g_diag_open && !DIAG_ALWAYS) {
            g_diag_open = 1;                            /* the code ends with A: it must not also open the row under the cursor, so this frame ends here */
            SFX(1);
            g_pg_start_cur = g_pg_cur;
            page_close();
            page_open((u32) PG_SETTINGS);
            return;
        }
        if (trig_lo & 0x100u) {
            if (k <= SETK_PRIV) {                       /* the sign overlay's Account / Friends page; B returns here with the cursor on that row */
                g_pg_parent_cur = g_pg_cur;
                SFX(1);
                g_sign_goto = (SG_HUB | SG_GOTO_PAGE | ((k + 1u) << 16)) + 1u;
                if (sign_open())
                    return;
                g_sign_goto = 0;
            } else if (k == SETK_ABOUT || k == SETK_UPDATE) {   /* the legal notice / the Update screen of the sign overlay, straight to it; Back leaves the overlay to this list */
                g_pg_parent_cur = g_pg_cur;
                SFX(1);
                g_sign_goto = (k == SETK_ABOUT ? SG_ABOUT : SG_UPD) + 1u;
                if (sign_open())
                    return;
                g_sign_goto = 0;
            } else if (k == SETK_GAME || k == SETK_NET || (SYS_ROWS && k == SETK_DIAG)) {
                int to = k == SETK_GAME ? PG_GAME : k == SETK_NET ? PG_NET : PG_SYS;
                SFX(1);
                g_pg_parent = PG_SETTINGS;
                g_pg_parent_cur = g_pg_cur;
                page_close();
                page_open((u32) to);
                g_pg_parent = PG_SETTINGS;
                return;
            }
        }
    }
#endif
    if (n == 0)
        return;
    if (rep_hi & (1u << 4)) {                       /* up */
        g_pg_cur = g_pg_cur ? g_pg_cur - 1u : n - 1u;
    } else if (rep_hi & (1u << 5)) {                /* down */
        g_pg_cur = g_pg_cur + 1u < n ? g_pg_cur + 1u : 0u;
    } else if ((trig_lo & 0x100u)
#ifdef ML_SETTINGS
               && g_page != PG_NET                      /* Network status has no stub rows: Reconnect is handled above, the facts do nothing */
#endif
               ) {                                      /* A: every other row of these screens is a stub */
        SFX(2);
        g_pg_status = 1;
        ST(S_NA)++;
        ST(S_PAGE_ROW) = g_pg_cur + 1u;
        pg_status_build();
        return;
    }
    if (g_pg_cur != old) {
#ifdef ML_SETTINGS
        g_net_rc_msg = 0;
#endif
        SFX(2);
        g_pg_status = 0;
        pg_status_build();
        pg_row_build(g_page, (int) old);
        pg_row_build(g_page, (int) g_pg_cur);
#if ML_NETTEST
        if (g_page == PG_NET)
            nt_rows_sync();
#endif
        pg_desc_build();
    }
}

#endif /* ML_PART_OVL (page_leave, page_frame) */

#ifdef ML_SETTINGS
#ifdef ML_SIGN
/* ---- the sign-in overlay (offline/multishine/menus/sign): resident side.  The overlay file is read from the DISC (/ms/sign.ovl) for now;  O-6 is
   card file first with the disc as fallback: only the three loader functions below (sd_open, sd_read_begin, sd_read_poll) change for that.
   Release overlay (SG_LAB 0): no network, no TLS; env.net and env.pool are null. ---- */
#define OS_ALLOC ((void* (*) (int, u32)) 0x80343EF0u)          /* OSAllocFromHeap */
#define OS_FREE ((void (*)(int, void*)) 0x80343FECu)           /* OSFreeToHeap */
#define HSD_HEAP ((int (*)(void)) 0x80375404u)                 /* the HSD main heap id */
#define DVD_PATH ((int (*)(const char*)) 0x8033796Cu)          /* DVDConvertPathToEntrynum */
#define DVD_FASTOPEN ((int (*)(int, void*)) 0x80337C60u)       /* DVDFastOpen */
#define DVD_CLOSE ((int (*)(void*)) 0x80337CD4u)               /* DVDClose */
#define DVD_READ ((int (*)(void*, void*, int, int, void*, int)) 0x80337CF8u)   /* DVDReadAsyncPrio(fi, addr, len, offset, callback, prio) */
#define PADM 0x804C1FACu                                       /* HSD_PadMasterStatus[0]: button +0, sticks +0x18, triggers +0x1C, err +0x41 */
#define GX_FIFO ((volatile u32*) 0xCC008000u)

extern void ml_initlod(void* texobj);                          /* ml_sign_stub.S: GXInitTexObjLOD(obj, NEAR, NEAR, 0, 0, 0, 0, 0, 0) */

#ifdef ML_OVL
#define SDK_TABLE (E->sdk)
#define SDK_CLOSE() (E->sdk->close())
#else
#define SDK_TABLE (&g_sdk)
#define SDK_CLOSE() sd_close()
#endif
#if ML_PART_OVL
static struct sg_hook g_sg;
static struct sgo_env g_env;
#endif
#if ML_PART_OVL
/* RETAIL TOURNAMENT MELEE ON THE CARD ROUTE.  Two things take Tournament Melee away on the card route
   : the exploit framing writes `li r0,2` at 0x8022D638 (the Tournament row starts VS), and the Gecko codehandler (run once at boot) sits over the
   first 0xBA8 bytes of the Tournament code at 0x801910E0.  When the Original Menu opens we read those bytes back from the DISC's own main.dol (header word 0x420 = DOL offset; the DOL
   section table says where; nothing of Nintendo's is in our files), put them back, flush the caches and then put the retail `li r0,27` word back.  Any failure leaves the redirect
   as it was (the Tournament row starts VS, no freeze).  Bounded wait; only runs while the redirect word is in place, so the patched-ISO route (retail word) never reads the disc. */
#define TN_CODE 0x801910E0u
#define TN_CODE_LEN 0xBA8u
#define TN_WORD 0x8022D638u
#define TN_R13_LO (-0x5068)        /* the r13 slots between -0x5068 and -0x5008: Tournament mode's small data, reused by the online codeset */
#define TN_R13_LEN 0x60u
static volatile int g_tn_res;
static void tn_cb(int result, void* fi)
{
    (void) fi;
    g_tn_res = result <= 0 ? -1 : result;
}
static int tn_read(u8* fi, void* dst, u32 off, u32 len)       /* absolute disc read through a hand-made DVDFileInfo (startAddr 0 = disc start), waits (bounded) */
{
    u32 n;
    W((u32) fi + 0x30u) = 0;
    W((u32) fi + 0x34u) = 0x7FFFFFF0u;
    g_tn_res = 0;
    if (!DVD_READ(fi, dst, (int) len, (int) off, (void*) tn_cb, 2))
        return 0;
    for (n = 0; n < 100000000u && g_tn_res == 0; n++)
        ;
    if (g_tn_res == 0) {
        ((int (*)(void*)) 0x80339E78u)(fi);                    /* DVDCancel (blocking): the buffer is freed by the caller */
        return 0;
    }
    return g_tn_res > 0;
}
static void tn_restore(void)
{
    u8* fi;
    u8* buf;
    u32 dol, i, a, start, skip, len, fo = 0;
    int e, ok = 0;
    if (W(TN_WORD) != 0x38000002u)                              /* retail word or something else is there: nothing to do */
        return;
    e = DVD_PATH("/GmTou1p.dat");                               /* any real file: its DVDFileInfo is the template for the forged one */
    if (e < 0)
        return;
    fi = (u8*) OS_ALLOC(HSD_HEAP(), 0x60u + 0xE00u + 0x40u);
    if (!fi)
        return;
    buf = (u8*) (((u32) fi + 0x60u + 31u) & ~31u);
    if (!DVD_FASTOPEN(e, fi))
        goto out;
    if (!tn_read(fi, buf, 0x420u, 0x20u))
        goto out;
    dol = W((u32) buf);
    if (!dol || (dol & 31u) || dol > 0x60000000u)
        goto out;
    if (!tn_read(fi, buf, dol, 0x100u))
        goto out;
    for (i = 0; i < 18u; i++) {
        u32 so = W((u32) buf + i * 4u), sa = W((u32) buf + 0x48u + i * 4u), sz = W((u32) buf + 0x90u + i * 4u);
        if (sz && TN_CODE >= sa && TN_CODE + TN_CODE_LEN <= sa + sz) {
            fo = dol + so + (TN_CODE - sa);
            break;
        }
    }
    if (!fo)
        goto out;
    start = fo & ~31u;
    skip = fo - start;
    len = (skip + TN_CODE_LEN + 31u) & ~31u;
    if (len > 0xE00u || !tn_read(fi, buf, start, len))
        goto out;
    if (W((u32) buf + skip) == 0)                               /* zeros are not code */
        goto out;
    {   const struct tmv_pub* tp = ML_TMV;                    /* the Gecko codehandler is about to go: keep it (the BSS copy it came from is reused by the codeset), tn_unrestore puts it back */
        if (tp && tp->hs && W((u32) tp->hs + TN_CODE_LEN) != 0x48534156u) {
            for (a = 0; a < TN_CODE_LEN; a += 4u)
                W((u32) tp->hs + a) = W(TN_CODE + a);
            W((u32) tp->hs + TN_CODE_LEN) = 0x48534156u;
        }
        if (tp && tp->hs) {                                   /* the online codeset's r13 slots (TN_R13: see tn_unrestore), as they are in the menu before the retail scene can write them */
            volatile u8* q = (volatile u8*) (r13_get() + (u32) TN_R13_LO);
            for (a = 0; a < TN_R13_LEN; a++)
                tp->hs[TN_CODE_LEN + 4u + a] = q[a];
            W((u32) tp->hs + TN_CODE_LEN + 4u + TN_R13_LEN) = 0x52313353u;
        }
    }
    for (a = 0; a < TN_CODE_LEN; a += 4u)
        W(TN_CODE + a) = W((u32) buf + skip + a);
    DC_STORE((void*) TN_CODE, TN_CODE_LEN);
    IC_INVAL((void*) TN_CODE, TN_CODE_LEN);
    W(TN_WORD) = 0x3800001Bu;                                   /* li r0,27 (GM_TOURNAMENT), the retail word */
    DC_STORE((void*) TN_WORD, 4);
    IC_INVAL((void*) TN_WORD, 4);
    ok = 1;
out:
    DVD_CLOSE(fi);
    OS_FREE(HSD_HEAP(), fi);
    ST(S_TOURN) += ok ? 0x10000u : 0x100u;                      /* proof word: +0x10000 restored, +0x100 tried and failed (the low byte counts refused go_scene(27)) */
}
/* Back from the retail Tournament scene (the Original Menu's row, or the animated bracket): the codehandler goes back over the retail code and the redirect word returns, so the hooks of the codeset
   (the online game start among them) run again.  Without it every game started after a visit hung on a black screen. */
/* The r13 half of tn_unrestore, on its own since it runs at the menu scene's ENTRY (mlo_enter), before the retail code builds the first screen.  Cube 389: B out of the retail
   Tournament screen lands on VS. Mode, a retail kind without our think, so the block went back only after B to the Original menu; until then the codeset's
   menu code read the Tournament scene's values in its slots and the VS. Mode screen was built without rows, preview or description.  Once per saved block (the magic word is cleared). */
static void __attribute__((unused)) tn_r13_back(void)
{
    const struct tmv_pub* tp = ML_TMV;
    u32 a;
    if (tp && tp->hs && W((u32) tp->hs + TN_CODE_LEN + 4u + TN_R13_LEN) == 0x52313353u && B(0x80479D32u) == 27u) {
        volatile u8* q = (volatile u8*) (r13_get() + (u32) TN_R13_LO);
        for (a = 0; a < TN_R13_LEN; a++)
            q[a] = tp->hs[TN_CODE_LEN + 4u + a];
        W((u32) tp->hs + TN_CODE_LEN + 4u + TN_R13_LEN) = 0u;
        ST(S_TOURN) += 0x10000000u;                           /* proof word: +0x10000000 r13 block put back */
    }
}
static void __attribute__((unused)) tn_unrestore(void)
{
    const struct tmv_pub* tp = ML_TMV;
    u32 a;
    /* (cube 371: the retail Tournament screen opened and left, then Multishine > Dojo: the whole console froze on the hub frame; our own Tournament list, an overlay from the card,
       still opened, so neither the card nor the handler).  Slippi's online code keeps its globals in r13 slots it took FROM Tournament mode (Online.s / Common.s: OFST_R13_ONLINE_MODE -0x5060,
       APP_STATE, the scene buffer pointer -0x503C, ISPAUSE -0x5038 "originally used for tournament mode", the callback address -0x5018, USE_PREMADE_TEXT -0x5014, ...).  The retail scene
       writes its own variables there; the next entry of the online scene then runs with that pointer and callback.  The block saved by tn_restore goes back when the scene just left was
       the Tournament one (previous major 27), before anything of the menu reads it. */
    tn_r13_back();
    if (!tp || !tp->hs || W((u32) tp->hs + TN_CODE_LEN) != 0x48534156u || W(TN_WORD) != 0x3800001Bu)
        return;
    for (a = 0; a < TN_CODE_LEN; a += 4u)
        W(TN_CODE + a) = W((u32) tp->hs + a);
    DC_STORE((void*) TN_CODE, TN_CODE_LEN);
    IC_INVAL((void*) TN_CODE, TN_CODE_LEN);
    W(TN_WORD) = 0x38000002u;
    DC_STORE((void*) TN_WORD, 4);
    IC_INVAL((void*) TN_WORD, 4);
    W((u32) tp->hs + TN_CODE_LEN) = 0u;
    ST(S_TOURN) += 0x1000000u;                                  /* proof word: +0x1000000 handler put back */
}
/* Leave for the retail Tourney scene to show the bracket handed over by env_opt id 80: restore the retail Tournament code (tn_restore, from the player's own disc), arm the engine's tmv request and exit the
   menu scene the way the stock VS. Mode > Tournament row does.  Returns 0 when the scene is left; -1 when the restore did not hold (the code still redirects): nothing is armed and the menu stays,
   the text bracket is what the player has. */
static int go_tmv(void)
{
    const struct tmv_pub* t = ML_TMV;
    u8* d;
    tn_restore();
    if (!t || W(TN_WORD) != 0x3800001Bu) {
        ST(S_TOURN) += 0x100u;
        return -1;
    }
    t->st[1] = 1u;                                       /* tmv_st[TMV_W_REQ] */
    B(MENU_FLOW + 0x11u) = 1;
    H(MENU_COOLDOWN) = 5;
    g_couch = 2u;                                        /* as go_scene: not Couch */
    g_ret_kind = 0;
    if (B(MENU_FLOW + 0u) == 0u || B(MENU_FLOW + 0u) == (u8) ML_KIND_MORE) {   /* the Tourney scene's exit returns to the Main Menu, cursor on the row the bracket was opened from (the retail exit goes to the VS. Mode screen otherwise) */
        g_ret_kind = B(MENU_FLOW + 0u) + 1u;
        g_ret_row = (g_ret_row & 0x80000000u) | H(MENU_FLOW + 2u);   /* (bit 31: the first-Main-Menu music draw is done) */
    }
    SFX(1);
    d = (u8*) EXIT_DATA();
    d[0] = 0x1Bu;                                        /* GM_TOURNAMENT: what go_scene refuses on the card route; the code is restored above */
    EXIT_GO();
    return 0;
}
#endif /* ML_PART_OVL (tn_restore) */
#if ML_PART_RES
static u8 g_dvd_fi[0x40] __attribute__((aligned(32)));
static volatile int g_dvd_res;                                 /* 0 busy, > 0 bytes read, < 0 error (set by the DVD callback) */
static u32 g_dvd_open;
static void dvd_cb(int result, void* fi)
{
    (void) fi;
    g_dvd_res = result == 0 ? -1 : result;
}
static void* sd_alloc(sgo_u32 n) { return OS_ALLOC(HSD_HEAP(), n); }
static void sd_free(void* p) { OS_FREE(HSD_HEAP(), p); }
#ifdef SD_CARD_HOLE
/* card route: an overlay that the memory-card pack serves (MultishinePack entry "ms/sign.ovl", H1 answers its path with a synthetic entry >= 0x4000) is produced whole into a
   heap block at open and handed out from there; the disc is not touched */
extern int df_ovl_size(int entry) __attribute__((weak));
extern int df_ovl_produce(int entry, u8* dest, u32 cap) __attribute__((weak));
extern int df_ovl_produce_w(int entry, u8* dest, u32 cap) __attribute__((weak));     /* the same, but waits (bounded) for Melee's card layer: the menu overlay's load at the scene entry cannot be retried per frame */
static u8* g_syn;
static u32 g_syn_len;
extern u32 df_ovl_why(u32* last) __attribute__((weak));
#endif
/* WHY the last overlay open failed (the failure page and the RAM proof show it): ml_st words CFG_SPARE5 = stage | why << 8 | rc << 16, CFG_SPARE7 = df_ovl_why's last word.
   stages: 1 table not read yet / path unknown, 2 no pack, 3 card or DVD busy (why = the first busy check: 0xB1..0xB8, 0xC0+i), 4 size, 5 heap block, 6 produce failed (rc), 7 DVDFastOpen failed */
static int sd_fail(u32 stage, u32 why, int rc, u32 last)
{
    ST(ML_CFG_BASE + CFG_SPARE5) = stage | (why << 8) | (((u32) rc & 0xFFFFu) << 16);
    ST(ML_CFG_BASE + CFG_SPARE7) = last;
    return -1;
}
__attribute__((unused)) static int sd_is_sign(const char* p)                           /* "/ms/sign.ovl" (the other overlay file through here is "/ms/menu.ovl"): the one open that must never wait (the menu thread shows its loading page and retries per frame) */
{
    while (p[1])
        p++;
    return p[-7] == 's';
}
static int sd_open(const char* path, sgo_u32* len)
{
    int e = DVD_PATH(path);
    u32 why = 0, last = 0;
#ifdef SD_CARD_HOLE
    if (sd_is_sign(path) && df_ovl_why)
        why = df_ovl_why(&last);
#endif
    if (e < 0)
        return sd_fail(why == 1u ? 1u : 2u, why, e, last);
#ifdef SD_CARD_HOLE
    if (e >= 0x4000) {
        int n;
        u32 cap;
        if (!df_ovl_size || !df_ovl_produce || g_syn)
            return sd_fail(2u, 0xFFu, 0, 0);
        if (why)
            return sd_fail(why >= 0xB1u ? 3u : 1u, why, 0, last);
        n = df_ovl_size(e);
        if (n <= 0)
            return sd_fail(4u, 0, n, last);
        cap = ((u32) n + 31u) & ~31u;
        g_syn = (u8*) OS_ALLOC(HSD_HEAP(), cap + 32u);     /* + 32: a compressed entry needs gap + stream >= n + 1 bytes in place (DF_OVL_SLACK, tools/card/build_pack.py OVL_SLACK) */
        if (!g_syn)
            return sd_fail(5u, 0, 0, cap);
        e = (!sd_is_sign(path) && df_ovl_produce_w) ? df_ovl_produce_w(e, g_syn, cap + 32u) : df_ovl_produce(e, g_syn, cap + 32u);     /* ms/menu.ovl (the menu overlay, blocking at the scene entry) waits; ms/sign.ovl never does (its caller retries per frame) */
        if (e != 0) {
            OS_FREE(HSD_HEAP(), g_syn);
            g_syn = 0;
            return sd_fail(6u, 0, e, last);
        }
        g_syn_len = cap;
        *len = (u32) n;
        return 0;
    }
#endif
    if (!DVD_FASTOPEN(e, g_dvd_fi))
        return sd_fail(7u, 0, 0, 0);
    g_dvd_open = 1;
    *len = W((u32) g_dvd_fi + 0x34u);                          /* DVDFileInfo.length */
    return 0;
}
static int sd_read_begin(void* dst, sgo_u32 len, sgo_u32 off)
{
    if ((len | off | (u32) dst) & 31u)
        return -1;
#ifdef SD_CARD_HOLE
    if (g_syn) {
        u32 i;
        if (off + len > g_syn_len || off + len < off)
            return -1;
        for (i = 0; i < len; i++)
            ((u8*) dst)[i] = g_syn[off + i];
        g_dvd_res = (int) len;
        return 0;
    }
#endif
    g_dvd_res = 0;
    return DVD_READ(g_dvd_fi, dst, (int) len, (int) off, (void*) dvd_cb, 2) ? 0 : -1;
}
static int sd_read_poll(void) { return g_dvd_res == 0 ? 0 : (g_dvd_res > 0 ? 1 : -1); }
static void sd_close(void)
{
#ifdef SD_CARD_HOLE
    if (g_syn) {
        OS_FREE(HSD_HEAP(), g_syn);
        g_syn = 0;
    }
#endif
    if (g_dvd_open) {
        if (g_dvd_res == 0)
            ((int (*)(void*)) 0x80339E78u)(g_dvd_fi);           /* DVDCancel (blocking): a read still in flight must not land in a block freed after this close */
        DVD_CLOSE(g_dvd_fi);
    }
    g_dvd_open = 0;
}
static void sd_flush(void* p, sgo_u32 n)
{
    DC_STORE(p, n);
    IC_INVAL(p, n);
}
#ifdef ML_OVL_RES
const struct sgo_sdk ml_sdk = { sd_alloc, sd_free, sd_open, sd_read_begin, sd_read_poll, sd_close, sd_flush };    /* [RES] shared with the nested sign-in overlay and with ml_res.c's loader */
/* a read that did not finish in time: cancel it (the SDK call waits until the drive has let go of the buffer), then close; the caller frees the buffer after this */
#define DVD_CANCEL ((int (*)(void*)) 0x80339E78u)                  /* DVDCancel (blocking) */
void ml_dvd_cancel(void)
{
    if (g_dvd_open)
        DVD_CANCEL(g_dvd_fi);
    sd_close();
}
/* the scene is over (ml_res.c ml_scene_change, after the menu overlay left): an overlay open that was still in flight (the nested sign-in overlay's load, e.g. the Game
   Night room reopened right after a set and the scene changed before its third poll) never reached sd_close, and a non-zero g_syn refuses every later open (sd_open
   stage 2): from then on each menu entry failed and the retail menu ran until a reboot.  Drop the open; the block is freed only if it is
   still a cell of the heap (`live`), a heap rebuilt under us is never touched. */
void ml_sdk_forget(int (*live)(const void*))
{
    if (g_dvd_open) {
        DVD_CANCEL(g_dvd_fi);
        DVD_CLOSE(g_dvd_fi);
        g_dvd_open = 0;
    }
#ifdef SD_CARD_HOLE
    if (g_syn) {
        if (live(g_syn))
            OS_FREE(HSD_HEAP(), g_syn);
        g_syn = 0;
    }
#endif
    (void) live;
}
#else
static const struct sgo_sdk g_sdk = { sd_alloc, sd_free, sd_open, sd_read_begin, sd_read_poll, sd_close, sd_flush };
#endif
#endif /* ML_PART_RES */
#if ML_PART_OVL

/* graphics table: the same helpers as the stub pages (text_new_at + ml_sis_add on the menu's front canvas), handles from a small pool */
static const u8 col_good[4] = { 0x70, 0xE8, 0x70, 0xFF }, col_bad[4] = { 0xFF, 0x6A, 0x5A, 0xFF };
static const u32 row_sy0[26] = { 0xC0D00000u, 0xC0B33333u, 0xC0966666u, 0xC0733333u, 0xC039999Au, 0xC0000000u, 0xBF8CCCCDu, 0xBE4CCCCDu, 0x3F333333u, 0x3FCCCCCDu, 0x40200000u, 0x4059999Au, 0x4089999Au, 0x40A66666u, 0x40C33333u, 0x40E00000u, 0x40FCCCCDu, 0x410CCCCDu, 0x411B3333u, 0x4129999Au, 0x41380000u, 0x41466666u, 0x4154CCCDu, 0x41633333u, 0x4171999Au, 0x41800000u };
static const u32 row_y[26] = { 0xC1466666u, 0xC1380000u, 0xC129999Au, 0xC11B3333u, 0xC10CCCCDu, 0xC0FCCCCDu, 0xC0E00000u, 0xC0C33333u, 0xC0A66666u, 0xC089999Au, 0xC059999Au, 0xC0200000u, 0xBFCCCCCDu, 0xBF333333u, 0x3E4CCCCDu, 0x3F8CCCCDu, 0x40000000u, 0x4039999Au, 0x40733333u, 0x40966666u, 0x40B33333u, 0x40D00000u, 0x40ECCCCDu, 0x4104CCCDu, 0x41133333u, 0x4121999Au };
static const u32 row_y2[26] = { 0x40E00000u, 0x40FCCCCDu, 0x410CCCCDu, 0x411B3333u, 0x4129999Au, 0x41380000u, 0x41466666u, 0x4154CCCDu, 0x41633333u, 0x4171999Au, 0x41800000u, 0x41873333u, 0x418E6666u, 0x4195999Au, 0x419CCCCDu, 0x41A40000u, 0x41AB3333u, 0x41B26666u, 0x41B9999Au, 0x41C0CCCDu, 0x41C80000u, 0x41CF3333u, 0x41D66666u, 0x41DD999Au, 0x41E4CCCDu, 0x41EC0000u };
struct gh { u32 kind, used; void* obj; };
#define GH_TEXT 1
#define GH_SPRITE 2
#define GH_MARK 3                /* the small mark of the Settings look (ui_mark): a sprite that owns no shared state */
#define GH_N 40
static struct gh GH[GH_N];
static struct gh* gh_get(u32 kind, void* obj)
{
    u32 i;
    if (!obj)
        return 0;
    for (i = 0; i < GH_N; i++)
        if (!GH[i].used) {
            GH[i].used = 1;
            GH[i].kind = kind;
            GH[i].obj = obj;
            return &GH[i];
        }
    if (kind == GH_TEXT) {
#if ML_UI
        if (!ft_release(obj))
#endif
            TEXT_FREE(obj);
    }
    return 0;
}
/* ---- the page header: the title TEXT is centred on the screen, the mark stays at its original place at the left (the Main Menu's place), so the two cannot meet with widescreen on. ---- */
static u8 spr_tobj[32] __attribute__((aligned(32)));
static void* spr_tex;
static u32 g_mark_b;                  /* the mark's blue mix 0 (white) .. 255 (brand cyan, the server answered): mark_level_tick, ml_mark_level.h */
static u32 g_mark_a;                  /* the mark's alpha now (MKL_ALPHA_OFF..ON, ml_mark_level.h): moved toward the online / offline level once a frame by mark_level_tick, 0 = not decided yet */
static u32 spr_row0, spr_rows = 15u, spr_dim = SG_QR_TEX_DIM;
/* v / 104 as float bits, soft-float: a 16-bit fraction, normalised */
static u32 spr_uv(u32 v)
{
    u32 q = (v << 16) / SG_QR_TEX_DIM, p = 31u;
    if (v >= SG_QR_TEX_DIM) return 0x3F800000u;
    if (!q) return 0x3F800000u;
    while (!(q >> p)) p--;
    return ((127u + p - 16u) << 23) | (((q << (23u - p)) & 0x7FFFFFu));
}
static const u32 spr_half[26] = { 0x00000000u, 0x3EE66666u, 0x3F666666u, 0x3FACCCCDu, 0x3FE66666u, 0x40100000u, 0x402CCCCDu, 0x4049999Au, 0x40666666u, 0x4081999Au, 0x40900000u, 0x409E6666u, 0x40ACCCCDu, 0x40BB3333u, 0x40C9999Au, 0x40D80000u, 0x40E66666u, 0x40F4CCCDu, 0x4101999Au, 0x4108CCCDu, 0x41100000u, 0x41173333u, 0x411E6666u, 0x4125999Au, 0x412CCCCDu, 0x41340000u };     /* rows * 0.45 as floats */
/* one textured quad of an I4 texture (w x h texels) on the menu camera: the QR code and the mark of the Settings look (corners as float bit patterns, screen units) */
static void spr_emit(void* tex, u32 tw, u32 th, u32 x0, u32 y0, u32 x1, u32 y1, u32 mode, u32 one)      /* one: the UV extent (1.0 = the whole texture); mode 0: opaque (the QR code), 1: the mark, white with the texel intensity as alpha times g_mark_a (the online / offline brightness, ml_mark_level.h) */
{
    u32 m[12];
    static const u32 z = F_POS_Z;
    ((void (*)(void*, void*)) 0x803695F0)(((void* (*) (void)) 0x8036A288)(), m);
    ((void (*)(int)) 0x80341510)(0);
    ((void (*)(void*, int)) 0x80341494)(m, 0);
    ((void (*)(int, int, int, int, int, int)) 0x8033C8A8)(0, 1, 4, 60, 0, 125);
    ((void (*)(int)) 0x803623D0)(0);
    ((void (*)(void)) 0x8033C3C8)();
    ((void (*)(int)) 0x8033D350)(0);
    ((void (*)(int)) 0x8033CB78)(1);
    ((void (*)(int)) 0x80340790)(1);
    ((void (*)(int, int, int, int, int)) 0x8033C414)(0, 9, 1, 4, 0);
    ((void (*)(int, int)) 0x8033BF00)(9, 1);
    ((void (*)(int, int, int, int, int)) 0x8033C414)(0, 13, 1, 4, 0);
    ((void (*)(int, int)) 0x8033BF00)(13, 1);
    ((void (*)(int, int, int, int)) 0x803405F0)(0, 0, 0, 255);
    ((void (*)(int, int, int, int, int)) 0x8033FF68)(0, 15, 15, 15, 8);
    ((void (*)(int, int, int, int, int, int)) 0x8034006C)(0, 0, 0, 0, 1, 0);
    ((void (*)(int, int, int, int)) 0x80340C3C)(0, 1, 0, 0);
    if (mode) {                                                                   /* genuinely transparent: colour white, alpha = the I4 intensity x the mark's level, blended over what is behind */
        static u32 lv;                                                            /* GXColor by pointer (the SDK takes it by reference): RGBA bytes, a word, aligned */
        lv = (mkl_rgb(255u, g_mark_b) << 8) | (g_mark_a ? g_mark_a : MKL_ALPHA_ON);     /* RGB: white mixed toward the brand cyan (the server answered), A: the link level */
        ((void (*)(int, const void*)) 0x803401EC)(1, &lv);                        /* GXSetTevColor(TEVREG0): A0 = the level (toast_pill.h does the same) */
        ((void (*)(int, int, int, int, int)) 0x8033FF68)(0, 15, 15, 15, 2);       /* GXSetTevColorIn: d = C0 (GX_CC_C0 is 2): the register's RGB, white mixed toward the brand cyan (was ONE, 12) */
        ((void (*)(int, int, int, int, int)) 0x8033FFE8)(0, 7, 4, 1, 7);          /* GXSetTevAlphaIn: (1 - c) * a + c * b with a = ZERO, b = TEXA, c = A0, d = ZERO: TEXA * A0 */
        ((void (*)(int, int, int, int, int, int)) 0x8034012C)(0, 0, 0, 0, 1, 0);  /* GXSetTevAlphaOp: ADD, scale 1, clamp */
        ((void (*)(int, int, int, int)) 0x80340C3C)(1, 4, 5, 0);                  /* GXSetBlendMode: BLEND, SRCALPHA, INVSRCALPHA */
    }
    ((void (*)(int, int, int)) 0x80340DC0)(0, 7, 0);
    ((void (*)(int)) 0x80340D40)(1);
    ((void (*)(int)) 0x80340D80)(0);
    ((void (*)(void*, void*, int, int, int, int, int, int)) 0x8033E9B0)(spr_tobj, tex, (int) tw, (int) th, 0, 0, 0, 0);
    if (!mode)
        ml_initlod(spr_tobj);                                                     /* the QR code: NEAREST (hard modules); the mark keeps GXInitTexObj's bilinear filter, the title's (one look) */
    ((void (*)(void*, int)) 0x8033EFD0)(spr_tobj, 0);
    ((void (*)(int, int, int)) 0x8033D0DC)(0x80, 0, 4);
    GX_FIFO[0] = x0; GX_FIFO[0] = y0; GX_FIFO[0] = z; GX_FIFO[0] = 0; GX_FIFO[0] = one;
    GX_FIFO[0] = x1; GX_FIFO[0] = y0; GX_FIFO[0] = z; GX_FIFO[0] = one; GX_FIFO[0] = one;
    GX_FIFO[0] = x1; GX_FIFO[0] = y1; GX_FIFO[0] = z; GX_FIFO[0] = one; GX_FIFO[0] = 0;
    GX_FIFO[0] = x0; GX_FIFO[0] = y1; GX_FIFO[0] = z; GX_FIFO[0] = 0; GX_FIFO[0] = 0;
}
static void spr_draw(void* gobj, int pass)
{
    u32 y0 = row_sy0[spr_row0], y1 = spr_row0 + spr_rows < 26u ? row_sy0[spr_row0 + spr_rows] : row_y2[spr_row0];     /* spr_rows text rows square (0.9 units a row): the caller says how big (Send Slippi Login 21 rows, Link 15) */
    u32 x0 = spr_half[spr_rows] ^ 0x80000000u, x1 = spr_half[spr_rows];                                                /* -/+ rows * 0.45, centred */
    (void) gobj;
    if (pass != 2 || !spr_tex)
        return;
    spr_emit(spr_tex, SG_QR_TEX_DIM, SG_QR_TEX_DIM, x0, y0, x1, y1, 0, spr_uv(spr_dim));
}
#if ML_UI && defined(ML_OVL)
/* THE SETTINGS GLOBE ("some opacity and the spinning globe from the tournament mode background"): while a Settings-look backdrop is up (an ml page or a sign-overlay page),
   Tournament Melee's animated background (GmTou1p, ScGamTour_scene_data set 5: the same piece and checks as the Game Night room screen, sign/gnb_art.c gart_bg) is drawn on the menu's background
   link with every menu model hidden, and the backdrop becomes see-through (col_bg_glass).  Read from the player's own disc at run time (no Nintendo bytes in our files).
   Any check that fails leaves the opaque black backdrop.  The archive (126 KB) is freed PGL_GRACE frames after the last backdrop went (moving between pages does not read it again). */
void ml_janim(void* jobj, unsigned frame);
#define PGL_FULLNAME ((char* (*)(const char*)) 0x80016204u)      /* lbFileGetFullName */
#define PGL_ENTRYNUM ((int (*)(const char*)) 0x8033796Cu)        /* DVDConvertPathToEntrynum */
#define PGL_ARC_LOAD ((void* (*)(const char*, void*)) 0x80016DBCu)
#define PGL_ARC_FREE ((void (*)(void*)) 0x80016EFCu)
#define PGL_ARC_PUB ((void* (*)(void*, const char*)) 0x80380358u)
#define PGL_JOBJ_LOAD ((void* (*)(void*)) 0x80370E44u)
#define PGL_GOBJ_NEW ((void* (*)(int, int, int)) 0x803901F0u)
#define PGL_GOBJ_OBJ ((void (*)(void*, int, void*)) 0x80390A70u)
#define PGL_GOBJ_LINK ((void (*)(void*, void*, int, int)) 0x8039069Cu)
#define PGL_GOBJ_FREE ((void (*)(void*)) 0x80390228u)
#define PGL_JOBJ_ADDANIM ((void (*)(void*, void*, void*, void*)) 0x8036FB5Cu)
#define PGL_JOBJ_DRAW 0x80391070u
#define PGL_JOBJ_KIND 0x804D7849u
#define PGL_NOP_DRAW 0x80081540u
#define PGL_FRAMES 800u
#define PGL_GRACE 120u
static void *pgl_arc, *pgl_g, *pgl_j, *g_ovl_bd;
static u32 pgl_set, pgl_f, pgl_idle;
static u8 pgl_tried;
/* Widescreen on: the globe model is drawn through the widened camera, so its far parts (blurry streaks left, striped shapes right) show beside the 4:3 picture ("Clip it").
   The draw callback clips the model to the middle of the 640 px wide frame (the globe itself spans about x 185..455 with widescreen on; scissor x 160..480) with GXSetScissor and gives the full scissor back; widescreen off: no change. */
static void pgl_draw(void* gobj, int pass)
{
    int ws = ons_cur()->ws ? 1 : 0;
    if (ws)
        ((void (*)(u32, u32, u32, u32)) 0x8034175C)(160u, 0u, 320u, 480u);
    ((void (*)(void*, int)) PGL_JOBJ_DRAW)(gobj, pass);
    if (ws)
        ((void (*)(u32, u32, u32, u32)) 0x8034175C)(0u, 0u, 640u, 480u);
}
static int pgl_ok(u32 p) { return p >= 0x80000000u && p < 0x81800000u && !(p & 3u); }
static void pgl_unlit(u8* j, unsigned depth)                    /* lit by the menu's lights the model comes out dark (Tournament has its own): as gnb_art.c unlit */
{
    unsigned guard = 0;
    while (j && pgl_ok((u32) j) && guard++ < 64u && depth < 8u) {
        u32 d = W(j + 0x18u), g2 = 0;
        if (!(W(j + 0x14u) & 0x4020u))
            while (d && pgl_ok(d) && g2++ < 8u) {
                u32 m = W(d + 8u);
                if (m && pgl_ok(m))
                    W(m + 4u) = (W(m + 4u) & ~6u) | 1u;
                d = W(d + 4u);
            }
        if (W(j + 0x10u))
            pgl_unlit((u8*) W(j + 0x10u), depth + 1u);
        j = (u8*) W(j + 0x08u);
    }
}
#define PGL_HID_MAX 48u
static u8* pgl_hid_g[PGL_HID_MAX];
static u32 pgl_hid_n;
static void pgl_menu(int show)                                  /* every menu model (a gobj drawn by HSD_GObj_JObjCallback) but the globe: not drawn while the globe is up (cube the More
                                                                   menu's buttons showed through the see-through backdrop); given back only to a gobj that is still in the lists */
{
    void** heads = (void**) W(0x804D782Cu);
    u32 n = (u32) B(0x804CE380u) + 1u, p, k;
    if (!heads || (show && !pgl_hid_n) || (!show && pgl_hid_n))
        return;
    if (n > 64u)
        n = 64u;
    for (p = 0; p < n; p++) {
        u8* go = (u8*) heads[p];
        u32 guard = 0;
        while (go && guard++ < 4096u) {
            if (!show) {
                if ((void*) go != pgl_g && W((u32) go + 0x1Cu) == PGL_JOBJ_DRAW && pgl_hid_n < PGL_HID_MAX) {
                    W((u32) go + 0x1Cu) = PGL_NOP_DRAW;
                    pgl_hid_g[pgl_hid_n++] = go;
                }
            } else if (W((u32) go + 0x1Cu) == PGL_NOP_DRAW)
                for (k = 0; k < pgl_hid_n; k++)
                    if (pgl_hid_g[k] == go) {
                        W((u32) go + 0x1Cu) = PGL_JOBJ_DRAW;
                        break;
                    }
            go = (u8*) W((u32) go + 0x08u);
        }
    }
    if (show)
        pgl_hid_n = 0;
}
static int pgl_show(void)                                       /* 1 = the globe is up */
{
    if (pgl_g) {
        pgl_idle = 0;
        return 1;
    }
    if (!pgl_arc) {
        char* full;
        u32 sc, sets, set, j, an;
        if (pgl_tried)
            return 0;
        pgl_tried = 1;
        full = PGL_FULLNAME("GmTou1p");
        if (!full || PGL_ENTRYNUM(full) < 0)
            return 0;
        pgl_arc = PGL_ARC_LOAD("GmTou1p", 0);
        if (!pgl_arc)
            return 0;
        sc = (u32) PGL_ARC_PUB(pgl_arc, "ScGamTour_scene_data");
        if (sc && pgl_ok(sc) && (sets = W(sc)) && pgl_ok(sets) && (set = W(sets + 4u * 5u)) && pgl_ok(set) && pgl_ok(j = W(set)) && W(j + 4u) == 0x20000008u &&
            pgl_ok(W(j + 8u)) && pgl_ok(W(W(j + 8u) + 0xCu)) && pgl_ok(W(W(W(j + 8u) + 0xCu) + 0x10u)) && (an = W(set + 4u)) && pgl_ok(an) && pgl_ok(W(an)))
            pgl_set = set;                                      /* the shape gnb_art.c checks; a different file is refused, never guessed */
        else {
            PGL_ARC_FREE(pgl_arc);
            pgl_arc = 0;
            return 0;
        }
    }
    {
        u32 an = W(pgl_set + 4u), ma = W(pgl_set + 8u), sa = W(pgl_set + 12u);
        void *g = PGL_GOBJ_NEW(4, 5, 0x80), *j;
        if (!g)
            return 0;
        j = PGL_JOBJ_LOAD((void*) W(pgl_set));
        if (!j) {
            PGL_GOBJ_FREE(g);
            return 0;
        }
        PGL_GOBJ_OBJ(g, B(PGL_JOBJ_KIND), j);
        pgl_unlit((u8*) j, 0);
        PGL_GOBJ_LINK(g, (void*) pgl_draw, 2, 0x80);
        PGL_JOBJ_ADDANIM(j, an && pgl_ok(an) ? (void*) W(an) : 0, ma && pgl_ok(ma) ? (void*) W(ma) : 0, sa && pgl_ok(sa) ? (void*) W(sa) : 0);
        ml_janim(j, 0);
        pgl_g = g;
        pgl_j = j;
        pgl_f = 0;
        pgl_idle = 0;
    }
    pgl_menu(0);
    {   /* the retail description line (Melee's text object, our line in it) would show through: back on Melee's blank string.  Only while no new hook came since our last write (the
           object is this screen's); the frame's description code writes the line again once the page is closed */
        u32* dt = (u32*) ST(S_DEBUG);
        if (dt && ST(S_BAR) == g_bar_seen) {
            ((void (*)(void*, u32)) 0x803A6368u)(dt, BLANK_ID);
            g_bar_seen = g_bar_gen_cur = ST(S_BAR);
            g_bar_hov = 0xFFFFu;
            g_bar_arm = 1;
        }
    }
    return 1;
}
static void pgl_drop(int all)                                   /* the globe goes (the menu is drawn again); all = also the archive and the retry */
{
    if (pgl_g) {
        PGL_GOBJ_FREE(pgl_g);
        pgl_g = pgl_j = 0;
    }
    pgl_menu(1);
    if (all) {
        if (pgl_arc)
            PGL_ARC_FREE(pgl_arc);
        pgl_arc = 0;
        pgl_set = 0;
        pgl_tried = 0;
    }
}
static void pgl_tick(void)                                      /* every menu frame */
{
    if (g_pg_back || g_ovl_bd) {
        if (pgl_g) {
            if (++pgl_f > PGL_FRAMES)
                pgl_f = 0;
            ml_janim(pgl_j, pgl_f);
        }
        pgl_idle = 0;
        return;
    }
    if (pgl_g)
        pgl_drop(0);                                            /* no backdrop: the menu at once */
    if (pgl_arc && ++pgl_idle > PGL_GRACE)
        pgl_drop(1);
}
#endif
static void* gfx_backdrop(void)
{
    u32* t = text_new_at(F_BG_X, F_BG_Y, F_PG_SIZE, col_white);
    u32 k;
    const u8* bg;
    if (!t)
        return 0;
    bg = PGL_BG();
    t[3] = F_BG_W;
    t[4] = F_BG_H;
    for (k = 0; k < 4; k++)
        ((u8*) t)[0x2C + k] = bg[k];
#if ML_UI && defined(ML_OVL)
    g_ovl_bd = t;
#endif
    ml_sis_add(t, zero_xy, "%s", " ");
    return gh_get(GH_TEXT, t);
}
#if ML_UI
#include "ml_mark_tex.h"               /* the small mark: I4 texture 64 x 32 (tools/brand/make_ui_mark.py), in the overlay part only */
#if ML_OWNFONT
#include "keyhint.h"                    /* the footer's key tokens and colours (mlf_footer) */
#include "ml_font.h"
static void* mlf_gobj;
#ifdef MLF_PROBE                                   /* TEMPORARY: S_DEBUG = live lines | used handles << 16 */
#define MLF_DBG() do { u32 k_, n_ = 0; for (k_ = 0; k_ < GH_N; k_++) n_ += GH[k_].used; ST(S_DEBUG) = mlf_used | (n_ << 16); } while (0)
#else
#define MLF_DBG() ((void) 0)
#endif
static int mlf_is(const void* t) { return (const unsigned char*) t >= (const unsigned char*) mlf_l && (const unsigned char*) t < (const unsigned char*) (mlf_l + MLF_ROWS); }
static int mlf_ensure(void)                     /* the one GObj that draws every line (made with the first line, destroyed with the last) */
{
    if (!mlf_gobj) {
        void* g = ((void* (*) (int, int, int)) 0x803901F0)(4, 5, 0x80);
        if (!g)
            return 0;
        DC_STORE((void*) mlf_tex, MLF_TEX_BYTES);
        GX_INV_TEX();
        ((void (*)(void*, void*, int, int)) 0x8039069C)(g, (void*) mlf_draw, 7, 0xFF);     /* link 7 as the page texts, priority 0xFF: drawn after them (the selection bar and backdrop stay behind) */
        mlf_gobj = g;
    }
    return 1;
}
static u32* mlf_text(int x, int y, unsigned sz, const u8* col, int right, const char* s)      /* a line of our font; the handle is the line (ft_release frees it) */
{
    struct mlf_line* l;
    if (!mlf_ensure())
        return 0;
    l = mlf_add(x, y, sz, ((u32) col[0] << 24) | ((u32) col[1] << 16) | ((u32) col[2] << 8) | col[3], right, s);
    MLF_DBG();
    return (u32*) l;
}
static void mlf_release(void* t)
{
    mlf_free((struct mlf_line*) t);
    MLF_DBG();
    if (!mlf_used && mlf_gobj) {
        ((void (*)(void*)) 0x80390228)(mlf_gobj);
        mlf_gobj = 0;
    }
}
#endif
/* ---- the Settings look (ml_ui.h): ONE set of helpers for the Melee-text pages (page_open) and the sign overlay (gfx_text, gfx2) ---- */
#define UI_X_TITLE 0xC11CCCCDu         /* -9.8: title and breadcrumb, right of the mark */
#define UI_Y_TITLE 0xC144CCCDu         /* -12.3 */
#define UI_Y_CRUMB 0xC12B3333u         /* -10.7 */
#define UI_SZ_TITLE 0x3D343958u        /* 0.044 */
#define UI_SZ_SMALL 0x3CB43958u        /* 0.022: breadcrumb, section headings, notes */
#define UI_SZ_BIG 0x3D03126Fu          /* 0.032: list rows */
#define UI_X_RIGHT 0x41500000u         /* 13.0: right margin of the values (right aligned text) */
#define UI_X_BAR 0xC1600000u           /* -14.0 */
#define UI_BAR_W 0x447A0000u           /* 28.0 / 0.028 (box sizes are in text units of the text's own size) */
#define UI_BAR_HB 0x424F2492u          /* 1.45 / 0.028: a big row */
#define UI_BAR_HS 0x420EDB6Eu          /* 1.0 / 0.028: a text row */
static const u8 col_dim2[4] = { 0x8C, 0x8C, 0xA4, 0xFF };       /* breadcrumb, headings, notes */
static const u8 col_barbg[4] = { 0x3C, 0x2E, 0x00, 0xFF };      /* the dark gold bar */
static const float ui_by[26] = { -12.55f, -11.65f, -10.75f, -9.85f, -8.95f, -8.05f, -7.15f, -6.25f, -5.35f, -4.45f, -3.55f, -2.65f, -1.75f, -0.85f, 0.05f, 0.95f, 1.85f, 2.75f, 3.65f, 4.55f, 5.45f, 6.35f, 7.25f, 8.15f, 9.05f, 9.95f };    /* row_y - 0.15: top of the bar of a big row (below the section heading above it) */
static const float ui_bs[26] = { -12.45f, -11.55f, -10.65f, -9.75f, -8.85f, -7.95f, -7.05f, -6.15f, -5.25f, -4.35f, -3.45f, -2.55f, -1.65f, -0.75f, 0.15f, 1.05f, 1.95f, 2.85f, 3.75f, 4.65f, 5.55f, 6.45f, 7.35f, 8.25f, 9.15f, 10.05f };      /* row_y - 0.05: a text row */


/* ---- the footer with button badges ----
   A footer string whose double-space separated tokens hold keys ("A: Check Again  B: Back", "Hold Z: Freeze  Hold B: Back", keyhint.h) is drawn as ONE row of pairs "(A) Check Again": the keys spread in equal
   shares of the text area (x -13 .. 13), each pair centred in its share, the badge a letter text on a coloured rounded plate (toast_pill.h).  The footer is several texts: the first one stands for
   all of them in the handle (ft_release frees the lot).  Any text that cannot be made, or a footer without a key: the old single text (ui_text goes on). */
#include "keyhint.h"
#define FT_MAX 16u
#define FT_HALF 851968                 /* 13.0 units, Q16 */
#define FT_W 1703936                   /* the text area: 26.0 units */
#define FT_FONT 280u                   /* F_PG_SIZE x 10000 */
static u32* ft_t[4][FT_MAX];          /* two sets per group (the overlay makes the next screen's footer BEFORE it frees the old one; with one set the new footer fell back to plain text) */
static void ft_drop(unsigned g)
{
    unsigned i;
    for (i = 0; i < ft_n[g]; i++)
        TEXT_FREE(ft_t[g][i]);
    ft_n[g] = 0;
}
static int ft_release(void* t)
{
    unsigned g;
#if ML_OWNFONT
    if (mlf_is(t)) {
        mlf_release(t);
        return 1;
    }
#endif
    for (g = 0; g < 4u; g++)
        if (ft_n[g] && ft_t[g][0] == (u32*) t) {
            ft_drop(g);
            return 1;
        }
    return 0;
}
static u32* ft_text(unsigned g, int x_q, int y_q, unsigned fe, const u8* col, unsigned align, const char* s)
{
    u32* t;
    char buf[120];
    if (ft_n[g] >= FT_MAX)
        return 0;
    t = text_new_at(tp_f2(x_q, 16), tp_f2(y_q, 16), tp_f2((int) (fe * 6554u / 1000u), 16), col);
    if (!t)
        return 0;
    ((u8*) t)[0x4A] = (u8) align;
    ml_sis_add(t, zero_xy, "%s", ml_sis_expand(buf, sizeof buf, s) ? buf : s);
    ft_t[g][ft_n[g]++] = t;
    return t;
}
static u32* ui_foot(const char* s)
{
    char k[KH_MAXTOK][KH_TOK];
    unsigned n = kh_split(s, k), i, j, g;
    int y = tp_q(((const u32a*) row_y)[UI_R_FOOT], 16);
    if (!n || !kh_has_key(k, n))
        return 0;
    g = 2u * ft_grp;
    if (ft_n[g])
        g++;                                    /* the old footer is still up: the second set of the group */
    if (ft_n[g])
        return 0;
    for (i = 0; i < n; i++) {
        int cx = -FT_HALF + (int) (((2u * i + 1u) * (unsigned) FT_W) / (2u * n));
        struct kh_seg sg;
        struct kh_pair p;
        if (!kh_parse(k[i], &sg)) {
            if (!ft_text(g, cx, y, FT_FONT, col_dim, 1, k[i]))
                goto fail;
            continue;
        }
        kh_pair_build(&sg, FT_FONT, &p);
        for (j = 0; j < p.n; j++) {
            const struct kh_item* it = &p.it[j];
            int x = cx - p.w / 2 + it->x;
            if (it->kind == KH_I_PLATE) {
                u8 ink[4];
                u32 ic = kh_ink_rgb(it->key);
                u32* t;
                ink[0] = (u8) (ic >> 16);
                ink[1] = (u8) (ic >> 8);
                ink[2] = (u8) ic;
                ink[3] = 0xFF;
                t = ft_text(g, x, y + it->dy, it->fe4, ink, 1, it->s);
                if (!t)
                    goto fail;
                kh_attach(t, it);
            } else if (!ft_text(g, x, y, FT_FONT, col_dim, 0, it->s))
                goto fail;
        }
    }
    return ft_t[g][0];
fail:
    ft_drop(g);
    return 0;
}

/* one text object of the Settings look at grid row `row` (right: aligned to the right margin, for the value of a row); attr = UI_A_* | UI_A_BIG */
static u32* ui_text(unsigned row, unsigned attr, const char* s, int right)
{
    unsigned a = attr & UI_A_MASK;
#if ML_OWNFONT
    if (row == UI_R_FOOT && a == UI_A_DIM && !right && !(attr & UI_A_BIG)) {      /* the footer in our font: badges and actions (ml_font.h mlf_footer); a footer without a key goes on below */
        u32* f;
        if (!mlf_ensure())
            return 0;
        f = (u32*) mlf_footer(s, mlf_row_top(UI_R_FOOT), ((u32) col_dim[0] << 24) | ((u32) col_dim[1] << 16) | ((u32) col_dim[2] << 8) | col_dim[3]);
        if (!f && !mlf_used && mlf_gobj) {                    /* no line at all: no GObj may outlive this overlay with nothing to free it */
            ((void (*)(void*)) 0x80390228)(mlf_gobj);
            mlf_gobj = 0;
        }
        if (f)
            return f;
    }
#endif
    if (!ML_OWNFONT && row == UI_R_FOOT && a == UI_A_DIM && !right && !(attr & UI_A_BIG)) {      /* the footer: keys as badges when it has any */
        u32* f = ui_foot(s);
        if (f)
            return f;
    }
    const u8* col = a == UI_A_HI || a == UI_A_HEAD ? col_gold : a == UI_A_DIM ? col_dim : a == UI_A_GOOD ? col_good : a == UI_A_BAD ? col_bad
                  : a == UI_A_CRUMB || a == UI_A_SEC ? col_dim2 : col_white;
    u32 x = F_PG_X, y = row < 26u ? row_y[row] : 0u, sz = F_PG_SIZE;
    u32* t;
    char buf[120];
    if (a == UI_A_TITLE) {
        x = UI_X_TITLE;
        y = UI_Y_TITLE;
        sz = UI_SZ_TITLE;
    } else if (a == UI_A_CRUMB) {
        x = UI_X_TITLE;
        y = UI_Y_CRUMB;
        sz = UI_SZ_SMALL;
    } else if (a == UI_A_SEC)
        sz = UI_SZ_SMALL;
    else if (attr & UI_A_BIG)
        sz = UI_SZ_BIG;
    if ((attr & UI_A_BIG) && !right && a != UI_A_TITLE) {              /* one size for rows and body text: a line too long for the list font keeps the text font */
        unsigned n = 0;
        while (s[n])
            n++;
        if (n > 46u)
            sz = F_PG_SIZE;
    }
#if ML_OWNFONT
    {                                                                  /* pixels: x left 78 (-13.0 units), title and breadcrumb 138 (-9.8), right edge 562; tops from the row grid (ml_font.h) */
        unsigned f = a == UI_A_TITLE ? MLF_TITLE : a == UI_A_CRUMB || a == UI_A_SEC ? MLF_SMALL : sz == UI_SZ_BIG ? MLF_BIG : MLF_BODY;
        int px = right ? 562 : a == UI_A_TITLE || a == UI_A_CRUMB ? 138 : 78;
        if (a == UI_A_TITLE) {                                 /* the header group centred on the screen */
            px = 320 - (int) mlf_width(s, MLF_TITLE) / 2;
        } else if (a == UI_A_CRUMB)
            px = 320 - (int) mlf_width(s, MLF_SMALL) / 2;
        int py = a == UI_A_TITLE ? 7 : a == UI_A_CRUMB ? 41 : mlf_row_top(row < 26u ? row : 0u);    /* title 7 (was 11): its capitals centred on the logo */
        return mlf_text(px, py, f, col, right, s);
    }
#endif
    if (right)
        x = UI_X_RIGHT;
    t = text_new_at(x, y, sz, col);
    if (!t)
        return 0;
    if (right)
        ((u8*) t)[0x4A] = 2;                                   /* alignment: right (0 left, 1 centre, 2 right), x is the right edge */
    ml_sis_add(t, zero_xy, "%s", ml_sis_expand(buf, sizeof buf, s) ? buf : s);
    return t;
}

/* line k of the Test connection block (ml_nt_geom.h): body font, pitch NTG_PITCH, from NTG_GAP under the row's pill */
#if ML_NETTEST
static u32* nt_text_row(unsigned row, unsigned k, unsigned cls, const char* s)
{
    static const u8 col_amber[4] = { 0xFF, 0xC0, 0x40, 0xFF };
    const u8* col = cls == 1u ? col_good : cls == 2u ? col_amber : cls == 3u ? col_bad : col_dim;
    (void) row;                                                    /* the footer area does not depend on the row (ml_nt_geom.h NTG_FOOT_Y) */
#if ML_OWNFONT
    return mlf_text(78, NTG_YR(row, k), MLF_BODY, col, 0, s);
#else
    return ui_text(row + 3u + k, (cls == 1u ? UI_A_GOOD : cls ? UI_A_BAD : UI_A_DIM) | UI_A_BIG, s, 0);
#endif
}
static u32* nt_text_at(unsigned k, unsigned cls, const char* s)
{
    return nt_text_row(NTG_ROW_TEST, k, cls, s);
}
static u32* nt_text_fact(unsigned k, const char* s)
{
#if ML_OWNFONT
    return mlf_text(78, NTG_FACT_Y(k), MLF_BODY, col_dim, 0, s);
#else
    return ui_text(18u + k, UI_A_DIM | UI_A_BIG, s, 0);
#endif
}
#endif

/* the selection bar behind the list row at grid row `row`: an empty text with a dark gold background box (as the backdrop), made BEFORE the row's text so the text is drawn over it */
static u32* ui_bar(unsigned row, unsigned big)
{
#if ML_OWNFONT
    if (row < 26u) {                                                   /* our font: the drawn pill that wraps the row's label (ml_font.h MLF_ROWPILL); the row's text is ours too, so its top is mlf_row_top(row) */
        struct mlf_line* l;
        (void) big;
        if (!mlf_ensure())
            return 0;
        l = mlf_add_rowpill(mlf_row_top(row), ((u32) col_barbg[0] << 24) | ((u32) col_barbg[1] << 16) | ((u32) col_barbg[2] << 8) | col_barbg[3]);
        MLF_DBG();
        return (u32*) l;
    }
#endif
    u32* t = row < 26u ? text_new_at(UI_X_BAR, big ? ((const u32a*) ui_by)[row] : ((const u32a*) ui_bs)[row], F_PG_SIZE, col_white) : 0;
    u32 k;
    if (!t)
        return 0;
    t[3] = UI_BAR_W;
    t[4] = big ? UI_BAR_HB : UI_BAR_HS;
    for (k = 0; k < 4; k++)
        ((u8*) t)[0x2C + k] = col_barbg[k];
    ml_sis_add(t, zero_xy, "%s", " ");
    return t;
}

/* the small Multishine mark (ml_mark_tex.h: our own five hexagons, I4 64 x 32) as a sprite at the top left; handle for ui_free */
static const u32 ui_mark_q[4] = { 0xC15BDB23u, 0x412C126Fu, 0xC120FDF4u, 0x41461687u };     /* (Dolphin pixel compare docs/screens//mark_compare_1068*): the menu ink box was 67 x 30 against the title 66 x 28 (+7 % high, +1.4 % wide); now x -13.741 .. -10.062, y 10.7545 .. 12.3805, same centre.  Before, x0 y0 x1 y1 = 0xC15C46DF 0x412B149B 0xC1209322 0x4147147A; the title's own texels (make_ui_mark.py coverage: fit 64 x 26, ink 62 x 26 of 64 x 32) and the title's bilinear filter, the ink box where it was (the quad 64 / 62 wider and 28 / 26 higher, same centre; x -13.767 .. -10.036, y 10.693 .. 12.442).  Before: x0 y0 x1 y1 = 0xC15B5810 0x412C149A 0xC12181F1 0x4146147B for a 64 x 28 core, nearest filter.  */     /* cube 1003: outline matched the title, 1 px high: both y 0.05 down */     /* 2026-10-24 later: the title logo's size exactly (Dolphin: menu 67 x 29 px against 65 x 28: x1 -10.094, y0 10.805; top and left kept) and no intensity gain in the texture (make_ui_mark.py), which had flattened the hexagons into a wider look.  Earlier 2026-10-24 (owner: the menu mark looked stretched next to the title logo; cube capture 960 x 540: same x, menu 31 px high against 28, top 1 px lower): y 10.747 .. 12.430, 10 percent less high, top 1 px up.  Before: x0 y0 x1 y1 in the sprite space of spr_emit (y up, the text space turned over): x -13.709 .. -10.109, y 10.57 (bottom) .. 12.37 (top); moved 11 px right and 2 px down on 2026-10-18 to the title logo's place: measured in Dolphin shots (640 x 528) the title logo covers x 65..131, y 13..40 (outline 66 x 27), the menu marks were x 54..121, y 11..38; then 3.5 percent bigger (outline 64 x 26 -> 66 x 27) */
static void mark_draw(void* gobj, int pass)
{
    void* tex = (void*) ml_mark_tex;
    (void) gobj;
    if (pass != 2)
        return;
#if MARK_BLUE_FADEOFF
    if (g_mark_b >= 128u)
        tex = (void*) ml_mark_tex_fo;                                             /* the cyan state with the pairs of hexagons fainter: swapped at half the white-to-cyan fade */
#endif
    spr_emit(tex, ML_MARK_W, ML_MARK_H, ui_mark_q[0], ui_mark_q[1], ui_mark_q[2], ui_mark_q[3], 1, 0x3F800000u);
}
/* the mark's brightness says whether the console is online (ml_mark_level.h): once a frame, the target level from ms_net_cost(107) (the same bits the Main Menu's panels read), g_mark_a moves
   toward it in 17 frames at most, and only when it changes (no flicker: the target is two fixed values).  A build without the network functions is offline. */
static void mark_level_tick(void)
{
#ifdef ML_OVL
    u32 have = E->net_cost != 0;
#else
    u32 have = ms_net_cost != 0;
#endif
    u32 v = have ? ml_net_cost(107) : 1u;
    u32 bt = mkl_blue(have && mkl_menu_online(v));
    g_mark_b = g_mark_a ? mkl_bstep(g_mark_b, bt) : bt;                       /* the first decision snaps */
    g_mark_a = mkl_step(g_mark_a, mkl_alpha(have && mkl_link(v)));
#if ML_NETTEST
    {   /* the network report without a button: a console that is still not online 45 s into the menus leaves MultishineDiag once (a busy card is retried every 5 s, six times at most) */
        static u32 t, tries;
        static u8 done;
        if (!done && have) {
            unsigned w[16];
            u32 i;
            for (i = 0; i < 16u; i++) w[i] = 0;
            if (ms_net_dbg) {
                ms_net_dbg(w);
                if (w[0] == 0x30u) done = 1;                 /* online: nothing to report */
                else if (++t >= 60u * 45u && (t % 300u) == 0u) {
                    if (nt_report() || ++tries >= 6u) done = 1;
                }
            }
        }
    }
#endif
}
static void gfx_tex_changed(void* tex, unsigned bytes);
static void* ui_mark(void)
{
    void* g = ((void* (*) (int, int, int)) 0x803901F0)(4, 5, 0x80);
    if (!g)
        return 0;
    gfx_tex_changed((void*) ml_mark_tex, ML_MARK_BYTES);
#if MARK_BLUE_FADEOFF
    gfx_tex_changed((void*) ml_mark_tex_fo, ML_MARK_BYTES);
#endif
    ((void (*)(void*, void*, int, int)) 0x8039069C)(g, (void*) mark_draw, 7, 0x80);
    return gh_get(GH_MARK, g);
}
static void* ui_mark_menu(void)       /* the same mark in the Melee menus, behind our text: the same gx link as the text canvas, made before the page and overlay objects so it is drawn first */
{
    void* g = ((void* (*) (int, int, int)) 0x803901F0)(4, 5, 0x80);
    if (!g)
        return 0;
    gfx_tex_changed((void*) ml_mark_tex, ML_MARK_BYTES);
#if MARK_BLUE_FADEOFF
    gfx_tex_changed((void*) ml_mark_tex_fo, ML_MARK_BYTES);
#endif
    ((void (*)(void*, void*, int, int)) 0x8039069C)(g, (void*) mark_draw, 7, 0x80);
    return gh_get(GH_MARK, g);
}
static void gfx_release(void* h);
static void ui_free(void* h) { gfx_release(h); }
#endif

static void* gfx_text(unsigned row, unsigned attr, const char* s)
{
#if ML_UI
    return gh_get(GH_TEXT, ui_text(row, attr, s, 0));
#else
    const u8* col = attr == SG_A_HI || attr == SG_A_HEAD ? col_gold : attr == SG_A_DIM ? col_dim : attr == SG_A_GOOD ? col_good : attr == SG_A_BAD ? col_bad : col_white;
    u32* t = row < 26u ? text_new_at(F_PG_X, row_y[row], F_PG_SIZE, col) : 0;
    char buf[120];
    if (!t)
        return 0;
    ml_sis_add(t, zero_xy, "%s", ml_sis_expand(buf, sizeof buf, s) ? buf : s);
    return gh_get(GH_TEXT, t);
#endif
}
static void* gfx_sprite(void* tex, unsigned row0)
{
    void* g = ((void* (*) (int, int, int)) 0x803901F0)(4, 5, 0x80);
    if (!g)
        return 0;
    spr_tex = tex;
    spr_rows = (row0 >> 8) & 0xFFu;                          /* the high byte is the size in text rows (SGO_VER 4); 0 = the old 15 */
    if (spr_rows == 0u || spr_rows > 25u) spr_rows = 15u;
    spr_dim = (row0 >> 16) & 0xFFu;                           /* SGO_VER 5: texels shown, 0 = all */
    if (spr_dim == 0u || spr_dim > SG_QR_TEX_DIM) spr_dim = SG_QR_TEX_DIM;
    row0 &= 0xFFu;
    spr_row0 = row0 < 26u ? row0 : 3u;
    ((void (*)(void*, void*, int, int)) 0x8039069C)(g, (void*) spr_draw, 7, 0x80);
    return gh_get(GH_SPRITE, g);
}
static void gfx_tex_changed(void* tex, unsigned bytes)
{
    DC_STORE(tex, bytes);
    GX_INV_TEX();
}
static void gfx_release(void* h)
{
    struct gh* g = (struct gh*) h;
    if (!g || !g->used)
        return;
    if (g->kind == GH_TEXT) {
#if ML_UI && defined(ML_OVL)
        if (g->obj == g_ovl_bd)
            g_ovl_bd = 0;
#endif
#if ML_UI
        if (!ft_release(g->obj))
#endif
            TEXT_FREE(g->obj);
    } else if (g->kind == GH_MARK)
        ((void (*)(void*)) 0x80390228)(g->obj);
    else {
        ((void (*)(void*)) 0x80390228)(g->obj);
        spr_tex = 0;
    }
    g->used = 0;
    g->obj = 0;
}
static const struct sgo_gfx g_gfx = { gfx_backdrop, gfx_text, gfx_sprite, gfx_tex_changed, gfx_release };
#if ML_UI
static void* gfx2_mark(void) { return ui_mark(); }
static void* gfx2_bar(unsigned row, unsigned big) { return gh_get(GH_TEXT, ui_bar(row, big)); }
static void* gfx2_value(unsigned row, unsigned attr, const char* s) { return gh_get(GH_TEXT, ui_text(row, attr, s, 1)); }
static const struct sgo_gfx2 g_gfx2 = { gfx2_mark, gfx2_bar, gfx2_value };
#endif

static u32 tb_now(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}
static u32 env_now_us(void) { return tb_now() / 40u; }          /* GameCube time base 40.5 MHz: about; only the lab build's TLS timings use it */
#ifdef SD_CARD_HOLE
#ifndef ML_OVL
extern u32 od_clock_ms(void);                                     /* s12_gc.c: the engine's one millisecond clock, the one the hub client's timers run on */
#endif
static u32 env_now_ms(void) { return od_clock_ms(); }
#else
static u32 env_now_ms(void) { return tb_now() / 40500u; }
#endif
#if defined(SD_CARD_HOLE)
/* the Update row's raw services for the overlay: the hub client's mnc and its three thunks live in on_backend.c (which knows struct od); the CARD work
   area calls are on_hist.c's.  The software reset is upd_reset below (reset = 0 would make the screen ask for a power cycle). */
#ifndef ML_OVL
extern void* ml_upd_net(void);
extern int ml_upd_hello(unsigned now_ms);
extern int ml_upd_poll(unsigned now_ms);
extern int ml_upd_link(void);
extern u8* ms_card_wa_acquire(u32 owner);
extern void ms_card_wa_release(void);
#endif
/* the loader's status block: list base + 0x3600 of the card layout the engine is built for (stock 0x817F7600, CL_LAYOUT=generic 0x817E3600, low 0x817C3600); MS_CARD_WA_ADDR is the layout's loader work area (release.sh WA_ADDR, card_payload.py) */
#if defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817D4000u
#define MS_CL_STATUS_ADDR 0x817E3600u
#elif defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817B4000u
#define MS_CL_STATUS_ADDR 0x817C3600u
#else
#define MS_CL_STATUS_ADDR 0x817F7600u
#endif
#ifndef ML_CL_STATUS
#define ML_CL_STATUS MS_CL_STATUS_ADDR                            /* CL_STATUS of the card layout (cardloader/cl/mscf.h) */
#endif
static unsigned g_upd_pending;
static void* upd_alloc(unsigned n) { return OS_ALLOC(HSD_HEAP(), (u32) n); }
static void upd_free(void* p) { OS_FREE(HSD_HEAP(), p); }
/* the software reset behind "A: Restart now" after an install: Melee's own reset-switch path (lb_8001955C, proven in Dolphin by ):
   audio off, finish the card task queue, drop the VI callbacks, black screen, two retraces, OSResetSystem(0,0,0) = __OSReboot (the apploader runs again, the loader re-injects at
   Name Entry).  The card was unmounted by the host before this is called (upd_host.c host_reset). */
static void upd_reset(void)
{
    soft_reset();
}
static struct sgo_updcfg g_ucfg;
/* the Online page: the identity in force, its card writer, and the start-check option (Update screen) */
#ifndef ML_OVL
extern void* ml_ident_cfg(void);
extern int ml_ident_save(const void* c);
#endif
static int env_opt(int id, int v)
{
    if (id == 80) {                                      /* the animated bracket: v = a `struct tmb_pack` in the overlay's memory; 0 = taken */
        const struct tmv_pub* t = ML_TMV;
        const u8* src = (const u8*) v;
        u32 i;
        if (!t || !v || t->in_size > 2048u || !g_ok)
            return -1;
        for (i = 0; i < t->in_size; i++)
            ((volatile u8*) t->in)[i] = src[i];
        g_tmv_go = 1;
        return 0;
    }
    if (id >= 60 && id <= 73)
        return slippi_friend_opt(id, v);
#ifdef ONS_NETSEL
    /* lab/dev builds: Settings > Online > Server (id 17: 0 the server, 1 Lab), Slippi (id 18: bit0 Real Slippi, bit1 the one-time confirm was accepted), and the card's
       matchmaker override as two halves for the read-only row (ids 40 high, 41 low; 0 = none) */
#ifndef ML_OVL
    extern unsigned sbr_nc_card_mm_ip(void);
#endif
    if (id == MSET_ID_SRV) {
        if (v >= 0) {
            (void) ons_change_srv(v > 2 ? 2u : (unsigned) v, ST(S_FRAME));
            ons_flush(ST(S_FRAME));
        }
        return (int) ons_cur()->srv;
    }
    if (id == MSET_ID_MMR) {
        if (v >= 0) {
            (void) ons_change_mmr(v & 1u, ST(S_FRAME));
            if (v & 2)
                (void) ons_change_mmk(ST(S_FRAME));
            ons_flush(ST(S_FRAME));
        }
        return (int) (ons_cur()->mmr | (ons_cur()->mmk ? 2u : 0u));
    }
    if (id == 40)
        return (int) (sbr_nc_card_mm_ip() >> 16);
    if (id == 41)
        return (int) (sbr_nc_card_mm_ip() & 0xFFFFu);
#endif
    if (id == 21) {                                               /* virtual: "Show technical info" (the Debug Lines bit), read by the sign overlay's SG_TECH_INFO; read only */
        return ons_cur()->dbgl ? 1 : 0;
    }
    if (id == MSET_ID_KEEP) {                                     /* Update > Keep the previous build as backup: 1 keep (default), 0 off */
        if (v >= 0) {
            (void) ons_change_keep(v ? 1u : 0u, ST(S_FRAME));
            ons_flush(ST(S_FRAME));
        }
        return ons_cur()->keepoff ? 0 : 1;
    }
    if (id == MSET_ID_BAK) {                                      /* Update > Backup to slot B: 1 on (default), 0 off */
        if (v >= 0) {
            (void) ons_change_bak(v ? 1u : 0u, ST(S_FRAME));
            ons_flush(ST(S_FRAME));
        }
        return ons_cur()->bakoff ? 0 : 1;
    }
    return -1;                                                    /* id 14 (the start check) is retired: always on, no setting */
}
static int env_seed(unsigned char rec[44], int write)
{
    int r = ons_seed(rec, ST(S_FRAME), write);
    if (write)
        ons_flush(ST(S_FRAME));
    return r;
}
static struct sgo_idcfg g_idcfg;
#endif

static int pad_mag(int x, int y) { return (x < 0 ? -x : x) + (y < 0 ? -y : y); }
/* the overlay's pad: ALL four ports ("any port must be able to work everywhere"; a controller in port 2 could not leave the Friends list).  Buttons of every connected
   port together, each stick and trigger from the port that moves it most; err is 0 when any port is connected, else port 1's (HSD_PadMasterStatus[4], 0x44 bytes, err at +0x41) */
static void sign_pad(struct sdent_pad* p)
{
    u32 i, b = 0, any = 0;
    int sx = 0, sy = 0, cx = 0, cy = 0, tl = 0, tr = 0;
    for (i = 0; i < 4u; i++) {
        u32 base = PADM + i * 0x44u, s = W(base + 0x18u), a = W(base + 0x1Cu);
        int x, y;
        if (B(base + 0x41u))
            continue;                                   /* no controller (or a transfer error) on this port */
        any = 1;
        b |= W(base) & 0xFFFFu;
        x = (signed char) (s >> 24);
        y = (signed char) (s >> 16);
        if (pad_mag(x, y) > pad_mag(sx, sy)) { sx = x; sy = y; }
        x = (signed char) (s >> 8);
        y = (signed char) s;
        if (pad_mag(x, y) > pad_mag(cx, cy)) { cx = x; cy = y; }
        if ((int) ((a >> 24) & 0xFFu) > tl) tl = (int) ((a >> 24) & 0xFFu);
        if ((int) ((a >> 16) & 0xFFu) > tr) tr = (int) ((a >> 16) & 0xFFu);
    }
    p->buttons = (u16) b;
    p->sx = (signed char) sx;
    p->sy = (signed char) sy;
    p->cx = (signed char) cx;
    p->cy = (signed char) cy;
    p->tl = (u8) tl;
    p->tr = (u8) tr;
    p->err = any ? 0u : (u8) B(PADM + 0x41u);
}

/* ---- WII_OVL_RETRY: the first open of the sign-in overlay can come while Melee's card layer or the DVD queue is busy (the Wii: Nintendont's emulated card was busy for over 11 s after the Name
   Entry start; the open used to wait inside the menu thread, then fail).  The open never waits now (sd_open): a busy refusal keeps the Loading page, counts the tries and opens again every
   half second for up to 30 s; the failure page shows the code line "E<err> S<stage> W<why> L<rc> T<tries>" (stage and why: sd_fail in this file; why 1 = card table not read yet, 0xB1..0xB8
   and 0xC0+i = the first busy check of df_ovl_why). ---- */
#define SG_RETRY_GAP 20250000u          /* 0.5 s of time base ticks */
#define SG_RETRY_MAX 1215000000u        /* 30 s */
static u32 g_sg_try, g_sg_t0, g_sg_t1;
static u8 g_sg_retry;
static char g_sg_msg[80];
static char* sg_put(char* d, const char* s, u32 v, u32 base)         /* text s, then v in `base` (no leading zeros) */
{
    char t[8];
    int n = 0;
    while (*s)
        *d++ = *s++;
    do { t[n] = "0123456789ABCDEF"[v % base]; n++; v /= base; } while (v && n < 8);
    while (n)
        *d++ = t[--n];
    return d;
}
/* Show technical info only (the Wii "Loading... WB1" report): the words of Melee's card layer that df_ovl_why checks, as hex in the technical line: K = LBCN_STATE+0x80,
   A = +0x8AC, M = the mask of the eleven card slots (+0x510 + 0x54 i) that are not idle (14), G = LBCG_STATE+0xC / +0x10, D = DVD status (the queue).  Read only, overlay code. */
#define SG_LBCN 0x80432A68u
#define SG_LBCG 0x80433318u
static char* sg_card_words(char* d)
{
    u32 i, m = 0;
    for (i = 0; i < 11u; i++)
        if (W(SG_LBCN + 0x510u + 0x54u * i) != 14u)
            m |= 1u << i;
    d = sg_put(d, " K", W(SG_LBCN + 0x80u) & 0xFFFFu, 16u);
    d = sg_put(d, " A", W(SG_LBCN + 0x8ACu) & 0xFFFFu, 16u);
    d = sg_put(d, " M", m, 16u);
    d = sg_put(d, " G", W(SG_LBCG + 0xCu) & 0xFFu, 16u);
    d = sg_put(d, "/", W(SG_LBCG + 0x10u) & 0xFFu, 16u);
    d = sg_put(d, " D", (u32) ((int (*)(void)) 0x80339B4Cu)() & 0xFFu, 16u);
    return d;
}
/* the page's line: "Loading (try 3) S3 W0xB1" while retrying, "Not available E4 S3 W0xB1 L0 T7" when it gave up (E error, S stage, W why, L rc, T tries) */
static void sg_show(u32 fail)
{
    u32 w = ST(ML_CFG_BASE + CFG_SPARE5);
    char* d = g_sg_msg;
    if (fail && !ons_cur()->dbgl) {
        d = sg_put(d, "Not available", 0u, 10u);
        d -= 1;
    } else if (fail) {
        d = sg_put(d, "Not available E", (u32) -g_sg.err, 10u);
        d = sg_put(d, " S", w & 0xFFu, 10u);
        d = sg_put(d, " W", (w >> 8) & 0xFFu, 16u);
        d = sg_put(d, " L", w >> 16, 16u);
        d = sg_put(d, " T", g_sg_try, 10u);
        d = sg_card_words(d);
    } else if (!ons_cur()->dbgl) {                      /* release: a friendly line, no try / stage / why codes; they show with Show technical info on */
        d = sg_put(d, "Loading, one moment.", 0u, 10u);
        d -= 1;                                          /* sg_put wrote a "0" after the text */
    } else {
        d = sg_put(d, "Loading (try ", g_sg_try, 10u);
        d = sg_put(d, ") S", w & 0xFFu, 10u);
        d = sg_put(d, " W", (w >> 8) & 0xFFu, 16u);
        d = sg_card_words(d);
    }
    *d = 0;
    g_pg_note_txt = g_sg_msg;
    if (g_page != PG_SIGN)
        return;
#if ML_UI
    pg_note_set(g_sg_msg);
#else
    pg_free(&g_pg_note);
    g_pg_note = pg_line(((const u32a*) pg_y)[1], col_dim, g_sg_msg);
#endif
}
static int sg_retryable(void)                                         /* the refusal was "busy / not read yet / no heap now", not a missing or bad file */
{
    u32 st = ST(ML_CFG_BASE + CFG_SPARE5) & 0xFFu;
    return g_sg.err == SGO_E_NOFILE && (st == 1u || st == 3u || st == 5u);
}

/* start loading: the message page (PG_SIGN) is shown while the overlay comes in; returns 1 when a screen was opened */
static int sign_open(void)
{
    u32 i;
    if (g_sign_state != 0)
        return 0;
#if ML_UI
    if (g_mmark) {                              /* the Melee-menu mark is a handle of this table: free it BEFORE the table is forgotten, or it stays on screen over the Event-view screens (rooms, Standings, Friends; audit RM-01) */
        ui_free(g_mmark);
        g_mmark = 0;
    }
#endif
#if ML_UI
    for (i = 0; i < GH_N; i++) {                /* any other mark still alive (the overlay's own mark of an earlier session, a page's mark) goes the same way: a mark whose handle is forgotten here is never freed */
        if (GH[i].used && GH[i].kind == GH_MARK)
            ui_free(&GH[i]);
    }
    if (g_page >= 0 && g_pg_mark) {
        ui_free(g_pg_mark);
        g_pg_mark = 0;
    }
#endif
    for (i = 0; i < GH_N; i++)
        GH[i].used = 0;
#if ML_UI
    ft_n[2] = ft_n[3] = 0;                      /* the overlay rows' footer texts are forgotten with their handles */
#endif
    if (g_bar) {                                /* the Main Menu row's description bar must not stay under the overlay's Event screen (it showed through the Friends panel); it comes back with the menu (g_bar_hov) */
        TEXT_FREE(g_bar);
        g_bar = 0;
    }
    g_bar_hov = 0xFFFFu;
    if (!sg_hook_parked(&g_sg))                 /* a parked or prefetching block (ev_keep_tick) is entered by sg_hook_open without a card read */
        sg_hook_init(&g_sg, SDK_TABLE);
    g_env.abi = SGO_VER;
    g_env.sdnet_size = 0;
    g_env.net = 0;
    g_env.pool = 0;
#ifdef MS_TLS_PROBE      /* LAB DIAGNOSTIC (tlsprobe=on): the lab overlay's TCP + TLS probe runs on the engine's own sdnet, in the menus only */
    {
        extern struct sdnet* ms_tls_net(void);
        extern void ms_tls_poll(struct sdnet*);
        extern int ms_tls_begin(void);
        extern void ms_tls_end(void);
        extern sdnet_u32 sdnet_csum_add(sdnet_u32 sum, const sdnet_u8* p, sdnet_u32 len);       /* sdnet.c, not declared in sdnet.h */
        extern sdnet_u16 sdnet_csum_fold(sdnet_u32 sum);
        g_env.sdnet_size = (u32) sizeof(struct sdnet);
        g_env.net = ms_tls_net();
        g_env.csum_add = sdnet_csum_add;
        g_env.csum_fold = sdnet_csum_fold;
        g_env.tx_ip = sdnet_tx_ip;
        g_env.pollmagic = SGO_POLLMAGIC;
        g_env.net_poll = ms_tls_poll;
        g_env.net_begin = ms_tls_begin;
        g_env.net_end = ms_tls_end;
    }
#endif
    g_env.now_us = env_now_us;
    g_env.now_ms = env_now_ms;
#if defined(ONS_NETSEL) && defined(SD_CARD_HOLE)
    {
#ifndef ML_OVL
        extern void ms_net_dbg(unsigned* w) __attribute__((weak));        /* s12_gc.c, netcfg=on card route only */
#endif
        g_env.netdbg = ms_net_dbg;
        g_env.netmagic = ms_net_dbg ? SGO_NETMAGIC : 0u;
    }
#endif
    g_env.gfx = &g_gfx;
#if ML_UI
    g_env.gfx2magic = SGO_GFX2MAGIC;
    g_env.gfx2 = &g_gfx2;
#endif
#ifdef ML_OVL
    g_env.upd = E->upd;                     /* the Update row's host services: 0 until the resident host exists, the row then says so */
#endif
#if defined(SD_CARD_HOLE)
    g_ucfg.net = ml_upd_net(); g_ucfg.hello = ml_upd_hello; g_ucfg.poll = ml_upd_poll; g_ucfg.link_up = ml_upd_link;
    g_ucfg.alloc = upd_alloc; g_ucfg.free = upd_free; g_ucfg.reset = upd_reset;
    g_ucfg.cl_status = (const volatile unsigned*) ML_CL_STATUS; g_ucfg.abi = 1u;      /* MSCF_ABI */
    g_ucfg.wa_acquire = ms_card_wa_acquire; g_ucfg.wa_release = ms_card_wa_release; g_ucfg.pending = &g_upd_pending; g_ucfg.proof = &ST(61);
    g_env.updmagic = SGO_UPDMAGIC; g_env.updcfg = &g_ucfg;
    g_idcfg.cfg = ml_ident_cfg(); g_idcfg.opt = env_opt; g_idcfg.seed = env_seed; g_idcfg.save = ml_ident_save;
    g_env.idmagic = SGO_IDMAGIC; g_env.idcfg = &g_idcfg;
#endif
    g_pg_note_txt = "Loading, one moment.";
    g_pg_parent = -1;
    if (g_page >= 0) {
        g_pg_parent = g_page;
        page_close();
    }
    ST(ML_CFG_BASE + CFG_SPARE5) = 0;
    ST(ML_CFG_BASE + CFG_SPARE7) = 0;
    g_sg_try = 1;
    g_sg_retry = 0;
    g_sign_state = sg_hook_open(&g_sg, &g_env, "/ms/sign.ovl") == 0 ? 1u : 3u;
    if (g_sign_state == 3 && sg_retryable()) {                      /* busy now: keep the Loading page and open again (sign_frame) */
        g_sign_state = 1;
        g_sg_retry = 1;
        g_sg_t0 = g_sg_t1 = tb_now();
    }
    if (!g_sg.reused)                           /* a kept block is entered at the next poll: no loading page for that one frame */
        page_open((u32) PG_SIGN);
    ST(S_SG_STATE) = g_sign_state;
    ST(S_SG_ERR) = (u32) -g_sg.err;
    if (g_sign_state == 3 || g_sg_retry)
        sg_show(g_sign_state == 3);
    return 1;
}

#define SG_COVER_MAX 240u                       /* frames the loading cover may wait for the overlay's Event Match screen (a screen that never comes must not keep the menu black) */
static u32 g_sign_cov;                          /* 0 = no wait; else 1 + the frames the cover has waited (sign_frame) */
static u8 g_ev_entry;                           /* 1 from the scene's start until the entry prefetch was started (sign_reset sets it, ev_keep_tick spends it) */
static void sign_reset(void)
{
    u32 gi;
    g_ev_entry = 1;                             /* a new menu scene: the sign overlay is prefetched in its first frames (ev_keep_tick) */
    g_sign_state = 0;
    g_sign_req = 0;
    g_sg_retry = 0;
#if SYS_CTL
    g_sign_ctl = 0;
#endif
    g_sign_from_online = 0;
    g_sign_goto = 0;
    ST(S_SG_STATE) = 0;
    sg_hook_init(&g_sg, SDK_TABLE);
    SDK_CLOSE();
    for (gi = 0; gi < GH_N; gi++)
        GH[gi].used = 0;
#if ML_UI
    ft_n[2] = ft_n[3] = 0;
#endif
}

/* abort/close the overlay (B while loading or failed, the player left the hub, a forced close): frees the block, detaches nothing else (release build) */
static void sign_shutdown(void)
{
    if (g_sg.state != SG_H_IDLE)
        sg_hook_close(&g_sg);
    ST(S_SG_FREES) = g_sg.frees;
    g_sg_retry = 0;
    g_sign_cov = 0;
#if SYS_CTL
    g_sign_ctl = 0;
#endif
    g_sign_goto = 0;
    g_sign_state = 0;
    ST(S_SG_STATE) = 0;
}

/* ---- the Event-view screens' loading: the sign overlay's file is produced from the memory card in one blocking call at open
   (df_ovl_produce: 18 fields of frozen menu in Dolphin, more on a console card).  So the block is KEPT (sg_hook_park) when Friends / Game Night / Standings is left and
   the cursor is back on one of those rows, and PREFETCHED when the cursor rests on one of them; both only on the Main Menu and the hub, freed as soon as the cursor
   is on any other row or the menu shows another kind (the scene's heap then never holds it while something else is built). Budget: the block (
   block_bytes_min, about 177 KB), nothing else.  ML_EV_KEEP=0 builds today's behaviour. ---- */
#ifndef ML_EV_KEEP
#define ML_EV_KEEP 1
#endif
#ifdef ML_SETTINGS
#define ML_PRELOAD_ON() (ons_cur()->preload != 0)         /* Settings > Game > Menu Preload (default Off): gates only the start of a prefetch; a kept block (sg_hook_park) is unaffected */
#else
#define ML_PRELOAD_ON() 0
#endif
#define ML_EV_DWELL 20u                         /* menu frames on the row before the prefetch (a cursor passing over the row loads nothing) */
#if ML_EV_KEEP
static u32 g_ev_dwell;
static int ev_row(u32 kind, u32 hover)
{
    const struct ml_screen* s;
    if (kind != 0u && kind != (u32) ML_KIND_MORE)    /* */
        return 0;
    s = ml_screen_for_kind(kind);
    if (!s || hover >= s->nrows || s->rows[hover].act != ML_A_SIGN)
        return 0;
    return s->rows[hover].arg == (u8) SG_FRD || s->rows[hover].arg == (u8) SG_GN || s->rows[hover].arg == 25u;
}
/* ("why only when hovering? load it when entering the main menu"): the prefetch starts in the menu scene's first frames (the screen still fades in from black, so the
   card produce stall is not seen as a hitch on a row) and the block stays parked for the whole menu scene: every sign-overlay screen (Friends, Game Night, Standings, Settings Online)
   opens from it; the scene's end frees it with the heap (ml_enter -> sign_reset).  The hover dwell stays as the fallback (Menu preload setting) when the entry prefetch could not run. */
static void ev_keep_tick(u32 kind, u32 hover)
{
    int on = g_ok && g_kind == kind && ev_row(kind, hover);
    if (g_ev_entry && g_ok && g_sg.state == SG_H_IDLE && g_sg.ld.sdk && (kind == 0u || kind == (u32) ML_KIND_MORE)) {
        g_ev_entry = 0;
        g_ev_dwell = 0;
        (void) sg_hook_prefetch(&g_sg, "/ms/sign.ovl");
    }
    if (sg_hook_parked(&g_sg)) {
        g_ev_dwell = 0;
        if (g_sg.state == SG_H_PREFETCH)
            (void) sg_hook_poll(&g_sg);                          /* a prefetch runs to its end (never released mid-read): PREFETCH -> PARKED */
    } else if (on && g_sg.state == SG_H_IDLE && g_sg.ld.sdk && ML_PRELOAD_ON()) {
        if (++g_ev_dwell >= ML_EV_DWELL) {
            g_ev_dwell = 0;
            (void) sg_hook_prefetch(&g_sg, "/ms/sign.ovl");
        }
    } else
        g_ev_dwell = 0;
    ST(S_SG_BLOCK) = (g_sg.last_block & 0xFFFFFFu) | ((u32) g_sg.state << 24) | ((g_sg.reuses & 15u) << 28);    /* RAM proof: hook state 4 parked, 5 prefetching; reuses */
}
#else
static int ev_row(u32 kind, u32 hover) { (void) kind; (void) hover; return 0; }
static void ev_keep_tick(u32 kind, u32 hover) { (void) kind; (void) hover; }
#endif

/* the player left the overlay: back to the page it was opened from (Settings: cursor on Online / About) or, when the Slippi screen asked for it, to the Main Menu */
static void sign_leave(void)
{
    int par = g_pg_parent;
#ifdef ML_SETTINGS
    if (g_sg.st && g_sg.st->prog_try) {        /* the first run's progressive scan question (sg_link.c SGL_PROGQ / SGL_PROGK): Not now stores Off; the inline test (prog_ovl_tick) has stored its answer already; left in the middle of a test: the picture goes back and Off is stored */
        u32 pt = g_sg.st->prog_try;
        ((struct sg*) g_sg.st)->prog_try = 0;    /* used once */
        if (pt != SG_PT_DONE) {
            if (CFG(CFG_PROG_TRIAL) && CFG(CFG_PROG_TRIAL) != PROG_TRIAL_END) {
                CFG(CFG_PROG_TRIAL) = PROG_TRIAL_END;
                settings_apply();
            }
            (void) ons_change_prog(0u, ST(S_FRAME));
        }
    }
#endif
#if ML_GN_HANDOFF
    if ((ml_gn_state() == 1u || ml_gn_state() == 5u) && g_slip_back != 3u) {           /* (g_slip_back 3: the Friends list's Watch,: its character screen is entered from the Main Menu think and returns to Friends, not to a room)  Game Night: the lobby armed the call-up's room search and closed; 5 = MNC_GN_WATCH ("X: Last game") */
        sign_shutdown();
        go_gn_play();
        return;
    }
    if (g_st_back) {                                    /* Back from Standings that R opened on the Grandmaster character screen: that screen again, as the Grandmaster row starts it (the rank block is drawn by the hub overlay on entry) */
        g_st_back = 0;
        sign_shutdown();
        g_sign_cov = 0;
        H(MENU_COOLDOWN) = 10;
        go_mode(0u);
        return;
    }
#endif
    g_sign_cov = 0;
    if (ML_EV_KEEP && !g_sign_from_online && g_sg.state == SG_H_ACTIVE) {
        sg_hook_park(&g_sg);                                    /* keep the block for the next open of any sign-overlay screen in this scene (the scene's end frees it) */
        g_sign_goto = 0;
        g_sign_state = 0;
        ST(S_SG_STATE) = 0;
    } else
        sign_shutdown();
    SFX(0);
    H(MENU_COOLDOWN) = 10;
    if (g_sign_from_online) {
        g_sign_from_online = 0;
        {
            const struct ml_screen* mo = ml_screen_for_kind(ML_KIND_MORE);
            int r = 0, i;
            for (i = 0; mo && i < (int) mo->nrows; i++)
                if (mo->rows[i].act == ML_A_ONLINE)
                    r = i;
            MN_GOTO((int) ML_KIND_MORE, r, 3);                  /* More, Slippi row */
        }
    } else if (par >= 0) {
        g_pg_start_cur = g_pg_parent_cur;
        page_open((u32) par);
#if SYS_FIRST
        if (par == PG_SYS) {                                    /* Controller test, Entropy test: B on System goes on to Settings, cursor on the System row */
            g_pg_parent = PG_SETTINGS;
            g_pg_parent_cur = g_sys_gp_cur;
        }
#endif
    }
}

#ifdef ML_SETTINGS
/* The first run's inline progressive scan test (console the Settings page lost the first run): while the overlay is open the glue runs the SAME trial as Settings > Game
   (prog_trial_start -> settings_apply -> prog_apply_ex; the resident settings_tick counts the 10 s down and ends it) and answers the overlay through sg.prog_try / prog_wii (seconds left).
   Off is stored first (so a power cut in the test never asks again and never leaves On); A stores On, B or the timeout puts the interlaced picture back.  Never a black picture: the revert is the resident's. */
static void prog_ovl_tick(void)
{
    struct sg* g = (struct sg*) g_sg.st;
    u32 t;
    if (!g)
        return;
    switch (g->prog_try) {
    case SG_PT_YES:
        (void) ons_change_prog(0u, ST(S_FRAME));
        if (!VI_DTV()) {
            g->prog_try = SG_PT_DONE;           /* no cable after all: nothing to test */
            break;
        }
        prog_trial_start();
        g->prog_try = SG_PT_RUN;
        break;
    case SG_PT_RUN:
        t = CFG(CFG_PROG_TRIAL);
        if (t == 0u || t == PROG_TRIAL_END) {
            g->prog_try = SG_PT_DONE;           /* timed out (the resident put the picture back) */
            break;
        }
        g->prog_wii = (unsigned char) (t / 60u + 1u > 10u ? 10u : t / 60u + 1u);
        break;
    case SG_PT_KEEP:
        CFG(CFG_PROG_TRIAL) = 0;                /* kept: the same steps as A on the Settings row */
        (void) ons_change_prog(1u, ST(S_FRAME));
        settings_apply();
        g->prog_try = SG_PT_DONE;
        break;
    case SG_PT_BACK:
        CFG(CFG_PROG_TRIAL) = PROG_TRIAL_END;   /* B: not kept, the picture goes back */
        settings_apply();
        g->prog_try = SG_PT_DONE;
        break;
    default:
        break;
    }
}
#endif

static void sign_frame(void)
{
    H(MENU_COOLDOWN) = 2;
#ifdef ML_SETTINGS
    if (g_sign_state == 2)
        prog_ovl_tick();
#endif
    if (g_sign_state == 1 && g_sg_retry) {                          /* the open was refused as busy: every half second the same open again, for up to 30 s */
        u32 now = tb_now();
        if (now - g_sg_t1 >= SG_RETRY_GAP) {
            g_sg_t1 = now;
            g_sg_try++;
            sg_hook_close(&g_sg);
            if (sg_hook_open(&g_sg, &g_env, "/ms/sign.ovl") == 0) {
                g_sg_retry = 0;
                ST(S_SG_ERR) = 0;
                sg_show(0u);                                          /* loading goes on (sg_hook_poll below, next frame) */
            } else if (!sg_retryable() || now - g_sg_t0 > SG_RETRY_MAX) {
                g_sg_retry = 0;
                g_sign_state = 3;
                ST(S_SG_STATE) = 3;
                ST(S_SG_ERR) = (u32) -g_sg.err;
                sg_show(1u);
            } else {
                ST(S_SG_ERR) = (u32) -g_sg.err;
                sg_show(0u);
            }
        }
        return;
    }
    if (g_sign_state == 1) {
        int st = sg_hook_poll(&g_sg);
        if (st == SG_H_ACTIVE) {
            g_sign_state = 2;
            ST(S_SG_LOADS) = g_sg.loads;
            ST(S_SG_BLOCK) = g_sg.last_block;
            ST(S_SG_STATE) = 2;
#if SYS_CTL
            if (g_sign_ctl && g_sg.api && g_sg.api->goto_screen)
                g_sign_goto = SG_CTL + 1u;                      /* straight to the Controller test; Back leaves the overlay */
#endif
            if (g_sign_goto && g_sg.api && g_sg.api->goto_screen)
                g_sg.api->goto_screen(g_sign_goto - 1u);        /* Controller test, or the Mockups page (Main Menu row) */
            g_sign_goto = 0;
            if (g_page >= 0) {                                  /* the loading page stays as a bare black cover (its texts go: they are scarce) until the overlay says its Event Match screen is built and drawn (sg.cover, set by
                                                                  the first overlay frame below): never a frame of the hub, the bare Event Match screen or the text view in between (the blip between a match and the room) */
                page_texts_free();
                if (g_pg_back) {                                /* fully opaque now (over the Settings globe it is 75 percent): the Event Match screen under it is built and animates meanwhile */
                    int bk;
                    for (bk = 0; bk < 4; bk++)
                        ((u8*) g_pg_back)[0x2C + bk] = col_bg[bk];
                }
                g_sign_cov = 1u;
            }
        } else if (st == SG_H_FAILED) {
            g_sign_state = 3;
            ST(S_SG_STATE) = 3;
            ST(S_SG_ERR) = (u32) -g_sg.err;
            sg_show(1u);
        }
    }
    if (g_sign_state == 2) {
        struct sdent_pad p;
        sign_pad(&p);
        if (sg_hook_frame(&g_sg, &p, tb_now())) {
            sign_leave();
            return;
        }
        sg_hook_present(&g_sg);
        ST(S_SG_SCR) = g_sg.st ? g_sg.st->scr : 0;
        if (g_sign_cov) {                                       /* the overlay's frame above ran: its cover flag is current.  Screens that draw their own backdrop answer 0 at once (the page goes the same frame as before) */
            if (!g_sg.st || !g_sg.st->cover || ++g_sign_cov > SG_COVER_MAX) {
                g_sign_cov = 0;
                if (g_page >= 0)
                    page_close();
            }
        }
    }
}

/* ML_A_SIGN rows (hub Game Night SG_GN, hub Standings SG_STAND; Main Menu Mockups SG_MOCK only with -DML_MOCKUPS=1): the sign overlay opened on page `scr`; the loading note is the usual one, Back from the
   Mockups page leaves the overlay and lands on the Main Menu with the cursor still on the row (the menu scene never left) */
static void mock_open(u32 scr)
{
    if (g_sign_state != 0 || scr >= SG_NSCREEN)
        return;
    SFX(1);
    g_ev_hdr = scr == (u32) SG_STAND ? 1u : scr == (u32) SG_FRD ? 2u : 0u;
    g_ev_par = B(MENU_FLOW);
    g_sign_goto = scr + 1u;
    if (scr == (u32) SG_GN && g_ev_room) {                  /* a toast's room (ms_net_cost 101: code + 1): the browser opens in it */
        g_sign_goto = ((u32) SG_GN | SG_GOTO_ROOM) + 1u + (g_ev_room << 16);
        g_ev_room = 0;
    }
    if (!sign_open())
        g_sign_goto = 0;
}

void ml_sign_request(void)      /* the entry the Slippi screen's log-in row calls: opens the overlay at the next menu frame */
{
    g_sign_req = 1;
    ST(S_SG_REQ)++;
}
#endif /* ML_PART_OVL */
#else /* !ML_SIGN */
#if ML_PART_OVL
static int sign_open(void) { return 0; }
static void sign_shutdown(void) { g_sign_state = 0; }
static void sign_frame(void) { }
static void sign_reset(void) { g_sign_state = 0; g_sign_req = 0; g_sign_from_online = 0; }
static void ev_keep_tick(u32 kind, u32 hover) { (void) kind; (void) hover; }
static void mock_open(u32 scr) { (void) scr; }
void ml_sign_request(void) { }
#endif
#endif /* ML_SIGN */
#endif /* ML_SETTINGS */
#if !defined(ML_SETTINGS) && ML_PART_OVL
static void mock_open(u32 scr) { (void) scr; }     /* no Settings wiring in this build: the Mockups row does nothing (as a stub row) */
#endif

#if defined(ONS_NETSEL) && ML_PART_OVL
/* Frame rate readout (lab/dev builds): every 60 menu frames the time base gives the milliseconds they took; frames per second x10 = 600000 / ms.  One short text on the front canvas,
   rebuilt once a second (a free and a new text per second); a scene change kills it with the heap (drop_all). */
static u32 g_tr_or, g_hd_or;                                   /* the buttons seen since the last readout update */
static char* pg_put_hex4(char* d, u32 v)
{
    static const char hx[] = "0123456789ABCDEF";
    *d++ = hx[(v >> 12) & 15u]; *d++ = hx[(v >> 8) & 15u]; *d++ = hx[(v >> 4) & 15u]; *d++ = hx[v & 15u];
    return d;
}
/* The network line (lab/dev builds, always on, rebuilt once a second): N glue state (hex), A / G / D our address, gateway, dns (low halves, hex), H dhcp state, tries, link byte, F frames sent / received by sdnet,
   O the sign-in overlay (0 idle, 1 loading, 2 up, 3 failed) and its open tries, S seconds since boot.  A stall before the network starts (no link, no lease) or before the overlay loads shows here. */
#ifndef ML_OVL
extern int br_line_up(void) __attribute__((weak));               /* br_glue.c: the notice line under the header is on screen */
#endif
#ifdef ML_SIGN
static u32 g_nd_lob;                           /* the line was last placed for the Game Night lobby (1) or not (0) */
static u32 nd_lobby(void)                      /* the Game Night browser is on its lobby screen (GNB_LOBBY = 4 in the low nibble of the browser's proof word ml_st[69]) */
{
    return g_sign_state == 2 && ST(S_SG_SCR) == (u32) SG_GN && (ST(69) & 15u) == 4u;
}
#endif
static void nd_tick(void)
{
    unsigned w[16];
    char buf[72], *d = buf;
#ifndef ML_OVL
    extern void ms_net_dbg(unsigned* w) __attribute__((weak));
#endif
    u32* tx;
    if (!ms_net_dbg)
        return;
    if (!ons_cur()->dbgl) {                        /* Settings > Diagnostics > Debug Lines (default Off): no debug text at all */
        if (g_nd_txt) {
            TEXT_FREE(g_nd_txt);
            g_nd_txt = 0;
        }
        return;
    }
    ms_net_dbg(w);
    d = pg_put(d, "N");
    d = pg_put_hex4(d, w[0]);
    d = pg_put(d, " A");
    d = pg_put_hex4(d, w[1]);
    d = pg_put(d, " G");
    d = pg_put_hex4(d, w[3]);
    d = pg_put(d, " D");
    d = pg_put_hex4(d, w[4]);
    d = pg_put(d, " H");
    d = pg_put_u(d, w[5] & 0xFFu);
    d = pg_put(d, ".");
    d = pg_put_u(d, (w[5] >> 8) & 0xFFu);
    d = pg_put(d, " F");
    d = pg_put_u(d, w[6]);
    d = pg_put(d, " ");
    d = pg_put_u(d, w[7]);
    d = pg_put(d, " S");
    d = pg_put_u(d, env_now_ms() / 1000u);
#ifdef SD_CARD_HOLE
    {
#ifndef ML_OVL
        extern u32 df_dbg[] __attribute__((weak));
#endif
        if (df_dbg && (df_dbg[1] | df_dbg[2] | df_dbg[3])) {   /* Z: the last blocking step and the counts of timeouts (card sync. retail read. DVD checks dropped), only once something timed out */
            d = pg_put(d, " Z");
            d = pg_put_hex4(d, df_dbg[0]);
            d = pg_put(d, " ");
            d = pg_put_u(d, df_dbg[1]);
            d = pg_put(d, ".");
            d = pg_put_u(d, df_dbg[2]);
            d = pg_put(d, ".");
            d = pg_put_u(d, df_dbg[3]);
        }
    }
#endif
    *d = 0;
    if (g_nd_txt) {
        TEXT_FREE(g_nd_txt);
        g_nd_txt = 0;
    }
    {   /* ONE compact line under the page header, never over the bar or the rows: on the Melee menu screens in the band above the first row (below the notice line when it is up), on our own pages in the empty row 2 */
        u32 y = (g_page >= 0 || g_sign_state != 0) ? 0xC129999Au : (br_line_up && br_line_up()) ? 0xC0E147AEu : 0xC0F9999Au, x = 0;
#ifdef ML_SIGN
        if (g_sign_state == 2 && (g_ev_cur || ST(S_SG_SCR) == (u32) SG_GN)) {           /* an Event-view screen (Friends, Standings, Game Night): its header tab strip is in the band, so the line goes above it, right of the mark */
            y = 0xC13B3333u;                           /* -11.7 */
            x = 0x3FCCCCCDu;                           /* +1.6 */
            if (nd_lobby()) {                          /* the Game Night lobby: the room title row (Room 4242, Random Stages) fills the top band, so the line goes in the empty band under the footer panel */
                y = 0x41300000u;                       /* +11.0: text centre about row 497, clear of the footer panel (ends about 476) and of the bottom overscan */
                x = 0;
            }
        }
#endif
#ifdef ML_SIGN
        g_nd_lob = nd_lobby();
#endif
        tx = text_new_at(x, y, F_PG_SIZE, col_gold);
        if (tx) {
            ((u8*) tx)[0x49] = 1;
            ((u8*) tx)[0x4A] = 1;                  /* centred */
            ml_sis_add(tx, zero_xy, "%s", buf);
            g_nd_txt = tx;
        }
    }
}
static void fps_tick(void)
{
    if (g_fps_on) {
        g_tr_or |= W(CTRL_ALL + 0xCu);
        g_hd_or |= W(0x804C1FACu);
    }
    if (g_page == PG_SYS && (ST(S_FRAME) & 63u) == 0) {
        int k;
        for (k = 0; k < 2; k++)
            pg_row_build(g_page, SYS_I_NET + k);
    }
    if (g_rst_until && (ST(S_FRAME) >= g_rst_until || g_page != PG_SYS)) {    /* the prompt timed out (3 s) or the page was left (B): back to "Restart" */
        g_rst_until = 0;
        if (g_page == PG_SYS)
            pg_row_build(g_page, SYS_I_RST);
    }
    static u32 t0, n;
    u32 t, ms, f10;
    char buf[56], *d = buf;
    if (!g_fps_on && g_fps_txt) {
        TEXT_FREE(g_fps_txt);
        g_fps_txt = 0;
    }
    if (n == 0) {
        __asm__ volatile("mftb %0" : "=r"(t0));
    }
#ifdef ML_SIGN
    if (g_nd_txt && nd_lobby() != g_nd_lob && n < 59u)     /* entering or leaving the Game Night lobby moves the debug line: remake it at once, not up to a second late */
        n = 59u;
#endif
    if (++n < 60u)
        return;
    __asm__ volatile("mftb %0" : "=r"(t));
    n = 0;
    nd_tick();
    if (!g_fps_on)
        return;
    ms = (t - t0) / 40500u;
    f10 = ms ? 600000u / ms : 0u;
    d = pg_put(d, "FPS ");
    d = pg_put_u(d, f10 / 10u);
    *d++ = '.';
    d = pg_put_u(d, f10 % 10u);
    /* INPUT PROBE (Slippi's submenu stuck after a match; the cause is not found): the menu kind, the cursor, the menu input cooldown, and over the last second the
       buttons the game saw TRIGGERED (what the menus act on, all ports) and HELD (port 1).  Stuck with the held word showing B and no B in the triggered word = the menu never gets the press. */
    d = pg_put(d, " K");
    d = pg_put_u(d, B(MENU_FLOW));
    d = pg_put(d, " H");
    d = pg_put_u(d, H(MENU_FLOW + 2u));
    d = pg_put(d, " C");
    d = pg_put_u(d, H(MENU_COOLDOWN));
    d = pg_put(d, " T");
    d = pg_put_hex4(d, g_tr_or);
    d = pg_put(d, " P");
    d = pg_put_hex4(d, g_hd_or);
    g_tr_or = g_hd_or = 0;
    *d = 0;
    if (g_fps_txt) {
        TEXT_FREE(g_fps_txt);
        g_fps_txt = 0;
    }
    {
        u32* tx = text_new_at(0xC1840000u, 0xC1500000u, F_PG_SIZE, col_gold);      /* -16.5, -13.0 */
        if (tx) {
            ml_sis_add(tx, zero_xy, "%s", buf);
            g_fps_txt = tx;
        }
    }
}
#endif

#if ML_PART_OVL
/* The description line in Melee's own text object.  The stream is built like one of Melee's own strings: the control codes in front of the first character and after the last one are
   copied from a retail description (the Main Menu's first, "Solo Smash!": position, colour, scale and the end marks that give the bar its size, weight, condensing and shadow), our characters are
   encoded in between by Melee's own encoder (HSD_SisLib_803A67EC).  Stream ops (fn_803A6FEC): 7, 10, 14 = 5 bytes; 12 = 4; 11, 13, 15 = 1; 0 = end; anything else is a two byte character. */
static u8 g_sis_line[160];
static int sis_op_len(u8 op)
{
    return (op == 7 || op == 10 || op == 14) ? 5 : op == 12 ? 4 : (op == 11 || op == 13 || op == 15) ? 1 : 0;
}
static int sis_line(const u32* dt, const char* ascii)
{
    u32 tab = W(0x804D1124u + (u32) B((u32) dt + 0x4Fu) * 4u), rdesc = W(TAB + (u32) ML_KIND_MISC * 0x14u + 0x08u), id, r;
    const u8* s;
    u8 enc[128];
    int n, i = 0, pre = 0, post = 0, last_char_end = -1;
    if (!tab || !rdesc)
        return 0;
    id = H(rdesc);
    r = W(tab + 4u * id);
    if (!r)
        return 0;
    s = (const u8*) r;
    while (i < 120 && s[i]) {                      /* find where the characters begin and where they end */
        int l = sis_op_len(s[i]);
        if (l) i += l;
        else {
            if (!pre) pre = i;
            i += 2;
            last_char_end = i;
        }
    }
    if (!pre || last_char_end < 0 || i >= 120)
        return 0;
    post = i - last_char_end;                       /* codes after the last character */
    n = ((int (*)(u8*, const u8*)) 0x803A67ECu)(enc, (const u8*) ascii);
    if (n <= 0 || pre + n + post + 1 > (int) sizeof g_sis_line)
        return 0;
    for (i = 0; i < pre; i++) g_sis_line[i] = s[i];
    for (i = 0; i < n; i++) g_sis_line[pre + i] = enc[i];
    for (i = 0; i < post; i++) g_sis_line[pre + n + i] = s[last_char_end + i];
    g_sis_line[pre + n + post] = 0;
    return 1;
}

#ifdef ML_SETTINGS
/* Music = Random: the three MENU slots of the pointer table (menu01, menu02, menu3) follow the stored setting; Random fills them with songs of the 38-candidate pool (menu + stage themes); the stage slots are never written.  Once per overlay load (the static starts at "unknown", so a Main Menu
   entry in Random draws a new order) and at every change of the setting, including the late arrival of the card record.  The seed is the time base: local, never Melee's RNG. */
static u32 g_music_seen = 0xFFu;
static void music_pool_keep(void)             /* the slots stay as they are (they already hold the playing song); the overlay's frame-time sync below then draws nothing for this load */
{
    const struct ms_settings* c = ons_cur();
    g_music_seen = (!c->musicoff && c->musicrand) ? 1u : 0u;
}
static void music_pool_sync(void)
{
    const struct ms_settings* c = ons_cur();
    u32 mode = (!c->musicoff && c->musicrand) ? 1u : 0u, t;
    if (g_music_seen == mode)
        return;
    __asm__ volatile("mftb %0" : "=r"(t));
    if (ml_music_apply((volatile unsigned*) ML_MUSIC_TAB, mode, t ^ (ST(S_FRAME) * 0x9E3779B1u)) && mode && g_music_seen != 0xFFu)
        ((int (*)(int)) 0x80023F28u)(52);     /* a change to Random INSIDE the menu (Settings, or the card record arriving after the scene's start): the drawn song starts now
                                                 (lbAudioAx_80023F28 restarts only when the name differs), so the next character screen finds it playing (Dolphin css_lmZ2: the
                                                 first CSS after the Settings switch changed the song) */
    g_music_seen = mode;
}
#endif

#if defined(ML_SIGN) && ML_UI
/* ---- VARIANT Z: the "(Z) Friends" hint and the Z tap, drawn by THIS overlay since.  The hub overlay drew it before, but the hub
   overlay is loaded only once the menu scene's own reads are done (hub_res.c: the card read waits for an idle card / DVD, up to HUB_WAIT_MAX passes) and then held it 0.5 s more
   (its toast settle time), so the hint appeared up to a second after the Main Menu.  The menu overlay is loaded before the Main Menu is drawn: the hint is made on the Main Menu's
   first frame (plain "Friends" until the counts are known) and follows the counts live (ms_net_cost 106 / 105, the Friends panel's source; the resident keeps them only while fresh).
   The hub overlay tells this one about its toasts through od.gn_lobby: bit 3 = a toast is on screen (the hint is hidden: the toast stands there), bit 1 = that toast waits for Z
   (Z is the toast's: hold Z accepts).  A TAP of Z (a new press on any pad) opens Friends with the old row's gate. ---- */
#if __has_include("od_glue.h")
#include "od_glue.h"                   /* staged build tree (offline/s17/net) */
#else
#include "../od/od_glue.h"             /* the source tree */
#endif
#define MZ_RIGHT_Q 891290              /* 13.6 units, Q16: the hint's right edge (the menu frame's right line is at x 14.4) */
#define MZ_Y_Q (-789709)               /* -12.05 units, Q16: level with the logo emblem (rows 14..39 of the 528-row frame) */
#define MZ_MAXW_Q (20 * 65536)         /* wider than this: the font shrinks (never at 'Friends (99 online, 99 requests)', about 17.5 units) */
#define MZ_FONT 280u                   /* 0.028: the toast / hub key-hint font */
#define MZ_PAD_COPY 0x804C20BCu        /* HSD_PadCopyStatus[4], 0x44 bytes: +8 newly pressed, +0x41 err */
#define MZ_PAD_Z 0x10u
static u32* g_mz[3];
static char g_mz_s[48];
static u8 g_mz_n;
static const struct od* mz_od(void)
{
#ifdef SD_CARD_HOLE
    char* c = (char*) ml_upd_net();
    return c ? (const struct od*) (c - __builtin_offsetof(struct od, m)) : 0;
#else
    return 0;
#endif
}
static void mz_forget(void)
{
    g_mz[0] = g_mz[1] = g_mz[2] = 0;
    g_mz_n = 0;
    g_mz_s[0] = 0;
}
static void mz_free(void)
{
    u32 i;
    for (i = 0; i < 3u; i++)
        if (g_mz[i]) {
            TEXT_FREE(g_mz[i]);
            g_mz[i] = 0;
        }
    g_mz_n = 0;
    g_mz_s[0] = 0;
}
static char* mz_put(char* d, const char* s) { while (*s) *d++ = *s++; return d; }
static char* mz_num(char* d, u32 v) { char b[4]; int n = 0; if (v > 99u) v = 99u; do { b[n++] = (char) ('0' + v % 10u); v /= 10u; } while (v); while (n) *d++ = b[--n]; return d; }
static void mz_text(char* o)
{
    u32 on = ml_net_cost(106) & 0xFFu, rq = ml_net_cost(105) & 0xFFu;
    char* p = mz_put(o, "Z: Friends");
#ifdef HZ_TEST                         /* LAB ONLY: cycle the five looks every 4 s (Dolphin close-ups); never in a release */
    { static const u8 tc[5][2] = { { 0, 0 }, { 6, 0 }, { 6, 1 }, { 0, 1 }, { 99, 99 } }; u32 k = (env_now_ms() / 4000u) % 5u; on = tc[k][0]; rq = tc[k][1]; }
#endif
    if (on || rq) {
        p = mz_put(p, " (");
        if (on) { p = mz_num(p, on); p = mz_put(p, " online"); }
        if (on && rq) p = mz_put(p, ", ");
        if (rq) { p = mz_num(p, rq); p = mz_put(p, rq == 1u ? " request" : " requests"); }
        *p++ = ')';
    }
    *p = 0;
}
static u32* mz_t(int x_q, int y_q, unsigned fe, const u8* col, unsigned align, const char* s)
{
    char buf[64];
    u32* t = text_new_at(tp_f2(x_q, 16), tp_f2(y_q, 16), tp_f2((int) (fe * 6554u / 1000u), 16), col);
    if (!t)
        return 0;
    ((u8*) t)[0x4A] = (u8) align;
    ml_sis_add(t, zero_xy, "%s", ml_sis_expand(buf, sizeof buf, s) ? buf : s);
    return t;
}
static void mz_draw(void)
{
    static const u8 white[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
    struct kh_seg g;
    struct kh_pair p;
    char s[48];
    u32 i, k;
    unsigned fe = MZ_FONT;
    int x0;
    mz_text(s);
    for (k = 0; s[k] == g_mz_s[k] && s[k]; k++)
        ;
    if (g_mz_n && s[k] == g_mz_s[k])
        return;                                                /* drawn, the same text */
    mz_free();
    if (!kh_parse(s, &g))
        return;
    kh_pair_build(&g, fe, &p);
    if (p.w > MZ_MAXW_Q) {
        fe = (unsigned) ((u32) fe * (u32) (MZ_MAXW_Q / 256) / (u32) (p.w / 256));
        kh_pair_build(&g, fe, &p);
    }
    x0 = MZ_RIGHT_Q - p.w - p.w / 12;                          /* right aligned on the glyph table's estimate (within about 8 percent): 1/12 more margin */
    for (i = 0; i < p.n && g_mz_n < 3u; i++) {
        const struct kh_item* it = &p.it[i];
        u32* t;
        if (it->kind == KH_I_PLATE) {
            u8 ink[4];
            u32 ic = kh_ink_rgb(it->key);
            ink[0] = (u8) (ic >> 16); ink[1] = (u8) (ic >> 8); ink[2] = (u8) ic; ink[3] = 0xFF;
            t = mz_t(x0 + it->x, MZ_Y_Q + it->dy, it->fe4, ink, 1, it->s);
            if (t)
                kh_attach(t, it);
        } else
            t = mz_t(x0 + it->x, MZ_Y_Q, fe, white, 0, it->s);
        if (!t) {
            mz_free();
            return;
        }
        g_mz[g_mz_n++] = t;
    }
    for (k = 0; s[k] && k < sizeof g_mz_s - 1u; k++)
        g_mz_s[k] = s[k];
    g_mz_s[k] = 0;
}
static void mz_frame(u32 kind)
{
    const struct od* o = mz_od();
    u32 lob = o ? o->gn_lobby : 0u, i, z = 0;
    int on = g_ok && (kind == 0u || kind == (u32) ML_KIND_MORE) && g_sign_state == 0 && g_page < 0 && !(lob & 8u);     /* the Main Menu and More; not on a Settings page (g_page), not on the Original menu */
    if (!on) {
        if (g_mz_n)
            mz_free();
        return;
    }
    mz_draw();
    if (lob & 2u)
        return;                                                /* a toast waits for Z: the Z is the toast's */
    for (i = 0; i < 4u; i++)
        if (B(MZ_PAD_COPY + i * 0x44u + 0x41u) == 0u && (W(MZ_PAD_COPY + i * 0x44u + 8u) & MZ_PAD_Z))
            z = 1;                                             /* a new press (the copy's trigger word), only ports with a pad */
    if (z) {
        int og = ml_offline_hook(1);
        if (og == 2)
            open_update_required();
        else if (og || (ml_net_cost(107) & 9u) || ml_online_data_missing(ml_net_cost(107))) {
            SFX(3);
            bar_refuse(H(MENU_FLOW + 2u), ml_net_cost(107));
        } else {
            mz_free();
            mock_open((u32) SG_FRD);
        }
    }
}
#else
static void mz_frame(u32 kind) { (void) kind; }
static void mz_forget(void) { }
#endif

#ifdef ML_OVL
static void mlo_frame(void)
#else
void ml_frame(void)
#endif
{
    u32 kind = B(MENU_FLOW + 0u), hover = H(MENU_FLOW + 2u);
    const struct ml_screen* s = g_ok ? ml_screen_for_kind(kind) : 0;
    mz_frame(kind);                                  /* VARIANT Z: the "(Z) Friends" hint from the Main Menu's first frame, and the Z tap */
#ifdef ML_HEAPPROBE                                  /* LAB ONLY (menu overlay built with -DML_HEAPPROBE): the HSD heap's free list every frame: total free -> ml_st[69], largest free cell -> ml_st[71] (CFG_SPARE5 / CFG_SPARE7) */
    {
        u32 arr = W(0x804D7360u), d, c, tot = 0, mx = 0, n = 0;
        int id = ((int (*)(void)) 0x80375404u)();
        d = arr + (u32) id * 12u;
        for (c = arr ? W(d + 4u) : 0; c && n < 4096u; c = W(c + 4u), n++) {
            u32 sz = W(c + 8u);
            tot += sz;
            if (sz > mx)
                mx = sz;
        }
        ST(69) = tot;
        ST(71) = mx;
    }
#endif
    g_bar_gen_prev = g_bar_gen_cur;
    g_bar_gen_cur = ST(S_BAR);
#ifdef ONS_NETSEL
    fps_tick();
#endif
#ifndef ML_OVL                              /* the overlay variant does the frame counter and the settings poll in the resident ml_frame (ml_res.c) */
    ST(S_FRAME)++;
    ST(S_HOVER) = hover;
#endif
#if ML_UI && defined(ML_OVL)
    pgl_tick();
#endif
#if ML_UI
    {   /* the Multishine mark on every menu scene our layer runs, the same artwork and brightness rule as the Settings pages' mark (online bright, offline a step dimmer: mark_level_tick), at the place of the overlay pages' mark; hidden while a page or the overlay is up (they draw their own) */
        u32 want = g_ok && g_page < 0 && g_sign_state == 0;
        mark_level_tick();
        if (want && !g_mmark)
            g_mmark = ui_mark_menu();
        else if (!want && g_mmark) {
            ui_free(g_mmark);
            g_mmark = 0;
        }
    }
#endif
#ifdef ML_SETTINGS
    music_pool_sync();
    {
#ifndef ML_OVL
        unsigned ev = ons_poll(ST(S_FRAME));       /* pending card read / due card write: menu scene only, bounded, never blocks the menu for long */
        if (ev & ONS_EV_LOADED)
            settings_apply();
        settings_tick();
        settings_mirror();
#endif
        if (g_sign_req && g_sign_state == 0 && g_ok) {
            g_sign_req = 0;
            (void) sign_open();
        }
#if defined(UPD_TITLE) && defined(HUB_OVL_RES) && defined(ML_SIGN)
        {   /* update check on the title (hub_res.c): START found an update: straight into the Update offer; Back = Later, the Main Menu */
#ifndef ML_OVL
            extern int ut_menu_frame(unsigned int kind, int settled, unsigned int sign_state);
            extern int fr_menu_frame(unsigned int kind, int settled, unsigned int sign_state);
#endif
            int utr = ut_menu_frame(kind, g_kind == kind && g_settle >= SETTLE, g_sign_state);
            if (utr == 2)                                                      /* an older resident's 'START while checking' hold: no page */
                utr = 0;
            if (utr == 3 && g_ut_hold) {                                       /* nothing newer, failed or the cap: the Main Menu continues */
                g_ut_hold = 0;
                if (g_page == PG_SIGN) { g_pg_parent = -1; page_close(); }
            }
            if (utr == 1 && g_ut_hold) {                                       /* the answer: the offer at once */
                g_ut_hold = 0;
                if (g_page == PG_SIGN) { g_pg_parent = -1; page_close(); }
            }
            if (utr == 1 && g_sign_state == 0 && g_ok) {
                g_sign_goto = SG_UPD + 1u;
                if (!sign_open())
                    g_sign_goto = 0;
            } else if (kind == 0u && g_kind == kind && g_settle >= SETTLE && g_page < 0 && g_sign_state == 0 && g_ok && pq_frame(kind, g_kind == kind && g_settle >= SETTLE)) {
                g_sign_goto = (SG_LINK | SG_GOTO_PROG) + 1u;          /* FIRST step of the first run: the progressive scan question, before the system files and the link offer, so the rest is shown in the better picture; once per boot, only for an unset setting and a cable */
                if (!sign_open())
                    g_sign_goto = 0;
            } else if (kind == 0u && g_kind == kind && g_settle >= SETTLE && !g_sysf_try && !prog_load_pending() && g_sign_state == 0 && g_ok && g_page < 0 && ml_online_data_missing(ml_net_cost(107)) && !(ml_net_cost(107) & 1u)) {
                void* probe;
                g_sysf_try = 1;                                                /* MultishinePack is missing and the console is online: the system files are set up now, by themselves; once per start, a failure is retried at the next start */
                probe = OS_ALLOC(HSD_HEAP(), SYSF_NEED);                       /* the lean build needs about 0.5 MB at its peak plus the 0.5 MB download buffer (Memory).  Without room nothing opens: the menu rows keep saying the files are not set up */
                if (probe) {
                    OS_FREE(HSD_HEAP(), probe);
                    g_sign_goto = (SG_UPD | SG_GOTO_SYSF) + 1u;
                    if (!sign_open())
                        g_sign_goto = 0;
                }
            } else if (g_page < 0 && !prog_load_pending() && fr_menu_frame(kind, g_kind == kind && g_settle >= SETTLE, g_sign_state) && g_sign_state == 0 && g_ok) {
                g_sign_goto = (SG_LINK | SG_GOTO_OFFER) + 1u;          /* the first-run offer: after the progressive scan question and the update offer, once per boot */
                if (!sign_open())
                    g_sign_goto = 0;
            }
        }
#endif
        {
            u32 want = g_sign_state == 2 ? g_ev_hdr : 0u;
            if (want != g_ev_cur)
                tab_event(want, kind);
        }
        if (g_sign_state)
            sign_frame();
        else
            ev_keep_tick(kind, hover);              /* : the kept / prefetched block of the sign overlay */
#if ML_GN_HANDOFF
        if (g_sign_state == 3 && g_gn_retry && g_sg.err == SGO_E_NOFILE) {      /* the reopen after a set came before the card's file table: try again (3 times) */
            g_gn_retry--;
            sign_shutdown();
            page_close();
            g_gn_open = 1u;
        }
#endif
        if (g_sign_state == 2)
            return;
    }
#endif
#if ML_GN_HANDOFF
    if (g_gn_open && kind == 0u && g_sign_state == 0 && g_ok) {   /* back from a Game Night set or watch: the browser again (it resumes the room from the hub client's hand-off block).
                                                                          Ahead of the page and settle gates: the hub's first frame is already covered ( "hub flash, fix"; build 557 showed the hub
                                                                          for the 40 settle frames after a watch, Dolphin hf_fix runs).  The room opens SETTLE frames in as before, once the hub overlay has taken
                                                                          the kept result report (ms_net_cost 102), 20 frames later at the latest */
#if defined(ML_SETTINGS) && defined(ML_SIGN)
        if (g_page < 0)
            page_open((u32) PG_SIGN);                   /* the black loading page over the hub */
#endif
        if (++g_gn_open > SETTLE + 1u && (g_gn_open > SETTLE + 20u || !ml_net_cost(102))) {
            g_gn_open = 0;
            if (g_page >= 0)
                page_close();                           /* not the room's parent page: leaving the room must not land on the empty loading page (sign_open opens its own) */
            mock_open(g_gn_scr ? g_gn_scr - 1u : (u32) SG_GN);          /* (g_gn_scr stays for the retry below; the next menu scene clears it) */
#if defined(ML_SETTINGS) && defined(ML_SIGN)
            if (g_sign_state && g_page < 0)
                page_open((u32) PG_SIGN);               /* a parked block opens without its loading page: keep the hub covered until the overlay's first frame */
#endif
        }
        return;
    }
#endif
    if (g_page >= 0) {
#ifdef ML_SETTINGS
        if (g_page != PG_SIGN && (!s || kind != g_page_kind)) {
#else
        if (!s || kind != g_page_kind) {
#endif
            page_close();
        } else {
            page_frame();
            return;
        }
    }
    if (!s || !s->text) {
        if (g_kind != 255u) {
            drop_all(1);
            g_kind = 255u;
            g_settle = 0;
        }
        return;
    }
    if (kind != g_kind) {
        drop_all(1);
        g_kind = kind;
        g_settle = 0;
    }
    if (g_settle < SETTLE) {
        g_settle++;
        return;
    }
    g_built = hover;
    if (g_pv_hov != ((kind << 16) | hover))
        pv_box(kind, hover);                /* the box takes the colour of the frame */
#ifdef ML_SIGN
    if (kind == (u32) ML_KIND_MORE && g_sign_state == 0 && (g_news_now() != g_news || ml_net_cost(107) != g_panel_st))      /* on More: the Friends counts or the online state changed, its panel again */
        tex_prepare(kind);
    if (kind == 0u && g_sign_state == 0) {                  /* the online state changed: the server rows' panel is made again; a toast's Game Night room: the browser opens in it */
        u32 room;
        if (g_news_now() != g_news || ml_net_cost(107) != g_panel_st)       /* the Friends counts (More's panel lists Friends) or the online state changed: the right panel is composed again */
            tex_prepare(0);
        room = ml_net_cost(101);
        if (room == ML_ROOM_FRIENDS && g_ok && g_page < 0) {    /* VARIANT Z: the hub overlay's hold Z on the Main Menu (hub_soc.c hz_frame): Friends, with the gate the Friends row had */
            int og = ml_offline_hook(1);
            if (og == 2)
                open_update_required();
            else if (og || (ml_net_cost(107) & 9u) || ml_online_data_missing(ml_net_cost(107))) {
                SFX(3);
                bar_refuse(hover, ml_net_cost(107));
            } else
                mock_open((u32) SG_FRD);
            room = 0;
        }
        if (room && room <= 10000u && g_ok) {
            g_ev_room = room;                                   /* code + 1 */
            mock_open((u32) SG_GN);
        }
    }
#endif
    {   /* the description of the hovered row goes into the RETAIL description text object (our own text object looked thin and small next to Melee's bar texts).  The resident hook
           (ml_text_hook) notes the object when Melee sets it to the blank string (ST(S_DEBUG), generation ST(S_BAR)).  Appending to that object crashes (its SIS block is Melee's), so the line is
           made in a hidden text of ours (same SisLib call) and its SIS buffer pointer is handed to Melee's object: size, weight, colour, shadow, centring and the dark box stay Melee's.
           Acts only after a hook generation of THIS screen (the object of the screen before is freed), re-made when the hover, the unread count or the generation changed. */
        u32* dt = (u32*) ST(S_DEBUG);
        u32 ss = 0, gen = ST(S_BAR);
#ifdef ML_SETTINGS
        ss = g_sign_state;
        if (g_page >= 0)
            ss |= 0x100u;                                   /* a page is up: the line stays blank */
#endif
        if (gen != g_bar_seen)
            g_bar_arm = 1;
        if (dt && (g_bar_arm || ((g_bar_sign ^ ss) & 0x100u)) && (gen != g_bar_seen || g_bar_hov != hover || g_bar_sign != ss)) {   /* (a page opening or closing over this screen's own object: no new hook needed) */
            const struct ml_row* r = ml_row_of(kind, hover);
            char buf[64];
            const char* want = (r && r->desc[0] && !ss) ? r->desc : 0;
            if (g_bar_msg && g_bar_msg_hov != hover)
                g_bar_msg = 0;                                  /* the cursor moved: the row's own description again */
            if (g_bar_msg && !ss)
                want = g_bar_msg;
            if (want && files_row(r) && ml_online_data_missing(ml_net_cost(107)))
                want = "Multishine system files not set up: check network";      /* no pack on the card: our modes and Friends need our files; Dojo (interim) until it runs without any file (rule, files_row) */
            g_bar_hov = hover;
            g_bar_sign = ss;
            ((void (*)(void*, u32)) 0x803A6368u)(dt, BLANK_ID);        /* Melee's object back on the blank string (it no longer points into our shadow); the hook counts the call */
            if (want && sis_line(dt, ml_sis_expand(buf, sizeof buf, want) ? buf : want))
                dt[0x5C / 4] = (u32) g_sis_line;                           /* HSD_Text.sis_buffer: Melee's object now draws our stream */
            g_bar_seen = g_bar_gen_cur = ST(S_BAR);                         /* our own call above counts too: only Melee's hooks may arm drop_all */
        }
    }
#ifdef ML_SIGN
    if (g_fr_open && (kind == 0u || kind == (u32) ML_KIND_MORE) && g_sign_state == 0 && ++g_fr_open > 30u) {     /* (More: the Friends row's screen since) */            /* back from a Friends hand-off: the Friends list again (always the styled Event screen: fr_glue never settles for the text view) */
        g_fr_open = 0;
        mock_open((u32) SG_FRD);
    }
    if (kind != 0u && kind != (u32) ML_KIND_MORE)
        g_fr_open = 0;
    if (g_cov_wd) {                  /* the add-friend hand-off holds the screen black (VISetBlack, gn_room[16] = 2) until the reopened Friends list lifts it (sg_entry.c, 3) or the engine's
                                        GNS_HOLD_MAX (~25 s) runs out.  This scene is the menu (never the keyboard, which is the character scene): once the Friends list is opening (the sign
                                        overlay loading or up) its own lift applies; if it has not started 90 frames after the menu entry (it did not reopen, or its load failed), lift here.
                                        A screen that is not black is unaffected.  Proof: the low half of the free status word S_WS_CHANGED counts the lifts, 'CL' in the high half. */
        if (g_sign_state == 1u || g_sign_state == 2u)
            g_cov_wd = 0;
        else if (++g_cov_wd > 90u) {
            g_cov_wd = 0;
            ((void (*)(int)) 0x80350100u)(0);                          /* VISetBlack(FALSE) */
            ((void (*)(void)) 0x8034FF78u)();                          /* VIFlush */
            ST(S_WS_CHANGED) = 0x434C0000u | ((ST(S_WS_CHANGED) + 1u) & 0xFFFFu);
        }
    }
#endif
}
#endif /* ML_PART_OVL */

#ifdef ML_OVL
/* ---- [OVL] the entry table the resident loader finds at hdr.api_off (ml_res.c calls everything through it) ---- */
#ifdef ML_SETTINGS
static void mlo_leave(void)                 /* the menu scene is over (lbHeap_80015900): give the nested sign-in overlay's block back before this block goes */
{
#if ML_UI && defined(ML_OVL)
    pgl_drop(1);                            /* the Settings globe and its archive (the gobjs are still alive here, as gart_close in the sign overlay's leave) */
#endif
    sign_shutdown();
}
#else
static void mlo_leave(void) { }
#endif

/* sgo_check (the loader shared with sign.ovl) sizes the table by struct sgo_api: ours must not be smaller */
typedef char mlo_api_not_smaller_than_sgo_api[(sizeof(struct mlo_api) >= sizeof(struct sgo_api)) ? 1 : -1];

const struct mlo_api mlo_api = { MLO_ABI, mlo_enter, mlo_frame, ml_think, ml_think_opt, ml_think_1p, ml_label_override, ml_pre_build, ml_unlock_override,
                                 ml_sign_request, mlo_leave, { 0, 0 } };
#endif
