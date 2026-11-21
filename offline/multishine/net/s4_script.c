/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* s4_script.c: see s4_script.h (project-owned, no libc) */
#include "s4_script.h"

unsigned int s4_mix32(unsigned int x)
{
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

void s4_script_pad(unsigned int seed, int tag, unsigned char out[8])
{
    unsigned int seg, h, g, kind;
    int sx, sy, i;
    for (i = 0; i < 8; i++) out[i] = 0;
    if (tag < 1) return;
    seg = (unsigned int)(tag - 1) / 6u;
    h = s4_mix32(seed ^ s4_mix32(seg * 2u + 1u));
    g = s4_mix32(h ^ 0x85EBCA6Bu);
    kind = h & 7u;
    sx = (int)((g & 0xFFu) % 201u) - 100;
    sy = (int)(((g >> 8) & 0xFFu) % 201u) - 100;
    if (kind == 0) return;                                   /* neutral */
    out[2] = (unsigned char)sx; out[3] = (unsigned char)sy;
    switch (kind) {
    case 3: out[0] = 0x01; break;                            /* A + stick */
    case 4: out[0] = 0x02; break;                            /* B + stick */
    case 5: out[0] = 0x04; break;                            /* X (jump) + stick */
    case 6: out[1] = 0x20; out[7] = 0xFF; break;             /* R shield + stick */
    case 7:                                                  /* C-stick smash */
        out[4] = (unsigned char)((int)(((g >> 16) & 0xFFu) % 3u) * 100 - 100);
        out[5] = (unsigned char)((int)(((g >> 24) & 0xFFu) % 3u) * 100 - 100);
        break;
    default: break;                                          /* 1, 2: stick only */
    }
}
