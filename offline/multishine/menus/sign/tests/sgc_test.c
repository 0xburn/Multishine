/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the website-command state machine (sgc.c) with a scripted mailbox (no server): what is asked, in which order, what is confirmed, what is retried, and that a
   dropped operation never loses an owed confirmation.   cc -std=c99 ... sgc_test.c ../sgc.c */
#include <stdio.h>
#include <string.h>
#include "../sgc.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* ---- the fake mailbox: a queue of scripted replies; every begin is recorded */
static struct {
    int up, begin_fail, clear_result, clears, aborts, wipes, wipe_result, idcalls, id_results[8];
    unsigned ops[64], nops;
    unsigned char reqs[64][8];
    unsigned reqn[64];
    int busy_polls;                       /* poll answers 0 this many times before the reply */
    int reply_kind;                       /* what the next poll returns: 1 done, -1 failed */
    int status;
    unsigned char reply[32];
    unsigned reply_n;
    int inflight;
} M;
static int h_up(void *c) { (void)c; return M.up; }
static int h_begin(void *c, unsigned op, const unsigned char *r, unsigned n, unsigned now)
{
    (void)c; (void)now;
    if (M.begin_fail) return 1;
    M.ops[M.nops] = op; M.reqn[M.nops] = n; memcpy(M.reqs[M.nops], r, n); M.nops++;
    M.inflight = 1;
    return 0;
}
static int h_poll(void *c, unsigned now, const unsigned char **r, unsigned *n, int *st)
{
    (void)c; (void)now;
    *st = 0;
    if (!M.inflight) return -1;
    if (M.busy_polls > 0) { M.busy_polls--; return 0; }
    M.inflight = 0;
    if (M.reply_kind < 0) { *st = M.status; return -1; }
    *r = M.reply; *n = M.reply_n;
    return 1;
}
static void h_abort(void *c) { (void)c; M.aborts++; M.inflight = 0; }
static int h_clear(void *c) { (void)c; M.clears++; return M.clear_result; }
static int h_wipe_data(void *c) { (void)c; M.wipes++; return M.wipe_result; }
static int h_wipe_id(void *c) { (void)c; M.idcalls++; return M.id_results[M.idcalls <= 8 ? M.idcalls - 1 : 7]; }
static const struct sgc_host H = { 0, h_up, h_begin, h_poll, h_abort, h_clear, h_wipe_data, h_wipe_id };

static void reply(unsigned n, ...) { (void)n; }
static void set_state(unsigned flags)
{
    memset(M.reply, 0, sizeof M.reply);
    M.reply[0] = (unsigned char)flags; M.reply[1] = 0; M.reply[2] = 1; M.reply[3] = 0; M.reply[4] = 0; M.reply[5] = 0;     /* flags | rev u16 | notices | code pstr | name pstr */
    M.reply_n = 6; M.reply_kind = 1;
}
static void set_cmd(unsigned kind, unsigned id)
{
    M.reply[0] = (unsigned char)kind; M.reply[1] = (unsigned char)(id >> 24); M.reply[2] = (unsigned char)(id >> 16); M.reply[3] = (unsigned char)(id >> 8); M.reply[4] = (unsigned char)id;
    M.reply_n = 5; M.reply_kind = 1;
}
static void set_done_ok(void) { M.reply_n = 0; M.reply_kind = 1; }
static void set_fail(int status) { M.reply_kind = -1; M.status = status; }

static struct sgc S;
static unsigned T;                         /* the test clock, ms */
static void frames(unsigned n, int allowed) { while (n--) { T += 10u; sgc_frame(&S, T, allowed); } }
static void reset(void) { memset(&M, 0, sizeof M); M.up = 1; T = 1000000u; sgc_init(&S, &H); }
static void start(void) { sgc_frame(&S, T, 1); T += SGC_FIRST_MS; }          /* the first frame fixes the first look SGC_FIRST_MS later */
static void jump_to_due(void) { T = S.next_at; }

