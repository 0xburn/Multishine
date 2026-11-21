/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17: locked-cache DMA and cache-block helpers for the S17 kernels (sd_s17.h, included by sd_core.c).  Original work.
 *
 * The kernels are the console-proven kbench4 kernels (G64 capture compare, H0 restore,
 * U1 undo); this header is the thin hardware layer under them: PowerPC inline asm on the console, a host mock
 * (sd_lc_host.h, asynchronous DMA queue, poisoned locked cache, write-back cache model) under SD_HOST.
 * Every function is a leaf; no C code here keeps data in LR (offline/).
 *
 *   lc_hid2 / lc_hid2_set   HID2 (SPR 920): LCE 0x10000000, DMAQL bits 24..27 (commands queued), error flags 0x00F00000,
 *                           error-exception enables 0x000F0000
 *   lc_enable / lc_disable  the SDK's LCEnable (0x803449FC) / LCDisable (0x80344A34) of retail GALE01 1.02: 16 KB of the L1 D
 *                           become the locked cache at 0xE0000000 (all 512 lines allocated)
 *   lc_dma(u, l)            DMA_U (SPR 922) then DMA_L; DMA_L with T (bit 1) queues the command
 *   lc_ql                 DMAQL
 *   lc_dcbz / lc_dcbst      one 32-byte line of the normal cache
 *   lc_sync                 sync
 *   lc_lc2live(d, s, nl)    nl whole lines from the locked cache (address s) to live memory (address d, 32-aligned): per line
 *                           dcbz d (no read of the destination), then 4 x lfd / stfd (bit patterns unchanged, f0-f3 only;
 *                           caller in the game thread with MSR[FP] set, as sd_fcopy)
 *   lc_irq_off / lc_irq_restore   MSR[EE]
 */
#ifndef SD_LC_H
#define SD_LC_H

#define SD_LC_BASE 0xE0000000u
#define SD_HID2_LCE 0x10000000u
#define SD_HID2_ERRS 0x00F00000u
#define SD_HID2_EES 0x000F0000u

#ifdef SD_HOST
/* the mock lives in the engine's translation unit (sd_lc_host.h), controlled by the tests through sdh_* variables */
#include "sd_lc_host.h"
#else
#define SDG(p) ((u32)(p))                    /* guest address of a pointer into guest memory */
#define LCP(a) ((u32 *)(a))                  /* locked-cache address -> pointer */
#define SD_NOTE_W(p, bytes) ((void)0)        /* host ledger hook (the console has real caches) */

static inline u32 lc_hid2(void) { u32 v; __asm__ volatile("mfspr %0,920" : "=r"(v) : : "memory"); return v; }
static inline void lc_hid2_set(u32 v) { __asm__ volatile("sync\n\tmtspr 920,%0\n\tisync" : : "r"(v) : "memory"); }
static inline u32 lc_ql(void) { u32 v; __asm__ volatile("mfspr %0,920" : "=r"(v) : : "memory"); return (v >> 24) & 0xFu; }
static inline void lc_dma(u32 u, u32 l) { __asm__ volatile("mtspr 922,%0\n\tmtspr 923,%1" : : "r"(u), "r"(l) : "memory"); }
static inline void lc_enable(void) { ((void (*)(void))0x803449FCu)(); }
static inline void lc_disable(void) { ((void (*)(void))0x80344A34u)(); }
static inline void lc_dcbz(void *p) { __asm__ volatile("dcbz 0,%0" : : "r"(p) : "memory"); }
static inline void lc_dcbst(const void *p) { __asm__ volatile("dcbst 0,%0" : : "r"(p) : "memory"); }
static inline void lc_dcbt(const void *p) { __asm__ volatile("dcbt 0,%0" : : "r"(p)); }   /* touch hint: no architectural effect (HID0 NOOPTI is clear on the console) */
static inline void lc_sync(void) { __asm__ volatile("sync" : : : "memory"); }
static inline u32 lc_irq_off(void)
{
    u32 m;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    __asm__ volatile("mtmsr %0" : : "r"(m & ~0x8000u) : "memory");
    return m & 0x8000u;
}
static inline void lc_irq_restore(u32 ee)
{
    u32 m;
    if (!ee)
        return;
    __asm__ volatile("mfmsr %0" : "=r"(m));
    __asm__ volatile("mtmsr %0" : : "r"(m | 0x8000u) : "memory");
}
void sd_lc2live(u32 d, u32 s, u32 nl);       /* sd_lc.S */
static inline void lc_lc2live(u32 d, u32 s, u32 nl) { sd_lc2live(d, s, nl); }
#endif

/* dcbst over the lines covering [p, p + bytes) (no sync) */
static inline void lc_dcbst_range(const void *p, u32 bytes)
{
    const u8 *q = (const u8 *)p, *e = q + bytes;
    q = (const u8 *)((unsigned long)q & ~31ul);
    for (; q < e; q += 32)
        lc_dcbst(q);
}
#endif
