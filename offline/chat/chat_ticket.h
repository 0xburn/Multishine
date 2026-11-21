/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * chat_ticket: the quick-chat part of our lab lobby protocol on the console (portable C99, no libc, no heap).
 *
 *   create-ticket (console -> lab server):  "user": {..., "chatMessages": ["16 strings in table order"]}
 *       ct_write_array appends the array; the strings come from the image's chat block (the presets or the player's
 *       , patched in by gc/build_chat9.py).  The lab server checks them again (the private lobby's  chat_array).
 *   get-ticket-resp (lab server -> console): "players": [{..., "port": 1..4, "chatMessages": [16 strings]}]  (absent when
 *       that player sent none, and only sent to a client that sent its own: the private lobby's )
 *       ct_read_players pulls every player's array out of the reply into struct cc_strings, by port - 1.
 *
 * Slippi does the same with its matchmaking server: SlippiMatchmaking.cpp reads players[].chatMessages ("is_array" and
 * "size != 16" -> the default messages), EXI_DeviceSlippi.cpp handleGetPlayerSettings hands them to the CSS code, and
 * slippi-ssbm-c GetChatText shows the SENDER's string for a received id.
 *
 * The reply is untrusted input that ends up in game memory, so the reader is strict and bounded:
 *   - one pass, never reads past len, nesting depth <= CT_MAX_DEPTH, unknown keys and values are skipped;
 *   - a chatMessages value that is not an array of exactly 16 strings leaves that player on the defaults (have = 0);
 *   - every string is JSON-unescaped into a 160-byte scratch (longer input is cut) and then cc_sanitize()d: only Melee-
 *     drawable ASCII survives, at most 25 characters; an empty result falls back to the default for that one message;
 *   - a syntax error anywhere returns < 0 and the caller keeps the defaults for everybody.
 */
#ifndef CHAT_TICKET_H
#define CHAT_TICKET_H

#include "chat_core.h"

#define CT_MAX_DEPTH 8
#define CT_SCRATCH 160

struct ct_result {
    int players_seen;         /* objects in players[] */
    int arrays_ok;            /* players whose chatMessages was a valid 16-string array */
    int arrays_unusable;      /* chatMessages present but not 16 strings (null, wrong count, wrong type) */
    int strings_cut;          /* strings that were sanitised to something shorter than their input */
    int strings_empty;        /* strings with nothing drawable (that message uses the default) */
};

/* parse a whole get-ticket-resp into s (cleared first; pass a scratch struct): s->have[p] / s->text[p] for every
   players[] entry with a valid array.  Returns 0 on success, < 0 on malformed JSON: then the caller must not use s and
   keeps the defaults for every player. */
int ct_read_players(const cc_u8 *js, cc_u32 len, struct cc_strings *s, struct ct_result *r);

/* append  ,"chatMessages":["...", ... 16]  to a JSON writer buffer: out/cap/len as the caller's writer.
   texts = 16 NUL-terminated strings of 1..CP_MAX_CHARS drawable characters (cc_drawable; no character needs a JSON
   escape).  Returns the new length, or -1 if a string breaks the rule or the array does not fit (out[len] is then 0 again:
   the caller's request is unchanged). */
int ct_write_array(char *out, cc_u32 cap, cc_u32 len, const char texts[CP_N][CC_TXT]);

#endif
