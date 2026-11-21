/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgr: Send Slippi Login, see sgr.h.  Strict C89. */
#include "sg_ui.h"
#include "sgr.h"

static void zero(void *p, unsigned n) { sdc_wipe(p, n); }

void sgr_init(struct sgr *s, const struct sgr_host *h)
{
    zero(s, sizeof *s);
    s->h = h;
}

static void drop_buf(struct sgr *s)
{
    if (s->buf) { zero(s->buf, SGX_BLOB_MAX + (unsigned)sizeof(struct sgx_ident)); if (s->h && s->h->free) s->h->free(s->buf); }
    s->buf = 0; s->id = 0;
}
static void drop_all(struct sgr *s)
{
    drop_buf(s);
    zero(s->priv, sizeof s->priv);
}
static void fail(struct sgr *s, unsigned err)
{
    drop_all(s);
    s->st = SGR_ERR; s->err = (unsigned char)err; s->have = 0;
}
static void abort_net(struct sgr *s)
{
    if (s->busy && s->h->net_abort) s->h->net_abort(s->h->ctx);
    s->busy = 0; s->op = 0;
}
/* CRED_DONE, one datagram, no wait (a screen that stays up goes on polling it in sgr_frame) */
static void send_done(struct sgr *s, unsigned res)
{
    unsigned char r[10];
    unsigned i;
    if (!sgx_code_ok(s->code)) return;
    abort_net(s);
    r[0] = 1;
    for (i = 0; i < 8u; i++) r[1 + i] = (unsigned char)s->code[i];
    r[9] = (unsigned char)res;
    if (s->h->net_begin(s->h->ctx, SGR_OP_DONE, r, sizeof r, s->now) == 0) { s->busy = 1; s->op = (unsigned char)SGR_OP_DONE; }
}
static unsigned map_status(int status)
{
    switch (status) {
    case 0: return SGR_E_UNREACH;
    case 2: case 18: return SGR_E_OLD;
    case 4: case 46: return SGR_E_LINK;
    case 3: return SGR_E_RATE;
    default: return SGR_E_SERVER;
    }
}

void sgr_enter(struct sgr *s, unsigned now)
{
    const struct sgr_host *h = s->h;
    abort_net(s);
    drop_all(s);
    s->now = now; s->err = SGR_E_NONE; s->leave = 0; s->saved = 0; s->have = 0; s->total = 0; s->fails = 0;
    s->code[0] = 0; s->fp[0] = 0; s->url[0] = 0; s->qr_ok = 0; s->qr_n = 0; s->name[0] = 0; s->cc[0] = 0;
    if (h && h->card && h->card_ready && h->card_ready(h->ctx)) (void)sgr_recover(h);       /* a swap that stopped half way (power cut) is finished or undone before anything new starts */
    if (!h || !h->net_up || !h->net_up(h->ctx)) { s->st = SGR_ERR; s->err = SGR_E_OFFLINE; return; }
    s->st = SGR_ENT;
}

static void start_offer(struct sgr *s)
{
    unsigned char r[33];
    unsigned i;
    sgx_pub(s->pub, s->priv);
    sgx_fingerprint(s->pub, s->fp);
    r[0] = 1;
    for (i = 0; i < 32u; i++) r[1 + i] = s->pub[i];
    if (s->h->net_begin(s->h->ctx, SGR_OP_OFFER, r, sizeof r, s->now) != 0) { fail(s, SGR_E_UNREACH); return; }
    s->busy = 1; s->op = (unsigned char)SGR_OP_OFFER; s->st = SGR_OFFER;
}

