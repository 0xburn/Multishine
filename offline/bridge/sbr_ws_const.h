/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_ws_const.h: EVERY constant of the Slippi 3.7.0 WebSocket matchmaker client in one place.
 * Values follow the Slippi 3.7.0 client; change them only here.  No host name lives
 * here on purpose (the caller resolves and connects).
 */
#ifndef SBR_WS_CONST_H
#define SBR_WS_CONST_H

/* ---- timers, milliseconds (protocol doc section 1) ---- */
#define SBRW_CONNECT_MS 5000u        /* TCP connect, per attempt */
#define SBRW_HANDSHAKE_MS 10000u     /* TLS + WebSocket upgrade, per attempt */
#define SBRW_JOINED_MS 5000u         /* `joined` due this long after the socket opened, on EVERY connection (a resume is acknowledged by `joined` only) */
#define SBRW_PING_IDLE_MS 5000u      /* no frame of any kind for this long: ONE app-level {"type":"ping"}; any received frame re-arms it */
#define SBRW_LOST_MS 15000u          /* silence = connection lost */
#define SBRW_RESUME_MS 15000u        /* window from the FIRST loss; cleared only by a `joined` */
#define SBRW_RECONNECT_MS 1000u      /* pause between attempts after a loss or a failed connect */
#define SBRW_CONNECT_TRIES 3u        /* connect attempts before the FIRST join fails */
#define SBRW_RETICKET_MS 1500u       /* pause before a fresh search after a failed peer connection (same as the ENet client) */
#define SBRW_RX_BURST 16u            /* messages handled per tick */

/* ---- wire strings ---- */
#define SBRW_PATH "/v1/queue"        /* GET path of the upgrade (host and port 443 belong to the transport config) */
#define SBRW_APP_VERSION "3.7.0"     /* default appVersion string */
#define SBRW_T_JOIN "join"
#define SBRW_T_RESUME "resume"
#define SBRW_T_CANCEL "cancel"
#define SBRW_T_PING "ping"
#define SBRW_T_JOINED "joined"
#define SBRW_T_STATUS "status"
#define SBRW_T_MATCH "match"
#define SBRW_T_ERROR "error"
#define SBRW_CAND_HOST "host"
#define SBRW_CAND_SRFLX "srflx"
#define SBRW_CAND_OBSERVED "observed"
#define SBRW_NAT_CONE "cone"
#define SBRW_NAT_SYMMETRIC "symmetric"
#define SBRW_NAT_UNKNOWN "unknown"

/* ---- sizes ---- */
#define SBRW_TICKET_MAX 64           /* ticketId */
#define SBRW_TOKEN_MAX 256           /* token (longer = resume impossible, the session then fails on a loss) */
#define SBRW_RX_MIN 4096u            /* receive buffer the caller supplies: a match is about 1.6 KB */
#define SBRW_TX_MIN 1024u            /* transmit buffer the caller supplies: join with a full identity */
#define SBRW_CANDS_MAX 4

/* ---- text shown to the player (the existing bridge notice path adds the R3 suffix for fault codes) ---- */
#define SBRW_TXT_CONNECT "Couldn't reach the matchmaking server."          /* stock: Failed to connect to mm server */
#define SBRW_TXT_JOIN "Couldn't join the matchmaking queue."                /* stock: Failed to join mm queue */
#define SBRW_TXT_LOST "Lost the matchmaking server. Try again."             /* stock: Lost connection to the mm server */
#define SBRW_TXT_SERVER "Matchmaking sent an error."                        /* stock: Received an error from the mm server */
#define SBRW_TXT_OUTDATED "Slippi has moved on. Update Multishine"

#endif
