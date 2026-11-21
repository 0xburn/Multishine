/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_card: the resident host's memory-card layer for the OTA installer ( and 9.3): a `struct ota_card_ops` over the retail CARD SDK
 * (create with its card status, write, read, delete, rename, exists) for the three files MultishineCode / MultishineCodeB / MultishineCodeN, in the card slot the loader
 * loaded the code from.  Called only from the Update screen's install step (sgu_core.c), in the menu scene, on the main thread; never from a match, never at boot.
 *
 * Design rules (what a failure may and may not do):
 *   - ONE mount for the whole install: the first operation probes and mounts the slot (CARD work area from the shared area of on_hist.c: ms_card_wa_acquire(UPC_OWNER)),
 *     upc_done unmounts and gives the area back; upc_done is idempotent and the host calls it after EVERY install attempt, success or not.
 *   - Every operation closes the file it opened before it returns (the SDK refuses to delete or rename an open file).
 *   - Nothing here decides what to delete: the order of the card operations is ota_core.c's (host-tested cut sweep: every cut leaves a card the loader boots).  This layer
 *     only maps SDK results: create -> 1 no room (INSSPACE / NOENT), 2 name exists; delete of a missing file -> 0; everything else nonzero = failed, with the SDK result kept in upc_st.
 *   - Files are only seen and made with the disc's game code and maker (GALE / 01): that is what CARDCreate stamps and what CARDOpen / CARDRename match, and it is what
 *     the loader's MultishineCode file carries (offline/provision GAME_CODE / MAKER).  A file with another code is invisible to the SDK (exists -> 0, rename -> NOFILE).
 * UNPROVEN ON A CONSOLE: CARDDeleteAsync, CARDRenameAsync (retail addresses symbols.txt, never called by this project's images before), and the whole
 * sequence on a real card.  The host test (tests/upd_card_test.c) drives this code against a model of the SDK with fault injection; see  9.3 for the console steps. */
#ifndef UPD_CARD_H
#define UPD_CARD_H
#include "../../../cardloader/ota/ota_core.h"

#define UPC_OWNER 3u                      /* ms_card_wa_acquire owner id of this layer (1 on_card, 2 sbr_card) */

struct upc_status {                       /* RAM proof block */
    unsigned int magic;                   /* 'UPCD' */
    unsigned int mounts, ops, fails;      /* mounts done, SDK-level operations tried, operations that failed */
    unsigned int last_op, last_res;       /* UPC_OP_* and the SDK result of the last failure */
    unsigned int chan;                    /* slot in use (0 = A, 1 = B) */
    unsigned int refused;                 /* upc_ready / mount refusals: Melee's own card layer busy, area held, no card */
    /* Melee save art proof (a RAM scan for art_magic finds it; read after Settings > Update): */
    unsigned int art_magic;               /* 'UPAR' */
    unsigned int art_res;                 /* last upc_art_repair result (signed, as unsigned: -1 = 0xFFFFFFFF, -2 = 0xFFFFFFFE, -3 = 0xFFFFFFFD; meaningful when art_runs > 0) */
    unsigned int art_why;                 /* UPA_W_*: the guard that refused last (0 = none), see upd_card.c */
    unsigned int art_runs, art_ok;        /* passes started, passes that put the art in RAM (bit0 of the result) */
    unsigned int art_files;               /* passes that wrote block 0 of MultishineMelee */
    unsigned int art_icon;                /* icon_data pointer value the last pass saw (0 = none), for a wrong-layout report */
};
extern struct upc_status upc_st;
enum { UPC_OP_NONE = 0, UPC_OP_MOUNT, UPC_OP_CREATE, UPC_OP_STATUS, UPC_OP_WRITE, UPC_OP_READ, UPC_OP_DELETE, UPC_OP_RENAME, UPC_OP_EXISTS };

void upc_set_wa(unsigned char *(*acquire)(unsigned owner), void (*release)(void));   /* the resident's CARD work area calls (non-host builds; the overlay cannot import them) */
void upc_set_chan(int chan);              /* the slot the loader read MultishineCode from (loader status block word 3) */
int upc_ready(void);                      /* 1: a formatted 8 KiB-sector card is in that slot and Melee's own card layer is idle (nothing is mounted or changed) */
const struct ota_card_ops *upc_ops(void);
/* The 8 KiB block buffer behind the CARD work area (the shared area is 0xA000 + 0x2000 bytes): valid while a mount is held (after the first operation, until upc_done).  32-byte
   aligned, usable as the buffer of a one-block read or write. 0 when nothing is mounted. */
unsigned char *upc_block(void);
/* May a file of `nblocks` be CREATED in this slot without taking the room Melee's own save needs? The rule om_card.c / keep: on slot A, when
   the save "SuperSmashBros0110290334" does not exist yet, 12 blocks and 2 directory entries must stay free after ours.  Mounts if needed. */
int upc_room_ok(unsigned nblocks);
/* earlier image: the size in blocks of one of our files (0 = absent); create with the card status copied from a.gci directory entry (0 ok, 1 no room, 2 exists, 3 failed) */
int upc_icon_repair(void);              /* give the engine-made files (Options, Codes, guest Online, Diag) the Code file's icon; count repaired, -1 = card not usable (overlay builds only) */
#ifdef UPC_ART
int upc_art_repair(void);               /* Melee save art (RAM buffers + header of MultishineMelee), see upd_card.c; bit0 RAM, bit1 file written, bit2 file write failed (retry), -1 card not usable / busy, -2 a guard that will not change failed, -3 Melee turned busy (retry), 0 no such file yet (retry) (overlay builds only) */
#endif
unsigned upc_file_blocks(const char *name);
unsigned upc_total_blocks(void);        /* usable blocks of the card (0 = unknown); call after upc_ready or a first operation */
int upc_free_raw(void);                 /* free blocks, no reserve for Melee's save (-1 = cannot tell) */
int upc_create_gci(const char *name, ota_u32 nblocks, const unsigned char *hdr);
/* ---- Backup to slot B.  Slot A (the loader's slot, g_chan 0) is only ever READ here; every write goes to physical slot B (chan 1) through a
   second, independent mount with a work area the caller allocates (0xA000 bytes, 32-byte aligned).  Nothing here can write slot A, and with the card in slot B (chan 1) there is no
   backup (the target would be the source). ---- */
struct upc_ent {                          /* one file of slot A: name and the card status a copy needs */
    char name[33];
    unsigned nblk;
    unsigned banner, icon_addr, icon_fmt, icon_speed, comment_addr;
};
#define UPC_MAXFILES 127
#define UPC_B_WA 0xA000u
int upc_a_entry(int idx, struct upc_ent *e);   /* directory entry idx (0..126) of the source slot: 0 a file of ours, 1 empty / not visible to this disc, 3 error */
int upc_b_probe(unsigned *blocks);             /* slot B, no mount: 0 a formatted 8 KiB-sector card (usable blocks in *blocks), 1 no card (or the source is slot B itself), 2 other sectors, 4 Melee busy */
int upc_b_mount(void *wa);                     /* 0 mounted; nonzero = not formatted / broken / error (the card is NEVER formatted here) */
int upc_b_free(int *blocks, int *files);       /* 0 ok */
unsigned upc_b_file_blocks(const char *name);  /* 0 = absent */
int upc_b_create(const char *name, unsigned nblk, const struct upc_ent *st);      /* 0 ok, 1 no room, 2 exists, 3 failed */
int upc_b_write(const char *name, unsigned off, const unsigned char *buf, unsigned n);
int upc_b_read(const char *name, unsigned off, unsigned char *buf, unsigned n);
int upc_b_delete(const char *name);            /* 0 also when absent */
void upc_b_done(void);                         /* unmount slot B; idempotent */
void upc_done(void);                      /* unmount and release; safe to call at any time, any number of times */
#endif
