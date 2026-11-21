/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the D-Pad quick-chat picker (qc_pick.h): the rules, and the SAME decisions as the chat lab's picker (offline/chat/chat_core.c cc_ui_frame, Slippi's chat window)
   on random pad sequences: the group, the text index of a pick, B, the opening delay. Build with -I<generated dir of > and chat_core.c. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../qc_pick.h"
#include "../../../../chat/chat_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    struct qc q;
    struct cc_ui u;
    unsigned idx = 0, r, i, t, seed;
    char txt[QC_N][QC_TXT], cell[32];
    unsigned long now = 4000000000ul;                                 /* near the 32-bit wrap: the clock must be wrap safe */
    for (i = 0; i < QC_N; i++) { txt[i][0] = 'a' + (char)i; txt[i][1] = 0; }
    strcpy(txt[2], "A very long quick chat text");
    qc_init(&q);
    /* nothing happens without a D-Pad press; B alone does not open */
    CHECK(qc_frame(&q, 0, now, 1000, 1, &idx) == QC_NONE && !q.open);
    CHECK(qc_frame(&q, QC_B, now, 1000, 1, &idx) == QC_NONE && !q.open);
    /* the first press opens that direction's group; several at once: Up, Left, Right, Down in that order */
    CHECK(qc_frame(&q, QC_DOWN | QC_RIGHT, now, 1000, 1, &idx) == QC_OPENED && q.group == 2u);
    CHECK(!strcmp(qc_group_name(q.group), "Right"));
    qc_cell(&q, (const char (*)[QC_TXT])txt, 0, 15, cell); CHECK(!strcmp(cell, "i"));        /* group Right = texts 8..11 */
    qc_cell(&q, (const char (*)[QC_TXT])txt, 3, 15, cell); CHECK(!strcmp(cell, "l"));
    /* B closes */
    CHECK(qc_frame(&q, QC_B, now + 150, 1000, 1, &idx) == QC_CLOSED && !q.open);
    /* group Up, pick Down at once later = index 3; blocked while a message is in flight; the delay starts again */
    CHECK(qc_frame(&q, QC_UP, now + 1000, 1000, 1, &idx) == QC_OPENED);
    qc_cell(&q, (const char (*)[QC_TXT])txt, 2, 15, cell); CHECK(!strcmp(cell, "A very long qu.") && strlen(cell) == 15u);
    CHECK(qc_frame(&q, QC_DOWN, now + 1300, 1000, 0, &idx) == QC_BLOCKED && q.open);
    CHECK(qc_frame(&q, QC_DOWN, now + 1400, 1000, 1, &idx) == QC_TOO_SOON);          /* the delay after a refused pick */
    r = qc_frame(&q, QC_DOWN, now + 1600, 1000, 1, &idx);
    CHECK(r == (QC_SENT | QC_CLOSED) && idx == 3u && !q.open);
    /* idle: 3 s after the last activity, not before (across the wrap of the clock) */
    now = 0xFFFFFF00ul;
    CHECK(qc_frame(&q, QC_LEFT, now, 1000, 1, &idx) == QC_OPENED);
    CHECK(qc_frame(&q, 0, now + 2999, 1000, 1, &idx) == QC_NONE && q.open);
    CHECK(qc_frame(&q, 0, now + 3000, 1000, 1, &idx) == QC_CLOSED && !q.open);
    /* a cell of a closed picker is empty; every slot of every group is reachable */
    qc_cell(&q, (const char (*)[QC_TXT])txt, 0, 15, cell); CHECK(!cell[0]);
    for (i = 0; i < 16u; i++) {
        static const unsigned P[4] = { QC_UP, QC_LEFT, QC_RIGHT, QC_DOWN };
        qc_init(&q);
        CHECK(qc_frame(&q, P[i / 4u], 0, 600, 1, &idx) == QC_OPENED);
        CHECK(qc_frame(&q, P[i % 4u], 1, 600, 1, &idx) == (QC_SENT | QC_CLOSED) && idx == i);      /* at once: Up, Up sends the first text */
    }
    /* the same decisions as the chat lab's window (60 frames a second): random pad edges for 140 frames (the lab closes at 2.5 s, ours at 3 s: compared inside both) */
    for (seed = 1; seed <= 400u; seed++) {
        unsigned f, same_ok = 1, just_opened = 0;
        srand(seed);
        qc_init(&q);
        memset(&u, 0, sizeof u);
        for (f = 0; f < 140u && same_ok; f++) {
            unsigned pressed = 0;
            cc_s32 sid = 0;
            int a, ours_sent = 0, lab_sent = 0;
            static const unsigned B[5] = { QC_UP, QC_LEFT, QC_RIGHT, QC_DOWN, QC_B };
            if (rand() % 9 == 0) pressed = B[rand() % 5] | ((rand() % 6 == 0) ? B[rand() % 4] : 0u);
            if (just_opened) pressed = 0;                      /* (the lab builds its text on the frame after opening and ignores the pad then: compared without that frame) */
            a = cc_ui_frame(&u, pressed, 1, &sid);
            lab_sent = (a & CC_UI_SENT) != 0;
            t = qc_frame(&q, pressed, f, 60, 1, &idx);
            ours_sent = (t & QC_SENT) != 0;
            just_opened = (t & QC_OPENED) != 0;
            if (lab_sent != ours_sent) same_ok = 0;
            else if (lab_sent && cc_index_of(sid) != (int)idx) same_ok = 0;
            else if (!!u.open != !!q.open) same_ok = 0;
            else if (u.open && u.group != (int)(q.group == 0 ? 8 : q.group == 1 ? 1 : q.group == 2 ? 2 : 4)) same_ok = 0;
        }
        CHECK(same_ok);
    }
    if (fails) { printf("%d failures\n", fails); return 1; }
    printf("quick chat picker: all checks passed\n");
    return 0;
}
