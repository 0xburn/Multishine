/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sdenet_clock.c: see sdenet_clock.h.  Project-owned. */
#include "sdenet_clock.h"

#if !(defined(__PPC__) || defined(__powerpc__))
sde_u32 sdenet_clock_host_bus_hz;
#endif

static sde_u32 gcd(sde_u32 a, sde_u32 b)
{
    while (b) { sde_u32 t = a % b; a = b; b = t; }
    return a;
}

sde_u32 sdenet_clock_tb_hz(sde_u8 *source)
{
    sde_u32 bus;
    sde_u8 src;
#if defined(__PPC__) || defined(__powerpc__)
    bus = *(volatile sde_u32 *)SDENET_CLK_OS_BUS_ADDR;
    src = SDENET_CLK_OS;
#else
    bus = sdenet_clock_host_bus_hz;
    src = SDENET_CLK_HOST;
    if (!bus) bus = SDENET_CLK_GC_TB_HZ * 4u;
#endif
    if (bus < SDENET_CLK_BUS_MIN || bus > SDENET_CLK_BUS_MAX) { bus = SDENET_CLK_GC_TB_HZ * 4u; src = SDENET_CLK_FALLBACK; }
    if (source) *source = src;
    return bus / 4u;
}

void sdenet_clock_init(struct sdenet_clock *c, sde_u32 tb_hz)
{
    sde_u8 src = SDENET_CLK_EXPLICIT, *b = (sde_u8 *)c;
    sde_u32 i, g;
    for (i = 0; i < sizeof *c; i++) b[i] = 0;
    if (!tb_hz) tb_hz = sdenet_clock_tb_hz(&src);
    c->tb_hz = tb_hz;
    c->source = src;
    g = gcd(tb_hz, 1000u);
    c->ms_ticks = tb_hz / g; c->ms_step = 1000u / g;
    g = gcd(tb_hz, 1000000u);
    c->us_ticks = tb_hz / g; c->us_step = 1000000u / g;
    c->frame_ticks = tb_hz / 60u;
}

void sdenet_clock_step(struct sdenet_clock *c, sde_u32 tb_now)
{
    sde_u32 d;
    if (!c->started) { c->started = 1; c->last = tb_now; return; }
    d = tb_now - c->last;
    c->last = tb_now;
    /* split the delta first so rem + d can never overflow */
    c->ms += (d / c->ms_ticks) * c->ms_step;
    c->rem_ms += d % c->ms_ticks;
    if (c->rem_ms >= c->ms_ticks) { c->rem_ms -= c->ms_ticks; c->ms += c->ms_step; }
    c->us += (d / c->us_ticks) * c->us_step;
    c->rem_us += d % c->us_ticks;
    if (c->rem_us >= c->us_ticks) { c->rem_us -= c->us_ticks; c->us += c->us_step; }
}

sde_u32 sdenet_clock_ms(struct sdenet_clock *c, sde_u32 tb_now) { sdenet_clock_step(c, tb_now); return c->ms; }
sde_u32 sdenet_clock_us(struct sdenet_clock *c, sde_u32 tb_now) { sdenet_clock_step(c, tb_now); return c->us; }

static sde_u32 scale(sde_u32 v, sde_u32 step, sde_u32 ticks)
{
    return (v / step) * ticks + ((v % step) * ticks) / step;   /* (v % step) * ticks < step * ticks = tb_hz */
}

sde_u32 sdenet_clock_ms_to_ticks(const struct sdenet_clock *c, sde_u32 ms) { return scale(ms, c->ms_step, c->ms_ticks); }
sde_u32 sdenet_clock_us_to_ticks(const struct sdenet_clock *c, sde_u32 us) { return scale(us, c->us_step, c->us_ticks); }

/* The GameCube's ticks per ms lives in DATA on purpose: Nintendont's timer patcher rewrites 40,500 immediates in loaded code
   (li 0 / ori 0x9E34) to the Wii value, which would silently turn the scaling below
   into the identity on a Wii.  (The 162,000,000 / 40,500,000 fallback constants may be patched: the fallback is then the Wii rate,
   which is what that case should give.) */
static const volatile sde_u32 gc_ticks_per_ms = 40500u;

sde_u32 sdenet_clock_gc_ticks(const struct sdenet_clock *c, sde_u32 gc_ticks)
{
    sde_u32 k = c->tb_hz / 1000u, g = gc_ticks_per_ms;   /* k: ticks per ms in use; k * 40,499 < 2^32 for tb_hz <= 75 MHz */
    return (gc_ticks / g) * k + ((gc_ticks % g) * k) / g;
}
