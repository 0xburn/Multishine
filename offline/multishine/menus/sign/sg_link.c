/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Link This Console (/ 2.4): the screen's state machine and its text.  Pure C89, no libc, no clock of
 * its own (the glue writes k->now in milliseconds every frame), no network and no card: it raises requests (k->req) that sg_entry.c answers with client_net/lnk.c (the
 * sealed LINK_POLL) and the resident card writer, and it is host-tested through tests/sg_link_test.c.
 *
 *   CHECK   linked already -> ALREADY ("Linked as NAME", A twice = link again); else the pool decides
 *   ENT     the entropy pass ("Getting ready"): only while the pool is short of the SESSION level (128 credited bits); A and B are pool input, hold B leaves;
 *           at 100 percent the link starts by itself (no key press)
 *   START   LINK_START                                   SHOW  QR of SGL_URL + the user code, polls every `interval` s, the code's life counted down
 *   EXPIRED A: a new code                                SAVE  the card write of the new identity, VERIFY  a handshake with the new secret, DONE  name and code
 *   OFFER   the first-run question (A link now, B later); with it the chain starts: leaving the Link flow (done, Later, Back after an error) goes to SLCHK (the glue looks for
 *           a Slippi login on the card and the "offered before" marker), then SLOFFER (A: the Slippi login screen, B: later) or the end of the first run
 *   ERR     one line per failure (the design's table); A retries the step that failed, B goes back (a card failure: B keeps the link until power off)
 */
#include "sg_ui.h"

static void kzero(void *p, sdc_u32 n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }
static void kcopy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s && s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; }

void sgl_init(struct sgl *k)
{
    kzero(k, sizeof *k);
    sdent_init(&k->pool);
}

static void to_start(struct sgl *k) { k->st = SGL_START; k->req = SGL_R_START; k->err = SGL_E_NONE; }

static void begin(struct sgl *k)
{
    k->err = SGL_E_NONE; k->req = SGL_R_NONE; k->saved = 0; k->verified = 0;
    if (sdent_ready(&k->pool, SDENT_L_SESSION)) { to_start(k); return; }
    k->st = SGL_ENT; k->ent_frames = 0; k->ent_best = 0; k->ent_best_at = 0; k->nopad = 0; k->hold = 0;
}

void sgl_enter(struct sg *g)
{
    struct sgl *k = &g->lk;
    k->again = 0;
    if (g->linked == SG_ID_LINKED || g->linked == SG_ID_RAMONLY) { k->st = SGL_ALREADY; k->req = SGL_R_NONE; return; }
    begin(k);
}

void sgl_offer(struct sg *g)
{
    struct sgl *k = &g->lk;
    if (k->st == SGL_ALREADY) return;              /* linked after all: nothing to offer (the screen says so) */
    k->st = SGL_OFFER; k->req = SGL_R_NONE;
    k->chain = 1;                                  /* the first run: the Slippi offer follows (sgl_slchecked) */
}

/* leave the screen: the Online page (cursor on Link, or on Device Identity after a link), or out of the overlay when it was opened straight from the menu (first-run offer) */
static void leave(struct sg *g, int done)
{
    struct sgl *k = &g->lk;
    if (k->st == SGL_SHOW || k->st == SGL_EXPIRED || (k->st == SGL_ERR && k->err_from == SGL_SHOW)) k->req = SGL_R_CANCEL;   /* the pending code goes on the server too */
    if (k->chain) { k->chain = 0; k->st = SGL_SLCHK; k->slasked = 0; k->noslp = done ? 0u : 1u; return; }          /* the first run goes on (after a pending cancel of the code): the Slippi offer follows only a link that completed; Later or an error ends the Slippi part (it was offered to consoles that never linked) */
    k->st = SGL_OFF;
    if (g->direct) { g->quit = 1; return; }
    sg_goto(g, SG_HUB);                            /* (Settings > Account: sg_goto lands on the Account page) */
    if (done && g->scr == SG_HUB) g->cur = SG_ON_IDENT;
}

