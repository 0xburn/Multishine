/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the kept / prefetched sign overlay block (sg_hook.c park, prefetch, reuse) with a synthetic overlay file:
   header + image (a host struct sgo_api at api_off, a .data tail the overlay writes) + no relocs.  The model: IDLE -> PREFETCH -> PARKED -> (open) LOADING ->
   ACTIVE -> (park) PARKED -> ... -> (close / changed image) IDLE; every path ends with no live allocation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sg_hook.h"

unsigned char *sgo_host_arena;
static unsigned char file[32 + 4096];
static unsigned file_len, arena_off, live, opens, reads, busy_polls, pend, enters, leaves, no_file;
#define ARENA 0x40000
#define IMG 4096u
#define API_OFF 64u
#define BSS 256u

static void *f_alloc(sgo_u32 n)
{
    void *p;
    arena_off = (arena_off + 31u) & ~31u;
    if (arena_off + n > ARENA) return 0;
    p = sgo_host_arena + arena_off; arena_off += n; live++;
    return p;
}
static void f_free(void *p) { (void)p; if (live) live--; }
static int f_open(const char *path, sgo_u32 *len) { if (no_file || strcmp(path, "/ms/sign.ovl")) return -1; opens++; *len = file_len; return 0; }
static int f_read_begin(void *dst, sgo_u32 len, sgo_u32 off) { if (off + len > file_len) return -1; memcpy(dst, file + off, len); reads++; busy_polls = 1; pend = 1; return 0; }
static int f_read_poll(void) { if (!pend) return -1; if (busy_polls) { busy_polls--; return 0; } pend = 0; return 1; }
static void f_close(void) { }
static void f_flush(void *p, sgo_u32 n) { (void)p; (void)n; }
static const struct sgo_sdk SDK = { f_alloc, f_free, f_open, f_read_begin, f_read_poll, f_close, f_flush };

static struct sg G;
static unsigned char *img_now;                       /* where the fake overlay runs (the loaded image) */
static int o_enter(const struct sgo_env *e) { (void)e; enters++; img_now[IMG - 1] ^= 0x5A; img_now[IMG] = 0x77; return 0; }   /* writes its .data tail and its bss */
static void o_frame(const struct sdent_pad *p, sgo_u32 tb) { (void)p; (void)tb; }
static const struct sg_view *o_draw(void) { return 0; }
static void o_present(void) { }
static const struct sg *o_state(void) { return &G; }
static void o_goto(sgo_u32 s) { (void)s; }
static void o_mock(sgo_u32 a, sgo_u32 b, sgo_u32 c, sgo_u32 d, sgo_u32 e) { (void)a; (void)b; (void)c; (void)d; (void)e; }
static void o_hide(void) { }
static int o_quit(void) { return 0; }
static void o_leave(void) { leaves++; }

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void wr(unsigned char *p, sgo_u32 v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }

static int poll_until(struct sg_hook *h, int want)
{
    int i, st = h->state;
    for (i = 0; i < 20 && st != want; i++) st = sg_hook_poll(h);
    if (st == SG_H_ACTIVE || st == SG_H_PARKED) img_now = SGO_A2P(h->ld.load_addr);
    return st;
}

