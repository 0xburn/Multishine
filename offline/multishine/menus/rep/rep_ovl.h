/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Report overlay "ms/rep.ovl": the Slippi game-report CLIENT (rep_cli / rep_body / rep_json / rep_dns / rep_glue), the TCP + TLS 1.3 stack
 * (sdtcp, sdtls with SDTLS_WEBPKI, sdsec, sdhttp, sdpk, sdx509, sdroots, sdcrypto, sdc25519, sdent, sddns) and a few lines of glue, about 100 KB, as a flat-linked overlay in the
 * file format of ms/sign.ovl (sg_ovl.h: struct sgo_hdr, relocation table, no import relocations; hdr.state_hint = REP_OVL_KIND).
 *
 * The resident image (menus/rep/rep_res.c) loads it into ONE block of the HSD heap only while ALL of these hold: our server announced capability bit 15 to this console (the only
 * gate: no setting), a report is waiting in the resident queue (`struct rep_keep`), a trusted time is known (the signed HELLO), and the scene is a main menu, or Slippi's
 * online character screen while idle (after a game, before the next search), with no search, bridge or engine session (br_menu_scene_ok, rep_scene.h).  It gives the block back as soon as the queue is empty and the session closed, and ALWAYS before a scene change
 * (om_heap_policy -> hub_scene_change -> rep_scene_change), so nothing of it ever exists in a match; on the character screen it exists only in the idle window and is closed the moment a search starts (rep_search_hook).  Everything that must survive (queue,
 * counters, HELLO clock) is the resident `struct rep_keep`; the overlay owns only the session.  It never allocates (its session block is its own bss) and never waits.
 *
 * ABI: REP_OVL_ABI.  The resident and the block refuse each other on a different ABI or a different layout of the shared structs (the env carries the sizes).
 */
#ifndef REP_OVL_H
#define REP_OVL_H
#include "rep_keep.h"
#include "sbr.h"                         /* struct sbr_mbx (sbr_mbx.h, the matchmaker mailbox) and struct sbr_ident */

#define REP_OVL_PATH "/ms/rep.ovl"
#define REP_OVL_KIND 6u                /* sgo_hdr.state_hint (0 sign-in, 1 menu, 3 hub, 5 gns) */
#define REP_OVL_ABI 2u                 /* 2: + the Slippi 3.7.0 matchmaker: mailbox, full identity, UDP on the netplay socket, entropy gate; busy carries two bits */

struct sdnet;
struct sddns;
struct rep_pad { unsigned short buttons; signed char sx, sy, cx, cy; unsigned char tl, tr, err; };       /* the layout of struct sdent_pad (sdent.h) */

/* resident -> block.  The block reaches the resident image only through this table (no import relocations). */
struct rep_ovl_env {
    rep_u32 abi, sz_sdnet, sz_keep, sz_rep_ovl_env, sz_mbx;     /* sz_mbx = sizeof(struct sbr_mbx), checked together with mbx->ver (SBR_MBX_VER) */
    struct sdnet *net;                                  /* the engine's sdnet, built with SDNET_ENABLE_TCP=1 (the block attaches its TCP context to it and detaches it on leave) */
    struct rep_keep *keep;                              /* the resident queue / counters / clock */
    struct sddns *dns;                                  /* the bridge's resolver (br_dns), shared with the hub: one lookup at a time */
    rep_u32 (*dns_server)(void);                        /* the DHCP-provided resolver */
    rep_u32 (*now_ms)(void);                            /* the engine's one millisecond clock */
    rep_u32 (*tb)(void);                                /* time base (low 32 bits), mixed into the entropy pool */
    int (*scene_ok)(void);                              /* br_menu_scene_ok */
    int (*ident)(char *uid, rep_u32 uidcap, char *key, rep_u32 keycap);   /* the Slippi uid and play key from the card identity (R7) */
    int (*iso_hash)(char *out33);                       /* the card-file MD5 of the image the card was built for, or 0 */
    int (*seed)(unsigned char rec[44], int write);      /* MultishineOptions' stored entropy seed (ons_seed): 1 done, 0 none, -1 not read yet */
    void (*pad)(struct rep_pad *p);                    /* the current pad (all four ports), feeds the entropy pool */
    int (*net_begin)(void);                             /* interrupts off + EXI lock for a stretch of TCP work (1 = entered, 0 = busy: skip this frame) */
    void (*net_end)(void);
    void (*net_poll)(struct sdnet *s);                  /* one sdnet_poll + drain from the menu thread (EXI lock handled by the resident) */
    rep_u32 (*csum_add)(rep_u32 sum, const rep_u8 *p, rep_u32 len);      /* sdnet.c's checksum and IP transmit: the TCP code of the block calls them through these */
    unsigned short (*csum_fold)(rep_u32 sum);
    int (*tx_ip)(struct sdnet *s, rep_u8 proto, rep_u32 dst_ip, unsigned short plen);
    rep_u32 build;                                      /* the release number (sbr_build_seq) */
    const char *platform;                               /* "console" (the build cannot tell GameCube, Wii, Wii U, Dolphin apart) */
    /* ---- ABI 2: the Slippi 3.7.0 matchmaker  ---- */
    struct sbr_mbx *mbx;                                /* the resident's mailbox (commands in, state / error / match out); outlives the block */
    int (*ident_full)(struct sbr_ident *out);           /* uid, play key, connect code, display name of the card identity, copied for ONE search (R7: wiped by the block when the connection ends); 1 = ok */
    int (*udp_send)(rep_u32 ip, unsigned short port, const rep_u8 *b, rep_u32 n);          /* one datagram from the NETPLAY socket (STUN must leave from the port the engine's ENet host will use); only inside the net_begin window */
    int (*udp_recv)(rep_u8 *b, rep_u32 cap, rep_u32 *ip, unsigned short *port);             /* n >= 0 one datagram from that socket, < 0 none; only inside the window */
};

