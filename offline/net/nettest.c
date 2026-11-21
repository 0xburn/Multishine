/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* nettest: see nettest.h and. */
#include "nettest.h"

#define NT_NAME "cloudflare.com"        /* always cached by a public resolver */

static nt_u16 rd16(const nt_u8 *p) { return (nt_u16)(((nt_u16)p[0] << 8) | p[1]); }

void nt_stats(const nt_u32 *s, nt_u32 n, nt_u32 *median, nt_u32 *spread)
{
    nt_u32 a[NT_PROBES];
    nt_u32 i, j, r, p95;

    *median = 0;
    *spread = 0;
    if (n == 0) {
        return;
    }
    if (n > NT_PROBES) {
        n = NT_PROBES;
    }
    for (i = 0; i < n; i++) {                       /* insertion sort of a copy */
        nt_u32 v = s[i];
        for (j = i; j > 0 && a[j - 1] > v; j--) {
            a[j] = a[j - 1];
        }
        a[j] = v;
    }
    *median = n & 1u ? a[n / 2u] : a[n / 2u - 1u] / 2u + a[n / 2u] / 2u + ((a[n / 2u - 1u] & a[n / 2u]) & 1u);
    r = (n * 95u + 99u) / 100u;                     /* rank ceil(0.95 n), 1-based */
    p95 = a[r - 1u];
    *spread = p95 > *median ? p95 - *median : 0u;
}

static void leg_clear(nt_leg *l)
{
    nt_u32 i;
    for (i = 0; i < NT_SLOTS; i++) {
        l->st[i] = 0;
        l->sent_at[i] = 0;
    }
    l->sent = 0;
    l->got = 0;
    l->nfine = 0;
    l->pend = 0;
    l->pslot = 0;
    l->pdue = 0;
    l->est = 0;
}

int nt_start(nettest *t, const sddns_io *io, int router_ok, nt_u32 router, nt_u32 router_alt, nt_u32 inet, nt_u32 inet_alt, nt_u32 now, nt_u32 seed)
{
    char host[SDDNS_HOST_MAX + 1];
    int n;

    if (!t || !io || !inet) {
        return -1;
    }
    t->io = *io;
    t->state = NT_IDLE;
    t->run = 0;
    t->t0 = now;
    t->slot_due = now;
    t->frame_t0 = now;
    t->frame_us = NT_FRAME_US;
    t->last_poll = now;
    t->slot = 0;
    t->nonce = (nt_u16)((seed ^ (seed >> 16)) & 0xFFFFu);
    t->st_sent = t->st_rx = t->st_ignored = 0;
    n = sddns_normalize_host(NT_NAME, host);
    if (n < 0) {
        return -1;
    }
    n = sddns_encode_name(host, t->qwire, NT_QW_MAX);
    if (n < 0) {
        return -1;
    }
    t->qlen = (nt_u32)n;
    leg_clear(&t->leg[NT_ROUTER]);
    leg_clear(&t->leg[NT_INTERNET]);
    t->leg[NT_ROUTER].target = router;
    t->leg[NT_ROUTER].alt = router_alt != router ? router_alt : 0;
    t->leg[NT_ROUTER].enabled = (nt_u8)(router_ok && router != 0);
    t->router_na = (nt_u8)(router_ok == 0);
    t->leg[NT_ROUTER].on_alt = 0;
    t->leg[NT_INTERNET].target = inet;
    t->leg[NT_INTERNET].alt = inet_alt != inet ? inet_alt : 0;
    t->leg[NT_INTERNET].enabled = 1;
    t->leg[NT_INTERNET].on_alt = 0;
    t->leg[NT_ROUTER].active = t->leg[NT_ROUTER].enabled;
    t->leg[NT_INTERNET].active = 1;
    t->state = NT_RUN;
    return 0;
}

void nt_abort(nettest *t)
{
    if (t->state == NT_RUN) {
        t->state = NT_STOPPED;
    }
}

