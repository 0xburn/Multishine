/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * QR code encoder for the console client (ISO/IEC 18004), byte mode, error correction level M.
 *
 * Strict C89, no libc (own loops, no memcpy/malloc/printf), no float/double (soft-float safe), static work
 * buffers only (NOT reentrant: one call at a time).
 *
 * Why level M: it recovers about 15 percent of damaged codewords, which is the best trade between size and
 * robustness for a code shown on a CRT through a composite cable (L is too fragile for blur, Q/H grow the
 * symbol past what a 480-line screen shows well).  Versions 1..6 only (21x21 .. 41x41 modules); byte
 * capacities at level M are 14, 26, 42, 62, 84 and 106 bytes.  MAXIMUM PAYLOAD: 106 bytes (version 6-M);
 * a longer payload returns QR_ERR_TOO_LONG.  The smallest version that fits is always chosen.
 * Version information blocks exist only from version 7, so none is written.
 *
 * Output format (out):
 *   N       = 17 + 4 * version     (21, 25, 29, 33, 37, 41), returned in *size
 *   stride  = (N + 7) / 8 bytes per row (3, 4, 4, 5, 5, 6)
 *   row r, column c (both 0 = top / left) is bit (7 - (c & 7)) of out[r * stride + (c >> 3)]; 1 = dark module.
 *   The padding bits at the end of each row are 0.  The quiet zone (4 modules) is NOT included.
 *   Only the first N * stride bytes are written; the caller needs QR_OUT_BYTES (version 6: 41 * 6 = 246).
 * Mask: all 8 masks are scored with the four penalty rules (runs, 2x2 blocks, finder-like 1:1:3:1:1 with four
 * light modules, dark balance); the lowest score wins, a tie goes to the lowest mask index.
 */
#ifndef QR_H
#define QR_H

#define QR_MAX_PAYLOAD 106u
#define QR_MAX_SIZE    41u
#define QR_OUT_BYTES   246u   /* 41 rows * ((41 + 7) / 8 = 6) bytes */

#define QR_OK           0
#define QR_ERR_ARG      1     /* NULL out/size, or NULL data with len > 0 */
#define QR_ERR_TOO_LONG 2     /* len > QR_MAX_PAYLOAD */
#define QR_ERR_MASK     3     /* mask outside -1..7 */

int qr_encode(const char *data, unsigned len, unsigned char *out, unsigned char *size);

/* Test hook: mask 0..7 forces that mask, -1 chooses by penalty (what qr_encode does). */
int qr_encode_mask(const char *data, unsigned len, int mask, unsigned char *out, unsigned char *size);

#endif