static void on_offer(struct sgr *s, const unsigned char *r, unsigned n)
{
    unsigned i, ttl;
    unsigned char qn = 0;
    unsigned len;
    if (n != 10u) { fail(s, SGR_E_SERVER); return; }
    ttl = (unsigned)r[0] << 8 | r[1];
    for (i = 0; i < 8u; i++) s->code[i] = (char)r[2 + i];
    s->code[8] = 0;
    if (ttl < 10u || ttl > 3600u || !sgx_code_ok(s->code)) { s->code[0] = 0; fail(s, SGR_E_SERVER); return; }
    s->buf = (unsigned char *)s->h->alloc(SGX_BLOB_MAX + (unsigned)sizeof(struct sgx_ident));
    if (!s->buf) { fail(s, SGR_E_MEM); return; }
    s->id = (struct sgx_ident *)(void *)(s->buf + ((SGX_BLOB_MAX + 3u) & ~3u));       /* aligned: the block is SGX_BLOB_MAX + ident, SGX_BLOB_MAX is a multiple of 4 */
    zero(s->buf, SGX_BLOB_MAX + (unsigned)sizeof(struct sgx_ident));
    len = sgx_url(s->url, sizeof s->url, s->h->site, s->code, s->pub);
    s->qr_ok = len && qr_encode(s->url, len, s->qr, &qn) == QR_OK ? 1 : 0;
    s->qr_n = s->qr_ok ? qn : 0;
    s->ttl = ttl; s->t0 = s->now; s->next_poll = s->now + SGR_POLL_MS; s->have = 0; s->total = 0; s->fails = 0;
    s->st = SGR_SHOW;
}

static void opened(struct sgr *s)
{
    sdc_u32 off = 0;
    int m = sgx_open(s->code, s->priv, s->pub, s->buf, s->total, &off);
    if (m < 0 || sgx_parse(s->buf + off, (sdc_u32)m, s->id) != 0) {
        send_done(s, 0);
        fail(s, SGR_E_BAD);
        return;
    }
    zero(s->buf, SGX_BLOB_MAX);                               /* the plaintext copy */
    zero(s->priv, sizeof s->priv);
    sgx_show(s->name, 41u, s->id->name);                       /* a row of the menu font holds 49 characters, "Name: " takes 6 */
    sgx_show(s->cc, sizeof s->cc, s->id->code);
    s->st = SGR_CONFIRM;
}

static void bad_reply(struct sgr *s) { send_done(s, 0); fail(s, SGR_E_SERVER); }

static void on_get(struct sgr *s, const unsigned char *r, unsigned n)
{
    unsigned state, total, off, cnt, i;
    if (n < 7u) { bad_reply(s); return; }
    state = r[0]; total = (unsigned)r[1] << 8 | r[2]; off = (unsigned)r[3] << 8 | r[4]; cnt = (unsigned)r[5] << 8 | r[6];
    if (7u + cnt != n || cnt > SGR_CHUNK) { bad_reply(s); return; }
    s->fails = 0;
    if (state == 0u) { s->next_poll = s->now + SGR_POLL_MS; return; }
    if (state != 1u || off != s->have) { bad_reply(s); return; }
    if (s->have == 0u) { if (total < SGX_BLOB_HDR + 1u || total > SGX_BLOB_MAX) { bad_reply(s); return; } s->total = total; }
    else if (total != s->total) { bad_reply(s); return; }
    if (cnt == 0u || s->have + cnt > s->total) { bad_reply(s); return; }
    for (i = 0; i < cnt; i++) s->buf[s->have + i] = r[7 + i];
    s->have += cnt;
    if (s->have < s->total) { s->next_poll = s->now; return; }
    opened(s);
}

static void start_get(struct sgr *s)
{
    unsigned char r[13];
    unsigned i;
    r[0] = 1;
    for (i = 0; i < 8u; i++) r[1 + i] = (unsigned char)s->code[i];
    r[9] = (unsigned char)(s->have >> 8); r[10] = (unsigned char)s->have; r[11] = (unsigned char)(SGR_CHUNK >> 8); r[12] = (unsigned char)SGR_CHUNK;
    if (s->h->net_begin(s->h->ctx, SGR_OP_GET, r, sizeof r, s->now) != 0) { s->fails++; s->next_poll = s->now + 1000u; if (s->fails >= SGR_NET_FAILS) fail(s, SGR_E_UNREACH); return; }
    s->busy = 1; s->op = (unsigned char)SGR_OP_GET;
}

static void pump(struct sgr *s)
{
    const unsigned char *r = 0;
    unsigned n = 0, op = s->op;
    int status = 0, rc;
    if (!s->busy) return;
    rc = s->h->net_poll(s->h->ctx, s->now, &r, &n, &status);
    if (rc == 0) return;
    s->busy = 0; s->op = 0;
    if (op == SGR_OP_DONE) return;                           /* best effort */
    if (rc < 0) {
        if (op == SGR_OP_OFFER) { fail(s, map_status(status)); return; }
        if (status == 16) { drop_all(s); s->st = SGR_EXPIRED; return; }      /* NOT_FOUND: the session is gone on the server */
        if (status != 0) { fail(s, map_status(status)); return; }
        s->fails++;
        s->next_poll = s->now + 1000u;
        if (s->fails >= SGR_NET_FAILS) fail(s, SGR_E_UNREACH);
        return;
    }
    if (op == SGR_OP_OFFER) on_offer(s, r, n);
    else if (op == SGR_OP_GET && s->st == SGR_SHOW) on_get(s, r, n);
}

