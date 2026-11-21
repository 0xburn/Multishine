/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* seed_plan: see seed_plan.h.  Strict C89, no libc, no heap. */
#include "seed_plan.h"
#include "seed_slp.h"

static const char *const NM[2][3] = {
    { "MultishineCode", "MultishineCodeB", "MultishineCodeN" },
    { "MultishineOvl", "MultishineOvlB", "MultishineOvlN" }
};

const char *sp_name(int kind, int which)
{
    if (kind == SP_LOADER) return which == 2 ? SP_LOADER_STAGE : SP_LOADER_NAME;
    if (kind == SP_MELEE) return which == 2 ? SP_MELEE_STAGE : SP_MELEE_NAME;
    if (kind == SP_PACK) return which == 2 ? SP_PACK_STAGE : SP_PACK_NAME;
    return NM[kind == SP_OVL ? 1 : 0][which];
}

const char *sp_act_text(unsigned a)
{
    switch (a) {
    case SD_WRITE: return "write (new)";
    case SD_UPDATE: return "update (older on card)";
    case SD_REPAIR: return "replace (bad file)";
    case SD_KEEP_SAME: return "keep (same build)";
    case SD_KEEP_NEWER: return "keep (newer on card)";
    case SD_HOLD: return "left alone (card is newer)";
    case SD_KEEP_OURS: return "keep (already ours)";
    case SD_FOREIGN: return "NOT touched (unknown file)";
    case SD_REPLACE: return "replace (Melee save)";
    case SD_NOROOM: return "NO ROOM";
    case SD_BLOCKED: return "not written";
    case SD_KEEP_OLD: return "keep (older, updated in game)";
    default: return "-";
    }
}

static int is_write(unsigned a) { return a == SD_WRITE || a == SD_UPDATE || a == SD_REPAIR || a == SD_REPLACE; }
static int sig_present(unsigned a) { return is_write(a) || a == SD_KEEP_SAME || a == SD_KEEP_NEWER || a == SD_KEEP_OLD; }

