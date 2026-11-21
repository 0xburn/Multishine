/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_host: the resident image's side of the Update row (/ 9.3): fills `struct sgu_host` (sgu_core.h) from what the resident has, so that
 * sgo_env.upd / mlo_env.upd can point at it.  Nothing here draws or decides policy; it only turns resident facts into the host contract:
 *   - installed build_seq, floor, card slot and "running from the fallback copy" from the LOADER'S STATUS BLOCK (cl/mscf.h CL_STATUS: word 0 magic 'CL' 0001, word 1 = 7 when
 *     the load finished, word 2 = E_OK, 3 = slot, 16 bit 0 = verifying loader, 17 = floor from the state file, 18 = build_seq, 20 = 1 when the PREVIOUS file was loaded);
 *   - pinned release keys: the loader's own list or an explicit list (host tests, dev builds);
 *   - the CARD ops (upd_card.c), the network thunks, heap and reset supplied by the glue.
 * Safety rules decided here (all host-tested, tests/upd_host_test.c):
 *   - If the status block is not a finished, successful load of a verifying loader, installed_seq is 0 and card_ready is 0: the screen then says "No usable memory card found"
 *     and installs nothing.  We never install over a build we cannot identify.
 *   - If the loader ran the PREVIOUS file (the active one was refused, skipped by the L+R safe mode, or exhausted by the boot record: automatic rollback),
 *     the host says from_prev: the installer then keeps the fallback and replaces the ACTIVE file (stage, delete the active file, rename), so every power cut still boots.
 *
 *   - reset unmounts the card first (upc_done), then calls the glue's reset (the retail soft-reset path); it does not return on a console. */
#ifndef UPD_HOST_H
#define UPD_HOST_H
#include "../sign/sgu_core.h"

struct upd_cfg {
    mnc *net;                                   /* the resident hub client (its io is the BBA UDP socket) */
    int (*hello)(unsigned now_ms);
    int (*poll)(unsigned now_ms);
    int (*link_up)(void);
    void *(*alloc)(unsigned n);                 /* OSAllocFromHeap(HSD_GetHeap, n): 0 when it does not fit, never halts */
    void (*free)(void *p);
    void (*reset)(void);                        /* the retail soft-reset path; does not return on a console */
    const volatile unsigned *cl_status;         /* CL_STATUS of this card layout (card_payload.py patches it), or 0: nothing is installable */
    const mw2_u8 (*pubs)[32];                   /* pinned keys, or 0 with UPD_CL_KEYS */
    unsigned npub;
    int allow_unsigned;                         /* the dev loader only (CL_DEV) */
    unsigned abi;                               /* MSCF_ABI */
    const struct sgu_adv *adv;                  /* the launch check's record, or 0 */
    unsigned char *(*wa_acquire)(unsigned owner);   /* the resident's shared CARD work area (ms_card_wa_acquire) and its release; or 0 in the host tests (they model the area) */
    void (*wa_release)(void);
    int at_entry;                               /* the caller runs inside the loader's entry call (status state 6, not yet 7): accept that block (the early rollback counter of the Dolphin test blob) */
    int (*bak_on)(void);                        /* or 0 = on: the player's switch "Backup to slot B" (Update screen; 1 on, the default); only read by builds with UPD_BAK */
    const struct sgu_disc *disc;                /* or 0: the player's disc (path size / read), for the system files (sgu_core.h); 0 = they cannot be set up here */
    int (*keep_on)(void);                       /* or 0 = on: the player's switch "Keep previous build as backup" (1 keep, the default; 0 = delete the B copies after a verified install) */
    unsigned *pending;                          /* a resident word for the pending-restart build_seq, or 0 = a static of this module (host tests) */
};

struct upd_boot {                               /* what the loader's status block says */
    unsigned valid;                             /* 1: a finished, successful load by a verifying loader */
    unsigned seq, floor_seq, chan, from_prev, dev, tried;     /* tried: status word 23, the build the boot record exhausted (0 = none) */
};
void upd_boot_read(const volatile unsigned *cl_status, struct upd_boot *b);
/* the host for the Update row; the returned pointer and the pending word are static (the resident keeps them for the whole session).  Calling it again re-reads the status. */
const struct sgu_host *upd_host_init(const struct upd_cfg *cfg);
const struct upd_boot *upd_host_boot(void);
/* boot record, engine side (ota_core.h BOOT RECORD).  upd_boot_note(0) counts this start, upd_boot_note(1) marks the Main Menu reached; 0 = done / nothing to do, 1 = try again.
   upd_boot_tick does the retries: call it every frame from the earliest resident frame that may use the card, and with main_menu = 1 from the Main Menu scene. */
int upd_boot_note(int good);
void upd_boot_tick(unsigned now_ms, int main_menu);
unsigned upd_host_pending(void);
#endif
