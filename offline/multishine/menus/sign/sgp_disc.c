/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgp_disc: the player's disc for the system-files build (sgp_core.c), on the console: Melee's own DVD calls at their retail NTSC-U 1.02 addresses (the same ones the system-files builder uses,
 * ). Reads are synchronous (a bounded spin on a completion flag, 5 s per call); the Update screen asks for at most 128 KiB per frame. */
#include "sgu_core.h"

#define DVD_PATH ((int (*)(const char *))0x8033796Cu)                    /* DVDConvertPathToEntrynum */
#define DVD_FASTOPEN ((int (*)(int, void *))0x80337C60u)
#define DVD_CLOSE ((int (*)(void *))0x80337CD4u)
#define DVD_READ ((int (*)(void *, void *, int, int, void *, int))0x80337CF8u)      /* DVDReadAsyncPrio(fi, addr, len, offset, callback, prio) */
#define DC_INVAL ((void (*)(void *, unsigned))0x803447DCu)               /* DCInvalidateRange */

static unsigned char g_fi[0x40] __attribute__((aligned(32)));
static volatile int g_res;

static unsigned tbl(void)
{
    unsigned t;
    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}
static void cb(int result, void *fi) { (void)fi; g_res = result == 0 ? -1 : result; }

static int d_size(const char *path)
{
    int e = DVD_PATH(path);
    unsigned len;
    if (e < 0 || !DVD_FASTOPEN(e, g_fi)) return -1;
    len = *(volatile unsigned *)(g_fi + 0x34);
    DVD_CLOSE(g_fi);
    return (int)len;
}

static int d_read(const char *path, void *dst, unsigned off, unsigned len)
{
    int e = DVD_PATH(path), r;
    unsigned t0, lim, bus = *(volatile unsigned *)0x800000F8u, rl = (len + 31u) & ~31u;
    if (e < 0 || !DVD_FASTOPEN(e, g_fi)) return -1;
    g_res = 0;
    lim = (bus >= 80000000u && bus <= 300000000u ? bus / 4u : 40500000u) * 5u;       /* 5 s of time base */
    t0 = tbl();
    if (!DVD_READ(g_fi, dst, (int)rl, (int)off, (void *)cb, 2)) { DVD_CLOSE(g_fi); return -1; }
    while (g_res == 0) if (tbl() - t0 > lim) break;
    r = g_res > 0 ? 0 : -1;
    DVD_CLOSE(g_fi);
    if (!r) DC_INVAL(dst, rl);
    return r;
}

const struct sgu_disc sgp_disc = { d_size, d_read };