/* the block's entry table, at hdr.api_off (padded to at least sizeof(struct sgo_api): the shared loader checks the table lies inside the image) */
struct rep;
struct rep_ovl_api {
    rep_u32 abi;
    int (*enter)(const struct rep_ovl_env *e);          /* 0 = ready */
    void (*frame)(void);                                /* once per menu frame while loaded: entropy pool, net poll, rep_poll */
    void (*leave)(void);                                /* abort the session (RST), wipe the request and key material, detach the TCP hooks: the caller frees the block after this */
    int (*busy)(void);                                  /* bit 0: a report session or report is in flight; bit 1: a matchmaker search or its transport is still live (the resident keeps the block); 0 = nothing to do, may be freed */
    const struct rep *(*state)(void);                   /* read only (proof words); struct rep is declared in rep.h (the overlay side only) */
    rep_u32 pad_[6];
};

/* resident RAM proof words (symbol rep_st): never a secret */
enum {
    RP_MAGIC,      /* 'REPS' */
    RP_STATE,      /* 0 absent, 1 loading, 2 loaded (running), 3 failed this scene, 4 waiting (gate closed: why in RP_WHY) */
    RP_LOADS, RP_FREES, RP_FAILS, RP_ERR,
    RP_BLOCK,      /* bytes of the HSD block while loaded */
    RP_WHY,        /* why nothing is loaded: bit 0 no queue, 1 server bit off, 2 no HELLO time, 3 scene not ok, 4 card/pack busy, 5 given up, 7 no heap room */
    RP_TICKS,      /* time base ticks of the last load */
    RP_ENQ, RP_OFF, RP_MODE, RP_BAD, RP_FULL,                    /* resident enqueue counters (struct rep_stats, copied while no overlay is loaded) */
    RP_ATTEMPTS, RP_OK, RP_SUCCESS_FALSE, RP_GQL_ERR, RP_HTTP_BAD, RP_PARSE_BAD, RP_RSP_TIMEOUT,
    RP_TCP_FAIL, RP_TLS_FAIL, RP_DNS_FAIL, RP_NO_IDENT, RP_NO_MEM, RP_SESSIONS,
    RP_DROP_MAX, RP_DROP_AGE, RP_DROP_CERT, RP_DROP_OFF,
    RP_HELD_SCENE, RP_HELD_TIME, RP_HELD_COOLDOWN, RP_HELD_ENT,
    RP_LAST_ERRS,  /* last_tls_err | last_cert_err << 8 | last_sec_err << 16 | last_tcp_err << 24 */
    RP_PENDING,
    RP_SCENES,     /* scene changes seen (rep_scene_change) */
    RP_LEAK,       /* blocks found no longer on the heap list at a scene change: forgotten, never called, never freed */
    RP_NOHEAP,     /* scene visits where the load was refused for lack of HSD heap room (block + reserve, rep_scene.h) */
    RP_CSS_LOADS,  /* loads made on Slippi's online character screen (the idle window after a game) */
    RP_LAST_HTTP,  /* status code of the last complete HTTP answer (0 none) */
    RP_ABORTS,     /* sessions closed because a search or lock-in started (rep_search_hook) */
    RP_MM_LOADS,   /* loads made for a matchmaker search */
    RP_HSD_FREE,   /* free bytes of the HSD heap just BEFORE the last load (the margin the doc asks to measure), and */
    RP_HSD_MIN,    /* the smallest value seen */
    RP_MM_STATE,   /* the mailbox's state word as the overlay last wrote it (SBR_IDLE..SBR_ERROR) */
    RP_MM_ALIVE,   /* overlay frame counter (mailbox alive) */
    RP_MM_ERR,     /* mailbox err */
    RP_MM_STUN,    /* stun_state | nat << 8 */
    RP_MM_JOINS,   /* joins | resumes << 8 | drops << 16 */
    RP_MM_SEC,     /* last TLS / TCP / WebSocket error bits of the matchmaker session */
    RP_N
};
enum { RE_NONE = 0, RE_NOPACK, RE_NOFILE, RE_SIZE, RE_NOMEM, RE_READ, RE_FORMAT, RE_ABI, RE_ENTER, RE_BUSY };

#endif
