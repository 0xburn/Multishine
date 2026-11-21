/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Sign-in screens: see sg_ui.h and.  Strict C89, no libc, no heap. */
#include "sg_ui.h"
#include "../upd/upd_ident.h"

/* ---- line builder (no libc) ---- */
unsigned sg_cut_count;                /* characters that did not fit a row (host test asserts 0 on every screen) */
struct ln { char b[SG_COLS + 1]; unsigned n; };

static void ln_init(struct ln *l) { l->n = 0; l->b[0] = 0; }
static struct ln *ln_s(struct ln *l, const char *s)
{
    while (*s && l->n < SG_COLS) l->b[l->n++] = *s++;
    if (*s) sg_cut_count++;
    l->b[l->n] = 0;
    return l;
}
static struct ln *ln_at(struct ln *l, unsigned col)         /* pad with spaces up to a column */
{
    while (l->n < col && l->n < SG_COLS) l->b[l->n++] = ' ';
    l->b[l->n] = 0;
    return l;
}
char *sg_u(char *p, sdc_u32 v)
{
    char t[11];
    int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { t[n++] = (char)('0' + v % 10u); v /= 10u; }
    while (n) *p++ = t[--n];
    return p;
}
static struct ln *ln_u(struct ln *l, sdc_u32 v, unsigned width)    /* right aligned in `width` columns */
{
    char t[12], *e = sg_u(t, v);
    unsigned len = (unsigned)(e - t), i;
    for (i = len; i < width && l->n < SG_COLS; i++) l->b[l->n++] = ' ';
    for (i = 0; i < len && l->n < SG_COLS; i++) l->b[l->n++] = t[i];
    l->b[l->n] = 0;
    return l;
}
static struct ln *ln_m(struct ln *l, sdc_u32 mbits, unsigned width)  /* milli-bits as "12.3" */
{
    struct ln t;
    unsigned i;
    ln_init(&t);
    ln_u(&t, mbits / 1000u, 0);
    ln_s(&t, ".");
    ln_u(&t, (mbits % 1000u) / 100u, 0);
    for (i = t.n; i < width && l->n < SG_COLS; i++) l->b[l->n++] = ' ';
    ln_s(l, t.b);
    return l;
}
#if SG_LAB
static struct ln *ln_ms(struct ln *l, sdc_u32 us, unsigned width)    /* microseconds as "3.4 ms" */
{
    struct ln t;
    unsigned i;
    ln_init(&t);
    ln_u(&t, us / 1000u, 0);
    ln_s(&t, ".");
    ln_u(&t, (us % 1000u) / 100u, 0);
    ln_s(&t, " ms");
    for (i = t.n; i < width && l->n < SG_COLS; i++) l->b[l->n++] = ' ';
    ln_s(l, t.b);
    return l;
}

#endif
static void put(struct sg_view *v, unsigned r, unsigned attr, const char *s)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (s[c] && c < SG_COLS) { v->t[r][c] = s[c]; c++; }
    if (s[c]) sg_cut_count++;
    v->t[r][c] = 0;
    v->attr[r] = (unsigned char)attr;
}
static void putl(struct sg_view *v, unsigned r, unsigned attr, const struct ln *l) { put(v, r, attr, l->b); }

static void bar(struct ln *l, sdc_u32 pct)                          /* [=====>    ] 47 */
{
    unsigned i, fill = (unsigned)(pct * 16u / 100u);
    for (i = 0; i < 16; i++) ln_s(l, i < fill ? "I" : ".");      /* glyphs of the menu font only: I and. */
    ln_s(l, "  ");
    ln_u(l, pct, 3);
    ln_s(l, " percent");
}

/* ---- the Settings look (ml_ui.h): header, right aligned values, selection bar; the resident draws them (ml_glue.c ui_*) ---- */
static void cpy(char *d, unsigned cap, const char *s)
{
    unsigned n = 0;
    while (s[n] && n < cap) { d[n] = s[n]; n++; }
    if (s[n]) sg_cut_count++;
    d[n] = 0;
}
void sg_putv(struct sg_view *v, unsigned r, unsigned attr, const char *s)      /* the value of row r, right aligned at the right margin */
{
    if (r >= SG_ROWS) return;
    cpy(v->val[r], SG_VAL, s);
    v->vattr[r] = (unsigned char)attr;
}
void sg_head_t(struct sg_view *v, const char *title)        /* the mark and the title (row 0); no breadcrumb (the player does not need to be told where they are) */
{
    v->mark = 1;
    put(v, UI_R_TITLE, SG_A_TITLE, title);
}
/* THE QR PAGE, shared by Link this console and Send Slippi login (one look): the header bar and title above, the code in rows 3 to 18 (SG_QR_ROWS, below the
   title), then two lines of text, the short code in yellow, and up to two status lines; the footer is the caller's (row SG_ROWS - 2).  Same rows on both pages. */
void sg_qr_page(struct sg_view *v, unsigned n, const unsigned char *bits, const char *l1, const char *l2, const char *code, const char *s1, const char *s2)
{
    unsigned i;
    if (bits) {                                                 /* no bits: the code could not be made, the text stays */
        v->qr_n = (unsigned char)n; v->qr_row = SG_QR_ROW; v->qr_rows = SG_QR_ROWS;
        for (i = 0; i < QR_OUT_BYTES; i++) v->qr[i] = bits[i];
    }
    put(v, SG_QR_ROW + SG_QR_ROWS, SG_A_BODY, l1);
    put(v, SG_QR_ROW + SG_QR_ROWS + 1u, SG_A_BODY, l2);
    put(v, SG_QR_ROW + SG_QR_ROWS + 2u, SG_A_HI, code);
    if (s1) put(v, SG_QR_ROW + SG_QR_ROWS + 3u, SG_A_DIM, s1);
    if (s2) put(v, SG_QR_ROW + SG_QR_ROWS + 4u, SG_A_DIM, s2);
}
void sg_head_tag(struct sg_view *v, unsigned attr, const char *tag)           /* a word at the right of the title row: PASS, FAIL, LIVE */
{
    sg_putv(v, UI_R_TITLE, attr | SG_A_BIG, tag);
}
void sg_bar(struct sg_view *v, unsigned r, unsigned big) { v->bar = (unsigned char)r; v->bar_big = (unsigned char)big; }
static void head_old(struct sg_view *v, const char *title, const char *tag)       /* the old gold header line (the Mockups screens and the lab TCP test, outside the Settings look) */
{
    struct ln l;
    ln_init(&l);
    ln_s(&l, title);
    ln_at(&l, 36);
    ln_s(&l, tag);
    put(v, 0, SG_A_HEAD, l.b);
}
static void foot(struct sg_view *v, const char *s) { put(v, SG_ROWS - 2, SG_A_DIM, s); }


/* ---- Controller test  ---- */
static int ctl_abs(int v) { return v < 0 ? -v : v; }
static int ctl_same(const struct sdent_pad *a, const struct sdent_pad *b)
{
    return a->buttons == b->buttons && a->sx == b->sx && a->sy == b->sy && a->cx == b->cx && a->cy == b->cy && a->tl == b->tl && a->tr == b->tr && a->err == b->err;
}
void sg_ctl_start(struct sg_ctl_t *t, unsigned port)
{
    unsigned char *b = (unsigned char *)t;
    unsigned i;
    for (i = 0; i < sizeof *t; i++) b[i] = 0;
    t->port = (unsigned char)port;
}
void sg_ctl_ports(struct sg_ctl_t *t, const unsigned char err[4])
{
    unsigned i;
    t->others = 0;
    for (i = 0; i < 4; i++) if (i != t->port && err[i] == 0) t->others |= (unsigned char)(1u << i);
}
void sg_ctl_frame(struct sg_ctl_t *t, const struct sdent_pad *p, sdc_u32 tb, sdc_u32 lim)
{
    sdc_u32 u = (lim ? lim : SG_HOLD_B_TICKS) / 4u;           /* 0.25 s of time base: a quarter of the 1.0 s hold-B limit */
    sdc_u32 s1 = u * 4u, s5 = u * 20u;
    int v[4], i;
    /* Z held for 1 s toggles the freeze (once per hold); this runs while frozen too */
    if (p->buttons & SG_B_Z) {
        if (!t->zlatch) {
            if (t->zhold == 0) t->zhold_tb = tb;
            if (t->zhold < 65535u) t->zhold++;
            if (t->zhold >= 60u || (t->zhold > 1u && (sdc_u32)(tb - t->zhold_tb) >= s1)) {
                t->frozen = (unsigned char)!t->frozen;
                t->zlatch = 1; t->zhold = 0;
                if (t->frozen) t->frozen_at = t->frames;
                else { t->same = 0; t->same_tb = tb; }
            }
        }
    } else { t->zhold = 0; t->zlatch = 0; }
    if (t->frozen) return;
    t->frames++;
    v[0] = p->sx; v[1] = p->sy; v[2] = p->cx; v[3] = p->cy;
    if (!t->seen) {
        for (i = 0; i < 4; i++) { t->mn[i] = t->mx[i] = (signed char)v[i]; }
        t->wlo[0] = t->whi[0] = p->sx; t->wlo[1] = t->whi[1] = p->sy;
        t->still = 0; t->same = 0; t->same_tb = tb; t->seen = 1;
    } else {
        for (i = 0; i < 4; i++) { if (v[i] < t->mn[i]) t->mn[i] = (signed char)v[i]; if (v[i] > t->mx[i]) t->mx[i] = (signed char)v[i]; }
        for (i = 0; i < 2; i++) { if (v[i] < t->wlo[i]) t->wlo[i] = (signed char)v[i]; if (v[i] > t->whi[i]) t->whi[i] = (signed char)v[i]; }
        if (t->whi[0] - t->wlo[0] >= SG_CTL_MOVE || t->whi[1] - t->wlo[1] >= SG_CTL_MOVE) {
            t->wlo[0] = t->whi[0] = p->sx; t->wlo[1] = t->whi[1] = p->sy;
            t->still = 0;
        } else if (t->still < 65535u) t->still++;
        if (ctl_same(p, &t->prev)) { if (t->same < 65535u) t->same++; }
        else { t->same = 0; t->same_tb = tb; }
    }
    t->cur = *p; t->prev = *p;
    t->moving = (unsigned char)(t->still < SG_CTL_STILL_FRAMES);
    t->off_centre = (unsigned char)(!t->moving && (ctl_abs(p->sx) > SG_CTL_OFFC || ctl_abs(p->sy) > SG_CTL_OFFC));
    t->not_changing = (unsigned char)((p->buttons != 0 || ctl_abs(p->sx) > SG_CTL_AXIS_STUCK || ctl_abs(p->sy) > SG_CTL_AXIS_STUCK || ctl_abs(p->cx) > SG_CTL_AXIS_STUCK ||
                       ctl_abs(p->cy) > SG_CTL_AXIS_STUCK || p->tl > SG_CTL_AXIS_STUCK || p->tr > SG_CTL_AXIS_STUCK) &&
                      (t->same >= SG_CTL_SAME_FRAMES || (t->same > 1u && (sdc_u32)(tb - t->same_tb) >= s5)));
}

/* ---- entropy test engine ---- */
void sg_ent_start(struct sg_ent_t *t)
{
    sdc_u8 probe[16];
    unsigned i;
    sdent_init(&t->e);
    t->frame = 0; t->ones = 0; t->why = 0; t->out_ok = 0; t->out_checked = 0;
    /* a pool with no credit must refuse to give bytes (never weak output): asked once, before anything is collected */
    for (i = 0; i < sizeof probe; i++) probe[i] = 0xA5;
    t->starved_ok = (sdent_bytes(&t->e, SDENT_L_SESSION, probe, sizeof probe) == SDENT_E_STARVED) ? 1 : 0;
    for (i = 0; i < sizeof probe; i++) if (probe[i] != 0xA5) t->starved_ok = 0;     /* and must not have written anything */
    t->state = SG_ET_RUN;
}

static void ent_output_check(struct sg_ent_t *t)
{
    sdc_u8 buf[SG_ET_OUT_BYTES];
    unsigned i, b, same = 1;
    sdc_u32 ones = 0;
    if (sdent_bytes(&t->e, SDENT_L_SESSION, buf, SG_ET_OUT_BYTES) != 0) { t->out_ok = 0; t->out_checked = 1; t->why = 2; return; }
    for (i = 0; i < SG_ET_OUT_BYTES; i++) {
        for (b = 0; b < 8; b++) ones += (buf[i] >> b) & 1u;
        if (buf[i] != buf[0]) same = 0;
    }
    for (i = 0; i < SG_ET_OUT_BYTES; i++) buf[i] = 0;               /* nothing of it is kept or shown */
    t->ones = ones;
    /* 2048 bits: ones ~ N(1024, 22.6); +-100 is more than 4 sigma, so a good generator fails about once in 10^5 runs and a broken one at once */
    t->out_ok = (!same && ones >= 924u && ones <= 1124u) ? 1 : 0;
    t->out_checked = 1;
    if (!t->out_ok) t->why = 2;
}

void sg_ent_frame(struct sg_ent_t *t, const struct sdent_pad *p, sdc_u32 tb)
{
    if (t->state != SG_ET_RUN) return;
    sdent_feed_pad(&t->e, p, tb);
    t->frame++;
    if (sdent_ready(&t->e, SDENT_L_SESSION)) {
        ent_output_check(t);
        if (!t->starved_ok) t->why = 3;
        t->state = (t->out_ok && t->starved_ok) ? SG_ET_PASS : SG_ET_FAIL;
    } else if (t->frame >= SG_ET_TIMEOUT_FRAMES) {
        t->why = 1;
        t->state = SG_ET_FAIL;
    }
}

/* ---- TLS verdict ---- */
int sg_tls_verdict(const struct sg_tls_view *v)
{
    if (!v->started) return 0;
    if (v->failed) return 2;
    if (!v->done) return 0;
    if (v->echo_ok != 1u || v->down_bad || v->down_bytes != v->want_down || v->hello_ok != 1u || v->https_status != 200u || v->https_bad || !v->https_bytes) return 2;
    return v->heavy_max_us > SG_TLS_FRAME_US ? 3 : 1;
}

