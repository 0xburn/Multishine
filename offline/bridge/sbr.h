/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr: the console-native Slippi bridge core (the Slippi interop rules "Console-native bridge", readiness:
 * ).  Project-owned C99, no libc, no heap, not reentrant (the GC glue calls it from the
 * scene loop with interrupts disabled, like s8_lobby).
 *
 * What it does (since the 3.7.0 matchmaker): the RESIDENT half of the Slippi bridge core.  The matchmaker protocol (WebSocket, text JSON)
 * runs in ms/rep.ovl (rep_mm.c over sbr_ws.c); this module gives it commands and takes its results through the versioned MAILBOX (sbr_mbx.h): search (mode, code),
 * cancel; state, error, queue seconds and the match record back.  It owns the states and status words the menus read, the R3 error texts, the peer step's time budget
 * and the bounded fresh search after a failed peer connection (cfg.reticket_max); the netplay layer then connects to the opponent from the SAME local UDP port.
 *
 * What it refuses (the charter): no Ranked, Teams or Party constant exists in this module (R6/M9: the only modes it can
 * produce are SBR_MODE_UNRANKED and SBR_MODE_DIRECT); an assignment whose matchId names "mode.ranked" is aborted; no search
 * unless an identity exists (no status server, no signed document since: Slippi itself decides by appVersion or identifier);
 * an error that carries latestVersion ends searching for the rest of the session (R4, nothing is stored); no retry after an error (R11); every error text carries the
 * R3 notice.  The play key (R7) never enters this module or the mailbox: the overlay copies it from the card identity for the join message only, status words and texts never contain it.
 *
 * Not here: the WebSocket client, DNS of the matchmaker, STUN (all in the overlay), the EXI/menu side (sbr_app.c), the peer protocol (the rollback engine).
 */
#ifndef SBR_H
#define SBR_H

#include "sbr_ws_const.h"

typedef unsigned int sbr_u32;
typedef int sbr_s32;
typedef unsigned short sbr_u16;
typedef unsigned char sbr_u8;

/* ---- the only two modes the bridge can ever send (Slippi's OnlinePlayMode values).  Ranked, Teams and Party have NO
   constant in this module on purpose (R6, mock test M9 and the run_tests.sh grep). ---- */
#define SBR_MODE_UNRANKED 1u
#define SBR_MODE_DIRECT 2u

#define SBR_PLAT_GC 0x01u
#define SBR_PLAT_WII 0x02u
#define SBR_PLAT_VWII 0x04u
#define SBR_VER_LEN 16                    /* "3.6.4": the appVersion string (cfg.app_version) and the latestVersion field of an outdated error */

/* ---- the player's own identity (MultishineIdentity on the card, from THEIR user.json; never shown, never logged) ---- */
#define SBR_UID_MAX 64
#define SBR_KEY_MAX 128                   /* the card payload's field size (offline/provision: playKey[128]) */
#define SBR_CODE_MAX 12
#define SBR_NAME_MAX 48
#define SBR_IDNAME_MAX 64                 /* displayName[64] of the card payload (UTF-8) */
struct sbr_ident {
    char uid[SBR_UID_MAX];
    char play_key[SBR_KEY_MAX];
    char code[SBR_CODE_MAX];
    char name[SBR_IDNAME_MAX];
};

/* ---- states: Slippi's ProcessState values (they go straight into the MSRB state byte) ---- */
enum { SBR_IDLE = 0, SBR_INIT = 1, SBR_MM = 2, SBR_OPP_CONN = 3, SBR_CONNECTED = 4, SBR_ERROR = 5 };
enum {
    SBR_E_NONE = 0, SBR_E_GATE /* only 'no app version configured' since */, SBR_E_MODE, SBR_E_CODE, SBR_E_NO_ID, SBR_E_NO_NET, SBR_E_CONNECT, SBR_E_JOIN,
    SBR_E_BAD_JOIN, SBR_E_BAD_STATUS, SBR_E_SERVER, SBR_E_LOST, SBR_E_OUTDATED, SBR_E_RANKED, SBR_E_PROTO, SBR_E_SEND,
    SBR_E_PEER, SBR_E_BUSY, SBR_E_DNS, SBR_E_COUNT
};

