/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Sign-in screens: the screen list, the navigation, the text of every screen, the real entropy test and the
 * verdict of the TCP/TLS test.  Pure C89, no libc, no heap, no clock, no Melee address: it draws into a character-cell view (52 x 26 cells, the
 * geometry of offline/common/xfbpage.h) plus an optional QR code, and a backend puts the view on a screen:
 *   - host mock renderer (tests/sg_mock.c + sg_png.py): the PNGs of /,
 *   - the Melee-hosted test image: the XFB text page of xfbpage.h, QR painted as pixels,
 *   - later the menu overlay: one SisLib text per row on the menu canvas, QR as pixels.
 * This whole module is the ON-DEMAND OVERLAY: nothing of it is in the resident image (section 4 of the design note).
 * Status: the identity, link and provider screens are MOCKUPS (placeholder data, nothing is stored or sent); the entropy test and the TCP/TLS test are
 * real (sdent, sdprobe) but unproven on a console.
 */
#ifndef SG_UI_H
#define SG_UI_H
#include "../../../net/tls/sdent.h"
#include "../../../qr/qr.h"
#include "../ml/ml_ui.h"          /* the Settings look: the row attributes and grid rows shared with the Melee-text pages */

#ifndef SG_NETSEL
#define SG_NETSEL 0     /* 1 = lab/dev overlay (netcfg=on builds): the Online page rows Server, Slippi and In Use; 0 = release: neither the rows nor their code exist */
#endif
#ifndef SG_LAB
#define SG_LAB 0        /* 1 = lab build: the TCP and TLS test row and screen (they name a lab server); 0 = release: neither exists */
#endif
#include "../on/on_bhold.h"
#include "sg_updv.h"
#include "sg_mockup.h"
#include "gnb.h"
#include "frd.h"
#include "stb.h"
#include "gnt.h"             /* the Tournament screens */
#include "sgr.h"

#ifndef SG_TECH_INFO                                   /* the gate of the technical info blocks (Update screen code rows, network diagnostic lines): Settings > Game > Show technical info, default off */
#define SG_TECH_INFO(g) (SG_LAB && (g)->tech)          /*: the switch is "Show frame delay" only (as Slippi); the code rows stay for lab builds */
#endif
#define SG_COLS 52
#define SG_ROWS 26                 /* row 0 is the header bar */

/* attributes of a row */
#define SG_A_BODY 0
#define SG_A_HEAD 1
#define SG_A_HI 2                  /* the selected row / emphasis */
#define SG_A_DIM 3
#define SG_A_GOOD 4
#define SG_A_BAD 5
#define SG_A_TITLE UI_A_TITLE      /* the Settings look (ml_ui.h): the page title next to the mark */
#define SG_A_CRUMB UI_A_CRUMB      /* the breadcrumb under it */
#define SG_A_SEC UI_A_SEC          /* a section heading */
#define SG_A_BIG UI_A_BIG          /* flag: a list row (bigger font, two grid rows) */
#define SG_VAL UI_VAL

/* held-button bits of HSD_PadStatus.button (the values the rest of the project uses) */
#define SG_B_LEFT 0x0001u
#define SG_B_RIGHT 0x0002u
#define SG_B_DOWN 0x0004u
#define SG_B_UP 0x0008u
#define SG_B_Z 0x0010u
#define SG_B_A 0x0100u
#define SG_B_X 0x0400u
#define SG_B_Y 0x0800u
#define SG_B_R 0x0020u
#define SG_B_L 0x0040u
#define SG_B_B 0x0200u
#define SG_B_START 0x1000u
#define SG_ACT_GAP 3u                     /*: A X Y Z L R Start and the d-pad's Left Right act on a FRESH press only: a raw press within SG_ACT_GAP overlay frames (100 ms at 30/s) of the button's last down sample is a repeat or a chatter and is dropped (Up Down keep their own held repeat in the screens; B is the hold button) */

