# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x8025ac6c
# Animates the "stage select" and "alt stage" textures based on cursor hover
################################################################################
# Multishine (own code, flag ms-sss-guard; derived from Slippi's StageSelectTextProc.asm, GPL-3.0): the SAME animation, but it first checks that the title tree this proc walks
# (jobj -> child -> next -> next) really exists.  Slippi's patched MnSlMap has it, the player's STOCK MnSlMap does not (the walk then loads null + 8: a DSI on hardware).  With the
# tree missing the proc skips the title text (retail's two JObj_Anim calls still run, see GUARD_EXIT), so the stage select can never crash whatever MnSlMap the disc / the card pack serves.  It also publishes the answer: the word at body + 8
# (found through the `b` at the hook, see CheckForFrozenOnStageNameGuard.asm) is 1 only while the tree exists, 0 otherwise (cleared at every stage select entry).  The two
# name modules read it, so that the 'Frozen Pokemon Stadium' name (anim 31) is only asked for when MnSlMap has it.
################################################################################

.include "Common/Common.s"

.set SSS_HoveredIcon, 0x804d6cae # u8
.set SSS_CreateStageNameText, 0x80259ed8
.set SITE_TEXTPROC, 0x8025ac6c   # the hook of StageSelectTextProcGuard.asm: its body holds the 'patched MnSlMap' word at body + 8
.set OFS_PATCHED, 8
.set ID_GRPS, 18
.set OFST_HOVERED_ICON, -0x49F2

.set REG_GOBJ, 31
.set REG_JOBJ, 30
.set REG_UDATA, 29
.set JObj_ForEachAnim, 0x80364C08
.set JObj_RemoveAnimAll, 0x8036f6b4
.set JObj_GetAnimFrame, 0x8022f298
.set AOBJ_ReqAnim, 0x8036410C
.set AOBJ_StopAnim, 0x8036414C

b CODE_START
PATCHED_BLRL:
blrl
.long 0                           # body + 8: 1 = the patched title tree is there

CODE_START:
  backup
  mr REG_GOBJ, r3
  bl PATCHED_BLRL
  mflr r4
  li r0, 0
  stw r0, 0(r4)                   # a fresh stage select: not proven yet

# alloc data
  li r3, 12
  branchl r12, HSD_MemAlloc
  mr REG_UDATA, r3
# zero data
  li r4, 12
  branchl r12, Zero_AreaLength

# add to gobj
  mr r3, REG_GOBJ
  li r4, 4
  load r5, HSD_Free
  mr r6, REG_UDATA
  branchl r12, GObj_AddUserData

# add proc
  mr r3, REG_GOBJ
  bl FN_AnimBackgroundText_BLRL
  mflr r4
  li r5, 0
  branchl r12, GObj_AddProc

  b EXIT

################################################################################
FN_AnimBackgroundText_BLRL:
blrl
# regs
.set REG_UDATA, 29
.set REG_TXT_FRAME, 28
.set REG_ALT_FRAME, 27
.set REG_STATE, 26
.set REG_TXT_JOBJ, 25
.set REG_ALT_JOBJ, 24
.set REG_MAX, 23
.set REG_PUB, 22
# data
.set TEXT_FRAME, 0
.set ALT_FRAME, TEXT_FRAME + 4
.set ANIM_STATE, ALT_FRAME + 4
# constants
.set DEFAULT_MAX, 20

FN_AnimBackgroundText:
  backup

# init vars
  mr REG_GOBJ, r3
  lwz REG_JOBJ, 0x28(REG_GOBJ)

# GUARD: every hop of the title tree must exist, else skip the title text (and say so in the published word); retail's own JObj_Anim calls still run (GUARD_EXIT)
  bl PATCHED_BLRL
  mflr REG_PUB
  li r0, 0
  stw r0, 0(REG_PUB)
  cmpwi REG_JOBJ, 0
  beq GUARD_DONE
  lwz r3, 0x10(REG_JOBJ)          # child
  cmpwi r3, 0
  beq GUARD_EXIT
  lwz r3, 0x8(r3)                 # child->next (the title text)
  cmpwi r3, 0
  beq GUARD_EXIT
  lwz r3, 0x8(r3)                 # ->next (the alt stage badge)
  cmpwi r3, 0
  beq GUARD_EXIT
  li r0, 1
  stw r0, 0(REG_PUB)
  lwz REG_TXT_JOBJ, 0x10(REG_JOBJ)
  lwz REG_TXT_JOBJ, 0x8(REG_TXT_JOBJ)
  lwz REG_ALT_JOBJ, 0x8(REG_TXT_JOBJ)

  lwz REG_UDATA, 0x2C(REG_GOBJ)
  lwz REG_TXT_FRAME, TEXT_FRAME(REG_UDATA)
  lwz REG_ALT_FRAME, ALT_FRAME(REG_UDATA)

