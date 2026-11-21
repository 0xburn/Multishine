/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sgu: the state machine behind the Update row of the Accounts overlay.  Pure C89, no libc, no heap of its own, no Melee
 * address: everything it needs from the console comes through `struct sgu_host` (function pointers and a few facts), so the same file runs in the overlay and
 * in the host test (tests/sgu_driver.c: the real mnu client against the real ms_update server over loopback, and the real ota_core installer on a file-backed card).
 *
 * Flow (one call of sgu_frame per rendered frame, never blocks except where noted):
 *   IDLE --A--> CHECKING (wire hello when no session is ready, then UPD_CHECK; a check on a session the server forgot is retried ONCE with a fresh hello after about 1 s +-20%) --> UPTODATE | AVAILABLE | OFFLINE | FAILED
 *   AVAILABLE --A--> DOWNLOADING (staging buffer from host->alloc, UPD_FETCH chunks, per-chunk resend in mnu.c) --> VERIFYING --> INSTALLING --> RESTART
 *   B while CHECKING or DOWNLOADING cancels (the buffer is freed, nothing was written to the card).  VERIFYING and INSTALLING take no input and the screen cannot be left.
 *   UPTODATE with the installed build itself on the server --X--> AVAILABLE-like reinstall (same download / verify / install path; the build_seq rule is 'newer OR EQUAL', older stays refused)
 *   Card tiers: the install is staged (second copy, previous file kept) when the card has room, else IN PLACE (delete, write, no fallback) after a second A on a warning.
 *   RESTART --A--> host->reset (a clean soft reset; the game and its saves are not touched).
 * VERIFYING (ota_verify: SHA-512 and Ed25519 over the whole file in RAM) and INSTALLING (ota_install_verified: delete, create, write, read back, two renames; the
 * card calls are synchronous) are single blocking steps.  Each waits SGU_SETTLE frames after its state is set, so that the screen has drawn the new text before
 * the frame stalls.  Nothing the server says is trusted for safety: the file must pass the loader's own checks (signature under the pinned key, CRCs, ABI, build_seq).
 * Memory: this struct (about 1 KB with the mnu client) plus ONE staging buffer of file size + OTA_BLOCK from host->alloc, held from the start of the download until
 * the install ends, the download is cancelled or the screen is left.
 */
#ifndef SGU_CORE_H
#define SGU_CORE_H
#include "sg_updv.h"
#pragma GCC diagnostic ignored "-Wunused-function"      /* ota_core.h pulls in the static signature check of the loader, which this file does not call */
#include "../../../client_net/mnu.h"
#include "../../../cardloader/ota/ota_core.h"
#include "../../../seed/seed_plan.h"          /* seed_ops, sp_ctx, sp_apply: the cut-safe write of MultishinePack */
#include "../../../seed/seed_slp.h"           /* the on-console pack builder */

#define SGU_SETTLE 3u
#define SGU_MAX_FILE (OTA_BLOCK * 128u)        /* 1 MiB: far above the 696,384 B engine file; larger offers are refused as not fitting */

/* the player's own disc, by path ("/MnMaAll.usd"): size in bytes (-1 = no such file) and a synchronous read (dst and off multiples of 32, len up to the next multiple of 32 is allowed), 0 = ok */
struct sgu_disc {
    int (*size)(const char *path);
    int (*read)(const char *path, void *dst, unsigned off, unsigned len);
};

struct sgp;                                     /* sgp_core.c: the system-files build (kind 3) */