void sp_decide(const struct sp_in *in, struct sp_out *o)
{
    int k, newer = 0, i;
    unsigned char *a = o->act;
    ota_u32 need = 0, credit = 0;
    int sigs;
    for (i = 0; i < (int)sizeof *o; i++) ((unsigned char *)o)[i] = 0;
    for (k = 0; k < 2; k++) {
        const struct sp_card *c = &in->c[k];
        if (!c->present) a[k] = SD_WRITE;
        else if (!c->valid) a[k] = SD_REPAIR;
        else if (!in->have[k]) a[k] = SD_KEEP_SAME;                       /* nothing to compare with; the pair rule below blocks writes anyway */
        else if (c->seq > in->seq[k]) { a[k] = SD_KEEP_NEWER; newer = 1; }
        else if (c->seq == in->seq[k]) a[k] = SD_KEEP_SAME;
        else a[k] = SD_UPDATE;
    }
    if (in->melee_keep && in->pack_blocks && in->c[0].valid && in->c[1].valid)         /* pack-only run (the card is set up apart from the pack, both signed files valid): an older engine / overlay stays; staging a second copy needs ~90 free blocks the card often lacks, and the game's updater does this job */
        for (k = 0; k < 2; k++) if (a[k] == SD_UPDATE) a[k] = SD_KEEP_OLD;
    if (newer) {                                                          /* a card that is ahead is never moved back: no signed file is written */
        for (k = 0; k < 2; k++) if (is_write(a[k])) a[k] = SD_HOLD;
        o->why = SW_NEWER;
    }
    if (!in->have[0] || !in->have[1]) {                                   /* never half a pair */
        for (k = 0; k < 2; k++) if (is_write(a[k])) a[k] = SD_BLOCKED;
        if (!o->why) o->why = SW_SERVER;
    }
    /* the standard Melee save and the loader save: written after the signed files, and only when those are there afterwards; the loader save last.  Existing saves are replaced. */
    a[SP_MELEE] = a[SP_LOADER] = a[SP_PACK] = SD_NONE;
    sigs = sig_present(a[0]) && sig_present(a[1]);
    if (in->pack_blocks) {                                                 /* the Slippi data pack, built from the disc: a valid pack on the card is kept, a damaged one replaced */
        a[SP_PACK] = !sigs ? SD_BLOCKED : in->pack_have <= 0 ? SD_WRITE : in->pack_valid ? SD_KEEP_OURS : SD_REPAIR;
    }
    if (!in->own) {
        if (in->loader_blocks == 0 || in->melee_blocks == 0) { a[SP_LOADER] = a[SP_MELEE] = SD_BLOCKED; if (!o->why) o->why = SW_NOLOADER; }
        else if (in->loader_state == SL_OTHER) {                           /* a card we cannot place a loader save on is not seeded at all: files without the loader would be inert */
            a[SP_LOADER] = SD_FOREIGN; a[SP_MELEE] = SD_BLOCKED; o->why = SW_FOREIGN;
            for (k = 0; k < 2; k++) if (is_write(a[k])) a[k] = SD_BLOCKED;
            if (is_write(a[SP_PACK])) a[SP_PACK] = SD_BLOCKED;
        } else {
            a[SP_MELEE] = sigs ? (in->melee_have ? (in->melee_keep ? SD_KEEP_OURS : SD_REPLACE) : SD_WRITE) : SD_BLOCKED;
            a[SP_LOADER] = in->loader_state == SL_OURS ? SD_KEEP_OURS : !sigs ? SD_BLOCKED : in->loader_state == SL_MELEE ? SD_REPLACE : SD_WRITE;
        }
    }
    /* room: the card's real size and free blocks decide, before anything is written.  The blocks of the saves that are replaced come back. */
    for (k = 0; k < 2; k++) if (is_write(a[k])) need += in->blocks[k];
    if (is_write(a[SP_MELEE])) { need += in->melee_blocks; if (in->melee_have > 0) credit += (ota_u32)in->melee_have; }
    if (is_write(a[SP_LOADER])) { need += in->loader_blocks; if (in->loader_state == SL_MELEE) credit += in->loader_blocks; }
    if (is_write(a[SP_PACK])) { need += in->pack_blocks; if (in->pack_have > 0) credit += (ota_u32)in->pack_have; }
    o->gross = need;
    need = need > credit ? need - credit : 0;
    o->need = need;
    o->have_free = in->free_blocks;
    o->headroom = in->blocks[0] + in->blocks[1];
    if (in->card_blocks >= 0 && in->card_blocks < SP_MIN_CARD) {
        for (k = 0; k < SP_NFILES; k++) if (is_write(a[k])) a[k] = SD_NOROOM;
        o->why = SW_SMALL; o->verdict = 2;
    } else if (need && in->free_blocks >= 0 && need > (ota_u32)in->free_blocks) {
        for (k = 0; k < SP_NFILES; k++) if (is_write(a[k])) a[k] = SD_NOROOM;
        o->why = SW_NOROOM; o->verdict = 2; o->missing = need - (ota_u32)in->free_blocks;
    } else if (in->free_blocks >= 0 && (ota_u32)in->free_blocks < need + o->headroom) o->verdict = 1;
    for (k = 0; k < SP_NFILES; k++) if (is_write(a[k])) o->changes++;
    /* a successful apply leaves a complete card when no file ended up blocked, held or without room */
    o->complete = 1;
    for (k = 0; k < 2; k++) if (!sig_present(a[k])) o->complete = 0;
    if (in->pack_blocks && a[SP_PACK] != SD_WRITE && a[SP_PACK] != SD_REPAIR && a[SP_PACK] != SD_KEEP_OURS) o->complete = 0;
    if (!in->own && ((a[SP_LOADER] != SD_WRITE && a[SP_LOADER] != SD_REPLACE && a[SP_LOADER] != SD_KEEP_OURS) || (a[SP_MELEE] != SD_WRITE && a[SP_MELEE] != SD_REPLACE && a[SP_MELEE] != SD_KEEP_OURS))) o->complete = 0;
}

