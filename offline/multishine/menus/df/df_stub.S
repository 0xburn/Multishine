/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Hook stubs of the disc-file pack (df.h, df_glue.c).  Three 04 `b` hooks on the function entries of Melee's file funnel; each displaced word is the retail `mflr r0`.
   Shape as menus/ml/ml_stub.S (offline/: the return address is stored in 4(SP0) before a frame is pushed).  No FPU.

   df_path_stub  at 0x8033796C DVDConvertPathToEntrynum(r3 = path)   -> r3 = df_path(path); DF_PASS (-2) = run the retail function
   df_open_stub at 0x80337C60 DVDFastOpen(r3 = entry, r4 = DVDFileInfo*) -> r3 = df_open(entry, info); 1 = opened (the size read by every inlined copy of lbFile_8001634C comes from here), 0 = retail; first word `cmpwi r3,0`
   df_req_stub   at 0x8038F6D4 HSD_DevComRequest(r3..r10)             -> df_req(&args) may rewrite the eight argument words; the retail function always runs afterwards */
        .text
        .macro PASS_ON hi, lo
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        mflr    0
        lis     12, \hi
        ori     12, 12, \lo
        mtctr   12
        bctr
        .endm

        .globl df_path_stub
df_path_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        stw     3, 8(1)
        bl      df_path
        cmpwi   3, -2
        beq     1f
        lwz     0, 20(1)                    # ours: return the entry to the caller
        addi    1, 1, 16
        mtlr    0
        blr
1:      lwz     3, 8(1)
        PASS_ON 0x8033, 0x7970

        .globl df_open_stub
df_open_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -16(1)
        stw     3, 8(1)
        stw     4, 12(1)
        bl      df_open
        cmpwi   3, 0
        bne     1f                          # ours: r3 = 1 (opened), the caller's DVDFileInfo is filled
        lwz     3, 8(1)
        lwz     4, 12(1)
        lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        cmpwi   3, 0                        # the displaced first word of DVDFastOpen
        lis     12, 0x8033
        ori     12, 12, 0x7C64
        mtctr   12
        bctr
1:      lwz     0, 20(1)
        addi    1, 1, 16
        mtlr    0
        blr

        .globl df_req_stub
df_req_stub:
        mflr    0
        stw     0, 4(1)
        stwu    1, -64(1)                   # 8 argument words at 16(1)..47(1)
        stw     3, 16(1)
        stw     4, 20(1)
        stw     5, 24(1)
        stw     6, 28(1)
        stw     7, 32(1)
        stw     8, 36(1)
        stw     9, 40(1)
        stw     10, 44(1)
        addi    3, 1, 16
        bl      df_req
        lwz     3, 16(1)
        lwz     4, 20(1)
        lwz     5, 24(1)
        lwz     6, 28(1)
        lwz     7, 32(1)
        lwz     8, 36(1)
        lwz     9, 40(1)
        lwz     10, 44(1)
        lwz     0, 68(1)
        addi    1, 1, 64
        mtlr    0
        mflr    0
                                            # the displaced word is `mflr r0` (run above); entry + 4 is the retail `lis r11,-32703`
        lis     12, 0x8038
        ori     12, 12, 0xF6D8
        mtctr   12
        bctr
