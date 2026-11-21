/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_app: the console side around the bridge core ( items 1-4).
 * Project-owned C99, no libc, no heap, not reentrant (the GC glue calls it from the EXI entry and from the scene-loop slice,
 * both on the game thread with interrupts disabled).
 *
 *  - Menu / EXI backend (sdshim vtable): Slippi's OWN online menus send B9 (online status), BE (code autocomplete), B4 (find
 *    opponent), B5 (selections), B3 (match state), BA (cleanup).  B9 reports "logged in" (the Unranked and Direct rows exist)
 *    ONLY when a usable identity exists (card file or patched in the image); otherwise Slippi's menu
 *    shows its logged-out form (no search row at all: hidden, not greyed).  There is NO status server, signed document or clock
 *    check any more (if Slippi wants to ban us it does so by appVersion or identifier).  sbra_row_hidden is the menu rule behind the
 *    unlock hook: Ranked, Teams and Party never exist on the bridge (R6).  B4 with any other mode than Unranked (1) or Direct
 *    (2) is refused by the core before any socket is used.  Direct takes the connect code typed on Slippi's code-entry screen.
 *  - Identity: "MultishineIdentity" read from the card at B9 (sbr_card.h), kept only in this struct (R7).
 *  - DNS: when the cfg gives no matchmaker address, the name is resolved with sddns at each search (cache, TTL clamps), and
 *    only after the mode, the identity and the code passed (no lookup the player did not ask for).
 *  - Peer: after the assignment and the matchmaker close, an ENet connection to the opponent from the SAME local port
 *    (the stock hole-punch rule); its success is reported to the core (sbr_peer_result).  The engine hand-off (the real
 *    Slippi peer protocol, D2-D11) is NOT here: in the lab profile the match runs on a stub opponent (neutral pads).
 */
#ifndef SBR_APP_H
#define SBR_APP_H

#include "sbr.h"
#ifndef SBRA_NO_PEER_PROBE
#include "../common/sdenet/sdenet.h"       /* the lab peer probe (struct sdenet_host); the product compiles it out and has no ENet here at all since the 3.7.0 matchmaker */
#endif
#include "../net/sddns.h"
#include "../exi_shim/sdshim.h"

typedef sbr_u32 sbra_u32;

/* ---- cfg block (patched into the image by build_sbr.py; magic SBRCFG01) ---- */
#define SBRA_F_NO_NET 0x01u        /* never bring up the BBA */
#define SBRA_F_STUB_MATCH 0x02u    /* LAB: once CONNECTED the opponent is a stub (ready after stub_ready_ms, neutral pads) */
#define SBRA_F_PEER_STUB 0x04u     /* LAB: no ENet probe of the opponent; the peer counts as connected after 500 ms */
#define SBRA_F_STOP_AT_ASSIGN 0x10u /* first live step option: end with an error right after the assignment (no peer contact, no
                                      re-ticket): the ticket and the assignment can be reviewed without touching a stranger's game */
#define SBRA_F_ENGINE_PEER 0x20u   /* ENGINE HAND-OFF: after the assignment the rollback engine plays the match against the real Slippi
                                      client instead of the lab probe / stub: the engine owns the peer ENet host (same UDP port), B0/B1/B2/B5 and the match
                                      block of B3 come from it.  Needs an io->eng table (br_glue.c in the combined image); without it the search ends with an
                                      error.  Live builds: only with `build_sbr.py live --peer-handoff`, never together with 0x10 or any LAB flag. */
#define SBRA_F_DNS_ANYSRC 0x08u    /* LAB/Dolphin only: Dolphin's BuiltIn BBA forwards a query sent to its router (the DHCP resolver) to
                                      BBA_BUILTIN_DNS and returns the answer with THAT address as source; a real router answers from
                                      its own address.  With this flag the glue maps a port-53 answer's source to the configured resolver
                                      (txid and question still checked).  Never in a non-lab image (build_sbr.py refuses it with --live). */