void sp_scan_signed(const struct sp_ctx *x, int kind, struct sp_card *out)
{
    const char *nm = sp_name(kind, 0);
    struct ota_policy pol;
    ota_u32 n, seq = 0;
    unsigned nb;
    out->present = out->valid = 0; out->seq = out->blocks = 0;
    nb = x->card->blocks(nm);
    if (nb == 0) return;
    out->present = 1; out->blocks = nb;
    n = nb * OTA_BLOCK;
    if (n > x->cap) return;
    if (x->card->ota->read(x->card->ota->ctx, nm, 0, x->buf, n) != 0) return;
    pol.pubs = x->pubs; pol.npub = x->npub; pol.abi = kind == SP_OVL ? SP_ABI_OVL : SP_ABI_ENG; pol.min_seq = 0; pol.allow_unsigned = x->allow_unsigned;
    if (ota_verify(x->buf, n, &pol, &seq) == OTA_OK) { out->valid = 1; out->seq = seq; }
}

/* ---- is the file under the loader name OUR loader save?  Melee rewrites a few bytes of the loader save at boot (date, hash bytes), and a Melee save made on THIS disc carries our banner and
   icon too, so the art alone does not tell them apart. The HSD sector scramble ( hsd_803B2B20 / fn_803B302C, as tools/melee_save_check.py) is undone byte by byte
   on both sides, and the decoded bytes of the ten data sectors are compared: our save against Melee's rewrite of it differs in about ten bytes, against ANY Melee-made save in about
   12,700 (the exploit payload), so a distance up to SP_DIST_MAX is 'ours'.  Nothing of the game is stored: only the comparison against the disc's own loader file. ---- */
static const unsigned char K13[13] = { 0x26, 0xFF, 0xE8, 0xEF, 0x42, 0xD6, 0x01, 0x54, 0x14, 0xA3, 0x80, 0xFD, 0x6E };
static unsigned f302c(unsigned prev, unsigned c)
{
    switch (prev % 7) {
    case 0: c = ((c & 1) | ((c << 1) & 4) | ((c << 2) & 0x10) | ((c << 3) & 0x40) | ((c >> 3) & 2) | ((c >> 2) & 8) | ((c >> 1) & 0x20) | (c & 0x80)); break;
    case 1: c = (((c << 1) & 2) | ((c << 6) & 0x80) | (c & 4) | ((c >> 3) & 1) | ((c << 1) & 0x20) | ((c >> 1) & 0x10) | ((c >> 3) & 8) | ((c >> 1) & 0x40)); break;
    case 2: c = (((c & 1) << 2) | ((c << 2) & 8) | ((c << 4) & 0x40) | ((c << 1) & 0x10) | ((c << 3) & 0x80) | ((c >> 4) & 2) | ((c >> 6) & 1) | ((c >> 2) & 0x20)); break;
    case 3: c = (((c << 4) & 0x10) | ((c >> 1) & 1) | ((c << 3) & 0x20) | ((c >> 2) & 2) | ((c >> 1) & 8) | ((c << 1) & 0x40) | ((c << 1) & 0x80) | ((c >> 5) & 4)); break;
    case 4: c = (((c << 3) & 8) | ((c << 4) & 0x20) | ((c >> 1) & 2) | ((c << 4) & 0x80) | ((c << 2) & 0x40) | ((c >> 3) & 4) | ((c >> 2) & 0x10) | ((c >> 7) & 1)); break;
    case 5: c = (((c & 1) << 5) | ((c << 5) & 0x40) | ((c & 4) << 5) | (c & 8) | ((c >> 2) & 4) | ((c >> 5) & 1) | ((c >> 5) & 2) | ((c >> 3) & 0x10)); break;
    default: c = (((c << 6) & 0x40) | (c & 2) | ((c >> 2) & 1) | ((c << 2) & 0x20) | (c & 0x10) | ((c << 2) & 0x80) | ((c >> 4) & 4) | ((c >> 4) & 8)); break;
    }
    c &= 0xFFu;
    c ^= K13[prev % 13]; c ^= prev;
    return c & 0xFFu;
}
/* number of decoded bytes of the ten data sectors that differ between the card's file and the disc's loader save; 0xFFFF when the card cannot be read */
static unsigned sp_dist(const struct sp_ctx *x)
{
    const struct ota_card_ops *o = x->card->ota;
    unsigned b, i, n = 0;
    for (b = 1; b <= 10; b++) {
        const mw2_u8 *d = x->ldata + (ota_u32)b * OTA_BLOCK;
        if (o->read(o->ctx, SP_LOADER_NAME, b * OTA_BLOCK, x->scratch, OTA_BLOCK) != 0) return 0xFFFFu;
        for (i = 16; i < OTA_BLOCK; i++) {
            if (f302c(x->scratch[i - 1], x->scratch[i]) != f302c(d[i - 1], d[i])) { if (++n > SP_DIST_MAX) return n; }
        }
    }
    return n;
}

