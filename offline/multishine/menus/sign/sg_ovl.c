/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Resident part of the sign-in overlay loader: see sg_ovl.h.  Strict C89, no libc.  Memory: one block of image + max(bss, relocs) bytes; the reloc table
   is read into the (still unused) bss area, applied, and the area is zeroed: nothing else is ever allocated, so the cost while inside is that block. */
#include "sg_ovl.h"

static sgo_u32 rd32(const sgo_u8 *p) { return ((sgo_u32)p[0] << 24) | ((sgo_u32)p[1] << 16) | ((sgo_u32)p[2] << 8) | p[3]; }
static void wr32(sgo_u8 *p, sgo_u32 v) { p[0] = (sgo_u8)(v >> 24); p[1] = (sgo_u8)(v >> 16); p[2] = (sgo_u8)(v >> 8); p[3] = (sgo_u8)v; }
static void wr16(sgo_u8 *p, sgo_u32 v) { p[0] = (sgo_u8)(v >> 8); p[1] = (sgo_u8)v; }
static sgo_u32 up(sgo_u32 v) { return (v + SGO_ALIGN - 1u) & ~(SGO_ALIGN - 1u); }

int sgo_check(const struct sgo_hdr *h, sgo_u32 file_len)
{
    if (file_len < sizeof *h || h->magic != SGO_MAGIC || h->ver != SGO_VER) return SGO_E_FORMAT;
    if (h->image_size > 0x100000u || h->bss_size > 0x100000u || h->nreloc > 0x10000u || (h->image_size % SGO_ALIGN) || (h->api_off & 3u)) return SGO_E_FORMAT;
    if (h->api_off + sizeof(struct sgo_api) > h->image_size) return SGO_E_FORMAT;
    if (SGO_ALIGN + h->image_size + up(h->nreloc * SGO_RELOC_BYTES) != file_len) return SGO_E_FORMAT;     /* the header takes one 32-byte unit */
    return SGO_E_NONE;
}

void sgo_relocate(sgo_u8 *img, const sgo_u8 *rel, sgo_u32 n, sgo_u32 load, sgo_u32 link_base)
{
    sgo_u32 i;
    for (i = 0; i < n; i++) {
        sgo_u32 ot = rd32(rel + i * SGO_RELOC_BYTES), off = ot >> 2, t = ot & 3u, tgt = rd32(rel + i * SGO_RELOC_BYTES + 4) + load;
        if (t == SGO_R_ADDR32) wr32(img + off, rd32(img + off) + (load - link_base));
        else if (t == SGO_R_HA) wr16(img + off, ((tgt + 0x8000u) >> 16) & 0xFFFFu);
        else wr16(img + off, tgt & 0xFFFFu);
    }
}

static int fail(struct sgo_loader *l, int err)
{
    if (l->hbuf) l->sdk->free(l->hbuf);
    l->hbuf = 0;
    if (l->buf) l->sdk->free(l->buf);
    l->sdk->close();
    l->buf = 0; l->state = SGO_S_FAILED; l->err = err;
    return err;
}

int sgo_begin(struct sgo_loader *l, const struct sgo_sdk *sdk, const char *path)
{
    sgo_u8 *h;
    l->sdk = sdk; l->state = SGO_S_IDLE; l->err = SGO_E_NONE; l->buf = 0; l->hbuf = 0; l->api = 0; l->file_len = 0;
    if (sdk->open(path, &l->file_len) < 0) { l->state = SGO_S_FAILED; return l->err = SGO_E_NOFILE; }
    if (l->file_len < SGO_ALIGN || (l->file_len % SGO_ALIGN)) return fail(l, SGO_E_FORMAT);
    l->hbuf = (sgo_u8 *)sdk->alloc(2u * SGO_ALIGN);
    if (!l->hbuf) return fail(l, SGO_E_NOMEM);
    h = SGO_A2P(up(SGO_P2A(l->hbuf)));
    if (sdk->read_begin(h, SGO_ALIGN, 0) < 0) return fail(l, SGO_E_READ);
    l->state = SGO_S_HDR;
    return SGO_E_NONE;
}