/* ---- QR texture ---- */
/* The code at 2 texels a module with a quiet zone of SG_QR_QUIET modules (the QR minimum is 4; the 104-texel texture holds at most 52 modules, so a big code gets less) in the top left corner of
   the texture; the sprite shows only that dim x dim part (sg_qr_dim), so the white square is code plus quiet zone and nothing more. */
static unsigned qr_quiet(unsigned n) { unsigned room = SG_QR_TEX_DIM / 2u > n ? (SG_QR_TEX_DIM / 2u - n) / 2u : 0u; return room < SG_QR_QUIET ? room : SG_QR_QUIET; }
unsigned sg_qr_dim(unsigned n)
{
    unsigned d = 2u * (n + 2u * qr_quiet(n));
    return d < SG_QR_TEX_DIM ? d : SG_QR_TEX_DIM;
}
void sg_qr_texture(const unsigned char *bits, unsigned n, unsigned char *out)
{
    unsigned tx, ty, stride = (n + 7u) / 8u, off = qr_quiet(n), i;                /* off: modules of white on each side */
    for (i = 0; i < SG_QR_TEX_BYTES; i++) out[i] = 0xFF;                          /* all white (intensity 15) */
    for (ty = 0; ty < SG_QR_TEX_DIM; ty++)
        for (tx = 0; tx < SG_QR_TEX_DIM; tx++) {
            unsigned mx = tx / 2u, my = ty / 2u;
            if (mx >= off && mx < off + n && my >= off && my < off + n) {
                unsigned c = mx - off, r = my - off;
                if ((bits[r * stride + (c >> 3)] >> (7 - (c & 7u))) & 1u) {
                    unsigned tile = (ty >> 3) * (SG_QR_TEX_DIM >> 3) + (tx >> 3);
                    unsigned at = tile * 32u + (ty & 7u) * 4u + ((tx & 7u) >> 1);
                    out[at] = (unsigned char)((tx & 1u) ? (out[at] & 0xF0u) : (out[at] & 0x0Fu));   /* dark = 0 in the texel's nibble (high nibble = left texel) */
                }
            }
        }
}

/* ---- navigation ---- */
void sg_init(struct sg *g)
{
    unsigned char *b = (unsigned char *)g;
    unsigned i;
    for (i = 0; i < sizeof *g; i++) b[i] = 0;
    g->scr = SG_HUB;
    g->hold_lim = SG_HOLD_B_TICKS;
    sgl_init(&g->lk);
    sg_cut_count = 0;
}

/* The Online page (Settings > Online;): the three identity rows, the two invite settings, Delete last.
   Up from the first row does not wrap onto Delete.  Lab builds add the TCP and TLS test row after Delete (it names a lab server; release builds have no such row or text). */
#define HUB_N SG_ON_SLOGIN                     /* the rows of the page: everything before the three moved rows */
#define HUB_UPD SG_ON_UPD
static const char *const ON_LABEL[SG_ON_N] = { "Device identity", "Link this console", "Friend requests after a fight", "Who can invite me",
#if SG_NETSEL
                                                "Server", "Slippi", "In use",
#endif
#if SG_LAB
                                                "TCP and TLS test",
#endif
                                                "Send Slippi login", "Update", "Delete: Multishine only", "Delete: Multishine and Slippi", "Delete: everything",
                                                "Region", "Who can watch my games",
};
static const char *const ON_DESC[SG_ON_N] = { "Name, connect code, Slippi login and delete.", "Use this console with your account.",
                                               "Let opponents send you a friend request.", "Who may invite you to a match.",
#if SG_NETSEL
                                               "Which game server this console uses.", "Matchmaking for Unranked and Direct.", "The server and Slippi address now in use.",
#endif
#if SG_LAB
                                               "Real test: TCP and TLS to the lab server.",
#endif
                                               "Get your Slippi login from your phone.", "Look for a newer Multishine build.", "Name, code and link go. Your account stays.", "Name, code, link and Slippi login go.", "Everything goes, the console key too.",
                                               "Where you play from. Helps find matches.", "Who may watch you live, outside Game Night.",
};
const char *sg_on_label(unsigned row) { return row < SG_ON_N ? ON_LABEL[row] : ""; }
const char *sg_on_desc(unsigned row) { return row < SG_ON_N ? ON_DESC[row] : ""; }

/* Settings > Online is a short list of three pages ("the online menu is a clusterfuck"): 1 Account, 2 Friends and
   privacy, 3 Delete.  The page of a row; 0 = not on any page (the update rows and the lab rows). */
static unsigned on_page(unsigned i)
{
    switch (i) {
    case SG_ON_IDENT: case SG_ON_LINK: case SG_ON_REGION: case SG_ON_SLOGIN: return 1u;
    case SG_ON_POL: case SG_ON_FIGHT: case SG_ON_WATCH: return 2u;
    case SG_ON_DEL: case SG_ON_DEL2: case SG_ON_DEL3: return 3u;
#if SG_LAB
    case SG_ON_TLS: return 1u;                                   /* lab builds: the TCP and TLS test row stays reachable (Account page) */
#endif
    default: return 0u;
    }
}
#define ON_PAGES 3u
static const char *const ONP_LABEL[ON_PAGES] = { "Account", "Friends", "Delete" };   /* "Friends and privacy" became "Friends" */
static const char *const ONP_DESC[ON_PAGES] = { "Name, code, link, region and Slippi login.", "Invites, friend requests and watching.", "Remove the identity from this console." };
static const unsigned char ONP_FIRST[ON_PAGES] = { SG_ON_IDENT, SG_ON_POL, SG_ON_DEL };
#define ONP_IN(g) ((g)->onp || (g)->dev)                /* the cursor is on a row (not on the list of pages) */

/* the Online page and its lab twin share the screen: the lab rows (Server, Slippi, In Use) belong to Settings > Diagnostics only; the three moved rows are never shown here; the
   start-check option is shown once the glue knows its value */
static int row_hidden(const struct sg *g, unsigned i)
{
#if SG_NETSEL
    int lab = i == SG_ON_SRV || i == SG_ON_MMR || i == SG_ON_NET;
    if (g->dev) return !lab;
    if (lab) return 1;
#else
    (void)g->dev;
#endif
    if (i == SG_ON_UPD) return 1;                   /* updates live in Settings > Update only */
    return on_page(i) != on_page(g->cur);                                /* only the rows of the open page */
}
/* the display order of the Online page rows (enum ids): the identity block first, then the settings (checklist 13) */
static const unsigned char ON_ORDER[] = { SG_ON_IDENT, SG_ON_LINK, SG_ON_REGION, SG_ON_SLOGIN, SG_ON_POL, SG_ON_FIGHT, SG_ON_WATCH, SG_ON_DEL, SG_ON_DEL2, SG_ON_DEL3,
#if SG_NETSEL
                                          SG_ON_SRV, SG_ON_MMR, SG_ON_NET,
#endif
#if SG_LAB
                                          SG_ON_TLS,
#endif
                                          SG_ON_UPD };
#define ON_ORDER_N ((unsigned) sizeof ON_ORDER)
static unsigned on_pos(unsigned id) { unsigned k; for (k = 0; k < ON_ORDER_N; k++) if (ON_ORDER[k] == id) return k; return 0; }
static unsigned hub_move(const struct sg *g, unsigned cur, int dir)      /* next visible row in display order; Down wraps, Up stops on the first visible row */
{
    unsigned k, p = on_pos(cur);
    for (k = 0; k < ON_ORDER_N; k++) {
        if (dir > 0) p = (p + 1u) % ON_ORDER_N;
        else { if (p == 0u) return cur; p--; }
        if (!row_hidden(g, ON_ORDER[p])) return ON_ORDER[p];
    }
    return cur;
}
static unsigned hub_row(unsigned from)     /* the Online row that opens a screen: Back lands on it (the cursor is not reset to the top: MENU_SWEEP 5) */
{
    switch (from) {
    case SG_IDENT: case SG_MAKEKEY: return SG_ON_IDENT;
    case SG_SLOGIN: return SG_ON_SLOGIN;
    case SG_DELID: return SG_ON_DEL;
    case SG_LINK: case SG_ENT: return SG_ON_LINK;
#if SG_LAB
    case SG_TLS: return SG_ON_TLS;
#endif
    default: return 0xFFu;                                     /* none: the Online page opens on its list of pages */
    }
}
void sg_hub_page(struct sg *g, unsigned page)
{
    if (page < 1u || page > ON_PAGES) return;
    g->id_top = 0; g->del_acct = 0;
    if (page == 1u) { sg_goto(g, SG_IDENT); g->id_top = 1; return; }       /* Settings > Account: the identity page itself (no Device identity step) */
    g->onp = 2; g->cur = ONP_FIRST[page - 1u]; g->hint = 0;
}
unsigned sg_acct_items(const struct sg *g, unsigned char out[4])
{
    unsigned n = 0;
    if (g->linked != SG_ID_LINKED && g->linked != SG_ID_RAMONLY) out[n++] = SG_AI_LINK;     /* linked: the row did nothing new */
    if (g->linked != SG_ID_GUEST) out[n++] = SG_AI_REGION;
    if (g->id_slippi != 2u) out[n++] = SG_AI_SLOGIN;                                     /* a Slippi login already on the card: nothing to send; Delete level 2 removes it */
    if (g->linked == SG_ID_GUEST) out[n++] = SG_AI_REGION;                               /* a guest's Region is greyed out: below Send Slippi login */
    out[n++] = SG_AI_DELETE;
    return n;
}
unsigned sg_acct_pos(const struct sg *g, unsigned item)
{
    unsigned char it[4];
    unsigned n = sg_acct_items(g, it), k;
    for (k = 0; k < n; k++) if (it[k] == item) return k;
    return 0;
}
void sg_goto(struct sg *g, unsigned scr)
{
    unsigned from = g->scr;
    g->dev = 0;
    if (scr == SG_HUB && g->id_top && !g->del_acct && (from == SG_LINK || from == SG_ENT || from == SG_MAKEKEY || from == SG_SLOGIN || from == SG_DELID)) {     /* back to the Account page */
        g->scr = SG_IDENT; g->confirm = 0;
        g->cur = (unsigned char)sg_acct_pos(g, from == SG_SLOGIN ? SG_AI_SLOGIN : from == SG_DELID ? SG_AI_DELETE : SG_AI_LINK);
        if (!g->rgn) g->fight_req = 1;
        return;
    }
    if (scr == SG_HUB && g->mk.on) scr = SG_MOCK;          /* a Mockups session: every way back to the hub lands on the Mockups hub (sg_mockup.c) */
    g->scr = (unsigned char)scr;
    g->cur = 0; g->confirm = 0;
    if (scr == SG_ABOUT) return;                          /* read-only text screen, nothing to start */
    if (scr == SG_FRD) { frd_enter(&g->fr, 1); return; }  /* the Friends screen (frd.c): the server's list */
    if (scr == SG_TOURN) { gnt_enter(&g->tn); return; }   /* the Tournament list (gnt.c): Loading, page asked */
    if (scr == SG_STAND) { stb_enter(&g->sd); return; }   /* the Grandmaster standings (stb.c): Loading, page 0 asked */
    if (scr == SG_DELID) { sgd_enter(g); return; }        /* Delete This Console's Identity (sg_delid.c) */
    if (scr == SG_SLOGIN) { sgr_enter(&g->sr, g->lk.now); return; }   /* Send Slippi Login (sgr.c) */
    if (scr >= SG_MOCK) { if (scr < SG_NSCREEN) sgm_enter(g, from); else g->scr = SG_HUB; return; }      /* the Mockups hub, and the Game Night browser (SG_GN), which every build keeps: sg_mockup.c */
    if (scr == SG_HUB) { unsigned r = hub_row(from); g->onp = r != 0xFFu ? (g->onp == 2u ? 2u : 1u) : 0u; g->cur = (unsigned char)(r != 0xFFu ? r : 0u); g->hint = 0; if (!g->fight || !g->pol || !g->rgn || !g->wch) g->fight_req = 1; }    /* the two invite settings are read when the page opens */
    if (scr == SG_IDENT && (!g->rgn || !g->fight || !g->pol || !g->wch)) g->fight_req = 1;    /* the Region value is read when the page opens (as the Online page) */
    if (scr == SG_UPD) g->upd_cur = 0;
    if (scr == SG_ENT || scr == SG_MAKEKEY) sg_ent_start(&g->ent);
    if (scr == SG_CTL) sg_ctl_start(&g->ctl, 0);
    if (scr == SG_TLS) {
#if !SG_LAB
        g->scr = SG_HUB;
        return;
#endif
        g->req_tls = 1; g->tls.started = 0; g->tls.done = 0; g->tls.failed = 0; }
    if (scr == SG_LINK) sgl_enter(g);                     /* Link This Console (sg_link.c) */
}

#define PROV_N 4

/* the Update screen shows its start-check option while nothing runs (the option moved here from the Online page) */
static int upd_opt_shown(const struct sg *g)
{
    unsigned st = g->upd.state;
    return st == SGU_IDLE || st == SGU_UPTODATE || st == SGU_FAILED || st == SGU_OFFLINE || st == SGU_AVAILABLE;      /* an update on offer (also a required one) keeps the rows: the backup switch matters most right before the install */
}

/* Card layout notice ("Migration of existing cards"): only an overlay built for the STOCK card layout (work area 0x817E4000, passed by tools/release.sh as
   MS_CARD_WA_ADDR) carries it; a generic or low layout build, and a lab build without the macro, compile it out.  It sits in the notes area, so the option rows step aside like for any note. */
#if defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817E4000u
#define SG_LAYOUT_NOTICE 1
#else
#define SG_LAYOUT_NOTICE 0
#endif
static int lay_note(const struct sgu_view *u)
{
    return SG_LAYOUT_NOTICE && u->state == SGU_UPTODATE && u->note_ours == SGU_NOTE_NONE && !u->note_slippi;
}

/* the Backup To Slot B row: with both advisory notes on screen the row would be the ninth live text object, so it waits (the notes go away after the update) */
static int bak_shown(const struct sg *g)
{
    return upd_opt_shown(g) && g->bak && !(g->upd.note_slippi && g->upd.note_ours != SGU_NOTE_NONE) && !lay_note(&g->upd);
}

