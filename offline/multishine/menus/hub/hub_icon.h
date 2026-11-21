/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hub_icon.h: ONE Slippi rank icon (Project Slippi's rank badges, GPL-3.0, offline/assets/slippi_ranks; 96 x 64 CMPR textures (badge in the right half)) on the Grandmaster
 * character screen, drawn by the hub overlay.  Included once by hub_soc.c (static functions, soft-float: floats are only moved).
 * The same technique as sign/gnb_art.c gart_badges_open / gart_rank_icon: a copy of the Trophy Gallery sort plate's descriptors (TyMnView, a retail
 * file read from the disc) with its image repointed to ours; the plate's quad and material draw our texture.  Only what ONE icon needs: the descriptor copy, one aligned 4 KB buffer.
 * Resident engine bytes: 0 (this is hub.ovl code).  Every shape is checked before use; any miss draws nothing (hi_make returns 0).
 */
#ifndef HUB_ICON_H
#define HUB_ICON_H
#define STB_RI_NO_SMALL 1                                  /* only the hub texture STB_RI_H is used here (stb_rank_tex.h) */
#define STB_RI_NO_BIG 1
#define STB_RI_WANT_HUB 1
#include "stb_rank_tex.h"

typedef char *(*hi_fullname)(const char *);
typedef int (*hi_entry)(const char *);
typedef void *(*hi_arc_load)(const char *, void *, ...);
typedef void *(*hi_pub)(void *, const char *);
typedef void *(*hi_load)(void *);
typedef void *(*hi_gobj)(int, int, int);
typedef void (*hi_obj)(void *, int, void *);
typedef void (*hi_link)(void *, void *, int, int);
typedef void (*hi_p)(void *);
typedef void (*hi_range)(void *, u32);
#define HI_FULLNAME ((hi_fullname)0x80016204u)          /* lbFileGetFullName */
#define HI_ENTRYNUM ((hi_entry)0x8033796Cu)             /* DVDConvertPathToEntrynum */
#define HI_ARC_LOAD ((hi_arc_load)0x80016DBCu)          /* lbArchive_80016DBC(name, NULL) */
#define HI_ARC_PUB ((hi_pub)0x80380358u)                /* HSD_ArchiveGetPublicAddress */
#define HI_JOBJ_LOAD ((hi_load)0x80370E44u)             /* HSD_JObjLoadJoint */
#define HI_GOBJ_NEW ((hi_gobj)0x803901F0u)              /* GObj_Create */
#define HI_GOBJ_OBJ ((hi_obj)0x80390A70u)               /* HSD_GObjObject_80390A70 */
#define HI_GOBJ_LINK ((hi_link)0x8039069Cu)             /* GObj_SetupGXLink */
#define HI_GOBJ_FREE ((hi_p)0x80390228u)                /* HSD_GObjFree */
#define HI_JOBJ_DRAW 0x80391070u                        /* HSD_GObj_JObjCallback */
#define HI_JOBJ_KIND 0x804D7849u                        /* HSD_GObj_JObjKind */
#define HI_DIRTYSUB ((hi_p)0x803732E8u)                 /* HSD_JObjSetMtxDirtySub */
#define HI_DC_STORE ((hi_range)0x80344840u)             /* DCStoreRange */
#define HI_FIG_BUTTON 18u                               /* ToyFigurePanel joint 18: a sort button (96 x 32 IA4 plate) */

static int hi_ok(u32 p) { return p >= 0x80000000u && p < 0x81800000u && !(p & 3u); }
static u32 *hi_dfs(u32 *j, unsigned want, unsigned *at)
{
    unsigned guard = 0;
    while (j && hi_ok((u32)j) && guard++ < 256u) {
        u32 *r;
        if (*at == want) return j;
        (*at)++;
        if (W((u32)j + 8u) && (r = hi_dfs((u32 *)W((u32)j + 8u), want, at)) != 0) return r;
        j = (u32 *)W((u32)j + 0xCu);
    }
    return 0;
}

static struct {
    u8 tried, ok;
    u32 *btn_j;
    u32 bj[16], bdo[4], bmo[8], bto[24], bim[8];
    u8 tex_raw[STB_RI_H_BYTES + 32u];
} HI;

