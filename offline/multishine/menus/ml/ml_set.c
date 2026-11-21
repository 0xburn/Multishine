/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* ml_set: see ml_set.h. The record format and its CRC are .[ch] (SDOF v1), used unchanged. */
#include "ml_set.h"
#include "optstore.h"
#include "ml_rec_tab.h"

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char) (v >> 24);
    p[1] = (unsigned char) (v >> 16);
    p[2] = (unsigned char) (v >> 8);
    p[3] = (unsigned char) v;
}

unsigned char ms_byte_delay(const struct ms_settings *s)
{
    return ((unsigned char) (s->delay ? (0x10u | s->delay) : (s->dman ? s->dman : 2u)));
}

unsigned char ms_byte_rbr(const struct ms_settings *s)
{
    return ((unsigned char) (s->rbr | (s->specexp ? (s->specoff ? 2u : 8u) : 0u) | (s->bakoff << 2) | (s->dbgl << 4) | (s->recseed << 5)));
}

unsigned char ms_byte_preload(const struct ms_settings *s)
{
    return ((unsigned char) ((s->preload ? 1u : 0u) | (s->pingon ? 2u : 0u) | ((s->hudpos & 3u) << 2) | ((s->hudraise & 3u) << 4) | (s->keepoff ? 64u : 0u) | (s->musicoff ? 128u : 0u)));
}

void ms_set_defaults(struct ms_settings *s, unsigned delay_default)
{
    unsigned char *z = (unsigned char *) s;
    unsigned i;
    for (i = 0; i < sizeof *s; i++)                          /* everything off / zero: Widescreen off, Pool both, Spectate Upload ON, backup and keep ON, Music ON, srv the server, mmr mock, nothing touched (review L8: the lab fields are set in every build) */
        z[i] = 0;
    s->delay = (unsigned char) (delay_default <= MSET_DELAY_MAX ? delay_default : MSET_DELAY_AUTO);   /* 0 = Auto, the default since */
    s->dman = s->delay ? s->delay : 2u;                      /* the remembered manual value */
    s->updchk = 1;
    s->prog = MSET_PROG_FOLLOW;
}

int ms_set_check(const unsigned char *payload)
{
    return optstore_check(payload);
}

