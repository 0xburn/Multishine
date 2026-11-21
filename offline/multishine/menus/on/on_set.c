/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_set: the settings record layer (on_card.h ons_*).  #included by on_card.c after on_hist.c, so the CARD work area, the block buffer, card_mount,
 * room_ok, melee_save_slot, slot_unusable and omc_melee_idle are the proven ones of the code-history layer (same call sequence, same slot policy:
 * BAKED_PLUS_CARD 7).  File "MultishineOptions" (SDOF v1, ONE block, payload at 0x1000, offline/options/optstore.h).
 *  - load: slot A then slot B, the first file that opens decides (valid: values taken; damaged: replaced by the next save; newer: never read, never written).
 *    Never creates anything.  Not final while there is no card, a card error or Melee's card layer is busy: retried (3 times per menu entry, every 2 s).
 *  - save: only when a value changed; target = the slot that holds the file, else the slot with Melee's save, else A, B; overwrite in place (comment, other
 *    option bytes and the chat table stay), or create.  Creating while Melee's save is missing needs room for it (11 blocks + 1 entry).  The record is
 *    read back and compared.  RATE LIMIT: a change waits ONS_DEBOUNCE frames for further changes, and two writes are at least ONS_MIN_GAP frames apart; after
 *    ONS_GIVE_UP failed passes nothing is tried again until the next menu entry.  Nothing here loops without a bound: the longest card call is the
 *    probe (BUSY retried for 2 s at most, per slot) of the history layer.
 *  - the values live in RAM first: with no card the menu still works, the change lasts until power off.  A change made before the card file could be read wins
 *    over the file's value for that field when the file is read later. */
#include "ml_set.h"
#include "optstore.h"

#ifndef ONS_NAME
#define ONS_NAME "MultishineOptions"
#endif
#define ONS_DEBOUNCE 90u              /* frames (1.5 s) after the last change */
#define ONS_MIN_GAP 180u              /* frames (3 s) between two writes */
#define ONS_BOOT_WAIT 6u              /* the same, in the boot scene (hub_res.c), where a hitch is invisible */
#define ONS_FIRST_WAIT 120u           /* frames after a menu entry before the first card read */
#define ONS_LOAD_GAP 120u             /* frames between load retries */
#define ONS_LOAD_TRIES 3u             /* per menu entry */
#define ONS_GIVE_UP 3u                /* failed save passes */
#define ONS_LINE1 "Multishine"
#define ONS_LINE2 "settings, no account data"

#ifdef HUB_OVL_RES
extern volatile unsigned int gns_off;                    /* menus/gns/gns_res.c: the Spectate Upload switch the resident uploader's loader reads (hub overlay builds only) */
#define GNS_OFF_SYNC() (gns_off = g_ss.specoff)
#else
#define GNS_OFF_SYNC() ((void)0)
#endif
struct ons_status ons_st OM_DATA = { .magic = 0x4F4E5345u };
static struct ms_settings g_ss OM_DATA;
static int g_ss_init OM_DATA, g_ss_final OM_DATA, g_ss_dirty OM_DATA, g_ss_newer OM_DATA, g_ss_damaged OM_DATA, g_ss_slot OM_DATA, g_ss_fails OM_DATA;
static unsigned g_ss_next_load OM_DATA, g_ss_visit_tries OM_DATA, g_ss_due OM_DATA, g_ss_last_save OM_DATA;
static const char g_sname[] = ONS_NAME;
/* the stored entropy seed: the 44-byte SDSD record (net/tls/sdent.h) at file offset 0x1800 of THIS block, outside the
   SDOF record and its CRC.  Kept as raw bytes here (the sign overlay parses and makes it); read with the record, written with the next save (whole-block writes keep it) */
#define ONS_SEED_OFF 0x1800u
#define ONS_SEED_LEN 44u
static unsigned char g_seed[ONS_SEED_LEN] OM_DATA;
static int g_seed_have OM_DATA, g_seed_dirty OM_DATA;
static void ss_seed_put(u8 *blk) { u32 i; if (g_seed_have) for (i = 0; i < ONS_SEED_LEN; i++) blk[ONS_SEED_OFF + i] = g_seed[i]; }

