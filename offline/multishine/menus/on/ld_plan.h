/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* LAB_DIAG upload plan (on_backend.c on_ld_step; pure, host-tested in menus/tests/fc_log_test.c): a readout's lines go out three per datagram, the whole readout TWICE
   (the second pass 5 s after the first (1260; 20 s in 1258-1259): merges the repeated lines of one readout, so a datagram lost on a slow link is filled in by the other pass), and a
   datagram the client refuses (busy) is tried again 1 s later instead of ending the upload.  After 40 refusals in a row the pass is given up. */
#ifndef LD_PLAN_H
#define LD_PLAN_H
struct ld_st { unsigned int gen, line, next, pass, refused, sent, nref, done; };   /* sent / nref / done: datagrams taken, refusals, readouts finished (since boot; FC word 120) */
#define LD_SEND 1
#define LD_DONE 2
/* gen / n = the readout on offer and its line count, now = ms.  0 wait, LD_SEND send lines from s->line on, LD_DONE every part sent twice: mark the readout sent */
static __inline__ int ld_plan(struct ld_st *s, unsigned int gen, unsigned int n, unsigned int now)
{
    if (gen != s->gen) { s->gen = gen; s->line = 0; s->pass = 0; s->refused = 0; s->next = now + 3000u; }   /* a new readout: wait 3 s, the late lines fill in */
    if (s->line >= n) {
        if (s->pass) {
#ifdef MM_FC
            s->done++;
#endif
            return LD_DONE;
        }
        s->pass = 1; s->line = 0; s->refused = 0; s->next = now + 5000u;
        return 0;
    }
    return (unsigned int)(now - s->next) > 0x7FFFFFFFu ? 0 : LD_SEND;
}
/* the result of one send: ok = the client took it, upto = the line after the last one in it */
static __inline__ void ld_done(struct ld_st *s, int ok, unsigned int upto, unsigned int n, unsigned int now)
{
#ifdef MM_FC
    if (ok) s->sent++; else s->nref++;                 /* the upload's own counters: FC word 120 only */
#endif
    if (ok) { s->line = upto; s->refused = 0; s->next = now + 300u; }
    else if (++s->refused >= 40u) s->line = n;
    else s->next = now + 1000u;
}
#endif