int ms_set_decode(struct ms_settings *s, const unsigned char *payload)
{
    unsigned d = payload[OS_VAL_OFF + MSET_ID_DELAY], p = payload[OS_VAL_OFF + MSET_ID_POOL], w = payload[OS_VAL_OFF + MSET_ID_WS], u = payload[OS_VAL_OFF + MSET_ID_UPDCHK];
    int n = 0;
    if (w <= 1) {
        s->ws = (unsigned char) w;
        n++;
    }
    if (d <= MSET_DELAY_MAX) {                               /* 1..15 / 0 = Auto delay ON (an old save reads as On), 1..15 is the remembered manual value; 0xFF keeps the default */
        s->delay = MSET_DELAY_AUTO;
        s->dman = (unsigned char) (d ? d : 2u);
        n++;
    } else if (d > 0x10 && d <= 0x1F) {                      /* 0x10 | frames: Auto delay OFF, that many frames */
        s->delay = s->dman = (unsigned char) (d & 15u);
        n++;
    }
    if (p < MSET_POOL_N) {
        s->pool = (unsigned char) p;
        n++;
    }
    if (u <= 1 || u == MSET_UPDCHK_SKIP) {
        s->updchk = (unsigned char) (u == MSET_UPDCHK_SKIP ? 0u : 1u);      /*: the start check is always on; a stored Off counts as On, only the bot cards' skip value turns it off */
        n++;
    }
    if (payload[OS_VAL_OFF + MSET_ID_PROG] <= 1) {
        s->prog = payload[OS_VAL_OFF + MSET_ID_PROG];
        n++;
    }
    {   unsigned r = payload[OS_VAL_OFF + MSET_ID_RBR];       /* bit 0 the readout (0 and 0xFF are 'off': the byte is a reserved zero in older records), bit 1 Spectate Upload OFF, bit 2 Backup to slot B OFF */
        if (r <= 127u) {
            if (r & 1u) {
                s->rbr = 1;
                n++;
            }
            if (r & 2u) {
                s->specoff = 1;
                s->specexp = 1;
                n++;
            }
            if (r & 8u) {                                    /* explicitly ON (wins over a stale bit 1) */
                s->specoff = 0;
                s->specexp = 1;
                n++;
            }
            if (r & 4u) {
                s->bakoff = 1;
                n++;
            }
            if (r & 16u) {
                s->dbgl = 1;
                n++;
            }
            if (r & 32u) {
                s->recseed = 1;
                n++;
            }
            if (r & 64u) {                                   /* Show ping as builds 1069/1070 stored it: taken once, moved to byte 19 by the next save (review M4) */
                s->pingon = 1;
                s->touched2 |= (unsigned char) (MSET_T2_PING | MSET_T2_MIG);
                n++;
            }
        }
    }
    {   unsigned p19 = payload[OS_VAL_OFF + MSET_ID_PRELOAD];   /* bit 0 Menu Preload, bit 1 Show ping, bits 2-3 Ping and delay position (0..2), bits 4-5 Raise HUD; 0xFF and anything above 127 mean all off / default; bit 6 = Keep previous build switched OFF */
        if (p19 != 0xFFu) {                                 /* bit 7 = Music off; 0xFF stays 'not set' (a writer never produces it: hud position 3 is not a value) */
            if (p19 & 128u) {
                s->musicoff = 1;
                n++;
            }
            if (p19 & 64u) {
                s->keepoff = 1;
                n++;
            }
            if (p19 & 1u) {
                s->preload = 1;
                n++;
            }
            if (p19 & 2u) {
                s->pingon = 1;
                n++;
            }
            if ((p19 & 12u) && (p19 & 12u) <= 8u) {
                s->hudpos = (unsigned char) ((p19 >> 2) & 3u);
                n++;
            }
            if (p19 & 48u) {
                s->hudraise = (unsigned char) ((p19 >> 4) & 3u);
                n++;
            }
        }
    }
    if (payload[OS_VAL_OFF + MSET_ID_SRV] <= 15u) {          /* Wired opponents only: bit 2 of byte 17, every build; kept in bit 2 of hudraise (this runs after the byte-19 read above); bit 3 = Music Random */
        s->hudraise |= (unsigned char) (payload[OS_VAL_OFF + MSET_ID_SRV] & 4u);
        s->musicrand = (unsigned char) (!s->musicoff && (payload[OS_VAL_OFF + MSET_ID_SRV] & 8u));
    }
#ifdef SBR_NETCFG                                            /* lab/dev builds only: a release build does not read the server pick */
    if ((payload[OS_VAL_OFF + MSET_ID_SRV] & 0xF0u) == 0 && ((payload[OS_VAL_OFF + MSET_ID_SRV] & 3u) == 1 || (payload[OS_VAL_OFF + MSET_ID_SRV] & 3u) == 2)) {   /* like rbr: 0, 0xFF and anything else mean the default (the server); bit 2 is the Wired switch above */
        s->srv = payload[OS_VAL_OFF + MSET_ID_SRV] & 3u;
        n++;
    }
    if (payload[OS_VAL_OFF + MSET_ID_MMR] <= 3) {            /* ... and Our mock; bit1 = the confirm was accepted */
        unsigned m = payload[OS_VAL_OFF + MSET_ID_MMR];
        s->mmr = (unsigned char) (m & 1u);
        s->mmk = (unsigned char) ((m >> 1) & 1u);
        if (m)
            n++;
    }
#endif
    s->from_card = (unsigned char) n;
    return n;
}

void ms_set_fill(unsigned char *payload, const struct ms_settings *s, int keep)
{
    int i;
    if (!keep) {
        for (i = 0; i < OS_PAYLOAD_SIZE; i++)
            payload[i] = 0;
        for (i = 0; i < OS_NVAL; i++)
            payload[OS_VAL_OFF + i] = OS_UNSET;
        put32(payload, OS_MAGIC);
        payload[4] = OS_VERSION;
        payload[5] = OS_NVAL;
    }
    if (s->touched & MSET_T_WS)
        payload[OS_VAL_OFF + MSET_ID_WS] = s->ws;
    if (s->touched & MSET_T_DELAY)
        payload[OS_VAL_OFF + MSET_ID_DELAY] = MSET_DELAY_BYTE(s);
    if (s->touched & MSET_T_POOL)
        payload[OS_VAL_OFF + MSET_ID_POOL] = s->pool;
    if ((s->touched & MSET_T_PROG) && s->prog <= 1)
        payload[OS_VAL_OFF + MSET_ID_PROG] = s->prog;
    if ((s->touched & MSET_T_RBR) || (s->touched2 & (MSET_T2_SPEC | MSET_T2_BAK | MSET_T2_DBG | MSET_T2_REC | MSET_T2_MIG)))
        payload[OS_VAL_OFF + MSET_ID_RBR] = MSET_RBR_BYTE(s);
    if ((s->touched2 & (MSET_T2_PRELOAD | MSET_T2_PING | MSET_T2_HUD)) || s->touched3)
    {
        payload[OS_VAL_OFF + MSET_ID_PRELOAD] = MSET_PRELOAD_BYTE(s);
        payload[OS_VAL_OFF + MSET_ID_SRV] = (unsigned char) ((payload[OS_VAL_OFF + MSET_ID_SRV] & 0xF0u ? 0u : payload[OS_VAL_OFF + MSET_ID_SRV] & 3u) | (s->hudraise & 4u) | (s->musicrand ? 8u : 0u));   /* Wired opponents only rides in bit 2 of byte 17; bits 0-1 (the lab server pick) stay as they are */
    }
#ifdef SBR_NETCFG
    if (s->touched & MSET_T_SRV)
        payload[OS_VAL_OFF + MSET_ID_SRV] = (unsigned char) (s->srv | (s->hudraise & 4u) | (s->musicrand ? 8u : 0u));
    if (s->touched & MSET_T_MMR)
        payload[OS_VAL_OFF + MSET_ID_MMR] = (unsigned char) (s->mmr | (s->mmk ? MSET_MMR_ACK : 0u));
#endif
    put32(payload + 8, os_crc32(payload + OS_CRC_FROM, OS_PAYLOAD_SIZE - OS_CRC_FROM));
}

