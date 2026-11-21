/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REP_GLUE_H
#define REP_GLUE_H
/* rep_glue: the overlay-side wiring of the Slippi report client.  In the product it runs inside ms/rep.ovl (rep_ovl.c), which the resident
   image (rep_res.c) loads only while a report is waiting in a menu scene; the queue, counters and HELLO clock are the RESIDENT `struct rep_keep` (host->keep).  The host tests
   link it as one module without -DREP_OVL: keep == NULL gives a private keep, and rep_game_hook / rep_menu_hook are the weak targets of offline/bridge/br_glue.c. */
#include "rep.h"

struct rep_glue_host {
    struct sdnet *net;                                  /* the engine's sdnet (struct built with SDNET_ENABLE_TCP=1), or NULL when rep_glue_tcp_up is not used */
    struct sdtcp *tcp;                                  /* a TCP context already attached to `net` (NULL: the caller creates one with rep_glue_tcp_up and passes it) */
    struct sddns *dns;                                  /* the shared resolver (the bridge's, br_dns()) */
    rep_u32 (*dns_server)(void);                        /* the DHCP-provided resolver address, set on the resolver before each lookup; NULL = leave it */
    rep_u32 (*now_ms)(void);                            /* the sdnet clock */
    int (*rand)(void *ctx, rep_u8 *out, rep_u32 n);     /* entropy (sdent session level); nonzero = not ready yet */
    void *rand_ctx;
    int (*entropy_ready)(void);                         /* 1 when `rand` can serve a session now; NULL = always (host tests) */
    int (*scene_ok)(void);                              /* br_menu_scene_ok */
    int (*hello)(rep_u32 *caps, rep_u32 *srv_time);     /* host tests only (keep == NULL): 1 once a HELLO was answered: capability word and Unix time.  The resident does this itself in the product */
    int (*ident)(char *uid, rep_u32 uidcap, char *key, rep_u32 keycap);   /* the Slippi uid and play key from the card identity (R7: copied for one request only) */
    int (*iso_hash)(char *out33);                       /* the card-file MD5 of the image the card was built for; NULL or 0 = empty isoHash */
    void *(*alloc)(rep_u32 size);                       /* the ~30 KB session block (the overlay hands out its own bss) */
    void (*release)(void *p);
    const struct sdx_anchor *anchors; rep_u32 nanchors; /* sdx_roots / sdx_nroots */
    rep_u32 build;                                      /* the release number (the same one `sbr_build_seq` carries into the ticket's client field) */
    const char *platform;                               /* "console" (the build cannot tell GameCube, Wii, Wii U, Dolphin apart) */
    struct rep_keep *keep;                              /* the resident queue / counters / clock; NULL = private (host tests) */
};

void rep_glue_init(const struct rep_glue_host *h);
/* Optional: bring up a TCP context (one connection, 4 KB receive ring, 2 KB send ring, window cap 2920 for the BBA's 3,840-byte receive ring) on `net`. */
struct sdtcp *rep_glue_tcp_up(struct sdnet *net, const rep_u8 seed[16]);
/* host-test targets of br_glue.c (not in the overlay build, -DREP_OVL) */
void rep_game_hook(const rep_u8 *rgb, const char *match_id, const char *opp_uid, rep_u32 local_idx);
void rep_menu_hook(void);
void rep_glue_frame(int allow_heavy);                   /* the per-frame body with the heavy-step permission as a parameter */
void rep_glue_match_start(void);                        /* call BEFORE the TCP mode goes to QUIESCE / PAUSED for a match: tears the session down, keeps the queue */
void rep_glue_set_tcp(struct sdtcp *t);                 /* the overlay creates its TCP context once the pool can seed it */
void rep_glue_leave(void);                              /* the overlay is about to be freed: tear the session down, forget the glue */
struct rep *rep_glue_state(void);                       /* counters for the status screen / log (R.k->st) */
#endif
