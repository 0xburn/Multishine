/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_bak: see upd_bak.h.  Soft-float, no libc.  All card access goes through upd_card.c (slot A read-only ops, slot B calls with their own mount). */
#pragma GCC diagnostic ignored "-Wunused-function" /* the verify header's static helpers are not all used here */
#include "upd_bak.h"

#define BLK OTA_BLOCK
#define MK_MAGIC 0x4D53424Bu              /* 'MSBK' */
#define MK_OFF 0x1000u                    /* payload offset in the marker block (comment at 0, like every file of ours) */
#define MK_HDR 16u                        /* magic, version, build_seq, count */
#define MK_ENT 40u                        /* name[32], nblk, crc32 of the file */

struct bak_st {
    struct upc_ent ent[BAK_MAXE];
    unsigned crc[BAK_MAXE];
    unsigned char made[BAK_MAXE];         /* created in slot B by this run */
    unsigned n;                           /* files */
    unsigned char marker_made;            /* this run created the marker (only then does a rollback remove it: an earlier good backup stays when this run never got to touch it) */
    unsigned char *raw_wa, *wa, *raw_buf, *buf, *raw_blk, *blk;
    unsigned bufn;                        /* chunk size in bytes */
    unsigned mode;                        /* BAK_M_* */
};

static unsigned crc_up(unsigned c, const unsigned char *p, unsigned n)
{
    unsigned i, k;
    c = ~c;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static int seq_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static unsigned char *al32(void *p) { return (unsigned char *)(((unsigned long)p + 31u) & ~31ul); }
static unsigned get32(const unsigned char *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
static void put32(unsigned char *p, unsigned v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

/* undo: remove what this run created (best effort; the marker is never present on a failed run) */
static void rollback(struct bak_st *b)
{
    unsigned i;
    if (b->marker_made) (void)upc_b_delete(BAK_MARKER);
    for (i = 0; i < b->n; i++) if (b->made[i]) (void)upc_b_delete(b->ent[i].name);
}

static int in_marker(const unsigned char *blk, const char *name)       /* the (valid) old marker in blk lists this name */
{
    unsigned i, k, cnt = get32(blk + MK_OFF + 12);
    for (i = 0; i < cnt; i++) {
        const unsigned char *e = blk + MK_OFF + MK_HDR + i * MK_ENT;
        for (k = 0; k < 32 && e[k] == (unsigned char)name[k] && name[k]; k++) { }
        if (k < 32 && e[k] == 0 && name[k] == 0) return 1;
    }
    return 0;
}

static int copy_file(struct bak_st *b, unsigned i)
{
    const struct ota_card_ops *ops = upc_ops();
    struct upc_ent *e = &b->ent[i];
    unsigned off = 0, total = e->nblk * BLK, n, c0, c1, run = 0;
    int r = upc_b_create(e->name, e->nblk, e);
    if (r == 1) return SGU_BAK_SMALL;
    if (r == 2) return SGU_BAK_INWAY;
    if (r == 3) b->made[i] = 1;                       /* the file may exist without its card status: the roll-back removes it */
    if (r != 0) return SGU_BAK_FAIL;
    b->made[i] = 1;
    while (off < total) {
        n = total - off < b->bufn ? total - off : b->bufn;
        if (ops->read(ops->ctx, e->name, off, b->buf, n) != 0) return SGU_BAK_FAIL;       /* slot A: read only */
        c0 = crc_up(0, b->buf, n);
        run = crc_up(run, b->buf, n);
        if (upc_b_write(e->name, off, b->buf, n) != 0) return SGU_BAK_FAIL;
        if (upc_b_read(e->name, off, b->buf, n) != 0) return SGU_BAK_FAIL;
        c1 = crc_up(0, b->buf, n);
        if (c0 != c1) return SGU_BAK_FAIL;                                                 /* the card did not keep it (one bit is enough) */
        off += n;
    }
    b->crc[i] = run;
    return SGU_BAK_DONE;
}

/* The marker is written twice: first (state 0, "incomplete") listing every file this run is about to create, before the first copy, so that a run cut short at any moment leaves a
   marker that names exactly what may be in the way; last (state 1, "complete") with the CRC-32 of every copied file.  Only state 1 is a backup the restore tools may rely on. */
static int write_marker(struct bak_st *b, unsigned seq, int final)
{
    static const char c1[] = "Multishine backup", c2u[] = "Made before an update", c2c[] = "Copy of the card";
    const char *c2 = b->mode == BAK_M_COPY ? c2c : c2u;
    struct upc_ent m;
    unsigned i, k, c0;
    unsigned char *p = b->blk;
    for (i = 0; i < BLK; i++) p[i] = 0;
    for (k = 0; c1[k]; k++) p[k] = (unsigned char)c1[k];
    for (k = 0; c2[k]; k++) p[32 + k] = (unsigned char)c2[k];
    put32(p + MK_OFF, MK_MAGIC); put32(p + MK_OFF + 4, final ? 1u : 0u); put32(p + MK_OFF + 8, seq); put32(p + MK_OFF + 12, b->n);
    for (i = 0; i < b->n; i++) {
        unsigned char *e = p + MK_OFF + MK_HDR + i * MK_ENT;
        for (k = 0; k < 32 && b->ent[i].name[k]; k++) e[k] = (unsigned char)b->ent[i].name[k];
        put32(e + 32, b->ent[i].nblk); put32(e + 36, final ? b->crc[i] : 0u);
    }
    c0 = crc_up(0, p, BLK);
    if (!final) {
        int r;
        m.name[0] = 0; m.nblk = 1; m.banner = 0; m.icon_addr = 0xFFFFFFFFu; m.icon_fmt = 0; m.icon_speed = 0; m.comment_addr = 0;
        r = upc_b_create(BAK_MARKER, 1, &m);
        if (r == 1) return SGU_BAK_SMALL;
        if (r == 3) b->marker_made = 1;                   /* possibly created without its status: it must not stay as a marker */
        if (r != 0) return SGU_BAK_FAIL;
        b->marker_made = 1;
    }
    if (upc_b_write(BAK_MARKER, 0, p, BLK) != 0) return SGU_BAK_FAIL;
    if (upc_b_read(BAK_MARKER, 0, p, BLK) != 0) return SGU_BAK_FAIL;
    if (crc_up(0, p, BLK) != c0) return SGU_BAK_FAIL;
    return SGU_BAK_DONE;
}

static int run(const struct bak_cfg *cfg, struct bak_st *b, unsigned total)
{
    unsigned need = 1, i, k, reclaim = 0, rfiles = 0;
    int idx, r, have_old = 0, fb = 0, ff = 0;
    /* 1. (bak_run probed slot B already: a usable card is there, `total` blocks) 2. what slot A holds */
    b->n = 0;
    for (idx = 0; idx < UPC_MAXFILES; idx++) {
        r = upc_a_entry(idx, &b->ent[b->n]);
        if (r == 3) return SGU_BAK_FAIL;
        if (r == 1) continue;
        if (b->ent[b->n].nblk == 0 || seq_eq(b->ent[b->n].name, BAK_MARKER)) continue;     /* an earlier backup's marker is not part of a backup */
        if (b->mode == BAK_M_COPY && (seq_eq(b->ent[b->n].name, BAK_NAME_ONLINE) || seq_eq(b->ent[b->n].name, BAK_NAME_IDENT))) continue;     /* a second card never gets the logins */
        need += b->ent[b->n].nblk;
        if (++b->n >= BAK_MAXE) return SGU_BAK_SMALL;                                      /* more files than a backup is built for: do not make half of one */
    }
    if (b->n == 0) return SGU_BAK_FAIL;
    if (need > total) return SGU_BAK_SMALL;
    /* 3. memory: the CARD work area of slot B, the chunk buffer (4 blocks, or 1), the marker block */
    b->raw_wa = (unsigned char *)cfg->alloc(UPC_B_WA + 32u);
    if (!b->raw_wa) return SGU_BAK_MEM;
    b->wa = al32(b->raw_wa);
    b->raw_blk = (unsigned char *)cfg->alloc(BLK + 32u);
    if (!b->raw_blk) return SGU_BAK_MEM;
    b->blk = al32(b->raw_blk);
    b->bufn = 4u * BLK;
    b->raw_buf = (unsigned char *)cfg->alloc(b->bufn + 32u);
    if (!b->raw_buf) { b->bufn = BLK; b->raw_buf = (unsigned char *)cfg->alloc(b->bufn + 32u); }
    if (!b->raw_buf) return SGU_BAK_MEM;
    b->buf = al32(b->raw_buf);
    /* 4. mount B (never formats) */
    if (upc_b_mount(b->wa) != 0) return SGU_BAK_BADCARD;
    /* 5. an earlier backup of ours in B (its marker, complete or not) may be replaced; a file of another origin with one of our names may not */
    if (upc_b_file_blocks(BAK_MARKER)) {
        if (upc_b_read(BAK_MARKER, 0, b->blk, BLK) != 0) return SGU_BAK_INWAY;
        if (get32(b->blk + MK_OFF) == 0xFFFFFFFFu && get32(b->blk + MK_OFF + 12) == 0xFFFFFFFFu) { put32(b->blk + MK_OFF, MK_MAGIC); put32(b->blk + MK_OFF + 12, 0); }     /* created, then cut before its first write: it lists nothing, and no copy was begun */
        if (get32(b->blk + MK_OFF) != MK_MAGIC || get32(b->blk + MK_OFF + 12) > BAK_MAXE) return SGU_BAK_INWAY;
        have_old = 1; reclaim = 1; rfiles = 1;
    }
    for (i = 0; i < b->n; i++) {
        unsigned bl = upc_b_file_blocks(b->ent[i].name);
        if (!bl) continue;
        if (!have_old || !in_marker(b->blk, b->ent[i].name)) return SGU_BAK_INWAY;
        reclaim += bl; rfiles++;
    }
    if (have_old) {                                                 /* old-listed files that A no longer has go too (a stale copy of a transient file): counted, then removed */
        unsigned cnt = get32(b->blk + MK_OFF + 12);
        for (i = 0; i < cnt; i++) {
            char nm[33];
            const unsigned char *e = b->blk + MK_OFF + MK_HDR + i * MK_ENT;
            int dup = 0;
            for (k = 0; k < 32; k++) nm[k] = (char)e[k];
            nm[32] = 0;
            for (k = 0; k < b->n; k++) if (seq_eq(nm, b->ent[k].name)) dup = 1;
            if (!dup && upc_b_file_blocks(nm)) { reclaim += upc_b_file_blocks(nm); rfiles++; }
        }
    }
    if (upc_b_free(&fb, &ff) != 0) return SGU_BAK_FAIL;
    if ((unsigned)fb + reclaim < need || (unsigned)ff + rfiles < b->n + 1u) return SGU_BAK_SMALL;
    /* 6. take the old backup away: marker first, so a cut here leaves "no backup", never a marker over a half copy */
    if (have_old) {
        unsigned cnt = get32(b->blk + MK_OFF + 12);
        char nm[33];
        if (upc_b_delete(BAK_MARKER) != 0) return SGU_BAK_FAIL;
        for (i = 0; i < cnt; i++) {
            const unsigned char *e = b->blk + MK_OFF + MK_HDR + i * MK_ENT;
            for (k = 0; k < 32; k++) nm[k] = (char)e[k];
            nm[32] = 0;
            if (upc_b_delete(nm) != 0) return SGU_BAK_FAIL;
        }
    }
    /* 7. the marker (incomplete), the copies, the marker again (complete) */
    r = write_marker(b, cfg->seq, 0);
    if (r != SGU_BAK_DONE) return r;
    for (i = 0; i < b->n; i++) { r = copy_file(b, i); if (r != SGU_BAK_DONE) return r; }
    return write_marker(b, cfg->seq, 1);
}

int bak_run(const struct bak_cfg *cfg)
{
    struct bak_st *b;
    int r;
    unsigned total = 0;
    if (!cfg || !cfg->alloc) return SGU_BAK_MEM;
    r = upc_b_probe(&total);                         /* is there a usable card in B?  Nothing is mounted or allocated before the answer is yes */
    if (r == 1) return SGU_BAK_NOCARD;
    if (r == 4) return SGU_BAK_BUSY;
    if (r != 0) return SGU_BAK_BADCARD;
    b = (struct bak_st *)cfg->alloc((unsigned)sizeof *b);
    if (!b) return SGU_BAK_MEM;
    { unsigned char *z = (unsigned char *)b; unsigned i; for (i = 0; i < (unsigned)sizeof *b; i++) z[i] = 0; }
    b->mode = cfg->mode;
    r = run(cfg, b, total);
    if (r != SGU_BAK_DONE) rollback(b);
    upc_b_done();
    upc_done();                                     /* slot A: unmount and give the shared work area back; the installer mounts it again */
    if (cfg->free) {
        if (b->raw_buf) cfg->free(b->raw_buf);
        if (b->raw_blk) cfg->free(b->raw_blk);
        if (b->raw_wa) cfg->free(b->raw_wa);
        cfg->free(b);
    }
    return r;
}