struct sbr_cfg {
    sbr_u32 server_ip;           /* 0 = the overlay resolves `host`; lab / Dolphin override, host order */
    sbr_u16 server_port;         /* TCP port of the WebSocket matchmaker; the mock listens on its own port */
    sbr_u16 dns_port;            /* 0 = 53 (lab resolver on another port) */
    sbr_u32 dns_ip;              /* 0 = the DHCP resolver (lab override) */
    const char *host;            /* the matchmaker's name (cfg mm_name, static); passed to the overlay in the mailbox */
    sbr_u16 local_port;          /* our UDP port: the netplay layer must reuse it (hole punch, M:316-346) */
    sbr_u32 lan_ip;              /* our DHCP address (0 = unknown -> ipAddressLan "") */
    sbr_u32 connect_ms;          /* unused since 3.7.0 (the overlay's timers are the stock ones, sbr_ws_const.h); the cfg block layout stays */
    sbr_u32 create_ms;           /* unused since 3.7.0 */
    sbr_u32 peer_ms;             /* 8000 (stock) */
    sbr_u32 reticket_max;        /* fresh tickets after a failed peer connection (stock: unbounded; ours: bounded, R11; compiled release default 10) */
    sbr_u32 platform;            /* SBR_PLAT_* */
    char app_version[SBR_VER_LEN]; /* the ticket's appVersion (Slippi release we match), NUL terminated */
};

struct sbr_ops {
    int (*send)(void *ctx, sbr_u32 ip, sbr_u16 port, const sbr_u8 *d, sbr_u32 len);    /* one datagram, >= 0 ok */
};

#define SBR_STAGES_MAX 16
struct sbr_match {                /* what the netplay layer consumes after an assignment */
    sbr_u32 seq;
    sbr_u32 ready;                /* 1 once the matchmaker connection is closed (DISCONNECT acknowledged or 3 s): bind the peer host now */
    sbr_u32 is_host;              /* the decider (player index 0 in 1v1) */
    sbr_u32 local_idx;            /* port - 1 of our row */
    sbr_u32 opp_ip;               /* host order */
    sbr_u16 opp_port, local_port; /* local_port == cfg.local_port: reuse it */
    sbr_u32 opp_is_bot, used_lan;
    sbr_u32 items, nstages;
    sbr_u16 stages[SBR_STAGES_MAX];
    char opp_code[SBR_CODE_MAX];
    char opp_name[SBR_NAME_MAX];
    char match_id[52];
    char opp_uid[SBR_UID_MAX];    /* the opponent's Slippi uid from get-ticket-resp ("" if absent): RAM only, never shown or logged (R7); read by the game report (menus/rep) */
};

struct sbr_status {               /* RAM words for DIAG / the harness: numbers only, never a secret */
    sbr_u32 magic;                /* 'SBR1' */
    sbr_u32 state, err, gate /* always 0 */, mode;
    sbr_u32 searches, tickets, cancels, errors, assigned, retickets, peer_ok, peer_fail;
    sbr_u32 rx_msgs, rx_bad, rx_ignored, srv_connects, srv_disc, wipes;
    sbr_u32 t_search_ms, t_assign_ms;
};

#define SBR_ERR_MAX 124           /* the MSRB error field shows up to 120 characters on 4 lines */

#include "sbr_mbx.h"              /* struct sbr_mbx needs struct sbr_match above */

struct sbr {
    struct sbr_cfg cfg;
    const struct sbr_ops *ops;
    void *ctx;
    struct sbr_status st;
    const struct sbr_ident *id;
    int id_ok, net_up, outdated;       /* outdated: the matchmaker answered with latestVersion this session (no second search) */
    char app_version[SBR_VER_LEN];
    char latest[SBR_VER_LEN];          /* latestVersion of the outdated error ("" if none) */
    int state, err, reticket, owned;   /* owned: a search command is out and the overlay's words are ours to take */
    sbr_u32 now, t_state, t_cmd, mode;
    sbr_u8 retry_wait;                 /* 1: a fresh search is due once SBR_RETICKET_MS have passed since t_state */
    char code[SBR_CODE_MAX];
    char err_text[SBR_ERR_MAX];
    struct sbr_match match;
    struct sbr_mbx mbx;                /* the mailbox to the matchmaker client in ms/rep.ovl (sbr_mbx.h) */
};

