/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Console Friends screen, the model (frd.h).  Strict C89, no libc, no heap, no Melee address, no network. */
#include "frd.h"
#include "sg_ui.h"
#include "../ms_fmt.h"

/* ---- small string helpers ---- */
static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static char *cpy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = s[i]; i++; } d[i] = 0; return d + i; }
static char *cat(char *d, const char *s, unsigned max) { unsigned n = slen(d); return cpy(d + n, s, max > n ? max - n : 0u); }
static void catu(char *d, unsigned v, unsigned max)
{
    char r[12];
    r[ms_fmt_u(r, v, 11u)] = 0;
    cat(d, r, max);
}
static int same(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void zero(void *p, unsigned n) { unsigned char *b = (unsigned char *)p; while (n--) *b++ = 0; }
/* the menu font plus the punctuation the glue expands to full-width (ml_sis_expand / sis_print); quote, backslash and backtick never reach the screen */
static int drawable(char c)
{
    static const char extra[] = " .,'-:!?;()[]+/%#&*@$=<>~|_^{}";
    unsigned i;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) return 1;
    for (i = 0; extra[i]; i++) if (extra[i] == c) return 1;
    return 0;
}
static void safe_cpy(char *d, const char *s, unsigned max) { unsigned i = 0; while (s[i] && i < max) { d[i] = drawable(s[i]) ? s[i] : ' '; i++; } d[i] = 0; }
static int has_room(const struct frd_ent *e) { return (e->flags & 4u) && e->room[0]; }                        /* a room to join, also while they play a set in it */
static int in_room(const struct frd_ent *e) { return has_room(e) && e->presence != 4u; }
const char *frd_game_name(unsigned mode) { return mode == 0u ? "Grandmaster" : mode == 1u ? "Dojo" : mode == 2u ? "Mano a mano" : ""; }
static int in_game(const struct frd_ent *e) { return (e->flags & 32u) && frd_game_name(e->gmode)[0] && e->presence == 4u; }       /* flag 32 = the server's game tail */
unsigned frd_watchable(const struct frd_ent *e) { return in_game(e) && (e->gflags & 1u); }
/* ---- example data (host tests; no server): invented names, Multishine codes (TAG@n) with a digit tag so they can never be real (player tags are letters only);
   the Slippi codes are what a friend typed (TAG#n) ---- */
static const struct frd_ent EX[6] = {
    { "PX1@11", "Wobble Pilot", 2, 0, 0, "WOB#123", "Wobbles", "", 0, 0, 0 },
    { "PX2@22", "Pocket Fox", 4, 0, 0, "", "", "", 0, 0, 0 },
    { "PX3@33", "Lylat Lou", 3, 0, 0, "LOU#7", "Lou", "", 0, 0, 0 },
    { "PX4@44", "Practice Bot", 1, 1, 0, "", "", "", 0, 0, 0 },
    { "PX5@55", "Tipper Tom", 0, 0, 3, "", "", "", 0, 0, 0 },
    { "PX6@66", "Cliff Hanger", 0, 0, 255, "", "", "", 0, 0, 0 },
};
static const struct frd_rq EXR[1] = { { "PX7@77", "Ledge Lily" } };
static const struct frd_rc EXC[2] = { { "PX8@88", "Edge Guard" }, { "PX9@99", "Ledge Dash" } };

static void refresh(struct frd *f)
{
    if (!f->live) return;
    f->req = FRD_REQ_LIST; f->lpage = 0; f->timer = 0; f->itimer = 0;
}

void frd_enter(struct frd *f, int live)
{
    unsigned char gfx = f->gfx, tried = f->gfx_tried;
    unsigned i;
    zero(f, sizeof *f);
    f->gfx = gfx; f->gfx_tried = tried;
    f->live = (unsigned char)(live != 0);
    f->scr = FRD_LIST;
    f->dirty = FRD_D_ALL;
    f->prev = 0xFFFFu;                         /* a button already down at entry counts only after a release (the B hold that left a room joined from here closed this list at once) */
    if (live) { f->net = FRD_N_LOADING; refresh(f); return; }
    for (i = 0; i < 6u; i++) f->f[i] = EX[i];
    f->nf = 6; f->total = 6;
    f->r[0] = EXR[0]; f->nrq = 1;
    f->rc[0] = EXC[0]; f->rc[1] = EXC[1]; f->nrc = 2;
    f->net = FRD_N_OK;
}

void frd_set_texts(struct frd *f, const char (*t)[FRD_TXT], unsigned n)
{
    unsigned i;
    f->ntxt = 0;
    for (i = 0; i < FRD_NTXT; i++) {                                           /* the slots stay IN PLACE: the quick-chat picker addresses them by group and position */
        f->txt[i][0] = 0;
        if (i >= n || !t[i][0] || (t[i][0] == '.' && !t[i][1])) continue;     /* an empty slot (mnc stores "." for none) */
        safe_cpy(f->txt[i], t[i], FRD_TXT - 1u);
        f->ntxt++;
    }
}

/* ---- rows ---- */
/* friend rows: ONE per person.  A friend's Slippi code and name come on the same entry, so a friend who typed a Slippi code is the same person and gets no second row:
   the Slippi side shows in the panel and as the action "Challenge on Slippi" (the older separate Slippi row, kind 3, is gone) */
static unsigned friend_rows(const struct frd *f) { return f->nf; }
/* the screen whose ROWS are drawn: while a challenge waits for its answer or a request runs the list stays on screen with the cursor where it was (nothing is
   blanked, only the description box and the footer show the waiting state); every other screen draws its own rows */
static unsigned vscr(const struct frd *f) { return (f->scr == FRD_WAIT || f->scr == FRD_BUSY) ? (unsigned)FRD_LIST : (unsigned)f->scr; }
unsigned frd_rows(const struct frd *f)
{
    switch (vscr(f)) {
    case FRD_LIST: return f->net == FRD_N_OK ? (unsigned)f->nrq + f->nin + friend_rows(f) + (f->nrc ? 1u + f->nrc : 0u) : 0u;
    case FRD_ACT: return FRD_AR_N;                                              /* the action list of a recent player (a friend has none any more:, the list itself acts) */
    case FRD_CODE_ENTRY: return 5;                                              /* the code (cursor row) and four help lines: the table is the form */
    default: return 0;
    }
}
unsigned frd_max_first(const struct frd *f) { unsigned n = frd_rows(f); return n > FRD_VIS ? n - FRD_VIS : 0u; }
unsigned frd_cur_first(const struct frd *f) { return vscr(f) == FRD_ACT ? 0u : f->first; }
unsigned frd_cur_page(const struct frd *f) { return vscr(f) == FRD_ACT ? f->a_cur : vscr(f) == FRD_CODE_ENTRY ? 0u : f->page; }

unsigned frd_row_kind(const struct frd *f, unsigned row, unsigned *idx)
{
    if (row < f->nrq) { *idx = row; return 1; }
    if (row < (unsigned)f->nrq + f->nin) { *idx = row - f->nrq; return 6; }                 /* the Invites: right below the requests */
    {
        unsigned r = row - f->nrq - f->nin, i;
        for (i = 0; i < f->nf; i++) {
            if (r == 0u) { *idx = i; return 2; }
            r--;
        }
        if (f->nrc && r < 1u + f->nrc) {                                       /* below the friends: the separator, then the recent players */
            if (r == 0u) { *idx = 0; return 4; }
            *idx = r - 1u; return 5;
        }
        *idx = r + f->nf;                    /* past the cached friends (not shown) */
        return 2;
    }
}
static unsigned cur_row(const struct frd *f) { return (unsigned)f->first + f->page; }

static int step(struct frd *f, int down)                                      /* one row up or down in the current list; 0 = at the end */
{
    unsigned n = frd_rows(f);
    unsigned char *first = &f->first, *page = f->scr == FRD_ACT ? &f->a_cur : &f->page;
    unsigned at = (unsigned)(f->scr == FRD_ACT ? 0u : *first) + *page;
    if (!n) return 0;
    if (down) {
        if (at + 1u >= n) return 0;
        if (f->scr != FRD_ACT && *page + 1u >= FRD_VIS) (*first)++; else (*page)++;
    } else {
        if (at == 0u) return 0;
        if (f->scr != FRD_ACT && *page == 0u) (*first)--; else (*page)--;
    }
    return 1;
}
/* the cursor never rests on the "Recent players" separator (it is a heading): step past it in the direction of travel, or the other way when there is nothing that way */
static void sep_skip(struct frd *f, int down)
{
    unsigned idx;
    if (f->scr != FRD_LIST || !frd_rows(f) || cur_row(f) >= frd_rows(f) || frd_row_kind(f, cur_row(f), &idx) != 4u) return;
    if (!step(f, down)) (void)step(f, !down);
}

static void clamp(struct frd *f)
{
    unsigned n = frd_rows(f), mf = frd_max_first(f);
    if (f->first > mf) f->first = (unsigned char)mf;
    if (n && cur_row(f) >= n) f->page = (unsigned char)(n - 1u - f->first);
    if (!n) { f->first = 0; f->page = 0; }
    sep_skip(f, 1);
}

static void move(struct frd *f, int down)
{
    if (!frd_rows(f)) return;
    if (!step(f, down)) return;
    sep_skip(f, down);
    f->a_confirm = 0; f->rm = 0;
    f->msg = 0; f->toast_t = 0; f->jump_code[0] = 0;
    f->sfx = FRD_SFX_MOVE; f->dirty = FRD_D_ALL;
}
static void page9(struct frd *f, int down)
{
    unsigned mf = frd_max_first(f), v = f->first;
    if (f->scr != FRD_LIST) return;
    v = down ? (v + FRD_VIS > mf ? mf : v + FRD_VIS) : (v > FRD_VIS ? v - FRD_VIS : 0u);
    if (v == f->first) return;
    f->first = (unsigned char)v; clamp(f);
    f->sfx = FRD_SFX_MOVE; f->dirty = FRD_D_ALL;
}

static void say(struct frd *f, const char *m, int warn) { f->msg = m; f->msg_warn = (unsigned char)warn; f->dirty = FRD_D_ALL; }
void frd_say(struct frd *f, const char *m, int warn) { say(f, m, warn); }
static unsigned ceq(const char *a, const char *b)                                   /* the same connect code, letters in any case */
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y) return 0;
    }
    return *a == *b;
}
static void toast(struct frd *f, const char *a, const char *b, const char *c, int warn)       /* the Add-a-friend result: a toast (a + b + c), not a line in the description box */
{
    char *e = cpy(f->toast, a, FRD_LINE);
    if (b) e = cpy(e, b, FRD_LINE - (unsigned)(e - f->toast));
    if (c) cpy(e, c, FRD_LINE - (unsigned)(e - f->toast));
    f->toast_warn = (unsigned char)warn; f->toast_t = FRD_TOAST_FRAMES; f->msg = 0; f->dirty = FRD_D_ALL;
    if (warn) f->sfx = FRD_SFX_DENY;
}
unsigned frd_toast(const struct frd *f, char *out)
{
    cpy(out, f->toast_t ? f->toast : "", FRD_LINE);
    return f->toast_t ? 1u + f->toast_warn : 0u;
}
static unsigned find_friend(const struct frd *f, const char *code)                  /* 1 + index in f[] of the friend with this code, 0 = none */
{
    unsigned i;
    for (i = 0; i < f->nf; i++) if (ceq(f->f[i].code, code)) return i + 1u;
    return 0u;
}
static void jump_to(struct frd *f, unsigned idx)                                    /* the cursor on friend idx (a row of the list) */
{
    unsigned row = (unsigned)f->nrq + f->nin + idx, mf = frd_max_first(f);
    if (row < f->first || row >= (unsigned)f->first + FRD_VIS) f->first = (unsigned char)(row > 3u ? row - 3u : 0u);
    if (f->first > mf) f->first = (unsigned char)mf;
    f->page = (unsigned char)(row - f->first);
    f->dirty = FRD_D_ALL;
}
static void jump_apply(struct frd *f)                                               /* after a refresh: the pending jump, once the list holds that player */
{
    unsigned k;
    if (!f->jump_code[0] || f->scr != FRD_LIST) return;
    k = find_friend(f, f->jump_code);
    if (k) {
        jump_to(f, k - 1u); f->jump_code[0] = 0;
        if (f->jump_name && f->toast_t) toast(f, f->f[k - 1u].name, " is already on your list", 0, 0);
        f->jump_name = 0;
    }
}
void frd_handoff(struct frd *f) { if (f->play_after) { f->play = f->play_after; f->play_after = 0; } }
static void busy(struct frd *f, unsigned what, unsigned back)
{
    f->req = (unsigned char)what; f->busy_what = (unsigned char)what; f->back_to = (unsigned char)back;
    f->scr = FRD_BUSY; f->dirty = FRD_D_ALL; f->msg = 0; f->wait = 0;
}

static void drop_req(struct frd *f) { f->req = FRD_REQ_NONE; f->abort = 1; f->msg_busy = 0; f->inv_chk = 0; }   /* the glue stops what it runs; nothing is left pending */
static void busy_frame(struct frd *f, unsigned edge)
{
    if (edge & FRD_B_B) { drop_req(f); f->scr = f->back_to; say(f, "Cancelled", 0); f->sfx = FRD_SFX_BACK; f->dirty = FRD_D_ALL; return; }
    if (++f->wait >= FRD_BUSY_FRAMES) { drop_req(f); frd_net_fail(f, f->busy_what, FRD_W_TIMEOUT); }
}

/* ---- the connect code entry (the Game Night "Join by code" picker, extended to the connect-code alphabet) ----
   '@' = a Multishine code (TAG@n, the default since), '#' = a Slippi-style code (older lab identities); up from Z gives '@' first */
static const char CODE_SET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ@#0123456789";
static unsigned set_index(char c) { unsigned i; for (i = 0; CODE_SET[i]; i++) if (CODE_SET[i] == c) return i; return 0; }
static int code_ok(const char *c)
{
    unsigned i = 0, tag = 0, hash = 0, dig = 0;
    while (c[i] && c[i] != '#' && c[i] != '@') { if (c[i] < 'A' || c[i] > 'Z') return 0; tag++; i++; }
    if (c[i] == '#' || c[i] == '@') { hash = 1; i++; }
    while (c[i]) { if (c[i] < '0' || c[i] > '9') return 0; dig++; i++; }
    return tag >= 1u && tag <= 7u && hash && dig >= 1u && i <= FRD_CODE;
}
static void code_frame(struct frd *f, unsigned edge, int up, int down)
{
    unsigned n = slen(f->cbuf), k;
    if (edge & FRD_B_B) {
        if (n > 1u) { f->cbuf[n - 1u] = 0; if (f->cpos >= n - 1u) f->cpos = (unsigned char)(n - 2u); f->sfx = FRD_SFX_BACK; f->dirty = FRD_D_ALL; return; }   /* B deletes the last character, then leaves */
        f->scr = FRD_LIST; f->sfx = FRD_SFX_BACK; f->msg = 0; f->dirty = FRD_D_ALL; return;
    }
    if (up || down) {
        unsigned m = sizeof CODE_SET - 1u;
        k = set_index(f->cbuf[f->cpos]);
        k = up ? (k + 1u) % m : (k + m - 1u) % m;
        f->cbuf[f->cpos] = CODE_SET[k];
        f->sfx = FRD_SFX_MOVE; f->msg = 0; f->dirty = FRD_D_ALL;
    }
    if ((edge & FRD_B_RIGHT) && f->cpos + 1u < FRD_CODE) {
        if (f->cpos + 1u >= n) { f->cbuf[n] = f->cbuf[n - 1u] >= '0' && f->cbuf[n - 1u] <= '9' ? '0' : (f->cbuf[n - 1u] == '#' || f->cbuf[n - 1u] == '@') ? '1' : 'A'; f->cbuf[n + 1u] = 0; }
        f->cpos++; f->sfx = FRD_SFX_MOVE; f->dirty = FRD_D_ALL;
    }
    if ((edge & FRD_B_LEFT) && f->cpos) { f->cpos--; f->sfx = FRD_SFX_MOVE; f->dirty = FRD_D_ALL; }
    if (edge & (FRD_B_A | FRD_B_START)) {
        if (!code_ok(f->cbuf)) { say(f, "A connect code is letters, then @ or #, then digits", 1); f->sfx = FRD_SFX_DENY; return; }
        cpy(f->arg_code, f->cbuf, FRD_CODE);
        f->sfx = FRD_SFX_FWD;
        if (f->live) busy(f, FRD_REQ_ADD, FRD_LIST);
        else { f->scr = FRD_LIST; toast(f, "Friend request sent to ", f->arg_code, 0, 0); }
    }
}

void frd_add_code(struct frd *f, const char *code)
{
    unsigned i;
    for (i = 0; i < FRD_CODE && code[i]; i++) { char ch = code[i]; if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32); f->cbuf[i] = ch; }
    f->cbuf[i] = 0;
    f->scr = FRD_LIST;
    if (!code_ok(f->cbuf)) { say(f, "A connect code is letters, then @ or #, then digits", 1); f->sfx = FRD_SFX_DENY; return; }
    cpy(f->arg_code, f->cbuf, FRD_CODE);
    if (f->live) busy(f, FRD_REQ_ADD, FRD_LIST); else toast(f, "Friend request sent to ", f->arg_code, 0, 0);
}

/* ---- the list ----
 no action page.  The list itself acts on the selected row; the panel and the footer say what the keys do:
   a friend: A = the obvious action (Join room in a Game Night room, else Watch a watchable match, else Mano a mano when they are online; nothing when offline), Y = Slippi challenge (only with a Slippi code),
   X = Remove or block (the panel asks: A Remove, Y Block, B Cancel); a request: A Accept, X Decline; a recent player: A opens its action list; L R page; Z adds a friend on every row. */
