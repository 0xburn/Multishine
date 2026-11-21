/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S12: copy of plus (S12) the idle-gap hook, the render-skip hook and the camera-task
   skip hooks (s12_gc.c).
   S10: unchanged copy of.
   S7: unchanged copy of. */
/* S6D: copy of plus the TEST ONLY SFX-log hooks (S6D).
   : pad-alarm pre-hook (fn_800195FC, VI interrupt) for the hang watchdog s6n_watchdog. */
        .text
        .macro  PREHOOK name, cfn, insn, ret
        .globl  \name
\name:
        stwu    1,-0x40(1)
        stw     3,0x10(1)
        stw     4,0x14(1)
        stw     5,0x18(1)
        stw     6,0x1C(1)
        stw     7,0x20(1)
        stw     8,0x24(1)
        stw     9,0x28(1)
        stw     10,0x2C(1)
        mflr    0
        stw     0,0x30(1)
        .ifnc   \cfn, s6d_sfx_stop
        bl      \cfn
        .else
        mr      4,0                             /* S6D: second argument = the caller's return address */
        bl      \cfn
        .endif
        lwz     0,0x30(1)
        mtlr    0
        lwz     3,0x10(1)
        lwz     4,0x14(1)
        lwz     5,0x18(1)
        lwz     6,0x1C(1)
        lwz     7,0x20(1)
        lwz     8,0x24(1)
        lwz     9,0x28(1)
        lwz     10,0x2C(1)
        addi    1,1,0x40
        .long   \insn
        lis     12,(\ret)@h
        ori     12,12,(\ret)@l
        mtctr   12
        bctr
        .endm

        PREHOOK sd_padalarm_stub, s6n_watchdog, 0x7C0802A6, 0x80019600

/* S6D TEST ONLY SFX log:
     0x8038CFF4  AXDriver_8038CFF4 entry (`mflr r0`): s6d_sfx_req(id, vol, pan, track, channel) with the original args.
     0x800236B8  lbAudioAx_800236B8 = SFX_StopSFXInstance entry (`mflr r0`): s6d_sfx_stop(instance, caller LR).
     0x8038D29C  AXDriver_8038CFF4 real-start tail `lwz r3,12(r29)` (reached only when a voice was started; every
                 failure and Slippi's dedupe skip branch to 0x8038D2A0): s6d_sfx_start(instance, id = r23, track = r26),
                 then continue at 0x8038D2A0 with r3 = instance.  r22..r31 are reloaded by the epilogue (lmw). */
        PREHOOK sfx_req_stub, s6d_sfx_req, 0x7C0802A6, 0x8038CFF8
        PREHOOK sfx_stop_stub, s6d_sfx_stop, 0x7C0802A6, 0x800236BC

        .globl  sfx_start_stub
sfx_start_stub:
        lwz     3,12(29)
        stwu    1,-0x20(1)
        stw     3,0x10(1)
        mflr    0
        stw     0,0x14(1)
        mr      4,23
        mr      5,26
        bl      s6d_sfx_start
        lwz     0,0x14(1)
        mtlr    0
        lwz     3,0x10(1)
        addi    1,1,0x20
        lis     12,0x8038
        ori     12,12,0xD2A0
        mtctr   12
        bctr

/* ---- S12 ----
     0x801A4DA8  04 `bl s12_idle_stub` (scene-loop wait loop of gm_801A4D34, replaces `bl lb_800195D0`, main thread,
                 interrupts on): s12_idle, then the displaced call with LR = 0x801A4DAC.
     0x801A5030  04 `bl s12_render_stub` (first instruction of the render part, replaces `bl lb_800195D0`): when
                 s12_render_skip returns non-zero, continue at 0x801A507C (loop end check) without rendering; else
                 the displaced call with LR = 0x801A5034.
     0x8002A4AC  Camera_8002A4AC entry (`mflr r0`) and 0x80086A8C ftLib_80086A8C entry (`mflr r0`): when
                 s12_cam_skip(index, caller LR) returns non-zero, return to the caller at once; else run the function.
   Every stub makes its own frame before calling C (offline/ rule 3). */
        .globl  s12_idle_stub
s12_idle_stub:
        stwu    1,-16(1)
        mflr    0
        stw     0,20(1)
        bl      s12_idle
        lwz     0,20(1)
        mtlr    0
        addi    1,1,16
        lis     12,0x8001
        ori     12,12,0x95D0
        mtctr   12
        bctr

        .globl  s12_render_stub
s12_render_stub:
        stwu    1,-16(1)
        mflr    0
        stw     0,20(1)
        bl      s12_render_skip
        lwz     0,20(1)
        mtlr    0
        addi    1,1,16
        cmpwi   3,0
        bne-    1f
        lis     12,0x8001
        ori     12,12,0x95D0
        mtctr   12
        bctr
1:      lis     12,0x801A
        ori     12,12,0x507C
        mtctr   12
        bctr

        .macro  SKIPHOOK name, idx, insn, ret
        .globl  \name
\name:
        stwu    1,-0x40(1)
        stw     3,0x10(1)
        stw     4,0x14(1)
        stw     5,0x18(1)
        stw     6,0x1C(1)
        stw     7,0x20(1)
        stw     8,0x24(1)
        stw     9,0x28(1)
        stw     10,0x2C(1)
        mflr    0
        stw     0,0x30(1)
        li      3,\idx
        mr      4,0
        bl      s12_cam_skip
        mr      11,3
        lwz     0,0x30(1)
        mtlr    0
        lwz     3,0x10(1)
        lwz     4,0x14(1)
        lwz     5,0x18(1)
        lwz     6,0x1C(1)
        lwz     7,0x20(1)
        lwz     8,0x24(1)
        lwz     9,0x28(1)
        lwz     10,0x2C(1)
        addi    1,1,0x40
        cmpwi   11,0
        bnelr-
        .long   \insn
        lis     12,(\ret)@h
        ori     12,12,(\ret)@l
        mtctr   12
        bctr
        .endm

        .globl  s12_fnhook_stubs
