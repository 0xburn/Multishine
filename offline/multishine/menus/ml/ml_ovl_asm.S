/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The two soft-float shims the menu overlay calls: they are position independent (absolute retail addresses only, no label of this image), so they link into the overlay like C.
   ml_sis_add is the same code as ml_stub.S (the resident copy serves the flat build); ml_initlod is ml_sign_stub.S. */
        .text
/* int ml_sis_add(HSD_Text* t, const double xy[2], const char* fmt, const char* s) -> HSD_SisLib_803A6B98(t, x, y, fmt, s)
   (f1/f2 = x/y, r4 = fmt, r5 = s, cr6 clear: no FP varargs).  xy are doubles (lfd, not lfs). */
        .globl ml_sis_add
ml_sis_add:
        lfd     1, 0(4)
        lfd     2, 8(4)
        mr      4, 5
        mr      5, 6
        crxor   6, 6, 6
        lis     12, 0x803A
        ori     12, 12, 0x6B98
        mtctr   12
        bctr

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

/* void* memcpy(void* dst, const void* src, size_t n): the struct copy in sg_hook_open (`h->env = *env`) compiles to a call.  Byte loop, no alignment assumption; the overlay
   may not import the resident image's copy (no import relocations), and this one is only used on a menu screen, never in a match. */
        .globl memcpy
memcpy:
        mr      12, 3
        cmpwi   5, 0
        beqlr
        mtctr   5
        subi    4, 4, 1
        subi    12, 12, 1
1:      lbzu    0, 1(4)
        stbu    0, 1(12)
        bdnz    1b
        blr

/* void ml_janim(HSD_JObj *j, unsigned frame): HSD_JObjReqAnimAll(j, (float)frame); HSD_JObjAnimAll(j) -- the Settings globe's step (ml_glue.c pgl_tick; the same code as sign/gnb_sis.S
   gnbev_anim: the integer becomes a float with the 2^52 magic double, the overlay is soft-float) */
        .globl ml_janim
ml_janim:
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
