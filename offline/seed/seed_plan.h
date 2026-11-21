/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* seed_plan: the card half of the earlier image.  Pure C (no libc, no heap, no Melee address): what is on the card, what the server's verified files are,
 * what to do with each file (the per-file decision the screen shows), and the cut-safe apply over ota_card_ops.  The same file runs on the console (upd_card.c wrappers) and
 * in the host tests (a model of the CARD SDK behind the same wrappers, or a fake card with fault injection).
 *
 * Files and rules ("per-file rule"):
 *   MultishineCode (engine, ABI 1) and MultishineOvl (overlay, ABI 2)  from the server, signature verified with the pinned keys.  Decided file by file on build_seq, but a PAIR for
 *       writing: nothing is written unless BOTH server files were downloaded and verified (never half a pair), and when either file on the card is NEWER than the server's, no signed
 *       file is touched at all (a card that is ahead is never moved back, and a missing member is not filled with an older build).
 *       card newer: keep; equal: keep (verified); older: update (staged install, the old file becomes the B copy); missing: write; damaged / unsigned / wrong key: replace.
 *   MultishineMelee (THE standard Melee save, tools/card/standard_save.py, from the disc file /ms/melee.gci "if no save is there we make it, if one exists we delete and create
 *       our own")  written whether absent or present (present = replaced), under the staging name MultishineMeleeN, read back, then the old file is deleted and the name taken by a rename.
 *       Only when the two signed files are on the card afterwards; written BEFORE the loader save (the loader save is the commit step).
 *   MultishinePack (the Slippi data pack, built ON THE CONSOLE from the disc by seed_slp.c, 29 blocks; only on a disc that carries the Slippi files, in.pack_blocks)  written after the Melee save, before the loader
 *       save, staged name MultishinePackN + rename like the saves; a valid pack on the card (whoever made it) is kept, a damaged one replaced; a card that is set up apart from the pack (sp_pack_only) gets only the pack.
 *   SuperSmashBros0110290334 (the loader save, our art, from the disc file /ms/loader.gci)  written LAST, under the staging name MultishineLoaderN and renamed, and only when the two
 *       signed files are on the card afterwards.  A Melee save under this name (a player's save from before) is REPLACED (deleted at the commit, no rename-keep); another size is refused.
 *   Everything else (MultishineIdentity, Online, Options, Codes, State, Bridge, Pack, Melee's other files, non-Multishine files) is never read, written or deleted here.
 */
#ifndef SEED_PLAN_H
#define SEED_PLAN_H
#include "../cardloader/ota/ota_core.h"

#define SP_ENG 0
#define SP_OVL 1
#define SP_LOADER 2
#define SP_MELEE 3
#define SP_PACK 4
#define SP_NFILES 5
/* the order of the apply: the standard Melee save and the Slippi data pack BEFORE the loader save (the loader save is the commit step) */
static const unsigned char SP_ORDER[SP_NFILES] = { SP_ENG, SP_OVL, SP_MELEE, SP_PACK, SP_LOADER };

#define SP_ABI_ENG 1u
#define SP_ABI_OVL 2u
#define SP_LOADER_NAME "SuperSmashBros0110290334"
#define SP_LOADER_STAGE "MultishineLoaderN"
#define SP_MELEE_NAME "MultishineMelee"
#define SP_MELEE_STAGE "MultishineMeleeN"
#define SP_PACK_NAME "MultishinePack"
#define SP_PACK_STAGE "MultishinePackN"
#define SP_GCI_HDR 0x40u

/* decisions (the word the screen shows) */
enum {
    SD_NONE = 0,
    SD_WRITE,        /* missing: written */
    SD_UPDATE,       /* older signed file on the card: staged install, the old file becomes the B copy */
    SD_REPAIR,       /* damaged / unsigned / wrong key: replaced */
    SD_KEEP_SAME,    /* same build, verified: kept */
    SD_KEEP_NEWER,   /* newer than the server's: kept */
    SD_HOLD,         /* the other file of the pair is newer on the card: left alone */
    SD_KEEP_OURS,    /* loader save already on the card and identical */
    SD_FOREIGN,      /* the loader name holds something we cannot classify (not 11 blocks): not touched */
    SD_NOROOM,       /* not enough free blocks */
    SD_REPLACE,      /* a Melee save is on the card under the name (loader name: a player's save; MultishineMelee): replaced by ours (staged, deleted at the commit) */
    SD_BLOCKED,      /* not written because the pair / the files before it are incomplete */
    SD_KEEP_OLD      /* pack-only run on a set-up card: an older valid engine / overlay is kept (the game's own updater moves it forward; the system-files builder never stages a second copy) */
};

/* why the plan blocks everything (sp_out.why) */
enum { SW_NONE = 0, SW_SERVER = 1, SW_NOROOM = 2, SW_FOREIGN = 3, SW_NEWER = 4, SW_NOLOADER = 5, SW_SMALL = 6 };   /* 7 was SW_TWO (two Melee saves): gone, both are replaced */
#define SP_MIN_CARD 251            /* usable blocks: smaller cards (59, 123) are rejected outright */

struct sp_card {
    unsigned char present, valid;     /* valid: passed ota_verify under the pinned keys */
    ota_u32 seq, blocks;
};

struct sp_in {
    struct sp_card c[2];              /* SP_ENG, SP_OVL on the card */
    int have[2];                      /* the server's file was downloaded and verified */
    ota_u32 seq[2], blocks[2];        /* its build_seq / size in blocks */
    int loader_state;                 /* SL_*: what sits under the loader name */
    int melee_have;                   /* blocks of the MultishineMelee already on the card (0 = absent): replaced */
    ota_u32 melee_blocks;             /* blocks of the disc's standard Melee save (0 = the disc has none) */
    int own;                          /* OWN-FILE design: no loader save is written or judged (the game makes its own file MultishineMelee); only the signed pair is planned */
    ota_u32 loader_blocks;            /* blocks of the disc's loader save (0 = the disc has none) */
    ota_u32 pack_blocks;              /* blocks of the Slippi data pack the system-files builder builds from the disc (seed_slp.h: SLP_PACK_BLOCKS; 0 = the disc carries no Slippi files: the pack is not part of the set-up) */
    int pack_have;                    /* blocks of the MultishinePack already on the card (0 = absent) */
    int pack_valid;                   /* that file passes sp_scan_pack (header, table CRC, every entry's stored CRC): kept; else replaced */
    int melee_keep;                   /* the card is set up apart from the pack (sp_ready_nopack): the Melee save already there is the player's, it is KEPT (not rewritten) and only the pack is added */
    int free_blocks;                  /* free blocks as the card counts them (no reserve), -1 = unknown */
    int card_blocks;                  /* usable blocks of the card, -1 = unknown */
    int other_blocks;                 /* blocks taken by files that are not ours (never deleted), -1 = unknown */
};

struct sp_out {
    unsigned char act[SP_NFILES];
    unsigned char why;
    ota_u32 need;                     /* blocks the plan needs FREE: what it writes minus the blocks of the saves it replaces */
    ota_u32 gross;                    /* blocks the plan writes (the progress bar's total) */
    int have_free;
    unsigned char changes;            /* number of files that will be written */
    unsigned char verdict;            /* 0 fits with room for later updates, 1 fits but updates will need more space (B copies / staging), 2 does not fit / card too small */
    ota_u32 missing;                  /* verdict 2: blocks short */
    ota_u32 headroom;                 /* blocks an engine + overlay update needs on top (the staging copy) */
    unsigned char complete;           /* after a successful apply the card holds both signed files and our loader save */
};

void sp_decide(const struct sp_in *in, struct sp_out *o);
const char *sp_act_text(unsigned act);          /* "write (new)" ... */

/* ---- card access: ota_card_ops plus the two calls the system-files builder needs (upc_file_blocks, upc_create_gci) ---- */
struct seed_ops {
    const struct ota_card_ops *ota;
    unsigned (*blocks)(const char *name);                                           /* size in blocks of one of our files, 0 = absent */
    int (*create_gci)(const char *name, ota_u32 nblocks, const mw2_u8 *hdr);       /* create with the card status of a .gci header: 0 ok, 1 no room, 2 exists, other = error */
    int (*free_raw)(void);                                                          /* free blocks, no reserve; -1 unknown */
    unsigned (*total)(void);                                                        /* usable blocks of the card, 0 unknown */
};
/* a copy of src with an ota free_blocks that counts raw free + the named files (the stock one holds back 12 blocks for Melee's save, which the loader save IS here) */
void sp_wrap(struct seed_ops *dst, struct ota_card_ops *odst, const struct seed_ops *src);
/* blocks used by files that are not ours (total - free - ours), -1 unknown */
int sp_other_blocks(const struct seed_ops *c);

struct sp_ctx {
    const struct seed_ops *card;
    const mw2_u8 (*pubs)[32];
    unsigned npub;
    int allow_unsigned;               /* dev builds only */
    mw2_u8 *scratch;                  /* one OTA_BLOCK, 32-byte aligned on the console */
    mw2_u8 *buf;                      /* scan buffer */
    ota_u32 cap;                      /* bytes in buf */
    const mw2_u8 *lhdr;               /* the .gci header (0x40 bytes) and the data (loader_blocks * OTA_BLOCK bytes, 32-byte aligned) */
    const mw2_u8 *ldata;
    ota_u32 lblocks;
    const mw2_u8 *mhdr, *mdata;       /* the same for the standard Melee save (/ms/melee.gci) */
    ota_u32 mblocks;
    int own;                          /* OWN-FILE design: ready = engine file + overlay file */
    ota_u32 want_pack;                /* the disc carries the Slippi files: a card without MultishinePack is not ready */
    const mw2_u8 *phdr, *pdata;       /* the built pack (seed_slp.h: slp_gci_hdr / slp_gci_data), 0x40 header + pblocks blocks, 32-byte aligned data; set once the build is done */
    ota_u32 pblocks;
};

/* Deletes the staging files of an interrupted earlier run (ours only: MultishineCodeN, MultishineOvlN, MultishineLoaderN) so that they do not count against the room.  0 ok. */
int sp_clean_stale(const struct sp_ctx *x);
/* Reads and verifies one signed file on the card (SP_ENG / SP_OVL).  A file the buffer cannot hold counts as damaged. */
void sp_scan_signed(const struct sp_ctx *x, int kind, struct sp_card *out);
/* 1 when the card is ALREADY SET UP, decided without the network: an engine file (MultishineCode, any build) and an overlay file (MultishineOvl) exist and a file of the loader's block count sits under
   the loader name, WHATEVER its art or payload (our working cards carry older banners; Melee repairs a header that differs from the disc's art by itself at boot), and MultishineMelee exists. Reads nothing. */
int sp_ready(const struct sp_ctx *x);
extern unsigned sp_ready_dbg;   /* last sp_ready outcome: 1 Code missing, 2 Ovl missing, 3 no loader data, 0x400|n<<16 loader blocks n differ, 5 read failed, 6|i<<8 art differs at i, 7 ready */
#define SP_DIST_MAX 512u          /* decoded bytes of difference up to which a file with our art is still our loader save (see sp_dist in seed_plan.c) */
enum { SL_ABSENT = 0, SL_OURS = 1, SL_MELEE = 2, SL_OTHER = 3 };
/* What sits under the loader name: SL_OURS = our loader save (our banner/icon/palette AND the decoded sectors within SP_DIST_MAX bytes of the disc's file: Melee rewrites a few bytes at boot, and a Melee
   save made on this disc has our art too), SL_MELEE = a file of the loader's block count with other art = the player's Melee save (never judged by its content: pristine, played, damaged, an old loader: all REPLACED),
   SL_OTHER = any other size. */
int sp_scan_loader(const struct sp_ctx *x);
/* 1 when the MultishinePack on the card is a whole, undamaged pack file (the format of offline/multishine/menus/df/df.h: 'MSPK' header, table CRC-32, every entry's stored bytes against p_old), reading every block; 0 when absent or damaged.  Any valid pack is kept, whoever made it. */
int sp_scan_pack(const struct sp_ctx *x);
/* 1 when the card was set up COMPLETELY by an earlier system-files builder and only the pack is missing: sp_ready as if the disc carried no Slippi files AND our loader save under its name (the loader save is
   written last, so this is no half-run card).  Such a card gets the pack and nothing else: no question, the Melee save is the player's and is not touched. */
int sp_pack_only(const struct sp_ctx *x);

/* One step of the apply, in the order SP_ORDER (SP_ENG, SP_OVL, SP_MELEE, SP_LOADER).  file/n = the verified server file (SP_ENG / SP_OVL; SP_MELEE and SP_LOADER come from the disc files in the ctx; the caller ran ota_verify on exactly this buffer).
   Returns OTA_OK or an OTA_E_* code (OTA_E_SPACE when the staged install does not fit: the system-files builder never installs in place).  On every error the card is as it was, apart from the
   documented states of ota_core.h; the Melee and loader saves only ever appear by their final rename. */
int sp_apply(const struct sp_ctx *x, int kind, unsigned act, const mw2_u8 *file, ota_u32 n);

const char *sp_name(int kind, int which);        /* which: 0 active, 1 previous, 2 staging */
#endif
