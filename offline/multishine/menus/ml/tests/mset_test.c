/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of ml_set.c: defaults, decode ranges, touched-only fill, keep rules, CRC, round trip through the SDOF checker.  mset_test (no arguments). */
#include <stdio.h>
#include <string.h>
#include "../ml_set.h"
#include "optstore.h"

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)

static void blank(unsigned char *p)
{
    struct ms_settings z;
    ms_set_defaults(&z, 2);
    memset(p, 0xAB, OS_PAYLOAD_SIZE);
    ms_set_fill(p, &z, 0);
}

struct fakeram_t { unsigned a[3], w[3]; unsigned n; };
static unsigned fr_rd(void *c, unsigned a) { struct fakeram_t *r = (struct fakeram_t *) c; unsigned i; for (i = 0; i < 3; i++) if (r->a[i] == a) return r->w[i]; return 0xDEADu; }
static void fr_wr(void *c, unsigned a, unsigned w) { struct fakeram_t *r = (struct fakeram_t *) c; unsigned i; for (i = 0; i < 3; i++) if (r->a[i] == a) r->w[i] = w; r->n++; }

int main(void)
{
    unsigned char p[OS_PAYLOAD_SIZE], q[OS_PAYLOAD_SIZE];
    struct ms_settings s, t, u;
    int i;
    ms_set_defaults(&s, 2);
    CHECK(s.delay == 2 && s.pool == 0 && s.ws == 0 && s.touched == 0);
    ms_set_defaults(&t, 0);
    CHECK(t.delay == MSET_DELAY_AUTO);                /* 0 = Auto, the default since */
    ms_set_defaults(&t, 99);
    CHECK(t.delay == MSET_DELAY_AUTO);                /* a bad default falls back to Auto */
    /* a fresh record: all value bytes 0xFF, valid, nothing decoded */
    blank(p);
    CHECK(ms_set_check(p) == MSET_OK);
    for (i = 0; i < OS_NVAL; i++) CHECK(p[OS_VAL_OFF + i] == 0xFF);
    CHECK(ms_set_decode(&s, p) == 0 && s.delay == 2 && s.pool == 0 && s.from_card == 0);
    /* changes */
    CHECK(ms_set_delay(&s, 5) == 1 && s.delay == 5 && (s.touched & MSET_T_DELAY));
    CHECK(ms_set_delay(&s, 5) == 0);
    CHECK(ms_set_delay(&s, 16) == 0 && s.delay == 5);
    CHECK(ms_set_pool(&s, 2) == 1 && s.pool == 2 && (s.touched & MSET_T_POOL));
    CHECK(ms_set_pool(&s, 3) == 0 && ms_set_pool(&s, 2) == 0);
    /* fill touched into a fresh record */
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK);
    CHECK(p[OS_VAL_OFF + 7] == 0x15 && p[OS_VAL_OFF + 13] == 2);
    for (i = 0; i < OS_NVAL; i++) if (i != 7 && i != 13) CHECK(p[OS_VAL_OFF + i] == 0xFF);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 2 && t.delay == 5 && t.pool == 2 && t.from_card == 2 && t.touched == 0 && t.ws == 0);
    /* an untouched value is not written and the other bytes of a kept record stay (widescreen on, a chat table byte, an unknown id 14, flags) */
    memcpy(q, p, sizeof q);
    q[OS_VAL_OFF + 0] = 1; q[OS_VAL_OFF + 14] = 9; q[0x20] = 0x5C; q[6] = 0; q[7] = 1;
    { unsigned long c = os_crc32(q + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); q[8] = (unsigned char) (c >> 24); q[9] = (unsigned char) (c >> 16); q[10] = (unsigned char) (c >> 8); q[11] = (unsigned char) c; }
    CHECK(ms_set_check(q) == MSET_OK);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_delay(&t, 9) == 1);                  /* only the delay is touched */
    ms_set_fill(q, &t, 1);
    CHECK(ms_set_check(q) == MSET_OK);
    CHECK(q[OS_VAL_OFF + 7] == 0x19 && q[OS_VAL_OFF + 13] == 2 && q[OS_VAL_OFF + 0] == 1 && q[OS_VAL_OFF + 0] == 1 && q[OS_VAL_OFF + 14] == 9 && q[0x20] == 0x5C && q[7] == 1);
    /* out of range / unset values on the card keep the default */
    memcpy(q, p, sizeof q);
    q[OS_VAL_OFF + 7] = 16; q[OS_VAL_OFF + 13] = 7;
    ms_set_fill(q, &s, 1);                            /* s has both touched: rewrite them, then break them by hand */
    q[OS_VAL_OFF + 7] = 16; q[OS_VAL_OFF + 13] = 7;
    { unsigned long c = os_crc32(q + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); q[8] = (unsigned char) (c >> 24); q[9] = (unsigned char) (c >> 16); q[10] = (unsigned char) (c >> 8); q[11] = (unsigned char) c; }
    ms_set_defaults(&t, 3);
    CHECK(ms_set_decode(&t, q) == 0 && t.delay == 3 && t.pool == 0);
    /* AUTO DELAY switch: byte 7 = 0x10 | frames is Auto delay OFF with that many frames; an OLD save (a plain 1..15, an explicit 0) reads as Auto delay ON and keeps the
       value as the remembered manual one; 0xFF (never changed) = the default Auto; Off -> On -> Off remembers the manual value; out-of-range bytes (0x10, 0x20+, 0xFE) change nothing */
    {
        unsigned dv;
        for (dv = 1; dv <= 15; dv++) {
            blank(q); q[OS_VAL_OFF + 7] = (unsigned char) dv;
            { unsigned long c = os_crc32(q + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); q[8] = (unsigned char) (c >> 24); q[9] = (unsigned char) (c >> 16); q[10] = (unsigned char) (c >> 8); q[11] = (unsigned char) c; }
            ms_set_defaults(&t, MSET_DELAY_AUTO);
            CHECK(ms_set_decode(&t, q) == 1 && t.delay == MSET_DELAY_AUTO && t.dman == dv);          /* an old explicit value: Auto On, value remembered */
            blank(q); q[OS_VAL_OFF + 7] = (unsigned char) (0x10u | dv);
            { unsigned long c = os_crc32(q + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); q[8] = (unsigned char) (c >> 24); q[9] = (unsigned char) (c >> 16); q[10] = (unsigned char) (c >> 8); q[11] = (unsigned char) c; }
            ms_set_defaults(&t, MSET_DELAY_AUTO);
            CHECK(ms_set_decode(&t, q) == 1 && t.delay == dv && t.dman == dv);                       /* the new Off form */
        }
        blank(q);
        ms_set_defaults(&t, MSET_DELAY_AUTO);
        CHECK(ms_set_decode(&t, q) == 0 && t.delay == MSET_DELAY_AUTO && t.dman == 2);               /* a card that never stored a delay: Auto On */
        { unsigned bad[3] = { 0x10u, 0x20u, 0xFEu }; unsigned k;
          for (k = 0; k < 3; k++) {
              blank(q); q[OS_VAL_OFF + 7] = (unsigned char) bad[k];
              { unsigned long c = os_crc32(q + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); q[8] = (unsigned char) (c >> 24); q[9] = (unsigned char) (c >> 16); q[10] = (unsigned char) (c >> 8); q[11] = (unsigned char) c; }
              ms_set_defaults(&t, MSET_DELAY_AUTO);
              CHECK(ms_set_decode(&t, q) == 0 && t.delay == MSET_DELAY_AUTO && t.dman == 2);
          } }
        ms_set_defaults(&t, 4);                                                                       /* delay 4 = Off, 4 frames */
        CHECK(ms_set_delay(&t, MSET_DELAY_AUTO) == 1 && t.delay == MSET_DELAY_AUTO && t.dman == 4 && (t.touched & MSET_T_DELAY));   /* switched On: the 4 is remembered */
        blank(q); ms_set_fill(q, &t, 1);
        CHECK(ms_set_check(q) == MSET_OK && q[OS_VAL_OFF + 7] == 4);                                  /* On is stored as the remembered value (an old reader plays it as manual 4: downgrade only) */
        ms_set_defaults(&u, 6);
        CHECK(ms_set_decode(&u, q) == 1 && u.delay == MSET_DELAY_AUTO && u.dman == 4);                /* reads back as On whatever the build default */
        CHECK(ms_set_delay(&u, MSET_DELAY_AUTO) == 0);
        CHECK(ms_set_delay(&u, u.dman) == 1 && u.delay == 4);                                         /* switched Off: plays the remembered 4 */
        blank(q); ms_set_fill(q, &u, 1);
        CHECK(q[OS_VAL_OFF + 7] == 0x14);
        CHECK(ms_set_delay(&u, 9) == 1 && u.dman == 9);                                               /* a new manual value replaces the remembered one */
        CHECK(ms_set_delay(&u, MSET_DELAY_AUTO) == 1 && u.dman == 9 && u.delay == 0);
        ms_set_defaults(&u, MSET_DELAY_AUTO); u.touched = MSET_T_DELAY;                               /* On with the default memory: stored 2 */
        blank(q); ms_set_fill(q, &u, 1);
        CHECK(q[OS_VAL_OFF + 7] == 2);
    }
    /* damaged and newer */
    memcpy(q, p, sizeof q);
    q[OS_VAL_OFF + 7] ^= 1;
    CHECK(ms_set_check(q) == MSET_DAMAGED);
    memcpy(q, p, sizeof q);
    q[4] = 2;
    CHECK(ms_set_check(q) == MSET_NEWER);
    memset(q, 0, sizeof q);
    CHECK(ms_set_check(q) == MSET_DAMAGED);
    /* texts: only characters the menu font draws */
    for (i = 0; i < MSET_POOL_N; i++) {
        const char *x = ms_pool_text((unsigned) i);
        while (*x) { CHECK((*x >= 'a' && *x <= 'z') || (*x >= 'A' && *x <= 'Z') || *x == ' '); x++; }
    }
    CHECK(strcmp(ms_pool_text(0), "Multishine and Slippi") == 0);
    /* widescreen: id 0, default off, a card byte 1 turns it on, the file's other bytes stay */
    ms_set_defaults(&s, 2);
    CHECK(ms_set_ws(&s, 1) == 1 && s.ws == 1 && (s.touched & MSET_T_WS) && ms_set_ws(&s, 1) == 0 && ms_set_ws(&s, 2) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[OS_VAL_OFF + 0] == 1 && p[OS_VAL_OFF + 7] == 0xFF && p[OS_VAL_OFF + 13] == 0xFF);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.ws == 1 && t.from_card == 1);
    blank(p);                                                  /* no widescreen byte: stays off */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.ws == 0);
    p[OS_VAL_OFF + 0] = 5;                                     /* out of range: ignored */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.ws == 0);
    /* update check at start: id 14 is RETIRED (mandatory): default ON, no setter, never written; only the bot cards' skip value reads as 0, every other stored byte counts as ON */
    ms_set_defaults(&s, 2);
    CHECK(s.updchk == 1);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[OS_VAL_OFF + 14] == 0xFF && p[OS_VAL_OFF + 0] == 0xFF && p[OS_VAL_OFF + 7] == 0xFF && p[OS_VAL_OFF + 13] == 0xFF && p[OS_VAL_OFF + 15] == 0xFF);     /* the retired byte is left blank */
    p[OS_VAL_OFF + 14] = MSET_UPDCHK_SKIP; ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.updchk == 0 && t.from_card == 1);           /* the skip value (bot cards) turns it off */
    ms_set_fill(p, &s, 1);
    CHECK(p[OS_VAL_OFF + 14] == MSET_UPDCHK_SKIP);                                   /* a save of other settings leaves the retired byte as it was */
    p[OS_VAL_OFF + 14] = 0; ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.updchk == 1);                               /*: a stored Off counts as On (always on for players) */
    blank(p);                                                  /* no byte: ON */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.updchk == 1);
    p[OS_VAL_OFF + 14] = 1; ms_set_defaults(&t, 2); t.updchk = 0;
    CHECK(ms_set_decode(&t, p) == 1 && t.updchk == 1);
    p[OS_VAL_OFF + 14] = 7; ms_set_defaults(&t, 2);            /* out of range: ignored, stays ON */
    CHECK(ms_set_decode(&t, p) == 0 && t.updchk == 1);
    /* progressive scan (id 15): default FOLLOW; 0 / 1 from the card; only 0 / 1 can be set; a touched value is written, FOLLOW never */
    ms_set_defaults(&s, 2);
    CHECK(s.prog == MSET_PROG_FOLLOW && s.rbr == 0 && ms_set_prog(&s, 2) == 0 && ms_set_prog(&s, 0) == 1 && s.prog == 0 && (s.touched & MSET_T_PROG) && ms_set_prog(&s, 0) == 0);
    CHECK(ms_set_prog(&s, 1) == 1 && s.prog == 1);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[OS_VAL_OFF + 15] == 1 && p[OS_VAL_OFF + 0] == 0xFF && p[OS_VAL_OFF + 14] == 0xFF && p[OS_VAL_OFF + 16] == 0);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.prog == 1 && t.rbr == 0);
    p[OS_VAL_OFF + 15] = 0; ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.prog == 0);
    p[OS_VAL_OFF + 15] = 0xFF; ms_set_defaults(&t, 2);                 /* unset: follow the console */
    CHECK(ms_set_decode(&t, p) == 0 && t.prog == MSET_PROG_FOLLOW);
    p[OS_VAL_OFF + 15] = 7; ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.prog == MSET_PROG_FOLLOW);
    /* FOLLOW is never written even when touched bits say so (it cannot be touched: ms_set_prog refuses 2) */
    ms_set_defaults(&s, 2);
    s.touched = MSET_T_PROG;
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(p[OS_VAL_OFF + 15] == 0xFF);
    /* rollback readout: value index 16 = payload +0x1C, 1 on, 0 and 0xFF off, the reserved byte of an older record (0) reads off, the CRC covers it */
    ms_set_defaults(&s, 2);
    CHECK(ms_set_rbr(&s, 0) == 0 && ms_set_rbr(&s, 2) == 0 && ms_set_rbr(&s, 1) == 1 && s.rbr == 1 && (s.touched & MSET_T_RBR) && ms_set_rbr(&s, 1) == 0);
    blank(p);
    CHECK(p[0x1C] == 0);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 1 && p[OS_VAL_OFF + 15] == 0xFF);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.rbr == 1 && t.prog == MSET_PROG_FOLLOW);
    p[0x1C] ^= 1;                                                       /* now 0: CRC no longer matches */
    CHECK(ms_set_check(p) == MSET_DAMAGED);
    ms_set_defaults(&s, 2);
    ms_set_rbr(&s, 1); s.rbr = 0; s.touched = MSET_T_RBR;
    ms_set_fill(p, &s, 1);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 0);
    ms_set_defaults(&t, 2); t.rbr = 1;
    CHECK(ms_set_decode(&t, p) == 0 && t.rbr == 1);                      /* a 0 does not turn a default-off flag anywhere: decode only ever sets 1 */
    p[0x1C] = 0xFF; ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.rbr == 0);
    /* Spectate upload: bit 1 = explicitly off, bit 3 = explicitly ON (wins over a stale bit 1), neither = the default ON; the readout bit stays independent */
    ms_set_defaults(&s, 2);
    CHECK(s.specoff == 0 && s.specexp == 0 && MSET_RBR_BYTE(&s) == 0);                    /* the default is ON and stores nothing */
    CHECK(ms_set_spec(&s, 1) == 0 && ms_set_spec(&s, 2) == 0 && !(s.touched2 & MSET_T2_SPEC) && ms_set_spec(&s, 0) == 1 && s.specoff == 1 && (s.touched2 & MSET_T2_SPEC) && ms_set_spec(&s, 0) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 2 && MSET_RBR_BYTE(&s) == 2);       /* explicit OFF */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.specoff == 1 && t.specexp == 1 && t.rbr == 0);
    ms_set_rbr(&s, 1); ms_set_fill(p, &s, 1);                            /* off + readout: 3 */
    CHECK(p[0x1C] == 3 && ms_set_check(p) == MSET_OK);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 2 && t.specoff == 1 && t.rbr == 1);
    ms_set_defaults(&t, 2); ms_set_rbr(&t, 1); ms_set_fill(p, &t, 1);    /* the readout alone: nothing explicit about the uploader, it stays the default ON */
    CHECK(p[0x1C] == 1);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.specoff == 0 && t.specexp == 0 && t.rbr == 1);
    ms_set_spec(&s, 1); ms_set_fill(p, &s, 1);                           /* switched back on: stored as the explicit ON, bit 3 */
    CHECK(p[0x1C] == 9);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 2 && t.specoff == 0 && t.specexp == 1);
    p[0x1C] = 2; ms_set_defaults(&t, 2); ms_set_decode(&t, p);           /* a card with the OFF switch stays off */
    CHECK(t.specoff == 1 && t.specexp == 1);
    p[0x1C] = 10; ms_set_defaults(&t, 2); ms_set_decode(&t, p);          /* both bits: the explicit ON wins */
    CHECK(t.specoff == 0);
    p[0x1C] = 0xFF; ms_set_defaults(&t, 2);
    ms_set_decode(&t, p); CHECK(t.specoff == 0);                              /* unset: the default, ON */
    p[0x1C] = 0; ms_set_defaults(&t, 2);
    ms_set_decode(&t, p); CHECK(t.specoff == 0 && t.specexp == 0);       /* a card with nothing stored: the default ON (cards that never chose get ON) */
    p[0x1C] = 8; ms_set_defaults(&t, 2); ms_set_decode(&t, p);           /* a readout change keeps the stored explicit ON */
    ms_set_rbr(&t, 1); ms_set_fill(p, &t, 1);
    CHECK(p[0x1C] == 9);
    /* Backup to slot B: bit 2 of the rbr byte = switched OFF, default ON (nothing stored; 0 and 0xFF = ON), independent of bits 0 and 1 */
    ms_set_defaults(&s, 2);
    CHECK(s.bakoff == 0 && ms_set_bak(&s, 1) == 0 && ms_set_bak(&s, 2) == 0 && !(s.touched2 & MSET_T2_BAK) && ms_set_bak(&s, 0) == 1 && s.bakoff == 1 && (s.touched2 & MSET_T2_BAK) && ms_set_bak(&s, 0) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 4 && MSET_RBR_BYTE(&s) == 4);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.bakoff == 1 && t.specoff == 0 && t.rbr == 0);
    ms_set_rbr(&s, 1); ms_set_spec(&s, 0); ms_set_spec(&s, 1); ms_set_fill(p, &s, 1);        /* readout + explicit ON + backup off: 13 */
    CHECK(p[0x1C] == 13 && ms_set_check(p) == MSET_OK);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 3 && t.bakoff == 1 && t.specoff == 0 && t.rbr == 1);
    ms_set_bak(&s, 1); ms_set_fill(p, &s, 1);                            /* backup back on: 9 */
    CHECK(p[0x1C] == 9);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 2 && t.bakoff == 0);
    p[0x1C] = 0xFF; ms_set_defaults(&t, 2);
    ms_set_decode(&t, p); CHECK(t.bakoff == 0 && t.rbr == 0 && t.specoff == 0);   /* unset: backup ON, uploader ON */
    p[0x1C] = 0; ms_set_defaults(&t, 2);
    ms_set_decode(&t, p); CHECK(t.bakoff == 0);                          /* an old card (reserved zero): ON, the default */
    ms_set_defaults(&t, 2);
    CHECK(t.bakoff == 0);                                                /* the default is ON */
    /* Debug lines: bit 4 of the rbr byte, default OFF (0, 0xFF, bits 0..3 only = off), its own touched2 bit, independent of the readout, the uploader and the backup bits */
    ms_set_defaults(&s, 2);
    CHECK(s.dbgl == 0 && ms_set_dbg(&s, 0) == 0 && ms_set_dbg(&s, 2) == 0 && !(s.touched2 & MSET_T2_DBG) && ms_set_dbg(&s, 1) == 1 && s.dbgl == 1 && (s.touched2 & MSET_T2_DBG) && ms_set_dbg(&s, 1) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 16 && MSET_RBR_BYTE(&s) == 16);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.dbgl == 1 && t.rbr == 0 && t.bakoff == 0 && t.specoff == 0);
    ms_set_rbr(&s, 1); ms_set_bak(&s, 0); ms_set_fill(p, &s, 1);         /* readout + backup off + debug lines: 21 */
    CHECK(p[0x1C] == 21);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) >= 3 && t.dbgl == 1 && t.rbr == 1 && t.bakoff == 1);
    ms_set_dbg(&s, 0); ms_set_fill(p, &s, 1);                            /* switched off again: 5 */
    CHECK(p[0x1C] == 5);
    ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.dbgl == 0);
    p[0x1C] = 0xFF; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.dbgl == 0);
    p[0x1C] = 0; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.dbgl == 0);
    /* Show ping (review M4): bit 1 (2) of the preload byte (id 19, payload +0x1F), default OFF, its own touched2 bit; byte 16 never gets bit 6 again */
    ms_set_defaults(&s, 2);
    CHECK(s.pingon == 0 && ms_set_ping(&s, 0) == 0 && ms_set_ping(&s, 2) == 0 && !(s.touched2 & MSET_T2_PING) && ms_set_ping(&s, 1) == 1 && s.pingon == 1 && (s.touched2 & MSET_T2_PING) && ms_set_ping(&s, 1) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1F] == 2 && p[0x1C] == 0 && MSET_RBR_BYTE(&s) == 0);   /* ping alone: byte 16 untouched */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.pingon == 1 && t.preload == 0 && t.dbgl == 0 && t.rbr == 0 && !(t.touched2 & MSET_T2_MIG));
    ms_set_dbg(&s, 1); ms_set_preload(&s, 1); ms_set_fill(p, &s, 1);      /* ping + preload + debug lines: byte 19 = 3, byte 16 = 16 */
    CHECK(p[0x1F] == 3 && p[0x1C] == 16);
    ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.pingon == 1 && t.preload == 1 && t.dbgl == 1);
    CHECK(!(p[0x1F] == 1) && p[0x1C] <= 63);                                   /* an OLDER reader (preload == 1 only, byte 16 <= 63): only Preload reads off, byte 16 is kept */
    ms_set_ping(&s, 0); ms_set_fill(p, &s, 1);                               /* ping off again: byte 19 = 1, an older reader sees Preload on again */
    CHECK(p[0x1F] == 1 && p[0x1C] == 16);
    ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.pingon == 0 && t.preload == 1 && t.dbgl == 1);
    p[0x1F] = 0xFF; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.pingon == 0 && t.preload == 0);
    p[0x1F] = 4; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.pingon == 0 && t.preload == 0);
    p[0x1F] = 0; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.pingon == 0);
    /* Ping and delay position (bits 2-3) and Raise HUD (bits 4-5) of the same byte: defaults 0 / 0, valid 0..2 and 0..3, one touched2 bit, the other bits of the byte survive */
    ms_set_defaults(&s, 2);
    CHECK(s.hudpos == 0 && s.hudraise == 0 && ms_set_hud(&s, 0, 0) == 0 && ms_set_hud(&s, 0, 3) == 0 && ms_set_hud(&s, 1, 8) == 0 && !(s.touched2 & MSET_T2_HUD));      /* Raise HUD 0..3 plus 4 = Wired opponents only: 0..7 are valid, 8 is not */
    CHECK(ms_set_hud(&s, 0, 2) == 1 && s.hudpos == 2 && (s.touched2 & MSET_T2_HUD) && ms_set_hud(&s, 1, 3) == 1 && s.hudraise == 3 && ms_set_hud(&s, 1, 3) == 0);
    ms_set_ping(&s, 1); ms_set_preload(&s, 1);
    CHECK(MSET_PRELOAD_BYTE(&s) == (1u | 2u | (2u << 2) | (3u << 4)));
    ms_set_fill(p, &s, 0);
    CHECK(p[0x1F] == 0x3Bu);
    ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.hudpos == 2 && t.hudraise == 3 && t.pingon == 1 && t.preload == 1);
    p[0x1F] = (unsigned char) (3u << 2); ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.hudpos == 0 && t.hudraise == 0);      /* position 3 is not valid: Default */
    p[0x1F] = 128; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.hudpos == 0 && t.hudraise == 0 && t.pingon == 0 && t.preload == 0 && t.keepoff == 0 && t.musicoff == 1);   /* bit 7 = Music off; the other bits stay default */
    /* Music: default On, bit 7 of the preload byte = off, independent of the other bits, 0xFF still means 'not set' */
    ms_set_defaults(&t, 2); CHECK(t.musicoff == 0 && ms_set_music(&t, 1) == 0 && t.touched3 == 0);
    CHECK(ms_set_music(&t, 0) == 1 && t.musicoff == 1 && (t.touched3 & MSET_T3_MUSIC) && ms_set_music(&t, 0) == 0 && ms_set_music(&t, 3) == 0);
    t.keepoff = 1; t.hudpos = 2; t.hudraise = 3; t.pingon = 1; t.preload = 1; CHECK(MSET_PRELOAD_BYTE(&t) == (64u | 128u | 8u | 48u | 2u | 1u));
    p[0x1F] = (unsigned char) MSET_PRELOAD_BYTE(&t); ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicoff == 1 && u.keepoff == 1 && u.hudpos == 2 && u.hudraise == 3 && u.pingon == 1 && u.preload == 1);
    p[0x1F] = 0xFF; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicoff == 0 && u.keepoff == 0);
    p[0x1F] = 0; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicoff == 0);
    ms_set_defaults(&t, 2); ms_set_music(&t, 0); ms_set_fill(p, &t, 0); CHECK(p[0x1F] == 128 && ms_set_check(p) == MSET_OK);
    /* Music = Random: an On with random menu-scene songs from the 38-song pool; setter value 2; stored as bit 3 of byte 17, never with Off; an old stored Off stays Off, an old On stays On */
    ms_set_defaults(&t, 2); CHECK(ms_set_music(&t, 2) == 1 && t.musicoff == 0 && t.musicrand == 1 && (t.touched3 & MSET_T3_MUSIC) && ms_set_music(&t, 2) == 0);
    ms_set_fill(p, &t, 0); CHECK(p[0x1F] == 0 && p[OS_VAL_OFF + MSET_ID_SRV] == 8u && ms_set_check(p) == MSET_OK);
    ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicrand == 1 && u.musicoff == 0 && (u.hudraise & 4u) == 0);
    CHECK(ms_set_music(&t, 0) == 1 && t.musicoff == 1 && t.musicrand == 0);                                                /* Random -> Off clears the random bit */
    ms_set_fill(p, &t, 1); CHECK(p[0x1F] == 128 && p[OS_VAL_OFF + MSET_ID_SRV] == 0u);
    CHECK(ms_set_music(&t, 2) == 1 && t.musicoff == 0 && t.musicrand == 1 && ms_set_music(&t, 1) == 1 && t.musicoff == 0 && t.musicrand == 0);
    p[0x1F] = 128; p[OS_VAL_OFF + MSET_ID_SRV] = 8; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicoff == 1 && u.musicrand == 0);   /* Off wins over a stale random bit */
    p[0x1F] = 0; p[OS_VAL_OFF + MSET_ID_SRV] = 0; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicoff == 0 && u.musicrand == 0);    /* an old On */
    p[0x1F] = 0xFF; p[OS_VAL_OFF + MSET_ID_SRV] = 0xFF; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicoff == 0 && u.musicrand == 0);
    p[0x1F] = 0; p[OS_VAL_OFF + MSET_ID_SRV] = 12; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.musicrand == 1 && (u.hudraise & 4u) != 0);   /* random + Wired opponents only share byte 17 */
    ms_set_defaults(&t, 2); ms_set_hud(&t, 1, 4); ms_set_music(&t, 2); ms_set_fill(p, &t, 0); CHECK(p[OS_VAL_OFF + MSET_ID_SRV] == 12u);
    p[OS_VAL_OFF + MSET_ID_SRV] = 0xFF;                                                                                     /* the shared buffer goes on with byte 17 unset */
    /* Keep previous build as backup: default keep, bit 6 of the preload byte = switched off, independent of the other bits */
    ms_set_defaults(&t, 2); CHECK(t.keepoff == 0 && t.touched3 == 0 && ms_set_keep(&t, 1) == 0 && t.touched3 == 0);
    CHECK(ms_set_keep(&t, 0) == 1 && t.keepoff == 1 && t.touched3 == MSET_T3_KEEP && ms_set_keep(&t, 0) == 0 && ms_set_keep(&t, 2) == 0);
    CHECK((MSET_PRELOAD_BYTE(&t) & 64u) == 64u && (MSET_PRELOAD_BYTE(&t) & 63u) == 0);
    t.hudpos = 2; t.hudraise = 3; t.pingon = 1; t.preload = 1; CHECK(MSET_PRELOAD_BYTE(&t) == (64u | 1u | 2u | (2u << 2) | (3u << 4)));
    p[0x1F] = (unsigned char) MSET_PRELOAD_BYTE(&t); ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.keepoff == 1 && u.hudpos == 2 && u.hudraise == 3 && u.pingon == 1 && u.preload == 1);
    p[0x1F] = 0; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.keepoff == 0);                 /* an old card: keep */
    p[0x1F] = 0xFF; ms_set_defaults(&u, 2); ms_set_decode(&u, p); CHECK(u.keepoff == 0);              /* never written: keep */
   
    p[0x1F] = (unsigned char) (1u << 4); ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.hudraise == 1 && t.hudpos == 0);
    {   /* WIRED OPPONENTS ONLY: bit 2 of byte 17 in every build, bit 2 of hudraise in the struct, set through ms_set_hud(1, 0..7), never in byte 19 */
        unsigned char q[OS_PAYLOAD_SIZE];
        ms_set_defaults(&s, 2);
        CHECK((s.hudraise & 4u) == 0 && ms_set_hud(&s, 1, 4) == 1 && (s.hudraise & 4u) && (s.hudraise & 3u) == 0 && (s.touched2 & MSET_T2_HUD) && ms_set_hud(&s, 1, 4) == 0);
        CHECK(MSET_PRELOAD_BYTE(&s) == 0 && ms_set_hud(&s, 1, 7) == 1 && MSET_PRELOAD_BYTE(&s) == (3u << 4));          /* byte 19 holds the two raise bits only */
        CHECK(ms_set_hud(&s, 1, 4) == 1 && ms_set_hud(&s, 1, 0) == 1 && (s.hudraise & 4u) == 0);
        ms_set_defaults(&s, 2); ms_set_hud(&s, 1, 4);
        blank(q); ms_set_fill(q, &s, 0);
        CHECK(ms_set_check(q) == MSET_OK && q[OS_VAL_OFF + MSET_ID_SRV] == 4u && q[0x1F] == 0u);
        ms_set_defaults(&t, 2); ms_set_decode(&t, q); CHECK((t.hudraise & 4u) && (t.hudraise & 3u) == 0 && t.srv == 0);
        ms_set_hud(&s, 1, 6); blank(q); ms_set_fill(q, &s, 0);
        CHECK(q[OS_VAL_OFF + MSET_ID_SRV] == 4u && q[0x1F] == (2u << 4));
        ms_set_defaults(&t, 2); ms_set_decode(&t, q); CHECK((t.hudraise & 7u) == 6u);                                  /* Medium plus Wired only */
        q[OS_VAL_OFF + MSET_ID_SRV] = 0xFF; ms_set_defaults(&t, 2); ms_set_decode(&t, q); CHECK((t.hudraise & 4u) == 0);   /* unset */
        q[OS_VAL_OFF + MSET_ID_SRV] = 0; ms_set_defaults(&t, 2); ms_set_decode(&t, q); CHECK((t.hudraise & 4u) == 0);
        q[OS_VAL_OFF + MSET_ID_SRV] = 20; ms_set_defaults(&t, 2); ms_set_decode(&t, q); CHECK((t.hudraise & 4u) == 0 && t.musicrand == 0);     /* above 15: not ours */
        q[OS_VAL_OFF + MSET_ID_SRV] = 5; ms_set_defaults(&t, 2); ms_set_decode(&t, q); CHECK((t.hudraise & 4u) != 0 && t.srv == 0);   /* a release build never reads the server pick; bit 2 stands */
        q[OS_VAL_OFF + MSET_ID_SRV] = 1; ms_set_defaults(&s, 2); ms_set_hud(&s, 1, 4); ms_set_fill(q, &s, 1);
        CHECK(q[OS_VAL_OFF + MSET_ID_SRV] == 5u);                                                                          /* a stored lab server pick (bits 0-1) survives the write */
        q[OS_VAL_OFF + MSET_ID_SRV] = 0xFF; ms_set_defaults(&s, 2); ms_set_music(&s, 0); ms_set_fill(q, &s, 1);
        CHECK(q[OS_VAL_OFF + MSET_ID_SRV] == 0u);                                                                          /* any byte-19 write sets byte 17 to 0 (Off), never leaves 0xFF out of range */
    }
    {   /* every switch of byte 16 on plus ping: byte 16 stays <= 63 (an older reader keeps it) */
        ms_set_defaults(&s, 2); ms_set_rbr(&s, 1); ms_set_spec(&s, 0); ms_set_spec(&s, 1); ms_set_bak(&s, 0); ms_set_dbg(&s, 1); ms_set_rec(&s, 1); ms_set_ping(&s, 1);
        blank(p); ms_set_fill(p, &s, 0);
        CHECK(p[0x1C] <= 63 && !(p[0x1C] & 64) && p[0x1F] == 2);
    }
    {   /* one-time migration: a card written by 1069/1070 with ping in bit 6 of byte 16 (here with Debug lines and backup off: 64 + 16 + 4 = 84) */
        blank(p); p[0x1C] = 84; p[0x1F] = 1;
        ms_set_defaults(&s, 2); ms_set_fill(p, &s, 1);                        /* nothing touched: only the CRC is fixed */
        CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 84 && p[0x1F] == 1);
        ms_set_defaults(&t, 2);
        ms_set_decode(&t, p);
        CHECK(t.pingon == 1 && t.dbgl == 1 && t.bakoff == 1 && t.preload == 1 && (t.touched2 & MSET_T2_MIG) && (t.touched2 & MSET_T2_PING));
        ms_set_fill(p, &t, 1);                                               /* the next save moves it */
        CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 20 && p[0x1F] == 3);
        ms_set_defaults(&s, 2);
        ms_set_decode(&s, p);
        CHECK(s.pingon == 1 && s.dbgl == 1 && s.bakoff == 1 && s.preload == 1 && !(s.touched2 & MSET_T2_MIG));   /* once: a migrated card does not migrate again */
    }
    /* Menu preload: value index 19 = payload +0x1F, default OFF, 1 on, 0 / 0xFF / anything else off; its own touched2 bit; the other bytes stay as they are */
    ms_set_defaults(&s, 2);
    CHECK(s.preload == 0 && ms_set_preload(&s, 0) == 0 && ms_set_preload(&s, 2) == 0 && !s.touched2 && ms_set_preload(&s, 1) == 1 && s.preload == 1 && (s.touched2 & MSET_T2_PRELOAD) && ms_set_preload(&s, 1) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1F] == 1 && p[0x1C] == 0 && p[OS_VAL_OFF + 15] == 0xFF);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.preload == 1 && t.prog == MSET_PROG_FOLLOW);
    blank(p);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.preload == 0);                  /* an old record: off */
    ms_set_defaults(&s, 2);
    ms_set_preload(&s, 1); s.preload = 0;
    ms_set_fill(p, &s, 1);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1F] == 0);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.preload == 0);
    /* Melee records marker: bit 5 (32) of the byte of id 16, its own touched2 bit, one-way (only 1 is accepted), independent of the other bits;
       an old card (byte 0, 0xFF, values up to 31) reads as 'not seeded' */
    ms_set_defaults(&s, 2);
    CHECK(s.recseed == 0 && ms_set_rec(&s, 0) == 0 && ms_set_rec(&s, 2) == 0 && !(s.touched2 & MSET_T2_REC) && ms_set_rec(&s, 1) == 1 && s.recseed == 1 && (s.touched2 & MSET_T2_REC) && ms_set_rec(&s, 1) == 0);
    blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1C] == 32 && MSET_RBR_BYTE(&s) == 32);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.recseed == 1 && t.rbr == 0 && t.dbgl == 0 && t.bakoff == 0 && t.specoff == 0);
    ms_set_rbr(&s, 1); ms_set_bak(&s, 0); ms_set_dbg(&s, 1); ms_set_fill(p, &s, 1);             /* everything on top of the marker: 32 + 16 + 4 + 1 */
    CHECK(p[0x1C] == 53);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) >= 4 && t.recseed == 1 && t.rbr == 1 && t.bakoff == 1 && t.dbgl == 1);
    p[0x1C] = 31; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.recseed == 0);
    p[0x1C] = 0xFF; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.recseed == 0);
    p[0x1C] = 0; ms_set_defaults(&t, 2); ms_set_decode(&t, p); CHECK(t.recseed == 0);
    {   /* a marker change leaves the other option bytes alone */
        unsigned char r[OS_PAYLOAD_SIZE];
        blank(r);
        ms_set_defaults(&s, 2);
        ms_set_delay(&s, 5); ms_set_fill(r, &s, 0);
        ms_set_defaults(&s, 2);
        ms_set_rec(&s, 1); ms_set_fill(r, &s, 1);
        CHECK(ms_set_check(r) == MSET_OK && r[OS_VAL_OFF + MSET_ID_DELAY] == 0x15 && r[0x1C] == 32);
    }