static ota_u32 be32(const mw2_u8 *p) { return ((ota_u32)p[0] << 24) | ((ota_u32)p[1] << 16) | ((ota_u32)p[2] << 8) | p[3]; }
int sp_scan_pack(const struct sp_ctx *x)
{
    const struct ota_card_ops *o = x->card->ota;
    unsigned nb = x->card->blocks(SP_PACK_NAME), cnt, k;
    ota_u32 off[10], len[10], crc[10], total, b;
    if (nb < 2 || !x->scratch) return 0;
    if (o->read(o->ctx, SP_PACK_NAME, 0, x->scratch, OTA_BLOCK) != 0) return 0;
    if (be32(x->scratch + 0x1000) != 0x4D53504Bu || be32(x->scratch + 0x1004) != 1u) return 0;            /* 'MSPK', version 1 */
    cnt = be32(x->scratch + 0x1008);
    total = be32(x->scratch + 0x100C);
    if (cnt < 1 || cnt > 10 || total != nb * OTA_BLOCK) return 0;
    if (slp_crc32(0, x->scratch + 0x1020, cnt * 64u) != be32(x->scratch + 0x1010)) return 0;
    for (k = 0; k < cnt; k++) {
        const mw2_u8 *e = x->scratch + 0x1020 + 64u * k;
        off[k] = be32(e + 40); len[k] = be32(e + 36); crc[k] = be32(e + 60);
        if (off[k] < OTA_BLOCK || (off[k] % OTA_BLOCK) || off[k] > total || len[k] > total - off[k]) return 0;
    }
    for (k = 0; k < cnt; k++) {
        ota_u32 done = 0, c = 0;
        if (crc[k] == 0) continue;                                          /* not recorded */
        while (done < len[k]) {
            ota_u32 n = len[k] - done > OTA_BLOCK ? OTA_BLOCK : len[k] - done;
            b = off[k] + done;
            if (o->read(o->ctx, SP_PACK_NAME, b, x->scratch, OTA_BLOCK) != 0) return 0;
            c = slp_crc32(c, x->scratch, n);
            done += n;
        }
        if (c != crc[k]) return 0;
    }
    return 1;
}

unsigned sp_ready_dbg;
static int ready_core(const struct sp_ctx *x, int want_pack)
{
    const struct ota_card_ops *o = x->card->ota;
    unsigned nb;
    sp_ready_dbg = 1;
    if (!x->card->blocks(sp_name(SP_ENG, 0))) return 0;
    sp_ready_dbg = 2;
    if (!x->card->blocks(sp_name(SP_OVL, 0))) return 0;
    sp_ready_dbg = 8;
    if (want_pack && !x->card->blocks(SP_PACK_NAME)) return 0;     /* the disc carries the Slippi files: a card without the pack is not set up yet (Dojo and the online rows need it) */
    sp_ready_dbg = 3;
    if (x->own) { sp_ready_dbg = 7; return 1; }
    if (x->lblocks == 0 || !x->ldata) return 0;
    nb = x->card->blocks(SP_LOADER_NAME);
    sp_ready_dbg = 0x400u | (nb << 16);
    if (nb != x->lblocks) return 0;
    if (!x->card->blocks(SP_MELEE_NAME)) return 0;           /* the standard Melee save is part of a set-up card (a cut run between it and the loader save re-runs) */
    (void)o;     /* no art compare and no content compare: a working card of an older build (older banner, another loader payload) is ready; Melee itself repairs a header that differs from the disc's art at boot */
    sp_ready_dbg = 7;
    return 1;
}
int sp_ready(const struct sp_ctx *x) { return ready_core(x, x->want_pack != 0); }

