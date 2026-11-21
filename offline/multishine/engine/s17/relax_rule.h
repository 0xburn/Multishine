/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : the relaxed main-stick compare (S17 flag RELAX, 0x4000), pure rule, no dependencies. Original work.
 * Derivation and exactness argument:.  Summary:
 *   Slippi compares the predicted and the received pad byte for byte (TriggerSendInput.asm 482-530).  The game reads the main stick only through
 *   (1) the fighter dead zone |x| <= 22 of 80 -> 0 (PlCo.dat 0.28;  Fighter_Spaghetti_8006AD10), (2) the HSD circle clamp (r > 80, melee gm init:
 *   clamp_stickMin 0, so no library dead zone), and (3) the UCF 0.84 pad buffer, which keeps the RAW (x, y) of the last 4 frames of every player and
 *   lets Shield SDI / SDI / Dashback / Tumble test the squared raw difference of the current frame and the frame two earlier against 3844 (62^2),
 *   5625 (75^2) and 1936 (44^2; Y only).  A pad that differs from the predicted one only in the raw X, with both X inside the dead zone and the
 *   circle inside the clamp, is identical for (1) and (2); (3) is handled by the guards G1 / G3 below and by patching the history entry of the frame; the
 *   derived stick-direction bits of the button word (HSD_PadADConvert) must be equal too (rx_adc_bits).
 */
#ifndef RELAX_RULE_H
#define RELAX_RULE_H

#define RX_DZ 22                    /* |x| <= 22 of 80: fighter dead zone (0.28 x 80 = 22.4; 23 / 80 = 0.2875 > 0.28) */
#define RX_CLAMP2 6400              /* x^2 + y^2 <= 80^2: the HSD circle clamp leaves the stick unchanged */

/* the squared-difference tests of the UCF modules on one (X, Y) difference of the current raw pad and the raw pad two frames earlier:
   bit 0 dx^2 > 3844 (Shield SDI), bit 1 dx^2 > 5625 (Dashback, Tumble), bit 2 dx^2 + dy^2 > 3844 (SDI), bit 3 dy^2 > 1936 (the pad buffer's counter) */
static inline unsigned rx_ucf_tests(int dx, int dy)
{
    unsigned t = 0;
    if (dx * dx > 3844) t |= 1u;
    if (dx * dx > 5625) t |= 2u;
    if (dx * dx + dy * dy > 3844) t |= 4u;
    if (dy * dy > 1936) t |= 8u;
    return t;
}

/* HSD_PadADConvert (adc_type 0, adc_th 30 from default_libinfo_data, adc_angle 0): the stick as direction bits of the pad's button word (PAD_STICK_UP 0x10000,
   DOWN 0x20000, LEFT 0x40000, RIGHT 0x80000) when r >= 30; they end up in fp->input.held_buttons / pressed / released.  0 = none, 1 up, 2 down, 4 left, 8 right,
   -1 = exactly on a 45 degree boundary (the float angle decides: not claimed). */
static inline int rx_adc_bits(int x, int y)
{
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    if (x * x + y * y < 900) return 0;
    if (ax == ay) return -1;
    if (ay > ax) return y > 0 ? 1 : 2;
    return x > 0 ? 8 : 4;
}

struct rx_ctx {
    int have_m2;                    /* frame h-2 of this player is known (it was confirmed before h) */
    int xm2_used, xm2_real, ym2;    /* its raw X as the game used it (the predicted X when it was accepted by this rule) and as it really was; its raw Y.
                                       The ring holds the used X until a patch rewrites it to the real X, so G1 tests both */
    int have_q2;                    /* frame h+2 has been simulated already */
    int xq2, yq2;                   /* its raw (X, Y) as the game used it (the predicted ring entry) */
};

/* may the predicted raw X xp be kept although the real one is xr (same raw Y = y in both pads)?  1 = the game cannot tell */
static inline int rx_main_ok(int xp, int xr, int y, const struct rx_ctx *c)
{
    if (xp == xr) return 1;
    if (xp < -RX_DZ || xp > RX_DZ || xr < -RX_DZ || xr > RX_DZ) return 0;                         /* the fighter would see a different x */
    if (xp * xp + y * y > RX_CLAMP2 || xr * xr + y * y > RX_CLAMP2) return 0;                      /* the circle clamp would couple x into y */
    {   /* the derived stick-direction bits of the button word must be identical (a flip needs |y| in about 21..29 and |x| near 20..22) */
        int a1 = rx_adc_bits(xp, y), a2 = rx_adc_bits(xr, y);
        if (a1 != a2 || a1 < 0) return 0;
    }
    if (!c || !c->have_m2) return 0;                                                               /* G1: the check of frame h itself needs the frame two back */
    {   /* the ring entry of frame h-2 held the used X or (after a patch) the real X when frame h was last simulated: both must give the real world's answer */
        unsigned want = rx_ucf_tests(xr - c->xm2_real, y - c->ym2);
        if (rx_ucf_tests(xp - c->xm2_used, y - c->ym2) != want || rx_ucf_tests(xp - c->xm2_real, y - c->ym2) != want) return 0;
    }
    if (c->have_q2 && rx_ucf_tests(c->xq2 - xp, c->yq2 - y) != rx_ucf_tests(c->xq2 - xr, c->yq2 - y)) return 0;   /* G3: the check of frame h+2 as simulated */
    return 1;
}
#endif
