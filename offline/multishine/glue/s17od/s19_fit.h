/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S19 store fit: how big a block may be asked from Melee's HSD heap without the allocator asserting.
 * HSD_MemAlloc (0x8037F1E4) -> OSAllocFromHeap (0x80343EF0, first fit over the heap's free list) and an HSD_ASSERT on 0: a request that
 * does not fit is a crash ("assertion adr failed line 52"), never a 0. OSAllocFromHeap rounds n + 0x20 (cell header) up to
 * 32 bytes and takes the first free cell at least that big.  So a request of n bytes is safe when
 *     round32(n + 0x20) <= the largest free cell,
 * and it leaves Melee `keep` bytes when n + 0x40 <= total free - keep (header and rounding counted generously).
 * Pure integer code, shared by the console glue (s12_gc.c) and the host test (tests/s19_fit_test.c). */
#ifndef S19_FIT_H
#define S19_FIT_H

#define S19_HDR 0x40u                    /* cell header (0x20) + rounding to 32, counted generously */

/* largest request (multiple of 32) that fits one free cell of `big` bytes and leaves `keep` of `total` free; 0 = none */
static inline unsigned int s19_fit_bytes(unsigned int total, unsigned int big, unsigned int keep)
{
    unsigned int a = big > S19_HDR ? (big - S19_HDR) & ~31u : 0u;
    unsigned int b = total > keep + S19_HDR ? (total - keep - S19_HDR) & ~31u : 0u;
    return a < b ? a : b;
}

/* the store for this match: `want` (the configured size) when it fits, else the largest fitting size if that is at least `min`, else 0
   (rollback off for the match) */
static inline unsigned int s19_store_size(unsigned int total, unsigned int big, unsigned int want, unsigned int keep, unsigned int min)
{
    unsigned int f = s19_fit_bytes(total, big, keep);
    want &= ~31u;
    if (f >= want) return want;
    return f >= min ? f : 0u;
}

/* S19d: store PLACEMENT.  The first real console match on Pokemon Stadium (RC1 a204e22e, Fox vs Kirby) ran "Rollback off: low
   memory" (MEM_NOFIT): the HSD heap could not give even S19_MIN_STORE while AllM (Melee heap 4, the preload cache, persistent for the online
   major scene) has about 4 MB free during a Stadium match and nothing allocates from it mid-match.  Rule (pure, host-tested): the store stays
   in the HSD heap whenever that heap holds the CONFIGURED size (nothing changes for stages with room); otherwise, when AllM is allowed and
   gives a LARGER store than the HSD heap would (with its own keep), the store comes from AllM; else the HSD answer (shrunk or 0 = off).
   Returns the heap: 0 = HSD, 4 = AllM, -1 = none (rollback off); *size = the bytes. */
static inline int s19_place(unsigned int hsd_total, unsigned int hsd_big, unsigned int allm_total, unsigned int allm_big,
                            unsigned int want, unsigned int keep_hsd, unsigned int min, unsigned int keep_allm, int allow_allm,
                            unsigned int *size)
{
    unsigned int h = s19_store_size(hsd_total, hsd_big, want, keep_hsd, min);
    unsigned int a = allow_allm ? s19_store_size(allm_total, allm_big, want, keep_allm, min) : 0u;
    if (h == (want & ~31u) || a <= h) { *size = h; return h ? 0 : -1; }
    *size = a;
    return 4;
}

#endif
