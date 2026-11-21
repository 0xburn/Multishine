/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S12 trace-hash scope: the render-cache bits that the gameplay hash must not read (pure, host-tested: tests/s12_gpmask_test.c).
   TRACE HASH ONLY: nothing here changes what the engine saves, restores or simulates. */
#ifndef S12_GPMASK_H
#define S12_GPMASK_H

#define S12GP_ANY 0xFFu
#define S12GP_KB 4u         /* FTKIND_KIRBY (internal fighter kind, fp+4; index 4 of ftData_UnkIntBoolFunc0.model_events) */
#define S12GP_PURIN 15u     /* FTKIND_PURIN = Jigglypuff (index 15) */

struct s12_gpm { unsigned short word; unsigned int mask; unsigned char kind; };

static const struct s12_gpm s12_gp_extra[] = {
    /* FtPartsVis.cleared[0..4] at fp+0x5B0..0x5B4 (ft/dobjlist.h:35-39): a render cache flag "DObj visibility of model
       group idx already applied". Set by the draw path (234-241/283-286/322-325/350-353/430-431 ->
       ftParts_800750C8 -> ftParts_80074B6C, :600-625), cleared by a logic-time model swap (ftParts_80074B0C ->
       ftParts_80074D7C, :595-597/652-670); read only by those same three DObj-flag functions. A side without a
       render between two logic frames (re-simulation) keeps the logic value: the YS Marth/Sheik gap (runs l1, l2). */
    { 0x5B0u / 4, 0xFFFFFFFFu, S12GP_ANY }, { 0x5B4u / 4, 0xFF000000u, S12GP_ANY },
    /* S12 (on S11): fp+0x2221 bit 0x10 = x2221_b3 (ft/types.h:1801), "colour-overlay light needs setup": set by the logic
       at the end of the colour-animation update, cleared only by the draw path (243/281
       -> ftCo_8009F5AC, :72-93, which reads it to position the overlay light); nothing else reads it
       (grep over src/melee).  Same render-cache pattern as FtPartsVis: the FD Falcon/ICs gap (runs g1, g1w: player 0
       primary chunk 15, word 24 = fp+0x2220, xor 0x00100000). */
    { 0x2220u / 4, 0x00100020u, S12GP_ANY },
    /* (stadium_locw, runs r3/r10/r14/r17 of 2026_10_22_0728; Jigglypuff vs Game and Watch on Stadium, player 1 chunk 15): three more draw-path bits, all in the same flag block.
       x2223_b2 = fp+0x2223 bit 0x20 (word 0x2220 mask 0x20), x2227_b7 = fp+0x2227 bit 0x01 (word 0x2224 mask 0x1), x2228_b0 = fp+0x2228 bit 0x80 (word 0x2228 mask 0x80000000).  Each is
       WRITTEN only (ftDrawCommon_8008..., lines 244-246 clear, 289-292 / 326 / 354 set: the material/transparency state of the last draw) and READ only (lines 76,
       122, 166, 222, 229: material render mode); no gameplay code reads them (grep over src/melee).  A side that re-simulates without a draw between two logic frames keeps the logic value:
       the same render-cache pattern as FtPartsVis and x2221_b3.  The raw-word locator (S12F_LOCW) showed exactly these bits as the ONLY difference at the first differing frame in all four runs. */
    { 0x2224u / 4, 0x00000001u, S12GP_ANY }, { 0x2228u / 4, 0x80000000u, S12GP_ANY },
    /* the SECOND FtPartsVis of two characters, same render cache, same call site (ftParts_800750C8 calls
       ftData_UnkIntBoolFunc0.model_events[fp->kind] right after the main one, :738-757).
       Jigglypuff: ftPurin_FighterVars.x2248 (ft/kinds/ftPurin/types.h:18, :524-531): model_num fp+0x2248,
       cleared[5] fp+0x224C..0x2250.  Kirby: hat.x24 at fp+0x2250 (ftKirby/types.h:25): cleared[5] fp+0x2254..0x2258.
       The words hold other (gameplay) data for the other characters, hence the kind. */
    { 0x224Cu / 4, 0xFFFFFFFFu, S12GP_PURIN }, { 0x2250u / 4, 0xFF000000u, S12GP_PURIN },
    { 0x2254u / 4, 0xFFFFFFFFu, S12GP_KB },    { 0x2258u / 4, 0xFF000000u, S12GP_KB }
};

/* clears the masked bits of word index i (fp word) of a fighter of `kind` */
static unsigned int s12_gp_extra_apply(unsigned int kind, unsigned int i, unsigned int w)
{
    unsigned int k;
    for (k = 0; k < sizeof s12_gp_extra / sizeof s12_gp_extra[0]; k++)
        if (s12_gp_extra[k].word == i && (s12_gp_extra[k].kind == S12GP_ANY || s12_gp_extra[k].kind == kind)) w &= ~s12_gp_extra[k].mask;
    return w;
}
#endif
