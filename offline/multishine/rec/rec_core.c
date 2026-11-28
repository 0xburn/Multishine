/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LOCAL FORK: see rec_core.h. */
#include "rec_core.h"

typedef rec_u32 u32;
typedef rec_s32 s32;
typedef rec_u8 u8;

static void cpy(u8 *d, const u8 *s, u32 n)               /* own copy: words when both sides are aligned (the ring copy is the biggest cost of a re-simulated frame) */
{
    if (!(((unsigned long)d | (unsigned long)s) & 3u))
        for (; n >= 4u; n -= 4u, d += 4, s += 4) *(volatile u32 *)d = *(const u32 *)s;
    while (n--) *(volatile u8 *)d++ = *s++;
}
static void zero(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }
static u32 g32(const u8 *p) { return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3]; }
static u32 g16(const u8 *p) { return ((u32)p[0] << 8) | p[1]; }
static void p32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

void rec_crc_table(u32 *t)
{
    u32 i, k, c;
    for (i = 0; i < 256u; i++) {
        c = i;
        for (k = 0; k < 8u; k++) c = c & 1u ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        t[i] = c;
    }
}
u32 rec_crc(const u32 *t, u32 c, const u8 *p, u32 n) { while (n--) c = t[(c ^ *p++) & 255u] ^ (c >> 8); return c; }

/* event sizes (command byte included), 0 = not part of a frame */
static u32 ev_size(u32 c)
{
    switch (c) {
    case 0x37: return 67; case 0x38: return 93; case 0x3A: return 13; case 0x3B: return 45;
    case 0x3C: return 9; case 0x3F: return 10; case 0x40: return 6; case 0x41: return 9;
    default: return 0;
    }
}

void rec_init(struct rec *r, int (*sink)(void *, const u8 *, u32), void *ctx)
{
    zero(r, (u32)sizeof *r);
    r->sink = sink; r->ctx = ctx; r->state = REC_WAIT;
    rec_crc_table(r->crct);
}
void rec_drop(struct rec *r, int why) { if (r->state == REC_WAIT || r->state == REC_RUN) { r->state = REC_DROPPED; r->why = why; } }

static int put(struct rec *r, const u8 *b, u32 n)
{
    if (r->state != REC_RUN) return -1;
    if (r->sink(r->ctx, b, n)) { rec_drop(r, REC_D_SINK); return -1; }
    r->out += n;
    return 0;
}
static void raw(struct rec *r, const u8 *b, u32 n) { r->crc = rec_crc(r->crct, r->crc, b, n); r->raw += n; }

static void push(struct rec_hist *h, const u8 *e, u32 n)
{
    cpy(h->pp, h->p, n); cpy(h->p, e, n);
    if (h->n < 2) h->n++;
}
static void base(u8 *q, const struct rec_hist *h, u32 n) { if (h && h->n) cpy(q, h->p, n); else zero(q, n); }
static void lin(u8 *q, const struct rec_hist *h, u32 o) { if (h && h->n == 2) p32(q + o, 2u * g32(h->p + o) - g32(h->pp + o)); }

static int delta(struct rec *r, const u8 *e, const u8 *q, u32 n)
{
    u8 o[2 + 12 + REC_EV];
    u32 ng, g, j, m, k, body = n - 5u, at;
    ng = (body + 7u) / 8u;
    at = (ng + 7u) / 8u;
    zero(o, at);
    {   /* masks and data interleave-free: first all masks, then all data (masks are at most 11 bytes) */
        u8 d[REC_EV];
        u32 nd = 0;
        for (g = 0; g < ng; g++) {
            m = 0;
            for (j = 0; j < 8u; j++) {
                k = g * 8u + j;
                if (k >= body) break;
                { u32 x = (u32)(e[5u + k] ^ q[5u + k]); if (x) { m |= 1u << j; d[nd++] = (u8)x; } }
            }
            if (m) { o[g >> 3] = (u8)(o[g >> 3] | (1u << (g & 7u))); o[at++] = (u8)m; }
        }
        cpy(o + at, d, nd);
        at += nd;
    }
    return put(r, o, at);
}

static int keep(struct rec *r, const u8 *e)              /* 0 = this wind / transformation event says nothing new */
{
    if (e[0] == 0x40) return (u32)e[5] != r->wind;
    if (e[0] == 0x41) return g32(e + 5) != r->trans;
    return 1;
}

