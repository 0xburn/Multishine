/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * chat_core: Slippi-compatible quick chat, portable C99 (no libc, no heap), host unit tested, linked into the console image.
 * Original work.  Behaviour (not code) follows the public Slippi sources named at each step; nothing is copied.
 *   [N]  Source/Core/Core/Slippi/SlippiNetplay.cpp   (wire, receive rules, auto-reply)
 *   [E]  Source/Core/Core/HW/EXI_DeviceSlippi.cpp    (handleChatMessage, prepareOnlineMatchState,
 *                                                                     handleGetPlayerSettings, isSlippiChatEnabled)
 *   [P]  Source/Core/Core/Slippi/SlippiPremadeText.h (the 16 ids, 0x10)
 *   [C] slippi-ssbm-c Scenes/CSS/Chat/Chat.c, Core/Notifications/Chat/Chat, , 
 *       (the CSS chat window and the on-screen messages; reference checkout a local work folder, head d7174ca;
 *       Slippi ships this code compiled into SlippiCSS.dat, an m-ex file our builds do not use)
 *   [M]  src/sysdolphin/baselib/hsd_3A64.c (Melee's ASCII-to-SIS text encoder, for the size guard)
 *
 * DATA FLOW, end to end (one console, 1v1 direct game, our lab build; chat exists only on the online CSS, as in Slippi):
 *
 *   D-pad on the online CSS --> cc_ui_frame: 1st press opens a group, 2nd press picks the message      [C] UpdateChatWindow
 *     --> id = group bit << 4 | message bit (one of the 16 ids)                                         [C] chatCommand
 *     --> cc_can_send_local: at most 4 own messages on screen, 12 frames between sends                  [C] CanAddNewChatMessage
 *     --> cc_send: chat enabled and a netplay session exists?  0x84 packet + remember id for the echo   [E] handleChatMessage
 *     --> glue: ENet reliable, channel 0, to the opponent                                               [N] Send
 *   ... network ...
 *   ENet receive (glue) --> cc_on_packet: parse 0x84, refuse truncated / unknown ids, keep the NEWEST   [N] OnData,
 *                                                                                                       ReadChatMessageFromPacket
 *   every B3 (each CSS frame) --> cc_poll: own echo first, else the received id; chat off -> auto-reply [E] prepareOnlineMatchState
 *                     0x10 once; result = {usr id, opp id, player index} (Slippi's MSRB chat bytes)     [N] GetSlippiRemoteChatMessage
 *     --> cc_subs_from_poll: validate again, queue a subtitle (5 shown, 10 at most, 210 frames each)    [C] UpdateChatNotifications
 *     --> cc_text_for: the SENDER's string for that id (from the lobby ticket), else our default        [C] GetChatText, [E]
 *                                                                                                       handleGetPlayerSettings
 *     --> cc_line: "NAME: text" as bytes for Melee's encoder (punctuation as Shift-JIS pairs), cut so
 *         that the encoded form fits Melee's 128-byte stack buffer                                      [M] HSD_SisLib_803A6B98
 *     --> glue (gc/chat_gc.c): Melee's SisLib text with a dark backdrop, inside the title-safe area
 *
 * THE WIRE MESSAGE (NP_MSG_SLIPPI_CHAT_MESSAGE, [N] WriteChatMessageToPacket; SFML sf::Packet = big-endian):
 *
 *   offset | size | field      | value
 *   -------+------+------------+---------------------------------------------------------------
 *     0    |  1   | message id | 0x84
 *     1    |  4   | messageId  | s32 big-endian, one of the 16 ids or 0x10 (an `int` written with packet << int)
 *     5    |  1   | playerIdx  | u8, the SENDER's absolute player index 0..3 (LocalPlayerPort())
 *   6 bytes, ENet reliable, channel 0 (everything except PAD/PAD_ACK goes there: [N] SlippiNetplayClient::Send).
 *   Bytes after offset 6 are ignored by Slippi's reader (sf::Packet reads only what it asks for); any channel is accepted.
 *
 * No text is on the wire: a receiver turns the id into text itself.  Slippi Dolphin takes that text from the sender's
 * chatMessages[16] that the matchmaking server put into the ticket ([E] handleGetPlayerSettings, SlippiMatchmaking.cpp
 * 'chatMessages'); we do the same with our lab lobby (players[].chatMessages) and fall back to our defaults per message.
 */
#ifndef CHAT_CORE_H
#define CHAT_CORE_H

typedef unsigned char cc_u8;
typedef unsigned short cc_u16;
typedef unsigned int cc_u32;
typedef int cc_s32;

#include "chat_presets_gen.h"   /* generated from chat_presets.json: CP_N, cp_ids, cp_default_text, charset, ... */

/* ---- ids ---------------------------------------------------------------------------------------- */
#define CC_MSG_CHAT 0x84          /* NP_MSG_SLIPPI_CHAT_MESSAGE */
#define CC_WIRE_LEN 6
#define CC_TXT 26                 /* CP_MAX_CHARS (25) + NUL */
enum { CC_PAD_LEFT = 0x1, CC_PAD_RIGHT = 0x2, CC_PAD_DOWN = 0x4, CC_PAD_UP = 0x8, CC_PAD_B = 0x200 };

int cc_is_menu_id(cc_s32 id);       /* one of the 16 menu ids */
int cc_is_wire_id(cc_s32 id);       /* what a Slippi receiver accepts: the 16 + 0x10 ([N] ReadChatMessageFromPacket) */
int cc_index_of(cc_s32 id);         /* table index 0..15, -1 otherwise */

/* ---- chat setting (Slippi's SlippiEnableQuickChat: SLIPPI_CHAT_*) ------------------ */
enum { CC_CHAT_ON = 0, CC_CHAT_DIRECT_ONLY = 1, CC_CHAT_OFF = 2 };
int cc_enabled(cc_u32 setting, int direct_mode);    /* [E] isSlippiChatEnabled */

/* ---- wire ---------------------------------------------------------------------------------------- */
enum { CC_RX_OK = 0, CC_RX_NOT_CHAT = -1, CC_RX_NO_ID = -2, CC_RX_NO_PLAYER = -3, CC_RX_BAD_ID = -4 };
void cc_encode(cc_u8 out[CC_WIRE_LEN], cc_s32 id, cc_u8 player_idx);
int cc_decode(const cc_u8 *d, cc_u32 len, cc_s32 *id, cc_u8 *player_idx);

/* ---- per-connection state: what SlippiNetplayClient keeps ------------------------------------------ */
struct cc_net {
    int have_remote;          /* remoteChatMessageSelection != nullptr */
    cc_s32 remote_id;         /*   ->messageId (newest received; an older unread one is overwritten) */
    cc_u8 remote_pidx;        /*   ->playerIdx (as sent; Slippi does not check it, we only record it) */
    cc_u8 sent_id;            /* remoteSentChatMessageId: our own last sent id, for the local echo */
    /* counters (status block) */
    cc_u32 rx_ok, rx_bad, rx_overwritten, rx_extra_bytes, tx, tx_refused, replies;
    cc_s32 last_bad;          /* last decode error */
};
void cc_net_reset(struct cc_net *n);
/* received ENet payload starting with 0x84 (any channel): CC_RX_* */
int cc_on_packet(struct cc_net *n, const cc_u8 *d, cc_u32 len);
/* local send request ([E] handleChatMessage): chat off -> nothing is sent and nothing is echoed; no netplay session ->
   nothing either (Slippi: `if (slippi_netplay)`).  Otherwise the id is kept for the echo and out[] is filled: returns
   CC_WIRE_LEN and the glue sends it reliably on channel 0 (to a connected opponent; Slippi's Send skips peers that are
   not connected).  Only the 16 menu ids are ever sent. */
int cc_send(struct cc_net *n, cc_s32 id, int enabled, int session, cc_u8 local_idx, cc_u8 out[CC_WIRE_LEN]);

/* once per match-state request (Slippi: every B3 = prepareOnlineMatchState while the online CSS runs) */
struct cc_poll_out {
    cc_u8 usr_id;             /* MSRB_USER_CHATMSG_ID  (our own message to echo) */
    cc_u8 opp_id;             /* MSRB_OPP_CHATMSG_ID   (a received message) */
    cc_u8 pidx;               /* MSRB_CHATMSG_PLAYER_INDEX */
};
/* 1v1 (Slippi's isSingleMode).  Returns CC_WIRE_LEN with reply[] filled when the auto-reply 0x10 must be sent (chat off
   and a message arrived), else 0. */
int cc_poll(struct cc_net *n, int enabled, cc_u8 local_idx, cc_u8 remote_idx, struct cc_poll_out *o,
            cc_u8 reply[CC_WIRE_LEN]);

/* ---- characters and strings ----------------------------------------------------------------------- */
int cc_drawable(cc_u8 c);           /* in the presets' charset (direct ASCII or Shift-JIS punctuation) */
/* text of each player's 16 messages by absolute player index (= ticket port - 1).  have[p] = 0: use the defaults. */
struct cc_strings {
    cc_u8 have[4];
    char text[4][CP_N][CC_TXT];
};
void cc_strings_clear(struct cc_strings *s);
/* sanitise one received string (UTF-8 bytes, already JSON-unescaped) into out: keeps only drawable characters, drops
   everything else (control codes, quotes, backslash, every non-ASCII sequence), squeezes runs of spaces, trims, cuts at
   CP_MAX_CHARS characters.  Returns the length (0 = nothing drawable left: the caller uses the default). */
int cc_sanitize(const cc_u8 *in, cc_u32 inlen, char out[CC_TXT]);
/* the sender's text for an id: its custom string if present and non-empty, else our default; 0 for a bad id.
   0x10 returns CP_NOTICE_DISABLED. */
const char *cc_text_for(const struct cc_strings *s, int pidx, cc_s32 id);

/* ---- the CSS chat window (Slippi's chat page) ------------------------------------------------------- */
#define CC_WINDOW_FRAMES 150      /* [C] CHAT_WINDOW_FRAMES 60*2.5: closes after this many frames without a send */
#define CC_ALLOW_FRAMES 12        /* [C] CHAT_ALLOW_COMMAND_FRAMES 60*0.2 */
struct cc_ui {
    int open;                 /* chatWindowOpened (the glue mirrors it into Slippi's CSSDT_CHAT_WINDOW_OPENED) */
    int group;                /* pad bit of the open group */
    int built;                /* first window frame only builds the text ([C] UpdateChatWindow: if (!data->text)) */
    int frames_left;
    int delayed;              /* delayedFrames */
    cc_u32 opens, sends, blocked, closes_b, closes_timeout;
};
enum { CC_UI_NONE = 0, CC_UI_OPENED = 1, CC_UI_CLOSED = 2, CC_UI_SENT = 4, CC_UI_BLOCKED = 8, CC_UI_TOO_SOON = 16 };
/* one frame: pressed = D-pad/B bits pressed THIS frame (rising edges of the CSS player's pad).  can_add =
   cc_can_send_local.  Returns CC_UI_* flags; *send_id is set with CC_UI_SENT. */
int cc_ui_frame(struct cc_ui *u, cc_u32 pressed, int can_add, cc_s32 *send_id);
void cc_ui_close(struct cc_ui *u);

/* ---- subtitles (Slippi's notification list, shown as a subtitle stack) ------------------------------ */
#define CC_SLOTS 10               /* [C] NOTIFICATION_MESSAGE_SET_LENGTH: messages in flight at most */
#define CC_VISIBLE 5              /* ours: rows on screen at once; the others wait in order (Slippi shows all 10) */
#define CC_LIFE_FRAMES 210        /* [C] 20 in + 20 * 8.5 idle + 20 out = 210 frames (3.5 s) per message */
#define CC_MAX_LOCAL 4            /* [C] CHAT_MAX_PLAYER_MESSAGES (halved with > 1 remote player) */
#define CC_MAX_REMOTE 4           /* ours (hardening): a remote player gets the same cap; Slippi's own UI never exceeds it */
struct cc_sub {
    cc_u8 local, pidx;
    cc_u16 age;               /* frames on screen; 0 while waiting */
    cc_s32 id;
    cc_u32 seq;               /* order of arrival */
};
struct cc_subs {
    struct cc_sub e[CC_SLOTS];    /* e[0 .. n-1] in arrival order; the first min(n, CC_VISIBLE) are on screen */
    int n;
    cc_u32 seq;
    cc_u32 added, dropped_full, dropped_cap, dropped_invalid, expired;
};
void cc_subs_clear(struct cc_subs *t);
int cc_subs_local(const struct cc_subs *t);
int cc_can_send_local(const struct cc_subs *t, int remote_players);       /* [C] CanAddNewChatMessage */
int cc_subs_add(struct cc_subs *t, int pidx, cc_s32 id, int local);       /* index or -1 (dropped) */
/* [C] UpdateChatNotifications: take the poll result, validate, add.  Returns the index or -1. */
int cc_subs_from_poll(struct cc_subs *t, const struct cc_poll_out *o, cc_u8 local_idx);
/* one frame: age the visible rows, drop expired ones (the rows below move up, waiting ones come in).  Returns the
   number of rows that changed (0 = nothing to redraw). */
int cc_subs_tick(struct cc_subs *t);

/* ---- one display line in the bytes Melee's text encoder takes --------------------------------------- */
#define CC_LINE_MAX 100           /* raw bytes of a line (Melee formats it into a 128-byte stack buffer) */
#define CC_SIS_MAX 120            /* encoded bytes of a line (Melee encodes it into a 128-byte stack buffer) */
#define CC_NAME_CHARS 15
/* [M] the number of bytes HSD_SisLib_803A67EC writes for raw (incl. its kerning codes, without the final 0): exact for
   the retail encoder, an upper bound with Slippi's ParseNumbersBetter patch (chat_core.c) */
int cc_sis_bytes(const cc_u8 *raw);
/* append one drawable character to a raw line (Shift-JIS pair for punctuation); returns the new length or -1 if it does
   not fit (raw length or encoded size) */
int cc_put_char(cc_u8 *raw, int n, int cap, char c);
/* "NAME: text" (or "NAME has chat turned off" for 0x10) as raw bytes, NUL-terminated.  The name is cut to
   CC_NAME_CHARS drawable characters ("P1".."P4" if it has none); the text is cut where the line would no longer fit
   (CC_LINE_MAX raw, CC_SIS_MAX encoded); a cut text ends in "..".  Returns the number of characters (not bytes). */
int cc_line(cc_u8 *raw, int cap, const char *name, int pidx, const char *text, cc_s32 id);
/* split text for two rows of at most `width` characters at a space if there is one (else hard), the second row cut with
   ".." when it is still too long.  Returns the number of rows (1 or 2); row2[0] = 0 for one row. */
int cc_wrap(const char *text, int width, char *row1, char *row2, int cap);

/* ---- TEST MODE: everything our should photograph on hardware, one step at a time --------------- */
enum { CC_DEMO_NONE = 0, CC_DEMO_SUB_OWN, CC_DEMO_SUB_OPP, CC_DEMO_NOTICE, CC_DEMO_WINDOW, CC_DEMO_LONG };
/* what to show at frame counter `frame` (a new step every `period` frames): the 16 presets as subtitles (own and
   opponent style alternating), the 0x10 notice, the four chat window pages, a worst-case long line; then it repeats.
   *arg = table index (subtitles), group pad bit (window), 0 otherwise.  Returns CC_DEMO_NONE between steps. */
int cc_demo_step(cc_u32 frame, cc_u32 period, int *arg);
#define CC_DEMO_STEPS (CP_N + 1 + 4 + 1)

#endif