static void join_room(struct frd *f, unsigned idx)                              /* A on a Room row: join that room (the glue closes this screen and opens the Game Night lobby) */
{
    if (!f->live) { say(f, "Example data: no room to join", 1); f->sfx = FRD_SFX_DENY; return; }
    cpy(f->join_code, f->f[idx].room, 4); f->join_rf = f->f[idx].rflags; f->join = 1; f->exit = 1; f->sfx = FRD_SFX_FWD;
}
/* the A action of a friend: FRD_FA_* */
enum { FRD_FA_NONE, FRD_FA_JOIN, FRD_FA_WATCH, FRD_FA_MANO };
static unsigned fa_of(const struct frd_ent *e)
{
    if (has_room(e)) return FRD_FA_JOIN;
    if (frd_watchable(e)) return FRD_FA_WATCH;
    if (e->presence == 1u || e->presence == 2u) return FRD_FA_MANO;            /* online, or ready in the menus */
    return FRD_FA_NONE;
}
static void rm_frame(struct frd *f, unsigned edge)                              /* the Remove / Block question in the panel */
{
    if (edge & FRD_B_B) { f->rm = 0; f->sfx = FRD_SFX_BACK; f->dirty = FRD_D_ALL; return; }
    if (!(edge & (FRD_B_A | FRD_B_Y))) return;
    cpy(f->arg_code, f->f[f->sel].code, 8);
    f->sfx = FRD_SFX_FWD; f->rm = 0;
    if (f->live) busy(f, (edge & FRD_B_A) ? FRD_REQ_REMOVE : FRD_REQ_BLOCK, FRD_LIST);
    else { unsigned i; for (i = (unsigned)f->sel + 1u; i < f->nf; i++) f->f[i - 1u] = f->f[i]; f->nf--; f->total--; say(f, (edge & FRD_B_A) ? "Friend removed" : "Blocked", 0); clamp(f); }
}
/* an invite row (the missed 10 s toast stays here for 5 minutes): A accepts, X declines (a room invite: dismisses).  A Mano a mano accept first asks the glue whether this console has the
   Multishine system files (inv_chk -> frd_inv_checked: the server is told "accepted" only for a match the console can enter); a room invite goes to the server at once and then opens the room. */
static void inv_key(struct frd *f, unsigned idx, unsigned edge)
{
    if (!(edge & (FRD_B_A | FRD_B_X)) || idx >= f->nin || f->req != FRD_REQ_NONE || f->inv_chk) return;
    f->arg_id = f->in[idx].id; f->arg_type = f->in[idx].type; cpy(f->arg_code, f->in[idx].code, 8); f->sel = 0;
    f->arg_yes = (unsigned char)((edge & FRD_B_A) != 0u);
    f->msg = 0; f->sfx = f->arg_yes ? FRD_SFX_FWD : FRD_SFX_BACK; f->dirty = FRD_D_ALL;
    if (!f->live) {                                                            /* example data: the row goes, nothing else happens */
        unsigned i;
        for (i = idx + 1u; i < f->nin; i++) f->in[i - 1u] = f->in[i];
        f->nin--; say(f, f->arg_yes ? "Example data: nothing to join" : "Invite dismissed", f->arg_yes ? 1 : 0); clamp(f);
        return;
    }
    if (f->arg_yes && f->arg_type == 1u) { f->inv_chk = 1; return; }
    busy(f, FRD_REQ_INVRESP, FRD_LIST);
}
void frd_inv_checked(struct frd *f, int missing)
{
    if (!f->inv_chk) return;
    f->inv_chk = 0;
    if (missing) { say(f, "Multishine system files not set up: check network", 1); f->sfx = FRD_SFX_DENY; return; }
    busy(f, FRD_REQ_INVRESP, FRD_LIST);
}
static void list_frame(struct frd *f, unsigned edge)
{
    unsigned idx, k;
    if (edge & FRD_B_B && !f->rm) { f->exit = 1; f->sfx = FRD_SFX_BACK; return; }
    if (edge & FRD_B_START) { if (f->live) { refresh(f); say(f, "Refreshing", 0); } return; }
    if (f->net != FRD_N_OK) {
        if (edge & (FRD_B_A | FRD_B_Z)) { if (f->live) { refresh(f); f->net = FRD_N_LOADING; f->dirty = FRD_D_ALL; } else f->sfx = FRD_SFX_DENY; }
        return;
    }
    if (f->rm) { rm_frame(f, edge); return; }
    if (edge & FRD_B_Z) {                                                       /* Z: Add a friend, on every row */
        if (f->kbd && f->live) { f->play_after = 5; f->exit = 1; f->sfx = FRD_SFX_FWD; return; }      /*: Slippi's code keyboard, as in Mano a mano (the glue hands off) */
        f->scr = FRD_CODE_ENTRY; f->cbuf[0] = 'A'; f->cbuf[1] = 0; f->cpos = 0; f->msg = 0; f->sfx = FRD_SFX_FWD; f->dirty = FRD_D_ALL;
        return;
    }
    if (edge & FRD_B_R) { page9(f, 1); return; }
    if (edge & FRD_B_L) { page9(f, 0); return; }
    if (!frd_rows(f)) return;
    k = frd_row_kind(f, cur_row(f), &idx);
    if (k == 1u) {                                                             /* a request: A accepts, X declines */
        if (f->req != FRD_REQ_NONE || !(edge & (FRD_B_A | FRD_B_X))) return;
        cpy(f->arg_code, f->r[idx].code, 8);
        f->arg_yes = (unsigned char)((edge & FRD_B_X) == 0u);
        f->sfx = f->arg_yes ? FRD_SFX_FWD : FRD_SFX_BACK;
        if (f->live) busy(f, FRD_REQ_ANSWER, FRD_LIST);
        else { unsigned i; for (i = idx + 1u; i < f->nrq; i++) f->r[i - 1u] = f->r[i]; f->nrq--; say(f, f->arg_yes ? "Friend added" : "Request declined", 0); clamp(f); }
        return;
    }
    if (k == 6u) { inv_key(f, idx, edge); return; }                            /* an invite: A accepts, X declines or dismisses */
    if (k == 4u) return;                                                       /* the separator: the cursor never rests on it */
    if (k == 5u) {                                                             /* a recent player: A opens its action list */
        if (edge & FRD_B_A) { f->sel = (unsigned char)idx; f->a_rec = 1; f->scr = FRD_ACT; f->a_cur = 0; f->a_confirm = 0; f->msg = 0; f->sfx = FRD_SFX_FWD; f->dirty = FRD_D_ALL; }
        return;
    }
    {
        const struct frd_ent *e = &f->f[idx];
        f->sel = (unsigned char)idx;
        if (edge & FRD_B_X) { f->rm = 1; f->msg = 0; f->sfx = FRD_SFX_MOVE; f->dirty = FRD_D_ALL; return; }
        if (edge & FRD_B_Y) {                                                  /* Slippi challenge: Slippi's Direct character screen with their code, searching (the glue arms it, play 3); the Event screen closes first (exit), the hand-off follows (frd_handoff) */
            if (!e->scode[0]) { say(f, "No Slippi code saved for them", 1); f->sfx = FRD_SFX_DENY; return; }   /* a Slippi challenge never reaches the friend: it searches their code from THIS console */
            cpy(f->play_code, e->scode, 8); f->play_after = 3; f->exit = 1; f->sfx = FRD_SFX_FWD;
            return;
        }
        if (!(edge & FRD_B_A)) return;
        switch (fa_of(e)) {
        case FRD_FA_JOIN: join_room(f, idx); return;
        case FRD_FA_WATCH:
            f->sfx = FRD_SFX_FWD;
            if (!f->live) { say(f, "Example data: nothing to watch", 1); f->sfx = FRD_SFX_DENY; return; }
            /* ask the server BEFORE leaving: the friends page is fetched again and
               the friend must still be watchable in the fresh answer (the server offers Watch only with a stream to start from); only then the Event screen closes (exit) and the hand-off follows (frd_handoff, play 4) */
            if (f->req != FRD_REQ_NONE) { say(f, "Try again in a moment", 1); f->sfx = FRD_SFX_DENY; return; }
            cpy(f->play_code, e->code, 8); f->wchk = 1; refresh(f); say(f, "Checking", 0);
            return;
        case FRD_FA_MANO:
            cpy(f->arg_code, e->code, 8);
            f->sfx = FRD_SFX_FWD;
            if (f->live) busy(f, FRD_REQ_INVITE, FRD_LIST);
            else { f->scr = FRD_WAIT; f->wait = 0; f->timer = 0; f->dirty = FRD_D_ALL; }
            return;
        default: f->sfx = FRD_SFX_DENY; return;   /* offline friend: the detail box already says Offline (the extra text overlapped Connect code); only the deny sound */
        }
    }
}