#ifdef ONS_NETSEL
extern void sbr_nc_set_real(int on);                  /* offline/bridge/sbr_netcfg.c: Real Slippi chosen = the card's mm_ip / dns override is not used */
extern void ml_cfg_reapply(void);                     /* on_backend.c: od_apply_config on the block the card layer just re-decoded (or at the hub overlay's next enter) */
static unsigned g_rs_next OM_DATA, g_rs_tries OM_DATA;
#define RS_TRIES 4u                                   /* re-read attempts per change of the server choice (Melee's card layer may be busy) */
#endif

static void ss_sync(void)
{
    ons_st.delay = g_ss.delay;
    ons_st.pool = g_ss.pool;
    ons_st.ws = g_ss.ws;
    ons_st.prog = g_ss.prog;
    ons_st.rbr = g_ss.rbr;
    GNS_OFF_SYNC();                                          /* Spectate Upload switch -> the resident uploader's loader (menus/gns/gns_res.c) */
    ons_st.flags = (unsigned)((g_ss_final ? 1 : 0) | (g_ss_dirty ? 2 : 0) | (g_ss_newer ? 4 : 0) | (g_ss_damaged ? 8 : 0) | (g_ss_fails >= (int)ONS_GIVE_UP ? 16 : 0));
    ons_st.slot = (unsigned)g_ss_slot;
}

const struct ms_settings *ons_cur(void) { return &g_ss; }

void ons_menu_enter(unsigned delay_default, unsigned frame)
{
    if (!g_ss_init) {
        g_ss_init = 1;
        ms_set_defaults(&g_ss, delay_default);
    }
    g_ss_visit_tries = 0;
    g_ss_fails = 0;
    g_ss_next_load = frame + ONS_FIRST_WAIT;             /* not in the first frames of the scene: the player is still settling, the card read can hitch */
    if (!g_ss_final) ons_st.load_status = ONS_L_NONE;
    ss_sync();
}

/* read our file's block on chan into buf: 1 = read, 0 = no file, -1000 = no card, other < 0 = SDK error.  Always unmounts. */
static int ss_read_chan(s32 chan, s32 *res)
{
    s32 r = card_mount(chan);
    int got;
    *res = r;
    if (r != 0) return r == R_NOCARD ? -1000 : (r < 0 ? r : -1);
    r = CARDOpen(chan, g_sname, &g_fi);
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
        got = r == R_READY ? 1 : (r < 0 ? r : -1);
        CARDClose(&g_fi);
    } else if (r == R_NOFILE) {
        got = 0;
    } else {
        got = r < 0 ? r : -1;
    }
    *res = r;
    CARDUnmount(chan);
    return got;
}

