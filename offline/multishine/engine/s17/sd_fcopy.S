/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : bulk copy for the snapshot engine (original work).
   void sd_fcopy(u32 *dst, const u32 *src, u32 nlines)   copies nlines x 32 bytes with lfd/stfd (8 bytes per access).
   lfd/stfd move bit patterns unchanged (no conversion, NaNs included); only volatile FPRs f0-f3 are used, which the
   EABI allows a callee to clobber.  Called only from the game thread (MSR[FP] set).  Pointers must be 8-aligned. */
        .text
        .globl  sd_fcopy
sd_fcopy:
        cmpwi   5,0
        beqlr
        mtctr   5
1:      lfd     0,0(4)
        lfd     1,8(4)
        lfd     2,16(4)
        lfd     3,24(4)
        addi    4,4,32
        stfd    0,0(3)
        stfd    1,8(3)
        stfd    2,16(3)
        stfd    3,24(3)
        addi    3,3,32
        bdnz    1b
        blr
