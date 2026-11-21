/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* chat_core: see chat_core.h for the data flow and the packet layout.  Original work, no third-party code. */
#include "chat_core.h"

static void zero(void *p, cc_u32 n)
{
    cc_u8 *b = (cc_u8 *)p;
    while (n--) *b++ = 0;
}

/* ---- ids ------------------------------------------------------------------------------------------ */
int cc_index_of(cc_s32 id)
{
    int i;
    for (i = 0; i < CP_N; i++)
        if (cp_ids[i] == id) return i;
    return -1;
}

int cc_is_menu_id(cc_s32 id) { return cc_index_of(id) >= 0; }

/* [N] ReadChatMessageFromPacket: "Only these 16 message IDs are allowed" + CHAT_MSG_CHAT_DISABLED */
int cc_is_wire_id(cc_s32 id) { return cc_is_menu_id(id) || id == CP_ID_DISABLED; }

/* [E] isSlippiChatEnabled: direct (connect-code) games honour "Direct only", every other mode needs "On" */
int cc_enabled(cc_u32 setting, int direct_mode)
{
    if (setting == CC_CHAT_ON) return 1;
    if (setting == CC_CHAT_DIRECT_ONLY) return direct_mode != 0;
    return 0;
}

/* ---- wire ------------------------------------------------------------------------------------------ */
void cc_encode(cc_u8 out[CC_WIRE_LEN], cc_s32 id, cc_u8 player_idx)
{
    cc_u32 v = (cc_u32)id;
    out[0] = CC_MSG_CHAT;                 /* [N] WriteChatMessageToPacket: packet << MessageId */
    out[1] = (cc_u8)(v >> 24);            /*   packet << messageId (int, big-endian) */
    out[2] = (cc_u8)(v >> 16);
    out[3] = (cc_u8)(v >> 8);
    out[4] = (cc_u8)v;
    out[5] = player_idx;                  /*   packet << playerIdx (u8) */
}

int cc_decode(const cc_u8 *d, cc_u32 len, cc_s32 *id, cc_u8 *player_idx)
{
    if (len < 1 || d[0] != CC_MSG_CHAT) return CC_RX_NOT_CHAT;
    if (len < 5) return CC_RX_NO_ID;      /* "Chat packet too small to read message ID" */
    *id = (cc_s32)(((cc_u32)d[1] << 24) | ((cc_u32)d[2] << 16) | ((cc_u32)d[3] << 8) | d[4]);
    if (len < 6) return CC_RX_NO_PLAYER;  /* "Chat packet too small to read player index" */
    *player_idx = d[5];
    if (!cc_is_wire_id(*id)) return CC_RX_BAD_ID;   /* "Received invalid chat message index" */
    return CC_RX_OK;
}

void cc_net_reset(struct cc_net *n) { zero(n, sizeof *n); }

/* [N] OnData case NP_MSG_SLIPPI_CHAT_MESSAGE: a good message replaces the stored one (newest wins, no queue); a bad one
   leaves the stored one alone. */
int cc_on_packet(struct cc_net *n, const cc_u8 *d, cc_u32 len)
{
    cc_s32 id = 0;
    cc_u8 pidx = 0;
    int r = cc_decode(d, len, &id, &pidx);
    if (r != CC_RX_OK) {
        n->rx_bad++;
        n->last_bad = r;
        return r;
    }
    if (n->have_remote && n->remote_id != 0) n->rx_overwritten++;   /* not shown yet: lost, as in Slippi */
    if (len > CC_WIRE_LEN) n->rx_extra_bytes++;
    n->have_remote = 1;
    n->remote_id = id;
    n->remote_pidx = pidx;
    n->rx_ok++;
    return CC_RX_OK;
}

/* [E] handleChatMessage: chat off -> return (nothing sent, nothing echoed); no netplay client -> nothing; else remember the
   id for the echo and send.  Slippi's device sends whatever byte the game hands it; our console only sends the 16. */
