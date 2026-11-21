/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REP_SCENE_H
#define REP_SCENE_H
/* rep_scene: WHERE the game-report client may use the network and the heap, as pure functions so the host tests can run them.  The facts
   come from br_glue.c (scene word, menu cursor, bridge state) and rep_res.c (HSD heap).  Strict C89, no libc. */
#if defined(__GNUC__)
#define REP_SCENE_FN static __attribute__((unused))
#else
#define REP_SCENE_FN static
#endif

#define REP_SC_NONE 0       /* never: a match, a loading scene, a search or session open, the Slippi submenu, any other scene */
#define REP_SC_MENU 1       /* a main menu (not Slippi's online submenu) */
#define REP_SC_CSS 2        /* Slippi's online character screen, idle: after a game, before the next search */

/* scene = the scene word [major, pending, prev, minor]; menu_cur = MenuFlow.cur_menu (8 = Slippi's online submenu); slippi_flow = the bridge owns this flow (br_glue.c origin());
   active = a search/session binding exists (g_active); engine_live = the engine plays a bridge match; bridge_idle = sbra_idle: no search, ticket, matchmaker or opponent connection. */
REP_SCENE_FN int rep_scene_class(unsigned int scene, unsigned int menu_cur, int slippi_flow, int active, int engine_live, int bridge_idle)
{
    unsigned int major = scene >> 24, minor = scene & 0xFFu;
    if (active || engine_live) return REP_SC_NONE;
    if (major == 1u) return menu_cur != 8u ? REP_SC_MENU : REP_SC_NONE;
    if (major == 8u && minor == 0u && slippi_flow && bridge_idle) return REP_SC_CSS;
    return REP_SC_NONE;
}

/* HSD heap room for the overlay block: one free cell of at least `block` bytes AND, after taking it, `reserve` bytes left for what the scene and the hub still allocate.  The online
   character screen has 318 KB free at its minimum and the block is 118,816 B: 199 KB stay. */
#define REP_HEAP_RESERVE 0x18000u       /* 96 KB */
REP_SCENE_FN int rep_heap_room(unsigned int largest_free, unsigned int total_free, unsigned int block)
{
    if (largest_free < block || total_free < block) return 0;
    return total_free - block >= REP_HEAP_RESERVE;
}
#endif