/* the plate of the retail Trophy Gallery sort button, once: 0 = not recognised (nothing drawn) */
static int hi_open(void)
{
    void *fig;
    u32 *root, dob, mob, tob, img, i;
    if (HI.tried) return HI.ok;
    HI.tried = 1;
    {   char *full = HI_FULLNAME("TyMnView");
        if (!full || HI_ENTRYNUM(full) < 0) return 0;
    }
    fig = HI_ARC_LOAD("TyMnView", 0);
    if (!fig) return 0;
    root = (u32 *)HI_ARC_PUB(fig, "ToyFigurePanel_Top_joint");
    if (!root || !hi_ok((u32)root)) return 0;
    {   unsigned at = 0;
        HI.btn_j = hi_dfs(root, HI_FIG_BUTTON, &at);
    }
    if (!HI.btn_j) return 0;
    dob = W((u32)HI.btn_j + 0x10u);
    if (!dob || !hi_ok(dob) || !(mob = W(dob + 8u)) || !hi_ok(mob) || !(tob = W(mob + 8u)) || !hi_ok(tob) || !(img = W(tob + 0x4Cu)) || !hi_ok(img)) return 0;
    if (H(img + 4u) != 96u || H(img + 6u) != 32u || W(img + 8u) != 2u) return 0;
    for (i = 0; i < 16u; i++) HI.bj[i] = HI.btn_j[i];
    HI.bj[3] = 0;                                              /* next = 0: only this joint is loaded */
    for (i = 0; i < 4u; i++) HI.bdo[i] = W(dob + 4u * i);
    HI.bdo[1] = 0;                                             /* the plate's quad and material only: no label dobj */
    for (i = 0; i < 8u; i++) HI.bmo[i] = W(mob + 4u * i);
    for (i = 0; i < 24u; i++) HI.bto[i] = W(tob + 4u * i);
    HI.bto[1] = 0;
    for (i = 0; i < 8u; i++) HI.bim[i] = W(img + 4u * i);
    HI.bto[15] = 0x01010000u | (HI.bto[15] & 0xFFFFu);         /* repeat 1: ours is one badge, not the plate's mirrored texture */
    HI.bto[19] = (u32)HI.bim;
    HI.bmo[2] = (u32)HI.bto;
    HI.bdo[2] = (u32)HI.bmo;
    HI.bj[4] = (u32)HI.bdo;
    HI.ok = 1;
    return 1;
}

static void hi_dirty(u8 *j)
{
    u32 fl = W(j + 0x14);
    if (!(fl & 0x2000000u) && !(!(fl & 0x800000u) && (fl & 0x40u))) HI_DIRTYSUB(j);
}

/* icon (STB_SH_*, 0 .. 7) at (x, y, z) with scale (sx, sy) (float bit patterns), on GX link `link` priority `prio`; returns the GObj or 0 */
static void *hi_make(unsigned icon, u32 x, u32 y, u32 z, u32 sx, u32 sy, int link, int prio)
{
    u8 *t, *j, *src;
    void *g, *o;
    u32 d, i;
    if (icon >= STB_RI_N || !hi_open()) return 0;
    t = (u8 *)((((u32)HI.tex_raw) + 31u) & ~31u);
    src = (u8 *)STB_RI_H[icon];
    for (i = 0; i < STB_RI_H_BYTES; i++) t[i] = src[i];
    HI_DC_STORE(t, STB_RI_H_BYTES);
    HI.bim[0] = (u32)t; HI.bim[1] = ((u32)STB_RI_H_W << 16) | STB_RI_H_H; HI.bim[2] = 14u;      /* CMPR 96 x 64, the badge in the right half (hub_tex: the plate's u mapping is only linear there) */
    g = HI_GOBJ_NEW(6, 7, 0x80);
    if (!g) return 0;
    o = HI_JOBJ_LOAD((void *)HI.bj);
    if (!o) { HI_GOBJ_FREE(g); return 0; }
    HI_GOBJ_OBJ(g, B(HI_JOBJ_KIND), o);
    HI_GOBJ_LINK(g, (void *)HI_JOBJ_DRAW, link, prio);
    j = (u8 *)W((u32)g + 0x28u);
    W((u32)g + 0x1Cu) = HI_JOBJ_DRAW;
    W(j + 0x38u) = x; W(j + 0x3Cu) = y; W(j + 0x40u) = z;
    W(j + 0x2Cu) = sx; W(j + 0x30u) = sy;
    hi_dirty(j);
    for (d = W(j + 0x18u), i = 0; d && hi_ok(d) && i < 8u; i++, d = W(d + 4u)) {            /* the material: white, opaque (the texture's own colours) */
        u32 m = W(d + 8u), mat = m && hi_ok(m) ? W(m + 0xCu) : 0;
        if (mat && hi_ok(mat)) { W(mat + 4u) = 0xFFFFFFFFu; W(mat + 0xCu) = 0x3F800000u; }
    }
    return g;
}
#endif
