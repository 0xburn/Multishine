/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Game Night match-time block "ms/gns.ovl" (2.1 and 10 "S1c"): the spectate UPLOADER.
 *
 * What it is: a small overlay (same file format and packer rules as ms/hub.ovl: sg_ovl.h struct sgo_hdr, relocation table, no import relocations) that is loaded into ONE
 * block of AllM (lbHeap heap 4, persistent for the whole online major scene) by the resident loader (gns_res.c) at the character screen of a Game Night set, and given back
 * when the major scene ends.  The menu-time overlays (hub.ovl, sign.ovl) live in the HSD heap, which is rebuilt at every scene change, so they cannot run inside the match;
 * this block can.  It carries gnrec.c (the packer / scheduler of offline/client_net), mw2_seal / mw2_open and the glue below, about 4 KB, and nothing else.
 *
 * Why it is small and safe (rules): Game Night features are overlay-only; the resident part is the loader and one hook call in be_inputs (gns_b0, guarded by one
 * word); the block is never loaded outside a Game Night set of a spectatable room; every failure leaves the match exactly as it is (no card file, no AllM room, a refused
 * upload: the uploader is simply off).  The block never waits, never receives (the resident hands it the server's replies), sends at most one datagram per 12 frames.
 *
 * ABI: GNS_ABI.  The resident and the block refuse each other on a different ABI or a different layout of the shared structs (struct gns_env carries the sizes).  This is
 * NOT HUB_ABI: nothing of struct hub_env / struct mnc changed (the hand-off facts ride in the existing mnc gn_room tail, see GNS_ROOM_*), so every card's hub.ovl and the
 * engine keep working with each other; HUB_ABI 2 (P17, the server's match-setup block) stays a separate, later step.
 */
#ifndef GNS_OVL_H
#define GNS_OVL_H
#include "mw2.h"

#define GNS_PATH "ms/gns.ovl"
#define GNS_KIND 5u                    /* sgo_hdr.state_hint of this block (0 sign-in, 1 menu, 3 hub) */
#ifdef MS_MATCH_MSG                    /*  (prototype, default off): ABI 9 = + w_msg / mmproof at the end of struct gns_api; a build without the switch is byte for byte the old one */
#define GNS_ABI 11u
#else
#define GNS_ABI 10u
#endif
/* (history) 2: w_begin_live (live-ish watching) and the on-demand uploader (history buffer in the block's bss); 4: w_toast (the hint toast moved out of the engine); 5: w_begin_friend (watch a friend outside Game Night);
   7 (6 was the message prototype): ad_prep / ad_off + gns_env.ad_buf (Friends "Add a friend": the tab art, the text rewrite and its backups moved out of the resident engine); 10 (11 with the message prototype): gns_env.zd_want (the room type's Zelda flag, a resident byte: the block applies the patch for a Low-Mid Fun room by itself, PC players and consoles alike); 8 (9 with the message prototype): gns_env.zd_keep */
#define GNS_AD_EXTRA 5760u              /* bytes after the block's bss / relocation area for the Add-a-friend backups (2 x 178 text + 2 x 2688 tab = 5732, rounded up to 32): the resident allocates them only for that hand-off (gns_wanted 3) */
#define GNS_BUF_BYTES 475u              /* sizeof(((gnrec *)0)->buf) = MW2_HDR + GNREC_PAYLOAD_MAX + MW2_MAC (checked at compile time in gns_ovl.c and at enter) */

/* ---- the hand-off facts the lobby (sign.ovl) leaves in mnc.gn_room next to the call-up name (struct mnc is unchanged) ----
   gn_room[0..] = "GN<code>S<set>" NUL terminated (at most 17 bytes incl. the NUL); gn_room[19] = GNS_ROOM_FLAGS, written by gn_handoff in sg_entry.c:
   (the sign overlay also keeps bit 2 = auto-watch on and bits 3-5 = automatic watches in a row in the WATCH hand-off's flags, sg_entry.c GN_RF_AUTO; the engine reads none of them)
   bit 0 the room is spectatable with the Frozen Stadium policy (view rflags 2 and the server's policy), i.e. the server accepts GN_REC for it.  Cleared with the name. */
#define GNS_ROOM_FLAGS 19u
/* : a WATCH hand-off for a friend's match outside Game Night: gn_room = "GNF" + the friend's connect code (at most 8 characters) NUL terminated, flags = CAP_WATCH | SPEC (no LIVE bit: the server decides). */
#define GNS_ROOM_PICK 17u              /* the room pick the character screen takes over (no second pick): character id + 1 (1..27, 27 = Random), 0 = none (the player picks on the character screen) */
#define GNS_ROOM_COSTUME 18u
#define GNS_ROOM_COVER 16u             /* 1 while the character screen is held black by the hub overlay; the RESIDENT engine lifts it (gns_cover_tick) */
#define GNS_ROOM_CAP 15u               /* = GNS_CAP_MAGIC when this resident engine lifts the cover: the overlay never blacks the screen otherwise */
#define GNS_CAP_MAGIC 0xC5u           /* ... and its costume 0..15 */
#define GNS_RF_SPEC 1u
#define GNS_RF_FROZEN 2u               /* the room's Stadium policy is Frozen: the engine runs every Stadium game of the room as the frozen variant (rules alt_mode ON, s12_gc.c od_rules) and the uploader records altStageMode 1 */
#define GNS_RF_CHARS 0x04u             /* the PLAY hand-off only (the watch hand-off keeps its auto-watch bits there): the server serves room types, so the match report carries the two bytes "characters as played" ( item 2); an older server refuses a longer report */
#define GNS_RF_ZELDA 0x08u             /* the PLAY hand-off only (the watch hand-off uses bits 3-5 for its auto-watch chain): the room type is Low-Mid Fun, whose rules carry the Zelda no-transform patch: the resident sets gns_zwant from it and loads the block for the patch even when the room is not spectatable or the upload switch is off */
#define GNS_RF_ZERR 0x10u              /* the PLAY hand-off, set by the resident: this console's guard refused the room type's code patch; the hub's TICKET_POLL tells the server, the lobby says so when the set is spent */
#define GNS_RF_ZON 0x20u               /* ... set by the resident: the patch is on (mnc.c reads 0x10 / 0x20 of gn_room[19] for the poll's patch byte;) */
#define GNS_RF_ITEMS 0x40u             /* the PLAY hand-off of a Wild And Crazy room: the engine builds the match with every item on at Very High (s12_gc.c od_rules, literal 0x40, the LIVE bit of the WATCH hand-off, free in the play hand-off; only the play hand-off reaches od_rules). Both consoles get it from the same lobby view */
#define GNS_RF_LIVE 0x40u              /* MNC_GN_WATCH: the lobby saw a set RUNNING when X was pressed: watch it live (GN_SPEC A_LIVE) instead of the room's last game */
#define GNS_RF_CAP_WATCH 0x80u         /* set by the RESIDENT engine whenever the hand-off block is idle: this engine can run MNC_GN_WATCH (an older engine leaves it 0: the lobby then does not offer "X: Last game" as a start) */

/* what the resident build_view hands the block every hooked frame (plain C types: s12_gc.c fills it from struct s3_core; layout == struct gnrec_rings + 3 words) */
struct gns_view {
    unsigned int mask;                 /* S3_RING - 1 */
    const int *ltag; const unsigned char *lpad; int lnewest;      /* own pads */
    const int *rtag; const unsigned char *rpad; int rlatest;      /* opponent pads as received */
    const int *hframe; const unsigned int *hcks; unsigned int hmask;   /* finalized frame -> Slippi checksum history */
    int fin;                           /* newest finalized frame */
    int frame;                         /* the frame of this B0 call */
    int in_rb;                         /* 1: a rollback replay is running */
    const unsigned char *lsel;         /* both players' MATCH_SELECTIONS, side 0 then side 1 (2 x 14 bytes), or 0 */
};

/* resident -> block.  The block reaches the resident image only through this table (no import relocations). */
struct gns_env {
    unsigned int abi, sz_view, sz_mw2, sz_gnrec_buf;
    mw2 *w;                            /* the hub client's wire session (od_o.m.w): the uploader seals with it */
    int (*send)(void *ctx, const unsigned char *p, unsigned int len);    /* od_send: one datagram to the server from the game socket (valid inside the engine's lock) */
    void *ctx;
    void (*hmac)(const mw2_u8 key[32], const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[32]);
    void (*sha512)(const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[64]);
    int (*ed_verify)(const mw2_u8 pub[32], const mw2_u8 *msg, mw2_u32 n, const mw2_u8 sig[64]);
    int (*ct_equal)(const mw2_u8 *a, const mw2_u8 *b, mw2_u32 n);
    unsigned char *ad_buf;             /* GNS_AD_EXTRA zeroed bytes behind the block (the Add-a-friend load only), else 0: ad_prep then reports "unavailable" */
    const volatile unsigned char *zd_want;  /* a resident byte (gns_res.c gns_zwant): 1 while the Game Night set being searched / played is of a room type whose rules carry the Zelda no-transform patch (Low-Mid Fun; the lobby's hand-off flag GNS_RF_ZELDA, PLAY only); 0 / null otherwise. The block applies the patch from it at enter and at every arm */
    volatile unsigned int *zd_keep;    /* two resident words (gns_st GS_ZD0 / GS_ZD1): the original first words of the two Zelda transform entries while the no-transform patch is on, 0 = not patched */
};

/* proof words the block keeps (the resident copies them into gns_st every pass) */
struct gns_proof {
    unsigned int state;                /* 0 idle, 1 armed (uploading), 2 dead (see why) */
    unsigned int why;                  /* gnrec st_dead_why */
    unsigned int sent, rollback, notready, replies;
    unsigned int refused;              /* last non-zero reply status */
    unsigned int merged;               /* tags the server merged (reply body), newest reply */
    unsigned int set_id, game;
    unsigned int zd;                   /* the code patch of a patched room type (Low-Mid Fun): 0 not wanted, 1 on, 2 refused by the guard (the resident then leaves the set:) */
    unsigned int bad_rx;               /* replies that failed the MAC */
};

/* proof words of the watcher (copied into gns_st by the resident) */
struct gns_wproof {
    unsigned int state, why, have, frames, feed, range, stalls, fed, cmp, cmp_bad, first_bad, leave, bad, sends, send_rc, rx_n, rx_op;
};

#ifdef MS_MATCH_MSG
/* proof / cost words of the message poll and plate (the resident copies them into gns_st GS_MM_*) */
struct gns_mmproof {
    unsigned int polls, rx, rx_empty, shown, bad, lost, nosess, txb, rxb;     /* requests sent / replies taken / of them empty / plates shown / replies refused / requests without a reply / session not ready / bytes sent / received (datagram payloads) */
    unsigned int state;                /* bit 0 request in flight, bit 1 plate visible, bit 2 text made this scene, bit 3 on */
    unsigned int pn, psum, pmax;       /* time base ticks of the poll path (w_msg without the plate) when it did work: count, sum, max (MM_PROBE builds; else 0) */
    unsigned int dn, dsum, dmax;       /* the plate's render callback (tp_draw), once a frame while visible (MM_PROBE builds) */
    unsigned int cn, csum, cmax;       /* the whole w_msg call, every pass (MM_PROBE builds) */
    unsigned int per_p50, per_p99, per_max, per_n;   /* the gap between two passes in 1/8 ms (MM_PROBE) */
    unsigned int last_id;
};
#endif

/* the block's entry table, at hdr.api_off */
struct gns_api {
    unsigned int abi;
    int (*enter)(const struct gns_env *e);                         /* 0 = ready */
    void (*leave)(void);
    /* a set's game starts: set_id from the call-up name, game = the game counter, sel = the local MATCH_SELECTIONS (14 bytes) or 0, frame of this call */
    int (*arm)(unsigned int set_id, unsigned int game, const unsigned char *sel, unsigned int sel_len, int frame);
    int (*step)(const struct gns_view *v, unsigned int now_ms);    /* one B0 call after the engine updated its rings: 1 a datagram went out */
    int (*rx)(const unsigned char *d, unsigned int n, unsigned int now_ms);   /* a server datagram with op reply 0xBF (GN_REC): 1 consumed */
    void (*kill)(unsigned int why);
    const struct gns_proof *proof;
    int (*close)(unsigned int now_ms);                             /* the closing datagram at the game's end (BD) */
    /* the WATCHER ("Watch Last Game", client_net/gnw.c): begin = GN_SPEC for the room's kept last game; tick = resends / HEAD / RANGE (menu pass or game thread); css = the character
       select answer while it is not ready (1 = msrb filled: state 2 searching or 5 + text); b0 = the receive-block rewrite after the real s3_core_b0 (returns the result byte);
       b3 = the match-block markers / demo words after the real B3 (0 ok); sels = the two players' MATCH_SELECTIONS of a real game (1 = valid; pointers into the block) so the
       resident can let the engine build the block itself; names = the two players' names. */
    int (*w_begin)(unsigned int now_ms);
    int (*w_tick)(unsigned int now_ms);
    int (*w_css)(unsigned char *msrb);
    int (*w_b0)(const unsigned char *txb, unsigned char *rxb);
    int (*w_b3)(unsigned char *msrb, int markers);
    int (*w_sels)(const unsigned char **s0, const unsigned char **s1);
    void (*w_names)(const char **n0, const char **n1);
    const struct gns_wproof *wproof;
    int (*w_called)(void);                                         /* the last RANGE reply said the watcher is called up (the toast) */
    int (*w_decline)(unsigned int now_ms);                         /* GN_SPEC A_DECLINE: the server's missed-call-up rule; 0 = sent */
    void (*w_leave)(void);                                         /* the next B0 ends the match (hold B leaves, hold Z accepts: both land in the room lobby) */
    int (*w_zstep)(int z_down, unsigned int now_ms);               /* the Z gestures (gnzk.c): 0 none, 1 accept (hold 1.0 s), 2 decline (double tap) */
    int (*w_begin_live)(unsigned int now_ms);                      /* begin = GN_SPEC A_LIVE: the room's game that is still being played */
    /* the in-match hint toast: line = a string literal or 0 (none), create = the engine's HUD text maker (or 0), now_ms, scene = the scene word (a new scene forgets the text),
       sticky = 1 for a prompt (stays), 0 for a hint (shown for a few seconds after a button press) */
    void (*w_toast)(const char *line, void *(*create)(const char *), unsigned int now_ms, unsigned int scene, unsigned int sticky);
    int (*w_begin_friend)(unsigned int now_ms, const char *code);   /* begin = GN_SPEC A_FRIEND: the live match of the friend with this connect code; 0 = sent */
    /* FRIENDS ADD: the help text and the top-left tab of Slippi's code keyboard, rewritten in the loaded game files for that one hand-off.  ad_prep: one engine pass of the work, called
       every pass until it says 1: 0 = still looking for the two places (half a megabyte of MEM1 a pass), 1 = looked and patched (or nothing found), -1 = unavailable (no ad_buf).  ad_off(reset): puts the original bytes
       back (reset 1: also forgets the scan, for the end of the hand-off). */
    int (*ad_prep)(void);
    void (*ad_off)(int reset);
#ifdef MS_MATCH_MSG
    /* friend messages in the match: one call per menu-thread pass.  on = 1 while a match of an unranked kind runs (the resident decides: Game Night / Unranked / Direct, never Grandmaster,
       never a watcher); 0 hides the plate and stops the poll.  create = the engine's HUD text maker (bh_toast_make), used once a scene; the text is re-printed for every message. */
    void (*w_msg)(unsigned int now_ms, void *(*create)(const char *), unsigned int scene, unsigned int on);
    const struct gns_mmproof *mmproof;
#endif
};

/* resident RAM proof words (symbol gns_st, gns_status_addr): never a secret */
enum {
    GS_MAGIC,      /* 'GNST' */
    GS_STATE,      /* 0 absent, 1 loaded (idle), 2 armed (uploading), 3 failed this scene, 4 waiting (card / AllM) */
    GS_LOADS, GS_FREES, GS_FAILS, GS_ERR,
    GS_BLOCK,      /* bytes of the AllM block while loaded */
    GS_ADDR,       /* load address */
    GS_TICKS,      /* time base ticks of the last load */
    GS_ALLM_TOT, GS_ALLM_BIG,     /* AllM free bytes / largest gap at the load decision */
    GS_ARMS,       /* matches armed */
    GS_SET,        /* set id of the last arm */
    GS_SENT, GS_ROLLBACK, GS_NOTREADY, GS_REPLIES, GS_REFUSED, GS_MERGED, GS_WHY, GS_BADRX,
    GS_NOWANT,     /* load/arm decisions skipped: not a Game Night set / room not spectatable / switch off */
    GS_SCENE,      /* scene word at the last load */
    GS_FRAME,      /* frame of the newest hooked call */
    GS_WAITS,      /* ticks a load waited for the card pack / card layer / AllM */
    GS_W_STATE, GS_W_WHY, GS_W_HAVE, GS_W_FRAMES, GS_W_FEED, GS_W_RANGE, GS_W_STALLS, GS_W_FED, GS_W_CMP, GS_W_BAD, GS_W_FIRSTBAD, GS_W_LEAVE, GS_W_BADRX, GS_W_BEGINS, GS_W_MODE, GS_W_SENDS, GS_W_SENDRC, GS_W_RXN, GS_W_RXOP, GS_W_B0N, GS_W_B0F, GS_W_B3N, GS_W_HOT, GS_W_TK, GS_W_TKN, GS_W_TKSC, GS_AMIN, GS_ABIG, GS_ALO, GS_ASC, GS_W_BLK, GS_W_BLKSC, GS_W_BLK2, GS_CLOSED, GS_AUTOLEAVE, GS_W_LEAVEKEY, GS_W_ACCEPT, GS_W_DECLINE,
    GS_H1_START, GS_H1_SIZE, GS_H1_TOT, GS_H1_BIG, GS_H1_SC, GS_W_P0, GS_W_P1, GS_W_P2, GS_W_P3, GS_FL_CSS, GS_FL_LOAD, GS_FL_SPAWN,   /* watch session: lbMemory heap 1 (fighter files, ftData_80085A14 -> 800168a0 -> 80015BD0 idx 1) start / size / free bytes / largest gap, last sample + scene */
    GS_LOST,       /* blocks found overwritten / unlisted at a scene change and forgotten (AllM reuse; never freed, never called) */
    GS_PICK,                           /* the room pick written on the character screen: character << 8 | costume | frames << 16 (gns_pick_tick) */
#ifdef MS_MATCH_MSG
    GS_MM_POLLS, GS_MM_RX, GS_MM_EMPTY, GS_MM_SHOWN, GS_MM_BAD, GS_MM_LOST, GS_MM_NOSESS, GS_MM_TXB, GS_MM_RXB, GS_MM_STATE, GS_MM_PN, GS_MM_PSUM, GS_MM_PMAX, GS_MM_DN, GS_MM_DSUM, GS_MM_DMAX,
    GS_MM_CN, GS_MM_CSUM, GS_MM_CMAX, GS_MM_P50, GS_MM_P99, GS_MM_PERMAX, GS_MM_PERN, GS_MM_ON,
#endif
    GS_ZD0, GS_ZD1,                    /* the originals of ftZd_SpecialLw_Enter / ftZd_SpecialAirLw_Enter while the Zelda no-transform patch is on (written by the block, put back by the block or by gns_drop) */
    GS_N
};
enum { GE_NONE = 0, GE_NOPACK, GE_NOFILE, GE_SIZE, GE_NOMEM, GE_READ, GE_FORMAT, GE_ABI, GE_ENTER, GE_BUSY, GE_NOALLM };

#endif