#define SBRA_HIST 4
#define SBRA_HISTN 10   /* Direct code list kept per player: Slippi's cap, the '#' namespace of MultishineCodes (codehist.h CH_NS_MAX) */
#define SBRA_HISTP 4    /* B4 codes waiting for the card write */
#define SBRA_NAME 64
struct sbra_cfg {
    sbr_u8 magic[8];
    sbr_u32 flags;
    sbr_u32 mm_ip;                 /* 0 = resolve mm_name */
    sbr_u32 mm_port;
    sbr_u32 dns_ip;                /* 0 = the DHCP server's */
    sbr_u32 local_port;            /* matchmaker AND peer port (same-port rule) */
    sbr_u32 dns_lport;             /* local source port of the DNS socket */
    sbr_u32 start_frame;
    sbr_u32 connect_ms, create_ms, peer_ms, reticket_max;
    sbr_u32 platform;              /* SBR_PLAT_* */
    sbr_u32 delay;                 /* MSRB delay frames (lab stub) */
    sbr_u32 stub_ready_ms;
    sbr_u32 stub_sel;              /* stub selections: char << 24 | color << 16 */
    sbr_u32 dns_port;              /* 0 = 53; a private resolver on another port (console run on the lab LAN: the Mac cannot bind 53) */
    char app_version[SBR_VER_LEN]; /* the ticket's appVersion: the Slippi release we match (default SBRA_APP_VERSION) */
    char mm_name[SBRA_NAME];
    char hist[SBRA_HIST][12];
};
#define SBRA_CFG_SIZE (8 + 4 * 16 + SBR_VER_LEN + SBRA_NAME + 4 * 12)   /* build_sbr.py CFG_LEN */
/* The one place the appVersion string is set in the source (everything else copies it): the Slippi release the bridge claims to match in create-ticket.
   Patchable per image: build_sbr.py ... app_version=3.7.0 */
#define SBRA_APP_VERSION "3.7.0"
#define SBRA_CFG_MAGIC "SBRCFG01"

/* ---- status block (RAM, watched by the harness; words, numbers only, never a secret) ---- */
enum { SBRA_N_WAIT = 0, SBRA_N_INIT, SBRA_N_LINK, SBRA_N_DHCP, SBRA_N_UP, SBRA_N_NOBBA, SBRA_N_FAIL_INIT, SBRA_N_FAIL_LINK,
       SBRA_N_DISABLED };
enum { SBRA_Q_IDLE = 0, SBRA_Q_SENT, SBRA_Q_DONE, SBRA_Q_FAILED, SBRA_Q_NONE };
enum { SBRA_P_IDLE = 0, SBRA_P_CONNECTING, SBRA_P_FAILED, SBRA_P_CONNECTED, SBRA_P_STUB };
struct sbra_status {
    sbr_u32 magic;                 /* 'SBRA' */
    sbr_u32 net, ip, app_state, ident, ident_ok, ident_parse, ident_slot;
    sbr_u32 cache, cache_slot, cache_dirty, cache_writes, cache_wstatus, cache_flags;
    sbr_u32 q_state, q_sent, q_rx, q_bad, q_last, time_ok, now_unix, doc_src;   /* LEGACY status-query words: always 0 since (kept so the RAM names do not move) */
    sbr_u32 gate, gate_evals, blocked;                                           /* LEGACY: always 0 */
    sbr_u32 dns_state, dns_err, dns_lookups, mm_ip, dns_server, dns_rx, dns_ignored, dns_bad, dns_last_src;
    sbr_u32 state, err, mode, searches, tickets, assigned, retickets, cancels, wipes;
    sbr_u32 peer, peer_ip, peer_port, peer_ok, peer_fail;
    sbr_u32 b9, b4, b3, b5, ba, be, b0, other, refused, hidden_rows;
    sbr_u32 local_ready, remote_ready, local_idx, is_host, mm_seen, lock_fail, tick_max, menu_ba;
    sbr_u32 lazy, entered, write_ok, enter_gate_evals;   /* LEGACY: always 0 */
    sbr_u32 err_text[32];          /* the MSRB error text (ASCII, contains the R3 notice) */
    sbr_u32 nc_state, nc_mm_ip, nc_status_ip, nc_status_port;   /* (nc_status_*: always 0, ignored since) NETCFG (dev/lab builds): SBR_NC_ST_*; the effective overrides; all 0 in a release build */
};

/* sbra_engine_ops.ready result bits.  Legacy (no 0x80): non-zero = the remote player is ready, the local flag is the bridge's own (set by B5, cleared by BA).
   With SBRA_ENG_FLAGS the engine owns BOTH flags (bit 0 remote, bit 1 local): it clears them when a game starts, so a finished game does not leave the CSS "locked in" (a stale pair
   of flags made the online CSS start a rematch by itself). */