static unsigned ss_load(void)
{
    s32 res[2];
    int got[2], chan, found = -1, valid = -1, cards = 0, errors = 0, nofiles = 0, newer = 0, st[2];
    unsigned ev = 0;
    struct ms_settings t;
    u32 t0 = tbl();
    res[0] = res[1] = R_NOCARD;
    got[0] = got[1] = -1000;
    st[0] = st[1] = 0;
    ons_st.load_tries++;
    g_ss_visit_tries++;
    if (!omc_melee_idle()) {
        ons_st.load_status = ONS_L_BUSY;
        g_ss_next_load += ONS_LOAD_GAP;
        return 0;
    }
    if (!WA_GET(1)) {                              /* no CARD work area (another card user holds it, or no heap room): the same 'later', not a try */
        ons_st.load_tries--;
        g_ss_visit_tries--;
        ons_st.load_status = ONS_L_BUSY;
        g_ss_next_load += ONS_LOAD_GAP;
        return 0;
    }
    t = g_ss;
    for (chan = 0; chan < 2 && valid < 0 && !newer; chan++) {
        got[chan] = ss_read_chan(chan, &res[chan]);
        if (got[chan] != -1000) cards++;
        if (got[chan] == 1) {
            st[chan] = ms_set_check(buf() + CH_PAYLOAD_OFF);
            if (found < 0) found = chan;
            if (st[chan] == MSET_OK) {
                valid = chan;
                found = chan;
                (void)ms_set_decode(&t, buf() + CH_PAYLOAD_OFF);
                if (!g_seed_dirty) { u32 i; for (i = 0; i < ONS_SEED_LEN; i++) g_seed[i] = buf()[ONS_SEED_OFF + i]; g_seed_have = 1; }
            } else if (st[chan] == MSET_NEWER) {
                newer = 1;
                found = chan;
            }
        } else if (got[chan] == 0) {
            nofiles++;
        } else if (!slot_unusable(got[chan])) {
            errors++;
        }
    }
    WA_PUT();                                      /* the record is decoded into t; nothing below reads buf */
    ons_st.load_res = ((u32)res[0] << 16) | ((u32)res[1] & 0xFFFFu);
    ons_st.ticks = tbl() - t0;
    if (valid >= 0) {
        unsigned fc = t.from_card;
        if (g_ss.touched & MSET_T_WS) t.ws = g_ss.ws;
        if (g_ss.touched & MSET_T_DELAY) { t.delay = g_ss.delay; t.dman = g_ss.dman; }     /* a change made before the file could be read wins for its field */
        if (g_ss.touched & MSET_T_POOL) t.pool = g_ss.pool;
        if (g_ss.touched & MSET_T_PROG) t.prog = g_ss.prog;
        if (g_ss.touched & MSET_T_RBR) t.rbr = g_ss.rbr;
        if (g_ss.touched2 & MSET_T2_SPEC) { t.specoff = g_ss.specoff; t.specexp = 1; }
        if (g_ss.touched2 & MSET_T2_BAK) t.bakoff = g_ss.bakoff;
        if (g_ss.touched2 & MSET_T2_DBG) t.dbgl = g_ss.dbgl;
        if (g_ss.touched2 & MSET_T2_PING) t.pingon = g_ss.pingon;
        if (g_ss.touched2 & MSET_T2_HUD) { t.hudpos = g_ss.hudpos; t.hudraise = g_ss.hudraise; }
        if (g_ss.touched2 & MSET_T2_REC) t.recseed = g_ss.recseed;
#ifdef ONS_NETSEL
        if (g_ss.touched & MSET_T_SRV) t.srv = g_ss.srv;
        if (g_ss.touched & MSET_T_MMR) { t.mmr = g_ss.mmr; t.mmk = g_ss.mmk; }
#endif
        if (g_ss.touched2 & MSET_T2_PRELOAD) t.preload = g_ss.preload;
        if (g_ss.touched3 & MSET_T3_MUSIC) { t.musicoff = g_ss.musicoff; t.musicrand = g_ss.musicrand; }
        t.touched3 = g_ss.touched3;
        t.touched = g_ss.touched;
        t.touched2 = (unsigned char)(g_ss.touched2 | (t.touched2 & (MSET_T2_PING | MSET_T2_MIG)));   /* review M4: a card with Show ping in bit 6 of byte 16 is moved by the next save (once) */
        if (t.touched2 & MSET_T2_MIG) g_ss_dirty = 1;
        ev = ONS_EV_LOADED;                        /* also when nothing differs: the first final read is the moment the progressive-scan decision can be made (ml_set.h ms_prog_plan) */
        g_ss = t;
#ifdef ONS_NETSEL
        sbr_nc_set_real(g_ss.mmr);                 /* the stored Slippi choice reaches the bridge as soon as the file is read */
#endif
        ons_st.from_card = fc;
        g_ss_slot = valid + 1;
        g_ss_final = 1;
        ons_st.load_status = ONS_L_OK;
    } else if (found >= 0) {
        ev = ONS_EV_LOADED;
        g_ss_slot = found + 1;
        g_ss_final = 1;
        g_ss_newer = newer;
        g_ss_damaged = !newer;
        ons_st.load_status = newer ? ONS_L_NEWER : ONS_L_DAMAGED;
    } else if (!cards) {
        ons_st.load_status = ONS_L_NOCARD;
        g_ss_next_load += ONS_LOAD_GAP;
    } else if (errors == 0 && nofiles > 0) {
        ev = ONS_EV_LOADED;
        g_ss_final = 1;
        ons_st.load_status = ONS_L_NOFILE;
    } else {
        ons_st.load_status = ONS_L_ERROR;
        g_ss_next_load += ONS_LOAD_GAP;
    }
#if defined(MM_FTSTATS) || defined(MS_LOG_CARD)   /* log lines of the settings record: test and lab builds only */
    OSReport("SDOF settings: read status=%d slot=%d card=%d/%d delay=%d pool=%d\n", (int)ons_st.load_status, g_ss_slot, (int)res[0], (int)res[1], (int)g_ss.delay, (int)g_ss.pool);
#endif
    return ev;
}

