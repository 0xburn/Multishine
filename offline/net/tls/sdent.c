/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#include "sdent.h"

static const sdc_u32 cap_bits[SDENT_NSRC] = { SDENT_CAP_STICK, SDENT_CAP_PRESS, SDENT_CAP_JITTER, SDENT_CAP_PROV, SDENT_CAP_STORED, SDENT_CAP_SERVER, SDENT_CAP_DIGI };

static void zero(void *p, sdc_u32 n) { volatile sdc_u8 *q = (volatile sdc_u8 *)p; sdc_u32 i; for (i = 0; i < n; i++) q[i] = 0; }

static void flush(struct sdent *e)
{
    if (e->bn) { sdc_rng_mix(&e->rng, e->batch, e->bn, 0); e->bn = 0; }
}
static void add_byte(struct sdent *e, sdc_u8 b)
{
    e->batch[e->bn++] = b;
    if (e->bn >= SDENT_BATCH) flush(e);
}
static void add_u32(struct sdent *e, sdc_u32 v)
{
    add_byte(e, (sdc_u8)v); add_byte(e, (sdc_u8)(v >> 8)); add_byte(e, (sdc_u8)(v >> 16)); add_byte(e, (sdc_u8)(v >> 24));
}

static void credit_add(struct sdent *e, int src, sdc_u32 add_m)
{
    sdc_u32 cap = cap_bits[src] * 1000u, old = e->mbits[src], nw;
    if (add_m > cap) add_m = cap;
    nw = old + add_m;
    if (nw > cap || nw < old) nw = cap;
    e->mtotal += nw - old;
    e->mbits[src] = nw;
    e->rng.credit = e->mtotal / 1000u;                     /* sdc_rng_ready (128) is therefore the SESSION rule over the same account */
}

void sdent_init(struct sdent *e)
{
    zero(e, sizeof *e);
    sdc_rng_init(&e->rng);
}

/* repetition count + adaptive proportion; the credit of a window is released only when the whole window passed */
static void health_feed(struct sdent *e, int src, sdc_u8 sym, sdc_u32 mbits)
{
    struct sdent_health *h = &e->h[src];
    int fail = 0;
    if (h->wn == 0) { h->ref = sym; h->wcnt = 0; }
    if (h->run && sym == h->last) h->run++; else h->run = 1;
    h->last = sym;
    if (h->run >= SDENT_RCT_CUTOFF) { h->rct_trips++; fail = 1; h->run = 0; }
    if (sym == h->ref) h->wcnt++;
    h->wn++;
    if (h->wcnt >= SDENT_APT_CUTOFF) { h->apt_trips++; fail = 1; }
    h->pend += mbits;
    if (fail) { h->pend = 0; h->wn = 0; h->wcnt = 0; return; }       /* the window is void: its held-back credit is dropped, a fresh window starts */
    if (h->wn >= SDENT_APT_WINDOW) {
        credit_add(e, src, h->pend);
        h->pend = 0; h->wn = 0; h->wcnt = 0; h->windows_ok++;
    }
}

