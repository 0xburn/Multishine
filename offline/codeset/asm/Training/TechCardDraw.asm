# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x80175D14
################################################################################
# Multishine training (own work; references: decomp gm/gmresult.c fn_80175C5C / fn_80175240,
# gm/types.h ResultsData + MatchEnd, baselib/sislib.c + (SisLib text, ASCII to font codes)).
# Opt-in flag tech-card. Drawing half of the tech card (R3, 1d).
#
# Hook: 0x80175D14 in fn_80175C5C (retail word 80010034, lwz r0,0x34(r1)), right after the results screen has
# created the KO / fall / SD numbers of the four panels (gm/gmresult.c:1034-1052). lbl_8046DBE8 (ResultsData) then
# holds player_data[slot].ko_count (the KO / fall / SD numbers' text, placed from the panel anchor x4C[slot]) and
# x94 = MatchEnd.
# For every slot whose player_standings[slot].pkind is Human (0) we create one SisLib text on the results canvas
# (HSD_SisLib_803A6754(0, 0), as the KO numbers do; one text per line) in the lower part of that port's portrait box
# (y from the KO numbers' text, x from the fixed box pitch), three small lines:
#   LC ok/attempts pct%     (L-cancels; "LC -" without an aerial landing)
#   Tech n  Miss m          (ground techs, missed techs)
#   WD n  Drop m            (wavedashes, shield drops)
# The texts belong to SisLib GObjs of the results scene and go away with it, as Melee's own texts do.
# The counters come from TechCardCount.asm: its block sits at its body + 8; the body address is decoded from the
# branch the patcher (or a Gecko handler) writes at its hook 0x800693AC. If that word is not a branch (module off)
# or the magic 'TCRD' is missing, nothing is drawn. After drawing, the block's last-frame word is set to
# 0xFFFFFFFF so the next match starts from zero. Reads only, except that one word and the new texts.
# '/' and '%' are passed as Shift-JIS pairs (81 5E, 81 93): the SisLib encoder maps only space " ' , - . 0-9 : A-Z a-z
# from plain ASCII.
# Registers: own frame, r25-r31 saved and restored; the original instruction runs last on the caller's frame.

.set TCD_TEXT_NEW, 0x803A6754   # HSD_Text* HSD_SisLib_803A6754(int sis, int canvas)
.set TCD_TEXT_ADD, 0x803A6B98   # int HSD_SisLib_803A6B98(HSD_Text*, float x, float y, const char* fmt, ...)
.set TCD_TEXT_SCALE, 0x803A7548 # void HSD_SisLib_803A7548(HSD_Text*, int line, float sx, float sy)
.set TCD_TEXT_COLOR, 0x803A74F0 # void HSD_SisLib_803A74F0(HSD_Text*, int line, GXColor*)
.set TCD_FMT_LC, 0x20
.set TCD_FMT_LC_NONE, 0x2F
.set TCD_FMT_TECH, 0x34
.set TCD_FMT_WD, 0x45

stwu r1, -0x50(r1)
stmw r25, 0x30(r1)

lis r31, 0x8006
ori r31, r31, 0x93AC          # TechCardCount hook
lwz r3, 0(r31)
rlwinm r4, r3, 6, 26, 31      # primary opcode
cmpwi r4, 18                  # b
bne TcdExit
rlwinm r4, r3, 0, 6, 29       # LI << 2
andis. r5, r4, 0x0200
beq TcdFwd
addis r4, r4, -0x0400         # sign-extend
TcdFwd:
add r31, r31, r4
addi r31, r31, 8              # counter block
lwz r3, 0(r31)
lis r4, 0x5443
ori r4, r4, 0x5244            # 'TCRD'
cmpw r3, r4
bne TcdExit

bl TcdGo
TcdData:
.float 0.0                    # +0x00 x of every line inside the text
.float 23.5                   # +0x04 card y: below the KO numbers' y (text y grows downwards), in the portrait box
.float 1.15                   # +0x08 line spacing
.float 0.038                  # +0x0C glyph scale x
.float 0.040                  # +0x10 glyph scale y
.float -20.5                  # +0x14 card x of port 1 (centre of its portrait box)
.float 13.8                   # +0x18 box pitch per port
.byte 0xF0, 0xF0, 0xF0, 0xFF  # +0x1C colour
# strings at fixed offsets (gecko's batched assembly cannot take label differences); checked by the build dump
.string "LC %d\x81\x5E%d %d\x81\x93"         # +0x20 (15 bytes)  "LC 2/5 40%"
.string "LC -"                                # +0x2F (5)
.string "Tech %d  Miss %d"                    # +0x34 (17)
.string "WD %d  Drop %d"                      # +0x45 (15) -> end +0x54
.align 2