/* the end of the first run: the overlay leaves (the progressive scan question is the FIRST step now,: sgl_progq) */
static void end_chain(struct sg *g)
{
    struct sgl *k = &g->lk;
    k->st = SGL_OFF;
    if (g->direct) { g->quit = 1; return; }
    sg_goto(g, SG_HUB);
}

/* the progressive scan question alone (SG_GOTO_PROG without SG_GOTO_OFFER): the first step of the first run, the glue opens the overlay on it before the system files and the link offer.  The Link screen was entered by sg_goto just before: its start request must not stand */
void sgl_progq(struct sg *g) { struct sgl *k = &g->lk; k->req = SGL_R_NONE; k->err = SGL_E_NONE; k->chain = 0; k->st = SGL_PROGQ; }

/* the glue looked at the card and the marker: show = 1 opens the Slippi offer, else the first run ends (the overlay leaves) */
void sgl_slchecked(struct sg *g, int show)
{
    struct sgl *k = &g->lk;
    if (k->st != SGL_SLCHK) return;
    if (show) { k->st = SGL_SLOFFER; return; }
    end_chain(g);
}

void sgl_started(struct sgl *k, const char *user, unsigned expires, unsigned interval)
{
    char url[64];
    unsigned i, n = 0;
    unsigned char qn = 0;
    if (k->st != SGL_START) return;
    kcopy(k->user, user, 9);
    for (i = 0; SGL_URL[i]; i++) url[n++] = SGL_URL[i];
    for (i = 0; user[i] && n < sizeof url - 1u; i++) if (user[i] != '-') url[n++] = user[i];   /* the user code without its dash */
    url[n] = 0;
    k->qr_ok = qr_encode(url, n, k->qr, &qn) == QR_OK ? 1 : 0;
    k->qr_n = k->qr_ok ? qn : 0;
    k->t0 = k->now; k->expires = expires ? expires : 600u; k->interval = interval ? interval : 5u;
    k->next_poll = k->now + k->interval * 1000u;
    k->st = SGL_SHOW;
}

static void error(struct sgl *k, unsigned err)
{
    k->err_from = k->st;
    k->err = (unsigned char)err;
    k->st = SGL_ERR;
    k->req = SGL_R_NONE;
}

void sgl_failed(struct sgl *k, unsigned err, unsigned status)
{
    k->status = (unsigned char)status;
    if (k->st != SGL_START && k->st != SGL_SHOW) return;
    if (status) {
        switch (status) {
        case 21: k->next_poll = k->now + k->interval * 1000u; return;                       /* LINK_PENDING: not approved yet */
        case 23: k->next_poll = k->now + (k->interval + 5u) * 1000u; return;                /* SLOW_DOWN */
        case 22: if (k->st == SGL_SHOW) { k->st = SGL_EXPIRED; k->req = SGL_R_NONE; return; } err = SGL_E_BAD; break;   /* LINK_EXPIRED (also: turned down on the website) */
        case 27: err = SGL_E_DEVLIMIT; break;
        case 5: err = SGL_E_BANNED; break;
        case 3: case 12: err = SGL_E_RATE; break;
        case 40: k->busy_s = 10; err = SGL_E_BUSY; break;
        case 41: case 17: k->busy_s = 30; err = SGL_E_BUSY; break;
        case 42: k->busy_s = 120; err = SGL_E_BUSY; break;
        case 43: err = SGL_E_MAINT; break;
        case 1: err = k->st == SGL_SHOW ? SGL_E_OLD : SGL_E_BAD; break;                        /* BAD_REQUEST to the sealed poll: a server without it */
        default: err = SGL_E_BAD; break;
        }
    }
    error(k, err ? err : SGL_E_UNREACH);
}