/* Keep the previous build as backup (the B copies of the engine and the overlay; off = they are deleted once the new build has reached the Main Menu): the third option row, same rules as the Backup row */
static int keep_shown(const struct sg *g)
{
    return upd_opt_shown(g) && g->keep && !(g->upd.note_slippi && g->upd.note_ours != SGU_NOTE_NONE) && !lay_note(&g->upd);
}
/* Copy card to slot B: an action row below the two switches; not while an offer waits (the download buffer is held then), same two-notes rule */
static int copy_shown(const struct sg *g)
{
    unsigned st = g->upd.state;
    return g->cpy_ok && g->bak && upd_opt_shown(g) && st != SGU_AVAILABLE && !g->upd.note_slippi && g->upd.note_ours == SGU_NOTE_NONE && !lay_note(&g->upd);      /* any advisory note pushes the rows down: no third row then */
}
static unsigned upd_cur_bak(const struct sg *g)      /* the cursor value of the Backup row: always the first option row (the start check is mandatory,, no row) */
{
    (void)g;
    return 1u;
}
static unsigned upd_cur_keep(const struct sg *g)     /* the cursor value of the Keep row: below the Backup row when that is shown */
{
    return 1u + (bak_shown(g) ? 1u : 0u);
}
static unsigned upd_cur_copy(const struct sg *g)     /* the cursor value of the Copy row: below the switches that are shown */
{
    return 1u + (bak_shown(g) ? 1u : 0u) + (keep_shown(g) ? 1u : 0u);
}
static int upd_toggle(struct sg *g)                  /* A / Left / Right on an option row: 1 when one was changed */
{
    if (g->upd_cur == upd_cur_bak(g) && bak_shown(g)) { g->bak = (unsigned char)(3u - g->bak); g->bak_req = 1; return 1; }
    if (g->upd_cur == upd_cur_keep(g) && keep_shown(g)) { g->keep = (unsigned char)(3u - g->keep); g->keep_req = 1; return 1; }
    return 0;
}
static unsigned nitems(const struct sg *g)
{
    switch (g->scr) {
    case SG_HUB: return HUB_N;
    case SG_IDENT: { unsigned char it[4]; return sg_acct_items(g, it); }    /* the Account page's choosable rows (sg_acct_items) */
    case SG_UPD: { unsigned k = (bak_shown(g) ? 1u : 0u) + (keep_shown(g) ? 1u : 0u) + (copy_shown(g) ? 1u : 0u); return k ? k + 1u : 0u; }      /* Check Now + the option rows */
    case SG_PROV: return PROV_N + 1;
    default: return 0;
    }
}

#if SG_NETSEL
/* A / Left / Right on Server and Slippi.  An unknown value is read again; Real Slippi asks once (nconf) unless the resident remembers the answer (mmk). */
static void ns_change(struct sg *g, int dir)
{
    g->hint = 0;
    if (g->cur == SG_ON_SRV) {
        if (g->srv == SG_SRV_NA) g->hint = "Not available in this build.";
        else if (g->srv == 0) g->srv_req = 1;
        else if (g->srv_lock == 1u) g->hint = "This build has no baked server.";
        else {                                                   /* A and Right go on, Left goes back: Production, Dev, Lab, around */
            static const unsigned char ord[3] = { SG_SRV_VPS, SG_SRV_LAB, SG_SRV_LAN };
            int i = 0, n = SG_SRV_COUNT, k;
            for (k = 0; k < 3; k++) if (ord[k] == g->srv) i = k;
            g->srv = ord[(i + n + dir) % n];
            g->srv_req = 2;
        }
    } else if (g->cur == SG_ON_MMR) {
        if (g->mmr == SG_MMR_NA) g->hint = "Not available in this build.";
        else if (g->mmr == 0) g->mmr_req = 1;
        else if (g->mmr == SG_MMR_REAL) { g->mmr = SG_MMR_MOCK; g->mmr_req = 2; }
        else if (g->mmk) { g->mmr = SG_MMR_REAL; g->mmr_req = 2; }
        else g->nconf = 1;
    }
}
static void ns_confirm(struct sg *g, unsigned edge)         /* the one-time question: A switches on and remembers it, B leaves everything as it was */
{
    if (edge & SG_B_A) { g->nconf = 0; g->mmk = 1; g->mmr = SG_MMR_REAL; g->mmr_req = 2; }
    else if (edge & SG_B_B) g->nconf = 0;
}
static const char *ns_text(const struct sg *g, unsigned row)       /* the value of Server and Slippi (right aligned) */
{
    static const char *const sv[8] = { "...", "Production", "Dev", "Not available", "Lab", "...", "...", "..." }      /* display names only: the stored values SG_SRV_VPS / SG_SRV_LAB are unchanged */;
    static const char *const mv[4] = { "...", "Our mock", "Real Slippi", "Not available" };
    return row == SG_ON_SRV ? sv[g->srv & 7u] : mv[g->mmr & 3u];
}
static void ns_ip(struct ln *l, sdc_u32 ip)
{
    ln_u(l, ip >> 24, 0); ln_s(l, "."); ln_u(l, (ip >> 16) & 255u, 0); ln_s(l, "."); ln_u(l, (ip >> 8) & 255u, 0); ln_s(l, "."); ln_u(l, ip & 255u, 0);
}
#endif

/* Left / Right (and A) on the two invite settings: the glue writes the new value (fight_req 2 / 3) and the server's answer shows; an unknown value is read again */
static void on_change_row(struct sg *g, int dir, unsigned row);
static void on_change(struct sg *g, int dir) { on_change_row(g, dir, g->cur); }
static void on_change_row(struct sg *g, int dir, unsigned row)
{
    if (g->linked == SG_ID_GUEST) { g->hint = "Link this console first."; return; }
    if (row == SG_ON_FIGHT) {
        if (g->fight == 1u || g->fight == 2u) { g->fight = (unsigned char)(3u - g->fight); g->fight_req = 2; }
        else if (g->fight != 0u || !g->fight_req) { g->fight = 0; g->fight_req = 1; }
    } else if (row == SG_ON_POL) {
        if (g->pol >= SG_POL_FRIENDS && g->pol <= SG_POL_NOBODY) { g->pol = (unsigned char)(((unsigned)g->pol - 1u + (dir < 0 ? 2u : 1u)) % 3u + 1u); g->fight_req = 3; }
        else if (g->pol != SG_POL_UNKNOWN || !g->fight_req) { g->pol = SG_POL_UNKNOWN; g->fight = 0; g->fight_req = 1; }
    } else if (row == SG_ON_REGION) {                                /* Not set, then the seven regions west to east (NAW, NAC, NAE, EU, LA, AS, OC); the glue writes both account values (request 4) */
        static const unsigned char nx[9] = { 0, 3, 7, 4, 5, 6, 8, 1, 2 }, pv[9] = { 0, 7, 8, 1, 3, 4, 5, 2, 6 };      /* west to east: NAW, NAC, NAE, EU, LA, AS, OC, Not set (the values stay the wire order + 1) */
        if (g->rgn >= 1u && g->rgn <= SG_RGN_NONE) { g->rgn = dir < 0 ? pv[g->rgn] : nx[g->rgn]; g->fight_req = 4; }
        else if (g->rgn != 0u || !g->fight_req) { g->rgn = 0; g->fight = 0; g->fight_req = 1; }
    } else if (row == SG_ON_WATCH) {
        if (g->wch == SG_WCH_NOBODY || g->wch == SG_WCH_FRIENDS) { g->wch = (unsigned char)(3u - g->wch); g->fight_req = 5; }
        else if (g->wch != 0u || !g->fight_req) { g->wch = 0; g->fight = 0; g->fight_req = 1; }
    }
}

static void activate(struct sg *g)
{
    switch (g->scr) {
    case SG_HUB:
        g->hint = 0;
        if (!ONP_IN(g)) { unsigned p = g->cur < ON_PAGES ? g->cur : 0u; g->onp = 1; g->cur = ONP_FIRST[p]; break; }      /* the list of pages: into the page */
        switch (g->cur) {
        case SG_ON_LINK: sg_goto(g, SG_LINK); break;
        case SG_ON_IDENT: sg_goto(g, SG_IDENT); break;
        case SG_ON_SLOGIN: sg_goto(g, SG_SLOGIN); g->sl_hub = 1; break;                    /* inline rows: straight to the step (checklist 13) */
        case SG_ON_DEL: case SG_ON_DEL2: case SG_ON_DEL3: sgd_enter_level(g, g->cur - SG_ON_DEL + 1u); g->del.from_ident = 2; break;
#if SG_LAB
        case SG_ON_TLS: sg_goto(g, SG_TLS); break;
#endif
        case SG_ON_FIGHT: case SG_ON_POL: case SG_ON_REGION: case SG_ON_WATCH: on_change(g, 1); break;
#if SG_NETSEL
        case SG_ON_SRV: case SG_ON_MMR: ns_change(g, 1); break;
#endif
        default: break;
        }
        break;
    case SG_IDENT: {                                    /* the Account page: Link (not linked), Region, Send Slippi login, Delete (the three levels on their own page) */
        unsigned char it[4];
        unsigned n = sg_acct_items(g, it), k = g->cur < n ? it[g->cur] : SG_AI_REGION;
        g->hint = 0;
        if (k == SG_AI_LINK) sg_goto(g, SG_LINK);
        else if (k == SG_AI_REGION) on_change_row(g, 1, SG_ON_REGION);
        else if (k == SG_AI_SLOGIN) sg_goto(g, SG_SLOGIN);
        else { sg_goto(g, SG_HUB); g->onp = 1; g->cur = SG_ON_DEL; g->del_acct = 1; }
        break;
    }
    case SG_PROV:
        if (g->cur < PROV_N) { g->prov = g->cur; g->scr = SG_PROV_DETAIL; } else sg_goto(g, SG_HUB);
        break;
    case SG_ENT:
        if (g->ent.state == SG_ET_PASS || g->ent.state == SG_ET_FAIL) sg_goto(g, SG_ENT);
        break;
    case SG_TLS:
        if (g->tls.done || g->tls.failed) sg_goto(g, SG_TLS);
        break;
    case SG_UPD:                                        /* A: check, download or restart, whichever the state offers; the state machine decides what a request means */
        if (upd_toggle(g)) break;     /* an option row (search for updates, Backup To Slot B): the glue saves it */
        if (copy_shown(g) && g->upd_cur == upd_cur_copy(g)) { if (g->upd.copyb != SGU_CPY_WORK) g->upd.req |= SGU_REQ_COPYB; break; }      /* Copy card to slot B */
        switch (g->upd.state) {
        case SGU_IDLE: case SGU_UPTODATE: case SGU_FAILED: case SGU_OFFLINE: g->upd.req |= SGU_REQ_CHECK; break;
        case SGU_AVAILABLE: g->upd.req |= SGU_REQ_DOWNLOAD; break;
        case SGU_RESTART: g->upd.req |= SGU_REQ_RESTART; break;
        case SGU_DONE: g->quit = 1; break;
        default: break;
        }
        break;
    default: break;
    }
}

static void back(struct sg *g)
{
    switch (g->scr) {
    case SG_HUB:
        if (g->del_acct) { g->del_acct = 0; sg_goto(g, SG_IDENT); g->cur = (unsigned char)sg_acct_pos(g, SG_AI_DELETE); break; }   /* the Delete page of the Account page: back on its Delete row */
        if (g->onp == 2u) { g->quit = 1; break; }                            /* opened straight from the Settings list: back there */
        if (!g->dev && g->onp) { unsigned p = on_page(g->cur); g->onp = 0; g->cur = (unsigned char)(p ? p - 1u : 0u); g->hint = 0; }     /* a page: back to the list of pages, on that page */
        else g->quit = 1;
        break;
    case SG_PROV_DETAIL: g->scr = SG_PROV; g->cur = g->prov; break;
    case SG_CTL: case SG_ENT: if (g->direct) g->quit = 1; else sg_goto(g, SG_HUB); break;     /* opened from the Settings page (Diagnostics): Back leaves the overlay */
    case SG_MAKEKEY: sg_goto(g, SG_IDENT); break;
    case SG_IDENT: if (g->id_top) g->quit = 1; else sg_goto(g, SG_HUB); break;      /* Settings > Account: back to the Settings list */
    case SG_UPD:
        switch (g->upd.state) {
        case SGU_CHECKING: case SGU_DOWNLOADING: g->upd.req |= SGU_REQ_CANCEL; break;      /* B cancels what is running and stays on the screen */
        case SGU_VERIFYING: case SGU_INSTALLING: break;                                    /* never leave while the card is being written */
        case SGU_RESTART: if (g->upd.noreset) { if (g->direct) g->quit = 1; else sg_goto(g, SG_HUB); } break;      /* the new build is on the card: no way back, the restart is automatic (A restarts at once) */
        default: if (g->direct) g->quit = 1; else sg_goto(g, SG_HUB); break;      /* opened by the title check: B = Later, back to the Main Menu; else the Online page on the Update row */
        }
        break;
    default: sg_goto(g, SG_HUB); break;
    }
}

/* The one gate every screen of the overlay reads its buttons through: whatever the console feeds (a rapid-repeat pulse train, a worn trigger or a chattering adapter), an action button counts as pressed only after it
   was up for SG_ACT_GAP samples.  A steady hold passes unchanged (its first sample is accepted), so Hold A / Hold Z / Hold B keep working; Up Down and B are not touched. */
unsigned sg_act_filter(struct sg *g, unsigned raw)
{
    static const unsigned short ACT[9] = { SG_B_A, SG_B_X, SG_B_Y, SG_B_Z, SG_B_L, SG_B_R, SG_B_START, SG_B_LEFT, SG_B_RIGHT };
    unsigned i, out = raw, prev = g->prev_raw;
    for (i = 0; i < 9u; i++) {
        unsigned m = ACT[i];
        if (raw & m) {
            if (g->act_blk & m) out &= ~m;
            else if (!(prev & m) && g->act_cool[i]) { g->act_blk = (unsigned short)(g->act_blk | m); out &= ~m; }
            g->act_cool[i] = (unsigned char)SG_ACT_GAP;
        } else {
            g->act_blk = (unsigned short)(g->act_blk & ~m);
            if (g->act_cool[i]) g->act_cool[i]--;
        }
    }
    g->prev_raw = (unsigned short)raw;
    return out;
}

