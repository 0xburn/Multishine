/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sgx: the crypto and record glue of "Send Slippi login" (console half ).
 * Pure C89, no libc, no heap, no clock.  NO new primitive: X25519 (sdc25519.c), HMAC-SHA256, ChaCha20-Poly1305 (sdcrypto.c) are the TLS stack's.
 *   blob   = ver 1 | e_pub 32 | tag 16 | ciphertext          (the page seals; the console opens)
 *   shared = X25519(c_priv, e_pub), all zero = refuse
 *   prk    = HMAC-SHA256(32 zero bytes, shared);  key = HMAC-SHA256(prk, "ms-cred-v1" | code 8 | c_pub | e_pub | 0x01)
 *   AEAD   = ChaCha20-Poly1305, key, 12 zero bytes nonce, aad "ms-cred-v1" | code 8
 * The play key is never logged, never drawn: sgx_ident.key goes from the opened plaintext to the card record and is wiped.
 */
#ifndef SGX_H
#define SGX_H
#include "../../../net/tls/sdcrypto.h"
#include "../../../net/tls/sdc25519.h"

#define SGX_CODE_LEN 8u
#define SGX_BLOB_MAX 4096u
#define SGX_BLOB_HDR 49u                /* ver + e_pub + tag */
#define SGX_URL_MAX 112u                /* "https://" host "/send?c=" code "#k=" 43 + NUL, host up to 40 */
#define SGX_FP_LEN 10u                  /* "NNNN NNNN" + NUL */
#define SGX_UID_LEN 64u                 /* the card record field sizes (offline/provision/multishine_provision.py, SDID v1): a value must be shorter (one NUL kept) */
#define SGX_KEY_LEN 128u
#define SGX_CC_LEN 32u
#define SGX_NAME_LEN 64u
#define SGX_REC_LEN 300u                /* 8 + 64 + 128 + 32 + 64 + 4 (CRC32): the payload at block offset 0x1000 */
#define SGX_REC_OFF 0x1000u

struct sgx_ident { char uid[SGX_UID_LEN]; char key[SGX_KEY_LEN]; char code[SGX_CC_LEN]; char name[SGX_NAME_LEN]; };   /* 288 bytes, NUL terminated UTF-8 */

#define SGX_E_FORMAT (-1)               /* blob too short / long, version */
#define SGX_E_AUTH (-2)                 /* low-order key or the tag does not open (wrong key, wrong code, damaged): one error, no detail */
#define SGX_E_JSON (-3)                 /* the plaintext is not the four fields within the record limits */

void sgx_pub(sdc_u8 pub[32], const sdc_u8 priv[32]);                    /* X25519(priv, 9) */
void sgx_fingerprint(const sdc_u8 pub[32], char out[SGX_FP_LEN]);       /* "NNNN NNNN" */
/* "https://<host>/send?c=<code>#k=<base64url pub>"; returns the length, 0 when it does not fit cap or the code is not 8 characters of the alphabet */
unsigned sgx_url(char *out, unsigned cap, const char *host, const char *code, const sdc_u8 pub[32]);
int sgx_code_ok(const char *code);                                       /* 8 characters of ABCDEFGHJKLMNPQRSTUVWXYZ23456789 */
/* Opens blob (n bytes, MODIFIED: tag moved behind the ciphertext, then decrypted in place).  Returns the plaintext length (>= 1); the plaintext is at blob + *off (33).
   Or a negative SGX_E_*, the buffer then holds garbage.  The caller wipes the whole blob buffer. */
int sgx_open(const char *code, const sdc_u8 priv[32], const sdc_u8 pub[32], sdc_u8 *blob, sdc_u32 n, sdc_u32 *off);
int sgx_parse(const sdc_u8 *plain, sdc_u32 n, struct sgx_ident *o);       /* 0 or SGX_E_JSON; on failure o is wiped */
void sgx_pack(const struct sgx_ident *id, sdc_u8 out[SGX_REC_LEN]);       /* SDID v1 payload with its CRC32 */
int sgx_unpack(const sdc_u8 *p, sdc_u32 n, struct sgx_ident *id);        /* 0 ok; -1 magic / version / CRC / field */
sdc_u32 sgx_crc32(const sdc_u8 *p, sdc_u32 n, sdc_u32 crc);              /* zlib CRC-32; start with 0 */
void sgx_show(char *dst, unsigned cap, const char *utf8);               /* printable text for the menu font: other bytes become '.' */
#endif
