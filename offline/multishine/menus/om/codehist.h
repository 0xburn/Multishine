/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* codehist: the direct-code history of the 'Multishine online-menu' image: record format "SDCH" v1,
 * most-recent-first list rules and Slippi's 0xBE FETCH_CODE_SUGGESTION answer.  Pure portable C89: no libc, no heap, no I/O, no clock, no
 * globals; the card side is om_card.c.  Reference implementation in Python: codehist.py (the host tests run both and compare).
 *
 * A code is 1..8 characters of [A-Z0-9#] (Slippi's connect code format TAG#123; lower case is folded to upper case, so "abc#1" and
 * "ABC#1" are one code), exactly one '#' that is neither the first nor the last character.  No names, no ids, no secrets.
 *
 * Record (payload at 0x1000 of the card block; the block's first 0x40 bytes are the two comment lines, same convention as MultishineOptions):
 *   +0x00 'SDCH'  +0x04 u8 version 1  +0x05 u8 count 0..20  +0x06 u16 flags (0)
 *   +0x08 u32 CRC-32 (zlib) of record[0x0C..0xB0)   +0x0C u32 write counter (informational)
 *   +0x10 20 x 8 bytes: code, upper-case ASCII, zero padded, most recent first; entries from `count` on are all zero
 *   size 0xB0.  Bytes after 0xB0 in the payload area are never touched.
 * Verdicts (ch_check): OK; DAMAGED (bad magic, version 0, count > 20, CRC, an entry that is not a valid code, a duplicate, non-zero tail
 * entries): treated as empty and replaced by the next save; NEWER (same magic, version > 1): never read, never overwritten.
 */
#ifndef CODEHIST_H
#define CODEHIST_H

#define CH_MAX 20
#define CH_LEN 8
#define CH_MAGIC 0x53444348u          /* 'SDCH' */
#define CH_VERSION 1
#define CH_REC_SIZE 0xB0u
#define CH_PAYLOAD_OFF 0x1000u        /* inside the 0x2000 card block */
#define CH_BLOCK 0x2000u
#define CH_COMMENT_LEN 32

enum { CH_OK = 0, CH_DAMAGED = 1, CH_NEWER = 2 };
enum { CH_SCROLL_NONE = 0, CH_SCROLL_OLDER = 1, CH_SCROLL_NEWER = 2, CH_SCROLL_RESET = 3 };
enum { CH_MODE_DIRECT = 2 };

struct ch_hist {
    unsigned int seq;                 /* write counter of the record this list came from */
    unsigned int n;                   /* 0..CH_MAX */
    char code[CH_MAX][CH_LEN + 1];    /* NUL terminated, most recent first */
};

unsigned int ch_crc32(const unsigned char *p, unsigned int len);

/* 1 when s (NUL terminated) is a valid stored code (upper case, charset, one inner '#', 1..8 characters) */
int ch_valid(const char *s);
/* B4 code field (Melee text: 2-byte full-width Shift-JIS per character, or single-byte ASCII, NUL terminated, at most len bytes) -> out[CH_LEN+1]
   upper case ASCII.  Returns the length (1..8) or 0 when the text is not a valid code (nothing is written then beyond out[0] = 0). */
unsigned int ch_from_melee(const unsigned char *b, unsigned int len, char *out);
/* stored code -> 0xBE answer text: pairs of Shift-JIS full-width bytes, three bytes per character (pair, 0), 24 bytes, zero padded */
void ch_to_melee(const char *code, unsigned char *out);   /* out: 24 bytes */

void ch_init(struct ch_hist *h);
/* MRU update: invalid code -> -1 (list unchanged); already first -> 0 (no change); else moved or inserted first, oldest dropped past 20 -> 1 */
int ch_add(struct ch_hist *h, const char *code);
/* NAMESPACES (hub images, CH_AT): one record holds both universes, told apart by the separator: '#' = Slippi lists, '@' = Multishine lists (Mano a mano).  The record format is
   unchanged.  ch_sep: the separator character of a valid code (0 for anything else).  ch_add_ns: ch_add for a code of namespace `sep` only (any other separator -> -1, list
   unchanged); a NEW entry of a full namespace (CH_NS_MAX of it) drops the oldest entry of the SAME namespace, else (list full) the oldest of the other namespace, so neither mode
   can push the other's recent codes out (only when the other one holds more than its own half).  ch_filter: dst = the entries of src with separator `sep`, order kept (a list of
   its own, so scroll indices are those of the list the player sees).  Defined only with CH_AT. */
#define CH_NS_MAX 10
char ch_sep(const char *code);
int ch_add_ns(struct ch_hist *h, const char *code, char sep);
void ch_filter(struct ch_hist *dst, const struct ch_hist *src, char sep);

/* append the entries of src that dst does not hold, until 20; returns the number appended */
unsigned int ch_merge(struct ch_hist *dst, const struct ch_hist *src);

/* record <-> list.  rec points to CH_REC_SIZE readable / writable bytes (bytes beyond are not touched). */
int ch_check(const unsigned char *rec);
void ch_decode(struct ch_hist *h, const unsigned char *rec);   /* only for ch_check == CH_OK */
void ch_encode(unsigned char *rec, const struct ch_hist *h);   /* seq is written as is, the CRC is computed */

/* a fresh card block: zeros, the two comment lines (NUL terminated texts, truncated to 31 bytes), then the record */
void ch_new_block(unsigned char *block, const char *line1, const char *line2, const struct ch_hist *h);

/* 0xBE: req = the 32 request bytes as the game sent them (req[0] = 0xBE, input[24] = 8 x (2 Shift-JIS bytes + 1), input_len, index u32 BE,
   scroll direction, mode); rsp = 30 bytes (has_suggestion, text[24], length, index u32 BE).  Slippi's algorithm
, history = h for mode CH_MODE_DIRECT, empty for any other mode.
   Returns 1 when a suggestion was produced, else 0 (the typed text is echoed back). */
int ch_answer(const struct ch_hist *h, const unsigned char *req, unsigned char *rsp);

#endif