void sg_frame(struct sg *g, const struct sdent_pad *praw, sdc_u32 tb)
{
    struct sdent_pad pf = *praw;
    const struct sdent_pad *p = &pf;
    unsigned edge, n;
    int up = 0, down = 0;
    n = sg_act_filter(g, praw->buttons);
    if (g->scr != SG_CTL) pf.buttons = (unsigned short)n;                 /* (the Controller test shows the raw pad) */
    edge = (unsigned)(p->buttons & ~g->prev_btn);
    g->prev_btn = p->buttons;
    if (p->sy > 40 && g->prev_sy <= 40) up = 1;
    if (p->sy < -40 && g->prev_sy >= -40) down = 1;
    g->prev_sy = p->sy;
    if (p->sx < -40 && g->stick_x >= -40) edge |= SG_B_LEFT;              /* the stick's Left / Right are the d-pad's ("Left Right: Change" did nothing with the stick); stick_x = last frame's x */
    if (p->sx > 40 && g->stick_x <= 40) edge |= SG_B_RIGHT;
    g->stick_x = p->sx;
    g->frame++;
    sdent_feed_pad(&g->lk.pool, praw, tb);                                   /* every frame of the overlay feeds the link's pool (only moving, irregular input is credited) */
    if (g->scr == SG_LINK) { sgl_frame(g, edge, p, tb); return; }        /* its own input: A / B are pool input on the entropy pass */
    if (g->scr == SG_DELID) { sgd_frame(g, edge | (up ? SG_B_UP : 0u) | (down ? SG_B_DOWN : 0u), p->buttons); return; } /* its own input: A, then A held 2 s */
    if (g->scr == SG_SLOGIN) {                                           /* Send Slippi Login: its own input (a short B cancels; the hold rule is for destructive confirms) */
        if (g->sr.st == SGR_ENT) {                                       /* except while collecting: A and B feed the pool, leaving is B held (as Link this console) */
            edge &= ~(unsigned)SG_B_B;
            if (p->buttons & SG_B_B) {
                if (g->hold_b == 0) g->hold_tb = tb;
                if (g->hold_b < 255) g->hold_b++;
                if (g->hold_b >= SG_HOLD_B_FRAMES || (g->hold_b > 1 && (sdc_u32)(tb - g->hold_tb) >= (g->hold_lim ? g->hold_lim : SG_HOLD_B_TICKS))) { edge |= SG_B_B; g->hold_b = 0; g->hold_tb = tb; }
            } else g->hold_b = 0;
        } else g->hold_b = 0;
        sgr_frame(&g->sr, edge, g->lk.now);
        if (g->sr.saved) g->id_slippi = 2u;       /* the login is on the card now: the Account page must say so at once (it only learned it at the next power-on, Zell) */
        if (g->sr.leave) { sgr_close(&g->sr); if (g->direct) g->quit = 1; else if (g->sl_hub) { g->sl_hub = 0; sg_goto(g, SG_HUB); g->cur = SG_ON_SLOGIN; } else { sg_goto(g, SG_IDENT); g->cur = (unsigned char)sg_acct_pos(g, SG_AI_SLOGIN); } }
        return;
    }
    if (g->scr == SG_ABOUT) {                                            /* About: up / down scroll the list one line (held: repeats), B leaves the overlay (back to the Settings page) */
        unsigned last = sg_about_lines() > 7u ? sg_about_lines() - 7u : 0u;     /* AB_ROWS lines on a page */
        if ((up || (edge & SG_B_UP)) && g->cur) g->cur--;                   /* the stick or the d-pad */
        if ((down || (edge & SG_B_DOWN)) && g->cur < last) g->cur++;
        if (edge & SG_B_B) g->quit = 1;
        return;
    }
    if (g->scr == SG_FRD) { g->fr.tb = tb; g->fr.hold_lim = g->hold_lim; frd_frame(&g->fr, p->buttons, up, down); if (g->fr.exit && !g->fr.gfx) { if (g->fr.join) sgm_frd_join(g); else g->quit = 1; } return; }   /* the Event screen front end leaves through its own exit first */
    if (g->scr == SG_TOURN) {                                             /* Tournament: its own input (gnt.c); B on the list leaves the overlay (back to the hub row) */
        gnt_frame(&g->tn, p->buttons | (edge & (SG_B_LEFT | SG_B_RIGHT)), up, down);      /* the stick's Left / Right as the d-pad's (the daily cycle) */
        if (g->tn.exit && !g->tn.gfx) g->quit = 1;
        return;
    }
    if (g->scr == SG_STAND) {                                             /* Standings: its own input (stb.c); B leaves the overlay (back to the hub row) */
        stb_frame(&g->sd, p->buttons, up, down);
        if (g->sd.exit && !g->sd.gfx) g->quit = 1;                        /* text view: at once (the Event screen front end leaves through the console glue) */
        return;
    }
    if (g->scr >= SG_MOCK) { sgm_frame(g, edge, up, down); return; }      /* the Mockups hub and its screens, and the Game Night browser: own input (sg_mockup.c) */
#if SG_NETSEL
    if (g->scr == SG_HUB && g->nconf) { ns_confirm(g, edge); return; }          /* the Real Slippi question owns the pad until it is answered */
#endif
    /* the entropy screens read the stick as input for the pool: it must not also navigate */
    if (g->scr == SG_ENT || g->scr == SG_MAKEKEY) {
        up = down = 0;
        if (g->scr == SG_ENT) sg_ent_frame(&g->ent, p, tb);
        else {
            sdent_feed_pad(&g->ent.e, p, tb);
            g->ent.frame++;
            if (sdent_ready(&g->ent.e, SDENT_L_LONGTERM)) { g->id_stage = 1; g->sub = 1; }       /* MOCK: no key is derived or stored */
        }
        if ((g->scr == SG_ENT && g->ent.state == SG_ET_RUN) || (g->scr == SG_MAKEKEY && !g->sub)) {
            /* while collecting, A and B are entropy input, not commands: leaving needs B held for SG_HOLD_B_FRAMES */
            edge &= ~(unsigned)(SG_B_A | SG_B_B);
            if (p->buttons & SG_B_B) {
                if (g->hold_b == 0) g->hold_tb = tb;
                if (g->hold_b < 255) g->hold_b++;
                if (g->hold_b >= SG_HOLD_B_FRAMES || (g->hold_b > 1 && (sdc_u32)(tb - g->hold_tb) >= (g->hold_lim ? g->hold_lim : SG_HOLD_B_TICKS))) { edge |= SG_B_B; g->hold_b = 0; g->hold_tb = tb; }
            } else g->hold_b = 0;
        } else g->hold_b = 0;
    }
    if (g->scr == SG_CTL) {
        /* the screen shows the pad as is: the stick does not navigate, A does nothing, B leaves only when held (a stuck pad must not close it by itself being noisy,
           and a tap of B is a button the test wants to display) */
        up = down = 0;
        sg_ctl_frame(&g->ctl, p, tb, g->hold_lim);
        edge &= ~(unsigned)(SG_B_A | SG_B_B);
        if (p->buttons & SG_B_B) {
            if (g->hold_b == 0) g->hold_tb = tb;
            if (g->hold_b < 255) g->hold_b++;
            if (g->hold_b >= SG_HOLD_B_FRAMES || (g->hold_b > 1 && (sdc_u32)(tb - g->hold_tb) >= (g->hold_lim ? g->hold_lim : SG_HOLD_B_TICKS))) { edge |= SG_B_B; g->hold_b = 0; g->hold_tb = tb; }
        } else g->hold_b = 0;
    }
    n = nitems(g);
    if (g->scr == SG_UPD) {                                         /* the Update screen: Up / Down between Check Now and the start-check option */
        if (n && ((edge & SG_B_DOWN) || down) && g->upd_cur + 1u < n) g->upd_cur++;
        if (((edge & SG_B_UP) || up) && g->upd_cur) g->upd_cur--;
        if (!n || g->upd_cur >= n) g->upd_cur = 0;
        if (n && (edge & (SG_B_LEFT | SG_B_RIGHT))) {                 /* Left / Right change the option under the cursor like A does */
            (void)upd_toggle(g);
        }
    } else if (n) {
        unsigned old = g->cur;
        if (g->scr == SG_HUB && !ONP_IN(g)) {
            if ((edge & SG_B_DOWN) || down) g->cur = (unsigned char)((g->cur + 1u) % ON_PAGES);
            if ((edge & SG_B_UP) || up) g->cur = (unsigned char)((g->cur + ON_PAGES - 1u) % ON_PAGES);
        } else if (g->scr == SG_HUB) {
            if ((edge & SG_B_DOWN) || down) g->cur = (unsigned char)hub_move(g, g->cur, 1);
            if ((edge & SG_B_UP) || up) g->cur = (unsigned char)hub_move(g, g->cur, -1);
        } else
        {
        if ((edge & SG_B_DOWN) || down) g->cur = (unsigned char)((g->cur + 1u) % n);
        if (((edge & SG_B_UP) || up) && !(g->scr == SG_HUB && g->cur == 0u)) g->cur = (unsigned char)((g->cur + n - 1u) % n);   /* Online: Up from the first row stays (never onto Delete) */
        }
        if (g->scr == SG_HUB && g->cur != old) g->hint = 0;
    }
    if (g->scr == SG_HUB && ONP_IN(g) && (g->cur == SG_ON_FIGHT || g->cur == SG_ON_POL || g->cur == SG_ON_REGION || g->cur == SG_ON_WATCH)) {
        if (edge & SG_B_RIGHT) on_change(g, 1);
        if (edge & SG_B_LEFT) on_change(g, -1);
    }
    if (g->scr == SG_IDENT && (edge & (SG_B_LEFT | SG_B_RIGHT))) {    /* the Account page's Region row: Left / Right change it like A */
        unsigned char it[4];
        unsigned na = sg_acct_items(g, it);
        if (g->cur < na && it[g->cur] == SG_AI_REGION) on_change_row(g, (edge & SG_B_LEFT) ? -1 : 1, SG_ON_REGION);
    }
#if SG_NETSEL
    if (g->scr == SG_HUB && ONP_IN(g) && (g->cur == SG_ON_SRV || g->cur == SG_ON_MMR) && (edge & (SG_B_LEFT | SG_B_RIGHT))) ns_change(g, (edge & SG_B_LEFT) ? -1 : 1);
#endif
#if SG_LAB
    if (g->scr == SG_TLS && (g->tls.done || g->tls.failed)) {         /* a finished run: Left / Right pick the polling variant of the next one, A runs it */
        if (edge & SG_B_RIGHT) g->tls.var = (sdc_u8)((g->tls.var + 1u) % 3u);
        if (edge & SG_B_LEFT) g->tls.var = (sdc_u8)((g->tls.var + 2u) % 3u);
    }
#endif
    if ((edge & SG_B_X) && g->scr == SG_UPD) g->upd.req |= SGU_REQ_REINSTALL;      /* X on "Up to date": install this same build again; the state machine ignores it elsewhere */
    if (edge & SG_B_A) activate(g);
    if (edge & SG_B_B) back(g);
}

/* ---- drawing ---- */
static void items(struct sg_view *v, const struct sg *g, unsigned r0, const char *const *names, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        struct ln l;
        ln_init(&l);
        ln_s(&l, i == g->cur ? "- " : "  ");
        ln_s(&l, names[i]);
        putl(v, r0 + i, i == g->cur ? SG_A_HI : SG_A_BODY, &l);
    }
}

