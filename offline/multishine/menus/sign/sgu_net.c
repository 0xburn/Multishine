/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgu_net: see sgu_net.h.  Strict C89. */
#include "sgu_net.h"

static const char F_ONLINE[] = "MultishineOnline";
static const char F_BRIDGE[] = "MultishineBridge";
static const char F_BRIDGE_NEW[] = "MultishineBridgeNew";      /* staging name of a bridge file the applier creates */
#define REC 0x1000u          /* record start inside the online block */
#define SEQREC 0x1200u
#define NC_OFF 0x1100u       /* SBNC section inside the bridge block */
#define NC_LEN 28u

static unsigned rd32(const mw2_u8 *p) { return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]; }
static void wr32(mw2_u8 *p, unsigned v) { p[0] = (mw2_u8)(v >> 24); p[1] = (mw2_u8)(v >> 16); p[2] = (mw2_u8)(v >> 8); p[3] = (mw2_u8)v; }

static unsigned crc32(const mw2_u8 *p, unsigned n)
{
    unsigned c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static void scrub(mw2_u8 *p, unsigned n) { volatile mw2_u8 *v = p; unsigned i; for (i = 0; i < n; i++) v[i] = 0; }
static int nonzero(const mw2_u8 *p, unsigned n) { unsigned i; for (i = 0; i < n; i++) if (p[i]) return 1; return 0; }

/* same address rules as onr_backup (on_cfgrec.c) */
static int backup_ok(unsigned ip, unsigned prim) { return ip == 0 || ((ip >> 24) != 0 && (ip >> 24) != 127u && (ip >> 24) < 224u && ip != prim); }
/* same address rules as sbr_nc_ip_ok (sbr_netcfg.c, private_only = 0) */
static int nc_ip_ok(unsigned ip) { unsigned a = ip >> 24, b = (ip >> 16) & 255u; return ip != 0 && a != 0 && a != 127u && a < 224u && !(a == 169u && b == 254u); }
/* same rules as on_cfgrec.c host_text_ok / od_host_ok: field = 48 bytes, NUL terminated, padded with zero */
static int host_ok(const mw2_u8 *p, unsigned len)
{
    unsigned i, lab = 0;
    mw2_u8 prev = '.';
    if (len == 0 || len > 47u) return 0;
    for (i = 0; i < len; i++) {
        mw2_u8 c = p[i];
        if (c == '.') { if (prev == '.' || prev == '-' || lab == 0) return 0; lab = 0; }
        else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') { if (c == '-' && prev == '.') return 0; if (++lab > 63u) return 0; }
        else return 0;
        prev = c;
    }
    return prev != '.' && prev != '-';
}

struct fld {
    unsigned have;               /* bit per tag index, see IDX */
    unsigned server, backup, mm, dns;
    unsigned port;
    mw2_u8 mask, hostlen;
    mw2_u8 host[48], k1[32], k2[32];
};
enum { H_SRV = 1, H_PORT = 2, H_HOST = 4, H_K1 = 8, H_K2 = 16, H_BAK = 32, H_MM = 64, H_DNS = 128, H_MASK = 256 };

static int parse(struct fld *f, const mw2_u8 *pl, unsigned n, unsigned seq)
{
    unsigned i, cnt, tot, k, bit;
    mw2_u8 t, l;
    const mw2_u8 *v;
    for (i = 0; i < sizeof *f; i++) ((mw2_u8 *)f)[i] = 0;
    if (n < 16u || n > NGP_MAX_PAYLOAD || rd32(pl) != 0x4D534E50u || pl[4] != 1u) return NGP_E_FORMAT;
    tot = ((unsigned)pl[6] << 8) | pl[7];
    if (tot < 16u || tot > n || rd32(pl + tot - 4u) != crc32(pl, tot - 4u) || rd32(pl + 8) != seq) return NGP_E_FORMAT;
    cnt = pl[5];
    i = 12u;
    for (k = 0; k < cnt; k++) {
        if (i + 2u > tot - 4u) return NGP_E_FORMAT;
        t = pl[i]; l = pl[i + 1]; v = pl + i + 2u;
        if (i + 2u + l > tot - 4u) return NGP_E_FORMAT;
        i += 2u + l;
        switch (t) {
        case 0x01: bit = H_SRV; if (l != 4) return NGP_E_FORMAT; f->server = rd32(v); break;
        case 0x02: bit = H_PORT; if (l != 2) return NGP_E_FORMAT; f->port = ((unsigned)v[0] << 8) | v[1]; break;
        case 0x03: { unsigned j; bit = H_HOST; if (l > 47u) return NGP_E_FORMAT; f->hostlen = l; for (j = 0; j < l; j++) f->host[j] = v[j]; break; }
        case 0x04: { unsigned j; bit = H_K1; if (l != 32) return NGP_E_FORMAT; for (j = 0; j < 32; j++) f->k1[j] = v[j]; break; }
        case 0x05: { unsigned j; bit = H_K2; if (l != 32) return NGP_E_FORMAT; for (j = 0; j < 32; j++) f->k2[j] = v[j]; break; }
        case 0x06: bit = H_BAK; if (l != 4) return NGP_E_FORMAT; f->backup = rd32(v); break;
        case 0x10: bit = H_MM; if (l != 4) return NGP_E_FORMAT; f->mm = rd32(v); break;
        case 0x11: bit = H_DNS; if (l != 4) return NGP_E_FORMAT; f->dns = rd32(v); break;
        case 0x12: bit = H_MASK; if (l != 1) return NGP_E_FORMAT; f->mask = v[0]; break;
        default: return NGP_E_FORMAT;                /* unknown field: the whole patch is refused */
        }
        if (f->have & bit) return NGP_E_FORMAT;     /* a field twice */
        f->have |= bit;
    }
    if (i != tot - 4u || !f->have) return NGP_E_FORMAT;
    /* values */
    if ((f->have & H_SRV) && f->server && ((f->server >> 24) == 0 || (f->server >> 24) == 127u || (f->server >> 24) >= 224u)) return NGP_E_RANGE;
    if ((f->have & H_HOST) && f->hostlen && !host_ok(f->host, f->hostlen)) return NGP_E_RANGE;
    if ((f->have & H_K1) && !nonzero(f->k1, 32)) return NGP_E_RANGE;
    if ((f->have & H_BAK) && !backup_ok(f->backup, f->server)) return NGP_E_RANGE;
    if ((f->have & H_MM) && f->mm && !nc_ip_ok(f->mm)) return NGP_E_RANGE;
    if ((f->have & H_DNS) && f->dns && !nc_ip_ok(f->dns)) return NGP_E_RANGE;
    if ((f->have & H_MASK) && (f->mask & ~5u)) return NGP_E_RANGE;
    return NGP_OK;
}

int ngp_check(const mw2_u8 *pl, unsigned n, unsigned seq) { struct fld f; return parse(&f, pl, n, seq); }

static int read_blk(const struct ota_card_ops *ops, const char *name, mw2_u8 *b)
{
    return ops->read(ops->ctx, name, 0, b, OTA_BLOCK) == 0 ? NGP_OK : NGP_E_IO;
}
/* write, read back into rb, one retry; NGP_OK or NGP_E_IO / NGP_E_VERIFY */
static int write_blk(const struct ota_card_ops *ops, const char *name, const mw2_u8 *b, mw2_u8 *rb)
{
    unsigned tries, i, same;
    for (tries = 0; tries < 2u; tries++) {
        if (ops->write(ops->ctx, name, 0, b, OTA_BLOCK) != 0) return NGP_E_IO;
        if (ops->read(ops->ctx, name, 0, rb, OTA_BLOCK) != 0) return NGP_E_IO;
        same = 1;
        for (i = 0; i < OTA_BLOCK; i++) if (rb[i] != b[i]) { same = 0; break; }
        if (same) return NGP_OK;
    }
    return NGP_E_VERIFY;
}

static int seq_in(const mw2_u8 *blk, unsigned *seq)
{
    const mw2_u8 *p = blk + SEQREC;
    *seq = 0;
    if (rd32(p) == 0x4D534E51u && p[4] == 1u && rd32(p + 12) == crc32(p, 12)) *seq = rd32(p + 8);
    return 0;
}

int ngp_stored_seq(const struct ota_card_ops *ops, mw2_u8 *work, unsigned *seq)
{
    int r;
    *seq = 0;
    if (!ops->exists(ops->ctx, F_ONLINE)) return NGP_OK;
    r = read_blk(ops, F_ONLINE, work);
    if (r == NGP_OK) seq_in(work, seq);
    scrub(work, OTA_BLOCK);                  /* the block holds the device secret: it does not stay in the heap */
    return r;
}

/* the checks onr_check makes that this patch can influence: magic, version, CRC, the reserved bytes, a usable server (address or valid host extension), key 1 */
static int rec_ok(const mw2_u8 *blk)
{
    const mw2_u8 *r = blk + REC;
    const mw2_u8 *x = blk + REC + 0x100u;
    int host = 0;
    if (rd32(x) == 0x5344484Eu && x[4] == 1u && !x[6] && !x[7] && rd32(x + 8) == crc32(x + 0x0C, 48) && x[5] >= 1u && x[5] <= 47u && x[0x0C + x[5]] == 0 && host_ok(x + 0x0C, x[5]) && !nonzero(x + 0x0C + x[5], 48u - x[5])) host = 1;
    if (r[4] != 1u || r[5] || r[6] || r[7] || rd32(r + 8) != crc32(r + 0x0C, 0xC0u - 0x0Cu)) return 0;
    return (rd32(r + 0x0C) != 0 || host) && nonzero(r + 0x20, 32);
}

struct undo { mw2_u8 head[0x14 - 0x08], keys[0x60 - 0x20], bak[4], ext[0x3C], seq[16], nc[NC_LEN]; };
static void cp(mw2_u8 *d, const mw2_u8 *s, unsigned n) { unsigned i; for (i = 0; i < n; i++) d[i] = s[i]; }

static void online_fields(mw2_u8 *blk, const struct fld *f, unsigned seq)
{
    mw2_u8 *r = blk + REC;
    mw2_u8 *x = r + 0x100u;
    unsigned i;
    if (f->have & H_SRV) wr32(r + 0x0C, f->server);
    if (f->have & H_PORT) { unsigned p = f->port == 43150u ? 0u : f->port; r[0x10] = (mw2_u8)(p >> 8); r[0x11] = (mw2_u8)p; }
    if (f->have & H_K1) cp(r + 0x20, f->k1, 32);
    if (f->have & H_K2) cp(r + 0x40, f->k2, 32);
    if (f->have & H_BAK) wr32(r + 0xA8, f->backup);
    if (f->have & H_HOST) {
        for (i = 0; i < 0x3C; i++) x[i] = 0;
        if (f->hostlen) {
            wr32(x, 0x5344484Eu); x[4] = 1; x[5] = f->hostlen;
            cp(x + 0x0C, f->host, f->hostlen);
            wr32(x + 8, crc32(x + 0x0C, 48));
        }
    }
    wr32(r + 8, crc32(r + 0x0C, 0xC0u - 0x0Cu));
    x = blk + SEQREC;
    for (i = 0; i < 16; i++) x[i] = 0;
    wr32(x, 0x4D534E51u); x[4] = 1; wr32(x + 8, seq); wr32(x + 12, crc32(x, 12));
}

static void save_undo(struct undo *u, const mw2_u8 *blk)
{
    const mw2_u8 *r = blk + REC;
    cp(u->head, r + 0x08, sizeof u->head); cp(u->keys, r + 0x20, sizeof u->keys); cp(u->bak, r + 0xA8, 4); cp(u->ext, r + 0x100, 0x3C); cp(u->seq, blk + SEQREC, 16);
}
static void apply_undo(mw2_u8 *blk, const struct undo *u)
{
    mw2_u8 *r = blk + REC;
    cp(r + 0x08, u->head, sizeof u->head); cp(r + 0x20, u->keys, sizeof u->keys); cp(r + 0xA8, u->bak, 4); cp(r + 0x100, u->ext, 0x3C); cp(blk + SEQREC, u->seq, 16);
}

/* bridge block: 1 = changed (write it), 0 = nothing to change, negative = error code */
static int bridge_fields(mw2_u8 *blk, const struct fld *f)
{
    mw2_u8 *s = blk + NC_OFF;
    unsigned mm = 0, dns = 0, mask = 0, i;
    int present = (s[0] == 'S' && s[1] == 'B' && s[2] == 'N' && s[3] == 'C');
    mw2_u8 old[NC_LEN];
    cp(old, s, NC_LEN);
    if (present) {
        if (s[4] > 1u) return -NGP_E_BADREC;                              /* a newer section is never guessed at */
        if (s[4] == 1u && s[6] == 0 && s[7] == NC_LEN && rd32(s + 24) == crc32(s, 24)) { mask = s[5]; mm = rd32(s + 8); dns = rd32(s + 16); }
        else { for (i = 0; i < NC_LEN; i++) s[i] = 0; }                  /* damaged: rebuilt from scratch */
    }
    if (f->have & H_MM) { if (f->mm) { mm = f->mm; mask |= 1u; } else { mm = 0; mask &= ~1u; } }
    if (f->have & H_DNS) { if (f->dns) { dns = f->dns; mask |= 4u; } else { dns = 0; mask &= ~4u; } }
    if (f->have & H_MASK) { mask = (mask & ~5u) | f->mask; if (!(f->mask & 1u)) mm = 0; if (!(f->mask & 4u)) dns = 0; }
    if (((mask & 1u) && !nc_ip_ok(mm)) || ((mask & 4u) && !nc_ip_ok(dns))) return -NGP_E_RANGE;
    if (!mask) { for (i = 0; i < NC_LEN; i++) s[i] = 0; }
    else {
        if (s[0] != 'S') { for (i = 0; i < NC_LEN; i++) s[i] = 0; }
        s[0] = 'S'; s[1] = 'B'; s[2] = 'N'; s[3] = 'C'; s[4] = 1; s[5] = (mw2_u8)mask; s[6] = 0; s[7] = NC_LEN;
        wr32(s + 8, mm); wr32(s + 16, dns); wr32(s + 24, crc32(s, 24));
    }
    for (i = 0; i < NC_LEN; i++) if (old[i] != s[i]) return 1;
    return 0;
}

/* create the bridge file from the prepared block: delete a stale staging file, create (1 block; ops->create returns 1 when the card is full), write + read back, rename.  Before the
   rename the real name does not exist, so a power cut leaves "no bridge file" (plus a harmless staging file the next run deletes); after it the whole file is there. */
static int create_bridge(const struct ota_card_ops *ops, const mw2_u8 *b, mw2_u8 *rb)
{
    int rc, r;
    unsigned i;
    (void)ops->del(ops->ctx, F_BRIDGE_NEW);
    rc = ops->create(ops->ctx, F_BRIDGE_NEW, 1u);
    if (rc != 0) return rc == 1 ? NGP_E_NOROOM : NGP_E_IO;
    r = write_blk(ops, F_BRIDGE_NEW, b, rb);
    if (r != NGP_OK) { (void)ops->del(ops->ctx, F_BRIDGE_NEW); return r; }
    if (ops->rename(ops->ctx, F_BRIDGE_NEW, F_BRIDGE) != 0) { (void)ops->del(ops->ctx, F_BRIDGE_NEW); return NGP_E_IO; }
    if (ops->read(ops->ctx, F_BRIDGE, 0, rb, OTA_BLOCK) != 0) return NGP_E_IO;
    for (i = 0; i < OTA_BLOCK; i++) if (rb[i] != b[i]) { (void)ops->del(ops->ctx, F_BRIDGE); return NGP_E_VERIFY; }
    return NGP_OK;
}

int ngp_apply(const struct ota_card_ops *ops, const mw2_u8 *pl, unsigned n, unsigned seq, mw2_u8 *work)
{
    struct fld f;
    struct undo u;
    mw2_u8 *ob = work, *bb = work + OTA_BLOCK, *rb = work + 2u * OTA_BLOCK;
    mw2_u8 oldnc[NC_LEN];
    int r, br = 0, i, created = 0;
    unsigned stored;
    r = parse(&f, pl, n, seq);
    if (r != NGP_OK) return r;
    if (!ops->exists(ops->ctx, F_ONLINE)) return NGP_E_NOREC;
    r = read_blk(ops, F_ONLINE, ob);
    if (r != NGP_OK) goto out;
    if (!rec_ok(ob)) { r = NGP_E_BADREC; goto out; }                       /* damaged, unknown (newer) version or incomplete: nothing is changed */
    seq_in(ob, &stored);
    if (seq <= stored) { r = NGP_E_SEQ; goto out; }
    save_undo(&u, ob);
    online_fields(ob, &f, seq);
    if (!rec_ok(ob)) { r = NGP_E_RANGE; goto out; }                        /* the result must pass the reader's checks BEFORE anything is written */
    if (f.have & (H_MM | H_DNS | H_MASK)) {
        if (!ops->exists(ops->ctx, F_BRIDGE)) {
            /* no bridge file (our cube): an otherwise empty record, laid out like vmc_builder set-net / the console-created file (comment strings, zeros elsewhere), plus the section */
            for (i = 0; i < (int)OTA_BLOCK; i++) bb[i] = 0;
            cp(bb, (const mw2_u8 *)"Multishine", 10); cp(bb + 32, (const mw2_u8 *)"Slippi bridge status", 20);
            cp(oldnc, bb + NC_OFF, NC_LEN);
            br = bridge_fields(bb, &f);
            if (br < 0) { r = -br; goto out; }
            if (br > 0) created = 1;                                       /* only clearing: nothing to clear, no file is made */
        } else {
            r = read_blk(ops, F_BRIDGE, bb);
            if (r != NGP_OK) goto out;
            cp(oldnc, bb + NC_OFF, NC_LEN);
            br = bridge_fields(bb, &f);
            if (br < 0) { r = -br; goto out; }
        }
    }
    if (created) {                                                         /* one whole file or none: staging name, write + read back, rename */
        r = create_bridge(ops, bb, rb);
        if (r != NGP_OK) goto out;
    } else if (br > 0) {                                                   /* bridge first: if the online write then fails, the bridge section is put back */
        r = write_blk(ops, F_BRIDGE, bb, rb);
        if (r != NGP_OK) { if (r == NGP_E_VERIFY) { cp(bb + NC_OFF, oldnc, NC_LEN); (void)write_blk(ops, F_BRIDGE, bb, rb); } goto out; }
    }
    r = write_blk(ops, F_ONLINE, ob, rb);
    if (r != NGP_OK) {
        if (created) (void)ops->del(ops->ctx, F_BRIDGE);                   /* we made it: take it away again (a half-applied patch must not leave a new file behind) */
        else if (br > 0) { cp(bb + NC_OFF, oldnc, NC_LEN); (void)write_blk(ops, F_BRIDGE, bb, rb); }
        if (r == NGP_E_VERIFY && read_blk(ops, F_ONLINE, ob) == NGP_OK) { apply_undo(ob, &u); (void)write_blk(ops, F_ONLINE, ob, rb); }     /* put the fields back; the secret never left the block */
    }
out:
    for (i = 0; i < (int)sizeof u; i++) ((volatile mw2_u8 *)&u)[i] = 0;
    scrub(work, NGP_WORK);
    return r;
}
