/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* offline/s17: leaf helper of the S17 restore kernel (sd_s17.h). Original work; the loop is kb4_lc2live
 * (console-proven in kbench4: H0 restore 3.49 ms, lab arena exact) with a zero-length guard.
 *
 *   void sd_lc2live(u32 d, u32 s, u32 nl)   nl whole lines from the locked cache (address s, 8-aligned) to live memory
 *       (address d, 32-aligned): per line dcbz d (the destination is not read), then 4 x lfd / stfd (bit patterns unchanged,
 *       NaNs included); only the volatile f0-f3 are used, MSR[FP] must be set (game thread), as.
 * A leaf without a stack frame: LR only ever holds the return address (offline/ rule 3). */
        .text
        .globl  sd_lc2live
sd_lc2live:
        cmpwi   5,0
        beqlr
        mtctr   5
1:      dcbz    0,3
        lfd     0,0(4)
        lfd     1,8(4)
        lfd     2,16(4)
        lfd     3,24(4)
        stfd    0,0(3)
        stfd    1,8(3)
        stfd    2,16(3)
        stfd    3,24(3)
        addi    4,4,32
        addi    3,3,32
        bdnz    1b
        blr
