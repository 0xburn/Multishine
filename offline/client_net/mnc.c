/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* mnc: console-side client of the Multishine server's UDP protocol.  See mnc.h.  Strict C89, no libc, no heap. */
#include "mnc.h"
#ifdef MNC_POLL_INVITE
mnc_u8 mnc_invite_status;
#endif
#ifdef MNC_WATCH_TELL
mnc_u8 mnc_watch_upload;
#endif
#ifdef MNC_RKSET
mnc_u8 mnc_rk_set;
#endif
#ifdef MNC_SEAL_CREDS   /* OFF in the console builds until the glue that owns an entropy pool calls mnc_set_seal ( status); the host tests build it on */
/*  option A: the console's ephemeral X25519 pair and the opener, set by mnc_set_seal (outside struct mnc: the struct is layout-checked).  No opener = the
   old cleartext ENROL / GUEST_START.  mnc_seal_ctx: 0 the request in flight carries no key, else 1 enrol / 2 guest (the context the reply must open under). */
static mnc_u8 seal_priv[32], seal_pub[32], mnc_seal_ctx;
static mnc_seal_open_fn seal_open;
#endif
#include "../chat/client_recog.h"

#define PH_REQ 0
#define PH_HS 1
#define PH_JIT 2                           /* the server forgot the session: waiting a random 0-1,500 ms before the new handshake ( C1) */

#define T_NONE 0
#define T_SIMPLE 1
#define T_FIND 2
#define T_LINKWAIT 3

enum { F_CANCEL = 1, F_CREATE, F_WAIT, F_POLL, F_FETCH };