/* screens */
enum { SG_HUB, SG_IDENT, SG_MAKEKEY, SG_LINK, SG_PROV, SG_PROV_DETAIL, SG_ENT, SG_TLS, SG_UPD, SG_CTL,
       /* the Mockups hub and its new mock screens (sg_mockup.h): SG_MOCK is the number the engine's Main Menu row "Mockups" opens; keep it 10 */
       SG_MOCK, SG_MK_ROOMS, SG_MK_FRIENDS, SG_MK_LB, SG_MK_TIERS, SG_MK_ROOM, SG_MK_SPEC, SG_MK_REC,
       /* the Game Night browser (gnb.h): SG_GN = the Multishine > Game Night row (rooms server data; the engine's table names 18),
          SG_MK_GNG = the Mockups hub entry "Rooms Browser (Graphic)" (example data); both on the retail Event Match screen when the console glue can build it */
       SG_GN, SG_MK_GNG,
       /* About (Settings > About): the legal notice and third-party list; read-only text, B leaves the overlay */
       SG_ABOUT,
       /* Standings (Multishine > Standings; stb.h): the Grandmaster ladder on the Event Match view, read only; the engine's table names 21 */
       SG_STAND,
       /* Friends: the Main Menu row Friends; the retail Event Match screen when the console glue can build it; the engine's table names 22 */
       SG_FRD,
       /* Delete This Console's Identity (Settings > Online, last row; sg_delid.c) */
       SG_DELID,
       /* Send Slippi Login (Settings > Online, next to Link; sgr.c): the website seals the Slippi login to a key this console shows as a QR */
       SG_SLOGIN,
       /* Tournament (Multishine > Tournament; gnt.h): the engine's table names 25 */
       SG_TOURN, SG_NSCREEN };

/* link screen mock states */
enum { SG_LK_WAIT, SG_LK_SCANNED, SG_LK_APPROVED, SG_LK_EXPIRED, SG_LK_N };

/* entropy test states */
enum { SG_ET_IDLE, SG_ET_RUN, SG_ET_PASS, SG_ET_FAIL };
#define SG_ET_TIMEOUT_FRAMES 3600u        /* 60 s at 60 frames/s */
#ifndef SG_HOLD_B_FRAMES
#define SG_HOLD_B_FRAMES 90u /*: the hold felt very brief: 30 frames came before 1.0 s (the overlay runs faster than 30/s), so the frame count is only a guard now */              /* leaving a collecting screen: B held 30 frames at most = 1.0 s at the 30 frames/s this overlay draws on the console (: ONE hold threshold of 1.0 s everywhere, replaces 1.5 s).  The real-time limit below decides whenever it comes first; this count guards a time base that does not tick */
#endif
#ifndef SG_HOLD_B_TICKS
#define SG_HOLD_B_TICKS 40500000u         /* ... or 1.0 s of time base (40,500,000 ticks at the GameCube 40.5 MHz; struct sg.hold_lim carries the value for the console real rate), whichever comes first.  Short B presses are entropy input */
#endif
#define SG_ET_OUT_BYTES 256u              /* output check: 2048 bits drawn at the session level, never shown */

struct sg_ent_t {
    struct sdent e;
    sdc_u8 state, starved_ok, out_ok, out_checked;
    sdc_u32 frame, ones, why;             /* why: 0 none, 1 timeout, 2 output check, 3 starved request was answered */
};

/* TCP/TLS test view: the glue fills it from the probe sequencer every frame (hil/sdprobe.h); times are the supplied clocks' */
struct sg_tls_view {
    sdc_u8 started, phase, done, failed;  /* phase: 1 TCP connect .. 6 HTTPS blob; 7 done; 8 failed (sdprobe numbering) */
    sdc_u8 err_phase, need_ent, ent_pct;   /* need_ent: the TLS test waits for the entropy pool (ent_pct of 128 bits) */
    sdc_u8 var, diag;                      /* var: poll variant of the next run (0 per frame, full window; 1 per frame, window 2920; 2 busy-poll 1 ms, full window); diag: result line to the lab server 0 none, 1 sending, 2 sent, 3 failed */
    int err, tcp_err, tls_err;
    sdc_u32 tcp_connect_ms, echo_ok, down_bytes, down_ms, down_bad;
    sdc_u32 tls_tcp_ms, tls_hs_ms, x25519_us0, x25519_us1, verify_us, heavy_total_us, heavy_max_us, hello_ok;
    sdc_u32 https_bytes, https_ms, https_bad, https_status;
    sdc_u32 retrans, elapsed_ms, want_down;
    sdc_u32 dbg[6];                       /* sdtcp counters for the lab run: seg_rx, rst_rx, rst_tx, no_conn, bad_csum, challenge_acks */
    char server[24];                      /* text of the lab server address */
};
#define SG_TLS_FRAME_US 16667u            /* one 60 Hz frame: the longest single crypto step must fit */

