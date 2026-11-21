/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Menu toasts: the MODEL of the small boxes that come up over the Melee menus.  Pure C89, no libc, no
 * heap, no clock of its own (the caller passes milliseconds), no Melee address, no network.  One toast at a time:
 *   - a Mano a mano challenge: "<name> (<code>) challenges you" / "to Mano a mano.   Z: Accept" and a thin line that shrinks over the accept window (msLeft from the
 *     server at the fetch: no clock sync); Z inside the window (plus a short grace for the frame and the way back) raises TST_A_INVITE; expiry just dismisses;
 *     a newer challenge replaces the one on screen (newest wins); a challenge pushes a notice on screen back into the queue;
 *   - a friend request notice: "Friend request from <name>" / "<code>   Hold Z: Accept" and a thin line that FILLS along the hold (HOLD Z for FRD_HOLD_MS raises TST_A_FRIEND, the glue answers FRIEND_ANSWER yes, then "You and <name> are friends"; releasing early cancels, a tap does nothing);
 *   - "<name> accepted your friend request" (information, 4 s);
 *   - the post-fight prompt on the screen after a match of ours: "Z: Add <name> as a friend" (Z raises TST_A_FIGHT, FIGHT_FRIEND); once per match id;
 *   - "Slippi Direct: play <code>" / "Z: Start" (a note to your own console from the website: the glue starts a Direct search of that code);
 *   - "<name> invites you to Game Night room <code>" and "Join <name>'s room <code>" / "Z: Open Game Night" (the glue opens the Game Night browser straight in that room); all three
 *     are a tap of Z within TST_NOTE_MS, like a Mano a mano challenge;
 *   - a short result line after an action ("Friend request sent", "The challenge is over", ...).
 * Notices wait in a queue of TST_QN (the oldest is dropped).  Text: Melee's menu font through the SJIS expansion of the glue ('#' and parentheses need it).
 * The front ends: the hub overlay's menu glue (hub_soc.c, console) and the host tests (tests/toast_test.c).
 */
#ifndef TOAST_H
#define TOAST_H

#include "frd_hold.h"             /* the hold length (FRD_HOLD_MS,: 1.0 s) */
#define TST_INVITE_MS 20000u       /* the server's accept window (invite_ttl: 10 s -> 20 s) */
#define TST_GRACE_MS 1000u         /* Z still accepted this long after the line reached zero (the server allows 1.5 s for the network) */
#define TST_REQ_MS 8000u           /* a friend request notice stays this long */
#define TST_INFO_MS 4000u          /* "accepted your friend request", results */
#define TST_FRIENDS_MS 3000u       /* "You and <name> are friends" */
#define TST_FIGHT_MS 20000u        /* the post-fight prompt */
#define TST_NOTE_MS 20000u         /* the Slippi Direct and Game Night room notices: the window of a challenge */
#define TST_MSG_MS 5000u           /* a friend's message (receive on the console): "Name: text", one line, no key */
#define TST_MSG_TXT 34u            /* characters of a message kept for the toast (the line holds 44 with "Name: ") */
#define TST_QN 4u
#define TST_LINE 44u               /* characters of a toast line (the menu line of the Event view) */

enum { TST_K_NONE, TST_K_INVITE, TST_K_REQUEST, TST_K_ACCEPTED, TST_K_FIGHT, TST_K_INFO, TST_K_DIRECT, TST_K_ROOM, TST_K_MSG };
enum { TST_A_NONE, TST_A_INVITE, TST_A_FRIEND, TST_A_FIGHT, TST_A_DIRECT, TST_A_ROOM };          /* what the glue must do (tst.act), cleared by the glue */
enum { TST_SFX_NONE, TST_SFX_SHOW, TST_SFX_OK, TST_SFX_DENY };

struct tst_item {
    unsigned char kind, busy, friends;      /* busy: Z was pressed, the glue's answer is pending (the toast says so and stays) */
    unsigned long id;              /* invite id */
    char code[9], name[13];
    unsigned char mid[8];          /* post-fight: the match id */
    unsigned long t0, until;       /* local ms: shown at, ends at (an invite: the accept window's end) */
    const char *info;              /* TST_K_INFO: the text */
    char text[TST_MSG_TXT];        /* TST_K_MSG: the message text */
};