/* ------------------------------------------------------------------ small helpers */
static void zero(void *p, mnc_u32 n)
{
    mnc_u8 *b = (mnc_u8 *)p;
    while (n--) *b++ = 0;
}
static void copy(void *d, const void *s, mnc_u32 n)
{
    mnc_u8 *a = (mnc_u8 *)d;
    const mnc_u8 *b = (const mnc_u8 *)s;
    while (n--) *a++ = *b++;
}
static mnc_u32 slen(const char *s)
{
    mnc_u32 n = 0;
    while (s[n]) n++;
    return n;
}
static void scopy(char *d, mnc_u32 cap, const char *s)      /* truncating copy, always NUL terminated */
{
    mnc_u32 i = 0;
    while (i + 1 < cap && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static int seq_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
/* elapsed milliseconds, 0 if the clock went backwards */
static mnc_u32 elapsed(mnc_u32 now, mnc_u32 then)
{
    mnc_u32 d = now - then;
    return (d & 0x80000000u) ? 0u : d;
}
static int reached(mnc_u32 now, mnc_u32 at)               /* wrap-safe now >= at */
{
    return ((now - at) & 0x80000000u) == 0;
}
static mnc_u32 be32(const mnc_u8 *p)
{
    return ((mnc_u32)p[0] << 24) | ((mnc_u32)p[1] << 16) | ((mnc_u32)p[2] << 8) | p[3];
}
static int default_drawable(mnc_u8 c)
{
    return c >= 0x20 && c <= 0x7E && c != '"' && c != '\\' && c != '`';
}
static int drawable(const mnc *m, mnc_u8 c)
{
    return m->drawable ? m->drawable(c) : default_drawable(c);
}

/* ------------------------------------------------------------------ bounded reader / writer */
typedef struct { const mnc_u8 *p; mnc_u32 n, i; int bad; } rd;
typedef struct { mnc_u8 *p; mnc_u32 n, cap; int bad; } wr;

static void rd_init(rd *r, const mnc_u8 *p, mnc_u32 n) { r->p = p; r->n = n; r->i = 0; r->bad = 0; }
static const mnc_u8 *rd_take(rd *r, mnc_u32 k)
{
    const mnc_u8 *q;
    if (r->bad || k > r->n - r->i) { r->bad = 1; return 0; }
    q = r->p + r->i;
    r->i += k;
    return q;
}
static mnc_u32 rd_u8(rd *r) { const mnc_u8 *q = rd_take(r, 1); return q ? q[0] : 0; }
static mnc_u32 rd_u16(rd *r) { const mnc_u8 *q = rd_take(r, 2); return q ? (mnc_u32)((q[0] << 8) | q[1]) : 0; }
static mnc_u32 rd_u32(rd *r) { const mnc_u8 *q = rd_take(r, 4); return q ? be32(q) : 0; }
/* u8 length + printable ASCII, at most maxlen (out holds maxlen + 1) */
static void rd_pstr(rd *r, char *out, mnc_u32 maxlen)
{
    mnc_u32 n = rd_u8(r), i;
    const mnc_u8 *q;
    out[0] = 0;
    if (r->bad) return;
    if (n > maxlen) { r->bad = 1; return; }
    q = rd_take(r, n);
    if (!q) return;
    for (i = 0; i < n; i++) {
        if (q[i] < 0x20 || q[i] > 0x7E) { r->bad = 1; out[0] = 0; return; }
        out[i] = (char)q[i];
    }
    out[n] = 0;
}
static int rd_done(const rd *r) { return !r->bad && r->i == r->n; }

static void wr_u8(wr *w, mnc_u32 v) { if (w->n >= w->cap) { w->bad = 1; return; } w->p[w->n++] = (mnc_u8)v; }
static void wr_u16(wr *w, mnc_u32 v) { wr_u8(w, v >> 8); wr_u8(w, v); }
static void wr_u32(wr *w, mnc_u32 v) { wr_u16(w, v >> 16); wr_u16(w, v); }
static void wr_bytes(wr *w, const mnc_u8 *b, mnc_u32 k) { while (k--) wr_u8(w, *b++); }
static void wr_pstr(wr *w, const char *s, mnc_u32 maxlen)
{
    mnc_u32 n = slen(s);
    if (n > maxlen) { w->bad = 1; return; }
    wr_u8(w, n);
    wr_bytes(w, (const mnc_u8 *)s, n);
}

/* ------------------------------------------------------------------ hashes, sanitising, records */
mnc_u32 mnc_crc32(const mnc_u8 *p, mnc_u32 len)
{
    mnc_u32 c = 0xFFFFFFFFu, i;
    int k;
    for (i = 0; i < len; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
    }
    return ~c;
}
mnc_u32 mnc_slot_hash(int index, const char *text)
{
    mnc_u8 b[1 + MNC_MAXC];
    mnc_u32 n = slen(text);
    if (n > MNC_MAXC) n = MNC_MAXC;
    b[0] = (mnc_u8)index;
    copy(b + 1, text, n);
    return mnc_crc32(b, 1 + n);
}
mnc_u32 mnc_list_hash(const char t[MNC_N][MNC_TXT])
{
    mnc_u8 b[4 * MNC_N];
    int i;
    for (i = 0; i < MNC_N; i++) {
        mnc_u32 h = mnc_slot_hash(i, t[i]);
        b[4 * i] = (mnc_u8)(h >> 24); b[4 * i + 1] = (mnc_u8)(h >> 16); b[4 * i + 2] = (mnc_u8)(h >> 8); b[4 * i + 3] = (mnc_u8)h;
    }
    return mnc_crc32(b, sizeof b);
}

/* chat_texts.sanitize: drop undrawable bytes, squeeze and trim spaces, cut at 25.  Returns 1 when the result equals the input. */
static int sanitize(const mnc *m, const mnc_u8 *in, mnc_u32 n, char out[MNC_TXT])
{
    mnc_u32 i, k = 0;
    for (i = 0; i < n && k < MNC_MAXC; i++) {
        mnc_u8 c = in[i];
        if (c >= 0x80 || !drawable(m, c)) continue;
        if (c == ' ' && (k == 0 || out[k - 1] == ' ')) continue;
        out[k++] = (char)c;
    }
    while (k > 0 && out[k - 1] == ' ') k--;
    out[k] = 0;
    if (k != n) return 0;
    for (i = 0; i < n; i++) if ((mnc_u8)out[i] != in[i]) return 0;
    return 1;
}
/* sanitise into slot i of a list; an empty result takes the default for that slot.  Returns 1 if unchanged, 0 if changed */
static int fill_slot(const mnc *m, char dst[MNC_TXT], int i, const mnc_u8 *in, mnc_u32 n)
{
    int clean = sanitize(m, in, n, dst);
    if (dst[0] == 0) scopy(dst, MNC_TXT, m->defaults[i]);
    return clean;
}

int mnc_record_load(mnc *m, const mnc_u8 *blob, mnc_u32 len, char out[MNC_N][MNC_TXT])
{
    int i, repaired = 0, rc;
    const mnc_u8 *body;
    for (i = 0; i < MNC_N; i++) scopy(out[i], MNC_TXT, m->defaults[i]);
    if (!blob || len == 0) return 1;
    if (len < MNC_RECORD_SIZE) return 1;
    if (blob[0] != 'S' || blob[1] != 'D' || blob[2] != 'C' || blob[3] != 'T') return 2;
    if (blob[4] != 1 || blob[5] != MNC_N) return 3;
    body = blob + 12;
    if (mnc_crc32(body, MNC_N * MNC_TXT) != be32(blob + 8)) return 4;
    for (i = 0; i < MNC_N; i++) {
        const mnc_u8 *raw = body + i * MNC_TXT;
        char tmp[MNC_TXT];
        int ok = 0;
        if (raw[MNC_TXT - 1] == 0) {
            mnc_u32 n = 0;
            while (raw[n]) n++;
            ok = sanitize(m, raw, n, tmp) && tmp[0] != 0;
        }
        if (ok) scopy(out[i], MNC_TXT, tmp); else repaired++;
    }
    rc = repaired ? 100 + repaired : 0;
    return rc;
}
void mnc_record_save(const mnc *m, const char t[MNC_N][MNC_TXT], mnc_u8 out[MNC_RECORD_SIZE])
{
    int i, custom = 0;
    mnc_u32 crc, k;
    zero(out, MNC_RECORD_SIZE);
    for (i = 0; i < MNC_N; i++) {
        for (k = 0; k < MNC_MAXC && t[i][k]; k++) out[12 + i * MNC_TXT + k] = (mnc_u8)t[i][k];
        if (!seq_eq(t[i], m->defaults[i])) custom = 1;
    }
    out[0] = 'S'; out[1] = 'D'; out[2] = 'C'; out[3] = 'T'; out[4] = 1; out[5] = MNC_N;
    out[6] = 0; out[7] = (mnc_u8)(custom ? 1 : 0);
    crc = mnc_crc32(out + 12, MNC_N * MNC_TXT);
    out[8] = (mnc_u8)(crc >> 24); out[9] = (mnc_u8)(crc >> 16); out[10] = (mnc_u8)(crc >> 8); out[11] = (mnc_u8)crc;
}

/* ------------------------------------------------------------------ cache of other players' lists */
static int cache_find(const mnc *m, mnc_u32 key)
{
    int i;
    for (i = 0; i < MNC_CACHE; i++) if (m->cache[i].age && m->cache[i].key == key) return i;
    return -1;
}
static void cache_touch(mnc *m, int i)
{
    m->age_clock++;
    if (m->age_clock == 0) m->age_clock = 1;       /* 0 means an empty slot */
    m->cache[i].age = m->age_clock;
}
static int cache_put(mnc *m, mnc_u32 key, const char t[MNC_N][MNC_TXT])
{
    int i = cache_find(m, key), k, j;
    if (i < 0) {
        i = 0;
        for (k = 0; k < MNC_CACHE; k++) {
            if (m->cache[k].age == 0) { i = k; break; }
            if (m->cache[k].age < m->cache[i].age) i = k;
        }
    }
    m->cache[i].key = key;
    for (j = 0; j < MNC_N; j++) scopy(m->cache[i].t[j], MNC_TXT, t[j]);
    cache_touch(m, i);
    return i;
}
/* the hashes to advertise: most recently used first.  Returns the count (<= MNC_CACHE) */
static int cache_have(const mnc *m, mnc_u32 out[MNC_CACHE])
{
    int n = 0, i, j;
    mnc_u32 ages[MNC_CACHE];
    for (i = 0; i < MNC_CACHE; i++) {
        if (!m->cache[i].age) continue;
        j = n;
        while (j > 0 && ages[j - 1] < m->cache[i].age) { ages[j] = ages[j - 1]; out[j] = out[j - 1]; j--; }
        ages[j] = m->cache[i].age; out[j] = m->cache[i].key;
        n++;
    }
    return n;
}

const char (*mnc_player_texts(const mnc *m, int i))[MNC_TXT]
{
    int c;
    if (i < 0 || i >= (int)m->nplayers) return m->defaults;
    if (m->pl[i].chat_hash == m->def_hash) return m->defaults;
    c = cache_find(m, m->pl[i].chat_hash);
    return c < 0 ? m->defaults : (const char (*)[MNC_TXT])m->cache[c].t;
}

/* ------------------------------------------------------------------ setup */
void mnc_init(mnc *m, const mnc_io *io, const char defaults[MNC_N][MNC_TXT], mnc_u32 build_id, mnc_u32 codeset, mnc_u32 rules, mnc_u32 seq_seed)
{
    int i;
    mnc_u8 gst = 0, gsets = 0;
    char groom[sizeof m->gn_room];
    if (m->gn_magic == MNC_GN_MAGIC) { gst = m->gn_state; gsets = m->gn_sets; copy(groom, m->gn_room, sizeof groom); }   /* a reload keeps a hand-off in flight */
    else zero(groom, sizeof groom);
    zero(m, sizeof *m);
    m->gn_magic = MNC_GN_MAGIC; m->gn_state = gst; m->gn_sets = gsets; copy(m->gn_room, groom, sizeof groom);
    copy(&m->io, io, sizeof m->io);
    m->timeout_ms = MNC_DEFAULT_TIMEOUT_MS;
    m->max_tries = MNC_DEFAULT_TRIES;
    m->version[0] = (mnc_u8)((build_id >> 24) & 0xFF); m->version[1] = (mnc_u8)((build_id >> 16) & 0xFF); m->version[2] = (mnc_u8)((build_id >> 8) & 0xFF);
    m->platform = 1;
    m->build_id = build_id; m->codeset = codeset; m->rules = rules;
    m->caps = (mnc_u16)(CR_CAP_L1_PRESETS | CR_CAP_FREE_TEXT);
    mw2_init(&m->w);
    m->rng = (seq_seed ^ build_id) * 2654435761u + 1u;
    {
        mnc_u8 sd[8];
        sd[0] = (mnc_u8)(seq_seed >> 24); sd[1] = (mnc_u8)(seq_seed >> 16); sd[2] = (mnc_u8)(seq_seed >> 8); sd[3] = (mnc_u8)seq_seed;
        sd[4] = (mnc_u8)(build_id >> 24); sd[5] = (mnc_u8)(build_id >> 16); sd[6] = (mnc_u8)(build_id >> 8); sd[7] = (mnc_u8)build_id;
        mw2_seed(&m->w, sd, 8);
    }
    for (i = 0; i < MNC_N; i++) {
        if (defaults) scopy(m->defaults[i], MNC_TXT, defaults[i]); else scopy(m->defaults[i], MNC_TXT, ".");
        scopy(m->own[i], MNC_TXT, m->defaults[i]);
        scopy(m->fetched[i], MNC_TXT, m->defaults[i]);
    }
    m->def_hash = mnc_list_hash((const char (*)[MNC_TXT])m->defaults);
    m->own_hash = m->def_hash;
}
void mnc_set_identity(mnc *m, const mnc_u8 dev[8], const mnc_u8 tok[16], const char *code, const char *name)
{
    copy(m->dev, dev, 8); copy(m->tok, tok, 16);
    scopy(m->code, MNC_STR, code ? code : ""); scopy(m->name, MNC_STR, name ? name : "");
    m->have_id = 1;
}
void mnc_clear_identity(mnc *m)
{
    zero(m->dev, 8); zero(m->tok, 16); m->code[0] = 0; m->name[0] = 0; m->have_id = 0;
}
void mnc_set_server_keys(mnc *m, const mnc_u8 *keys, int n)
{
    mw2_set_pins(&m->w, keys, n);
}
void mnc_set_net(mnc *m, mnc_u32 own_ip, mnc_u32 own_mask, mnc_u32 server_ip, int lax)
{
    m->own_ip = own_ip; m->own_mask = own_mask; m->srv_ip = server_ip;
#ifdef MNC_LAB_SAMENAT
    m->lax_addr = (mnc_u8)((lax & MNC_LAX_ALL) != 0);                 /* LAB build: bit 0 = lab on one host (host tests), bit 1 = server and opponent behind one NAT */
    m->lab_samenat = (mnc_u8)((lax & MNC_LAX_SAMENAT) != 0);
#else
    m->lax_addr = (mnc_u8)((lax & MNC_LAX_ALL) != 0);                 /* release: only bit 0 (lab on one host) exists; the same-NAT bit is ignored, the check stays */
#endif
}
void mnc_seed_entropy(mnc *m, const mnc_u8 *data, mnc_u32 len)
{
    mw2_seed(&m->w, data, len);
}
/* jitter for retries (not a secret): xorshift32, seeded from the console's seed in mnc_init and mixed with every use */
mnc_u32 mnc_rand(mnc *m)
{
    mnc_u32 x = m->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    m->rng = x ? x : 0x9E3779B9u;
    return x;
}
mnc_u32 mnc_jitter(mnc *m, mnc_u32 base_ms)                 /* base +-20% */
{
    mnc_u32 d = base_ms / 5u;
    return base_ms - d + mnc_rand(m) % (2u * d + 1u);
}
int mnc_busy_hint(int status)
{
    switch (status) {
    case MNC_ST_BUSY_10: return 10;
    case MNC_ST_BUSY_30: case MNC_ST_SERVER_FULL: return 30;
    case MNC_ST_BUSY_120: return 120;
    default: return 0;
    }
}
int mnc_set_texts(mnc *m, const char texts[MNC_N][MNC_TXT])
{
    int i, changed = 0;
    for (i = 0; i < MNC_N; i++) {
        mnc_u8 raw[MNC_TXT];
        mnc_u32 n = 0;
        while (n < MNC_MAXC && texts[i][n]) { raw[n] = (mnc_u8)texts[i][n]; n++; }
        if (!fill_slot(m, m->own[i], i, raw, n)) changed++;
    }
    m->own_hash = mnc_list_hash((const char (*)[MNC_TXT])m->own);
    return changed;
}

/* ------------------------------------------------------------------ request building and the retry machine */
static void use_creds(mnc *m, mnc_u8 op)
{
    if (m->have_id && op != MNC_OP_ENROL && op != MNC_OP_LINK_START && op != MNC_OP_LINK_POLL && op != MNC_OP_GUEST_START) mw2_use(&m->w, m->dev, m->tok);
    else mw2_use(&m->w, 0, 0);
}
static void req_begin(mnc *m, mnc_u8 op, wr *w)
{
    m->op = op;
    use_creds(m, op);
    w->p = m->req; w->n = MW2_HDR; w->cap = MW2_HDR + MW2_MAX_PAYLOAD; w->bad = 0;     /* the header and the tag are added when the request is sealed */
}
static void put_client(const mnc *m, wr *w)
{
    wr_u8(w, m->version[0]); wr_u8(w, m->version[1]); wr_u8(w, m->version[2]); wr_u8(w, CR_PROTO); wr_u8(w, m->platform);
    wr_u16(w, m->caps); wr_u32(w, m->codeset); wr_u32(w, m->rules);
}
static void put_have(const mnc *m, wr *w)
{
    mnc_u32 h[MNC_CACHE];
    int n = cache_have(m, h), i;
    wr_u8(w, n);
    for (i = 0; i < n; i++) wr_u32(w, h[i]);
}
static void transmit(mnc *m)
{
    if (m->phase == PH_HS) (void)m->io.send(m->io.ctx, m->w.hs1, MW2_HS1_LEN);
    else (void)m->io.send(m->io.ctx, m->req, m->req_len);
    m->st_sent++;
}
/* the session is ready: seal the pending payload (header + tag) and start the request's own retry budget */
static void seal_and_send(mnc *m, mnc_u32 now)
{
    m->req_len = (mnc_u16)mw2_seal(&m->w, m->req, m->op, m->pay_len);
    m->phase = PH_REQ; m->tries = 1; m->sent_ms = now;
    transmit(m);
}
static void start_hs(mnc *m, mnc_u32 now)
{
    (void)mw2_hs1(&m->w, now);
    m->phase = PH_HS; m->tries = 1; m->sent_ms = now;
    transmit(m);
}
/* send the request just built (state BUSY, task as given) */
static int req_send(mnc *m, mnc_u32 now, wr *w)
{
    if (w->bad || m->w.npin == 0) { m->state = MNC_IDLE; m->inflight = 0; return MNC_E_ARG; }     /* no pinned server key: nothing may be sent */
    m->pay_len = (mnc_u16)(w->n - MW2_HDR);
    m->inflight = 1; m->bad_seen = 0; m->rehs = 0;
    if (mw2_ready(&m->w, now)) seal_and_send(m, now); else start_hs(m, now);
    return 0;
}
static void begin_task(mnc *m, int task)
{
    m->state = MNC_BUSY; m->task = (mnc_u8)task; m->err = MNC_E_NONE; m->status = 0; m->inflight = 0;
}
static void finish_ok(mnc *m)
{
    m->state = MNC_DONE; m->task = T_NONE; m->inflight = 0; m->err = MNC_E_NONE; m->status = 0;
}
static void finish_fail(mnc *m, int err, int status)
{
    if (m->task == T_FIND) { m->nplayers = 0; m->my_slot = 0; m->is_host = 0; }   /* a failed find leaves no match behind (an accepted ticket whose later step failed) */
    m->state = MNC_FAILED; m->task = T_NONE; m->inflight = 0; m->err = err; m->status = status;
}
static int can_start(const mnc *m) { return m->state == MNC_BUSY ? MNC_E_BUSY : 0; }

/* a simple (single request) operation: build with `build`, run as T_SIMPLE */
static int simple(mnc *m, mnc_u32 now, wr *w)
{
    int rc;
    begin_task(m, T_SIMPLE);
    rc = req_send(m, now, w);
    if (rc) { m->err = rc; m->state = MNC_IDLE; }
    return rc;
}

int mnc_hello(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    req_begin(m, MNC_OP_HELLO, &w); put_client(m, &w); wr_u8(&w, MNC_HELLO_EXT_CAPS2);   /* additive: an older server answers without the caps2 byte */
    return simple(m, now, &w);
}
int mnc_whoami(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    req_begin(m, MNC_OP_WHOAMI, &w);
    return simple(m, now, &w);
}
static int printable_ok(const char *s, mnc_u32 maxlen)
{
    mnc_u32 i, n = slen(s);
    if (n > maxlen) return 0;
    for (i = 0; i < n; i++) if ((mnc_u8)s[i] < 0x20 || (mnc_u8)s[i] > 0x7E) return 0;
    return 1;
}
#ifdef MNC_SEAL_CREDS
void mnc_set_seal(const mnc_u8 priv[32], const mnc_u8 pub[32], mnc_seal_open_fn open_fn)
{
    if (!open_fn || !priv || !pub) { zero(seal_priv, 32); zero(seal_pub, 32); seal_open = 0; return; }
    copy(seal_priv, priv, 32); copy(seal_pub, pub, 32); seal_open = open_fn;
}
/* the optional trailing key: only when a key is set AND the server's HELLO announced MNC_SRV_CAP_SEAL (an older server would refuse the longer request) */
static void put_seal_key(mnc *m, wr *w, mnc_u8 ctx)
{
    mnc_seal_ctx = 0;
    if (seal_open && m->have_hello && (m->srv_caps & MNC_SRV_CAP_SEAL)) { wr_bytes(w, seal_pub, 32); mnc_seal_ctx = ctx; }
}
#else
#define put_seal_key(m, w, c) ((void)0)
#endif
int mnc_enrol(mnc *m, mnc_u32 now, const char *wanted_name, const char *tag)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    if (!wanted_name) wanted_name = "";
    if (!tag) tag = "";
    if (!printable_ok(wanted_name, 12) || !printable_ok(tag, 6)) return MNC_E_ARG;
    req_begin(m, MNC_OP_ENROL, &w); put_client(m, &w); wr_pstr(&w, wanted_name, 12); wr_pstr(&w, tag, 6); put_seal_key(m, &w, 1);
    return simple(m, now, &w);
}
int mnc_guest(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    req_begin(m, MNC_OP_GUEST_START, &w); put_client(m, &w); put_seal_key(m, &w, 2);
    return simple(m, now, &w);
}
int mnc_link_start(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    m->link_expires = 0;
    req_begin(m, MNC_OP_LINK_START, &w); put_client(m, &w);
    return simple(m, now, &w);
}
static int send_link_poll(mnc *m, mnc_u32 now)
{
    wr w;
    req_begin(m, MNC_OP_LINK_POLL, &w); wr_bytes(&w, m->link_dev, 16);
    return req_send(m, now, &w);
}
int mnc_link_wait(mnc *m, mnc_u32 now)
{
    int rc = can_start(m);
    if (rc) return rc;
    if (m->link_expires == 0) return MNC_E_ARG;
    begin_task(m, T_LINKWAIT);
    m->l_interval_ms = (mnc_u32)(m->link_interval ? m->link_interval : 1) * 1000u;
    rc = send_link_poll(m, now);
    if (rc) { m->err = rc; m->state = MNC_IDLE; }
    return rc;
}
#define NEED_ID(m) do { if (!(m)->have_id) return MNC_E_ARG; } while (0)
int mnc_profile(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_PROFILE, &w); 
    return simple(m, now, &w);
}
int mnc_chat_set(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m), i;
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_CHAT_SET, &w); 
    for (i = 0; i < MNC_N; i++) wr_pstr(&w, m->own[i], MNC_MAXC);
    return simple(m, now, &w);
}
int mnc_chat_get(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_CHAT_GET, &w); 
    return simple(m, now, &w);
}
int mnc_logout(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_LOGOUT, &w); 
    return simple(m, now, &w);
}
int mnc_room_reserve(mnc *m, mnc_u32 now, const char *room, const char *guest, mnc_u16 ttl)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    if (!guest) guest = "";
    if (!room || !printable_ok(room, 18) || !printable_ok(guest, 14)) return MNC_E_ARG;
    req_begin(m, MNC_OP_ROOM_RESERVE, &w); wr_pstr(&w, room, 18); wr_pstr(&w, guest, 14); wr_u16(&w, ttl);
    return simple(m, now, &w);
}
int mnc_room_release(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_ROOM_RELEASE, &w); 
    return simple(m, now, &w);
}
int mnc_game_report(mnc *m, mnc_u32 now, const mnc_u8 *blob, mnc_u32 len)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    if (!blob || len == 0 || len > 128) return MNC_E_ARG;
    req_begin(m, MNC_OP_GAME_REPORT, &w); wr_bytes(&w, m->match_id, 8); wr_bytes(&w, blob, len);
    return simple(m, now, &w);
}
int mnc_game_record(mnc *m, mnc_u32 now, const mnc_u8 *blob, mnc_u32 len)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    if (!blob || len < MNC_GAME_RECORD_LEN || len > MNC_GAME_RECORD_MAX) return MNC_E_ARG;
    req_begin(m, MNC_OP_GAME_RECORD, &w); wr_bytes(&w, blob, len);
    return simple(m, now, &w);
}
int mnc_lab_diag(mnc *m, mnc_u32 now, const mnc_u8 *blob, mnc_u32 len)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    if (!blob || !len || len > MNC_LAB_DIAG_MAX) return MNC_E_ARG;
    req_begin(m, MNC_OP_LAB_DIAG, &w); wr_bytes(&w, blob, len);
    return simple(m, now, &w);
}
int mnc_match_status(mnc *m, mnc_u32 now, mnc_u8 code)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_MATCH_STATUS, &w); wr_bytes(&w, m->match_id, 8); wr_u8(&w, code);
    return simple(m, now, &w);
}
int mnc_cancel(mnc *m, mnc_u32 now)
{
    wr w;
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    req_begin(m, MNC_OP_TICKET_CANCEL, &w); 
    return simple(m, now, &w);
}
void mnc_abort(mnc *m)
{
    m->state = MNC_IDLE; m->task = T_NONE; m->inflight = 0; m->err = MNC_E_NONE;
}