/* the action list of a recent player: Add Friend (the ordinary request), Remove From Recent (my entry only), Block (confirmed by a second A), Back */
static void rc_drop(struct frd *f, unsigned idx)
{
    unsigned i;
    for (i = idx + 1u; i < f->nrc; i++) f->rc[i - 1u] = f->rc[i];
    if (f->nrc) f->nrc--;
}
static void act_rec_frame(struct frd *f, unsigned edge)
{
    if (edge & FRD_B_B) { f->scr = FRD_LIST; f->a_rec = 0; f->msg = 0; f->sfx = FRD_SFX_BACK; f->dirty = FRD_D_ALL; return; }
    if (!(edge & (FRD_B_A | FRD_B_Z))) return;
    if (f->sel >= f->nrc) { f->scr = FRD_LIST; f->a_rec = 0; f->dirty = FRD_D_ALL; return; }
    cpy(f->arg_code, f->rc[f->sel].code, 8);
    switch (f->a_cur) {
    case FRD_AR_ADD:
        f->sfx = FRD_SFX_FWD;
        if (f->live) busy(f, FRD_REQ_ADD, FRD_LIST);
        else { f->scr = FRD_LIST; toast(f, "Friend request sent to ", f->arg_code, 0, 0); }
        return;
    case FRD_AR_REMOVE:
        f->sfx = FRD_SFX_FWD;
        if (f->live) busy(f, FRD_REQ_RMREC, FRD_LIST);
        else { rc_drop(f, f->sel); f->scr = FRD_LIST; say(f, "Removed from recent", 0); clamp(f); }
        return;
    case FRD_AR_BLOCK:
        if (!f->a_confirm) { f->a_confirm = 1; f->dirty = FRD_D_ALL; f->sfx = FRD_SFX_MOVE; return; }
        f->sfx = FRD_SFX_FWD;
        if (f->live) busy(f, FRD_REQ_BLOCK, FRD_LIST);
        else { rc_drop(f, f->sel); f->scr = FRD_LIST; say(f, "Blocked", 0); clamp(f); }
        return;
    default:
        f->scr = FRD_LIST; f->a_rec = 0; f->msg = 0; f->sfx = FRD_SFX_BACK; f->dirty = FRD_D_ALL;
        return;
    }
}

static void wait_frame(struct frd *f, unsigned edge)
{
    if (edge & FRD_B_B) { f->scr = FRD_LIST; f->msg = 0; f->sfx = FRD_SFX_BACK; f->dirty = FRD_D_ALL; return; }   /* stop waiting (the challenge ends by itself on the server) */
    if (f->play_after) return;                                                 /* accepted: the screen is closing */
    f->wait++;
    if (!f->live) { if (f->wait >= 90u) { f->scr = FRD_LIST; say(f, "Not answered", 1); } return; }
    if (f->wait >= FRD_WAIT_FRAMES) { f->scr = FRD_LIST; say(f, "Not answered", 1); f->sfx = FRD_SFX_DENY; return; }
    if (++f->timer >= FRD_POLL_FRAMES && f->req == FRD_REQ_NONE) { f->timer = 0; f->req = FRD_REQ_STATUS; }
}

/* ---- quick chat: the D-Pad on the list picks a text for the friend under the cursor; Slippi's two presses (qc_pick.h) ---- */
static const struct frd_ent *qc_target(const struct frd *f)               /* the friend under the cursor, 0 on any other row */
{
    unsigned idx;
    if (f->scr != FRD_LIST || f->net != FRD_N_OK || f->rm || !frd_rows(f)) return 0;
    if (frd_row_kind(f, (unsigned)f->first + f->page, &idx) != 2u) return 0;
    return &f->f[idx];
}
static unsigned long qc_sec(const struct frd *f) { return f->hold_lim ? f->hold_lim : (unsigned long)FRD_HOLD_TICKS; }     /* one second of the glue's clock: the hold length is one second */
static int qc_frame_run(struct frd *f, unsigned edge)      /* 1: the picker took this frame (it was open or just opened): the list sees no key */
{
    unsigned idx = 0, r, pressed = edge & (QC_DPAD | QC_B);
    int can = !f->msg_busy && f->req == FRD_REQ_NONE && !f->io_busy;
    if (f->scr != FRD_LIST || f->net != FRD_N_OK) { if (f->qc.open) { qc_close(&f->qc); f->dirty = FRD_D_ALL; } return 0; }
    if (!f->qc.open) {
        const struct frd_ent *e;
        if (!(pressed & QC_DPAD)) return 0;
        e = qc_target(f);
        if (!e) return 0;                                                        /* only on a friend's row (a request, a recent player or the separator: the D-Pad does nothing) */
        if (!f->ntxt) { say(f, "No quick chat texts yet", 1); f->sfx = FRD_SFX_DENY; return 1; }
        cpy(f->qc_code, e->code, 8);
        pressed &= QC_DPAD;                                                    /* (the opening press: B alone does not open) */
    }
    r = qc_frame(&f->qc, pressed, f->tb, qc_sec(f), can, &idx);
    if (r == QC_NONE) return f->qc.open;
    f->dirty = FRD_D_ALL;
    if (r & QC_OPENED) { f->msg = 0; f->sfx = FRD_SFX_MOVE; }
    if (r & QC_BLOCKED) { f->sfx = FRD_SFX_DENY; }
    if ((r & QC_CLOSED) && !(r & QC_SENT)) f->sfx = FRD_SFX_BACK;
    if (r & QC_SENT) {
        if (idx >= FRD_NTXT || !f->txt[idx][0]) { say(f, "No text there", 1); f->sfx = FRD_SFX_DENY; return 1; }
        cpy(f->arg_code, f->qc_code, 8); f->arg_txt = (unsigned char)idx; f->sfx = FRD_SFX_FWD;
        if (f->live) {
            f->req = FRD_REQ_MSG; f->busy_what = FRD_REQ_MSG; f->back_to = FRD_LIST; f->msg_busy = 1; f->wait = 0;       /* the list stays on screen: the answer says "Sent" in the panel */
            say(f, "Sending", 0);
        } else say(f, "Sent", 0);
    }
    return 1;
}

void frd_frame(struct frd *f, unsigned held, int stick_up, int stick_down)
{
    unsigned edge = held & ~(unsigned)f->prev;
    int up, down;
    f->prev = (unsigned short)held;
    if (f->toast_t) { f->toast_t--; f->dirty = f->toast_t ? f->dirty : FRD_D_ALL; }
    if (f->qc_on) {                                                            /* quick chat owns the D-Pad on the list: the stick scrolls (Left Right stay free there too) */
        if (qc_frame_run(f, edge)) return;                                     /* the picker takes every key until it closes (also the B that closes it) */
        if (f->scr == FRD_LIST) { held &= ~(unsigned)(FRD_B_UP | FRD_B_DOWN); edge &= ~(unsigned)(FRD_B_UP | FRD_B_DOWN | FRD_B_LEFT | FRD_B_RIGHT); }
    }
    up = stick_up || (edge & FRD_B_UP) != 0; down = stick_down || (edge & FRD_B_DOWN) != 0;
    if (held & (FRD_B_UP | FRD_B_DOWN)) {
        unsigned d = (held & FRD_B_UP) ? 1u : 2u;
        if (d != f->rep_dir || (edge & (FRD_B_UP | FRD_B_DOWN))) { f->rep_dir = (unsigned char)d; f->rep = 0; }
        else if (++f->rep >= FRD_REP_FIRST) { f->rep = (unsigned char)(FRD_REP_FIRST - FRD_REP_NEXT); if (d == 1u) up = 1; else down = 1; }
    } else { f->rep = 0; f->rep_dir = 0; }
    if (f->live && f->scr == FRD_LIST && f->net == FRD_N_OK && ++f->timer >= FRD_AUTO_FRAMES && f->req == FRD_REQ_NONE) refresh(f);
    else if (f->live && f->scr == FRD_LIST && f->net == FRD_N_OK && ++f->itimer >= FRD_INV_FRAMES && f->req == FRD_REQ_NONE && !f->inv_chk) { f->itimer = 0; f->req = FRD_REQ_INVITES; }      /* the Invites alone, every 10 s */
    if (f->msg_busy && f->scr == FRD_LIST && ++f->wait >= FRD_BUSY_FRAMES) { drop_req(f); frd_net_fail(f, FRD_REQ_MSG, FRD_W_TIMEOUT); }   /* a quick-chat message the glue never answered */
    switch (f->scr) {
    case FRD_LIST:
        if (up || down) { f->a_confirm = 0; f->rm = 0; }
        if (up) move(f, 0); else if (down) move(f, 1);
        list_frame(f, edge);
        break;
    case FRD_ACT:
        if (up) move(f, 0); else if (down) move(f, 1);
        act_rec_frame(f, edge);
        break;
    case FRD_CODE_ENTRY: code_frame(f, edge, up, down); break;
    case FRD_WAIT: wait_frame(f, edge); break;
    case FRD_BUSY: busy_frame(f, edge); break;                                 /* the glue answers; B cancels, 12 s ends it */
    default: break;
    }
}

