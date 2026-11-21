/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sdenet_glue_clock.h: the time helpers every network glue needs, with the
 * time-base RATE read once from the OS (sdenet_clock.h) instead of the hard-coded 40,500 ticks/ms, 81 ticks per 2 us and
 * 675,000 ticks per frame.  Include after sdenet.h in the single glue translation unit and link sdenet_clock.c.
 * On a GameCube (162 MHz bus word, or an implausible word) every value equals the old constants bit for bit.
 *
 *   sdg_tbl          the time base low word (mftb; host tests define SDG_TBL to a fake counter before including)
 *   sdg_clock_ms/us    the sdenet host callbacks' time (monotonic, wrap-safe, accumulated like the old clock_step)
 *   sdg_frame_ticks  tb_hz / 60 (675,000 on a GameCube)
 *   sdg_us_ticks(us)   microseconds -> ticks (the old x_delay: us * 81 / 2)
 *   sdg_gc_ticks(n)    a tick count tuned at 40.5 MHz scaled to the actual rate (same wall-clock time)
 */
#ifndef SDENET_GLUE_CLOCK_H
#define SDENET_GLUE_CLOCK_H

#include "sdenet_clock.h"

#ifndef SDG_TBL
#define SDG_TBL() sdenet_clock_tbl()
#endif

static struct sdenet_clock sdg_clk;
static int sdg_ready;

static inline sde_u32 sdg_tbl(void) { return SDG_TBL(); }
static inline void sdg_init(void)
{
    if (!sdg_ready) { sdenet_clock_init(&sdg_clk, 0); sdg_ready = 1; }
}
static inline sde_u32 sdg_hz(void) { sdg_init(); return sdg_clk.tb_hz; }
static inline sde_u32 sdg_frame_ticks(void) { sdg_init(); return sdg_clk.frame_ticks; }
static inline sde_u32 sdg_us_ticks(sde_u32 us) { sdg_init(); return sdenet_clock_us_to_ticks(&sdg_clk, us); }
static inline sde_u32 sdg_ms_ticks(sde_u32 ms) { sdg_init(); return sdenet_clock_ms_to_ticks(&sdg_clk, ms); }
static inline sde_u32 sdg_gc_ticks(sde_u32 n) { sdg_init(); return sdenet_clock_gc_ticks(&sdg_clk, n); }
static inline sde_u32 sdg_clock_ms(void) { sdg_init(); return sdenet_clock_ms(&sdg_clk, sdg_tbl()); }
static inline sde_u32 sdg_clock_us(void) { sdg_init(); return sdenet_clock_us(&sdg_clk, sdg_tbl()); }

#endif
