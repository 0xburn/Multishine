# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x802F368C # ifAll_802F343C epilogue (its last instruction, blr)
################################################################################
# Multishine (own code, opt-in flag ms-hud-raise): "Raise HUD" against a CRT's overscan, which cut off the stock icons, the percent, the series
# emblem and the name tag of the bottom HUD.
#
# ifAll_802F343C(count) fills the per-player HUD position table ifAll_804A0FD8.x18 (Vec3 x 6, at 0x804A0FF0) from the scene model's joints at the start of every
# match (ifStatus_802F665C calls it right before it creates each player's HUD).  Everything of the bottom HUD reads that table: the percent / emblem model
# (ifStatus_802F61FC HSD_JObjSetTranslate), the stock icons (ifStock) and Slippi's name text and plate (InitInGame.asm REG_HUDPOS = 0x804a0ff0).  So adding to the y of
# each entry right after the table is filled lifts the whole HUD, in online and offline matches alike.  Nothing here is synced or in a checksum (the HUD is cosmetic).
#
# The level (0 Off, 1 Small, 2 Medium, 3 Large) is a byte in this body's header, written by the engine (ml_glue.c hudr_mark, from Settings > Game > Raise HUD):
# body+0 `b START`, body+4 `blrl`, body+8 the flag word (level in byte 11), body+12 'MSHR', body+16 / +20 / +24 the three amounts in HUD world units (0.1 unit = 1 px of 640x480:
# the HUD canvas shows 10 px per unit, the name text uses canvas scale 0.1): 8, 16 and 24 px.
# The recomputation by ifAll_802F343C at every match start means the amount never accumulates; entries of unused slots are not read.
# The level is 0..3 by construction (two bits of the option byte, decoded by ms_set_decode).
# Lost-stock icon: ifStock's per-frame function (symbol ifStock_802F8298; on the retail disc it is at 0x802F8298-0x802F89F4) keeps a lost slot's icon visible in its
# "lost" pose, which falls below the bottom edge of the screen.  The raise lifts that fall into view (a red Roy head at the very bottom for about 20 fields after every loss).  So while the
# level is not Off the module hides the icons of the lost slots: at match start it installs a branch to STUB over the slot loop's counter increment (0x802F853C `addi r25,r25,1`, executed
# once per slot; r25 = slot, r27 = the slot's icon JObj, r28 / r29 = the function's player pointer / data base); STUB does that increment and, if the level byte is not 0 and slot >= stocks,
# calls HSD_JObjSetFlagsAll(icon, JOBJ_HIDDEN) (0x80371D9C, the call the function itself uses for the stock counter).  Slots below the stock count get the flag cleared by the function every
# frame, so a respawned or new match shows them again.  The smoke burst (efSync 0x474) is a separate effect and stays.  At level 0 the retail word is written back at every match start
# (Off runs the retail code unchanged); the stub also tests the level byte, so switching to Off in the pause menu stops the hiding at once.  The word is written with dcbst / icbi so the
# instruction cache sees it.  STUB runs inside that loop and uses only r0, r3-r5, r12 and CTR (volatile there).
# r4-r8 and f1-f2 only (all volatile at a function's end); LR is restored; the module ends with the replaced retail instruction (blr).
.include "Common/Common.s"

b START
DATA_BLRL:
blrl
.long 0
.long 0x4D534852
.float 0.8
.float 1.6
.float 2.4

START:
mflr r0
bl DATA_BLRL
mflr r4
lis r8, 0x802F
ori r8, r8, 0x853C
lbz r5, 3(r4)
cmplwi r5, 0
beq CLEAR
slwi r5, r5, 2
add r5, r4, r5
lfs f1, 4(r5)
lis r6, 0x804A
ori r6, r6, 0x0FF0
li r7, 6
mtctr r7
RAISE_LOOP:
lfs f2, 4(r6)
fadds f2, f2, f1
stfs f2, 4(r6)
addi r6, r6, 12
bdnz RAISE_LOOP
bl GETSTUB
mflr r7
subf r6, r8, r7
rlwinm r6, r6, 0, 6, 29
oris r6, r6, 0x4800
b WRITE

CLEAR:
lis r6, 0x3B39
ori r6, r6, 1
WRITE:
stw r6, 0(r8)
dcbst 0, r8
sync
icbi 0, r8
sync
isync
mtlr r0
blr

GETSTUB:
blrl
STUB:
addi r25, r25, 1
bl DATA_BLRL
mflr r12
lbz r5, 3(r12)
cmplwi r5, 0
beq STUB_END
lbz r0, 0(r28)
mulli r0, r0, 80
add r3, r29, r0
lwz r0, 84(r3)
cmpw r25, r0
ble STUB_END
mr r3, r27
li r4, 16
lis r12, 0x8037
ori r12, r12, 0x1D9C
mtctr r12
bctrl
STUB_END:
lis r12, 0x802F
ori r12, r12, 0x8540
mtctr r12
bctr
