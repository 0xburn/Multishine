# Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
################################################################################
# Address: 0x8015FAE4
################################################################################
# Multishine offline (own code, opt-in flag rules-after-load): re-apply the build's default item/stage preferences
# after the Melee save file has been loaded from a memory card.
#
# Why: the FasterMeleeSettings codes (NoItems, SinglesStages; also StockMode/4Stocks/8Minutes) patch the DEFAULT tables
# gmMainLib_DefaultGameRules (0x803D4A48) / gmMainLib_DefaultGamePrefs (0x803D4A60).data. Those are copied into the
# live save struct by gmMainLib_8015FBA4 (boot / soft reset / erase data). The GameRules block (save+0x1850..0x1868) is
# NOT part of the card file, so stocks/time/mode always come from the defaults. GamePrefs (save+0x1CB0: item switch,
# item mask, rumble, deflicker, random-stage mask) IS part of the card file (GmSaveData starts at save+0x1868), and the
# memory card load (gm_1AED memcard scene -> lb_8001CBBC -> gmMainLib_8015FA34) overwrites it with whatever the card holds:
# items on (item_freq 2) and the retail stage mask. See.
#
# Hook: 0x8015FAE4 is the final "bl lbAudioAx_80028690" of gmMainLib_8015FA34 (decomp :1301, the save
# post-load function). Retail instruction: 4BEC8BAD (bl 0x80028690). We run it first, then copy
#   GamePrefs.item_freq (u8)    DefaultGamePrefs @803D4A60 -> save+0x1CB0
#   GamePrefs.item_mask (u64)   @803D4A68                  -> save+0x1CB8
#   GamePrefs.stage_mask (u32)  @803D4A78                  -> save+0x1CC8
# where save = *(0x804D3EE0) (gmMainLib_804D3EE0). Rumble, sound balance, deflicker, language, unlocks, names and
# stats from the card are untouched. This code never touches the card; the game itself writes the (now default) prefs
# back at its next save, as it does for any other change.
# The no-card path (Skip Memcard Prompt, status 13/15) never reaches gmMainLib_8015FA34 and is therefore unchanged.
# Only volatile registers are used; after the hooked call the function continues with "bl 8015F500" and its epilogue.
.include "Common/Common.s"

# original instruction: bl lbAudioAx_80028690
branchl r12, 0x80028690

lis r12, 0x804D
lwz r12, 0x3EE0(r12)          # r12 = save struct (gmMainLib_804D3EE0)
lis r11, 0x803D
addi r11, r11, 0x4A60         # r11 = &gmMainLib_DefaultGamePrefs

lbz r0, 0x00(r11)             # item_freq
stb r0, 0x1CB0(r12)
lwz r0, 0x08(r11)             # item_mask (u64)
stw r0, 0x1CB8(r12)
lwz r0, 0x0C(r11)
stw r0, 0x1CBC(r12)
lwz r0, 0x18(r11)             # stage_mask
stw r0, 0x1CC8(r12)
