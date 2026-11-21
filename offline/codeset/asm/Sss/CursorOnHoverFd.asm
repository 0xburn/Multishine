# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x8025a530
# SSS_CursorThink: Z toggles the frozen Final Destination background (Stadium: stock behaviour).
################################################################################
# Multishine (own code, opt-in flag ms-fd-toggle; replaces Slippi's CursorOnHoverStadium.asm, which this is derived from, GPL-3.0).
# A SUPERSET of the stock module: for every icon but Final Destination (25) it does exactly what the stock module does (Z on any port flips Slippi's Stadium toggle byte, body+8 of
# IngameCheckIfFrozen = INJ_FREEZE_STADIUM; Slippi's own presentation modules draw the Stadium name and badge).  Differences from the stock ring ("Stadium's ring is
# always purple and FD's is never purple"): the ring is purple ONLY while the hovered icon is a FROZEN stage, Pokemon Stadium (18) with Slippi's Stadium byte set or Final Destination (25) with
# our byte set, and the default colour for every other icon and for an unfrozen Stadium / FD (stock: purple on the Stadium icon whatever its state, never on FD). Added, for Final Destination only:
#   - Z while the FD icon is hovered flips OUR toggle byte (body+8 of THIS module, found by the engine through the `b` at 0x8025a530) and not Slippi's Stadium byte.  The FD toggle is a local
#     screen option: it is never sent anywhere.
#   - NO text: "Frozen Final Destination" and a badge were tried as menu text (a canvas of their own created in the SSS, text struct with white colour on link 8 / own ortho camera): the
#     objects exist but nothing is drawn, so that part is not built.  There is no on-screen indication.
# DEFAULT ("FD still transforms"): the FD byte starts at 1 = FROZEN.  There is no on-screen indication, so nobody could tell whether
#   Z had been pressed: FD is frozen from the first boot, and Z on the FD icon flips it (0 = the background transforms again) until the next boot.  The byte is RAM only.
# Body: b CODE_START; blrl; byte FD toggle (1 = frozen, boot default); align; unused word; word: magic MSFD.
################################################################################

.include "Common/Common.s"

.set SSS_HoveredIcon, 0x804d6cae
.set GOBJ_Current, 0x804d781c
.set HSD_PadMaster, 0x804c1fac
.set ID_GRPS, 18                 # Pokemon Stadium icon (mnStageSel_803F06D0[18].stkind = 3)
.set ID_GRFD, 25                 # Final Destination icon (mnStageSel_803F06D0[25].stkind = 0x20)
.set CLR_HOVER, 0x8052D5FF
.set CLR_DEFAULT, 0x99B3B3FF

.set REG_IDX, 30
.set REG_ST, 29                  # body + 8: the FD toggle byte, +4 the last ring colour
.set REG_STAD, 28                # Slippi's toggle body (Stadium byte at +8)
.set REG_COUNT, 27
.set REG_JOBJ, 24

b CODE_START
STATE:
blrl
.byte 1
.align 2
.long 0                          # unused (was the last ring colour)
.long 0x4D534644                 # 'MSFD': the engine reads body+8 as the FD toggle only when this magic is at body+16

CODE_START:
  backup
  bl STATE
  mflr REG_ST
  computeBranchTargetAddress REG_STAD, INJ_FREEZE_STADIUM

# any port pressed z?
  li REG_COUNT, 0
LOOP_TOGGLE:
  load r3, HSD_PadMaster
  mulli r0, REG_COUNT, 0x44
  add r3, r3, r0
  lwz r4, 0x8(r3)                # instant buttons
  rlwinm. r0, r4, 0, 27, 27      # z button is bit 4
  beq LOOP_TOGGLE_CHECK
  addi r5, REG_STAD, 0x8         # stock: Z flips Slippi's Stadium byte ...
  cmpwi REG_IDX, ID_GRFD
  bne TOGGLE
  mr r5, REG_ST                  # ... except on Final Destination: our byte
TOGGLE:
  lbz r4, 0x0(r5)
  xori r4, r4, 1
  stb r4, 0x0(r5)
  b STATE_START
LOOP_TOGGLE_CHECK:
  addi REG_COUNT, REG_COUNT, 1
  cmpwi REG_COUNT, 4
  blt LOOP_TOGGLE

STATE_START:
  loadwz r3, GOBJ_Current
  lwz REG_JOBJ, 0x28(r3)
  cmpwi REG_IDX, ID_GRPS         # purple only while the hovered icon is a FROZEN stage: Stadium with Slippi's toggle byte set, or FD with ours
  bne CHECK_FD
  lbz r5, 0x8(REG_STAD)
  b CHECK_FROZEN
CHECK_FD:
  cmpwi REG_IDX, ID_GRFD
  bne ON_UNHOVERED
  lbz r5, 0x0(REG_ST)
CHECK_FROZEN:
  cmpwi r5, 0
  beq ON_UNHOVERED
  load r4, CLR_HOVER
  b SET_COLOR
ON_UNHOVERED:
  load r4, CLR_DEFAULT
SET_COLOR:
  lwz r3, 0x18(REG_JOBJ)         # dobj
  lwz r3, 0x8(r3)                # mobj
  lwz r3, 0xC(r3)                # material
  stw r4, 4(r3)                  # ambient colour

EXIT:
  restore
  stb r30, -0x49F2(r13)