/* read the record back and compare the value bytes of the touched fields */
static int ss_verify(s32 chan, s32 *res)
{
    s32 r = CARDOpen(chan, g_sname, &g_fi);
    int ok = 0;
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
        if (r == R_READY && ms_set_check(buf() + CH_PAYLOAD_OFF) == MSET_OK) {
            ok = 1;
            if ((g_ss.touched & MSET_T_WS) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_WS] != g_ss.ws) ok = 0;
            if ((g_ss.touched & MSET_T_DELAY) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_DELAY] != MSET_DELAY_BYTE(&g_ss)) ok = 0;
            if ((g_ss.touched & MSET_T_POOL) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_POOL] != g_ss.pool) ok = 0;
            if ((g_ss.touched & MSET_T_PROG) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_PROG] != g_ss.prog) ok = 0;
            if (((g_ss.touched & MSET_T_RBR) || (g_ss.touched2 & (MSET_T2_SPEC | MSET_T2_BAK | MSET_T2_DBG | MSET_T2_REC | MSET_T2_MIG))) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_RBR] != MSET_RBR_BYTE(&g_ss)) ok = 0;
            if ((g_ss.touched2 & (MSET_T2_PRELOAD | MSET_T2_PING)) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_PRELOAD] != MSET_PRELOAD_BYTE(&g_ss)) ok = 0;
#ifdef ONS_NETSEL
            if ((g_ss.touched & MSET_T_SRV) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_SRV] != g_ss.srv) ok = 0;
            if ((g_ss.touched & MSET_T_MMR) && buf()[CH_PAYLOAD_OFF + OS_VAL_OFF + MSET_ID_MMR] != (unsigned char)(g_ss.mmr | (g_ss.mmk ? MSET_MMR_ACK : 0u))) ok = 0;
#endif
        }
        CARDClose(&g_fi);
    }
    *res = r;
    return ok;
}

