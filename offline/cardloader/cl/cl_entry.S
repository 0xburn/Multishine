/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* First word of the loader image: the C0 ("execute ASM") code of the Gecko list starts executing here.
 *
 * Calling convention of the UCF codehandler (the old third-party handler, replaced by our own ch/ms_codehandler.S, which does not depend on r4; the full preserve is kept as harmless): `mtlr r15; blrl`, and
 * AFTER the call it computes the next code address from r4 (= line count * 8), a volatile register.  So this entry preserves EVERYTHING the
 * handler might hold: r0, r3-r31, CR, CTR, XER, LR.  (First run, a plain C function clobbered r4, the handler walked the rest of
 * the list from a wrong address and the game executed garbage about 600 fields later.)
 * Frame 0xA0: r0 at 0x0C, r3..r31 at 0x10..0x83, CR 0x88, CTR 0x8C, XER 0x90, LR in the caller's slot 0xA4. */
        .section .entry, "ax"
        .globl cl_entry
cl_entry:
        stwu    1, -0xA0(1)
        stw     0, 0x0C(1)
        mflr    0
        stw     0, 0xA4(1)
        stmw    3, 0x10(1)
        mfcr    0
        stw     0, 0x88(1)
        mfctr   0
        stw     0, 0x8C(1)
        mfxer   0
        stw     0, 0x90(1)
        bl      cl_main
        lwz     0, 0x90(1)
        mtxer   0
        lwz     0, 0x8C(1)
        mtctr   0
        lwz     0, 0x88(1)
        mtcr    0
        lmw     3, 0x10(1)
        lwz     0, 0xA4(1)
        mtlr    0
        lwz     0, 0x0C(1)
        addi    1, 1, 0xA0
        blr
