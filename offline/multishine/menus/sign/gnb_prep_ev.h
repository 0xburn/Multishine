/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The stage prep screen on the Game Night room screen (gnb_prep.h, E1) in Slippi Ranked's GameSetup look: the console front end.
 * CONSOLE ONLY: this file is INCLUDED by gnb_ev.c (-DGNB_MELEE) after grid_art, because it draws with that file's own helpers (axs, art_box, the text slots and the art ids); nothing of it is a
 * second code base.  Layout and look follow project-slippi/slippi-ssbm-c Scenes/Ranked/GameSetup.c (GPL-3.0 "We should use this for consistency"): a header with both players (rank
 * icon slot, name, a small line under it) and the pick clock between two pairs of turn arrows, the stage icons in one row (a big X over a struck one), the line of what to do, and the bottom bar
 * YOUR CHAR, OPP CHAR, STRIKE 1, STRIKE 2/3, STRIKE 4 (a later game: ... BAN, STAGE) of icons and "?" boxes.  It is DRAWN here from the model (gnb_prep.[ch]) and the pieces below, not run as
 * Slippi's scene (that is MexTK code on m-ex, which this engine does not have):
 *   Melee's own art, read from the player's disc at run time (nothing copied): the stage icons (MnSlMap), the fighter icons and the "?" icon (MnSlChr), the Tournament boxes and animated background
 *     (GmTou1p), the menu font;
 *   Slippi's pictures from GameSetup_gui.dat (the turn chevrons, the small triangles, the timeline arrow; the strike X is drawn by tools/gnb_setup_art.py): gart_tex, built in at build time
 *     (credit); without them (a build that has no GameSetup_gui.dat) the arrows are text and the X a letter.
 * Slots: art 10..60 (the room's list-screen slots are free while this screen is up), text slots X_LOB 0..11 and 17..20 (the key line), X_GRID 0..11.  The rank icon is one call (prep_rank).
 * Resident engine bytes: none (this is sign.ovl code).
 */
#ifndef GNB_PREP_EV_H
#define GNB_PREP_EV_H
/* ---- art slots (all inside 0 .. AI_END - 1; room_art hides 10 .. 61 and re-draws what it shows, so nothing of this screen survives into the lobby) ---- */
enum {
    P_INNER = AF_FRAME,                                  /* 10: the dark inner panel */
    P_CHEV_L, P_TRI_L, P_TRI_R, P_CHEV_R,                /* 11..14 the turn arrows */
    P_FR0,                                               /* 15..20 the stage frames (the cursor is the gold one) */
    P_ST0 = P_FR0 + 6,                                   /* 21..26 the stage icons */
    P_DM0 = P_ST0 + 6,                                   /* 27..32 the dark plates over a struck / banned / not allowed stage */
    P_X0 = P_DM0 + 6,                                    /* 33..38 the strike X */
    P_TF0 = P_X0 + 6,                                    /* 39..44 the timeline frames */
    P_TI0 = P_TF0 + 6,                                   /* 45..50 the timeline icons */
    P_TD0 = P_TI0 + 6,                                   /* 51..56 the timeline plates over a later step's "?" */
    P_TA0 = P_TD0 + 6,                                   /* 57..60 the timeline arrows */
    P_END = P_TA0 + 4
};
typedef char gnbev_prep_slot_check[(unsigned)P_END <= (unsigned)AI_END ? 1 : -1];
/* P_BRK / P_SEL (art slots 62 and 63: the cursor bracket and frame, the OK / Redo boxes) are defined in gnb_ev.c next to AG_CHAT */
#define PS_RANK_L AB_PORT_L                               /* the two rank icon slots (free here) */
#define PS_RANK_R AB_PORT_R
#define COL_BAR 0x31407AFFu
#define COL_BAR_PLAY 0x2E7A28FFu
#define PC_GOLD 0xF0C028FFu                               /* the stage under the cursor */
#define PC_FRAME 0xDCDCE6FFu                              /* a stage frame / a done step: thin and white as Slippi's */
#define PC_FRAME_DIM 0x4A4A56FFu                          /* a struck stage / a step to come */
#define PC_BAR 0x8C8C96FFu                                /* the header and bottom boxes (the room's) */
#define PC_LABEL 0xE6E6F0FFu                              /* the timeline labels */
#define PC_CLK_WARN 0xFECA34FFu                           /* the clock of the player to act: yellow from 10 s, red from 3 s (Slippi's UpdateTimer: 254, 202, 52 and 255, 50, 50) */
#define PC_CLK_PANIC 0xFF3232FFu
#define PC_WIN 0x58C858FFu                                /* the set's pips: won, lost, to come, the game now */
#define PC_LOSS 0xE05858FFu
#define PC_BTN_ON 0x7A6014FFu                             /* the OK / Redo buttons: the one under the cursor, the other */
#define PC_BTN_OFF 0x2E2E3CFFu

/* layout in px of the 640 x 528 frame, from Slippi's 2000 x 1486 capture (the 583 x 447 safe area x 29.5, y 37 scaled by 0.2955) */
#define PR_HY 68.0f                                       /* the header box centre y (the room's) */
static const float PR_NAME_Y = 48.0f, PR_SUB_Y = 77.0f, PR_CLK_Y = 48.0f, PR_SCORE_Y = 111.0f;
static const float PR_BTN_Y = 292.0f, PR_BTN_XL = 275.0f, PR_BTN_XR = 367.0f, PR_BTN_HW = 22.0f, PR_BTN_HH = 3.0f;
static const float PR_PIPX[6][5] = { { 0 }, { 321.0f }, { 312.0f, 330.0f }, { 303.0f, 321.0f, 339.0f }, { 294.0f, 312.0f, 330.0f, 348.0f }, { 285.0f, 303.0f, 321.0f, 339.0f, 357.0f } };     /* the pips, 18 px apart round the clock's centre: constants, no float maths in the overlay */
static const float PR_STAGE_Y = 205.0f, PR_MSG_Y = 292.0f, PR_MSG2_Y = 322.0f, PR_HINT_Y = 498.0f;
static const float PR_SX5[5] = { 169.3f, 245.2f, 321.0f, 396.9f, 472.8f };
static const float PR_SX6[6] = { 156.0f, 222.0f, 288.0f, 354.0f, 420.0f, 486.0f };
static const float PR_TL_Y = 461.0f, PR_TL_LABEL_Y = 427.0f;      /* Slippi: the boxes' centre at screen row 421, the labels at 390 */
static const float PR_TLX[5] = { 75.8f, 170.0f, 264.7f, 384.0f, 503.0f };             /* the entry centres, game 1 (the fourth is the pair's middle) */
static const float PR_TLX2[2] = { 357.5f, 410.5f };       /* Slippi: 47 px boxes 53 apart (6 px between them) */                                   /* the pair's two pictures */
static const float PR_TLP[4] = { 75.8f, 170.0f, 264.7f, 359.0f };                     /* a later game's entries (up to four) */
static const float PR_TLA5[4] = { 122.0f, 217.0f, 311.5f, 455.5f };                   /* the arrows between the entries, game 1 */
static const float PR_TLA4[3] = { 122.0f, 217.0f, 311.5f };
/* the pictures' sizes in scale units (gnb_art.h: a 24 px badge is 0.216 x 0.453, so 0.009 and 0.01887 a pixel), the stage / fighter icon quad (2.99 units) */
#define PR_CHEV_SX 0.6780f          /* 32 texel picture drawn 34 px wide: the chevron (28 texels) 30 px */
#define PR_CHEV_SY 0.6340f
#define PR_CHEV_SX_S 0.5430f
#define PR_CHEV_SY_S 0.5072f
#define PR_QUAD_K 84.8f              /* the picture is the RIGHT HALF of its canvas (the plate's evenly mapped part, tools/gnb_setup_art.py): 2.64 units wide, centre 4.56 units right of the joint; a unit is 18.6 frame px per unit of sx, so the picture is 49.1 px wide per unit of sx (x the depth's magnification) and its centre 84.8 px right */
#define PR_TRI_SX 0.2970f           /* 16 texel picture 15 x 15 px */
#define PR_TRI_SY 0.3120f
#define PR_ARW_SX 0.3560f           /* 18 x 18 px */
#define PR_ARW_SY 0.3760f
#define PR_X_SX 1.156f              /* the X (52.6 of 64 texels) 51 px wide in a 58 px outline */
#define PR_X_SY 0.851f
#define PR_X6_SX 0.997f
#define PR_X6_SY 0.729f
#define PR_FRM_SX 1.224f            /* the outline (60 of 64 texels) 58 px wide */
#define PR_FRM_SY 0.900f            /* square on the screen (1.082 drew 49 x 59 px on the cube) */
#define PR_FRM6_SX 1.055f
#define PR_FRM6_SY 0.775f

/* the elements are at different depths (dz) and the camera is a perspective one: an element drawn at world x lands at c + (x - c) * m(dz) on screen, m = 1 / (1 - 0.03 dz) (measured on a rig shot with the frame, the X and the cursor painted in pure colours: the X at dz 2.8 sat 13 px out at the grid's edge, the frame at dz 1.0 about 5), so a picture at depth dz is asked for the point pulled in by (1 - 0.03 dz) about the screen's centre (321, 266) and every layer of a tile lands on the same pixel */
static float pr_ax(float px, float dz) { return add(321.0f, mula(ZERO, sub(px, 321.0f), sub(1.0f, mula(ZERO, 0.03f, dz)))); }
static float pr_ay(float py, float dz) { return add(266.0f, mula(ZERO, sub(py, 266.0f), sub(1.0f, mula(ZERO, 0.03f, dz)))); }
static float pr_wy(float py) { return sub(ZERO, mula(TY(py), BOX_DY, S.pyk)); }                  /* world y of the frame row py: boxes and icons share it (grid_art, portrait) */
/* a box centred at (cx, cy) frame px: hw, hh the corner bones' half sizes (px); alpha < 1 for a see-through plate */
static void pr_box(unsigned slot, float cx, float cy, float hw, float hh, unsigned rgb, float alpha, float dz)
{
    gart_put(slot, GART_BOX, TX(pr_ax(cx, dz)), pr_wy(pr_ay(cy, dz)), add(S.nz, dz), mula(ZERO, hw, PXK), mula(ZERO, hh, S.pyk), rgb, alpha);
    gart_scale(slot, ONE, ONE);
}
static void pr_tex(unsigned slot, unsigned id, float cx, float cy, float sx, float sy, unsigned rgb, float alpha, float dz)
{
    gart_tex(slot, id, sub(TX(pr_ax(cx, dz)), mula(ZERO, mula(ZERO, sx, PR_QUAD_K), PXK)), pr_wy(pr_ay(cy, dz)), add(S.nz, dz), sx, sy, rgb, alpha);
}
/* a stage icon / fighter icon `wpx` frame px wide at (cx, cy), drawn at the icons' own aspect (64 x 56, the 48 x 48 Nintendo 64 stage square at the same height): the first version squeezed every
   icon into a square, which read as "cut off at the left and right".  0.01798 = 0.05376 / 2.99 (the icon quad is 2.99 units, a pixel is 0.05376 wide); 0.29264 = 0.875 / 2.99 */
static int pr_stage(unsigned slot, unsigned sid, float cx, float cy, float wpx, float dz)
{
    float h = mula(ZERO, wpx, 0.29264f);                                         /* the height in px / 2.99 */
    float sx = gart_stage_w(sid) == 48u ? mula(ZERO, wpx, 0.01573f) : mula(ZERO, wpx, 0.01798f);       /* 48 wide: the same 0.875 x wpx square */
    return gart_stage(slot, sid, TX(pr_ax(add(cx, mula(ZERO, wpx, gart_stage_w(sid) == 48u ? 0.013f : 0.022f)), dz)), pr_wy(pr_ay(cy, dz)), add(S.nz, dz), sx, mula(ZERO, S.pyk, h));
}
static int pr_fighter(unsigned slot, unsigned ch, float cx, float cy, float wpx, float dz)
{
    return gart_icon(slot, ch, TX(pr_ax(cx, dz)), pr_wy(pr_ay(cy, dz)), add(S.nz, dz), mula(ZERO, wpx, 0.01798f), mula(ZERO, S.pyk, mula(ZERO, wpx, 0.29264f)));
}
/* the header's rank icon (Project Slippi's rank badges, gart_rank_icon): the server's tier byte (a Grandmaster set view fills p->tier) through stb_rank_icon; a room has no rank, its players show the unrated "?" icon */
static void pr_rank(unsigned slot, unsigned tier, float cx, float cy)
{
    unsigned ic = tier == 0xFFu ? (unsigned)STB_SH_PLACEMENT : stb_rank_icon(0u, 0u, tier, 0, 0, 0, 0);
    gart_rank_icon(slot, ic, 0u, add(TX(pr_ax(cx, 1.0f)), GART_RI_X(0.726f, 30)), pr_wy(cy), add(S.nz, 1.0f), GART_RI_SX(30), GART_RI_SY(30));
}

static void pr_hide_texts(void)
{
    unsigned i;
    for (i = 14u; i <= 16u; i++) axs(X_LOB + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");            /* (0 .. 13 and 17, 18 are this screen's own) */
    axs(X_LOB + 19u, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    axs(X_LOB + 20u, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    for (i = 0; i < 3u; i++) xs(X_CSB + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    xs(X_OPEN, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    for (i = 6u; i < 8u; i++) axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    for (i = 14u; i < GNB_PICK_N; i++) axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    for (i = 0; i < GNB_ROSTER_MAX; i++) axs(X_GRID + 3u * GNB_ROSTER_MAX + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");   /* (the crowns panel's Streak column) */
}

/* the stage row: icons (or a name box), frames, the cursor, the X over a stage that is out */
static void pr_stages(const struct gnp *p, unsigned tex)
{
    unsigned i, six = p->n > 5u;
    unsigned my = p->mine && (p->step == GNP_STRIKE || p->step == GNP_BAN || p->step == GNP_PICK);
    if (p->step == GNP_CHAR) {                                               /* a character step: one big fighter in the middle (this player's last fighter, a "?" for the other player's step), OK / Change under it (Slippi's char_selectors / char_wait_selectors) */
        unsigned id = p->mine && p->prevc < 26u ? p->prevc : 26u;
        for (i = 0; i < GNP_MAX; i++) { axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); axs(X_GRID + 8u + i, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); if (i) { gart_hide(P_FR0 + i); gart_hide(P_ST0 + i); } gart_hide(P_DM0 + i); gart_hide(P_X0 + i); }
        pr_box(P_FR0, 321.0f, PR_STAGE_Y, 36.0f, 31.0f, PC_FRAME, 1.0f, 1.0f);
        if (!pr_fighter(P_ST0, id, 321.0f, PR_STAGE_Y, 68.0f, 1.6f)) gart_hide(P_ST0);
        gart_hide(P_SEL); gart_hide(P_BRK);
        return;
    }
    for (i = 0; i < GNP_MAX; i++) {
        float cx;
        unsigned st, out, hover, ico = 0;
        char nm[24], nt[16];
        if (i >= p->n || p->step == GNP_WAIT) {
            gart_hide(P_FR0 + i); gart_hide(P_ST0 + i); gart_hide(P_DM0 + i); gart_hide(P_X0 + i);
            axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
            axs(X_GRID + 8u + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
            continue;
        }
        cx = six ? PR_SX6[i] : PR_SX5[i];
        st = p->state[i];
        out = st == GNP_S_STRUCK || st == GNP_S_BANNED || st == GNP_S_DAVE || st == GNP_S_OUT;
        hover = my && !p->confirm && i == p->cur && st == GNP_S_FREE;
        /* ("make it look good", against Slippi's own picture ): every stage stands in a THIN square outline as in Slippi (the selection-frame
           picture), white, gold for the stage under the cursor / the game's stage, dim for one that is out; the thick grey box round each tile is gone, and the cursor is that gold outline
           plus the bracket under it (no second white frame) */
        if (tex && out) gart_hide(P_FR0 + i);                                  /* Slippi hides the border of a disabled stage */
        else if (tex) pr_tex(P_FR0 + i, GT_FRM, add(321.0f, mula(ZERO, sub(cx, 321.0f), 0.99f)), PR_STAGE_Y, six ? PR_FRM6_SX : PR_FRM_SX, six ? PR_FRM6_SY : PR_FRM_SY, st == GNP_S_CHOSEN ? 0xFFCC0000u : 0x9A9AA400u, 1.0f, 1.0f);   /*, against the real Slippi frame: the outline is a mid grey there (about 120 of 255), and ours sat about 1 px outward of its icon at 76 px from the middle */      /* (the tile under the cursor keeps its white outline: in Slippi's model only the tray is gold) */
        else pr_box(P_FR0 + i, cx, PR_STAGE_Y, six ? 8.0f : 11.5f, six ? 7.0f : 10.5f, st == GNP_S_CHOSEN ? PC_GOLD : out ? PC_FRAME_DIM : PC_FRAME, 1.0f, 1.0f);
        ico = (unsigned)(six ? pr_stage(P_ST0 + i, p->id[i], cx, PR_STAGE_Y, 38.0f, 1.6f) : pr_stage(P_ST0 + i, p->id[i], cx, PR_STAGE_Y, 46.0f, 1.6f));
        if (!ico) {                                                          /* no icon (a card-served or other file): the name in a plain box */
            gart_hide(P_ST0 + i);
            gnp_row(p, i, nm, nt);
            axs(X_GRID + i, cx, add(PR_STAGE_Y, 14.0f), 0.017f, out ? COL_DIM2 : COL[0], 1, nm);
        } else axs(X_GRID + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        axs(X_GRID + 8u + i, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        if (out) {
            pr_box(P_DM0 + i, cx, PR_STAGE_Y, six ? 7.0f : 10.0f, six ? 7.0f : 10.0f, 0x000000FFu, 0.8f, 2.2f);
            if (st == GNP_S_STRUCK || st == GNP_S_BANNED || st == GNP_S_DAVE) {              /* Slippi draws the same X on every disabled stage: struck, banned or not allowed (Dave's rule) */
                if (tex) pr_tex(P_X0 + i, GT_X, cx, PR_STAGE_Y, six ? PR_X6_SX : PR_X_SX, six ? PR_X6_SY : PR_X_SY, 0x87878700u, 0.8f, 2.8f);       /* Slippi: a disabled stage's X is grey 0x87, alpha 0.8 */
                else { gart_hide(P_X0 + i); axs(X_GRID + 8u + i, cx, sub(PR_STAGE_Y, 14.0f), 0.05f, COL_DIM2, 1, "X"); }
            } else gart_hide(P_X0 + i);
        } else if (p->sel[i] && p->step != GNP_PICK && tex) {                  /* marked, not yet confirmed: the X, lighter, no dark plate */
            gart_hide(P_DM0 + i);
            pr_tex(P_X0 + i, GT_X, cx, PR_STAGE_Y, six ? PR_X6_SX : PR_X_SX, six ? PR_X6_SY : PR_X_SY, 0xFF000000u, 0.8f, 2.8f);       /* Slippi: the X on a stage this player marked is RED, alpha 0.8 */
        } else { gart_hide(P_DM0 + i); gart_hide(P_X0 + i); }
        if (p->sel[i] && p->step == GNP_PICK) {                                  /* a marked stage pick: Slippi's green tick on the stage (its Check model: 00FF00, alpha 0.8), the outline stays white; without the pictures a gold box */
            if (tex && !out) pr_tex(P_X0 + i, GT_CHK, cx, PR_STAGE_Y, six ? PR_X6_SX : PR_X_SX, six ? PR_X6_SY : PR_X_SY, 0x00FF0000u, 0.8f, 2.8f);
            else if (!tex) pr_box(P_FR0 + i, cx, PR_STAGE_Y, six ? 8.0f : 11.5f, six ? 7.0f : 10.5f, PC_GOLD, 1.0f, 1.0f);
        }
        if (hover && tex) {                                                      /* the cursor: a thin white frame round the tile and a yellow bracket under it (Slippi's) */
            gart_hide(P_SEL);                                                    /* */
            pr_tex(P_BRK, GT_BRK, cx, add(PR_STAGE_Y, six ? 19.5f : 22.6f), six ? 1.503f : 1.744f, six ? 0.474f : 0.55f, 0xFFCC0000u, 1.0f, 2.6f);      /* Slippi's tray (its model: 1.54 x the tile's width, centred 3.63 units under the tile's centre, gold FFCC00): the bar directly under the outline, the stubs outside its sides */
        }
    }
    if (!(my && !p->confirm && p->cur < p->n && p->state[p->cur] == GNP_S_FREE && tex)) { gart_hide(P_SEL); gart_hide(P_BRK); }
}

/* the timeline bar: one entry per step of the game, an icon or the "?" box in a frame under its label, an arrow between the entries */
static void pr_timeline(const struct gnp *p, unsigned tex)
{
    struct gnp_tl tl[GNP_TL_MAX];
    unsigned n = gnp_timeline(p, tl), e, k = 0, game1 = p->game <= 1u;
    for (e = 0; e < n; e++) {
        float lx = game1 ? PR_TLX[e] : PR_TLP[e];
        unsigned j;
        axs(X_LOB + 6u + e, lx, PR_TL_LABEL_Y, 0.019f, PC_LABEL, 1, gnp_label(tl[e].label));
        for (j = 0; j < tl[e].n && k < 6u; j++, k++) {
            float cx = tl[e].n == 2u ? PR_TLX2[j] : lx;
            unsigned id = tl[e].id[j], st = tl[e].state, shown;
            if (tex) pr_tex(P_TF0 + k, GT_FRM, cx, PR_TL_Y, 0.992f, 0.729f, st == 1u ? 0xFFFFFF00u : 0xDCDCE600u, st == 0u ? 0.55f : 1.0f, 1.0f);      /*: the same thin outline as the stage tiles (the box plate has the uneven edges of its stretch band); the size of the six-stage row's, whose icons are 38 px too */
            else pr_box(P_TF0 + k, cx, PR_TL_Y, 5.5f, 5.5f, st == 1u ? 0xFFFFFFFFu : PC_FRAME, 1.0f, 1.0f);         /* the step now bright white, the done ones white, the later ones dim */
            if (tl[e].label == GNP_L_YOUR || tl[e].label == GNP_L_OPP) shown = id != 0xFFu && pr_fighter(P_TI0 + k, id, cx, PR_TL_Y, 40.0f, 1.6f);     /* a fighter */
            else shown = id != 0xFFu && pr_stage(P_TI0 + k, id, cx, PR_TL_Y, 40.0f, 1.6f);              /* a stage struck / banned / picked */
            if (!shown) {                                                                                             /* not known yet: the "?" icon, darker until its step is next */
                if (!pr_fighter(P_TI0 + k, 26u, cx, PR_TL_Y, 40.0f, 1.6f)) gart_hide(P_TI0 + k);
                if (st == 0u) pr_box(P_TD0 + k, cx, PR_TL_Y, 5.0f, 5.0f, 0x000000FFu, 0.6f, 2.2f); else gart_hide(P_TD0 + k);
            } else gart_hide(P_TD0 + k);
        }
        if (e + 1u < n) {
            float ax = game1 ? PR_TLA5[e] : PR_TLA4[e];
            if (tex) pr_tex(P_TA0 + e, GT_ARW, ax, PR_TL_Y, PR_ARW_SX, PR_ARW_SY, 0xFFFFFF00u, tl[e + 1u].state ? 0.9f : 0.35f, 1.0f);
            else gart_hide(P_TA0 + e);
        }
    }
    for (; e < GNP_TL_MAX; e++) { axs(X_LOB + 6u + e, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); if (e < 4u) gart_hide(P_TA0 + e); }
    for (; k < 6u; k++) { gart_hide(P_TF0 + k); gart_hide(P_TI0 + k); gart_hide(P_TD0 + k); }
}

/* Slippi's two animations of this screen, from its model's tracks (GameSetup_gui.dat, decoded), played at this overlay's 30 frames a second:
     the cursor tray blinks between FFCC00 and CB9933, 5 frames each at 60 a second (CSBoxSelector's material animation: a 10 frame loop of the diffuse colour), here 3 and 2 ticks;
     the X on a stage this player marks grows from a quarter to full size in 4 frames (the X's four end bones run 0.2 -> 0.8), here over two ticks.  A struck stage's X is shown grown.
   Called every frame while the screen is up: it only moves pieces that prep_art has drawn (a slot's picture is kept, gart_tex sets place, size and tint). */
static unsigned long pa_t0[GNP_MAX];
static unsigned char pa_on[GNP_MAX];
static void prep_anim(void)
{
    struct gnb *g = S.g;
    const struct gnp *p = &g->prep;
    unsigned i, six = p->n > 5u, my = p->mine && (p->step == GNP_STRIKE || p->step == GNP_BAN || p->step == GNP_PICK);
    if (!gart_tex_open() || p->step == GNP_CHAR || p->step == GNP_WAIT) { for (i = 0; i < GNP_MAX; i++) pa_on[i] = 0; return; }
    if (my && !p->confirm && p->cur < p->n && p->state[p->cur] == GNP_S_FREE)
        pr_tex(P_BRK, GT_BRK, six ? PR_SX6[p->cur] : PR_SX5[p->cur], add(PR_STAGE_Y, six ? 19.5f : 22.6f), six ? 1.503f : 1.744f, six ? 0.474f : 0.55f, (g->clk % 5ul) < 3ul ? 0xFFCC0000u : 0xCB993300u, 1.0f, 2.6f);
    for (i = 0; i < p->n && i < GNP_MAX; i++) {
        unsigned st = p->state[i], out = st == GNP_S_STRUCK || st == GNP_S_BANNED || st == GNP_S_DAVE || st == GNP_S_OUT;
        unsigned marked = !out && p->sel[i], tick = p->step == GNP_PICK;      /* (a marked strike / ban: the red X; a marked pick: the green tick; both grow the same way) */
        unsigned long age;
        if (!marked) { pa_on[i] = 0; continue; }
        if (!pa_on[i]) { pa_on[i] = 1; pa_t0[i] = g->clk; }
        age = g->clk - pa_t0[i];
        if (age > 2ul) continue;                                               /* grown: prep_art's own picture stands */
        {
            float k = age == 0ul ? 0.25f : age == 1ul ? 0.625f : 1.0f;
            pr_tex(P_X0 + i, tick ? GT_CHK : GT_X, six ? PR_SX6[i] : PR_SX5[i], PR_STAGE_Y, mula(ZERO, six ? PR_X6_SX : PR_X_SX, k), mula(ZERO, six ? PR_X6_SY : PR_X_SY, k), tick ? 0x00FF0000u : 0xFF000000u, 0.8f, 2.8f);
        }
    }
}

static void prep_art(void)
{
    struct gnb *g = S.g;
    const struct gnp *p = &g->prep;
    char t[44];
    unsigned tex = (unsigned)gart_tex_open(), turn_me, turn_opp;
    static const unsigned SL[4] = { X_LOB + 17u, 54u, 55u, 56u };
    art_dz = 0.0f;
    art_hide(AB_OUTER, AB_FOOT); art_hide(AB_PORT_L, AB_N); gart_hide(AI_L); gart_hide(AI_R); chat_slots_clear();
    art_hide(AG_PANEL, AI_END);
    pr_hide_texts();
    turn_me = p->mine && p->step != GNP_PLAY; turn_opp = !p->mine && (p->step == GNP_STRIKE || p->step == GNP_BAN || p->step == GNP_PICK);
    /* the header: both players, the clock between the turn arrows */
    /* ("conform to Slippi, it needs breathing room"): the real Slippi frame at 640 x 480 has its three panels at y..97, 111..358 and 372..455, 14 px apart; ours were..104,
       109..356 and 363..435 (5 and 7 px apart) with the key line under the bar.  A box of half height hh round cy covers the screen rows 0.916 cy - 1.3 +- (0.916 hh + 14.6).
       No key line any more (Slippi has none; B held still leaves) */
    art_box(AB_HEAD, 321.0f, 64.2f, 291.5f, 27.2f, PC_BAR);
    art_box(AB_FOOT, 321.0f, 452.3f, 291.5f, 29.9f, PC_BAR);                     /* (the boxes draw about 14 px beyond their half size: the old 31 reached into the middle box)  */
    if (p->kind == 1u) { pr_rank(PS_RANK_L, p->tier[0], 37.0f, 68.0f); pr_rank(PS_RANK_R, p->tier[1], 579.0f, 68.0f); }
    else { gart_hide(PS_RANK_L); gart_hide(PS_RANK_R); }                         /* Slippi draws the rank icon only when there is a rank ( InitPlayerInfo: rank < 0 hides it and the name moves to the edge): a room has none */
    {
        unsigned rl = p->kind == 1u, rr = rl;
        char sb[16];
        axs(X_LOB + 0u, rl ? 73.0f : 41.0f, PR_NAME_Y, 0.04f, COL[0], 0, p->me);
        axs(X_LOB + 1u, rr ? 569.0f : 600.0f, PR_NAME_Y, 0.04f, COL[0], 2, p->opp);
        gnp_sub_name(p, 0, sb); axs(X_LOB + 2u, rl ? 73.0f : 41.0f, PR_SUB_Y, 0.027f, COL_DIM2, 0, sb);
        gnp_sub_name(p, 1, sb); axs(X_LOB + 3u, rr ? 569.0f : 600.0f, PR_SUB_Y, 0.027f, COL_DIM2, 2, sb);
    }
    gnp_clock(p, t);
    { unsigned cs = gnp_clock_state(p); axs(X_LOB + 18u, 321.0f, PR_CLK_Y, 0.045f, cs == 2u ? PC_CLK_PANIC : cs == 1u ? PC_CLK_WARN : COL[0], 1, t); }
    axs(X_LOB + 5u, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
    {                                                                            /* the pips of the set under the header, in a small box of their own */
        unsigned nb = p->best_of > 5u ? 5u : p->best_of;
        if (nb) { static const float PHW[6] = { 0.0f, 4.0f, 13.0f, 22.0f, 31.0f, 40.0f }; float hw = PHW[nb]; pr_box(AB_INNER, 321.0f, PR_SCORE_Y, hw, 0.0f, PC_BAR, 1.0f, 0.5f); pr_box(AB_INNER2, 321.0f, PR_SCORE_Y, sub(hw, 1.5f), 0.0f, 0x000000FFu, 1.0f, 0.55f); }
    }
    {                                                                            /* the pips of the set under the clock (Slippi's GameResult row): W won, L lost, o to come, a gold o the game now */
        unsigned k, bo = p->best_of > 5u ? 5u : p->best_of;
        for (k = 0; k < 5u; k++) {
            unsigned pip = k < bo ? gnp_pip(p, k) : 0u;
            if (k >= bo) { axs(X_GRID + 14u + k, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); continue; }
            axs(X_GRID + 14u + k, PR_PIPX[bo][k], PR_SCORE_Y, 0.03f, pip == 1u ? PC_WIN : pip == 2u ? PC_LOSS : pip == 3u ? PC_GOLD : COL_DIM2, 1, pip == 1u ? "W" : pip == 2u ? "L" : "o");
        }
    }
    if (tex) {
        axs(X_LOB + 11u, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); axs(X_LOB + 12u, 0.0f, 0.0f, 0.03f, COL[0], 0, "");
        pr_tex(P_CHEV_L, GT_CHEV_L, 256.0f, 51.0f, turn_me ? PR_CHEV_SX : PR_CHEV_SX_S, turn_me ? PR_CHEV_SY : PR_CHEV_SY_S, turn_me ? 0xF0C02800u : 0x8C8C9600u, turn_me ? 1.0f : 0.6f, 1.0f);       /* the arrow of the player to act: big and gold, the other grey */
        pr_tex(P_TRI_L, GT_TRI_L, 281.0f, 51.0f, PR_TRI_SX, PR_TRI_SY, turn_me ? 0xF0C02800u : 0x8C8C9600u, turn_me ? 1.0f : 0.6f, 1.0f);
        pr_tex(P_TRI_R, GT_TRI_R, 361.0f, 51.0f, PR_TRI_SX, PR_TRI_SY, turn_opp ? 0xF0C02800u : 0x8C8C9600u, turn_opp ? 1.0f : 0.6f, 1.0f);
        pr_tex(P_CHEV_R, GT_CHEV_R, 386.0f, 51.0f, turn_opp ? PR_CHEV_SX : PR_CHEV_SX_S, turn_opp ? PR_CHEV_SY : PR_CHEV_SY_S, turn_opp ? 0xF0C02800u : 0x8C8C9600u, turn_opp ? 1.0f : 0.6f, 1.0f);
    } else {
        art_hide(P_CHEV_L, P_CHEV_R + 1u);
        axs(X_LOB + 11u, 270.0f, 66.0f, 0.04f, turn_me ? COL[0] : COL[1], 1, "<");
        axs(X_LOB + 12u, 372.0f, 66.0f, 0.04f, turn_opp ? COL[0] : COL[1], 1, ">");
    }
    /* the middle: the panel, the stage row, what to do */
    art_box(AB_OUTER, 321.0f, 257.0f, 291.5f, 118.3f, 0xB4B4C0FFu);
    pr_box(AB_LEFT, 321.0f, 257.0f, 289.0f, 116.0f, 0xA0A0AEFFu, 0.42f, 0.15f);              /* Slippi's plate is a visible grey round the black band (ours showed the dark background there) */              /* Slippi's double frame: the grey plate, a black band, a thin grey line, the dark inside */
    /* ("an inner and outer box, basically on each other; make the inner box the size of Slippi's").  Measured on the REAL Slippi frame (the left half of
       , 640 x 528; the other pictures of that folder are our own build of): outer plate x 5..634, y 122..394;
       black band from x 46 / y 133 (87 % of the outer size); the thin grey line at x 56..582, y 142..360 (83.5 % x 80 %), 3 px; its centre 7 px above the outer one's (ours is centred on the outer plate, 257).
       Ours until 394: the line at 96.7 % x 92 %, 5 px inside the border */
    pr_box(AB_RIGHT, 321.0f, 257.0f, 253.0f, 103.5f, 0x000000FFu, 1.0f, 0.3f);
    pr_box(AB_ME, 321.0f, 257.0f, 243.4f, 95.4f, 0x8C8C96FFu, 1.0f, 0.45f);
    pr_box(P_INNER, 321.0f, 257.0f, 240.6f, 92.6f, 0x000000FFu, 1.0f, 0.6f);
    pr_stages(p, tex);
    gnp_msg(p, t);
    axs(X_LOB + 4u, 321.0f, PR_MSG_Y, 0.024f, COL_DIM2, 1, t);
    if (p->confirm) {                                                            /* OK / Redo in the prompt's place (Slippi's two buttons at the foot of the panel) */
        pr_box(P_BRK, PR_BTN_XL, PR_BTN_Y, PR_BTN_HW, PR_BTN_HH, p->btn == 0u ? PC_BTN_ON : PC_BTN_OFF, 1.0f, 1.0f);
        pr_box(P_SEL, PR_BTN_XR, PR_BTN_Y, PR_BTN_HW, PR_BTN_HH, p->btn == 1u ? PC_BTN_ON : PC_BTN_OFF, 1.0f, 1.0f);
        axs(X_LOB + 14u, PR_BTN_XL, add(PR_BTN_Y, 3.0f), 0.026f, COL[0], 1, gnp_btn_label(p, 0u));
        axs(X_LOB + 15u, PR_BTN_XR, add(PR_BTN_Y, 3.0f), 0.026f, COL[0], 1, gnp_btn_label(p, 1u));
    } else { axs(X_LOB + 14u, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); axs(X_LOB + 15u, 0.0f, 0.0f, 0.03f, COL[0], 0, ""); }                   /* the prompt: small and light grey, centred in the lower part of the inner box */
    axs(X_LOB + 13u, 321.0f, PR_MSG2_Y, 0.02f, COL_DIM2, 1, p->step == GNP_PLAY && p->chosen < p->n ? gnp_stage_name(p->id[p->chosen]) : "");
    key_line(SL, X_KLB, "", TX(120.0f), 1409200, TY(PR_HINT_Y), 270u, COL[0]);          /* "A Strike  Hold B Leave" in the footer area under the bottom bar, out of the panel (400 px = 1409200 Q16 units) */
    /* the bottom bar: the timeline */
    pr_timeline(p, tex);
    art_dz = 0.0f;
}
#endif
