/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Music = Random: the menu-slot draw (Random changes the MENU music only, but may pick any of 38 songs: the 3 menu songs and the 35 stage themes; stage slots are never written).  Pure C, host-tested (tests/music_test.c).
 *
 * Melee finds a track's file name through the table `hps_files` (98 pointers to the file names on the DISC, 0x803BC314, retail GALE01 v1.02): lbAudioAx_80023F28(idx) copies
 * hps_files[idx] and streams that file (a track whose name equals the one already playing is not restarted).  The menu scenes ask for one of the three menu indices.  We never ship a name or any audio:
 * a draw only COPIES pointers the game itself put in the table (menu or stage slots) into the three menu slots, so a menu scene then plays another song; stage slots are never written.
 * Local only: the seed is the time base, the game's RNG is never read or advanced.
 */
#ifndef ML_MUSIC_H
#define ML_MUSIC_H

#define ML_MUSIC_TAB 0x803BC314u        /* hps_files[0] (symbols.txt has it inside lbAudioAx's static data; lbAudioAx_80023F28 reads r31+0x1014 with r31 = 0x803BB300) */
#define ML_MUSIC_N 98u                  /* entries; lbAudioAx_80023F28 refuses idx >= 0x62 */
#define ML_MUSIC_PTR_LO 0x80000000u     /* a pointer outside MEM1 means the table is not what we expect: write nothing */
#define ML_MUSIC_PTR_HI 0x81800000u

/* The three MENU slots (indices into hps_files; by file name menu01 menu02 menu3): the only slots Random ever writes.  The CANDIDATES (more variation) are the three menu
   songs plus the full-length VS stage themes.  POOL AUDIT (Wii "sound effects as background music";, table from the stock 1.02 disc
   by an internal design note): a candidate must LOOP and be at least 45 s long with a loop of at least 30 s.  Cut from the 38 of 1286..1293:
   85 vl_battle (8.8 s), 86 vl_castle (8.1 s), 87 vl_corneria (12.7 s), 91 vl_fzero (4.8 s), 92 vl_last_v2 (9.3 s) = one-shot jingles, they never loop (the "sound effects");
   44 inis2_02 (11.4 s, loop 9.6 s) and 42 inis1_02 (39.2 s) = short variants.  31 candidates remain (castle rcruise kongo garden greatbay shrine zebes kraid ystory yorster izumi greens
   corneria venom pstadium pokesta mutecity bigblue onetto onetto2 fourside icemt inis1_01 inis2_01 flatzone old_kb old_ys old_dk + menu01 menu02 menu3), all loop and exist on a stock disc.
   ONE song per draw (entering Dojo, Grandmaster, Mano a mano or Couch changed the song): a draw writes the SAME candidate pointer into all three menu slots, so
   whatever menu index a scene asks for (the Main Menu, a character screen, Slippi's online character screen) it finds the name that is playing and lbAudioAx_80023F28 does not restart.
   A stage slot is never written: a stage plays its own track under every setting; a menu slot only receives a COPY of a candidate's pointer.
   The canonical pointers of the menu slots are not stored anywhere (a menu slot may hold a stage song's pointer, and the menu overlay reloads on every Main Menu entry, so no static survives):
   they are recomputed from the untouched slot 51 (kraid): the retail image lays the file names out in table order, 12 bytes each (name, NUL, padding to 4), so menu01 = tab[51] + 12, menu02 + 24,
   menu3 + 36, and tab[55] (mrider) must be tab[51] + 48; anything else = not the table we know = nothing is written. */
#define ML_MUSIC_MENU_N 3u
static const unsigned char ml_music_menu[ML_MUSIC_MENU_N] = { 52, 53, 54 };
#define ML_MUSIC_ANCHOR 51u            /* kraid.hps, a stage slot (never written) */
#define ML_MUSIC_ANCHOR_END 55u        /* mrider.hps, the first name after menu3 */
#define ML_MUSIC_STEP 12u
#define ML_MUSIC_POOL_N 31u
static const unsigned char ml_music_pool[ML_MUSIC_POOL_N] = {
    52, 53, 54,
    3, 4, 6, 30, 31, 33, 34, 35, 40, 41, 43, 49, 50, 51, 56, 57, 58, 59, 60, 61, 63, 64, 66, 75, 84, 95, 96, 97
};

/* WHEN: a menu-scene entry draws only on the first entry since power-on or the first entry after a match was played (the engine's
   'MPLY' mark); every other entry keeps the playing song.  Switching Random on in Settings draws from the frame (ml_glue.c music_pool_sync). */
#define ML_MUSIC_PLAYED_MARK 0x4D504C59u
static int ml_music_entry_draws(unsigned first_since_power_on, unsigned played_mark)
{
    return first_since_power_on || played_mark == ML_MUSIC_PLAYED_MARK;
}

/* xorshift32 step (never 0 in, never 0 out) */
static unsigned ml_music_next(unsigned x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* tab = the 98-entry pointer table (RAM on the console, an array on the host); mode 0 = canonical menu pointers (On, Off), 1 = a draw with `seed` (Random): ONE candidate, uniform over the
   31, written into all three menu slots.  Returns 1 when the table was written, 0 when it was left alone (a pointer that is not a MEM1 address, or the table is not the known layout). */
static int ml_music_apply(volatile unsigned *tab, unsigned mode, unsigned seed)
{
    unsigned v[ML_MUSIC_POOL_N], a, i, x;
    a = tab[ML_MUSIC_ANCHOR];
    if (a < ML_MUSIC_PTR_LO || a >= ML_MUSIC_PTR_HI - 64u || tab[ML_MUSIC_ANCHOR_END] != a + 4u * ML_MUSIC_STEP)
        return 0;
    for (i = 0; i < ML_MUSIC_POOL_N; i++) {
        v[i] = i < ML_MUSIC_MENU_N ? a + (i + 1u) * ML_MUSIC_STEP : tab[ml_music_pool[i]];
        if (v[i] < ML_MUSIC_PTR_LO || v[i] >= ML_MUSIC_PTR_HI)
            return 0;
    }
    x = ml_music_next(seed ? seed : 0x9E3779B9u);
    x = v[(x >> 8) % ML_MUSIC_POOL_N];                    /* (x >> 8 < 2^24: the modulo bias over 31 is below 2e-6) */
    for (i = 0; i < ML_MUSIC_MENU_N; i++)
        tab[ml_music_menu[i]] = mode ? x : a + (i + 1u) * ML_MUSIC_STEP;
    return 1;
}

#endif