static void d_hub(const struct sg *g, struct sg_view *v)
{
    static const char *const fight_v[4] = { "Loading...", "Off", "On", "Offline" };        /* "Offline": the value must fit beside the 29 character label */
    static const char *const pol_v[5] = { "Loading...", "Friends", "Anyone with my code", "Nobody", "Offline" };
    static const char *const rgn_v[SG_RGN_NA + 1] = { "Loading...", "North America East", "North America West", "Europe", "Latin America", "Asia", "Oceania", "North America Central", "Not set", "Not available" };
    static const char *const wch_v[4] = { "Loading...", "Nobody", "Friends", "Offline" };
    static const char *const link_v[4] = { "...", "Guest", "Linked", "Not saved" };
    unsigned i;
    unsigned line = 0;
    unsigned big = 1u;                                      /* list rows two grid rows high with the bar: at most 7 visible rows (the lab rows are on their own page) */
#if SG_NETSEL
    unsigned after;
#endif
    if (!ONP_IN(g)) {                                       /* the list of the three pages */
        for (i = 0; i < ON_PAGES; i++) {
            unsigned r = UI_R_BODY + 2u * i, attr = (i == g->cur ? SG_A_HI : SG_A_BODY) | SG_A_BIG;
            if (i == g->cur) sg_bar(v, r, big);
            put(v, r, attr, ONP_LABEL[i]);
        }
        sg_head(v, "Online", "More - Settings - Online");
        put(v, UI_R_DESC, SG_A_DIM, ONP_DESC[g->cur < ON_PAGES ? g->cur : 0u]);
        if (g->hint) put(v, UI_R_NOTE, SG_A_BAD, g->hint);
        foot(v, "A: Select  B: Back");
        return;
    }
    if (g->dev) sg_head(v, "Server and Slippi", "More - Settings - Diagnostics - Server");
    else {
        unsigned p = on_page(g->cur);
        sg_head(v, ONP_LABEL[p ? p - 1u : 0u], "");
    }
    {   /* the list scrolls with the cursor (checklist 13): 7 rows in view, the window follows the cursor (stateless), a note names the hidden side */
        unsigned vis = 0, pcur = 0, first = 0, k;
        for (k = 0; k < ON_ORDER_N; k++) {
            if (row_hidden(g, ON_ORDER[k])) continue;
            if (ON_ORDER[k] == g->cur) pcur = vis;
            vis++;
        }
        if (pcur > 3u) first = pcur - 3u;
        if (vis > 7u && first > vis - 7u) first = vis - 7u;
        if (vis <= 7u) first = 0;
        vis = 0;
        for (k = 0; k < ON_ORDER_N; k++) {
            struct ln l;
            const char *val = "";
            unsigned r;
            unsigned attr;
            i = ON_ORDER[k];
            if (row_hidden(g, i)) continue;
            if (vis++ < first) continue;
            if (line >= 7u) continue;
            r = UI_R_BODY + 2u * line;
            attr = i == g->cur ? SG_A_HI : SG_A_BODY;
            ln_init(&l);
            ln_s(&l, ON_LABEL[i]);
            /* Device identity has no value: Link this console right below says Linked / Guest (it showed twice) */
            if (i == SG_ON_LINK) val = link_v[g->linked & 3u];
            if (i == SG_ON_DEL2 && g->id_slippi != 2u && i != g->cur) attr = SG_A_DIM;        /* no Slippi login on this card: the level is grey (as on the Device Identity page) */
            if (i == SG_ON_FIGHT || i == SG_ON_POL || i == SG_ON_REGION || i == SG_ON_WATCH) {
                if (g->linked == SG_ID_GUEST) { if (i != g->cur) attr = SG_A_DIM; }      /* a guest: the row is grey and has no value (the label leaves no room for "Needs An Account"); the description says why */
                else val = i == SG_ON_FIGHT ? fight_v[g->fight & 3u] : i == SG_ON_POL ? pol_v[g->pol <= SG_POL_NA ? g->pol : 0u] : i == SG_ON_REGION ? rgn_v[g->rgn <= SG_RGN_NA ? g->rgn : 0u] : wch_v[g->wch & 3u];
            }
#if SG_NETSEL
            if (i == SG_ON_SRV || i == SG_ON_MMR) val = ns_text(g, i);
#endif
            attr |= SG_A_BIG;
            if (i == g->cur) sg_bar(v, r, big);
            putl(v, r, attr, &l);
            if (val[0]) sg_putv(v, r, attr, val);
            line++;
        }
        {
            unsigned total = 0;
            for (k = 0; k < ON_ORDER_N; k++) if (!row_hidden(g, ON_ORDER[k])) total++;
            if (total > 7u) put(v, UI_R_NOTE, SG_A_DIM, first == 0u ? "More rows below" : first + 7u >= total ? "More rows above" : "More rows above and below");
        }
    }
#if SG_NETSEL
    after = UI_R_BODY + 2u * line + 1u;
    if (g->nconf) {                                                  /* the one-time question replaces the description */
        put(v, after, SG_A_HI, "Unranked and Direct will contact Slippi's real");
        put(v, after + 1u, SG_A_HI, "servers. This client is not Slippi.");
        put(v, after + 2u, SG_A_HI, "Switch it on.");
        foot(v, "A: Yes  B: No");
        return;
    }
    if (g->cur == SG_ON_NET) {                                       /* the read-only row: what the console is using now */
        struct ln a, b;
        ln_init(&a); ln_init(&b);
        ln_s(&a, "Server: ");
        if (g->srv_ip) ns_ip(&a, g->srv_ip); else ln_s(&a, g->srv_host[0] ? g->srv_host : "none");
        ln_s(&a, g->srv == SG_SRV_LAN ? " Lab" : g->srv == SG_SRV_LAB || g->srv_lock == 1u ? " Dev" : " Production");
        ln_s(&b, "Slippi: ");
        if (g->mmr == SG_MMR_REAL) ln_s(&b, "Real, mm.slippi.gg");
        else if (g->mm_ip) { ln_s(&b, "Our mock at "); ns_ip(&b, g->mm_ip); }
        else ln_s(&b, "no override, baked name");
        putl(v, after, SG_A_BODY, &a);
        putl(v, after + 1u, SG_A_BODY, &b);
    } else
#endif
    put(v, UI_R_DESC, SG_A_DIM, (g->linked == SG_ID_GUEST && (g->cur == SG_ON_FIGHT || g->cur == SG_ON_POL || g->cur == SG_ON_REGION || g->cur == SG_ON_WATCH)) ? "Needs an account. Link this console first." : ON_DESC[g->cur % SG_ON_N]);          /* the description of the row under the cursor */
    if (g->hint) put(v, UI_R_NOTE, SG_A_BAD, g->hint);
    foot(v, (g->cur == SG_ON_FIGHT || g->cur == SG_ON_POL || g->cur == SG_ON_REGION || g->cur == SG_ON_WATCH
#if SG_NETSEL
             || g->cur == SG_ON_SRV || g->cur == SG_ON_MMR
#endif
             ) ? "A: Change  B: Back" : "A: Select  B: Back");
}

static void d_makekey(const struct sg *g, struct sg_view *v)
{
    struct ln l;
    sdc_u32 pct = sdent_progress(&g->ent.e, SDENT_L_LONGTERM);
    head_old(v, "MAKE YOUR CONSOLE KEY", "MOCKUP");
    if (!g->sub) {
        put(v, 2, SG_A_BODY, "Press any buttons, move any stick and");
        put(v, 3, SG_A_BODY, "squeeze the triggers, at random.");
        put(v, 4, SG_A_BODY, "Keep going until the bar is full.");      /* input-neutral wording (tester feedback: box controllers have no analog stick): buttons alone fill the pool (sdent.h SDENT_DIGI), so does the stick */
        ln_init(&l); bar(&l, pct); putl(v, 7, SG_A_HI, &l);
        if (g->ent.frame > 1200u && pct < 100u) put(v, 9, SG_A_DIM, "Vary your rhythm.");
        put(v, 12, SG_A_DIM, "Nothing is saved if you cancel: hold B.");
    } else {
        put(v, 2, SG_A_GOOD, "Key made.");
        put(v, 4, SG_A_DIM, "MOCK: in the real flow the key is written to the");
        put(v, 5, SG_A_DIM, "card, read back, and only then sent, public half");
        put(v, 6, SG_A_DIM, "only, to the server. Here nothing is stored.");
        put(v, 8, SG_A_HI, "- Done, press B");
    }
    foot(v, g->sub ? "B: Back" : "Any input feeds the pool  Hold B: Cancel");
}

static void d_prov(const struct sg *g, struct sg_view *v)
{
    static const char *const names[PROV_N + 1] = { "Passkey, recommended", "Discord, optional", "Google and Apple, later", "Email, only if you ask for it", "Back" };
    head_old(v, "SIGN-IN PROVIDERS", "MOCKUP");
    put(v, 2, SG_A_BODY, "How an account is attached to this console.");
    put(v, 3, SG_A_DIM, "All of it happens on your phone, never here.");
    items(v, g, 6, names, PROV_N + 1);
    put(v, 13, SG_A_DIM, "The console always has its own key, so play");
    put(v, 14, SG_A_DIM, "never depends on one of these being online.");
    foot(v, "A: Details  B: Back");
}

static void d_provd(const struct sg *g, struct sg_view *v)
{
    head_old(v, "SIGN-IN PROVIDERS", "MOCKUP");
    switch (g->prov) {
    case 0:
        put(v, 2, SG_A_HI, "Passkey");
        put(v, 4, SG_A_BODY, "Your phone or computer keeps a secret key and");
        put(v, 5, SG_A_BODY, "unlocks it with your fingerprint or PIN.");
        put(v, 7, SG_A_BODY, "No password. No email. Nothing to phish.");
        put(v, 8, SG_A_BODY, "The server stores only a public key.");
        put(v, 10, SG_A_DIM, "Needs the project's own web address, HTTPS.");
        break;
    case 1:
        put(v, 2, SG_A_HI, "Discord");
        put(v, 4, SG_A_BODY, "Sign in with the Discord you already use.");
        put(v, 5, SG_A_BODY, "Only your Discord id is read no email, no");
        put(v, 6, SG_A_BODY, "friends list, and it is stored scrambled.");
        put(v, 8, SG_A_BODY, "Used once to prove who you are, then dropped.");
        put(v, 10, SG_A_DIM, "Optional: a passkey alone is enough.");
        break;
    case 2:
        put(v, 2, SG_A_HI, "Google and Apple");
        put(v, 4, SG_A_BODY, "Not at launch. The account can take more");
        put(v, 5, SG_A_BODY, "sign-in methods later without changing your");
        put(v, 6, SG_A_BODY, "console key or your save.");
        break;
    default:
        put(v, 2, SG_A_HI, "Email");
        put(v, 4, SG_A_BODY, "Never required. Only if you choose it, for");
        put(v, 5, SG_A_BODY, "getting back in if you lose every other way.");
        put(v, 7, SG_A_DIM, "Typed on your phone, never on this console.");
        break;
    }
    put(v, 14, SG_A_BODY, "To attach it: open 'Link this console', scan the");
    put(v, 15, SG_A_BODY, "QR code and pick it on your phone.");
    put(v, 18, SG_A_DIM, "MOCKUP: no provider is connected yet.");
    foot(v, "B: Back");
}

static const char *const SRC_NAME[4] = { "Stick movement", "Button presses", "Timing, mixed only", "Button timing" };

static void bar10(struct ln *l, sdc_u32 pct)                         /* a short bar for a value column: IIIIII....  47 percent */
{
    unsigned i, fill = (unsigned)(pct * 10u / 100u);
    for (i = 0; i < 10; i++) ln_s(l, i < fill ? "I" : ".");
    ln_s(l, "  ");
    ln_u(l, pct, 0);
    ln_s(l, " percent");
}

static void d_ent(const struct sg *g, struct sg_view *v)
{
    const struct sg_ent_t *t = &g->ent;
    static const unsigned caps[4] = { SDENT_CAP_STICK, SDENT_CAP_PRESS, SDENT_CAP_JITTER, SDENT_CAP_DIGI };
    static const unsigned srcs[4] = { SDENT_STICK, SDENT_PRESS, SDENT_JITTER, SDENT_DIGI };
    struct ln l;
    unsigned i;
    sdc_u32 pct;
    sg_head(v, "Entropy test", "More - Settings - Diagnostics - Entropy test");
    if (t->state == SG_ET_PASS) sg_head_tag(v, SG_A_GOOD, "PASS");
    else if (t->state == SG_ET_FAIL) sg_head_tag(v, SG_A_BAD, "FAIL");
    if (t->state == SG_ET_RUN) { put(v, 4, SG_A_BODY, "Press buttons, move sticks at random."); put(v, 5, SG_A_BODY, "Keep going until the bar is full."); }
    else put(v, 4, SG_A_DIM, "Result below. Nothing secret is displayed.");
    for (i = 0; i < 4; i++) {                                         /* source, credited bits of its cap */
        ln_init(&l);
        ln_m(&l, t->e.mbits[srcs[i]], 0); ln_s(&l, " of "); ln_u(&l, caps[i], 0);
        put(v, 7u + i, SG_A_BODY, SRC_NAME[i]);
        sg_putv(v, 7u + i, SG_A_BODY, l.b);
    }
    ln_init(&l); ln_m(&l, t->e.mtotal, 0); ln_s(&l, " bits");
    put(v, 11, SG_A_BODY, "Total credited");
    sg_putv(v, 11, SG_A_BODY, l.b);
    pct = sdent_progress(&t->e, SDENT_L_SESSION);
    ln_init(&l); bar10(&l, pct);
    put(v, 12, SG_A_HI, "Session level 128");
    sg_putv(v, 12, SG_A_HI, l.b);
    pct = sdent_progress(&t->e, SDENT_L_LONGTERM);
    ln_init(&l); bar10(&l, pct);
    put(v, 13, SG_A_BODY, "Device key 192");
    sg_putv(v, 13, SG_A_BODY, l.b);
    put(v, 14, SG_A_BODY, "Output check, 2048 bits");
    sg_putv(v, 14, !t->out_checked ? SG_A_DIM : t->out_ok ? SG_A_GOOD : SG_A_BAD, !t->out_checked ? "not yet" : t->out_ok ? "ok" : "BAD");
    ln_init(&l);
    ln_s(&l, "Health: repeat trips "); ln_u(&l, t->e.h[SDENT_STICK].rct_trips, 0);
    ln_s(&l, ", window trips "); ln_u(&l, t->e.h[SDENT_STICK].apt_trips, 0);
    ln_s(&l, ", ok "); ln_u(&l, t->e.h[SDENT_STICK].windows_ok, 0);
    putl(v, 16, SG_A_DIM, &l);
    if (!t->starved_ok) put(v, 18, SG_A_BAD, "An empty pool gave bytes: broken.");
    if (t->state == SG_ET_PASS) {
        put(v, 20, SG_A_GOOD, "RESULT: PASS");
        put(v, 21, SG_A_BODY, "The pool earned 128 bits from your input and");
        put(v, 22, SG_A_BODY, "its output looks random.");
    } else if (t->state == SG_ET_FAIL) {
        put(v, 20, SG_A_BAD, "RESULT: FAIL");
        put(v, 21, SG_A_BODY, t->why == 1 ? "Not enough varied movement in 60 seconds." : t->why == 3 ? "An empty pool gave bytes: broken." : "The output check failed.");
    } else put(v, 20, SG_A_DIM, "Collecting. Times out after 60 seconds");
    foot(v, t->state == SG_ET_RUN ? "Any input feeds the pool  Hold B: Leave" : "A: Run again  B: Back");
}

#if SG_LAB
static void tline(struct sg_view *v, unsigned r, const struct sg_tls_view *t, unsigned phase, const char *name, int ok_cond, const struct ln *detail)
{
    struct ln l;
    unsigned attr;
    const char *st;
    if (t->failed && t->err_phase == phase) { st = "FAIL"; attr = SG_A_BAD; }
    else if (!t->started || t->phase < phase) { st = "...."; attr = SG_A_DIM; }
    else if (t->phase == phase && !t->done) { st = "run "; attr = SG_A_HI; }
    else { st = ok_cond ? "ok  " : "BAD "; attr = ok_cond ? SG_A_GOOD : SG_A_BAD; }
    ln_init(&l);
    ln_s(&l, st); ln_s(&l, "  "); ln_s(&l, name);
    if (detail) { ln_at(&l, 27); ln_s(&l, detail->b); }
    putl(v, r, attr, &l);
}

