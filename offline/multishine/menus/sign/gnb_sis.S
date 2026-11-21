/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Soft-float shim of the Game Night browser (gnb_ev.c, console only): the same trick as offline/multishine/menus/ml/ml_stub.S ml_sis_add.
   int gnbev_sis(HSD_Text *t, const char *fmt, const char *s) -> HSD_SisLib_803A6B98(t, 0.0f, 0.0f, fmt, s): r3 t, r4 fmt, r5 s are already in place; f1 = f2 = 0.0
   (loaded from a zero word on the stack: no relocation), cr6 clear (no floating point varargs), tail call. */
        .text
        .globl gnbev_sis
gnbev_sis:
        stwu    1, -16(1)
        li      0, 0
        stw     0, 8(1)
        stw     0, 12(1)
        lfd     1, 8(1)
        fmr     2, 1
        addi    1, 1, 16
        crxor   6, 6, 6
        lis     12, 0x803A
        ori     12, 12, 0x6B98
        mtctr   12
        bctr

/* void gnbev_madd(float *out, const float *a, const float *b, const float *c): *out = *a + *b * *c (the browser's layout arithmetic: the overlay is soft-float C and
   devkitPPC has no soft-float libgcc, so the few float sums are done here with the FPU; no other FPR is touched, f1-f3 are volatile in the EABI) */
        .globl gnbev_madd
gnbev_madd:
        lfs     1, 0(4)
        lfs     2, 0(5)
        lfs     3, 0(6)
        fmadds  1, 2, 3, 1
        stfs    1, 0(3)
        blr

/* void gnbev_anim(HSD_JObj *j, unsigned frame): HSD_JObjReqAnimAll(j, (float)frame); HSD_JObjAnimAll(j) (Tournament Melee's own background step, gm fn_8019044C; the overlay is soft-float, so the
   integer is turned into a float here with the usual 2^52 magic double) */
        .globl gnbev_anim
gnbev_anim:
        stwu    1, -32(1)
        mflr    0
        stw     0, 36(1)
        stw     31, 28(1)
        mr      31, 3
        lis     0, 0x4330
        stw     0, 8(1)
        xoris   4, 4, 0x8000
        stw     4, 12(1)
        stw     0, 16(1)
        lis     0, 0x8000
        stw     0, 20(1)
        lfd     1, 8(1)
        lfd     2, 16(1)
        fsub    1, 1, 2
        frsp    1, 1
        lis     12, 0x8036
        ori     12, 12, 0xF8BC
        mtctr   12
        bctrl
        mr      3, 31
        lis     12, 0x8037
        ori     12, 12, 0x0928
        mtctr   12
        bctrl
        lwz     0, 36(1)
        lwz     31, 28(1)
        addi    1, 1, 32
        mtlr    0
        blr