s12_fnhook_stubs:
        SKIPHOOK s12_cam0_stub, 0, 0x7C0802A6, 0x8002A4B0
        SKIPHOOK s12_cam1_stub, 1, 0x7C0802A6, 0x80086A90

/* S12 (on S11): DYNB.  0x8001044C lb_8001044C entry (`mflr r0`, 04 `b s12_dyn_stub`): s12_dyn_skip(desc = r3, part = r7)
   non-zero -> return to the caller at once; else the displaced `mflr r0` and on at 0x80010450.  Own frame before the C
   call (offline/ rule 3); r3..r10 are kept for the original; f1 (pos_y) is not touched by the soft-float C
   on the path that runs the original. */
        .globl  s12_dyn_stub
s12_dyn_stub:
        stwu    1,-0x40(1)
        stw     3,0x10(1)
        stw     4,0x14(1)
        stw     5,0x18(1)
        stw     6,0x1C(1)
        stw     7,0x20(1)
        stw     8,0x24(1)
        stw     9,0x28(1)
        stw     10,0x2C(1)
        mflr    0
        stw     0,0x30(1)
        mr      4,7
        bl      s12_dyn_skip
        mr      11,3
        lwz     0,0x30(1)
        mtlr    0
        lwz     3,0x10(1)
        lwz     4,0x14(1)
        lwz     5,0x18(1)
        lwz     6,0x1C(1)
        lwz     7,0x20(1)
        lwz     8,0x24(1)
        lwz     9,0x28(1)
        lwz     10,0x2C(1)
        addi    1,1,0x40
        cmpwi   11,0
        bnelr-
        mflr    0
        lis     12,0x8001
        ori     12,12,0x0450
        mtctr   12
        bctr

/* ---- S17 RELAX ----
   s17_relax_stub: `bl s17_relax_stub` replaces the `cmpw r3, r4` of the main/C-stick word compare of TriggerSendInput.asm (section 9, `lwz r3, 0x2(r6)
   lwz r4, 0x2(r7) cmpw r3, r4 bne INDICATE_ROLLBACK_REQUIRED`); the build patches it after patching (build_s17_net.py --relax-stub, found by the words
   around it).  In: r3 real stick word, r4 predicted stick word, r6 / r7 real / predicted pad, r20 REG_COUNT, r22 REG_RXB_OFFSET, r24 REG_RXB_ADDRESS,
   r27 REG_ODB_ADDRESS.  Out: cr0 EQ = the game cannot tell the two pads apart (the caller then takes the INPUTS_MATCH path), NE = strict.  Preserved:
   every register except r0 (the original `cmpw` changed only cr0) and the other cr fields; LR is saved in the stub's own frame (offline/
   rule 3).  Equal words return at once, exactly as the original instruction; the C side answers 0 when RELAX is off. */
        .globl  s17_relax_stub
s17_relax_stub:
        cmpw    3,4
        beqlr
        stwu    1,-0x80(1)
        mflr    0
        stw     0,0x84(1)
        mfcr    0
        stw     0,0x10(1)
        stw     3,0x14(1)
        stw     4,0x18(1)
        stw     5,0x1C(1)
        stw     6,0x20(1)
        stw     7,0x24(1)
        stw     8,0x28(1)
        stw     9,0x2C(1)
        stw     10,0x30(1)
        stw     11,0x34(1)
        stw     12,0x38(1)
        mr      10,6
        mr      11,7
        mr      5,27
        mr      6,24
        mr      7,20
        mr      8,22
        mr      3,10
        mr      4,11
        bl      s17_relax_c
        stw     3,0x3C(1)
        lwz     0,0x84(1)
        mtlr    0
        lwz     3,0x14(1)
        lwz     4,0x18(1)
        lwz     5,0x1C(1)
        lwz     6,0x20(1)
        lwz     7,0x24(1)
        lwz     8,0x28(1)
        lwz     9,0x2C(1)
        lwz     10,0x30(1)
        lwz     11,0x34(1)
        lwz     12,0x38(1)
        lwz     0,0x10(1)
        mtcrf   0x7F,0
        lwz     0,0x3C(1)
        addi    1,1,0x80
        cmpwi   0,1
        blr

#ifdef MS_HIL_DEV
/* ---- HIL dev link (lab images only: assembled with -DMS_HIL_DEV by profile s17-compat-hil; hooked by build_s17_net.py when MS_HIL_DEV=1) ----
     0x80376A24  `bl PADRead` in HSD_PadRenewRawStatus (retail word 4BFD6FDD; r3 = PADStatus[4] on the caller's stack) is replaced by `bl hil_padread_stub`:
     the real PADRead runs first, then hil_pad_inject(r3) overwrites the ports a script owns.  PADRead's return value is preserved.  Own frame, LR saved
     before the first call (offline/ rule 3).  Slippi's own hook at 0x80376A28 (right after) then sees the injected pad, so an injected pad
     is also what an online match would send. */
        .globl  hil_padread_stub
hil_padread_stub:
        stwu    1,-16(1)
        mflr    0
        stw     0,20(1)
        stw     3,8(1)
        lis     12,0x8034
        ori     12,12,0xDA00                    /* PADRead */
        mtctr   12
        bctrl
        stw     3,12(1)
        lwz     3,8(1)
        bl      hil_pad_inject
        lwz     3,12(1)
        lwz     0,20(1)
        mtlr    0
        addi    1,1,16
        blr
#endif
