/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* OTA installer core ( and 7): verify a downloaded MSCF code file, then put it on the memory card so that a power cut or a bad
 * write at ANY point leaves a card the loader can still boot.  Freestanding C89, no heap, no libc; all card access goes through ota_card_ops so the same code
 * runs on the console (CARD SDK wrappers) and on the host (fake card with fault injection, ota/tests).  Nothing here is wired into an engine image yet. */
#ifndef OTA_CORE_H
#define OTA_CORE_H
#include "../cl/mscf_verify.h"

typedef unsigned int ota_u32;     /* exactly 32 bits (mw2_u32 is "at least 32") */

#define OTA_BLOCK 0x2000u

#define OTA_OK        0
#define OTA_E_HDR     3     /* same numbers as the loader's E_* where they exist (cl/mscf.h) */
#define OTA_E_SEQ     11    /* build_seq not above the installed one / below the floor */
#define OTA_E_SIG     12
#define OTA_E_ABI     13
#define OTA_E_RANGE   4
#define OTA_E_CRC     6
#define OTA_E_DECODE  14    /* compressed file: the stream is not a well-formed MSLZ stream of image_len bytes, or the hole is too small for the in-place decode */
#define OTA_E_SPACE   20    /* not enough free blocks; the card is as it was (the previous-version copy may be gone, the active file is intact) */
#define OTA_E_IO      21    /* a card call failed; active file intact */
#define OTA_E_VERIFY  22    /* read-back differed twice; staging file removed, active file intact */
#define OTA_E_HALF    24    /* BUNDLE plan B only: the new overlay is on the card, the engine could not be written and the old overlay could not be put back (the pair does not match until the engine is installed: retry) */
#define OTA_E_LOST    23    /* IN-PLACE install only: the old active file was already deleted when the write failed.  The card has NO engine file now (the game starts as vanilla Melee) */

/* card-size tiers: which install ota_plan picks */
#define OTA_TIER_STAGED  1  /* room for a second copy: staged swap, the old file kept as MultishineCodeB; every cut boots */
#define OTA_TIER_INPLACE 2  /* room only if the old file goes first: delete, then write; NO fallback; a cut or write error leaves a card without an engine (re-seed needed) */
#define OTA_TIER_NOROOM  3  /* not even in place */

struct ota_card_ops {
    void* ctx;
    /* each returns 0 on success.  create makes a file of nblocks whole blocks WITH its card status (comment/icon offsets) set; names are the card file names. */
    int (*create)(void* ctx, const char* name, ota_u32 nblocks);       /* returns 1 when there is no room, 2 when the name exists, other nonzero = error */
    int (*write)(void* ctx, const char* name, ota_u32 off, const mw2_u8* buf, ota_u32 n);     /* off, n multiples of OTA_BLOCK */
    int (*read)(void* ctx, const char* name, ota_u32 off, mw2_u8* buf, ota_u32 n);
    int (*del)(void* ctx, const char* name);                            /* 0 also when the file does not exist */
    int (*rename)(void* ctx, const char* from, const char* to);        /* the target must not exist */
    int (*exists)(void* ctx, const char* name);                         /* 1 / 0 */
    /* OPTIONAL (0 = unknown, the staged install is tried as before): whole blocks that would be free once the files `previous` and `staging` are deleted, and `active` as well when
       it is not 0; counts what Melee's own save still needs; -1 = cannot tell.  Only ota_plan calls it. */
    int (*free_blocks)(void* ctx, const char* previous, const char* staging, const char* active);
};

struct ota_policy {
    const mw2_u8 (*pubs)[32];   /* pinned Ed25519 public keys */
    unsigned npub;
    ota_u32 abi;                /* ABI the running loader supports (the file must match exactly) */
    ota_u32 min_seq;            /* the new file must have build_seq >= min_seq (the installed build_seq + 1, or the floor if higher) */
    int allow_unsigned;         /* dev loaders only */
};

/* Parse and verify a code file given as card data (block 0 with the header at +0x1000, then the payload at +0x2000), n = bytes (a multiple of OTA_BLOCK).
 * Same checks, same order as the loader: magic/version, ABI, build_seq, counts + header CRC, ranges, image CRC, signature.  *seq = its build_seq.
 * A COMPRESSED file (MSCF v2, F_COMP) is checked the way the verifying loader checks it: stream blocks exactly fill the file, stream CRC, signature over the STORED
 * stream (message tag MSCFSIGz1); only after the signature passed is the stream walked (no output is written, no RAM beyond the file) to confirm it is a well-formed
 * MSLZ stream of exactly image_len bytes and that the in-place decode margin (hole_size - stream blocks >= the walk's needed gap) holds, else OTA_E_DECODE. */
int ota_verify(const mw2_u8* file, ota_u32 n, const struct ota_policy* pol, ota_u32* seq);

