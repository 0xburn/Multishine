/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * chat_link: one console's quick chat, the glue-independent part (portable C99, no libc, no heap).  Used by the console
 * image (gc/chat_gc.c draws it with Melee's text) and by the host integration test (gc/tests/host_chat9.c, two consoles
 * against the real lab server).  Every rule is in chat_core.c; this file only wires them together per console.
 *
 *   lobby reply (players[].chatMessages) --> chat_link_strings: the texts each player wants shown for its ids
 *   CSS frame:   pad edges --> chat_link_frame --> window (cc_ui) --> id --> cc_send --> 6 bytes for the glue to send
 *   ENet 0x84:   --> chat_link_rx --> cc_on_packet (newest wins)
 *   B3:          --> chat_link_b3 --> cc_poll (echo / received / auto-reply 0x10) --> Slippi's three MSRB chat bytes
 *                    (offsets 10..12, Online.s MSRB_USER_CHATMSG_ID..MSRB_CHATMSG_PLAYER_INDEX) --> subtitle queue
 *   draw:        chat_link_row(i) --> "NAME: text" bytes for Melee's text encoder (cc_line)
 */
#ifndef CHAT_LINK_H
#define CHAT_LINK_H

#include "chat_core.h"

#define CL_MSRB_USER_ID 10        /* Online.s:324 MSRB_USER_CHATMSG_ID = MSRB_DELAY_FRAMES (9) + 1 */
#define CL_MSRB_OPP_ID 11         /* Online.s:325 */
#define CL_MSRB_PIDX 12           /* Online.s:326 MSRB_CHATMSG_PLAYER_INDEX */

struct chat_link {
    struct cc_net net;
    struct cc_subs subs;
    struct cc_strings str;
    struct cc_ui ui;
    cc_u32 setting;               /* CC_CHAT_ON / DIRECT_ONLY / OFF */
    int direct;                   /* 1: this lobby is a direct (connect-code) game; our lab build has only those */
    int session;                  /* a netplay session exists (Slippi: slippi_netplay != nullptr) */
    int local_idx, remote_idx;    /* absolute player indices (MSRB) */
    char name[4][16];             /* drawable display names by player index ("" = use "P1".."P4") */
    char own[CP_N][CC_TXT];       /* our 16 texts (presets or ), sent in the ticket */
    cc_u32 sent, blocked, replies, shown, ui_opens;
};

void chat_link_init(struct chat_link *l, cc_u32 setting, const char own[CP_N][CC_TXT]);
/* a new opponent (Slippi: a new SlippiNetplayClient): forget the pending ids; strings and names are set again by the
   caller.  The subtitles on screen stay (they belong to the CSS scene, the glue clears them with the scene). */
void chat_link_new_session(struct chat_link *l, int local_idx, int remote_idx);
void chat_link_end_session(struct chat_link *l);
/* take every player's texts from a get-ticket-resp (raw JSON); our own index always gets our own texts.  Returns
   ct_read_players' result (< 0: malformed, everybody keeps the defaults). */
int chat_link_strings(struct chat_link *l, const cc_u8 *json, cc_u32 len);
void chat_link_set_name(struct chat_link *l, int pidx, const char *name);
int chat_link_enabled(const struct chat_link *l);
/* one CSS frame with the pad edges of the CSS player; returns CC_UI_* flags; *tx_len = CC_WIRE_LEN when out[] must be
   sent (reliable, channel 0) */
int chat_link_frame(struct chat_link *l, cc_u32 pressed, cc_u8 out[CC_WIRE_LEN], int *tx_len);
/* a received ENet payload starting with 0x84 */
int chat_link_rx(struct chat_link *l, const cc_u8 *d, cc_u32 len);
/* one match-state request: msrb (may be 0) gets Slippi's three chat bytes; returns CC_WIRE_LEN with reply[] filled when
   the 0x10 auto-reply must be sent */
int chat_link_b3(struct chat_link *l, cc_u8 *msrb, cc_u8 reply[CC_WIRE_LEN]);
/* one frame of the subtitle queue; returns the number of changed rows */
int chat_link_tick(struct chat_link *l);
/* the text of visible row i (0 = top) as bytes for Melee's encoder; returns 0 if there is no such row.  *pidx and *local
   tell the glue which colour to use. */
int chat_link_row(const struct chat_link *l, int i, cc_u8 *raw, int cap, int *pidx, int *local);

#endif
