/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sbr_mbx: the MAILBOX between the resident bridge (sbr.c, in the engine image) and the Slippi 3.7.0 matchmaker client that runs in ms/rep.ovl
 * (rep_mm.c over sbr_ws.c / ms_ws.c / the overlay's TLS).  Versioned, documented.  Include sbr.h, not this file
 * (sbr.h includes it after struct sbr_match).
 *
 * One struct, resident RAM, owned by the resident (it lives inside struct sbr).  The overlay receives a pointer in struct rep_ovl_env (`mbx`, with its size).
 * Direction is fixed per field group; each side writes only its own group.  Commands are EDGE words: the resident bumps cmd_seq after filling cmd and its
 * arguments; the overlay acts when cmd_seq != ack_seq and then sets ack_seq = cmd_seq (it reads the arguments BEFORE it acks).  Everything is plain 32 bit words
 * and byte arrays: no pointers, no secrets.  THE PLAY KEY IS NOT HERE AND NEVER GOES THROUGH HERE: the overlay copies uid + play key from the card identity
 * (env->ident, R7) into its own stack/bss buffer when a search starts and wipes it when the matchmaker connection ends.
 *
 * Lifetime: the overlay is loaded only while a search is active in a menu scene (see the integration doc); the mailbox outlives it.  `alive` is the overlay's frame
 * counter (the resident watches it to notice a vanished overlay: a search whose overlay is gone and was never acked fails after SBR_MBX_ACK_MS).
 */
#ifndef SBR_MBX_H
#define SBR_MBX_H

#define SBR_MBX_MAGIC 0x53424D58u        /* 'SBMX' */
#define SBR_MBX_VER 1u                   /* bump on ANY layout or meaning change; the overlay refuses a mailbox of another version (rep_ovl.h env.sz_mbx and mbx->ver) */
#define SBR_MBX_HOST_MAX 64
#ifndef SBR_MBX_ACK_MS
#define SBR_MBX_ACK_MS 20000u            /* a command nobody acked for this long (overlay missing, failed to load) ends the search: "Online files missing" (a card read + load is a few seconds) */
#endif

enum { SBR_MC_NONE = 0, SBR_MC_SEARCH = 1, SBR_MC_CANCEL = 2 };

/* STUN state of the overlay's step (informational for the status words) */
enum { SBR_MS_OFF = 0, SBR_MS_RUN = 1, SBR_MS_DONE = 2, SBR_MS_NONE = 3 /* no answer: natType unknown, the search goes on without candidates */ };

struct sbr_mbx {
    sbr_u32 magic, ver, size;            /* constants; size = sizeof(struct sbr_mbx), checked by both sides */
    /* ---- resident -> overlay (the overlay only reads) ---- */
    sbr_u32 cmd_seq, cmd;                /* SBR_MC_* ; cmd_seq changes with every command */
    sbr_u32 mode;                        /* SBR_MODE_UNRANKED / SBR_MODE_DIRECT (validated by the resident before the command) */
    sbr_u32 lan_ip;                      /* our DHCP address, host order, 0 = unknown */
    sbr_u32 local_port;                  /* the netplay layer's UDP port (also the STUN source port) */
    sbr_u32 mm_ip;                       /* 0 = resolve `host`; lab / Dolphin override (cfg mm_ip / netcfg) */
    sbr_u32 mm_port;                     /* TCP port of the matchmaker */
    sbr_u32 dns_ip, dns_port;            /* 0 = the DHCP resolver / port 53 (lab override) */
    sbr_u32 reserved1[2];
    char code[SBR_CODE_MAX];             /* Direct connect code, ASCII, "" for Unranked (the Shift-JIS form is made by the overlay) */
    char app_version[SBR_VER_LEN];       /* the join's appVersion */
    char host[SBR_MBX_HOST_MAX];         /* the matchmaker's name (cfg mm_name; the only place a Slippi host name exists) */
    /* ---- overlay -> resident (the resident only reads) ---- */
    sbr_u32 ack_seq;                     /* = cmd_seq once the overlay has taken the command */
    sbr_u32 alive;                       /* frame counter of the overlay (0 = never ran) */
    sbr_u32 state;                       /* SBR_IDLE / INIT (connecting, joining, resuming before `joined`) / MM (queued) / OPP_CONN (match valid) / ERROR */
    sbr_u32 err;                         /* SBR_E_* when state == SBR_ERROR */
    sbr_u32 queue_s;                     /* secondsInQueue of the last `status` */
    sbr_u32 stun_state, nat;             /* SBR_MS_*, SBR_NAT_* (sbr_stun.h) */
    sbr_u32 n_searches, n_joins, n_resumes, n_drops, n_assigned, n_errors, n_pings, n_rx_msgs, n_rx_bad;
    sbr_u32 sec_err;                     /* last TLS / TCP error bits of the overlay's session (counters, never a secret) */
    sbr_u32 match_seq;                   /* bumped when `match` holds a new assignment; the resident copies it exactly once per change */
    char err_text[SBR_ERR_MAX];          /* plain text, no R3 suffix (the resident adds it for fault codes via sbr_fail) */
    char latest[SBR_VER_LEN];            /* latestVersion of an outdated error, "" otherwise */
    struct sbr_match match;              /* valid when match_seq changed */
};

#endif
