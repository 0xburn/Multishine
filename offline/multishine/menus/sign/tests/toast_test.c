/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the menu toast model (toast.c): countdown from msLeft, the accept window with its grace, expiry, newest challenge wins,
   a challenge pushes a notice back, the notice queue (dedup, cap, oldest dropped), Z on a friend request, the post-fight prompt once per match, results, texts and widths. */
#include <stdio.h>
#include <string.h>
#include "../toast.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static void txt(const struct tst *t, unsigned long now, char *a, char *b, int *line) { *line = tst_text(t, now, a, b); }

int main(void)
{
    static struct tst T;
    char a[TST_LINE + 1], b[TST_LINE + 1];
    int line;
    unsigned long now = 0xFFFFF000ul;                               /* near the 32-bit wrap: every comparison must survive it */
    unsigned i;
    static const unsigned char m1[8] = { 1, 2, 3, 4, 5, 6, 7, 8 }, m2[8] = { 9, 9, 9, 9, 9, 9, 9, 9 };
    tst_init(&T);
    CHECK(!tst_on(&T));
    /* a challenge: texts, the line shrinks with msLeft, Z inside the window accepts */
    tst_invite(&T, now, 41, "TEST#1", "Nigzel", 20000);
    CHECK(tst_on(&T) && T.sfx == TST_SFX_SHOW);
    txt(&T, now, a, b, &line);
    CHECK(!strcmp(a, "Challenge: Nigzel"));
    CHECK(!strcmp(b, "Z: Play Mano a mano"));
    CHECK(line == 1000);
    txt(&T, now + 5000, a, b, &line);
    CHECK(line == 750);
    tst_invite(&T, now + 3000, 41, "TEST#1", "Nigzel", 12000);      /* the next tick: the server says 12 s left (no clock sync): the end follows the server */
    txt(&T, now + 3000, a, b, &line);
    CHECK(line == 600);
    tst_frame(&T, now + 3100, 0, 0);
    CHECK(T.act == TST_A_NONE && tst_on(&T));
    tst_frame(&T, now + 3200, 1, 0);
    CHECK(T.act == TST_A_INVITE && T.cur.id == 41 && T.cur.busy && T.accepted == 1);
    txt(&T, now + 3200, a, b, &line);
    CHECK(!strcmp(b, "Starting...") && line == 0);
    tst_frame(&T, now + 99999, 0, 0);
    CHECK(tst_on(&T));                                              /* busy: only the glue's answer ends it */
    tst_done(&T, now + 99999, 1, 0);
    CHECK(!tst_on(&T) && T.act == TST_A_NONE);
    /* expiry just dismisses; Z in the grace after the line reached zero still counts, after it not */
    now += 100000;
    tst_invite(&T, now, 42, "AB#2", "", 20000);                     /* no display name: the code stands in */
    txt(&T, now, a, b, &line);
    CHECK(!strcmp(a, "Challenge: AB#2"));
    tst_frame(&T, now + 20000, 0, 0);
    txt(&T, now + 20000, a, b, &line);
    CHECK(tst_on(&T) && line == 0);
    tst_frame(&T, now + 20000 + TST_GRACE_MS - 1, 1, 0);
    CHECK(T.act == TST_A_INVITE);
    tst_done(&T, now + 11000, 0, "The challenge is over");          /* the server said INVITE_GONE: a result line */
    txt(&T, now + 11000, a, b, &line);
    CHECK(!strcmp(a, "The challenge is over") && line == -1 && T.sfx == TST_SFX_DENY);
    tst_frame(&T, now + 11000 + TST_INFO_MS, 0, 0);
    CHECK(!tst_on(&T));
    now += 100000;
    tst_invite(&T, now, 43, "AB#2", "Ab", 20000);
    tst_frame(&T, now + 20000 + TST_GRACE_MS, 1, 0);                   /* too late: dismissed, nothing raised */
    CHECK(!tst_on(&T) && T.act == TST_A_NONE && T.expired >= 1);
    tst_invite(&T, now, 44, "AB#2", "Ab", 0);                        /* a window already over is never shown */
    CHECK(!tst_on(&T));
    /* newest challenge wins */
    now += 100000;
    tst_invite(&T, now, 50, "AA#1", "First", 20000);
    tst_invite(&T, now + 500, 51, "BB#1", "Second", 20000);
    CHECK(T.cur.id == 51 && T.qn == 0);
    tst_frame(&T, now + 500 + 20000 + TST_GRACE_MS, 0, 0);
    CHECK(!tst_on(&T));
    /* notices: queue, one at a time, a challenge pushes the notice back, dedup, cap */
    now += 100000;
    tst_notice(&T, now, TST_K_REQUEST, "CC#3", "Carol");
    tst_notice(&T, now, TST_K_ACCEPTED, "DD#4", "Dave");
    tst_notice(&T, now, TST_K_ACCEPTED, "DD#4", "Dave");             /* twice: once */
    CHECK(T.cur.kind == TST_K_REQUEST && T.qn == 1);
    txt(&T, now, a, b, &line);
    CHECK(!strcmp(a, "Request: Carol") && !strcmp(b, "Hold Z: Add friend") && line == -1);
    tst_invite(&T, now + 100, 60, "EE#5", "Eve", 20000);
    CHECK(T.cur.kind == TST_K_INVITE && T.qn == 2 && T.q[0].kind == TST_K_REQUEST);
    tst_frame(&T, now + 100 + 20000 + TST_GRACE_MS, 0, 0);              /* the challenge ran out: the request comes back with a fresh time */
    CHECK(T.cur.kind == TST_K_REQUEST && T.qn == 1);
    tst_frame(&T, now + 11200, 1, 1);                                /* a TAP of Z on the request: nothing is accepted, nothing raised */
    tst_frame(&T, now + 11230, 0, 0);
    CHECK(T.act == TST_A_NONE && !T.cur.busy && T.cur.kind == TST_K_REQUEST);
    tst_frame(&T, now + 11300, 0, 1);                                /* hold: starts at the press, the line fills */
    txt(&T, now + 11300, a, b, &line); CHECK(line == -1);            /* just started: no line yet */
    tst_frame(&T, now + 11300 + 500, 0, 1);
    txt(&T, now + 11300 + 500, a, b, &line); CHECK(line == 500 && !strcmp(b, "Hold Z: Add friend"));
    tst_frame(&T, now + 11300 + 700, 0, 0);                          /* released early: cancelled, nothing raised, the toast stays */
    CHECK(T.act == TST_A_NONE && !T.cur.busy && T.hold.pm == 0);
    txt(&T, now + 11300 + 700, a, b, &line); CHECK(line == -1);
    tst_frame(&T, now + 12100, 0, 1);                                /* again: a full hold */
    tst_frame(&T, now + 12100 + FRD_HOLD_MS - 1u, 0, 1);
    CHECK(T.act == TST_A_NONE && !T.cur.busy);
    tst_frame(&T, now + 12100 + FRD_HOLD_MS, 0, 1);                  /* completion fires ONCE */
    CHECK(T.act == TST_A_FRIEND && T.cur.busy && T.sfx == TST_SFX_OK);
    tst_frame(&T, now + 12100 + FRD_HOLD_MS + 50, 0, 1);
    CHECK(T.act == TST_A_FRIEND && T.sfx == TST_SFX_OK);          /* still held: nothing more is raised (busy) */
    txt(&T, now + 11200, a, b, &line);
    CHECK(!strcmp(b, "Accepting..."));
    tst_invite(&T, now + 11300, 61, "FF#6", "Fay", 20000);           /* a challenge while an answer is pending waits right behind it */
    CHECK(T.cur.kind == TST_K_REQUEST && T.q[0].kind == TST_K_INVITE);
    tst_done_friends(&T, now + 12400);                               /* the answer went through: a 3 s confirmation */
    CHECK(T.cur.kind == TST_K_INFO && T.sfx == TST_SFX_SHOW);
    txt(&T, now + 12400, a, b, &line);
    CHECK(!strcmp(a, "New friend: Carol") && line == -1);
    tst_frame(&T, now + 12400 + TST_FRIENDS_MS - 1u, 0, 0);
    CHECK(T.cur.kind == TST_K_INFO);
    tst_frame(&T, now + 12400 + TST_FRIENDS_MS, 0, 0);
    CHECK(T.cur.kind == TST_K_INVITE && T.cur.id == 61);
    tst_frame(&T, now + 40000, 0, 0);
    CHECK(T.cur.kind == TST_K_ACCEPTED);
    txt(&T, now + 40000, a, b, &line);
    CHECK(!strcmp(a, "New friend: Dave"));
    tst_frame(&T, now + 40000 + TST_INFO_MS, 0, 0);
    CHECK(!tst_on(&T) && T.qn == 0);
    for (i = 0; i < 7u; i++) { char c[9]; snprintf(c, sizeof c, "Q%u#1", i); tst_notice(&T, now + 50000, TST_K_ACCEPTED, c, c); }
    CHECK(T.qn == TST_QN && !strcmp(T.cur.code, "Q0#1") && !strcmp(T.q[0].code, "Q3#1"));   /* 1 shown + 4 queued, the oldest queued dropped */
    tst_clear(&T);
    CHECK(!tst_on(&T) && T.qn == 0);
    /* post-fight prompt: once per match id, Z raises FIGHT, the result line */
    now += 100000;
    CHECK(tst_fight(&T, now, m1, "Nigzel") == 1);
    txt(&T, now, a, b, &line);
    CHECK(!strcmp(a, "Add Nigzel?"));
    CHECK(tst_fight(&T, now + 10, m1, "Nigzel") == 0);
    tst_frame(&T, now + 100, 1, 0);
    CHECK(T.act == TST_A_FIGHT && !memcmp(T.cur.mid, m1, 8));
    tst_done(&T, now + 200, 1, "Friend request sent");
    txt(&T, now + 200, a, b, &line);
    CHECK(!strcmp(a, "Friend request sent"));
    tst_frame(&T, now + 200 + TST_INFO_MS, 0, 0);
    CHECK(!tst_on(&T));
    CHECK(tst_fight(&T, now + 300000, m2, "") == 1);
    txt(&T, now + 300000, a, b, &line);
    CHECK(!strcmp(a, "Add your opponent?"));
    tst_frame(&T, now + 300000 + TST_FIGHT_MS, 0, 0);
    CHECK(!tst_on(&T));
    /* WHEN the prompt may come: a recorded match of Grandmaster / Mano a mano / Game Night against a human, never as a guest */
    CHECK(tst_fight_allowed(TST_FM_RANKED, 1, "TEST#1", 0) == 1);
    CHECK(tst_fight_allowed(TST_FM_DIRECT, 1, "TEST@12", 0) == 1);
    CHECK(tst_fight_allowed(TST_FM_ROOM, 1, "AB#7", 0) == 1);
    CHECK(tst_fight_allowed(TST_FM_RANKED, 1, "NO1@0042", 0) == 0);   /* a Sandbag is never offered */
    CHECK(tst_fight_allowed(TST_FM_DIRECT, 1, "G0@12345", 0) == 0);
    CHECK(tst_fight_allowed(TST_FM_ROOM, 0, "AB#7", 0) == 0);          /* no match ended yet: the character screen BEFORE the fight */
    CHECK(tst_fight_allowed(1, 1, "AB#7", 0) == 0);                     /* Dojo (Unranked) */
    CHECK(tst_fight_allowed(6, 1, "AB#7", 0) == 0);                     /* the console queue */
    CHECK(tst_fight_allowed(TST_FM_ROOM, 1, "BOT#101", 0) == 0);        /* the lab bot playing for a scripted member */
    CHECK(tst_fight_allowed(TST_FM_RANKED, 1, "BOT@1", 0) == 0);
    CHECK(tst_fight_allowed(TST_FM_RANKED, 1, "BOTA#1", 0) == 1);       /* only the tag BOT itself */
    CHECK(tst_fight_allowed(TST_FM_DIRECT, 1, "AB#7", 1) == 0);         /* a guest */
    CHECK(tst_fight_allowed(TST_FM_DIRECT, 1, "", 0) == 0);
    CHECK(tst_fight_allowed(TST_FM_DIRECT, 1, 0, 0) == 0);
    /* widths: the longest texts (a 12-character name) stay at 24 characters: the half-width plate */
    tst_invite(&T, now + 400000, 70, "ABCDEFG#", "WWWWWWWWWWWW", 20000);
    txt(&T, now + 400000, a, b, &line);
    CHECK(strlen(a) <= TST_LINE && strlen(a) == 23);
    tst_clear(&T);
    tst_notice(&T, now, TST_K_ACCEPTED, "ABCDEFG#", "WWWWWWWWWWWW");
    txt(&T, now, a, b, &line);
    CHECK(strlen(a) == 24);
    /* Slippi Direct and Game Night room notices: texts, widths, a tap of Z within the 10 s window raises the action, expiry dismisses, dedup */
    tst_clear(&T);
    now += 500000;
    tst_note(&T, now, TST_K_DIRECT, "ABCD#123", "", 0);
    CHECK(tst_on(&T) && T.cur.kind == TST_K_DIRECT && T.sfx == TST_SFX_SHOW);
    txt(&T, now, a, b, &line);
    CHECK(!strcmp(a, "Slippi Direct: ABCD#123") && !strcmp(b, "Z: Play") && line == -1);
    tst_frame(&T, now + 100, 0, 0);
    CHECK(T.act == TST_A_NONE);
    tst_frame(&T, now + 200, 1, 0);
    CHECK(T.act == TST_A_DIRECT && T.cur.busy);
    txt(&T, now + 200, a, b, &line);
    CHECK(!strcmp(b, "Starting..."));
    tst_done(&T, now + 300, 1, 0);
    CHECK(!tst_on(&T) && T.act == TST_A_NONE);
    tst_note(&T, now + 400, TST_K_ROOM, "4242", "Nigzel", 0);
    txt(&T, now + 400, a, b, &line);
    CHECK(!strcmp(a, "Nigzel: Room 4242") && !strcmp(b, "Z: Join Game Night"));
    tst_note(&T, now + 410, TST_K_ROOM, "4242", "Nigzel", 0);          /* the same notice twice: once */
    CHECK(T.qn == 0);
    tst_frame(&T, now + 500, 1, 0);
    CHECK(T.act == TST_A_ROOM && T.cur.busy && !strcmp(T.cur.code, "4242"));
    tst_done(&T, now + 600, 1, 0);
    tst_note(&T, now + 700, TST_K_ROOM, "0702", "WWWWWWWWWWWW", 1);
    txt(&T, now + 700, a, b, &line);
    CHECK(!strcmp(a, "WWWWWWWWWWWW's Room 0702") && !strcmp(b, "Z: Join"));
    tst_frame(&T, now + 700 + TST_NOTE_MS - 1u, 0, 0);
    CHECK(tst_on(&T));
    tst_frame(&T, now + 700 + TST_NOTE_MS, 0, 0);                      /* the window ends: dismissed, no action */
    CHECK(!tst_on(&T) && T.act == TST_A_NONE);
    tst_note(&T, now + 20000, TST_K_ROOM, "", "X", 0);                  /* no code: nothing */
    CHECK(!tst_on(&T));
    tst_note(&T, now + 20000, TST_K_REQUEST, "ABC#1", "X", 0);          /* only the two kinds */
    CHECK(!tst_on(&T));
    tst_clear(&T);
    tst_note(&T, now + 30000, TST_K_ROOM, "9999", "WWWWWWWWWWWW", 0);   /* widest texts fit the line */
    txt(&T, now + 30000, a, b, &line);
    CHECK(strlen(a) <= TST_LINE && strlen(b) <= TST_LINE);
    tst_clear(&T);
    tst_note(&T, now + 30000, TST_K_DIRECT, "ABCDEFG#", "", 0);
    txt(&T, now + 30000, a, b, &line);
    CHECK(strlen(a) <= TST_LINE);
    /* a friend's message: "Name: text", one line, no key, no de-duplication, own time, a Z does nothing */
    tst_clear(&T);
    tst_msg(&T, now + 40000, "PAT@1", "Pat", "Good games");
    txt(&T, now + 40000, a, b, &line);
    CHECK(T.cur.kind == TST_K_MSG && !strcmp(a, "Pat: Good games") && !b[0] && line < 0);
    tst_msg(&T, now + 40001, "PAT@1", "Pat", "one more?");              /* the same friend again: queued, not dropped */
    CHECK(T.qn == 1);
    tst_frame(&T, now + 40100, 1, 0);                                    /* Z on a message toast: nothing */
    CHECK(T.act == TST_A_NONE && !T.cur.busy);
    tst_frame(&T, now + 40000 + TST_MSG_MS - 1u, 0, 0);
    CHECK(!strcmp(T.cur.text, "Good games"));
    tst_frame(&T, now + 40000 + TST_MSG_MS, 0, 0);                     /* ends: the next one, its own time */
    txt(&T, now + 40000 + TST_MSG_MS, a, b, &line);
    CHECK(!strcmp(a, "Pat: one more?"));
    tst_frame(&T, now + 40000 + 2u * TST_MSG_MS - 1u, 0, 0);
    CHECK(tst_on(&T));
    tst_frame(&T, now + 40000 + 2u * TST_MSG_MS, 0, 0);
    CHECK(!tst_on(&T));
    tst_msg(&T, now + 50000, "PAT@1", "WWWWWWWWWWWW", "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW");   /* the longest: cut inside the line */
    txt(&T, now + 50000, a, b, &line);
    CHECK(strlen(a) <= TST_LINE);
    tst_clear(&T);
    tst_msg(&T, now + 60000, "PAT@1", "Pat", "");                         /* empty: nothing */
    CHECK(!tst_on(&T));
    tst_clear(&T);
    tst_msg(&T, now + 70000, "PAT@1", "Pat", "unseen?");                /* a message whose toast cannot be drawn (a page of ours holds the menu) keeps its 5 s for after */
    tst_hold(&T, 8000u);                                              /* 8 s suppressed: the clock stood still */
    tst_frame(&T, now + 70000 + TST_MSG_MS + 7999u, 0, 0);
    CHECK(tst_on(&T) && !strcmp(T.cur.text, "unseen?"));
    tst_frame(&T, now + 70000 + TST_MSG_MS + 8000u, 0, 0);
    CHECK(!tst_on(&T));
    tst_done(&T, now + 80000, 1, "x");                                /* (an info toast is not held) */
    tst_hold(&T, 8000u);
    CHECK(T.cur.until == now + 80000 + TST_INFO_MS);
    /* the sticky status line: a live countdown refreshes the text without a new toast, a sound or a restart; a real toast hides it; clearing removes it */
    tst_init(&T); now = 1000000ul;
    tst_status(&T, "Waiting for the other player... 50s");
    CHECK(!tst_on(&T) && !strcmp(tst_status_text(&T), "Waiting for the other player... 50s") && T.status_dirty == 1u && T.sfx == TST_SFX_NONE && T.shown == 0u);
    T.status_dirty = 0;
    tst_status(&T, "Waiting for the other player... 50s");              /* the same text: nothing changes */
    CHECK(T.status_dirty == 0u);
    tst_status(&T, "Waiting for the other player... 49s");              /* a second later: the text only */
    CHECK(T.status_dirty == 1u && !strcmp(tst_status_text(&T), "Waiting for the other player... 49s") && T.sfx == TST_SFX_NONE && T.shown == 0u && T.qn == 0u && !tst_on(&T));
    tst_notice(&T, now, TST_K_REQUEST, "CC#3", "Carol");               /* a real toast comes: it hides the status line, the status keeps counting */
    CHECK(tst_on(&T) && !strcmp(tst_status_text(&T), "") && T.sfx == TST_SFX_SHOW);
    T.sfx = TST_SFX_NONE;
    tst_status(&T, "Waiting for the other player... 48s");
    CHECK(T.sfx == TST_SFX_NONE && !strcmp(tst_status_text(&T), ""));  /* no sound for a refresh, still hidden */
    tst_clear(&T);
    CHECK(!strcmp(tst_status_text(&T), "") && !tst_on(&T));            /* leaving the screen drops it */
    tst_status(&T, "Not answered"); tst_status(&T, 0);
    CHECK(!strcmp(tst_status_text(&T), ""));                           /* 0 / "" ends it */
    { unsigned i; char big[80]; for (i = 0; i < 79u; i++) big[i] = 'x'; big[79] = 0; tst_status(&T, big); CHECK(strlen(tst_status_text(&T)) == TST_LINE); }   /* one line at most */
    if (fails) { printf("%d failures\n", fails); return 1; }
    printf("toast model: all checks passed\n");
    return 0;
}
