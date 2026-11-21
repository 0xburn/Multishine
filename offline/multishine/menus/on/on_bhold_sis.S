/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_bhold_sis: the two Melee text calls of on_bhold.c that the soft-float C side cannot make itself.  Original work.
   int bh_sis_add(HSD_Text* t, const u32 xy_bits[2], const char* fmt, const char* s)
       -> HSD_SisLib_803A6B98(t, x, y, fmt, s): add a subtext; f1/f2 = x/y (hard-float ABI), r4 = fmt, r5 = s, cr6 clear (no FP
          varargs). The same stub as (chat owns its files, so it is written again here).
   int bh_sis_set(HSD_Text* t, int idx, const char* fmt, int a, int b)
       -> HSD_SisLib_803A70A0(t, idx, fmt, a, b): replace a subtext's contents (variadic: cr6 clear, integer arguments only).
   LR safety (offline/ rule 3): tail calls, LR still holds our caller's return address, no frame needed; the lfs reads
   are word aligned (xy_bits is a u32 array).  These two lfs are the only FPU instructions of the unit (the profile's fpu_arith
   list names every other unit). */
        .text
        .globl  bh_sis_add
bh_sis_add:
        lfs     1,0(4)
        lfs     2,4(4)
        mr      4,5
        mr      5,6
        crxor   6,6,6
        lis     12,0x803A
        ori     12,12,0x6B98
        mtctr   12
        bctr

        .globl  bh_sis_size
bh_sis_size:                            # int bh_sis_size(HSD_Text *t, int idx, const u32 *sxy_bits): HSD_SisLib_803A7548(t, idx, sx, sy) (tmv.c: name text scale)
        lfs     1,0(5)
        lfs     2,4(5)
        crxor   6,6,6
        lis     12,0x803A
        ori     12,12,0x7548
        mtctr   12
        bctr

        .globl  tm_a514
tm_a514:                                # void tm_a514(int count): fn_8018A514(count, 4.5f), the retail bracket table copy (tmv.c: back to the winners page)
        lis     11,tm_c45@ha
        lfs     1,tm_c45@l(11)
        lis     12,0x8018
        ori     12,12,0xA514
        mtctr   12
        bctr
        .section .rodata
        .align  2
tm_c45: .long   0x40900000
        .text

        .globl  bh_sis_set
bh_sis_set:
        crxor   6,6,6
        lis     12,0x803A
        ori     12,12,0x70A0
        mtctr   12
        bctr

/* S19: the in-match "rollback off" notice (on_bhold.c bh_note_tick).  The CSS canvas of bh_sis_add does not exist in a match,
   so the notice makes its own HUD camera, canvas and one-line text, the technique of the rollback self-test's overlay (/
   , original work of this project; call sequence / ): HUD CObj from the HUD archive
   (0x804D6D5C, null outside a match), GObj_Create(19, 20, 0), a camera callback that always draws, canvas HSD_SisLib_803A611C, text
   HSD_SisLib_803A6754, line init HSD_SisLib_803A6B98 (x, y, size in f1-f3 from bh_note_pool), subtext size HSD_SisLib_803A7548.
       void *bh_note_create(const char *line)   -> HSD_Text* or 0
   LR safety (offline/): framed (stwu first, the return address in the own frame's N+4 slot before any bl), LR as data only
   inside the frame.  FPU: lfs / stfs of bh_note_pool constants (this unit is outside the profile's fpu_arith list). */
        .macro NCALL addr
        lis     12, ((\addr) >> 16) & 0xFFFF
        ori     12, 12, (\addr) & 0xFFFF
        mtctr   12
        bctrl
        .endm

        .globl  bh_note_create
        .globl  bh_ping_create
bh_note_create:                         # the notice: pool 0, left aligned, full alpha
        li      4, 0
        li      5, 2                    # r5 = font, r6 = 1: the menu camera instead of the HUD camera
        li      6, 0
        b       bh_text_create
        .globl  bh_sss_create
bh_sss_create:                          # void *bh_sss_create(const char *line, u32 font): the notice's text on its own camera made from the MENU camera (MenMain_cam), for the stage select
        mr      5, 4
        li      4, 0
        li      6, 1
        b       bh_text_create
bh_ping_create:                         # the in-match ping line (bh_ping_tick): the DELAY TEXT's own parameters, Slippi's InitInGame C2 (pool 1: x 270, y 207, size 0.33, canvas 0.1, z 0), right aligned, alpha 0x80
        li      4, 1
        li      5, 2
        li      6, 0
bh_text_create:
        stwu    1, -0x40(1)
        mflr    0
        stw     0, 0x44(1)
        stmw    24, 0x20(1)
        mr      26, 3                   # r26 = the line
        mr      29, 4                   # r29 = 0 notice, 1 ping line
        mr      30, 5                   # r30 = font
        mr      31, 6                   # r31 = 1: camera from the menu camera descriptor
        cmpwi   31, 0
        beq     nc_hud
        lis     3, 0x804D
        lwz     3, 0x6BC4(3)            # MenMain_cam (set by the running menu scene)
        b       nc_cam