static void d_tls(const struct sg *g, struct sg_view *v)
{
    const struct sg_tls_view *t = &g->tls;
    int vd = sg_tls_verdict(t);
    struct ln l, d;
    head_old(v, "TCP AND TLS TEST", vd == 1 ? "PASS" : vd == 3 ? "PASS" : vd == 2 ? "FAIL" : "RUNNING");
    ln_init(&l); ln_s(&l, "Lab server "); ln_s(&l, t->server[0] ? t->server : "none"); putl(v, 1, SG_A_DIM, &l);
    put(v, 2, SG_A_DIM, "TLS 1.3, ChaCha20-Poly1305, pinned Ed25519 key");
    ln_init(&d); ln_u(&d, t->tcp_connect_ms, 0); ln_s(&d, " ms");
    tline(v, 4, t, 1, "TCP connect", 1, &d);
    ln_init(&d); ln_s(&d, t->echo_ok == 1u ? "1000 B echoed" : "echo mismatch");
    tline(v, 5, t, 2, "TCP echo", t->echo_ok == 1u, &d);
    ln_init(&d); ln_u(&d, t->down_bytes, 0); ln_s(&d, " B in "); ln_u(&d, t->down_ms, 0); ln_s(&d, " ms");
    tline(v, 6, t, 3, "TCP download", t->down_bad == 0 && t->down_bytes == t->want_down, &d);
    ln_init(&d); ln_u(&d, t->tls_hs_ms, 0); ln_s(&d, " ms, TCP "); ln_u(&d, t->tls_tcp_ms, 0); ln_s(&d, " ms");
    tline(v, 7, t, 4, "TLS handshake", t->phase >= 5u || t->done, &d);
    ln_init(&d); ln_s(&d, t->hello_ok == 1u ? "pong, status " : "no pong, status "); ln_u(&d, t->https_status, 0);
    tline(v, 8, t, 5, "HTTPS ping", t->hello_ok == 1u, &d);
    ln_init(&d); ln_u(&d, t->https_bytes, 0); ln_s(&d, " B in "); ln_u(&d, t->https_ms, 0); ln_s(&d, " ms");
    tline(v, 9, t, 6, "HTTPS blob", t->https_bad == 0 && t->https_bytes > 0 && t->https_status == 200u, &d);
    put(v, 11, SG_A_DIM, "CPU per crypto step, must fit one 16.7 ms frame:");
    ln_init(&l); ln_s(&l, "  x25519 "); ln_ms(&l, t->x25519_us0, 0); ln_s(&l, " and "); ln_ms(&l, t->x25519_us1, 0);
    ln_s(&l, "   verify "); ln_ms(&l, t->verify_us, 0);
    putl(v, 12, SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "  longest step "); ln_ms(&l, t->heavy_max_us, 0); ln_s(&l, "   all steps "); ln_ms(&l, t->heavy_total_us, 0);
    putl(v, 13, t->heavy_max_us > SG_TLS_FRAME_US ? SG_A_BAD : SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "  retransmits "); ln_u(&l, t->retrans, 0); ln_s(&l, "   run time "); ln_u(&l, t->elapsed_ms, 0); ln_s(&l, " ms");
    putl(v, 14, SG_A_BODY, &l);
    if (vd == 1) put(v, 17, SG_A_GOOD, "RESULT: PASS");
    else if (vd == 3) put(v, 17, SG_A_BAD, "RESULT: PASS, but a step is over one frame");
    else if (vd == 2) {
        ln_init(&l); ln_s(&l, "RESULT: FAIL in step "); ln_u(&l, t->err_phase, 0); ln_s(&l, "  error "); ln_u(&l, (sdc_u32)(t->err < 0 ? -t->err : t->err), 0);
        putl(v, 17, SG_A_BAD, &l);
    } else if (t->need_ent) {
        ln_init(&l); ln_s(&l, "Entropy "); bar(&l, t->ent_pct); putl(v, 17, SG_A_HI, &l);
        put(v, 18, SG_A_BODY, "Press buttons or move a stick: TLS needs 128 bits.");
    } else put(v, 17, SG_A_DIM, "Running ...");
    ln_init(&l); ln_s(&l, "Poll: ");
    ln_s(&l, t->var == 0 ? "every frame, full window" : t->var == 1 ? "every frame, window 2920" : "1 ms busy poll, full window");
    putl(v, 15, SG_A_HI, &l);
    put(v, 16, SG_A_DIM, t->diag == 0 ? "Result line to lab server: not yet" : t->diag == 1 ? "Result line to lab server: sending" : t->diag == 2 ? "Result line to lab server: sent" : "Result line to lab server: failed");
    put(v, 19, SG_A_DIM, "DIAG build. Only the lab server is contacted. No");
    put(v, 20, SG_A_DIM, "account, no Slippi, no internet. Nothing stored.");
    foot(v, (t->done || t->failed) ? "A: Run again  Left Right: Poll  B: Back" : "B: Back");
}

#endif

/* ---- the Update screen: at most 6 live rows (title, installed, three text rows, hint) plus the one progress row ---- */
static void upd_reason(unsigned r, const char **a, const char **b)
{
    switch (r) {
    case SGU_R_NOHOST: *a = "Updates are not available in this build."; *b = "Update the memory card by hand."; break;
    case SGU_R_NETWORK: *a = "The connection was lost during the download."; *b = "Nothing was changed. Try again."; break;
    case SGU_R_SERVER: *a = "The update server did not accept the request."; *b = "Nothing was changed. Try again later."; break;
    case SGU_R_NOFILES: *a = "No updates are published on this server."; *b = "Try again later."; break;
    case SGU_R_ABI: *a = "The new version needs a new loader."; *b = "Update the memory card by hand."; break;
    case SGU_R_MEMORY: *a = "Not enough free memory for the download."; *b = "Leave this screen and try again."; break;
    case SGU_R_BADFILE: *a = "The download did not pass the checks."; *b = "Nothing was changed on the memory card."; break;
    case SGU_R_NOCARD: *a = "No usable memory card found."; *b = "Put a card in slot A and try again."; break;
    case SGU_R_FULL: *a = "The memory card has no room for the update."; *b = "Free some blocks. Your old version still works."; break;
    case SGU_R_WRITE: *a = "The memory card could not be written."; *b = "Your old version is still in place."; break;
    case SGU_R_CARDBITS: *a = "The memory card did not keep the data."; *b = "Your old version is still in place."; break;
    case SGU_R_BUSY: *a = "The network is busy with something else."; *b = "Try again in a moment."; break;
    case SGU_R_UNREACH: *a = "Could not reach the update server."; *b = "Check the network cable and try again."; break;
    case SGU_R_HASH: *a = "The update did not pass its checks."; *b = "Nothing was changed. Try again."; break;
    case SGU_R_NONET: *a = "This memory card has no online setup."; *b = "Nothing was changed."; break;
    case SGU_R_HALF: *a = "The update is only half installed."; *b = "Do not switch off. Press A to try again."; break;
    case SGU_R_LOST: *a = "The file could not be written. Do not switch off."; *b = "Press A to try again. No Multishine until then."; break;
    default: *a = "Unknown error."; *b = "Nothing was changed."; break;
    }
}

/* ---- TECHNICAL INFO BLOCK of the Update screen (shown only when the setting "Show technical info" is on).  One function, one gate macro: SG_TECH_INFO(g) is
   the gate (today always on; the settings layer replaces it with the setting).  The player-facing words around it are in upd_reason and the states below. ---- */
static void upd_tech_rows(const struct sgu_view *u, struct sg_view *v)
{
    struct ln l;
    ln_init(&l); ln_s(&l, "Code "); ln_u(&l, u->reason, 0); ln_s(&l, ".");
    if (u->code) { ln_s(&l, " Detail "); ln_u(&l, u->code, 0); ln_s(&l, "."); }
    putl(v, 16, SG_A_DIM, &l);                              /* below the two reason lines (UR 8, 10), three grid rows apart like the others */
}

/* the advisory notes: at most two more rows (8 and 9) on the states that have room, so the screen stays within its 8 live text objects */
static unsigned upd_notes(const struct sgu_view *u, struct sg_view *v)     /*: every line two grid rows apart (they touched); -> the next free row */
{
    struct ln l;
    unsigned r = 16u;                 /* (three rows apart, after the state lines at 7 10 13) */
    if (u->state != SGU_IDLE && u->state != SGU_UPTODATE && u->state != SGU_AVAILABLE) return r;
    if (lay_note(u)) {
        put(v, r, SG_A_HI, "A new card layout is out. To keep getting");
        put(v, r + 2u, SG_A_HI, "updates, update the card in the Nintendont");
        put(v, r + 4u, SG_A_HI, "or GameCube app, or set it up again.");
        return r + 6u;
    }
    if (u->note_ours == SGU_NOTE_TOO_OLD) {                      /* FORCED UPDATE: the server's minimum build is above ours; online modes stay closed until installed */
        if (u->req_min) { ln_init(&l); ln_s(&l, "Update required: build "); ln_u(&l, u->req_min, 0); ln_s(&l, " needed for online."); putl(v, r, SG_A_BAD, &l); }
        else put(v, r, SG_A_BAD, "Update required to play online.");
        r += 3u;
    }
    else if (u->note_ours == SGU_NOTE_NEWER && u->state != SGU_AVAILABLE) { put(v, r, SG_A_HI, "A newer Multishine version is out. Update it."); r += 3u; }
    if (u->note_slippi && !(u->state == SGU_AVAILABLE && u->note_ours == SGU_NOTE_TOO_OLD)) {      /* on the Available screen the stronger note wins: 8 live rows at most */
        ln_init(&l);
        if (u->note_slippi == 2u) ln_s(&l, "Slippi "); else ln_s(&l, "Slippi has a newer version, ");
        ln_u(&l, u->slippi_new[0], 0); ln_s(&l, "."); ln_u(&l, u->slippi_new[1], 0); ln_s(&l, "."); ln_u(&l, u->slippi_new[2], 0);
        ln_s(&l, u->note_slippi == 2u ? " does not work with this build." : ".");
        putl(v, r, SG_A_BODY, &l);
        put(v, r + 2u, SG_A_BODY, u->note_slippi == 2u ? "Online play with Slippi players may fail." : "Online play may fail until Multishine updates.");
        r += 4u;
    }
    return r;
}

/* the Copy card to slot B row: the short word at the right and the sentence under the list (the copy has no logins in it) */
static const char *copy_val(unsigned c)
{
    switch (c) {
    case SGU_CPY_WORK: return "Copying...";
    case SGU_BAK_DONE: return "Done";
    case SGU_BAK_NOCARD: return "No card";
    case SGU_BAK_SMALL: return "Too small";
    case SGU_BAK_BADCARD: return "Not usable";
    case SGU_BAK_INWAY: return "In the way";
    case SGU_BAK_MEM: return "No memory";
    case SGU_BAK_BUSY: return "Busy";
    default: return "Failed";
    }
}
static const char *copy_desc(unsigned c)
{
    switch (c) {
    case 0: return "Copies the card to slot B. Logins stay behind.";
    case SGU_CPY_WORK: return "Copying. Do not turn the power off.";
    case SGU_BAK_DONE: return "Slot B has a copy. No logins were copied.";
    case SGU_BAK_NOCARD: return "No card in slot B.";
    case SGU_BAK_SMALL: return "The card in slot B is too small.";
    case SGU_BAK_BADCARD: return "The card in slot B is not usable.";
    case SGU_BAK_INWAY: return "Other saves in slot B are in the way.";
    case SGU_BAK_MEM: return "Not enough memory.";
    case SGU_BAK_BUSY: return "The card is busy. Try again.";
    default: return "The copy did not work. Slot B is unchanged.";
    }
}

/* the one status line of the backup to slot B; 0 = nothing to say */
static const char *bak_line(unsigned b)
{
    switch (b) {
    case SGU_BAK_DONE: return "Backup to slot B: done.";
    case SGU_BAK_OFF: return "Backup to slot B: off.";
    case SGU_BAK_NOCARD: return "Backup to slot B: no card in B, skipped.";
    case SGU_BAK_SMALL: return "Backup to slot B: card too small, skipped.";
    case SGU_BAK_BADCARD: return "Backup to slot B: card not usable, skipped.";
    case SGU_BAK_INWAY: return "Backup to slot B: other saves in the way.";
    case SGU_BAK_MEM: return "Backup to slot B: not enough memory, skipped.";
    case SGU_BAK_BUSY: return "Backup to slot B: card busy, skipped.";
    case SGU_BAK_FAIL: return "Backup to slot B: did not work, skipped.";
    default: return 0;
    }
}

/* The automatic system-files set-up (SGU kind 3, opened by the glue when MultishinePack is missing; no key starts it): a plain progress screen, then it closes by itself.  The failure text is the
   only place the player reads about a missing pack besides the menu rows; it retries by itself at the next start (A tries again now). */
static void d_sysf(const struct sg *g, struct sg_view *v)
{
    const struct sgu_view *u = &g->upd;
    struct ln l;
    const char *foot_s = "Working...";
    sg_head(v, "Multishine system files", "Setting up");
    switch (u->state) {
    case SGU_DONE:
        put(v, 7, SG_A_GOOD, "Multishine system files are set up.");
        put(v, 10, SG_A_BODY, "Everything is ready. Continuing...");
        foot_s = "A: Continue"; break;
    case SGU_FAILED: case SGU_OFFLINE: case SGU_UPTODATE:
        put(v, 7, SG_A_BAD, "Multishine system files could not be set up:");
        put(v, 10, SG_A_BODY, "check the network and try again.");
        if (u->reason == SGU_R_NODISC) put(v, 13, SG_A_BODY, "Keep your Melee disc in the console.");
        else if (u->reason == SGU_R_FULL) put(v, 13, SG_A_BODY, "The memory card has no room for them.");
        else if (u->reason == SGU_R_MEMORY) put(v, 13, SG_A_BODY, "Not enough free memory right now.");
        put(v, 16, SG_A_DIM, "It tries again by itself at the next start.");
        { ln_init(&l); ln_s(&l, "Code "); ln_u(&l, u->reason, 0); ln_s(&l, "-"); ln_u(&l, u->code, 0); putl(v, 19, SG_A_DIM, &l); }       /* for a bug report */
        foot_s = "A: Try again  B: Back"; break;
    case SGU_INSTALLING:
        put(v, 7, SG_A_HI, "Setting up Multishine system files");
        ln_init(&l); bar(&l, u->pct); putl(v, 10, SG_A_BODY, &l);
        put(v, 13, SG_A_BODY, "Building the files from your Melee disc.");
        put(v, 16, SG_A_BAD, "Do not turn the power off.");
        put(v, 19, SG_A_BODY, "This takes about a minute.");
        break;
    case SGU_DOWNLOADING: case SGU_VERIFYING:
        put(v, 7, SG_A_HI, "Setting up Multishine system files");
        ln_init(&l); bar(&l, u->state == SGU_DOWNLOADING ? u->pct : 100u); putl(v, 10, SG_A_BODY, &l);
        put(v, 13, SG_A_BODY, "Downloading the setup data.");
        foot_s = u->state == SGU_DOWNLOADING ? "B: Cancel" : "Working..."; break;
    default:
        put(v, 7, SG_A_HI, "Setting up Multishine system files");
        put(v, 10, SG_A_BODY, "Checking the update server...");
        foot_s = "B: Cancel"; break;
    }
    foot(v, foot_s);
}