struct sgu_host {
    unsigned size;                              /* sizeof(struct sgu_host) of the resident build: the overlay refuses a different layout */
    unsigned net_size;                          /* sizeof(mnc) of the resident build (mnu.c reads the session and buffers of that struct) */
    mnc *net;                                   /* the resident wire v2 client; its io is the console's UDP socket to the server */
    int (*hello)(unsigned now_ms);              /* starts mnc_hello on `net` (0 = started): the session the check rides on; the 12 KB of mnc.c stay in the resident image */
    int (*poll)(unsigned now_ms);               /* mnc_poll on `net`, returns its state (MNC_BUSY / MNC_DONE / MNC_FAILED) */
    int (*link_up)(void);                       /* 1 when the network has an address */
    int (*card_ready)(void);                    /* 1 when a formatted card is in the slot and no other card operation is running */
    const struct ota_card_ops *card;            /* CARD SDK wrappers (create, write, read, delete, rename, exists) */
    void *(*alloc)(unsigned n);                 /* heap block, 0 when it cannot be given (must not halt) */
    void (*free)(void *p);
    void (*reset)(void);                        /* clean soft reset; does not return on a console */
    const mw2_u8 (*pubs)[32];                   /* pinned release public keys: the same ones the loader pins */
    unsigned npub;
    unsigned abi;                               /* loader ABI */
    unsigned installed_seq;                     /* build_seq of the code file that is running */
    unsigned floor_seq;                         /* build_seq floor from the state file, 0 = none */
    unsigned tried_seq;                         /* the build_seq the loader refused because its boot record was exhausted (status word 23), 0 = none: the server's offer of exactly this build is not shown again */
    unsigned from_prev;                         /* 1 when the running code is the FALLBACK file (the loader refused or skipped the active one): installs then keep the fallback and replace the active file */
    unsigned *pending_seq;                      /* resident word (0 at boot): the build_seq installed since boot and waiting for the restart; keeps the screen from
                                                   installing the same file twice (a second install would overwrite the fallback copy) after the player left and came back */
    int allow_unsigned;                         /* dev loaders only */
    void (*card_done)(void);                    /* or 0: called after every install attempt, success or not (unmounts the card, gives the CARD work area back) */
    const struct sgu_adv *adv;                  /* or 0: the launch check's advisory record (read only here; shown as notes on the screen) */
    int (*backup)(void);                        /* or 0: the full backup copy of the card to physical slot B, run once before the first install of a release; returns SGU_BAK_*, never blocks the update (appended last) */
    const struct seed_ops *seed;                /* or 0: the card with create_gci / blocks / free (seed_plan.h), for MultishinePack (appended last) */
    const struct sgu_disc *disc;                /* or 0: the player's disc; without it the system files cannot be set up (appended last) */
    int (*keep_prev)(void);                     /* or 0 = keep: the player's switch "Keep previous build as backup"; returns 0 when it is OFF, then a verified install deletes the B copy of what it replaced (appended last) */
    int (*copy_b)(void);                        /* or 0: the player's "Copy card to slot B" (SGU_REQ_COPYB): the card's files, without the two login files, to the memory card in slot B (upd_bak.c BAK_M_COPY); returns SGU_BAK_* (appended last of all) */
};

#define SGU_DL_WINDOW_MS 90000u /* after a download stalled for MNU_STALL_MS or lost its session, fresh handshakes (each keeps the received chunks) are tried for this long, however each one
                                   ends: link down, no answer, handshake refused */