nc_hud:
        lis     3, 0x804D
        lwz     3, 0x6D5C(3)            # HUD archive (null in scenes without the HUD)
        cmpwi   3, 0
        beq     nc_fail
        lis     4, 0x803F
        ori     4, 4, 0x94D0            # HUD camera symbol name
        NCALL   0x80380358              # HSD_ArchiveGetPublicAddress
        cmpwi   3, 0
        beq     nc_fail
        lwz     3, 4(3)
        lwz     3, 0(3)
nc_cam:
        cmpwi   3, 0
        beq     nc_fail
        NCALL   0x8036A590              # HSD_CObjLoadDesc
        cmpwi   3, 0
        beq     nc_fail
        mr      25, 3
        li      3, 19
        li      4, 20
        li      5, 0
        NCALL   0x803901F0              # GObj_Create(19, 20, 0)
        cmpwi   3, 0
        beq     nc_fail
        mr      24, 3
        lbz     4, -0x3E55(13)          # CObj object kind
        mr      5, 25
        NCALL   0x80390A70              # attach the CObj
        bl      1f
1:      mflr    4
        addi    4, 4, bh_note_cam - 1b
        mr      3, 24
        li      5, 8                    # cobj gx priority
        NCALL   0x8039075C              # camera draw callback
        li      3, 0x1000               # gx link mask 1 << 12
        stw     3, 0x24(24)
        mr      3, 30
        mr      4, 24
        li      5, 9
        li      6, 13
        li      7, 0
        li      8, 12                   # text gx link
        li      9, 80                   # text gx priority
        li      10, 8                   # cobj gx priority
        NCALL   0x803A611C              # canvas
        mr      4, 3
        mr      3, 30
        NCALL   0x803A6754              # text object
        mr.     27, 3
        beq     nc_fail
        lis     28, bh_note_pool@ha
        addi    28, 28, bh_note_pool@l
        cmpwi   29, 0
        beq     1f
        addi    28, 28, 24              # the ping line's pool
1:      li      0, 1
        stb     0, 0x49(27)             # tight kerning
        li      0, 0
        beq     2f
        li      0, 2                    # ping: right aligned like the delay text
2:      stb     0, 0x4A(27)             # alignment
        lfs     1, 20(28)
        stfs    1, 0x8(27)              # z
        lfs     1, 16(28)
        stfs    1, 0x24(27)             # canvas scale x / y
        stfs    1, 0x28(27)
        cmpwi   29, 0
        li      0, 0xFF
        beq     3f
        li      0, 0x80                 # ping: active_color alpha 0x80 like the delay text
3:      stb     0, 0x8F(27)             # alpha
        lfs     1, 0(28)
        lfs     2, 4(28)
        mr      3, 27
        mr      4, 26                   # the line (no conversions)
        lfs     3, 8(28)
        crxor   6, 6, 6
        NCALL   0x803A6B98              # line init (subtext 0)
        mr      3, 27
        li      4, 0
        lfs     1, 12(28)
        lfs     2, 12(28)
        NCALL   0x803A7548              # subtext size
        mr      3, 27
        b       nc_ret
nc_fail:
        li      3, 0
nc_ret:
        lmw     24, 0x20(1)
        lwz     0, 0x44(1)
        mtlr    0
        addi    1, 1, 0x40
        blr

        .globl  bh_note_fit
bh_note_fit:                            # r3 = HSD_Text: box width (+0x0C) = current_width (+0x70, the line's right end in text units, left inset included) + the same inset on the right
        lis     4, (bh_note_pool+48)@ha
        lfs     2, (bh_note_pool+48)@l(4)
        lfs     1, 0x70(3)
        fadds   1, 1, 2
        stfs    1, 0x0C(3)
        blr

bh_note_cam:                            # draw the notice camera every frame (paused or not)
        stwu    1, -0x10(1)
        mflr    0
        stw     0, 0x14(1)
        NCALL   0x803910D8              # HSD_GObj_803910D8 (camera draw of this GObj)
        lwz     0, 0x14(1)
        mtlr    0
        addi    1, 1, 0x10
        blr

        .data                           # writable since 2026-11-03: on_bhold.c ping_place writes the ping line's x / y (pool 1, words 6 and 7) before each match
        .balign 4
        .globl  bh_note_pool
bh_note_pool:
        .float  6.0                     # x of the line, relative to HSD_Text.pos (set by on_bhold.c)
        .float  6.0                     # y of the line
        .float  15.0                    # line spacing (one line)
        .float  0.55                    # text size
        .float  0.1                     # canvas scale
        .float  0.0                     # z
        .float  270.0                   # pool 1 (ping): x of the line (right edge, right aligned), Slippi's DOFST_TEXT_X_POS
        .float  207.0                   # y, DOFST_TEXT_Y_POS
        .float  15.0                    # unused spacing
        .float  0.33                    # DOFST_TEXT_SIZE
        .float  0.1                     # DOFST_TEXT_BASE_CANVAS_SCALING_1
        .float  0.0                     # DOFST_TEXT_BASE_Z
        .float  6.0                     # [12] bh_note_fit: the right inset of the notice's box (= the line x, the left inset)