int ms_set_delay(struct ms_settings *s, unsigned v)
{
    if (v > MSET_DELAY_MAX || v == s->delay)                 /* 0 = Auto delay On */
        return 0;
    if (v)
        s->dman = (unsigned char) v;                         /* the manual value is remembered while Auto is switched back On */
    s->delay = (unsigned char) v;
    s->touched |= MSET_T_DELAY;
    return 1;
}

int ms_set_pool(struct ms_settings *s, unsigned v)
{
    if (v >= MSET_POOL_N || v == s->pool)
        return 0;
    s->pool = (unsigned char) v;
    s->touched |= MSET_T_POOL;
    return 1;
}

const char *ms_pool_text(unsigned pool)
{
    return pool == MSET_POOL_MS ? "Multishine only" : pool == MSET_POOL_SL ? "Slippi only" : "Multishine and Slippi";
}

/* one on/off switch: v must be 0 or 1 and differ from the stored value; marks the field's touched bit */
static __attribute__((noinline)) int flag_set(unsigned char *f, unsigned char *t, unsigned bit, unsigned v)
{
    if (v > 1u || v == *f)
        return 0;
    *f = (unsigned char) v;
    *t = (unsigned char) (*t | bit);
    return 1;
}

int ms_set_ws(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->ws, &s->touched, MSET_T_WS, v);
}

int ms_set_prog(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->prog, &s->touched, MSET_T_PROG, v);
}

int ms_set_preload(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->preload, &s->touched2, MSET_T2_PRELOAD, v);
}

int ms_set_keep(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->keepoff, &s->touched3, MSET_T3_KEEP, v > 1u ? 2u : (v ? 0u : 1u));
}

int ms_set_music(struct ms_settings *s, unsigned v)
{
    unsigned char off = (unsigned char) (v == 0u), rnd = (unsigned char) (v == 2u);    /* v: 1 On, 0 Off, 2 Random (an On with the random stage track) */
    if (v > 2u || (off == s->musicoff && rnd == s->musicrand))
        return 0;
    s->musicoff = off;
    s->musicrand = rnd;
    s->touched3 |= MSET_T3_MUSIC;
    return 1;
}

int ms_set_bak(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->bakoff, &s->touched2, MSET_T2_BAK, v > 1u ? 2u : (v ? 0u : 1u));
}


int ms_set_ping(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->pingon, &s->touched2, MSET_T2_PING, v);
}

int ms_set_dbg(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->dbgl, &s->touched2, MSET_T2_DBG, v);
}

int ms_set_rec(struct ms_settings *s, unsigned v)
{
    if (v != 1u || s->recseed)
        return 0;
    s->recseed = 1;
    s->touched2 |= MSET_T2_REC;
    return 1;
}

int ms_set_spec(struct ms_settings *s, unsigned v)
{
    if (!flag_set(&s->specoff, &s->touched2, MSET_T2_SPEC, v > 1u ? 2u : (v ? 0u : 1u)))
        return 0;
    s->specexp = 1;
    return 1;
}

int ms_set_rbr(struct ms_settings *s, unsigned v)
{
    return flag_set(&s->rbr, &s->touched, MSET_T_RBR, v);
}

#ifdef SBR_NETCFG
int ms_set_srv(struct ms_settings *s, unsigned v)
{
    if (v > 2 || v == s->srv)
        return 0;
    s->srv = (unsigned char) v;
    s->touched |= MSET_T_SRV;
    return 1;
}

int ms_set_mmr(struct ms_settings *s, unsigned v)
{
    if (v > 1 || v == s->mmr)
        return 0;
    s->mmr = (unsigned char) v;
    s->touched |= MSET_T_MMR;
    return 1;
}

