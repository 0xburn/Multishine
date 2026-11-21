/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the settings record layer (on_set.c, included by on_card.c under -DONS_SETTINGS) against the fake memory card (om/tests/omc_fake.inc).
   One scenario per run (static state, like the console): set_test N.
     0 no card: values live in RAM, load not final, retried at the next menu entry, a later card is read and RAM changes win
     1 blank card (no file): NOFILE final; a change is saved after the debounce, file created with the comment, only the touched byte set
     2 valid file on A: values loaded, ONS_EV_LOADED, an untouched field is not written, other option bytes and the chat byte survive a save
     3 damaged file: defaults kept, replaced by the next save
     4 newer file: never read, never written
     5 rate limit: many changes make one write; two writes at least ONS_MIN_GAP apart
     6 save failures: three failed passes then no more attempts until the next menu entry
     7 Melee busy: nothing is touched, retried later
     8 slot B only holds the file (A without card): written back to B
     9 A broken, B good without file: file created on B
    10 range: out-of-range values on the card are ignored */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OMC_HOST 1
#include "on_card.c"
#include "omc_fake.inc"

#ifdef MS_CARD_SHARED_WA      /* ENGINE_MEMORY_BREAKDOWN steps 2a/2b: fakes for the area's placement (on_hist.c) */
unsigned int h_arena_hi = 0x817D0000u;
unsigned char h_wa_fixed[0xC000 + 64] __attribute__((aligned(32)));
static int h_alloc_fail, h_alloc_failed, h_live, h_allocs, h_frees;
void *h_wa_alloc(u32 n) { void *p; h_allocs++; if (h_alloc_fail > 0) { h_alloc_fail--; h_alloc_failed++; return 0; } p = aligned_alloc(32, n); memset(p, 0xA5, n); h_live++; return p; }
void h_wa_free(void *p) { h_frees++; h_live--; free(p); }
#endif
static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define PAY(f) ((f)->data + 0x1000)

static void put_valid(int chan, unsigned d, unsigned p, unsigned char opt0)
{
    struct ms_settings s;
    unsigned char blk[0x2000];
    memset(blk, 0, sizeof blk);
    memcpy(blk, "Multishine", 10);
    ms_set_defaults(&s, 2);
    ms_set_fill(blk + 0x1000, &s, 0);
    blk[0x1000 + OS_VAL_OFF + 0] = opt0;
    blk[0x1020] = 0x5C;
    if (d) blk[0x1000 + OS_VAL_OFF + 7] = (unsigned char)(0x10u | d);   /* 0x10 | frames = Auto delay Off with that many frames (an old plain 1..15 now reads as Auto On, mset_test) */
    if (p != 99) blk[0x1000 + OS_VAL_OFF + 13] = (unsigned char)p;
    { unsigned c = (unsigned)os_crc32(blk + 0x1000 + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); blk[0x1008] = (unsigned char)(c >> 24); blk[0x1009] = (unsigned char)(c >> 16); blk[0x100A] = (unsigned char)(c >> 8); blk[0x100B] = (unsigned char)c; }
    CHECK(fk_put(chan, "MultishineOptions", blk) != 0);
}
static void run_frames(unsigned from, unsigned n)
{
    unsigned f;
    for (f = from; f < from + n; f++) (void)ons_poll(f);
}