/* ---- find ---- */
/* CONNECT FAILED note: the match id of the last pairing whose peer connection failed, appended to the NEXT ticket (after a seats byte of 2) so the server keeps that pair apart in the queues.
   Static, not in struct mnc (the struct is layout-checked); set by mnc_note_connect_failed before mnc_find, cleared by the caller after it (the request bytes are already built). */
static mnc_u8 failed_mid[8], failed_set;
static const mnc_u32 *why_rec;                                   /* the caller's 8 words (kept by the caller until the ticket is built) */
void mnc_note_connect_failed(const mnc_u8 *id)
{
    int i;
    failed_set = id ? 1 : 0;
    for (i = 0; i < 8; i++) failed_mid[i] = id ? id[i] : 0;
    if (!id) why_rec = 0;
}
void mnc_note_why(const mnc_u32 *v) { why_rec = v; }
mnc_u8 mnc_link_cls, mnc_link_pref;                                  /*  9: see mnc.h */
mnc_u8 mnc_region_chip;                                            /* CSS region chip: 0 Auto, 1..7 NAE NAW EU LA AS OC NAC; static for the same reason as failed_mid */
static int send_ticket_create(mnc *m, mnc_u32 now, int mode, const char *target, mnc_u16 gport, mnc_u32 lan_ip, mnc_u16 lan_port)
{
    wr w;
    req_begin(m, MNC_OP_TICKET_CREATE, &w); put_client(m, &w);
    wr_u8(&w, mode); wr_pstr(&w, target, 18); wr_u16(&w, gport); wr_u32(&w, lan_ip); wr_u16(&w, lan_port);
    put_have(m, &w);
    if (failed_set) { int i; wr_u8(&w, 2); for (i = 0; i < 8; i++) wr_u8(&w, failed_mid[i]);   /* seats 2 + the failed match id (additive trailing fields) */
        if (why_rec && (m->srv_caps2 & MNC_SRV_CAPS2_WHYFAIL)) for (i = 0; i < 8; i++) wr_u32(&w, why_rec[i]); }   /* + the why-it-failed record (32 bytes; only to a server that reads it) */
#ifdef MNC_RKSET
    if (mode == MNC_MODE_RANKED) {                                   /* Grandmaster sets: the trailing ext byte (after the seats byte, behind the failed id when there is one): bit 0 = this console can act in a ranked set */
        if (!failed_set) wr_u8(&w, 2);
        wr_u8(&w, mnc_region_chip ? (mnc_u8)(MNC_TICKET_EXT_SET | MNC_TICKET_EXT_REGION) : MNC_TICKET_EXT_SET);
        if (mnc_region_chip) wr_u8(&w, (mnc_u8)(mnc_region_chip - 1u));      /* the search's region byte (ext bit 1): an older server would refuse the longer request, so Auto (0) sends nothing */
    }
#endif
    if (mode == MNC_MODE_UNRANKED && m->have_hello && (m->srv_caps2 & MNC_SRV_CAPS2_LINKPREF) && (mnc_link_cls || mnc_link_pref)) {   /* the LINK extension: seats 2 (when the failed id did not send it), ext bit 2, class, preference; an older server never sees it (no caps2 bit 4) */
        if (!failed_set) wr_u8(&w, 2);
        wr_u8(&w, MNC_TICKET_EXT_LINK); wr_u8(&w, mnc_link_cls); wr_u8(&w, mnc_link_pref);
    }
    return req_send(m, now, &w);
}
static void send_ticket_poll(mnc *m, mnc_u32 now)
{
    wr w;
    mnc_u8 ext = 0, ps = 0;
    req_begin(m, MNC_OP_TICKET_POLL, &w); put_have(m, &w);
#ifdef MNC_SETUP_BLOCK
    ext |= 2u;                                                       /* ext bit 1: append the match setup block (an older server ignores the bit) */
#endif
#ifdef MNC_POLL_INVITE
    if (m->f_mode == MNC_MODE_DIRECT) ext |= 4u;                     /* ext bit 2: a queued reply carries the challenge status */
#endif
#ifdef MNC_WATCH_TELL
    ext |= 8u;                                                       /* ext bit 3: the assigned reply ends with watch u8 (1 = upload this match with GN_REC); an older server ignores the bit */
#endif
    if (m->gn_magic == MNC_GN_MAGIC && m->gn_room[0] && (((mnc_u8)m->gn_room[MNC_GN_FLAGS_AT]) & (MNC_GN_ZON | MNC_GN_ZERR))) {      /* a Game Night set of a room type with a code patch (Low-Mid Fun): tell the server whether this console's patch is on */
        ext |= 64u;
        ps = (((mnc_u8)m->gn_room[MNC_GN_FLAGS_AT]) & MNC_GN_ZERR) ? 2u : 1u;
    }
#ifdef MNC_RKSET
    if (m->f_mode == MNC_MODE_RANKED) ext |= MNC_POLL_EXT_SET;       /* ext bit 4 (Grandmaster sets): the assigned reply ends with gmSet u8 (0 = a best of 1, else the games of the set), after the watch byte */
#endif
    if (ext) wr_u8(&w, ext);
    if (ext & 64u) wr_u8(&w, ps);                                    /* ext bit 6: the patch state byte follows the ext byte (1 on, 2 refused); an older server refuses the longer request, which it only gets in a patched room type */
    (void)req_send(m, now, &w);
    m->f_stage = F_POLL;
}
int mnc_find(mnc *m, mnc_u32 now, int mode, const char *target, mnc_u16 game_port, mnc_u32 lan_ip, mnc_u16 lan_port)
{
    int rc = can_start(m);
    if (rc) return rc;
    NEED_ID(m);
    if (!target) target = "";
    if (!printable_ok(target, 18) || mode < 0 || mode > 255) return MNC_E_ARG;
    begin_task(m, T_FIND);
    m->f_mode = (mnc_u8)mode; scopy(m->f_target, sizeof m->f_target, target); m->f_gport = game_port; m->f_lan_ip = lan_ip; m->f_lan_port = lan_port; m->f_retried = 0;
    m->nplayers = 0; m->my_slot = 0; m->is_host = 0; m->f_next = 0; m->f_deadline = now + MNC_FIND_MAX_MS;
#ifdef MNC_SETUP_BLOCK
    m->su_have = 0;
#endif
    m->f_stage = F_CREATE;
#ifdef MNC_POLL_INVITE
    mnc_invite_status = 0;
#endif
#ifdef MNC_WATCH_TELL
    mnc_watch_upload = 0;
    if (m->gn_magic == MNC_GN_MAGIC && !m->gn_room[0]) m->gn_room[MNC_UPLOAD_AT] = 0;
#endif
#ifdef MNC_RKSET
    mnc_rk_set = 0;
#endif
    rc = send_ticket_create(m, now, mode, target, game_port, lan_ip, lan_port);
    if (rc) { m->err = rc; m->state = MNC_IDLE; }
    return rc;
}

