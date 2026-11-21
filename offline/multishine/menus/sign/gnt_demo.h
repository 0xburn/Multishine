/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Demo cups for the Tournament screens: a double elimination bracket of 4..64 players with the server's own match numbering and routing
 * (the server's Bracket._build, mirrored here), a number of finished matches, the called and live matches and one human (you). Pure C89, no libc. Used by the host
 * tests and, only in a build with -DSG_TN_DEMO (a Dolphin proof overlay, never released), as the answer of the Tournament screens' network (sg_entry.c).
 */
#ifndef GNT_DEMO_H
#define GNT_DEMO_H
#include "gnt.h"

#ifdef GNT_DEMO_MANY
#define GNT_DEMO_CUPS 12u                 /* proof of a list past one 8-row page (cube reset with 11 cups): ids 4.. are 4-player cups */
#else
#define GNT_DEMO_CUPS 3u
#endif
/* the demo list (Demo 4, Demo 16, Demo 64) as the model's entries; returns the count */
unsigned gnt_demo_list(struct gnt_ent *e);
/* a bracket of `size` (a power of two, 4..64) players with `done` matches finished, entrant index `me` (0xFF = a spectator) the human; answers like gnt_br_begin .. gnt_br_end */
void gnt_demo_bracket(struct gnt *g, unsigned size, unsigned done, unsigned me);
/* the size of demo cup `id` (1..3: 4, 16, 64) and how far it is: finished matches */
unsigned gnt_demo_size(unsigned id);
unsigned gnt_demo_done(unsigned id);
#endif
