/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Multishine mark as an online indicator: ONE artwork and ONE look on the title, the Main Menu and the Settings pages, in two brightnesses.
 *   ONLINE  = the mark at full brightness (alpha 255).
 *   OFFLINE = the same mark 27 percent dimmer (alpha 187 = 11/15 of full): a step the eye sees when it is looked for, never a different mark.  187 and not a rounder
 *             number because the title's mark is a 4 bit image (15 levels): 11/15 of the full white is a whole level, so the title and the menus dim by the SAME ratio.
 *             (The Main Menu's old fixed half alpha was too dramatic for an indicator and made the mark look like a different graphic from the title's.)
 * Pure functions, no Melee address, plain C89: tests/ml_test.c (host) pins them; ml_glue.c (menus, Settings pages) and tt_glue.c (title) call them.
 *
 * Online, as the Main Menu's panels read it (ms_net_cost 107, on_backend.c): bit 0 (1) = link down, bit 3 (8) = the server answered before but not for 16 s, or never within the
 * grace after boot.  0 = online.  Bit 1 (2, guest) does not matter: a guest with a reachable server is online.
 * Menus and Settings pages: server answered = (v107 & 9) == 0; the white/dim level follows the LINK alone (mkl_link).
 * Title: the server is asked once per boot (the update check) and nothing else runs there, so bit 3 would turn on by itself 16 s after that one answer; the title uses the link bit
 * only, plus a PROOF: a server answer heard since boot (ms_net_cost 103 true once).  Until then (the first seconds, or the update check is off) the title shows the OFFLINE level:
 * a mark that dims for a moment is honest ("not known yet"), a mark that is bright and then drops would be the false claim.
 */
#ifndef ML_MARK_LEVEL_H
#define ML_MARK_LEVEL_H

#ifndef MARK_BLUE_FADEOFF
#define MARK_BLUE_FADEOFF 0             /* 0: the cyan state is the flat artwork in cyan (default); 1: the pairs of hexagons fainter (100 / 70 / 40 percent), a second texture, swapped at half the white-to-cyan fade */
#endif
#define MKL_FN static __attribute__((unused))
#define MKL_ALPHA_ON 255u
#define MKL_ALPHA_OFF 187u              /* = 255 * 11 / 15 */
#define MKL_NIB_ON 15u                  /* the same two levels in the title's 4 bit image: the full white, and 11 of 15 */
#define MKL_NIB_OFF 11u
#define MKL_BLUE_R 0x37u                /* the brand cyan of the logo's hexagons: the SERVER layer, */
#define MKL_BLUE_G 0xD6u
#define MKL_BLUE_B 0xFFu
#define MKL_BLUE_STEP 16u               /* blue mix (0 white .. 255 cyan) per frame on the menus: 16 frames, about 0.27 s */
#define MKL_FADE_STEP 4u                /* alpha per frame on the menus (68 / 4 = 17 frames, about 0.3 s); the title swaps its image at once */

/*, three states of ONE mark (the white texels simply change colour, no extra layer):
 *   link down                        = dim white   (alpha / level 187 = 11/15)         mkl_link false
 *   link up                          = full white                                      mkl_link true,  server false
 *   link up + the server answered    = the same mark in the brand cyan (mix 255)       mkl_menu_online / mkl_title_online (the "server answered" notions below)
 * Link is ms_net_cost(107) bit 0 only; the server proof is the existing notion of each place (menus: bits 0 and 3 clear; title: link and a heard answer). */
MKL_FN unsigned mkl_link(unsigned v107)
{
    return (v107 & 1u) == 0u;
}

MKL_FN unsigned mkl_menu_online(unsigned v107)
{
    return (v107 & 9u) == 0u;
}

MKL_FN unsigned mkl_title_online(unsigned v107, unsigned heard)
{
    return (v107 & 1u) == 0u && heard != 0u;
}

/* one colour channel from a (0..255) toward c by mix b (0..255): integer only */
MKL_FN unsigned mkl_mix(unsigned a, unsigned c, unsigned b)
{
    return a < c ? a + ((c - a) * b + 127u) / 255u : a - ((a - c) * b + 127u) / 255u;
}

/* the mark's colour 0xRRGGBB: grey level a (255 white, 187 dim) mixed toward the brand cyan by b */
MKL_FN unsigned mkl_rgb(unsigned a, unsigned b)
{
    return (mkl_mix(a, MKL_BLUE_R, b) << 16) | (mkl_mix(a, MKL_BLUE_G, b) << 8) | mkl_mix(a, MKL_BLUE_B, b);
}

MKL_FN unsigned mkl_blue(unsigned server)       /* the target mix */
{
    return server ? 255u : 0u;
}

/* one frame of the colour fade (the first decision is made by the caller: snap) */
MKL_FN unsigned mkl_bstep(unsigned cur, unsigned target)
{
    if (cur < target)
        return target - cur < MKL_BLUE_STEP ? target : cur + MKL_BLUE_STEP;
    if (cur > target)
        return cur - target < MKL_BLUE_STEP ? target : cur - MKL_BLUE_STEP;
    return cur;
}

MKL_FN unsigned mkl_alpha(unsigned online)
{
    return online ? MKL_ALPHA_ON : MKL_ALPHA_OFF;
}

/* one frame of the fade: cur moves toward target by at most MKL_FADE_STEP; cur 0 = not decided yet (snaps) */
MKL_FN unsigned mkl_step(unsigned cur, unsigned target)
{
    if (cur == 0u || cur == target)
        return target;
    if (cur < target)
        return target - cur < MKL_FADE_STEP ? target : cur + MKL_FADE_STEP;
    return cur - target < MKL_FADE_STEP ? target : cur - MKL_FADE_STEP;
}
#endif
