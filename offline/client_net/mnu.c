/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* mnu: UPD_CHECK / UPD_FETCH client, see mnu.h.  Host-tested (tests/mnu_driver.c, tests/test_mnu.py); not part of any console image. */
#include "mnu.h"

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }
static void p32(mnc_u8 *p, mnc_u32 v) { p[0] = (mnc_u8) (v >> 24); p[1] = (mnc_u8) (v >> 16); p[2] = (mnc_u8) (v >> 8); p[3] = (mnc_u8) v; }
static mnc_u32 elapsed(mnc_u32 now, mnc_u32 t) { return now - t; }

void mnu_init(mnu *u, mnc *c)
{
    mnc_u32 i;
    u->c = c; u->state = MNU_IDLE; u->err = MNU_E_NONE; u->status = 0; u->timeout_ms = 500u;
    u->flags = 0; u->srv_abi = 0; u->blocks = 0; u->latest = u->size = 0;
    for (i = 0; i < 32; i++) u->sha256[i] = 0;
    u->have_check = u->prefer_signed = u->fast = u->hash_on = u->hash_ok = 0; u->hashed = 0;
    u->want_adv = u->have_adv = 0; u->adv_state = u->adv_text = 0; u->adv_ours_latest = u->adv_ours_min = 0;
    for (i = 0; i < 3; i++) u->adv_slippi_sup[i] = u->adv_slippi_new[i] = 0;
    u->kabi = 0; u->op = 0; u->buf = 0; u->cap = u->seq = u->nchunks = u->next = u->got = 0;
    for (i = 0; i < sizeof u->have; i++) u->have[i] = 0;
    for (i = 0; i < MNU_WIN; i++) u->slot[i].used = 0;
    u->st_sent = u->st_resent = u->st_rx = u->st_bad = u->st_rate = u->st_unsigned = 0;
    u->stall_ms = MNU_STALL_MS; u->last_ok_ms = 0; u->st_gap_ms = 0; u->resumes = 0; u->resumable = 0;
    for (i = 0; i < 32; i++) u->dsha[i] = 0;
}

static void fail(mnu *u, int err, int status) { u->state = MNU_FAILED; u->err = err; u->status = status; }

/* seal the payload (already at req + MW2_HDR) and send it; the sealed datagram is kept in slot s for resends with the same wire counter */
static int send_req(mnu *u, mnc_u8 op, mnc_u32 plen, int s, mnc_u32 now)
{
    mnc *c = u->c;
    mnc_u32 n = mw2_seal(&c->w, c->req, op, plen), i;
    if (n > sizeof u->slot[s].req) return -1;
    for (i = 0; i < n; i++) u->slot[s].req[i] = c->req[i];
    u->slot[s].len = (mnc_u8) n; u->slot[s].ctr = c->w.ctr; u->slot[s].sent_ms = now; u->slot[s].tries = 1; u->slot[s].used = 1;
    u->st_sent++;
    (void) c->io.send(c->io.ctx, c->req, n);
    return 0;
}

int mnu_check(mnu *u, mnc_u32 now, mnc_u32 abi, mnc_u32 have)
{
    mnc *c = u->c;
    mnc_u32 i;
    if (u->state == MNU_BUSY) return MNU_E_BUSY;
    if (!mw2_ready(&c->w, now)) { fail(u, MNU_E_NOSESSION, 0); return MNU_E_NOSESSION; }
    u->state = MNU_BUSY; u->err = MNU_E_NONE; u->status = 0; u->op = MNU_OP_CHECK; u->have_check = 0; u->have_adv = 0; u->kabi = abi;
    for (i = 0; i < MNU_WIN; i++) u->slot[i].used = 0;
    c->req[MW2_HDR] = (mnc_u8) (abi >> 8); c->req[MW2_HDR + 1] = (mnc_u8) abi; p32(c->req + MW2_HDR + 2, have);
    c->req[MW2_HDR + 6] = 1u;                                          /* option byte: bit 0 = send the advisory record (only sent in the datagram when want_adv) */
    return send_req(u, MNU_OP_CHECK, u->want_adv ? 7u : 6u, 0, now) == 0 ? 0 : (fail(u, MNU_E_ARG, 0), MNU_E_ARG);
}

/* the wire key of the file in flight: the build_seq, with the kind (ABI, set by the check) in the top byte for any kind but the engine's (ABI 1;  /) */
static mnc_u32 kseq(const mnu *u) { return u->kabi > 1u ? (u->kabi << 24) | (u->seq & 0xFFFFFFu) : u->seq; }

