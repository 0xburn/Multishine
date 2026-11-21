/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the lab/dev Settings > Online toggles: the two consumer rules in on_card.c / on_set.c / on_cfgrec.c against the fake memory card.
   Built with -DSBR_NETCFG -DSD_CARD_HOLE -DONS_SETTINGS (= ONS_NETSEL).  One scenario per run (static state, like the console): netsel_test N.  Mocks only: no network at all.
     0 locked build (patched the server), Server: the server -> Lab -> the server: the card record's address and pinned key (and identity) are used when Lab is chosen, the patched ones come back; the config
       is re-read and re-applied; the choice is saved in the options file (payload +0x1D) and survives a new session (stored Lab is applied after the file is read)
     1 default (no options byte): the patched server wins as before, the card's identity is dropped (guest), no re-read
     2 an unlocked build: the Server choice changes nothing, no re-read
     3 Slippi: Our mock (default) / Real Slippi reach the bridge accessor at once, the choice and the one-time confirm are saved (+0x1E = 1, 3), the confirm survives Real -> mock
     4 a stored byte from an older block (0) and 0xFF read as the defaults; the Real choice stored with the confirm is applied when the file is read
     5 the re-read finds the card busy: tried again later, at most four times, then the config in force stays
     6 owner bug (linked on the server, card key 1 still the lab key): the identity is the Production slot's
     7 IDENTITY SLOTS: link on Production, switch to Dev and Lab and back; the old single identity moves to the Lab slot (lab build, prod selected, unknown origin),
       every environment keeps its own identity, a link/delete acts on the active slot only, the old single-identity bytes and the record's CRC are not touched, the file stays one block
     8 a card that already holds an SDSL: Production / Dev / Lab each read their own identity at once; a delete (all-zero device id) clears only the active slot and survives a re-read
     9 an unlocked build keeps the old single identity exactly (no SDSL is made, the legacy fields are written) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define OMC_HOST 1
#include "on_card.c"
#include "omc_fake.inc"

static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)
#define PAY(f) ((f)->data + 0x1000)

static int g_real = -1, g_reapply;
void sbr_nc_set_real(int on) { g_real = on; }
void ml_cfg_reapply(void) { g_reapply++; }

