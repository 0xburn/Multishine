/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The RESIDENT side of the menu overlay (ml_ovl.h;  step 3).  Compiled only with -DML_OVL_RES (manifest option menuovl=ovl); in
 * every other build this file is empty.
 *
 * What it does: the eight menu hook stubs (ml_stub.S) and the retail menu table call the functions below exactly as they called the flat ml_glue.c.  Each one forwards into the
 * overlay when one is loaded and otherwise does nothing the retail code could notice:
 *   ml_enter            mnMain_Scene_OnEnter hook: settings plumbing (card load of the player's settings, widescreen, delay), then loads ms/menu.ovl into ONE block of the HSD heap
 *                       (blocking, bounded: the first menu frame needs the patched table), enters it.  Any failure puts the retail menu table back and the retail menu runs.
 *   ml_frame            mnMain_Scene_OnFrame hook: frame counter, settings poll, then the overlay's frame
 *   ml_label_override / ml_pre_build / ml_unlock_override    the label, build and unlock hooks: 0 / nothing / 0 ("no opinion") without an overlay
 *   ml_think*           the think slots of the patched menu table: thunks that stay valid when the overlay moves
 *   ml_scene_change     called by om_heap_policy (lbHeap_80015900, every scene transition): the menu scene is over, give the block back
 * The stubs need no change: they already treat the C functions as ordinary calls (r3/r4 and LR kept in their own frame, then the retail body).
 * Strict about memory: the block is freed only if it is still on the HSD heap's allocated list (a heap that was rebuilt under us is not touched).
 * Soft-float, no libc.  Host-tested by tests/ml_res_test.c (ML_HOST).
 */
#ifdef ML_OVL_RES
#if !defined(ML_SETTINGS) || !defined(ONS_SETTINGS)
#error "the menu overlay variant needs the settings layer (-DML_SETTINGS -DONS_SETTINGS)"
#endif
#include "ml_ovl.h"
extern const struct tmv_pub tmv_pub __attribute__((weak));     /* menus/on/tmv.c (engine): the retail Tourney view */
#include "sg_ovl.h"
#ifndef ML_HOST
#include "on_card.h"
#endif
#ifdef ML_UPDCHK
#include "upd_glue.h"            /* the update host and the launch-time check; off by default */
#endif

#ifdef ML_HOST
#include "ml_res_host.h"
#else
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
#define W(a) (*(volatile u32*) (a))
#define B(a) (*(volatile u8*) (a))
#define H(a) (*(volatile u16*) (a))
#define MLO_API_OF(ld) ((const struct mlo_api*) (ld).api)
#define MLO_FN_ADDR(f) ((unsigned) (f))                                /* the console's pointers are 32 bits */
#define OS_HEAP_ID() (((int (*)(void)) 0x80375404u)())                 /* HSD_GetHeap */
#define RETAIL_OPT_THINK_CALL(g) (((void (*)(void*)) 0x8022D104u)(g))   /* mn_8022D104 */
#define RETAIL_1P_THINK_CALL(g) (((void (*)(void*)) 0x8022D7F4u)(g))    /* mn_8022D7F4 */
static u32 tb_now(void)
{
    u32 t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}
#define MLO_BUS() W(0x800000F8u)    /* the OS bus clock word (Hz): 162 MHz on a GameCube, 243 MHz on a Wii (the time base runs at a quarter of it) */
#endif

#define TAB 0x803EB6B0u            /* MenuKindData[0x22], 20 bytes each */
#define MENU_FLOW 0x804A04F0u
#define HEAP_ARRAY 0x804D7360u     /* HeapArray (sbss): pointer to 12-byte descriptors {size, free list, allocated list}; cell = {prev, next, size} + 32 bytes header */
#define MLO_TIMEOUT_TB 202500000u  /* 5 s of the 40.5 MHz time base: a drive that does not answer in that time gives the menu to the retail code.  Dolphin's first load took 1.15 s
                                      (ml_mo_ticks 46.7 M of the emulated drive), a real disc or a FlippyDrive may be slower: 2 s would have been too close */
/* MLO_TIMEOUT_TB for the clock in use (5 s of real time): the Wii's time base is 60.75 MHz, so the GameCube constant would end the wait after 3.3 s there (Nintendont's emulated card
   has needed over 11 s once, WII_CARD_ROUTE_ANALYSIS / df_glue.c).  An implausible word keeps the GameCube value (same window as on_bhold.c, sdenet_clock.c). */
static u32 mlo_timeout_tb(void)
{
    u32 bus = MLO_BUS();
    return (bus >= 80000000u && bus <= 300000000u) ? bus / 4u * 5u : MLO_TIMEOUT_TB;
}
#define KIND_1P 1u

extern volatile u32 ml_st[];
extern u32 ml_main_anim[], ml_hub_anim[], ml_more_anim[], ml_misc_anim[], ml_opt_anim[];     /* ml_glue.c [RES]: the preview range tables the patched menu table points at */
extern u16 ml_opt_desc[];
extern const u16 ml_blank_desc[];
extern const struct sgo_sdk ml_sdk;
extern void ml_dvd_cancel(void);
extern void ml_sdk_forget(int (*live)(const void*));   /* ml_glue.c: drop an overlay open the scene change cut short */
extern void ml_settings_apply(void);
extern void ml_settings_mirror(void);
extern void ml_settings_tick(void);
extern volatile u32 s3_cfg[];
#define ENGINE_DELAY (s3_cfg[3])
#define ST(i) (ml_st[i])
extern int ms_offline_server(int need_account) __attribute__((weak));
extern int br_friend_direct(const char *code) __attribute__((weak));
extern unsigned ms_net_cost(int i) __attribute__((weak));
extern void ms_test_toast(int kind) __attribute__((weak));     /* on_backend.c (the hub client's resident part); absent in other images */
#ifdef ml_online_hook
extern int ml_online_hook(void);
#define ONLINE_HOOK ml_online_hook
#else
#define ONLINE_HOOK 0
#endif

struct mlo_tabsave ml_tabsave;

#ifdef SD_CARD_HOLE
/* the card route's services for the overlay (struct mlo_card, ml_ovl.h): the same functions the flat build of ml_glue.c links by name.
   The ones a lab / release shape may lack are weak (0 in the table = absent, as in the flat build). */
extern int df_reload(void) __attribute__((weak));               /* df_glue.c: read the pack's table again after the system files were set up */
extern void df_arm(void) __attribute__((weak));                 /* df_glue.c: read the pack's table of contents now (the overlay file is served from it) */
extern void df_note(unsigned kind, unsigned a, unsigned count) __attribute__((weak));
extern volatile unsigned df_dbg[] __attribute__((weak));
extern void ms_net_dbg(unsigned* w) __attribute__((weak));
extern int br_line_up(void) __attribute__((weak));
extern unsigned sbr_nc_card_mm_ip(void) __attribute__((weak));
extern int ut_menu_frame(unsigned kind, int settled, unsigned sign_state) __attribute__((weak));
extern int fr_menu_frame(unsigned kind, int settled, unsigned sign_state) __attribute__((weak));
extern int fr_prog_menu_frame(unsigned kind, int settled, unsigned sign_state, int due) __attribute__((weak));
extern int odl_save(const unsigned *w) __attribute__((weak));        /* on_dlog.c (-DONS_DLOG only) */
extern int ons_change_srv(unsigned v, unsigned frame) __attribute__((weak));
extern int ons_change_mmr(unsigned v, unsigned frame) __attribute__((weak));
extern int ons_change_mmk(unsigned frame) __attribute__((weak));
extern void* ml_upd_net(void);
extern int ml_upd_hello(unsigned now_ms);
extern int ml_upd_poll(unsigned now_ms);
extern int ml_upd_link(void);
extern unsigned char* ms_card_wa_acquire(unsigned owner);
extern void ms_card_wa_release(void);
extern void* ml_ident_cfg(void);
extern int ml_ident_save(const void* c);
extern unsigned od_clock_ms(void);
extern unsigned ml_gn_left_ack(void);
extern unsigned ml_gn_state(void);
extern unsigned ml_gn_rk(void);
static struct mlo_card card;
#endif

static struct {
    struct sgo_loader ld;
    const struct mlo_api* api;
} M;

void ml_think(void* gobj);
void ml_think_opt(void* gobj);
void ml_think_1p(void* gobj);

/* the resident services handed to the overlay (filled at the first ml_enter; function addresses are 32-bit values on the console, which is why the table is not a constant initializer) */
static struct mlo_env env;
static void env_init(void)
{
    if (env.abi)
        return;
    env.size = (unsigned) sizeof env;
    mlo_st[MO_MAGIC] = 0x4D4C4F56u;            /* "MLOV" */
    env.st = ml_st;
    env.ok = &ml_g_ok;
    env.carry = &ml_carry;
    env.save = &ml_tabsave;
    env.main_anim = ml_main_anim;
    env.hub_anim = ml_hub_anim;
    env.more_anim = ml_more_anim;
    env.misc_anim = ml_misc_anim;
    env.opt_anim = ml_opt_anim;
    env.opt_desc = ml_opt_desc;
    env.blank_desc = ml_blank_desc;
    env.th_think = MLO_FN_ADDR(ml_think);
    env.th_think_opt = MLO_FN_ADDR(ml_think_opt);
    env.th_think_1p = MLO_FN_ADDR(ml_think_1p);
    env.ons_cur = ons_cur;
    env.ons_change_delay = ons_change_delay;
    env.ons_change_pool = ons_change_pool;
    env.ons_change_ws = ons_change_ws;
    env.ons_change_prog = ons_change_prog;
    env.ons_change_rbr = ons_change_rbr;
    env.ons_change_preload = ons_change_preload;
    env.ons_change_spec = ons_change_spec;
    env.ons_change_bak = ons_change_bak;
    env.ons_change_keep = ons_change_keep;
    env.ons_change_dbg = ons_change_dbg;
    env.ons_change_ping = ons_change_ping;
    env.ons_change_music = ons_change_music;
    env.ons_change_hud = ons_change_hud;
    env.ons_flush = ons_flush;
    env.ons = &ons_st;
    env.apply_settings = ml_settings_apply;
    env.online_hook = ONLINE_HOOK;
    env.offline_hook = ms_offline_server;
    env.friend_direct = br_friend_direct;
    env.net_cost = ms_net_cost;
    env.test_toast = ms_test_toast;
    env.sdk = &ml_sdk;
    env.upd = 0;
#ifdef SD_CARD_HOLE
    card.upd_net = ml_upd_net;
    card.upd_hello = ml_upd_hello;
    card.upd_poll = ml_upd_poll;
    card.upd_link = ml_upd_link;
    card.wa_acquire = ms_card_wa_acquire;
    card.wa_release = ms_card_wa_release;
    card.ident_cfg = ml_ident_cfg;
    card.ident_save = ml_ident_save;
    card.clock_ms = od_clock_ms;
    card.df_note = df_note;
    card.df_reload = df_reload;
    card.df_dbg = df_dbg;
    card.net_dbg = ms_net_dbg;
    card.line_up = br_line_up;
    card.nc_card_mm_ip = sbr_nc_card_mm_ip;
    card.gn_left_ack = ml_gn_left_ack;
    card.gn_state = ml_gn_state;
    card.gn_rk = ml_gn_rk;
    card.ut_menu_frame = ut_menu_frame;
    card.fr_menu_frame = fr_menu_frame;
    card.fr_prog_menu_frame = fr_prog_menu_frame;
    card.odl_save = odl_save;
    card.ons_seed = ons_seed;
    card.ons_change_srv = ons_change_srv;
    card.ons_change_mmr = ons_change_mmr;
    card.ons_change_mmk = ons_change_mmk;
    env.card = &card;
#endif
    env.tmv = &tmv_pub;                                  /* the retail-Tourney view's request / data blocks (tmv.c), 0 in a build without it */
    env.abi = MLO_ABI;
}

/* ---- the HSD heap's own bookkeeping, read only: is `p` (a block from sdk->alloc) still an allocated cell? ---- */
static int cell_live(const void* p)
{
    u32 a = SGO_P2A(p), hd, c;
    if (!a)
        return 0;
    hd = W(HEAP_ARRAY);
    if (!hd)
        return 0;
    hd += 12u * (u32) OS_HEAP_ID();
    for (c = W(hd + 8u); c; c = W(c + 4u))
        if (c + 32u == a)
            return 1;
    return 0;
}

/* give the block back: the overlay first closes what it holds (the nested sign-in overlay), then the block is freed - both only while the block is still ours */
static void mlo_free(void)
{
    u8* blk = M.ld.buf;
    if (blk && cell_live(blk)) {
        if (M.api)
            M.api->leave();
        M.api = 0;
        sgo_release(&M.ld);                    /* frees the block through sdk->free, state IDLE */
        mlo_st[MO_FREES]++;
    } else {
        if (blk)
            mlo_st[MO_LEAK_SKIP]++;            /* not on the heap's list any more (rebuilt under us): forget it, never call into it, never free it */
        M.api = 0;
        M.ld.buf = 0;
        M.ld.api = 0;
        M.ld.state = SGO_S_IDLE;
    }
    mlo_st[MO_STATE] = 0;
    mlo_st[MO_BLOCK] = 0;
    mlo_st[MO_ADDR] = 0;
}

void ml_scene_change(void)
{
    mlo_st[MO_SCENES]++;
    if (M.api || M.ld.buf)
        mlo_free();
    ml_sdk_forget(cell_live);
}

/* the load is synchronous: the table must be patched before the retail OnEnter body builds the first screen.  Bounded by MLO_TIMEOUT_TB; a read that is still running is
   cancelled before its buffers are freed. */
static int mlo_load(void)
{
    u32 t0 = tb_now(), spins = 0, lim = mlo_timeout_tb();
    int e = sgo_begin(&M.ld, &ml_sdk, MLO_PATH);
    if (e)
        return e;
    for (;;) {
        int st = sgo_step(&M.ld);
        spins++;
        if (st == SGO_S_READY)
            break;
        if (st == SGO_S_FAILED)
            return M.ld.err;
        if (tb_now() - t0 > lim) {
            ml_dvd_cancel();
            if (M.ld.hbuf) {
                ml_sdk.free(M.ld.hbuf);
                M.ld.hbuf = 0;
            }
            sgo_release(&M.ld);
            return (int) MO_E_TIMEOUT;
        }
    }
    mlo_st[MO_SPINS] = spins;
    mlo_st[MO_TICKS] = tb_now() - t0;
    return 0;
}

/* the retail menu table back to the words the install found (struct mlo_tabsave), so the retail menu runs as if we were never there */
static void mlo_unpatch(void)
{
    static const u8 sk[5] = MLO_SAVE_KINDS;
    u32 k, j;
    if (!ml_g_ok || !ml_tabsave.valid) {
        ml_g_ok = 0;
        return;
    }
    for (k = 0; k < 5; k++)
        for (j = 0; j < 5; j++)
            W(TAB + (u32) sk[k] * 0x14u + j * 4u) = ml_tabsave.ent[k][j];
    W(TAB + KIND_1P * 0x14u + 0x10u) = ml_tabsave.th1p;
    W(TAB + 2u * 0x14u + 0x08u) = ml_tabsave.desc[0];
    W(TAB + 8u * 0x14u + 0x08u) = ml_tabsave.desc[1];
    ml_g_ok = 0;
    ST(S_INSTALLED) = 0;
    mlo_st[MO_UNPATCH]++;
}

static void mlo_fail(int e)
{
    mlo_st[MO_FAILS]++;
    mlo_st[MO_ERR] = (u32) (e < 0 ? -e : e);
    mlo_st[MO_STATE] = 2;
    mlo_unpatch();
    ml_carry.from_hub = 0;                      /* what the overlay would have consumed at this entry */
    ml_carry.ret_kind = 0;
    ml_carry.couch = 0;
    ml_carry.gn_back = 0;
}

void ml_enter(void* enter_data)
{
    int e;
    ST(S_ENTER)++;
    ons_menu_enter(0u, ST(S_FRAME));                /* 0 = Auto: the default before the card is read */
    ml_settings_apply();
    if (M.api || M.ld.buf) {                    /* the scene hook did not run since the last menu scene (or the same scene entered again): start clean */
        mlo_st[MO_STALE]++;
        mlo_free();
    }
    env_init();
#ifdef SD_CARD_HOLE
    if (df_arm)
        df_arm();                               /* card route: the overlay file is served from the pack's table of contents, which must be read before the open (also the flat ml_enter's job) */
#endif
    e = mlo_load();
    if (e) {
        mlo_fail(e);
        return;
    }
#ifdef ML_UPDCHK
    env.upd = upd_glue_host();                  /* 0 until the resident network owner attached the host (the Update row then says it is not available) */
#endif
    {
        const struct mlo_api* api = MLO_API_OF(M.ld);
        if (M.ld.hdr.state_hint != MLO_KIND || api->abi != MLO_ABI) {
            sgo_release(&M.ld);
            mlo_fail(MO_E_ABI);
            return;
        }
        M.api = api;
        if (api->enter(&env, enter_data) != 0) {
            M.api = 0;
            sgo_release(&M.ld);
            mlo_fail(MO_E_ENTER);
            return;
        }
    }
    mlo_st[MO_STATE] = 1;
    mlo_st[MO_LOADS]++;
    mlo_st[MO_BLOCK] = M.ld.peak_bytes;
    mlo_st[MO_ADDR] = M.ld.load_addr;
}

void ml_frame(void)
{
    u32 hover = (u32) H(MENU_FLOW + 2u);
    ST(S_FRAME)++;
    ST(S_HOVER) = hover;
    {
        unsigned ev = ons_poll(ST(S_FRAME));       /* pending card read / due card write: menu scene only, bounded, never blocks the menu for long */
        if (ev & ONS_EV_LOADED)
            ml_settings_apply();
        ml_settings_tick();
        ml_settings_mirror();
    }
#ifdef ML_UPDCHK
    upd_menu_frame(ons_cur()->updchk, ST(S_FRAME) * 50u / 3u);      /* the launch-time update check: Main Menu scene only, ms from the 60 Hz frame counter */
#endif
    if (M.api)
        M.api->frame();
}

int ml_label_override(u32 kind, u32 sel)
{
    ST(S_LABEL)++;
    if (!ml_g_ok || !M.api)
        return 0;                               /* the retail plate stays */
    return M.api->label_override(kind, sel);
}

void ml_pre_build(u32 state)
{
    if (!ml_g_ok || !M.api)
        return;
    M.api->pre_build(state);
}

int ml_unlock_override(u32 kind, u32 sel)
{
    if (!ml_g_ok || !M.api)
        return 0;                               /* no opinion: the retail code (and Slippi's two patches inside it) decide */
    return M.api->unlock_override(kind, sel);
}

void ml_think(void* gobj)
{
    if (M.api)
        M.api->think(gobj);
}

void ml_think_opt(void* gobj)
{
    if (M.api)
        M.api->think_opt(gobj);
    else
        RETAIL_OPT_THINK_CALL(gobj);            /* only reachable if the table is patched and the block is gone: keep the Options screen alive */
}

void ml_think_1p(void* gobj)
{
    if (M.api)
        M.api->think_1p(gobj);
    else if (B(MENU_FLOW) != 0u)
        RETAIL_1P_THINK_CALL(gobj);
}

void ml_sign_request(void)
{
    if (M.api)
        M.api->sign_request();
}
#endif /* ML_OVL_RES */
