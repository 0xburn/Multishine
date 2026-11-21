/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* scan_windows: does any candidate file share a 64-byte window with a reference file?  (tests: "no Nintendo bytes on anything the site serves")
   usage: scan_windows REFERENCE CANDIDATE...   prints "HITS n" and the first hits (SCAN_PRINT=N: the first N); exit 0 = no hit, 1 = hits, 2 = error.
   Windows with fewer than 12 distinct byte values (zero fill and other trivia) and counting runs (see counting()) are ignored.  Reads only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define W 64
static const uint64_t B = 1000003ULL;
static uint64_t bpow;                         /* B^(W-1) */
typedef struct { uint64_t h; uint32_t file, off; } Ent;
static Ent *tab; static size_t cap;
/* a counting run: the byte differences repeat with a period of 1, 2 or 4 (0x00 0x01 0x02.., u16 0x0006 0x0007.., u32 indices): a card's block allocation table, a font's cmap/post glyph
   index list and many tables of every program are such runs; they carry no content, so they are not a shared stretch */
static int counting(const unsigned char *p) { for (int k = 1; k <= 4; k <<= 1) { int ok = 1; for (int i = k; i + k < W && ok; i++) ok = (unsigned char)(p[i + k] - p[i]) == (unsigned char)(p[i] - p[i - k]); if (ok) return 1; } return 0; }
static int rich(const unsigned char *p) { unsigned char seen[256] = {0}; int n = 0; for (int i = 0; i < W; i++) if (!seen[p[i]]) { seen[p[i]] = 1; n++; } return n >= 12 && !counting(p); }
static uint64_t hw(const unsigned char *p) { uint64_t h = 0; for (int i = 0; i < W; i++) h = h * B + p[i] + 1; return h ? h : 1; }
static unsigned char **gcb;                   /* the candidates: put keeps one entry per distinct window (a file with many equal windows made every probe walk them all: minutes) */
static void put(uint64_t h, uint32_t f, uint32_t o) { size_t i = h & (cap - 1); while (tab[i].h) { if (tab[i].h == h && !memcmp(gcb[tab[i].file] + tab[i].off, gcb[f] + o, W)) return; i = (i + 1) & (cap - 1); } tab[i].h = h; tab[i].file = f; tab[i].off = o; }
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    unsigned char **cb = calloc(argc, sizeof *cb); size_t *cl = calloc(argc, sizeof *cl); size_t total = 0;
    for (int i = 2; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb"); if (!f) { perror(argv[i]); return 2; }
        fseek(f, 0, SEEK_END); cl[i] = ftell(f); fseek(f, 0, SEEK_SET); cb[i] = malloc(cl[i] + 1);
        if (fread(cb[i], 1, cl[i], f) != cl[i]) return 2;
        fclose(f); total += cl[i];
    }
    gcb = cb; cap = 1; while (cap < total * 2 + 16) cap <<= 1; tab = calloc(cap, sizeof *tab);
    bpow = 1; for (int i = 0; i < W - 1; i++) bpow *= B;
    for (int i = 2; i < argc; i++) for (size_t o = 0; o + W <= cl[i]; o++) if (rich(cb[i] + o)) put(hw(cb[i] + o), i, (uint32_t)o);
    int fd = open(argv[1], O_RDONLY); struct stat st;
    if (fd < 0 || fstat(fd, &st)) { perror(argv[1]); return 2; }
    unsigned char *m = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) { perror("mmap"); return 2; }
    long hits = 0, maxp = getenv("SCAN_PRINT") ? atol(getenv("SCAN_PRINT")) : 5; uint64_t h = 0;
    for (int i = 0; i < W; i++) h = h * B + m[i] + 1;
    for (off_t p = 0; p + W <= st.st_size; p++) {
        if (p) h = (h - (uint64_t)(m[p - 1] + 1) * bpow) * B + m[p + W - 1] + 1;
        uint64_t k = h ? h : 1; size_t i = k & (cap - 1);
        while (tab[i].h) {
            if (tab[i].h == k && !memcmp(cb[tab[i].file] + tab[i].off, m + p, W)) {
                if (hits < maxp) printf("hit: %s offset %u = reference offset %lld\n", argv[tab[i].file], tab[i].off, (long long)p);
                hits++; break;
            }
            i = (i + 1) & (cap - 1);
        }
    }
    printf("HITS %ld\n", hits);
    return hits ? 1 : 0;
}
