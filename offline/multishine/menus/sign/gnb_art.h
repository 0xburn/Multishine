/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Night screens in the original Rooms look: retail art BY REFERENCE, loaded from the game disc at run time, nothing shipped.
 * CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  Two retail archives, both on every NTSC disc, read into the menu heap while the browser is open and freed when it closes:
 *   GmTou1p (.usd/.dat)  Tournament Melee's entry screen: the rounded translucent box (one 16x16 IA4 quarter texture, four corner bones: any size), the room screen's panels;
 *   TyMnView (.usd/.dat) the Trophy Gallery list: the green frame, its header tabs and side label, the round-ended sort buttons, the list cursor; the list screen.
 * The rooms-beta-2 screens are built from the same two files (Rooms.dat loads TyMnView.usd; Room.dat's GameSetup_gui.dat carries GmTou's ScGamTour scene).
 * A third source, small and on demand: the fighter portraits (GART_ICON below).
 * Pieces are gobjs on the main menu's screen link (gx link 4, as the Event Match screen), freed with the browser; the main menu's own panel (MenMainPanel) is not drawn
 * while a piece is up and is drawn again when the browser closes.
 */
#ifndef GNB_ART_H
#define GNB_ART_H

enum { GART_BOX, GART_FRAME, GART_BUTTON, GART_CURSOR, GART_ICON, GART_BADGE, GART_TEX, GART_KINDS };
#define GART_SLOTS 64u

/* Fighter portraits: the retail character-select icons (MnSlChr, 64x56 CI8 with the name patched in, RGB5A3 palette), one 4 KB block per
   character read from the disc ON DEMAND (a partial read: nothing else of the 3.8 MB file), checked against a hash of the NTSC 1.02 file, drawn on a copy of the
   square quad of GmTou1p's scene (set 1 joint 1, a 32x32 CI4 picture; the archive the room screen already loads) whose texture points at the icon.  Ids: Melee's external character ids 0..25 (the ids of the
   room view); 19 Sheik has no retail icon of its own: it shows Zelda's (which carries the name "ZELDA"; the caller writes "SHEIK" under it).  Any failed check: the icon is unknown and the caller keeps its "?" box. */
#define GART_ICON_N 27u                         /* 0..25 the fighters, 26 Random (the retail "?" icon, a dark grey box with a big grey question mark) */
#define GART_ICON_W 64u
#define GART_ICON_H 56u
#define GART_ICON_BYTES 0x1100u                 /* heap per loaded icon: 3,584 B image + 512 B palette + 240 B of descriptors (+ padding) */
int gart_icon_ok(unsigned ch);                  /* 1: the icon of external id ch is loaded (read now when needed) and checked */
/* icon ch on piece `slot` centred at world (x, y, z), its quad scaled (sx, sy) (1, 1 = the template quad: 2.99 x 2.99 units); returns 0 (and hides the slot)
   when there is no icon for ch */
int gart_icon(unsigned slot, unsigned ch, float x, float y, float z, float sx, float sy);
unsigned gart_icon_heap(void);                 /* bytes of icon blocks held now (proof / report) */
unsigned gart_icon_why(void);                  /* proof: 0 none tried, 1 closed, 2 no quad template, 3 MnSlChr missing / other size / card-served, 4 heap, 5 hash, 8 the last load was good */

/* Stage icons (the stage prep screen): the retail stage-select pictures (MnSlMap, 64x56 or 48x48 CI8 with their RGB565 palette), read ON DEMAND
   like the portraits (one 4 KB block each, checked against a hash of the NTSC 1.02 file, drawn on the same template quad).  The six stages of a set: external stage ids 0x1F Battlefield,
   0x20 Final Destination, 0x02 Fountain of Dreams, 0x08 Yoshi's Story, 0x03 Pokemon Stadium, 0x1C Dream Land (N64).  Any other id, a failed check, or a card-served / other file: no icon. */
