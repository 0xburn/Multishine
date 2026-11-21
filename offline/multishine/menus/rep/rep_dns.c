/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_dns: the rep_env.dns body on top of sddns (offline/net/sddns.c; in the overlay the bridge's resolver, shared).  One lookup at a time; if the shared
   resolver is busy with someone else's lookup we simply wait (return 0) and try again on the next frame.  host == NULL aborts our lookup. */
#include "rep.h"
#include "sddns.h"

int rep_dns_step(struct rep_dns *s, const char *host, rep_u32 *ip, rep_u32 now)
{
    int st;
    rep_u32 i;
    if (!host) { if (s->busy) sddns_abort(s->d); s->busy = 0; return 0; }
    if (!s->busy) {
        for (i = 0; host[i] && i < sizeof s->host - 1; i++) s->host[i] = host[i];
        s->host[i] = 0;
        if (s->d->state == SDDNS_BUSY) return 0;                 /* busy with another user: do not rewrite its server list (review L3) */
        if (s->server) sddns_set_servers(s->d, s->server, 0);    /* the DHCP-provided resolver only, as the hub does (od_glue.c) */
        if (s->port) s->d->port = (unsigned short)s->port;
        if (sddns_start(s->d, s->host, now) != 0) return 0;      /* busy with another user: try again next frame */
        s->busy = 1;
    }
    st = sddns_poll(s->d, now);
    if (st == SDDNS_DONE) { s->busy = 0; *ip = s->d->result_ip; return 1; }
    if (st == SDDNS_FAILED) { s->busy = 0; return -1; }
    return 0;
}