/* names: active (MultishineCode), previous (MultishineCodeB), staging (MultishineCodeN).  scratch = one OTA_BLOCK buffer for the read-back.
 * Order of card changes (cut-safe, section 6): verify in RAM; delete previous; delete stale staging; create staging; write; read back and compare (one retry);
 * rename active -> previous; rename staging -> active.  Returns OTA_OK (a reset applies it) or an error; on every error the active file is untouched. */
int ota_install(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                const mw2_u8* file, ota_u32 n, const struct ota_policy* pol, mw2_u8* scratch);
/* The card half of ota_install for a caller that has ALREADY run ota_verify on this very buffer (the Update screen verifies in its own state, so the Ed25519 and
 * SHA-512 work is not done twice on the console).  Never call it on a buffer ota_verify has not accepted: it writes what it is given. */
int ota_install_verified(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                         const mw2_u8* file, ota_u32 n, mw2_u8* scratch);
/* Same-build REINSTALL (the verified file's build_seq equals the running one): the fallback MultishineCodeB is left UNTOUCHED when it exists, so a reinstall made because the current
 * file is suspect never replaces the real older fallback with a copy of the suspect build.  Order: delete stale staging; create staging, write, read back (one retry); then delete the
 * active file and rename staging -> active.  A power cut after the delete boots the fallback (older build); before it, the old active file.  If there is no room for a staging copy next
 * to active + fallback, the active file is rewritten in place (delete, create, write, read back): the fallback still boots at every cut.  Without a MultishineCodeB there is nothing to
 * protect: the normal install (old file becomes the fallback), or in place when that has no room.  Only call it on a buffer ota_verify accepted. */
int ota_reinstall_verified(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                           const mw2_u8* file, ota_u32 n, mw2_u8* scratch);

/* Which install fits this card for a file of n bytes (a multiple of OTA_BLOCK): OTA_TIER_STAGED when previous and staging deleted leave room for the file next to the active
 * one, else OTA_TIER_INPLACE when the file fits once the active one is gone too, else OTA_TIER_NOROOM.  Without ops->free_blocks: OTA_TIER_STAGED. */
int ota_plan(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging, ota_u32 n);
/* The small-card install (tier OTA_TIER_INPLACE): delete previous, staging and the ACTIVE file, create the active name, write, read back (one retry).  No second copy exists at any
 * moment, so a power cut after the first delete, or a failed write, leaves a card without a code file: the game boots as vanilla Melee and the engine has to be put back
 * (the patched bootstrap over the network, or a hand copy).  Returns OTA_OK, or OTA_E_LOST once the old file is gone, or OTA_E_IO if the very first delete failed (card unchanged).
 * Only call it on a buffer ota_verify accepted. */
int ota_install_inplace_verified(const struct ota_card_ops* ops, const char* active, const char* previous, const char* staging,
                                 const mw2_u8* file, ota_u32 n, mw2_u8* scratch);

/* BUNDLE (engine MultishineCode + overlay MultishineOvl as ONE release).  Two files that only work as a matching pair must never be left
 * half installed by a lack of room.  ota_bundle_plan looks at the card BEFORE anything is written:
 *   OTA_PLAN_A  both new files fit as staging copies beside the old ones (free blocks after dropping the old spare copies >= new engine + new overlay): stage both, verify both, then the
 *               swap is four directory operations (milliseconds, no data written): the old files become the spare copies.  Every failure before the swap leaves the old pair; a failure
 *               inside the swap puts the first rename back.  This is exactly the install of two staged files in a row, for cards with room (the 2043 block card).
 *   OTA_PLAN_B  small card (A does not fit, but overlay first then engine does: free >= new overlay and free - new overlay + old overlay >= new engine): the old spare copies are deleted first,
 *               the overlay is staged and replaces the old one (NO MultishineOvlB: the old overlay's blocks are the room for the engine), then the engine is staged and swapped in with the
 *               old engine kept as MultishineCodeB (those blocks are already used).  Between the overlay swap and the engine swap the card holds new overlay + old engine: that window is the
 *               engine write (seconds).  A write error with the power on is repaired: the old overlay is written back from `restore` (a RAM copy the caller read before: restore_cap bytes,
 *               0 = none) and the old pair is whole again; without a restore copy the result is OTA_E_HALF and the caller retries the engine alone.  A power cut inside that window leaves the
 *               new overlay with the old engine (the update screen cannot start then): the one case a small card cannot avoid, because the old and the new pair do not fit together.
 *   OTA_PLAN_C  not even B: the caller refuses, nothing is written; shortfall = how many more blocks must be free.
 * Without ops->free_blocks the plan is A (the install reports OTA_E_SPACE itself, the old pair untouched, as the single file installs do). */
