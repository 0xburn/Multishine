/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the Friends net glue (frd_net.c) against a server that says nothing or "session unknown" (Wii U: "Sending the challenge" never ended
   after a server restart).  A virtual clock, a fake wire (the datagrams sent are recorded, the replies scripted), the real frd.c / mnc / socc / mw2. */
#include <stdio.h>
#include <string.h>
#include "../frd_net.h"
static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static mnc M;
static unsigned now;
static int sends, hs_sends, unknown_mode;
static mnc_u8 last_pkt[MNC_MAX_PACKET];
static int pending;
static int w_send(void *ctx, const mnc_u8 *p, mnc_u32 len)
{
    (void)ctx; sends++;
    if (len <= sizeof last_pkt) memcpy(last_pkt, p, len);
    if (p[2] == MW2_OP_HS) hs_sends++;
    if (unknown_mode) pending = 1;
    return 0;
}
static int w_recv(void *ctx, mnc_u8 *buf, mnc_u32 cap)
{
    (void)ctx; (void)cap;
    if (!unknown_mode || !pending) return 0;
    pending = 0;
    memset(buf, 0, MW2_HDR);                                           /* the server's answer to an unknown session: the bare 12-byte header, status 31 */
    buf[0] = 0x6D; buf[1] = 2; buf[2] = (mnc_u8)(last_pkt[2] | 0x80u); buf[3] = MW2_ST_SESSION_UNKNOWN;
    memcpy(buf + 4, last_pkt + 4, 8);
    return MW2_HDR;
}
static int t_hello(unsigned t) { return mnc_hello(&M, t); }
static int t_poll(unsigned t) { return mnc_poll(&M, t); }
static int t_up(void) { return 1; }

static struct frd F;
static struct frdn_io IO;
static unsigned char fight, freq, pol, rgn, wch;
static void frame(unsigned held)
{
    frd_frame(&F, held, 0, 0);
    frdn_frame(&F, &fight, &freq, &pol, &rgn, &wch, &IO, now);
    now += 33u;
}
static void setup(int stale_session)
{
    static const mnc_u8 key[32] = { 1 };
    static const char defs[MNC_N][MNC_TXT];
    static mnc_io io;
    struct frd_ent e;
    memset(&F, 0, sizeof F); memset(&M, 0, sizeof M); frdn_reset();
    io.send = w_send; io.recv = w_recv; io.ctx = 0;
    mnc_init(&M, &io, defs, 1, 1, 1, 1);
    mnc_set_server_keys(&M, key, 1);
    M.have_hello = 1; M.srv_caps = SOCC_CAP_SOCIAL;
    now = 100000u; sends = hs_sends = pending = 0;
    if (stale_session) { M.w.state = MW2_READY; M.w.dev_sess = 1; M.w.sid = 77; M.w.ctr = 5; M.w.ttl_ms = 3600000u; M.w.last_ok = now; }     /* the client thinks it has a session; the server restarted */
    IO.net = &M; IO.hello = t_hello; IO.poll = t_poll; IO.link_up = t_up;
    frd_enter(&F, 1); F.req = FRD_REQ_NONE;
    frd_net_requests(&F, 0, 0, 0);
    memset(&e, 0, sizeof e); strcpy(e.code, "BOT@1002"); strcpy(e.name, "PC Bot 9"); e.presence = 2;
    frd_net_friends(&F, 1, 0, 1, &e);
    F.sel = 0;
}
static void press_a(void) { frame(0); frame(FRD_B_A); frame(0); }

/* returns the frames until the screen left BUSY (0 = never within 40 s) */
static unsigned run_until_free(void)
{
    unsigned n;
    for (n = 0; n < 30u * 40u; n++) { frame(0); if (F.scr != FRD_BUSY && !frdn_busy()) return n + 1u; }
    return 0;
}
static void clean_and_works(void)                                      /* the next request is taken and ends by itself, in time */
{
    unsigned n;
    CHECK(!frdn_busy());
    F.req = FRD_REQ_ADD; strcpy(F.arg_code, "ABC@1"); F.scr = FRD_BUSY; F.busy_what = FRD_REQ_ADD; F.back_to = FRD_LIST; F.wait = 0;
    frame(0);
    CHECK(F.req == FRD_REQ_NONE);                                      /* taken by the glue */
    n = run_until_free();
    CHECK(n > 0u && F.scr == FRD_LIST && !frdn_busy());
}

int main(void)
{
    unsigned n;
    /* 1. no session, the server says nothing: the HELLO runs out (4 sends over 2 s), the request fails with a message */
    setup(0);
    press_a();
    CHECK(F.scr == FRD_BUSY);
    n = run_until_free();
    CHECK(n > 0u && n < FRD_BUSY_FRAMES && F.scr == FRD_LIST && F.msg && F.msg_warn);
    CHECK(hs_sends >= 1);
    clean_and_works();
    /* 2. a session the server forgot WITHOUT a word: 5 sends, then one fresh handshake (the dead session is dropped first), then the end; bounded */
    setup(1);
    press_a();
    n = run_until_free();
    CHECK(n > 0u && n <= FRD_BUSY_FRAMES && F.scr == FRD_LIST && F.msg);
    CHECK(hs_sends >= 1);                                              /* the re-HELLO really went out as a handshake, not as a request on the dead session */
    clean_and_works();
    /* 3. the server answers "session unknown" to everything: the NOSESSION path, one fresh handshake, then a bounded end */
    setup(1); unknown_mode = 1;
    press_a();
    n = run_until_free();
    CHECK(n > 0u && n <= FRD_BUSY_FRAMES && F.scr == FRD_LIST && F.msg);
    CHECK(hs_sends >= 1);
    unknown_mode = 0;
    clean_and_works();
    /* 4. B cancels while the request waits for the wire; the glue drops it and the next request works */
    setup(1);
    press_a();
    for (n = 0; n < 20u; n++) frame(0);
    CHECK(F.scr == FRD_BUSY && frdn_busy());
    frame(0); frame(FRD_B_B); frame(0);
    CHECK(F.scr == FRD_LIST && F.msg && !strcmp(F.msg, "Cancelled") && !frdn_busy() && !F.abort);
    clean_and_works();
    /* 5. the 12 s screen limit fires even when the glue never ends (a hello nobody completes): the glue is told to drop it */
    setup(0);
    press_a();
    M.timeout_ms = 1000000u; M.max_tries = 1000u;                      /* a wire whose own timers never fire */
    for (n = 0; n < FRD_BUSY_FRAMES + 10u; n++) frame(0);
    CHECK(F.scr == FRD_LIST && F.msg && !strcmp(F.msg, "No answer from the server") && !frdn_busy());
    if (fails) { printf("frdn: %d FAILED\n", fails); return 1; }
    printf("friends net glue: all checks passed\n");
    return 0;
}
