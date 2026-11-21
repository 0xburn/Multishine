/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgp: the automatic set-up of the "Multishine system files" (the card file MultishinePack) from the Update machine.
 * The signed container (MSCF ABI 4, tools/card/pack_data.py) is in s->buf, verified.  Its payload holds Slippi's public GPL files unmodified (the four *.usd.diff deltas, slpCSS.dat) and our
 * pack header block; the four retail menu files come from the player's own disc.  The build is the system-files builder's (offline/seed/seed_slp.c: byte-identical to tools/card/build_pack.py), sliced
 * over frames; the card write is the system-files builder's cut-safe one (seed_plan.c sp_apply: staging name MultishinePackN, read back, delete the old name, one rename).  Pure C89, no libc. */
#include <stddef.h>
#include "sgu_core.h"
#include "../../../seed/seed_lean.h"

#define SGP_NIDS 12u
static const char *const SGP_PATH[4] = { "/MnExtAll.usd", "/MnMaAll.usd", "/SdMenu.usd", "/SdSlChr.usd" };
#define SGP_PATH_SLMAP "/MnSlMap.usd"          /* slp_io id 10: the optional 6th entry's retail file (its Slippi diff is container id 11) */
static const char *sgp_path(int id) { return id < 4 ? SGP_PATH[id] : id == 10 ? SGP_PATH_SLMAP : 0; }

struct sgp {
    struct slpl sl;
    struct slpl_io io;
    struct sp_ctx x;
    const struct sgu_host *h;
    const mw2_u8 *pl;                   /* the payload inside the staging buffer */
    unsigned off[SGP_NIDS], len[SGP_NIDS];      /* by slp_io id 4..9 and 11 (0 = absent) */
    unsigned char built;
};

static unsigned be32(const mw2_u8 *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }

/* ---- the slp_io of the build ---- */
static const slp_u8 *io_map(void *ctx, int id)                  /* ids 4..9 and 11 are in the staging buffer already: no copy on the heap */
{
    struct sgp *p = (struct sgp *)ctx;
    return ((id >= 4 && id < 10) || id == 11) && p->len[id] ? p->pl + p->off[id] : 0;
}

static int io_size(void *ctx, int id)
{
    struct sgp *p = (struct sgp *)ctx;
    if ((id >= 0 && id < 4) || id == 10) return p->h->disc && p->h->disc->size ? p->h->disc->size(sgp_path(id)) : -1;
    if (((id >= 4 && id < 10) || id == 11) && p->len[id]) return (int)p->len[id];
    return -1;
}

static int io_read(void *ctx, int id, slp_u8 *dst, slp_u32 off, slp_u32 len)
{
    struct sgp *p = (struct sgp *)ctx;
    slp_u32 n, i, rl;
    if ((((size_t)dst) & 31u) || (off & 31u)) return -1;
    if ((id >= 0 && id < 4) || id == 10) return p->h->disc && p->h->disc->read ? p->h->disc->read(sgp_path(id), dst, off, len) : -1;
    if (id < 4 || id >= (int)SGP_NIDS || id == 10 || !p->len[id] || off > p->len[id]) return -1;
    n = p->len[id] - off;
    if (n > len) n = len;
    for (i = 0; i < n; i++) dst[i] = p->pl[p->off[id] + off + i];
    rl = (len + 31u) & ~31u;
    for (; i < rl; i++) dst[i] = 0;
    return 0;
}

static void *io_alloc(void *ctx, slp_u32 n)                  /* 32-byte aligned; the block's start sits just before it */
{
    struct sgp *p = (struct sgp *)ctx;
    unsigned char *raw = (unsigned char *)p->h->alloc(n + 64u);
    size_t a;
    if (!raw) return 0;
    a = ((size_t)raw + 32u + 31u) & ~(size_t)31u;
    ((void **)a)[-1] = raw;
    return (void *)a;
}

static void io_release(void *ctx, void *q)
{
    struct sgp *p = (struct sgp *)ctx;
    p->h->free(((void **)q)[-1]);
}

void sgp_free(struct sgu *s)
{
    struct sgp *p = s->pk;
    if (!p) return;
    slpl_free(&p->sl);
    s->pk = 0;
    if (s->h && s->h->free) s->h->free(p);
}

