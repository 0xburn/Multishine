/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of ms_rec_seed (ml_set.c, the console-side seeding of Melee's own save) on a synthetic GmSaveData.
     mrec_test            run the checks
     mrec_test --dump     write the seeded image of an EMPTY save (0x1790 bytes) to stdout */
#include <stdio.h>
#include <string.h>
#include "../ml_set.h"
#include "../ml_rec_tab.h"

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define GSD_SIZE 0x1790u
#define FD(s) (0x6C4u + 0xACu * (s))

static unsigned long g32(const unsigned char *p) { return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) | ((unsigned long) p[2] << 8) | p[3]; }
static void p32(unsigned char *p, unsigned long v) { p[0] = (unsigned char) (v >> 24); p[1] = (unsigned char) (v >> 16); p[2] = (unsigned char) (v >> 8); p[3] = (unsigned char) v; }
static unsigned flags(const unsigned char *g, unsigned s) { return (unsigned) ((g[FD(s) + 0x7C] << 8) | g[FD(s) + 0x7D]); }

int main(int argc, char **argv)
{
    static unsigned char g[GSD_SIZE], h[GSD_SIZE];
    unsigned s, nt = 0, nh = 0, ne = 0, t, hh, n;
    for (s = 0; s < MREC_NSLOT; s++) {
        nt += mrec_target[s] != 0;
        nh += mrec_hrc[s] != 0;
    }
    for (s = 0; s < MREC_NEVENT; s++)
        ne += mrec_event[s] != 0;
    if (argc > 1 && !strcmp(argv[1], "--dump")) {
        memset(g, 0, sizeof g);
        ms_rec_seed(g, 0, 0);
        fwrite(g, 1, sizeof g, stdout);
        return 0;
    }
    CHECK(nt >= 10 && nh >= 20 && ne == MREC_NEVENT);       /* all 51 events have a record */                                   /* the dataset is not empty */
    /* empty save: everything in the table is written */
    memset(g, 0, sizeof g);
    n = ms_rec_seed(g, &t, &hh);
    CHECK(n == nt + nh + ne && t == nt && hh == nh);
    for (s = 0; s < MREC_NEVENT; s++) {                           /* every event: completed bit (big-endian s64 at 0x200) and the record in best[k] */
        unsigned long lo = g32(g + 0x204), hi = g32(g + 0x200);
        CHECK((s < 32 ? (lo >> s) : (hi >> (s - 32))) & 1ul);
        CHECK(g32(g + 0x208 + 4 * s) == mrec_event[s]);
    }
    CHECK(g32(g + 0x200) == 0x7FFFFul && g32(g + 0x204) == 0xFFFFFFFFul);   /* exactly bits 0..50 */
    for (s = 0; s < MREC_NSLOT; s++) {
        if (mrec_target[s]) {
            CHECK((flags(g, s) & 0x8000u) && g32(g + FD(s) + 0x94) == mrec_target[s]);
            CHECK(g32(g + 0x7C + 4 * s) == mrec_target[s] && (g32(g + 0x38) & (1ul << s)));
        } else {
            CHECK(!(flags(g, s) & 0x8000u) && g32(g + FD(s) + 0x94) == 0 && g32(g + 0x7C + 4 * s) == 0 && !(g32(g + 0x38) & (1ul << s)));
        }
        CHECK(g32(g + FD(s) + 0x84) == mrec_hrc[s] && g32(g + 0xE0 + 4 * s) == mrec_hrc[s]);
    }
    /* a second run is a no-op, byte for byte */
    memcpy(h, g, sizeof g);
    CHECK(ms_rec_seed(g, &t, &hh) == 0 && t == 0 && hh == 0 && !memcmp(g, h, sizeof g));
    /* the player's data: a completed Target Test (also a worse one than the world record) and a Home-Run distance stay as they are */
    memset(g, 0, sizeof g);
    g[FD(2) + 0x7C] = 0x80; g[FD(2) + 0x7D] = 0x05; p32(g + FD(2) + 0x94, 9999);      /* Fox: completed, 166 s, plus another flag bit */
    p32(g + 0x38, 1ul << 2); p32(g + 0x7C + 8, 9999);
    p32(g + FD(0) + 0x84, 12345); p32(g + 0xE0, 12345);                                /* Captain Falcon: the player flew 123.45 m */
    g[FD(8) + 0x7D] = 0x09; p32(g + FD(8) + 0x94, 3);                                  /* Mario: not completed, 3 targets broken (the word is a count) */
    p32(g + 0x204, (1ul << 5) | (1ul << 11)); p32(g + 0x208 + 4 * 5, 777); p32(g + 0x208 + 4 * 11, 1);   /* events k=5 (a worse time) and k=11 (a worse KO count) already cleared by the player */
    memcpy(h, g, sizeof g);
    n = ms_rec_seed(g, &t, &hh);
    CHECK(g[FD(2) + 0x7C] == 0x80 && g[FD(2) + 0x7D] == 0x05 && g32(g + FD(2) + 0x94) == 9999 && g32(g + 0x7C + 8) == 9999);
    CHECK(g32(g + FD(2) + 0x84) == mrec_hrc[2]);                                       /* Fox's empty Home-Run slot is still seeded */
    CHECK(g32(g + FD(0) + 0x84) == 12345 && g32(g + 0xE0) == 12345);
    CHECK(mrec_target[0] && (flags(g, 0) & 0x8000u) && g32(g + FD(0) + 0x94) == mrec_target[0]);   /* ... and Falcon's empty Target Test slot */
    CHECK(mrec_target[8] && flags(g, 8) == (0x8000u | 0x09u) && g32(g + FD(8) + 0x94) == mrec_target[8]);   /* a count is not a time: replaced, other flag bits kept */
    CHECK(t == nt - 1 && hh == nh - 1 && n == t + hh + ne - 2);
    CHECK(g32(g + 0x208 + 4 * 5) == 777 && g32(g + 0x208 + 4 * 11) == 1 && g32(g + 0x208 + 4 * 40) == mrec_event[40]);   /* done events keep the player's value, an incomplete one is seeded */
    /* every byte outside the touched fields is unchanged */
    for (s = 0; s < GSD_SIZE; s++) {
        unsigned slot = (s >= 0x6C4u && s < 0x6C4u + 0xACu * MREC_NSLOT) ? (s - 0x6C4u) / 0xACu : 99u, off = (s - 0x6C4u) % 0xACu;
        int touched = (slot < MREC_NSLOT && (off == 0x7C || off == 0x7D || (off >= 0x84 && off < 0x88) || (off >= 0x94 && off < 0x98))) || (s >= 0x38 && s < 0x3C) || (s >= 0x7C && s < 0xE0 + 4 * MREC_NSLOT) || (s >= 0x200 && s < 0x208 + 4 * MREC_NEVENT);
        if (!touched && g[s] != h[s]) {
            printf("FAIL byte 0x%X changed\n", s);
            fails++;
            break;
        }
    }
    /* a mirror that holds a better time than the table keeps it; one that holds a longer flight keeps it */
    memset(g, 0, sizeof g);
    p32(g + 0x38, 1ul << 13); p32(g + 0x7C + 4 * 13, 100);                              /* Pikachu mirror 100 frames, table has more */
    p32(g + 0xE0 + 4 * 12, 999999);                                                    /* Peach mirror flew 9999.99 m, fighter table empty */
    ms_rec_seed(g, 0, 0);
    CHECK(g32(g + 0x7C + 4 * 13) == 100 && g32(g + FD(13) + 0x94) == mrec_target[13]);
    CHECK(g32(g + FD(12) + 0x84) == 999999 && g32(g + 0xE0 + 4 * 12) == 999999);
    /* NULL counters are fine */
    memset(g, 0, sizeof g);
    CHECK(ms_rec_seed(g, 0, 0) == nt + nh + ne);
    if (fails)
        printf("%d failure(s)\n", fails);
    else
        printf("mrec_test: all ok (%u Target Test, %u Home-Run slots in the table)\n", nt, nh);
    return fails != 0;
}