void sgl_linked(struct sgl *k, const char *code, const char *name)
{
    if (k->st != SGL_SHOW) return;
    kcopy(k->code, code, 23); kcopy(k->name, name, 23);
    k->st = SGL_SAVE; k->req = SGL_R_SAVE;
}

void sgl_saved(struct sgl *k, int result)
{
    if (k->st != SGL_SAVE) return;
    if (result == 0) { k->saved = 1; k->st = SGL_VERIFY; k->req = SGL_R_VERIFY; return; }
    error(k, result == -1 ? SGL_E_NOREC : SGL_E_CARD);
}

void sgl_verified(struct sgl *k, int ok)
{
    if (k->st != SGL_VERIFY) return;
    k->verified = (unsigned char)(ok ? 1 : 0);
    k->st = SGL_DONE;
}

void sgl_frame(struct sg *g, unsigned edge, const struct sdent_pad *p, sdc_u32 tb)
{
    struct sgl *k = &g->lk;
    switch (k->st) {
    case SGL_OFFER:                                     /* the first-run offer: A = Link Now (the entropy pass when the pool is short, then the code), B = Later */
        if (edge & SG_B_A) begin(k);
        else if (edge & SG_B_B) leave(g, 0);
        break;
    case SGL_SLCHK:
        if (k->req == SGL_R_NONE && k->noslp) { k->noslp = 0; end_chain(g); }                    /* the console is not linked (Later, an error): no Slippi offer, the first run ends */
        else if (k->req == SGL_R_NONE && !k->slasked) { k->slasked = 1; k->req = SGL_R_SLCHK; }      /* after a pending cancel of the code was taken */
        break;
    case SGL_SLOFFER:                                   /* the Slippi offer after the account step: A = the Slippi login screen (Back from it leaves the overlay), B = Later */
        if (edge & SG_B_A) { k->st = SGL_OFF; sg_goto(g, SG_SLOGIN); }
        else if (edge & SG_B_B) end_chain(g);
        break;
    case SGL_PROGQ:                                     /* the progressive scan question, FIRST step of the first run: A = Yes, try it (the test runs INLINE: the glue starts the same 10 s trial as Settings > Game, SGL_PROGK), B = Not now (the glue stores Off) */
        if (edge & SG_B_A) { g->prog_try = SG_PT_YES; g->prog_wii = 10u; k->st = SGL_PROGK; }
        else if (edge & SG_B_B) { g->prog_try = SG_PT_NO; k->st = SGL_OFF; if (g->direct) g->quit = 1; else sg_goto(g, SG_HUB); }
        break;
    case SGL_PROGK:                                     /* the keep / go back step: the real safety net.  A keeps, B or waiting goes back; the glue does the picture and the card and says DONE; the overlay then leaves and the next first-run steps follow */
        if (g->prog_try == SG_PT_RUN) { if (edge & SG_B_A) g->prog_try = SG_PT_KEEP; else if (edge & SG_B_B) g->prog_try = SG_PT_BACK; }
        else if (g->prog_try == SG_PT_DONE) { k->st = SGL_OFF; if (g->direct) g->quit = 1; else sg_goto(g, SG_HUB); }
        break;
    case SGL_ALREADY:
        if (edge & SG_B_A) { if (!k->again) k->again = 1; else begin(k); }
        else if (edge & SG_B_B) leave(g, 0);
        break;
    case SGL_ENT: {
        sdc_u32 pct = sdent_progress(&k->pool, SDENT_L_SESSION);
        k->ent_frames++;
        if (pct > k->ent_best) { k->ent_best = pct; k->ent_best_at = k->ent_frames; }
        k->nopad = p->err ? k->nopad + 1u : 0u;
        if (sdent_ready(&k->pool, SDENT_L_SESSION)) { to_start(k); break; }       /* full: the link starts by itself */
        if (p->buttons & SG_B_B) {                                                  /* A and B feed the pool: leaving is B held 1.5 s (as the collecting screens) */
            if (k->hold == 0) k->hold_tb = tb;
            if (k->hold < 255u) k->hold++;
            if (k->hold >= SG_HOLD_B_FRAMES || (k->hold > 1u && (sdc_u32)(tb - k->hold_tb) >= (g->hold_lim ? g->hold_lim : SG_HOLD_B_TICKS))) { k->hold = 0; leave(g, 0); }
        } else k->hold = 0;
        break;
    }
    case SGL_START:
        if (edge & SG_B_B) { k->req = SGL_R_NONE; leave(g, 0); }
        break;
    case SGL_SHOW:
        if (edge & SG_B_B) { leave(g, 0); break; }
        if ((sdc_u32)(k->now - k->t0) >= k->expires * 1000u) { k->st = SGL_EXPIRED; k->req = SGL_R_CANCEL; break; }
        if (k->req == SGL_R_NONE && (int)(k->now - k->next_poll) >= 0) { k->req = SGL_R_POLL; k->next_poll = k->now + k->interval * 1000u; }
        break;
    case SGL_EXPIRED:
        if (edge & SG_B_A) to_start(k);
        else if (edge & SG_B_B) leave(g, 0);
        break;
    case SGL_ERR:
        if (edge & SG_B_A) {
            if (k->err_from == SGL_SAVE) { k->st = SGL_SAVE; k->req = SGL_R_SAVE; }
            else if (k->err_from == SGL_SHOW && (sdc_u32)(k->now - k->t0) < k->expires * 1000u) { k->st = SGL_SHOW; k->req = SGL_R_NONE; k->next_poll = k->now; }
            else to_start(k);
            k->err = SGL_E_NONE;
        } else if (edge & SG_B_B) {
            if (k->err_from == SGL_SAVE) { k->saved = 2; k->st = SGL_DONE; }      /* "Use until power off": the identity stays in force in RAM */
            else leave(g, 0);
        }
        break;
    case SGL_DONE:
        if (edge & (SG_B_A | SG_B_B)) leave(g, 1);
        break;
    default:
        break;
    }
}

