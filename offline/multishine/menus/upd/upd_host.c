/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_host: see upd_host.h.  Soft-float, no libc, no heap. */
#include "upd_host.h"
#include "upd_card.h"
#ifdef UPD_BAK
#include "upd_bak.h"
#endif

#ifdef UPD_CL_KEYS
typedef unsigned char u8;
#include "cl_keys.h"                            /* CL_NKEYS, cl_pub[][32]: the very list the loader of this build pins */
#endif

#define CLS_MAGIC 0x434C0001u
#define CLS_DONE 7u

#define CLS_LOADED 6u                           /* image loaded and patched, the entry is about to run (the loader has not returned yet) */

static void boot_read(const volatile unsigned *st, struct upd_boot *b, int at_entry)
{
    b->valid = b->seq = b->floor_seq = b->chan = b->from_prev = b->dev = b->tried = 0;
    if (!st) return;
    if (st[0] != CLS_MAGIC || (st[1] != CLS_DONE && !(at_entry && st[1] == CLS_LOADED)) || st[2] != 0u) return;       /* not a finished, successful load */
    if (!(st[16] & 1u)) return;                                               /* not a verifying loader: the build_seq in the file is not vouched for */
    if (st[18] == 0u) return;
    b->valid = 1; b->seq = st[18]; b->floor_seq = st[17]; b->chan = st[3] == 1u ? 1u : 0u; b->from_prev = st[20] ? 1u : 0u; b->dev = (st[16] & 2u) ? 1u : 0u; b->tried = st[23];
}

void upd_boot_read(const volatile unsigned *st, struct upd_boot *b) { boot_read(st, b, 0); }

static struct sgu_host H;
static struct upd_boot BOOT;
static struct upd_cfg CFG;
#ifdef UPD_BAK
static int host_backup(void)                    /* the Update row's backup to slot B (upd_bak.c): off by the player's switch, or the result of the copy; never blocks the install */
{
    struct bak_cfg c;
    if (CFG.bak_on && !CFG.bak_on()) return SGU_BAK_OFF;
    c.alloc = CFG.alloc; c.free = CFG.free; c.seq = BOOT.valid ? BOOT.seq : 0u; c.mode = BAK_M_UPDATE;
    return bak_run(&c);
}
static int host_copy_b(void)                    /* the Update row's "Copy card to slot B" (the player's own request, not tied to an update, not switched off by the Backup option): the same copy without the two login files */
{
    struct bak_cfg c;
    c.alloc = CFG.alloc; c.free = CFG.free; c.seq = BOOT.valid ? BOOT.seq : 0u; c.mode = BAK_M_COPY;
    return bak_run(&c);
}
#endif
/* a card of 251 blocks or fewer (59, 123, 251: 2 MB and smaller) cannot hold the previous build next to the new one and the pack: the B copies are off there whatever the switch says; 0 = unknown size = the player's switch */
static int host_keep(void) { unsigned t = upc_total_blocks(); if (t && t <= 251u) return 0; return CFG.keep_on ? CFG.keep_on() : 1; }
static unsigned pending;
static struct seed_ops SEEDOPS;                   /* the card for MultishinePack (seed_plan.h): the ota ops plus blocks / create_gci / free / total of upd_card.c */

static int host_card_ready(void)
{
    if (!BOOT.valid) return 0;                  /* running from the fallback is fine: the installer then keeps it (sgu_core.c KEEPPREV) and replaces the active file */
    return upc_ready();
}

static void host_reset(void)
{
    upc_done();                                 /* nothing mounted when the console restarts */
    if (CFG.reset) CFG.reset();
}

