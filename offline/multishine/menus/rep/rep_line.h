/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* rep_line.h: the REP status line of the Main Menu (rep_res.c rp_say, MS_REP_LINE).  Every write is bounded by the buffer (review M6): the line stops at cap - 1 characters
 * and is always NUL-terminated; a field that does not fit is cut, nothing is written past the buffer.  ok and try always; every other field only when it is not zero (the line must fit
 * the screen).  Letters, numerals, space. , ' - : only (the menu font has no equals sign).  Included by rep_res.c and by the host test tests/rep_line_test.c. */
#ifndef REP_LINE_H
#define REP_LINE_H
#include "rep_keep.h"

struct rpl { char *d, *e; };                                              /* d = next byte, e = the last byte (kept for the NUL) */
static void rpl_c(struct rpl *w, char c) { if (w->d < w->e) *w->d++ = c; }
static void rpl_put(struct rpl *w, const char *s, unsigned int max) { unsigned int i; for (i = 0; i < max && s[i]; i++) rpl_c(w, s[i]); }
static void rpl_putn(struct rpl *w, unsigned int v) { char t[11]; int n = 0; if (!v) t[n++] = '0'; while (v && n < 10) { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (n) rpl_c(w, t[--n]); }
static void rpl_puth(struct rpl *w, unsigned int v) { static const char hx[] = "0123456789ABCDEF"; rpl_c(w, hx[(v >> 4) & 15u]); rpl_c(w, hx[v & 15u]); }
static void rpl_kv(struct rpl *w, const char *k, unsigned int v) { rpl_c(w, ' '); rpl_put(w, k, 8u); rpl_c(w, ':'); rpl_putn(w, v); }
#define RPL_NZ(w, k, v) do { if (v) rpl_kv(w, k, v); } while (0)

/* srv: rp_k_server + 1 (0 no HELLO yet, 1 not announced, 2 announced).  cap >= 1. */
static void rep_line_fmt(char *buf, unsigned int cap, const struct rep_stats *s, unsigned int srv)
{
    struct rpl w;
    unsigned int drops = s->dropped_max + s->dropped_age + s->dropped_cert + s->dropped_off;
    w.d = buf; w.e = buf + cap - 1u;
    rpl_put(&w, "REP", 3u); rpl_kv(&w, "ok", s->ok); rpl_kv(&w, "try", s->attempts);
    RPL_NZ(&w, "h", s->last_http); RPL_NZ(&w, "dns", s->dns_fail); RPL_NZ(&w, "tcp", s->tcp_fail); RPL_NZ(&w, "tls", s->tls_fail);
    RPL_NZ(&w, "sf", s->success_false); RPL_NZ(&w, "gq", s->graphql_errors); RPL_NZ(&w, "st", s->http_status_bad); RPL_NZ(&w, "pb", s->parse_bad);
    RPL_NZ(&w, "to", s->rsp_timeout); RPL_NZ(&w, "nm", s->no_mem); RPL_NZ(&w, "id", s->no_ident); RPL_NZ(&w, "drop", drops);
    RPL_NZ(&w, "off", s->off + s->dropped_off); RPL_NZ(&w, "mode", s->mode_refused); RPL_NZ(&w, "bad", s->bad);          /* off = refused because our server did not announce the bit */
    if (srv != 2u) rpl_kv(&w, "srv", srv);                                                                               /* shown only when the bit is not announced */
    RPL_NZ(&w, "hold", s->held_scene + s->held_time + s->held_cooldown); RPL_NZ(&w, "ent", s->held_ent); RPL_NZ(&w, "ab", s->scene_aborts);
    if (s->last_tls_err | s->last_cert_err | s->last_sec_err) { rpl_put(&w, " e:", 3u); rpl_puth(&w, s->last_tls_err); rpl_puth(&w, s->last_cert_err); rpl_puth(&w, s->last_sec_err); }
    if (s->last_msg[0]) { rpl_put(&w, " m:", 3u); rpl_put(&w, s->last_msg, (unsigned int)sizeof s->last_msg); }
    *w.d = 0;
}
#endif
