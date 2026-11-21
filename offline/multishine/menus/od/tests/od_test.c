/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of od_backend.c behind the real exi_shim router (the online-direct image's menu answers): B9 identity from the config block, B4 Direct starts the
   controller, B3 states 1/2/5 and the engine's answer once the session runs (names from the ticket filled in), BA cancels, Ranked / other modes / bad code
   refusals, B5 and the engine's B3 slot still called.  No network: od_send is a counter, the engine is a stub. */
#include <stdio.h>
#include <string.h>
#include "../../exi_shim/sdshim.h"
#define OMC_HOST 1
#include "om_card.c"           /* the card layer against the fake card: od_backend.c links it (history of codes) */
#include "omc_fake.inc"
#include "od_glue.h"

extern void od_set_backend(struct sdshim *s, const struct sdshim_backend *b);
extern struct od od_o;
extern struct od_status od_st;
extern struct od_cfg od_cfg;

static int sent, engine_b5, engine_b3, core_state_v = 4;
int od_send(void *ctx, const unsigned char *p, unsigned int len) { (void)ctx; (void)p; (void)len; sent++; return (int)len; }
int od_core_state(void) { return core_state_v; }
unsigned int od_clock_ms(void) { static unsigned int t; return t += 7; }

static int e_sel(void *c, const sdshim_u8 *p) { (void)c; (void)p; engine_b5++; return 0; }
static int e_state(void *c, sdshim_u8 *m)
{
    (void)c;
    engine_b3++;
    m[0] = 4;
    memcpy(m + 15, "SIDE A", 7); memcpy(m + 46, "SIDE A", 7); memcpy(m + 77, "SIDE B", 7); memcpy(m + 170, "SIDE B", 7);
    memcpy(m + 201, "SDA", 3); m[204] = 0x81; m[205] = 0x94; m[206] = '1';
    return 0;
}
static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)

static struct sdshim S;
static unsigned char tb[1024];
static void wr(const unsigned char *p, unsigned n) { memcpy(tb, p, n); sdshim_transfer_ctx(&S, tb, n, 1); }
static void rd(unsigned n) { memset(tb, 0xEE, sizeof tb); sdshim_transfer_ctx(&S, tb, n, 0); }
static void cmd1(unsigned char c) { wr(&c, 1); }
static void find(unsigned char mode, const char *code)
{
    unsigned char r[20];
    memset(r, 0, sizeof r);
    r[0] = 0xB4; r[1] = mode;
    if (code) memcpy(r + 2, code, strlen(code));       /* ASCII bytes are accepted by the code parser as well as the full-width SJIS pairs */
    wr(r, 20);
}

