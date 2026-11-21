# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x8017a890
################################################################################
# Multishine own replacement (flag ms-scale-own, card route) of Slippi's Online/Menus/ResultsScreen/MoveScaleTable.asm.
# Same job and same hook: r0 = address of the 26-float results-screen character scale table that lives in this payload (m-ex patches the retail table).
# Same trick: `bl BLRL` runs the blrl, which comes back to the mflr with LR = the first table word.
#
# Difference: the table is NOT stored as IEEE floats (it is Melee's own data; the same bytes sit in the retail disc, and nothing we ship may hold a run
# of the player's disc).  Each entry is stored as a whole PERCENT (85, 80, 100 ... one word) and turned into the IEEE float of percent/100 in place, once,
# on the first call (word 0 below 0x1000 = still percents).  The float is built with integer arithmetic, bit-identical to the old literals:
#   q = ((p << 24) + 50) / 100        (round to nearest; 2^24 * p mod 100 is never 50, so no tie)
#   bits = 0x3E800000 + q             (p/100 in [0.5, 1]: exponent 125 + the 24-bit significand q;  p = 100 gives exactly 0x3F800000)
# The host test proves all 26 results against the old float bits.
# Registers: r0 is the result; r3..r6 and cr are saved and restored (the hook sits inside the results-screen function: nothing else may change).
.include "Common/Common.s"

b CODE_START

SCALE_TABLE_BLRL:
blrl
# 26 entries, percent
.long 85
.long 80
.long 100
.long 100
.long 100
.long 70
.long 90
.long 100
.long 100
.long 88
.long 80
.long 100
.long 90
.long 100
.long 90
.long 100
.long 90
.long 90
.long 90
.long 90
.long 100
.long 100
.long 100
.long 100
.long 100
.long 79

CODE_START:
bl SCALE_TABLE_BLRL
mflr r0                       # r0 = table

stwu r1, -0x20(r1)
stw r3, 0x08(r1)
stw r4, 0x0C(r1)
stw r5, 0x10(r1)
stw r6, 0x14(r1)
mfcr r6
stw r6, 0x18(r1)

mr r5, r0                     # (r0 as a base register would read as the number 0)
lwz r3, 0(r5)
cmplwi cr0, r3, 0x1000
bge cr0, SCALE_DONE           # already floats (0x3F59999A ...)
li r4, 26
li r6, 100
SCALE_LOOP:
lwz r3, 0(r5)
slwi r3, r3, 24
addi r3, r3, 50
divwu r3, r3, r6
addis r3, r3, 0x3E80
stw r3, 0(r5)
addi r5, r5, 4
subic. r4, r4, 1
bne SCALE_LOOP

SCALE_DONE:
lwz r6, 0x18(r1)
mtcrf 0xFF, r6
lwz r3, 0x08(r1)
lwz r4, 0x0C(r1)
lwz r5, 0x10(r1)
lwz r6, 0x14(r1)
addi r1, r1, 0x20
EXIT:
