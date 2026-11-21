/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Device Identity and Delete This Console's Identity (/ 3.2).  Pure C89, no libc: the glue (sg_entry.c)
 * fills the identity (struct sg id_*), the clock (lk.now) and answers the requests of the delete sequence:
 *   ASK     THREE levels, Up / Down, default 1: Multishine only / Multishine and Slippi / everything incl. the console key; what goes and what stays, A continues, B cancels
 *   HOLD    "Hold Z for 1 second to clear." with a bar (a NEW press of Z; letting go early does nothing; B steps back)
 *   SERVER  DEVICE_FORGET 0x1A in a device session (linked consoles only); OK / AUTH_FAILED / TOKEN_REVOKED all mean the server knows
 *   CARD    the MultishineOnline record keeps its server setup, its identity becomes the guest placeholder (all-zero device id and secret); read back by the writer
 *   SLIPPI  MultishineIdentity and MultishineBridge deleted (level 2 and 3, when the card has them)
 *   KEY     level 3: the stored entropy seed (the console key) of MultishineOptions replaced by an empty record (never shown, never logged)
 *   DONE    "Cleared. This console is now a guest." (or the server could not be told: remove it on the website) and one line per further step
 */
#include "sg_ui.h"

/* What a Delete level keeps: ONE place for these three texts, mirrored by the keys st.kept_1.. st.kept_3 of docs/data/ui_strings.json.
   TODO(remove with the save-hook change): the "Melee save" clause stops being true when the loader payload lives in its own save file (system-files builder plan); remove ONLY that clause, in the SAME change that lands the new save behaviour. */
#define SGD_KEPT_1 "Kept: Slippi login, console key, Melee save."
#define SGD_KEPT_2 "Kept: console key, Melee save and settings."
#define SGD_KEPT_3 "Kept: your Melee save, settings and codes."
#define SGD_NOT_DELETED "Your account is not deleted."

static void put(struct sg_view *v, unsigned r, unsigned attr, const char *s)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (s[c] && c < SG_COLS) { v->t[r][c] = s[c]; c++; }
    if (s[c]) sg_cut_count++;
    v->t[r][c] = 0;
    v->attr[r] = (unsigned char)attr;
}
void sgd_enter_level(struct sg *g, unsigned level)
{
    struct sgd *d;
    sgd_enter(g);
    d = &g->del;
    d->from_ident = 1;
    d->level = (unsigned char)(level < 1u ? 1u : level > 3u ? 3u : level);
    d->also_slippi = (unsigned char)(d->level >= 2u && d->have_slippi);
    g->scr = SG_DELID;
    d->st = SGD_HOLD;                     /* the level was chosen on the Device Identity page: straight to "Hold Z" */
    d->hold_t0 = 0; d->held_ms = 0;
}

static void leave(struct sg *g)           /* back to where the level was chosen: the Device Identity page on the row of that level, or the Online page */
{
    unsigned lvl = g->del.level, id = g->del.from_ident;
    g->del.st = SGD_OFF;
    if (id == 2u) { sg_goto(g, SG_HUB); g->cur = (unsigned char)(SG_ON_DEL + lvl - 1u); if (g->del_acct) g->onp = 1; }        /* chosen on the Delete page itself: back on that row (the Account page's Delete page stays one) */
    else if (id) { sg_goto(g, SG_IDENT); g->cur = (unsigned char)lvl; }
    else { sg_goto(g, SG_HUB); g->cur = SG_ON_IDENT; }
}

void sgd_enter(struct sg *g)
{
    struct sgd *d = &g->del;
    unsigned char k;
    for (k = 0; k < (unsigned char)sizeof *d; k++) ((unsigned char *)d)[k] = 0;
    d->have_slippi = (unsigned char)(g->id_slippi == 2u);
    d->was_linked = (unsigned char)(g->linked == SG_ID_LINKED || g->linked == SG_ID_RAMONLY);
    d->level = 1;
    d->st = SGD_ASK;                      /* the console key can always go, so there is always something to ask */
}