struct tst {
    struct tst_item cur;           /* cur.kind == TST_K_NONE: nothing shown */
    struct tst_item q[TST_QN];
    unsigned char qn;
    unsigned char act;             /* TST_A_* raised by Z */
    unsigned char sfx;             /* TST_SFX_* to play this frame (front end clears it) */
    unsigned char dirty;           /* the texts changed */
    unsigned char fight_done[8];   /* the last match id a post-fight prompt was shown for (one prompt per match) */
    unsigned char fight_have;
    struct frd_hold hold;          /* the friend request toast: hold Z (FRD_HOLD_MS); progress hold.pm permille */
    unsigned long shown, accepted, expired;   /* counters (RAM proof) */
    char status[TST_LINE + 1];     /* the sticky status line (tst_status): "Waiting for the other player... 49s" and the like; no queue, no clock, no sound */
    unsigned char status_dirty;    /* the status text changed since the front end last drew it */
};

void tst_init(struct tst *t);
/* a challenge from the menu tick: ms_left = the server's msLeft at the fetch.  The same id again only refreshes the end (the server is the clock). */
void tst_invite(struct tst *t, unsigned long now, unsigned long id, const char *code, const char *name, unsigned ms_left);
/* a notice (kind TST_K_REQUEST or TST_K_ACCEPTED) */
void tst_notice(struct tst *t, unsigned long now, unsigned kind, const char *code, const char *name);
/* a Direct / room notice (kind TST_K_DIRECT: code = the Slippi code; kind TST_K_ROOM: code = the room code, name = who, join = 1 for the "Join <name>'s room" wording) */
void tst_note(struct tst *t, unsigned long now, unsigned kind, const char *code, const char *name, int join);
/* a friend's message (MSG_FETCH): shown once as "Name: text" for TST_MSG_MS.  Several from one friend all show (no de-duplication, unlike the notices); the queue drops the oldest when full. */
void tst_msg(struct tst *t, unsigned long now, const char *code, const char *name, const char *text);
/* the clock of a friend message stands still while the toast cannot be drawn (a page of ours holds the menu): dt = the milliseconds of that stretch.  The server marks a fetched message shown, so a message
   that ran out unseen is lost for good. */
void tst_hold(struct tst *t, unsigned long dt);
/* the post-fight prompt (returns 0 when this match already had its prompt) */
int tst_fight(struct tst *t, unsigned long now, const unsigned char mid[8], const char *name);
/* WHEN the post-fight prompt may come: only after a Multishine match that really ended (`recorded`: the game end (BD) of the
   match this ticket paired was seen), in Grandmaster (mode 0), Mano a mano (2) or a Game Night set (5) (the mnc MNC_MODE_* numbers; never Dojo / Unranked, never a
   Slippi or bridge match), against a human (an opponent code with the tag BOT is a lab seat or a bot: no prompt) and never as a guest.  1 = allowed. */
#define TST_FM_RANKED 0
#define TST_FM_DIRECT 2
#define TST_FM_ROOM 5
int tst_fight_allowed(int mode, int recorded, const char *opp_code, int guest);
/* one frame: z = Z was pressed this frame (the invite and the post-fight prompt: a tap), zh = Z is held down (the friend request: hold FRD_HOLD_MS) */
void tst_frame(struct tst *t, unsigned long now, int z, int zh);
/* the friend answer went through: "You and <name> are friends" (information, TST_FRIENDS_MS) */
void tst_done_friends(struct tst *t, unsigned long now);
/* the glue's answer to the raised action: ok = done; msg = the result line (0 = none: the toast just goes) */
void tst_done(struct tst *t, unsigned long now, int ok, const char *msg);
/* a sticky STATUS line (the yellow strip's momentary messages are toasts): the glue calls it every frame with the current text, or 0 / "" when there is none.  The same
   text again changes nothing; another text only refreshes the line (a live countdown): it is NOT a new toast, so no show sound and no restart (t->sfx is never touched here).  It does not
   count for tst_on (the real toasts and the keep-alive rules stay as they were) and a real toast on screen hides it (tst_status_text returns it only while none shows). */
void tst_status(struct tst *t, const char *text);
const char *tst_status_text(const struct tst *t);   /* the status to draw: "" while a real toast is on screen or none is set */
/* drop everything on screen and in the queue (leaving the menus) */
void tst_clear(struct tst *t);
/* the texts of the toast on screen (two lines, "" when none) and the line: permille of the accept window left (0..1000), -1 = no line */
int tst_text(const struct tst *t, unsigned long now, char *l0, char *l1);
int tst_on(const struct tst *t);
#endif