#define SBRA_ENG_REMOTE 0x01u
#define SBRA_ENG_LOCAL 0x02u
#define SBRA_ENG_FLAGS 0x80u
struct sbra_engine_ops {         /* implemented by br_glue.c on top of the online glue's engine (s12_gc.c br_engine_*) */
    int (*begin)(void *ctx, const struct sbr_match *m, sbr_u32 mode);   /* start the engine session for this assignment (network slice); 0 ok */
    int (*state)(void *ctx);                                             /* 1 connected (ENet up), 0 connecting, -1 lost / failed */
    int (*b3)(void *ctx, sbr_u8 *msrb);                                  /* the engine's B3 block into a scratch MSRB (SBRA_MSRB_SIZE) */
    int (*b5)(void *ctx, const sbr_u8 *pstb);                            /* the engine's SetMatchSelections */
    sbr_u32 (*ready)(void *ctx);                                         /* the remote player's character is selected (legacy: non-zero) or SBRA_ENG_* bits */
    void (*end)(void *ctx);                                              /* optional (may be 0): the bridge dropped the assignment (re-ticket, error, cancel): end the engine session unless a match is running (review M1) */
};
struct sbra_io {
    int (*send)(void *ctx, sbr_u32 ip, sbr_u16 port, const sbr_u8 *d, sbr_u32 len);          /* matchmaker + peer socket */
    void *ctx;
    const struct sbra_engine_ops *eng;                                                        /* optional (SBRA_F_ENGINE_PEER) */
};

struct sbra {
    const struct sbra_cfg *cfg;
    struct sbra_status *st;
    const struct sbra_io *io;
    struct sbr B;
    struct sbr_ident id;           /* R7: never shown, never logged; read by the core's ticket writer only */
    sddns dns;
#ifndef SBRA_NO_PEER_PROBE
    struct sdenet_host P;          /* the peer probe (same local port); LAB modes only (flags without SBRA_F_ENGINE_PEER / SBRA_F_STOP_AT_ASSIGN) */
#endif
    sbr_u32 now, seed, dhcp_dns, net_up;
    /* card */
    struct sbr_ident bid;          /* the PATCHED identity (image), see sbra_set_baked_ident; R7 like id */
    int bid_ok, baked_first, ident_src;   /* ident_src: 0 none, 1 card, 2 patched */
    int ident_final, nc_final, ident_err_tries, cache_err_tries;   /* nc_final: the dev net-config file was read (always 1 in a release build) */
    sbr_u32 t_card, t_first;                       /* t_first: the clock of the first card attempt (IDENT SETTLE) */
    /* search */
    int want_search, dns_pending, want_cancel;
    sbr_u32 want_mode;
    char want_code[SBR_CODE_MAX];
    /* peer */
    int p_active, p_peer, p_state, eng_live;   /* eng_live: the engine session was started for the current assignment */
    sbr_u32 p_t;
    /* menu */
    int sel_char_ok, sel_stage_ok, stub_ready;
    sbr_u8 sel_char, sel_color, sel_team, sel_alt;
    sbr_u16 sel_stage;
    sbr_u32 rng, rng_offset, t_both;
    int nhist;
    char hist[SBRA_HISTN][SBR_CODE_MAX];
    char hist_pend[SBRA_HISTP][SBR_CODE_MAX];   /* codes recorded by B4 and not yet written to MultishineCodes (oldest first) */
    int npend, hist_seeded;
};

void sbra_init(struct sbra *A, const struct sbra_cfg *cfg, struct sbra_status *st, const struct sbra_io *io,
               const sddns_io *dns_io, sbr_u32 seed);
void sbra_set_net(struct sbra *A, int up, sbr_u32 net_state, sbr_u32 lan_ip, sbr_u32 dhcp_dns);
/* network side (scene-loop slice, the network is usable) */
void sbra_rx(struct sbra *A, sbr_u32 now, sbr_u32 ip, sbr_u16 port, const sbr_u8 *d, sbr_u32 len);
void sbra_tick(struct sbra *A, sbr_u32 now);
/* EXI side */
const struct sdshim_backend *sbra_backend(struct sbra *A, sbr_u32 (*clock_ms)(void));
/* menu rule for the unlock hook (mn_80229938): 1 = the row does not exist, 0 = no opinion (Slippi's own rule decides).
   logged_in = the app state Slippi's menu holds (r13): in the logged-out form Slippi's own rule shows only its Log-in row and we
   hide nothing (hiding that row too would leave no row at all: the menu code loops forever looking for one, seen in Dolphin). */