#define SGU_DL_TRIES 60u        /* and at most this many rounds within the window */
#define SGU_RESTART_MS 5000u    /* after an install the console restarts by itself after this long (A restarts at once; there is no way back) */
struct sgu {
    const struct sgu_host *h;
    mnu u;
    unsigned char state, reason, phase, settle, hello_tried, hash_retry, tier, armed;     /* hash_retry: the one automatic signed retry after MNU_E_HASH is under way or spent */
    unsigned installed, latest, size, code, reset_calls, retry_at;
    unsigned char same, reinstall, kind, chain_done;   /* kind: 0 the engine's code file, 1 MultishineOvl (our own files), 2 a network patch (sgu_net.h; net_leg: 1 = the check will end with the net leg, 2 = the net leg is running); chain_done: the engine check came back up to date, the files check runs after it */
    unsigned char c_same, pad5, chain_install_ok, net_leg;      /* pad5: the engine check/download chained after a files install is under way (one A installs the whole release); chain_install_ok: the files part is already on the card */       /* same: the check found the installed build on the server (X offers a reinstall); reinstall: the download under way is that one */
    mw2_u8 *buf;
    unsigned buf_len;
    mw2_u8 *scratch;
    unsigned fetch_total;
    mnu u_eng;                                 /* the engine check's mnu, kept while the net leg runs: a download (X reinstall) must run on the mnu that did ITS check */
    unsigned net_seq;                          /* seq of the last applied network patch (read from the online block when the net leg starts) */
    unsigned ovl_seq, c_latest, c_size;                    /* installed MultishineOvl build_seq (read from the card file's header at the files check); the engine check's result kept while the files check runs */
    unsigned prog_tot, prog_base;              /* ONE bar for a combined release: total bytes of both parts, and the bytes of the parts already done (0 tot = a single part: the bar is that part) */
    unsigned boot_run;                         /* the release that is RUNNING: highest of the engine and the files as read at open; installs never change it until the restart */
    unsigned f_latest, f_size;                 /* the files offer, held while the engine is checked too: the player sees ONE offer (highest release, summed size) */
    unsigned char forced_try, st_try, dl_retry, dl_pend;    /* forced_try: the automatic check of a forced-update open was started; dl_retry: fresh handshakes made for this file after a download stalled or lost its session (at most SGU_DL_TRIES; was one); st_try: start-class retries made (sgu_core.c start_retry, at most 2); dl_pend: such a handshake and check is under way, the staging buffer and the received chunks are kept for the resume */
    unsigned char cpy, cpy_wait, cpy_pad1, cpy_pad2;   /* cpy: SGU_BAK_* of the player's last "Copy card to slot B" (0 = none), SGU_CPY_WORK while it is about to run; cpy_wait: frames to draw "Copying..." first */
    unsigned char bak, bak_pad1, bak_pad2, bak_pad3;   /* bak: SGU_BAK_* of this release's backup to slot B (0 = not made yet); reset by a new check by the player */
    unsigned char comb, f_have, comb_chk, auto_dl;    /* comb: the screen shows the combined offer; comb_chk: 1 = the engine check for the combined offer is under way, 2 = re-checking the files for display; auto_dl: download the next offer without a key press */
    /* ALL OR NOTHING (cube: 1053's files went on the card, its engine download failed, the cube restarted on half a release): the verified files part waits in
       RAM while the engine is checked and downloaded; both are written only when the engine is verified too.  A files-only release (engine up to date) writes the files alone. */
    mw2_u8 *hold_buf;                          /* the verified files part (MultishineOvl) held for the install, 0 = none */
    unsigned hold_len, hold_size, hold_latest;
    unsigned char hold_tier, hold_same, hold_pad1, hold_pad2;
    unsigned char bplan, bp_pad1, bp_pad2, bp_pad3;   /* bplan: OTA_PLAN_* of the combined release (0 = not planned); the plan is made at the combined offer and again, with the real files, right before the first write */
    unsigned bshort;                           /* plan C: how many more blocks must be free */
    struct sgp *pk;                            /* kind 3 (the system files): the build, a heap block while the data is installed */
    unsigned char sysf, sysf_pad1, sysf_pad2, sysf_pad3;     /* sysf: this open is the automatic system-files set-up (SGU_REQ_SYSFILES): the check, download and install follow each other without a key */
    unsigned dl_t0, dl_window;                 /* when the first retry round of this file started; the retry window (SGU_DL_WINDOW_MS, set at init; host tests shorten it) */
};

/* h may be 0 (a build without the update client): every request then ends in FAILED / SGU_R_NOHOST.  Checks the host's struct sizes against this build. */
void sgu_init(struct sgu *s, const struct sgu_host *h);
/* consumes v->req, advances the machine, fills the view (state, reason, pct, numbers) */
void sgu_frame(struct sgu *s, struct sgu_view *v, unsigned now_ms);
/* leaving the screen: abort anything running and free the buffer */
void sgu_close(struct sgu *s);
const char *sgu_state_name(unsigned state);
/* kind 3 (sgp_core.c).  begin: parse the verified container in s->buf, allocate the build; 0 or an SGU_R_* reason.  step: one slice, 0 = running, 1 = installed on the card, else an SGU_R_*; *pct 0..99.
   free: give everything back (also when it was never begun). */
int sgp_begin(struct sgu *s);
int sgp_step(struct sgu *s, unsigned *pct, unsigned *code);
void sgp_free(struct sgu *s);
#endif