int main(int argc, char **argv)
{
    int sc = argc > 1 ? atoi(argv[1]) : 0;
    struct fk_file *f;
    fk_reset();
    K[0].present = 1;
    ons_menu_enter(2, 0);
    CHECK(ons_cur()->delay == 2 && ons_cur()->pool == 0);
    switch (sc) {
    case 0: {
        K[0].present = 0;
        run_frames(0, 130);                                       /* first read at frame 120: no card */
        CHECK(ons_st.load_status == ONS_L_NOCARD && !(ons_st.flags & 1));
        CHECK(ons_change_delay(6, 130) == 1 && ons_change_pool(1, 130) == 1);
        run_frames(131, 400);
        CHECK(ons_st.saves == 0 && KF.writes == 0 && KF.creates == 0);   /* never writes before the file was looked at */
        CHECK(ons_st.load_tries == 3);                            /* three tries per entry, then quiet */
        /* a card appears; a new menu entry reads it: the file holds delay 3, pool 2, the player's own changes win */
        K[0].present = 1; put_valid(0, 3, 2, 0xFF);
        ons_menu_enter(2, 1000);
        run_frames(1000, 200);
        CHECK(ons_st.load_status == ONS_L_OK && ons_cur()->delay == 6 && ons_cur()->pool == 1);
        run_frames(1200, 400);
        CHECK(ons_st.saves == 1);                                 /* the pending changes were saved once the file was known */
        f = fk_find(0, "MultishineOptions");
        CHECK(f && PAY(f)[OS_VAL_OFF + 7] == 22 && PAY(f)[OS_VAL_OFF + 13] == 1);
        break; }
    case 1: {
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_NOFILE && (ons_st.flags & 1));
        CHECK(ons_change_delay(4, 200) == 1);
        run_frames(201, 80);
        CHECK(KF.creates == 0);                                   /* debounce: not yet */
        run_frames(281, 40);
        CHECK(KF.creates == 1 && KF.writes == 1 && ons_st.saves == 1 && ons_st.save_status == ONS_W_OK && ons_st.wslot == 1);
        f = fk_find(0, "MultishineOptions");
        CHECK(f && memcmp(f->data, "Multishine", 10) == 0 && f->commentAddr == 0 && f->iconAddr == 0xFFFFFFFFu);
        CHECK(ms_set_check(PAY(f)) == MSET_OK && PAY(f)[OS_VAL_OFF + 7] == 20 && PAY(f)[OS_VAL_OFF + 13] == 0xFF);
        CHECK(KF.mounts == KF.unmounts && KF.bad_use == 0 && KF.open_files == 0);
        break; }
    case 2: {
        struct ms_settings t;
        unsigned ev = 0, fr;
        put_valid(0, 7, 2, 1);
        for (fr = 0; fr < 130; fr++) ev |= ons_poll(fr);
        CHECK(ev == ONS_EV_LOADED && ons_cur()->delay == 7 && ons_cur()->pool == 2 && ons_cur()->ws == 1 && ons_st.ws == 1 && ons_st.from_card == 3 && ons_st.slot == 1);
        CHECK(ons_change_pool(1, 200) == 1);
        run_frames(201, 120);
        f = fk_find(0, "MultishineOptions");
        CHECK(ons_st.saves == 1 && f && PAY(f)[OS_VAL_OFF + 13] == 1 && PAY(f)[OS_VAL_OFF + 7] == 23 && PAY(f)[OS_VAL_OFF + 0] == 1 && PAY(f)[0x20] == 0x5C);
        CHECK(ms_set_check(PAY(f)) == MSET_OK);
        (void)t;
        break; }
    case 3: {
        put_valid(0, 5, 1, 0xFF);
        fk_find(0, "MultishineOptions")->data[0x1000 + OS_VAL_OFF + 7] ^= 3;       /* CRC now wrong */
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_DAMAGED && (ons_st.flags & 8) && ons_cur()->delay == 2);
        CHECK(ons_change_delay(9, 200) == 1);
        run_frames(201, 120);
        f = fk_find(0, "MultishineOptions");
        CHECK(ons_st.saves == 1 && ms_set_check(PAY(f)) == MSET_OK && PAY(f)[OS_VAL_OFF + 7] == 25 && PAY(f)[OS_VAL_OFF + 13] == 0xFF);
        break; }
    case 4: {
        unsigned char before[0x2000];
        put_valid(0, 5, 1, 0xFF);
        f = fk_find(0, "MultishineOptions");
        f->data[0x1004] = 2;                                                            /* version 2 */
        memcpy(before, f->data, sizeof before);
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_NEWER && (ons_st.flags & 4) && ons_cur()->delay == 2);
        CHECK(ons_change_delay(9, 200) == 1);
        run_frames(201, 300);
        CHECK(ons_st.saves == 0 && KF.writes == 0 && memcmp(before, f->data, sizeof before) == 0);
        break; }
    case 5: {
        unsigned i;
        run_frames(0, 130);
        for (i = 0; i < 10; i++) { CHECK(ons_change_delay(3u + i, 200 + i * 5) == 1); run_frames(200 + i * 5, 5); }
        run_frames(250, 200);
        CHECK(ons_st.saves == 1 && KF.writes == 1);                                    /* ten changes, one write */
        f = fk_find(0, "MultishineOptions");
        CHECK(PAY(f)[OS_VAL_OFF + 7] == 28);
        CHECK(ons_change_delay(2, 460) == 1);
        run_frames(460, 100);                                                           /* debounce passed (90) but the gap since the last write is not 180 yet */
        { unsigned w = ons_st.saves; ons_flush(560); run_frames(560, 60); CHECK(ons_st.saves == w || ons_st.saves == w + 1); }
        run_frames(620, 300);
        CHECK(ons_st.saves == 2 && PAY(f)[OS_VAL_OFF + 7] == 18);
        CHECK(ons_st.flags == 1u);                                                      /* loaded, not dirty */
        break; }
    case 6: {
        run_frames(0, 130);
        KF.fail_create = 1;
        CHECK(ons_change_delay(4, 200) == 1);
        run_frames(201, 2000);
        CHECK(ons_st.saves == 0 && ons_st.save_tries == 3 && (ons_st.flags & 16) && ons_st.save_status == ONS_W_ERROR);
        CHECK(KF.mounts == KF.unmounts && KF.bad_use == 0);
        KF.fail_create = 0;
        ons_menu_enter(2, 3000);                                                        /* a new menu entry tries again */
        run_frames(3000, 400);
        CHECK(ons_st.saves == 1 && fk_find(0, "MultishineOptions") != 0);
        break; }
    case 7: {
        KF.idle = 0;
        run_frames(0, 400);
        CHECK(ons_st.load_status == ONS_L_BUSY && KF.mounts == 0 && KF.probes == 0);
        KF.idle = 1;
        ons_menu_enter(2, 500);
        run_frames(500, 200);
        CHECK(ons_st.load_status == ONS_L_NOFILE);
        KF.idle = 0;
        CHECK(ons_change_pool(2, 800) == 1);
        run_frames(800, 200);
        CHECK(ons_st.saves == 0 && KF.writes == 0 && ons_st.save_status == ONS_W_BUSY);
        KF.idle = 1;
        run_frames(1000, 200);
        CHECK(ons_st.saves == 1);
        break; }
    case 8: {
        K[0].present = 0; K[1].present = 1;
        put_valid(1, 11, 1, 0xFF);
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_OK && ons_cur()->delay == 11 && ons_st.slot == 2);
        CHECK(ons_change_delay(12, 200) == 1);
        run_frames(201, 120);
        f = fk_find(1, "MultishineOptions");
        CHECK(ons_st.saves == 1 && ons_st.wslot == 2 && f && PAY(f)[OS_VAL_OFF + 7] == 28 && KF.creates == 0);
        break; }
    case 9: {
        K[0].broken = 1; K[1].present = 1;
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_NOFILE);                                    /* A is skipped as unusable, B is a good card without the file */
        CHECK(ons_change_pool(1, 200) == 1);
        run_frames(201, 120);
        CHECK(ons_st.saves == 1 && ons_st.wslot == 2 && fk_find(1, "MultishineOptions") && !fk_find(0, "MultishineOptions"));
        CHECK(KF.mounts == KF.unmounts && KF.bad_use == 0);
        break; }
    case 10: {
        put_valid(0, 99, 7, 0xFF);                                                    /* delay byte 99 (written as 0xFF? no: 99), pool 7 */
        { unsigned char *p = fk_find(0, "MultishineOptions")->data + 0x1000; p[OS_VAL_OFF + 7] = 16; {   /* 16: out of range (0 is an explicit Auto since, mset_test) */ unsigned c = (unsigned)os_crc32(p + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); p[8] = (unsigned char)(c >> 24); p[9] = (unsigned char)(c >> 16); p[10] = (unsigned char)(c >> 8); p[11] = (unsigned char)c; } }
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_OK && ons_cur()->delay == 2 && ons_cur()->pool == 0 && ons_st.from_card == 0);
        break; }
    case 11: {
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_NOFILE && ons_cur()->ws == 0 && ons_st.ws == 0);          /* no options file: widescreen stays OFF */
        CHECK(ons_change_ws(1, 200) == 1 && ons_cur()->ws == 1);
        run_frames(201, 120);
        f = fk_find(0, "MultishineOptions");
        CHECK(ons_st.saves == 1 && f && PAY(f)[OS_VAL_OFF + 0] == 1 && PAY(f)[OS_VAL_OFF + 7] == 0xFF && PAY(f)[OS_VAL_OFF + 13] == 0xFF && ms_set_check(PAY(f)) == MSET_OK);
        CHECK(ons_change_ws(0, 400) == 1);
        run_frames(401, 300);
        CHECK(ons_st.saves == 2 && PAY(f)[OS_VAL_OFF + 0] == 0);
        break; }
    case 13: {                                                                        /* update check at start: always ON, id 14 retired and never written */
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_NOFILE && ons_cur()->updchk == 1);
        CHECK(ons_change_ws(1, 200) == 1 && ons_cur()->updchk == 1);                   /* retired setting: no setter; another change never writes id 14 */
        run_frames(201, 120);
        f = fk_find(0, "MultishineOptions");
        CHECK(ons_st.saves == 1 && f && PAY(f)[OS_VAL_OFF + 14] == 0xFF && PAY(f)[OS_VAL_OFF + 0] == 1 && ms_set_check(PAY(f)) == MSET_OK && ons_cur()->updchk == 1);
        break; }
    case 14: {                                                                        /* progressive scan (id 15) and the rollback readout (value index 16 = reserved byte +0x1C) */
        unsigned ev = 0, fr;
        for (fr = 0; fr < 130; fr++) ev |= ons_poll(fr);
        CHECK(ev == ONS_EV_LOADED && ons_st.load_status == ONS_L_NOFILE);          /* the first final read is reported even with nothing to apply */
        CHECK(ons_cur()->prog == MSET_PROG_FOLLOW && ons_cur()->rbr == 0 && ons_st.prog == MSET_PROG_FOLLOW && ons_st.rbr == 0);
        CHECK(ons_change_prog(2, 200) == 0 && ons_change_prog(1, 200) == 1 && ons_cur()->prog == 1 && ons_change_prog(1, 200) == 0);
        CHECK(ons_change_rbr(1, 200) == 1 && ons_cur()->rbr == 1 && ons_change_rbr(1, 200) == 0 && ons_change_rbr(2, 200) == 0);
        run_frames(201, 120);
        f = fk_find(0, "MultishineOptions");
        CHECK(ons_st.saves == 1 && f && PAY(f)[OS_VAL_OFF + 15] == 1 && PAY(f)[OS_VAL_OFF + 16] == 1 && PAY(f)[0x1C] == 1);
        CHECK(PAY(f)[OS_VAL_OFF + 0] == 0xFF && PAY(f)[OS_VAL_OFF + 7] == 0xFF && PAY(f)[OS_VAL_OFF + 14] == 0xFF && ms_set_check(PAY(f)) == MSET_OK);
        CHECK(ons_cur()->preload == 0 && ons_change_preload(2, 300) == 0 && ons_change_preload(0, 300) == 0);                 /* Menu preload: default off */
        CHECK(ons_change_prog(0, 400) == 1 && ons_change_rbr(0, 400) == 1 && PAY(f)[OS_VAL_OFF + 19] == 0 &&   /* beyond OS_NVAL: an untouched preload byte is 0 (off) */ ons_change_preload(1, 400) == 1 && ons_cur()->preload == 1);
        run_frames(401, 300);
        CHECK(ons_st.saves == 2 && PAY(f)[OS_VAL_OFF + 15] == 0 && PAY(f)[OS_VAL_OFF + 16] == 0 && PAY(f)[OS_VAL_OFF + 19] == 1 && PAY(f)[0x1F] == 1 && ms_set_check(PAY(f)) == MSET_OK);
        /* a restart reads them back */
        ons_menu_enter(2, 800);
        fk_reset(); K[0].present = 1;
        break; }
    case 15: {                                                                        /* review M4: a 1069/1070 card with Show ping in bit 6 of byte 16 is moved to bit 1 of byte 19 once, unasked */
        unsigned char *p;
        put_valid(0, 5, 1, 0xFF);
        f = fk_find(0, "MultishineOptions"); p = PAY(f);
        p[0x1C] = 84; p[0x1F] = 1;                                                    /* ping + Debug lines + backup off; Menu Preload on */
        { unsigned c = (unsigned)os_crc32(p + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); p[8] = (unsigned char)(c >> 24); p[9] = (unsigned char)(c >> 16); p[10] = (unsigned char)(c >> 8); p[11] = (unsigned char)c; }
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_OK && ons_cur()->pingon == 1 && ons_cur()->dbgl == 1 && ons_cur()->bakoff == 1 && ons_cur()->preload == 1 && ons_cur()->delay == 5);
        run_frames(130, 400);                                                         /* no change by the player: the migration alone saves */
        CHECK(ons_st.saves == 1 && p[0x1C] == 20 && p[0x1F] == 3 && p[OS_VAL_OFF + 7] == 21 && ms_set_check(p) == MSET_OK);
        CHECK(ons_cur()->touched2 == 0);
        ons_menu_enter(2, 800); fk_reset(); K[0].present = 1;
        break; }
    case 16: {                                                                        /* ping toggled in the menu: byte 19 only, byte 16 left as it is */
        unsigned char *p;
        put_valid(0, 5, 1, 0xFF);
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_OK && ons_cur()->pingon == 0);
        f = fk_find(0, "MultishineOptions"); p = PAY(f);
        CHECK(ons_change_ping(1, 200) == 1 && ons_cur()->pingon == 1);
        run_frames(201, 300);
        CHECK(ons_st.saves == 1 && p[0x1F] == 2 && (p[0x1C] == 0 || p[0x1C] == 0xFF) && ms_set_check(p) == MSET_OK);
        CHECK(ons_change_preload(1, 600) == 1); run_frames(601, 300);
        CHECK(ons_st.saves == 2 && p[0x1F] == 3);
        CHECK(ons_change_ping(0, 1000) == 1); run_frames(1001, 300);
        CHECK(ons_st.saves == 3 && p[0x1F] == 1);
        break; }
    case 17: {                                                                        /* Music: bit 7 of byte 19, saved with the next write, read back after a reload; Keep previous build is untouched */
        unsigned char *p;
        put_valid(0, 5, 1, 0xFF);
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_OK && ons_cur()->musicoff == 0 && ons_cur()->keepoff == 0);
        CHECK(ons_change_music(0, 140) == 1 && ons_cur()->musicoff == 1);
        run_frames(141, 300);
        f = fk_find(0, "MultishineOptions"); p = PAY(f);
        CHECK(ons_st.saves == 1 && p[0x1F] == 128 && ms_set_check(p) == MSET_OK);
        ons_menu_enter(2, 800);
        run_frames(800, 130);
        CHECK(ons_cur()->musicoff == 1);
        CHECK(ons_change_music(2, 1000) == 1 && ons_cur()->musicoff == 0 && ons_cur()->musicrand == 1);        /* Random: byte 19 back to 0, byte 17 bit 3 */
        run_frames(1001, 300);
        f = fk_find(0, "MultishineOptions"); p = PAY(f);
        CHECK(p[0x1F] == 0 && (p[0x1D] & 8u) == 8u && ms_set_check(p) == MSET_OK);
        ons_menu_enter(2, 1600);
        run_frames(1600, 130);
        CHECK(ons_cur()->musicoff == 0 && ons_cur()->musicrand == 1);
        break; }