#define GART_STAGE_N 6u
int gart_stage_ok(unsigned sid);
/* the stage icon `sid` on piece `slot`, centred at world (x, y, z), its quad scaled (sx, sy) (1, 1 = the template quad 2.99 x 2.99 units); 0 (and the slot hidden) when there is no icon */
int gart_stage(unsigned slot, unsigned sid, float x, float y, float z, float sx, float sy);
unsigned gart_stage_w(unsigned sid);           /* the icon's own size in pixels (64 x 56, Dream Land 48 x 48); 0 for an unknown id */
unsigned gart_stage_h(unsigned sid);

/* Slippi's GameSetup pictures (tools/gnb_setup_art.py: three IA4 textures Slippi made, a drawn strike X; generated into the build, GNB_SETUP_ART) on copies of the Trophy Gallery sort
   plate's descriptors like the badges: the colour is the material tint.  gart_tex_open loads the archives like gart_open and returns 1 when the pictures are usable (0: the build
   has none or the plate was not recognised: the caller draws text instead). */
enum { GT_CHEV_R, GT_CHEV_L, GT_TRI_R, GT_TRI_L, GT_ARW, GT_X, GT_BRK, GT_FRM, GT_CHK, GT_N };
int gart_tex_open(void);
void gart_tex(unsigned slot, unsigned id, float x, float y, float z, float sx, float sy, unsigned rgb, float alpha);
unsigned gart_tex_w(unsigned id);              /* the picture's own size in pixels (0 when unknown) */
unsigned gart_tex_h(unsigned id);

/* loads the two archives (once per browser visit); returns a bit mask of what is usable: 1 << GART_BOX, 1 << GART_FRAME ... (0: nothing, the caller keeps the Event look) */
unsigned gart_open(void);
/* frees every piece, draws the main menu panel again, frees the archives (the browser closes or the overlay is freed) */
void gart_close(void);
unsigned gart_have(void);
/* the main menu's panel (blue frame, tabs, grid of the menu kinds) drawn or not */
void gart_menu_panel(int show);
/* Tournament Melee's animated background (GmTou1p set 5) behind the room screen; the main menu's own background is not drawn meanwhile.  gart_bg(1) returns 1 when it is up (0: not
   available, nothing changed), gart_bg(0) removes it and shows the menu's again; gart_tick runs its animation, once a frame. */
int gart_bg(int show);
void gart_tick(void);
void gart_menu_back(int show);                 /* only the main menu's background: show 0 = not drawn (the list's plain dark one), 1 = drawn again (gart_bg(0) and gart_close also do) */
/* piece `slot` of `kind` at world (x, y, z): a BOX takes half sizes (hw, hh) through its corner bones, the others take them as scale; rgb 0xRRGGBB00 tints the
   material (0 keeps the retail colour), alpha (0..1) its material alpha.  Created on first use, moved after. */
void gart_put(unsigned slot, unsigned kind, float x, float y, float z, float hw, float hh, unsigned rgb, float alpha);
void gart_scale(unsigned slot, float sx, float sy);      /* the piece's root scale (a BOX: its border too; thin bars) */
/* a joint of a piece (DFS index as lb_80011E24), shown: its local translation x, y; or the corner bones of a box joint (ToyFigurePanel 8, the list panel) */
void gart_part_pos(unsigned slot, unsigned idx, float x, float y);
void gart_part_box(unsigned slot, unsigned idx, float hw, float hh);
void gart_part_hide(unsigned slot, unsigned idx);      /* hide one joint of a slot's tree again */
void gart_hide(unsigned slot);
void gart_hide_all(void);
/* the frame's header tabs and side label: 1 = the Rooms art was put in their place (Online Play / Rooms / Public, built in), 0 = the retail labels
   are hidden and the caller writes text there */
