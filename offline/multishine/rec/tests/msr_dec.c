/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "msr_dec.h"
#include <string.h>
typedef unsigned u32; typedef unsigned char u8;
static u32 g32(const u8 *p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static void p32(u8 *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
struct H { u8 p[93], pp[93]; int n; };
static u32 sz(u32 c) { switch (c) { case 0x37: return 67; case 0x38: return 93; case 0x3A: return 13; case 0x3B: return 45; case 0x3C: return 9; case 0x3F: return 10; case 0x40: return 6; case 0x41: return 9; } return 0; }
static void lin(u8 *q, struct H *h, int o) { if (h && h->n == 2) p32(q + o, 2 * g32(h->p + o) - g32(h->pp + o)); }
static u32 crc(const u8 *p, u32 n) { u32 c = ~0u, k; while (n--) { c ^= *p++; for (k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; } return ~c; }
unsigned msr_decode(const u8 *in, unsigned n, u8 *out, unsigned cap, unsigned *frames)
{
    static struct H st, bk, pre[8], post[8], wl[2], ds, ps, it[2][64];
    u32 a = 0, o = 0, l, itn[2] = { 0, 0 }, iti = 0, ne = 0, i, nf = 0;
    u8 shape[48][3], seed[4] = { 0 };
    memset(&st, 0, sizeof st); memset(&bk, 0, sizeof bk); memset(pre, 0, sizeof pre); memset(post, 0, sizeof post); memset(wl, 0, sizeof wl); memset(&ds, 0, sizeof ds); memset(&ps, 0, sizeof ps);
    if (n < 10 || memcmp(in, "MSR1\1\0", 6)) return 0;
    a = 6;
    for (i = 0; i < 2; i++) { l = (in[a] << 8) | in[a + 1]; a += 2; if (a + l > n || o + l > cap) return 0; memcpy(out + o, in + a, l); a += l; o += l; }
    for (;;) {
        u32 tag, cur, k;
        if (a + 1 > n) return 0;
        tag = in[a++];
        if (tag == 3) break;
        if (tag == 2) { if (a + 1 + 144 > n + 144 || a >= n) return 0; ne = in[a++]; if (ne > 48 || a + 3 * ne > n) return 0; for (i = 0; i < ne; i++) { shape[i][0] = in[a++]; if (shape[i][0] == 0x37 || shape[i][0] == 0x38) { shape[i][1] = in[a++]; shape[i][2] = in[a++]; } else if (shape[i][0] == 0x3F) shape[i][1] = in[a++]; } }
        else if (tag != 1) return 0;
        cur = iti ^ 1; itn[cur] = 0;
        for (i = 0; i < ne; i++) {
            u32 c = shape[i][0], z = sz(c), g, ng, gb; u8 q[93]; struct H *h = 0; const u8 *gm;
            if (!z || o + z > cap || a + 16 > n) return 0;
            k = ((u32)shape[i][1] << 1) | shape[i][2];
            if ((c == 0x37 || c == 0x38) && k > 7) return 0;
            if (c == 0x3A) h = &st; else if (c == 0x37) h = &pre[k]; else if (c == 0x38) h = &post[k]; else if (c == 0x3C) h = &bk; else if (c == 0x40) h = &ds; else if (c == 0x41) h = &ps;
            else if (c == 0x3B) { u32 x = in[a++]; if (x != 255) { if (x >= itn[iti]) return 0; h = &it[iti][x]; } }
            if (c == 0x3F) h = &wl[shape[i][1] & 1];
            if (h && h->n) memcpy(q, h->p, z); else memset(q, 0, z);
            if (c == 0x3A) { lin(q, h, 5); if (h->n) p32(q + 9, g32(h->p + 9) + 1); }
            if (c == 0x37) { memcpy(q + 7, seed, 4); if (post[k].n) { memcpy(q + 0xB, post[k].p + 8, 2); memcpy(q + 0xD, post[k].p + 0xA, 12); memcpy(q + 0x3C, post[k].p + 0x16, 4); } }
            if (c == 0x38) { lin(q, h, 0x22); lin(q, h, 0xA); lin(q, h, 0xE); lin(q, h, 0x35); lin(q, h, 0x39); lin(q, h, 0x45); }
            if (c == 0x37 || c == 0x38) { q[5] = shape[i][1]; q[6] = shape[i][2]; }
            if (c == 0x3F) q[5] = shape[i][1];
            if (c == 0x3B) { lin(q, h, 12); lin(q, h, 20); lin(q, h, 24); lin(q, h, 30); }
            if (c == 0x3C) p32(q + 5, h->n ? g32(h->p + 5) + 1 : (u32)((int)nf - 123));
            q[0] = c; p32(q + 1, (u32)((int)nf - 123));
            ng = (z - 5 + 7) / 8; gb = (ng + 7) / 8; gm = in + a; a += gb;
            if (a + 12 > n) return 0;
            { const u8 *mp = in + a; u32 nm = 0, d; for (g = 0; g < ng; g++) if (gm[g >> 3] & (1 << (g & 7))) nm++; d = a + nm;
              for (g = 0; g < ng; g++) if (gm[g >> 3] & (1 << (g & 7))) { u32 m = *mp++, j; for (j = 0; j < 8; j++) if (m & (1 << j)) { if (5 + g * 8 + j >= z || d >= n) return 0; q[5 + g * 8 + j] ^= in[d++]; } }
              a = d; }
            memcpy(out + o, q, z); o += z;
            if (c == 0x3A) memcpy(seed, q + 5, 4);
            if (c == 0x3B) { if (itn[cur] < 15) { struct H *d = &it[cur][itn[cur]++]; if (h) { memcpy(d->pp, h->p, z); d->n = 2; } else d->n = 1; memcpy(d->p, q, z); } }
            else { memcpy(h->pp, h->p, z); memcpy(h->p, q, z); if (h->n < 2) h->n++; }
        }
        iti = cur; nf++;
    }
    if (a + 16 > n + 16 || a + 7 + 12 != n || o + 7 > cap) return 0;
    memcpy(out + o, in + a, 7); o += 7; a += 7;
    if (g32(in + a) != nf || g32(in + a + 4) != o || g32(in + a + 8) != crc(out, o)) return 0;
    *frames = nf;
    return o;
}
