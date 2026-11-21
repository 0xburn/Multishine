/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hub_rank.h: the texts of the Rank block on OUR Grandmaster character screen ("Rank" label, a tier disc, the tier name, the rating with one decimal; while
 * unrated "Placement" and the games left).  Pure C89, no libc, no heap, no Melee address, no float: the glue (hub_soc.c) draws what this fills in, host test hub/tests/hub_rank_test.c.
 * The data is OURS (RANKED_STATUS of our own server, client_net/socc.h struct socc_rank): the tier byte the server sends (0..4 Novice..Sensei, 254 Grandmaster, 255 placement), the
 * names and colours of docs/data/grandmaster_tiers.json (stb_tiers.h, the Standings screen's tier discs).  Nothing of Slippi's: no Slippi rank name, icon or data.
 */
#ifndef HUB_RANK_H
#define HUB_RANK_H
#include "socc.h"
#include "stb_tiers.h"
#include "../ms_fmt.h"

#define HR_TIER_MAX 12u              /* "Grandmaster" 11 characters + NUL = 12 */
#define HR_RATE_MAX 14u              /* "214748364.7" and the like: the bound keeps a hostile value inside the buffer */
#define HR_GREY 0x8A94A0ul           /* the placement disc (the same grey as the Standings placement ring) */

/* the tier text and its colour 0xRRGGBB for a tier byte; the empty string for a byte the screen does not know (the parser refuses those already) */
static __inline__ const char *hr_tier(unsigned tier, unsigned long *rgb)
{
    if (tier == SOCC_TIER_GM) { *rgb = STB_GM_RGB; return STB_GM_NAME; }
    if (tier == SOCC_TIER_PLACEMENT) { *rgb = STB_PL_RGB; return STB_PL_NAME; }
    if (tier < STB_NSTEPS) { *rgb = STB_STEP_RGB[STB_NSTEPS - 1u - tier]; return STB_STEP_NAME[STB_NSTEPS - 1u - tier]; }
    *rgb = HR_GREY;
    return "";
}

/* the rating with ONE decimal from tenths ("1439.1", "1440.0", "0.0": a negative number shows as 0.0, nobody has one); returns the length */
static __inline__ unsigned hr_rating(char *d, int tenths)
{
    unsigned long v = tenths < 0 ? 0ul : (unsigned long)tenths;
    unsigned n = ms_fmt_u(d, v / 10ul, 9u);
    d[n++] = '.';
    d[n++] = (char)('0' + v % 10ul);
    d[n] = 0;
    return n;
}

/* "3 games left" / "1 game left" for the placement line */
static __inline__ unsigned hr_left(char *d, unsigned left)
{
    static const char g[] = " games left", o[] = " game left";
    const char *w = left == 1u ? o : g;
    unsigned n = ms_fmt_u(d, left, 3u), i = 0;
    while (w[i]) { d[n + i] = w[i]; i++; }
    d[n + i] = 0;
    return n + i;
}

/* everything the block shows: tier text, the line under it (rating or games left), the disc colour; 0 = nothing to show (no reply, or a server without the tail) */
static __inline__ int hr_block(const struct socc_rank *rk, char *tier, char *line, unsigned long *rgb)
{
    const char *t;
    unsigned i = 0;
    if (!rk || !rk->have) return 0;
    t = hr_tier(rk->tier, rgb);
    while (t[i] && i + 1u < HR_TIER_MAX) { tier[i] = t[i]; i++; }
    tier[i] = 0;
    if (!tier[0]) return 0;
    if (rk->tier == SOCC_TIER_PLACEMENT) hr_left(line, rk->pl_left); else hr_rating(line, rk->shown10);
    return 1;
}
#endif
