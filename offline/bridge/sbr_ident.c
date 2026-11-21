/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr_ident: see sbr_ident.h.  Project-owned C99 (no libc, no heap). */
#include "sbr_ident.h"

typedef sbr_u32 u32;
typedef sbr_u8 u8;

void sbr_wipe(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }

u32 sbr_crc32(const u8 *p, u32 n)                  /* bitwise, reflected 0xEDB88320 (no table: 1 KB saved) */
{
    u32 c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

static u32 get32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }

/* one text field: NUL inside the field, NUL padding after it, non-empty, no control characters (< 0x20, 0x7F); bytes >= 0x80
   (UTF-8) pass as they are (the stock client sends the name as UTF-8).  The copy fits `cap` (cap >= field size). */
static int field(const u8 *p, u32 n, char *out, u32 cap)
{
    u32 i, end = n;
    for (i = 0; i < n; i++) if (p[i] == 0) { end = i; break; }
    if (end == n || end == 0 || end + 1 > cap) return -1;
    for (i = end; i < n; i++) if (p[i]) return -1;
    for (i = 0; i < end; i++) {
        if (p[i] < 0x20 || p[i] == 0x7F) return -1;
        out[i] = (char)p[i];
    }
    out[end] = 0;
    return 0;
}

int sbr_ident_parse(const u8 *p, u32 len, struct sbr_ident *o)
{
    int r = SBR_ID_OK;
    char code[32];
    u32 i;
    sbr_wipe(o, sizeof *o);
    if (!p || len < SBR_ID_PAYLOAD_LEN) return SBR_ID_SHORT;
    if (p[0] != 'S' || p[1] != 'D' || p[2] != 'I' || p[3] != 'D') return SBR_ID_MAGIC;
    if (p[4] != 1) return SBR_ID_VERSION;
    if (sbr_crc32(p, SBR_ID_BODY_LEN) != get32(p + SBR_ID_BODY_LEN)) return SBR_ID_CRC;
    if (field(p + 8, 64, o->uid, sizeof o->uid) < 0 || field(p + 72, 128, o->play_key, sizeof o->play_key) < 0 ||
        field(p + 200, 32, code, sizeof code) < 0 || field(p + 232, 64, o->name, sizeof o->name) < 0)
        r = SBR_ID_FIELD;
    else {
        for (i = 0; code[i] && i + 1 < sizeof o->code; i++) o->code[i] = code[i];
        o->code[i] = 0;
        if (code[i] || !sbr_code_valid(o->code)) r = SBR_ID_CODE;   /* longer than a Slippi code, or not one */
    }
    sbr_wipe(code, sizeof code);
    if (r != SBR_ID_OK) sbr_wipe(o, sizeof *o);
    return r;
}

const char *sbr_ident_name(int r)
{
    static const char *const n[SBR_ID_COUNT] = { "ok", "short", "magic", "version", "crc", "field", "code" };
    return r >= 0 && r < SBR_ID_COUNT ? n[r] : "?";
}