/* parse the container payload (the rules of pack_data.py parse_payload) */
static int parse(struct sgp *p, const mw2_u8 *pl, unsigned plen)
{
    unsigned n, total, k, seen = 0;
    if (plen < 32u || pl[0] != 'M' || pl[1] != 'S' || pl[2] != 'P' || pl[3] != 'D' || pl[4] != 1 || (pl[5] != 6 && pl[5] != 7) || pl[6] || pl[7]) return -1;       /* 7 = with the optional MnSlMap.usd.diff (id 11) */
    n = pl[5]; total = be32(pl + 8);
    if (total != plen || (total & 31u)) return -1;
    if (slp_crc32(0, pl + 16, 16u * n) != be32(pl + 12)) return -1;
    for (k = 0; k < n; k++) {
        const mw2_u8 *e = pl + 16 + 16u * k;
        unsigned id = e[0], off = be32(e + 4), ln = be32(e + 8);
        if ((id < 4u || (id > 9u && id != 11u)) || (seen & (1u << id)) || (off & 31u) || off < 16u + 16u * n || !ln || off > total || ln > total - off) return -1;
        if (slp_crc32(0, pl + off, ln) != be32(e + 12)) return -1;
        seen |= 1u << id; p->off[id] = off; p->len[id] = ln;
    }
    return seen == (n == 7u ? 0xBF0u : 0x3F0u) ? 0 : -1;
}

int sgp_begin(struct sgu *s)
{
    const struct sgu_host *h = s->h;
    struct sgp *p;
    unsigned ilen, i;
    if (!h->seed || !h->disc || !h->disc->size || !h->disc->read) return SGU_R_NODISC;
    ilen = be32(s->buf + 0x1000 + 0x18);
    p = (struct sgp *)h->alloc((unsigned)sizeof *p);
    if (!p) return SGU_R_MEMORY;
    {   unsigned char *b = (unsigned char *)p; for (i = 0; i < (unsigned)sizeof *p; i++) b[i] = 0; }
    p->h = h; p->pl = s->buf + OTA_BLOCK;
    s->pk = p;
    if (parse(p, p->pl, (ilen + 31u) & ~31u) != 0) { sgp_free(s); return SGU_R_BADFILE; }
    p->io.io.ctx = p; p->io.io.size = io_size; p->io.io.read = io_read; p->io.io.alloc = io_alloc; p->io.io.release = io_release; p->io.map = io_map;
    if (!slpl_disc_has(&p->io.io)) { sgp_free(s); return SGU_R_NODISC; }          /* the disc in the drive is not the retail Melee (a size differs) or cannot be read */
    slpl_init(&p->sl, &p->io);
    p->x.card = h->seed; p->x.pubs = h->pubs; p->x.npub = h->npub; p->x.allow_unsigned = h->allow_unsigned; p->x.scratch = s->scratch;
    return 0;
}

/* 0 running, 1 on the card, else an SGU_R_* */
int sgp_step(struct sgu *s, unsigned *pct, unsigned *code)
{
    struct sgp *p = s->pk;
    int r;
    if (!p) return SGU_R_SYSFILES;
    if (!p->built) {
        r = slpl_step(&p->sl, 200000u);
        *pct = slpl_percent(&p->sl);
        if (r == SLP_RUN) return 0;
        if (r != SLP_DONE) {
            *code = (unsigned)(-r);
            if (r == SLP_E_MEM) {          /* for a bug report: the largest block the heap gives now (in 64 KiB steps) and how much the build had taken */
                unsigned n, big = 0;
                for (n = 2u << 20; n >= (64u << 10); n -= (64u << 10)) { void *t = s->h->alloc(n); if (t) { s->h->free(t); big = n >> 16; break; } }
                *code = 1u | (p->sl.peak >> 16) << 8 | big << 20;           /* 1, peak in 64 KiB units at bit 8, largest free block in 64 KiB units at bit 20 */
            }
            return r == SLP_E_MEM ? SGU_R_MEMORY : (r == SLP_E_INPUT || r == SLP_E_READ) ? SGU_R_NODISC : SGU_R_SYSFILES;
        }
        p->built = 1;
        return 0;                               /* the write is the next frame: the screen shows 100 % first */
    }
    *pct = 100u;
    p->x.phdr = slpl_gci_hdr(&p->sl); p->x.pdata = slpl_gci_data(&p->sl); p->x.pblocks = slpl_gci_blocks(&p->sl);
    if (!s->h->card_ready || !s->h->card_ready()) { *code = 0; return SGU_R_NOCARD; }
    /* MultishinePack is never replaced when it is whole (df_glue.c: derived entries are not OTA-able; a pack of an older format or another builder stays): only a missing file is written, and a damaged one
       (header, table CRC or an entry's stored CRC wrong, seed_plan.c sp_scan_pack) is rebuilt */
    if (p->h->seed->blocks(SP_PACK_NAME) && sp_scan_pack(&p->x)) { if (s->h->card_done) s->h->card_done(); return 1; }
    r = sp_apply(&p->x, SP_PACK, p->h->seed->blocks(SP_PACK_NAME) ? SD_REPAIR : SD_WRITE, 0, 0);
    if (s->h->card_done) s->h->card_done();
    if (r == OTA_OK) return 1;
    *code = (unsigned)r;
    return r == OTA_E_SPACE ? SGU_R_FULL : r == OTA_E_VERIFY ? SGU_R_CARDBITS : SGU_R_WRITE;
}
