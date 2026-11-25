/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host tests of rec_core: synthetic games with rollbacks against an independent model (last simulation of each frame wins), error paths, and (argv) real .slp files replayed as the
   device stream, re-simulated frames included: the decoded stream must equal the file's own last version of every frame. */
#include "../rec_core.h"
#include "msr_dec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char OUT[8 << 20], DEC[24 << 20], WANT[24 << 20];
static unsigned n_out, cap_out = sizeof OUT, fails;
static struct rec R;
#define CK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static int sink(void *c, const rec_u8 *b, rec_u32 n) { (void)c; if (n_out + n > cap_out) return -1; memcpy(OUT + n_out, b, n); n_out += n; return 0; }
static void w32(unsigned char *p, unsigned v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static unsigned g32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static unsigned rs = 1;
static unsigned rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static unsigned header(unsigned char *b)
{
    static const unsigned t[][2] = { {0x36,828},{0x37,66},{0x38,92},{0x39,6},{0x3A,12},{0x3B,44},{0x3C,8},{0x3D,1234},{0x10,516},{0x3F,9},{0x40,5},{0x41,8} };
    unsigned i, n = 2;
    b[0] = 0x35; b[1] = 3 * 12 + 1;
    for (i = 0; i < 12; i++) { b[n++] = t[i][0]; b[n++] = t[i][1] >> 8; b[n++] = t[i][1]; }
    b[n] = 0x36; for (i = 1; i < 829; i++) b[n + i] = (unsigned char)(i * 7);
    return n + 829;
}
/* one simulated frame: content depends on (frame, variant) */
static unsigned mk(unsigned char *b, int f, unsigned var, int fin, int ics, unsigned items, int wind)
{
    unsigned n = 0, p, i, k;
    unsigned s = (unsigned)f * 2654435761u + var * 40503u + 1;
    b[n] = 0x3A; w32(b + n + 1, f); w32(b + n + 5, (unsigned)f << 16); w32(b + n + 9, f + 200); n += 13;
    for (p = 0; p < 2; p++) for (i = 0; i < (ics && p ? 2u : 1u); i++) {
        b[n] = 0x37; w32(b + n + 1, f); b[n + 5] = p; b[n + 6] = i; for (k = 7; k < 67; k++) b[n + k] = (unsigned char)((s >> (k & 15)) & (k & 3 ? 0xFF : 1)); w32(b + n + 7, (unsigned)f << 16); n += 67; }
    for (p = 0; p < 2; p++) for (i = 0; i < (ics && p ? 2u : 1u); i++) {
        if (ics && p && i && (f / 100) % 2) continue;                     /* the follower comes and goes */
        b[n] = 0x38; w32(b + n + 1, f); b[n + 5] = p; b[n + 6] = i; for (k = 7; k < 93; k++) b[n + k] = (unsigned char)(k < 0x2B ? (s * k) >> 11 : f / 50); n += 93; }
    for (i = 0; i < items; i++) { b[n] = 0x3B; w32(b + n + 1, f); for (k = 5; k < 45; k++) b[n + k] = (unsigned char)(s + k * i); w32(b + n + 34, (unsigned)f / 40 + i); n += 45; }
    if (f % 3 == 0) for (i = 0; i < 2; i++) { b[n] = 0x3F; w32(b + n + 1, f); b[n + 5] = i; w32(b + n + 6, s); n += 10; }
    b[n] = 0x40; w32(b + n + 1, f); b[n + 5] = wind; n += 6;
    b[n] = 0x3C; w32(b + n + 1, f); w32(b + n + 5, fin); n += 9;
    return n;
}
static unsigned final_(unsigned char *o, const unsigned char *b, unsigned n, unsigned *wind)     /* the model's copy of a final frame: a wind event only when it changes */
{
    unsigned a = 0, m = 0;
    static const unsigned z[16] = { 0 }; (void)z;
    while (a < n) { unsigned l = b[a] == 0x37 ? 67 : b[a] == 0x38 ? 93 : b[a] == 0x3A ? 13 : b[a] == 0x3B ? 45 : b[a] == 0x3C ? 9 : b[a] == 0x3F ? 10 : b[a] == 0x40 ? 6 : 9;
        if (!(b[a] == 0x40 && b[a + 5] == *wind)) { memcpy(o + m, b + a, l); m += l; }
        if (b[a] == 0x40) *wind = b[a + 5];
        a += l; }
    return m;
}
static void synth(int frames, int rollback, int ics, unsigned items)
{
    unsigned char b[4096], h[1024];
    unsigned n, want = 0, wind = 0, fr = 0, l, i, hn = header(h);
    int f, g, fin = -124;
    static unsigned char last[16384][2100]; static unsigned lastn[16384];
    n_out = 0; rec_init(&R, sink, 0);
    rec_feed(&R, h, hn);
    for (f = -123; f < -123 + frames; ) {
        n = mk(b, f, rnd(), rollback ? fin : f, ics, items, (f / 200) & 1); rec_feed(&R, b, n);
        memcpy(last[f + 123], b, n); lastn[f + 123] = n;
        if (rollback && f > fin + 1 && rnd() % 3 == 0) {                  /* confirmed up to some frame; re-run from there with other content */
            int d = 1 + rnd() % (f - fin), to = fin + d;
            if (to < f) {
                for (g = fin + 1; g <= f; g++) { int nf = to < g ? to : g; n = mk(b, g, rnd(), nf, ics, items, (g / 200) & 1); rec_feed(&R, b, n); memcpy(last[g + 123], b, n); lastn[g + 123] = n; }
                fin = to;
            }
        }
        if (rollback && f - fin > 7) { int nf = f - 2; for (g = fin + 1; g <= f; g++) { n = mk(b, g, rnd(), nf < g ? nf : g, ics, items, (g / 200) & 1); rec_feed(&R, b, n); memcpy(last[g + 123], b, n); lastn[g + 123] = n; } fin = nf; }
        f++;
    }
    { unsigned char e[7] = { 0x39, 2, 0xFF, 0, 1, 0xFF, 0xFF }; rec_feed(&R, e, 7); }
    CK(R.state == REC_DONE);
    /* expected raw */
    { unsigned char *w = WANT; unsigned o = 2; w[0] = 0x35; for (i = 2; i < 38; i += 3) if (h[i] != 0x3D && h[i] != 0x10) { memcpy(w + o, h + i, 3); o += 3; } w[1] = o - 1; want = o; memcpy(w + want, h + 38, 829); want += 829; }
    for (i = 0; i < (unsigned)frames; i++) want += final_(WANT + want, last[i], lastn[i], &wind);
    { unsigned char e[7] = { 0x39, 2, 0xFF, 0, 1, 0xFF, 0xFF }; memcpy(WANT + want, e, 7); want += 7; }
    l = msr_decode(OUT, n_out, DEC, sizeof DEC, &fr);
    CK(l == want && fr == (unsigned)frames && !memcmp(DEC, WANT, want));
    printf("synthetic frames %d rollback %d ics %d items %u: raw %u msr %u (%.1f%%) resim %u depth %u\n", frames, rollback, ics, items, want, n_out, 100.0 * n_out / want, R.resim, R.maxdepth);
}
static void errors(void)
{
    unsigned char b[4096], h[1024]; unsigned n, hn = header(h), i, fr;
    /* sink full */
    n_out = 0; cap_out = 3000; rec_init(&R, sink, 0); rec_feed(&R, h, hn);
    for (i = 0; i < 100; i++) { n = mk(b, -123 + (int)i, i, -123 + (int)i, 0, 0, 0); rec_feed(&R, b, n); }
    CK(R.state == REC_DROPPED && R.why == REC_D_SINK); cap_out = sizeof OUT;
    /* a skipped frame */
    n_out = 0; rec_init(&R, sink, 0); rec_feed(&R, h, hn); n = mk(b, -123, 0, -123, 0, 0, 0); rec_feed(&R, b, n); n = mk(b, -121, 0, -121, 0, 0, 0); rec_feed(&R, b, n); CK(R.state == REC_DROPPED && R.why == REC_D_ORDER);
    /* never finalized: too deep */
    n_out = 0; rec_init(&R, sink, 0); rec_feed(&R, h, hn); for (i = 0; i < 40; i++) { n = mk(b, -123 + (int)i, 0, -124, 0, 0, 0); rec_feed(&R, b, n); } CK(R.state == REC_DROPPED && R.why == REC_D_DEPTH);
    /* truncated, wrong event, another version's sizes */
    n_out = 0; rec_init(&R, sink, 0); rec_feed(&R, h, hn); n = mk(b, -123, 0, -123, 0, 0, 0); rec_feed(&R, b, n - 3); CK(R.why == REC_D_EVENT);
    n_out = 0; rec_init(&R, sink, 0); rec_feed(&R, h, hn); n = mk(b, -123, 0, -123, 0, 0, 0); b[13] = 0x55; rec_feed(&R, b, n); CK(R.why == REC_D_EVENT);
    n_out = 0; rec_init(&R, sink, 0); h[10]++; rec_feed(&R, h, hn); CK(R.why == REC_D_HEADER);
    /* finalized frame ahead of the frame just simulated is clamped (Slippi's clamp, kept as a second line) */
    hn = header(h); n_out = 0; rec_init(&R, sink, 0); rec_feed(&R, h, hn); n = mk(b, -123, 0, 500, 0, 0, 0); rec_feed(&R, b, n); CK(R.state == REC_RUN && R.final == -123);
    /* a finalized frame that re-runs: the same is counted, a different one is reported, the first version stays */
    n = mk(b, -122, 0, -122, 0, 0, 0); rec_feed(&R, b, n); rec_feed(&R, b, n); CK(R.refinal_n == 1 && R.refinal_diff == 0); b[30] ^= 1; rec_feed(&R, b, n); CK(R.refinal_diff == 1 && R.state == REC_RUN);
    /* no end: nothing decodes */
    CK(msr_decode(OUT, n_out, DEC, sizeof DEC, &fr) == 0);
    /* any flipped byte is noticed */
    { unsigned char e[7] = { 0x39, 7, 0, 0, 1, 0xFF, 0xFF }; unsigned k, miss = 0; rec_feed(&R, e, 7); CK(R.state == REC_DONE); { unsigned g = msr_decode(OUT, n_out, WANT, sizeof WANT, &fr); CK(g > 0); 
      for (k = 0; k < n_out; k++) { OUT[k] ^= 4; { unsigned q = msr_decode(OUT, n_out, DEC, sizeof DEC, &fr); if (q && (q != g || memcmp(DEC, WANT, g))) miss++; } OUT[k] ^= 4; } CK(miss == 0); } }
    rec_feed(&R, b, n); CK(R.state == REC_DONE);       /* after the end nothing is taken */
}
static unsigned nsz(unsigned c) { switch (c) { case 0x36: return 829; case 0x37: return 67; case 0x38: return 93; case 0x3A: return 13; case 0x3B: return 45; case 0x3C: return 9; case 0x39: return 7; case 0x3F: return 10; case 0x40: return 6; case 0x41: return 9; } return 0; }
static void slp(const char *path)                /* older recordings: events are padded with zeros to the current sizes (the rollback pattern and the values are the real ones) */
{
    FILE *f = fopen(path, "rb"); unsigned char *d, *pad; long n; unsigned a, e, l, want = 0, fr, sizes[256] = { 0 }, fa, dl = 0, ps = 5, ended = 0, po = 0;
    static unsigned off[40000], len[40000]; unsigned nfr = 0, i;
    unsigned char h[1024];
    if (!f) { printf("no %s\n", path); fails++; return; }
    fseek(f, 0, 2); n = ftell(f); rewind(f); d = malloc(n); pad = calloc(1, 2 * n + 4096); if (fread(d, 1, n, f) != (size_t)n) exit(1); fclose(f);
    a = 15; e = a + g32(d + 11); if (g32(d + 11) == 0) e = n;
    for (i = 2; i < 1u + d[a + 1]; i += 3) sizes[d[a + i]] = ((d[a + i + 1] << 8) | d[a + i + 2]) + 1;
    if (!sizes[0x3C] || sizes[0x38] > 93 || sizes[0x36] > 829) { printf("skip %s\n", path); goto out; }
    n_out = 0; rec_init(&R, sink, 0);
    header(h); memset(h + 38, 0, 829); memcpy(h + 38, d + a + 1 + d[a + 1], sizes[0x36]); rec_feed(&R, h, 38 + 829); fa = a + 1 + d[a + 1] + sizes[0x36];
    if (R.state != REC_RUN) { printf("%s why %d\n", path, R.why); fails++; goto out; }
    memset(len, 0, sizeof len);
    while (fa < e) {
        unsigned c = d[fa], s = po; int fno;
        if (!sizes[c]) break;
        if (c == 0x10 || c == 0x3D) { fa += sizes[c]; continue; }
        if (c == 0x39) { memcpy(pad + po, d + fa, sizes[c] < 7 ? sizes[c] : 7); rec_feed(&R, pad + po, 7); ended = po; break; }
        fno = (int)g32(d + fa + 1);
        for (;;) { c = d[fa]; if (fa >= e || !nsz(c) || !sizes[c]) break; memcpy(pad + po, d + fa, sizes[c]); po += nsz(c); fa += sizes[c]; if (c == 0x3C) break; }
        if (c != 0x3C) break;
        rec_feed(&R, pad + s, po - s);
        off[fno + 123] = s; len[fno + 123] = po - s; if ((unsigned)(fno + 124) > nfr) nfr = fno + 124;
    }
    if (!ended) { printf("skip %s (no game end) state %d why %d\n", path, R.state, R.why); goto out; }
    if (R.state != REC_DONE) { printf("%s: state %d why %d\n", path, R.state, R.why); fails++; goto out; }
    { unsigned o = 2; WANT[0] = 0x35; for (i = 2; i < 38; i += 3) if (h[i] != 0x3D && h[i] != 0x10) { memcpy(WANT + o, h + i, 3); o += 3; } WANT[1] = o - 1; want = o; memcpy(WANT + want, h + 38, 829); want += 829; }
    for (i = 0; i < nfr; i++) { unsigned p = off[i], q = p + len[i]; while (p < q) { unsigned z = nsz(pad[p]); int k = !(pad[p] == 0x40 && pad[p + 5] == dl) && !(pad[p] == 0x41 && g32(pad + p + 5) == ps); if (pad[p] == 0x40) dl = pad[p + 5]; if (pad[p] == 0x41) ps = g32(pad + p + 5); if (k) { memcpy(WANT + want, pad + p, z); want += z; } p += z; } }
    memcpy(WANT + want, pad + ended, 7); want += 7;
    l = msr_decode(OUT, n_out, DEC, sizeof DEC, &fr);
    if (l != want || memcmp(DEC, WANT, want)) { unsigned k = 0; while (k < l && k < want && DEC[k] == WANT[k]) k++; printf("MISMATCH %s l %u want %u at %u\n", path, l, want, k); fails++; }
    else printf("%s: frames %u raw %u msr %u = %.1f B/frame resim %u maxdepth %u refinal %u diff %u\n", strrchr(path, '/') + 1, nfr, want, n_out, (double)n_out / nfr, R.resim, R.maxdepth, R.refinal_n, R.refinal_diff);
out:
    free(d); free(pad);
}
/* --fixture OUT.msr OUT.raw: a synthetic game with rollbacks, valid names / codes / match id: the website's decoder test reads it */
static int fixture(const char *msr, const char *rawp)
{
    unsigned char b[4096], h[1024], *g = h + 38; unsigned n, l, fr, i; int f; FILE *o;
    static const char *code[2] = { "FIXA\x81\x94" "1", "FIXB\x81\x94" "22" }, *nm[2] = { "Fixture A", "Fixture\x81\x49" "B" };
    header(h);
    memset(g + 1, 0, 828); g[1] = 3; g[2] = 20; g[0x14] = 31;
    for (i = 0; i < 4; i++) { g[0x65 + 0x24 * i] = i ? 10 : 2; g[0x65 + 0x24 * i + 1] = i < 2 ? 0 : 3; }
    for (i = 0; i < 2; i++) { strcpy((char *)g + 0x1A5 + 31 * i, nm[i]); strcpy((char *)g + 0x221 + 10 * i, code[i]); }
    strcpy((char *)g + 0x2BE, "mode.unranked-2026-11-25T12:00:00.00-0"); g[0x2F4] = 1;
    n_out = 0; rec_init(&R, sink, 0); rec_feed(&R, h, 38 + 829);
    for (f = -123; f < 277; f++) {
        n = mk(b, f, rnd(), f - 3 < -123 ? -124 : f - 3, 1, 2, (f / 100) & 1); rec_feed(&R, b, n);
        if (f % 7 == 0 && f > -110) { int k; for (k = f - 2; k <= f; k++) { n = mk(b, k, rnd(), k < f - 1 ? k : f - 1, 1, 2, (k / 100) & 1); rec_feed(&R, b, n); } }
    }
    { unsigned char e[7] = { 0x39, 2, 0xFF, 0, 1, 0xFF, 0xFF }; rec_feed(&R, e, 7); }
    l = msr_decode(OUT, n_out, DEC, sizeof DEC, &fr);
    if (R.state != REC_DONE || !l) return 1;
    o = fopen(msr, "wb"); fwrite(OUT, 1, n_out, o); fclose(o);
    o = fopen(rawp, "wb"); fwrite(DEC, 1, l, o); fclose(o);
    printf("fixture: %u frames, raw %u, msr %u, resim %u\n", fr, l, n_out, R.resim);
    return 0;
}
int main(int argc, char **argv)
{
    int i;
    if (argc == 4 && !strcmp(argv[1], "--fixture")) return fixture(argv[2], argv[3]);
    printf("sizeof(struct rec) = %u\n", (unsigned)sizeof(struct rec));
    if (argc > 1) { for (i = 1; i < argc; i++) slp(argv[i]); }
    else {
        synth(1, 0, 0, 0); synth(3000, 0, 0, 0); synth(3000, 1, 0, 0); synth(4000, 1, 1, 3); synth(1500, 1, 1, 15);
        for (i = 0; i < 30; i++) synth(300 + i * 17, 1, i & 1, i % 16);
        errors();
    }
    printf(fails ? "rec_test: %u FAILED\n" : "rec_test: all passed\n", fails);
    return fails != 0;
}