static void encode(struct rec *r, const struct rec_slot *s)
{
    const u8 *p, *end = s->d + s->len;
    u8 sh[REC_MAXEV * 3], one[2], q[REC_EV];
    u32 n = 0, ne = 0, i, cur = r->iti ^ 1u, same;
    for (p = s->d; p < end; p += ev_size(p[0])) {        /* validated by store() */
        if (!keep(r, p)) continue;
        sh[n++] = p[0];
        if (p[0] == 0x37 || p[0] == 0x38) { sh[n++] = p[5]; sh[n++] = p[6]; }
        else if (p[0] == 0x3F) sh[n++] = p[5];
        ne++;
    }
    same = n == r->nshape;
    for (i = 0; same && i < n; i++) if (sh[i] != r->shape[i]) same = 0;
    if (same) { one[0] = 1; if (put(r, one, 1)) return; }
    else {
        one[0] = 2; one[1] = (u8)ne;
        if (put(r, one, 2) || put(r, sh, n)) return;
        cpy(r->shape, sh, n); r->nshape = n;
    }
    r->itn[cur] = 0;
    for (p = s->d; p < end; p += ev_size(p[0])) {
        u32 c = p[0], z = ev_size(c), k;
        struct rec_hist *h = 0;
        if (!keep(r, p)) continue;
        switch (c) {
        case 0x3A:
            h = &r->start; base(q, h, z); lin(q, h, 5);
            if (h->n) p32(q + 9, g32(h->p + 9) + 1u);
            cpy(r->seed, p + 5, 4);
            break;
        case 0x37:
            k = ((u32)p[5] << 1) | p[6];
            h = &r->pre[k]; base(q, h, z);
            cpy(q + 7, r->seed, 4);
            if (r->post[k].n) { const u8 *o = r->post[k].p; cpy(q + 0xB, o + 8, 2); cpy(q + 0xD, o + 0xA, 12); cpy(q + 0x3C, o + 0x16, 4); }
            q[5] = p[5]; q[6] = p[6];
            break;
        case 0x38:
            k = ((u32)p[5] << 1) | p[6];
            h = &r->post[k]; base(q, h, z);
            lin(q, h, 0x22); lin(q, h, 0xA); lin(q, h, 0xE); lin(q, h, 0x35); lin(q, h, 0x39); lin(q, h, 0x45);
            q[5] = p[5]; q[6] = p[6];
            break;
        case 0x3B: {
            struct rec_hist *o = r->it[r->iti];
            one[0] = 255;
            for (k = 0; k < r->itn[r->iti]; k++) if (g32(o[k].p + 34) == g32(p + 34)) { one[0] = (u8)k; break; }
            h = one[0] == 255 ? 0 : &o[k];
            base(q, h, z); lin(q, h, 12); lin(q, h, 20); lin(q, h, 24); lin(q, h, 30);
            if (put(r, one, 1)) return;
            break; }
        case 0x3C:
            h = &r->book; base(q, h, z);
            p32(q + 5, h->n ? g32(h->p + 5) + 1u : (u32)s->frame);
            break;
        case 0x3F: h = &r->wl[p[5] & 1u]; base(q, h, z); q[5] = p[5]; break;
        case 0x40: h = &r->ds; base(q, h, z); r->wind = p[5]; break;
        default: h = &r->ps; base(q, h, z); r->trans = g32(p + 5); break;
        }
        if (delta(r, p, q, z)) return;
        raw(r, p, z);
        if (c == 0x3B) {
            if (r->itn[cur] < REC_ITEMS) {
                struct rec_hist *d = &r->it[cur][r->itn[cur]++];
                if (h) { cpy(d->pp, h->p, z); cpy(d->p, p, z); d->n = 2; } else { cpy(d->p, p, z); d->n = 1; }
            }
        } else push(h, p, z);
    }
    r->iti = cur;
    r->frames++;
}

/* 1 = a well-formed frame: known events, all of one frame, 0x3A first, one 0x3C, at the end */
static int well_formed(const u8 *p, u32 n, s32 *frame)
{
    u32 at = 0, z, k = 0;
    s32 f = (s32)g32(p + 1);
    if (n < 13u + 9u || p[0] != 0x3A) return 0;
    while (at < n) {
        z = ev_size(p[at]);
        if (!z || at + z > n || (s32)g32(p + at + 1) != f || ++k > REC_MAXEV) return 0;
        if ((p[at] == 0x37 || p[at] == 0x38) && (p[at + 5] > 3 || p[at + 6] > 1)) return 0;
        if (p[at] == 0x3F && p[at + 5] > 1) return 0;
        if ((p[at] == 0x3C) != (at + z == n)) return 0;
        if (p[at] == 0x3A && at) return 0;
        at += z;
    }
    *frame = f;
    return 1;
}