/* ---- answers ---- */
void frd_net_requests(struct frd *f, unsigned total, unsigned n, const struct frd_rq *e)
{
    unsigned i;
    (void)total;
    for (i = 0; i < n && i < FRD_RQ; i++) { safe_cpy(f->r[i].code, e[i].code, 8); safe_cpy(f->r[i].name, e[i].name[0] ? e[i].name : e[i].code, 12); }
    f->nrq = (unsigned char)i;
    f->dirty = FRD_D_ALL;
}

void frd_net_invites(struct frd *f, unsigned n, const struct frd_in *e)
{
    unsigned i;
    f->itimer = 0;
    for (i = 0; i < n && i < FRD_IN; i++) {
        f->in[i] = e[i];
        f->in[i].type = (unsigned char)(e[i].type == 2u ? 2u : 1u);
        safe_cpy(f->in[i].code, e[i].code, 8); safe_cpy(f->in[i].name, e[i].name[0] ? e[i].name : e[i].code, 12);
    }
    f->nin = (unsigned char)i;
    if (f->scr == FRD_LIST) clamp(f);
    f->dirty = FRD_D_ALL;
}

void frd_net_invresp(struct frd *f, const char *peer)
{
    unsigned k;
    f->scr = f->back_to; f->sent++; f->a_confirm = 0;
    if (!f->arg_yes) { say(f, f->arg_type == 2u ? "Invite dismissed" : "Invite declined", 0); refresh(f); clamp(f); return; }
    if (f->arg_type == 2u) {                                                   /* a Game Night room: the list closes and the Game Night browser joins it (as Y on a Room row) */
        for (k = 0; k < 4u && peer && peer[k] >= '0' && peer[k] <= '9'; k++) f->join_code[k] = peer[k];
        if (k != 4u) { say(f, "That invite is over", 1); f->sfx = FRD_SFX_DENY; refresh(f); return; }
        f->join_code[4] = 0; f->join_rf = 0; f->join = 1; f->exit = 1; f->sfx = FRD_SFX_FWD;
        return;
    }
    cpy(f->play_code, peer && peer[0] ? peer : f->arg_code, 8);               /* accepted: both consoles search each other, as for a toast */
    f->play_after = 1; f->exit = 1; f->answered++;
    say(f, "Accepted: starting Mano a mano", 0); f->sfx = FRD_SFX_FWD;
}

void frd_net_friends(struct frd *f, unsigned total, unsigned offset, unsigned n, const struct frd_ent *e)
{
    unsigned i, at;
    if (offset == 0u) f->nf = 0;
    at = f->nf;
    for (i = 0; i < n && at < FRD_MAX; i++, at++) {
        struct frd_ent *d = &f->f[at];
        *d = e[i];
        safe_cpy(d->code, e[i].code, 8); safe_cpy(d->name, e[i].name[0] ? e[i].name : e[i].code, 12);
        safe_cpy(d->scode, e[i].scode, 8); safe_cpy(d->sname, e[i].sname, 15);
        if (d->presence > 4u) d->presence = 0;
        if (!(d->flags & 32u) || d->presence != 4u || !frd_game_name(d->gmode)[0]) { d->flags &= (unsigned char)~32u; d->gmode = 0; d->gflags = 0; } else d->gflags &= 1u;     /* a game tail only with flag 32 and a known mode */
        if (!(d->flags & 4u) || d->rflags > 3u) { d->room[0] = 0; d->rflags = 0; d->flags &= (unsigned char)~4u; }       /* a room tail only with flag 4 */
        else safe_cpy(d->room, e[i].room, 4);
    }
    f->nf = (unsigned char)at;
    f->total = (unsigned char)(total > 255u ? 255u : total);
    f->net = FRD_N_OK;
    if (f->nf < f->total && f->nf < FRD_MAX && n) { f->req = FRD_REQ_LIST; f->lpage = f->nf; }   /* the next page */
    else if (f->wchk) {                                                        /* the last page of the Watch check: still watchable in the fresh answer = go; otherwise stay here with a plain line */
        unsigned k, ok = 0;
        f->wchk = 0; f->msg = 0;
        for (k = 0; k < f->nf; k++) if (same(f->f[k].code, f->play_code) && frd_watchable(&f->f[k])) ok = 1;
        if (ok) { f->play_after = 4; f->exit = 1; }
        else { f->scr = FRD_LIST; f->a_rec = 0; say(f, "No game to watch right now", 0); f->sfx = FRD_SFX_DENY; }
    }
    else if (f->msg && f->msg[0] == 'R' && f->msg[1] == 'e') f->msg = 0;      /* "Refreshing" done */
    if (f->scr == FRD_LIST) { clamp(f); jump_apply(f); }
    f->dirty = FRD_D_ALL;
}

void frd_net_recent(struct frd *f, unsigned total, unsigned offset, unsigned n, const struct frd_rc *e)
{
    unsigned i, at;
    (void)total;
    if (offset == 0u) f->nrc = 0;
    at = f->nrc;
    for (i = 0; i < n && at < FRD_RC; i++, at++) {
        safe_cpy(f->rc[at].code, e[i].code, 8);
        safe_cpy(f->rc[at].name, e[i].name[0] ? e[i].name : e[i].code, 12);
    }
    f->nrc = (unsigned char)at;
    if (f->scr == FRD_LIST) clamp(f);
    f->dirty = FRD_D_ALL;
}

void frd_net_done(struct frd *f, unsigned what, unsigned result)
{
    f->scr = f->back_to;
    f->sent++;
    switch (what) {
    case FRD_REQ_RMREC: say(f, "Removed from recent", 0); f->scr = FRD_LIST; refresh(f); break;
    case FRD_REQ_ADD: {
        unsigned k = find_friend(f, f->arg_code);
        if (result == 2u) {                                                    /* already on the list: the cursor goes to that player and the toast names them */
            if (k) { jump_to(f, k - 1u); toast(f, f->f[k - 1u].name, " is already on your list", 0, 0); }
            else { toast(f, f->arg_code, " is already on your list", 0, 0); cpy(f->jump_code, f->arg_code, 8); f->jump_name = 1; }      /* the list is not loaded yet (the hand-off sends the code at once): jump and name them when it is */
        } else if (result == 1u) { toast(f, "You and ", f->arg_code, " are friends now", 0); cpy(f->jump_code, f->arg_code, 8); }
        else if (result == 3u) toast(f, "No player found for that Slippi code", 0, 0, 1);
        else if (result == 4u) toast(f, "Sandbag players can't be friends", 0, 0, 1);
        else if (result > 4u) toast(f, "No player found for that code", 0, 0, 1);
        else toast(f, "Friend request sent to ", f->arg_code, 0, 0);
        refresh(f); break;
    }
    case FRD_REQ_ANSWER: say(f, f->arg_yes ? "Friend added" : "Request declined", 0); refresh(f); break;
    case FRD_REQ_REMOVE: say(f, "Friend removed", 0); f->scr = FRD_LIST; refresh(f); break;
    case FRD_REQ_BLOCK: say(f, "Blocked", 0); f->scr = FRD_LIST; refresh(f); break;
    case FRD_REQ_MSG:
        f->msg_busy = 0; f->scr = FRD_LIST;
        say(f, "Sent", 0);
        break;
    default: break;
    }
    f->a_confirm = 0;
    clamp(f);
    f->dirty = FRD_D_ALL;
}

unsigned frd_qc_open(const struct frd *f) { return f->qc.open; }
void frd_qc_cell(const struct frd *f, unsigned pos, char *out) { qc_cell(&f->qc, (const char (*)[QC_TXT])f->txt, pos, 15u, out); }
void frd_qc_head(const struct frd *f, char *out)
{
    out[0] = 0;
    if (!f->qc.open) return;
    cpy(out, "Chat: ", 24); cat(out, qc_group_name(f->qc.group), 24);
}

void frd_net_invite(struct frd *f, unsigned long id)
{
    cpy(f->inv_name, f->f[f->sel].name, 13); f->inv_id = id; f->scr = FRD_WAIT; f->wait = 0; f->timer = 0; f->msg = 0; f->sent++;
    f->dirty = FRD_D_ALL;
}

void frd_net_status(struct frd *f, unsigned state, const char *peer)
{
    if (f->scr != FRD_WAIT || f->play_after) return;
    if (state == 1u) {                                                         /* accepted: the challenged player searches us; we search them */
        cpy(f->play_code, peer && peer[0] ? peer : f->f[f->sel].code, 8);
        f->play_after = 1; f->exit = 1; f->answered++;                         /* the Event screen closes first (exit), then play (frd_handoff): leaving it frozen under the scene change was the bug */
        say(f, "Accepted: starting Mano a mano", 0); f->sfx = FRD_SFX_FWD;
        return;
    }
    if (state == 2u) { f->scr = FRD_LIST; say(f, "Not answered", 1); f->sfx = FRD_SFX_DENY; }
}

