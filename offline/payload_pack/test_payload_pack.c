/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later
 * Host test driver (libc allowed here only).  Commands:
 *   dec IN.mslz EXPECTED_LEN OUT        decode a file, print status and length
 *   sha IN                               print sha256 hex
 *   open CONTAINER PUBHEX MINSEQ OUT     ms_pack_open with a stub verifier that accepts iff the signature bytes equal PUBHEX-given
 *                                        "good" marker (the Ed25519 check is done by the Python reference in the test); prints status
 *   fuzz SEED ITER                       random and mutated streams, must never crash (ASan/UBSan) or exceed the cap
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ms_pack.h"
#include "ms_sha256.h"

static ms_u8 *slurp(const char *p, ms_u32 *n)
{
    FILE *f = fopen(p, "rb"); long sz; ms_u8 *b;
    if (!f) { perror(p); exit(3); }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    b = (ms_u8 *)malloc(sz ? sz : 1);
    if (fread(b, 1, sz, f) != (size_t)sz) exit(3);
    fclose(f); *n = (ms_u32)sz; return b;
}
static int stub_ok(const ms_u8 *pub, const ms_u8 *m, ms_u32 n, const ms_u8 *sig) { (void)pub; (void)m; (void)n; return sig[0] != 0xEE; }
static int stub_bad(const ms_u8 *pub, const ms_u8 *m, ms_u32 n, const ms_u8 *sig) { (void)pub; (void)m; (void)n; (void)sig; return 0; }
static unsigned long rs;
static unsigned rnd(void) { rs = rs * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(rs >> 33); }

int main(int argc, char **argv)
{
    ms_u32 n, m, len = 0; ms_u8 *a, *o; int r, i;
    if (argc >= 5 && !strcmp(argv[1], "dec")) {
        a = slurp(argv[2], &n); m = (ms_u32)strtoul(argv[3], 0, 10); o = (ms_u8 *)malloc(m + 1);
        r = ms_lz_decode(a, n, o, m, &len);
        if (r == 0) { FILE *f = fopen(argv[4], "wb"); fwrite(o, 1, len, f); fclose(f); }
        printf("%d %lu\n", r, (unsigned long)len); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "sha")) {
        ms_u8 h[32]; a = slurp(argv[2], &n); ms_sha256(a, n, h);
        for (i = 0; i < 32; i++) printf("%02x", h[i]); printf("\n"); return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "open")) {
        struct ms_pack_policy p; ms_u8 pub[32]; ms_u32 seq = 0, cap = MS_MAX_RAW; ms_u8 *raw = (ms_u8 *)malloc(cap);
        a = slurp(argv[2], &n); memset(pub, 0, 32);
        p.pub = pub; p.min_seq = (ms_u32)strtoul(argv[4], 0, 10); p.loader_version = 1; p.codeset_sha256 = 0;
        p.verify = !strcmp(argv[3], "reject") ? stub_bad : stub_ok;
        r = ms_pack_open(a, n, &p, raw, cap, &len, &seq);
        if (r == 0) { FILE *f = fopen(argv[5], "wb"); fwrite(raw, 1, len, f); fclose(f); }
        printf("%d %lu %lu\n", r, (unsigned long)len, (unsigned long)seq); return 0;
    }
    if (argc >= 5 && !strcmp(argv[1], "fuzz")) {
        /* argv[2]=seed argv[3]=iterations argv[4]=a valid MSLZ file to mutate */
        ms_u32 vn; ms_u8 *v = slurp(argv[4], &vn); ms_u8 buf[4096]; ms_u8 out[8192]; long it, iters = atol(argv[3]);
        rs = strtoul(argv[2], 0, 10);
        for (it = 0; it < iters; it++) {
            ms_u32 bl = vn < sizeof buf ? vn : (ms_u32)sizeof buf, cap = 1 + rnd() % 8192, k;
            memcpy(buf, v, bl);
            for (k = rnd() % 6; k; k--) buf[rnd() % bl] = (ms_u8)rnd();
            if (rnd() % 4 == 0) bl = rnd() % (bl + 1);
            if (rnd() % 8 == 0) for (k = 0; k < bl; k++) buf[k] = (ms_u8)rnd();
            r = ms_lz_decode(buf, bl, out, cap, &len);
            if (r == 0 && len > cap) { puts("cap exceeded"); return 1; }
        }
        puts("fuzz ok"); return 0;
    }
    fprintf(stderr, "usage\n"); return 2;
}
