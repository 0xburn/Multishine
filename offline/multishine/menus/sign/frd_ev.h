/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The Friends screen on the retail Event Match screen (frd_ev.c): CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  Same life cycle as gnb_ev.h. */
#ifndef FRD_EV_H
#define FRD_EV_H
#include "frd.h"

enum { FRDEV_IDLE, FRDEV_LAUNCH, FRDEV_UP, FRDEV_CLOSING, FRDEV_CLOSED, FRDEV_FAILED };

int frdev_open(struct frd *f);          /* 0 = launching (the next menu proc pass builds the screen), < 0 = cannot (the text view is used) */
void frdev_frame(void);                 /* once per overlay frame while open */
void frdev_close(void);                 /* leave: back to the screen we came from at our think's next run */
int frdev_state(void);
void frdev_reset(void);
void frdev_abort(void);                 /* the overlay is being freed: our think replaced by a retail no-op, our texts freed */
unsigned frdev_err(void);
#endif