/* (re)send chunk `chunk` in slot s with a NEW wire counter: the server drops a counter at or below the highest it has seen (replay rule) unless it can answer from
   its reply cache, so after a lost request only a fresh counter gets through.  The op is idempotent, so a fresh request is always safe. */
static int chunk_send(mnu *u, int s, mnc_u32 chunk, mnc_u32 now)
{
    mnc *c = u->c;
    mnc_u32 off = chunk * MNU_CHUNK, n = u->size - off;
    if (n > MNU_CHUNK) n = MNU_CHUNK;
    p32(c->req + MW2_HDR, kseq(u)); p32(c->req + MW2_HDR + 4, off);
    c->req[MW2_HDR + 8] = (mnc_u8) (n >> 8); c->req[MW2_HDR + 9] = (mnc_u8) n;
    u->slot[s].chunk = chunk;
    return send_req(u, u->op, 10u, s, now) == 0;
}

static int request_chunk(mnu *u, mnc_u32 chunk, mnc_u32 now)
{
    int s;
    for (s = 0; s < MNU_WIN && u->slot[s].used; s++) ;
    if (s == MNU_WIN) return 0;
    return chunk_send(u, s, chunk, now);
}

static int checked_for(const mnu *u, mnc_u32 seq, mnc_u32 size) { return u->have_check && (u->flags & MNU_UF_NEWER) && u->latest == seq && u->size == size; }

/* the part of a fetch start shared by mnu_fetch and mnu_resume: path choice (fast / signed), hash state, slots; `keep` = the received chunks stay */
static int fetch_start(mnu *u, mnc_u32 now, mnc_u32 seq, mnc_u32 size, mnc_u8 *buf, mnc_u32 cap, int keep)
{
    mnc_u32 i;
    if (u->state == MNU_BUSY) return MNU_E_BUSY;
    if (!buf || size == 0) { fail(u, MNU_E_ARG, 0); return MNU_E_ARG; }
    if (size > cap || (size + MNU_CHUNK - 1u) / MNU_CHUNK > MNU_MAX_CHUNKS) { fail(u, MNU_E_TOO_BIG, 0); return MNU_E_TOO_BIG; }
    if (!mw2_ready(&u->c->w, now)) { fail(u, MNU_E_NOSESSION, 0); return MNU_E_NOSESSION; }
    {
        /* the check result is usable only if it announced exactly this file */
        int checked = checked_for(u, seq, size);
        u->hash_on = (mnc_u8) checked; u->hash_ok = 0; u->hashed = 0;
        u->fast = (mnc_u8) (checked && (u->flags & MNU_UF_FETCHU) && !u->c->w.dev_sess && !u->prefer_signed);
        u->op = u->fast ? MNU_OP_FETCHU : MNU_OP_FETCH;
        if (u->hash_on) { ms_sha256_init(&u->hctx); for (i = 0; i < 32; i++) u->dsha[i] = u->sha256[i]; }
        u->st_rate = 0;
    }
    u->state = MNU_BUSY; u->err = MNU_E_NONE; u->status = 0;
    u->buf = buf; u->cap = cap; u->seq = seq; u->size = size; u->nchunks = (size + MNU_CHUNK - 1u) / MNU_CHUNK; u->next = 0;
    if (!keep) {
        u->got = 0; u->st_gap_ms = 0; u->resumes = 0;
        for (i = 0; i < sizeof u->have; i++) u->have[i] = 0;
    }
    for (i = 0; i < MNU_WIN; i++) u->slot[i].used = 0;
    u->last_ok_ms = now;
    u->resumable = 1;
    return 0;
}

static void feed_hash(mnu *u);
int mnu_fetch(mnu *u, mnc_u32 now, mnc_u32 seq, mnc_u32 size, mnc_u8 *buf, mnc_u32 cap) { return fetch_start(u, now, seq, size, buf, cap, 0); }

int mnu_resume(mnu *u, mnc_u32 now, mnc_u32 seq, mnc_u32 size, mnc_u8 *buf, mnc_u32 cap)
{
    mnc_u32 i, diff = 0;
    int r, was_hashed = u->hash_on;
    /* the kept chunks belong to THIS file: same buffer, build and size as the download in flight; a hash-checked one also needs a check that announces the same SHA-256 */
    int keep = u->resumable && u->got > 0 && u->buf == buf && u->seq == seq && u->size == size;
    if (keep && was_hashed) {
        if (!checked_for(u, seq, size)) keep = 0;
        else for (i = 0; i < 32; i++) diff |= (mnc_u32) (u->sha256[i] ^ u->dsha[i]);
        if (diff) keep = 0;
    }
    if (!keep) return fetch_start(u, now, seq, size, buf, cap, 0);
    r = fetch_start(u, now, seq, size, buf, cap, 1);
    if (r != 0) return r;
    u->resumes++;
    feed_hash(u);                                                       /* the kept contiguous prefix is hashed again from the buffer (the hash context starts over) */
    return 0;
}

