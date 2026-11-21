/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_card: see on_card.h.  Same CARD SDK call sequence as om_card.c (its fork on_hist.c is included below, so card_mount / wa / buf / g_fi / omc_melee_idle are the proven
 * ones): probe (BUSY retried) -> sector size 0x2000 -> mount -> open "MultishineOnline" -> read the one block -> close -> unmount on EVERY path.  Read only. */
#include "on_card.h"
#include "on_hist.c"           /* the fork of om_card.c with the slot policy (BAKED_PLUS_CARD 7); on_card.h first: it declares onc_st for on_hist.c */
#include "on_cfgrec.h"

#define ONC_NAME "MultishineOnline"
#define ONC_ERROR_TRIES 3

struct onc_status onc_st OM_DATA = { 0x4F4E4346u, 0, 0, 0xFFu, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static const char g_cname[] = ONC_NAME;
static int g_cdone OM_DATA;
static unsigned int g_first_tb OM_DATA;           /* IDENT SETTLE: the time base of the first attempt */
static int g_noreg OM_DATA;                       /* 1: a good card without MultishineOnline and a server-locked build: the console runs as a guest of the patched server (RAM only); the first Link creates the record */

/* SOFT FINAL: a verdict that is only "the card layer was busy / read errors for ONC_SETTLE_MS or ONC_ERROR_TRIES" is not a fact about the card, it is what a slow
   card load (Melee's own save scan on a Wii in single-context mode takes tens of seconds) does to the one-shot budget.  Such a verdict is kept as soft: onc_final lifts it by itself after a
   growing pause (2, 4, 8, 16, then 30 s) so the callers that ask `!onc_final()` read the setup again, and Reconnect lifts it at once (onc_rearm(1)).  Definite verdicts (loaded, no file, damaged,
   newer, incomplete) stay final. */
static int g_soft OM_DATA;                        /* 1: g_cdone was set by the busy / error budget */
static unsigned int g_soft_tb OM_DATA, g_soft_n OM_DATA;
void onc_rearm(int force)
{
    unsigned int need = 2000u << (g_soft_n < 4u ? g_soft_n : 4u);
    if (need > 30000u) need = 30000u;
    if (!g_cdone || !g_soft) return;
    if (!force && (unsigned int)(tbl() - g_soft_tb) / 40500u < need) return;
    g_cdone = 0; g_soft = 0; g_first_tb = 0; g_soft_n++;
    onc_st.tries = 0; onc_st.done = 0;
}
int onc_final(void) { onc_rearm(0); return g_cdone; }
int onc_soft(void) { return g_cdone && g_soft; }

/* which condition of omc_melee_idle fails (bit 0: card task busy words, bit 1: a task slot not idle, bit 2: lbcg busy, bit 3: card block busy); 0 = idle */
static unsigned int idle_mask(void)
{
#ifdef OMC_HOST
    return omc_melee_idle() ? 0u : 1u;
#else
    unsigned int m = 0;
    int i;
    if (*(volatile s32 *)(LBCN_STATE + 0x80) || *(volatile s32 *)(LBCN_STATE + 0x8AC)) m |= 1u;
    for (i = 0; i < 11; i++)
        if (*(volatile s32 *)(LBCN_STATE + 0x510 + 0x54 * i) != 14) m |= 2u;
    if (*(volatile s32 *)(LBCG_STATE + 0xC) || *(volatile s32 *)(LBCG_STATE + 0x10)) m |= 4u;
    if (*(volatile s32 *)CARD_BLOCK || *(volatile s32 *)(CARD_BLOCK + 0x110)) m |= 8u;
    return m;
#endif
}

/* read the setup block of chan into buf: 1 = read, 0 = no file, -1000 = no card, other < 0 = SDK error.  Always unmounts. */
static int read_cfg_chan(s32 chan, s32 *res)
{
    s32 r = card_mount(chan);
    int got;
    *res = r;
    if (r != 0) return r == R_NOCARD ? -1000 : (r < 0 ? r : -1);
    r = CARDOpen(chan, g_cname, &g_fi);
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

/* NUL padded text field of n bytes */
static void put_text24(unsigned char *d, const char *s)
{
    unsigned int i;
    for (i = 0; i < 24; i++) d[i] = 0;
    for (i = 0; i < 23 && s[i]; i++) d[i] = (unsigned char)s[i];
}

static u32 g_backup OM_DATA;
unsigned int onc_backup(void) { return g_backup; }

#ifdef ONS_NETSEL
/* Settings > Online > Server: in a server-locked build the patched server wins unless the player chose Lab.  The patched server fields are kept in
   a copy at the first card decode, so that the choice can be flipped back and forth without a reboot: every decode starts from them. */
struct nsel_bk { u32 ip, port, host_flag; unsigned char pin[2][32]; char host[OD_HOST_MAX]; };
static struct nsel_bk g_bk OM_DATA;
static struct od_cfg *g_ncfg OM_DATA;                 /* the block the card layer decodes into (handed in by every onc_on_status) */
static int g_bk_have OM_DATA, g_srv_have OM_DATA, g_srv_used OM_DATA;   /* g_srv_used: MSET_SRV_* of the last decode */
static void netsel_decode(struct od_cfg *cfg, const unsigned char *r)
{
    int lab = 0, env = 0;
    g_ncfg = cfg;
    if (cfg->flags & OD_CFGF_SRVLOCK) {
        if (!g_bk_have) {
            g_bk.ip = cfg->server_ip; g_bk.port = cfg->server_port; g_bk.host_flag = cfg->flags & OD_CFGF_HOST;
            onr_cpz(g_bk.pin, cfg->pin[0], 64); onr_cpz(g_bk.host, (const unsigned char *)cfg->host, OD_HOST_MAX);
            g_bk_have = 1;
        } else {
            cfg->server_ip = g_bk.ip; cfg->server_port = g_bk.port; cfg->flags = (cfg->flags & ~OD_CFGF_HOST) | g_bk.host_flag;
            onr_cpz(cfg->pin, g_bk.pin[0], 64); onr_cpz(cfg->host, (const unsigned char *)g_bk.host, OD_HOST_MAX);
        }
        env = (int)ons_cur()->srv;                                      /* MSET_SRV_*: 0 Production, 1 Dev, 2 Lab */
        lab = env != MSET_SRV_VPS;
    }
    onr_decode_lab(cfg, r, lab);
    onr_decode_host_lab(cfg, r, lab);                                    /* the optional server name */
    if (cfg->flags & OD_CFGF_SRVLOCK) onr_decode_slot(cfg, r, env, 1, g_bk.pin[0], g_bk.pin[1]);   /* one identity per environment */
    g_backup = ((cfg->flags & OD_CFGF_SRVLOCK) && !lab) ? 0u : onr_backup(r);
    g_srv_used = env; g_srv_have = 1;
}
const struct od_cfg *onc_netsel_cfg(void) { return g_ncfg; }
int onc_netsel_locked(void) { return g_ncfg && (g_ncfg->flags & OD_CFGF_SRVLOCK) ? 1 : 0; }
#endif

/* the identity of c as the 0x48 byte group of a slot (device id, secret, code, name) */
static void ident_group(unsigned char *g, const struct od_cfg *c)
{
    onr_cpz(g, c->dev, 24);                                              /* dev, tok: contiguous in the config (on_cfgrec.c put_ident) */
    put_text24(g + 24, c->code);
    put_text24(g + 48, c->name);
}

/* FRESH CARD (the system-files builder never writes MultishineOnline,  rule 3): make the guest record (onr_make_guest: patched server, patched key, NO secret) so that the Link has a record to save the identity in.
   Only after a link succeeded (onc_save_ident), never at boot.  0 = made and verified (onc_st.slot / verdict are set), 1 = card busy, -2 = failed (no room, create or write error). */
static int onc_create_rec(const struct od_cfg *c)
{
    s32 rc;
    int out = -2, first, k;
    if (!omc_melee_idle() || !WA_GET(1)) return 1;
    first = melee_save_slot();
    if (first < 0) first = 0;
    for (k = 0; k < 2 && out != 0; k++) {
        s32 chan = k == 0 ? first : 1 - first;
        if (card_mount(chan) != 0) continue;                    /* card_mount leaves a failed channel unmounted */
        rc = CARDOpen(chan, g_cname, &g_fi);
        if (rc == R_READY) CARDClose(&g_fi);                          /* it exists after all (the card changed): leave it, the next boot reads it */
        else if (rc == R_NOFILE && room_ok(chan, &rc)) {
            rc = CARDCreateAsync(chan, g_cname, CH_BLOCK, &g_fi, CARDSyncCallback);
            if (rc >= 0) rc = CARDSync(chan);
            if (rc == R_READY) {
                struct card_stat st;
                u8 *b = buf();
                u32 i;
                for (i = 0; i < CH_BLOCK; i++) b[i] = 0;
                for (i = 0; i < 31 && "Multishine"[i]; i++) b[i] = (u8)"Multishine"[i];
                for (i = 0; i < 31 && "Online setup (secret)"[i]; i++) b[32 + i] = (u8)"Online setup (secret)"[i];
                onr_make_guest(b + ONR_PAYLOAD_OFF, c);
                rc = CARDWrite(&g_fi, b, CH_BLOCK, 0);
                if (rc == R_READY) rc = CARDGetStatus(chan, g_fi.fileNo, &st);
                if (rc == R_READY) {
                    st.commentAddr = 0; st.iconAddr = 0xFFFFFFFFu; st.iconFormat = 0; st.iconSpeed = 0; st.bannerFormat = 0;
                    rc = CARDSetStatusAsync(chan, g_fi.fileNo, &st, CARDSyncCallback);
                    if (rc >= 0) rc = CARDSync(chan);
                }
                if (rc == R_READY) {
                    onr_cpz(b, 0, CH_BLOCK);
                    rc = CARDRead(&g_fi, b, CH_BLOCK, 0);                      /* read back and check */
                    if (rc == R_READY && onr_check2(b + ONR_PAYLOAD_OFF) == ONR_OK) { out = 0; onc_st.slot = (u32)chan + 1u; onc_st.verdict = ONR_OK; g_noreg = 0; }
                }
                CARDClose(&g_fi);
            }
        }
        CARDUnmount(chan);
    }
    WA_PUT();
    return out;
}

int onc_save_ident(const struct od_cfg *c)
{
    unsigned char *r;
    s32 chan, rc;
    u32 i, crc;
    int out = -2, slots = (c->flags & OD_CFGF_SRVLOCK) != 0;     /* a server-locked build keeps one identity per environment; an unlocked one has the card's single server and identity */
    const unsigned char *bk0 = c->pin[0], *bk1 = c->pin[1], *got;
    int env = 0, labb = 0;
    unsigned char grp[0x48];
    onc_st.ident_last = 3;
    if (onc_st.slot == 0 || onc_st.verdict != ONR_OK) {
        if (!g_noreg) return -1;
        out = onc_create_rec(c);                                   /* a guest of the patched server links: the record the Link saves into is made now */
        if (out != 0) { onc_st.ident_last = (u32)(out == 1 ? 5 : 2); return out; }
        out = -2;
    }
    onc_st.ident_last = 5;
    if (!omc_melee_idle() || !WA_GET(1)) return 1;
#ifdef ONS_NETSEL
    if (g_bk_have) { bk0 = g_bk.pin[0]; bk1 = g_bk.pin[1]; }
    env = g_srv_used; labb = 1;
#endif
    ident_group(grp, c);
    r = buf() + ONR_PAYLOAD_OFF;
    chan = (s32)onc_st.slot - 1;
    if (card_mount(chan) == 0) {
        if (CARDOpen(chan, g_cname, &g_fi) == R_READY) {
            rc = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
            if (rc == R_READY && onr_check2(r) == ONR_OK && (slots ? onr_slot_put(r, env, labb, bk0, bk1, grp) : 1)) {
                if (!slots) {
                    onr_cpz(r + 0x60, grp, 0x48);
                    crc = ch_crc32(r + 0x0C, ONR_SIZE - 0x0C);
                    for (i = 0; i < 4; i++) r[8 + i] = (unsigned char)(crc >> (24 - 8 * i));
                }
                rc = CARDWrite(&g_fi, buf(), CH_BLOCK, 0);
                if (rc == R_READY) {
                    onr_cpz(r + 0x60, 0, 8);                                         /* the read-back must bring them again */
                    if (slots) onr_cpz(r + ONR_SL_OFF, 0, ONR_SL_SIZE);
                    rc = CARDRead(&g_fi, buf(), CH_BLOCK, 0);
                    if (rc == R_READY && onr_check2(r) == ONR_OK) {
                        out = 0;
                        got = r + 0x60;
                        if (slots && !onr_slot_get(r, env, labb, bk0, bk1, &got)) got = 0;      /* an all-zero device id clears the slot: nothing may come back */
                        for (i = 0; i < 8; i++) if ((got ? got[i] : 0) != c->dev[i]) out = -2;
                    }
                }
            }
            CARDClose(&g_fi);
        }
        CARDUnmount(chan);
    }
    WA_PUT();
    onc_st.ident_last = (u32)(out + 4);
    if (out == 0) onc_st.ident_saved = 1;
    return out;
}

int onc_on_status(struct od_cfg *cfg)
{
    s32 res[2];
    int got[2], chan, found = -1, verdict[2], cards = 0, why, sv[2], unusable = 0, errors = 0, nofiles = 0;
    u32 t0;
    if (g_cdone) return (int)onc_st.why;
    t0 = tbl();
    if (!g_first_tb) g_first_tb = t0 | 1u;
    else if ((unsigned int)((t0 | 1u) - g_first_tb) >= ONC_SETTLE_MS * 40500u && onc_st.why != OD_WHY_NOCARD && onc_st.why != OD_WHY_NONE) {   /* IDENT SETTLE: the same moment as the bridge's (sbr_card.h); a missing card is asked again */
        g_cdone = 1; g_soft = 1; g_soft_tb = t0;
        onc_st.done = 1u;
        return (int)onc_st.why;
    }
    res[0] = res[1] = R_NOCARD;
    got[0] = got[1] = -1000;
    verdict[0] = verdict[1] = -1;
    sv[0] = sv[1] = ONH_SV_NONE;
    onc_st.tries++;
    onc_st.idle_mask = idle_mask();
    if (!omc_melee_idle()) {                       /* Melee is using the card: try at the next menu command */
        onc_st.why = OD_WHY_BUSY;
        return OD_WHY_BUSY;
    }
    if (!WA_GET(1)) {                              /* no CARD work area (the other user holds it, or no heap room): the same 'try at the next menu command', not a try */
        onc_st.tries--;
        onc_st.why = OD_WHY_BUSY;
        return OD_WHY_BUSY;
    }
    why = OD_WHY_NOFILE;
    onc_st.verdict = 0xFFu;
    for (chan = 0; chan < 2 && found < 0; chan++) {
        got[chan] = read_cfg_chan(chan, &res[chan]);
        if (got[chan] != -1000) cards++;
        if (got[chan] == 1) {
            verdict[chan] = onr_check2(buf() + ONR_PAYLOAD_OFF);
            found = chan;
            onc_st.verdict = (u32)verdict[chan];
            onc_st.slot = (u32)chan + 1u;
            if (verdict[chan] == ONR_OK) {
#ifdef ONS_NETSEL
                netsel_decode(cfg, buf() + ONR_PAYLOAD_OFF);          /* lab/dev builds: the same steps, honouring Settings > Online > Server */
#else
                onr_decode(cfg, buf() + ONR_PAYLOAD_OFF);
                onr_decode_host(cfg, buf() + ONR_PAYLOAD_OFF);        /* the optional server name */
                if (cfg->flags & OD_CFGF_SRVLOCK) onr_decode_slot(cfg, buf() + ONR_PAYLOAD_OFF, 0, 0, cfg->pin[0], cfg->pin[1]);   /* the Production slot */
                g_backup = (cfg->flags & OD_CFGF_SRVLOCK) ? 0u : onr_backup(buf() + ONR_PAYLOAD_OFF);   /* the optional backup server ( C4); a server-locked build ignores the card's */
#endif
                why = OD_WHY_NONE;
                sv[chan] = ONH_SV_VALID;
            } else if (verdict[chan] == ONR_NEWER) { why = OD_WHY_NEWER; sv[chan] = ONH_SV_NEWER; }
            else if (verdict[chan] == ONR_INCOMPLETE) { why = OD_WHY_INCOMPLETE; sv[chan] = ONH_SV_DAMAGED; }
            else { why = OD_WHY_DAMAGED; sv[chan] = ONH_SV_DAMAGED; }
        } else if (got[chan] == 0) {
            sv[chan] = ONH_SV_NOFILE;
            nofiles++;
        } else if (slot_unusable(got[chan])) {     /* no card / BROKEN / unformatted / ENCODING / WRONGDEVICE: skipped, the other slot decides */
            sv[chan] = ONH_SV_UNUSABLE;
            unusable++;
        } else {
            sv[chan] = ONH_SV_ERROR;
            errors++;
        }
    }
    WA_PUT();                                      /* the decoded setup is in cfg; nothing below reads buf */
    onc_st.res = ((u32)res[0] << 16) | ((u32)res[1] & 0xFFFFu);
    onc_st.ticks = tbl() - t0;
    onc_st.set_sv = (u32)sv[0] | ((u32)sv[1] << 4);
    onc_st.set_unusable = (u32)unusable;
    if (found >= 0) {
        g_cdone = 1;
    } else if (!cards) {
        why = OD_WHY_NOCARD;                       /* a card may be inserted later: asked again at the next menu load */
    } else if (errors == 0 && nofiles > 0) {
        g_cdone = 1;                               /* a good card without the file (the other slot, if any, was unusable and skipped) */
        if (cfg->flags & OD_CFGF_SRVLOCK) {        /* the seeded card ( rule 3 never writes MultishineOnline): the patched server and key are all a guest needs.  RAM only; the first Link makes the record */
            onr_guest(cfg);
            g_backup = 0u;
            g_noreg = 1;
            why = OD_WHY_NONE;
        }
    } else {                                       /* a hard error on a slot, or every card present is unusable: no definite answer */
        why = OD_WHY_ERROR;
        if (onc_st.tries >= ONC_ERROR_TRIES) { g_cdone = 1; g_soft = 1; g_soft_tb = tbl(); }
    }
    onc_st.why = (u32)why;
    onc_st.done = (u32)g_cdone;
#ifdef MS_CARD_REPORT                              /* diagnostics for a debugger / Dolphin log only (OSReport prints nowhere on a console): compiled out of the release engine, which sits 1 KB under its card hole */
    OSReport("SDON setup: why=%d verdict=%d slot=%d card=%d/%d sv=%d%d\n", why, (int)onc_st.verdict, (int)onc_st.slot, (int)res[0], (int)res[1], sv[0], sv[1]);
#else
    (void)res; (void)sv;
#endif
    return why;
}

#ifdef ON_DIAG
static unsigned int put_dec(char *o, unsigned int cap, unsigned int n, unsigned int v)
{
    char t[12];
    unsigned int k = 0;
    if (v == 0) t[k++] = '0';
    while (v && k < sizeof t) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k && n + 1 < cap) o[n++] = t[--k];
    return n;
}
static unsigned int put_hex(char *o, unsigned int cap, unsigned int n, unsigned int v)
{
    int i;
    int started = 0;
    for (i = 28; i >= 0; i -= 4) {
        unsigned int d = (v >> i) & 15u;
        if (d || started || i == 0) { started = 1; if (n + 1 < cap) o[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); }
    }
    return n;
}
static unsigned int put_s(char *o, unsigned int cap, unsigned int n, const char *s)
{
    while (*s && n + 1 < cap) o[n++] = *s++;
    return n;
}
/* compact form for the user name line of the CSS (B9 answers at every menu load / CSS entry): "L2 W1 n1 B4h3 A1c1 w0" = history load status, write status, codes in RAM,
   BE calls and h = of which with a suggestion, adds = codes confirmed and c = of which changed the list, setup status (0 = loaded from the card).
   Letters and digits only: Melee's text drawing swallows '/' and everything up to the next space (seen in Dolphin) and '|'. */
void onc_diag_short(char *o, unsigned int cap)
{
    unsigned int n = 0;
    if (cap == 0) return;
    n = put_s(o, cap, n, "L"); n = put_dec(o, cap, n, omc_st.load_status);
    n = put_s(o, cap, n, " W"); n = put_dec(o, cap, n, omc_st.write_status);
    n = put_s(o, cap, n, " n"); n = put_dec(o, cap, n, omc_st.count);
    n = put_s(o, cap, n, " B"); n = put_dec(o, cap, n, omc_st.be); n = put_s(o, cap, n, "h"); n = put_dec(o, cap, n, omc_st.be_hits);
    n = put_s(o, cap, n, " A"); n = put_dec(o, cap, n, omc_st.adds); n = put_s(o, cap, n, "c"); n = put_dec(o, cap, n, omc_st.add_changes);
    n = put_s(o, cap, n, " w"); n = put_dec(o, cap, n, onc_st.why);
    o[n] = 0;
}
/* the long text of the confirm box: "SDCH L2 s0 W1 r0 n1 be4 hit3 add1 chg1 lrFFFCFFFD SDON w2 idle0 lv22 sv22 ws1 P A r0 m128 s2000 B rFFFFFFFD m0 s0":
   history load status, slot, write status, last write SDK result (hex), codes in RAM, BE calls / of which with a suggestion, codes confirmed / of which changed the list,
   load SDK results (slot A and B, 16 bits each, hex), setup status, Melee card-layer idle mask now, per-slot verdict digits (A then B) of the history load (lv) and the setup
   read (sv): 0 not looked at, 1 file valid, 2 no file on a good card, 3 damaged, 4 newer, 5 unusable (skipped), 6 error; ws = slot of the last write; then a fresh probe of
   both slots: SDK result (hex), memory size in Mbit and SECTOR SIZE (Melee needs 2000).  No '/' or '|': see above.  The box shows about 113 characters (the rest is cut by the
   layout), so the tries (lt / wt), the setup verdict and the idle mask at the last read are left to the RAM block (onc_st, omc_st). */
void onc_diag_text(char *o, unsigned int cap)
{
    unsigned int n = 0;
    if (cap == 0) return;
    n = put_s(o, cap, n, "SDCH L"); n = put_dec(o, cap, n, omc_st.load_status);
    n = put_s(o, cap, n, " s"); n = put_dec(o, cap, n, omc_st.load_slot);
    n = put_s(o, cap, n, " W"); n = put_dec(o, cap, n, omc_st.write_status);
    n = put_s(o, cap, n, " r"); n = put_hex(o, cap, n, omc_st.write_res);
    n = put_s(o, cap, n, " n"); n = put_dec(o, cap, n, omc_st.count);
    n = put_s(o, cap, n, " be"); n = put_dec(o, cap, n, omc_st.be); n = put_s(o, cap, n, " hit"); n = put_dec(o, cap, n, omc_st.be_hits);
    n = put_s(o, cap, n, " add"); n = put_dec(o, cap, n, omc_st.adds); n = put_s(o, cap, n, " chg"); n = put_dec(o, cap, n, omc_st.add_changes);
    n = put_s(o, cap, n, " lr"); n = put_hex(o, cap, n, omc_st.load_res);
    n = put_s(o, cap, n, " SDON w"); n = put_dec(o, cap, n, onc_st.why);
    n = put_s(o, cap, n, " idle"); n = put_hex(o, cap, n, idle_mask());
    /* slot policy: per-slot verdict digits (A then B) of the last history load (lv) and setup read (sv), 5 = skipped as unusable, 6 = error, 2 = no file on a good card; ws = slot of the last write */
    n = put_s(o, cap, n, " lv"); n = put_dec(o, cap, n, onc_st.hist_sv & 15u); n = put_dec(o, cap, n, (onc_st.hist_sv >> 4) & 15u);
    n = put_s(o, cap, n, " sv"); n = put_dec(o, cap, n, onc_st.set_sv & 15u); n = put_dec(o, cap, n, (onc_st.set_sv >> 4) & 15u);
    n = put_s(o, cap, n, " ws"); n = put_dec(o, cap, n, omc_st.write_slot);
    {
        int chan;
        for (chan = 0; chan < 2; chan++) {
            s32 mem = 0, sector = 0, r = CARDProbeEx(chan, &mem, &sector);
            n = put_s(o, cap, n, chan ? " B r" : " P A r"); n = put_hex(o, cap, n, (u32)r);
            n = put_s(o, cap, n, " m"); n = put_dec(o, cap, n, (u32)mem); n = put_s(o, cap, n, " s"); n = put_hex(o, cap, n, (u32)sector);
        }
    }
    o[n] = 0;
}
#endif

#ifdef ONS_SETTINGS
#include "on_set.c"           /* the settings record layer (ons_*): same translation unit, shares the work area, block buffer and slot policy; only the combined image */
#endif
#ifdef ONS_DLOG
#include "on_dlog.c"          /* the network report file "MultishineDiag" (odl_*): same translation unit, shares the work area and slot policy */
#endif
