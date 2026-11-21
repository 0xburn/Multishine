/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * ms_fmt.h: the one decimal formatter of the menu code (pruning item D4): unsigned to decimal digits, no libc, no heap, no float, strict C89.
 * Static inline, so a translation unit that does not use it pays nothing.  Include as "../ms_fmt.h" from a menus/<dir>/ file (the manifest stages
 * it at for the staged builds, next to offline/s17/net/).
 *
 *   ms_fmt_u(d, v, cap)       writes the decimal digits of v at d (most significant first, no terminator, 0 -> "0"), at most cap digits (the LOW
 *                             cap digits when v is longer; cap <= 24), returns the digit count
 *   ms_fmt_ue(p, e, v, cap)   the same appended at p while p < e (a line that must stay inside its buffer); returns the new p
 */
#ifndef MS_FMT_H
#define MS_FMT_H

static __inline__ unsigned ms_fmt_u(char *d, unsigned long v, unsigned cap)
{
    char t[24];
    unsigned n = 0, i;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < cap);
    for (i = 0; i < n; i++) d[i] = t[n - 1u - i];
    return n;
}

static __inline__ char *ms_fmt_ue(char *p, char *e, unsigned long v, unsigned cap)
{
    char t[24];
    unsigned n = ms_fmt_u(t, v, cap), i;
    for (i = 0; i < n && p < e; i++) *p++ = t[i];
    return p;
}

#endif
