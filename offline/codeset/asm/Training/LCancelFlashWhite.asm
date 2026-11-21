# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x8008D6EC
################################################################################
# Multishine training (own work; references: decomp ft/kinds/ftCommon/ftCo_LandingAir.c,
# ft/ftcolanim.c, lb/lb_013B.c, lb/types.h ColorOverlay, ft/ftdevice.c). Opt-in flag flash-white-lcancel.
#
# White flash on a SUCCESSFUL L-cancel (the companion of Slippi's optional red flash on a missed one).
#
# Hook: 0x8008D6EC in ftCo_LandingAir_EnterWithLag, retail word 4800001D
# (bl ftCo_LandingAir_EnterWithMsidLag). Reached only when an aerial landing picked a LandingAir motion (msid != -1).
# Registers here: r3 = gobj, r4 = msid, f1 = lag, r5 = fp->x67F (frames since the last L/R/Z press, loaded at
# 0x8008D690), r6 = p_ftCommonData (loaded at 0x8008D68C). Success = x67F < p_ftCommonData->xE4 (line 43), the same
# test the game uses to halve the lag.
#
# We make the original call ourselves (absolute, the bl is PC-relative), so the flash is written AFTER
# Fighter_ChangeMotionState has reset the overlay (ftCo_800C0134 -> lb_80014498(&fp->x488), :155).
# Melee's own overlay fp->x488 (ColorOverlay, lb/types.h:357) is then set to white with no colour script:
#   x4_pri = 10 (lb_80014258 counts it down and resets the overlay at 0, :252),
#   colour (255,255,255), alpha 240 with alpha blend -24 per frame (fade out), x7C_color_enable = 1.
# The next motion change (end of landing lag) resets it as usual, so the flash lasts min(10, landing lag) frames.
# Skipped when the overlay is busy (x8 script or x28 colanim set, or x408 active: ftCo_800C0658 shows x408 then).
#
# Writes: only fp+0x488..0x507 (the colour overlay). Nothing reads it except the colour update and the material
# draw (ft/ftmaterial.c), so no simulation field changes. It is inside the Fighter cell (rollback snapshot) and the
# project's gameplay/full test hashes, NOT in the core hash or the Slippi checksum. Kept off in the Slippi online
# in-game scene (major 8) anyway: only. G&W's nair/bair/uair enter LandingAir through their own path
# and cannot be L-cancelled; they never reach this hook.
# Only volatile registers r0, r3-r6, r12 and a 0x20-byte stack frame are used.

stwu r1, -0x20(r1)
stw r3, 0x10(r1)              # gobj
lwz r0, 0xE4(r6)              # L-cancel window (p_ftCommonData->xE4)
cmpw r5, r0
li r0, 0
bge MsLcwFail
li r0, 1
MsLcwFail:
stw r0, 0x14(r1)              # success flag

# original instruction: bl ftCo_LandingAir_EnterWithMsidLag(gobj, msid, lag)
lis r12, 0x8008
ori r12, r12, 0xD708
mtctr r12
bctrl

lwz r0, 0x14(r1)
cmpwi r0, 0
beq MsLcwExit
lis r12, 0x8048
lbz r12, -0x62D0(r12)         # scene major (0x80479D30)
cmpwi r12, 0x08               # Slippi online in-game major: never
beq MsLcwExit
lwz r3, 0x10(r1)
lwz r3, 0x2C(r3)              # fp
lwz r0, 0x408+0x28(r3)        # x408 colanim active -> x488 is not shown
cmpwi r0, 0
bne MsLcwExit
addi r3, r3, 0x488            # ColorOverlay x488
lwz r0, 0x08(r3)              # colour script running
cmpwi r0, 0
bne MsLcwExit
lwz r0, 0x28(r3)              # colanim id
cmpwi r0, 0
bne MsLcwExit

li r0, 10
stw r0, 0x04(r3)              # x4_pri: frames until reset
li r0, -16                    # 0xFFFFFFF0
stw r0, 0x2C(r3)              # x2C_hex = (255,255,255,240)
lis r0, 0x437F                # 255.0
stw r0, 0x30(r3)
stw r0, 0x34(r3)
stw r0, 0x38(r3)
lis r0, 0x4370                # 240.0
stw r0, 0x3C(r3)
li r0, 0
stw r0, 0x40(r3)
stw r0, 0x44(r3)
stw r0, 0x48(r3)
lis r0, 0xC1C0                # -24.0 alpha per frame
stw r0, 0x4C(r3)
lbz r0, 0x7C(r3)
ori r0, r0, 0x80              # x7C_color_enable
andi. r0, r0, 0xBF            # x7C_flag2 (light) off
stb r0, 0x7C(r3)

MsLcwExit:
addi r1, r1, 0x20