int ms_set_mmk(struct ms_settings *s)
{
    if (s->mmk)
        return 0;
    s->mmk = 1;
    s->touched |= MSET_T_MMR;
    return 1;
}
#endif

unsigned ms_prog_plan(unsigned prog, unsigned dtv, unsigned sram, unsigned live)
{
    unsigned r = 0;
    if (!dtv)
        return 0;
    if (sram)                                                /* the console flag (Melee's prompt answered Yes, or Nintendont at 480p) is only cleared: the stored setting decides, the flag is never adopted */
        r = MP_CLEAR_SRAM;
    if (prog == MSET_PROG_ON) {
        if (!live)
            r |= MP_LIVE_ON;
    } else if (live) {                                       /* Off and not set (a freshly built card) are the same: progressive only when the player turned it On in Settings */
        r |= MP_LIVE_OFF;
    }
    return r;
}

unsigned ms_ws_apply(const unsigned *tab, unsigned on, unsigned (*rd)(void *ctx, unsigned addr), void (*wr)(void *ctx, unsigned addr, unsigned word), void *ctx)
{
    unsigned n = tab[0], i, changed = 0;
    if (n > MSET_WS_MAX)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned a = tab[1 + 3 * i], w = on ? tab[2 + 3 * i] : tab[3 + 3 * i];
        if (rd(ctx, a) != w) {
            wr(ctx, a, w);
            changed++;
        }
    }
    return changed;
}

/* ---- Melee single-player records, console side (see ml_set.h) ---- */
#define REC_FD 0x6C4u
#define REC_STRIDE 0xACu
#define REC_FLAGS 0x7Cu
#define REC_HRC 0x84u
#define REC_TARGET 0x94u
#define REC_DONE 0x8000u
#define REC_ED_MASK 0x38u
#define REC_ED_TARGET 0x7Cu
#define REC_ED_HRC 0xE0u

static unsigned long get32(const unsigned char *p)
{
    return ((unsigned long) p[0] << 24) | ((unsigned long) p[1] << 16) | ((unsigned long) p[2] << 8) | (unsigned long) p[3];
}

#define REC_EV_MASK 0x200u
#define REC_EV_BEST 0x208u

unsigned ms_rec_seed(unsigned char *gsd, unsigned *target, unsigned *hrc)
{
    unsigned s, nt = 0, nh = 0, ne = 0;
    for (s = 0; s < MREC_NSLOT; s++) {
        unsigned char *fd = gsd + REC_FD + REC_STRIDE * s;
        unsigned long fl = ((unsigned long) fd[REC_FLAGS] << 8) | fd[REC_FLAGS + 1], bit = 1ul << s;
        if (mrec_target[s] && !(fl & REC_DONE)) {
            unsigned long m = get32(gsd + REC_ED_MASK), cur = get32(gsd + REC_ED_TARGET + 4 * s);
            put32(fd + REC_TARGET, mrec_target[s]);
            fl |= REC_DONE;
            fd[REC_FLAGS] = (unsigned char) (fl >> 8);
            fd[REC_FLAGS + 1] = (unsigned char) fl;
            if (!(m & bit) || cur == 0ul || cur > mrec_target[s]) {       /* the mirror never gets a worse time than it already holds */
                put32(gsd + REC_ED_MASK, m | bit);
                put32(gsd + REC_ED_TARGET + 4 * s, mrec_target[s]);
            }
            nt++;
        }
        if (mrec_hrc[s] && get32(fd + REC_HRC) == 0ul) {
            unsigned long cur = get32(gsd + REC_ED_HRC + 4 * s), v = mrec_hrc[s];
            if (cur < 0x80000000ul && cur > v)                           /* the mirror already holds a longer flight: the screen reads the same */
                v = cur;
            put32(fd + REC_HRC, v);
            if (cur < v || cur >= 0x80000000ul)
                put32(gsd + REC_ED_HRC + 4 * s, v);
            nh++;
        }
    }
    for (s = 0; s < MREC_NEVENT; s++) {            /* Event Matches: s64 completed mask (bit k) + best s32[k]; only an event not yet completed is written, the player's own clear stays */
        unsigned char *m = gsd + REC_EV_MASK + (s < 32u ? 4u : 0u);          /* big-endian s64: bit k < 32 lives in the low word */
        unsigned long w = get32(m), b = 1ul << (s & 31u);
        if (mrec_event[s] && !(w & b)) {
            put32(gsd + REC_EV_BEST + 4 * s, mrec_event[s]);
            put32(m, w | b);
            ne++;
        }
    }
    if (target)
        *target = nt;
    if (hrc)
        *hrc = nh;
    return nt + nh + ne;
}
