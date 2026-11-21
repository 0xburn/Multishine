/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Tournament reply parsers of gnc.c (gnc_parse_tn) against replies built by hand from the wire spec:
   every op's good reply, then truncation / extension / out-of-range mutations of each, which must all be refused.  Build:
   cc -std=gnu99 -O1 -g -fsanitize=address,undefined -I offline/client_net offline/client_net/tests/tn_parse_test.c offline/client_net/gnc.c offline/client_net/mnc.c \
      offline/client_net/mw2.c offline/client_net/mw2_crypto.c offline/payload_pack/ms_sha256.c */
#include <stdio.h>
#include <string.h>
#include "../gnc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static unsigned p12(mnc_u8 *d, const char *s) { unsigned n = (unsigned)strlen(s); d[0] = (mnc_u8)n; memcpy(d + 1, s, n); return n + 1u; }

/* every strict prefix and a one byte extension must be refused */
static void mutate(unsigned op, const mnc_u8 *b, unsigned n)
{
    static struct gnc_tn t;
    unsigned k;
    mnc_u8 x[512];
    for (k = 0; k < n; k++) if (gnc_parse_tn(op, b, k, &t) == 0 && !(op == GNC_OP_TN_MYMATCH && 0)) { printf("FAIL op %02X prefix %u of %u accepted\n", op, k, n); fails++; }
    memcpy(x, b, n); x[n] = 0;
    CHECK(gnc_parse_tn(op, x, n + 1u, &t) != 0);
}

