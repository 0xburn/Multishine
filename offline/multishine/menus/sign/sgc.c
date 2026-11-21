/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgc: commands from the website, see sgc.h.  C89, no libc, no heap, no clock. */
#include "sgc.h"

static int due(unsigned now, unsigned at) { return (int)(now - at) >= 0; }

void sgc_init(struct sgc *s, const struct sgc_host *h)
{
    unsigned char *b = (unsigned char *)s;
    unsigned i;
    for (i = 0; i < sizeof *s; i++) b[i] = 0;
    s->h = h;
}

/* the next try: after a good round the normal interval, after a bad one a doubling back-off */
static void later(struct sgc *s, unsigned now, int bad)
{
    if (!bad) { s->backoff = 0; s->next_at = now + SGC_POLL_MS; }
    else {
        s->backoff = s->backoff ? (s->backoff >= SGC_MAX_BACKOFF_MS / 2u ? SGC_MAX_BACKOFF_MS : s->backoff * 2u) : SGC_MIN_BACKOFF_MS;
        s->next_at = now + s->backoff;
    }
    s->st = SGC_IDLE;
}

static void begin_get(struct sgc *s, unsigned now)
{
    static const unsigned char q[1] = { 1 };
    if (s->h->net_begin(s->h->ctx, SGC_OP_GET, q, 1u, now) != 0) { later(s, now, 1); return; }
    s->st = SGC_GET;
}

static void send_done(struct sgc *s, unsigned now)
{
    unsigned char q[6];
    q[0] = 1; q[1] = (unsigned char)(s->id >> 24); q[2] = (unsigned char)(s->id >> 16); q[3] = (unsigned char)(s->id >> 8); q[4] = (unsigned char)s->id; q[5] = s->res;
    if (s->h->net_begin(s->h->ctx, SGC_OP_DONE, q, 6u, now) != 0) { later(s, now, 1); return; }
    s->st = SGC_DONE;
}

void sgc_frame(struct sgc *s, unsigned now, int allowed)
{
    const struct sgc_host *h = s->h;
    const unsigned char *r = 0;
    unsigned n = 0;
    int st = 0, p;
    if (!s->started) { s->started = 1; s->next_at = now + SGC_FIRST_MS; }
    if (!allowed) {
        if (s->st != SGC_IDLE) { h->net_abort(h->ctx); s->st = SGC_IDLE; }          /* an owed CMD_DONE (s->owe) is sent the next time */
        return;
    }
    switch (s->st) {
    case SGC_IDLE:
        if (!due(now, s->next_at)) return;
        if (!h->net_up(h->ctx)) { s->next_at = now + 5000u; return; }
        if (s->owe) { send_done(s, now); return; }
        if (s->revoked) { if (s->polls < 255u) s->polls++; begin_get(s, now); return; }       /* a removed console: ACCT_STATE would only say 26 again, ask for the wipe */
        {
            static const unsigned char q[3] = { 1, 0, 0 };                          /* ACCT_STATE: aver 1, no profile revision held */
            if (h->net_begin(h->ctx, SGC_OP_STATE, q, 3u, now) != 0) { later(s, now, 1); return; }
            s->st = SGC_ASK;
            if (s->polls < 255u) s->polls++;
        }
        return;
    case SGC_ASK:
        p = h->net_poll(h->ctx, now, &r, &n, &st);
        if (p == 0) return;
        if (p < 0 && st == (int)SGC_ST_TOKEN_REVOKED && h->wipe_data) { s->revoked = 1; begin_get(s, now); return; }     /* removed from its account: only CMD_GET is still answered, and only for a wipe */
        if (p < 0 || n < 4u) { later(s, now, 1); return; }
        if (!(r[0] & SGC_FLAG_PENDING)) { later(s, now, 0); return; }
        begin_get(s, now);
        return;
    case SGC_GET:
        p = h->net_poll(h->ctx, now, &r, &n, &st);
        if (p == 0) return;
        if (p < 0 || n != 5u) { later(s, now, 1); return; }
        if (r[0] == 0) { int was = s->revoked; s->revoked = 0; later(s, now, was); return; }   /* nothing (expired, cancelled, or a removed console without a wipe: ask the state again next time) */
        if (r[0] != SGC_KIND_CLEAR_SLIPPI && !(r[0] == SGC_KIND_WIPE && h->wipe_data)) { later(s, now, 0); return; }      /* a kind this build does not know: not confirmed, not touched */
        s->id = ((unsigned)r[1] << 24) | ((unsigned)r[2] << 16) | ((unsigned)r[3] << 8) | r[4];
        s->kind = r[0];
        if (s->kind == SGC_KIND_WIPE) {                                             /* step 1 of the wipe: the data files; the identity stays until the server has heard 'done' */
            s->res = (unsigned char)(h->wipe_data(h->ctx) == 0 ? 1u : 0u);
            s->owe = 1;
            if (!s->res && s->failed < 255u) s->failed++;
            send_done(s, now);
            return;
        }
        s->res = (unsigned char)(h->clear(h->ctx) == 0 ? 1u : 0u);                  /* the same clear as Delete Identity > Multishine And Slippi, Slippi part only */
        s->owe = 1;
        if (s->res) { if (s->cleared < 255u) s->cleared++; s->msg = 1; s->msg_until = now + SGC_MSG_MS; }
        else if (s->failed < 255u) s->failed++;
        send_done(s, now);
        return;
    case SGC_DONE:
        p = h->net_poll(h->ctx, now, &r, &n, &st);
        if (p == 0) return;
        if (p < 0) { later(s, now, 1); return; }                                    /* the confirmation stays owed */
        s->owe = 0;
        if (s->kind == SGC_KIND_WIPE && s->res && h->wipe_identity) {                /* the server heard 'done' (and now refuses this device for good): step 2, the identity record */
            s->idtries = 0; s->st = SGC_WIPEID; s->next_at = now;
            return;
        }
        later(s, now, s->res ? 0 : 1);                                             /* a failed clear: ask again after a back-off */
        return;
    case SGC_WIPEID:
        if (!due(now, s->next_at)) return;
        p = h->wipe_identity(h->ctx);
        if (p == 0) { if (s->wiped < 255u) s->wiped++; s->revoked = 0; s->msg = 2; s->msg_until = now + SGC_MSG_MS; later(s, now, 0); return; }
        if (p == 1 && ++s->idtries < SGC_ID_TRIES) { s->next_at = now + SGC_ID_RETRY_MS; return; }       /* the card was busy */
        later(s, now, 1);                                                          /* could not write the placeholder: the device secret on the card is dead on the server anyway */
        return;
    default:
        s->st = SGC_IDLE;
        return;
    }
}

const char *sgc_status(struct sgc *s, unsigned now)
{
    if (s->msg && (int)(now - s->msg_until) >= 0) s->msg = 0;
    return s->msg == 2 ? SGC_MSG_WIPED : s->msg ? SGC_MSG_CLEARED : 0;
}