static const char *why_text(struct frd *f, unsigned why)           /* one line, at most 30 characters (the old sentences ran out of the notice box) */
{
    switch (why) {
    case FRD_W_NOSOC: return "Friends are unavailable now";
    case FRD_W_SIGNIN: return "Link this console first";
    case FRD_W_NOTFOUND: return "Not found";
    case FRD_W_RATE: return "Too many requests, wait";
    case FRD_W_FULL: return "The list is full";
    case FRD_W_GONE: return "That challenge is over";
    case FRD_W_NOMSG: return "Messages are off here";
    case FRD_W_PENDING:                                                        /* the open invite went to inv_name (the last accepted one); unknown: a friend */
        cpy(f->info, "Already invited ", FRD_LINE); cat(f->info, f->inv_name[0] ? f->inv_name : "a friend", FRD_LINE);
        return f->info;
    case FRD_W_BAD: return "The server refused that";
    case FRD_W_TIMEOUT: return "No answer from the server";
    default: return "Server not available";
    }
}

void frd_net_fail(struct frd *f, unsigned what, unsigned why)
{
    if (what == FRD_REQ_LIST) {
        if (f->wchk) { f->wchk = 0; f->scr = FRD_LIST; say(f, "Could not check that game", 1); f->sfx = FRD_SFX_DENY; return; }      /* the Watch check could not ask: stay, nothing was left */
        if (f->net == FRD_N_OK && f->nf) { say(f, why_text(f, why), 1); return; }  /* a refresh failed: keep what is shown */
        f->net = why == FRD_W_NOSOC ? FRD_N_NOSOC : why == FRD_W_SIGNIN ? FRD_N_SIGNIN : FRD_N_UNREACH;
        f->nf = 0; f->nrq = 0; f->nin = 0; f->nrc = 0; f->first = 0; f->page = 0;
        f->dirty = FRD_D_ALL;
        return;
    }
    if (what == FRD_REQ_STATUS || what == FRD_REQ_INVITES) return;             /* one lost poll: the next one asks again (also the Invites alone) */
    if (what == FRD_REQ_INVRESP) {
        f->scr = f->back_to; f->inv_chk = 0;
        if (why == FRD_W_GONE || why == FRD_W_NOTFOUND) { say(f, "That invite is over", 1); refresh(f); }
        else say(f, why_text(f, why), 1);
        f->sfx = FRD_SFX_DENY;
        return;
    }
    if (what == FRD_REQ_MSG) f->msg_busy = 0;
    f->scr = f->back_to;
    if (what == FRD_REQ_ADD && why == FRD_W_NOTFOUND) toast(f, "No player found for that code", 0, 0, 1);
    else if (what == FRD_REQ_ADD) toast(f, why_text(f, why), 0, 0, 1);
    else say(f, why_text(f, why), 1);
    f->sfx = FRD_SFX_DENY;
    f->a_confirm = 0;
}

/* ---- texts ---- */
/* The status dot of a friend: red = in a game, green = ready (in the menus) or a Game Night room to join, amber = busy (online elsewhere) or searching, grey = offline, gold = a request.
   The server's presence has no 'room' value (a console in a Game Night room is simply online):
   2 in the menus = green (free to invite); 1 online but not in the menus (a lobby, any other screen) = amber (busy); 3 searching = amber with its own word; 4 in a match = red. */
static unsigned dot_of(const struct frd_ent *e)
{
    if (in_room(e)) return FRD_DOT_ROOM;                                       /* a Game Night room this console may join (the server puts a friend who is playing a set at Match, never Room) */
    switch (e->presence) {
    case 2: return FRD_DOT_GREEN;
    case 1: return FRD_DOT_YELLOW;
    case 3: return FRD_DOT_SEARCH;
    case 4: return FRD_DOT_RED;
    default: return FRD_DOT_GREY;
    }
}
const char *frd_dot_label(unsigned dot)       /* the Event view's second column: the status as a short word, in the colour of its dot */
{
    static const char *const W[10] = { "", "Ready", "Busy", "In match", "Offline", "Request", "Room", "Slippi", "In queue", "Invite" };
    return W[dot <= FRD_DOT_INV ? dot : 0u];
}
const char *frd_dot_word(unsigned dot) { return frd_dot_label(dot); }
unsigned frd_dot(const struct frd *f, unsigned slot)
{
    unsigned row, idx;
    if (vscr(f) != FRD_LIST) return FRD_DOT_NONE;
    row = (unsigned)f->first + slot;
    if (row >= frd_rows(f)) return FRD_DOT_NONE;
    switch (frd_row_kind(f, row, &idx)) {
    case 1: return FRD_DOT_REQ;
    case 6: return FRD_DOT_INV;
    case 4: case 5: return FRD_DOT_NONE;                                       /* the "Recent players" separator and the recent players below it: never a presence word or dot */
    default: return dot_of(&f->f[idx]);
    }
}
unsigned frd_mark(const struct frd *f, unsigned slot)
{
    unsigned row, idx;
    if (vscr(f) != FRD_LIST || f->net != FRD_N_OK) return 0;
    row = (unsigned)f->first + slot;
    if (row >= frd_rows(f) || frd_row_kind(f, row, &idx) != 2u) return 0;
    return (f->f[idx].flags & 8u) ? 2u : 1u;                                   /* FRIEND_LIST ext flag 8: unread messages from this friend; the console draws the status dot in the emblem column, a white outer ring while unread */
}
unsigned frd_legend(const struct frd *f)
{
    (void)f;
    return 0;                                                                   /* the words say it now: no legend line */
}
static const char *pres_line(const struct frd_ent *e)
{
    static const char *const L[5] = { "Offline", "Online", "Online in the menus", "Searching for a match", "In a match" };
    if (in_room(e)) return "In a Game Night room";
    if (has_room(e)) return "In a Game Night match";                            /* A joins the room, watch from there */
    if (in_game(e)) return e->gmode == 0u ? "In a game: Grandmaster" : e->gmode == 1u ? "In a game: Dojo" : "In a game: Mano a mano";     /**/
    return L[e->presence <= 4u ? e->presence : 0u];
}

static void fmt_secs(char *out, unsigned s)                                         /* "45s", "2m" (at most 99m) */
{
    out[0] = 0;
    if (s < 60u) { catu(out, s, 8); cat(out, "s", 8); }
    else { catu(out, s / 60u > 99u ? 99u : s / 60u, 8); cat(out, "m", 8); }
}
unsigned frd_row_parts(const struct frd *f, unsigned slot, char *num, char *name, char *code)
{
    unsigned row, idx, k;
    num[0] = 0; name[0] = 0; code[0] = 0;
    if (vscr(f) != FRD_LIST) { char t[FRD_TITLE + 1]; unsigned a = frd_row(f, slot, num, t); cpy(name, t, FRD_TITLE); return a; }
    row = (unsigned)f->first + slot;
    if (row >= frd_rows(f)) return 0;
    k = frd_row_kind(f, row, &idx);
    if (k == 1u) { cpy(name, f->r[idx].name, FRD_TITLE); cpy(code, f->r[idx].code, 8); return 2u; }
    if (k == 6u) { cpy(name, f->in[idx].name, FRD_TITLE); fmt_secs(code, f->in[idx].age); return 2u; }       /* the connect-code column holds how long ago it came ("2m") */
    if (k == 4u) { cpy(name, "Recent players", FRD_TITLE); return 1u; }
    if (k == 5u) { cpy(name, f->rc[idx].name, FRD_TITLE); cpy(code, f->rc[idx].code, 8); return 0u; }
    cpy(name, f->f[idx].name, FRD_TITLE); cpy(code, f->f[idx].code, 8);
    return (f->f[idx].presence || in_room(&f->f[idx])) ? 2u : 1u;              /* online friends stand out, offline ones are dim */
}

unsigned frd_row(const struct frd *f, unsigned slot, char *num, char *title)
{
    unsigned row, idx, k;
    num[0] = 0; title[0] = 0;
    if (vscr(f) == FRD_ACT && f->a_rec) {
        static const char *const AR[FRD_AR_N] = { "Add friend", "Remove from recent", "Block", "Back" };
        if (slot >= FRD_AR_N) return 0;
        cpy(title, AR[slot], FRD_TITLE);
        return slot == FRD_AR_BLOCK && f->a_confirm && slot == f->a_cur ? 3u : 0u;
    }
    if (vscr(f) == FRD_CODE_ENTRY) {
        static const char *const H[2] = { "A: Send request", "B: Delete or back" };
        if (slot == 0u) {                                                       /* "Code  AB[C]D@1": the letter under the cursor in brackets */
            unsigned i, n = slen(f->cbuf);
            char c1[2];
            cpy(title, "Code  ", FRD_TITLE);
            c1[1] = 0;
            for (i = 0; i < n; i++) {
                c1[0] = f->cbuf[i];
                if (i == f->cpos) cat(title, "[", FRD_TITLE);
                cat(title, c1, FRD_TITLE);
                if (i == f->cpos) cat(title, "]", FRD_TITLE);
            }
            return 2;
        }
        if (slot <= 2u) { cpy(title, H[slot - 1u], FRD_TITLE); return 1; }
        return 0;
    }
    if (vscr(f) != FRD_LIST) return 0;
    {
        char nm[FRD_TITLE + 1], cd[10];
        unsigned a = frd_row_parts(f, slot, num, nm, cd);
        row = (unsigned)f->first + slot;
        if (row >= frd_rows(f)) return 0;
        k = frd_row_kind(f, row, &idx);
        if (k == 4u) { cpy(title, nm, FRD_TITLE); return a; }
        frd_fit(title, nm, cd, FRD_TITLE - (k == 2u ? 2u : 0u));              /* the full code always; a long name is cut first; two characters stay free for the unread marker (frd_mark) */
        return a;
    }
}

