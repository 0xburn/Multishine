/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* frd_net: the network of the Friends screen and the Accounts row "Friend requests after a fight" (frd_net.c). */
#ifndef FRD_NET_H
#define FRD_NET_H
#include "frd.h"
#include "socc.h"

struct frdn_io {                   /* the resident hub client and its thunks (the Update row's, struct sgo_updcfg) */
    mnc *net;
    int (*hello)(unsigned now_ms);
    int (*poll)(unsigned now_ms);
    int (*link_up)(void);
};
void frdn_reset(void);
int frdn_busy(void);                                /* 1 while a request of this module is running (sgc.c waits for it: one client, one request at a time) */
/* one frame: answers f->req (f may be 0) and the setting request (*fight_req 1 get, 2 set to *fight: 1 off, 2 on; 3 set the policy *pol: 1 Friends, 2 Anyone With My Code,
   3 Nobody; 4 and 5 write the account's Region (*rgn 1..7 regions, 8 not set) and Who can watch my games (*wch 1 Nobody, 2 Friends);
   *fight becomes 1 / 2 and *pol 1..3, or 3 / 4 = not available; a read fills *rgn (9 = not available) and *wch (3 = not available); rgn / wch may be 0) */
void frdn_frame(struct frd *f, unsigned char *fight, unsigned char *fight_req, unsigned char *pol, unsigned char *rgn, unsigned char *wch, const struct frdn_io *io, unsigned now);
void frdn_texts(struct frd *f, const mnc *c);       /* the message presets from the client's lists */
#ifdef GNB_MELEE
void frdn_go_css(mnc *c, const char *code);         /* console: arm the Direct hand-off and leave for the online character screen */
#endif
#endif
