/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sdenet_clock: PowerPC time base -> milliseconds / microseconds for the network glue, with the time-base RATE read at
 * run time instead of the 40,500 ticks/ms (and 81 ticks per 2 us, 675,000 ticks per frame) hard-coded in every stage's
 * clock_step.
 *
 * Rate: the OS global "bus clock" at 0x800000F8 divided by 4 (Gekko/Broadway time base = bus / 4): 162 MHz -> 40.5 MHz
 * on a GameCube (Dolphin writes 162 MHz there: :81), 243 MHz ->
 * 60.75 MHz on a Wii under Nintendont (its kernel rewrites the word; ). A value outside
 * 100..300 MHz is treated as unset and the GameCube rate is used (source = SDENET_CLK_FALLBACK).  On the host (tests)
 * the word does not exist: sdenet_clock_host_bus_hz (0 = GameCube) stands in for it, or pass tb_hz explicitly.
 *
 * Conversion: exact rational steps from gcd(tb_hz, 1000) and gcd(tb_hz, 1000000); at 40.5 MHz they are 1 ms per 40,500
 * ticks and 2 us per 81 ticks, i.e. bit-identical to the existing clock_step (tests/test_clock_tb.c checks it); at
 * 60.75 MHz 1 ms per 60,750 and 4 us per 243 ticks.  Overflow-free for any delta (the 32-bit TBL wraps every 106 s at
 * 40.5 MHz, 70 s at 60.75 MHz; call sdenet_clock_step at least that often).  No libc, no 64-bit arithmetic, no FPU.
 */
#ifndef SDENET_CLOCK_H
#define SDENET_CLOCK_H

#include "sdenet.h"

#define SDENET_CLK_GC_TB_HZ 40500000u     /* GameCube: 162 MHz bus / 4 */
#define SDENET_CLK_OS_BUS_ADDR 0x800000F8u
#define SDENET_CLK_BUS_MIN 100000000u
#define SDENET_CLK_BUS_MAX 300000000u

enum { SDENET_CLK_EXPLICIT = 0, SDENET_CLK_OS = 1, SDENET_CLK_FALLBACK = 2, SDENET_CLK_HOST = 3 };

struct sdenet_clock {
    sde_u32 tb_hz;                   /* time-base ticks per second in use */
    sde_u32 ms_ticks, ms_step;       /* ms_step milliseconds per ms_ticks ticks (exact) */
    sde_u32 us_ticks, us_step;       /* us_step microseconds per us_ticks ticks (exact) */
    sde_u32 frame_ticks;             /* tb_hz / 60 (675,000 on a GameCube), the glue's FRAME_TICKS */
    sde_u32 last, rem_ms, rem_us, ms, us;
    sde_u8 started, source;
};

#if !(defined(__PPC__) || defined(__powerpc__))
extern sde_u32 sdenet_clock_host_bus_hz;   /* host tests only: stands in for the word at 0x800000F8 (0 = GameCube) */
#endif

/* Time-base rate from the OS bus clock (see above); *source (may be 0) says where it came from. */
sde_u32 sdenet_clock_tb_hz(sde_u8 *source);
/* tb_hz 0: sdenet_clock_tb_hz.  The first sdenet_clock_step only latches the time base (ms/us start at 0). */
void sdenet_clock_init(struct sdenet_clock *c, sde_u32 tb_hz);
/* Advance by the ticks since the previous call (tb_now = TBL, wrap-safe). */
void sdenet_clock_step(struct sdenet_clock *c, sde_u32 tb_now);
sde_u32 sdenet_clock_ms(struct sdenet_clock *c, sde_u32 tb_now);
sde_u32 sdenet_clock_us(struct sdenet_clock *c, sde_u32 tb_now);
/* Ticks for a duration (wraps like the time base; fine for durations below one TBL period). */
sde_u32 sdenet_clock_ms_to_ticks(const struct sdenet_clock *c, sde_u32 ms);
sde_u32 sdenet_clock_us_to_ticks(const struct sdenet_clock *c, sde_u32 us);
/* A tick count tuned on a GameCube (40.5 MHz) scaled to the rate in use: unchanged at 40.5 MHz, x1.5 at 60.75 MHz (same
   wall-clock time).  Uses ticks-per-ms (tb_hz / 1000), 32-bit only; results above 2^32 wrap. */
sde_u32 sdenet_clock_gc_ticks(const struct sdenet_clock *c, sde_u32 gc_ticks);

#if defined(__PPC__) || defined(__powerpc__)
static inline sde_u32 sdenet_clock_tbl(void) { sde_u32 v; __asm__ volatile("mftb %0" : "=r"(v)); return v; }
#endif

#endif
