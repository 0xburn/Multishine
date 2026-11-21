/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* MSCF: the Multishine code file on the memory card.  Original work.  Mirrored by offline/cardloader/mscf.py.
 *
 * The card file "MultishineCode" is an ordinary GALE/01 save of N whole 0x2000-byte blocks:
 *   block 0   0x0000 comment (2 x 32 B), 0x0040 icon (0x800 B), 0x1000 MSCF header + tables (this file)
 *   block 1.. payload = the memory image of the claimed hole (image_len bytes, zero padded to a whole block)
 * Everything is big-endian.
 */
#ifndef MSCF_H
#define MSCF_H

#define MSCF_MAGIC      0x4D534346u     /* 'MSCF' */
#define MSCF_VERSION    1u              /* header layout */
#define MSCF_VERSION_Z  2u              /* header layout of a compressed file (F_COMP, H_Z_LEN, H_Z_CRC); only a loader built with CL_COMP=1 accepts it, older loaders refuse with E_HDR */
#define MSCF_ABI        1u              /* loader <-> image contract (entry convention, status block) */
#define MSCF_HDR_OFF    0x1000u         /* header offset inside block 0 */
#define MSCF_PAYLOAD_OFF 0x2000u        /* payload offset inside the file */
#define MSCF_BLOCK      0x2000u

/* header (at MSCF_HDR_OFF) */
#define H_MAGIC     0x00u   /* u32 */
#define H_VERSION   0x04u   /* u16 */
#define H_ABI       0x06u   /* u16 */
#define H_SEQ       0x08u   /* u32 build sequence (monotonic; OTA anti-rollback input: enforced by the verifying loader) */
#define H_FLAGS     0x0Cu   /* u32 bit0: claim the hole (lower the heap bounds), bit1: call entry after patching */
#define H_HOLE_BASE 0x10u   /* u32 first byte of the claimed hole = where the payload image is read to (32-byte aligned) */
#define H_HOLE_SIZE 0x14u   /* u32 bytes claimed (>= image_len); the loader's card work area is NOT part of it: it sits directly above the hole, see CL_WA_SIZE */
#define H_IMG_LEN   0x18u   /* u32 payload bytes that matter (CRC covers exactly these) */
#define H_IMG_CRC   0x1Cu   /* u32 CRC-32 (zlib) of the image */
#define H_NSEG      0x20u   /* u32 */
#define H_NPATCH    0x24u   /* u32 */
#define H_ENTRY     0x28u   /* u32 absolute address called as void entry(void *hole_base) when flag bit1 is set, else 0 */
#define H_HDR_CRC   0x2Cu   /* u32 CRC-32 of header bytes 0x00..0x2B and of the tables */
#define H_GUARD_CRC 0x30u   /* u32 CRC-32 over the big-endian words found at the patch addresses BEFORE patching (0 = unchecked): version/region guard without carrying game words */
#define H_GUARD_N   0x34u   /* u32 how many of the first patches (the file-backed ones; .bss targets are listed after them) the guard covers */
#define H_Z_LEN     0x38u   /* u32 (version 2, F_COMP) bytes of the MSLZ stream stored as the payload; the stream is read (whole blocks) to hole_base + hole_size - zpad and decoded in place */
#define H_Z_CRC     0x3Cu   /* u32 (version 2, F_COMP) CRC-32 of the stream; H_IMG_LEN/H_IMG_CRC stay the length/CRC of the DECODED image (what the signature covers) */
#define H_SIG       0x40u   /* 64 B reserved for the Ed25519 signature of OTA stage 0 (zero in v1) */
#define H_SEGS      0x80u   /* nseg * 16: dst u32, src_off u32 (inside the image), len u32, flags u32 (copy image bytes to a fixed address) */
/* then npatch * 8: addr u32, word u32 */
/* H_SIG holds the Ed25519 signature R||S (64 B) over the message built by verify_sig in cl_loader.c; all-zero = unsigned.  Signing and bumping build_seq: tools/card/mscf_sign.py. */

#define F_CLAIM  1u
#define F_ENTRY  2u
#define F_COMP   4u     /* payload is an MSLZ stream */

/* where the loader may put the hole: [CL_HOLE_MIN, CL_HOLE_MAX) (the top of AllM, below the exploit's Gecko list at 0x817F4000 and the FST; the hole's own tail holds the temporary CARD work area) */
#define CL_HOLE_MIN  0x81700000u
#ifdef CL_LIST_BASE
#define CL_HOLE_MAX  CL_LIST_BASE
#elif defined(CL_LAYOUT_GENERIC)
/* generic-VMC-safe layout: Cubeboot's resident VMC image (0x817F5200..0x81800000 diag, 0x817F7E00.. prod)
   and the FST moved below it (FST/arena top ~0x817ED5xx..0x817F08xx) occupy 0x817F4000-0x817F7700 where the stock layout puts the exploit's Gecko list, our loader and the work area. */
#define CL_HOLE_MAX  0x817E0000u
#else
#define CL_HOLE_MAX  0x817F0000u
#endif
#define CL_WA_SIZE   0xA000u    /* CARD work area, 32-byte aligned.  Layout (cl_loader.c load_*): header block hb = CL_HOLE_MAX - CL_WA_SIZE - MSCF_BLOCK, work area wa = CL_HOLE_MAX - CL_WA_SIZE;
                                   the 0xC000 bytes [hb, CL_HOLE_MAX) lie directly ABOVE the claimed hole (base + size <= hb is enforced), i.e. above the lowered lbHeap arena_hi and AllM end, so no
                                   heap owns them after the claim.  They are unused once the loader returns; ENGINE_MEMORY_BREAKDOWN step 2b: the engine's CARD layer (on_card + sbr_card, one shared
                                   49,152 B area, on_hist.c ms_card_wa_acquire) uses exactly this window at run time (card_payload.py patches -DMS_CARD_WA_ADDR = CL_HOLE_MAX - 0xC000 per CL_LAYOUT).
                                   It is OWNED by the engine; nothing else may be placed there. */
#ifdef CL_LIST_BASE
#define CL_STATUS    (CL_LIST_BASE + 0x3600u)
#elif defined(CL_LAYOUT_GENERIC)
#define CL_STATUS    0x817E3600u
#else
#define CL_STATUS    0x817F7600u
#endif
/* CL_STATUS: 16 words of status inside the Gecko slot's last bytes (read by the harness) */

#define E_OK        0
#define E_NOCARD    1
#define E_NOFILE    2
#define E_HDR       3
#define E_RANGE     4
#define E_READ      5
#define E_CRC       6
#define E_BUSY      7
#define E_ARENA     8
#define E_GUARD     9
#define E_MOUNT     10
#define E_SEQ       11      /* verifying loader only: build_seq below the floor */
#define E_SIG       12      /* verifying loader only: no valid signature under a pinned key */
#define E_ABI       13      /* verifying loader only: ABI field differs from the loader's */
#define E_DECODE    14      /* compressed file: the stream does not decode to image_len bytes (the stream CRC was good) */
#define E_TRIED     15      /* verifying loader only: the boot record says this build started CL_TRIES times and never reached the Main Menu */
#define E_SAFE      16      /* verifying loader only: L+R held, the active file was skipped on purpose (safe mode) */

#endif
