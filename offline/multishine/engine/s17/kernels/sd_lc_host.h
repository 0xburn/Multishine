/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17: host mock of the locked cache, its DMA queue and the write-back cache of the normal cache (SD_HOST only;
 * included by sd_lc.h into the engine's translation unit, controlled by the tests through the sdh_* variables).  Original work.
 *
 * What it models, so that a host run (ASan / UBSan) can prove what a Dolphin run cannot:
 *  - DMA commands are decoded exactly as the hardware encodes them (DMA_U: physical address | length >> 2, DMA_L: LC address |
 *    length & 3 | load bit | T), queued (15 deep), and completed ASYNCHRONOUSLY: lc_ql completes at most one command per
 *    call, with probability 1/2 (deterministic generator), so a kernel that reads locked-cache data before its commands
 *    completed reads the stale / poisoned line and the exactness checks fail;
 *  - the locked cache is poisoned at every enable and disable (a stale line never looks plausible);
 *  - write-back cache: sdh_dirty[line] = the CPU wrote the line (a test mutation, an engine copy, a dcbz) and no dcbst has
 *    followed; a DMA load of such a line is a COHERENCY VIOLATION (the console's DMA reads memory, not the cache): counted in
 *    sdh_violations, and the data the DMA delivers is the OLD memory image (sdh_mem) so the error also shows as wrong data.
 *    sdh_mem is the memory image as the DMA sees it: it follows the CPU view at every dcbst of the line (and at lc_enable for
 *    lines that are clean).  Lines are never written back on their own (worst case).
 *  - a full queue, a command with the locked cache disabled, a store command, a length / range error: sdh_violations.
 */
#ifndef SD_LC_HOST_H
#define SD_LC_HOST_H
#include <stdint.h>
#include <string.h>

#include <stdlib.h>
extern u8 *sd_host_mem;
u32 sd_host_mem_size;                        /* bytes of guest memory (sdh_init_model sets it; 0 = unknown, no range checks) */

#define SDG(p) (0x80000000u + (u32)((const u8 *)(p) - sd_host_mem))

static u8 sdh_lc[16384];
#define LCP(a) ((u32 *)(sdh_lc + ((u32)(a) - SD_LC_BASE)))
u8 *sdh_dirty;                               /* one byte per 32-byte guest line; NULL = coherency model off */
u8 *sdh_mem;                                 /* memory image as seen by the DMA (NULL = DMA reads the CPU view) */
u32 sdh_violations, sdh_hid2_reg, sdh_lce, sdh_stuck, sdh_err_next, sdh_rng = 0x2545F491u, sdh_lc_busy_pre;
u32 sdh_q_n, sdh_q_mem[16], sdh_q_lc[16], sdh_q_len[16], sdh_dma_u;
u32 sdh_ncmd, sdh_nlines, sdh_nflush, sdh_nenable, sdh_irq_depth, sdh_ndcbz, sdh_ndcbst, sdh_lines_dirty_dma;
u32 sdh_no_dcbst;                            /* TEST negative control: dcbst does nothing (violations must show) */
u32 sdh_last_viol_line;

static inline u32 sdh_rand(void) { sdh_rng ^= sdh_rng << 13; sdh_rng ^= sdh_rng >> 17; sdh_rng ^= sdh_rng << 5; return sdh_rng; }

static inline u32 sdh_off(const void *p) { return (u32)((const u8 *)p - sd_host_mem); }

static inline void sdh_note_w(const void *p, u32 bytes)
{
    u32 a, l, e;
    if (!sdh_dirty || !bytes || (const u8 *)p < sd_host_mem)
        return;
    a = sdh_off(p);
    if (sd_host_mem_size && a >= sd_host_mem_size)
        return;
    e = (a + bytes - 1) >> 5;
    for (l = a >> 5; l <= e; l++)
        if (!sd_host_mem_size || l < sd_host_mem_size / 32u)
            sdh_dirty[l] = 1;
}
#define SD_NOTE_W(p, bytes) sdh_note_w((p), (bytes))

static void sdh_poison(void)
{
    u32 i;
    for (i = 0; i < 4096; i++)
        ((u32 *)sdh_lc)[i] = 0xDEAD0000u | (sdh_nenable & 0xFFFFu);
}

static void sdh_complete_one(void)
{
    u32 mem, lc, n, i;
    if (!sdh_q_n)
        return;
    mem = sdh_q_mem[0]; lc = sdh_q_lc[0]; n = sdh_q_len[0];
    for (i = 1; i < sdh_q_n; i++) { sdh_q_mem[i - 1] = sdh_q_mem[i]; sdh_q_lc[i - 1] = sdh_q_lc[i]; sdh_q_len[i - 1] = sdh_q_len[i]; }
    sdh_q_n--;
    if (lc < SD_LC_BASE || lc - SD_LC_BASE + 32u * n > 16384u) { sdh_violations++; return; }
    if (sd_host_mem_size && mem + 32u * n > sd_host_mem_size) { sdh_violations++; return; }
    for (i = 0; i < n; i++) {
        u32 line = (mem >> 5) + i;
        const u8 *src = (sdh_mem ? sdh_mem : sd_host_mem) + 32u * line;
        if (sdh_dirty && sdh_dirty[line]) {      /* the cache holds newer data than memory: the DMA reads the old image */
            sdh_violations++;
            sdh_lines_dirty_dma++;
            sdh_last_viol_line = line;
            if (!sdh_mem)
                src = sd_host_mem + 32u * line;
        }
        memcpy(sdh_lc + (lc - SD_LC_BASE) + 32u * i, src, 32);
    }
    sdh_nlines += n;
}

static inline u32 lc_hid2(void) { return sdh_hid2_reg | (sdh_lce ? SD_HID2_LCE : 0u) | (sdh_q_n << 24); }
static inline void lc_hid2_set(u32 v) { sdh_hid2_reg = v & (SD_HID2_ERRS | SD_HID2_EES); }
static inline u32 lc_ql(void)
{
    if (sdh_q_n && !sdh_stuck && (sdh_rand() & 1u))
        sdh_complete_one();
    return sdh_q_n;
}
static inline void lc_dma(u32 u, u32 l)
{
    if (!(l & 2u)) {                         /* DMA_U write (the encoding writes U first) or flush */
        sdh_dma_u = u;
        if (l & 1u) { sdh_q_n = 0; sdh_nflush++; }
        return;
    }
    if (!sdh_lce) sdh_violations++;
    if (sdh_q_n >= 15u) { sdh_violations++; return; }
    if (!(l & 0x10u)) { sdh_violations++; return; }                     /* S17 issues loads only */
    {
        u32 n = ((u & 0x1Fu) << 2) | ((l >> 2) & 3u);
        sdh_q_mem[sdh_q_n] = u & 0x0FFFFFE0u;
        sdh_q_lc[sdh_q_n] = l & ~31u;
        sdh_q_len[sdh_q_n] = n ? n : 128u;
        sdh_q_n++;
        sdh_ncmd++;
        if (sdh_err_next) { sdh_err_next = 0; sdh_hid2_reg |= 0x00100000u; }
    }
}
static inline void lc_enable(void) { sdh_lce = 1; sdh_nenable++; sdh_poison(); }
static inline void lc_disable(void) { if (sdh_q_n) sdh_violations++; sdh_lce = 0; sdh_q_n = 0; sdh_poison(); }
static inline void lc_dcbz(void *p)
{
    u32 *q = (u32 *)((unsigned long)p & ~31ul);
    int i;
    for (i = 0; i < 8; i++)
        q[i] = 0;
    sdh_note_w(q, 32);
    sdh_ndcbz++;
}
static inline void lc_dcbst(const void *p)
{
    const u8 *q = (const u8 *)((unsigned long)p & ~31ul);
    u32 line;
    sdh_ndcbst++;
    if (sdh_no_dcbst || !sdh_dirty || q < sd_host_mem)
        return;
    line = sdh_off(q) >> 5;
    if (sd_host_mem_size && line >= sd_host_mem_size / 32u)
        return;
    sdh_dirty[line] = 0;
    if (sdh_mem)
        memcpy(sdh_mem + 32u * line, q, 32);
}
static inline void lc_dcbt(const void *p) { (void)p; }   /* touch hint: nothing to model */
static inline void lc_sync(void) { }
static inline u32 lc_irq_off(void) { sdh_irq_depth++; return 0x8000u; }
static inline void lc_irq_restore(u32 ee) { if (ee) sdh_irq_depth--; }
static inline void lc_lc2live(u32 d, u32 s, u32 nl)
{
    u32 i;
    for (i = 0; i < nl; i++) {
        u32 *q = SDP(d + 32u * i);
        const u32 *l = LCP(s + 32u * i);
        int w;
        lc_dcbz(q);
        for (w = 0; w < 8; w++)
            q[w] = l[w];
        sdh_note_w(q, 32);
    }
}

/* test interface (non-static): switch the coherency model on for a guest memory of gsz bytes (sd_host_mem must be allocated and
   hold the initial state, which is taken as written back), note a guest write, re-synchronise after a bulk change of the memory */
void sdh_init_model(u32 gsz)
{
    sd_host_mem_size = gsz;
    free(sdh_dirty);
    free(sdh_mem);
    sdh_dirty = (u8 *)calloc(gsz / 32u + 1u, 1);
    sdh_mem = (u8 *)malloc(gsz);
    memcpy(sdh_mem, sd_host_mem, gsz);
}
void sdh_resync(void)
{
    if (sdh_mem) {
        memcpy(sdh_mem, sd_host_mem, sd_host_mem_size);
        memset(sdh_dirty, 0, sd_host_mem_size / 32u + 1u);
    }
}
void sdh_note_guest(u32 a, u32 bytes) { sdh_note_w(SDP(a), bytes); }
#endif
