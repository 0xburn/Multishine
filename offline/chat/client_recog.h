/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
 * Console-side packet layout of the Multishine hello (ENet message id 0xD0). Twin of; the host test
 * compiles this header and compares bytes. C89, no heap, no libc beyond nothing, big-endian explicit.
 * Rules:.  The hello is the ONLY Multishine message that may reach an unproven peer. */
#ifndef CLIENT_RECOG_H
#define CLIENT_RECOG_H

#define CR_MSG_HELLO        0xD0u
#define CR_MSG_L1_FIRST     0xD1u
#define CR_MSG_L1_LAST      0xDFu
#define CR_PROTO            1u
#define CR_HELLO_SIZE       20u    /* fixed: id 1 | magic 4 | proto 1 | build 4 | caps 2 | nonce 4 | tag 4 */
#define CR_MAX_HELLO_SENDS  3u     /* per connection: 60 bytes at most */
#define CR_HELLO_TIMEOUT_MS 2000u
#define CR_CAP_L1_PRESETS   1u
#define CR_CAP_FREE_TEXT    2u
#define CR_CAP_L2_OVERLAY   4u     /* reserved, never sent, cleared on receipt */
#define CR_CAPS_KNOWN       3u

typedef struct cr_hello {
    unsigned char proto;
    unsigned long build_id;
    unsigned short caps;
    unsigned long nonce;
    unsigned long tag;
} cr_hello;

static void cr_put32(unsigned char *p, unsigned long v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static unsigned long cr_get32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }

/* out must hold CR_HELLO_SIZE bytes; the tag is computed by the caller (keyed hash, see the Python reference) */
static void cr_hello_pack(unsigned char *out, const cr_hello *h)
{
    out[0] = (unsigned char)CR_MSG_HELLO;
    out[1] = 'M'; out[2] = 'S'; out[3] = 'H'; out[4] = 'N';
    out[5] = h->proto;
    cr_put32(out + 6, h->build_id);
    out[10] = (unsigned char)(h->caps >> 8); out[11] = (unsigned char)h->caps;
    cr_put32(out + 12, h->nonce);
    cr_put32(out + 16, h->tag);
}

/* 1 = a well-formed hello (exact length, id, magic); 0 = anything else.  Never reads beyond len. */
static int cr_hello_parse(const unsigned char *p, unsigned long len, cr_hello *h)
{
    if (len != CR_HELLO_SIZE || p[0] != CR_MSG_HELLO || p[1] != 'M' || p[2] != 'S' || p[3] != 'H' || p[4] != 'N') return 0;
    h->proto = p[5]; h->build_id = cr_get32(p + 6); h->caps = (unsigned short)((p[10] << 8) | p[11]);
    h->nonce = cr_get32(p + 12); h->tag = cr_get32(p + 16);
    return 1;
}

/* the send gate: unproven peer (level 0) gets only Slippi's own ids 0x80..0x86 and the hello */
static int cr_may_send(unsigned msg_id, int peer_level)
{
    if (msg_id == CR_MSG_HELLO || (msg_id >= 0x80u && msg_id <= 0x86u)) return 1;
    if (msg_id >= CR_MSG_L1_FIRST && msg_id <= CR_MSG_L1_LAST) return peer_level >= 1;
    return 0;
}
#endif