int main(void)
{
    static struct sg_hook H;
    struct sgo_env env;
    struct sgo_api api;
    unsigned i;
    memset(&env, 0, sizeof env);
    sgo_host_arena = (unsigned char *)calloc(1, ARENA);
    api.abi = SGO_VER; api.enter = o_enter; api.frame = o_frame; api.draw = o_draw; api.present = o_present; api.state = o_state; api.goto_screen = o_goto;
    api.mock = o_mock; api.hide = o_hide; api.quit = o_quit; api.leave = o_leave;
    wr(file, SGO_MAGIC); wr(file + 4, SGO_VER); wr(file + 8, IMG); wr(file + 12, BSS); wr(file + 16, 0); wr(file + 20, API_OFF); wr(file + 24, 0); wr(file + 28, 0);
    for (i = 0; i < IMG; i++) file[32 + i] = (unsigned char)(i * 7u + 3u);
    memcpy(file + 32 + API_OFF, &api, sizeof api);
    file_len = 32 + IMG;

    /* 1. prefetch on a dwell: IDLE -> PREFETCH -> PARKED, nothing entered */
    sg_hook_init(&H, &SDK);
    CHECK(sg_hook_prefetch(&H, "/ms/sign.ovl") == 0);
    CHECK(H.state == SG_H_PREFETCH && sg_hook_parked(&H));
    CHECK(poll_until(&H, SG_H_PARKED) == SG_H_PARKED);
    CHECK(enters == 0 && opens == 1 && live == 1 && H.prefetches == 1);
    /* 2. the open of a prefetched block: no second read, entered once */
    CHECK(sg_hook_open(&H, &env, "/ms/sign.ovl") == 0);
    CHECK(poll_until(&H, SG_H_ACTIVE) == SG_H_ACTIVE);
    CHECK(opens == 1 && enters == 1 && H.reuses == 1 && live == 1);
    /* 3. leave with park: the block stays; the next open restores the tail and zeroes bss, again without a read */
    sg_hook_park(&H);
    CHECK(H.state == SG_H_PARKED && leaves == 1 && live == 1 && H.parks == 1);
    CHECK(img_now[IMG - 1] != file[32 + IMG - 1] && img_now[IMG] == 0x77);       /* the overlay changed its data and bss while it ran */
    CHECK(sg_hook_open(&H, &env, "/ms/sign.ovl") == 0);
    CHECK(img_now[IMG - 1] == file[32 + IMG - 1] && img_now[IMG] == 0);           /* as loaded again before enter */
    CHECK(poll_until(&H, SG_H_ACTIVE) == SG_H_ACTIVE);
    CHECK(opens == 1 && enters == 2 && H.reuses == 2);
    /* 4. a parked block whose code changed is never entered: the file is read again */
    sg_hook_park(&H);
    img_now[100] ^= 1u;
    CHECK(sg_hook_open(&H, &env, "/ms/sign.ovl") == 0);
    CHECK(H.state == SG_H_LOADING && opens == 2);
    CHECK(poll_until(&H, SG_H_ACTIVE) == SG_H_ACTIVE);
    CHECK(img_now[100] == file[32 + 100] && H.reuses == 2 && live == 1);
    /* 5. close from ACTIVE and from PARKED frees everything */
    sg_hook_close(&H);
    CHECK(H.state == SG_H_IDLE && live == 0);
    CHECK(sg_hook_prefetch(&H, "/ms/sign.ovl") == 0 && poll_until(&H, SG_H_PARKED) == SG_H_PARKED);
    sg_hook_close(&H);
    CHECK(H.state == SG_H_IDLE && live == 0);
    /* 6. an open while the prefetch read is still running becomes the normal load */
    CHECK(sg_hook_prefetch(&H, "/ms/sign.ovl") == 0);
    CHECK(sg_hook_open(&H, &env, "/ms/sign.ovl") == 0 && H.state == SG_H_LOADING);
    CHECK(poll_until(&H, SG_H_ACTIVE) == SG_H_ACTIVE && enters == 4);
    sg_hook_close(&H);
    CHECK(live == 0);
    /* 7. a failed prefetch is quiet (IDLE); the open then reports the error as today */
    no_file = 1;
    CHECK(sg_hook_prefetch(&H, "/ms/sign.ovl") != 0 && H.state == SG_H_IDLE && live == 0);
    CHECK(sg_hook_open(&H, &env, "/ms/sign.ovl") == SGO_E_NOFILE && H.state == SG_H_FAILED);
    sg_hook_close(&H);
    no_file = 0;
    /* 8. park outside ACTIVE is a close; prefetch outside IDLE is refused */
    sg_hook_park(&H);
    CHECK(H.state == SG_H_IDLE && live == 0);
    CHECK(sg_hook_open(&H, &env, "/ms/sign.ovl") == 0 && sg_hook_prefetch(&H, "/ms/sign.ovl") != 0);
    CHECK(poll_until(&H, SG_H_ACTIVE) == SG_H_ACTIVE);                  /* (the glue never releases a load mid-read: the loader would leave its header block) */
    sg_hook_close(&H);
    CHECK(live == 0);
    printf("sgh_park_test: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    free(sgo_host_arena);
    return fails != 0;
}