int sgo_step(struct sgo_loader *l)
{
    const struct sgo_sdk *sdk = l->sdk;
    int rc;
    if (l->state < SGO_S_HDR || l->state > SGO_S_REL) return l->state;
    rc = sdk->read_poll();
    if (rc == 0) return l->state;
    if (rc < 0) { fail(l, SGO_E_READ); return l->state; }
    if (l->state == SGO_S_HDR) {
        const sgo_u8 *h = SGO_A2P(up(SGO_P2A(l->hbuf)));
        sgo_u32 area;
        l->hdr.magic = rd32(h); l->hdr.ver = rd32(h + 4); l->hdr.image_size = rd32(h + 8); l->hdr.bss_size = rd32(h + 12);
        l->hdr.nreloc = rd32(h + 16); l->hdr.api_off = rd32(h + 20); l->hdr.link_base = rd32(h + 24); l->hdr.state_hint = rd32(h + 28);
        sdk->free(l->hbuf); l->hbuf = 0;
        if (sgo_check(&l->hdr, l->file_len) != SGO_E_NONE) { fail(l, SGO_E_FORMAT); return l->state; }
        l->rel_len = up(l->hdr.nreloc * SGO_RELOC_BYTES);
        area = up(l->hdr.bss_size);
        if (area < l->rel_len) area = l->rel_len;
        l->buf_len = l->hdr.image_size + area + SGO_ALIGN;           /* + 32: the allocator's address may need aligning up */
        l->buf = (sgo_u8 *)sdk->alloc(l->buf_len);
        if (!l->buf) { fail(l, SGO_E_NOMEM); return l->state; }
        l->load_addr = up(SGO_P2A(l->buf));
        if (sdk->read_begin(SGO_A2P(l->load_addr), l->hdr.image_size, SGO_ALIGN) < 0) { fail(l, SGO_E_READ); return l->state; }
        l->state = SGO_S_IMG;
    } else if (l->state == SGO_S_IMG) {
        if (l->rel_len == 0) goto finish;
        if (sdk->read_begin(SGO_A2P(l->load_addr + l->hdr.image_size), l->rel_len, SGO_ALIGN + l->hdr.image_size) < 0) { fail(l, SGO_E_READ); return l->state; }
        l->state = SGO_S_REL;
    } else {
    finish:
        {
            sgo_u8 *img = SGO_A2P(l->load_addr), *area = img + l->hdr.image_size;
            sgo_u32 i, clr = l->buf_len - l->hdr.image_size - (l->load_addr - SGO_P2A(l->buf));
            sdk->close();
            sgo_relocate(img, area, l->hdr.nreloc, l->load_addr, l->hdr.link_base);
            for (i = 0; i < clr; i++) area[i] = 0;                 /* the reloc table is gone, bss is zero */
            sdk->flush(img, l->hdr.image_size);
            l->image_size = l->hdr.image_size; l->bss_size = l->hdr.bss_size;
            l->peak_bytes = l->buf_len;
            l->api = (const struct sgo_api *)(img + l->hdr.api_off);   /* on the host the test reads the table by offset instead */
            l->state = SGO_S_READY;
        }
    }
    return l->state;
}

void sgo_release(struct sgo_loader *l)
{
    if (l->sdk && (l->state == SGO_S_HDR || l->state == SGO_S_IMG || l->state == SGO_S_REL)) {      /* released mid-read (B on the loading page, a dropped prefetch): the file is
                                                                                                   closed too, else the glue's open file (the card pack's produced copy) stayed and refused every
                                                                                                   later open until the scene changed */
        if (l->hbuf) l->sdk->free(l->hbuf);
        l->hbuf = 0;
        l->sdk->close();
    }
    if (l->buf) l->sdk->free(l->buf);
    l->buf = 0; l->api = 0; l->state = SGO_S_IDLE;
}