static void on_check_reply(mnu *u, const mnc_u8 *b, mnc_u32 n)
{
    mnc_u32 i;
    mnc_u32 adv = 0;
    if (n == 45u + MNU_ADV_LEN && u->want_adv && (b[0] & MNU_UF_ADV)) adv = 1;      /* the advisory is accepted only when it was asked for and the flag says it is there */
    else if (n != 45u || (b[0] & MNU_UF_ADV)) { u->st_bad++; return; }
    u->have_adv = 0;
    if (adv) {
        const mnc_u8 *a = b + 45;
        if (a[14] > 2u) { u->st_bad++; return; }                       /* a state this build does not know: not a valid record */
        u->adv_ours_latest = g32(a); u->adv_ours_min = g32(a + 4);
        for (i = 0; i < 3; i++) { u->adv_slippi_sup[i] = a[8 + i]; u->adv_slippi_new[i] = a[11 + i]; }
        u->adv_state = a[14]; u->adv_text = a[15];
        u->have_adv = 1;
    }
    u->flags = b[0]; u->srv_abi = (mnc_u16) ((b[1] << 8) | b[2]); u->latest = g32(b + 3); u->size = g32(b + 7); u->blocks = (mnc_u16) ((b[11] << 8) | b[12]);
    for (i = 0; i < 32; i++) u->sha256[i] = b[13 + i];
    u->slot[0].used = 0;
    u->have_check = 1;
    u->state = MNU_DONE;
}

/* hash the chunks that are now contiguous from the start (chunks may arrive in any order; each is hashed once, from the buffer, when its predecessors are in) */
static void feed_hash(mnu *u)
{
    if (!u->hash_on) return;
    while (u->hashed < u->nchunks && (u->have[u->hashed >> 3] & (1u << (u->hashed & 7u)))) {
        mnc_u32 off = u->hashed * MNU_CHUNK, n = u->size - off;
        if (n > MNU_CHUNK) n = MNU_CHUNK;
        ms_sha256_update(&u->hctx, u->buf + off, n);
        u->hashed++;
    }
}

static int is_fetch(const mnu *u) { return u->op == MNU_OP_FETCH || u->op == MNU_OP_FETCHU; }

static void on_fetch_reply(mnu *u, int s, const mnc_u8 *b, mnc_u32 n, mnc_u32 now)
{
    mnc_u32 off = u->slot[s].chunk * MNU_CHUNK, want = u->size - off, i, k;
    if (want > MNU_CHUNK) want = MNU_CHUNK;
    if (n != 10u + want || g32(b) != kseq(u) || g32(b + 4) != off || (mnc_u32) ((b[8] << 8) | b[9]) != want) { u->st_bad++; return; }
    if (elapsed(now, u->last_ok_ms) > u->st_gap_ms) u->st_gap_ms = elapsed(now, u->last_ok_ms);
    u->last_ok_ms = now;                                                 /* progress: the stall budget starts again */
    k = u->slot[s].chunk;
    if (!(u->have[k >> 3] & (1u << (k & 7u)))) {
        for (i = 0; i < want; i++) u->buf[off + i] = b[10 + i];
        u->have[k >> 3] |= (mnc_u8) (1u << (k & 7u));
        u->got++;
        feed_hash(u);
    }
    u->slot[s].used = 0;
}