int cc_send(struct cc_net *n, cc_s32 id, int enabled, int session, cc_u8 local_idx, cc_u8 out[CC_WIRE_LEN])
{
    if (!enabled || !session || !cc_is_menu_id(id)) {
        n->tx_refused++;
        return 0;
    }
    n->sent_id = (cc_u8)id;               /* remoteSentChatMessageId = messageId */
    cc_encode(out, id, local_idx);        /* playerIdx = LocalPlayerPort */
    n->tx++;
    return CC_WIRE_LEN;
}

/* [E] prepareOnlineMatchState ("Set chat message if any") for a 1v1 (isSingleMode) with
   [N] GetSlippiRemoteSentChatMessage and GetSlippiRemoteChatMessage. */
int cc_poll(struct cc_net *n, int enabled, cc_u8 local_idx, cc_u8 remote_idx, struct cc_poll_out *o,
            cc_u8 reply[CC_WIRE_LEN])
{
    cc_u8 sent = 0, chat = 0;
    int ret = 0;
    if (enabled) {                        /* GetSlippiRemoteSentChatMessage: 0 when off (and not cleared) */
        sent = n->sent_id;
        n->sent_id = 0;
    }
    if (sent == 0) {                      /* "Prevent processing a message in the same frame" */
        if (n->have_remote && enabled) {
            chat = (cc_u8)n->remote_id;   /* u8 chatMessageId = messageId */
            n->remote_id = 0;             /* "Clear it out" (the selection object stays) */
            n->remote_pidx = 0;
        } else if (n->have_remote && !enabled && n->remote_id > 0 && n->remote_id != CP_ID_DISABLED) {
            cc_encode(reply, CP_ID_DISABLED, local_idx);   /* "automatically send back a message saying so" */
            n->have_remote = 0;
            n->replies++;
            ret = CC_WIRE_LEN;
        }
    }
    /* isSingleMode: chatMessagePlayerIdx = sentChatMessageId > 0 ? localPlayerIndex: remotePlayerIndex */
    o->usr_id = sent;
    o->opp_id = chat;
    o->pidx = sent > 0 ? local_idx : remote_idx;
    return ret;
}

/* ---- characters and strings ------------------------------------------------------------------------ */
static int sjis_lo_of(cc_u8 c)
{
    int i;
    for (i = 0; i < CP_SJIS_N; i++)
        if ((cc_u8)cp_sjis_char[i] == c) return cp_sjis_lo[i];
    return -1;
}

static int direct(cc_u8 c)
{
    const char *p = CP_ASCII_DIRECT;
    if (c == 0) return 0;
    while (*p)
        if ((cc_u8)*p++ == c) return 1;
    return 0;
}

int cc_drawable(cc_u8 c) { return direct(c) || sjis_lo_of(c) >= 0; }

void cc_strings_clear(struct cc_strings *s) { zero(s, sizeof *s); }