/* ---- drawing (rows <= 49 characters, menu-font glyphs; '#' and '@' of a connect code are expanded by the glue) ---- */
static void put(struct sg_view *v, unsigned r, unsigned attr, const char *s)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (s[c] && c < SG_COLS) { v->t[r][c] = s[c]; c++; }
    if (s[c]) sg_cut_count++;
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
static void head(struct sg_view *v, const char *title) { sg_head(v, title, "More - Settings - Account - Link"); }
static void foot(struct sg_view *v, const char *s) { put(v, SG_ROWS - 2, SG_A_DIM, s); }

static void d_err(const struct sgl *k, struct sg_view *v)
{
    const char *a = "", *b = 0, *f = "A: Try again  B: Back";
    char t[8];
    switch (k->err) {
    case SGL_E_OFFLINE: a = "No network. Check the cable, then press A."; break;
    case SGL_E_UNREACH: a = "Couldn't reach the server."; break;
    case SGL_E_RATE: a = "Too many tries. Wait a minute, then press A."; break;
    case SGL_E_BUSY: a = "The server is busy. Try again in"; t[0] = 0;
        { char *e = sg_u(t, k->busy_s); e[0] = ' '; e[1] = 's'; e[2] = '.'; e[3] = 0; }
        b = t; break;
    case SGL_E_MAINT: a = "The server is down for maintenance."; break;
    case SGL_E_DEVLIMIT: a = "Your account has the most consoles allowed."; b = "Remove one on multishine.link, then press A."; break;
    case SGL_E_BANNED: a = "This server does not accept this console."; f = "B: Back"; break;
    case SGL_E_CARD: a = "Linked, but not saved to the memory card."; b = "Insert a card in slot A or B, then press A."; f = "A: Try again  B: Use until power off"; break;
    case SGL_E_NOREC: a = "Linked, but the memory card has no online"; b = "setup to save it in. Bake the card again."; f = "A: Try again  B: Use until power off"; break;
    case SGL_E_OLD: a = "This server cannot link securely yet."; b = "Update the server, then press A."; break;
    default: a = "Unknown error. Nothing was changed."; break;
    }
    if (k->err == SGL_E_BUSY) put2(v, 6, SG_A_BAD, "The server is busy. Try again in ", b);
    else { put(v, 6, SG_A_BAD, a); if (b) put(v, 7, SG_A_BODY, b); }
    foot(v, f);
}