int main(void)
{
    struct sdshim_backend eng;
    unsigned char *b = tb;
    int i;
    memset(&eng, 0, sizeof eng);
    eng.set_selections = e_sel;
    eng.match_state = e_state;
    fk_reset();
    K[0].present = 1;
    /* the config block as od_patch.py writes it */
    od_cfg.flags = OD_CFGF_ID | OD_CFGF_LAX;
    od_cfg.server_ip = 0x0AD56E02u; od_cfg.server_port = 43150;
    for (i = 0; i < 32; i++) od_cfg.pin[0][i] = (unsigned char)(i + 1);
    for (i = 0; i < 8; i++) od_cfg.dev[i] = (unsigned char)(0xA0 + i);
    for (i = 0; i < 16; i++) od_cfg.tok[i] = (unsigned char)(0x50 + i);
    strcpy(od_cfg.code, "AA#12345"); strcpy(od_cfg.name, "Alice");
    sdshim_init(&S);
    od_set_backend(&S, &eng);
    CHECK(od_o.cfg_ok == 1);

    cmd1(0xB9); rd(42);
    CHECK(b[0] == 1);                                   /* appState: logged in */
    CHECK(memcmp(b + 1, "Alice", 6) == 0);
    CHECK(memcmp(b + 32, "AA", 2) == 0 && b[34] == 0x81 && b[35] == 0x94 && memcmp(b + 36, "12345", 5) == 0 && b[41] == 0);   /* code field 10 bytes: AA, SJIS #, 5 digits = 9 */
    cmd1(0xB3); rd(962);
    CHECK(b[0] == 0 && engine_b3 == 0);                 /* idle: the engine is not asked */

    /* Direct: typed code, B3 INITIALIZING until the network slice picks the request up */
    find(2, "BB#54321"); cmd1(0xB3); rd(962);
    CHECK(od_st.requests == 1 && od_st.last_mode == 2 && od_o.phase == OD_WAIT_NET);
    CHECK(b[0] == 1 && engine_b3 == 0);
    CHECK(strcmp(od_o.target, "BB#54321") == 0);
    od_net_up(&od_o, 0xC0A80002u, 0xFFFFFF00u);
    od_tick(&od_o, od_clock_ms());
    CHECK(od_o.phase == OD_FIND && sent == 1);          /* the handshake datagram of mnc_find (wire v2) */
    cmd1(0xB3); rd(962);
    CHECK(b[0] == 2);                                   /* MATCHMAKING */
    cmd1(0xBA); cmd1(0xB3); rd(962);
    CHECK(b[0] == 0 && od_o.phase == OD_IDLE && od_st.cancels == 1);

    /* LINK QUALITY cooldown: status 50..53 on the ticket = "Matchmaking paused for N min after leaving games", shown through the error box (no retry) */
    {
        static const unsigned st[4] = { 50, 51, 52, 53 };
        static const char *const tx[4] = { "Matchmaking paused for up to 15 min after leaving games", "Matchmaking paused for up to 60 min after leaving games",
                                           "Matchmaking paused for up to 4 hours after leaving games", "Matchmaking paused for up to 24 hours after leaving games" };
        unsigned k;
        for (k = 0; k < 4; k++) {
            find(2, "BB#54321"); od_net_up(&od_o, 0xC0A80002u, 0xFFFFFF00u); od_tick(&od_o, od_clock_ms());
            CHECK(od_o.phase == OD_FIND);
            od_o.m.state = MNC_FAILED; od_o.m.err = MNC_E_SERVER; od_o.m.status = (int)st[k];
            od_tick(&od_o, od_clock_ms());
            CHECK(od_o.phase == OD_ERR && od_o.err == OD_E_DNS && od_o.rt_n == 0 && strcmp(od_err_text(&od_o), tx[k]) == 0);
            cmd1(0xB3); rd(962);
            CHECK(b[0] == SDSHIM_MM_ERROR && strcmp((char *)b + SDSHIM_MSRB_ERROR_OFFSET, tx[k]) == 0);
            cmd1(0xBA);
        }
        CHECK(od_o.phase == OD_IDLE);
    }

    /* refusals */
    find(0, 0); cmd1(0xB3); rd(962);
    CHECK(b[0] == SDSHIM_MM_ERROR && strcmp((char *)b + SDSHIM_MSRB_ERROR_OFFSET, "Ranked is not available") == 0);
    cmd1(0xBA); cmd1(0xB3); rd(962);
    CHECK(b[0] == 0 && od_st.latch == 0);
    find(3, "AB#1"); cmd1(0xB3); rd(962);
    CHECK(b[0] == SDSHIM_MM_ERROR && strcmp((char *)b + SDSHIM_MSRB_ERROR_OFFSET, "Online play is not available yet") == 0);
    cmd1(0xBA);
    find(1, 0); cmd1(0xB3); rd(962);                    /* unranked is off without the config flag */
    CHECK(b[0] == SDSHIM_MM_ERROR && strcmp((char *)b + SDSHIM_MSRB_ERROR_OFFSET, "Online play is not available yet") == 0);
    cmd1(0xBA);
    find(2, "nonsense"); cmd1(0xB3); rd(962);
    CHECK(b[0] == SDSHIM_MM_ERROR && strstr((char *)b + SDSHIM_MSRB_ERROR_OFFSET, "not a valid connect code") != 0);
    cmd1(0xBA);
    find(2, 0); cmd1(0xB3); rd(962);                    /* empty code */
    CHECK(b[0] == SDSHIM_MM_ERROR);
    cmd1(0xBA);

    /* the session runs: the engine answers, names and codes come from the ticket */
    od_o.phase = OD_UP; od_o.side = 1;
    strcpy(od_o.opp_name, "Carol"); strcpy(od_o.opp_code, "CC#7777");
    cmd1(0xB3); rd(962);
    CHECK(engine_b3 == 1 && b[0] == 4);
    CHECK(strcmp((char *)b + 15, "Alice") == 0);                      /* local name */
    CHECK(strcmp((char *)b + 46, "Carol") == 0 && strcmp((char *)b + 77, "Alice") == 0);   /* player 1 is the opponent: this side is 1 */
    CHECK(strcmp((char *)b + 170, "Carol") == 0);
    CHECK(memcmp(b + 201, "CC", 2) == 0 && b[203] == 0x81 && b[204] == 0x94 && memcmp(b + 205, "7777", 4) == 0);
    CHECK(memcmp(b + 211, "AA", 2) == 0 && b[213] == 0x81 && b[214] == 0x94 && memcmp(b + 215, "12345", 5) == 0);
    {   unsigned char sel[10] = { 0xB5, 0, 1, 2, 1, 0, 0x1F, 1, 2, 0 };
        wr(sel, 10);
        CHECK(engine_b5 == 1);
    }
    cmd1(0xBA); cmd1(0xB3); rd(962);
    CHECK(b[0] == 0 && od_o.phase == OD_IDLE);

    /* no config: the menu still works with the fixed identity, B4 says why it cannot */
    memset(&od_cfg.flags, 0, sizeof od_cfg.flags);
    sdshim_init(&S);
    od_set_backend(&S, &eng);
    CHECK(od_o.cfg_ok == 0);
    cmd1(0xB9); rd(42);
    CHECK(b[0] == 1 && memcmp(b + 1, "MULTI", 6) == 0);
    find(2, "BB#54321"); cmd1(0xB3); rd(962);
    CHECK(b[0] == SDSHIM_MM_ERROR && strstr((char *)b + SDSHIM_MSRB_ERROR_OFFSET, "not set up") != 0);

    /* minimum-build gate: status 32 (UPDATE_REQUIRED) has its own text; status 6 and unknown statuses keep theirs (letters only, no / or |) */
    {
        struct od t;
        const char *x;
        memset(&t, 0, sizeof t);
        t.err = OD_E_REFUSED;
        t.m.status = 32; x = od_err_text(&t);
        CHECK(strcmp(x, "Update required to play online") == 0 && !strchr(x, '/') && !strchr(x, '|'));
        t.m.status = 6; CHECK(strcmp(od_err_text(&t), "Build too old for the server") == 0);
        t.m.status = 1; CHECK(strcmp(od_err_text(&t), "Connect code not found") == 0);
        t.m.status = 16; CHECK(strcmp(od_err_text(&t), "Connect code not found") == 0);
        t.m.status = 33; CHECK(strcmp(od_err_text(&t), "The server refused the request") == 0);
    }

    {   /* the per-boot game port stays in 43201..44199 and varies with the seed */
        unsigned int z, lo = 65535, hi = 0, same = 0; unsigned short p0 = OD_GAME_PORT(0);
        for (z = 0; z < 5000; z++) { unsigned short p = OD_GAME_PORT(z * 7919u + 13u); if (p < lo) lo = p; if (p > hi) hi = p; if (p == p0) same++; }
        CHECK(lo >= 43201u && hi <= 44199u && hi - lo > 900u && same < 30u);
    }
    printf("od_test: %s (%d failure%s)\n", fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails != 0;
}
