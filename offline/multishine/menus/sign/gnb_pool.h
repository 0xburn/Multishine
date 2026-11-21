/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * SisLib pool dry run (GUI audit GN-01 / GN-02 / ST-01): can the pool hold one more text object?  Pure C89, no Melee address, host-tested
 * (tests/gnb_pool_test.c, which carries a model of the retail allocator).
 *
 * The retail SisLib allocator ( sysdolphin/baselib/sislib.c HSD_SisLib_Alloc / HSD_SisLib_Free) is a best-fit list over ONE pool of about 13 KB:
 *   - blocks are {next, data, size}, a 12 byte header in front of the data;
 *   - an allocation takes the first block of exactly its size, else the smallest block that is larger; only the FIRST block of the list (the head, the big remainder of the pool)
 *     is ever split (the rest is handed out whole); a request that no block can serve, or that would leave the head with a negative size, is an OSPanic (a console reset);
 *   - a free appends the block to the END of the list (it is merged into the head only when it is directly in front of it).
 * One text object (HSD_SisLib_803A6754) is three allocations: the HSD_Text (0xA0 bytes), a 0x10 block and the 0x80 SIS buffer (grown in steps of 0x80 by a long print).
 *
 * The first guard looked at the LARGEST free block only (refuse below 2 KB).  That is wrong in the one situation that matters: after a page of texts is freed (a screen change, a
 * cursor move that recolours a row) the pool is full of small, exactly fitting blocks and a small head, so the guard refused every text although each would have been served from a
 * freed block: rows lost their texts after the cursor moved, and the lobby opened with no text at all after a long list.  This file simulates the three allocations on a snapshot of
 * the free list instead.
 */
#ifndef GNB_POOL_H
#define GNB_POOL_H

#define GP_HDR 12u              /* sizeof(SisBlock) */
#define GP_TEXT 0xA0u           /* sizeof(HSD_Text) */
#define GP_AUX 0x10u            /* the text's alloc record */
#define GP_BUF 0x80u            /* the first SIS buffer */
#define GP_MAXB 192u            /* free blocks looked at */
#define GP_RESERVE 1536u        /* the head must keep this much when a text is cut from it (other code needs the pool too: the Event screen, the menu labels) */

/* take `size` bytes from the model of the free list fl[0..*n) (fl[0] = the head); 1 = served, 0 = the retail allocator would panic */
static int gp_take(unsigned *fl, unsigned *n, unsigned size)
{
    unsigned i, best = 0xFFFFu;
    size = (size + 3u) & ~3u;
    for (i = 0; i < *n; i++) {
        if (fl[i] == size) { best = i; break; }
        if (fl[i] > size && (best == 0xFFFFu || fl[best] > fl[i])) best = i;
    }
    if (best == 0xFFFFu) return 0;
    if (best == 0u) {
        if (fl[0] < size + GP_HDR) return 0;
        fl[0] -= size + GP_HDR;
    } else {
        for (i = best + 1u; i < *n; i++) fl[i - 1u] = fl[i];
        (*n)--;
    }
    return 1;
}

/* may one more text object be made?  fl = the free list sizes in list order (fl[0] = the head), n = how many.  Refused when the three allocations would panic, or when they would
   cut the head below GP_RESERVE (blocks served from freed pieces do not touch the head and are always fine). */
static int gp_text_ok(const unsigned *fl0, unsigned n)
{
    unsigned fl[GP_MAXB], i, head;
    if (!n || n > GP_MAXB) return 0;
    for (i = 0; i < n; i++) fl[i] = fl0[i];
    head = fl[0];
    if (!gp_take(fl, &n, GP_TEXT) || !gp_take(fl, &n, GP_AUX) || !gp_take(fl, &n, GP_BUF)) return 0;
    return n && (fl[0] >= GP_RESERVE || fl[0] == head) ? 1 : 0;
}

#endif