int cc_sanitize(const cc_u8 *in, cc_u32 inlen, char out[CC_TXT])
{
    cc_u32 i = 0;
    int n = 0;
    while (i < inlen && n < CP_MAX_CHARS) {
        cc_u8 c = in[i];
        if (c >= 0x80) {                  /* one UTF-8 sequence (or a stray byte): dropped whole */
            cc_u32 skip = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            i += skip;
            continue;
        }
        i++;
        if (!cc_drawable(c)) continue;    /* control codes, '"', '\\', '`' cannot be drawn (or are excluded) */
        if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;
        out[n++] = (char)c;
    }
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

const char *cc_text_for(const struct cc_strings *s, int pidx, cc_s32 id)
{
    int i;
    if (id == CP_ID_DISABLED) return CP_NOTICE_DISABLED;
    i = cc_index_of(id);
    if (i < 0) return 0;
    /* [C] GetChatText: playerSettingsResp->settings[playerIdx].chatMessages[index]; [E] handleGetPlayerSettings fills a
       player with the defaults when its list is missing or not 16 long; we do the same per message (empty -> default) */
    if (s && pidx >= 0 && pidx < 4 && s->have[pidx] && s->text[pidx][i][0]) return s->text[pidx][i];
    return cp_default_text[i];
}

/* ---- chat window ---------------------------------------------------------------------------------------- */
/* [C] PadGetChatInput: the D-pad is checked in the order up, left, right, down; B only while the window is open */
static int dpad_input(cc_u32 pressed)
{
    static const cc_u32 order[4] = {CC_PAD_UP, CC_PAD_LEFT, CC_PAD_RIGHT, CC_PAD_DOWN};
    int i;
    for (i = 0; i < 4; i++)
        if (pressed & order[i]) return (int)order[i];
    return 0;
}

void cc_ui_close(struct cc_ui *u)
{
    u->open = 0;
    u->group = 0;
    u->built = 0;
}

int cc_ui_frame(struct cc_ui *u, cc_u32 pressed, int can_add, cc_s32 *send_id)
{
    int in;
    if (!u->open) {                       /* [C] UpdateChat -> CreateAndOpenChatWindow */
        in = dpad_input(pressed);
        if (!in) return CC_UI_NONE;
        u->open = 1;
        u->group = in;
        u->built = 0;
        u->frames_left = CC_WINDOW_FRAMES;
        u->delayed = CC_ALLOW_FRAMES;     /* "Allow sending right away" */
        u->opens++;
        return CC_UI_OPENED;
    }
    u->delayed++;                         /* [C] UpdateChatWindow */
    if (!u->built) {                      /* first window frame only builds the text and returns */
        u->built = 1;
        return CC_UI_NONE;
    }
    u->frames_left--;
    in = dpad_input(pressed);
    if (!in && (pressed & CC_PAD_B)) {
        cc_ui_close(u);
        u->closes_b++;
        return CC_UI_CLOSED;
    }
    if (in) {
        if (u->delayed < CC_ALLOW_FRAMES) return CC_UI_TOO_SOON;
        if (can_add) {
            u->delayed = 0;
            *send_id = (u->group << 4) + in;   /* chatCommand = (groupId << 4) + chatInput */
            cc_ui_close(u);
            u->sends++;
            return CC_UI_SENT | CC_UI_CLOSED;
        }
        u->delayed = 0;                   /* too many own messages on screen: "blocked" sound, window stays open */
        u->blocked++;
        return CC_UI_BLOCKED;
    }
    if (u->frames_left <= 0) {
        cc_ui_close(u);
        u->closes_timeout++;
        return CC_UI_CLOSED;
    }
    return CC_UI_NONE;
}

/* ---- subtitles ------------------------------------------------------------------------------------------ */
void cc_subs_clear(struct cc_subs *t) { zero(t, sizeof *t); }

int cc_subs_local(const struct cc_subs *t)
{
    int i, k = 0;
    for (i = 0; i < t->n; i++) k += t->e[i].local;
    return k;
}

static int remote_from(const struct cc_subs *t, int pidx)
{
    int i, k = 0;
    for (i = 0; i < t->n; i++) k += (!t->e[i].local && t->e[i].pidx == pidx);
    return k;
}

/* [C] CanAddNewChatMessage = CanAddNewMessage (a free slot of the 10) && local count < GetMaxAllowedLocalMessages */
int cc_can_send_local(const struct cc_subs *t, int remote_players)
{
    int max_local = remote_players > 1 ? CC_MAX_LOCAL / 2 : CC_MAX_LOCAL;
    return t->n < CC_SLOTS && cc_subs_local(t) < max_local;
}

int cc_subs_add(struct cc_subs *t, int pidx, cc_s32 id, int local)
{
    struct cc_sub *e;
    if (t->n >= CC_SLOTS) {               /* [C] CreateAndAddChatMessage: "Prevent going over the limit" */
        t->dropped_full++;
        return -1;
    }
    if (!local && remote_from(t, pidx) >= CC_MAX_REMOTE) {
        t->dropped_cap++;
        return -1;
    }
    e = &t->e[t->n];
    e->local = (cc_u8)(local ? 1 : 0);
    e->pidx = (cc_u8)pidx;
    e->age = 0;
    e->id = id;
    e->seq = t->seq++;
    t->added++;
    return t->n++;
}

int cc_subs_from_poll(struct cc_subs *t, const struct cc_poll_out *o, cc_u8 local_idx)
{
    cc_s32 id = o->usr_id ? o->usr_id : o->opp_id;
    if (id == 0) return -1;
    /* [C] UpdateChatNotifications: valid group and message bit, or a special id; player index 0..3 */
    if ((!cc_is_menu_id(id) && id != CP_ID_DISABLED) || o->pidx > 3) {
        t->dropped_invalid++;
        return -1;
    }
    return cc_subs_add(t, o->pidx, id, o->pidx == local_idx);
}

int cc_subs_tick(struct cc_subs *t)
{
    int i, k, changed = 0;
    for (i = 0; i < t->n && i < CC_VISIBLE;) {
        struct cc_sub *e = &t->e[i];
        if (e->age == 0) changed++;       /* new on screen this frame */
        if (++e->age < CC_LIFE_FRAMES) {
            i++;
            continue;
        }
        for (k = i; k + 1 < t->n; k++) t->e[k] = t->e[k + 1];   /* rows below move up, a waiting one comes in */
        t->n--;
        t->expired++;
        changed++;
    }
    return changed;
}

/* ---- display line ------------------------------------------------------------------------------------------ */
/* [M] HSD_SisLib_803A67EC, the size of what it writes: space. 0-9 : open a "digit run" (5 bytes the first time), the
   letters and ' , - " close it (1 byte each time), every other byte starts a 2-byte Shift-JIS pair (1 + 2); each drawn
   glyph is 2 bytes; a run still open at the end costs 1.  It reads at most 128 input bytes.  Our lines only contain
   characters that have a glyph (gen_chat_tables.py check-font).  This is exact for the retail encoder.  Slippi's
   ParseNumbersBetter.asm (04 code at 0x803A69C0, profile net-menu: our chat image) stops digits from opening a run; a run
   it opens later is one the retail encoder had already opened, so the patched size is never larger: the model is an
   upper bound there (measured in Dolphin: chat_flags=4 self-check, README). */
int cc_sis_bytes(const cc_u8 *s)
{
    int out = 0, kern = 0, i = 0;
    while (i < 0x80) {
        cc_u8 c = s[i];
        if (c == 0) {
            if (kern) out++;
            break;
        }
        if (c == ' ' || c == '.' || c == ':' || (c >= '0' && c <= '9')) {
            if (!kern) { out += 5; kern = 1; }
            out += 2;
        } else if (c == '"' || c == '\'' || c == ',' || c == '-' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            out += 1 + 2;
            kern = 0;
        } else {
            out += 1 + 2;
            kern = 0;
            if (s[i + 1] == 0) break;     /* a lone lead byte: our lines never have one */
            i++;
        }
        i++;
    }
    return out;
}

int cc_put_char(cc_u8 *raw, int n, int cap, char ch)
{
    cc_u8 c = (cc_u8)ch;
    int lo = sjis_lo_of(c), need = lo >= 0 ? 2 : 1;
    if (!cc_drawable(c) || n + need + 1 > cap || n + need > CC_LINE_MAX) return -1;
    if (lo >= 0) {
        raw[n] = 0x81;
        raw[n + 1] = (cc_u8)lo;
    } else {
        raw[n] = c;
    }
    raw[n + need] = 0;
    if (cc_sis_bytes(raw) > CC_SIS_MAX) {
        raw[n] = 0;
        return -1;
    }
    return n + need;
}

int cc_line(cc_u8 *raw, int cap, const char *name, int pidx, const char *text, cc_s32 id)
{
    int n = 0, chars = 0, k, starts[CC_LINE_MAX + 1], cut = 0, m, t0;
    const char *sep = id == CP_ID_DISABLED ? " " : ": ";
    if (!raw || cap < 8) return 0;
    raw[0] = 0;
    for (k = 0; name && name[k] && chars < CC_NAME_CHARS; k++) {
        m = cc_put_char(raw, n, cap, name[k]);
        if (m < 0) continue;              /* not drawable: skipped */
        n = m;
        chars++;
    }
    while (n > 0 && raw[n - 1] == ' ') raw[--n] = 0, chars--;
    if (chars == 0) {                     /* no drawable name: the port label (cap >= 8 always holds it) */
        n = cc_put_char(raw, n, cap, 'P');
        n = cc_put_char(raw, n, cap, (char)('1' + (pidx & 3)));
        chars = 2;
    }
    for (k = 0; sep[k]; k++) {
        m = cc_put_char(raw, n, cap, sep[k]);
        if (m < 0) return chars;          /* a tiny buffer: the name alone */
        n = m;
        chars++;
    }
    if (id == CP_ID_DISABLED) text = CP_NOTICE_DISABLED;
    t0 = chars;
    for (k = 0; text && text[k]; k++) {
        if (!cc_drawable((cc_u8)text[k])) continue;
        starts[chars] = n;
        m = cc_put_char(raw, n, cap, text[k]);
        if (m < 0) { cut = 1; break; }
        n = m;
        chars++;
    }
    if (cut) {                            /* remove characters until ".." fits, then add it */
        for (;;) {
            int t = cc_put_char(raw, n, cap, '.');
            if (t >= 0) t = cc_put_char(raw, t, cap, '.');
            if (t >= 0) { n = t; chars += 2; break; }
            raw[n] = 0;
            if (chars <= t0) break;       /* nothing of the text left to remove (cannot happen with our limits) */
            chars--;
            n = starts[chars];
            raw[n] = 0;
        }
    }
    raw[n] = 0;
    return chars;
}

int cc_wrap(const char *text, int width, char *row1, char *row2, int cap)
{
    int len = 0, split, i, k;
    row1[0] = row2[0] = 0;
    if (!text || width < 4 || cap < 4) return 1;
    while (text[len]) len++;
    if (len <= width && len < cap) {
        for (i = 0; i <= len; i++) row1[i] = text[i];
        return 1;
    }
    split = width < cap - 1 ? width : cap - 1;
    for (i = split; i > width / 2; i--)   /* the last space in the first row, if it is not too early */
        if (text[i] == ' ') { split = i; break; }
    for (i = 0; i < split; i++) row1[i] = text[i];
    while (i > 0 && row1[i - 1] == ' ') i--;
    row1[i] = 0;
    while (text[split] == ' ') split++;
    for (k = 0; text[split + k] && k < width && k < cap - 1; k++) row2[k] = text[split + k];
    row2[k] = 0;
    if (text[split + k]) {                /* still too long: cut the second row with ".." */
        if (k > 2) k -= 2;
        row2[k] = '.';
        row2[k + 1] = '.';
        row2[k + 2] = 0;
    }
    return 2;
}

/* ---- test mode ------------------------------------------------------------------------------------------ */
int cc_demo_step(cc_u32 frame, cc_u32 period, int *arg)
{
    cc_u32 k;
    *arg = 0;
    if (period == 0 || frame % period != 0) return CC_DEMO_NONE;
    k = (frame / period) % CC_DEMO_STEPS;
    if (k < CP_N) {
        *arg = (int)k;
        return (k & 1) ? CC_DEMO_SUB_OPP : CC_DEMO_SUB_OWN;
    }
    if (k == CP_N) return CC_DEMO_NOTICE;
    if (k < CP_N + 1 + 4) {
        *arg = cp_menu_bits[k - CP_N - 1];
        return CC_DEMO_WINDOW;
    }
    return CC_DEMO_LONG;
}