int sp_pack_only(const struct sp_ctx *x)
{
    return ready_core(x, 0) && !x->own && sp_scan_loader(x) == SL_OURS;
}

int sp_scan_loader(const struct sp_ctx *x)
{
    const struct ota_card_ops *o = x->card->ota;
    unsigned nb = x->card->blocks(SP_LOADER_NAME);
    unsigned i;
    if (nb == 0) return SL_ABSENT;
    if (nb != x->lblocks || x->lblocks == 0 || !x->ldata) return SL_OTHER;
    if (o->read(o->ctx, SP_LOADER_NAME, 0, x->scratch, OTA_BLOCK) != 0) return SL_OTHER;
    for (i = 0x40; i < 0x1E40; i++) if (x->scratch[i] != x->ldata[i]) return SL_MELEE;        /* banner, icon, palette: Melee's own header compare (any other art is not ours) */
    return sp_dist(x) <= SP_DIST_MAX ? SL_OURS : SL_MELEE;
}

/* One of our save files (the loader save, the standard Melee save): staged under its staging name, read back, then the file already under the final name is deleted (a player's old save, replaced) and
   the staged one takes the name by one rename: the final name is either the old file, absent, or the complete new one, never partial.  When the card has no room for the staged copy next to the old file,
   the old one is deleted first (it is being replaced anyway). */
static int apply_gci(const struct sp_ctx *x, const char *name, const char *stage, const mw2_u8 *hdr, const mw2_u8 *data, unsigned nblk)
{
    const struct ota_card_ops *o = x->card->ota;
    unsigned b;
    ota_u32 i;
    int rc;
    if (nblk == 0 || !hdr || !data) return OTA_E_HDR;
    if (o->del(o->ctx, stage) != 0) return OTA_E_IO;                         /* a stale staging file of an interrupted run */
    rc = x->card->create_gci(stage, nblk, hdr);
    if (rc == 1 && x->card->blocks(name)) {
        if (o->del(o->ctx, name) != 0) return OTA_E_IO;
        rc = x->card->create_gci(stage, nblk, hdr);
    }
    if (rc == 1) return OTA_E_SPACE;
    if (rc != 0) return OTA_E_IO;
    if (o->write(o->ctx, stage, 0, data, nblk * OTA_BLOCK) != 0) { (void)o->del(o->ctx, stage); return OTA_E_IO; }
    for (b = 0; b < nblk; b++) {
        if (o->read(o->ctx, stage, b * OTA_BLOCK, x->scratch, OTA_BLOCK) != 0) { (void)o->del(o->ctx, stage); return OTA_E_IO; }
        for (i = 0; i < OTA_BLOCK; i++)
            if (x->scratch[i] != data[b * OTA_BLOCK + i]) { (void)o->del(o->ctx, stage); return OTA_E_VERIFY; }
    }
    if (x->card->blocks(name) && o->del(o->ctx, name) != 0) { (void)o->del(o->ctx, stage); return OTA_E_IO; }
    if (o->rename(o->ctx, stage, name) != 0) { (void)o->del(o->ctx, stage); return OTA_E_IO; }       /* the name appears only here: the commit step */
    return OTA_OK;
}

