/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* nettext: see nettext.h and. */
#include "nettext.h"

static char *ntx_put(char *d, const char *s)
{
    while (*s) {
        *d++ = *s++;
    }
    return d;
}

static char *ntx_put_u(char *d, unsigned v)
{
    char t[4];
    unsigned n = 0;

    if (v > 999u) {
        v = 999u;                                   /* clamped: the lines stay inside 28 characters */
    }
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) {
        *d++ = t[--n];
    }
    return d;
}

nt_u32 ntx_decode(nt_u32 c)                         /* the inverse of nt_code (tenths of a ms) */
{
    return c < 600u ? c : (c - 600u + 60u) * 10u;
}

void ntx_unpack(nt_u32 w, nt_leg_result *r)
{
    r->median = (nt_u16)ntx_decode(w & 1023u);
    r->spread = (nt_u16)ntx_decode((w >> 10) & 1023u);
    r->coarse = 0;
    r->got = (nt_u8)((w >> 20) & 63u);
    r->sent = (nt_u8)((w >> 26) & 63u);
}

static int ntx_loss_high(const nt_leg_result *r)
{
    return r->sent && (unsigned)(r->sent - r->got) * 100u >= NTX_LOSS_PERCENT * (unsigned)r->sent;
}

int ntx_verdict(const nt_leg_result *router, const nt_leg_result *inet)
{
    int rm = router->sent != 0;                     /* the router leg was measured on this console */
    int ra = rm && router->got != 0;                /* ... and answered at least once */
    int ia = inet->got != 0;

    if (!ia) {
        return ra ? NTX_NOINET : NTX_NOTHING;
    }
    if (ra && (router->spread > NTX_ROUTER_SPREAD_MS * 10u || ntx_loss_high(router))) {
        return NTX_UNSTABLE;
    }
    if (inet->median > NTX_INET_SLOW_MS * 10u) {
        return NTX_SLOW;
    }
    if (ntx_loss_high(inet)) {
        return NTX_LOSSY;
    }
    if (rm && !ra) {
        return NTX_NOROUTER;
    }
    return NTX_GOOD;
}

const char *ntx_word(int v)
{
    switch (v) {
    case NTX_UNSTABLE: return "Unsteady";
    case NTX_SLOW: return "Slow";
    case NTX_LOSSY: return "Loses packets";
    case NTX_NOROUTER: return "No router";
    case NTX_NOINET: return "No answer";
    case NTX_NOTHING: return "Nothing";
    default: return "Good";
    }
}

/* tenths of a millisecond: "4.7" below 10 ms, whole milliseconds from 10 ms (rounded), at most 999 */
static char *ntx_put_t(char *d, unsigned t)
{
    if (t < 100u) {
        *d++ = (char)('0' + t / 10u);
        *d++ = '.';
        *d++ = (char)('0' + t % 10u);
        return d;
    }
    return ntx_put_u(d, (t + 5u) / 10u);
}

static void ntx_leg_line(char *out, const char *name, const nt_leg_result *r)
{
    char *d = ntx_put(out, name);

    if (!r->sent) {
        d = ntx_put(d, r->na ? ": Not available on Wii/Wii U" : ": not measured");    /* na = the Wii family: its vBBA drops router datagrams, so the leg is categorically unavailable; elsewhere an unmeasured leg is only 'not measured' (no gateway known) */
    } else if (!r->got) {
        d = ntx_put(d, ": no answer");
    } else {
        d = ntx_put(d, ": ");
        d = ntx_put_t(d, r->median);
        d = ntx_put(d, " ms, worst ");                                 /* the slowest 5 percent of the probes (median + spread), what a stutter feels like ("why not just worst") */
        { unsigned w = (unsigned)r->median + (unsigned)r->spread; d = ntx_put_t(d, w > 9990u ? 9990u : w); }
    }
    *d = 0;
}

void ntx_lines(const nt_leg_result *router, const nt_leg_result *inet, char out[NTX_RESULT_LINES][NTX_LINE_MAX])
{
    const nt_leg_result *w = (router->sent && (unsigned)(router->sent - router->got) >= (unsigned)(inet->sent - inet->got)) ? router : inet;
    char *d;
    const char *a, *b;

    ntx_leg_line(out[0], "Router", router);
    ntx_leg_line(out[1], "Internet", inet);
    d = ntx_put(out[2], "Lost: ");
    d = ntx_put_u(d, (unsigned)(w->sent - w->got));
    d = ntx_put(d, " of ");
    d = ntx_put_u(d, w->sent);
    *d = 0;
    switch (ntx_verdict(router, inet)) {
    case NTX_UNSTABLE: a = "Wi-Fi is unsteady right now."; b = "Move closer or use a cable."; break;   /* owner tone decent, a cable is best, not alarming */
    case NTX_SLOW: a = "Your connection to the"; b = "internet is slow"; break;
    case NTX_LOSSY: a = "The internet link loses"; b = "packets. Try again later."; break;
    case NTX_NOROUTER: a = "Router does not answer:"; b = "showing the internet only"; break;
    case NTX_NOINET: a = "No answer from the internet."; b = "UDP may be blocked."; break;
    case NTX_NOTHING: a = "Nothing answered."; b = "Check cable and router."; break;
    default: a = "Good"; b = ""; break;
    }
    *ntx_put(out[3], a) = 0;
    *ntx_put(out[4], b) = 0;
}

const char *ntx_notice(unsigned i)
{
    switch (i) {
    case 0: return "Sends small DNS queries to your router";
    case 1: return "and to Cloudflare public DNS (1.1.1.1).";
    case 2: return "Cloudflare sees your address, like in";
    case 3: return "any DNS lookup. Takes about 5 seconds.";
    default: return "";
    }
}

unsigned ntx_progress(char *out, unsigned n)
{
    char *d = ntx_put(out, "Testing ");

    d = ntx_put_u(d, n > NT_SLOTS ? NT_SLOTS : n);
    d = ntx_put(d, " of ");
    d = ntx_put_u(d, NT_SLOTS);
    *d = 0;
    return (unsigned)(d - out);
}