/* ---- the description panel: like Melee's own, a grey title bar that always holds a title (frd_req_badge) and at most three body lines, WRAPPED at the panel's width
   (the number box sits on the right of it).  Text widths are estimated per character in 1/8 of a menu-font em; FRD_WRAP_W is the room left of the box. ---- */
#define FRD_WRAP_W 240u            /* 285 at the old small body font; the body is drawn 1.25 times as big now */
static unsigned cwid(char c)
{
    static const char NARROW[] = " il.,':;!|()[]I1";
    static const char WIDE[] = "mwMW@#%&";
    unsigned i;
    for (i = 0; NARROW[i]; i++) if (NARROW[i] == c) return 4u;
    for (i = 0; WIDE[i]; i++) if (WIDE[i] == c) return 12u;
    return (c >= 'A' && c <= 'Z') ? 9u : 8u;
}
struct body { char l[FRD_PANEL][FRD_LINE + 1]; unsigned char a[FRD_PANEL]; unsigned n; };
#define para(b, attr, t) para_w(b, attr, t, FRD_WRAP_W)
#define FRD_STATUS_W 220u          /* wrap width of the centred status message of the list (it is drawn 1.25 times as big as the panel text) */
static void para_w(struct body *b, unsigned attr, const char *t, unsigned wrap)
{
    unsigned i = 0;
    while (t[i] == ' ') i++;
    while (t[i] && b->n < FRD_MSG_LINE + 1u) {
        unsigned w = 0, j = i, brk = 0, k, e;
        while (t[j] && w + cwid(t[j]) <= wrap && j - i < FRD_LINE) { w += cwid(t[j]); if (t[j] == ' ') brk = j; j++; }
        k = (t[j] && brk > i) ? brk : j;                                       /* cut at the last space that fits (a word longer than a line is cut where it stops fitting) */
        for (e = i; e < k; e++) b->l[b->n][e - i] = t[e];
        b->l[b->n][k - i] = 0;
        b->a[b->n] = (unsigned char)attr;
        b->n++;
        i = k;
        while (t[i] == ' ') i++;
    }
}
static void code_para(struct body *b, const char *code, int bot)
{
    char t[FRD_LINE + 1];
    cpy(t, "Connect code ", FRD_LINE); cat(t, code, FRD_LINE); if (bot) cat(t, ", a bot", FRD_LINE);
    para(b, 1u, t);
}
static void build(const struct frd *f, struct body *b)
{
    unsigned idx, k, i;
    char t[FRD_LINE + 1];
    const struct frd_ent *e = &f->f[f->sel];
    for (i = 0; i < FRD_PANEL; i++) { b->l[i][0] = 0; b->a[i] = 0; }
    b->n = 0;
    switch (f->scr) {
    case FRD_LIST:
        if (f->qc.open) {                                                      /* the picker (the console draws its cross itself, frd_qc_cell; this is the text view and the fallback) */
            char cl[16];
            for (i = 0; i < 4u; i++) {
                frd_qc_cell(f, i, cl);
                cpy(t, qc_group_name(i), FRD_LINE); cat(t, ": ", FRD_LINE); cat(t, cl, FRD_LINE);
                cpy(b->l[i], t, FRD_LINE); b->a[i] = 0;
            }
            b->n = 4u;
            return;
        }
        if (f->net == FRD_N_LOADING) { para_w(b, 0u, "Loading Friends", FRD_STATUS_W); return; }                /* the non-OK states of the list: white, gold, or the warning colour; the console centres them in the box (frd_status_view) */
        if (f->net != FRD_N_OK) {
            para_w(b, f->net == FRD_N_UNREACH ? 3u : 0u, f->net == FRD_N_NOSOC ? "Friends are not available right now. Try again later." : f->net == FRD_N_SIGNIN ? "Link this console: friends need an account" : "Server not available", FRD_STATUS_W);
            return;                                    /* no key line in the box: the footer (frd_hint) already says A: Try again  B: Back */
        }
        if (!frd_rows(f)) { para(b, 0u, "Press Z to add one by connect code, or after a fight."); return; }   /* the empty state: the key line (frd_hint) carries Z Add a friend */
        if (f->rm) { para(b, 0u, "Remove: you are no longer friends."); para(b, 0u, "Block: they can't find or invite you."); return; }
        k = frd_row_kind(f, cur_row(f), &idx);
        if (k == 1u) { cpy(t, f->r[idx].name, FRD_LINE); cat(t, " wants to be friends.", FRD_LINE); para(b, 0u, t); code_para(b, f->r[idx].code, 0); return; }
        if (k == 6u) {                                                         /* an invite: who, what, how long ago, how long it can still be accepted */
            const struct frd_in *v = &f->in[idx];
            cpy(t, "Invite from ", FRD_LINE); cat(t, v->name, FRD_LINE); para(b, 0u, t);   /*: short; the title says which kind */
            if (v->type == 2u) { cpy(t, "Room ", FRD_LINE); cat(t, v->code, FRD_LINE); cat(t, ", ", FRD_LINE); } else t[0] = 0;
            { char u[10]; fmt_secs(u, v->left > 300u ? 300u : v->left); cat(t, u, FRD_LINE); cat(t, " left", FRD_LINE); }
            para(b, 1u, t);
            return;
        }
        if (k == 4u) { para(b, 0u, "People you finished a match with."); para(b, 1u, "Not friends yet, so no online status."); return; }
        if (k == 5u) { code_para(b, f->rc[idx].code, 0); para(b, 1u, "Recent player, not a friend yet."); return; }
        e = &f->f[idx];
        if (!e->presence && !in_room(e) && e->seen < 255u) {                  /* the title bar says what they are doing (frd_req_badge); an offline friend's body says when they were seen */
            cpy(t, "Seen ", FRD_LINE);
            if (e->seen == 0u) cat(t, "today", FRD_LINE); else if (e->seen == 1u) cat(t, "yesterday", FRD_LINE);
            else { catu(t, e->seen, FRD_LINE); cat(t, e->seen >= 254u ? " or more days ago" : " days ago", FRD_LINE); }
            para(b, 1u, t);
        }
        code_para(b, e->code, e->flags & 1u);
        if (e->sname[0] || e->scode[0]) {                                      /* the Slippi name ONLY when the friend has one */
            cpy(t, "Slippi: ", FRD_LINE); cat(t, e->sname, FRD_LINE); if (e->sname[0] && e->scode[0]) cat(t, " ", FRD_LINE); cat(t, e->scode, FRD_LINE);
            para(b, 1u, t);
        }
        return;
    case FRD_ACT:
        {                                                                       /* the action list of a recent player */
            static const char *const DR[FRD_AR_N] = { "Send them a friend request.", "Take them off your recent players.", "Block: no more invites or messages.", "Back to the list." };
            if (f->sel >= f->nrc) return;
            if (f->a_confirm && f->a_cur == FRD_AR_BLOCK) para(b, 3u, "Press A again to block"); else para(b, 0u, DR[f->a_cur % FRD_AR_N]);
            para(b, 1u, "Recent player, not a friend yet.");
            return;
        }
        return;
    case FRD_CODE_ENTRY: para(b, 0u, "Like TAG@123 or TAG#123"); return;     /* the code itself is row 0 of the table (frd_row) */
    case FRD_WAIT: para(b, 0u, "Waiting for an answer."); para(b, 1u, "They have 20 seconds to accept."); return;
    case FRD_BUSY: para(b, 1u, f->busy_what == FRD_REQ_INVITE ? "Sending the challenge" : f->busy_what == FRD_REQ_MSG ? "Sending the message" : "Asking the server"); return;
    default: return;
    }
}

unsigned frd_status_view(const struct frd *f)                                  /* the list is not usable (loading, offline, not linked, not on this server): the number of message lines (1..3) the console draws centred in the box, else 0 */
{
    struct body b;
    if (f->scr != FRD_LIST || f->qc.open || f->net == FRD_N_OK) return 0;
    build(f, &b);
    return b.n > FRD_PANEL - 1u ? FRD_PANEL - 1u : b.n;
}