int mnu_poll(mnu *u, mnc_u32 now)
{
    mnc *c = u->c;
    int i, s;
    if (u->state != MNU_BUSY) return u->state;
    for (i = 0; i < (is_fetch(u) ? MNU_RX_FETCH : MNC_RX_BUDGET); i++) {
        mnc_u8 st = 0, op;
        mw2_u32 bl = 0;
        mnc_u32 ctr;
        int n = c->io.recv(c->io.ctx, c->rx, MNC_MAX_PACKET), r;
        if (n <= 0) break;
        u->st_rx++;
        if (n < (int) MW2_HDR || n > MNC_MAX_PACKET) { u->st_bad++; continue; }
        op = (mnc_u8) (c->rx[2] & 0x7Fu);
        ctr = g32(c->rx + 8);
        if (op != u->op) { u->st_bad++; continue; }
        for (s = 0; s < MNU_WIN; s++) if (u->slot[s].used && u->slot[s].ctr == ctr) break;
        if (s == MNU_WIN) { u->st_bad++; continue; }
        r = mw2_open(&c->w, now, c->rx, (mnc_u32) n, op, ctr, &st, &bl);
        if (r == 2) { fail(u, MNU_E_NOSESSION, 0); return u->state; }         /* the server forgot the session */
        if (r == 3 && u->op == MNU_OP_FETCHU) u->st_unsigned++;                /* an unauthenticated chunk: only the final hash vouches for it */
        else if (r != 1) { u->st_bad++; continue; }
        if (st == MNU_ST_RATE_LIMITED) {                                 /* a burst above the server's bucket (a frame loop faster than about 75 per second with a window of 4): not an error */
            u->st_rate++;
            if (u->st_rate > MNU_MAX_RATE_REPLIES) { fail(u, MNU_E_TIMEOUT, st); return u->state; }
            u->slot[s].sent_ms = now - u->timeout_ms + 40u;              /* ask again in 40 ms (the bucket refills 300 per second), with a new counter, without using up the tries */
            u->slot[s].tries = 1;
            continue;
        }
        if (st != 0) { fail(u, MNU_E_SERVER, st); return u->state; }
        if (u->op == MNU_OP_CHECK) on_check_reply(u, c->rx + MW2_HDR, (mnc_u32) bl);
        else on_fetch_reply(u, s, c->rx + MW2_HDR, (mnc_u32) bl, now);
        if (u->state != MNU_BUSY) return u->state;
    }
    if (is_fetch(u) && u->got == u->nchunks) {
        if (u->hash_on) {
            mnc_u8 d[32];
            mnc_u32 i, diff = 0;
            ms_sha256_final(&u->hctx, d);
            for (i = 0; i < 32; i++) diff |= (mnc_u32) (d[i] ^ u->sha256[i]);
            if (u->hashed != u->nchunks || diff) { u->resumable = 0; fail(u, MNU_E_HASH, 0); return u->state; }      /* untrusted bytes: never kept for a resume */
            u->hash_ok = 1;
        }
        u->state = MNU_DONE;
        return u->state;
    }
    if (is_fetch(u) && elapsed(now, u->last_ok_ms) >= u->stall_ms) {    /* no valid chunk for the whole stall budget: the link is gone */
        if (elapsed(now, u->last_ok_ms) > u->st_gap_ms) u->st_gap_ms = elapsed(now, u->last_ok_ms);
        fail(u, MNU_E_TIMEOUT, 0); return u->state;
    }
    for (s = 0; s < MNU_WIN; s++) {                                     /* resend with the same counter */
        mnc_u32 wait = u->timeout_ms;
        if (is_fetch(u)) wait *= u->slot[s].tries < MNU_BACKOFF_MAX ? u->slot[s].tries : MNU_BACKOFF_MAX;      /* backoff: a stalled link is not hammered */
        if (!u->slot[s].used || elapsed(now, u->slot[s].sent_ms) < wait) continue;
        if (u->slot[s].tries >= MNU_TRIES && !is_fetch(u)) { fail(u, MNU_E_TIMEOUT, 0); return u->state; }      /* a fetch slot goes on while the stall budget lasts */
        {
            mnc_u8 t = u->slot[s].tries;
            u->st_resent++;
            if (is_fetch(u)) (void) chunk_send(u, s, u->slot[s].chunk, now);
            else {
                mnc *cc = u->c;
                cc->req[MW2_HDR] = u->slot[s].req[MW2_HDR]; cc->req[MW2_HDR + 1] = u->slot[s].req[MW2_HDR + 1];
                p32(cc->req + MW2_HDR + 2, g32(u->slot[s].req + MW2_HDR + 2));
                cc->req[MW2_HDR + 6] = 1u;
                (void) send_req(u, MNU_OP_CHECK, u->want_adv ? 7u : 6u, 0, now);
            }
            u->slot[s].tries = (mnc_u8) (t < 250u ? t + 1u : t);
        }
    }
    if (is_fetch(u))
        for (;;) {
            while (u->next < u->nchunks && (u->have[u->next >> 3] & (1u << (u->next & 7u)))) u->next++;      /* a resume skips the chunks it kept */
            if (u->next >= u->nchunks || !request_chunk(u, u->next, now)) break;
            u->next++;
        }
    return u->state;
}

void mnu_abort(mnu *u)
{
    mnc_u32 i;
    for (i = 0; i < MNU_WIN; i++) u->slot[i].used = 0;
    u->state = MNU_IDLE;
    u->resumable = 0;
}