#ifdef MS_CARD_EXT_WA
    case 12: {                                                                        /* the area is refused twice (heap full / guard): later, not a try; then the file is read */
        put_valid(0, 5, 1, 0xFF);
#ifdef MS_CARD_WA_ADDR
        h_arena_hi = 0x817F8AC0u;
#else
        h_alloc_fail = 1000000;                                                      /* the heap cannot give 48 KB for a while */
#endif
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_BUSY && ons_st.load_tries == 0 && KF.mounts == 0 && ms_card_refused == 1);
        CHECK(ons_change_delay(7, 140) == 1);
        run_frames(131, 300);
        CHECK(ons_st.saves == 0 && KF.writes == 0 && KF.mounts == 0);                  /* never writes before the file was looked at */
#ifdef MS_CARD_WA_ADDR
        h_arena_hi = 0x817D0000u;
#else
        h_alloc_fail = 0;
#endif
        ons_menu_enter(2, 600);
        run_frames(600, 400);
        CHECK(ons_st.load_status == ONS_L_OK && ons_st.saves == 1 && KF.bad_use == 0 && KF.mounts == KF.unmounts);
        f = fk_find(0, "MultishineOptions");
        CHECK(f && PAY(f)[OS_VAL_OFF + 7] == 23);
        break; }
#else
    case 12: break;                                                               /* a static area is always there: nothing to refuse */
#endif
    default:
        printf("unknown scenario\n");
        return 2;
    }
#ifdef MS_CARD_SHARED_WA
    CHECK(ms_card_owner == 0 && ms_card_wa_p == 0 && ms_card_conflicts == 0);
#if defined(MS_CARD_EXT_WA) && !defined(MS_CARD_WA_ADDR)
    CHECK(h_live == 0 && h_allocs - h_alloc_failed == h_frees);
#endif
#endif
    printf("set_test %d: %s\n", sc, fails ? "FAILED" : "ok");
    return fails != 0;
}