void sdent_feed_pad(struct sdent *e, const struct sdent_pad *p, sdc_u32 tb)
{
    sdc_u8 a[6];
    sdc_u32 i, mv = 0;
    e->frame++;
    if (p->err) { e->have_prev = 0; return; }              /* no controller in the port: nothing to learn */
    a[0] = (sdc_u8)p->sx; a[1] = (sdc_u8)p->sy; a[2] = (sdc_u8)p->cx; a[3] = (sdc_u8)p->cy; a[4] = p->tl; a[5] = p->tr;
    for (i = 0; i < 6; i++) add_byte(e, a[i]);
    add_byte(e, (sdc_u8)p->buttons); add_byte(e, (sdc_u8)(p->buttons >> 8));
    add_u32(e, tb);
    e->n_samples++;
    if (e->have_prev) {
        /* axes are signed bytes: the signed difference keeps a step through zero small.  Credit needs a step that is not a repeat of the previous
           step (a perfectly regular sweep is predictable however its low bits look: second difference must be non-zero on some axis) */
        int dv[4], irregular = 0, dt[2], tmv = 0, tirr = 0;
        for (i = 0; i < 4; i++) {
            int d = (int)(signed char)a[i] - (int)(signed char)e->prev[i];
            dv[i] = d;
            mv += (sdc_u32)(d < 0 ? -d : d);
            if (d != (int)(signed char)e->pd[i]) irregular = 1;
        }
        for (i = 0; i < 4; i++) e->pd[i] = (sdc_u8)(signed char)(dv[i] > 127 ? 127 : dv[i] < -128 ? -128 : dv[i]);
        for (i = 0; i < 2; i++) {            /* the analog triggers (0..255) are two more axes: squeezing them counts like waggling a stick */
            int d = (int)a[4 + i] - (int)e->prev[4 + i];
            dt[i] = d; tmv += d < 0 ? -d : d;
            if (d != (int)(signed char)e->pdt[i]) tirr = 1;
        }
        for (i = 0; i < 2; i++) e->pdt[i] = (sdc_u8)(signed char)(dt[i] > 127 ? 127 : dt[i] < -128 ? -128 : dt[i]);
        if ((mv >= SDENT_MOVE_MIN && irregular) || ((sdc_u32)tmv >= SDENT_MOVE_MIN && tirr)) {
            sdc_u8 sym = (mv >= SDENT_MOVE_MIN && irregular) ? (sdc_u8)((a[0] & 3u) | ((a[1] & 3u) << 2) | ((a[2] & 3u) << 4) | ((a[3] & 3u) << 6))
                                                              : (sdc_u8)((a[4] & 15u) | ((a[5] & 15u) << 4));
            e->n_moving++;
            health_feed(e, SDENT_STICK, sym, SDENT_STICK_MBITS);
        }
    }
    for (i = 0; i < 6; i++) e->prev[i] = a[i];
    e->have_prev = 1;
    {   /* press timing: a press edge whose spacing from the previous press differs from the previous spacing by >= 2 frames */
        sdent_u16 edges = (sdent_u16)(p->buttons & (sdent_u16)~e->prev_buttons & 0x1F7Fu);
        if (edges) {
            sdc_u32 gap = e->frame - e->last_press;
            e->n_press++;
            if (e->have_press) {
                if (gap <= 600u) {
                    if (e->have_gap && (gap > e->last_gap ? gap - e->last_gap : e->last_gap - gap) >= 2u) credit_add(e, SDENT_PRESS, SDENT_PRESS_MBITS);
                    e->last_gap = gap; e->have_gap = 1;
                } else e->have_gap = 0;
            }
            e->last_press = e->frame; e->have_press = 1;
        }
        e->prev_buttons = p->buttons;
    }
    {   /* digital timing (box controllers: no analog stick, so the stick source stays silent): the pad's digital state is the buttons and the two trigger
           clicks; ANY change of it is an event (a press or a release); the spacing of events earns like the press spacing does.  Stick axes are not part of it (an analog
           stick wobbling over a threshold is the stick source's business, and the stick-alone cap stays what it was). */
        sdc_u32 dig = (sdc_u32)(p->buttons & 0x1F7Fu);
        dig |= (sdc_u32)(p->tl >= SDENT_DIGI_TRIG ? 1u : 0u) << 16;
        dig |= (sdc_u32)(p->tr >= SDENT_DIGI_TRIG ? 1u : 0u) << 17;
        if (dig != e->prev_dig) {
            sdc_u32 gap = e->frame - e->last_dev;
            e->n_dig++;
            if (e->have_dev) {
                if (gap <= 600u) {       /* compared with the spacing TWO events back: a press-release-press rhythm (alternating short and long gaps) that repeats exactly is predictable */
                    if (e->have_dgap >= 2 && (gap > e->last_dgap2 ? gap - e->last_dgap2 : e->last_dgap2 - gap) >= 2u) credit_add(e, SDENT_DIGI, SDENT_DIGI_MBITS);
                    e->last_dgap2 = e->last_dgap; e->last_dgap = gap;
                    if (e->have_dgap < 2) e->have_dgap++;
                } else e->have_dgap = 0;
            }
            e->last_dev = e->frame; e->have_dev = 1;
        }
        e->prev_dig = dig;
    }
#if SDENT_JITTER_MBITS
    credit_add(e, SDENT_JITTER, SDENT_JITTER_MBITS);
#endif
}

void sdent_mix(struct sdent *e, const sdc_u8 *p, sdc_u32 n)
{
    sdc_u32 i;
    for (i = 0; i < n; i++) add_byte(e, p[i]);
    flush(e);
}

