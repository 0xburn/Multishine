/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * chat_layout: where the quick-chat subtitles and the chat window go on the online CSS, in px of the 640x480 frame.
 * Everything stays inside the 80 % title-safe area (x 64..576, y 48..432), which is also the visible centre of a 16:9
 * picture (the widescreen codes widen the view; they do not move or scale a 4:3 layout), so one layout serves both.
 * Rows (newest last) start at the top-left of the title-safe area, as Slippi's own messages (top
 * left, one row per message); the window sits over the player's own card, as Slippi's chat window.
 * Compile-time constants: the soft-float image computes no float at run time; the C compiler folds the tables below.
 * Text units: font_size 0.1 makes 1 text unit = 0.1 world unit; the px mapping of the CSS camera is below.
 */
#ifndef CHAT_LAYOUT_H
#define CHAT_LAYOUT_H

#define LAY_ROW_X_PX 64           /* left edge of every subtitle backdrop */
#define LAY_ROW_Y_PX 48           /* top of the first row */
#define LAY_ROW_STEP_PX 24        /* row pitch (backdrop 22 + 2 px gap): 5 rows end at 168 px */
#define LAY_BOX_H_PX 22
#define LAY_WIN_X_PX 64
#define LAY_WIN_Y_PX 300
#define LAY_LINE_STEP_PX 24       /* window: header + 4 lines = 5 x 24 = 120 px -> ends at 420 px */
#define LAY_WIN_W_PX 300          /* ends at x 364: clear of "Direct Mode" (x 384), hardware photo chat9_hw_19 */
#define LAY_TEXT_DX_PX 6          /* text inside its backdrop */
#define LAY_TEXT_DY_PX (-9)        /* a subtext's glyphs start ~13 px below its y (demo2): -9 puts them 4 px into the box */
#define LAY_PX_PER_CHAR 10        /* backdrop width table step (px); the width itself is estimated per character, 
                                     char_px (measured: M/W ~13 px, lower case ~8.5 px at the scale below) */
#define LAY_MAX_W_PX 512          /* never wider than the title-safe width */
#define LAY_SCALE 0.5f            /* glyph scale: Melee's 32-unit glyph cell -> 16 px */
#define LAY_XSCALE 0.38f          /* glyph x scale: narrower glyphs on the same advance, so wide letters (M, W) no longer
                                     touch (runs r5_kern0 / r5_xs42: kerning off made lines too wide, 0.42 left a gap
                                     in Dolphin but M/W still nearly touched on the TV, photo chat9_hw_18 -> 0.38) */

/* screen mapping of the CSS camera (measured in Dolphin, run demo2 shot f003568: a 330 x 120 unit window drawn at world
   (-25.6, 6.0) covered x 77..390, y 322..435 of the 640x480 frame): 9.45 px per world unit, world origin at (320, 265) */
#define LAY_PX_PER_WORLD 9.45f
#define LAY_WX(px) ((float)((px) - 320) / LAY_PX_PER_WORLD)
#define LAY_WY(px) ((float)((px) - 265) / LAY_PX_PER_WORLD)
#define LAY_U(px) ((float)(px) * 10.0f / LAY_PX_PER_WORLD)      /* px -> text units (font_size 0.1) */
#define LAY_BOXW(n) LAY_U((n) * LAY_PX_PER_CHAR + 2 * LAY_TEXT_DX_PX + 4 > LAY_MAX_W_PX ? LAY_MAX_W_PX \
                          : (n) * LAY_PX_PER_CHAR + 2 * LAY_TEXT_DX_PX + 4)
/* draw order: the CSS camera renders GX links 0..4 (mask 0x1F), its text canvas is link 1 prio
   0x80; Slippi moves its messages to link 3 prio 129, which still left ours behind the character
   icons (run demo3); the last link the camera renders, at the highest priority, draws above everything of the CSS */
#define LAY_GX_LINK 4
#define LAY_GX_PRIO 255

#endif
