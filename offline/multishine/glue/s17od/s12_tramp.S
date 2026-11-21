/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S10: unchanged copy of.
   S7: unchanged copy of. */
/* trampolines (original work).
     0x801A4D98 C2 payload +0: s3_entry (scene-loop top; as ): s3_tick, then the displaced
                 `bl hsd_80392E80`.
     0x801A4DE8  04 `b s6n_iter_stub`: start of every engine iteration, right after Slippi's StartEngineLoop (C2 at
                 0x801A4DE4); runs s6n_iter, then the displaced `bl 0x800198E0` with LR = 0x801A4DEC.
     0x801A4FF0 04 `bl s6n_end_stub`: end of the logic of an iteration (as ); then HSD_PerfSetCPUTime. */
        .section .text.s3_entry,"ax"
        .globl  s3_entry
s3_entry:
        stwu    1,-16(1)
        bl      s3_tick
        addi    1,1,16
        lis     12,0x801A
        ori     12,12,0x4D9C
        mtlr    12
        lis     12,0x8039
        ori     12,12,0x2E80
        mtctr   12
        bctr

        .text
        .globl  s6n_iter_stub
s6n_iter_stub:
        stwu    1,-16(1)
        bl      s6n_iter
        addi    1,1,16
        lis     12,0x801A
        ori     12,12,0x4DEC
        mtlr    12
        lis     12,0x8001
        ori     12,12,0x98E0
        mtctr   12
        bctr

        .globl  s6n_end_stub
s6n_end_stub:
        stwu    1,-16(1)
        mflr    0
        stw     0,20(1)
        bl      s6n_end
        lwz     0,20(1)
        mtlr    0
        addi    1,1,16
        lis     12,0x8037
        ori     12,12,0xE23C
        mtctr   12
        bctr

/* diagnostics: LoadState.asm's "frame not found" `b 0` (patched at runtime by to branch here):
   r24 = requested frame, r26 = SSCB.  Report, then spin like the original. */
        .globl  s6n_lsfail_stub
s6n_lsfail_stub:
        mr      3,24
        mr      4,26
        bl      s6n_lsfail
1:      b       1b
