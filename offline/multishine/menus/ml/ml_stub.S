/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Menu layout runtime stubs (ml_glue.c).  Four 04 hooks, none changes a register the hooked code relies on (offline/ shape: the
   return address is stored in 4(SP0) before a frame is pushed, and while LR holds anything else r1 = SP0 - 16 owns a frame).
   No FPU arithmetic mnemonics (the build's fpu_arith check): doubles move through lfd / stfd, a float argument register holds the same value.

   ml_enter_stub    04 b at 0x8022DDA8 mnMain_Scene_OnEnter (original word `mflr r0`), r3 = MenuEnterData*      -> ml_enter(r3)
   ml_frame_stub    04 b at 0x8022DD38 mnMain_Scene_OnFrame (original word `mflr r0`)                          -> ml_frame
   ml_label_stub    04 bl at 0x8022B7E0, the `bl HSD_JObjReqAnim` of the row label inside mn_8022B3A0 (r3 = label part, f1 = frame,
                    r17 = row index, MenuFlow.cur_menu at 0x804A04F0): an empty plate (frame 86) for rows whose label we draw
   ml_label_hover_stub / ml_label_unhover_stub   the same at 0x8022A014 (mn_80229F60, row in r27) and 0x8022A4D4 (mn_8022A440, row in r28):
                    the hover and unhover animations request the label frame again
   ml_light_stub    04 b at 0x8022C010 mn_8022C010 (original word `cmpwi r3,0`): light colour = row for the Miscellaneous screen too
   ml_unlock_stub   04 b at 0x80229938 mn_80229938 (original word `mflr r0`): rows our tree hides in retail screens, our own screens' rows
   ml_build_stub    04 b at 0x8022B3A0 mn_8022B3A0 (original word `mflr r0`), r3 = state -> ml_pre_build(state): our plate art goes into the label images, and
                    state 3 with cur_menu 0 and prev_menu 1..5 (the Back path of a retail submenu) goes to the Miscellaneous screen (kind 11) */
        .text
        .globl ml_enter_stub
ml_enter_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        stw     3, 8(1)
        bl      ml_enter
        lwz     3, 8(1)
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, 0x8022
        ori     12, 12, 0xDDAC
        mtctr   12
        bctr

        .globl ml_frame_stub
ml_frame_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
#ifdef UPD_BOOT
        bl      upd_boot_menu           /* OTA automatic rollback: the Main Menu is reached, this build is good (upd_host.c) */
#endif
        bl      ml_frame
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, 0x8022
        ori     12, 12, 0xDD3C
        mtctr   12
        bctr

/* the three label requests of mn_8022B3A0 (build, r17 = row), mn_80229F60 (hover, r27 = row) and mn_8022A440 (unhover, r28 = row) each call
   `bl 0x8036F934` HSD_JObjReqAnim(r3 = jobj, f1 = frame); the stub replaces that call (the caller's LR is the return address), asks
   ml_label_override(MenuFlow.cur_menu, row) and passes the empty plate (frame 86) on when the row's label is ours */
        .macro LABEL_STUB name, rowreg
        .globl \name
\name:
        mflr    0
        stw     0, 4(1)
        stwu    1, -32(1)
        stw     3, 8(1)
        stfd    1, 16(1)
        lis     4, 0x804A
        lbz     3, 0x04F0(4)
        mr      4, \rowreg
        bl      ml_label_override
        mr      5, 3
        lwz     3, 8(1)
        lfd     1, 16(1)
        cmpwi   5, 0
        beq     1f
        slwi    5, 5, 3
        lis     4, ml_ftab@ha
        addi    4, 4, ml_ftab@l
        lfdx    1, 4, 5
1:      lwz     0, 36(1)
        addi    1, 1, 32
        mtlr    0
        lis     12, 0x8036
        ori     12, 12, 0xF934
        mtctr   12
        bctr
        .endm

        LABEL_STUB ml_label_stub, 17
        LABEL_STUB ml_label_hover_stub, 27
        LABEL_STUB ml_label_unhover_stub, 28

        .globl ml_build_stub
ml_build_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        stw     3, 8(1)
        bl      ml_pre_build
        lwz     3, 8(1)
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, 0x8022
        ori     12, 12, 0xB3A4
        mtctr   12
        bctr

/* mn_8022C010(menu_kind r3, selection r4) -> light colour index: the retail Main Menu gives one colour per row; the Miscellaneous
   screen (kind 11, ML_KIND_MISC) is the retail Main Menu, so it does the same.  04 b at 0x8022C010 (original word `cmpwi r3,0`). */
        .globl ml_light_stub
ml_light_stub:
        cmpwi   3, 11
        beq     1f
        cmpwi   3, 0
        lis     12, 0x8022
        ori     12, 12, 0xC014
        mtctr   12
        bctr
1:      cmpwi   4, 5
        blt     2f
        subi    4, 4, 5
2:      mr      3, 4
        blr

/* mn_80229938(menu_kind r3, selection r4) -> row exists: 04 b at 0x80229938 (original word `mflr r0`). ml_unlock_override says
   0 = no opinion (the retail code and Slippi's two patches inside it decide), 1 = hidden, 2 = shown. */
        .globl ml_unlock_stub
ml_unlock_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        stw     3, 8(1)
        stw     4, 12(1)
        bl      ml_unlock_override
        cmpwi   3, 1
        beq     2f
        cmpwi   3, 2
        beq     3f
        lwz     3, 8(1)
        lwz     4, 12(1)
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, 0x8022
        ori     12, 12, 0x993C
        mtctr   12
        bctr
2:      li      3, 0
        b       4f
3:      li      3, 1
4:      lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        blr

/* HSD_SisLib_803A6368(text r3, SIS id r4): 04 b at 0x803A6368 (original word `mflr r0`) -> ml_text_hook(text, id), then the function goes on. */
        .globl ml_text_stub
ml_text_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        stw     3, 8(1)
        stw     4, 12(1)
        bl      ml_text_hook
        lwz     3, 8(1)
        lwz     4, 12(1)
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, 0x803A
        ori     12, 12, 0x636C
        mtctr   12
        bctr

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

        .section .rodata
        .align  3
/* frames the label stub can request, as doubles (index 0 unused): the placeholder frames we fill with our
   art and Slippi's own label frames; the same list as tools/menu_layout.py ART_FRAMES (host test) */
ml_ftab:
        .double 0.0, 220.0, 222.0, 224.0, 226.0, 180.0, 182.0, 184.0, 64.0, 86.0, 150.0, 152.0, 154.0, 140.0, 142.0, 144.0, 146.0, 148.0