int sbra_row_hidden(sbr_u32 kind, sbr_u32 sel, sbr_u32 logged_in);
/* the app state B9 would report now (1 = a usable identity exists); the glue pushes it into Slippi's r13 byte while the
   player is on the menus, because Slippi asks B9 at boot (before the identity was read from the card) */
int sbra_app_state(const struct sbra *A);
/* Short on-screen words for why the player is logged out (ASCII, at most 40 chars), 0 = logged in.  Only the identity can be missing. */
const char *sbra_login_words(const struct sbra *A);
/* 1 once the identity read has a final verdict (found, none, damaged, no card, gave up); the verdict is in sbra_app_state */
int sbra_ident_done(const struct sbra *A);
/* IDENTITY PRECEDENCE.  payload = the 0x12C-byte identity payload (sbr_ident.h) patched into the image, or 0 = none patched.
   baked_first = 1 (LAB/DEV builds, SBRA_BAKED_FIRST default): the patched identity decides, the card's MultishineIdentity is not even read;
   baked_first = 0 (RELEASE builds): the card file decides, the patched identity is only the fallback when the card has none (no file, damaged, no card).
   A Melee save file is never an identity source and nothing here writes either.  Call once, before the first card poll. */
void sbra_set_baked_ident(struct sbra *A, const sbr_u8 *payload, int baked_first);
/* "Identity: patched in the image" / "Identity: memory card" / 0 (none) */
const char *sbra_ident_words(const struct sbra *A);
void sbra_name_for_game(sbr_u8 *dst, const char *name, sbr_u32 cap);   /* display name to Melee's field, full-width Shift-JIS punctuation (host test) */
int sbra_idle(const struct sbra *A);           /* 1 when no search, ticket, matchmaker or opponent connection is open or starting (the report client's network window) */
int sbra_engine_live(const struct sbra *A);        /* 1 while the engine plays the current assignment (SBRA_F_ENGINE_PEER) */
/* card work outside B9 (the glue calls it on the menus, outside the network slice, at most every 2 s): the identity (and in dev builds the net-config
   file) read until the verdict is final.  Never writes the card.  Returns 1 if the card was touched. */
int sbra_card_poll(struct sbra *A, sbr_u32 now);
/* the EXI handlers (exported for the host tests) */
int sbra_b9(struct sbra *A, sbr_u8 *osb);
int sbra_b4(struct sbra *A, sbr_u32 now, sbr_u8 mode, const sbr_u8 *code18);
int sbra_b3(struct sbra *A, sbr_u32 now, sbr_u8 *msrb);
int sbra_b5(struct sbra *A, const sbr_u8 *pstb);
void sbra_ba(struct sbra *A);
/* DIRECT CODE HISTORY ON THE CARD.  B4 (Slippi's rule: the typed code is recorded when the search starts) adds the code to the RAM list and queues it;
   the glue (menus only, interrupts on, no EXI lock) writes the queue to MultishineCodes and, once the card was read, replaces the RAM list by the card's '#' list:
     const char *c = sbra_hist_pending(A);  -> persist c (omc_slippi_sync), then sbra_hist_done(A);   sbra_hist_set(A, codes, n) with the card's list (newest first).
   sbra_hist_set keeps queued (unwritten) codes in front.  This module never touches the card itself. */
const char *sbra_hist_pending(const struct sbra *A);   /* the oldest queued code, 0 = none */
void sbra_hist_done(struct sbra *A);                   /* drop it from the queue */
void sbra_hist_set(struct sbra *A, const char (*codes)[9], int n);
int sbra_be(struct sbra *A, const sbr_u8 *req, sbr_u8 *rsp);
int sbra_b0(struct sbra *A, const sbr_u8 *txb, sbr_u8 *rxb);

#define SBRA_MSRB_SIZE 962
#define SBRA_KIND_ONLINE 8u        /* Slippi's online submenu (rows 0 Ranked, 1 Unranked, 2 Direct, 3 Teams, 4 Party) */

#endif