/* one write attempt on chan; returns ONS_W_* (or ONH_W_SKIP) */
static int ss_write_chan(s32 chan)
{
    struct card_stat st;
    s32 r = card_mount(chan);
    int out = ONS_W_ERROR, keep;
    ons_st.save_res = (u32)r;
    if (r != 0) return r == R_NOCARD ? ONS_W_NOCARD : (slot_unusable(r) ? ONH_W_SKIP : ONS_W_ERROR);
    ons_st.wslot = (u32)(chan + 1);
    r = CARDOpen(chan, g_sname, &g_fi);
    if (r == R_READY) {
        r = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
        if (r == R_READY) {
            keep = ms_set_check(buf() + CH_PAYLOAD_OFF);
            if (keep == MSET_NEWER) {
                out = ONS_W_NEWER;
                g_ss_newer = 1;
            } else {
                ms_set_fill(buf() + CH_PAYLOAD_OFF, &g_ss, keep == MSET_OK);
                if (g_seed_dirty) ss_seed_put(buf());
                r = CARDWrite(&g_fi, buf(), CH_BLOCK, 0);
                out = r == R_READY ? ONS_W_OK : ONS_W_ERROR;
            }
        }
        CARDClose(&g_fi);
    } else if (r == R_NOFILE) {
        if (!room_ok(chan, &r)) {
            out = r == R_READY ? ONS_W_RESERVE : ONS_W_ERROR;
        } else {
            r = CARDCreateAsync(chan, g_sname, CH_BLOCK, &g_fi, CARDSyncCallback);
            if (r >= 0) r = CARDSync(chan);
            if (r == R_INSSPACE || r == R_NOENT) {
                out = ONS_W_FULL;
            } else if (r == R_READY) {
                u32 i;
                u8 *b = buf();
                for (i = 0; i < CH_BLOCK; i++) b[i] = 0;
                for (i = 0; i < 31 && ONS_LINE1[i]; i++) b[i] = (u8)ONS_LINE1[i];
                for (i = 0; i < 31 && ONS_LINE2[i]; i++) b[32 + i] = (u8)ONS_LINE2[i];
                ms_set_fill(b + CH_PAYLOAD_OFF, &g_ss, 0);
                ss_seed_put(b);
                r = CARDWrite(&g_fi, b, CH_BLOCK, 0);
                if (r == R_READY) r = CARDGetStatus(chan, g_fi.fileNo, &st);
                if (r == R_READY) {
                    st.commentAddr = 0;
                    st.iconAddr = 0xFFFFFFFFu;
                    st.iconFormat = 0;
                    st.iconSpeed = 0;
                    st.bannerFormat = 0;
                    r = CARDSetStatusAsync(chan, g_fi.fileNo, &st, CARDSyncCallback);
                    if (r >= 0) r = CARDSync(chan);
                }
                out = r == R_READY ? ONS_W_OK : ONS_W_ERROR;
                CARDClose(&g_fi);
            }
        }
    }
    if (out == ONS_W_OK && !ss_verify(chan, &r)) out = ONS_W_VERIFY;
    ons_st.save_res = (u32)r;
    CARDUnmount(chan);
    return out;
}

static void ss_save(unsigned frame)
{
    u32 t0 = tbl();
    int out = ONS_W_NOCARD;
    if (g_ss_newer) { ons_st.save_status = ONS_W_NEWER; g_ss_dirty = 0; return; }
    if (!g_ss_final) { ons_st.save_status = ONS_W_NOLOAD; return; }
    if (!omc_melee_idle()) { ons_st.save_status = ONS_W_BUSY; g_ss_due = frame + 60u; return; }
    if (!WA_GET(1)) { ons_st.save_status = ONS_W_BUSY; g_ss_due = frame + 60u; return; }       /* no CARD work area: later (not a try) */
    ons_st.save_tries++;
    ons_st.wslot = 0;
    if (g_ss_slot) {
        out = ss_write_chan(g_ss_slot - 1);
        if (out == ONH_W_SKIP) out = ONS_W_ERROR;
    } else {
        u32 res = (u32)R_NOCARD, slot = 0;
        int first = melee_save_slot(), k, skipped = 0;
        if (first < 0) first = 0;
        for (k = 0; k < 2; k++) {
            int chan = k == 0 ? first : 1 - first;
            int r = ss_write_chan(chan);
            if (r == ONH_W_SKIP) { skipped = 1; res = ons_st.save_res; continue; }
            if (r != ONS_W_NOCARD) { out = r; res = ons_st.save_res; slot = ons_st.wslot; }
            if (r == ONS_W_OK || r == ONS_W_NEWER) break;
        }
        if (out == ONS_W_NOCARD && skipped) out = ONS_W_ERROR;
        ons_st.save_res = res;
        ons_st.wslot = slot;
    }
    WA_PUT();
    g_ss_last_save = frame;
    if (out == ONS_W_OK || out == ONS_W_VERIFY) {
        ons_st.saves++;
        g_ss_slot = (int)ons_st.wslot;
        g_ss_dirty = 0;
        g_seed_dirty = 0;
        g_ss.touched2 = 0;
        g_ss.touched3 = 0;
        g_ss.touched = 0;                          /* written: the file now holds these values */
        g_ss_fails = 0;
    } else if (out == ONS_W_NEWER) {
        g_ss_dirty = 0;
    } else {
        g_ss_fails++;
        g_ss_due = frame + ONS_MIN_GAP;
    }
    ons_st.save_status = (u32)out;
    ons_st.ticks = tbl() - t0;
#if defined(MM_FTSTATS) || defined(MS_LOG_CARD)   /* log lines of the settings record: test and lab builds only */
    OSReport("SDOF settings: write status=%d slot=%d card=%d delay=%d pool=%d\n", out, (int)ons_st.wslot, (int)ons_st.save_res, (int)g_ss.delay, (int)g_ss.pool);
#endif
}

