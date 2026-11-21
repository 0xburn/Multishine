/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Auto input delay: see delay_auto.h and.  Original work. */
#include "delay_auto.h"

da_s32 da_rule(da_u32 rtt_us)
{
    da_u32 q = rtt_us / DA_STEP_US + (rtt_us % DA_STEP_US != 0u);
    return (q > 100u ? 100 : (da_s32)q) + 1;
}

da_s32 da_clamp(da_s32 d, da_s32 floor_, da_s32 cap)
{
    if (floor_ < 1) floor_ = 1;
    if (floor_ > DA_MAX) floor_ = DA_MAX;
    if (cap < floor_) cap = floor_;
    if (cap > DA_MAX) cap = DA_MAX;
    return d < floor_ ? floor_ : d > cap ? cap : d;
}

void da_reset(struct da_state *s)
{
    da_u32 i;
    for (i = 0; i < DA_RING; i++) s->ring[i] = 0;
    s->n = s->head = 0;
}

void da_init(struct da_state *s) { da_reset(s); }

void da_sample(struct da_state *s, da_u32 rtt_us)
{
    s->ring[s->head] = rtt_us;
    s->head = (s->head + 1u) % DA_RING;
    if (s->n < DA_RING) s->n++;
}

void da_decide(const struct da_state *s, da_s32 floor_, da_s32 cap, da_s32 fallback, struct da_result *r)
{
    da_u32 v[DA_RING], i, j, x, n = 0, k;
    da_s32 d;
    if (cap == 0) cap = DA_CAP_DEFAULT;
    r->delay = da_clamp(fallback, floor_, cap);
    r->raw = 0; r->rtt_us = 0; r->spread_us = 0; r->ping_ms = 0; r->n_valid = 0;
    r->reason = DA_R_FEW; r->confident = 0;
    for (i = 0; i < s->n; i++) {
        x = s->ring[i];
        if (x == 0u || x > DA_MAX_US) continue;       /* junk: no clock tick, or a lost ack */
        for (j = n; j > 0 && v[j - 1] > x; j--) v[j] = v[j - 1];
        v[j] = x;
        n++;
    }
    r->n_valid = n;
    if (n < DA_MIN_N) return;
    k = n - n / 4u;                                    /* keep the lowest three quarters */
    r->rtt_us = v[(k - 1u) / 2u];                      /* lower median of the kept */
    r->spread_us = v[k - 1u] - r->rtt_us;
    r->ping_ms = (r->rtt_us + 500u) / 1000u;
    d = da_rule(r->rtt_us);
    r->reason = DA_R_RULE;
    if (r->spread_us > DA_JITTER_US) { d++; r->reason = DA_R_JITTER; }
    r->raw = d;
    r->delay = da_clamp(d, floor_, cap);
    r->confident = 1;
    if (d > r->delay) r->reason = DA_R_CAP;
    else if (r->reason == DA_R_RULE && d <= (floor_ < 1 ? 1 : floor_)) r->reason = DA_R_FLOOR;
}

da_s32 da_opt_is_auto(da_u32 byte) { return byte == DA_OPT_AUTO; }
da_s32 da_opt_cap(da_u32 byte) { return (byte >= 2u && byte <= (da_u32)DA_MAX) ? (da_s32)byte : DA_CAP_DEFAULT; }
da_s32 da_opt_manual(da_u32 byte, da_s32 dflt) { return (byte >= 1u && byte <= (da_u32)DA_MAX) ? (da_s32)byte : dflt; }

static da_s32 put(char *buf, da_s32 pos, da_s32 n, const char *s)
{
    while (*s && pos < n - 1) buf[pos++] = *s++;
    buf[pos] = 0;
    return pos;
}

static da_s32 put_uint(char *buf, da_s32 pos, da_s32 n, da_u32 v)
{
    char tmp[12];
    da_s32 k = 0;
    do { tmp[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 11);
    while (k && pos < n - 1) buf[pos++] = tmp[--k];
    buf[pos] = 0;
    return pos;
}

da_s32 da_text(char *buf, da_s32 n, const struct da_result *r)
{
    da_s32 p = 0;
    if (n <= 0) return 0;
    buf[0] = 0;
    p = put(buf, p, n, "Delay ");
    p = put_uint(buf, p, n, (da_u32)r->delay);
    p = put(buf, p, n, " auto, ");
    if (!r->confident) return put(buf, p, n, "no ping yet");
    p = put(buf, p, n, "ping ");
    p = put_uint(buf, p, n, r->ping_ms);
    p = put(buf, p, n, " ms");
    if (r->reason == DA_R_CAP) p = put(buf, p, n, ", capped");
    else if (r->reason == DA_R_JITTER) p = put(buf, p, n, ", jittery link");
    return p;
}

da_s32 da_text_manual(char *buf, da_s32 n, da_s32 delay)
{
    da_s32 p;
    if (n <= 0) return 0;
    buf[0] = 0;
    p = put(buf, 0, n, "Delay ");
    return put_uint(buf, p, n, (da_u32)delay);
}
