/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Settings of the Settings screens: the values the player can change in the menu and how they sit in the
 * "<name>Options" memory-card file (SDOF v1, offline/options/optstore.h: 16 value bytes by option id, 0xFF = not set = the build default, CRC-32).
 * Only the bytes of values the player changed are written; every other byte of the block (other options, the chat table, ids this build does not know)
 * stays as it is.  Pure C89, no libc, no Melee address, host-tested (tests/mset_test.c).
 *   id 0  OPT_WIDESCREEN  Widescreen 16:9 (Slippi's module group), 0 off (default) / 1 on; applied by switching the 13 hook words of the group (ms_ws_apply)
 *   id 7  OPT_DELAY   online input delay in frames, 1..15, or 0 = Auto (the delay from the boot connection test).
 *         0xFF (never changed) = the default, Auto; an explicit stored 1..15 is the player's choice and is kept.  An older build reads 0 as out of range: its default 2.
 * (Settings > Game > Auto delay): the byte now also carries the switch.  17..31 (0x10 | frames) = Auto delay OFF, that many frames;
 *         1..15 and 0 = Auto delay ON (an OLD save reads as Auto ON), 1..15 is then only the remembered manual value.  An older build reads 17..31 as out of range: its default (Auto).
 *   id 13 pool        Matchmaking pool: 0 Multishine and Slippi (default), 1 Multishine only, 2 Slippi only
 *   id 14 update check at start: 1 on (the default, also when the byte is not set), 0 off (read by )
 *   id 15 progressive scan (480p): 0 off, 1 on, 0xFF / not set = OFF like 0 (a freshly built card is off, On only from Settings after the 10 s test; the console flag is never adopted); a progressive mode the player already
 *         chose (Melee's own prompt, answered Yes) is adopted and stored as 1.  Decision table: ms_prog_plan.
 *   id 16 rollback readout (the four yellow lines after a match, on_bhold.c rbr_tick): 1 on, 0 and 0xFF off.  It lives in the first of the four 'reserved' bytes of the SDOF
 *         record (payload +0x01C = value index 16): an older reader ignores it, an older writer that rebuilds the record leaves it 0 = off, the CRC covers it.
 *         Bit 2 of the same byte: Update > Backup To Slot B switched OFF (the backup is ON by default; only an explicit off
 *         is stored, so 0 and 0xFF, i.e. every old card, mean ON).  Values 4..7 = bit 2 plus the two bits below.  An older reader sees 4 as 0 and 5..7 as "not 1 / not 3": readout off, harmless.
 * the uploader is OFF by default.  Bit 3 (8) = explicitly switched ON; bit 1 (2) = explicitly off (every card
 *         written before had it for an off switch, so those stay off); neither = the default, OFF.  Values 0..15; an older reader ignores a byte above 7 (readout off, harmless).
 *         (history) Bit 1 of the same byte: Diagnostics > Spectate Upload switched OFF (the uploader of the Game Night match-time block was ON by
 *         default; only an explicit off is stored).  Value 0 / 1 = on-by-default with the readout off / on, 2 / 3 = upload off with the readout off / on.  An older reader sees 2 as
 *         0 (readout off) and 3 as "not 1" (readout off): harmless, it never writes the byte unless the player changes the readout.
 *   id 17 server pick (lab/dev builds only): 0 the server (the patched server, default), 1 Lab (the address and pinned key of the card's MultishineOnline record).
 *         Value index 17 = payload +0x1D, the second 'reserved' byte; a reader that does not know it, an older writer and 0xFF all mean 0 = default.
 * (Settings > Network status > Wired opponents only): bit 2 (value 4) of the same byte = ON in EVERY build (the default is Off: 0, 0xFF and anything above 7 read as off).  The value is
 *         the server pick (bits 0-1, lab builds) | 4; a reader that knows only the server pick (an older lab build) sees 4..6 as 'not 1 or 2' = the server, which is its default; a release build before this one never reads the byte.
 *         In the struct the switch is bit 2 of `hudraise` (bits 0-1 stay Raise HUD): no new field, no new touched bit, no new setter (ons_change_hud(1, v) takes 0..7); byte 19 keeps only the two raise bits.
 *   id 19 bit 0 Menu Preload, bit 1 Show ping, bits 2-3 Ping and delay position (0..2), bits 4-5 Raise HUD (0..3).  Older readers accept only 1 for Preload, so 2 and 3 read as "Preload off" there: only Preload is lost, only when both
 *         are on, and an older writer writes byte 19 only when Preload is touched.  Some earlier builds stored Show ping as bit 6 of byte 16, which made older readers drop the whole of byte
 *         16 (readout, Spectate Upload, Backup to slot B, Debug lines, records-seeded): the reader still takes bit 6 once and the next save moves it (MSET_T2_MIG).
 *   id 18 Slippi matchmaking (lab/dev builds only): bit0 0 Our mock (default; the card's SBNC mm_ip override applies) / 1 Real Slippi (the override is ignored); bit1 = the one-time
 *         'real servers' confirm was answered Yes (remembered).  Payload +0x1E; 0xFF or any other bit set = not set = defaults.
 * Id 13 is the first id after OPT_COUNT (13 options in optengine.h); the options-page build does not know it and leaves the byte alone. */
#ifndef ML_SET_H
#define ML_SET_H

#define MSET_ID_WS 0
#define MSET_ID_DELAY 7
#define MSET_ID_POOL 13
#define MSET_ID_UPDCHK 14           /* RETIRED setting id (the launch-time update check is mandatory, no row, never reuse 14).  Kept only for the bot-card skip value below */
#define MSET_UPDCHK_SKIP 0x5Au      /* the update-check byte of the PC bots' cards: no check at start (always on for everyone else) */
#define MSET_ID_PROG 15
#define MSET_ID_RBR 16          /* value index 16 = payload +0x1C (first reserved byte); beyond OS_NVAL, so addressed as OS_VAL_OFF + 16 */
#define MSET_ID_SRV 17          /* value index 17 = payload +0x1D (second reserved byte) */
#define MSET_ID_MMR 18          /* value index 18 = payload +0x1E (third reserved byte) */
#define MSET_ID_KEEP 22         /* VIRTUAL id for the opt callback of the Update screen: Keep the previous build as backup, bit 6 of the byte of id 19 (1 keep = the default, 0 off) */
#define MSET_ID_BAK 20          /* VIRTUAL id for the opt callback of the Update screen: no byte of its own, it is bit 2 of the byte of id 16 */
#define MSET_ID_PRELOAD 19      /* value index 19 = payload +0x1F (fourth reserved byte): bit 0 Menu Preload, bit 1 Show ping (review M4); 0..3 valid, 0xFF / anything else = both off */
#define MSET_SRV_VPS 0u
#define MSET_SRV_LAB 1u         /* shown as Dev (the card record's server) */
#define MSET_SRV_LAN 2u         /* shown as Lab (also the card record's server, its own identity slot); lab builds only */
#define MSET_MMR_MOCK 0u
#define MSET_MMR_REAL 1u
#define MSET_MMR_ACK 2u         /* in the stored byte: the confirm was accepted once */
#define MSET_DELAY_AUTO 0       /* = AD_AUTO (offline/delay_auto/auto_delay_tab.h) */
#define MSET_DELAY_MIN 1
#define MSET_DELAY_MAX 15
#define MSET_POOL_BOTH 0
#define MSET_POOL_MS 1
#define MSET_POOL_SL 2
#define MSET_POOL_N 3
#define MSET_T_DELAY 1u         /* touched: the player changed it, so the file gets an explicit byte */
#define MSET_T_POOL 2u
#define MSET_T_WS 4u
/* touched bit 8u was MSET_T_UPD: do not reuse */
#define MSET_T_PROG 16u
#define MSET_T_RBR 32u
#define MSET_T_SRV 64u
#define MSET_T_MMR 128u
#define MSET_T2_SPEC 2u         /* touched2: the Spectate Upload switch was changed */
#define MSET_T2_BAK 4u          /* touched2: the Slot B backup switch was changed (bit 2 of the rbr byte) */
#define MSET_T2_DBG 8u          /* touched2: the Debug Lines switch was changed (bit 4 of the rbr byte) */
#define MSET_T2_PING 32u        /* touched2: the Show ping switch was changed (bit 1 of the preload byte, id 19) */
#define MSET_T2_MIG 64u         /* touched2: the card had Show ping in bit 6 of byte 16: the next save rewrites byte 16 without it and byte 19 with it (once) */
#define MSET_T2_REC 16u         /* touched2: the Melee records were seeded into the Melee save (bit 5 of the rbr byte) */
#define MSET_T2_HUD 128u        /* touched2: Ping and delay position or Raise HUD was changed (bits 2-3 and 4-5 of the preload byte, id 19) */
#define MSET_T3_MUSIC 2u        /* touched3: the Music switch was changed (bit 7 of the preload byte, id 19) */
#define MSET_T3_KEEP 1u         /* touched3: the Keep previous build switch was changed (bit 6 of the preload byte, id 19) */
#define MSET_T2_PRELOAD 1u      /* in touched2 (appended: the 8-bit touched is full; an older overlay's view of the struct is unchanged) */
#define MSET_PROG_OFF 0u
#define MSET_PROG_ON 1u
#define MSET_PROG_FOLLOW 2u     /* no byte on the card: follow the console */

enum { MSET_OK = 0, MSET_DAMAGED = 1, MSET_NEWER = 2 };

struct ms_settings {
    unsigned char ws;           /* 0 off, 1 on */
    unsigned char delay;        /* frames, 1..15; 0 = Auto */
    unsigned char pool;         /* MSET_POOL_* */
    unsigned char updchk;       /* update check at start: 1 (always, mandatory); 0 only for the bot cards' skip value; no setter, never written */
    unsigned char prog;         /* progressive scan: MSET_PROG_* */
    unsigned char rbr;          /* rollback readout: 1 on, 0 off (default) */
    unsigned char srv;          /* MSET_SRV_*: the server (default) / Lab; read only by lab/dev builds (netcfg=on) */
    unsigned char mmr;          /* MSET_MMR_*: Our mock (default) / Real Slippi; read only by lab/dev builds */
    unsigned char mmk;          /* 1 = the first-time confirm for Real Slippi was accepted (stored as bit1 of the mmr byte) */
    unsigned char touched;      /* MSET_T_* bits */
    unsigned char from_card;    /* number of values that came from the card file (0..2) */
    unsigned char preload;      /* Menu Preload: 1 = the Event-view overlay is read in while the cursor rests on its row; 0 = off, the default */
    unsigned char touched2;     /* MSET_T2_* bits */
    unsigned char bakoff;       /* Update > Backup to slot B before an update: 0 = on (the default), 1 = switched off; stored as bit 2 of the rbr byte (appended last) */
    unsigned char specoff;      /* Spectate Upload (Game Night match-time uploader): 0 = on, 1 = switched off by the player */
    unsigned char dbgl;         /* Debug Lines (lab/dev on-screen debug text, Settings > Diagnostics): 1 = shown, 0 = hidden (the default); stored as bit 4 of the rbr byte (appended last) */
    unsigned char recseed;      /* 1 = the world records were put into Melee's own save once; stored as bit 5 (32) of the rbr byte; 0 = not yet (every old card) */
    unsigned char pingon;       /* Show ping (Settings > Game, the in-match "Ping: N ms" line): 1 = shown, 0 = hidden (the default); stored as bit 1 (2) of the preload byte (id 19); bit 6 of byte 16 is read once and moved (MSET_T2_MIG) */
    unsigned char hudpos;       /* Ping and delay position (Settings > Game): 0 Default (Slippi's place, bottom right), 1 above the timer, 2 below the timer; bits 2-3 of the preload byte (id 19) */
    unsigned char hudraise;     /* Raise HUD: bits 0-1 = 0 Off, 1 Small, 2 Medium, 3 Large; bits 4-5 of the preload byte (id 19); applied by the codeset module ms-hud-raise at the next match.  BIT 2 (4) = Wired opponents only: stored as bit 2 of byte 17, NEVER in byte 19; always read the raise with `& 3` */
    unsigned char keepoff;      /* Update > Keep the previous build as backup (the B copies MultishineCodeB / MultishineOvlB): 0 = keep (the default), 1 = switched off; bit 6 (64) of the preload byte (id 19) */
    unsigned char touched3;     /* MSET_T3_* bits (touched2 is full) */
    unsigned char musicoff;     /* Settings > Game > Music: 0 = On (the default), 1 = off; bit 7 (128) of the preload byte (id 19); applied by ml_glue.c settings_apply (one word at 0x80023FFC) */
    unsigned char musicrand;    /* Music = Random: 1 = every stage plays a random OST track; ONLY with musicoff == 0 (it is a kind of On); stored as bit 3 (8) of byte 17 (id 17); an older build reads it as plain On */
    unsigned char specexp;      /* the stored value is explicit: bit 1 (off, legacy cards) or bit 3 (on) of the rbr byte was read or the player changed the switch */
    unsigned char dman;         /* AUTO DELAY switch: the remembered manual delay 1..15 (default 2).  `delay` stays the EFFECTIVE word: 0 = the Auto delay switch is On (the default), 1..15 = Off, playing that many frames */
};
#define MSET_DELAY_BYTE(s) ms_byte_delay(s)      /* the stored value of option byte 7: 17..31 = Auto delay OFF with that many frames; 1..15 = Auto ON, the remembered manual value; 0 = Auto ON */
#define MSET_RBR_BYTE(s) ms_byte_rbr(s)      /* the stored value of option byte 16 (bit 6 is never written: an older reader drops a byte above 63) */
#define MSET_PRELOAD_BYTE(s) ms_byte_preload(s)      /* the stored value of option byte 19 */

/* the stored value bytes */
unsigned char ms_byte_delay(const struct ms_settings *s);
unsigned char ms_byte_rbr(const struct ms_settings *s);
unsigned char ms_byte_preload(const struct ms_settings *s);
void ms_set_defaults(struct ms_settings *s, unsigned delay_default);
/* payload = the 0x240-byte SDOF record; MSET_OK, or why it cannot be used (a NEWER one is never read and never overwritten) */
int ms_set_check(const unsigned char *payload);
/* values from a checked payload; values outside their range or 0xFF keep what is in `s`; returns how many came from the card */
int ms_set_decode(struct ms_settings *s, const unsigned char *payload);
/* write the touched values into payload (keep = 1: payload passed ms_set_check, every other byte stays; keep = 0: a fresh record) and fix the CRC */
void ms_set_fill(unsigned char *payload, const struct ms_settings *s, int keep);
/* value changes: return 1 when the value changed (and mark it touched), 0 when it was already that value or out of range */
int ms_set_delay(struct ms_settings *s, unsigned v);
int ms_set_pool(struct ms_settings *s, unsigned v);
int ms_set_ws(struct ms_settings *s, unsigned v);
int ms_set_prog(struct ms_settings *s, unsigned v);        /* 0 or 1 only (FOLLOW is the state of an unset byte, never written) */
int ms_set_rbr(struct ms_settings *s, unsigned v);
int ms_set_preload(struct ms_settings *s, unsigned v);
int ms_set_keep(struct ms_settings *s, unsigned v);        /* 1 = keep the previous build as the B copy (default), 0 = do not */
int ms_set_music(struct ms_settings *s, unsigned v);      /* 1 = Music on (default), 0 = off, 2 = Random (an On with a random stage track) */
/* Music apply (ml_glue.c settings_apply and ml_prog_early): the start-volume word of Melee's lbAudioAx_80023F28 at 0x80023FFC (`li r4,127` On, `li r4,0` Off) */
#define MS_MUSIC_WORD(off) ((off) ? 0x38800000u : 0x3880007Fu)
int ms_set_bak(struct ms_settings *s, unsigned v);         /* 1 = Backup to slot B on (default), 0 = off */
static __inline__ int ms_set_hud(struct ms_settings *s, unsigned which, unsigned v)
{
    unsigned char *p = which ? &s->hudraise : &s->hudpos;
    if (v > (which ? 7u : 2u) || v == *p)      /* Raise HUD: 0..3 in bits 0-1, bit 2 = Wired opponents only (ml_set.h id 17); the setter stays the one the HUD rows use (engine size) */
        return 0;
    *p = (unsigned char) v;
    s->touched2 |= MSET_T2_HUD;
    return 1;
}
/* which 0: Ping and delay position (0 Default, 1 above, 2 below the timer), 1: Raise HUD (0 Off .. 3 Large) plus 4 = Wired opponents only */
int ms_set_ping(struct ms_settings *s, unsigned v);        /* 1 = the in-match ping line shown, 0 = hidden (the default) */
int ms_set_dbg(struct ms_settings *s, unsigned v);         /* 1 = Debug Lines shown, 0 = hidden (the default) */
int ms_set_rec(struct ms_settings *s, unsigned v);         /* 1 = the Melee records were seeded (set once, never cleared by the game) */
int ms_set_spec(struct ms_settings *s, unsigned v);        /* 1 = Spectate Upload on, 0 = off (the default) */
#ifdef SBR_NETCFG                                          /* lab/dev builds only: a release build has no code for the netsel toggles */
int ms_set_srv(struct ms_settings *s, unsigned v);        /* MSET_SRV_VPS / MSET_SRV_LAB / MSET_SRV_LAN */
int ms_set_mmr(struct ms_settings *s, unsigned v);        /* MSET_MMR_MOCK / MSET_MMR_REAL */
int ms_set_mmk(struct ms_settings *s);                    /* remember the accepted confirm (1 = changed) */
#endif

/* Melee single-player records, console side.  `gsd` = Melee's GmSaveData (0x1790 bytes, big-endian, the same bytes as the card's logical
   file 1: fighter table x1F2C[25] at 0x6C4 stride 0xAC, mirror EDBC at 0x30).  For each SELKIND slot of the generated table (ml_rec_tab.h): Target Test only when the completed bit
   (0x8000 of the u16 at +0x7C) is clear -> frames at +0x94, the bit, and the mirror (mask bit at 0x38, frames at 0x7C + 4*slot); Home-Run only when the distance at +0x84 is 0 -> the
   distance there and in the mirror at 0xE0 + 4*slot.  A slot that holds anything else is never changed.  Returns the number of slots written; *target and *hrc get the counts per kind. */
#define MREC_NSLOT_ 25
unsigned ms_rec_seed(unsigned char *gsd, unsigned *target, unsigned *hrc);

/* Progressive scan decision (pure; the glue reads the four inputs and acts on the bits).  Inputs: prog = stored MSET_PROG_*, dtv = VIGetDTVStatus != 0 (a component cable
   is reported), sram = OSGetProgressiveMode == 1 (the console's own flag, set by Melee's prompt), live = Melee's mode flag (gmMainLib_8046B0F0.progressive).
   Outputs (bits): MP_LIVE_ON / MP_LIVE_OFF switch the picture now (the prompt's own sequence), MP_ADOPT store prog = 1 on the card, MP_CLEAR_SRAM clear the console flag so that the next
   cold boot does not ask again (the card owns the choice from here on).  No cable: nothing at all (never touches a console without one). */
#define MP_LIVE_ON 1u
#define MP_LIVE_OFF 2u
#define MP_ADOPT 4u
#define MP_CLEAR_SRAM 8u
unsigned ms_prog_plan(unsigned prog, unsigned dtv, unsigned sram, unsigned live);
/* The first-run progressive scan question is due (pure): the options record was read (ok = a record or NO FILE, which is a fresh card: nothing stored yet counts as unset), the stored byte is unset
   (only an answered On / Off skips it), a cable is reported and the picture is still interlaced.  The console's own flag (sram) is NOT a condition: Nintendont sets it on every launch of a Wii
   with a component cable, so asking only without it skipped the question there. */
static inline unsigned ms_prog_ask(unsigned read_ok, unsigned prog, unsigned dtv, unsigned live) { return read_ok && prog == MSET_PROG_FOLLOW && dtv && !live; }

/* Widescreen: the image carries a table {count, then count x (address, word when on, word when off)} (payload.py ws_toggle fills it; the patched DOL holds the
   off words).  ms_ws_apply writes the on or off word of every entry through rd/wr (the glue flushes the caches) and returns how many words it changed. */
#define MSET_WS_MAX 16
unsigned ms_ws_apply(const unsigned *tab, unsigned on, unsigned (*rd)(void *ctx, unsigned addr), void (*wr)(void *ctx, unsigned addr, unsigned word), void *ctx);
/* text of a pool value (menu-font characters only: no plus sign, it does not draw) */
const char *ms_pool_text(unsigned pool);

#endif
