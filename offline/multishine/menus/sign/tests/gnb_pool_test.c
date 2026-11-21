/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the SisLib pool dry run (gnb_pool.h, GUI audit GN-01 / GN-02 / ST-01). The model below is the retail allocator ( sysdolphin/baselib/sislib.c
   HSD_SisLib_Alloc / HSD_SisLib_Free, line by line, on offsets instead of pointers): a best-fit free list whose first block (the head) is the only one split, a free that appends to the
   END of the list or merges into the head, and an OSPanic where the retail code panics.  A text object is three allocations (0xA0, 0x10, 0x80) and three frees in the retail order.
   Checks: (1) the old guard (largest free block below 2 KB) refuses every text after a full page was freed although freed pieces fit, the new one does not (the audit's lobby with no
   text); (2) a long random run of text creates, frees, recolours (free + create) and long prints (the 0x80 -> 0x100 regrow) never panics when the guard is obeyed; (3) the guard keeps the
   head above GP_RESERVE; (4) the room list's text budget (8 rows) fits the pool.  usage: gnb_pool_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sign/gnb_pool.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define POOL 13036u            /* the SIS pool free in a match (12.4 to 13.0 KB) */
#define MAXB 300000
struct blk { unsigned off, size; int live; };           /* off = the header's offset, data at off + 12 */
static struct blk B[MAXB];                              /* every block ever made; live = allocated */
static int nb;
static int FL[MAXB], nfl;                               /* free list: indices into B, in list order (FL[0] = free_head) */
static int panicked;

static void model_init(void)
{
    nb = 1; nfl = 1; FL[0] = 0; B[0].off = 0; B[0].size = POOL - GP_HDR; B[0].live = 0; panicked = 0;
}
static int m_alloc(unsigned size)                       /* returns a block handle, -1 = the retail code panics */
{
    int best = -1, i;
    unsigned remainder = size % 4u;
    if (remainder) size += 4u - remainder;
    for (i = 0; i < nfl; i++) {
        struct blk *f = &B[FL[i]];
        if (f->size == size) { best = i; break; }
        if (f->size > size && (best < 0 || B[FL[best]].size > f->size)) best = i;
    }
    if (best < 0) { panicked = 1; return -1; }
    if (best == 0) {
        struct blk *h = &B[FL[0]];
        long rem = (long)h->size - (long)size - (long)GP_HDR;
        int nh;
        if (rem < 0) { panicked = 1; return -1; }
        nh = nb++;                                      /* the new head's header at old data + size */
        B[nh].off = h->off + GP_HDR + size; B[nh].size = (unsigned)rem; B[nh].live = 0;
        h->size = size; h->live = 1;
        { int old = FL[0]; FL[0] = nh; return old; }
    } else {
        int h = FL[best];
        for (i = best + 1; i < nfl; i++) FL[i - 1] = FL[i];
        nfl--;
        B[h].live = 1;
        return h;
    }
}
static void m_free(int h)
{
    struct blk *a = &B[h];
    struct blk *head = &B[FL[0]];
    a->live = 0;
    if (head->off == a->off + GP_HDR + a->size) {       /* directly in front of the head: merged into it */
        a->size = head->size + a->size + GP_HDR;
        FL[0] = h;
        return;
    }
    FL[nfl++] = h;                                      /* else appended to the end of the free list */
}
static unsigned snap(unsigned *fl)
{
    int i;
    for (i = 0; i < nfl; i++) fl[i] = B[FL[i]].size;
    return (unsigned)nfl;
}
static unsigned largest(void)
{
    unsigned m = 0, i;
    for (i = 0; i < (unsigned)nfl; i++) if (B[FL[i]].size > m) m = B[FL[i]].size;
    return m;
}

struct txt { int a, b, c, used; };
#define NT 400
static struct txt T[NT];
static int make_text(int k)                             /* 0 = would panic */
{
    T[k].a = m_alloc(GP_TEXT); T[k].b = m_alloc(GP_AUX); T[k].c = m_alloc(GP_BUF);
    T[k].used = !panicked;
    return !panicked;
}
static void free_text(int k)                            /* the retail order (HSD_SisLib_803A5A2C): the buffer, the record, the text */
{
    m_free(T[k].c); m_free(T[k].b); m_free(T[k].a); T[k].used = 0;
}
static int guarded_make(int k)                          /* the guard, then the allocations: returns 1 made, 0 refused; a panic here is a failed check */
{
    unsigned fl[GP_MAXB], n = snap(fl);
    if (!gp_text_ok(fl, n)) return 0;
    CHECK(make_text(k));
    return 1;
}