static void on_reply(nettest *t, const nt_u8 *p, nt_u32 len, nt_u32 ip, nt_u16 port, nt_u32 now, int fine)
{
    nt_u32 d, slot, li;
    nt_u16 id;
    nt_leg *l;
    nt_u32 dummy_ip, dummy_ttl;

    if (port != NT_PORT || len < 12) {
        t->st_ignored++;
        return;
    }
    id = rd16(p);
    d = (nt_u32)((nt_u16)(id - t->nonce));
    slot = d >> 1;
    li = d & 1u;
    if (slot >= NT_SLOTS) {
        t->st_ignored++;
        return;
    }
    l = &t->leg[li];
    if (!l->active || l->st[slot] != 1 || ip != l->target) {
        t->st_ignored++;                            /* not ours, a duplicate, or later than the timeout */
        return;
    }
    if (sddns_parse_reply(p, len, id, t->qwire, t->qlen, &dummy_ip, &dummy_ttl) == SDDNS_E_BAD) {
        t->st_ignored++;                            /* malformed or not an answer to our question: the probe stays open until the timeout */
        return;
    }
    l->st[slot] = 2;
    t->st_rx++;
    {
        nt_u32 rtt = now - l->sent_at[slot];
        if (l->est == 0u || rtt < l->est) {
            l->est = rtt ? rtt : 1u;                /* where the next reply is expected (the warm-up counts) */
        }
        if (slot >= 1u) {
            l->rtt[l->got] = rtt;
            l->fine[l->got] = (nt_u8)(fine ? 1 : (l->kn[slot] ? 2 : 0));   /* 1 fine, 2 coarse although a window was expected, 0 coarse before any estimate */
            l->nfine = (nt_u8)(l->nfine + (fine ? 1 : 0));
            l->got++;
        }
    }
}

/* where inside the frame a probe goes out so that its reply, expected est after it, lands in the polled window at the start of a later frame
   (windows start at frame_t0 + k * frame_us and are polled for up to NT_SPIN_MAX_US): 0 when it already does */
static nt_u32 send_offset(const nettest *t, const nt_leg *l)
{
    nt_u32 f = t->frame_us ? t->frame_us : NT_FRAME_US;
    nt_u32 m;

    if (l->est == 0u) {
        return 0;
    }
    m = l->est % f;
    if (m <= 5500u) {
        return 5500u - m;                           /* arrival in the middle of the window: room for jitter both ways */
    }
    if (m <= 9000u) {
        return 0;
    }
    return f - m + 3000u;
}

static void do_send(nettest *t, nt_leg *l, nt_u32 li, nt_u32 now)
{
    nt_u8 pkt[12 + NT_QW_MAX + 4];
    nt_u32 slot = l->pslot;
    nt_u16 id = (nt_u16)(t->nonce + 2u * slot + li);
    int n, rc = -1;

    l->pend = 0;
    n = sddns_build_query(t->qwire, t->qlen, id, pkt, (sddns_u32)sizeof(pkt));
    if (n > 0 && t->io.send) {
        rc = t->io.send(t->io.ctx, l->target, NT_PORT, pkt, (sddns_u32)n);
    }
    l->sent_at[slot] = now;
    l->st[slot] = (nt_u8)(rc < 0 ? 3 : 1);          /* a send that could not be made is a lost probe */
    if (slot >= 1u) {
        l->sent++;
    }
    t->st_sent++;
}

static void begin_slot(nettest *t, nt_u32 now)
{
    nt_u32 li;

    for (li = 0; li < 2; li++) {
        nt_leg *l = &t->leg[li];
        if (!l->active) {
            continue;
        }
        l->pend = 1;
        l->pslot = t->slot;
        l->pdue = now + (t->slot >= 1u ? send_offset(t, l) : 0u);
        l->kn[t->slot] = (nt_u8)(l->nfine != 0u);        /* the estimate rests on a fine sample */
    }
    t->slot++;
}

static void send_due(nettest *t, nt_u32 now)
{
    nt_u32 li;

    for (li = 0; li < 2; li++) {
        nt_leg *l = &t->leg[li];
        if (l->active && l->pend && (nt_u32)(now - l->pdue) <= 0x7FFFFFFFu) {
            do_send(t, l, li, now);
        }
    }
}