#ifdef SBR_NETCFG
    /* netsel toggles (lab/dev builds): server pick = value index 17 (payload +0x1D), Slippi matchmaking = index 18 (+0x1E); defaults the server / Our mock; an old record reads as defaults */
    ms_set_defaults(&s, 2);
    CHECK(s.srv == MSET_SRV_VPS && s.mmr == MSET_MMR_MOCK && ms_set_srv(&s, 0) == 0 && ms_set_srv(&s, 3) == 0 && ms_set_mmr(&s, 0) == 0 && ms_set_mmr(&s, 2) == 0 && !(s.touched & (MSET_T_SRV | MSET_T_MMR)));
    blank(p);
    CHECK(p[0x1D] == 0 && p[0x1E] == 0);                                  /* an older record: reserved zeros */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.srv == MSET_SRV_VPS && t.mmr == MSET_MMR_MOCK);
    CHECK(ms_set_srv(&s, 1) == 1 && s.srv == MSET_SRV_LAB && (s.touched & MSET_T_SRV) && !(s.touched & MSET_T_MMR) && ms_set_srv(&s, 1) == 0);
    ms_set_fill(p, &s, 0);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1D] == 1 && p[0x1E] == 0 && p[0x1C] == 0 && p[OS_VAL_OFF + 15] == 0xFF);       /* only the touched byte is written; the CRC covers it */
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 1 && t.srv == MSET_SRV_LAB && t.mmr == MSET_MMR_MOCK && t.rbr == 0);
    CHECK(ms_set_mmr(&s, 1) == 1 && s.mmr == MSET_MMR_REAL && (s.touched & MSET_T_MMR));
    ms_set_fill(p, &s, 1);
    CHECK(ms_set_check(p) == MSET_OK && p[0x1D] == 1 && p[0x1E] == 1);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 2 && t.srv == MSET_SRV_LAB && t.mmr == MSET_MMR_REAL);
    p[0x1D] = 0xFF; p[0x1E] = 0xFF; ms_set_defaults(&t, 2);                   /* unset / out of range: the defaults */
    CHECK(ms_set_decode(&t, p) == 0 && t.srv == MSET_SRV_VPS && t.mmr == MSET_MMR_MOCK);
    CHECK(ms_set_mmk(&s) == 1 && s.mmk == 1 && ms_set_mmk(&s) == 0);        /* the remembered confirm: bit1 of the byte */
    ms_set_fill(p, &s, 1);
    CHECK(p[0x1E] == 3);
    ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 2 && t.mmr == 1 && t.mmk == 1);
    p[0x1E] = 2; ms_set_defaults(&t, 2);                                    /* confirmed, then switched back to Our mock: stays confirmed */
    CHECK(ms_set_decode(&t, p) == 2 && t.mmr == 0 && t.mmk == 1);
    ms_set_srv(&s, 0); ms_set_mmr(&s, 0); s.mmk = 0; s.touched = MSET_T_SRV | MSET_T_MMR;   /* back to the defaults writes explicit zeros over the old ones */
    p[0x1D] = 1; p[0x1E] = 1; ms_set_fill(p, &s, 0);
    CHECK(p[0x1D] == 0 && p[0x1E] == 0);