const struct sgu_host *upd_host_init(const struct upd_cfg *cfg)
{
    CFG.reset = cfg->reset; CFG.alloc = cfg->alloc; CFG.free = cfg->free; CFG.bak_on = cfg->bak_on; CFG.keep_on = cfg->keep_on;                     /* only what is used after init is kept (no struct copy: no memcpy in the resident) */
    boot_read(cfg->cl_status, &BOOT, cfg->at_entry);
#ifndef UPC_HOST
    upc_set_wa(cfg->wa_acquire, cfg->wa_release);
#endif
    upc_set_chan((int)BOOT.chan);
    H.size = (unsigned)sizeof H; H.net_size = (unsigned)sizeof(mnc); H.net = cfg->net;
    H.hello = cfg->hello; H.poll = cfg->poll; H.link_up = cfg->link_up;
    H.card_ready = host_card_ready; H.card = upc_ops(); H.card_done = upc_done;
    H.alloc = cfg->alloc; H.free = cfg->free; H.reset = cfg->reset ? host_reset : 0;      /* no software reset wired (card route, first console builds): the screen says to switch the console off and on */
#ifdef UPD_CL_KEYS
    H.pubs = cl_pub; H.npub = CL_NKEYS;
    (void)cfg->pubs; (void)cfg->npub;
#else
    H.pubs = cfg->pubs; H.npub = cfg->npub;
#endif
    H.abi = cfg->abi;
    H.installed_seq = BOOT.valid ? BOOT.seq : 0u;
    H.floor_seq = BOOT.valid ? BOOT.floor_seq : 0u;
    H.from_prev = BOOT.valid ? BOOT.from_prev : 0u;
    H.tried_seq = BOOT.valid ? BOOT.tried : 0u;
    H.pending_seq = cfg->pending ? cfg->pending : &pending;
    H.allow_unsigned = cfg->allow_unsigned && BOOT.dev;       /* an unsigned file is only ever accepted when the running loader is itself a dev loader */
    H.adv = cfg->adv;
    SEEDOPS.ota = upc_ops(); SEEDOPS.blocks = upc_file_blocks; SEEDOPS.create_gci = upc_create_gci; SEEDOPS.free_raw = upc_free_raw; SEEDOPS.total = upc_total_blocks;
    H.seed = &SEEDOPS; H.disc = cfg->disc; H.keep_prev = cfg->keep_on ? host_keep : 0;
#ifdef UPD_BAK
    H.backup = host_backup; H.copy_b = host_copy_b;
#else
    H.backup = 0;
#endif
    return &H;
}

const struct upd_boot *upd_host_boot(void) { return &BOOT; }
unsigned upd_host_pending(void) { return H.pending_seq ? *H.pending_seq : pending; }

/* ---- engine side of the boot record (the rules are bootrec_* in ota_core.c) ---- */
static const char ST_NAME[] = "MultishineState";
static const char PV_NAME[] = "MultishineCodeB";

/* one card attempt: good = 0 counts this start, good = 1 marks the Main Menu reached.  Returns 0 when done or nothing to do, 1 when it should be tried again later
   (Melee's card layer busy, no work area, a card error).  Never blocks beyond the card calls; the card is unmounted again before it returns. */
int upd_boot_note(int good)
{
    const struct ota_card_ops *ops = upc_ops();
    unsigned char *blk;
    int r;
    if (!BOOT.valid || BOOT.from_prev) return 0;        /* unknown build, or running as the fallback of a failed one: nothing to count */
    if (!upc_ready()) return 1;
    (void)ops->exists(0, ST_NAME);                      /* the first call mounts; the 8 KiB block behind the work area is valid from then on */
    blk = upc_block();
    if (!blk) { upc_done(); return 1; }
    r = good ? bootrec_good(ops, ST_NAME, blk, BOOT.seq) : bootrec_start(ops, ST_NAME, PV_NAME, blk, BOOT.seq);
    upc_done();
    return r == BR_IO ? 1 : 0;
}

/* Once per frame, from the first resident frame that can run card calls (phase 0: count this start), and with main_menu = 1 once the Main Menu scene runs (phase 1: good).
   A busy card is retried once a second: phase 0 at most UBT_TRIES0 times (then the start stays uncounted this session: a missing count only weakens the rollback), phase 1 at
   most UBT_TRIES1 times (a missing good marker leaves tries at 1 or 2, so a build that cannot mark itself good would be rolled back after a few boots: the Main Menu has a long
   window, so the limit is generous).  After phase 1 is over nothing is called any more: a steady-state boot costs the two reads of phase 0 and 1 and writes nothing. */