/* all probes of the active legs resolved? */
static int run_resolved(const nettest *t)
{
    nt_u32 li, s;
    for (li = 0; li < 2; li++) {
        if (t->leg[li].active) {
            if (t->leg[li].pend) {
                return 0;
            }
            for (s = 0; s < NT_SLOTS; s++) {
                if (t->leg[li].st[s] == 1) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

int nt_poll(nettest *t, nt_u32 now)
{
    nt_u8 buf[SDDNS_MAX_PACKET + 1];
    nt_u32 i, li, s, ip;
    nt_u16 port;
    int r, fine;

    if (t->state != NT_RUN) {
        return t->state;
    }
    if ((nt_u32)(now - t->t0) >= NT_HARD_US) {
        t->state = NT_STOPPED;
        return t->state;
    }
    fine = (nt_u32)(now - t->last_poll) <= NT_FINE_US;
    t->last_poll = now;
    for (i = 0; i < NT_RX_BUDGET; i++) {
        ip = 0;
        port = 0;
        r = t->io.recv ? t->io.recv(t->io.ctx, buf, (sddns_u32)sizeof(buf), &ip, &port) : 0;
        if (r <= 0) {
            break;
        }
        if ((nt_u32)r > SDDNS_MAX_PACKET) {
            t->st_ignored++;
            continue;
        }
        on_reply(t, buf, (nt_u32)r, ip, port, now, fine);
    }
    for (li = 0; li < 2; li++) {                    /* timeouts */
        nt_leg *l = &t->leg[li];
        if (!l->active) {
            continue;
        }
        for (s = 0; s < NT_SLOTS; s++) {
            if (l->st[s] == 1 && (nt_u32)(now - l->sent_at[s]) >= NT_TIMEOUT_US) {
                l->st[s] = 3;
            }
        }
    }
    send_due(t, now);
    if (t->slot < NT_SLOTS) {
        if ((nt_u32)(now - t->slot_due) <= 0x7FFFFFFFu) {       /* due (the clock may wrap) */
            begin_slot(t, now);
            send_due(t, now);
            t->slot_due += NT_SLOT_US;
            if ((nt_u32)(now - t->slot_due) <= 0x7FFFFFFFu && (nt_u32)(now - t->slot_due) >= NT_SLOT_US) {
                t->slot_due = now + NT_SLOT_US;                 /* far behind (a long frame): no burst to catch up */
            }
        }
        return t->state;
    }
    if (!run_resolved(t)) {
        return t->state;
    }
    if (t->run == 0) {                              /* end of the first run: a leg without a single answer gets its alternate target once */
        int again = 0;
        for (li = 0; li < 2; li++) {
            nt_leg *l = &t->leg[li];
            if (l->active && l->got == 0 && l->alt) {
                leg_clear(l);
                l->target = l->alt;
                l->alt = 0;
                l->on_alt = 1;
                again = 1;
            } else {
                l->active = 0;
            }
        }
        if (again) {
            t->run = 1;
            t->slot = 0;
            t->slot_due = now;
            t->nonce = (nt_u16)(t->nonce + 2u * NT_SLOTS);
            return t->state;
        }
    }
    t->state = NT_DONE;
    return t->state;
}

void nt_frame(nettest *t, nt_u32 now)
{
    nt_u32 d = now - t->frame_t0;

    if (d >= 8000u && d <= 34000u) {                /* a frame, not a pause: smooth the period (7/8 old, 1/8 new) */
        t->frame_us = (7u * t->frame_us + d) / 8u;
    }
    t->frame_t0 = now;
}

nt_u32 nt_spin_us(const nettest *t, nt_u32 now)
{
    nt_u32 li, s, rem = 0, left, used;

    if (t->state != NT_RUN) {
        return 0;
    }
    used = now - t->frame_t0;
    if (used >= NT_SPIN_MAX_US) {
        return 0;
    }
    left = NT_SPIN_MAX_US - used;
    for (li = 0; li < 2; li++) {
        const nt_leg *l = &t->leg[li];
        if (!l->active) {
            continue;
        }
        if (l->pend) {
            nt_u32 w = (nt_u32)(l->pdue - now);
            w = w <= 0x7FFFFFFFu ? w + 400u : 400u;
            if (w > rem) {
                rem = w;
            }
        }
        for (s = 0; s < NT_SLOTS; s++) {
            nt_u32 age, lo, hi, need;
            if (l->st[s] != 1) {
                continue;
            }
            age = now - l->sent_at[s];
            if (l->est == 0u) {
                lo = 0u;
                hi = 6000u;
            } else {
                lo = (l->nfine >= 3u && l->est > 2000u) ? l->est - 2000u : l->est / 2u;
                hi = l->est + 8000u;
            }
            if (age > hi || age + left < lo) {
                continue;                           /* not within reach of this frame yet, or already overdue */
            }
            need = hi - age;
            if (need > rem) {
                rem = need;
            }
        }
    }
    return rem > left ? left : rem;
}

void nt_leg_get(const nettest *t, int leg, nt_leg_result *r)
{
    const nt_leg *l = &t->leg[leg & 1];
    nt_u32 a[NT_PROBES], all[NT_PROBES], n = 0, i, med, sp;
    int use_fine;

    r->median = r->spread = 0;
    r->got = r->sent = 0;
    r->coarse = 0;
    r->na = (nt_u8)(leg == NT_ROUTER && t->router_na);
    if (!l->enabled) {
        return;
    }
    r->sent = l->sent;
    r->got = l->got;
    use_fine = l->nfine >= NT_FINE_MIN;
    for (i = 0; i < l->got; i++) {
        if (!use_fine || l->fine[i] == 1u) {
            a[n++] = l->rtt[i];
        }
    }
    r->coarse = (nt_u8)(l->got && !use_fine);
    nt_stats(a, n, &med, &sp);
    if (use_fine) {                                 /* the median comes from the fine samples; the spread must also see the late ones that no window caught (rounded up to the frame: an upper bound) */
        nt_u32 m2, s2, k = 0;
        for (i = 0; i < l->got; i++) {
            if (l->fine[i] != 0u) {
                all[k++] = l->rtt[i];
            }
        }
        nt_stats(all, k, &m2, &s2);
        sp = (m2 + s2 > med) ? m2 + s2 - med : 0u;
    }
    med = (med + 50u) / 100u;                       /* tenths of a millisecond, rounded */
    sp = (sp + 50u) / 100u;
    r->median = (nt_u16)(med > 0xFFFFu ? 0xFFFFu : med);
    r->spread = (nt_u16)(sp > 0xFFFFu ? 0xFFFFu : sp);
}

nt_u32 nt_progress(const nettest *t)
{
    return t->slot;
}

nt_u32 nt_code(nt_u32 d)                            /* below 60.0 ms: tenths (0..599); from 60 ms: 600 + whole ms above 60, to 1023 (483 ms) */
{
    nt_u32 ms;
    if (d < 600u) {
        return d;
    }
    ms = (d + 5u) / 10u;
    return ms - 60u > 423u ? 1023u : 600u + (ms - 60u);
}

nt_u32 nt_pack(const nt_leg_result *r)
{
    nt_u32 m = nt_code(r->median);
    nt_u32 sp = nt_code(r->spread);
    nt_u32 g = r->got > 63u ? 63u : r->got;
    nt_u32 s = r->sent > 63u ? 63u : r->sent;
    return m | (sp << 10) | (g << 20) | (s << 26);
}

nt_u32 nt_state_word(const nettest *t)
{
    nt_u32 w = (nt_u32)t->state & 15u;
    w |= (nt_u32)t->slot << 8;
    if (!t->leg[NT_ROUTER].enabled) {
        w |= 1u << 16;
    }
    if (t->leg[NT_INTERNET].on_alt) {
        w |= 1u << 17;
    }
    if (t->leg[NT_INTERNET].got && t->leg[NT_INTERNET].nfine < NT_FINE_MIN) {
        w |= 1u << 18;
    }
    if (t->leg[NT_ROUTER].enabled && t->leg[NT_ROUTER].got && t->leg[NT_ROUTER].nfine < NT_FINE_MIN) {
        w |= 1u << 19;
    }
    return w;
}