static void write_now(struct sgr *s)
{
    int rc = sgr_card_write(s->h, s->id);
    switch (rc) {
    case SGR_C_OK:
        s->saved = 1; zero(s->id, sizeof *s->id);
        send_done(s, 1);
        s->st = SGR_DONE;
        break;
    case SGR_C_NOCARD: s->st = SGR_ERR; s->err = SGR_E_NOCARD; break;
    case SGR_C_NOROOM: s->st = SGR_ERR; s->err = SGR_E_NOROOM; break;
    default: s->st = SGR_ERR; s->err = SGR_E_CARD; break;
    }
}

void sgr_frame(struct sgr *s, unsigned edge, unsigned now)
{
    s->now = now;
    switch (s->st) {
    case SGR_ENT: {
        if (s->h->progress) s->ent_pct = (unsigned char)s->h->progress(s->h->ctx);
        if (edge & SG_B_B) { s->leave = 1; break; }
        if (s->h->rand(s->h->ctx, s->priv, 32u) == 0) start_offer(s);
        break;
    }
    case SGR_OFFER:
        pump(s);
        if (s->st == SGR_OFFER && (edge & SG_B_B)) { abort_net(s); drop_all(s); s->leave = 1; }
        break;
    case SGR_SHOW:
        if (edge & SG_B_B) { send_done(s, 2); drop_all(s); s->leave = 1; break; }
        pump(s);
        if (s->st != SGR_SHOW) break;
        if ((unsigned)(now - s->t0) >= s->ttl * 1000u) { abort_net(s); drop_all(s); s->st = SGR_EXPIRED; break; }
        if (!s->busy && (int)(now - s->next_poll) >= 0) start_get(s);
        break;
    case SGR_CONFIRM:
        pump(s);
        if (edge & SG_B_A) { s->st = SGR_SAVE; s->settle = SGR_SETTLE; }
        else if (edge & SG_B_B) { send_done(s, 2); drop_all(s); s->leave = 1; }
        break;
    case SGR_SAVE:
        pump(s);
        if (s->settle) { s->settle--; break; }
        write_now(s);
        break;
    case SGR_DONE:
        pump(s);                                              /* the CRED_DONE reply (and its repeats) */
        if (edge & (SG_B_A | SG_B_B)) s->leave = 1;
        break;
    case SGR_EXPIRED:
        if (edge & SG_B_A) sgr_enter(s, now);
        else if (edge & SG_B_B) s->leave = 1;
        break;
    case SGR_ERR:
        if (edge & SG_B_A) {
            if ((s->err == SGR_E_NOCARD || s->err == SGR_E_NOROOM || s->err == SGR_E_CARD) && s->id) { s->st = SGR_SAVE; s->settle = SGR_SETTLE; s->err = SGR_E_NONE; }
            else sgr_enter(s, now);
        } else if (edge & SG_B_B) {
            if (s->id) { send_done(s, 2); drop_all(s); }
            s->leave = 1;
        }
        break;
    default:
        break;
    }
}

void sgr_close(struct sgr *s)
{
    abort_net(s);
    drop_all(s);
    s->st = SGR_OFF;
}

/* ---- the card write: stage, read back, swap (the applier pattern of upd_card / ota_core: every cut leaves a record that recovery can use) ---- */
static int cw_err(const struct sgr_host *h, int rc)
{
    if (h->card_done) h->card_done(h->ctx);
    return rc;
}
/* Recovery of an interrupted swap (also run when the screen opens and when the overlay looks at the card): a stale staging file goes; a record under the previous name with no
   (valid) record under the real name is put back; once the real name holds a valid record the previous one goes.  Every step is one directory change, so a cut inside the recovery
   is recovered by the next run.  *b is the card buffer (valid after the first card call).  Returns 0 ok, -1 card error; *has_id = a record exists now, *restored = the old one was put back. */