int sp_apply(const struct sp_ctx *x, int kind, unsigned act, const mw2_u8 *file, ota_u32 n)
{
    const struct ota_card_ops *o = x->card->ota;
    if (kind == SP_PACK) return (act == SD_WRITE || act == SD_REPAIR) ? apply_gci(x, SP_PACK_NAME, SP_PACK_STAGE, x->phdr, x->pdata, x->pblocks) : OTA_E_HDR;
    if (kind == SP_LOADER) return (act == SD_WRITE || act == SD_REPLACE) ? apply_gci(x, SP_LOADER_NAME, SP_LOADER_STAGE, x->lhdr, x->ldata, x->lblocks) : OTA_E_HDR;
    if (kind == SP_MELEE) return (act == SD_WRITE || act == SD_REPLACE) ? apply_gci(x, SP_MELEE_NAME, SP_MELEE_STAGE, x->mhdr, x->mdata, x->mblocks) : OTA_E_HDR;
    if (!is_write(act)) return OTA_E_HDR;
    if (act == SD_REPAIR && o->del(o->ctx, sp_name(kind, 0)) != 0) return OTA_E_IO;       /* the bad file is dropped, not kept as the B copy */
    if (ota_plan(o, sp_name(kind, 0), sp_name(kind, 1), sp_name(kind, 2), n) != OTA_TIER_STAGED) return OTA_E_SPACE;        /* the system-files builder never installs in place */
    return ota_install_verified(o, sp_name(kind, 0), sp_name(kind, 1), sp_name(kind, 2), file, n, x->scratch);
}

/* ---- ops wrapper ---- */
static const struct seed_ops *W_SRC;
static unsigned total_ours(void)
{
    static const char *const nm[] = { "MultishineCode", "MultishineCodeB", "MultishineCodeN", "MultishineOvl", "MultishineOvlB", "MultishineOvlN", SP_LOADER_NAME, SP_LOADER_STAGE, SP_MELEE_NAME, SP_MELEE_STAGE, SP_PACK_NAME, SP_PACK_STAGE };
    unsigned i, t = 0;
    for (i = 0; i < sizeof nm / sizeof nm[0]; i++) t += W_SRC->blocks(nm[i]);
    return t;
}
static int w_free(void *ctx, const char *previous, const char *staging, const char *active)
{
    int f;
    (void)ctx;
    if (!W_SRC->free_raw) return -1;
    f = W_SRC->free_raw();
    if (f < 0) return -1;
    return f + (int)(previous ? W_SRC->blocks(previous) : 0) + (int)(staging ? W_SRC->blocks(staging) : 0) + (int)(active ? W_SRC->blocks(active) : 0);
}
void sp_wrap(struct seed_ops *dst, struct ota_card_ops *odst, const struct seed_ops *src)
{
    *odst = *src->ota;
    odst->free_blocks = w_free;
    *dst = *src;
    dst->ota = odst;
    W_SRC = src;
}
int sp_other_blocks(const struct seed_ops *c)
{
    int f, r;
    unsigned t;
    if (!c->total || !c->free_raw) return -1;
    t = c->total();
    f = c->free_raw();
    if (!t || f < 0) return -1;
    W_SRC = c;
    r = (int)t - f - (int)total_ours();
    return r >= 0 ? r : 0;
}

int sp_clean_stale(const struct sp_ctx *x)
{
    const struct ota_card_ops *o = x->card->ota;
    int r = 0;
    if (x->card->blocks(sp_name(SP_ENG, 2))) r |= o->del(o->ctx, sp_name(SP_ENG, 2));
    if (x->card->blocks(sp_name(SP_OVL, 2))) r |= o->del(o->ctx, sp_name(SP_OVL, 2));
    if (x->card->blocks(SP_LOADER_STAGE)) r |= o->del(o->ctx, SP_LOADER_STAGE);
    if (x->card->blocks(SP_MELEE_STAGE)) r |= o->del(o->ctx, SP_MELEE_STAGE);
    if (x->card->blocks(SP_PACK_STAGE)) r |= o->del(o->ctx, SP_PACK_STAGE);
    return r;
}