unsigned frd_panel(const struct frd *f, unsigned line, char *out)
{
    struct body b;
    out[0] = 0;
    if (line == FRD_MSG_LINE && f->msg) { cpy(out, f->msg, FRD_LINE); return f->msg_warn ? 3u : 0u; }      /*: every line is the normal text colour (a warning stays coral); gold is for the REP line only */
    if (line >= FRD_PANEL) return 0;
    build(f, &b);
    cpy(out, b.l[line], FRD_LINE);
    return b.a[line] == 3u ? 3u : 0u;
}

const char *frd_title(const struct frd *f)
{
    switch (f->scr) {
    case FRD_ACT: return "Friend";
    case FRD_CODE_ENTRY: return "Add a friend";
    case FRD_WAIT: return "Mano a mano";
    default: return "Friends";
    }                                                                           /* host text view only: the console draws no gold title (frd_ev.c item 20), the panel header says it (frd_req_badge) */
}
unsigned frd_hold_pct(const struct frd *f) { return f->hold.on ? (unsigned)(f->hold.pm / 10u) : 0u; }
const char *frd_hint(const struct frd *f)
{
    static char h[80];
    unsigned idx, k, n = 0, have_page, a = 0, y = 0, x = 0;
    switch (f->scr) {
    case FRD_LIST:
        if (f->net != FRD_N_OK) return "A: Try again  B: Back";
        if (f->qc.open) return "D-Pad: Send  B: Close";
        if (f->rm) return "A: Remove  Y: Block  B: Cancel";
        if (!frd_rows(f)) return "Z: Add a friend  B: Back";
        have_page = frd_rows(f) > FRD_VIS;
        k = frd_row_kind(f, cur_row(f), &idx);
        h[0] = 0;
        if (k == 1u) { cat(h, "A: Accept  X: Decline", 79); n = 2; }
        else if (k == 6u) { cat(h, f->in[idx].type == 2u ? "A: Join room  X: Dismiss" : "A: Accept  X: Decline", 79); n = 2; }
        else if (k == 5u) { cat(h, "A: Open", 79); n = 1; }
        else if (k == 2u) {                                                    /* at most four keys: A, Y, X and B first; Z and the page keys only where a slot is free */
            const struct frd_ent *e = &f->f[idx];
            unsigned fa = fa_of(e);
            if (fa) { cat(h, fa == FRD_FA_JOIN ? "A: Join room" : fa == FRD_FA_WATCH ? "A: Watch" : "A: Mano a mano", 79); a = 1; }
            if (e->scode[0]) { cat(h, a ? "  Y: Slippi" : "Y: Slippi", 79); y = 1; }
            cat(h, a || y ? "  X: Remove" : "X: Remove", 79); x = 1;
            n = a + y + x;
        }
        if (k == 2u && n < 3u && f->qc_on && f->ntxt) { cat(h, n ? "  D-Pad: Chat" : "D-Pad: Chat", 79); n++; }       /* quick chat, before Z and the page keys; a full line (A, Y, X) has no room for it */
        if (n < 3u) { cat(h, n ? "  Z: Add a friend" : "Z: Add a friend", 79); n++; }
        if (n < 3u && have_page) { cat(h, "  L R: Page", 79); n++; }
        cat(h, "  B: Back", 79);
        return h;
    case FRD_ACT: return "A: Choose  B: Back";
    case FRD_CODE_ENTRY: return "A: Send request  B: Delete or back";
    case FRD_WAIT: return "B: Stop waiting";
    default: return "";
    }
}
void frd_badge(const struct frd *f, char *out)
{
    unsigned i, on = 0;
    out[0] = 0;
    if (f->net != FRD_N_OK) return;
    for (i = 0; i < f->nf; i++) if (f->f[i].presence) on++;
    if (f->nin) { catu(out, f->nin, 12); cat(out, " inv  ", 18); }
    if (f->nrq) { catu(out, f->nrq, 12); cat(out, " req  ", 18); }           /* the pending requests count lives with the Online count (the list's Requests section has the rows) */
    catu(out, on, 22); cat(out, " online", 23);
}

void frd_req_badge(const struct frd *f, char *out)                         /* the grey title bar of the description panel: always a title */
{
    unsigned idx, k;
    out[0] = 0;
    if (f->scr == FRD_LIST && f->qc.open) { frd_qc_head(f, out); return; }
    if (f->scr == FRD_ACT && f->a_rec) { cpy(out, f->sel < f->nrc ? f->rc[f->sel].name : "Recent player", 18); return; }
    if (f->scr == FRD_WAIT) { cpy(out, f->f[f->sel].name, 18); return; }
    if (f->scr == FRD_CODE_ENTRY) { cpy(out, "Add a friend", 18); return; }
    if (f->scr == FRD_LIST && f->net != FRD_N_OK) return;                    /*: no title on the box's top line while the list shows a status (it sits centred in the box) */
    if (f->scr != FRD_LIST || f->net != FRD_N_OK) { cpy(out, "Friends", 18); return; }
    if (!frd_rows(f)) { cpy(out, "No friends yet", 18); return; }
    if (f->rm) { cpy(out, "Remove ", 18); cat(out, f->f[f->sel].name, 17); cat(out, "?", 18); return; }
    k = frd_row_kind(f, cur_row(f), &idx);
    if (k == 1u) cpy(out, "Friend request", 18);
    else if (k == 6u) cpy(out, f->in[idx].type == 2u ? "Room invite" : "Mano a mano", 18);
    else if (k == 4u) cpy(out, "Recent players", 18);
    else if (k == 5u) cpy(out, f->rc[idx].name, 18);
    else cpy(out, pres_line(&f->f[idx]), 24);                                  /* a friend: what they are doing (the name is already on the selected row) */
}
unsigned frd_online(const struct frd *f)
{
    unsigned i, on = 0;
    for (i = 0; i < f->nf; i++) if (f->f[i].presence || in_room(&f->f[i])) on++;
    return on;
}
unsigned frd_box(const struct frd *f, unsigned line, char *out)
{
    unsigned n;
    out[0] = 0;
    if (f->net != FRD_N_OK || vscr(f) != FRD_LIST) return 0;
    n = frd_online(f);
    if (line == 0u) { catu(out, n > 99u ? 99u : n, 3); return 1; }
    if (line == 1u) cpy(out, "online", 8);                                      /* one line, "online"; line 2 stays empty */
    return 0;
}

void frd_fit(char *out, const char *name, const char *code, unsigned width)
{
    unsigned nc = slen(code), nn = slen(name), room;
    out[0] = 0;
    if (nc >= width) { cpy(out, code, width); return; }                       /* never happens (a code is at most 8 characters), but stay in the buffer */
    room = width - nc;                                                         /* name + "  " */
    if (nn + 2u <= room) { cpy(out, name, nn); }
    else if (room >= 4u) { cpy(out, name, room - 3u); cat(out, ".", width); }  /* cut the name, mark it with a dot */
    if (out[0]) cat(out, "  ", width);
    cat(out, code, width);
}

/* ---- the character-cell text view (host golden tests, console fallback) ---- */
static void put(struct sg_view *v, unsigned r, unsigned attr, const char *s)
{
    unsigned c = 0;
    if (r >= SG_ROWS) return;
    while (s[c] && c < SG_COLS) { v->t[r][c] = s[c]; c++; }
    if (s[c]) sg_cut_count++;
    v->t[r][c] = 0;
    v->attr[r] = (unsigned char)attr;
}
void frd_text(const struct frd *f, struct sg_view *v)
{
    static const unsigned char ATTR[4] = { SG_A_BODY, SG_A_DIM, SG_A_GOOD, SG_A_BAD };
    char b[64], num[FRD_NUM + 1], title[FRD_TITLE + 1], ln[64];
    unsigned s, a, i, n = frd_rows(f), pg = frd_cur_page(f);
    put(v, 0, SG_A_HEAD, frd_title(f));
    frd_badge(f, b);
    put(v, 1, SG_A_DIM, b);
    for (s = 0; s < FRD_VIS; s++) {
        a = frd_row(f, s, num, title);
        ln[0] = 0;
        cat(ln, s == pg && n ? "- " : "  ", 60);
        if (!num[0] && title[0]) cpy(num, frd_dot_word(frd_dot(f, s)), FRD_NUM);
        if (num[0] || title[0]) { cat(ln, num, 60); while (slen(ln) < 2u + FRD_NUM + 1u) cat(ln, " ", 60); cat(ln, title, 60); }
        put(v, 3u + s, s == pg && n ? SG_A_HI : ATTR[a & 3u], ln);
    }
    for (i = 0; i < FRD_PANEL; i++) { a = frd_panel(f, i, ln); put(v, 14u + i, ATTR[a & 3u], ln); }
    put(v, SG_ROWS - 1u, SG_A_DIM, frd_hint(f));
    (void)same;
}