/* Controller test screen: read-only, text only, no heap.  The numbers are the raw HSD_PadMasterStatus values the console sees. */
#define SG_CTL_STILL_FRAMES 60u           /* the stick counts as at rest after this many frames without a move of SG_CTL_MOVE counts */
#define SG_CTL_MOVE 3                     /* a spread (max - min) of this many counts in the window means "moving" */
#define SG_CTL_OFFC 8                     /* a rest value beyond +/- this in either axis of the main stick is flagged OFF CENTRE */
#define SG_CTL_AXIS_STUCK 8               /* NOT CHANGING needs the buttons word non-zero or an axis beyond this */
#define SG_CTL_SAME_FRAMES 300u           /* 5 s at 60 frames/s ... */
#define SG_CTL_RATE_FRAMES 6u             /* the presenter redraws this screen at most every 6 frames (10 times a second at 60 Hz) */
struct sg_ctl_t {
    struct sdent_pad cur, prev;
    signed char mn[4], mx[4];             /* since the page opened: main X, main Y, C X, C Y */
    signed char wlo[2], whi[2];           /* range of the main stick in the current still window */
    unsigned char seen, frozen, zlatch, port, others, off_centre, not_changing, moving;
    unsigned short still, same;           /* frames still / frames bit-identical */
    unsigned short zhold;
    sdc_u32 frames, same_tb, zhold_tb, frozen_at;
};

struct sg_view {
    char t[SG_ROWS][SG_COLS + 1];
    unsigned char attr[SG_ROWS];
    char val[SG_ROWS][SG_VAL + 1];        /* the right aligned value of a row ("" = none); vattr its attribute */
    unsigned char vattr[SG_ROWS];
    unsigned char bar, bar_big;           /* grid row of the selection bar behind the selected list row (0 = none); big: the two row list font */
    unsigned char mark;                   /* 1: the small mark top left (every Settings page) */
    unsigned char qr_n;                   /* modules per side, 0 = no QR on this screen */
    unsigned char qr_row;                 /* first text row of the QR block (quiet zone included) */
    unsigned char qr_rows;                /* text rows the block covers (blank in t[]) */
    unsigned char qr[QR_OUT_BYTES];
};

/* ---- Link This Console (sg_link.c): the state machine of the screen; the glue (sg_entry.c) answers its requests with the
   wire (client_net/lnk.c) and the card (struct sgo_idcfg save) ---- */