#ifdef UPD_TITLE
/* The update check on the title (hub_res.c) needs the start-check flag (always on; 0 only from a bot card's skip byte, setting retired) before the first menu entry.  menu = 0 (the title): the record
   is read here, with the same rules as the menu's read (first try ONS_FIRST_WAIT frames in, ONS_LOAD_TRIES tries ONS_LOAD_GAP apart, Melee busy = later); the LOADED event is kept
   for the menu's first ons_poll, so the settings are applied exactly as before.  menu = 1: no card access, the value once the read is final.  Returns 1 on / 0 off; when no read
   could be made on the title (no card, errors, busy all the time) the default (on); -1 while the read is still pending. */
static int g_ss_held OM_DATA;
int ons_title_updchk(unsigned delay_default, unsigned frame, int menu)
{
    if (menu == 1) return g_ss_init && g_ss_final ? (int)g_ss.updchk : -1;
    if (!g_ss_init) {
        g_ss_init = 1;
        ms_set_defaults(&g_ss, delay_default);
        g_ss_next_load = frame + (menu == 2 ? ONS_BOOT_WAIT : ONS_FIRST_WAIT);     /* the boot scene (logos) has nothing to hitch: the stored Music Off must land before the first track */
    }
    if (!g_ss_final && g_ss_visit_tries < ONS_LOAD_TRIES) {
        if ((int)(frame - g_ss_next_load) >= 0 && ss_load()) g_ss_held = 1;
        if (!g_ss_final && g_ss_visit_tries < ONS_LOAD_TRIES) return -1;
    }
    ss_sync();
    return (int)g_ss.updchk;
}
int ons_title_final(void) { return g_ss_init && g_ss_final; }       /* 1 once the stored record has been read (hub_res.c: the progressive setting is applied before the title) */
#endif

#ifdef ONS_NETSEL
/* the Server choice differs from the one the last card decode used (changed in the menu, or the stored Lab choice was read after the config): read the MultishineOnline record again and
   apply it.  A locked build only; at most RS_TRIES tries a second apart per change.  A failed try leaves the config as it was. */
static void ss_netsel(unsigned frame)
{
    if (!g_ncfg || !g_srv_have || !(g_ncfg->flags & OD_CFGF_SRVLOCK) || g_srv_used == (int)g_ss.srv) { g_rs_tries = 0; return; }
    if (g_rs_tries >= RS_TRIES || (int)(frame - g_rs_next) < 0) return;
    g_rs_tries++;
    g_rs_next = frame + 60u;
    g_cdone = 0;
    if (onc_on_status(g_ncfg) == OD_WHY_NONE) ml_cfg_reapply();
    if (g_rs_tries >= RS_TRIES) g_cdone = 1;                                  /* out of tries: the config in force stays, nothing is left half-open */
}
#endif

unsigned ons_poll(unsigned frame)
{
    unsigned ev = 0;
    if (!g_ss_init) return 0;
#ifdef UPD_TITLE
    if (g_ss_held) { g_ss_held = 0; ev = ONS_EV_LOADED; }     /* read on the title: the menu applies it now */
#endif
    if (!g_ss_final && g_ss_visit_tries < ONS_LOAD_TRIES && (int)(frame - g_ss_next_load) >= 0)
        ev = ss_load();
    if (g_ss_dirty && g_ss_final && g_ss_fails < (int)ONS_GIVE_UP && (int)(frame - g_ss_due) >= 0 && (int)(frame - g_ss_last_save) >= (int)ONS_MIN_GAP)
        ss_save(frame);
#ifdef ONS_NETSEL
    if (g_ss_final) ss_netsel(frame);
#endif
    ss_sync();
    return ev;
}