static int cw_recover(const struct sgr_host *h, unsigned char **b, int *has_id, int *restored)
{
    const struct sgr_card *c = h->card;
    struct sgx_ident tmp;
    int id, nw, old, id_ok = 0, old_ok = 0, rc = -1;
    *restored = 0;
    id = c->exists(c->ctx, SGR_ID_NAME);
    *b = h->card_buf ? h->card_buf(h->ctx) : 0;
    if (id < 0 || !*b) return -1;
    nw = c->exists(c->ctx, SGR_ID_NEW);
    old = c->exists(c->ctx, SGR_ID_OLD);
    if (nw < 0 || old < 0) return -1;
    if (nw == 1 && c->del(c->ctx, SGR_ID_NEW) != 0) return -1;
    if (old == 1) {
        if (id == 1) {
            if (c->read(c->ctx, SGR_ID_NAME, 0, *b, SGR_BLOCK) != 0) goto out;
            id_ok = sgx_unpack(*b + SGX_REC_OFF, SGX_REC_LEN, &tmp) == 0;
        }
        if (id == 1 && id_ok) {
            if (c->del(c->ctx, SGR_ID_OLD) != 0) goto out;                 /* the swap had finished */
        } else {
            if (c->read(c->ctx, SGR_ID_OLD, 0, *b, SGR_BLOCK) != 0) goto out;
            old_ok = sgx_unpack(*b + SGX_REC_OFF, SGX_REC_LEN, &tmp) == 0;
            if (old_ok) {
                if (id == 1 && c->del(c->ctx, SGR_ID_NAME) != 0) goto out;   /* a damaged record under the real name */
                if (c->rename(c->ctx, SGR_ID_OLD, SGR_ID_NAME) != 0) goto out;
                id = 1; *restored = 1;
            }                                                              /* an unreadable previous file is left alone */
        }
    }
    *has_id = id;
    rc = 0;
out:
    sdc_wipe(&tmp, sizeof tmp);
    sdc_wipe(*b, SGR_BLOCK);
    return rc;
}
/* 1 = the previous record was put back, 0 = nothing to do, -1 = no card or a card error (the caller may try again later) */
int sgr_recover(const struct sgr_host *h)
{
    unsigned char *b = 0;
    int has_id = 0, restored = 0, rc;
    if (!h->card || !h->card_ready || !h->card_ready(h->ctx)) return -1;
    rc = cw_recover(h, &b, &has_id, &restored);
    (void)cw_err(h, 0);
    return rc ? -1 : restored;
}

