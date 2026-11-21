/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* QR encoder, byte mode, level M, versions 1..6.  See qr.h.  Strict C89, no libc, no floating point. */
#include "qr.h"

#define MAXN 41
#define MAXCW 172   /* total codewords at v6 */

/* version tables (index = version), level M: data codewords, ECC codewords per block, blocks */
static const unsigned char V_DATA[7] = {0, 16, 28, 44, 64, 86, 108};
static const unsigned char V_EC[7]   = {0, 10, 16, 26, 18, 24, 16};
static const unsigned char V_BLK[7]  = {0, 1, 1, 1, 2, 2, 4};

static unsigned char gf_exp[512];
static unsigned char gf_log[256];
static int gf_ready;

static unsigned char mod[MAXN * MAXN];    /* base matrix: function patterns + unmasked data */
static unsigned char fn[MAXN * MAXN];     /* 1 = function module (never masked) */
static unsigned char trial[MAXN * MAXN];  /* masked candidate */
static unsigned char best[MAXN * MAXN];
static unsigned char cw[MAXCW];           /* final interleaved codewords */
static unsigned char dat[108];
static unsigned char gen[27];

static void gf_init(void)
{
    unsigned x = 1, i;
    if (gf_ready) return;
    for (i = 0; i < 255; i++) {
        gf_exp[i] = (unsigned char)x;
        gf_log[x] = (unsigned char)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (i = 255; i < 512; i++) gf_exp[i] = gf_exp[i - 255];
    gf_ready = 1;
}

static unsigned char gf_mul(unsigned char a, unsigned char b)
{
    if (!a || !b) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

/* generator polynomial of degree e: gen[0] is the x^e coefficient (always 1) */
static void make_gen(unsigned e)
{
    unsigned i, j;
    for (i = 0; i <= e; i++) gen[i] = 0;
    gen[0] = 1;
    for (i = 0; i < e; i++) {                    /* multiply by (x + alpha^i) */
        for (j = i + 1; j >= 1; j--)
            gen[j] = (unsigned char)(gen[j] ^ gf_mul(gen[j - 1], gf_exp[i]));
    }
}

/* ecc[0..e-1] = remainder of data(x) * x^e divided by gen */
static void rs_ecc(const unsigned char *d, unsigned n, unsigned e, unsigned char *ecc)
{
    unsigned i, j;
    unsigned char f;
    for (j = 0; j < e; j++) ecc[j] = 0;
    for (i = 0; i < n; i++) {
        f = (unsigned char)(d[i] ^ ecc[0]);
        for (j = 0; j + 1 < e; j++) ecc[j] = (unsigned char)(ecc[j + 1] ^ gf_mul(gen[j + 1], f));
        ecc[e - 1] = gf_mul(gen[e], f);
    }
}

static unsigned bitpos;
static void put_bits(unsigned v, unsigned n)
{
    while (n) {
        n--;
        if ((v >> n) & 1u) dat[bitpos >> 3] = (unsigned char)(dat[bitpos >> 3] | (0x80u >> (bitpos & 7u)));
        bitpos++;
    }
}

static void set_fn(unsigned N, int r, int c, int dark)
{
    if (r < 0 || c < 0 || r >= (int)N || c >= (int)N) return;
    mod[r * N + c] = (unsigned char)dark;
    fn[r * N + c] = 1;
}

static void reserve(unsigned N, int r, int c)    /* mark as function module, keep the value (timing stays) */
{
    fn[r * N + c] = 1;
}

static void finder(unsigned N, int r0, int c0)
{
    int dr, dc, d, a, b;
    for (dr = -1; dr <= 7; dr++)
        for (dc = -1; dc <= 7; dc++) {
            /* Chebyshev distance from the centre (3,3): 0..1 dark, 2 light ring, 3 dark ring, 4 separator */
            a = dr - 3; if (a < 0) a = -a;
            b = dc - 3; if (b < 0) b = -b;
            d = a > b ? a : b;
            set_fn(N, r0 + dr, c0 + dc, d != 2 && d != 4);
        }
}

static void layout(unsigned N, unsigned ver)
{
    unsigned i, j;
    int ap, dr, dc, d;
    for (i = 0; i < N * N; i++) { mod[i] = 0; fn[i] = 0; }
    for (i = 8; i + 8 < N; i++) {                /* timing */
        set_fn(N, 6, (int)i, (int)((i & 1u) == 0));
        set_fn(N, (int)i, 6, (int)((i & 1u) == 0));
    }
    finder(N, 0, 0);
    finder(N, 0, (int)N - 7);
    finder(N, (int)N - 7, 0);
    if (ver >= 2) {                              /* one alignment pattern, centre (N-7, N-7) */
        ap = (int)N - 7;
        for (dr = -2; dr <= 2; dr++)
            for (dc = -2; dc <= 2; dc++) {
                d = (dr < 0 ? -dr : dr) > (dc < 0 ? -dc : dc) ? (dr < 0 ? -dr : dr) : (dc < 0 ? -dc : dc);
                set_fn(N, ap + dr, ap + dc, d != 1);
            }
    }
    set_fn(N, (int)N - 8, 8, 1);                 /* dark module */
    for (j = 0; j < 9; j++) {                    /* reserve format areas (value written later) */
        reserve(N, 8, (int)j);
        reserve(N, (int)j, 8);
    }
    for (j = 0; j < 8; j++) {
        reserve(N, 8, (int)(N - 1 - j));
        reserve(N, (int)(N - 1 - j), 8);
    }
}

static void place_data(unsigned N, unsigned total_bits_cw)
{
    int c, r, up = 1, k, col, row;
    unsigned bit = 0, nbits = total_bits_cw * 8u;
    for (c = (int)N - 1; c > 0; c -= 2) {
        if (c == 6) c = 5;
        for (k = 0; k < (int)N; k++) {
            r = up ? (int)N - 1 - k : k;
            for (col = c; col >= c - 1; col--) {
                row = r;
                if (fn[row * N + col]) continue;
                if (bit < nbits) {
                    if ((cw[bit >> 3] >> (7u - (bit & 7u))) & 1u) mod[row * N + col] = 1;
                    bit++;
                }                                /* remainder bits stay 0 */
            }
        }
        up = !up;
    }
}

static int mask_bit(int m, int r, int c)
{
    switch (m) {
    case 0: return ((r + c) & 1) == 0;
    case 1: return (r & 1) == 0;
    case 2: return c % 3 == 0;
    case 3: return (r + c) % 3 == 0;
    case 4: return (((r >> 1) + c / 3) & 1) == 0;
    case 5: return ((r * c) & 1) + (r * c) % 3 == 0;
    case 6: return ((((r * c) & 1) + (r * c) % 3) & 1) == 0;
    default: return ((((r + c) & 1) + (r * c) % 3) & 1) == 0;
    }
}

/* 15-bit format word for level M (bits 00) and mask m: BCH(15,5), generator 0x537, xor 0x5412 */
static unsigned format_bits(int m)
{
    unsigned d = (unsigned)m;              /* 00 for M, then 3 mask bits */
    unsigned r = d << 10, i;
    for (i = 14; i >= 10; i--)
        if ((r >> i) & 1u) r ^= 0x537u << (i - 10);
    return ((d << 10) | r) ^ 0x5412u;
}

static void put_format(unsigned char *t, unsigned N, int m)
{
    unsigned f = format_bits(m), i;
    int dark;
    for (i = 0; i < 15; i++) {
        dark = (int)((f >> i) & 1u);
        /* copy 1 around the top-left finder */
        if (i < 6) t[i * N + 8] = (unsigned char)dark;
        else if (i == 6) t[7 * N + 8] = (unsigned char)dark;
        else if (i == 7) t[8 * N + 8] = (unsigned char)dark;
        else if (i == 8) t[8 * N + 7] = (unsigned char)dark;
        else t[8 * N + (14 - i)] = (unsigned char)dark;
        /* copy 2: top-right row 8 and bottom-left column 8 */
        if (i < 8) t[8 * N + (N - 1 - i)] = (unsigned char)dark;
        else t[(N - 15 + i) * N + 8] = (unsigned char)dark;
    }
}

static void apply_mask(unsigned N, int m)
{
    unsigned r, c;
    for (r = 0; r < N; r++)
        for (c = 0; c < N; c++)
            trial[r * N + c] = (unsigned char)(mod[r * N + c] ^ (!fn[r * N + c] && mask_bit(m, (int)r, (int)c)));
    put_format(trial, N, m);
}

static unsigned long penalty(unsigned N)
{
    static const unsigned char PA[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    static const unsigned char PB[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    unsigned long p = 0;
    unsigned r, c, i, run, dark = 0, q, a, b;
    int line, w, okA, okB;
    unsigned char v, prev;
    for (line = 0; line < 2; line++)             /* 0 = rows, 1 = columns */
        for (r = 0; r < N; r++) {
            run = 0; prev = 2;
            for (c = 0; c < N; c++) {
                v = line ? trial[c * N + r] : trial[r * N + c];
                if (v == prev) run++;
                else { if (run >= 5) p += 3 + (run - 5); run = 1; prev = v; }
            }
            if (run >= 5) p += 3 + (run - 5);
            if (N >= 11)
                for (c = 0; c + 11 <= N; c++) {
                    okA = okB = 1;
                    for (w = 0; w < 11; w++) {
                        v = line ? trial[(c + (unsigned)w) * N + r] : trial[r * N + c + (unsigned)w];
                        if (v != PA[w]) okA = 0;
                        if (v != PB[w]) okB = 0;
                    }
                    if (okA) p += 40;
                    if (okB) p += 40;
                }
        }
    for (r = 0; r + 1 < N; r++)
        for (c = 0; c + 1 < N; c++) {
            v = trial[r * N + c];
            if (v == trial[r * N + c + 1] && v == trial[(r + 1) * N + c] && v == trial[(r + 1) * N + c + 1]) p += 3;
        }
    for (i = 0; i < N * N; i++) dark += trial[i];
    q = dark * 20u / (N * N);                    /* floor(percent / 5) */
    a = q > 10 ? q - 10 : 10 - q;                /* |lower multiple of 5 - 50| / 5 */
    b = q + 1 > 10 ? q + 1 - 10 : 10 - (q + 1); /* |next multiple of 5 - 50| / 5 */
    p += 10ul * (a < b ? a : b);
    return p;
}

int qr_encode_mask(const char *data, unsigned len, int mask, unsigned char *out, unsigned char *size)
{
    unsigned ver, N, i, j, b, nb, ec, dpb, stride, pos, total;
    int m, bestm = 0;
    unsigned long sc, bests = 0;
    unsigned char ecc[26];

    if (!out || !size || (!data && len)) return QR_ERR_ARG;
    if (len > QR_MAX_PAYLOAD) return QR_ERR_TOO_LONG;
    if (mask < -1 || mask > 7) return QR_ERR_MASK;
    gf_init();

    for (ver = 1; ver < 6 && len + 2 > V_DATA[ver]; ver++) {}
    N = 17 + 4 * ver;

    /* data codewords: mode 0100, 8-bit count, bytes, terminator, pad to byte, alternate 0xEC 0x11 */
    for (i = 0; i < 108; i++) dat[i] = 0;
    bitpos = 0;
    put_bits(4, 4);
    put_bits(len, 8);
    for (i = 0; i < len; i++) put_bits((unsigned char)data[i], 8);
    i = V_DATA[ver] * 8u - bitpos;
    put_bits(0, i < 4 ? i : 4);
    pos = (bitpos + 7) >> 3;
    for (j = 0; pos < V_DATA[ver]; pos++, j++) dat[pos] = (unsigned char)((j & 1u) ? 0x11 : 0xEC);

    /* Reed-Solomon per block (all blocks have the same size at v1-6 M), interleave data then ECC */
    nb = V_BLK[ver]; ec = V_EC[ver]; dpb = V_DATA[ver] / nb;
    make_gen(ec);
    pos = 0;
    for (i = 0; i < dpb; i++)
        for (b = 0; b < nb; b++) cw[pos++] = dat[b * dpb + i];
    for (b = 0; b < nb; b++) {
        rs_ecc(dat + b * dpb, dpb, ec, ecc);
        for (i = 0; i < ec; i++) cw[V_DATA[ver] + i * nb + b] = ecc[i];
    }
    total = V_DATA[ver] + nb * ec;

    layout(N, ver);
    place_data(N, total);

    if (mask >= 0) bestm = mask;
    else
        for (m = 0; m < 8; m++) {
            apply_mask(N, m);
            sc = penalty(N);
            if (m == 0 || sc < bests) { bests = sc; bestm = m; }
        }
    apply_mask(N, bestm);
    for (i = 0; i < N * N; i++) best[i] = trial[i];

    stride = (N + 7) >> 3;
    for (i = 0; i < N * stride; i++) out[i] = 0;
    for (i = 0; i < N; i++)
        for (j = 0; j < N; j++)
            if (best[i * N + j]) out[i * stride + (j >> 3)] = (unsigned char)(out[i * stride + (j >> 3)] | (0x80u >> (j & 7u)));
    *size = (unsigned char)N;
    return QR_OK;
}

int qr_encode(const char *data, unsigned len, unsigned char *out, unsigned char *size)
{
    return qr_encode_mask(data, len, -1, out, size);
}