/* the order of the steps: server (linked consoles), the card record (linked), the Slippi login (level 2 and 3, when the card has one), the console key (level 3); steps that do not apply are skipped */
static void go(struct sgd *d, unsigned s)
{
    if (s == 0u && !d->was_linked) s = 1u;
    if (s == 1u && !d->was_linked) s = 2u;
    if (s == 2u && !(d->level >= 2u && d->have_slippi)) s = 3u;
    if (s == 3u && d->level < 3u) s = 4u;
    switch (s) {
    case 0u: d->st = SGD_SERVER; d->req = SGD_R_FORGET; break;
    case 1u: d->st = SGD_CARD; d->req = SGD_R_CARD; break;
    case 2u: d->st = SGD_SLIPPI; d->req = SGD_R_SLIPPI; break;
    case 3u: d->st = SGD_KEY; d->req = SGD_R_KEY; break;
    default: d->st = SGD_DONE; d->req = SGD_R_NONE; break;
    }
}

void sgd_server(struct sgd *d, int ok)
{
    if (d->st != SGD_SERVER) return;
    d->server_ok = (unsigned char)(ok ? 1 : 0);
    go(d, 1u);
}
void sgd_card(struct sgd *d, int ok)
{
    if (d->st != SGD_CARD) return;
    d->card_ok = (unsigned char)(ok ? 1 : 0);
    go(d, 2u);
}
void sgd_slippi(struct sgd *d, int ok)
{
    if (d->st != SGD_SLIPPI) return;
    d->slippi_ok = (unsigned char)(ok ? 1 : 0);
    go(d, 3u);
}
void sgd_key(struct sgd *d, int ok)
{
    if (d->st != SGD_KEY) return;
    d->key_ok = (unsigned char)(ok ? 1 : 0);
    go(d, 4u);
}

/* edge: newly pressed buttons; held: buttons held now; the clock is g->lk.now (ms) */
void sgd_frame(struct sg *g, unsigned edge, unsigned held)
{
    struct sgd *d = &g->del;
    switch (d->st) {
    case SGD_NOTHING:
        if (edge & (SG_B_A | SG_B_B)) leave(g);
        break;
    case SGD_ASK:
        if (edge & SG_B_DOWN) d->level = (unsigned char)(d->level >= 3u ? 1u : d->level + 1u);
        else if (edge & SG_B_UP) d->level = (unsigned char)(d->level <= 1u ? 3u : d->level - 1u);
        d->also_slippi = (unsigned char)(d->level >= 2u && d->have_slippi);
        if (edge & SG_B_A) { d->st = SGD_HOLD; d->hold_t0 = 0; d->held_ms = 0; }
        else if (edge & SG_B_B) leave(g);
        break;
    case SGD_HOLD:
        if (edge & SG_B_B) { if (d->from_ident) leave(g); else d->st = SGD_ASK; break; }
        if (edge & SG_B_Z) { d->hold_t0 = g->lk.now ? g->lk.now : 1u; d->held_ms = 0; }          /* a NEW press of Z starts the hold (the A of the first step does not count) */
        else if ((held & SG_B_Z) && d->hold_t0) {
            d->held_ms = g->lk.now - d->hold_t0;
            if (d->held_ms >= SGD_HOLD_MS) go(d, 0u);
        } else { d->hold_t0 = 0; d->held_ms = 0; }                          /* let go early: nothing happens */
        break;
    case SGD_DONE:
        if (edge & (SG_B_A | SG_B_B)) { unsigned id = d->from_ident; d->st = SGD_OFF; if (id == 2u && g->del_acct) { g->del_acct = 0; sg_goto(g, SG_IDENT); g->cur = (unsigned char)sg_acct_pos(g, SG_AI_DELETE); } else if (id == 2u) { sg_goto(g, SG_HUB); g->cur = SG_ON_IDENT; } else if (id) { sg_goto(g, SG_IDENT); g->cur = 0; } else { sg_goto(g, SG_HUB); g->cur = SG_ON_IDENT; } }
        break;
    default:
        break;
    }
}