static void ss_changed(unsigned frame)
{
    g_ss_dirty = 1;
    g_ss_due = frame + ONS_DEBOUNCE;
    if (!g_ss_final) ons_st.save_status = ONS_W_NOLOAD;
    ss_sync();
}

/* the stored entropy seed: write = 1 takes rec (44 bytes) for the next save; write = 0 copies the card's bytes into rec.  1 = done, 0 = none on the card, -1 = the record is not read yet */
int ons_seed(unsigned char *rec, unsigned frame, int write)
{
    u32 i;
    if (write) {
        for (i = 0; i < ONS_SEED_LEN; i++) g_seed[i] = rec[i];
        g_seed_have = 1; g_seed_dirty = 1;
        ss_changed(frame);
        return 1;
    }
    if (!g_ss_final) return -1;
    if (!g_seed_have) return 0;
    for (i = 0; i < ONS_SEED_LEN; i++) rec[i] = g_seed[i];
    return 1;
}

/* the Settings record setters below: one shared body (ss_change) instead of twelve identical ones; the same calls in the same order */
static int __attribute__((noinline)) ss_change(int (*set)(struct ms_settings *, unsigned), unsigned v, unsigned frame)
{
    if (!set(&g_ss, v)) return 0;
    ss_changed(frame);
    return 1;
}

int ons_change_delay(unsigned v, unsigned frame) { return ss_change(ms_set_delay, v, frame); }

int ons_change_ws(unsigned v, unsigned frame) { return ss_change(ms_set_ws, v, frame); }

int ons_change_prog(unsigned v, unsigned frame) { return ss_change(ms_set_prog, v, frame); }

int ons_change_rbr(unsigned v, unsigned frame) { return ss_change(ms_set_rbr, v, frame); }

int ons_change_spec(unsigned v, unsigned frame) { return ss_change(ms_set_spec, v, frame); }

int ons_change_dbg(unsigned v, unsigned frame) { return ss_change(ms_set_dbg, v, frame); }

int ons_change_ping(unsigned v, unsigned frame) { return ss_change(ms_set_ping, v, frame); }

int ons_change_hud(unsigned which, unsigned v, unsigned frame)       /* which 0: Ping and delay position, 1: Raise HUD */
{
    if (!ms_set_hud(&g_ss, which, v)) return 0;
    ss_changed(frame);
    return 1;
}

int ons_change_rec(unsigned v, unsigned frame) { return ss_change(ms_set_rec, v, frame); }

int ons_change_keep(unsigned v, unsigned frame) { return ss_change(ms_set_keep, v, frame); }
int ons_change_music(unsigned v, unsigned frame) { return ss_change(ms_set_music, v, frame); }

int ons_change_bak(unsigned v, unsigned frame) { return ss_change(ms_set_bak, v, frame); }

int ons_change_preload(unsigned v, unsigned frame) { return ss_change(ms_set_preload, v, frame); }

int ons_change_pool(unsigned v, unsigned frame) { return ss_change(ms_set_pool, v, frame); }

#ifdef ONS_NETSEL
int ons_change_srv(unsigned v, unsigned frame)
{
    if (!ms_set_srv(&g_ss, v)) return 0;
    g_rs_tries = 0; g_rs_next = frame;
    ss_changed(frame);
    return 1;
}

int ons_change_mmr(unsigned v, unsigned frame)
{
    if (!ms_set_mmr(&g_ss, v)) return 0;
    sbr_nc_set_real(g_ss.mmr);
    ss_changed(frame);
    return 1;
}

int ons_change_mmk(unsigned frame)
{
    if (!ms_set_mmk(&g_ss)) return 0;
    ss_changed(frame);
    return 1;
}
#endif

void ons_flush(unsigned frame)
{
    if (g_ss_dirty) g_ss_due = frame;
}
