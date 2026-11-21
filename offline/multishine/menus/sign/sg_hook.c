/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The resident hook of the sign-in overlay: see sg_hook.h.  Strict C89, no libc. */
#include "sg_hook.h"

void sg_hook_init(struct sg_hook *h, const struct sgo_sdk *sdk)
{
    unsigned char *b = (unsigned char *)h;
    unsigned i;
    for (i = 0; i < sizeof *h; i++) b[i] = 0;
    h->ld.sdk = sdk;
}

/* ---- a parked block: the relocated image is a pure function of the file and the load address, apart from.data (at the
   image's end, sg_ovl.ld: kept at load time) and bss (zeroed again).  A checksum of everything before the tail proves nothing else changed; if it did, the block is
   dropped and the file read again: a parked block is never entered unless it is exactly as loaded. ---- */
static sgo_u32 img_sum(const struct sg_hook *h)
{
    const sgo_u8 *p = SGO_A2P(h->ld.load_addr);
    sgo_u32 i, n = h->ld.image_size - h->tail_n, x = 0x811C9DC5u;
    for (i = 0; i < n; i++) x = (x ^ p[i]) * 0x01000193u;
    return x;
}
static void park_capture(struct sg_hook *h)
{
    const sgo_u8 *p = SGO_A2P(h->ld.load_addr);
    sgo_u32 i;
    h->tail_n = h->ld.image_size < SG_H_TAIL ? h->ld.image_size : SG_H_TAIL;
    for (i = 0; i < h->tail_n; i++) h->tail[i] = p[h->ld.image_size - h->tail_n + i];
    h->sum = img_sum(h);
}
static int park_restore(struct sg_hook *h)                                 /* 1 = the block is as loaded again (tail put back, bss zero) */
{
    sgo_u8 *p;
    sgo_u32 i, clr;
    if (h->ld.state != SGO_S_READY || !h->ld.buf || !h->tail_n || img_sum(h) != h->sum) return 0;
    p = SGO_A2P(h->ld.load_addr);
    for (i = 0; i < h->tail_n; i++) p[h->ld.image_size - h->tail_n + i] = h->tail[i];
    clr = h->ld.buf_len - h->ld.image_size - (h->ld.load_addr - SGO_P2A(h->ld.buf));
    for (i = 0; i < clr; i++) p[h->ld.image_size + i] = 0;
    h->ld.sdk->flush(p + h->ld.image_size - h->tail_n, h->tail_n + clr);
    return 1;
}

int sg_hook_open(struct sg_hook *h, const struct sgo_env *env, const char *path)
{
    const struct sgo_sdk *sdk = h->ld.sdk;
    if (h->state == SG_H_PREFETCH) { h->env = *env; h->state = SG_H_LOADING; return 0; }      /* the prefetch read goes on; sg_hook_poll enters it when done */
    if (h->state == SG_H_PARKED) {
        h->env = *env;
        if (park_restore(h)) { h->reuses++; h->reused = 1; h->state = SG_H_LOADING; return 0; }    /* ld.state is READY: the next sg_hook_poll enters it */
        sgo_release(&h->ld);                                                                /* changed while parked: never entered, the file is read again */
        h->tail_n = 0;
        h->state = SG_H_IDLE;
    }
    if (h->state != SG_H_IDLE) return SGO_E_FORMAT;
    h->env = *env;
    h->err = sgo_begin(&h->ld, sdk, path);
    h->state = h->err ? SG_H_FAILED : SG_H_LOADING;
    return h->err;
}

int sg_hook_prefetch(struct sg_hook *h, const char *path)
{
    if (h->state != SG_H_IDLE) return SGO_E_FORMAT;
    if (sgo_begin(&h->ld, h->ld.sdk, path) != 0) { h->ld.state = SGO_S_IDLE; return SGO_E_NOFILE; }     /* quietly: the open reads it again and reports */
    h->prefetches++;
    h->tail_n = 0;
    h->state = SG_H_PREFETCH;
    return 0;
}

int sg_hook_parked(const struct sg_hook *h) { return h->state == SG_H_PARKED || h->state == SG_H_PREFETCH; }

int sg_hook_poll(struct sg_hook *h)
{
    if (h->state == SG_H_PREFETCH) {
        int st = sgo_step(&h->ld);
        if (st == SGO_S_READY) {
            if (h->ld.api->abi != SGO_VER) { sgo_release(&h->ld); h->state = SG_H_IDLE; }    /* the open reads it again and reports "not available" */
            else { park_capture(h); h->last_block = h->ld.peak_bytes; h->last_addr = h->ld.load_addr; h->state = SG_H_PARKED; }
        } else if (st == SGO_S_FAILED) { h->ld.state = SGO_S_IDLE; h->state = SG_H_IDLE; }
        return h->state;
    }
    if (h->state != SG_H_LOADING) return h->state;
    if (sgo_step(&h->ld) == SGO_S_READY) {
        const struct sgo_api *api = h->ld.api;
        if (!h->reused && !h->tail_n) park_capture(h);                     /* a fresh load: how it looks before anything of it runs */
        h->reused = 0;
        if (api->abi != SGO_VER || api->enter(&h->env) != 0) {          /* an overlay built for another resident layout: refuse, free, show "not available" */
            sgo_release(&h->ld);
            h->tail_n = 0;
            h->err = SGO_E_ABI;
            h->state = SG_H_FAILED;
        } else {
            h->api = api; h->st = api->state();
            h->last_block = h->ld.peak_bytes; h->last_addr = h->ld.load_addr;
            h->loads++;
            h->state = SG_H_ACTIVE;
        }
    } else if (h->ld.state == SGO_S_FAILED) {
        h->err = h->ld.err;
        h->state = SG_H_FAILED;
    }
    return h->state;
}

int sg_hook_frame(struct sg_hook *h, const struct sdent_pad *p, sgo_u32 tb)
{
    if (h->state != SG_H_ACTIVE) return 0;
    h->api->frame(p, tb);
    return h->api->quit();
}

void sg_hook_present(struct sg_hook *h) { if (h->state == SG_H_ACTIVE) h->api->present(); }

static void leave_active(struct sg_hook *h)
{
    h->api->leave();                                                       /* aborts a TLS session, wipes key material, detaches the stack's hooks */
#if SG_LAB
    h->hooks_clear = (h->env.net->tcp_rx == 0 && h->env.net->tcp_poll == 0 && h->env.net->tcp == 0) ? 1u : 0u;
#else
    h->hooks_clear = 1u;                                                   /* release overlay: no TCP, no TLS, no hooks into the network stack at all */
#endif
}

void sg_hook_close(struct sg_hook *h)
{
    if (h->state == SG_H_ACTIVE) {
        leave_active(h);
        h->frees++;
    }
    sgo_release(&h->ld);                                                   /* frees the block (no-op when the load never got one; also a parked one) */
    h->api = 0; h->st = 0; h->tail_n = 0; h->reused = 0;
    h->state = SG_H_IDLE;
}

void sg_hook_park(struct sg_hook *h)
{
    if (h->state != SG_H_ACTIVE || !h->tail_n) { sg_hook_close(h); return; }
    leave_active(h);
    h->parks++;
    h->api = 0; h->st = 0;
    h->state = SG_H_PARKED;                                                /* ld.state stays READY and the block stays allocated */
}