int gart_frame_labels(void);
int gart_sheik_own(void);                  /* 1 = the Sheik cell shows Sheik's own icon (the build's delta applied): no caption over it */
/* Grandmaster standings icons: our own textures on copies of the Trophy Gallery sort plate's descriptors (the plate's quad and material, its image repointed to ours).
   gart_badges_open loads the archives like gart_open and returns 1 when the plate was recognised (else 0: the caller keeps the retail marker).
   The RANK ICONS are Project Slippi's rank badges (GPL-3.0, offline/assets/slippi_ranks; stb_rank_tex.h), colour textures drawn untinted. THE helper:
       gart_rank_icon(slot, icon, big, x, y, z, GART_RI_SX(px), GART_RI_SY(px))
   icon = STB_SH_T1 .. STB_SH_TMAX, STB_SH_GM, STB_SH_PLACEMENT of stb.h (stb_mark / stb_rank_icon give it); (x, y) the icon's CENTRE and z in the units of gart_badge's callers (the Standings list uses
   TX(118), the row's text y minus 13.5 px, text z + 1); px the drawn size in pixels of the 640 x 528 frame, a compile-time constant (the scale is folded by the compiler: soft-float code only moves floats);
   big is ignored (the 64 x 64 textures are retired).  One slot per icon on screen; gart_hide(slot) removes it.
   gart_badge draws the Friends marker ring / disc (STB_SH_RING, STB_SH_DISC; tinted by rgb 0xRRGGBB00). */
/* THE PLATE'S GEOMETRY: the quad runs x -5.78 .. 5.88, y +-1.355 (local units; world = local x scale; 0.05376 world per frame px
   horizontally), and its u coordinates are piecewise (x -5.78 .. 1.90 -> u -1 .. 0.4546, x 1.90 .. 3.24 -> u 0.4546 .. 0.5 (29.5 x stretched), x 3.24 .. 5.88 -> u 0.5 .. 1).  Only x 3.24 .. 5.88
   (2.64 wide, centre 4.56) maps a texture undistorted, so the rank textures (STB_RI_L, 64 x 32) carry the badge in their RIGHT half and the plate's clamp shows the clear left half as nothing.
   The drawn badge is px tall; its width is 1.013 x px (the constant measured on the cube: hexagon w/h 0.886 as in Project Slippi's art).  x of gart_rank_icon is the joint's: use GART_RI_X. */
#define GART_RI_SX(px) ((px) * 0.02063f)               /* 2.64 local x sx x 0.05376 = 1.013 x px frame px wide */
#define GART_RI_SY(px) ((px) * 0.018875f)
#define GART_RI_X(cx, px) ((cx) - 4.56f * GART_RI_SX(px))   /* joint x for the badge's centre cx (world units) */
#define GART_NVAR (18u + GT_N)                           /* texture variants: 8 icons small, ring, disc, 8 icons big, the GameSetup pictures */
int gart_badges_open(void);
void gart_rank_icon(unsigned slot, unsigned icon, unsigned big, float x, float y, float z, float sx, float sy);
void gart_badge(unsigned slot, unsigned shape, float x, float y, float z, float sx, float sy, unsigned rgb);
/* the marker column of the Event-view lists (Friends dots, Standings tiers, one recipe): Melee draws a 640 x 528 EFB that the video output squeezes to 480 lines, so a circle must be
   528 / 480 = 1.1 times taller than wide in the frame.  The badge quad at 0.216 x 0.453 draws 23 x 22 px (art edges included); these factors make it 22 x 24 px: a circle of 21.8 px on the TV
   that stays inside the 22 px grid cell.  DX: the quad scales from its corner, so the disc / ring sit this far left of the column pixel to be centred in the cell; STEPK: the dots' row step
   against the rows' (Dolphin: 28.0 px a row against 27.0). */
#define GART_CIRC_SX_K 0.956f
#define GART_CIRC_SY_K 1.062f
#define GART_CIRC_DX -0.215f
#define GART_CIRC_STEPK 0.9705f
#endif