void sdent_load_seed(struct sdent *e, int kind, const sdc_u8 seed[32], sdc_u32 credited_bits)
{
    sdc_u32 b = credited_bits > 128u ? 128u : credited_bits;
    if (kind != SDENT_PROV && kind != SDENT_STORED) { sdent_mix(e, seed, 32); return; }
    sdent_mix(e, seed, 32);
    if (e->mbits[kind] == 0) credit_add(e, kind, b * 1000u);     /* once per kind per boot: loading the same file twice earns nothing */
}

sdc_u32 sdent_credit(const struct sdent *e, sdc_u32 level)
{
    if (level >= SDENT_L_LONGTERM) return (e->mtotal - e->mbits[SDENT_STORED]) / 1000u;
    return e->mtotal / 1000u;
}
int sdent_ready(const struct sdent *e, sdc_u32 level)
{
    return sdent_credit(e, level) >= level;
}
sdc_u32 sdent_progress(const struct sdent *e, sdc_u32 level)
{
    sdc_u32 c = sdent_credit(e, level);
    return c >= level ? 100u : c * 100u / level;
}

int sdent_bytes(struct sdent *e, sdc_u32 level, sdc_u8 *out, sdc_u32 n)
{
    if (!sdent_ready(e, level)) return SDENT_E_STARVED;
    e->calls++;
    add_u32(e, e->calls);
    flush(e);
    sdc_rng_bytes(&e->rng, out, n);
    return 0;
}
int sdent_rand_session(void *ctx, sdc_u8 *out, sdc_u32 n)
{
    return sdent_bytes((struct sdent *)ctx, SDENT_L_SESSION, out, n) == 0 ? 0 : 1;
}

int sdent_export_seed(struct sdent *e, sdc_u8 seed[32], sdc_u32 *bits)
{
    static const sdc_u8 lab[14] = { 'm', 's', '-', 'e', 'n', 't', '-', 's', 'e', 'e', 'd', '-', 'v', '1' };
    sdc_u8 r[32], buf[46];
    sdc_u32 i;
    if (sdent_bytes(e, SDENT_L_SESSION, r, 32) != 0) return SDENT_E_STARVED;
    for (i = 0; i < 14; i++) buf[i] = lab[i];
    for (i = 0; i < 32; i++) buf[14 + i] = r[i];
    sdc_sha256(buf, 46, seed);
    zero(r, 32); zero(buf, 46);
    *bits = e->mtotal / 1000u > 128u ? 128u : e->mtotal / 1000u;
    return 0;
}

static sdc_u32 crc32(const sdc_u8 *p, sdc_u32 n)
{
    sdc_u32 c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (sdc_u32)(0u - (c & 1u)));
    }
    return ~c;
}
void sdent_seed_pack(const sdc_u8 seed[32], sdc_u32 bits, sdc_u8 out[SDENT_SEED_REC_LEN])
{
    sdc_u32 i, c;
    if (bits > 128u) bits = 128u;
    out[0] = 'S'; out[1] = 'D'; out[2] = 'S'; out[3] = 'D'; out[4] = 1; out[5] = 0; out[6] = 0; out[7] = (sdc_u8)bits;
    for (i = 0; i < 32; i++) out[8 + i] = seed[i];
    c = crc32(out, 40);
    out[40] = (sdc_u8)(c >> 24); out[41] = (sdc_u8)(c >> 16); out[42] = (sdc_u8)(c >> 8); out[43] = (sdc_u8)c;
}
int sdent_seed_parse(const sdc_u8 *in, sdc_u32 n, sdc_u8 seed[32], sdc_u32 *bits)
{
    sdc_u32 i, c;
    if (n < SDENT_SEED_REC_LEN) return -1;
    if (in[0] != 'S' || in[1] != 'D' || in[2] != 'S' || in[3] != 'D' || in[4] != 1) return -2;
    c = ((sdc_u32)in[40] << 24) | ((sdc_u32)in[41] << 16) | ((sdc_u32)in[42] << 8) | in[43];
    if (crc32(in, 40) != c || in[5] != 0 || in[6] != 0 || in[7] > 128u) return -3;
    for (i = 0; i < 32; i++) seed[i] = in[8 + i];
    *bits = in[7];
    return 0;
}
