/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* 'Multishine online-menu' image: the AllM / HSD-heap boundary of the online major scene.  Own re-implementation of the S14 policy
 * ( s14_heap_policy, measured in the S14 memory work, / "FIRST MEASUREMENT"); no S14 file is copied.
 *
 * Why: Melee's HSD main heap on Slippi's online CSS has only ~141 KB free before the connect-code screen and ~12-16 KB while it is open,
 * and every byte the patched image takes below ArenaLo comes out of those bytes.  This image's patched section ends at ArenaLo 0x80549000
 * (S14's final image: 0x8052D400), so the connect-code screen ran the HSD heap dry (OSReport "assertion adr failed line 52",
 * Melee's crash thread, Dolphin run). lbHeap_80015900 runs at every scene transition and puts the
 * HSD heap between the bottom heaps and the persistent top heap AllM (start = arena_hi - 0x64B400); on the online CSS AllM holds only the
 * preload cache (~6.2 of 6.6 MB free).  So when AllM is about to be CREATED for the online major scene (8) it is created OM_ALLM_SHIFT bytes
 * smaller and the HSD heap gets those bytes; for any other scene the retail partition comes back.  A created AllM is never moved.
 * Heap descriptor layout (lbHeap_80431FA0 + 0x10 + 0x1C * i): start +8, size +C, transient +14, status +18 (1 = destroyed).
 * Called through om_heap_stub (om_heap_stub.S, 04 hook at 0x80015900, original `mflr r0`).  Soft-float, no heap, no libc. */
typedef unsigned int u32;
#define W(a) (*(volatile u32 *)(a))
#define HEAP(i) (0x80431FA0u + 0x10u + 0x1Cu * (u32)(i))
#define SCENE_WORD (*(volatile u32 *)0x80479D30u)
#ifndef OM_ALLM_SHIFT
#define OM_ALLM_SHIFT 0x50000u   /* was 0x40000 before the 48 KB card work area of om_card.c */
#endif

/* Couch feedback 4 / 7 / 8 / 17: the combined image's patched section is ~207 KB larger than the layout image's, so the HSD heap of the
   other game scenes (VS / Couch, Training, Special Melee, ...) ran dry at the scene load (OSReport "assertion adr failed line 52", reproduced in Dolphin
 Couch and Stamina; fixed by moving 256 KB from AllM).  OM_ALLM_OTHER (default 0 = off: the other images behave as before) is the shift for every game
   scene except the boot, title, opening and menu scenes and the online one (that one keeps OM_ALLM_SHIFT). */
#ifndef OM_ALLM_OTHER
#define OM_ALLM_OTHER 0u
#endif
static u32 other_scene(u32 major)
{
    return major != 0u && major != 1u && major != 8u && major != 0x18u && major != 0x28u && major != 0x29u && major != 0x27u;
}

/* RAM proof words: [0] lbHeap_80015900 calls, [1] times AllM was created smaller for the online scene (symbol om_heap_st, om_heap_addr) */
volatile u32 om_heap_st[2] __attribute__((section(".data")));
static u32 g_h4_start, g_h4_size, g_h4_saved;

#ifdef ML_OVL_RES
extern void ml_scene_change(void);
#endif

void om_heap_policy(void)
{
    u32 h = HEAP(4), sc = SCENE_WORD, sh = OM_ALLM_SHIFT & ~31u, want, created_now, oth = OM_ALLM_OTHER & ~31u;
    if (!g_h4_saved && W(h + 0x0Cu)) { g_h4_start = W(h + 8u); g_h4_size = W(h + 0x0Cu); g_h4_saved = 1; }
    created_now = W(h + 0x18u) == 1u && W(h + 0x14u) == 0u;      /* destroyed and persistent: lbHeap_80015900 creates it now */
    want = (created_now && (sc >> 24) == 8u && sh && sh < g_h4_size / 2u) ? sh : 0u;
    if (created_now && oth && other_scene(sc >> 24) && oth < g_h4_size / 2u) want = oth;
    if (g_h4_saved && W(h + 0x18u) == 1u) {                       /* only a destroyed AllM may change */
        W(h + 8u) = g_h4_start + want;
        W(h + 0x0Cu) = g_h4_size - want;
        if (want) om_heap_st[1]++;
    }
    om_heap_st[0]++;
#ifdef ML_OVL_RES
    ml_scene_change();      /* a scene transition: the menu overlay (offline/multishine/menus/ml/ml_res.c) gives its HSD-heap block back, if it holds one */
#endif
#ifdef HUB_OVL_RES
    { extern void hub_scene_change(void); hub_scene_change(); }   /* the hub overlay (menus/hub/hub_res.c) gives its block back too; the next hub scene loads it again */
#endif
}