enum { SGL_OFF = 0, SGL_ALREADY, SGL_ENT, SGL_START, SGL_SHOW, SGL_EXPIRED, SGL_SAVE, SGL_VERIFY, SGL_DONE, SGL_ERR, SGL_OFFER, SGL_SLCHK, SGL_SLOFFER, SGL_PROGQ, SGL_PROGK };
#define SG_GOTO_DEV 0x200u                             /* goto_screen(SG_HUB | this): the Online page's lab rows (Server, Slippi, In Use) alone, opened from Settings > Diagnostics (netcfg=on builds); Back leaves the overlay */
#define SG_GOTO_PAGE 0x800u                            /* goto_screen(SG_HUB | this | page << 16): one Online page (1 Account, 2 Friends and privacy, 3 Delete) straight from the Settings list; Back leaves the overlay */
#define SG_GOTO_ROOM 0x400u                            /* goto_screen(SG_GN | this | (room code + 1) << 16): the Game Night browser straight in that room (a toast's "Open Game Night") */
#define SG_GOTO_PROG 0x1000u                           /* goto_screen(SG_LINK | this): the first run STARTS with the progressive scan question, a screen of its own (with SG_GOTO_OFFER too it is not used) (the glue adds it only for an unset byte, a component cable and an interlaced picture)  */
#define SG_PT_YES 1u
#define SG_PT_NO 2u
#define SG_PT_RUN 3u                                   /* the glue started the test picture; g->prog_wii = seconds left (the byte is reused: no Wii variant exists any more) */
#define SG_PT_KEEP 4u
#define SG_PT_BACK 5u
#define SG_PT_DONE 6u                                  /* the test is over (kept, reverted, timed out, or there was no cable): the overlay leaves */
#define SG_GOTO_PROGW 0x4000u                          /* unused since (a Wii runs the same live test as a GameCube); was: with SG_GOTO_PROG: the console is a Wii (the question turns progressive scan on for the next start, no test picture) */
#define SG_GOTO_SYSF 0x2000u                           /* goto_screen(SG_UPD | this): the automatic system-files set-up (kind 3 of the Update machine): no key, a progress screen, the overlay closes by itself when done (the glue opens it when MultishinePack is missing) */
#define SG_GOTO_MINE 0x8000u                           /* goto_screen(SG_TOURN | this): the Tournament screens open on the bracket of the cup the player is in (back from the animated bracket, ml_glue.c) */
#define SG_GOTO_OFFER 0x100u                           /* goto_screen(SG_LINK | this): the first-run offer after START (hub_ovl.h fr_step): a question first, B = Later */
enum { SGL_R_NONE = 0, SGL_R_START, SGL_R_POLL, SGL_R_CANCEL, SGL_R_SAVE, SGL_R_VERIFY, SGL_R_SLCHK };
enum { SGL_E_NONE = 0, SGL_E_OFFLINE, SGL_E_UNREACH, SGL_E_RATE, SGL_E_BUSY, SGL_E_MAINT, SGL_E_DEVLIMIT, SGL_E_BANNED, SGL_E_CARD, SGL_E_NOREC, SGL_E_OLD, SGL_E_BAD };
#define SGL_URL "https://multishine.link/qr?code="   /*: the QR is the user-code URL (40 bytes with the code: QR version 3-M, 29 modules) */
#define SGL_NOPAD_FRAMES 180u                          /* no pad on the port for 3 s: the hint line */
#define SGL_SLOW_FRAMES 1200u                          /* 20 s without progress on the entropy screen: "Vary your speed." */
struct sgl {
    unsigned char st, req, err, err_from, status, saved, again, verified;   /* err_from: the state an A retries; saved 1 on the card, 2 in RAM only; again: a relink was confirmed */
    char user[10];                    /* "ABCD-EFGH" */
    char code[24], name[24];          /* the identity the server linked */
    unsigned char qr[QR_OUT_BYTES], qr_n, qr_ok;
    sdc_u32 now, t0, expires, interval, next_poll, busy_s;   /* ms clock of the glue (now), the code's start and life (s), poll period (s), next poll (ms) */
    sdc_u32 ent_frames, ent_best, ent_best_at, nopad;
    unsigned char hold;                /* B frames held on the entropy screen */
    unsigned char chain, slasked, noslp;      /* the first-run chain: chain = the offer was opened by the first run, so leaving it (Linked, Later, an error's Back) goes on to the Slippi offer; slasked = SLCHK's request was sent */
    sdc_u32 hold_tb;
    struct sdent pool;                 /* the overlay's entropy pool (fed by every frame of the overlay; SESSION level makes the link key) */
};
void sgl_init(struct sgl *k);
/* answers of the glue */
void sgl_started(struct sgl *k, const char *user, unsigned expires, unsigned interval);
void sgl_failed(struct sgl *k, unsigned err, unsigned status);               /* a network step failed: err SGL_E_*, or status = the server's (mapped here) */
void sgl_linked(struct sgl *k, const char *code, const char *name);          /* the sealed poll opened: SAVE next */
void sgl_saved(struct sgl *k, int result);                                   /* the card write: 0 saved, 1 busy (retried by the glue), -1 no usable record, -2 failed */
void sgl_verified(struct sgl *k, int ok);
void sgl_slchecked(struct sg *g, int show);                                  /* the glue's answer to SGL_R_SLCHK: 1 = show the Slippi offer (no Slippi login on the card, never offered before), 0 = the first run ends here */

/* ---- Delete This Console's Identity (sg_delid.c): two steps (A, then A held 2 s), then the server (DEVICE_FORGET 0x1A), the card,
   RAM; the glue answers the requests ---- */