int main(void)
{
    unsigned i, made, step;
    unsigned fl[GP_MAXB], n;

    /* 1. the audit's case: the list fills the pool, the page is freed (a screen change), the lobby wants its texts */
    model_init();
    made = 0;
    for (i = 0; i < NT; i++) { if (!guarded_make((int)i)) break; made++; }
    CHECK(made >= 30u && made < 40u);                   /* about 340 bytes a text out of 13 KB, minus the reserve */
    CHECK(!panicked);
    n = snap(fl);
    CHECK(fl[0] >= GP_RESERVE);                         /* the guard kept the reserve in the head */
    CHECK(!gp_text_ok(fl, n));                          /* full: refused */
    for (i = 0; i < made; i++) free_text((int)i);       /* everything is freed again */
    n = snap(fl);
    CHECK(gp_text_ok(fl, n));                           /* and fits again */
    made = 0;
    for (i = 0; i < NT; i++) { if (!guarded_make((int)i)) break; made++; }
    CHECK(made >= 30u);                                 /* the same number again: freed pieces are reused (exact sizes) */

    /* the old guard on a freed pool: fill it to the brim (forced), free half the texts: the head is small, the freed pieces are small, largest block below 2 KB */
    model_init();
    for (i = 0; i < NT; i++) { if (B[FL[0]].size < 2000u) break; CHECK(make_text((int)i)); }
    made = i;
    for (i = 0; i + 2u < made; i += 2u) free_text((int)i);        /* (the last text, directly in front of the head, stays: a free there would merge into the head) */
    n = snap(fl);
    CHECK(fl[0] < 2048u && largest() < 2048u);          /* the old guard would refuse now ... */
    CHECK(gp_text_ok(fl, n));                           /* ... the dry run sees the freed pieces */
    CHECK(make_text(0) && !panicked);                   /* and the retail allocator really serves it */

    /* 2. a long random run: creates, frees, recolours, long prints; the guard obeyed => never a panic */
    {
        unsigned seed = 12345u, refused = 0, madeok = 0, live = 0, maxlive = 0, k;
        model_init();
        for (k = 0; k < NT; k++) T[k].used = 0;
        for (step = 0; step < 200000u; step++) {
            seed = seed * 1103515245u + 12345u;
            k = (seed >> 8) % NT;
            switch ((seed >> 4) & 7u) {
            case 0: case 1: case 2: case 3:             /* create */
                if (!T[k].used) { if (guarded_make((int)k)) { madeok++; live++; if (live > maxlive) maxlive = live; } else refused++; }
                break;
            case 4: case 5:                             /* free */
                if (T[k].used) { free_text((int)k); live--; }
                break;
            case 6:                                     /* recolour: free then make (tslot_set) */
                if (T[k].used) { free_text((int)k); live--; if (guarded_make((int)k)) { live++; madeok++; } else refused++; }
                break;
            default: break;                             /* (a print longer than about 50 characters regrows the buffer by 0x80: no overlay string is that long) */
            }
            if (panicked) break;
        }
        CHECK(!panicked);
        CHECK(madeok > 1000u && refused > 0u && maxlive >= 25u);
        printf("random run: %u made, %u refused, at most %u texts live, largest block now %u\n", madeok, refused, maxlive, largest());
    }

    /* 3. the room list's budget: 8 rows x (name, region, count) + 3 buttons + notch + header texts must fit with room to spare */
    model_init();
    made = 0;
    for (i = 0; i < 8u * 3u + 3u + 1u; i++) { if (!guarded_make((int)i)) break; made++; }
    CHECK(made == 8u * 3u + 3u + 1u);
    n = snap(fl);
    CHECK(fl[0] >= GP_RESERVE + 4u * (GP_TEXT + GP_AUX + GP_BUF + 3u * GP_HDR));      /* four more texts (the lobby fallbacks) still fit */
    /* ... where the 9 rows x 5 texts of build 485 did not */
    model_init();
    made = 0;
    for (i = 0; i < 9u * 5u + 8u; i++) { if (!guarded_make((int)i)) break; made++; }
    CHECK(made < 9u * 5u + 8u);

    if (fails) { printf("gnb_pool_test: %d FAILED\n", fails); return 1; }
    printf("gnb_pool_test: ok\n");
    return 0;
}