int main(void)
{
    static struct gnc_tn t;
    mnc_u8 b[512];
    unsigned n, k;

    /* TN_LIST: flags, total, n=2 */
    n = 0; b[n++] = 1; b[n++] = 0; b[n++] = 0; b[n++] = 0x1C; b[n++] = 0x20; b[n++] = 2; b[n++] = 2;      /* flags, next_cup_secs 7200, total, n */
    b[n++] = 0x01; b[n++] = 0x02; n += p12(b + n, "Friday Cup"); b[n++] = 0; b[n++] = 5; b[n++] = 16; b[n++] = 1; b[n++] = 1; b[n++] = 0x4C; b[n++] = 0; b[n++] = 0; b[n++] = 0x0E; b[n++] = 0x10;
    b[n++] = 0x05; b[n++] = 0xF5; b[n++] = 0xE0; b[n++] = 0xFF; b[n++] = 0; b[n++] = 0; b[n++] = 0x0B; b[n++] = 0xB8; b[n++] = 1; b[n++] = 3;      /* v3: id 99999999, seconds to the start 3000, scheduled; v4: region 3 (EU) */
    b[n++] = 0x00; b[n++] = 0x09; n += p12(b + n, "Chill Night"); b[n++] = 2; b[n++] = 8; b[n++] = 8; b[n++] = 0; b[n++] = 0; b[n++] = 0xC0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    b[n++] = 0x05; b[n++] = 0xF5; b[n++] = 0xE0; b[n++] = 0xFE; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0;
    CHECK(gnc_parse_tn(GNC_OP_TN_LIST, b, n, &t) == 0 && t.n == 2 && t.total == 2 && t.flags == 1);
    CHECK(t.e[0].tid == 0x0102 && !strcmp(t.e[0].name, "Friday Cup") && t.e[0].entrants == 5 && t.e[0].cap == 16 && t.e[0].format == 1 && t.e[0].my == 1 && t.e[0].eflags == 0x4C && t.e[0].secs == 3600 && t.next_cup == 7200);
    CHECK(t.e[0].pubid == 99999999ul && t.e[0].signup_left == 3000 && t.e[0].scheduled == 1 && t.e[1].pubid == 99999998ul && t.e[1].scheduled == 0 && t.e[0].region == 3 && t.e[1].region == 0);
    b[n - 1] = 7; CHECK(gnc_parse_tn(GNC_OP_TN_LIST, b, n, &t) == 0 && t.e[1].region == 7);       /* region 7 = NAC (version 5) */
    b[n - 1] = 8; CHECK(gnc_parse_tn(GNC_OP_TN_LIST, b, n, &t) != 0); b[n - 1] = 0;        /* region 8 is no region */
    CHECK(t.e[1].tid == 9 && t.e[1].state == 2 && t.e[1].cap == 8);
    mutate(GNC_OP_TN_LIST, b, n);
    b[7 + 2 + 1 + 10 + 1] = 99; CHECK(gnc_parse_tn(GNC_OP_TN_LIST, b, n, &t) != 0);        /* state 99 */
    b[21] = 5; b[30] = 0x06; CHECK(gnc_parse_tn(GNC_OP_TN_LIST, b, n, &t) != 0);   /* id 8 digits no more (100000000+) */

    /* TN_INFO */
    n = 0; n += p12(b + n, "Friday Cup"); n += p12(b + n, "Nigzel");
    b[n++] = 2; b[n++] = 8; b[n++] = 8; b[n++] = 16; b[n++] = 0; b[n++] = 1; b[n++] = 0x0C; b[n++] = 2; b[n++] = 4; b[n++] = 7; b[n++] = 0; b[n++] = 0;
    b[n++] = 0xDE; b[n++] = 0xAD; b[n++] = 0xBE; b[n++] = 0xEF; n += p12(b + n, "");
    CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) == 0 && t.info.state == 2 && t.info.round == 2 && t.info.rounds == 4 && t.info.rules_hash == 0xDEADBEEFu && !strcmp(t.info.host, "Nigzel"));
    mutate(GNC_OP_TN_INFO, b, n);
    b[11 + 7 + 7] = 9; CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) != 0);             /* round 9 > rounds */

    /* TN_INFO version 3 tail (after the version 2 tail official u8, secs u32, reason u8, next_cup u32): tid u16, id u32, seconds left u32, signup_min u8, host_plays u8, scheduled u8, start_at u32 */
    n = 0; n += p12(b + n, "Friday Cup"); n += p12(b + n, "Nigzel");
    b[n++] = 0; b[n++] = 3; b[n++] = 0; b[n++] = 16; b[n++] = 0; b[n++] = 2; b[n++] = 0x04; b[n++] = 0; b[n++] = 0; b[n++] = 7; b[n++] = 0; b[n++] = 0;
    b[n++] = 0xDE; b[n++] = 0xAD; b[n++] = 0xBE; b[n++] = 0xEF; n += p12(b + n, "");
    b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0xFF; b[n++] = 0xFF; b[n++] = 0xFF; b[n++] = 0xFF;   /* v2 tail */
    k = n;
    b[n++] = 0x00; b[n++] = 0x2A; b[n++] = 0x05; b[n++] = 0xF5; b[n++] = 0xE0; b[n++] = 0xFF; b[n++] = 0; b[n++] = 0; b[n++] = 0x0E; b[n++] = 0x10; b[n++] = 60; b[n++] = 0; b[n++] = 1; b[n++] = 0x65; b[n++] = 0x4F; b[n++] = 0x00; b[n++] = 0x00; b[n++] = 5;      /* v4: region 5 (AS) */
    CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) == 0 && t.info.tid == 42 && t.info.pubid == 99999999ul && t.info.signup_left == 3600 && t.info.signup_min == 60 && t.info.host_plays == 0 && t.info.scheduled == 1 && t.info.start_at == 0x654F0000ul && t.info.seeding == 2 && t.info.region == 5);
    { unsigned j; for (j = k + 1u; j < n; j++) CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, j, &t) != 0); }   /* a cut inside the version 3 tail */
    b[k + 2] = 0x06; CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) != 0); b[k + 2] = 0x05;                /* id out of range */
    b[n - 1] = 7; CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) == 0 && t.info.region == 7);             /* NAC */
    b[n - 1] = 8; CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) != 0); b[n - 1] = 5;                    /* no region */
    b[k + 13 - 1] = 2; CHECK(gnc_parse_tn(GNC_OP_TN_INFO, b, n, &t) != 0);                               /* scheduled 2 */

    /* TN_CREATE, JOIN, LEAVE, START, CHECKIN */
    b[0] = 0x00; b[1] = 0x2A; b[2] = 0x05; b[3] = 0xF5; b[4] = 0xE0; b[5] = 0xFF; CHECK(gnc_parse_tn(GNC_OP_TN_CREATE, b, 6, &t) == 0 && t.tid == 42 && t.pubid == 99999999ul);
    CHECK(gnc_parse_tn(GNC_OP_TN_CREATE, b, 2, &t) != 0);                               /* the id is part of the reply */
    b[0] = 0; b[1] = 0; CHECK(gnc_parse_tn(GNC_OP_TN_CREATE, b, 6, &t) != 0);
    mutate(GNC_OP_TN_CREATE, (const mnc_u8 *)"\x00\x2A\x05\xF5\xE0\xFF", 6);
    b[0] = 1; CHECK(gnc_parse_tn(GNC_OP_TN_JOIN, b, 1, &t) == 0 && t.result == 1 && t.pubid == 0);
    b[0] = 0; b[1] = 0x00; b[2] = 0x2A; b[3] = 0x05; b[4] = 0xF5; b[5] = 0xE0; b[6] = 0xFF; CHECK(gnc_parse_tn(GNC_OP_TN_JOIN, b, 7, &t) == 0 && t.result == 0 && t.tid == 42 && t.pubid == 99999999ul);   /* by id */
    CHECK(gnc_parse_tn(GNC_OP_TN_JOIN, b, 5, &t) != 0 && gnc_parse_tn(GNC_OP_TN_JOIN, b, 8, &t) != 0);
    b[0] = 1;
    b[0] = 3; CHECK(gnc_parse_tn(GNC_OP_TN_LEAVE, b, 1, &t) != 0);
    b[0] = 2; CHECK(gnc_parse_tn(GNC_OP_TN_START, b, 1, &t) == 0 && t.result == 2);
    b[0] = 0; CHECK(gnc_parse_tn(GNC_OP_TN_START, b, 1, &t) != 0);
    CHECK(gnc_parse_tn(GNC_OP_TN_CHECKIN, b, 0, &t) == 0 && gnc_parse_tn(GNC_OP_TN_CHECKIN, b, 1, &t) != 0);

    /* TN_BRACKET version 2: a 4 entrant double elimination bracket, 7 matches of 12 bytes on one match page; header: state rev size entrants rounds format nmatches me page pages n */
    n = 0; b[n++] = 2; b[n++] = 3; b[n++] = 4; b[n++] = 4; b[n++] = 2; b[n++] = 0; b[n++] = 7; b[n++] = 3; b[n++] = 0; b[n++] = 2; b[n++] = 7;
    {   /* mid side round state a b wa wb win bo next_win next_lose */
        static const mnc_u8 M[7][12] = { { 1, 0, 1, 4, 0, 3, 2, 0, 1, 3, 3, 4 }, { 2, 0, 1, 4, 1, 2, 2, 1, 1, 3, 3, 4 }, { 3, 0, 2, 3, 0, 1, 1, 0, 0, 3, 7, 5 },
                                         { 4, 1, 1, 1, 3, 2, 0, 0, 0, 3, 5, 0 }, { 5, 1, 2, 0, 0xFF, 0xFF, 0, 0, 0, 3, 6, 0 }, { 6, 2, 1, 0, 0xFF, 0xFF, 0, 0, 0, 5, 7, 0 }, { 7, 2, 2, 7, 0xFF, 0xFF, 0, 0, 0, 5, 0, 0 } };
        for (k = 0; k < 7; k++) { memcpy(b + n, M[k], 12); n += 12; }
    }
    CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) == 0 && t.b_is_match && t.b_n == 7 && t.b_nm == 7 && t.b_me == 3 && t.b_format == 0 && t.m[3].side == 1 && t.m[2].state == 3 && t.m[5].bo == 5 && t.m[6].state == 7);
    mutate(GNC_OP_TN_BRACKET, b, n);
    b[11 + 4] = 9; CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) != 0);                    /* entrant index 9 >= 4 */
    b[11 + 4] = 0; b[11 + 9] = 4; CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) != 0);    /* bo 4 */
    b[11 + 9] = 3; b[11] = 8; CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) != 0);        /* mid 8 > nmatches */
    /* its name page (page 1 of 2): name p12, state, seed */
    n = 0; b[n++] = 2; b[n++] = 3; b[n++] = 4; b[n++] = 4; b[n++] = 2; b[n++] = 0; b[n++] = 7; b[n++] = 0xFF; b[n++] = 1; b[n++] = 2; b[n++] = 4;
    for (k = 0; k < 4; k++) { static const char *const N[4] = { "Nigzel", "Alpha", "Bravo", "Charlie" }; n += p12(b + n, N[k]); b[n++] = (mnc_u8)(k == 3 ? 1 : 0); b[n++] = (mnc_u8)(k + 1); }
    CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) == 0 && !t.b_is_match && t.b_n == 4 && !strcmp(t.names[3], "Charlie") && t.nstate[3] == 1 && t.nseed[3] == 4 && t.b_me == 0xFF);
    mutate(GNC_OP_TN_BRACKET, b, n);
    /* before the start: size 0, no matches, one name page */
    n = 0; b[n++] = 0; b[n++] = 1; b[n++] = 0; b[n++] = 2; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 0xFF; b[n++] = 0; b[n++] = 1; b[n++] = 2;
    n += p12(b + n, "A"); b[n++] = 0; b[n++] = 1; n += p12(b + n, "B"); b[n++] = 0; b[n++] = 2;
    CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) == 0 && !t.b_is_match && t.b_size == 0 && t.b_n == 2);
    /* 64 double: 127 matches = 4 match pages of 38 (the last holds 13) */
    n = 0; b[n++] = 2; b[n++] = 1; b[n++] = 64; b[n++] = 64; b[n++] = 6; b[n++] = 0; b[n++] = 127; b[n++] = 0xFF; b[n++] = 3; b[n++] = 7; b[n++] = 13;
    for (k = 0; k < 13; k++) { b[n++] = (mnc_u8)(115 + k); b[n++] = 1; b[n++] = 2; b[n++] = 0; b[n++] = 0xFF; b[n++] = 0xFF; b[n++] = 0; b[n++] = 0; b[n++] = 0; b[n++] = 3; b[n++] = 0; b[n++] = 0; }
    CHECK(gnc_parse_tn(GNC_OP_TN_BRACKET, b, n, &t) == 0 && t.b_is_match && t.b_n == 13);

    /* TN_MYMATCH kinds: kind rev, then (2) mid side round rounds opp code room best_of secs, (3) round rounds side, (4) place round side */
    n = 0; b[n++] = 2; b[n++] = 5; b[n++] = 5; b[n++] = 1; b[n++] = 2; b[n++] = 3; n += p12(b + n, "Alpha"); n += p12(b + n, "ALPH#1"); n += p12(b + n, "4821"); b[n++] = 5; b[n++] = 0x00; b[n++] = 0xB4;
    CHECK(gnc_parse_tn(GNC_OP_TN_MYMATCH, b, n, &t) == 0 && t.my.kind == 2 && t.my.mid == 5 && t.my.side == 1 && t.my.round == 2 && !strcmp(t.my.opp, "Alpha") && !strcmp(t.my.room, "4821") && t.my.secs == 180 && t.my.best_of == 5);
    mutate(GNC_OP_TN_MYMATCH, b, n);
    b[0] = 3; b[1] = 6; b[2] = 2; b[3] = 3; b[4] = 1; CHECK(gnc_parse_tn(GNC_OP_TN_MYMATCH, b, 5, &t) == 0 && t.my.kind == 3 && t.my.round == 2 && t.my.side == 1);
    b[0] = 4; b[2] = 3; b[3] = 2; b[4] = 0; CHECK(gnc_parse_tn(GNC_OP_TN_MYMATCH, b, 5, &t) == 0 && t.my.kind == 4 && t.my.place == 3);
    b[0] = 5; CHECK(gnc_parse_tn(GNC_OP_TN_MYMATCH, b, 2, &t) == 0 && t.my.kind == 5);
    b[0] = 7; CHECK(gnc_parse_tn(GNC_OP_TN_MYMATCH, b, 2, &t) == 0 && t.my.kind == 7); b[0] = 8; CHECK(gnc_parse_tn(GNC_OP_TN_MYMATCH, b, 2, &t) != 0);
    CHECK(gnc_parse_tn(0x30, b, 2, &t) != 0);
    printf(fails ? "tn_parse_test: %d FAILED\n" : "tn_parse_test: all passed\n", fails);
    return fails ? 1 : 0;
}