enum { SGD_OFF = 0, SGD_NOTHING, SGD_ASK, SGD_HOLD, SGD_SERVER, SGD_CARD, SGD_SLIPPI, SGD_KEY, SGD_DONE };
enum { SGD_R_NONE = 0, SGD_R_FORGET, SGD_R_CARD, SGD_R_SLIPPI, SGD_R_KEY };
#define SGD_HOLD_MS 1000u                 /* the one hold length (hold B leaves, hold Z accepts, 1.0 s); was 2 s with A */
/* THREE LEVELS: 1 Multishine only (name, code, link: the server forgets it, the card record becomes the guest placeholder), 2 Multishine and the Slippi login
   (MultishineIdentity, MultishineBridge), 3 everything: also the console key, i.e. the stored entropy seed of MultishineOptions (the first-run offer returns). */
struct sgd {
    unsigned char st, req, also_slippi, have_slippi, server_ok, card_ok, slippi_ok, was_linked, level, key_ok, from_ident;   /* from_ident: opened from a Delete row of the Device Identity page (the level is chosen there: straight to the hold, B and Done return to that page) */
    sdc_u32 hold_t0, held_ms;             /* ms of the glue clock (struct sgl now) */
};
void sgd_server(struct sgd *d, int ok);   /* DEVICE_FORGET: 1 the server forgot (OK, AUTH_FAILED, TOKEN_REVOKED), 0 not told (no network, no answer) */
void sgd_card(struct sgd *d, int ok);     /* the card record no longer holds the identity (read back) */
void sgd_slippi(struct sgd *d, int ok);   /* MultishineIdentity (and MultishineBridge) deleted */
void sgd_key(struct sgd *d, int ok);      /* the stored entropy seed (console key) replaced by an empty record */

