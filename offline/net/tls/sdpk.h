/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SDPK_H
#define SDPK_H
/* sdpk: public-key SIGNATURE VERIFICATION for the console TLS client's WebPKI mode: RSA (PKCS#1 v1.5
   and PSS, 2048..4096 bit, e up to 32 bits) and ECDSA over P-256 / P-384, all on one generic Montgomery multiplier (32-bit limbs).
   Verification only: every input is public, so the code is NOT constant time and holds no secrets.  Strict C89, no libc, no heap: all
   working memory is the caller's struct sdpk_ws.  One call = one "heavy" operation for the poll scheduler (RSA-4096 ~7 M cycles, P-384
   ~20 M cycles at -Os, see the doc). */
typedef unsigned char sdpk_u8;
typedef unsigned int sdpk_u32;

#define SDPK_HASH_SHA256 1
#define SDPK_HASH_SHA384 2

#define SDPK_CURVE_P256 1
#define SDPK_CURVE_P384 2

#define SDPK_RSA_MAX_BYTES 512          /* 4096-bit modulus */
#define SDPK_WS_WORDS (6 * 132)

struct sdpk_ws { sdpk_u32 w[SDPK_WS_WORDS]; };      /* 3.1 KB; reused by every verify call, never holds secrets */

/* Returns 1 if sig is a valid signature of the hash `h` (already computed with `hash`) under the key, 0 otherwise (any malformation counts). */
int sdpk_rsa_pkcs1_verify(struct sdpk_ws *ws, const sdpk_u8 *n, sdpk_u32 nlen, const sdpk_u8 *e, sdpk_u32 elen, int hash, const sdpk_u8 *h,
                          const sdpk_u8 *sig, sdpk_u32 siglen);
int sdpk_rsa_pss_verify(struct sdpk_ws *ws, const sdpk_u8 *n, sdpk_u32 nlen, const sdpk_u8 *e, sdpk_u32 elen, int hash, const sdpk_u8 *h,
                        const sdpk_u8 *sig, sdpk_u32 siglen);
/* pub: SEC1 uncompressed point 04||X||Y (65 or 97 bytes).  sig: DER ECDSA-Sig-Value SEQUENCE { INTEGER r, INTEGER s }.  h: the digest, any length
   (truncated or zero-extended per FIPS 186-4 like OpenSSL does). */
int sdpk_ecdsa_verify(struct sdpk_ws *ws, int curve, const sdpk_u8 *pub, sdpk_u32 publen, const sdpk_u8 *h, sdpk_u32 hlen, const sdpk_u8 *sig, sdpk_u32 siglen);

/* SHA-384 (and SHA-512), for chain signatures made with sha384 and for ECDSA P-384 CertificateVerify. */
struct sdpk_sha512 { sdpk_u32 h[16]; sdpk_u32 n; sdpk_u8 buf[128]; sdpk_u32 fill; sdpk_u32 outlen; };
void sdpk_sha384_init(struct sdpk_sha512 *c);
void sdpk_sha512_init(struct sdpk_sha512 *c);
void sdpk_sha512_update(struct sdpk_sha512 *c, const sdpk_u8 *p, sdpk_u32 n);
void sdpk_sha512_final(struct sdpk_sha512 *c, sdpk_u8 *out);       /* 48 or 64 bytes depending on init */
void sdpk_sha384(const sdpk_u8 *p, sdpk_u32 n, sdpk_u8 out[48]);

/* operation counters for the cycle model (host builds with -DSDPK_COUNT) */
#ifdef SDPK_COUNT
extern unsigned long sdpk_count_mm[130];     /* montgomery multiplies by limb count */
#endif
#endif
