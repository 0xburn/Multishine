/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* gnc: the Game Night room ops client, see gnc.h.  C89, no libc, no heap. */
#include "gnc.h"
#ifdef GNC_LAB_MOCK_YOU                  /* (set by hand for a never-shipped test build, e.g. a Dolphin run with an SG_LAB=0 overlay) */
#elif defined(SG_LAB) && SG_LAB
#define GNC_LAB_MOCK_YOU 1      /* lab overlays only: the release parser refuses a 'you' row in example data */
#else
#define GNC_LAB_MOCK_YOU 0
#endif

#define GNC_PROTO 1u                /* client_recog.h CR_PROTO (the client block's protocol byte, as mnc.c put_client) */

static mnc_u32 g32(const mnc_u8 *p) { return ((mnc_u32) p[0] << 24) | ((mnc_u32) p[1] << 16) | ((mnc_u32) p[2] << 8) | p[3]; }

void gnc_init(gnc *g, mnc *c)
{
    mnc_u8 *b = (mnc_u8 *) g;
    mnc_u32 i;
    for (i = 0; i < sizeof *g; i++) b[i] = 0;
    g->c = c; g->state = GNC_IDLE; g->timeout_ms = 700u;
}

static void fail(gnc *g, int err, int status) { g->state = GNC_FAILED; g->err = err; g->status = status; }

/* ---- request building into c->req + MW2_HDR ---- */
typedef struct { mnc_u8 *p; mnc_u32 n, cap; int bad; } wb;
static void w8(wb *w, unsigned v) { if (w->n < w->cap) w->p[w->n++] = (mnc_u8) v; else w->bad = 1; }
static void w16(wb *w, unsigned v) { w8(w, v >> 8); w8(w, v); }
static void w32(wb *w, mnc_u32 v) { w8(w, v >> 24); w8(w, v >> 16); w8(w, v >> 8); w8(w, v); }
static void wstr(wb *w, const char *s, unsigned max)
{
    unsigned n = 0, i;
    while (s && s[n]) n++;
    if (n > max) { w->bad = 1; return; }
    w8(w, n);
    for (i = 0; i < n; i++) { if ((mnc_u8) s[i] < 0x20u || (mnc_u8) s[i] > 0x7Eu) w->bad = 1; w8(w, (mnc_u8) s[i]); }
}
static void wclient(wb *w, const mnc *c)
{
    w8(w, c->version[0]); w8(w, c->version[1]); w8(w, c->version[2]); w8(w, GNC_PROTO); w8(w, c->platform);
    w16(w, c->caps); w32(w, c->codeset); w32(w, c->rules);
}
static void begin(gnc *g, wb *w)
{
    w->p = g->c->req + MW2_HDR; w->n = 0; w->cap = 96u; w->bad = 0;
}
static int send(gnc *g, mnc_u8 op, wb *w, mnc_u32 now)
{
    mnc *c = g->c;
    mnc_u32 n, i;
    if (g->state == GNC_BUSY) return GNC_E_BUSY;
    if (w->bad) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    if (!mw2_ready(&c->w, now)) { fail(g, GNC_E_NOSESSION, 0); return GNC_E_NOSESSION; }
    n = mw2_seal(&c->w, c->req, op, w->n);
    if (n > sizeof g->pkt) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    for (i = 0; i < n; i++) g->pkt[i] = c->req[i];
    g->pkt_len = n; g->ctr = c->w.ctr; g->op = op; g->sent_ms = now; g->tries = 1;
    g->state = GNC_BUSY; g->err = GNC_E_NONE; g->status = 0; g->have_view = 0; g->unchanged = 0; g->have_roster = 0; g->have_set = 0;
    g->st_sent++;
    (void) c->io.send(c->io.ctx, g->pkt, n);
    return 0;
}

