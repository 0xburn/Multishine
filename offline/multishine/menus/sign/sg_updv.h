/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The view model of the Update screen: the few numbers the screen draws and the requests it raises.  Shared by the screen
 * (sg_ui.c, pure drawing and key handling) and the state machine (sgu_core.c, network and card): neither includes the other.  Pure C89, no libc.
 */
#ifndef SG_UPDV_H
#define SG_UPDV_H

/* states of the Update row */
enum { SGU_IDLE = 0, SGU_CHECKING, SGU_UPTODATE, SGU_AVAILABLE, SGU_DOWNLOADING, SGU_VERIFYING, SGU_INSTALLING, SGU_RESTART, SGU_FAILED, SGU_OFFLINE, SGU_DONE, SGU_NSTATE };      /* SGU_DONE: the system files (kind 3) are on the card, no restart needed */

/* why a FAILED state failed (plain words on the screen) */
enum { SGU_R_NONE = 0, SGU_R_NOHOST, SGU_R_NETWORK, SGU_R_SERVER, SGU_R_NOFILES, SGU_R_ABI, SGU_R_MEMORY, SGU_R_BADFILE, SGU_R_NOCARD, SGU_R_FULL, SGU_R_WRITE,
       SGU_R_CARDBITS, SGU_R_BUSY, SGU_R_HASH, SGU_R_LOST, SGU_R_NONET, SGU_R_UNREACH, SGU_R_HALF, SGU_R_NODISC, SGU_R_SYSFILES, SGU_R_N };       /* SGU_R_NODISC: the disc in the drive is not the retail Melee (or cannot be read); SGU_R_SYSFILES: the build itself failed (code = SLP_E_*) */       /* SGU_R_LOST: an in-place install failed after the old file was deleted: the card has no engine */

/* SGU_R_HALF: a small-card BUNDLE install (engine + files) wrote the new files but not the engine and could not put the old files back: nothing is lost, A tries the engine again */

/* result of the backup to physical slot B that runs before an install; NONE = no backup was tried in this release (host without one, network patch) */
enum { SGU_BAK_NONE = 0, SGU_BAK_DONE, SGU_BAK_OFF, SGU_BAK_NOCARD, SGU_BAK_SMALL, SGU_BAK_BADCARD, SGU_BAK_INWAY, SGU_BAK_FAIL, SGU_BAK_MEM, SGU_BAK_BUSY, SGU_BAK_N };

/* requests the screen raises on a key press; the state machine consumes them (and ignores the ones that do not fit its state) */
#define SGU_REQ_CHECK 1u
#define SGU_REQ_DOWNLOAD 2u
#define SGU_REQ_CANCEL 4u
#define SGU_REQ_RESTART 8u
#define SGU_REQ_SYSFILES 32u       /* the automatic system-files set-up (kind 3): check, download, build and write without a key */
#define SGU_REQ_COPYB 64u          /* the Update row's "Copy card to slot B"; only in IDLE / UPTODATE / FAILED / OFFLINE */
#define SGU_CPY_WORK 255u          /* view.copyb: the copy is about to run (the screen shows 'Copying...' first) */
#define SGU_REQ_REINSTALL 16u      /* X on 'Up to date': install the same signed build again */

/* What the launch-time check learned from the signed UPD_CHECK reply, or read back from the card cache. The Update screen shows
   it; the resident main-menu notice uses the same record. */
struct sgu_adv {
    unsigned char valid;            /* 0: nothing known yet */
    unsigned char slippi_state;     /* 0 verified, 1 unverified-newer, 2 known-broken */
    unsigned char text_id, pad;     /* the fixed text the server picked (0 = none) */
    unsigned ours_latest, ours_min; /* newest published build_seq / oldest build the server still accepts (0 = no minimum) */
    unsigned char slippi_sup[3], slippi_new[3];     /* Slippi version this build speaks / the newest the server knows: major, minor, patch */
};
/* note codes of the view */
enum { SGU_NOTE_NONE = 0, SGU_NOTE_NEWER = 1, SGU_NOTE_TOO_OLD = 2 };

struct sgu_view {
    unsigned char state, reason, pct, req;
    unsigned char kind;         /* 3 the system files (MultishinePack, built on the console), 2 a network patch ("Network settings"), 0 the engine's code file, 1 our own files (MultishineOvl: the Update screen's own overlay and texts) */
    unsigned installed;         /* build_seq of the running code file, 0 = not known */
    unsigned latest;            /* build_seq on offer (AVAILABLE) or just installed (RESTART) */
    unsigned size_kb;           /* size of the offered file, rounded up */
    unsigned code;              /* the underlying error number (OTA_E_*, MNU_E_*), shown small for a bug report */
    unsigned char note_ours;    /* SGU_NOTE_*: from the advisory, against the build that is running (or was installed since boot) */
    unsigned char note_slippi;  /* the advisory's slippi_state (0 = no note) */
    unsigned char slippi_new[3];
    unsigned char tier;         /* 0 not known yet, 1 staged swap with a fallback copy, 2 in place (small card: no fallback copy, a cut leaves no engine), 3 combined release on a small card (no spare copy of the old files), 4 combined release that does not fit (code = blocks to free) */
    unsigned char armed;        /* AVAILABLE on an in-place card: the warning is on screen, the next A starts the download */
    unsigned char same;         /* UPTODATE and the server holds this very build: X reinstalls; AVAILABLE/INSTALLING/RESTART of a reinstall */
    unsigned char noreset;      /* the host has no software reset (card route, first console builds): the player switches the console off and on */
    unsigned other;             /* build_seq of the OTHER part: our files when kind = 0 (engine check), the running engine when kind = 1; 0 = not known (appended last) */
    unsigned run_rel;           /* the release that is running now (fixed at open; installs on the card change it only after the restart) */
    unsigned char restart_s;    /* RESTART with a software reset: seconds left before the console restarts by itself (5 .. 0); 255 = no automatic restart (the player switches off and on) */
    unsigned req_min;           /* note_ours == SGU_NOTE_TOO_OLD: the build the server requires for online play, else 0 (appended last) */
    unsigned char backup;       /* SGU_BAK_*: what the backup to slot B did for this install (0 = none yet / not applicable; appended last) */
    unsigned char copyb;        /* SGU_BAK_* of the player's own "Copy card to slot B" (0 = not asked this open), SGU_CPY_WORK while it is about to run (appended last of all) */
};
#endif