static void d_upd(const struct sg *g, struct sg_view *v)
{
    const struct sgu_view *u = &g->upd;
    struct ln l;
    const char *a = "", *b = "", *foot_s = "B: Back";
    unsigned eng, fil, run;
#ifndef SG_TITLE
#define SG_TITLE "Update"                     /* overlay builds may carry another title (the OTA proof pushes "UPDATE v2" through the card only) */
#endif
    if (g->sysf) { d_sysf(g, v); return; }
    sg_head(v, SG_TITLE, "More - Settings - Update");
#define UR(r) (4u + ((r) - 4u) * 3u / 2u)        /*: three grid rows between lines (was two): 6 8 10 12 14 -> 7 10 13 16 19 */
    ln_init(&l);
    eng = u->kind ? u->other : u->installed; fil = u->kind ? u->installed : u->other;
    run = eng > fil ? eng : fil;
    if (u->kind == 2) run = u->run_rel;      /* the network patch has its own number: the release line is the running release */          /* to the player it is ONE release: the highest of the two parts; the split is a small detail line for the builders */
    ln_s(&l, "Running: ");
    if (u->run_rel) { ln_s(&l, "release "); ln_u(&l, u->run_rel, 0); } else ln_s(&l, "release not known");
    putl(v, 4, SG_A_BODY, &l);
    switch (u->state) {
    case SGU_CHECKING:
        put(v, UR(6), SG_A_HI, "Checking for an update..."); foot_s = "B: Cancel"; break;
    case SGU_UPTODATE:
        ln_init(&l); ln_s(&l, "Up to date. Release "); ln_u(&l, run, 0); ln_s(&l, " is the newest.");
        putl(v, UR(6), SG_A_GOOD, &l);
        if (u->same) { put(v, UR(8), SG_A_BODY, "X installs this same release again, to repair it."); foot_s = "A: Check again  X: Reinstall  B: Back"; }
        else foot_s = "A: Check again  B: Back";
        break;
    case SGU_AVAILABLE:
        if (u->kind == 2) {                                  /* a network patch is on offer (standalone, after the release check): only server addresses change */
            ln_init(&l); ln_s(&l, "Network settings available: "); ln_u(&l, u->latest, 0); putl(v, UR(6), SG_A_HI, &l);
            put(v, UR(8), SG_A_BODY, "Changes the server address and keys only.");
            put(v, UR(10), SG_A_BODY, "Your account and settings stay as they are.");
            foot_s = g->direct ? "A: Install  B: Later" : "A: Install  B: Back"; break;
        }
        ln_init(&l); ln_s(&l, u->same ? "Reinstall release " : "Update available: release "); ln_u(&l, u->latest, 0); putl(v, UR(6), SG_A_HI, &l);
        if (u->armed) {                                      /* small card: no spare copy, so a power cut while writing leaves the card without Multishine */
            put(v, UR(8), SG_A_BAD, "Small memory card: there is no spare copy.");
            put(v, UR(10), SG_A_BAD, "A power cut while writing leaves no Multishine.");
            put(v, UR(12), SG_A_BODY, "It then needs the network or a copy by hand.");
            foot_s = "A: Install anyway  B: Back"; break;
        }
        if (u->tier == 4) {                                  /* the combined release does not fit on this card, even on the small-card plan: say how much to free (nothing is written) */
            put(v, UR(8), SG_A_BAD, "Not enough room on this memory card");
            ln_init(&l); ln_s(&l, "for an update. Free "); ln_u(&l, u->code, 0); ln_s(&l, u->code == 1u ? " block." : " blocks."); putl(v, UR(10), SG_A_BAD, &l);
            foot_s = "A: Check again  B: Back"; break;
        }
        ln_init(&l); ln_s(&l, "Size "); ln_u(&l, u->size_kb, 0); ln_s(&l, u->same ? " KB. The same signed build, written again." : " KB. A few seconds to download."); putl(v, UR(8), SG_A_BODY, &l);
        put(v, UR(10), SG_A_BODY, "Your account and settings stay as they are.");
        if (u->tier == 3) put(v, UR(12), SG_A_HI, "Small card: no spare copy of the old files.");
        foot_s = u->note_ours == SGU_NOTE_TOO_OLD ? "A: Update now  B: Back" : g->direct ? "A: Download  B: Later" : "A: Download  B: Back"; break;
    case SGU_DOWNLOADING:
        put(v, UR(6), SG_A_HI, "Downloading...");
        ln_init(&l); bar(&l, u->pct); putl(v, UR(10), SG_A_BODY, &l);
        foot_s = "B: Cancel"; break;
    case SGU_VERIFYING:
        if (u->pct < 100u) {          /* the first part of a combined release: the screen does not change, the one heading and the one bar stay where they were */
            put(v, UR(6), SG_A_HI, "Downloading...");
            ln_init(&l); bar(&l, u->pct); putl(v, UR(10), SG_A_BODY, &l);
            foot_s = "Working..."; break;
        }
        put(v, UR(6), SG_A_HI, "Checking the download...");
        put(v, UR(8), SG_A_BODY, "Checking the signature.");
        foot_s = "Working..."; break;
    case SGU_INSTALLING:
        if (u->pct < 100u) {          /* the first part of a combined release is being written: same heading and bar, only the warning is added below */
            put(v, UR(6), SG_A_HI, "Downloading...");
            ln_init(&l); bar(&l, u->pct); putl(v, UR(10), SG_A_BODY, &l);
            put(v, UR(14), SG_A_BAD, "Do not turn the power off.");
            foot_s = "Working..."; break;
        }
        put(v, UR(6), SG_A_HI, "Writing to the memory card...");
        if (u->tier == 2) {
            put(v, UR(8), SG_A_BAD, "Do not turn the power off.");
            put(v, UR(10), SG_A_BAD, "There is no spare copy. Wait until it ends.");
            put(v, UR(12), SG_A_BODY, "This can take several seconds.");
        } else {
            put(v, UR(8), SG_A_BAD, "Do not turn the power off.");
            put(v, UR(10), SG_A_BODY, "This can take several seconds.");
        }
        if (u->kind != 2 && g->bak != 1u) put(v, UR(14), SG_A_BODY, "Card, Multishine and Slippi logins go to slot B.");
        foot_s = "Working..."; break;
    case SGU_RESTART:
        if (u->kind == 2) { ln_init(&l); ln_s(&l, "Network settings "); ln_u(&l, u->latest, 0); ln_s(&l, " are installed."); putl(v, UR(6), SG_A_GOOD, &l); }
        else {
        ln_init(&l); ln_s(&l, "Release "); ln_u(&l, u->latest, 0); ln_s(&l, u->same ? " is installed on the card again." : " is installed on the card."); putl(v, UR(6), SG_A_GOOD, &l);
        }
        if (u->noreset) {
            put(v, UR(8), SG_A_HI, "Switch the console off and on again");
            put(v, UR(10), SG_A_HI, "to start using it.");
            put(v, UR(12), SG_A_BODY, "Your account and settings stay as they are.");
            if (bak_line(u->backup)) put(v, UR(14), SG_A_BODY, bak_line(u->backup));
            foot_s = "B: Back"; break;
        }
        ln_init(&l); ln_s(&l, "Restarting in "); ln_u(&l, u->restart_s, 0); ln_s(&l, u->restart_s == 1u ? " second..." : " seconds...");
        putl(v, 21, SG_A_HI, &l);                                   /* the countdown at the bottom, above the footer */
        put(v, UR(8), SG_A_BODY, "Your account and settings stay as they are.");
        if (bak_line(u->backup)) put(v, UR(10), u->backup == SGU_BAK_DONE || u->backup == SGU_BAK_OFF ? SG_A_BODY : SG_A_HI, bak_line(u->backup));      /* replaces the third line: the 8-object cap */
        else put(v, UR(10), SG_A_BODY, "The console restarts and Melee starts as usual.");
        foot_s = "A: Restart now"; break;
    case SGU_FAILED:
        upd_reason(u->reason, &a, &b);
        if (u->reason == SGU_R_NOHOST || u->reason == SGU_R_NOFILES) put(v, UR(6), SG_A_HI, "No update available.");      /* nothing to install is not a failure */
        else put(v, UR(6), SG_A_BAD, "The update failed.");
        if (u->reason == SGU_R_FULL && u->code) {            /* a combined release that does not fit: the number to free */
            ln_init(&l); ln_s(&l, "for an update. Free "); ln_u(&l, u->code, 0); ln_s(&l, u->code == 1u ? " block." : " blocks.");
            put(v, UR(8), SG_A_BODY, "Not enough room on this memory card"); putl(v, UR(10), SG_A_BODY, &l); put(v, UR(12), SG_A_BODY, "Your old version still works.");
        } else { put(v, UR(8), SG_A_BODY, a); put(v, UR(10), SG_A_BODY, b); }
        if (u->reason != SGU_R_NOHOST && u->reason != SGU_R_NOFILES && !(u->reason == SGU_R_FULL && u->code)) upd_tech_rows(u, v);       /* the reason code is always visible on a real failure, small and dim */
        foot_s = u->reason == SGU_R_NOHOST || u->reason == SGU_R_ABI ? "B: Back" : "A: Try again  B: Back"; break;
    case SGU_OFFLINE:
        put(v, UR(6), SG_A_BAD, "Offline.");
        put(v, UR(8), SG_A_BODY, "Couldn't reach the update server.");
        put(v, UR(10), SG_A_BODY, "Check the network cable. Your game still works.");
        foot_s = "A: Try again  B: Back"; break;
    default:
        put(v, UR(6), SG_A_BODY, "Check for a new version of Multishine.");
        put(v, UR(8), SG_A_BODY, "Nothing is changed until you say so.");
        foot_s = "A: Check  B: Back"; break;
    }
    {
        unsigned r = upd_notes(u, v);
        unsigned ob = upd_cur_bak(g), ok = upd_cur_keep(g);
        if (bak_shown(g) || keep_shown(g) || copy_shown(g)) {       /* the option rows (MultishineOptions, saved by the resident): Back up before updating, Keep the previous build, Copy card to slot B */
                        unsigned nr = (bak_shown(g) ? 1u : 0u) + (keep_shown(g) ? 1u : 0u) + (copy_shown(g) ? 1u : 0u);
                        unsigned first = 19u - 2u * (nr - 1u);   /* two rows: 17 and 19, three: 15, 17, 19, so the last highlight (19-20) stays above the hint line (22); at 19 and 21 it covered it */
            if (r < first || r == 16u) r = first;      /* (16 = no notes) */
            if (bak_shown(g)) {
                unsigned a = g->upd_cur == ob ? SG_A_HI : SG_A_BODY;
                put(v, r, a | SG_A_BIG, "Back up before updating");
                sg_putv(v, r, a | SG_A_BIG, g->bak == 2u ? "On" : "Off");
                if (g->upd_cur == ob) { sg_bar(v, r, 1); put(v, UI_R_DESC, SG_A_DIM, "Slot B gets a copy first, if a card is in."); }
                r += 2u;
            }
            if (keep_shown(g)) {
                unsigned a = g->upd_cur == ok ? SG_A_HI : SG_A_BODY;
                put(v, r, a | SG_A_BIG, "Keep previous build as backup");
                sg_putv(v, r, a | SG_A_BIG, g->keep == 2u ? "On" : "Off");
                if (g->upd_cur == ok) { sg_bar(v, r, 1); put(v, UI_R_DESC, SG_A_DIM, g->keep == 2u ? "Old build stays as fallback (about 130 blocks)." : "Off: no fallback; frees ~130 blocks next update."); }
                r += 2u;
            }
            if (copy_shown(g)) {
                unsigned a = g->upd_cur == upd_cur_copy(g) ? SG_A_HI : SG_A_BODY;
                put(v, r, a | SG_A_BIG, "Set up Multishine on card slot B");
                if (u->copyb) sg_putv(v, r, a | SG_A_BIG, copy_val(u->copyb));
                if (g->upd_cur == upd_cur_copy(g)) { sg_bar(v, r, 1); put(v, UI_R_DESC, SG_A_DIM, copy_desc(u->copyb)); }
            }
            if (g->upd_cur) foot_s = copy_shown(g) && g->upd_cur == upd_cur_copy(g) ? "A: Copy  B: Back" : "A: Change  B: Back";                 /* no stick hint in a footer */
        }
    }
    foot(v, foot_s);
}


static struct ln *ln_i(struct ln *l, int v)                          /* signed, 4 columns: " 041" / "-041" */
{
    sdc_u32 a = (sdc_u32)(v < 0 ? -v : v);
    ln_s(l, v < 0 ? "-" : " ");
    if (a < 100u) ln_s(l, "0");
    if (a < 10u) ln_s(l, "0");
    return ln_u(l, a, 0);
}
static struct ln *ln_h2(struct ln *l, unsigned v)
{
    static const char hx[] = "0123456789ABCDEF";
    char t[3];
    t[0] = hx[(v >> 4) & 15u]; t[1] = hx[v & 15u]; t[2] = 0;
    return ln_s(l, t);
}
static struct ln *ln_h4(struct ln *l, unsigned v) { ln_h2(l, (v >> 8) & 255u); return ln_h2(l, v & 255u); }
static struct ln *ln_n3(struct ln *l, unsigned v)                   /* unsigned, 3 digits: "007" */
{
    if (v < 100u) ln_s(l, "0");
    if (v < 10u) ln_s(l, "0");
    return ln_u(l, v, 0);
}