int gnc_list_f(gnc *g, mnc_u32 now, unsigned page, unsigned filter)
{
    wb w;
    begin(g, &w);
    g->lreq_page = (mnc_u8) page; g->lreq_filter = (mnc_u8) filter;
    w8(&w, page); w8(&w, g->lv4 ? GNC_LIST_V4 : g->lv5 ? GNC_LIST_V5 : g->nrg7 ? GNC_LIST_V6 : (GNC_LIST_V6 | GNC_VER_RG7)); w8(&w, filter);   /* + GNC_VER_RG7: region 6 (NAC) may come */   /* list v6: the filter byte is the room type 0..5, 0xFF every room; v5 / v4 (an older server): 0 Legal, 2 All stages, 0xFF every room */
    return send(g, GNC_OP_LIST, &w, now);
}
int gnc_list(gnc *g, mnc_u32 now, unsigned page) { return gnc_list_f(g, now, page, GNC_SM_ANY); }
static unsigned vview(const gnc *g) { return g->vver == GNC_VIEW_V5 ? GNC_VIEW_V5 : g->nrg7 ? GNC_VIEW_V6 : (GNC_VIEW_V6 | GNC_VER_RG7); }     /* view v6 + GNC_VER_RG7 (NAC) unless the server predates them */
static void cp_s(char *d, const char *s, unsigned max) { unsigned i = 0; while (s && s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }
int gnc_join(gnc *g, mnc_u32 now, const char *code, const char *pin, unsigned ch)
{
    wb w;
    begin(g, &w);
    cp_s(g->jcode, code, 8); cp_s(g->jpin, pin, 4); g->jch = (mnc_u8) ch; g->jkind = 1;
    wclient(&w, g->c); wstr(&w, code, 8); wstr(&w, pin ? pin : "", 4); w8(&w, ch); w8(&w, vview(g));      /* view v6 (room types) unless the server is known to predate it */
    return send(g, GNC_OP_JOIN, &w, now);
}
int gnc_create(gnc *g, mnc_u32 now, const char *name, unsigned flags, unsigned policy, const char *pin, unsigned capacity, unsigned ch)
{
    return gnc_create_sm(g, now, name, flags, policy, pin, capacity, ch, GNC_SM_ANY);
}
int gnc_create_sm(gnc *g, mnc_u32 now, const char *name, unsigned flags, unsigned policy, const char *pin, unsigned capacity, unsigned ch, unsigned smode)
{
    return gnc_create_rt(g, now, name, flags, policy, pin, capacity, ch, smode, GNC_RT_NONE);
}
int gnc_create_rt(gnc *g, mnc_u32 now, const char *name, unsigned flags, unsigned policy, const char *pin, unsigned capacity, unsigned ch, unsigned smode, unsigned rtype)
{
    wb w;
    begin(g, &w);
    cp_s(g->jname, name, 12); cp_s(g->jpin, pin, 4); g->jch = (mnc_u8) ch; g->jflags = (mnc_u8) flags; g->jpolicy = (mnc_u8) policy; g->jcap = (mnc_u8) capacity; g->jsm = (mnc_u8) smode; g->jrt = (mnc_u8) rtype; g->jkind = 2;
    wclient(&w, g->c); w8(&w, capacity); w8(&w, ch); wstr(&w, pin ? pin : "", 4);
    if (rtype != GNC_RT_NONE) {                                                       /* a room type needs view v6 (else the server refuses, BAD_REQUEST); the stage mode byte is ignored then, the server derives it from the type */
        wstr(&w, name ? name : "", 12); w8(&w, flags & 11u); w8(&w, policy); w8(&w, GNC_SM_ANY); w8(&w, g->nrg7 ? GNC_VIEW_V6 : (GNC_VIEW_V6 | GNC_VER_RG7)); w8(&w, rtype);
        return send(g, GNC_OP_CREATE, &w, now);
    }
    wstr(&w, name ? name : "", 12); w8(&w, flags & 11u); w8(&w, policy); w8(&w, smode); w8(&w, vview(g));    /* stage mode (0 Legal, 2 All stages, 0xFF the server's default = Legal) | view v6 / v5 (the stage mode may be 2) */
    return send(g, GNC_OP_CREATE, &w, now);
}
int gnc_action_cos(gnc *g, mnc_u32 now, unsigned action, unsigned value, unsigned costume)
{
    wb w;
    if (costume > 15u) return gnc_action(g, now, action, value);
    begin(g, &w);
    w8(&w, action); w8(&w, value); w8(&w, costume);
    return send(g, GNC_OP_ACTION, &w, now);
}
int gnc_state(gnc *g, mnc_u32 now, int have_rev)
{
    wb w;
    begin(g, &w);
    if (have_rev >= 0) w8(&w, (unsigned) have_rev);
    return send(g, GNC_OP_STATE, &w, now);
}
int gnc_roster(gnc *g, mnc_u32 now)
{
    wb w;
    begin(g, &w);
    w8(&w, 0); w8(&w, 2);                                           /* roster flag 2: with each player's streak wins and the lead flag (reply tag 0x03) */
    return send(g, GNC_OP_STATE, &w, now);
}
int gnc_action(gnc *g, mnc_u32 now, unsigned action, unsigned value)
{
    wb w;
    begin(g, &w);
    w8(&w, action); w8(&w, value);
    return send(g, GNC_OP_ACTION, &w, now);
}
int gnc_rk_state(gnc *g, mnc_u32 now, const mnc_u8 mid[8])
{
    wb w;
    unsigned i;
    begin(g, &w);
    for (i = 0; i < 8u; i++) w8(&w, mid[i]);
    return send(g, GNC_OP_RK_STATE, &w, now);
}
int gnc_rk_action(gnc *g, mnc_u32 now, const mnc_u8 mid[8], unsigned action, unsigned value, unsigned costume)
{
    wb w;
    unsigned i;
    begin(g, &w);
    for (i = 0; i < 8u; i++) w8(&w, mid[i]);
    w8(&w, action); w8(&w, value);
    if (costume <= 15u) w8(&w, costume);
    return send(g, GNC_OP_RK_ACTION, &w, now);
}
int gnc_rk_status(gnc *g, mnc_u32 now, const mnc_u8 mid[8], unsigned code)
{
    wb w;
    unsigned i;
    begin(g, &w);
    for (i = 0; i < 8u; i++) w8(&w, mid[i]);
    w8(&w, code);
    return send(g, GNC_OP_MATCH_STATUS, &w, now);
}
int gnc_leave(gnc *g, mnc_u32 now)
{
    wb w;
    begin(g, &w);
    return send(g, GNC_OP_LEAVE, &w, now);
}

/* ---- parsers (ports of ms_rooms.parse_list / parse_view) ---- */
typedef struct { const mnc_u8 *b; mnc_u32 n, i; } rd;
static int pstr(rd *r, char *out, unsigned max)
{
    unsigned k, len;
    if (r->i >= r->n) return -1;
    len = r->b[r->i];
    if (len > max || r->i + 1u + len > r->n) return -1;
    for (k = 0; k < len; k++) {
        mnc_u8 ch = r->b[r->i + 1u + k];
        if (ch < 0x20u || ch > 0x7Eu) return -1;
        out[k] = (char) ch;
    }
    out[len] = 0;
    r->i += 1u + len;
    return 0;
}

int gnc_parse_roster(const mnc_u8 *b, mnc_u32 n, struct gnc_roster *out)
{
    rd r;
    unsigned k;
    if (n < 2u || b[1] > GNC_ROSTER_MAX) return -1;
    out->streak = b[0]; out->n = b[1];
    r.b = b; r.n = n; r.i = 2;
    for (k = 0; k < out->n; k++) {
        if (pstr(&r, out->name[k], 12) || r.i + 2u > n) return -1;
        out->crowns[k] = b[r.i]; out->place[k] = b[r.i + 1u];
        r.i += 2u;
        if (r.i + 2u > n || b[r.i + 1u] > 1u) return -1;
        out->pstreak[k] = b[r.i]; out->plead[k] = b[r.i + 1u]; r.i += 2u;
    }
    return r.i == n ? 0 : -1;
}

int gnc_parse_list(const mnc_u8 *b, mnc_u32 n, int v2, mnc_u8 *total, mnc_u8 *page, mnc_u8 *count, struct gnc_entry *out)
{
    rd r;
    unsigned k, known = GNC_F_SPECT | GNC_F_PASS | GNC_F_FULL | (v2 ? (GNC_F_RUN | GNC_F_MOCK) : 0u);
    unsigned lmask = v2 == 6 ? (GNC_LF_ALL_STAGES | GNC_LF_ITEMS | GNC_LF_TYPES) : GNC_LF_ALL_STAGES;
    if (n < 3u || n > (v2 >= 3 ? 420u : GNC_VIEW_MAX)) return -1;
    if (b[2] > GNC_PAGE_MAX || b[2] > b[0]) return -1;
    r.b = b; r.n = n; r.i = 3;
    if (v2 == 5 || v2 == 6) { if (n < 4u || (b[3] & ~lmask) || (v2 == 6 && !(b[3] & GNC_LF_TYPES))) return -1; r.i = 4; }       /* v5: the list-flags byte; v6: + items switch and "room types served" (always set) */
    for (k = 0; k < b[2]; k++) {
        struct gnc_entry *e = &out[k];
        mnc_u8 mode, mem, cap, fl, sp;
        if (pstr(&r, e->code, 8) || pstr(&r, e->name, 12)) return -1;
        if (r.i + 5u > n) return -1;
        mode = b[r.i]; mem = b[r.i + 1u]; cap = b[r.i + 2u]; fl = b[r.i + 3u]; sp = b[r.i + 4u];
        r.i += 5u;
        if (pstr(&r, e->host, 12)) return -1;
        if (cap < 2u || cap > 32u || mem > cap || (fl & ~known) || sp > 2u || ((fl & GNC_F_FULL) != 0) != (mem >= cap)) return -1;
        e->mode = mode; e->members = mem; e->capacity = cap; e->flags = fl; e->stadium = sp; e->region = GNC_REGION_NONE; e->smode = 0; e->minutes = 0; e->rtype = 0;
        if (v2 >= 3) {
            if (r.i + 4u > n) return -1;
            if ((b[r.i] >= GNC_REGION_N && b[r.i] != GNC_REGION_NONE) || b[r.i + 1u] > (v2 >= 4 ? 2u : 1u)) return -1;      /* All stages (2) only in a v4 list; region 6 (NAC) comes only after GNC_VER_RG7 */
            e->region = b[r.i]; e->smode = b[r.i + 1u];
            e->minutes = (mnc_u16) ((b[r.i + 2u] << 8) | b[r.i + 3u]);              /* minutes the room has been open (server time, saturating) */
            r.i += 4u;
            if (v2 >= 6) {                                                          /* list v6: the room type; its stage rule is the stage mode byte (All for Wild and Wild And Crazy) */
                if (r.i >= n || b[r.i] > 5u || e->smode == 1u || (e->smode == 2u) != (b[r.i] == 2u || b[r.i] == 3u)) return -1;
                e->rtype = b[r.i++];
            } else e->rtype = (mnc_u8) (e->smode == 2u ? 2u : 0u);
        }
    }
    if (r.i != n) return -1;
    *total = b[0]; *page = b[1]; *count = b[2];
    return 0;
}

int gnc_parse_view(const mnc_u8 *b, mnc_u32 n, struct gnc_view *v)
{
    rd r;
    unsigned k;
    mnc_u8 ver, phase, flags, nmem, nq, pos, side;
    if (n < 13u || n > GNC_VIEW_MAX) return -1;
    ver = b[0]; phase = b[2]; flags = b[3]; nmem = b[4]; nq = b[5]; pos = b[6]; side = b[7];
    if ((ver != 2u && ver != 3u && ver != 4u && ver != 5u && ver != 6u) || phase > 2u || nmem > 32u || nq > nmem || (flags & 0x80u)) return -1;
    if (pos != 0xFFu && (pos < 1u || pos > nq)) return -1;
    if (side != 0u && side != 1u && side != 0xFFu) return -1;
    if (((flags & 8u) != 0) != (pos != 0xFFu) || ((flags & 16u) != 0) != (side != 0xFFu) || (pos != 0xFFu && side != 0xFFu)) return -1;
    v->rev = b[1]; v->phase = phase; v->flags = flags; v->members = nmem; v->qlen = nq; v->pos = pos; v->side = side;
    v->streak = b[8]; v->crowns = b[9]; v->secs = b[10]; v->set_id = (mnc_u16) ((b[11] << 8) | b[12]);
    r.b = b; r.n = n; r.i = 13;
    if (pstr(&r, v->code, 8)) return -1;
    for (k = 0; k < 2u; k++) {
        if (r.i >= n || b[r.i] > 1u) return -1;
        v->side_on[k] = b[r.i++];
        v->side_name[k][0] = 0; v->side_char[k] = v->side_color[k] = v->side_crowns[k] = 0;
        if (!v->side_on[k]) continue;
        if (pstr(&r, v->side_name[k], 12)) return -1;
        if (r.i + 3u > n) return -1;
        if (b[r.i] > 26u || b[r.i + 1u] > 15u) return -1;
        v->side_char[k] = b[r.i]; v->side_color[k] = b[r.i + 1u]; v->side_crowns[k] = b[r.i + 2u];
        r.i += 3u;
    }
    if (!v->side_on[0] && v->side_on[1]) return -1;
    if (r.i >= n) return -1;
    v->nnext = b[r.i++];
    if (v->nnext > GNC_UPNEXT || v->nnext > nq) return -1;
    for (k = 0; k < v->nnext; k++) if (pstr(&r, v->next[k], 12)) return -1;
    if (r.i + 2u > n) return -1;
    v->rflags = b[r.i]; v->stadium = b[r.i + 1u];
    r.i += 2u;
    if ((v->rflags & ~7u) || v->stadium > 2u) return -1;
    if (pstr(&r, v->rname, 12)) return -1;
    v->v3 = 0; v->region = GNC_REGION_NONE; v->smode = 0; v->capacity = 0; v->turn = 0xFFu; v->picked = 0; v->stage = 0xFFu; v->pw[0] = 0; v->minutes = 0; v->wfacts = 0;
    v->v6 = 0; v->rtype = 0; v->best_of = 1; v->game = 0; v->score_w = 0; v->score_c = 0; v->step = 0; v->step_owner = 0xFFu; v->step_secs = 0xFFu; v->stage_avail = 0; v->stage_n = 0; v->forced = GNC_RT_NONE; v->struck = 0; v->excl = 0; v->rules_hash = 0;
    if (ver == 2u) {
        if (r.i + 1u != n) return -1;
        v->mode = b[r.i];
        return 0;
    }
    if (r.i + 8u > n) return -1;                                    /* v3 (ms_rooms.parse_view): mode, region, stage mode, capacity, turn, picked, stage, password p4 */
    v->mode = b[r.i]; v->region = b[r.i + 1u]; v->smode = b[r.i + 2u]; v->capacity = b[r.i + 3u]; v->turn = b[r.i + 4u]; v->picked = b[r.i + 5u]; v->stage = b[r.i + 6u];
    r.i += 7u;
    if ((v->region >= GNC_REGION_N && v->region != GNC_REGION_NONE) || v->smode > (ver >= 5u ? 2u : 1u) || v->capacity < 2u || v->capacity > 32u || nmem > v->capacity) return -1;
    if ((v->turn > 1u && v->turn != GNC_TURN_GO && v->turn != 0xFFu && !(ver == 6u && v->turn == GNC_TURN_WAIT)) || (v->picked & ~3u) || ((v->turn == 0xFFu) != (phase != 1u))) return -1;
    if (ver != 6u && ((v->turn == GNC_TURN_GO && v->picked != 3u) || (v->turn == 1u && !(v->picked & 1u)) || (v->turn == 0u && (v->picked & 1u)))) return -1;      /* (in v6 the winner side does not always pick first: no such rule) */
    if (pstr(&r, v->pw, 4)) return -1;
    if (v->pw[0]) { for (k = 0; k < 4u; k++) if (v->pw[k] < '0' || v->pw[k] > '9') return -1; if (v->pw[4] != 0) return -1; }
    if (r.i + 2u + (ver >= 4u ? 1u : 0u) > n || (ver != 6u && r.i + 2u + (ver >= 4u ? 1u : 0u) != n)) return -1;
    v->minutes = (mnc_u16) ((b[r.i] << 8) | b[r.i + 1u]);                  /* minutes up (v3 view), then in v4 the watch facts byte */
    if (ver >= 4u) { if (b[r.i + 2u] & ~3u) return -1; v->wfacts = b[r.i + 2u]; }
    r.i += 2u + (ver >= 4u ? 1u : 0u);
    if (ver == 6u) {                                                        /* the room type trailer (ms_rooms._parse_trailer): 10 fixed bytes, the stage list, 10 more */
        mnc_u8 st, ow;
        if (r.i + 10u > n) return -1;
        v->rtype = b[r.i]; v->best_of = b[r.i + 1u]; v->game = b[r.i + 2u]; v->score_w = b[r.i + 3u]; v->score_c = b[r.i + 4u]; st = b[r.i + 5u]; ow = b[r.i + 6u];
        v->step_secs = b[r.i + 7u]; v->stage_avail = b[r.i + 8u]; v->stage_n = b[r.i + 9u];
        r.i += 10u;
        if (v->rtype > 5u || (v->best_of != 1u && v->best_of != 3u && v->best_of != 5u && v->best_of != 7u && v->best_of != 9u) || st > GNC_STEP_PICK_CHAR || v->stage_n > GNC_STAGE_LIST_MAX || r.i + v->stage_n + 10u != n) return -1;
        for (k = 0; k < v->stage_n; k++) v->stage_list[k] = b[r.i + k];
        r.i += v->stage_n;
        v->forced = b[r.i]; v->excl = g32(b + r.i + 1u); v->struck = b[r.i + 5u]; v->rules_hash = g32(b + r.i + 6u);
        r.i += 10u;
        v->step = st; v->step_owner = ow;
        if ((st != 0u && ow != 0u && ow != 1u) || (st == 0u && ow != 0xFFu) || (((unsigned) v->stage_avail | v->struck) >> v->stage_n) || (v->excl >> GNC_CHAR_COUNT) || (v->forced != GNC_RT_NONE && v->forced > 25u)) return -1;
        if ((v->game == 0u) != (phase == 0u)) return -1;
        v->v6 = 1;
    }
    v->v3 = 1;
    return r.i == n ? 0 : -1;
}

/* the Grandmaster set view (ms_ranked.encode_set_view; ms_client.decode_set_view is the reference): 15 header bytes, n stage ids, n stage states, the game's stage, 4 character bytes, result, reason */
int gnc_parse_setview(const mnc_u8 *b, mnc_u32 n, struct gnc_setview *v)
{
    unsigned k, cnt;
    if (n < 15u + 1u + 4u + 2u || n > 64u) return -1;
    cnt = b[14];
    if (cnt > 8u || (n != 15u + 2u * cnt + 1u + 4u + 2u && n != 15u + 2u * cnt + 1u + 4u + 2u + 2u)) return -1;     /* + the two tier bytes (G2) */
    v->ver = b[0]; v->phase = b[1]; v->my_slot = b[2]; v->best_of = b[3]; v->game = b[4]; v->score[0] = b[5]; v->score[1] = b[6]; v->p0 = b[7]; v->prev_winner = b[8];
    v->step = b[9]; v->owner = b[10]; v->remaining = b[11]; v->secs = b[12]; v->avail = b[13]; v->n = (mnc_u8) cnt;
    if (v->ver != 1u || v->phase > 2u || (v->my_slot != 1u && v->my_slot != 2u) || v->best_of < 1u || v->best_of > 9u || !(v->best_of & 1u) || v->step > 5u || v->p0 > 2u || v->prev_winner > 2u) return -1;
    if ((v->step != 0u && v->owner != 1u && v->owner != 2u) || (v->step == 0u && v->owner != 0xFFu) || (cnt < 8u && (v->avail >> cnt) != 0u)) return -1;
    for (k = 0; k < 8u; k++) { v->id[k] = 0; v->state[k] = 0; }
    for (k = 0; k < cnt; k++) { v->id[k] = b[15u + k]; v->state[k] = b[15u + cnt + k]; if (v->state[k] > 4u) return -1; }
    k = 15u + 2u * cnt;
    v->stage = b[k]; v->ch[0] = b[k + 1u]; v->ch[1] = b[k + 2u]; v->ch[2] = b[k + 3u]; v->ch[3] = b[k + 4u]; v->result = b[k + 5u]; v->reason = b[k + 6u];
    if (v->result > 2u || v->reason > 6u) return -1;
    v->tier[0] = v->tier[1] = 0xFFu;
    if (n == k + 9u) { v->tier[0] = b[k + 7u]; v->tier[1] = b[k + 8u]; }
    v->have = 1;
    return 0;
}

static int stand_req(gnc *g, mnc_u32 now, unsigned page, unsigned size, unsigned ver)
{
    wb w;
    if (!g->st || size < 1u || size > GNC_STAND_MAX || page > 255u) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w8(&w, ver); w8(&w, page); w8(&w, size);
    return send(g, GNC_OP_STAND, &w, now);
}
int gnc_standings(gnc *g, mnc_u32 now, unsigned page, unsigned size) { return stand_req(g, now, page, size, GNC_STAND_VER); }

static int stand_row(rd *r, struct gnc_stand_row *e)
{
    const mnc_u8 *b = r->b;
    if (r->i + 2u > r->n) return -1;
    e->rank = (mnc_u16) ((b[r->i] << 8) | b[r->i + 1u]);
    r->i += 2u;
    if (pstr(r, e->name, 12) || r->i + 6u > r->n) return -1;
    if (b[r->i] & ~1u) return -1;
    e->you = (mnc_u8) (b[r->i] & 1u);
    r->i += 1u;
    e->rating = (mnc_u16) ((b[r->i] << 8) | b[r->i + 1u]);
    e->tier = b[r->i + 2u];
    e->games = (mnc_u16) ((b[r->i + 3u] << 8) | b[r->i + 4u]);
    r->i += 5u;
    if (e->tier > GNC_TIER_MAX && e->tier != GNC_TIER_PLACEMENT) return -1;
    if (!e->name[0]) return -1;
    return 0;
}

int gnc_parse_standings(const mnc_u8 *b, mnc_u32 n, struct gnc_stand *o)
{
    rd r;
    unsigned k;
    if (n < 13u || n > 400u) return -1;
    if (b[0] != GNC_STAND_VER || (b[1] & ~GNC_STAND_F_MOCK)) return -1;
    o->flags = b[1];
    o->season = (mnc_u16) ((b[2] << 8) | b[3]);
    o->updated = g32(b + 4);
    o->total = (mnc_u16) ((b[8] << 8) | b[9]);
    o->page = b[10]; o->n = b[11];
    if (o->n > GNC_STAND_MAX || o->n > o->total) return -1;
    r.b = b; r.n = n; r.i = 12;
    for (k = 0; k < o->n; k++) if (stand_row(&r, &o->e[k])) return -1;
    if (r.i >= n || b[r.i] > 1u) return -1;
    o->have_you = b[r.i++];
    if (o->have_you && (stand_row(&r, &o->you) || (!GNC_LAB_MOCK_YOU && (o->flags & GNC_STAND_F_MOCK)))) return -1;     /* never a 'you' row in example data, except in a LAB build (SG_LAB=1: the lab server's --standings-mock-you marks one mock row as yours to try X: Me) */
    if (r.i + 1u != n || b[r.i] < 1u || b[r.i] > 31u) return -1;
    o->days_left = b[r.i++];                                                                                                  /* season days left, 1..31 */
    if (r.i != n) return -1;
    return 0;
}

/* ---- Tournament ops (wire 10.4): requests and strict parsers ---- */
static int tn_req(gnc *g, mnc_u32 now, mnc_u8 op, unsigned tid, int with_page, unsigned page)
{
    wb w;
    if (!g->tn || tid > 0xFFFFu) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w16(&w, tid);
    if (with_page) w8(&w, page);
    return send(g, op, &w, now);
}
int gnc_tn_list(gnc *g, mnc_u32 now, unsigned kind, unsigned offset)
{
    wb w;
    if (!g->tn || kind > 1u || offset > 255u) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w8(&w, kind); w8(&w, offset); w8(&w, GNC_TN_VER);                          /* version 4: next_cup_secs, the seconds until the start, the 8 digit id, the scheduled flag, the region; 5: the region may be 7 (NAC) */
    return send(g, GNC_OP_TN_LIST, &w, now);
}
int gnc_tn_info(gnc *g, mnc_u32 now, unsigned tid)
{
    wb w;
    if (!g->tn || tid > 0xFFFFu) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w16(&w, tid); w8(&w, GNC_TN_VER);                                                   /* version 3: official, secs_until_start, reason, next_cup_secs, then tid, id, sign-up seconds left ... */
    return send(g, GNC_OP_TN_INFO, &w, now);
}
int gnc_tn_info_id(gnc *g, mnc_u32 now, mnc_u32 pubid)
{
    wb w;
    if (!g->tn || pubid < 10000000ul || pubid > 99999999ul) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w16(&w, 0); w8(&w, GNC_TN_VER); w32(&w, pubid);                                     /* tid 0 + version 3 + the id */
    return send(g, GNC_OP_TN_INFO, &w, now);
}
static int tn_pin_ok(const char *pin)
{
    unsigned i;
    if (!pin || !pin[0]) return 1;
    for (i = 0; i < 8u; i++) if (pin[i] < '0' || pin[i] > '9') return 0;
    return pin[8] == 0;                                                        /* exactly 8 digits or none */
}
int gnc_tn_create(gnc *g, mnc_u32 now, const char *name, unsigned cap, unsigned format, unsigned checkin, const char *pin, unsigned signup_min, unsigned seeding, unsigned host_plays, mnc_u32 start_in, unsigned region)
{
    wb w;
    if (!g->tn || cap < 2u || cap > 64u || format > 1u || (format == 0u && cap < 3u) || (checkin != 0u && checkin != 5u && checkin != 10u) || !tn_pin_ok(pin) ||
        (signup_min != 15u && signup_min != 30u && signup_min != 60u && signup_min != 120u) || seeding > 2u || host_plays > 1u || region > GNC_TN_REGION_MAX || (start_in != 0ul && (start_in < 300ul || start_in > 2592000ul))) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    wstr(&w, name ? name : "", 12); w8(&w, cap); w8(&w, format); w8(&w, checkin); wstr(&w, pin ? pin : "", 8); w8(&w, 0);
    w8(&w, 3); w8(&w, signup_min); w8(&w, seeding); w8(&w, host_plays); w32(&w, start_in); w8(&w, region);      /* the version 3 tail: the version 2 tail + region (0 Any, 1..7 NAE NAW EU LA AS OC NAC) */
    return send(g, GNC_OP_TN_CREATE, &w, now);
}
int gnc_tn_join(gnc *g, mnc_u32 now, unsigned tid, const char *pin)
{
    wb w;
    if (!g->tn || tid > 0xFFFFu || !tn_pin_ok(pin)) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w16(&w, tid); wstr(&w, pin ? pin : "", 8);
    return send(g, GNC_OP_TN_JOIN, &w, now);
}
int gnc_tn_join_id(gnc *g, mnc_u32 now, mnc_u32 pubid, const char *pin)
{
    wb w;
    if (!g->tn || pubid < 10000000ul || pubid > 99999999ul || !tn_pin_ok(pin)) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w16(&w, 0); wstr(&w, pin ? pin : "", 8); w32(&w, pubid);
    return send(g, GNC_OP_TN_JOIN, &w, now);
}
int gnc_tn_leave(gnc *g, mnc_u32 now, unsigned tid) { return tn_req(g, now, GNC_OP_TN_LEAVE, tid, 0, 0); }
int gnc_tn_checkin(gnc *g, mnc_u32 now, unsigned tid) { return tn_req(g, now, GNC_OP_TN_CHECKIN, tid, 0, 0); }
int gnc_tn_bracket(gnc *g, mnc_u32 now, unsigned tid, unsigned page)
{
    wb w;
    if (!g->tn || tid > 0xFFFFu || page > 255u) { fail(g, GNC_E_ARG, 0); return GNC_E_ARG; }
    begin(g, &w);
    w16(&w, tid); w8(&w, page); w8(&w, 2);                                    /* version 2: the reply carries `me` and the 12-byte match items */
    return send(g, GNC_OP_TN_BRACKET, &w, now);
}
int gnc_tn_mymatch(gnc *g, mnc_u32 now, unsigned tid) { return tn_req(g, now, GNC_OP_TN_MYMATCH, tid, 0, 0); }
int gnc_tn_start(gnc *g, mnc_u32 now, unsigned tid) { return tn_req(g, now, GNC_OP_TN_START, tid, 0, 0); }
int gnc_tn_cancel(gnc *g, mnc_u32 now, unsigned tid) { return tn_req(g, now, GNC_OP_TN_CANCEL, tid, 0, 0); }

static int tn_u8(rd *r, mnc_u8 *o) { if (r->i >= r->n) return -1; *o = r->b[r->i++]; return 0; }
static int tn_u16(rd *r, mnc_u16 *o) { if (r->i + 2u > r->n) return -1; *o = (mnc_u16) ((r->b[r->i] << 8) | r->b[r->i + 1u]); r->i += 2u; return 0; }
static int tn_info(rd *r, struct gnc_tn_info *o)
{
    if (pstr(r, o->name, 12) || pstr(r, o->host, 12)) return -1;
    if (tn_u8(r, &o->state) || tn_u8(r, &o->entrants) || tn_u8(r, &o->checked) || tn_u8(r, &o->cap) || tn_u8(r, &o->format) || tn_u8(r, &o->seeding) ||
        tn_u8(r, &o->eflags) || tn_u8(r, &o->round) || tn_u8(r, &o->rounds) || tn_u8(r, &o->rev) || tn_u16(r, &o->checkin_left)) return -1;
    if (r->i + 4u > r->n) return -1;
    o->rules_hash = g32(r->b + r->i); r->i += 4u;
    if (pstr(r, o->champion, 12)) return -1;
    o->official = 0; o->reason = 0; o->secs = 0; o->next_cup = 0xFFFFFFFFul;
    if (r->i < r->n) {                                                         /* the version 2 tail */
        if (tn_u8(r, &o->official) || r->i + 4u > r->n) return -1;
        o->secs = g32(r->b + r->i); r->i += 4u;
        if (tn_u8(r, &o->reason) || r->i + 4u > r->n) return -1;
        o->next_cup = g32(r->b + r->i); r->i += 4u;
        if (o->official > 1u || o->reason > 2u) return -1;
    }
    o->region = 0;
    o->tid = 0; o->pubid = 0; o->signup_left = 0; o->start_at = 0; o->signup_min = 0; o->host_plays = 1; o->scheduled = 0;
    if (r->i < r->n) {                                                         /* the version 3 tail: tid u16, id u32, seconds left u32, signup_min u8, host_plays u8, scheduled u8, start_at u32 */
        if (tn_u16(r, &o->tid) || r->i + 8u > r->n) return -1;
        o->pubid = g32(r->b + r->i); r->i += 4u;
        o->signup_left = g32(r->b + r->i); r->i += 4u;
        if (tn_u8(r, &o->signup_min) || tn_u8(r, &o->host_plays) || tn_u8(r, &o->scheduled) || r->i + 4u > r->n) return -1;
        o->start_at = g32(r->b + r->i); r->i += 4u;
        if (tn_u8(r, &o->region) || o->region > GNC_TN_REGION_MAX) return -1;                 /* version 4: region u8 closes the tail */
        if (o->host_plays > 1u || o->scheduled > 1u || o->pubid < 10000000ul || o->pubid > 99999999ul) return -1;
    }
    if (o->state > 4u || o->format > 1u || o->seeding > 2u || o->cap < 2u || o->cap > 64u || o->entrants > o->cap || o->rounds > 6u || o->round > o->rounds || 0) return -1;
    return 0;
}
int gnc_parse_tn(unsigned op, const mnc_u8 *b, mnc_u32 n, struct gnc_tn *o)
{
    rd r;
    unsigned k;
    r.b = b; r.n = n; r.i = 0;
    switch (op) {
    case GNC_OP_TN_LIST:
        if (tn_u8(&r, &o->flags) || r.i + 4u > n) return -1;
        o->next_cup = g32(b + r.i); r.i += 4u;                                  /* version 2 (the only one asked for) */
        if (tn_u8(&r, &o->total) || tn_u8(&r, &o->n) || (o->flags & ~3u) || o->n > GNC_TN_LIST_MAX || o->n > o->total) return -1;
        for (k = 0; k < o->n; k++) {
            struct gnc_tn_ent *e = &o->e[k];
            if (tn_u16(&r, &e->tid) || pstr(&r, e->name, 12) || tn_u8(&r, &e->state) || tn_u8(&r, &e->entrants) || tn_u8(&r, &e->cap) || tn_u8(&r, &e->format) || tn_u8(&r, &e->my) || tn_u8(&r, &e->eflags) || r.i + 4u > n) return -1;
            e->secs = g32(b + r.i); r.i += 4u;
            if (r.i + 9u > n) return -1;                                        /* version 3: id u32, seconds to the start u32, scheduled u8 */
            e->pubid = g32(b + r.i); e->signup_left = g32(b + r.i + 4u); e->scheduled = b[r.i + 8u]; r.i += 9u;
            if (r.i + 1u > n) return -1;                                        /* version 4: region u8 */
            e->region = b[r.i++];
            if (e->region > GNC_TN_REGION_MAX || e->pubid < 10000000ul || e->pubid > 99999999ul || e->scheduled > 1u) return -1;
            if (!e->name[0] || e->state > 4u || e->format > 1u || e->my > 5u || e->cap < 2u || e->cap > 64u || e->entrants > e->cap) return -1;
        }
        return r.i == n ? 0 : -1;
    case GNC_OP_TN_INFO:
        return tn_info(&r, &o->info) || r.i != n ? -1 : 0;
    case GNC_OP_TN_CREATE:
        if (tn_u16(&r, &o->tid) || r.i + 4u != n || !o->tid) return -1;
        o->pubid = g32(b + r.i);
        return (o->pubid < 10000000ul || o->pubid > 99999999ul) ? -1 : 0;
    case GNC_OP_TN_JOIN: case GNC_OP_TN_LEAVE:
        if (tn_u8(&r, &o->result) || o->result > 2u) return -1;
        o->pubid = 0;
        if (op == GNC_OP_TN_JOIN && r.i != n) {                                 /* join by id: result, tid u16, id u32 */
            if (tn_u16(&r, &o->tid) || r.i + 4u != n || !o->tid) return -1;
            o->pubid = g32(b + r.i);
            return (o->pubid < 10000000ul || o->pubid > 99999999ul) ? -1 : 0;
        }
        return r.i == n ? 0 : -1;
    case GNC_OP_TN_START:
        if (tn_u8(&r, &o->result) || r.i != n || o->result < 1u || o->result > 2u) return -1;
        return 0;
    case GNC_OP_TN_CHECKIN: case GNC_OP_TN_CANCEL:
        return n == 0u ? 0 : -1;
    case GNC_OP_TN_BRACKET: {
        unsigned mpages;
        if (tn_u8(&r, &o->b_state) || tn_u8(&r, &o->b_rev) || tn_u8(&r, &o->b_size) || tn_u8(&r, &o->b_entrants) || tn_u8(&r, &o->b_rounds) || tn_u8(&r, &o->b_format) ||
            tn_u8(&r, &o->b_nm) || tn_u8(&r, &o->b_me) || tn_u8(&r, &o->b_page) || tn_u8(&r, &o->b_pages) || tn_u8(&r, &o->b_n)) return -1;
        if (o->b_state > 4u || o->b_size > 64u || (o->b_size & (o->b_size - 1u)) || o->b_size == 1u || o->b_entrants > 64u || o->b_rounds > 6u || o->b_format > 1u ||
            o->b_nm > GNC_TN_MAXM || o->b_pages > 8u || (o->b_me != 0xFFu && o->b_me >= o->b_entrants)) return -1;
        mpages = (o->b_nm + GNC_TN_MPAGE - 1u) / GNC_TN_MPAGE;
        o->b_is_match = (mnc_u8) (o->b_page < mpages);
        if (o->b_is_match) {
            if (o->b_n > GNC_TN_MPAGE) return -1;
            for (k = 0; k < o->b_n; k++) {
                struct gnc_tn_m *m = &o->m[k];
                if (r.i + 12u > n) return -1;
                m->mid = r.b[r.i]; m->side = r.b[r.i + 1u]; m->round = r.b[r.i + 2u]; m->state = r.b[r.i + 3u]; m->a = r.b[r.i + 4u]; m->b = r.b[r.i + 5u];
                m->wa = r.b[r.i + 6u]; m->wb = r.b[r.i + 7u]; m->win = r.b[r.i + 8u]; m->bo = r.b[r.i + 9u]; m->next_win = r.b[r.i + 10u]; m->next_lose = r.b[r.i + 11u];
                r.i += 12u;
                if (m->mid < 1u || m->mid > o->b_nm || m->side > 2u || m->round < 1u || m->state > 7u || m->win > 2u || (m->bo != 1u && m->bo != 3u && m->bo != 5u) ||
                    m->next_win > o->b_nm || m->next_lose > o->b_nm ||
                    (m->a != 0xFFu && m->a >= o->b_entrants) || (m->b != 0xFFu && m->b >= o->b_entrants)) return -1;
            }
        } else {
            if (o->b_n > GNC_TN_NPAGE || o->b_n > o->b_entrants) return -1;
            for (k = 0; k < o->b_n; k++) {
                if (pstr(&r, o->names[k], 12) || !o->names[k][0] || tn_u8(&r, &o->nstate[k]) || tn_u8(&r, &o->nseed[k]) || o->nstate[k] > 3u) return -1;
            }
        }
        return r.i == n ? 0 : -1;
    }
    case GNC_OP_TN_MYMATCH: {
        struct gnc_tn_my *y = &o->my;
        if (tn_u8(&r, &y->kind) || tn_u8(&r, &y->rev) || y->kind > 7u) return -1;
        y->mid = y->side = y->round = y->rounds = y->best_of = y->place = 0; y->opp[0] = y->code[0] = y->room[0] = 0; y->secs = 0;
        if (y->kind == 2u) {
            if (tn_u8(&r, &y->mid) || tn_u8(&r, &y->side) || tn_u8(&r, &y->round) || tn_u8(&r, &y->rounds) || pstr(&r, y->opp, 12) || pstr(&r, y->code, 8) || pstr(&r, y->room, 8) ||
                tn_u8(&r, &y->best_of) || tn_u16(&r, &y->secs)) return -1;
            if (!y->room[0] || y->side > 2u || (y->best_of != 1u && y->best_of != 3u && y->best_of != 5u) || y->rounds > 6u || y->round < 1u) return -1;
        } else if (y->kind == 3u) {
            if (tn_u8(&r, &y->round) || tn_u8(&r, &y->rounds) || tn_u8(&r, &y->side) || y->side > 2u) return -1;
        } else if (y->kind == 4u) {
            if (tn_u8(&r, &y->place) || tn_u8(&r, &y->round) || tn_u8(&r, &y->side) || y->side > 2u) return -1;
        }
        return r.i == n ? 0 : -1;
    }
    default: return -1;
    }
}

static void on_reply(gnc *g, const mnc_u8 *b, mnc_u32 n)
{
    switch (g->op) {
    case GNC_OP_TN_LIST: case GNC_OP_TN_INFO: case GNC_OP_TN_CREATE: case GNC_OP_TN_JOIN: case GNC_OP_TN_LEAVE: case GNC_OP_TN_CHECKIN:
    case GNC_OP_TN_BRACKET: case GNC_OP_TN_MYMATCH: case GNC_OP_TN_START: case GNC_OP_TN_CANCEL:
        if (!g->tn || gnc_parse_tn(g->op, b, n, g->tn)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        break;
    case GNC_OP_STAND:
        if (!g->st || gnc_parse_standings(b, n, g->st)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        break;
    case GNC_OP_LIST:
        if (gnc_parse_list(b, n, g->lv4 ? 4 : g->lv5 ? 5 : 6, &g->total, &g->page, &g->n, g->e)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        g->list_all = (mnc_u8) (!g->lv4 && (b[3] & GNC_LF_ALL_STAGES) != 0u);
        g->list_items = (mnc_u8) (!g->lv4 && !g->lv5 && (b[3] & GNC_LF_ITEMS) != 0u);
        g->list_rt = (mnc_u8) (!g->lv4 && !g->lv5 && (b[3] & GNC_LF_TYPES) != 0u);
        break;
    case GNC_OP_RK_STATE:
    case GNC_OP_RK_ACTION:
        if (gnc_parse_setview(b, n, &g->sv)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        g->have_set = 1;
        break;
    case GNC_OP_MATCH_STATUS:
        if (n != 1u) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        g->rk_match = b[0];
        break;
    case GNC_OP_LEAVE:
        if (n) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        break;
    case GNC_OP_CREATE: {
        rd r;
        r.b = b; r.n = n; r.i = 0;
        if (pstr(&r, g->created, 8) || r.i >= n || b[r.i] != 1u || gnc_parse_view(b + r.i + 1u, n - r.i - 1u, &g->v)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        g->have_view = 1;
        break;
    }
    case GNC_OP_STATE:
        if (n == 2u && b[0] == 0u) { g->unchanged = 1; g->v.rev = b[1]; break; }
        if (n >= 1u && b[0] == 3u) { if (gnc_parse_roster(b + 1, n - 1u, &g->ros)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; } g->have_roster = 1; break; }
        if (n < 1u || b[0] != 1u || gnc_parse_view(b + 1, n - 1u, &g->v)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        g->have_view = 1;
        break;
    default:
        if (n < 1u || b[0] != 1u || gnc_parse_view(b + 1, n - 1u, &g->v)) { g->st_bad++; fail(g, GNC_E_BAD_REPLY, 0); return; }
        g->have_view = 1;
        break;
    }
    g->state = GNC_DONE;
}

int gnc_poll(gnc *g, mnc_u32 now)
{
    mnc *c = g->c;
    int i;
    if (g->state != GNC_BUSY) return g->state;
    for (i = 0; i < MNC_RX_BUDGET; i++) {
        mnc_u8 st = 0, op;
        mw2_u32 bl = 0;
        int n = c->io.recv(c->io.ctx, c->rx, MNC_MAX_PACKET), r;
        if (n <= 0) break;
        g->st_rx++;
        if (n < (int) MW2_HDR || n > MNC_MAX_PACKET) { g->st_bad++; continue; }
        op = (mnc_u8) (c->rx[2] & 0x7Fu);
        if (op != g->op || g32(c->rx + 8) != g->ctr) { g->st_bad++; continue; }
        r = mw2_open(&c->w, now, c->rx, (mnc_u32) n, op, g->ctr, &st, &bl);
        if (r == 2) { fail(g, GNC_E_NOSESSION, 0); return g->state; }
        if (r != 1) { g->st_bad++; continue; }
        if (st != 0) {
            if (g->op == GNC_OP_LIST && !g->lv4 && st == GNC_ST_BAD_REQUEST) {         /* an older server does not know GNC_VER_RG7: ask v6 without it; nor list v6: ask v5; nor v5: ask v4 (All stages stays off) */
                if (!g->lv5 && !g->nrg7) g->nrg7 = 1; else if (!g->lv5) g->lv5 = 1; else g->lv4 = 1;
                g->list_all = 0; g->list_items = 0; g->list_rt = 0; g->state = GNC_IDLE;
                if (gnc_list_f(g, now, g->lreq_page, g->lreq_filter) == 0) return g->state;
            }
            if ((g->op == GNC_OP_JOIN || g->op == GNC_OP_CREATE) && g->vver != GNC_VIEW_V5 && !g->nrg7 && st == GNC_ST_BAD_REQUEST) {    /* a server that predates NAC refuses GNC_VER_RG7: once more without it (also for a room type) */
                mnc_u8 kind = g->jkind;
                g->nrg7 = 1; g->state = GNC_IDLE;
                if ((kind == 1u ? gnc_join(g, now, g->jcode, g->jpin, g->jch) : gnc_create_rt(g, now, g->jname, g->jflags, g->jpolicy, g->jpin, g->jcap, g->jch, g->jsm, g->jrt)) == 0) return g->state;
            }
            if ((g->op == GNC_OP_JOIN || (g->op == GNC_OP_CREATE && g->jrt == GNC_RT_NONE)) && g->vver != GNC_VIEW_V5 && !g->jretry && st == GNC_ST_BAD_REQUEST) {   /* an older server does not know view v6: once more with v5 (a room type cannot be asked for then) */
                mnc_u8 kind = g->jkind;
                g->jretry = 1; g->vver = GNC_VIEW_V5; g->state = GNC_IDLE;
                if ((kind == 1u ? gnc_join(g, now, g->jcode, g->jpin, g->jch) : gnc_create_sm(g, now, g->jname, g->jflags, g->jpolicy, g->jpin, g->jcap, g->jch, g->jsm)) == 0) return g->state;
            }
            fail(g, GNC_E_SERVER, st); return g->state;
        }
        on_reply(g, c->rx + MW2_HDR, (mnc_u32) bl);
        return g->state;
    }
    if (now - g->sent_ms >= g->timeout_ms) {
        if (g->tries >= GNC_TRIES) { fail(g, GNC_E_TIMEOUT, 0); return g->state; }
        g->tries++; g->sent_ms = now; g->st_resent++;
        (void) c->io.send(c->io.ctx, g->pkt, g->pkt_len);         /* the same counter: the server's reply cache answers a repeat, nothing runs twice */
    }
    return g->state;
}

void gnc_abort(gnc *g) { g->state = GNC_IDLE; }