void sgd_draw(const struct sg *g, struct sg_view *v)
{
    const struct sgd *d = &g->del;
    sg_head(v, "Delete identity", "More - Settings - Delete - Delete identity");
    switch (d->st) {
    case SGD_NOTHING:
        put(v, 4, SG_A_BODY, "Nothing to clear.");
        put(v, SG_ROWS - 2, SG_A_DIM, "B: Back");
        break;
    case SGD_ASK: {
        static const char *const lab[3] = { "1  Multishine only", "2  Multishine and Slippi", "3  Everything, console key too" };
        unsigned i;
        for (i = 0; i < 3u; i++) {
            char t[SG_COLS + 1];
            unsigned n = 0, k;
            const char *p = lab[i];
            t[n++] = i + 1u == d->level ? '-' : ' '; t[n++] = ' ';
            for (k = 0; p[k] && n < SG_COLS; k++) t[n++] = p[k];
            t[n] = 0;
            put(v, 4 + i, i + 1u == d->level ? SG_A_HI : (i == 1u && !d->have_slippi ? SG_A_DIM : SG_A_BODY), t);
        }
        if (d->level == 1u) {
            put(v, 8, SG_A_BODY, d->was_linked ? "Name, code and link go. This console" : "This console is a guest already:");
            put(v, 9, SG_A_BODY, d->was_linked ? "becomes a guest." : "nothing of Multishine to clear.");
            put(v, 11, SG_A_DIM, SGD_KEPT_1);
        } else if (d->level == 2u) {
            put(v, 8, SG_A_BODY, d->was_linked ? "Name, code, link and the Slippi login go." : "The Slippi login goes.");
            put(v, 9, d->have_slippi ? SG_A_DIM : SG_A_BAD, d->have_slippi ? "Getting it back means baking the card again." : "There is no Slippi login on this card.");
            put(v, 11, SG_A_DIM, SGD_KEPT_2);
        } else {
            put(v, 8, SG_A_BODY, "Everything goes, the console key too.");
            put(v, 9, SG_A_DIM, "The first-run setup comes back at the next start.");
            put(v, 11, SG_A_DIM, SGD_KEPT_3);
        }
        put(v, 12, SG_A_DIM, SGD_NOT_DELETED);
        put(v, SG_ROWS - 2, SG_A_DIM, "A: Continue  B: Cancel");
        break;
    }
    case SGD_HOLD: {
        char t[SG_COLS + 1];
        unsigned i, fill = (unsigned)(d->held_ms >= SGD_HOLD_MS ? 24u : d->held_ms * 24u / SGD_HOLD_MS);
        put(v, 4, SG_A_BAD, "Hold Z for 1 second to clear.");
        for (i = 0; i < 24u; i++) t[i] = i < fill ? 'I' : '.';
        t[24] = 0;
        put(v, 6, SG_A_HI, t);
        put(v, 8, SG_A_DIM, d->level == 1u ? "Clearing: Multishine." : d->level == 2u ? "Clearing: Multishine and the Slippi login." : "Clearing: everything, the console key too.");
        put(v, SG_ROWS - 2, SG_A_DIM, "B: Cancel");
        break;
    }
    case SGD_SERVER: put(v, 4, SG_A_HI, "Telling the server..."); put(v, SG_ROWS - 2, SG_A_DIM, "Working..."); break;
    case SGD_CARD: case SGD_SLIPPI: case SGD_KEY: put(v, 4, SG_A_HI, "Clearing on the memory card..."); put(v, 6, SG_A_BAD, "Do not remove the memory card."); put(v, SG_ROWS - 2, SG_A_DIM, "Working..."); break;
    case SGD_DONE:
        if (!d->was_linked) put(v, 4, SG_A_DIM, "Multishine: nothing to clear, already a guest.");
        else if (d->server_ok && d->card_ok) put(v, 4, SG_A_GOOD, "Cleared. This console is now a guest.");
        else if (d->card_ok) { put(v, 4, SG_A_GOOD, "Cleared on this card. The server could not"); put(v, 5, SG_A_GOOD, "be told: remove it on multishine.link, Consoles."); }
        else { put(v, 4, SG_A_BAD, "This console is a guest until power off, but"); put(v, 5, SG_A_BAD, "the memory card could not be changed."); }
        if (d->level >= 2u && d->have_slippi) put(v, 7, d->slippi_ok ? SG_A_BODY : SG_A_BAD, d->slippi_ok ? "The Slippi login is gone." : "The Slippi login could not be cleared.");
        if (d->level >= 3u) put(v, 8, d->key_ok ? SG_A_BODY : SG_A_BAD, d->key_ok ? "The console key is gone. First-run setup returns." : "The console key could not be cleared.");
        put(v, SG_ROWS - 2, SG_A_DIM, "A: Done");
        break;
    default:
        break;
    }
}

