/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * s8_json: just enough JSON for the lab lobby protocol (the private lobby's ), project-owned C99, no libc, no heap.
 *
 * Writer: bounded append into a caller buffer (s8_jw_*); strings are escaped (", \, control bytes as \u00XX).
 * Reader: one pass over a reply, depth-limited (S8_JSON_MAX_DEPTH), never reads past `len`, extracts only the fields the
 * console needs into struct s8_reply.  Unknown keys and nested values are skipped.  Any syntax error -> < 0.
 */
#ifndef S8_JSON_H
#define S8_JSON_H

typedef unsigned int s8_u32;
typedef int s8_s32;
typedef unsigned short s8_u16;
typedef unsigned char s8_u8;

#define S8_JSON_MAX_DEPTH 6

struct s8_jw {            /* writer */
    char *buf;
    s8_u32 cap, len;
    int overflow;
};
void s8_jw_init(struct s8_jw *w, char *buf, s8_u32 cap);
void s8_jw_raw(struct s8_jw *w, const char *s);      /* verbatim (keys, punctuation) */
void s8_jw_str(struct s8_jw *w, const char *s);      /* quoted + escaped */
void s8_jw_uint(struct s8_jw *w, s8_u32 v);

#define S8_PLAYERS_MAX 4
struct s8_player {
    char name[32];
    char code[20];
    char ip[24];
    s8_s32 port;            /* slot 1..2 */
    s8_s32 local;           /* -1 unknown, 0/1 */
};

struct s8_reply {
    char type[28];
    char error_code[24];
    char error[100];
    char match_id[52];
    s8_s32 is_assigned;     /* -1 absent, 0, 1 */
    s8_s32 is_host;         /* -1 absent, 0, 1 */
    s8_s32 queued;          /* -1 absent, 0, 1 */
    s8_s32 nplayers;
    struct s8_player pl[S8_PLAYERS_MAX];
    char server_name[24];
};

/* 0 ok, < 0 malformed / too deep / truncated.  Fields not present stay empty / -1. */
int s8_json_parse_reply(const s8_u8 *js, s8_u32 len, struct s8_reply *out);

#endif