void sbr_init(struct sbr *B, const struct sbr_cfg *cfg, const struct sbr_ops *ops, void *ctx);
/* identity read from the card (status != 0 -> usable); the struct must stay valid while the bridge runs */
void sbr_identity(struct sbr *B, const struct sbr_ident *id);
void sbr_set_net(struct sbr *B, int up, sbr_u32 lan_ip);
/* B4 equivalent: mode SBR_MODE_UNRANKED or SBR_MODE_DIRECT, code = ASCII connect code (Direct only; ignored otherwise).
   0 = search started, < 0 = refused (state ERROR with a text, except SBR_E_BUSY which leaves the state alone). */
int sbr_search(struct sbr *B, sbr_u32 now, sbr_u32 mode, const char *code);
void sbr_cancel(struct sbr *B);               /* BA equivalent: back to IDLE from any state */
void sbr_tick(struct sbr *B, sbr_u32 now);
/* the netplay layer reports the peer connection (OPP_CONN only): ok -> CONNECTED; fail -> a fresh ticket while
   reticket < cfg.reticket_max, else ERROR */
void sbr_peer_result(struct sbr *B, sbr_u32 now, int ok);
int sbr_state(const struct sbr *B);
const char *sbr_error_text(const struct sbr *B);   /* "" when none; always ends with the R3 notice */
/* an error found by the console glue before or around a search (e.g. SBR_E_DNS): state ERROR with the R3 text, the matchmaker
   connection (if any) closed; cleared by sbr_cancel like every other error */
void sbr_fail(struct sbr *B, sbr_u32 now, int e, const char *text);   /* R3 suffix only for fault codes */
void sbr_fail_fault(struct sbr *B, sbr_u32 now, int e, const char *text);   /* always with the R3 suffix (test mode, engine unavailable) */
/* the two R3 lines every bridge screen shows (entry, searching, error, connected) */
extern const char *const sbr_notice[2];

/* helpers exported for the host tests */
int sbr_code_valid(const char *code);
sbr_u32 sbr_code_to_sjis(const char *code, sbr_u8 out[18]);     /* the game's full-width form, cut at the first 0 */

/* reply parser (strict, bounded, no heap): the fields the stock client reads */
#define SBR_PLAYERS_MAX 4
struct sbr_player {
    char uid[SBR_UID_MAX];
    char name[SBR_NAME_MAX];
    char code[SBR_CODE_MAX];
    char ip[24], lan[24];
    sbr_s32 port;                 /* 1-based index, 0 absent */
    sbr_s32 local, bot;           /* 0/1 (absent = 0) */
    sbr_s32 has_lan;              /* ipAddressLan present (empty string counts as present-and-empty) */
};
struct sbr_reply {
    char type[24];
    char error[SBR_ERR_MAX];
    char latest[SBR_VER_LEN];
    char ticket[SBRW_TICKET_MAX], token[SBRW_TOKEN_MAX];   /* WebSocket matchmaker: joined{ticketId,token} */
    sbr_u32 queue_s;                                       /* status{secondsInQueue}, whole seconds */
    char match_id[52];
    sbr_s32 is_host;              /* absent = 0 (stock default false) */
    sbr_u32 items, nstages, nplayers, players_seen;
    sbr_u16 stages[SBR_STAGES_MAX];
    struct sbr_player pl[SBR_PLAYERS_MAX];
};
#define SBR_JSON_MAX_DEPTH 6
#define SBR_RETICKET_MS 1500      /* pause before a fresh ticket after a failed peer connection (the stock client has none; ours is bounded and polite) */
#define SBR_CLOSE_MS 3000         /* stock: wait up to 3 s for the matchmaker DISCONNECT (M:185-204) */
int sbr_parse_reply(const sbr_u8 *js, sbr_u32 len, struct sbr_reply *out);   /* 0 ok, < 0 malformed */
/* validate a match body and fill the match record (shared by sbr.c and sbr_ws.c): 0, or SBR_E_RANKED / SBR_E_PROTO with *text */
int sbr_assign_parse(const struct sbr_reply *R, sbr_u16 local_port, sbr_u32 mseq, struct sbr_match *M, const char **text);

#endif