#define VPS_IP 0xC63364D6u                      /* 198.51.100.214 */
#define LAB_IP 0x0AD56E02u                      /* 10.213.110.2 */
static void be32(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

/* a valid MultishineOnline block: server LAB_IP, pin = bytes 1..32, an identity, a code and a name */
static void put_online2(int chan, int vps_pin2);
static void put_online2(int chan, int vps_pin2)
{
    unsigned char blk[0x2000];
    unsigned char *r = blk + ONR_PAYLOAD_OFF;
    unsigned i;
    memset(blk, 0, sizeof blk);
    memcpy(blk, "Multishine", 10);
    be32(r, 0x53444F4Eu); r[4] = 1;
    be32(r + 0x0C, LAB_IP); r[0x10] = 0xA7; r[0x11] = 0x8E;
    r[0x12] = 0; r[0x13] = 1;
    for (i = 0; i < 32; i++) r[0x20 + i] = (unsigned char)(i + 1);
    if (vps_pin2) for (i = 0; i < 32; i++) r[0x40 + i] = (unsigned char)(0xA0 + i);      /* pin 2 = the patched the server key: a link made on the server */
    for (i = 0; i < 8; i++) r[0x60 + i] = (unsigned char)(0x40 + i);
    for (i = 0; i < 16; i++) r[0x68 + i] = (unsigned char)(0x50 + i);
    memcpy(r + 0x78, "TESTER#1", 8);
    memcpy(r + 0x90, "Lab", 3);
    be32(r + 8, ch_crc32(r + 0x0C, ONR_SIZE - 0x0C));
    CHECK(fk_put(chan, "MultishineOnline", blk) != 0);
}
static void put_options(int chan, unsigned srv_byte, unsigned mmr_byte)
{
    struct ms_settings s;
    unsigned char blk[0x2000];
    memset(blk, 0, sizeof blk);
    memcpy(blk, "Multishine", 10);
    ms_set_defaults(&s, 2);
    ms_set_fill(blk + 0x1000, &s, 0);
    blk[0x1000 + OS_VAL_OFF + MSET_ID_SRV] = (unsigned char)srv_byte;
    blk[0x1000 + OS_VAL_OFF + MSET_ID_MMR] = (unsigned char)mmr_byte;
    { unsigned c = (unsigned)os_crc32(blk + 0x1000 + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM); be32(blk + 0x1008, c); }
    CHECK(fk_put(chan, "MultishineOptions", blk) != 0);
}
static void run_frames(unsigned from, unsigned n)
{
    unsigned f;
    for (f = from; f < from + n; f++) (void)ons_poll(f);
}
/* the patched block of the online profile: the server address, pin = 0xA0.., SRVLOCK; no identity */
static void baked(struct od_cfg *c, int locked)
{
    unsigned i;
    memset(c, 0, sizeof *c);
    c->magic0 = OD_CFG_MAGIC0; c->magic1 = OD_CFG_MAGIC1;
    if (locked) {
        c->flags = OD_CFGF_SRVLOCK; c->server_ip = VPS_IP; c->server_port = 43150u;
        for (i = 0; i < 32; i++) c->pin[0][i] = (unsigned char)(0xA0 + i);
    }
}
static int is_vps(const struct od_cfg *c) { return c->server_ip == VPS_IP && c->server_port == 43150u && c->pin[0][0] == 0xA0 && c->pin[0][31] == 0xBF; }
static int is_lab(const struct od_cfg *c) { return c->server_ip == LAB_IP && c->server_port == 0xA78Eu && c->pin[0][0] == 1 && c->pin[0][31] == 32; }
static int has_ident(const struct od_cfg *c) { return c->dev[0] == 0x40 && c->dev[7] == 0x47 && c->tok[0] == 0x50 && !strcmp(c->code, "TESTER#1"); }
static int guest(const struct od_cfg *c) { unsigned i, a = 0; for (i = 0; i < 8; i++) a |= c->dev[i]; for (i = 0; i < 16; i++) a |= c->tok[i]; return a == 0; }

int main(int argc, char **argv)
{
    int sc = argc > 1 ? atoi(argv[1]) : 0;
    static struct od_cfg cfg;
    struct fk_file *f;
    fk_reset();
    K[0].present = 1;
    baked(&cfg, sc != 2 && sc != 9);
    put_online2(0, sc == 6);
    ons_menu_enter(2, 0);
    switch (sc) {
    case 0: {
        put_options(0, 0xFF, 0xFF);
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE && onc_final());
        CHECK(is_vps(&cfg) && guest(&cfg) && onc_netsel_locked() && onc_backup() == 0);        /* before the file is read: the lock rule as ever */
        run_frames(0, 130);
        CHECK(ons_st.load_status == ONS_L_OK && g_reapply == 0);
        CHECK(ons_change_srv(1, 200) == 1 && ons_cur()->srv == MSET_SRV_LAB);
        run_frames(200, 80);
        CHECK(is_lab(&cfg) && has_ident(&cfg) && g_reapply == 1 && onc_final());               /* Lab: the card record's address, pinned key and identity */
        CHECK(ons_change_srv(0, 300) == 1);
        run_frames(300, 80);
        CHECK(is_vps(&cfg) && guest(&cfg) && g_reapply == 2 && cfg.host[0] == 0);              /* back to the patched server */
        CHECK(ons_change_srv(1, 400) == 1);
        run_frames(400, 400);                                                                   /* re-read, then the options file is written */
        CHECK(is_lab(&cfg) && g_reapply == 3);
        f = fk_find(0, "MultishineOptions");
        CHECK(f && ms_set_check(PAY(f)) == MSET_OK && PAY(f)[OS_VAL_OFF + MSET_ID_SRV] == 1 && PAY(f)[OS_VAL_OFF + MSET_ID_MMR] == 0xFF);
        CHECK(ons_st.saves >= 1 && KF.bad_use == 0 && KF.open_files == 0);
        break; }
    case 1: {
        run_frames(0, 130);                                                                     /* no options file at all: NOFILE, defaults */
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        CHECK(is_vps(&cfg) && guest(&cfg));
        run_frames(130, 600);
        CHECK(g_reapply == 0 && is_vps(&cfg) && ons_cur()->srv == MSET_SRV_VPS);                /* nothing re-read while the choice equals the default */
        break; }
    case 2: {
        put_options(0, 1, 0xFF);
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        CHECK(is_lab(&cfg) && has_ident(&cfg) && !onc_netsel_locked());                         /* no lock patched: the card was always the server */
        run_frames(0, 130);
        CHECK(ons_cur()->srv == MSET_SRV_LAB);
        CHECK(ons_change_srv(0, 200) == 1);
        run_frames(200, 400);
        CHECK(is_lab(&cfg) && g_reapply == 0);                                                  /* nothing to switch to: no re-read, no re-apply */
        break; }
    case 3: {
        put_options(0, 0xFF, 0xFF);
        run_frames(0, 130);
        CHECK(g_real == 0);                                                                     /* the stored default reaches the bridge as 'mock' */
        CHECK(ons_change_mmr(1, 200) == 1 && g_real == 1 && ons_cur()->mmr == MSET_MMR_REAL);
        CHECK(ons_change_mmk(200) == 1 && ons_cur()->mmk == 1 && ons_change_mmk(200) == 0);
        run_frames(200, 400);
        f = fk_find(0, "MultishineOptions");
        CHECK(f && PAY(f)[OS_VAL_OFF + MSET_ID_MMR] == 3 && PAY(f)[OS_VAL_OFF + MSET_ID_SRV] == 0xFF);
        CHECK(ons_change_mmr(0, 700) == 1 && g_real == 0);
        run_frames(700, 400);
        CHECK(PAY(f)[OS_VAL_OFF + MSET_ID_MMR] == 2 && ons_cur()->mmk == 1);                    /* mock again, the answer to the confirm is remembered */
        CHECK(ons_st.saves >= 2 && ms_set_check(PAY(f)) == MSET_OK);
        break; }
    case 4: {
        put_options(0, 0, 3);                                                                   /* an older block (0) for the server, Real + confirmed for Slippi */
        run_frames(0, 130);
        CHECK(ons_cur()->srv == MSET_SRV_VPS && ons_cur()->mmr == MSET_MMR_REAL && ons_cur()->mmk == 1 && g_real == 1);
        break; }
    case 5: {
        put_options(0, 0xFF, 0xFF);
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        run_frames(0, 130);
        KF.idle = 0;
        CHECK(ons_change_srv(1, 200) == 1);
        run_frames(200, 600);
        CHECK(is_vps(&cfg) && g_reapply == 0 && g_rs_tries == RS_TRIES && onc_final());        /* four tries, the config in force stays, nothing left half-open */
        break; }
    case 6: {                                                                                   /* owner bug linked on the server, card pin 1 still the lab key */
        put_options(0, 0xFF, 0xFF);
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        run_frames(0, 130);
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        CHECK(is_vps(&cfg) && has_ident(&cfg) && onc_netsel_locked());                          /* locked to the server and still the card's identity */
        break; }
    case 7: case 8: {
        struct od_cfg nc;
        unsigned char before[0x2000];
        unsigned i;
        put_options(0, 0xFF, 0xFF);
        f = fk_find(0, "MultishineOnline");
        if (sc == 8) {                                                                          /* a card that already holds three different identities */
            unsigned char id[0x48];
            for (i = 0; i < 3; i++) {
                memset(id, 0, sizeof id);
                memset(id, (int)(0x61 + i), 8); memset(id + 8, (int)(0x71 + i), 16);
                memcpy(id + 24, i == 0 ? "PROD#1" : i == 1 ? "DEV#1" : "LAB#1", 6); memcpy(id + 48, i == 0 ? "Prod" : i == 1 ? "Dev" : "Lab", 4);
                CHECK(onr_slot_put(PAY(f), (int)i, 1, (const unsigned char *)cfg.pin[0], 0, id));       /* tags: prod = the patched key (in cfg now), dev / lab = the record's key 1 */
            }
        }
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        run_frames(0, 130);
        memcpy(before, f->data, sizeof before);
        CHECK(is_vps(&cfg));
        if (sc == 7) CHECK(guest(&cfg));                                                        /* Production: the foreign (lab) identity is not used */
        else CHECK(cfg.dev[0] == 0x61 && !strcmp(cfg.code, "PROD#1"));
        if (sc == 7) {                                                                          /* link on Production */
            for (i = 0; i < 8; i++) cfg.dev[i] = (unsigned char)(0x90 + i);
            for (i = 0; i < 16; i++) cfg.tok[i] = (unsigned char)(0x20 + i);
            strcpy(cfg.code, "PRD@777"); strcpy(cfg.name, "Prod");
            CHECK(onc_save_ident(&cfg) == 0);
            CHECK(memcmp(f->data, before, ONR_PAYLOAD_OFF + 0x140) == 0);                       /* record, CRC, host extension and the old single identity: byte for byte */
            CHECK(onr_check2(PAY(f)) == ONR_OK);
        }
        CHECK(ons_change_srv(1, 200) == 1);                                                     /* Dev */
        run_frames(200, 80);
        CHECK(is_lab(&cfg) && g_reapply == 1);
        if (sc == 7) CHECK(guest(&cfg));                                                        /* the old identity went to Lab, Dev has none */
        else CHECK(cfg.dev[0] == 0x62 && !strcmp(cfg.code, "DEV#1"));
        CHECK(ons_change_srv(2, 300) == 1);                                                     /* Lab */
        run_frames(300, 80);
        CHECK(is_lab(&cfg) && g_reapply == 2);
        if (sc == 7) CHECK(has_ident(&cfg));
        else CHECK(cfg.dev[0] == 0x63 && !strcmp(cfg.code, "LAB#1"));
        CHECK(ons_cur()->srv == MSET_SRV_LAN);
        f = fk_find(0, "MultishineOptions");
        run_frames(400, 400);
        CHECK(f && PAY(f)[OS_VAL_OFF + MSET_ID_SRV] == 2);                                      /* the choice is stored as 2 */
        {   /* link on Lab: only the Lab slot changes */
            unsigned char snap[0x2000];
            struct fk_file *g = fk_find(0, "MultishineOnline");
            memcpy(snap, g->data, sizeof snap);
            for (i = 0; i < 8; i++) cfg.dev[i] = (unsigned char)(0xC0 + i);
            strcpy(cfg.code, "LAB@555");
            CHECK(onc_save_ident(&cfg) == 0);
            CHECK(memcmp(snap + ONR_PAYLOAD_OFF + 0x140 + 0x0C, g->data + ONR_PAYLOAD_OFF + 0x140 + 0x0C, 2 * 0x50) == 0);   /* Production and Dev entries: unchanged */
            CHECK(memcmp(snap + ONR_PAYLOAD_OFF + 0x140 + 0x0C + 2 * 0x50, g->data + ONR_PAYLOAD_OFF + 0x140 + 0x0C + 2 * 0x50, 0x50) != 0);
            /* delete on Lab: an all-zero device id clears the Lab slot only */
            memset(cfg.dev, 0, 8); memset(cfg.tok, 0, 16);
            CHECK(onc_save_ident(&cfg) == 0);
            CHECK(memcmp(snap + ONR_PAYLOAD_OFF + 0x140 + 0x0C, g->data + ONR_PAYLOAD_OFF + 0x140 + 0x0C, 2 * 0x50) == 0);
            for (i = 0; i < 0x50; i++) CHECK(g->data[ONR_PAYLOAD_OFF + 0x140 + 0x0C + 2 * 0x50 + i] == 0);
        }
        CHECK(ons_change_srv(0, 900) == 1);                                                     /* back to Production: its identity is intact */
        run_frames(900, 80);
        CHECK(is_vps(&cfg));
        if (sc == 7) CHECK(cfg.dev[0] == 0x90 && !strcmp(cfg.code, "PRD@777"));
        else CHECK(cfg.dev[0] == 0x61 && !strcmp(cfg.code, "PROD#1"));
        memset(&nc, 0, sizeof nc);
        CHECK(KF.bad_use == 0 && KF.open_files == 0);
        break; }
    case 9: {                                                                                   /* unlocked build: the old single identity, no SDSL */
        unsigned i;
        unsigned char *r;
        put_options(0, 0xFF, 0xFF);
        CHECK(onc_on_status(&cfg) == OD_WHY_NONE);
        run_frames(0, 130);
        CHECK(is_lab(&cfg) && has_ident(&cfg));
        for (i = 0; i < 8; i++) cfg.dev[i] = (unsigned char)(0x90 + i);
        strcpy(cfg.code, "UNL@999");
        CHECK(onc_save_ident(&cfg) == 0);
        f = fk_find(0, "MultishineOnline");
        r = PAY(f);
        CHECK(onr_check2(r) == ONR_OK && r[0x60] == 0x90 && !strcmp((char *)r + 0x78, "UNL@999") && onr_slots_state(r) == 0);
        break; }
    default: printf("unknown scenario\n"); return 2;
    }
    if (fails) { printf("netsel_test %d: %d FAILED\n", sc, fails); return 1; }
    printf("netsel_test %d ok\n", sc);
    return 0;
}