# decide hover state
CHECK_STATE:
  loadbz r3, SSS_HoveredIcon
  cmpwi r3, ID_GRPS
  beq SET_ALT_STATE

SET_DEFAULT_STATE:
  li r4, 0
  stw r4, ANIM_STATE(REG_UDATA)
  b ANIMATE_START

SET_ALT_STATE:
  li r4, 1
  stw r4, ANIM_STATE(REG_UDATA)

ANIMATE_START:
  lwz REG_TXT_FRAME, TEXT_FRAME(REG_UDATA)
  lwz REG_ALT_FRAME, ALT_FRAME(REG_UDATA)
  lwz REG_STATE, ANIM_STATE(REG_UDATA)

# forward when unhovered, reverse when hovered
  cmpwi REG_STATE, 0
  beq TEXT_FORWARD
  b TEXT_REVERSE

TEXT_FORWARD:
  cmpwi REG_TXT_FRAME, DEFAULT_MAX
  bge TEXT_DONE # if >= max, skip
  addi REG_TXT_FRAME, REG_TXT_FRAME, 1
  stw REG_TXT_FRAME, TEXT_FRAME(REG_UDATA)
  mr r3, REG_TXT_FRAME
  branchl r12, FN_IntToFloat
  mr r3, REG_TXT_JOBJ
  branchl r12, JObj_ReqAnimAll
  mr r3, REG_TXT_JOBJ
  branchl r12, JObj_AnimAll
  b TEXT_DONE

TEXT_REVERSE:
  cmpwi REG_TXT_FRAME, 0
  ble TEXT_DONE # if <= 0, skip
  subi REG_TXT_FRAME, REG_TXT_FRAME, 1
  stw REG_TXT_FRAME, TEXT_FRAME(REG_UDATA)
  mr r3, REG_TXT_FRAME
  branchl r12, FN_IntToFloat
  mr r3, REG_TXT_JOBJ
  branchl r12, JObj_ReqAnimAll
  mr r3, REG_TXT_JOBJ
  branchl r12, JObj_AnimAll

TEXT_DONE:
  cmpwi REG_STATE, 1
  beq ALT_FORWARD
  b ALT_REVERSE

ALT_FORWARD:
  cmpwi REG_ALT_FRAME, DEFAULT_MAX
  bge ALT_DONE
  addi REG_ALT_FRAME, REG_ALT_FRAME, 1
  stw REG_ALT_FRAME, ALT_FRAME(REG_UDATA)
  mr r3, REG_ALT_FRAME
  branchl r12, FN_IntToFloat
  mr r3, REG_ALT_JOBJ
  branchl r12, JObj_ReqAnimAll
  mr r3, REG_ALT_JOBJ
  branchl r12, JObj_AnimAll
  b ALT_DONE

ALT_REVERSE:
  cmpwi REG_ALT_FRAME, 0
  ble ALT_DONE
  subi REG_ALT_FRAME, REG_ALT_FRAME, 1
  stw REG_ALT_FRAME, ALT_FRAME(REG_UDATA)
  mr r3, REG_ALT_FRAME
  branchl r12, FN_IntToFloat
  mr r3, REG_ALT_JOBJ
  branchl r12, JObj_ReqAnimAll
  mr r3, REG_ALT_JOBJ
  branchl r12, JObj_AnimAll

ALT_DONE:
  mr r3, REG_JOBJ
  branchl r12, JObj_Anim
  lwz r3, 0x10(REG_JOBJ)
  branchl r12, JObj_Anim

  # lwz r5, TEXT_FRAME(REG_UDATA)
  # lwz r6, ALT_FRAME(REG_UDATA)
  # logf LOG_LEVEL_WARN, "[T FRAME, ALT FRAME] %d, %d\n"

  restore
  blr
GUARD_EXIT:
  # the title nodes are missing (stock MnSlMap) but this proc REPLACES retail's proc at the hook: retail's work is the two JObj_Anim calls of ALT_DONE, and they are what
  # plays the stage select frame lines, the circular emblem and the 'STAGE SELECT' caption (without them those stay unseen: owner capture, Couch).  Do them here too.
  mr r3, REG_JOBJ
  branchl r12, JObj_Anim
  lwz r3, 0x10(REG_JOBJ)
  cmpwi r3, 0
  beq GUARD_DONE
  branchl r12, JObj_Anim
GUARD_DONE:
  restore
  blr
################################################################################

EXIT:
  restore
  