struct sg {
    unsigned char scr, cur, sub, prov, id_stage, signed_in, confirm, quit, req_tls, qr_ok, qr_n, hold_b;
    unsigned short prev_btn;
    unsigned short prev_raw;              /* last raw buttons word (sg_act_filter) */
    unsigned short act_blk;               /* action buttons whose raw press was refused as a repeat and are still down (sg_act_filter) */
    unsigned char act_cool[9];            /* per action button: samples left before a new raw press counts again (SG_ACT_GAP) */
    unsigned char cover;                  /* 1 while the screen the overlay was opened on is an Event Match screen it has not built yet (Game Night, Standings, Friends): the resident glue keeps its loading cover up until it is 0 (the hole before hold_tb, nothing moves) */
    sdc_u32 hold_tb;                     /* time base when the current B hold began */
    signed char prev_sy;
    signed char stick_x;                  /* main stick X this frame (the Game Night pick grid moves left / right with it) */
    sdc_u32 frame, t_link;
    struct sg_ent_t ent;
    struct sg_tls_view tls;
    struct sgu_view upd;                  /* the Update screen: drawn from here, requests raised here, filled by the state machine (sgu_core.c) every frame */
    unsigned char qr[QR_OUT_BYTES];
    sdc_u32 hold_lim;                     /* time-base ticks of the hold-B limit (0 = SG_HOLD_B_TICKS); ctl and direct are after it so the resident glue's view of the leading fields does not move */
    struct sg_ctl_t ctl;                  /* the Controller test screen */
    unsigned char direct;                 /* the screen was opened straight from the menu (goto): Back leaves the overlay instead of showing the hub */
    struct sgm mk;                        /* the Mockups hub (sg_mockup.c); after every field the resident glue reads */
    struct gnb gn;                        /* the Game Night browser (gnb.c); after everything above */
    struct stb sd;                        /* the Grandmaster standings (stb.c) */
    struct frd fr;                        /* the Friends screen (frd.c); after everything above */
    struct gnt tn;                        /* the Tournament screens (gnt.c) */
    unsigned char fight, fight_req;       /* Online row "Friend requests after a fight": 0 unknown, 1 off, 2 on, 3 not available; request 1 read, 2 write fight, 3 write pol (the glue answers) */
    /* ---- the Online page; after every field above ---- */
    unsigned char sl_hub;                 /* Send Slippi Login was opened from the Online page (Back returns there, not to the Device Identity page) */
    unsigned char id_top, del_acct;       /*: Settings > Account IS the identity page (id_top: B leaves to the Settings list); del_acct: the Delete page was opened from its Delete row */
    unsigned char pol;                    /* "Who can invite me": 0 unknown, SG_POL_FRIENDS / ANYONE / NOBODY, SG_POL_NA not available (read with the fight byte) */
    unsigned char linked;                 /* SG_ID_*: the identity in force (the glue fills it from the resident config) */
    unsigned char upd_cur;                /* Update screen cursor: 0 Check Now (A), 1 the Backup To Slot B option, 2 the Keep option */
    unsigned char bak, bak_req;           /* Update > Backup To Slot B: 0 unknown (the row is not shown), 1 off, 2 on (the default); req 1 = the glue writes it */
    unsigned char cpy_ok;                 /* the host can copy the card to slot B (sgu_host.copy_b): the Update page shows the row 'Copy card to slot B'; 0 = no row (set by the glue) */
    unsigned char keep, keep_req;         /* Update > Keep the previous build as backup: 0 unknown (the row is not shown), 1 off, 2 on (the default); req 1 = the glue writes it */
    const char *hint;                     /* Online page: the status line under the rows (0 = none) */
    unsigned char sysf, sysf_t;           /* sysf: this open is the system-files set-up; sysf_t: frames since it finished (the screen stays a moment, then closes) */
    unsigned char dev;                    /* 1 = the Online page screen shows the lab rows only (SG_GOTO_DEV); appended inside the Online block */
    unsigned char onp;                    /* Online page: 0 = its short list of three pages (Account, Friends and privacy, Delete), 1 = inside the page of the row under the cursor */
    struct sgl lk;                        /* Link This Console */
    char id_code[24], id_name[24];        /* the identity in force (the glue copies it from the resident config): Device Identity, "Linked as" */
    unsigned char id_dev[8];
    unsigned char id_sync;                /* 0 not this session, 1 the server accepted this identity this session */
    unsigned char id_slippi;              /* MultishineIdentity on the card: 0 unknown, 1 none, 2 present (the glue looks once per entry; never its contents) */
    struct sgd del;                       /* Delete This Console's Identity */
    unsigned char tech;                   /* Settings > Game > Show technical info (option id 21, read every frame by id_frame; an older resident answers -1 = off) */
#if SG_NETSEL
    /* lab/dev Online rows: Server (the server / Lab) and Slippi (Our Mock / Real Slippi), the one-time confirm, the In Use row.  req: 1 read, 2 write (the glue answers) */
    unsigned char srv, srv_req;           /* 0 unknown, SG_SRV_VPS / SG_SRV_LAB, SG_SRV_NA (the resident has no such option) */
    unsigned char mmr, mmr_req, mmk;      /* 0 unknown, SG_MMR_MOCK / SG_MMR_REAL, SG_MMR_NA; mmk: the confirm was accepted once (stored by the resident) */
    unsigned char nconf, srv_lock;        /* nconf: the Real Slippi confirm is on screen; srv_lock: 0 unknown, 1 no patched server, 2 patched and locked */
    unsigned char ns_age;                 /* frames since the values were read (re-read about twice a second while the page is open) */
    sdc_u32 srv_ip, mm_ip;                /* the server address in force (0 = by name), the card's matchmaker override (0 = none) */
    char srv_host[24];                    /* the server name when the address is 0 */
#endif
    struct sgr sr;                        /* Send Slippi Login (sgr.c); the glue sets sr.h (the host services) after sg_init; at the very end so no earlier field moves */
    unsigned char rgn, wch;               /* Online rows Region and Who can watch my games (read with the fight byte; fight_req 4 / 5 write them): rgn 0 unknown, 1..6 NAE NAW EU LA AS OC, 7 not set, 8 not available; wch 0 unknown, 1 Nobody, 2 Friends, 3 not available */
    unsigned char prog_wii, sl_chain;     /* prog_wii: the question for a Wii (no late picture switch there: A turns it On for the next start, nothing is tested); sl_chain: unused since (the question is the first step now; the byte stays so the struct layout does not move) */
    unsigned char prog_ask, prog_try;     /* first-run progressive scan question (sg_link.c SGL_PROGQ): prog_ask = the glue wants it at the end of the first run; prog_try = the answer / state shared with the glue (SG_PT_*: 1 Yes asked, 2 Not now, 3 glue started the 10 s test, 4 keep, 5 go back, 6 test over) */
};
enum { SG_ID_UNKNOWN = 0, SG_ID_GUEST = 1, SG_ID_LINKED = 2, SG_ID_RAMONLY = 3 };
#if SG_NETSEL
enum { SG_SRV_VPS = 1, SG_SRV_LAB = 2, SG_SRV_NA = 3, SG_SRV_LAN = 4 };   /* stored 0 / 1 / 2; shown Production / Dev / Lab */
#ifndef SG_SRV_COUNT
#define SG_SRV_COUNT 3                    /* lab builds: Production, Dev, Lab.  A release build that shows the row will define 2: Production, Dev */
#endif
enum { SG_MMR_MOCK = 1, SG_MMR_REAL = 2, SG_MMR_NA = 3 };
#define SG_NS_REFRESH 30u                 /* frames between two reads of the rows' values */
#endif
enum { SG_RGN_N = 7, SG_RGN_NONE = 8, SG_RGN_NA = 9, SG_WCH_NOBODY = 1, SG_WCH_FRIENDS = 2, SG_WCH_NA = 3 };
enum { SG_POL_UNKNOWN = 0, SG_POL_FRIENDS = 1, SG_POL_ANYONE = 2, SG_POL_NOBODY = 3, SG_POL_NA = 4 };
/* the Online page rows (SG_HUB) */
/*: Device Identity on top; Update is its own top-level row of the Settings list (the start-check option stays here as a value row), Send Slippi Login and Delete are rows of the Device Identity page:
   the last three enum slots are kept for their labels and descriptions but are not rows of this page (row_hidden) */