static void d_ctl(const struct sg *g, struct sg_view *v)
{
    const struct sg_ctl_t *t = &g->ctl;
    const struct sdent_pad *p = &t->cur;
    static const struct { unsigned bit; const char *name; } bt[] = {
        { SG_B_A, "A" }, { SG_B_B, "B" }, { SG_B_X, "X" }, { SG_B_Y, "Y" }, { SG_B_Z, "Z" }, { SG_B_L, "L" }, { SG_B_R, "R" }, { SG_B_START, "Start" } };
    static const struct { unsigned bit; const char *name; } dp[] = { { SG_B_UP, "Up" }, { SG_B_DOWN, "Down" }, { SG_B_LEFT, "Left" }, { SG_B_RIGHT, "Right" } };
    static const char *const axn[4] = { "Main X", "Main Y", "C X   ", "C Y   " };
    struct ln l;
    unsigned i, n;
    sg_head(v, "Controller test", "More - Settings - Diagnostics - Controller test");
    sg_head_tag(v, t->frozen ? SG_A_HI : SG_A_GOOD, t->frozen ? "FROZEN" : "LIVE");
    put(v, 3, SG_A_DIM, "Raw values as the console reads them. Read only.");
    ln_init(&l); ln_s(&l, "Port "); ln_u(&l, (unsigned)t->port + 1u, 0); ln_s(&l, "      other ports with a pad: ");
    for (i = 0, n = 0; i < 4; i++) if (t->others & (1u << i)) { if (n) ln_s(&l, " "); ln_u(&l, i + 1u, 0); n++; }
    if (!n) ln_s(&l, "none");
    putl(v, 4, SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "Main stick   X "); ln_i(&l, p->sx); ln_s(&l, "   Y "); ln_i(&l, p->sy); ln_s(&l, "   raw "); ln_h2(&l, (unsigned char)p->sx); ln_s(&l, " "); ln_h2(&l, (unsigned char)p->sy);
    putl(v, 6, SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "C stick      X "); ln_i(&l, p->cx); ln_s(&l, "   Y "); ln_i(&l, p->cy); ln_s(&l, "   raw "); ln_h2(&l, (unsigned char)p->cx); ln_s(&l, " "); ln_h2(&l, (unsigned char)p->cy);
    putl(v, 7, SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "Triggers     L "); ln_n3(&l, p->tl); ln_s(&l, "   R "); ln_n3(&l, p->tr);
    putl(v, 8, SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "Buttons      "); ln_h4(&l, p->buttons); ln_s(&l, "  ");
    for (i = 0, n = 0; i < sizeof bt / sizeof bt[0]; i++) if (p->buttons & bt[i].bit) { if (n) ln_s(&l, " "); ln_s(&l, bt[i].name); n++; }
    if (!n) ln_s(&l, "none");
    putl(v, 9, SG_A_BODY, &l);
    ln_init(&l); ln_s(&l, "D-pad        ");
    for (i = 0, n = 0; i < 4; i++) if (p->buttons & dp[i].bit) { if (n) ln_s(&l, " "); ln_s(&l, dp[i].name); n++; }
    if (!n) ln_s(&l, "none");
    putl(v, 10, SG_A_BODY, &l);
    if (!t->moving) {
        ln_init(&l); ln_s(&l, "Rest value   X "); ln_i(&l, p->sx); ln_s(&l, "   Y "); ln_i(&l, p->sy); putl(v, 12, SG_A_BODY, &l);
        ln_init(&l); ln_s(&l, "Rest range   X "); ln_i(&l, t->wlo[0]); ln_s(&l, "..."); ln_i(&l, t->whi[0]); ln_s(&l, "  Y "); ln_i(&l, t->wlo[1]); ln_s(&l, "..."); ln_i(&l, t->whi[1]);
        putl(v, 13, SG_A_DIM, &l);
    } else {
        put(v, 12, SG_A_BODY, "Rest value   moving");
        ln_init(&l); ln_s(&l, "Still for "); ln_u(&l, t->still, 0); ln_s(&l, " of "); ln_u(&l, SG_CTL_STILL_FRAMES, 0); ln_s(&l, " frames");
        putl(v, 13, SG_A_DIM, &l);
    }
    if (t->off_centre) put(v, 15, SG_A_BAD, "OFF CENTRE   the stick rests away from centre");
    else if (!t->moving) put(v, 15, SG_A_GOOD, "Centre ok     rest value within 8 counts");
    else put(v, 15, SG_A_DIM, "Centre        let the stick go to find its rest");
    if (t->not_changing) put(v, 16, SG_A_BAD, "NOT CHANGING  same input for 5 seconds");
    else put(v, 16, SG_A_DIM, "Input is changing or idle");
    put(v, 18, SG_A_DIM, "Since the page opened       min    max");
    for (i = 0; i < 4; i++) {
        ln_init(&l); ln_s(&l, axn[i]); ln_at(&l, 28); ln_i(&l, t->mn[i]); ln_s(&l, "   "); ln_i(&l, t->mx[i]);
        putl(v, 19 + i, SG_A_BODY, &l);
    }
    ln_init(&l); ln_s(&l, "Frames "); ln_u(&l, t->frames, 0); ln_s(&l, "   err "); ln_h2(&l, p->err);
    putl(v, 23, SG_A_BODY, &l);
    if (t->frozen) { ln_init(&l); ln_s(&l, "Display frozen at frame "); ln_u(&l, t->frozen_at, 0); ln_s(&l, ". Hold Z to run."); putl(v, 17, SG_A_HI, &l); }      /* row 17: the footer now sits on row 24 */
    foot(v, "Hold Z: Freeze  Hold B: Back");
}

/* Settings > About: GPL-3.0-or-later section 5(d) notice plus every attribution and licence, SCROLLED ("scrollable with all the attributions and
   licenses").  the maintainer's own lines are kept word for word; the rest is the component list of as facts (component, people, licence), no new
   credit prose; items that file still marks open (UCF author names, the Rooms art) keep our earlier wording.  The licence texts are on the site (Help). */
struct ab_line { unsigned char attr; const char *t; };
static const struct ab_line AB[] = {
    { SG_A_SEC, "Licence" },
    { SG_A_DIM, "Free software, GNU GPL v3. No warranty." },
    { SG_A_DIM, "Licences and source: multishine.link, FAQ" },    /* the repository link is at the bottom of the site's FAQ page */
    { SG_A_DIM, "Alpha: full source is published at release." },   /*: alpha testers agree to wait for the source until the release */
    { SG_A_DIM, "Licence texts: GPL-3.0, MPL-2.0, zlib, MIT." },
    { SG_A_SEC, "Third party" },
    { SG_A_BODY, "Project Slippi: netplay code, Gecko codes" },
    { SG_A_BODY, "UCF 0.84 as packaged by Project Slippi" },
    { SG_A_BODY, "UnclePunch, Achilles1515, tauKhan: codes" },
    { SG_A_BODY, "Rooms: adapted from attunegames Rooms," },
    { SG_A_BODY, "   whose design was similar to ours." },
    { SG_A_BODY, "Name Entry exploit technique, loader format:" },
    { SG_A_BODY, "   wParam 2008, Extrems of Home Bros 2015," },
    { SG_A_BODY, "   the 20XX and UCF teams" },
    { SG_A_BODY, "Exploit files: practicaltas, MPL-2.0" },
    { SG_A_BODY, "TweetNaCl, libogc, ENet: crypto, BBA, net" },
    { SG_A_SEC, "In detail" },
    { SG_A_BODY, "slippi-ssbm-asm, GPL-3.0: Jas Laferriere," },
    { SG_A_BODY, "   Vin Bertinelli, KELLZ, UnclePunch," },
    { SG_A_BODY, "   Calvin Keats, Kyle Havlovitz, Nikhil" },
    { SG_A_BODY, "   Narayana and Project Slippi contributors" },
    { SG_A_BODY, "Slippi Ishiiruka, GPL-2.0 or later:" },
    { SG_A_BODY, "   the netplay protocol we follow" },
    { SG_A_BODY, "Slippi slippi-ssbm-c, GPL-3.0: the stage" },
    { SG_A_BODY, "   strike screen and its pictures" },
    { SG_A_BODY, "melee-gci-compiler by practicaltas, MPL-2.0:" },
    { SG_A_BODY, "   the exploit save files" },
    { SG_A_BODY, "BBA driver parts after libogc and libogc2," },
    { SG_A_BODY, "   devkitPro and Extrems, zlib licence" },
    { SG_A_BODY, "Ed25519 after TweetNaCl, public domain:" },
    { SG_A_BODY, "   D. J. Bernstein, B. van Gastel, W. Janssen," },
    { SG_A_BODY, "   T. Lange, P. Schwabe, S. Smetsers" },
    { SG_A_BODY, "X25519 after ref10, D. J. Bernstein et al." },
    { SG_A_BODY, "ENet protocol by Lee Salzman, MIT" },
    { SG_A_BODY, "Rank icons: Project Slippi, slippi-launcher," },
    { SG_A_BODY, "   GPL-3.0" },
    { SG_A_BODY, "Font: Aileron by Sora Sagano, CC0" },
    { SG_A_BODY, "Patch format: VCDIFF, RFC 3284" },
    { SG_A_BODY, "QR Code is a registered trademark of" },
    { SG_A_BODY, "   DENSO WAVE INCORPORATED" },
    { SG_A_SEC, "Notices" },
    { SG_A_BODY, "Not affiliated with Nintendo or Project Slippi." },
    { SG_A_BODY, "Do not report Multishine problems to Slippi." },
    { SG_A_BODY, "No rights claimed in Melee. Own a copy to play." },
    { SG_A_BODY, "Super Smash Bros. Melee: Nintendo, HAL." },
};
#define AB_N (sizeof AB / sizeof AB[0])
#define AB_ROW0 8u               /* the scrolled list: rows 8, 10... 20, two grid rows apart (spaced, not packed; Multishine, Build and Matches Slippi stay at the top) */
#define AB_ROWS 7u
unsigned sg_about_lines(void) { return (unsigned)AB_N; }
static void d_about(const struct sg *g, struct sg_view *v)
{
    struct ln l;
    unsigned i, top = g->cur;
    sg_head(v, "About", "More - Settings - About");
    put(v, 4, SG_A_HI, "Multishine");
    ln_init(&l);          /* the same number as the Update screen's 'Running: release N' (run_rel); the Slippi version is the one the bridge speaks (upd_ident.h) */
    if (g->upd.run_rel) ln_u(&l, g->upd.run_rel, 0); else ln_s(&l, "not known");
    put(v, 5, SG_A_BODY, "Build");
    sg_putv(v, 5, SG_A_BODY, l.b);
    put(v, 6, SG_A_BODY, "Matches Slippi");
    sg_putv(v, 6, SG_A_BODY, UPDID_SLIPPI_COMPAT);
    for (i = 0; i < AB_ROWS && top + i < AB_N; i++) put(v, AB_ROW0 + 2u * i, AB[top + i].attr, AB[top + i].t);
    ln_init(&l); ln_u(&l, top + 1u, 0); ln_s(&l, " - "); ln_u(&l, top + i, 0); ln_s(&l, " of "); ln_u(&l, (unsigned)AB_N, 0);
    put(v, 22, SG_A_DIM, top + AB_ROWS < AB_N ? "More below." : top ? "End of the list." : "");
    sg_putv(v, 22, SG_A_DIM, l.b);
    foot(v, "B: Back");                                                    /*: no stick help */
}

void sg_draw(const struct sg *g, struct sg_view *v)
{
    unsigned r;
    for (r = 0; r < SG_ROWS; r++) { v->t[r][0] = 0; v->attr[r] = SG_A_BODY; v->val[r][0] = 0; v->vattr[r] = SG_A_BODY; }
    v->qr_n = 0; v->qr_row = 0; v->qr_rows = 0; v->bar = 0; v->bar_big = 0; v->mark = 0;
    switch (g->scr) {
    case SG_HUB: d_hub(g, v); break;
    case SG_IDENT: sg_ident_draw(g, v); break;
    case SG_DELID: sgd_draw(g, v); break;
    case SG_MAKEKEY: d_makekey(g, v); break;
    case SG_LINK: sgl_draw(g, v); break;
    case SG_SLOGIN: sgr_draw(&g->sr, v); break;
    case SG_PROV: d_prov(g, v); break;
    case SG_PROV_DETAIL: d_provd(g, v); break;
    case SG_ENT: d_ent(g, v); break;
#if SG_LAB
    case SG_TLS: d_tls(g, v); break;
#endif
    case SG_UPD: d_upd(g, v); break;
    case SG_CTL: d_ctl(g, v); break;
    case SG_ABOUT: d_about(g, v); break;
    case SG_FRD: frd_text(&g->fr, v); break;
    case SG_STAND: stb_text(&g->sd, v); break;
    case SG_TOURN: gnt_text(&g->tn, v); break;
    default: if (g->scr >= SG_MOCK) sgm_draw(g, v); break;
    }
    if (g->scr == SG_HUB || g->scr == SG_UPD || g->scr == SG_IDENT) {     /* ONE size on the list pages: body text and descriptions in the list font too (the footer stays small; a line too long for it keeps the small font: ml_glue.c ui_text) */
        unsigned r;
        for (r = UI_R_BODY - 1u; r < SG_ROWS - 2u; r++) {                  /* not the footer row (SG_ROWS - 2): a big footer lost its button badges */
            unsigned a = v->attr[r] & 15u;
            if (v->t[r][0] && a <= SG_A_BAD) v->attr[r] |= SG_A_BIG;
            if (v->val[r][0] && (v->vattr[r] & 15u) <= SG_A_BAD) v->vattr[r] |= SG_A_BIG;
        }
    }
}