#define OTA_PLAN_A 1
#define OTA_PLAN_B 2
#define OTA_PLAN_C 3

struct ota_bundle {
    const char *code, *code_prev, *code_stage;      /* MultishineCode, B, N */
    const char *ovl, *ovl_prev, *ovl_stage;         /* MultishineOvl, B, N */
    ota_u32 code_n, ovl_n;                          /* bytes of the new verified files (multiples of OTA_BLOCK) */
    int keep_code_prev;                             /* 1: MultishineCodeB is never touched (the running engine IS the fallback, or a same-build reinstall) */
};
struct ota_bplan {
    ota_u32 shortfall;                              /* plan C: blocks that must still be freed */
    ota_u32 free_blocks;                            /* free blocks once the old spare and stale staging files are gone */
    ota_u32 ovl_old;                                /* blocks of the installed overlay (the size of the restore copy plan B wants) */
};
int ota_bundle_plan(const struct ota_card_ops* ops, const struct ota_bundle* b, struct ota_bplan* out);
/* Both buffers must already be accepted by ota_verify.  `plan` = the value ota_bundle_plan returned (A or B); scratch = one OTA_BLOCK.  Returns OTA_OK, an error with the old pair whole,
   or OTA_E_HALF (see plan B). */
int ota_bundle_install(const struct ota_card_ops* ops, const struct ota_bundle* b, int plan, const mw2_u8* code_file, const mw2_u8* ovl_file,
                       mw2_u8* scratch, mw2_u8* restore, ota_u32 restore_cap);

/* BOOT RECORD: the automatic rollback of a signed build that crashes or hangs at start.  Freestanding C89, no heap, all card access through
 * ota_card_ops (the same ops the installer uses), so the code is host-tested against a fake card and runs on the console over upd_card.c.
 *
 * The record lives in the optional state file (MultishineState), block offset 0x1000 + 16:   'MSBT' | candidate build_seq u32 | tries u16 | good u16 | CRC-32 of those 12 bytes
 * (the floor record 'MSST' at +0 of the same area is never touched here).  Three writers, one reader:
 *   updater  bootrec_arm    after a successful install of build S:               candidate = S, tries = 0, good = 0
 *   engine   bootrec_start  at its start (first card write; only while not good): tries + 1 (a record for an older build or no record: candidate = own build, tries = 1)
 *   engine   bootrec_good   when the Main Menu is reached:                       tries = 0, good = 1     (no write when already good: a steady-state boot writes nothing)
 *   LOADER   (cl_loader.c, read only)  a file whose build_seq == candidate with tries >= CL_TRIES and not good is refused (E_TRIED) and the previous file loads.
 * An engine whose own build_seq is BELOW the candidate (it runs as the fallback of a build that failed) touches nothing; so does an engine without this code.
 * Without a fallback file (MultishineCodeB absent: the small-card in-place tier) the engine does not count at all: a refusal would leave vanilla Melee, which is worse
 * than the build it protects.  Every function returns BR_OK or a BR_* error and leaves the card consistent: the record is one 8 KiB block write with a read-back. */

#define BR_OFF       0x1000u      /* the record area inside block 0 of the state file (same offset as the floor record) */
#define BR_REC       16u          /* the boot record starts at BR_OFF + 16 */
#define BR_TRIES     3u           /* the loader's default CL_TRIES: tries at which a candidate is refused (informational for tests and the engine) */

#define BR_OK        0
#define BR_SKIP      1            /* nothing to do (already good, a fallback run, no fallback file, or a newer candidate): no card write */
#define BR_NOROOM    2            /* the state file is absent and could not be created: no counter (the L+R safe mode still works) */
#define BR_IO        3            /* a card call failed; the previous record is intact or unreadable (= no record) */

struct bootrec {
    ota_u32 cand;                 /* candidate build_seq */
    ota_u32 tries;                /* starts counted */
    ota_u32 good;                 /* 1 = reached the Main Menu */
    int valid;                    /* the record was present with a good CRC */
};

/* Reads and parses the record.  blk = one OTA_BLOCK buffer.  Returns BR_OK (r filled, r->valid says whether a record was there; the file may exist without one), BR_NOROOM when the
   state file does not exist (r->valid = 0), BR_IO on a read error. */
int bootrec_read(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, struct bootrec* r);
/* The loader's rule, for tests and the engine: 1 when a file with this build_seq would be refused. */
int bootrec_exhausted(const struct bootrec* r, ota_u32 seq, ota_u32 limit);

int bootrec_arm(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, ota_u32 seq);
int bootrec_start(const struct ota_card_ops* ops, const char* state, const char* previous, mw2_u8* blk, ota_u32 seq);
int bootrec_good(const struct ota_card_ops* ops, const char* state, mw2_u8* blk, ota_u32 seq);
#endif