enum { SG_ON_IDENT = 0, SG_ON_LINK, SG_ON_FIGHT, SG_ON_POL,
#if SG_NETSEL
       SG_ON_SRV, SG_ON_MMR, SG_ON_NET,
#endif
#if SG_LAB
       SG_ON_TLS,
#endif
       SG_ON_SLOGIN, SG_ON_UPD, SG_ON_DEL, SG_ON_DEL2, SG_ON_DEL3,
       SG_ON_REGION, SG_ON_WATCH,            /* appended: the account's Region and Who can watch my games (shown after Who can invite me) */
       SG_ON_N };
/* (checklist 13): nothing is hidden behind a press.  The Online page lists, in the order of ON_ORDER (sg_ui.c): Device Identity (status, A opens the details), Send Slippi Login, the three Delete
   rows (SG_ON_DEL .. SG_ON_DEL3 = levels 1 .. 3), Link This Console, the start check, the two invite settings (lab: the lab rows); the list scrolls with the cursor (7 rows in view).  SG_ON_UPD stays hidden. */
const char *sg_on_label(unsigned row);     /* label of an Online page row (host checks) */
/* the Account page's choosable rows in order: Link this console (only when not linked), Region, Send Slippi login, Delete; returns how many */
enum { SG_AI_LINK, SG_AI_REGION, SG_AI_SLOGIN, SG_AI_DELETE };
unsigned sg_acct_items(const struct sg *g, unsigned char out[4]);
unsigned sg_acct_pos(const struct sg *g, unsigned item);   /* the row of that item (0 when absent) */
const char *sg_on_desc(unsigned row);      /* its one-line description (<= 44 characters) */

/* placeholder pairing URL of the mockup (reserved .example host, a fixed non-random code: nobody can approve anything with it) */
#define SG_MOCK_URL "https://link.multishine.example/l/mockmockmockmockmockmockmo"
#define SG_MOCK_USER_CODE "ABCD-EFGH"
#define SG_MOCK_DEVICE_ID "7F3A-91C2-E0B4-5D18"