static void header(struct rec *r, const u8 *p, u32 n)
{
    static const u8 head[6] = { 'M', 'S', 'R', '1', 1, 0 };
    u32 n35 = 1u + p[1], n36, i, o = 2;
    u8 b[2];
    if (r->state != REC_WAIT || n < 2u || n35 > n || n35 > 64u || (p[1] - 1u) % 3u) { rec_drop(r, REC_D_HEADER); return; }
    n36 = 0;
    for (i = 2; i + 2u < n35; i += 3u) {
        u32 z = g16(p + i + 1) + 1u, want = ev_size(p[i]);
        if (p[i] == 0x3D || p[i] == 0x10) continue;       /* the code list (and its carrier) is not part of our stream: not announced either */
        if (p[i] == 0x36) n36 = z;
        else if (p[i] == 0x39) { if (z != 7u) { rec_drop(r, REC_D_HEADER); return; } }
        else if (!want || z != want) { rec_drop(r, REC_D_HEADER); return; }    /* another recording version: never guess */
        r->h35[o] = p[i]; r->h35[o + 1] = p[i + 1]; r->h35[o + 2] = p[i + 2]; o += 3;
    }
    r->h35[0] = 0x35; r->h35[1] = (u8)(o - 1u);
    r->n35 = o;
    if (n36 < 400u || n36 > sizeof r->h36 || n35 + n36 > n || p[n35] != 0x36) { rec_drop(r, REC_D_HEADER); return; }
    cpy(r->h36, p + n35, n36); r->n36 = n36;
    r->state = REC_RUN;
    r->newest = r->final = REC_FIRST - 1;
    r->crc = 0xFFFFFFFFu; r->raw = 0; r->out = 0; r->trans = 5u; r->wind = 0;
    for (i = 0; i < REC_RING; i++) r->ring[i].frame = 0x7FFFFFFF;
    if (put(r, head, 6)) return;
    b[0] = (u8)(r->n35 >> 8); b[1] = (u8)r->n35;
    if (put(r, b, 2) || put(r, r->h35, r->n35)) return;
    b[0] = (u8)(n36 >> 8); b[1] = (u8)n36;
    if (put(r, b, 2) || put(r, r->h36, n36)) return;
    raw(r, r->h35, r->n35); raw(r, r->h36, n36);
}

static void finalize_to(struct rec *r, s32 f)
{
    while (r->state == REC_RUN && r->final < f) {
        struct rec_slot *s = &r->ring[(u32)(r->final + 1 - REC_FIRST) & (REC_RING - 1u)];
        if (s->frame != r->final + 1) { rec_drop(r, REC_D_ORDER); return; }
        encode(r, s);
        r->final++;
    }
}

static void frame_in(struct rec *r, const u8 *p, u32 n)
{
    struct rec_slot *s;
    s32 f, fin;
    if (n > REC_FB) { rec_drop(r, REC_D_SIZE); return; }
    if (!well_formed(p, n, &f)) { rec_drop(r, REC_D_EVENT); return; }
    if (f < REC_FIRST || f > r->newest + 1) { rec_drop(r, REC_D_ORDER); return; }
    s = &r->ring[(u32)(f - REC_FIRST) & (REC_RING - 1u)];
    if (f <= r->newest) r->resim++;
    if (f <= r->final) {                                  /* already encoded: the engine re-ran a finalized frame (a restore deeper than the mispredicted frame); it must come out the same */
        if (s->frame == f) {
            u32 i;
            r->refinal_n++;
            for (i = 0; i + 4u < n && i + 4u < s->len && p[i] == s->d[i]; i++) {}       /* all but the bookend's finalized frame */
            if (n != s->len || i + 4u != n) { r->refinal_diff++; if (!r->diff_at) { r->diff_at = i + 1u; r->diff_f = f; } }   /* counted, the first final version stays (a cosmetic bit such as the off-screen flag may depend on the camera of the last drawn frame) */
        }
        r->newest = f;
        return;
    }
    if ((u32)(f - r->final) > REC_RING) { rec_drop(r, REC_D_DEPTH); return; }
    if ((u32)(f - r->final) > r->maxdepth) r->maxdepth = (u32)(f - r->final);
    cpy(s->d, p, n);
    s->len = n; s->frame = f;
    r->newest = f;
    fin = (s32)g32(p + n - 4);
    finalize_to(r, fin < f ? fin : f);
}

static void game_end(struct rec *r, const u8 *p, u32 n)
{
    u8 t[12], tag = 3;
    if (n < 7u) { rec_drop(r, REC_D_EVENT); return; }
    finalize_to(r, r->newest);                            /* the game is over: nothing is re-simulated any more */
    if (r->state != REC_RUN || put(r, &tag, 1) || put(r, p, 7)) return;
    raw(r, p, 7);
    p32(t, r->frames); p32(t + 4, r->raw); p32(t + 8, ~r->crc);
    if (put(r, t, 12)) return;
    r->state = REC_DONE;
}

void rec_feed(struct rec *r, const u8 *p, u32 n)
{
    if (!n) return;
    if (r->state == REC_WAIT && p[0] == 0x35) { header(r, p, n); return; }
    if (r->state != REC_RUN) return;
    if (p[0] == 0x35) { rec_drop(r, REC_D_HEADER); return; }
    if (p[0] == 0x10 || p[0] == 0x3D) return;
    if (p[0] == 0x39) game_end(r, p, n);
    else frame_in(r, p, n);
}