int sgr_card_write(const struct sgr_host *h, const struct sgx_ident *id)
{
    const struct sgr_card *c = h->card;
    unsigned char *b = 0;
    sdc_u32 crc, crc2;
    int has_id = 0, restored, rc;
    unsigned i;
    if (!c || !h->card_ready || !h->card_ready(h->ctx)) return cw_err(h, SGR_C_NOCARD);
    if (cw_recover(h, &b, &has_id, &restored) != 0) return cw_err(h, SGR_C_IO);
    if (has_id == 1) {
        if (c->read(c->ctx, SGR_ID_NAME, 0, b, SGR_BLOCK) != 0) { sdc_wipe(b, SGR_BLOCK); return cw_err(h, SGR_C_IO); }
    } else {
        static const char C1[] = "Multishine", C2[] = "Identity (keep private)";
        for (i = 0; i < SGR_BLOCK; i++) b[i] = 0;
        for (i = 0; C1[i]; i++) b[i] = (unsigned char)C1[i];
        for (i = 0; C2[i]; i++) b[32 + i] = (unsigned char)C2[i];
    }
    sgx_pack(id, b + SGX_REC_OFF);
    crc = sgx_crc32(b, SGR_BLOCK, 0);
    rc = c->create(c->ctx, SGR_ID_NEW, 1u);
    if (rc != 0) { sdc_wipe(b, SGR_BLOCK); return cw_err(h, rc == 1 ? SGR_C_NOROOM : SGR_C_IO); }
    if (c->write(c->ctx, SGR_ID_NEW, 0, b, SGR_BLOCK) != 0) { sdc_wipe(b, SGR_BLOCK); (void)c->del(c->ctx, SGR_ID_NEW); return cw_err(h, SGR_C_IO); }
    sdc_wipe(b, SGR_BLOCK);
    if (c->read(c->ctx, SGR_ID_NEW, 0, b, SGR_BLOCK) != 0) { sdc_wipe(b, SGR_BLOCK); (void)c->del(c->ctx, SGR_ID_NEW); return cw_err(h, SGR_C_IO); }
    crc2 = sgx_crc32(b, SGR_BLOCK, 0);
    if (crc2 != crc) { sdc_wipe(b, SGR_BLOCK); (void)c->del(c->ctx, SGR_ID_NEW); return cw_err(h, SGR_C_VERIFY); }
    if (has_id == 1) {
        if (c->rename(c->ctx, SGR_ID_NAME, SGR_ID_OLD) != 0) { sdc_wipe(b, SGR_BLOCK); (void)c->del(c->ctx, SGR_ID_NEW); return cw_err(h, SGR_C_IO); }
        if (c->rename(c->ctx, SGR_ID_NEW, SGR_ID_NAME) != 0) {
            (void)c->rename(c->ctx, SGR_ID_OLD, SGR_ID_NAME);
            (void)c->del(c->ctx, SGR_ID_NEW);
            sdc_wipe(b, SGR_BLOCK);
            return cw_err(h, SGR_C_IO);
        }
        (void)c->del(c->ctx, SGR_ID_OLD);
    } else if (c->rename(c->ctx, SGR_ID_NEW, SGR_ID_NAME) != 0) { sdc_wipe(b, SGR_BLOCK); (void)c->del(c->ctx, SGR_ID_NEW); return cw_err(h, SGR_C_IO); }
    sdc_wipe(b, SGR_BLOCK);
    if (c->read(c->ctx, SGR_ID_NAME, 0, b, SGR_BLOCK) != 0 || sgx_crc32(b, SGR_BLOCK, 0) != crc) { sdc_wipe(b, SGR_BLOCK); return cw_err(h, SGR_C_VERIFY); }
    sdc_wipe(b, SGR_BLOCK);
    return cw_err(h, SGR_C_OK);
}

/* ---- drawing (rows <= 49 characters, menu-font glyphs) ---- */
static void put(struct sg_view *v, unsigned r, unsigned attr, const char *t)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (t[c] && c < SG_COLS) { v->t[r][c] = t[c]; c++; }
    if (t[c]) sg_cut_count++;
    v->t[r][c] = 0;
    v->attr[r] = (unsigned char)attr;
}
static void put2(struct sg_view *v, unsigned r, unsigned attr, const char *a, const char *b)
{
    char t[SG_COLS + 1];
    unsigned n = 0, i;
    for (i = 0; a[i] && n < SG_COLS; i++) t[n++] = a[i];
    for (i = 0; b && b[i] && n < SG_COLS; i++) t[n++] = b[i];
    t[n] = 0;
    put(v, r, attr, t);
}