# TcdLine: r4 = fmt, r5..r7 = values, f2 = line y offset. One text per line (Melee's multi-line spacing depends on
# the line scale), placed at the card base (caller frame +0x08 x, +0x0C y, +0x10 z) + f2; adds, scales, colours.
TcdLine:
mflr r0
stwu r1, -0x30(r1)
stw r0, 0x34(r1)
stw r4, 0x10(r1)
stw r5, 0x14(r1)
stw r6, 0x18(r1)
stw r7, 0x1C(r1)
stfs f2, 0x20(r1)
li r3, 0
li r4, 0
lis r12, TCD_TEXT_NEW@h
ori r12, r12, TCD_TEXT_NEW@l
mtctr r12
bctrl
stw r3, 0x24(r1)
cmpwi r3, 0
beq TcdLineOut
lfs f0, 0x38(r1)              # base x
stfs f0, 0(r3)
lfs f0, 0x3C(r1)              # base y + offset
lfs f1, 0x20(r1)
fadds f0, f0, f1
stfs f0, 4(r3)
lfs f0, 0x40(r1)              # z
stfs f0, 8(r3)
li r0, 1
stb r0, 0x49(r3)              # default_kerning
stb r0, 0x4A(r3)              # default_alignment: centre
lwz r4, 0x10(r1)
lwz r5, 0x14(r1)
lwz r6, 0x18(r1)
lwz r7, 0x1C(r1)
lfs f1, 0x00(r30)
fmr f2, f1
crclr 6
lis r12, TCD_TEXT_ADD@h
ori r12, r12, TCD_TEXT_ADD@l
mtctr r12
bctrl
stw r3, 0x28(r1)
mr r4, r3
lwz r3, 0x24(r1)
lfs f1, 0x0C(r30)
lfs f2, 0x10(r30)
lis r12, TCD_TEXT_SCALE@h
ori r12, r12, TCD_TEXT_SCALE@l
mtctr r12
bctrl
lwz r3, 0x24(r1)
lwz r4, 0x28(r1)
addi r5, r30, 0x1C
lis r12, TCD_TEXT_COLOR@h
ori r12, r12, TCD_TEXT_COLOR@l
mtctr r12
bctrl
TcdLineOut:
lwz r0, 0x34(r1)
addi r1, r1, 0x30
mtlr r0
blr

TcdGo:
mflr r30                      # r30 = data
lis r29, 0x8047
subi r29, r29, 0x2418         # lbl_8046DBE8 (ResultsData)
li r28, 0                     # slot
TcdSlot:
lwz r3, 0x94(r29)             # MatchEnd*
cmpwi r3, 0
beq TcdDone
mulli r4, r28, 0xA8
add r3, r3, r4
lbz r3, 0x58(r3)              # player_standings[slot].pkind
cmpwi r3, 0                   # Human
bne TcdNext
mulli r4, r28, 0xD8
add r4, r4, r29
lwz r4, 0x120(r4)             # player_data[slot].ko_count (ResultsData +0x98 + 0xD8 * slot + 0x88)
cmpwi r4, 0
beq TcdNext
lfs f0, 0x14(r30)             # x: centre of this port's portrait box = x0 + pitch * slot
lfs f1, 0x18(r30)
lis r0, 0x4330
stw r0, 0x18(r1)
stw r28, 0x1C(r1)
lfd f2, 0x18(r1)              # (double) slot via 2^52 + slot
lis r0, 0x5980                # 2^52 as float: 0x59800000
stw r0, 0x20(r1)
lfs f3, 0x20(r1)
fsub f2, f2, f3
frsp f2, f2
fmadds f0, f1, f2, f0
stfs f0, 0x08(r1)             # card base x
lfs f0, 4(r4)                 # KO numbers' pos_y + offset
lfs f1, 0x04(r30)
fadds f0, f0, f1
stfs f0, 0x0C(r1)             # card base y
lfs f0, 8(r4)
stfs f0, 0x10(r1)             # z
slwi r26, r28, 4
add r26, r26, r31
addi r26, r26, 8              # slot counters

# line 1: L-cancel successes/attempts and %, line 2: techs and missed techs, line 3: wavedashes and shield drops
lhz r6, 0(r26)                # attempts
lhz r5, 2(r26)                # successes
lfs f2, 0x00(r30)
cmpwi r6, 0
bne TcdLcPct
addi r4, r30, TCD_FMT_LC_NONE
bl TcdLine
b TcdLine2
TcdLcPct:
mulli r7, r5, 100
divwu r7, r7, r6
addi r4, r30, TCD_FMT_LC
bl TcdLine
TcdLine2:
lfs f2, 0x08(r30)
addi r4, r30, TCD_FMT_TECH
lhz r5, 4(r26)
lhz r6, 6(r26)
bl TcdLine
lfs f2, 0x08(r30)
fadds f2, f2, f2
addi r4, r30, TCD_FMT_WD
lhz r5, 8(r26)
lhz r6, 0xA(r26)
bl TcdLine
TcdNext:
addi r28, r28, 1
cmpwi r28, 4
blt TcdSlot
TcdDone:
li r3, -1
stw r3, 4(r31)                # reset pending for the next match

TcdExit:
lmw r25, 0x30(r1)
addi r1, r1, 0x50
lwz r0, 0x34(r1)              # original instruction