#define UBT_TRIES0 30u
#define UBT_TRIES1 120u
/* RAM proof words (engine builds: upd_bst_addr): [0] 'UBT1', [1] phase (0 counting, 1 waiting for the Main Menu, 2 done), [2] calls of upd_boot_note, [3] last result
   of the count (0xFF = not tried), [4] last result of the good marker (0xFF = not tried), [5] loader status valid (1) / not (0), [6] build_seq the loader reported */
volatile unsigned upd_bst[8] = { 0x55425431u, 0, 0, 0xFFu, 0xFFu, 0, 0, 0 };

void upd_boot_tick(unsigned now_ms, int main_menu)
{
    static unsigned char phase, tries;
    static unsigned next_ms;
    int r;
    if (phase >= 2) return;
    if (phase == 1 && !main_menu) return;
    if (tries && (int)(now_ms - next_ms) < 0) return;
    r = upd_boot_note(phase);
    upd_bst[2]++; upd_bst[3 + phase] = (unsigned)r; upd_bst[5] = BOOT.valid; upd_bst[6] = BOOT.seq;
    if (r == 0 || ++tries >= (phase ? UBT_TRIES1 : UBT_TRIES0)) {
        phase++; tries = 0; upd_bst[1] = phase;
#ifdef UPD_BOOT_TEST_HANG
        /* TEST ONLY (MANIFEST option rbtest=hang, never in a shipped build): a build whose first frames hang once its start is counted, to prove the automatic rollback with the real engine */
        if (phase == 1 && BOOT.valid && !BOOT.from_prev && r == 0) { for (;;) { } }
#endif
        return;
    }
    next_ms = now_ms + 1000u;
}

#ifdef UPD_BOOT_ENGINE
/* ---- the resident engine's two calls: upd_boot_frame from the scene loop's first frames (s3_tick_body), upd_boot_menu from the Main Menu frame
   stub (ml_stub.S).  The shared CARD work area is the engine's (on_hist.c), the loader status block is at its stock address. ---- */
extern unsigned char *ms_card_wa_acquire(unsigned owner);
extern void ms_card_wa_release(void);
extern unsigned od_clock_ms(void);
/* the loader's status block: list base + 0x3600 of the card layout the engine is built for (stock 0x817F7600, CL_LAYOUT=generic 0x817E3600, low 0x817C3600); MS_CARD_WA_ADDR is the layout's loader work area (release.sh WA_ADDR, card_payload.py) */
#if defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817D4000u
#define MS_CL_STATUS_ADDR 0x817E3600u
#elif defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817B4000u
#define MS_CL_STATUS_ADDR 0x817C3600u
#else
#define MS_CL_STATUS_ADDR 0x817F7600u
#endif
#ifndef UBT_CL_STATUS
#define UBT_CL_STATUS MS_CL_STATUS_ADDR                 /* CL_STATUS of the card layout (cardloader/cl/mscf.h) */
#endif
static void boot_frame(int main_menu)
{
    static unsigned char init;
    if (!init) {                                /* the same three steps as upd_host_init, without the Update row's host (that lives in the overlay) */
        boot_read((const volatile unsigned *)UBT_CL_STATUS, &BOOT, 1);
        upc_set_wa(ms_card_wa_acquire, ms_card_wa_release);
        upc_set_chan((int)BOOT.chan);
        init = 1;
    }
    upd_boot_tick(od_clock_ms(), main_menu);
}
void upd_boot_frame(void) { boot_frame(0); }
void upd_boot_menu(void) { boot_frame(1); }
#ifdef UPD_TITLE
/* the update check on the title (hub_res.c): the running engine build as the Update screen sees it (a finished load by a verifying loader), 0 = not known (yet: the status block is
   read by the first upd_boot_frame); *tried = the build the boot record gave up on (status word 23) */
unsigned upd_boot_seq(unsigned *tried)
{
    *tried = BOOT.valid ? BOOT.tried : 0u;
    return BOOT.valid ? BOOT.seq : 0u;
}
#endif
#endif
