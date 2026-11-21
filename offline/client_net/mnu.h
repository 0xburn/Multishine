/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNU_H
#define MNU_H
/*
 * mnu: the console-side client of the server's OTA ops UPD_CHECK / UPD_FETCH.  HOST-TESTED ONLY: it is not linked into
 * any console image.  Same style as mnc.c (C89, no libc, no heap, never blocks, caller-owned buffers) and it rides on an mnc's wire v2 session and io: run
 * mnc_hello (or any mnc operation) first so that m->w is READY, then mnu_check, then mnu_fetch into a caller buffer, driving mnu_poll from the frame loop.
 *
 * Nothing the server sends is trusted for safety: the fetched bytes are a signed MSCF file (ota_core.c ota_verify checks the Ed25519 signature, CRCs and
 * the build_seq / ABI policy before anything is written to the card).  The transport only has to be reliable, which this file provides: a window of
 * MNU_WIN chunk requests in flight, each resent with the SAME wire counter (the server answers a repeated counter from its reply cache) every timeout_ms,
 * MNU_TRIES sends in total.  Replies are accepted only through mw2_open (MAC in a device session, server signature in an anonymous one - an anonymous
 * session costs one Ed25519 verify per chunk, so use the device session for a download), for the request in flight and with the exact body shape.
 *
 * Fast anonymous download (-10-12).  An anonymous session verifies one Ed25519 signature per reply, about 0.24 s each: not usable for 320+
 * chunks.  So the signed UPD_CHECK reply also announces (flags bit 2) that the server answers UPD_FETCHU (0x32): the same chunk request, but in an anonymous session
 * the status-0 reply has NO signature.  mnu then (1) takes size, build_seq and the SHA-256 of the file from the SIGNED check reply, (2) hashes the chunks in order as they
 * arrive (streaming, 104 bytes of state, no extra buffer), (3) goes to DONE only when the hash of the whole stored stream equals the announced one, otherwise FAILED with
 * MNU_E_HASH (the buffer then holds untrusted bytes and must not be installed).  The installer still verifies the Ed25519 file signature itself.  In a device session
 * (MAC per reply, cheap) and against a server without the flag the old signed UPD_FETCH is used; the hash is checked there too.  To use this, call mnu_fetch on the SAME
 * mnu that did the check, without an mnu_init in between (the check result lives in the struct); after mnu_init, mnu_fetch is the old signed, unhashed download.
 * Memory: this struct (about 0.5 KB) plus the caller's file buffer; the engine's free heap at the Main Menu is 4.29 MB (SETTINGS_WIRING_STATUS section 4).
 */
#include "mnc.h"
#include "../payload_pack/ms_sha256.h"

#define MNU_CHUNK 384u              /* data bytes per UPD_FETCH reply: 12 + 10 + 384 + 64 (anonymous signature) = 470 <= 512 */
#define MNU_WIN 4
#define MNU_TRIES 6u                /* a check fails after this many sends; a fetch slot keeps resending past it while the stall budget lasts (below) */
#define MNU_STALL_MS 20000u         /* a fetch fails with MNU_E_TIMEOUT only after this long without ANY valid chunk reply */
#define MNU_BACKOFF_MAX 4u          /* fetch resend interval: timeout_ms x tries, at most x4 (500, 1000, 1500, 2000 ms ...) */
#define MNU_RX_FETCH 8              /* replies handled per poll during a fetch (MNC_RX_BUDGET for the rest): drains a burst after a frame stall before the 3,840 B BBA ring overflows */
#define MNU_MAX_CHUNKS 2048u        /* 786,432 B: more than the largest file planned (the 682,560 B engine image) */
#define MNU_OP_CHECK 0x30
#define MNU_OP_FETCH 0x31

#define MNU_E_NONE 0
#define MNU_E_BUSY 1
#define MNU_E_ARG 2
#define MNU_E_NOSESSION 3           /* the mnc session is not READY (run an mnc operation first) or the server forgot it */
#define MNU_E_TIMEOUT 4
#define MNU_E_SERVER 5              /* status != 0 (NOT_SUPPORTED: no update dir; NOT_FOUND: that build is gone) */
#define MNU_E_BAD_REPLY 6
#define MNU_ST_RATE_LIMITED 3       /* server status: the per-address bucket is empty; an idempotent request is simply asked again later */
#define MNU_E_TOO_BIG 7             /* the file does not fit the caller's buffer or MNU_MAX_CHUNKS */
#define MNU_E_HASH 8                /* every chunk arrived but the SHA-256 of the file is not the one the signed check announced: nothing may be installed from the buffer */
#define MNU_OP_FETCHU 0x32          /* = MW2_OP_UPD_FETCHU */
#define MNU_UF_NEWER 1u             /* UPD_CHECK flags */
#define MNU_UF_ABI 2u
#define MNU_UF_FETCHU 4u            /* the server answers UPD_FETCHU */
#define MNU_UF_ADV 8u               /* the reply carries the advisory record (only when the request asked: want_adv) */
#define MNU_ADV_LEN 16u
#define MNU_MAX_RATE_REPLIES 4096u  /* a download that keeps getting RATE_LIMITED this often fails with MNU_E_TIMEOUT (a forged or stuck server must not hold it for ever) */