/* next player whose list we do not have; sends the fetch and returns 1, or 0 when all are resolved */
static int fetch_next(mnc *m, mnc_u32 now)
{
    wr w;
    while ((int)m->f_next < (int)m->nplayers) {
        mnc_player *p = &m->pl[m->f_next];
        int c;
        if (p->slot == m->my_slot || p->chat_hash == m->def_hash) { m->f_next++; continue; }
        c = cache_find(m, p->chat_hash);
        if (c >= 0) { cache_touch(m, c); m->f_next++; continue; }
        {
            mnc_u32 h[MNC_CACHE];
            int n = cache_have(m, h);
            m->f_fetch_base = n ? h[0] : 0;
        }
        req_begin(m, MNC_OP_CHAT_FETCH, &w); wr_bytes(&w, m->match_id, 8); wr_u8(&w, p->slot); wr_u32(&w, m->f_fetch_base);
        (void)req_send(m, now, &w);
        m->f_stage = F_FETCH;
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ reply parsing (untrusted) */
static int parse_creds(mnc *m, rd *r, int set)
{
    char code[MNC_STR], name[MNC_STR];
    mnc_u8 dev[8], tok[16], sid[8];
    const mnc_u8 *q;
    rd_pstr(r, code, MNC_STR - 1); rd_pstr(r, name, MNC_STR - 1);
    q = rd_take(r, 8); if (q) copy(dev, q, 8);
    q = rd_take(r, 16); if (q) copy(tok, q, 16);
    q = rd_take(r, 8); if (q) copy(sid, q, 8);
    if (!rd_done(r)) return 0;
    if (set) { mnc_set_identity(m, dev, tok, code, name); copy(m->server_id, sid, 8); }
    return 1;
}

/* the chat entry of CHAT_FETCH.  1 = list resolved, 0 = refused (the player keeps the defaults), -1 = malformed packet */
static int parse_chat_entry(mnc *m, const mnc_u8 *body, mnc_u32 len)
{
    rd r;
    mnc_u32 kind, h, i;
    char t[MNC_N][MNC_TXT];
    int clean = 1, c;
    rd_init(&r, body, len);
    kind = rd_u8(&r); h = rd_u32(&r);
    if (r.bad) return -1;
    if (kind == 0) {
        if (!rd_done(&r)) return -1;
        c = cache_find(m, h);
        if (c < 0) return 0;
        cache_touch(m, c);
        m->st_chat[0]++;
        return 1;
    }
    if (kind == 2) {
        for (i = 0; i < MNC_N; i++) {
            char raw[MNC_TXT];
            rd_pstr(&r, raw, MNC_MAXC);
            if (r.bad) return -1;
            if (!fill_slot(m, t[i], (int)i, (const mnc_u8 *)raw, slen(raw))) clean = 0;
        }
        if (!rd_done(&r)) return -1;
    } else if (kind == 1) {
        mnc_u32 base = rd_u32(&r), n = rd_u8(&r), seen = 0;
        if (r.bad || n > 8) return -1;
        c = cache_find(m, base);
        for (i = 0; i < n; i++) {
            mnc_u32 idx = rd_u8(&r);
            char raw[MNC_TXT];
            rd_pstr(&r, raw, MNC_MAXC);
            if (r.bad || idx >= MNC_N || (seen & (1u << idx))) return -1;
            seen |= 1u << idx;
            if (c >= 0) {
                if (i == 0) { int j; for (j = 0; j < MNC_N; j++) scopy(t[j], MNC_TXT, m->cache[c].t[j]); }
                if (!fill_slot(m, t[idx], (int)idx, (const mnc_u8 *)raw, slen(raw))) clean = 0;
            }
        }
        if (!rd_done(&r)) return -1;
        if (c < 0) return 0;
        if (n == 0) { int j; for (j = 0; j < MNC_N; j++) scopy(t[j], MNC_TXT, m->cache[c].t[j]); }
    } else {
        return -1;
    }
    if (clean && mnc_list_hash((const char (*)[MNC_TXT])t) != h) return 0;       /* the sender's hash covers its own text; only fatal if nothing was sanitised */
    cache_put(m, h, (const char (*)[MNC_TXT])t);
    m->st_chat[kind]++;
    return 1;
}

/* a peer address from a ticket we are willing to connect to ("Ticket addresses"): never unspecified,
   multicast / reserved / broadcast (>= 224), link-local, port 0; unless m->lax_addr (lab on one host): not loopback, not the server's,
   not our own (the DHCP address or the address WHOAMI reported) */
static int peer_addr_ok(const mnc *m, mnc_u32 ip, mnc_u32 port)
{
    if (ip == 0 || (ip >> 24) >= 224 || (ip >> 16) == 0xA9FE || port == 0) return 0;
    if (!m->lax_addr) {
        if ((ip >> 24) == 127 || ip == m->srv_ip || ip == m->own_ip || (m->whoami_ip && ip == m->whoami_ip)) {
#ifdef MNC_LAB_SAMENAT
            /* LAB ONLY: server and opponent seat behind one NAT share a public address; only the server rule is relaxed */
            if (m->lab_samenat && ip == m->srv_ip && (ip >> 24) != 127 && ip != m->own_ip && !(m->whoami_ip && ip == m->whoami_ip)) return 1;
#endif
            return 0;
        }
    }
    return 1;
}
/* the opponent's public address is OUR public address (WHOAMI, or the lab's server address rule aside): only acceptable together with a usable LAN hint (the caller checks) */
static int same_public(const mnc *m, mnc_u32 ip, mnc_u32 port)
{
    if (ip == 0 || (ip >> 24) >= 224 || (ip >> 16) == 0xA9FE || port == 0 || (ip >> 24) == 127) return 0;
    return m->whoami_ip && ip == m->whoami_ip && ip != m->srv_ip && ip != m->own_ip;
}
/* the optional LAN hint: a private address on OUR subnet (no subnet known = refused), never a public one */
static int lan_addr_ok(const mnc *m, mnc_u32 ip, mnc_u32 port)
{
    mnc_u32 a = ip >> 24, b = (ip >> 16) & 255;
    if (!peer_addr_ok(m, ip, port)) return 0;
    if (!(a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (m->lax_addr && a == 127))) return 0;
    return m->own_mask != 0 && ((ip ^ m->own_ip) & m->own_mask) == 0;
}

static int parse_ticket_poll(mnc *m, const mnc_u8 *body, mnc_u32 len, int *assigned)
{
    rd r;
    mnc_u32 st, i, j, np, seen = 0;
    mnc_player pl[MNC_MAX_PLAYERS];
    mnc_u8 mid[8], host, slot;
    const mnc_u8 *q;
#ifdef MNC_RKSET
    mnc_u8 rkset = 0;                                                /* gmSet (ext bit 4) */
#endif
#ifdef MNC_WATCH_TELL
    mnc_u8 watch = 0;
#ifdef MNC_SETUP_BLOCK
    mnc_u8 watch_only = 0;                                           /* the one byte after the players is the watch byte, not a setup block */
#endif
#endif
#ifdef MNC_SETUP_BLOCK
    struct { mnc_u8 have, flags, alt, stocks, n; mnc_u16 time; mnc_u32 items; mnc_u16 st[16]; } su;
    zero(&su, sizeof su);
#endif
    rd_init(&r, body, len);
    st = rd_u8(&r);
    if (st == 0) {
        mnc_u32 s = rd_u16(&r);
#ifdef MNC_POLL_INVITE
        if (!r.bad && r.i + 1u == r.n) {                             /* the challenge status (asked with ext bit 2): 0 nothing yet, 1 accepted, 2 not answered */
            mnc_u32 v = rd_u8(&r);
            if (v > 2u) return 0;
            mnc_invite_status = (mnc_u8)v;
        }
#endif
        if (!rd_done(&r)) return 0;
        m->seconds_left = (mnc_u16)s; *assigned = 0;
        return 1;
    }
    if (st != 1) return 0;
    q = rd_take(&r, 8); if (q) copy(mid, q, 8);
    host = (mnc_u8)rd_u8(&r); slot = (mnc_u8)rd_u8(&r); np = rd_u8(&r);
    if (r.bad || host > 1 || slot < 1 || slot > MNC_MAX_PLAYERS || np < 1 || np > MNC_MAX_PLAYERS) return 0;
    zero(pl, sizeof pl);
    for (i = 0; i < np; i++) {
        mnc_player *p = &pl[i];
        p->slot = (mnc_u8)rd_u8(&r);
        rd_pstr(&r, p->name, MNC_STR - 1); rd_pstr(&r, p->code, MNC_STR - 1);
        p->ip = rd_u32(&r); p->port = (mnc_u16)rd_u16(&r); p->lan_ip = rd_u32(&r); p->lan_port = (mnc_u16)rd_u16(&r); p->chat_hash = rd_u32(&r);
        if (r.bad || p->slot < 1 || p->slot > MNC_MAX_PLAYERS || (seen & (1u << p->slot))) return 0;
        seen |= 1u << p->slot;
        if (p->slot != slot) {                                       /* an opponent: its addresses must be ones we may connect to */
            int lan_ok = (p->lan_ip || p->lan_port) && lan_addr_ok(m, p->lan_ip, p->lan_port);
            if (!lan_ok) { p->lan_ip = 0; p->lan_port = 0; }                                 /* a LAN hint we cannot use (not our subnet): ignored, the public address stands */
            if (!peer_addr_ok(m, p->ip, p->port) && !(lan_ok && same_public(m, p->ip, p->port))) return -1;   /* two consoles behind one router share the public address: fine when the opponent's LAN address on our subnet came with it */
        }
        p->custom = (mnc_u8)(p->chat_hash != m->def_hash);
    }
#if defined(MNC_WATCH_TELL) && defined(MNC_SETUP_BLOCK)
#ifdef MNC_RKSET
    watch_only = !r.bad && r.n - r.i >= 1u && r.n - r.i <= 2u;       /* one or two single bytes after the players: the watch byte and gmSet (a setup block is longer) */
#else
    watch_only = !r.bad && r.i + 1u == r.n;                          /* exactly one byte after the players: the watch byte (a setup block is longer) */
#endif
#endif
#ifdef MNC_SETUP_BLOCK
#ifdef MNC_WATCH_TELL
    if (!r.bad && r.i < r.n && !watch_only) {                        /* MATCH SETUP block (we asked with ext bit 1) */
#else
    if (!r.bad && r.i < r.n) {                                       /* MATCH SETUP block (we asked with ext bit 1) */
#endif
        if (rd_u8(&r) != 1) return 0;
        su.flags = (mnc_u8)rd_u8(&r); su.alt = (mnc_u8)rd_u8(&r); su.stocks = (mnc_u8)rd_u8(&r);
        su.time = (mnc_u16)rd_u16(&r); su.items = rd_u32(&r); su.n = (mnc_u8)rd_u8(&r);
        if (r.bad || su.alt > 2 || su.n > 16) return 0;
        for (j = 0; j < su.n; j++) su.st[j] = (mnc_u16)rd_u16(&r);
        su.have = 1;
    }
#endif
#ifdef MNC_WATCH_TELL
#ifdef MNC_RKSET
    if (!r.bad && r.n - r.i >= 1u && r.n - r.i <= 2u) {              /* the watch byte (asked with ext bit 3), then gmSet when two bytes are left */
#else
    if (!r.bad && r.i + 1u == r.n) {                                 /* the watch byte (asked with ext bit 3), last */
#endif
        mnc_u32 v = rd_u8(&r);
        if (v > 1u) return 0;
        watch = (mnc_u8)v;
    }
#endif
#ifdef MNC_RKSET
    if (!r.bad && r.i + 1u == r.n) {                                 /* gmSet (asked with ext bit 4), last: the games of the Grandmaster set, 0 = a best of 1 */
        mnc_u32 v = rd_u8(&r);
        if (v > 9u) return 0;
        rkset = (mnc_u8)v;
    }
#endif
    if (!rd_done(&r) || !(seen & (1u << slot))) return 0;
#ifdef MNC_RKSET
    mnc_rk_set = rkset;
#endif
#ifdef MNC_WATCH_TELL
    mnc_watch_upload = watch;
    if (m->gn_magic == MNC_GN_MAGIC && m->gn_state == MNC_GN_NONE && !m->gn_room[0]) m->gn_room[MNC_UPLOAD_AT] = watch ? (char)MNC_UPLOAD_MARK : 0;     /* for the resident uploader (gns_res.c gns_wanted) */
#endif
#ifdef MNC_SETUP_BLOCK
    m->su_have = su.have; m->su_flags = su.flags; m->su_alt = su.alt; m->su_stocks = su.stocks; m->su_time = su.time; m->su_items = su.items; m->su_n = su.n;
    for (j = 0; j < 16; j++) m->su_stages[j] = j < su.n ? su.st[j] : 0;
#endif
    for (j = 0; j < 8; j++) m->match_id[j] = mid[j];
    m->is_host = host; m->my_slot = slot; m->nplayers = (mnc_u8)np;
    copy(m->pl, pl, sizeof pl);
    *assigned = 1;
    return 1;
}

/* ------------------------------------------------------------------ the reply dispatcher */
static void find_wait(mnc *m, mnc_u32 now)
{
    m->inflight = 0; m->f_stage = F_WAIT; m->f_next_at = now + MNC_POLL_INTERVAL_MS;
}

/* a ticket from an earlier session was still there (TICKET_EXISTS): cancelled it, now ask again once */
static void recreate(mnc *m, mnc_u32 now)
{
    m->f_stage = F_CREATE;
    (void)send_ticket_create(m, now, m->f_mode, m->f_target, m->f_gport, m->f_lan_ip, m->f_lan_port);
}

/* a valid status-0 reply for the request in flight.  Returns 0 if the body is malformed (dropped, the timer goes on). */
static int on_ok(mnc *m, mnc_u32 now, const mnc_u8 *body, mnc_u32 len)
{
    rd r;
    int i;
    rd_init(&r, body, len);
    switch (m->op) {
    case MNC_OP_HELLO: {
        const mnc_u8 *a = rd_take(&r, 3), *b = rd_take(&r, 3), *s;
        mnc_u32 caps = rd_u16(&r), t, c2;
        s = rd_take(&r, 8); t = rd_u32(&r);
        c2 = (r.i < r.n) ? rd_u8(&r) : 0;                 /* trailing caps2 byte only from servers that know the extension */
        if (!rd_done(&r)) return 0;
        m->srv_caps2 = (mnc_u8)c2;
        copy(m->srv_latest, a, 3); copy(m->srv_min, b, 3); copy(m->server_id, s, 8);
        m->srv_caps = (mnc_u16)caps; m->srv_time = t; m->have_hello = 1;
        m->update_available = (m->srv_latest[0] != m->version[0]) ? (m->srv_latest[0] > m->version[0])
                            : (m->srv_latest[1] != m->version[1]) ? (m->srv_latest[1] > m->version[1]) : (m->srv_latest[2] > m->version[2]);
        break;
    }
    case MNC_OP_WHOAMI: {
        mnc_u32 ip = rd_u32(&r), port = rd_u16(&r);
        if (!rd_done(&r)) return 0;
        m->whoami_ip = ip; m->whoami_port = (mnc_u16)port;
        break;
    }
    case MNC_OP_ENROL: case MNC_OP_GUEST_START:
#ifdef MNC_SEAL_CREDS
        if (mnc_seal_ctx && m->op == (mnc_seal_ctx == 1 ? MNC_OP_ENROL : MNC_OP_GUEST_START)) {      /* the secret came sealed to our key (never in clear) */
            static const char cx[2][17] = { "ms-enrol-v1\0\0\0\0\0", "ms-guest-v1\0\0\0\0\0" };
            char code[MNC_STR], name[MNC_STR];
            mnc_u8 dev[8], tok[16], sid[8];
            if (seal_open(body, len, (const mnc_u8 *)cx[mnc_seal_ctx - 1], seal_priv, seal_pub, code, name, dev, tok, sid)) return 0;
            mnc_set_identity(m, dev, tok, code, name); copy(m->server_id, sid, 8);
            zero(tok, 16); mnc_set_seal(0, 0, 0);                                 /* one key, one reply: set a new one for the next enrol */
        } else
#endif
        if (!parse_creds(m, &r, 1)) return 0;
        break;
    case MNC_OP_LINK_POLL:
        if (!parse_creds(m, &r, 1)) return 0;
        m->link_expires = 0;
        if (m->task == T_LINKWAIT) { finish_ok(m); return 1; }
        break;
    case MNC_OP_LINK_START: {
        char uc[MNC_STR];
        const mnc_u8 *dc;
        mnc_u32 exp, iv;
        rd_pstr(&r, uc, MNC_STR - 1); dc = rd_take(&r, 16); exp = rd_u16(&r); iv = rd_u8(&r);
        if (!rd_done(&r) || exp == 0 || iv == 0) return 0;
        scopy(m->link_user, MNC_STR, uc); copy(m->link_dev, dc, 16);
        m->link_expires = (mnc_u16)exp; m->link_interval = (mnc_u8)iv;
        m->f_deadline = now + exp * 1000u;
        break;
    }
    case MNC_OP_PROFILE: {
        char code[MNC_STR], name[MNC_STR];
        mnc_u32 fl, h;
        rd_pstr(&r, code, MNC_STR - 1); rd_pstr(&r, name, MNC_STR - 1); fl = rd_u8(&r); h = rd_u32(&r);
        if (!rd_done(&r)) return 0;
        scopy(m->code, MNC_STR, code); scopy(m->name, MNC_STR, name); m->profile_flags = (mnc_u8)fl; m->profile_hash = h;
        break;
    }
    case MNC_OP_CHAT_SET: {
        mnc_u32 h = rd_u32(&r);
        if (!rd_done(&r)) return 0;
        if (h != m->own_hash) { finish_fail(m, MNC_E_HASH, 0); return 1; }
        break;
    }
    case MNC_OP_CHAT_GET: {
        char t[MNC_N][MNC_TXT];
        for (i = 0; i < MNC_N; i++) {
            char raw[MNC_TXT];
            rd_pstr(&r, raw, MNC_MAXC);
            if (!r.bad) (void)fill_slot(m, t[i], i, (const mnc_u8 *)raw, slen(raw));
        }
        if (!rd_done(&r)) return 0;
        for (i = 0; i < MNC_N; i++) scopy(m->fetched[i], MNC_TXT, t[i]);
        break;
    }
    case MNC_OP_TICKET_CREATE: {
        mnc_u32 st = rd_u8(&r), to = rd_u16(&r);
        if (!rd_done(&r) || st > 1) return 0;
        m->ticket_state = (mnc_u8)st; m->ticket_timeout = (mnc_u16)to;
        if (m->task == T_FIND) {
            mnc_u32 ms = to ? (mnc_u32)to * 1000u : MNC_FIND_MAX_MS;
            if (ms > MNC_FIND_MAX_MS) ms = MNC_FIND_MAX_MS;
            m->f_deadline = now + ms + MNC_FIND_SLACK_MS;
            if (st) send_ticket_poll(m, now); else find_wait(m, now);
            return 1;
        }
        break;
    }
    case MNC_OP_TICKET_POLL: {
        int assigned = 0;
        int pr = parse_ticket_poll(m, body, len, &assigned);
        if (pr < 0) { finish_fail(m, MNC_E_TICKET, 0); return 1; }   /* the whole ticket is refused: nothing is connected to */
        if (!pr) return 0;
        if (m->task == T_FIND) {
            if (!assigned) {
                if (m->f_open) { mnc_u32 ms = m->ticket_timeout ? (mnc_u32)m->ticket_timeout * 1000u : MNC_FIND_MAX_MS; if (ms > MNC_FIND_MAX_MS) ms = MNC_FIND_MAX_MS; m->f_deadline = now + ms + MNC_FIND_SLACK_MS; }
                find_wait(m, now);
                return 1;
            }
            m->f_next = 0;
            if (!fetch_next(m, now)) finish_ok(m);
            return 1;
        }
        break;
    }
    case MNC_OP_CHAT_FETCH: {
        int rc = parse_chat_entry(m, body, len);
        if (rc < 0) return 0;
        if (m->task == T_FIND) {
            m->f_next++;
            if (!fetch_next(m, now)) finish_ok(m);
            return 1;
        }
        break;
    }
    case MNC_OP_ROOM_RESERVE: {
        mnc_u32 ttl = rd_u16(&r);
        if (!rd_done(&r)) return 0;
        m->room_ttl = (mnc_u16)ttl;
        break;
    }
    case MNC_OP_GAME_REPORT:
        if (len == 0) { m->rep_game = 0; m->rep_match = 0; break; }
        if (len != 2) return 0;
        m->rep_game = body[0]; m->rep_match = body[1];
        break;
    case MNC_OP_GAME_RECORD:
        if (len) return 0;
        break;
    case MNC_OP_LAB_DIAG:
        if (len) return 0;
        break;
    case MNC_OP_MATCH_STATUS:
        if (len != 1) return 0;
        m->rep_match = body[0];
        break;
    case MNC_OP_LOGOUT:
        if (len) return 0;
        mnc_clear_identity(m);
        break;
    case MNC_OP_ROOM_RELEASE:
    case MNC_OP_TICKET_CANCEL:
        if (len) return 0;
        if (m->task == T_FIND && m->f_stage == F_CANCEL) { recreate(m, now); return 1; }      /* the leftover ticket is gone */
        break;
    default:
        return 0;
    }
    finish_ok(m);
    return 1;
}

/* a status != 0 reply (header only) */
static void on_status(mnc *m, mnc_u32 now, int status)
{
    if (m->op == MNC_OP_LOGOUT && status == MNC_ST_AUTH_FAILED) { mnc_clear_identity(m); finish_ok(m); return; }
    if (m->task == T_FIND && m->f_stage == F_CANCEL) { recreate(m, now); return; }       /* before the NO_TICKET rule: a find is not done without a match */
    if (m->op == MNC_OP_TICKET_CANCEL && status == MNC_ST_NO_TICKET) { finish_ok(m); return; }
    if (m->task == T_FIND && m->f_stage == F_CREATE && status == MNC_ST_TICKET_EXISTS && !m->f_retried) {
        wr w;
        m->f_retried = 1; m->f_stage = F_CANCEL;
        req_begin(m, MNC_OP_TICKET_CANCEL, &w); 
        (void)req_send(m, now, &w);
        return;
    }
    if (m->task == T_FIND && m->f_stage == F_FETCH) {            /* texts are cosmetic: this player keeps the defaults */
        m->f_next++;
        if (!fetch_next(m, now)) finish_ok(m);
        return;
    }
    if (m->task == T_LINKWAIT && m->op == MNC_OP_LINK_POLL) {
        if (status == MNC_ST_LINK_PENDING || status == MNC_ST_SLOW_DOWN) {
            if (status == MNC_ST_SLOW_DOWN && m->l_interval_ms < 60000u) m->l_interval_ms += 5000u;
            m->inflight = 0; m->f_stage = F_WAIT; m->f_next_at = now + m->l_interval_ms;
            return;
        }
    }
    finish_fail(m, MNC_E_SERVER, status);
}

static void on_packet(mnc *m, mnc_u32 now, mnc_u32 n)
{
    const mnc_u8 *p = m->rx;
    mnc_u8 st = 0;
    mw2_u32 blen = 0;
    int rc;
    if (m->phase == PH_HS) {
        mw2_u32 bad0 = m->w.st_bad;
        rc = mw2_hs2(&m->w, now, p, n);
        if (rc == 1) { seal_and_send(m, now); return; }
        if (rc < 0) { on_status(m, now, -rc); return; }               /* an unsigned refusal: acted on like a status (it carries nothing else) */
        if (m->w.st_bad != bad0) { m->st_bad++; m->bad_seen = 1; } else m->st_ignored++;
        return;
    }
    {
        mw2_u32 bad0 = m->w.st_bad;
        rc = mw2_open(&m->w, now, p, n, m->op, m->w.ctr, &st, &blen);
        if (rc == 2) {                                                /* the server forgot the session: handshake again, at most twice per request */
            if (m->rehs < 2) { m->rehs++; mw2_drop(&m->w); m->phase = PH_JIT; m->tries = 0; m->sent_ms = now + mnc_rand(m) % (MNC_REHS_JITTER_MS + 1u); } else m->st_ignored++;
            return;
        }
        if (rc != 1) {
            if (m->w.st_bad != bad0) { m->st_bad++; m->bad_seen = 1; } else m->st_ignored++;
            return;
        }
    }
    if (st != 0) {
        if (blen != 0) { m->st_bad++; m->bad_seen = 1; return; }      /* an error reply carries no payload */
        on_status(m, now, st);
        return;
    }
    if (!on_ok(m, now, p + MW2_HDR, (mnc_u32)blen)) { m->st_bad++; m->bad_seen = 1; }
}

/* the retry budget of the request in flight is spent */
static void on_exhausted(mnc *m, mnc_u32 now)
{
    int err = m->bad_seen ? MNC_E_BAD_REPLY : MNC_E_TIMEOUT;
    if (m->task == T_FIND && m->f_stage == F_FETCH) {
        m->f_next++;
        if (!fetch_next(m, now)) finish_ok(m);
        return;
    }
    if (m->task == T_FIND && m->f_stage == F_CANCEL) { recreate(m, now); return; }
    finish_fail(m, err, 0);
}

int mnc_poll(mnc *m, mnc_u32 now)
{
    int i;
    for (i = 0; i < MNC_RX_BUDGET && m->io.recv; i++) {
        int n = m->io.recv(m->io.ctx, m->rx, MNC_MAX_PACKET);
        if (n <= 0) break;
        m->st_rx++;
        if ((mnc_u32)n > MNC_MAX_PACKET || m->state != MNC_BUSY || !m->inflight) { m->st_ignored++; continue; }
        on_packet(m, now, (mnc_u32)n);
    }
    if (m->state != MNC_BUSY) return m->state;
    if (m->inflight) {
        if (m->phase == PH_JIT) { if (reached(now, m->sent_ms)) start_hs(m, now); return m->state; }
        if (elapsed(now, m->sent_ms) >= m->timeout_ms) {
            if (m->tries < m->max_tries) { m->tries++; m->sent_ms = now; m->st_resent++; transmit(m); }
            else on_exhausted(m, now);
        }
        return m->state;
    }
    /* between requests of a task */
    if (m->task == T_FIND || m->task == T_LINKWAIT) {
        if (reached(now, m->f_deadline)) { finish_fail(m, MNC_E_DEADLINE, 0); return m->state; }
        if (reached(now, m->f_next_at)) {
            if (m->task == T_FIND) send_ticket_poll(m, now); else (void)send_link_poll(m, now);
        }
    }
    return m->state;
}

/* ------------------------------------------------------------------ peer hello */
mnc_u32 mnc_peer_hello(const mnc *m, mnc_u8 *out, mnc_u32 cap, mnc_u32 nonce, mnc_u32 tag)
{
    cr_hello h;
    if (cap < CR_HELLO_SIZE) return 0;
    h.proto = CR_PROTO; h.build_id = m->build_id; h.caps = (unsigned short)(m->caps & CR_CAPS_KNOWN); h.nonce = nonce; h.tag = tag;
    cr_hello_pack(out, &h);
    return CR_HELLO_SIZE;
}
int mnc_peer_hello_ok(const mnc_u8 *p, mnc_u32 len, mnc_u16 *caps_out)
{
    cr_hello h;
    if (!cr_hello_parse(p, len, &h)) return 0;
    if (caps_out) *caps_out = (mnc_u16)(h.caps & CR_CAPS_KNOWN);
    return 1;
}
int mnc_peer_may_send(unsigned msg_id, int peer_level)
{
    return cr_may_send(msg_id, peer_level);
}

#ifdef MNC_TEST_HOOKS
/* host tests only: run the reply parser of op on a body that already "authenticated" (fuzzing the parsers behind the tag check) */
int mnc_test_feed_ok(mnc *m, mnc_u32 now, int op, int task, const mnc_u8 *body, mnc_u32 len)
{
    m->state = MNC_BUSY; m->op = (mnc_u8)op; m->task = (mnc_u8)task; m->inflight = 1; m->f_stage = F_WAIT;
    return on_ok(m, now, body, len);
}
#endif
