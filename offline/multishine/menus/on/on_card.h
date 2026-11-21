/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_card: reads the online setup ("MultishineOnline", on_cfgrec.h) from the memory card for the 'Multishine online' image, in the same translation unit as
 * the code-history card layer (om_card.c is #included: the CARD work area, block buffer and Melee-idle check are shared, not duplicated).
 * Called only from the EXI shim's B9 (menu load), never from a match, never at boot, never writes.  Failure never blocks: the caller shows a message. */
#ifndef ON_CARD_H
#define ON_CARD_H

#include "om_card.h"
#include "od_glue.h"
#ifdef ONS_SETTINGS
#include "ml_set.h"
#endif

/* ONS_NETSEL: the lab/dev "Settings > Online" toggles Server (the server / Lab) and Slippi (Our mock / Real Slippi),.  Only in a netcfg=on build (-DSBR_NETCFG) of the card
   route with the settings layer; a release build has none of this code. */
#if defined(SBR_NETCFG) && defined(ONS_SETTINGS) && defined(SD_CARD_HOLE)
#define ONS_NETSEL 1
#endif

struct onc_status {                  /* RAM proof block (symbol onc_st, onc_status_addr) */
    unsigned int magic;              /* 'ONCF' */
    unsigned int tries;              /* card reads attempted */
    unsigned int why;                /* OD_WHY_* of the last attempt (0 = loaded) */
    unsigned int verdict;            /* ONR_* of the record that was found (0xFF = none) */
    unsigned int slot;               /* 1 = A, 2 = B, 0 = none */
    unsigned int res;                /* SDK results of slot A << 16 | slot B (low 16 bits each) */
    unsigned int done;               /* verdict final: no more reads */
    unsigned int ticks;              /* time base ticks of the last read */
    unsigned int idle_mask;          /* bit set = a Melee-card-layer condition that made the card unavailable at the last attempt (0 = idle) */
    unsigned int hist_sv;            /* slot policy: per-slot verdict of the last code-history load, slot A in bits 0-3, slot B in bits 4-7:
                                        0 not looked at, 1 file valid, 2 no file (good card), 3 file damaged, 4 file newer, 5 unusable (skipped), 6 error */
    unsigned int hist_unusable;      /* slots skipped as unusable in that load */
    unsigned int set_sv;             /* the same for the last setup read */
    unsigned int set_unusable;
    unsigned int ident_tries;        /* record writes after a silent re-enrolment (onc_save_ident) that failed on the card; 3 end the attempts */
    unsigned int ident_saved;        /* 1 once the fresh identity is in the card record */
    unsigned int ident_last;         /* the last onc_save_ident result + 4 (4 saved, 5 card busy, 3 no record, 2 write failed); 0 = not called */
};
extern struct onc_status onc_st;

#ifdef ONS_DLOG
/* the secret-free network report "MultishineDiag" (on_dlog.c): w = the 16 words of ms_net_dbg; 0 saved and verified, 1 card layer busy, -1 failed */
int odl_save(const unsigned *w);
unsigned odl_format(char *o, const unsigned *w, unsigned seq);
#endif

#ifdef ONS_SETTINGS
/* ---- the settings record (ons_*, on_set.c) ---- the player's menu settings in the "MultishineOptions" file
   (SDOF v1, ml_set.h).  Called only from the menu scene (ml_glue.c), never from a match, never at boot; read when the menu is entered, written in whole 8 KB
   blocks (rate limited) when a value changed; every failure leaves the values in RAM only and the menu running. */
struct ons_status {                  /* RAM proof block (symbol ons_st, ons_status_addr) */
    unsigned int magic;              /* 'ONSE' */
    unsigned int load_status;        /* ONS_L_* */
    unsigned int save_status;        /* ONS_W_* */
    unsigned int load_tries, save_tries, saves;
    unsigned int slot;               /* 1 = A, 2 = B, 0 = no file known */
    unsigned int load_res, save_res; /* SDK results (load: slot A << 16 | slot B, low 16 bits each) */
    unsigned int delay, pool;        /* the values now */
    unsigned int flags;              /* bit0 load final, bit1 dirty, bit2 file newer, bit3 file damaged, bit4 given up (3 failed saves) */
    unsigned int from_card;          /* values that came from the card file */
    unsigned int ticks;              /* time base ticks of the last card pass */
    unsigned int wslot;              /* slot of the last write attempt that reached a card */
    unsigned int ws;                 /* widescreen now (0 off, 1 on) */
    unsigned int prog;               /* progressive scan setting now (MSET_PROG_*: 0 off, 1 on, 2 follow the console) */
    unsigned int rbr;                /* rollback readout setting now (0 off, 1 on) */
};
extern struct ons_status ons_st;
enum { ONS_L_NONE = 0, ONS_L_OK = 1, ONS_L_NOFILE = 2, ONS_L_NOCARD = 3, ONS_L_DAMAGED = 4, ONS_L_NEWER = 5, ONS_L_ERROR = 6, ONS_L_BUSY = 7 };
enum { ONS_W_NONE = 0, ONS_W_OK = 1, ONS_W_NOCARD = 2, ONS_W_ERROR = 3, ONS_W_NEWER = 4, ONS_W_FULL = 5, ONS_W_RESERVE = 6, ONS_W_BUSY = 7,
       ONS_W_VERIFY = 8, ONS_W_NOLOAD = 9, ONS_W_WAIT = 10 };
#define ONS_EV_LOADED 1u             /* ons_poll: the values changed because the card file was read: apply them */
void ons_menu_enter(unsigned delay_default, unsigned frame);   /* every entry of the menu scene: first call sets the defaults, every call re-arms the load */
unsigned ons_poll(unsigned frame);                            /* every menu frame: pending load, due save; returns ONS_EV_* */
const struct ms_settings *ons_cur(void);
int ons_change_delay(unsigned v, unsigned frame);             /* 1 when the value changed; the card write follows after the debounce */
int ons_change_pool(unsigned v, unsigned frame);
int ons_change_ws(unsigned v, unsigned frame);
int ons_change_prog(unsigned v, unsigned frame);              /* progressive scan: 0 off, 1 on (a stored On is what a later boot applies without the prompt) */
int ons_change_rbr(unsigned v, unsigned frame);               /* rollback readout: 1 on, 0 off (default) */
int ons_change_music(unsigned v, unsigned frame);            /* Settings > Game > Music: 1 on (default), 0 off, 2 random stage track */
int ons_change_keep(unsigned v, unsigned frame);              /* Update > Keep the previous build as backup: 1 keep (default), 0 off */
int ons_change_bak(unsigned v, unsigned frame);               /* Update > Backup to slot B: 1 on (default), 0 off */
int ons_change_rec(unsigned v, unsigned frame);               /* the Melee records were seeded into Melee's save: 1 once, stored as bit 5 of option byte 16 */
int ons_change_ping(unsigned v, unsigned frame);              /* Settings > Game > Show ping: 1 shown, 0 hidden (default) */
int ons_change_hud(unsigned which, unsigned v, unsigned frame);   /* Settings > Game: which 0 = Ping and delay position (0 Default, 1 above, 2 below the timer), 1 = Raise HUD (0 Off .. 3 Large) */
int ons_change_dbg(unsigned v, unsigned frame);               /* Diagnostics > Debug Lines: 1 shown, 0 hidden (default) */
int ons_change_spec(unsigned v, unsigned frame);              /* Diagnostics > Spectate Upload: 1 on (default), 0 off */
int ons_change_preload(unsigned v, unsigned frame);           /* Menu Preload (Settings > Game): 1 on, 0 off (default) */
#ifdef ONS_NETSEL
int ons_change_srv(unsigned v, unsigned frame);               /* Server: 0 Production (the patched server, default), 1 Dev, 2 Lab (both use the card record's server, each has its own identity slot); the config is re-read from the card and re-applied within a second */
int ons_change_mmr(unsigned v, unsigned frame);               /* Slippi: 0 Our mock (default), 1 Real Slippi; takes effect at once (the bridge's override accessors) */
int ons_change_mmk(unsigned frame);                           /* remember that the one-time "real servers" confirm was accepted */
const struct od_cfg *onc_netsel_cfg(void);                    /* the config block the card layer decodes into (read-only info row), or 0 before the first read */
int onc_netsel_locked(void);                                  /* 1 when this build has a patched, locked server (the Server toggle does something) */
#endif
int ons_seed(unsigned char *rec, unsigned frame, int write);  /* the stored entropy seed (44-byte SDSD record at 0x1800 of the block): see on_set.c */
void ons_flush(unsigned frame);                               /* the page was closed: make the save due now (still at most one write per ONS_MIN_GAP frames) */
#endif

/* IDENT SETTLE: must equal SBR_IDENT_SETTLE_MS (bridge/sbr_card.h): after this long from the first attempt without a definite answer (busy card, read errors) the setup counts as not found */
#define ONC_SETTLE_MS 3000u

/* One attempt, once per menu load until the verdict is final.  `cfg` is filled on success.  Returns OD_WHY_NONE (loaded) or the reason. */
int onc_on_status(struct od_cfg *cfg);
/* 1 once the verdict is final (loaded, no file, damaged, newer, incomplete, or the error budget is spent) */
int onc_final(void);
/* soft final: the verdict is only a spent busy / error budget (onc_final lifts it by itself after 2, 4, 8, 16, 30 s; onc_rearm(1) at once, for Reconnect) */
int onc_soft(void);
void onc_rearm(int force);
/* Slippi route: add a '#' code (0 = none) and/or read the '#' list from MultishineCodes, see omc_slippi_sync in on_hist.c; -1 = card not read yet */
int omc_slippi_sync(const char *add, char out[][9], int max);
/* After a silent re-enrolment (od_glue.c, a purged anonymous device): put the fresh device id, secret, code and name of `c` into the record that was read, in place (one block
   write, read back).  0 = saved, 1 = the card is busy (try again later), -1 = no record to update (none was read), -2 = the card write failed.  Only for a record whose verdict was OK;
   nothing else in the block changes, the CRC is recomputed. */
int onc_save_ident(const struct od_cfg *c);
/* the backup server address of the setup read by onc_on_status (host order IPv4), 0 = none */
unsigned int onc_backup(void);

#ifdef ON_DIAG
/* the diagnostic variant's status line (card history and setup), NUL terminated, at most cap bytes */
void onc_diag_text(char *out, unsigned int cap);
/* the same in at most 30 characters, for the user name line of the CSS (shown at every menu load) */
void onc_diag_short(char *out, unsigned int cap);
#endif

#endif