enum { MNU_IDLE = 0, MNU_BUSY = 1, MNU_DONE = 2, MNU_FAILED = 3 };

typedef struct mnu {
    mnc *c;
    int state, err, status;
    mnc_u32 timeout_ms;
    /* UPD_CHECK result */
    mnc_u8 flags;                   /* bit 0 newer file available, bit 1 ABI mismatch */
    mnc_u16 srv_abi, blocks;
    mnc_u32 kabi;                   /* the ABI (file kind) of the last check: 1 = the engine's code files, 2 = MultishineOvl; the fetch key carries it in the top byte */
    mnc_u32 latest, size;
    mnc_u8 sha256[32];
    mnc_u8 want_adv;                /* set by the caller (after mnu_init) before mnu_check: also ask for the 16-byte advisory record; cleared by mnu_init */
    mnc_u8 have_adv;                /* the last accepted check reply carried the advisory (the fields below are valid) */
    mnc_u8 adv_state, adv_text;     /* Slippi state (0 verified, 1 unverified-newer, 2 known-broken) and the fixed-text id the server picked (0 = none) */
    mnc_u8 adv_slippi_sup[3], adv_slippi_new[3];    /* the Slippi version this build speaks / the newest the server knows: major, minor, patch */
    mnc_u32 adv_ours_latest, adv_ours_min;          /* newest published Multishine build_seq / the oldest build the server still accepts at matchmaking (0 = no minimum) */
    mnc_u8 have_check;              /* a check reply was accepted: latest/size/sha256/flags are valid (cleared by mnu_init and by starting a check) */
    mnc_u8 prefer_signed;           /* set by the caller (after mnu_init) to force the signed UPD_FETCH, e.g. for a retry after MNU_E_HASH */
    /* fetch */
    mnc_u8 fast;                    /* this download uses UPD_FETCHU (unsigned replies) */
    mnc_u8 hash_on, hash_ok;        /* the file is checked against sha256; hash_ok = it was and it matched */
    mnc_u32 hashed;                 /* chunks already fed to the hash (in order) */
    ms_sha256_ctx hctx;
    mnc_u8 op;
    mnc_u8 *buf;
    mnc_u32 cap, seq, nchunks, next, got;   /* next = next chunk index to request, got = chunks received */
    mnc_u8 have[MNU_MAX_CHUNKS / 8u];
    struct { mnc_u8 used, len, tries; mnc_u32 ctr, chunk, sent_ms; mnc_u8 req[40]; } slot[MNU_WIN];
    mnc_u32 st_sent, st_resent, st_rx, st_bad, st_rate, st_unsigned;     /* st_rate: replies that said RATE_LIMITED (status 3): not fatal, the slot is resent with a new counter after timeout_ms */
    /* robust download */
    mnc_u32 stall_ms;               /* MNU_STALL_MS after mnu_init; the caller may change it */
    mnc_u32 last_ok_ms;             /* time of the last valid chunk reply (or of the fetch / resume start) */
    mnc_u32 st_gap_ms;              /* longest time without a valid chunk reply in this download */
    mnc_u32 resumes;                /* mnu_resume calls that kept the received chunks */
    mnc_u8 resumable;               /* the buffer holds chunks of the download in flight that a resume may keep (cleared by mnu_init, mnu_fetch's reset, a hash failure, mnu_abort) */
    mnc_u8 dsha[32];                /* the SHA-256 this download is checked against (a later check overwrites sha256; a resume needs the same file) */
} mnu;

void mnu_init(mnu *u, mnc *c);
/* 0 = started.  Query: abi = the loader ABI, have = the installed build_seq. */
int mnu_check(mnu *u, mnc_u32 now, mnc_u32 abi, mnc_u32 have);
/* 0 = started.  After a check that reported a file: size bytes of build `seq` into buf (cap >= size).  If u still holds that check's result (no mnu_init since) the
   download is hash-checked, and fast in an anonymous session when the server offers it; DONE means the whole file matched the signed hash. */
int mnu_fetch(mnu *u, mnc_u32 now, mnc_u32 seq, mnc_u32 size, mnc_u8 *buf, mnc_u32 cap);
/* 0 = started.  Continue a download that failed (MNU_E_TIMEOUT / MNU_E_NOSESSION) into the SAME buffer, keeping every chunk already received, typically after a fresh
   handshake and check on this mnu.  Only when the new check (if the download was hash-checked) announces the same file: same seq, size and SHA-256.  Otherwise, or when
   nothing is kept, it is mnu_fetch (a fresh download).  The hash is rebuilt over the kept prefix, so DONE still means the whole file matched. */
int mnu_resume(mnu *u, mnc_u32 now, mnc_u32 seq, mnc_u32 size, mnc_u8 *buf, mnc_u32 cap);
int mnu_poll(mnu *u, mnc_u32 now);          /* returns u->state */
void mnu_abort(mnu *u);
#endif
