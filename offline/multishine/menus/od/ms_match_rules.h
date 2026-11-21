/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* THE PER-MATCH RULES RECORD. Pure, header-only, host-tested.
 *
 * Every in-match behaviour that depends on WHICH kind of match this is (pause / LRAS, the setup block's pause bit and Stadium policy, result reports, spectate upload, friend
 * messages, the chat's Direct flag) reads ONE record, written ONCE when the match is set up and cleared when the session ends:
 *   - OUR route  (our server's ticket paired: Grandmaster / Dojo / Mano a mano / Game night): ms_rules_begin_ours, by the engine glue at the peer hand-off (s12_gc.c od_rules);
 *   - SLIPPI route (a real Slippi client through the bridge): ms_rules_begin_slippi, by br_engine_begin;  Slippi's own behaviour is untouched BY CONSTRUCTION: every policy of
 *     this record is "untouched / off" for it, there is no mode of ours that could be mistaken for it;
 *   - cleared (end_session, the watch session, boot): route OFFLINE / mode NONE: every policy is "untouched / off".  A record that was never written is all zero: route NONE, the same.
 * Fail safe: an unknown mode number writes nothing (route OFFLINE).  Mode 0 is NONE and never a real mode (the old od_o.mode 0 = Ranked read as "Ranked" in a route that never wrote it:
 * cube, release Start / LRAS desync against desktop Slippi).
 * Rules for a new policy: add a field or a flag HERE, set it in the three writers below, read it through the accessors; never read od_o.mode / a scene id / a route global in match-time
 * code (tools/check_match_rules.py fails the build runners when it does). */
#ifndef MS_MATCH_RULES_H
#define MS_MATCH_RULES_H

enum { MR_ROUTE_NONE = 0, MR_ROUTE_OFFLINE = 1, MR_ROUTE_OUR_SERVER = 2, MR_ROUTE_SLIPPI_BRIDGE = 3 };
enum { MR_MODE_NONE = 0, MR_MODE_GRANDMASTER = 1, MR_MODE_DOJO = 2, MR_MODE_MANO = 3, MR_MODE_GAMENIGHT = 4 };
enum { MR_PAUSE_UNTOUCHED = 0, MR_PAUSE_OFF_NO_QUIT = 1, MR_PAUSE_OFF_LRAS_ONLY = 2 };     /* in-match pause: leave Melee / Slippi alone | no pause, no quit | no pause screen, L+R+A+Start quits */
enum { MR_SRC_NONE = 0, MR_SRC_PEER = 1, MR_SRC_BRIDGE = 2, MR_SRC_CLEAR = 3 };             /* who wrote the record last (diagnostics) */

#define MR_F_SETUP_PAUSE_OFF 0x0001u   /* the setup block (s7_rules.flags S7R_PAUSE_OFF) leaves the pause bit set */
#define MR_F_STADIUM_OFF     0x0002u   /* Pokemon Stadium transforms (s7_rules alt_mode S7R_ALT_OFF); neither bit = the stage chooser's own toggle */
#define MR_F_STADIUM_FROZEN  0x0004u   /* Pokemon Stadium frozen for everybody (S7R_ALT_ON) */
#define MR_F_REPORT          0x0008u   /* the result of a game is reported to OUR server */
#define MR_F_SPECTATE        0x0010u   /* the spectate uploader may run when the server asks for it (a room decides by its own flags) */
#define MR_F_FRIEND_MSG      0x0020u   /* friend messages during the match (Dojo, Mano a mano, Game night; never Grandmaster, never the Slippi route) */

/* the mnc.h numbers the writer maps from (static-asserted against MNC_MODE_* in s12_gc.c) */
#define MR_MNC_RANKED 0
#define MR_MNC_UNRANKED 1
#define MR_MNC_DIRECT 2
#define MR_MNC_ROOM 5

struct ms_match_rules {
    unsigned char route, mode, pause, src;
    unsigned short flags;
};

static __attribute__((noinline, unused)) void ms_rules_clear(struct ms_match_rules *r)           /* session end, the watch session, boot: nothing of ours runs */
{
    r->route = MR_ROUTE_OFFLINE; r->mode = MR_MODE_NONE; r->pause = MR_PAUSE_UNTOUCHED; r->src = MR_SRC_CLEAR; r->flags = 0;
}
/* THE MODE TABLE (one copy, resident cost 8 bytes): the policy of each of our modes.  Index = MR_MODE_*; entry = MR_F_* flags | pause << 12.
   Grandmaster = Slippi Ranked rules; Dojo = Slippi Unranked rules (Slippi's own pause overlay kept); Mano a mano = Slippi Direct rules; Game night = Unranked-like setup, no pause screen, LRAS quit. */
#define MR_E_PAUSE(e) ((int)((e) >> 12))
static __inline__ int ms_mode_of_mnc(int mnc_mode) { return mnc_mode == MR_MNC_ROOM ? (int)MR_MODE_GAMENIGHT : ((unsigned int)mnc_mode < 3u ? mnc_mode + 1 : (int)MR_MODE_NONE); }   /* MR_MODE_NONE = not ours */
static __inline__ unsigned int ms_mode_entry(int mode)
{
    static const unsigned short T[5] = {
        0,
        MR_F_SETUP_PAUSE_OFF | MR_F_STADIUM_OFF | MR_F_REPORT | MR_F_SPECTATE | (MR_PAUSE_OFF_NO_QUIT << 12),                   /* Grandmaster */
        MR_F_SETUP_PAUSE_OFF | MR_F_STADIUM_OFF | MR_F_SPECTATE | MR_F_FRIEND_MSG | (MR_PAUSE_UNTOUCHED << 12),                 /* Dojo */
        MR_F_SPECTATE | MR_F_FRIEND_MSG | (MR_PAUSE_UNTOUCHED << 12),                                                           /* Mano a mano */
        MR_F_SETUP_PAUSE_OFF | MR_F_REPORT | MR_F_FRIEND_MSG | (MR_PAUSE_OFF_LRAS_ONLY << 12) };                                /* Game night (frozen Stadium: MR_F_STADIUM_FROZEN, added by the writer) */
    return (unsigned int)mode < 5u ? T[mode] : 0u;
}
/* OUR route.  mnc_mode = the mode both tickets searched (mnc.h MNC_MODE_*), frozen = a Game night room whose Stadium policy is Frozen.  Returns 0, or -1 for a mode that is none of ours
   (nothing is written but the clear: unknown = touch nothing). */
static __attribute__((noinline, unused)) int ms_rules_begin_ours(struct ms_match_rules *r, int mnc_mode, int frozen)
{
    int m = ms_mode_of_mnc(mnc_mode);
    unsigned int e = ms_mode_entry(m);
    ms_rules_clear(r);
    if (!m) return -1;
    r->route = MR_ROUTE_OUR_SERVER; r->src = MR_SRC_PEER; r->mode = (unsigned char)m; r->pause = (unsigned char)MR_E_PAUSE(e);
    r->flags = (unsigned short)((e & 0x0FFFu) | (m == MR_MODE_GAMENIGHT && frozen ? MR_F_STADIUM_FROZEN : 0u));
    return 0;
}
/* SLIPPI route: the match is Slippi's (fixed_rules 1 = Unranked, 0 = Direct, as s7_slippi_match.fixed_rules).  Every policy untouched / off: the real client decides pause, LRAS, setup, reports. */
static __attribute__((noinline, unused)) void ms_rules_begin_slippi(struct ms_match_rules *r, int fixed_rules)
{
    ms_rules_clear(r);                 /* pause untouched, no flag: the invariant every accessor below relies on */
    r->route = MR_ROUTE_SLIPPI_BRIDGE; r->src = MR_SRC_BRIDGE; r->mode = fixed_rules ? MR_MODE_DOJO : MR_MODE_MANO;
}

/* ---- the accessors every policy site uses (a NULL record = nothing of ours) ---- */
static __inline__ int ms_rules_ours(const struct ms_match_rules *r) { return r && r->route == MR_ROUTE_OUR_SERVER && r->mode != MR_MODE_NONE; }
static __inline__ int ms_rules_slippi(const struct ms_match_rules *r) { return r && r->route == MR_ROUTE_SLIPPI_BRIDGE; }
/* INVARIANT (checked by ): pause and flags are non-zero ONLY in a record of OUR route; every other writer leaves them 0. So the two accessors need no route test. */
static __inline__ int ms_rules_pause(const struct ms_match_rules *r) { return r ? (int)r->pause : (int)MR_PAUSE_UNTOUCHED; }
static __inline__ int ms_rules_flag(const struct ms_match_rules *r, unsigned int f) { return r && (r->flags & f) != 0u; }   /* every flag is OUR route's: false for the Slippi route and for no match */
/* a Direct (connect-code) match on either route: Slippi's isSlippiChatEnabled ("Direct only" chat) and the like follow the KIND of the match, not who runs it */
/* the Pokemon Stadium policy the setup block takes from the record: 0 the stage chooser's own toggle, 1 transforms (S7R_ALT_OFF), 2 frozen for everybody (S7R_ALT_ON); static-asserted against s7_core.h in s12_gc.c */
static __inline__ unsigned int ms_rules_alt(const struct ms_match_rules *r) { return !r ? 0u : (r->flags & MR_F_STADIUM_FROZEN) ? 2u : (r->flags & MR_F_STADIUM_OFF) ? 1u : 0u; }
static __inline__ int ms_rules_is_direct(const struct ms_match_rules *r) { return r && (r->route == MR_ROUTE_OUR_SERVER || r->route == MR_ROUTE_SLIPPI_BRIDGE) && r->mode == MR_MODE_MANO; }
static __inline__ int ms_rules_mode(const struct ms_match_rules *r) { return ms_rules_ours(r) ? (int)r->mode : (int)MR_MODE_NONE; }

/* SEARCH-TIME question ("would a match of this requested mode have flag f?", before any match exists: the spectate uploader arms during the search): the SAME mode table, no second copy */
static __inline__ int ms_search_flag(int mnc_mode, unsigned int f) { return (ms_mode_entry(ms_mode_of_mnc(mnc_mode)) & f) != 0u; }

/* ---- FROZEN FINAL DESTINATION: the FD background does not transform.  A LOCAL screen option: Slippi's DesyncProofBGTransformations restores the RNG seed
   around the background think, so nothing of the match depends on it and nothing is synced.  The engine writes the fd-freeze switch word (0x8021AAE0 `mr r3,r30` <-> `b +8`) at match
   start (s12_gc.c fd_step).  Frozen when: the stage is FD, the match is not a Slippi bridge match (Slippi's rules, nothing of ours changes there), and the Z toggle of the stage select is on
   or FD came up by a random pick (nobody picked, the core derived the stage: Z can never be pressed on it). ---- */
#define MFD_GRKIND_FD 0x25u           /* in-game stage ground kind (Gr_Kind_Last, word 0x8049E6C8 + 0x88); the external stage id is 0x20 */
#define MFD_ICON_FD 25u               /* stage select icon of Final Destination (mnStageSel_803F06D0[25].stkind = 0x20) */
#define MFD_WHY_OFF 0u
#define MFD_WHY_TOGGLE 1u
#define MFD_WHY_RANDOM 2u
#define MFD_SWITCH_ADDR 0x8021AAE0u   /* the fd-freeze switch word */
#define MFD_WORD_ON 0x48000008u       /* b +8: skips `mr r3,r30` and the (DesyncProof-hooked) call; the off word is read from the live retail word, never typed here */
#define MFD_GRKIND_PS 0x10u           /* in-game stage ground kind of Pokemon Stadium (Gr_Kind_PStadium) */
#define MFD_INJ_PS 0x801D457Cu        /* PokemonStadium_TransformationDecide: the `b` of Slippi's IngameCheckIfFrozen (INJ_FREEZE_STADIUM); its body + 8 is the Frozen Pokemon Stadium toggle byte (1 = frozen) */
#define MFD_INJ_CURSOR 0x8025A530u    /* SSS_CursorThink: the `b` of the cursor C2.  With flag ms-fd-toggle that body is asm/Sss/CursorOnHoverFd.asm (FD toggle byte at +8, magic MFD_MAGIC at +16); without it the body is Slippi's CursorOnHoverStadium, which has no such magic */
#define MFD_MAGIC 0x4D534644u         /* 'MSFD' */
static __inline__ unsigned int mfd_decide(unsigned int grkind, unsigned int toggle, unsigned int random_pick, int slippi_route)
{
    if (slippi_route || grkind != MFD_GRKIND_FD) return MFD_WHY_OFF;
    if (random_pick) return MFD_WHY_RANDOM;
    return toggle ? MFD_WHY_TOGGLE : MFD_WHY_OFF;
}
/* the body address of the cursor C2 from the instruction at MFD_INJ_CURSOR (a plain `b`, like Slippi's computeBranchTargetAddress); 0 when it is not one.  The toggle byte is body+8, valid only when the word at body+16 is MFD_MAGIC */
static __inline__ unsigned int mfd_body_addr(unsigned int site, unsigned int word)
{
    int off;
    if ((word >> 26) != 18u || (word & 3u) != 0u) return 0u;
    off = (int)((word & 0x03FFFFFCu) << 6) >> 6;        /* LI sign-extended (AA and LK are clear) */
    return site + (unsigned int)off;
}
/* what the match-start notice says: 1 = Frozen Final Destination (the engine froze the background: fd_on = mfd_decide's answer), 2 = Frozen Pokemon Stadium (the toggle byte is set,
   never on the Slippi route), 0 = nothing to say */
static __inline__ unsigned int mfd_kind(unsigned int grkind, unsigned int fd_on, unsigned int ps_toggle, int slippi_route)
{
    if (fd_on) return 1u;
    return (grkind == MFD_GRKIND_PS && ps_toggle && !slippi_route) ? 2u : 0u;
}
/* the scene of a match: Slippi's online scene (major 8) or the offline Vs Melee scene (major 2), minor 2, nothing loading.  The offline major was missing until fd_step never ran in Couch / Vs Melee */
static __inline__ int mfd_scene(unsigned int scene_word, unsigned int loading)
{
    unsigned int m = scene_word >> 24;
    return (m == 8u || m == 2u) && (scene_word & 0xFFu) == 2u && loading == 0u;
}

#endif
