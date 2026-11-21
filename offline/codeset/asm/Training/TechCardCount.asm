# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x800693AC
################################################################################
# Multishine training (own work; references: decomp ft/fighter.c Fighter_ChangeMotionState,
# ft/kinds/ftCommon/ftCo_LandingAir.c:14-43, ft/kinds/ftCommon/forward.h (motion ids), ft/types.h, gm/types.h).
# Opt-in flag tech-card. Counting half of the tech card (R3, 1d); TechCardDraw.asm shows it.
#
# Hook: first instruction of Fighter_ChangeMotionState (retail word 7C0802A6, mflr r0). At entry r3 = gobj,
# r4 = new motion id, fp->motion_id (fp+0x10) is still the old one, r5/r6/f1-f3 are arguments (kept).
# Every motion change in a Vs match (scene major 2, minor 2 match or 3 sudden death) is classified READ-ONLY:
#   L-cancel   old AttackAirN..Lw (65..69) -> LandingAirN..Lw (70..74): attempt; success when
#              fp->x67F < p_ftCommonData->xE4, the game's own halving test
#   tech       -> Passive / PassiveStandF / PassiveStandB (199..201): ground tech
#   missed     -> DownBoundU / DownBoundD (183, 191): landed in tumble (or a ground bounce) without teching
#   wavedash   -> KneeBend (24) stamps the frame; EscapeAir (236) -> LandingFallSpecial (43) within 20 frames
#              of that stamp counts (jumpsquat <= 8 frames + airdodge into the ground)
#   shield drop  Guard / GuardOn / GuardReflect (179, 178, 182) -> Pass (244)
# Ice Climbers' Nana (fp+0x221F bit 0x08, is_sub_fighter) and slots above 3 are skipped.
#
# Counter block (72 bytes, inside this payload at body+8; TechCardDraw finds it by decoding the hook branch):
#   +0x00 'TCRD'   +0x04 last match frame seen (0xFFFFFFFF = reset pending)
#   +0x08 + 16 * slot: u16 lc_attempts, lc_ok, techs, missed_techs, wavedashes, shield_drops; u32 kneebend frame
# Reset: on the first motion change of a match (scene minor 2) whose match frame (0x8046B6C4) is below the last one
# seen; TechCardDraw sets the reset-pending value after drawing. Nothing in the game reads this block, it is outside
# every Fighter cell, snapshot and hash. Writes only the block. No call, LR kept (r0 holds it, as the original does).
# Registers: r0 (= LR, the original instruction), r7-r12; cr0/cr1.

mflr r0                       # original instruction (r0 = return address, stored by the next retail instruction)
bl TccAfterBlock
TccBlock:
.long 0x54435244              # 'TCRD'
.long 0xFFFFFFFF              # last match frame (reset pending)
.long 0, 0, 0, 0xFFFF0000     # slot 0
.long 0, 0, 0, 0xFFFF0000     # slot 1
.long 0, 0, 0, 0xFFFF0000     # slot 2
.long 0, 0, 0, 0xFFFF0000     # slot 3
TccAfterBlock:
mflr r12                      # r12 = block
mtlr r0

lis r11, 0x8048
lbz r10, -0x62D0(r11)         # scene major (0x80479D30)
cmpwi r10, 2                  # Vs Melee
bne TccExit
lbz r10, -0x62CD(r11)         # scene minor: 2 match, 3 sudden death
cmpwi r10, 2
beq TccScene
cmpwi r10, 3
bne TccExit
TccScene:
lis r11, 0x8047
lwz r11, -0x493C(r11)         # match frame (0x8046B6C4)
lwz r9, 4(r12)
cmplw r11, r9
stw r11, 4(r12)
bge TccNoReset
cmpwi r10, 2                  # new match only in the match scene (sudden death keeps the counts)
bne TccNoReset
li r9, 0
lis r8, 0xFFFF
addi r7, r12, 8
li r10, 4
TccClear:
stw r9, 0(r7)
stw r9, 4(r7)
stw r9, 8(r7)
stw r8, 0xC(r7)
addi r7, r7, 16
subic. r10, r10, 1
bne TccClear
TccNoReset:
lwz r10, 0x2C(r3)             # fp
lbz r9, 0x221F(r10)
andi. r9, r9, 0x08            # Nana
bne TccExit
lbz r9, 0xC(r10)              # player slot
cmplwi r9, 3
bgt TccExit
slwi r9, r9, 4
add r8, r12, r9
addi r8, r8, 8                # r8 = slot counters
lwz r9, 0x10(r10)             # old motion id

# --- L-cancel
subi r7, r4, 70
cmplwi r7, 4
bgt TccNotLanding
subi r7, r9, 65
cmplwi r7, 4
bgt TccNotLanding
lhz r7, 0(r8)
addi r7, r7, 1
sth r7, 0(r8)
lbz r7, 0x67F(r10)            # frames since the last L/R/Z press
lwz r11, -0x514C(r13)         # p_ftCommonData
lwz r11, 0xE4(r11)            # L-cancel window
cmpw r7, r11
bge TccExit
lhz r7, 2(r8)
addi r7, r7, 1
sth r7, 2(r8)
b TccExit
TccNotLanding:
# --- techs
subi r7, r4, 199
cmplwi r7, 2
bgt TccNotTech
lhz r7, 4(r8)
addi r7, r7, 1
sth r7, 4(r8)
b TccExit
TccNotTech:
cmpwi r4, 183
beq TccMissed
cmpwi r4, 191
bne TccNotMissed
TccMissed:
lhz r7, 6(r8)
addi r7, r7, 1
sth r7, 6(r8)
b TccExit
TccNotMissed:
# --- wavedash
cmpwi r4, 24
bne TccNotKnee
lis r11, 0x8047
lwz r11, -0x493C(r11)
stw r11, 0xC(r8)
b TccExit
TccNotKnee:
cmpwi r4, 43
bne TccNotWd
cmpwi r9, 236
bne TccExit
lis r11, 0x8047
lwz r11, -0x493C(r11)
lwz r7, 0xC(r8)
subf r7, r7, r11
cmplwi r7, 20
bgt TccExit
lhz r7, 8(r8)
addi r7, r7, 1
sth r7, 8(r8)
b TccExit
TccNotWd:
# --- shield drop
cmpwi r4, 244
bne TccExit
cmpwi r9, 179
beq TccSdrop
cmpwi r9, 178
beq TccSdrop
cmpwi r9, 182
bne TccExit
TccSdrop:
lhz r7, 0xA(r8)
addi r7, r7, 1
sth r7, 0xA(r8)
TccExit:
