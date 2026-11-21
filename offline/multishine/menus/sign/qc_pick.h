/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * qc_pick: the D-Pad quick-chat picker, the state machine only ("send via website and quick commands. Like in slippi").  Pure C89, no libc, no heap, no
 * Melee address, no network, no drawing: the Friends list (frd.c) and the Game Night room (gnb.c) each own one `struct qc`, feed it the pad every frame and draw its cross.
 * It is the picker of the older S14 / chat lab line, ported with the same rules (offline/chat/chat_core.c cc_ui_frame, itself Slippi's chat window, UpdateChatWindow):
 *   - the first D-Pad press (Up, Left, Right, Down; in that order when several are pressed at once) opens the GROUP of that direction;
 *   - the second D-Pad press picks one of the four texts of the group: index = group * 4 + position, both in menu order Up, Left, Right, Down (chat_presets.json "menu_order"),
 *     the 16 texts are the player's chat texts (mnc own list) or the defaults; the same 16 slots as the chat lab, so a text keeps its place everywhere;
 *   - B closes; so does a pause of QC_IDLE_S seconds without a pick (Slippi: 2.5 s; our default for the console is 3 s);
 *   - a pick may follow the opening at once (Slippi: Up, Up sends the first text of the Up group); a pick refused because a message is still in flight keeps the picker open and ignores
 *     further picks for a fifth of a second (Slippi's 0.2 s delay after a blocked send).
 * Header-only (like keyhint.h): each screen that uses it compiles its own copy, no link lists.  Time is the caller's clock in any unit (the time base ticks of the menu overlays); `sec` is one second in that unit.  Host test: tests/qc_test.c (also against cc_ui_frame).
 */
#ifndef QC_PICK_H
#define QC_PICK_H

#define QC_LEFT 0x0001u            /* the pad bits of HSD_PadStatus.button (frd.h FRD_B_*) */
#define QC_RIGHT 0x0002u
#define QC_DOWN 0x0004u
#define QC_UP 0x0008u
#define QC_B 0x0200u
#define QC_DPAD (QC_LEFT | QC_RIGHT | QC_DOWN | QC_UP)
#define QC_N 16u                   /* texts: 4 groups of 4 */
#define QC_TXT 26u                 /* characters of a text with its end (chat_core.h CC_TXT, mnc MNC_TXT) */
#define QC_IDLE_S 3u               /* closes after this many seconds without a pick */
#define QC_GAP_DIV 5u              /* the pick delay after a refused pick: one second / 5 */

enum { QC_NONE = 0, QC_OPENED = 1, QC_CLOSED = 2, QC_SENT = 4, QC_BLOCKED = 8, QC_TOO_SOON = 16 };

struct qc {
    unsigned char open;            /* 1 while a group is shown */
    unsigned char group;           /* 0 Up, 1 Left, 2 Right, 3 Down (menu order) */
    unsigned long t0, t_open;      /* the last activity / the opening, caller's clock */
};

/* qc_frame: one frame.  pressed = the pad bits that went down THIS frame (rising edges), now = the clock, sec = one second of it, can_send = 0 refuses a pick (a message is still in flight):
   the picker stays open and says QC_BLOCKED.  Returns QC_* flags; with QC_SENT, *idx = the text slot 0..15.
   qc_cell: the text drawn in cell `pos` (0 Up, 1 Left, 2 Right, 3 Down: the cross) of the open group, cut to `max` characters (a longer text ends with a dot); "" when nothing is open or the slot is empty. */
#ifdef __GNUC__
#define QC_FN static __attribute__((unused))
#else
#define QC_FN static
#endif
QC_FN int elapsed(unsigned long now, unsigned long at, unsigned long span) { return ((now - at) & 0xFFFFFFFFul) >= span; }

QC_FN void qc_init(struct qc *q) { q->open = 0; q->group = 0; q->t0 = 0; q->t_open = 0; }
QC_FN void qc_close(struct qc *q) { q->open = 0; }

/* the first direction pressed, in menu order (chat_core.c dpad_input): Up, Left, Right, Down -> 1..4, 0 = none */
QC_FN unsigned dir_of(unsigned pressed)
{
    if (pressed & QC_UP) return 1u;
    if (pressed & QC_LEFT) return 2u;
    if (pressed & QC_RIGHT) return 3u;
    if (pressed & QC_DOWN) return 4u;
    return 0u;
}

QC_FN unsigned qc_frame(struct qc *q, unsigned pressed, unsigned long now, unsigned long sec, int can_send, unsigned *idx)
{
    unsigned d = dir_of(pressed);
    if (!q->open) {
        if (!d) return QC_NONE;
        q->open = 1; q->group = (unsigned char)(d - 1u); q->t0 = now; q->t_open = now - sec / QC_GAP_DIV;      /* (the delay has already passed) */
        return QC_OPENED;
    }
    if (d) {
        if (!elapsed(now, q->t_open, sec / QC_GAP_DIV)) return QC_TOO_SOON;
        if (can_send) {
            *idx = (unsigned)q->group * 4u + (d - 1u);
            q->open = 0;
            return QC_SENT | QC_CLOSED;
        }
        q->t_open = now; q->t0 = now;                        /* refused: stays open, the delay starts again (Slippi: the blocked sound) */
        return QC_BLOCKED;
    }
    if (pressed & QC_B) { q->open = 0; return QC_CLOSED; }
    if (elapsed(now, q->t0, sec * QC_IDLE_S)) { q->open = 0; return QC_CLOSED; }
    return QC_NONE;
}

QC_FN const char *qc_group_name(unsigned group)
{
    static const char *const N[4] = { "Up", "Left", "Right", "Down" };
    return N[group & 3u];
}

/* the 16 default texts, table order (group * 4 + place): THE SAME as offline/chat/chat_presets.json "messages" (the server posts its copy of the sender's list by this index; a host test,
   tests/test_qc_defaults.py, compares the two).  The hub client (mnc) is started without defaults on the console and fills every slot with ".", so the picker takes these. */
QC_FN const char *qc_default(unsigned i)
{
    static const char *const D[16] = {
        "Good games", "Another game?", "Back soon", "Best of luck!", "Nicely played", "Fun games", "Thank you", "So good",
        "My apologies", "My fault", "Haha", "Whoa!", "Have to go", "One moment", "Play again later", "Connection is bad" };
    return i < 16u ? D[i] : "";
}

QC_FN void qc_cell(const struct qc *q, const char (*txt)[QC_TXT], unsigned pos, unsigned max, char *out)
{
    const char *t;
    unsigned n = 0;
    out[0] = 0;
    if (!q->open || pos > 3u || !max) return;
    t = txt[(unsigned)q->group * 4u + pos];
    while (t[n]) n++;
    if (n <= max) { for (n = 0; t[n]; n++) out[n] = t[n]; out[n] = 0; return; }
    for (n = 0; n + 1u < max; n++) out[n] = t[n];
    out[n++] = '.';
    out[n] = 0;
}
#endif