int main(void)
{
    (void)reply;
    /* 1. nothing pending: one ACCT_STATE, no GET, no clear, the next one a minute later */
    reset();
    frames(10, 1);
    CHECK(M.nops == 0);                                   /* the first look waits SGC_FIRST_MS */
    set_state(0);
    T += SGC_FIRST_MS; frames(1, 1);
    CHECK(M.nops == 1 && M.ops[0] == SGC_OP_STATE && M.reqn[0] == 3 && M.reqs[0][0] == 1);
    frames(2, 1);
    CHECK(S.st == SGC_IDLE && M.nops == 1 && M.clears == 0);
    CHECK(S.next_at - T >= SGC_POLL_MS - 100u && S.next_at - T <= SGC_POLL_MS);
    frames(100, 1);
    CHECK(M.nops == 1);                                   /* not before the interval */
    CHECK(sgc_status(&S, T) == 0);

    /* 2. pending: ASK -> GET -> clear -> DONE(1) with the id, the status line, then quiet */
    reset(); start(); set_state(SGC_FLAG_PENDING | 1u);
    frames(2, 1);
    CHECK(M.nops == 2 && M.ops[1] == SGC_OP_GET && M.reqn[1] == 1 && M.reqs[1][0] == 1);
    set_cmd(SGC_KIND_CLEAR_SLIPPI, 0xA1B2C3D4u);
    frames(1, 1);
    CHECK(M.clears == 1);
    CHECK(M.nops == 3 && M.ops[2] == SGC_OP_DONE && M.reqn[2] == 6);
    CHECK(M.reqs[2][0] == 1 && M.reqs[2][1] == 0xA1 && M.reqs[2][2] == 0xB2 && M.reqs[2][3] == 0xC3 && M.reqs[2][4] == 0xD4 && M.reqs[2][5] == 1);
    CHECK(sgc_status(&S, T) && !strcmp(sgc_status(&S, T), "Slippi login removed by the website."));
    set_done_ok();
    frames(1, 1);
    CHECK(S.st == SGC_IDLE && !S.owe && S.cleared == 1);
    T += SGC_MSG_MS + 10u;
    CHECK(sgc_status(&S, T) == 0);

    /* 3. the clear fails: DONE(0), back-off, asked again, cleared the second time */
    reset(); start(); set_state(SGC_FLAG_PENDING);
    frames(2, 1); M.clear_result = 3; set_cmd(1, 77);
    frames(1, 1);
    CHECK(M.clears == 1 && M.nops == 3 && M.reqs[2][5] == 0 && S.failed == 1 && sgc_status(&S, T) == 0);
    set_done_ok(); frames(1, 1);
    CHECK(S.st == SGC_IDLE && !S.owe && S.next_at - T > 0 && S.next_at - T <= SGC_MIN_BACKOFF_MS && S.next_at - T > SGC_MIN_BACKOFF_MS - 100u);
    frames(100, 1); CHECK(M.nops == 3);
    jump_to_due(); M.clear_result = 0; set_state(SGC_FLAG_PENDING);
    frames(2, 1); set_cmd(1, 77); frames(1, 1);
    CHECK(M.clears == 2 && M.reqs[M.nops - 1][5] == 1 && sgc_status(&S, T));

    /* 4. network failures back off by doubling up to the maximum, a good round resets */
    reset(); start(); set_fail(0);
    frames(2, 1);
    CHECK(S.st == SGC_IDLE && S.next_at - T > SGC_MIN_BACKOFF_MS - 100u && S.next_at - T <= SGC_MIN_BACKOFF_MS);
    jump_to_due(); frames(2, 1);
    CHECK(S.next_at - T > 2u * SGC_MIN_BACKOFF_MS - 100u && S.next_at - T <= 2u * SGC_MIN_BACKOFF_MS);
    { int i; for (i = 0; i < 10; i++) { jump_to_due(); frames(2, 1); } }
    CHECK(S.next_at - T > SGC_MAX_BACKOFF_MS - 100u && S.next_at - T <= SGC_MAX_BACKOFF_MS);
    jump_to_due(); set_state(0); frames(2, 1);
    CHECK(S.backoff == 0 && S.next_at - T > SGC_POLL_MS - 100u);
    CHECK(M.clears == 0);
    /* an old server (UNKNOWN_OP) is the same as a failure */
    reset(); start(); set_fail(2); frames(2, 1);
    CHECK(S.backoff == SGC_MIN_BACKOFF_MS && M.clears == 0);
    /* the request cannot even be sent */
    reset(); start(); M.begin_fail = 1; frames(1, 1);
    CHECK(S.st == SGC_IDLE && S.backoff == SGC_MIN_BACKOFF_MS && M.nops == 0);

    /* 5. no link: nothing is sent, nothing is backed off */
    reset(); M.up = 0; start(); frames(50, 1);
    CHECK(M.nops == 0 && S.backoff == 0);
    M.up = 1; set_state(0); frames(600, 1); CHECK(M.nops >= 1);

    /* 6. hostile replies: a short state, a short or long command, an unknown kind, kind 0 */
    reset(); start(); set_state(SGC_FLAG_PENDING); M.reply_n = 3; frames(2, 1);
    CHECK(M.nops == 1 && M.clears == 0 && S.backoff == SGC_MIN_BACKOFF_MS);
    reset(); start(); set_state(SGC_FLAG_PENDING); frames(2, 1); set_cmd(1, 5); M.reply_n = 4; frames(1, 1);
    CHECK(M.clears == 0 && M.nops == 2 && S.backoff == SGC_MIN_BACKOFF_MS);
    reset(); start(); set_state(SGC_FLAG_PENDING); frames(2, 1); set_cmd(1, 5); M.reply_n = 6; frames(1, 1);
    CHECK(M.clears == 0 && M.nops == 2);
    reset(); start(); set_state(SGC_FLAG_PENDING); frames(2, 1); set_cmd(9, 5); frames(1, 1);
    CHECK(M.clears == 0 && M.nops == 2 && !S.owe && S.st == SGC_IDLE);                   /* unknown kind: left alone, not confirmed */
    reset(); start(); set_state(SGC_FLAG_PENDING); frames(2, 1); set_cmd(0, 0); frames(1, 1);
    CHECK(M.clears == 0 && M.nops == 2 && !S.owe && S.backoff == 0);                     /* cancelled meanwhile */

    /* 7. a flag without the pending bit but other bits set does nothing */
    reset(); start(); set_state(1u | 4u | 16u); frames(3, 1);
    CHECK(M.nops == 1 && M.clears == 0);

    /* 8. not allowed (another screen, another client): nothing starts, a running op is dropped, the owed confirmation survives */
    reset(); frames(1000, 0); CHECK(M.nops == 0);
    T += SGC_FIRST_MS; set_state(SGC_FLAG_PENDING); M.busy_polls = 5;
    frames(1, 1); CHECK(S.st == SGC_ASK);
    frames(1, 0); CHECK(S.st == SGC_IDLE && M.aborts == 1);
    M.busy_polls = 0; frames(2, 1); CHECK(S.st == SGC_GET);
    set_cmd(1, 0x01020304u); frames(1, 1);
    CHECK(M.clears == 1 && S.owe && S.st == SGC_DONE);
    M.busy_polls = 3; frames(1, 0);                                                      /* dropped while the confirmation is in flight */
    CHECK(S.st == SGC_IDLE && S.owe && M.aborts == 2);
    { unsigned before = M.nops; M.busy_polls = 0; set_done_ok(); frames(2, 1);
      CHECK(M.nops == before + 1 && M.ops[M.nops - 1] == SGC_OP_DONE && M.reqs[M.nops - 1][4] == 4 && M.reqs[M.nops - 1][5] == 1);   /* only the confirmation, no new ask, no second clear */
      CHECK(M.clears == 1 && !S.owe); }

    /* 9. a failed confirmation stays owed and is repeated after the back-off */
    reset(); start(); set_state(SGC_FLAG_PENDING); frames(2, 1); set_cmd(1, 42); frames(1, 1);
    CHECK(S.st == SGC_DONE);
    set_fail(0); frames(1, 1);
    CHECK(S.st == SGC_IDLE && S.owe && M.clears == 1);
    jump_to_due(); set_done_ok(); frames(2, 1);
    CHECK(!S.owe && M.ops[M.nops - 1] == SGC_OP_DONE && M.clears == 1 && S.backoff == 0);

    /* 10. a REMOVED console (ACCT_STATE answers 26): straight to CMD_GET, kind 2 -> the data wipe, CMD_DONE(1) with the id, ONLY THEN the identity, then the status line once */
    reset(); start(); M.id_results[0] = 0; set_fail(SGC_ST_TOKEN_REVOKED);
    frames(1, 1); CHECK(S.st == SGC_ASK && M.ops[0] == SGC_OP_STATE);
    frames(1, 1); CHECK(S.st == SGC_GET && S.revoked && M.ops[1] == SGC_OP_GET && M.reqn[1] == 1);
    set_cmd(SGC_KIND_WIPE, 0x0A0B0C0Du); frames(1, 1);
    CHECK(M.wipes == 1 && M.clears == 0 && M.idcalls == 0 && S.st == SGC_DONE && S.owe);
    CHECK(M.ops[2] == SGC_OP_DONE && M.reqn[2] == 6 && M.reqs[2][1] == 0x0A && M.reqs[2][4] == 0x0D && M.reqs[2][5] == 1);
    CHECK(sgc_status(&S, T) == 0);                                                       /* nothing is claimed before the server heard it */
    set_done_ok(); frames(1, 1);
    CHECK(S.st == SGC_WIPEID && !S.owe && M.idcalls == 0);
    frames(1, 1);
    CHECK(M.idcalls == 1 && S.wiped == 1 && S.st == SGC_IDLE && !S.revoked && !strcmp(sgc_status(&S, T), "This console was removed and its data erased."));
    T += SGC_MSG_MS + 10u; CHECK(sgc_status(&S, T) == 0);

    /* 11. the data wipe fails: DONE(0), the identity is never touched, the wipe is asked again after the back-off and then works */
    reset(); start(); M.wipe_result = 5; M.id_results[0] = 0; set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1);
    set_cmd(SGC_KIND_WIPE, 9); frames(1, 1);
    CHECK(M.wipes == 1 && M.reqs[2][5] == 0 && S.failed == 1);
    set_done_ok(); frames(2, 1);
    CHECK(M.idcalls == 0 && S.st == SGC_IDLE && !S.owe && S.next_at - T > SGC_MIN_BACKOFF_MS - 100u);
    jump_to_due(); M.wipe_result = 0; frames(1, 1); CHECK(S.st == SGC_GET || M.ops[M.nops - 1] == SGC_OP_GET);
    set_cmd(SGC_KIND_WIPE, 9); frames(1, 1); set_done_ok(); frames(3, 1);
    CHECK(M.wipes == 2 && M.idcalls == 1 && S.wiped == 1);

    /* 12. the data is gone but CMD_DONE does not get through: the identity (the key that can still talk to the server) stays until it does */
    reset(); start(); M.id_results[0] = 0; set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1);
    set_cmd(SGC_KIND_WIPE, 31); frames(1, 1);
    set_fail(0); frames(1, 1);
    CHECK(S.st == SGC_IDLE && S.owe && M.idcalls == 0 && M.wipes == 1);
    jump_to_due(); set_done_ok(); frames(2, 1);
    CHECK(M.ops[M.nops - 1] == SGC_OP_DONE && M.wipes == 1);                              /* only the confirmation, no second wipe */
    frames(3, 1); CHECK(M.idcalls == 1 && S.wiped == 1 && !S.owe);

    /* 13. the identity record is busy a few times (retry every 2 s), then done; a record that never works ends in a back-off with no message */
    reset(); start(); M.id_results[0] = 1; M.id_results[1] = 1; M.id_results[2] = 0; set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1);
    set_cmd(SGC_KIND_WIPE, 4); frames(1, 1); set_done_ok(); frames(2, 1);
    CHECK(M.idcalls == 1 && S.st == SGC_WIPEID);
    frames(100, 1); CHECK(M.idcalls == 1);                                                /* 1 s: not yet */
    T += SGC_ID_RETRY_MS; frames(1, 1); CHECK(M.idcalls == 2);
    T += SGC_ID_RETRY_MS; frames(1, 1); CHECK(M.idcalls == 3 && S.wiped == 1 && sgc_status(&S, T));
    reset(); start(); { int i; for (i = 0; i < 8; i++) M.id_results[i] = 1; } set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1);
    set_cmd(SGC_KIND_WIPE, 4); frames(1, 1); set_done_ok(); frames(2, 1);
    { int i; for (i = 0; i < 12; i++) { T += SGC_ID_RETRY_MS; frames(1, 1); } }
    CHECK(M.idcalls == (int)SGC_ID_TRIES && S.wiped == 0 && S.st == SGC_IDLE && sgc_status(&S, T) == 0 && S.backoff == SGC_MIN_BACKOFF_MS);

    /* 14. a removed console with NO wipe waiting: CMD_GET answers kind 0, the state is asked again later (a back-off, so no hammering), nothing is wiped or confirmed */
    reset(); start(); set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1);
    set_cmd(0, 0); frames(1, 1);
    CHECK(M.wipes == 0 && M.idcalls == 0 && !S.owe && !S.revoked && S.backoff == SGC_MIN_BACKOFF_MS);
    jump_to_due(); set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1); CHECK(M.ops[M.nops - 2] == SGC_OP_STATE && S.revoked);       /* it asked the state again, not the command */

    /* 15. a build that does not know kind 2 (no wipe host): kind 2 is left alone and not confirmed, and a 26 on ACCT_STATE is only a failure (what the shipped consoles do today) */
    {
        static const struct sgc_host OLD = { 0, h_up, h_begin, h_poll, h_abort, h_clear, 0, 0 };
        sgc_init(&S, &OLD); memset(&M, 0, sizeof M); M.up = 1; T = 1000000u; start();
        set_fail(SGC_ST_TOKEN_REVOKED); frames(2, 1);
        CHECK(M.nops == 1 && !S.revoked && S.backoff == SGC_MIN_BACKOFF_MS);
        sgc_init(&S, &OLD); memset(&M, 0, sizeof M); M.up = 1; T = 1000000u; start(); set_state(SGC_FLAG_PENDING); frames(2, 1);
        set_cmd(SGC_KIND_WIPE, 8); frames(1, 1);
        CHECK(M.wipes == 0 && M.nops == 2 && !S.owe && S.st == SGC_IDLE);
    }

    /* 16. a kind 1 command still works exactly as before next to the new code (no wipe, no identity) */
    reset(); start(); set_state(SGC_FLAG_PENDING); frames(2, 1); set_cmd(1, 5); frames(1, 1); set_done_ok(); frames(2, 1);
    CHECK(M.clears == 1 && M.wipes == 0 && M.idcalls == 0 && S.cleared == 1);

    printf("sgc_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
