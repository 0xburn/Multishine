/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Soft-float shim of the sign-in overlay's QR sprite (the same code as sgt_initlod): Melee's float arguments through lfs. */
        .text
/* void ml_initlod(GXTexObj* obj) -> GXInitTexObjLOD(obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1) */
        .globl ml_initlod
ml_initlod:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        li      0, 0
        stw     0, 8(1)
        lfs     1, 8(1)
        lfs     2, 8(1)
        lfs     3, 8(1)
        li      4, 0
        li      5, 0
        li      6, 0
        li      7, 0
        li      8, 0
        lis     12, 0x8033
        ori     12, 12, 0xEC6C
        mtctr   12
        bctrl
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        blr