#else
    /* release: the two bytes are never read or written */
    blank(p); p[0x1D] = 1; p[0x1E] = 3; ms_set_defaults(&t, 2);
    CHECK(ms_set_decode(&t, p) == 0 && t.srv == 0 && t.mmr == 0);
    ms_set_defaults(&s, 2); s.srv = 1; s.mmr = 1; s.touched = MSET_T_SRV | MSET_T_MMR; blank(p);
    ms_set_fill(p, &s, 0);
    CHECK(p[0x1D] == 0 && p[0x1E] == 0);
#endif
    /* ms_prog_plan: no cable = nothing; stored On switches the picture on; Off and not set (fresh card) are the same and switch a live progressive picture off; the console flag is only cleared, never adopted */
    CHECK(ms_prog_plan(MSET_PROG_ON, 0, 1, 0) == 0 && ms_prog_plan(MSET_PROG_OFF, 0, 0, 1) == 0 && ms_prog_plan(MSET_PROG_FOLLOW, 0, 1, 1) == 0);
    {   /* the first-run question is the first step: a fresh card (options record absent = defaults, unset), cable present, interlaced picture -> asked, also when the console flag is 1 (a Wii) */
        struct ms_settings f;
        ms_set_defaults(&f, 2);
        CHECK(ms_prog_ask(1, f.prog, 1, 0) == 1);                       /* fresh card, no file: asked */
        CHECK(ms_prog_ask(0, f.prog, 1, 0) == 0);                       /* record not read (busy, card error): not yet */
        CHECK(ms_prog_ask(1, f.prog, 0, 0) == 0);                       /* no cable: no question */
        CHECK(ms_prog_ask(1, f.prog, 1, 1) == 0);                       /* already progressive */
        CHECK(ms_prog_ask(1, MSET_PROG_ON, 1, 0) == 0 && ms_prog_ask(1, MSET_PROG_OFF, 1, 0) == 0);   /* answered: never again */
    }
    CHECK(ms_prog_plan(MSET_PROG_FOLLOW, 1, 1, 1) == (MP_CLEAR_SRAM | MP_LIVE_OFF));
    CHECK(ms_prog_plan(MSET_PROG_FOLLOW, 1, 1, 0) == MP_CLEAR_SRAM);
    CHECK(ms_prog_plan(MSET_PROG_ON, 1, 1, 1) == MP_CLEAR_SRAM);
    CHECK(ms_prog_plan(MSET_PROG_ON, 1, 1, 0) == (MP_CLEAR_SRAM | MP_LIVE_ON));
    CHECK(ms_prog_plan(MSET_PROG_OFF, 1, 1, 1) == (MP_CLEAR_SRAM | MP_LIVE_OFF));
    CHECK(ms_prog_plan(MSET_PROG_ON, 1, 0, 0) == MP_LIVE_ON);
    CHECK(ms_prog_plan(MSET_PROG_ON, 1, 0, 1) == 0);
    CHECK(ms_prog_plan(MSET_PROG_OFF, 1, 0, 1) == MP_LIVE_OFF && ms_prog_plan(MSET_PROG_OFF, 1, 0, 0) == 0);
    CHECK(ms_prog_plan(MSET_PROG_FOLLOW, 1, 0, 0) == 0);
    CHECK(ms_prog_plan(MSET_PROG_FOLLOW, 1, 0, 1) == MP_LIVE_OFF);
    /* ms_ws_apply with a fake RAM: with the off words nothing is written, switching on writes every word, off restores the retail words */
    /* Music apply plan (boot apply): words and the live stream volume write */
    CHECK(MS_MUSIC_WORD(0) == 0x3880007Fu && MS_MUSIC_WORD(1) == 0x38800000u);
    {
        unsigned tab[1 + 3 * 3] = { 3, 0x100, 0xAAAA0001u, 0x11110001u, 0x104, 0xAAAA0002u, 0x11110002u, 0x108, 0xAAAA0003u, 0x11110003u };
        struct fakeram_t ram = { { 0x100, 0x104, 0x108 }, { 0x11110001u, 0x11110002u, 0x11110003u }, 0 };
        CHECK(ms_ws_apply(tab, 0, fr_rd, fr_wr, &ram) == 0);                     /* default state: the off words are already there */
        CHECK(ms_ws_apply(tab, 1, fr_rd, fr_wr, &ram) == 3 && ram.w[0] == 0xAAAA0001u && ram.w[2] == 0xAAAA0003u);
        CHECK(ms_ws_apply(tab, 1, fr_rd, fr_wr, &ram) == 0);                     /* idempotent */
        CHECK(ms_ws_apply(tab, 0, fr_rd, fr_wr, &ram) == 3 && ram.w[1] == 0x11110002u);
        tab[0] = 0;
        CHECK(ms_ws_apply(tab, 1, fr_rd, fr_wr, &ram) == 0);                     /* an image without the table */
        tab[0] = 99;
        CHECK(ms_ws_apply(tab, 1, fr_rd, fr_wr, &ram) == 0);                     /* a damaged count is refused */
    }
    printf(fails ? "mset_test: %d FAILED\n" : "mset_test: all checks passed\n", fails);
    return fails != 0;
}