void sgr_draw(const struct sgr *s, struct sg_view *v)
{
    const char *a = "", *b = 0, *f = "A: Try again  B: Back";
    unsigned i;
    sg_head(v, "Send Slippi login", "More - Settings - Account - Send Slippi login");
    switch (s->st) {
    case SGR_ENT: {
        char t[SG_COLS + 1], *e;
        unsigned pct = s->ent_pct > 100u ? 100u : s->ent_pct, fill = pct * 24u / 100u;
        put(v, 4, SG_A_BODY, "Input random stick and button events");      /* every line two grid rows apart (loads of space here) */
        put(v, 6, SG_A_BODY, "to create a key. Go between fast and");
        put(v, 8, SG_A_BODY, "slow, until the bar is full.");
        put(v, 10, SG_A_DIM, "This makes the key that protects your login.");
        for (i = 0; i < 24u; i++) t[i] = i < fill ? 'I' : '.';
        t[24] = ' ';
        e = sg_u(t + 25, pct); *e = 0;
        put(v, 13, SG_A_HI, t);
        put(v, SG_ROWS - 2, SG_A_DIM, "Hold B: Cancel");                 /* B taps are pool input here (a short B cancelled); sg_ui.c holds it */
        break;
    }
    case SGR_OFFER:
        put(v, 6, SG_A_BODY, "Asking the server for a code...");
        put(v, SG_ROWS - 2, SG_A_DIM, "B: Cancel");
        break;
    case SGR_SHOW: {
        char t[SG_COLS + 1], *e;
        unsigned ms = (unsigned)(s->now - s->t0), left = s->ttl * 1000u > ms ? (s->ttl * 1000u - ms + 999u) / 1000u : 0u;
        char sc[10], cd[40];
        for (i = 0; i < 4u; i++) { sc[i] = s->code[i]; sc[5u + i] = s->code[4u + i]; }
        sc[4] = '-'; sc[9] = 0;                                     /* ABCD-2345, the form the site accepts and the Link page shows */
        e = cd;
        { const char *x = "Check digits  "; while (*x) *e++ = *x++; }
        for (i = 0; s->fp[i] && i < 12u; i++) *e++ = s->fp[i];
        *e = 0;
        e = t;
        { const char *x = s->have ? "Receiving.  " : "Waiting.  "; while (*x) *e++ = *x++; }
        { const char *x = "Time left "; while (*x) *e++ = *x++; }
        e = sg_u(e, left / 60u); *e++ = ':'; if (left % 60u < 10u) *e++ = '0'; e = sg_u(e, left % 60u);
        *e = 0;
        sg_qr_page(v, s->qr_n, s->qr_ok ? s->qr : 0, "On your phone, scan the code, or go to", "multishine.link, open the QR page and enter:", sc, cd, t);
        if (!s->qr_ok) put(v, 10, SG_A_BAD, "The QR code could not be made.");
        put(v, SG_ROWS - 2, SG_A_DIM, "B: Cancel");
        break;
    }
    case SGR_CONFIRM:
        sg_head(v, "Slippi login received", "More - Settings - Account - Send Slippi login");
        put2(v, 4, SG_A_GOOD, "Name: ", s->name);
        put2(v, 6, SG_A_GOOD, "Connect code: ", s->cc);
        put(v, 9, SG_A_BODY, "Save this login on the memory card");
        put(v, 11, SG_A_BODY, "only if it is your own.");
        put(v, 14, SG_A_DIM, "It replaces the Slippi login already there.");
        put(v, SG_ROWS - 2, SG_A_DIM, "A: Save  B: Don't save");
        break;
    case SGR_SAVE:
        put(v, 6, SG_A_HI, "Saving to the memory card...");
        put(v, 9, SG_A_BAD, "Do not remove the memory card.");
        put(v, SG_ROWS - 2, SG_A_DIM, "Working...");
        break;
    case SGR_DONE:
        sg_head(v, "Slippi login saved", "More - Settings - Account - Send Slippi login");
        put(v, 4, SG_A_GOOD, "Saved to the memory card for this console.");
        put(v, 6, SG_A_DIM, "Your Slippi login stays private.");
        put(v, SG_ROWS - 2, SG_A_DIM, "A: Done");
        break;
    case SGR_EXPIRED:
        put(v, 6, SG_A_BAD, "The code expired.");
        put(v, SG_ROWS - 2, SG_A_DIM, "A: New code  B: Back");
        break;
    case SGR_ERR:
        switch (s->err) {
        case SGR_E_OFFLINE: a = "No network. Check the cable, then press A."; break;
        case SGR_E_UNREACH: a = "Couldn't reach the server."; break;
        case SGR_E_OLD: a = "This server cannot do this yet."; f = "B: Back"; break;
        case SGR_E_LINK: a = "Link this console first."; b = "Settings, Account, Link this console."; f = "B: Back"; break;
        case SGR_E_RATE: a = "Too many tries. Wait a minute, then press A."; break;
        case SGR_E_BAD: a = "The login could not be read."; b = "Nothing was saved."; break;
        case SGR_E_MEM: a = "Not enough memory for this right now."; break;
        case SGR_E_NOCARD: a = "No memory card found."; b = "Insert one in slot A or B, then press A."; f = "A: Try again  B: Cancel"; break;
        case SGR_E_NOROOM: a = "The memory card has no room for this."; b = "Free a block, then press A."; f = "A: Try again  B: Cancel"; break;
        case SGR_E_CARD: a = "The memory card write failed."; b = "Your old login is still there."; f = "A: Try again  B: Cancel"; break;
        default: a = "Unknown error. Nothing was saved."; break;
        }
        put(v, 6, SG_A_BAD, a);
        if (b) put(v, 9, SG_A_BODY, b);
        put(v, SG_ROWS - 2, SG_A_DIM, f);
        break;
    default:
        break;
    }
}