void sgl_draw(const struct sg *g, struct sg_view *v)
{
    const struct sgl *k = &g->lk;
    unsigned i;
    switch (k->st) {
    case SGL_ENT: {
        char t[SG_COLS + 1], *e;
        sdc_u32 pct = sdent_progress(&k->pool, SDENT_L_SESSION), fill = pct * 24u / 100u;
        head(v, "Getting ready");
        put(v, 4, SG_A_BODY, "Input random stick and button events");      /* every line two grid rows apart, like the Slippi login screen */
        put(v, 6, SG_A_BODY, "to create a key. Go between fast and");
        put(v, 8, SG_A_BODY, "slow, until the bar is full.");
        put(v, 10, SG_A_DIM, "This makes the keys that protect your link.");
        for (i = 0; i < 24u; i++) t[i] = i < fill ? 'I' : '.';
        t[24] = ' ';
        e = sg_u(t + 25, pct); *e = 0;
        put(v, 13, SG_A_HI, t);
        if (k->ent_frames - k->ent_best_at > SGL_SLOW_FRAMES) put(v, 15, SG_A_DIM, "Vary your rhythm.");
        if (k->nopad > SGL_NOPAD_FRAMES) put(v, 16, SG_A_BAD, "Plug in a controller.");
        foot(v, "Hold B: Cancel");
        break;
    }
    case SGL_OFFER:
        sg_head(v, "Welcome to Multishine", "");
        put(v, 4, SG_A_BODY, "Link this console to your account to keep");
        put(v, 6, SG_A_BODY, "a name, friends, rank and rooms.");
        put(v, 9, SG_A_DIM, "Dojo works without linking.");                       /* wording */
        put(v, 11, SG_A_DIM, "Other features require an account.");
        put(v, 14, SG_A_DIM, "You need a phone or a computer for this.");      /* paragraphs one row apart */
        put(v, 16, SG_A_DIM, "You can also link it later in Settings.");
        foot(v, "A: Link now  B: Later");
        break;
    case SGL_SLCHK:
        sg_head(v, "Set up Slippi", "");
        break;
    case SGL_PROGQ:
        sg_head(v, "Sharper picture", "");                 /* (the menu font has no parentheses or question mark: the question is worded without the mark) */
        put(v, 4, SG_A_BODY, "Does your TV support progressive scan");
        put(v, 6, SG_A_BODY, "It makes the picture sharper, 480p.");
        put(v, 9, SG_A_DIM, "We test it for 10 seconds first.");
        put(v, 11, SG_A_DIM, "Nothing changes unless you keep it.");
        put(v, 14, SG_A_DIM, "You can change this later in Settings.");
        foot(v, "A: Yes, try it  B: Not now");
        break;
    case SGL_PROGK: {
        char nb[40];
        unsigned n = g->prog_wii, i = 0;
        const char *t = "Going back in ", *u = " seconds.";
        sg_head(v, "Sharper picture", "");
        put(v, 4, SG_A_BODY, "Press A to keep this picture.");
        put(v, 6, SG_A_BODY, "B or waiting goes back.");
        while (*t) nb[i++] = *t++;
        if (n >= 10u) nb[i++] = (char)('0' + n / 10u % 10u);
        nb[i++] = (char)('0' + n % 10u);
        while (*u) nb[i++] = *u++;
        nb[i] = 0;
        put(v, 10, SG_A_DIM, g->prog_try == SG_PT_RUN ? nb : "One moment.");
        foot(v, "A: Keep  B: Go back");
        break;
    }
    case SGL_SLOFFER:
        sg_head(v, "Set up Slippi", "");
        put(v, 4, SG_A_BODY, "Slippi Unranked and Direct need your own");
        put(v, 6, SG_A_BODY, "Slippi login. It is optional.");
        put(v, 9, SG_A_DIM, "Everything else works without it.");
        put(v, 11, SG_A_DIM, "You need a phone or a computer for this.");
        put(v, 14, SG_A_DIM, "You can also do this later in Settings.");
        foot(v, "A: Set up now  B: Later");
        break;
    case SGL_ALREADY:
        head(v, "Link this console");
        put2(v, 4, SG_A_GOOD, "Linked as ", g->id_name[0] ? g->id_name : "this console");
        if (g->id_code[0]) put2(v, 5, SG_A_BODY, "Connect code: ", g->id_code);
        if (k->again) { put(v, 7, SG_A_HI, "Press A again to link again."); put(v, 8, SG_A_BODY, "This replaces the identity on this console."); }
        foot(v, k->again ? "A: Link again  B: Back" : "A: Link again  B: Back");
        break;
    case SGL_START:
        head(v, "Link this console");
        put(v, 6, SG_A_BODY, "Asking the server for a code...");
        foot(v, "B: Cancel");
        break;
    case SGL_SHOW: {
        char t[SG_COLS + 1], *e;
        sdc_u32 left = k->expires * 1000u > (sdc_u32)(k->now - k->t0) ? (k->expires * 1000u - (sdc_u32)(k->now - k->t0) + 999u) / 1000u : 0u;
        head(v, "Link this console");
        e = t;
        { const char *x = "Works for "; while (*x) *e++ = *x++; }
        e = sg_u(e, left / 60u); *e++ = ':'; if (left % 60u < 10u) *e++ = '0'; e = sg_u(e, left % 60u);
        { const char *x = " more."; while (*x) *e++ = *x++; }
        *e = 0;
        sg_qr_page(v, k->qr_n, k->qr_ok ? k->qr : 0, "On your phone, scan the code, or go to", "multishine.link and open the QR page:", k->user, t, 0);
        if (!k->qr_ok) put(v, 10, SG_A_BAD, "The QR code could not be made.");
        foot(v, "B: Cancel");
        break;
    }
    case SGL_EXPIRED:
        head(v, "Link this console");
        put(v, 6, SG_A_BAD, "The code expired or was declined.");
        foot(v, "A: New code  B: Back");
        break;
    case SGL_SAVE:
        head(v, "Link this console");
        put(v, 6, SG_A_HI, "Linked. Saving to the memory card...");
        put(v, 8, SG_A_BAD, "Do not remove the memory card.");
        foot(v, "Working...");
        break;
    case SGL_VERIFY:
        head(v, "Link this console");
        put(v, 6, SG_A_HI, "Saved. Checking the link with the server...");
        foot(v, "Working...");
        break;
    case SGL_DONE:
        head(v, "Linked");
        put2(v, 4, SG_A_GOOD, "Name: ", k->name);
        put2(v, 5, SG_A_GOOD, "Connect code: ", k->code);
        put(v, 7, k->saved == 1 ? SG_A_BODY : SG_A_BAD, k->saved == 1 ? "Saved to the memory card." : "Not saved: works until power off.");
        if (!k->verified && k->saved == 1) put(v, 8, SG_A_DIM, "The server check comes with the next search.");
        foot(v, "A: Done");
        break;
    case SGL_ERR:
        head(v, "Link this console");
        d_err(k, v);
        break;
    default:
        head(v, "Link this console");
        break;
    }
}