/* The Account page (Device identity merged into it; was Device Identity 3.1): the identity read only at the top (from the resident config via the glue, never the device
   secret, never the Slippi play key), then the choosable rows (sg_acct_items): Link this console (only when not linked), Region, Send Slippi login, Delete (opens the three levels). */
void sg_ident_draw(const struct sg *g, struct sg_view *v)
{
    static const char *const rgn_v[SG_RGN_NA + 1] = { "Loading...", "North America East", "North America West", "Europe", "Latin America", "Asia", "Oceania", "North America Central", "Not set", "Not available" };
    static const char *const lab[4] = { "Link this console", "Region", "Send Slippi login", "Delete" };
    static const char *const desc[4] = { "Use this console with your account.", "Where you play from. Helps find matches.", "Get your Slippi login from your phone.", "Remove the identity from this console." };
    unsigned char it[4];
    unsigned n = sg_acct_items(g, it), k, r;
    int linked = g->linked == SG_ID_LINKED || g->linked == SG_ID_RAMONLY;
    sg_head(v, "Account", "");
    /* rows two grid rows apart (the list font is taller than one grid row); the device id and the last sync are lab details and are not shown */
    put(v, 4, SG_A_BODY, "Status");
    sg_putv(v, 4, linked ? SG_A_GOOD : SG_A_BODY, g->linked == SG_ID_LINKED ? "Linked" : g->linked == SG_ID_RAMONLY ? "Not saved, until power off" : g->linked == SG_ID_GUEST ? "Guest" : "...");
    r = 6;
    if (linked) {
        put(v, r, SG_A_BODY, "Name");  sg_putv(v, r, SG_A_BODY, g->id_name); r += 2;
        put(v, r, SG_A_BODY, "Connect code");  sg_putv(v, r, SG_A_BODY, g->id_code); r += 2;
    }
    put(v, r, SG_A_BODY, "Slippi login");
    sg_putv(v, r, SG_A_BODY, g->id_slippi == 2u ? "On this card" : g->id_slippi == 1u ? "None" : "...");
    r = 12;
    for (k = 0; k < n; k++) {
        unsigned row = r + 2u * k, sel = k == g->cur, attr = (sel ? SG_A_HI : SG_A_BODY) | SG_A_BIG;
        if (it[k] == SG_AI_REGION && g->linked == SG_ID_GUEST && !sel) attr = SG_A_DIM | SG_A_BIG;
        if (sel) sg_bar(v, row, 1);
        put(v, row, attr, lab[it[k]]);
        if (it[k] == SG_AI_REGION && g->linked != SG_ID_GUEST) sg_putv(v, row, attr, rgn_v[g->rgn <= SG_RGN_NA ? g->rgn : 0u]);
    }
    k = g->cur < n ? it[g->cur] : SG_AI_REGION;
    if (g->hint) put(v, 21, SG_A_BAD, g->hint);
    put(v, 20, SG_A_DIM, k == SG_AI_REGION && g->linked == SG_ID_GUEST ? "Link this console first." : desc[k]);
    put(v, SG_ROWS - 2, SG_A_DIM, k == SG_AI_REGION ? "A: Change  B: Back" : "A: Select  B: Back");
}