/* the Settings look, shared by the screens of every sign-in source file (defined in sg_ui.c) */
void sg_putv(struct sg_view *v, unsigned r, unsigned attr, const char *s);                  /* the right aligned value of row r */
void sg_head_t(struct sg_view *v, const char *title);                                        /* the mark and the title on row 0 */
#define sg_head(v, t, c) sg_head_t((v), (t))                                                  /* the third argument (a breadcrumb) is dropped by the preprocessor: no breadcrumb any more */
#define SG_QR_ROW 3u               /* the shared QR page: first grid row of the code, */
#define SG_QR_ROWS 16u             /* rows it covers (3 to 18; the title is above, the text from row 19) */
void sg_qr_page(struct sg_view *v, unsigned n, const unsigned char *bits, const char *l1, const char *l2, const char *code, const char *s1, const char *s2);
void sg_head_tag(struct sg_view *v, unsigned attr, const char *tag);                         /* a word at the right of the title row (PASS, FAIL) */
void sg_bar(struct sg_view *v, unsigned r, unsigned big);                                    /* the selection bar behind the list row at grid row r */
void sg_init(struct sg *g);
/* Link This Console (sg_link.c): entered by sg_goto(SG_LINK); one frame (edge = newly pressed buttons); the view */
void sgl_enter(struct sg *g);
void sgl_progq(struct sg *g);                 /* the first-run progressive scan question alone (SG_GOTO_PROG without SG_GOTO_OFFER) */
void sgl_offer(struct sg *g);                 /* the Link screen opened as the first-run offer (the question first) */
void sgl_frame(struct sg *g, unsigned edge, const struct sdent_pad *p, sdc_u32 tb);
void sgl_draw(const struct sg *g, struct sg_view *v);
/* Delete This Console's Identity (sg_delid.c) and the Device Identity screen */
void sgd_enter(struct sg *g);
void sgd_enter_level(struct sg *g, unsigned level);   /* a Delete row of the Device Identity page: the level 1..3 is the row, A went to the hold at once */
void sgd_frame(struct sg *g, unsigned edge, unsigned held);
void sgd_draw(const struct sg *g, struct sg_view *v);
void sg_ident_draw(const struct sg *g, struct sg_view *v);
/* one call per rendered frame; p = the pad of the port in use (buttons are the held bits above), tb = low 32 bits of the time base */
unsigned sg_act_filter(struct sg *g, unsigned raw);   /* the fresh-press gate of sg_frame (SG_ACT_GAP) */
void sg_frame(struct sg *g, const struct sdent_pad *p, sdc_u32 tb);
void sg_draw(const struct sg *g, struct sg_view *v);
void sg_goto(struct sg *g, unsigned scr);
unsigned sg_about_lines(void);           /* lines of the scrolled About list (sg_ui.c AB) */
void sg_hub_page(struct sg *g, unsigned page);   /* inside page 1..3 of the Online pages, opened straight from Settings (onp 2: Back leaves the overlay) */
void sgm_frd_join(struct sg *g);                  /* the Friends list's Join Room (frd.join): to the Game Night browser, joining that room */
/* QR as a runtime texture (GX_TF_I4, 8x8 tiles, 4 bits per texel, 2 x 2 texels per module, the module matrix centred on a white 104 x 104 field = 52 modules: the quiet zone is
   whatever is left around it: 11 modules in all for version 6 (41 modules, the Send Slippi Login address: 5 left and 6 right, at least 4 required), 12 for version 5, 23 for the
   version 3 Link code.  Enlarged from 88 (3,872 bytes) on; the resident's quad is 16 text rows square to match, and SGO_VER 3 makes an old resident and a new overlay refuse
   each other.  out must be 32-byte aligned for the GPU: SG_QR_TEX_BYTES bytes.  Pure, host-tested. */
#define SG_QR_TEX_DIM 104u
#define SG_QR_QUIET 4u                /* modules of white around the code: the QR minimum */
unsigned sg_qr_dim(unsigned n);   /* texels the sprite shows (the top left dim x dim of the texture): code + quiet zone */
#define SG_QR_TEX_BYTES 5408u
void sg_qr_texture(const unsigned char *bits, unsigned n, unsigned char *out);
/* pure helpers (host tests) */
void sg_ctl_start(struct sg_ctl_t *t, unsigned port);
/* one call per frame: p = the port in use; lim = ticks of 0.75 s (0 = the GameCube value).  Frozen: nothing updates.  The view calls it through sg_frame. */
void sg_ctl_frame(struct sg_ctl_t *t, const struct sdent_pad *p, sdc_u32 tb, sdc_u32 lim);
void sg_ctl_ports(struct sg_ctl_t *t, const unsigned char err[4]);      /* err byte of HSD_PadMasterStatus[0..3]: ports other than the one in use whose err is 0 (a pad) */
void sg_ent_start(struct sg_ent_t *t);
void sg_ent_frame(struct sg_ent_t *t, const struct sdent_pad *p, sdc_u32 tb);
int sg_tls_verdict(const struct sg_tls_view *v);     /* 0 running / not started, 1 pass, 2 fail, 3 pass but a crypto step is over one frame */
extern unsigned sg_cut_count;
char *sg_u(char *p, sdc_u32 v);                    /* decimal, returns the end */
#endif
