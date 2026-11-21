/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : copy of; S11: allocation sites (sd_site_*), sd_osalloc_post gets the stack pointer.
   : copy of; S10: render-set selection and never-written exclusion (rs_sel, nwx).
   : Melee manifest configuration and statistics (sd_melee.c). Original work. */
#ifndef SD_MELEE_H
#define SD_MELEE_H
#include "sd_snap.h"

#define SDM_PADQ 1u      /* raw pad queue (HSD_PadLibData + entries) in the manifest (Slippi B1/B2 mode) */
#define SDM_ANIMBAR 2u   /* fighter animation buffers compared only after an ARAM transfer into them */

struct sd_mcfg { u32 flags, deny, rs_sel, nwx; };   /* S10: rs_sel (SDM_RS_*), nwx (never-written exclusion list) */
#define SDM_RS_ALL 1u        /* S10 TEST: every range is in the render set (RSET == compare everything) */
#define SDM_RS_W 2u          /* S10: render set from the W profile of run r1_prof_ref (outer-loop writes): the statics
                                listed in sd_melee.c rs_w_static and every cell whose size is in rs_w_size */
struct sd_mstats { u32 free_cells, pool_links, images, arq_calls, npools_uncov; u8 pools_uncov[64]; };

extern struct sd_mcfg sd_mc;
extern struct sd_mstats sd_ms;
extern u32 sd_pools[64], sd_npools, sd_ucf_body;

void sd_melee_init(void);
u32 sd_hsd_desc(void);
/* hook targets (sd_hooks.S) */
void sd_osalloc_pre(u32 heap, u32 size);
void sd_osalloc_post(u32 ptr, u32 heap, u32 sp);   /* S11: sp = r1 of OSAllocFromHeap's caller (stack walk) */
void sd_osfree_pre(u32 heap, u32 ptr);
void sd_objinit(u32 data);
void sd_hobjalloc(u32 obj);                       /* S11: HSD_ObjAlloc post-hook (returned object) */
void sd_hpiecealloc(u32 mem);                     /* S11: hsdAllocMemPiece post-hook (returned piece) */
void sd_hpiecefree(u32 mem);                      /* S11: hsdFreeMemPiece(mem, size) pre-hook */
void sd_hobjfree(u32 data, u32 obj);              /* S11: HSD_ObjFree(data, obj) pre-hook */
void sd_arq(u32 dst, u32 len);
/* ---- S11: allocation sites.  Every HSD-heap allocation of at most SD_CELL_MAX bytes is tagged with its site = the
   SD_SITE_DEPTH return addresses above OSAllocFromHeap (caller, its caller, its caller), recorded from boot on when
   sd_site_enable says so. Cells look their site up by payload address. */
#ifdef SD_NO_SITES                                /* product profiles: tables compiled out */
#define SD_SITES 1
#define SD_SITES_ON 0
#else
#define SD_SITES 512
#define SD_SITE_SLOTS 8192
#define SD_SITES_ON 1
#endif
#define SD_SITE_DEPTH 5                           /* return addresses per site key */
struct sd_site {
    u32 lr[SD_SITE_DEPTH];
    u32 cold;                                     /* listed in s11_cold.h */
    u32 allocs;                                   /* allocations recorded */
    u32 live_n, live_b, live_chg_n, chg_sum, age_sum, live_chg_b;   /* aggregated over the current manifest (sd_site_aggregate) */
    u32 dead_n, dead_chg_n, dead_age, dead_chg_sum;     /* cells that left the manifest list */
    u32 live_uchg, dead_uchg;                     /* changes no invalidation explains (SD_SITE_UCHG; live / gone cells) */
};
struct sd_site_stats { u32 nsites, site_full, slots_used, wipes, walk_bad, unknown_n, unknown_b, cold_listed; };
extern struct sd_site sd_sites[SD_SITES];
extern struct sd_site_stats sd_ss;
u32 sd_site_enable(void);                         /* platform config (weak default 0) */
u32 sd_site_epoch(void);                          /* platform: scene id; the site table starts over when it changes */
void sd_site_aggregate(void);                     /* fill live_* from the current manifest */
#endif
