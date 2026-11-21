/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Hub overlay (slimming step 4): the menu-time part of the Multishine hub client (od_glue's search / report /
 * name-lookup logic, the mnc request client, the mw2 session layer, SHA-256, the B9 / B4 / BE / BD answers of on_backend.c) moves out of the resident image into the
 * file "ms/hub.ovl".  On the card route the file is an "ours" entry of the signed MultishineOvl container (df_glue.c verifies the container's Ed25519 signature when the
 * table is read and every entry's CRC; tools: offline/multishine/menus/hub/hub_ovl_pack.py, hub_pack.py).
 *
 * File format, relocation scheme and packer rules are the sign-in overlay's (sg_ovl.h: struct sgo_hdr, sgo_check, sgo_relocate); hdr.state_hint = HUB_KIND.  The overlay
 * has no import relocations: it reaches the resident image only through struct hub_env (function and data pointers).  It has no state of its own that must survive a
 * reload: every field lives in the resident `struct od od_o` (the mnc client, its session keys and buffers included) and the resident status blocks.
 *
 * Life cycle (hub_res.c, resident): loaded into ONE block of the HSD heap by the first engine pass of a hub scene (main menu, Slippi's online character select) once the card
 * pack is armed and the card layer is idle; freed at every scene transition (om_heap_policy, lbHeap_80015900 hook) and whenever the scene is not a hub scene (the match,
 * the stage select, offline modes).  While it is absent every entry point answers from the resident remainder: B3 (match state), the hand-off (od_take_peer,
 * od_core_report, od_rx), BA (the search state), and BD (kept, given to the overlay at its next load); B4 says "Online files missing on the memory card" when the file
 * cannot be loaded at all.
 */
#ifndef HUB_OVL_H
#define HUB_OVL_H
#include "od_glue.h"
#include "../../exi_shim/sdshim.h"
#include "mw2_crypto.h"

#define HUB_PATH "ms/hub.ovl"
#define HUB_KIND 3u                    /* sgo_hdr.state_hint of a hub overlay (0 sign-in, 1 menu) */
#define HUB_ABI 1u                     /* struct hub_env / struct hub_api; resident and overlay of different ABIs refuse each other */

struct on_status;
struct onc_status;
struct ms_settings;

/* ---- update check on the title  ----
   The resident (hub_res.c, -DUPD_TITLE) owns this block: it fills the inputs (setting, installed builds, scene), loads the hub overlay on the title while a check is wanted,
   calls api->tchk once per title / Main Menu frame, and decides at the first settled Main Menu frame after the title whether the player goes straight into the Update offer
   (ut_decide below).  The overlay (the UPD_TITLE part of on_backend.c + upd/ut_check.c) does the network part: the online setup from the card, hello, one UPD_CHECK for our files (MultishineOvl)
   and one for the engine.  Everything that must survive the overlay's reloads (the block is freed at every scene change) lives here; never a secret. */
#define HUB_TCHK_MAGIC 0x54434B32u                          /* 'TCK2' (req_min appended to struct hub_tchk: an overlay of the other layout runs no title check) */
enum { UT_WAIT = 0, UT_HELLO = 1, UT_CHK_FILES = 2, UT_CHK_ENG = 3, UT_DONE = 4 };
enum { UT_W_NONE = 0, UT_W_FOUND = 1, UT_W_UPTODATE = 2, UT_W_OFF = 3, UT_W_NOCFG = 4, UT_W_NOLINK = 5, UT_W_FAILED = 6, UT_W_NOBUILD = 7 };     /* why UT_DONE */
enum { UT_D_NONE = 0, UT_D_WAIT = 1, UT_D_OFFER = 2, UT_D_LATE = 3, UT_D_HOLD = 4 };   /* ut_decide */
#define UT_SETTLE_BOOT_MS 0u        /* the FIRST check of a boot starts the moment the link is up: no settle, no jitter (the answer should be in before a typical START); one console, nobody to spread */
#define UT_HOLD_MS 4000u            /* START with the check still running and a real chance to finish: the 'Checking for updates.' page for at most this long, then the menu (a later answer is the toast) */
#define UT_HOLD_FRAMES (UT_HOLD_MS * 60u / 1000u)
#define UT_SETTLE_MS 1000u          /* after the link came up (DHCP done): the hub's own first steps go first; the title is short, so no long jitter here (LAUNCH_SCALE C6 is the menu check's) */
#define UT_JITTER_MS 2000u          /* re-checks only (a retry, a title revisit): + random 0..2 s: a server restart does not bring every console back in the same second */
#define UT_GIVE_UP_MS 120000u       /* no link (or no setup) this long after the first title frame: no check this boot, no text */
#define UT_RETRY_MS 5000u           /* a failed check is tried again after this long, only while still on the title */
#define UT_RETRY1_MS 2000u          /* the pause before the FIRST retry (then UT_RETRY_MS): 3 attempts per boot in all, 2 s and 5 s apart */
#define UT_RETRIES 2u
#define UT_RECHECK_MS 300000u       /* a title visit after an 'up to date' answer asks again once the answer is this old */
#define UT_BUSY_FRAMES 120u         /* Main Menu entry with an update found but the card busy (Melee's own save): wait at most this long, then the menu opens normally */
struct hub_tchk {
    unsigned int magic;
    /* resident -> overlay */
    unsigned int eng_seq, ovl_seq, tried_seq;              /* installed engine build_seq (loader status block, 0 = not known), MultishineOvl build_seq (df_glue, 0 = none), build the loader gave up on */
    unsigned char enabled, scene, pause, gen;              /* enabled: the setting (1 on, 0 off, 2 not read yet); scene: 0 title, 1 Main Menu scene, 2 elsewhere; pause: the sign overlay is open
                                                              (the Update screen uses the same client); gen: hub overlay loads (the overlay's own state is gone after each) */
    /* overlay -> resident (kept across reloads) */
    unsigned char state, found, retries, kgen;             /* UT_*; found: 1 = a newer release exists; kgen: the gen the overlay last ran in */
    unsigned char why, link_seen, t0_set, hold;
    unsigned int latest;                                   /* the newest release on offer (max of both parts) */
    unsigned int t0, t_link, jit, t_hold, t_done;
    unsigned int checks, fails;                            /* proof counters */
    /* resident only: the prompt */
    unsigned char prompted, late, toasted, entry;          /* prompted: the offer was shown this boot (A or B, never again); late: the Main Menu opened before the answer;
                                                              entry: the Main Menu scene began after the title and its first settled frame has not decided yet */
    unsigned int wait;
    unsigned int req_min;                                  /* overlay -> resident, appended last: the minimum build the server asked for in its last answer (0 = none or not asked); */
};
/* FORCED UPDATE gate (pure; ms_offline_server): the server's minimum is above the running engine build.  Fail open: no answer yet (0), or the running build not known (0). */
static inline int ut_req_needed(const struct hub_tchk *t) { return t->req_min && t->eng_seq && t->req_min > t->eng_seq; }

/* The running check has a reasonable chance to finish within the hold: the link is up and the setup is in (link_seen), no failed try is waiting to be repeated, or a request is already out. */
static inline int ut_chance(const struct hub_tchk *t)
{
    if (t->state >= UT_DONE) return 0;
    if (t->state != UT_WAIT) return 1;
    return t->link_seen && !t->hold && t->retries == 0u && t->fails == 0u;
}

/* At the first settled Main Menu frame after the title (pure: host-tested in upd/tests/ut_test.c).  card_idle: Melee's card layer, the DVD and the shared CARD work area are
   free; waited: frames this decision has been waiting for an idle card. */
static inline int ut_decide(const struct hub_tchk *t, int card_idle, unsigned int waited)
{
    if (t->enabled != 1u || t->prompted) return UT_D_NONE;          /* setting off (or not known: the check never ran), or already offered this boot */
    if (t->state == UT_DONE) {
        if (!t->found) return UT_D_NONE;                            /* up to date, no setup, no link, failed: the menu, silently */
        if (!card_idle) return waited < UT_BUSY_FRAMES ? UT_D_WAIT : UT_D_LATE;   /* never while Melee writes its save: a moment, then the normal menu */
        return UT_D_OFFER;
    }
    return ut_chance(t) ? UT_D_HOLD : UT_D_LATE;                    /* still checking: a short hold when it can finish, else no waiting; a later answer is only a toast */
}
/* The title was entered again (backing out of the menus; pure, host-tested): the check is a once-per-boot state machine that stayed in UT_DONE, so a release published after the
   first answer (or a first try that failed: no link yet, a busy server) was never looked for again.  A DONE check that found nothing is armed anew: at once after a failure / no link /
   no setup (nothing was learned), UT_RECHECK_MS after an 'up to date' answer.  Never after an offer was made (prompted: once per boot, also after Later), never with the setting off,
   never for a found update (the next START offers it).  1 = armed again. */
static inline int ut_rearm(struct hub_tchk *t, unsigned int now)
{
    if (t->enabled != 1u || t->prompted || t->state != UT_DONE || t->found) return 0;
    if (t->why == UT_W_OFF || t->why == UT_W_NOBUILD) return 0;
    if (t->why == UT_W_UPTODATE && now - t->t_done < UT_RECHECK_MS) return 0;
    t->state = UT_WAIT; t->why = UT_W_NONE; t->retries = 0; t->link_seen = 0; t->t0_set = 0; t->hold = 0;
    t->late = 0; t->toasted = 0; t->latest = 0;
    return 1;
}
/* the check finished after the Main Menu opened: one toast (when the console has the mechanism), never a screen */
static inline int ut_toast_due(const struct hub_tchk *t)
{
    return t->enabled == 1u && t->late && t->found && !t->prompted && !t->toasted && t->scene == 1u;
}
/* One Main Menu scene frame (hub_res.c ut_menu_frame; pure, host-tested): kind = the menu screen (0 = the Main Menu's top level), settled = its rows are built, sign_state = the
   sign overlay (0 = closed), card_idle as above.  1 = open the Update offer now (marked as offered: 'Later' or not, never again this boot), -1 = post the toast (a late answer),
   0 = nothing.  NO SCREEN unless an update IS available (the 'Checking for updates.' hold page, titled Update, opened on every quick START, also when
   up to date): START with a check that can still finish is a SILENT wait (entry 2) of at most UT_HOLD_FRAMES on the Main Menu as it is; the answer 'found' opens the offer, anything
   else ends the wait without a sign; the player moving on (another screen, the sign overlay) ends it too (a later answer is the toast / the offer back on the top level).  The
   first-run offer still waits for this decision (fr_step ut_pending), so the update offer stays first.  2 and 3 (the old hold page) are never returned. */
static inline int ut_menu_step(struct hub_tchk *t, unsigned int kind, int settled, unsigned int sign_state, int card_idle)
{
    int d;
    t->pause = (unsigned char)(sign_state != 0u);
    if (t->entry == 2u) {                                           /* the silent wait: wait counts its frames */
        if (kind != 0u || sign_state) { t->entry = 0; t->late = 1; return 0; }                 /* the player moved on */
        t->wait++;
        if (t->state == UT_DONE) {
            if (!t->found || t->enabled != 1u || t->prompted) { t->entry = 0; return 0; }     /* nothing newer, failed, off: nothing shown */
            if (!card_idle && t->wait < UT_HOLD_FRAMES + UT_BUSY_FRAMES) return 0;             /* never while Melee writes its save: a moment */
            if (!card_idle) { t->entry = 0; t->late = 1; return 0; }
            if (!settled) return 0;
            t->entry = 0; t->prompted = 1;
            return 1;                                                                           /* the offer */
        }
        if (t->wait >= UT_HOLD_FRAMES) { t->entry = 0; t->late = 1; return 0; }              /* the cap: a late answer is the toast and the offer back on the top level */
        return 0;
    }
    if (!t->entry) {
        if (ut_toast_due(t)) {
            /* the answer came after START: still on the Main Menu's top level
               with nothing open and the card free = the offer now; on another screen = the toast */
            if (kind == 0u && settled && !sign_state && card_idle) { t->prompted = 1; return 1; }
            if (kind != 0u) { t->toasted = 1; return -1; }
        }
        /* the toast was shown on another screen: the next time the player is back on the Main Menu's top level (nothing open, card free), the offer opens once */
        if (t->enabled == 1u && t->found && t->toasted && !t->prompted && kind == 0u && settled && !sign_state && card_idle) { t->prompted = 1; return 1; }
        return 0;
    }
    if (sign_state || !settled) return 0;
    if (kind != 0u) { t->entry = 0; t->late = 1; return 0; }        /* the player is already on another screen */
    d = ut_decide(t, card_idle, t->wait++);
    if (d == UT_D_WAIT) return 0;
    if (d == UT_D_HOLD) { t->entry = 2u; t->wait = 0; return 0; }   /* START while the check runs: the silent wait (no page) */
    t->entry = 0;
    if (d == UT_D_LATE) t->late = 1;
    if (d != UT_D_OFFER) return 0;
    t->prompted = 1;
    return 1;
}

/* B during the hold: straight to the menu; a later answer is still the toast (pure, host-tested) */
static inline void ut_hold_skip(struct hub_tchk *t) { if (t->entry == 2u) { t->entry = 0; t->late = 1; } }

/* the network part's io (upd/ut_check.c, see ut_check.h): the overlay supplies the real one (on_backend.c), the host test fakes */
struct ut_io {
    int (*cfg)(unsigned now);                       /* the online setup: 1 ready, 0 not yet (card busy), -1 never (no file, damaged, incomplete) */
    int (*link_up)(void);                           /* BBA address and server address known */
    int (*net_busy)(void);                          /* the hub client runs an operation of its own (a search, a report) */
    int (*sess_ready)(unsigned now);                /* a wire session is READY (no hello needed) */
    int (*hello)(unsigned now);                     /* start a hello: 0 started, 1 the client is busy (later), -1 failed */
    int (*hello_poll)(unsigned now);                /* 0 busy, 1 done, -1 failed */
    int (*check)(unsigned now, unsigned abi, unsigned have);       /* start one UPD_CHECK: 0 started */
    int (*check_poll)(unsigned now, unsigned *latest);             /* 0 busy, 1 a newer build (*latest), 2 nothing newer, -1 failed (no answer, busy server: worth a retry), -2 failed for good */
    void (*abort)(void);                            /* drop the UPD_CHECK in flight (the hello, if any, is left to finish: its state is the resident client's) */
    unsigned (*rand)(void);
    unsigned (*req_min)(void);                      /* appended: after a finished check, the minimum build of its advisory (0 = none / not answered); may be 0 (the host tests) */
};

#define UT_ABI_ENGINE 1u
#define UT_ABI_FILES 2u

void ut_step(struct hub_tchk *t, const struct ut_io *io, unsigned now_ms);

/* what the resident image hands the overlay */
struct hub_env {
    unsigned int abi;
    unsigned int sz_od, sz_mnc, sz_cfg, sz_st;            /* layouts of the resident build: the overlay refuses others */
    struct od *o;
    struct od_cfg *cfg;
    struct od_status *st;
    struct on_status *on_st;
    struct onc_status *onc_st;
    unsigned int seed;                                     /* time base at od_set_backend (nonce pool only) */
    int (*send_cb)(void *ctx, const mnc_u8 *p, mnc_u32 len);   /* od_glue's mnc io: resident, so the mnc io pointers never point into a freed block */
    int (*recv_cb)(void *ctx, mnc_u8 *buf, mnc_u32 cap);
    unsigned int (*clock_ms)(void);
    int (*core_final)(unsigned int *frame, unsigned int *cks);
    void (*ob_arm)(void);
    int (*onc_on_status)(struct od_cfg *cfg);
    int (*onc_final)(void);
    unsigned int (*onc_backup)(void);
    int (*onc_save_ident)(const struct od_cfg *c);
    void (*omc_on_status)(void);
    void (*omc_on_code)(const char *code);
    int (*omc_answer)(const unsigned char *req, unsigned char *rsp);
    unsigned int (*ch_from_melee)(const unsigned char *b, unsigned int len, char *out);
    void (*sha512)(const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[64]);
    void (*hmac)(const mw2_u8 key[32], const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[32]);
    int (*ed_verify)(const mw2_u8 pub[32], const mw2_u8 *msg, mw2_u32 n, const mw2_u8 sig[64]);
    int (*ct_equal)(const mw2_u8 *a, const mw2_u8 *b, mw2_u32 n);
    void (*dns_set_servers)(struct sddns *d, unsigned int primary, unsigned int secondary);
    void (*dns_cache_flush)(struct sddns *d);
    int (*dns_start)(struct sddns *d, const char *host, unsigned int now);
    int (*dns_poll)(struct sddns *d, unsigned int now);
    void (*dns_abort)(struct sddns *d);
    int (*dns_parse_dotted)(const char *s, unsigned int *ip);
    int (*dns_normalize_host)(const char *host, char *out);
    void (*ranked_as_unranked)(int on);                   /* the resident mode-byte switch (it must also run while the overlay is absent: BA) */
    /* appended (GAME_RECORD of OUR modes); HUB_ABI unchanged: the overlay uses these only when gs_magic == HUB_GS_MAGIC, so an
       older resident (a table that ends above) simply gets no record, and an older overlay never reads them.  The queue lives in the RESIDENT (it must outlive the overlay,
       which is freed at the next scene change) as HUB_GR_SLOTS slots of HUB_GR_SLOT bytes: [0] used, [1] tries, [2..55] the 54-byte GAME_RECORD body. */
    unsigned int gs_magic;
    void (*core_stats)(unsigned int *w);                  /* od_core_stats (s12_gc.c): the facts of the newest match, 12 words, [0] = 1 when valid */
    unsigned char *gr_q;
    unsigned int gr_q_n;                                  /* the queue's REAL size: the overlay checks it before every part (review H1): >= 112 the slots, >= HUB_GR_BYTES the refresh-HELLO
                                                             bytes too.  Residents between e512b944 and d635ef5d have 112 under the same magic; never touch a byte past gr_q_n. */
    /* appended (test builds): the readout lines for the LAB_DIAG upload; read only when ld_magic == HUB_LD_MAGIC (an older resident's table ends above) */
    unsigned int ld_magic;
    const char *(*ld_lines)(unsigned int *gen, unsigned int *n);
    /* appended (CSS chips): read only when ch_magic == HUB_CH_MAGIC.  The delay chip and the Grandmaster region chip run in this overlay (they left the
       engine: release engine size); the delay word is the engine's s3_cfg[3] (one source of truth), ch_chg = ons_change_delay (the Settings record), ch_region = the resident region chip
       value (0 Auto, 1..6 NAE..OC; it outlives the overlay's reloads). */
    unsigned int ch_magic;
    volatile unsigned int *ch_delay;
    int (*ch_chg)(unsigned int v, unsigned int frame);
    unsigned char *ch_region;
    /* appended (Online delay Auto): read only when ad_magic == HUB_AD_MAGIC.  The engine's Auto word (offline/delay_auto/auto_delay_tab.h
       AD_W_*: the frames Auto plays now and the RTT it came from) for the chip's "Delay Auto (3f)"; 0 pointer = an engine without Auto (plain "Auto"). */
    unsigned int ad_magic;
    volatile unsigned int *ad_word;
    /* appended (Wired opponents only): read only when wf_magic == HUB_WF_MAGIC (an older resident's table ends above).  The resident settings record
       (on_set.c ons_cur): the overlay reads bit 2 of `hudraise` = the switch when it builds an Unranked search.  8 bytes of resident data, no resident code. */
    unsigned int wf_magic;
    const struct ms_settings *(*wf_cur)(void);
};
#define HUB_WF_MAGIC 0x57465231u                           /* 'WFR1' */
#define HUB_AD_MAGIC 0x41444C31u                           /* 'ADL1' */
#define HUB_CH_MAGIC 0x43484931u                           /* 'CHI1' */
#define HUB_LD_MAGIC 0x4C444731u                           /* 'LDG1' */
#define HUB_GS_MAGIC 0x47535631u                           /* 'GSV1' */
#define HUB_GR_SLOTS 2u
#define HUB_GR_SLOT 56u
#define HUB_GR_HELLO 112u                                  /* byte after the slots: refresh-HELLO state (0 not yet, 2 done), 113 = failed tries */
#define HUB_GR_BYTES 120u
#define HUB_GR_LOST 114u                                   /* records lost (pushed out of the full queue, or given up) since the last one the server took; 255 = 255 or more. */
/* appended (the record's trailing IO block,  "IO block"; the Wii counters go to the server, not on the screen): a resident with gr_q_n >= HUB_GR_XBYTES has, after the
   120 bytes above, one IO block per slot (HUB_GR_IOX_SLOT bytes each; the 23-byte block of s12_gc.c od_core_io, byte 0 = 1 once written).  The RESIDENT writes it, inside core_stats, which every overlay
   with records calls right before it queues a record (on_backend.c gr_record): hub_gr_io_slot picks the slot with the overlay's own rule (the first free one, else the oldest goes: shift, newest last),
   so the blocks stay with their records whatever overlay version runs.  The overlay appends a slot's block when byte 0 is 1; on an older resident (120 / 112 bytes) the records stay 54 bytes. */
#define HUB_GR_IOX 120u
#define HUB_GR_IO_LEN 23u
#define HUB_GR_IOX_SLOT 24u
#define HUB_GR_XBYTES 168u
/* appended (the record's trailing GAME-OVER block,  "Game-over block"): after the IO blocks, one HUB_GR_GOX_SLOT-byte block per slot (10 bytes of s12_gc.c od_core_go, byte 0 = 2
   once written, 0 = the game never ended / no flag seen).  Written by the resident in core_stats next to the IO block, in the same slot; a resident with gr_q_n >= HUB_GR_X2BYTES has it. */
#define HUB_GR_GOX 168u
#define HUB_GR_GO_LEN 10u
#define HUB_GR_GOX_SLOT 10u
#define HUB_GR_X2BYTES 188u
/* appended (the record's trailing PQ block, DESYNC_DEEP_REVIEW_2026-11-13 section 7, ms_records.PQ): after the game-over blocks, one HUB_GR_PQX_SLOT-byte block per slot (9 bytes of
   s12_gc.c od_core_pq, byte 0 = 3 once written).  Written by the resident in core_stats next to the other two, in the same slot; a resident with gr_q_n >= HUB_GR_X3BYTES has it. */
#define HUB_GR_PQX 188u
#define HUB_GR_PQ_LEN 9u
#define HUB_GR_PQX_SLOT 10u
#define HUB_GR_X3BYTES 208u
static inline unsigned char *hub_gr_pq_slot(unsigned char *q)      /* q = the whole queue (HUB_GR_X3BYTES): the PQ block of the slot the next record goes to (same rule as hub_gr_go_slot) */
{
    unsigned int k, i;
    unsigned char *x = q + HUB_GR_PQX;
    for (k = 0; k < HUB_GR_SLOTS && q[k * HUB_GR_SLOT]; k++) ;
    if (k == HUB_GR_SLOTS) { for (i = 0; i < HUB_GR_PQX_SLOT; i++) x[i] = x[HUB_GR_PQX_SLOT + i]; k--; }
    return x + k * HUB_GR_PQX_SLOT;
}
static inline unsigned char *hub_gr_go_slot(unsigned char *q)      /* q = the whole queue (HUB_GR_X2BYTES): the game-over block of the slot the next record goes to (call after hub_gr_io_slot: the slots are the same) */
{
    unsigned int k, i;
    unsigned char *x = q + HUB_GR_GOX;
    for (k = 0; k < HUB_GR_SLOTS && q[k * HUB_GR_SLOT]; k++) ;
    if (k == HUB_GR_SLOTS) { for (i = 0; i < HUB_GR_GOX_SLOT; i++) x[i] = x[HUB_GR_GOX_SLOT + i]; k--; }
    return x + k * HUB_GR_GOX_SLOT;
}
static inline unsigned char *hub_gr_io_slot(unsigned char *q)      /* q = the whole queue (HUB_GR_XBYTES): the IO block of the slot the next record goes to */
{
    unsigned int k, i;
    unsigned char *x = q + HUB_GR_IOX;
    for (k = 0; k < HUB_GR_SLOTS && q[k * HUB_GR_SLOT]; k++) ;
    if (k == HUB_GR_SLOTS) { for (i = 0; i < HUB_GR_IOX_SLOT; i++) x[i] = x[HUB_GR_IOX_SLOT + i]; k--; }
    return x + k * HUB_GR_IOX_SLOT;
}

/* the overlay's entry table, at hdr.api_off */
struct hub_api {
    unsigned int abi;
    int (*enter)(const struct hub_env *e, int first);     /* 0 = ready; first = the first load since boot: the mnc client is initialised (od_init's second half) */
    void (*leave)(void);
    void (*tick)(struct od *o, unsigned int now_ms);       /* od_tick */
    void (*set_net)(struct od *o);                        /* mnc_set_net for the address in force (od_net_up while loaded, and at every enter) */
    int (*b9)(void *ctx, sdshim_u8 *osb);
    int (*b4)(void *ctx, sdshim_u8 mode, const sdshim_u8 *code);
    int (*be)(void *ctx, const sdshim_u8 *req, sdshim_u8 *rsp);
    void (*bd)(void *ctx, const sdshim_u8 *rgb);
    void (*ba_tail)(void);                                 /* od_cancel + the identity flush of BA (the resident did the rest) */
    int (*upd_hello)(unsigned int now_ms);                 /* mnc_hello / mnc_poll on the resident client for the Update row */
    int (*upd_poll)(unsigned int now_ms);
    void (*cfg_apply)(void);                               /* the setup the resident B9 read from the card while the overlay was absent: od_apply_config + the backup address */
    /* appended (update check on the title); HUB_ABI unchanged: the resident checks the image bounds with
       HUB_API_BASE_SIZE and calls tchk only when tchk_magic == HUB_TCHK_MAGIC, so an older overlay (no such fields) simply runs no title check */
    unsigned int tchk_magic;
    void (*tchk)(struct hub_tchk *t, unsigned int now_ms);
};
#define HUB_API_BASE_SIZE (13u * 4u)                       /* struct hub_api up to cfg_apply: what every hub.ovl since ABI 1 carries */

/* od_glue.c: the second half of od_init (HUB_OVL build): mnc_init with the resident io callbacks, the config, the status block */
void od_init_mnc(struct od *o, int (*send_cb)(void *, const mnc_u8 *, mnc_u32), int (*recv_cb)(void *, mnc_u8 *, mnc_u32), unsigned int seed);

/* ---- resident RAM proof words (symbol hub_st, hub_status_addr): never a secret ---- */
enum {
    HB_MAGIC,      /* 'HBST' */
    HB_STATE,      /* 0 absent, 1 loaded, 2 failed (this scene), 3 waiting for the card pack / card layer */
    HB_LOADS,      /* successful loads */
    HB_FREES,      /* blocks given back */
    HB_FAILS,      /* failed load attempts */
    HB_ERR,        /* HB_E_* of the last failure */
    HB_BLOCK,      /* bytes of the HSD block while loaded */
    HB_ADDR,       /* load address while loaded */
    HB_TICKS,      /* time base ticks of the last load (40.5 MHz) */
    HB_SCENE,      /* scene word at the last load */
    HB_FSCENE,     /* scene word at the last free */
    HB_LEAK_SKIP,  /* blocks not on the heap's list any more at a scene change (heap rebuilt): forgotten, never freed, never called */
    HB_WAITS,      /* ticks a load waited for the pack table / card layer */
    HB_PEND_BD,    /* BD reports kept while absent / given to the overlay */
    HB_PEND_BA,    /* BA tails kept while absent / given to the overlay */
    HB_ABSENT_B4,  /* B4 answered "online files missing" */
    HB_ABSENT_B9,  /* B9 answered by the resident fallback */
    HB_HSD_FREE,   /* largest free HSD cell seen just before the last load (bytes) */
#ifdef UPD_TITLE
    HB_UT_STATE,   /* update check on the title: state | why << 8 | enabled << 16 | found << 20 | pack table read << 22 | overlay has tchk << 23 | prompted << 24 | late << 25 | toasted << 26 | BBA address << 27 | scene << 28 */
    HB_UT_LATEST,  /* the release on offer (0 = none) */
    HB_UT_SEQ,     /* installed engine build_seq << 16 | MultishineOvl build_seq (low 16 bits each) */
    HB_UT_COUNT,   /* checks sent | fails << 8 | bit 16: the backend was set up early on the title | gen << 24 (hub overlay loads) */
    HB_FR,         /* the first-run offer (struct fr_offer below): armed | done << 8 | why << 16 | offered << 24 */
#endif
    HB_REARMS,     /* give-ups lifted while a report / cancel was kept (hub_gate.h) */
    HB_N
};
enum { HB_E_NONE = 0, HB_E_NOPACK = 1, HB_E_NOFILE = 2, HB_E_SIZE = 3, HB_E_NOMEM = 4, HB_E_READ = 5, HB_E_FORMAT = 6, HB_E_ABI = 7, HB_E_ENTER = 8, HB_E_BUSY = 9 };

/* ---- the FIRST-RUN offer after START ("when the title-screen update check runs, also check whether the entropy > link
   flow has been done; if not, offer it").  Pure (static inline): hub_res.c runs it on every Main Menu scene frame (fr_menu_frame), upd/tests/ut_test.c tests it.  Resident only,
   not part of the overlay ABI (struct hub_env / hub_tchk are unchanged).
     - armed when the Main Menu scene begins after the title (START), at most once per boot;
     - waits while the update decision of the same entry is open (the update offer comes FIRST) and while the sign overlay is open (the Update screen);
     - offers only on the Main Menu's top level; a player who moved on to another screen is not interrupted (no offer this boot);
     - a linked console (device id not all zero) is never asked; neither is one without its online setup or a network address (the link needs the server): it waits
       FR_WAIT_FRAMES for both, then gives up for this boot;
     - the offer is shown once per boot, whatever the answer ('Later' is remembered).  It is a question first: only "Link Now" leads to the entropy pass (when the pool
       is short) and the Link screen, so a guest who says Later never sees the entropy screen. ---- */
#define FR_WAIT_FRAMES 3600u         /* 60 s on the Main Menu for the online setup and the BBA address (was 10 s: a Wii's BBA address comes late and the first run was never offered) */
struct fr_offer { unsigned char armed, done, offered, why, pdone, pad; unsigned short wait; };      /* pdone: the progressive scan question (the FIRST step of the first run) was opened or given up this boot */
enum { FR_W_NONE = 0, FR_W_OFFERED, FR_W_LINKED, FR_W_MOVED, FR_W_NOTREADY };
static inline void fr_arm(struct fr_offer *f) { if (!f->done) f->armed = 1; }
/* kind: the menu screen (0 = the Main Menu's top level); settled: its rows are built; sign_open: the sign overlay is open; ut_pending: the update decision of this entry is not made
   (hub_tchk.entry); linked: the identity in force has a device id; ready: the online setup is in force and the BBA has an address.  1 = open the offer now (done: never again) */
static inline int fr_step(struct fr_offer *f, unsigned int kind, int settled, int sign_open, int ut_pending, int linked, int ready)
{
    if (f->done || !f->armed) return 0;
    if (sign_open || ut_pending || !settled) return 0;
    if (kind != 0u) { f->done = 1; f->why = FR_W_MOVED; return 0; }
    if (linked) { f->done = 1; f->why = FR_W_LINKED; return 0; }
    if (!ready) {
        if (++f->wait >= FR_WAIT_FRAMES) { f->done = 1; f->why = FR_W_NOTREADY; }
        return 0;
    }
    f->done = 1; f->offered = 1; f->why = FR_W_OFFERED;
    return 1;
}
/* The progressive scan question, FIRST step of the first run ("ask progressive scan first, so the rest is in better quality if possible").  The same moment rules as the
   first-run offer (armed after START, the update decision first, no overlay open, Main Menu top level, rows built) but NO link / network conditions: it needs only a cable and an unset
   setting, which the glue tells (due).  Once per boot whatever the answer; a player who moved on is not asked this boot.  1 = open the question now. */
static inline int fr_prog_step(struct fr_offer *f, unsigned int kind, int settled, int sign_open, int ut_pending, int due)
{
    if (f->pdone || !f->armed) return 0;
    if (sign_open || ut_pending || !settled) return 0;
    if (kind != 0u) { f->pdone = 1; return 0; }
    if (!due) return 0;
    f->pdone = 1;
    return 1;
}

#endif
