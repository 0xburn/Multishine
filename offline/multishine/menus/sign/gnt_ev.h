/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The Tournament screens on the retail Event Match screen (gnt_ev.c): CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  Same life cycle as frd_ev.h. */
#ifndef GNT_EV_H
#define GNT_EV_H
#include "gnt.h"

enum { GNTEV_IDLE, GNTEV_LAUNCH, GNTEV_UP, GNTEV_CLOSING, GNTEV_CLOSED, GNTEV_FAILED };

int gntev_open(struct gnt *f);          /* 0 = launching (the next menu proc pass builds the screen), < 0 = cannot (the text view is used) */
void gntev_frame(void);                 /* once per overlay frame while open */
void gntev_close(void);                 /* leave: back to the screen we came from at our think's next run */
int gntev_state(void);
void gntev_reset(void);
void gntev_abort(void);                 /* the overlay is being freed: our think replaced by a retail no-op, our texts freed */
unsigned gntev_err(void);
#endif
