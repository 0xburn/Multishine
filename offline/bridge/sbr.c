/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sbr: see sbr.h.  The RESIDENT half of the Slippi bridge since the 3.7.0 matchmaker: states, error texts with the R3 notice, the mailbox commands to the
   matchmaker client in ms/rep.ovl (sbr_mbx.h), the match record, the peer step's budget and the bounded re-search.  The ENet matchmaker connection that used to live here
   is gone; the WebSocket protocol runs in the overlay (rep_mm.c over sbr_ws.c).  Project-owned C99 (no libc, no heap). */
#include "sbr.h"

typedef sbr_u32 u32;
typedef sbr_s32 s32;
typedef sbr_u16 u16;
typedef sbr_u8 u8;

/* ------------------------------------------------------------------ small helpers (no libc) */
static void zero(void *p, u32 n) { volatile u8 *b = (volatile u8 *)p; while (n--) *b++ = 0; }   /* volatile: wipes are not optimised away */
static void bcopy_(void *d, const void *s, u32 n) { u8 *a = (u8 *)d; const u8 *b = (const u8 *)s; while (n--) *a++ = *b++; }   /* no memcpy (freestanding) */
static u32 slen(const char *s) { u32 n = 0; if (!s) return 0; while (s[n]) n++; return n; }
static void scpy(char *d, const char *s, u32 cap) { u32 i = 0; if (!cap) return; if (s) for (; s[i] && i + 1 < cap; i++) d[i] = s[i]; d[i] = 0; }
static void scat(char *d, const char *s, u32 cap) { u32 n = slen(d); if (n < cap) scpy(d + n, s, cap - n); }
static int seq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

u32 sbr_build_seq;      /* our release (the User-Agent of the report client), set by the console glue (br_glue.c core_init); 0 = unknown (host tests, other images) */

/* ------------------------------------------------------------------ texts */
const char *const sbr_notice[2] = { "Unofficial client. Not Slippi.", "Problems: report to Multishine, not Slippi." };
#define NOTICE_SHORT ". Not Slippi - report to Multishine."     /* error-box form of R3 (the box holds 120 characters) */

/* The R3 suffix says "this is our fault, not Slippi's": only for internal / protocol faults.  Ordinary outcomes (opponent left, no
   opponent, network down, timeouts, bad user input) get the plain text. */
static int fault_code(int e)
{
    switch (e) {
    case SBR_E_GATE: case SBR_E_BAD_JOIN: case SBR_E_BAD_STATUS: case SBR_E_SERVER: case SBR_E_OUTDATED:
    case SBR_E_PROTO: case SBR_E_SEND: case SBR_E_BUSY: return 1;
    default: return 0;
    }
}

static void set_error_x(struct sbr *B, int e, const char *text, int fault)
{
    char t[SBR_ERR_MAX];
    u32 i, room = fault ? 120u - (u32)(sizeof(NOTICE_SHORT) - 1) : 119u;   /* the plain form needs no room for the notice */
    scpy(t, text, sizeof t);
    for (i = 0; t[i]; i++) {                           /* Melee's text drawing swallows '/' and '|' (ONLINE_GLUE finding 3) */
        u8 c = (u8)t[i];
        if (c < 0x20 || c > 0x7E) t[i] = '?';
        else if (c == '/' || c == '|' || c == '\\' || c == '"') t[i] = '-';
    }
    if (slen(t) > room) t[room] = 0;
    while (slen(t) && t[slen(t) - 1] == '.') t[slen(t) - 1] = 0;
    scpy(B->err_text, t, sizeof B->err_text);
    scat(B->err_text, fault ? NOTICE_SHORT : ".", sizeof B->err_text);
    B->err = e;
    B->state = SBR_ERROR;
    B->st.err = (u32)e;
    B->st.state = SBR_ERROR;
    B->st.errors++;
}

static void set_error(struct sbr *B, int e, const char *text) { set_error_x(B, e, text, fault_code(e)); }

const char *sbr_error_text(const struct sbr *B) { return B->state == SBR_ERROR ? B->err_text : ""; }
int sbr_state(const struct sbr *B) { return B->state; }

/* ------------------------------------------------------------------ mailbox commands (the overlay acts on cmd_seq != ack_seq) */
static void mbx_cmd(struct sbr *B, u32 cmd)
{
    B->mbx.cmd = cmd;
    B->mbx.cmd_seq++;
    B->t_cmd = B->now;
}

static void start_search(struct sbr *B)             /* a fresh search for B->mode / B->code: arguments first, then the edge */
{
    struct sbr_mbx *M = &B->mbx;
    M->mode = B->mode;
    M->lan_ip = B->cfg.lan_ip;
    M->local_port = B->cfg.local_port;
    M->mm_ip = B->cfg.server_ip;
    M->mm_port = B->cfg.server_port;
    M->dns_ip = B->cfg.dns_ip;
    M->dns_port = B->cfg.dns_port;
    scpy(M->code, B->code, sizeof M->code);
    scpy(M->app_version, B->app_version, sizeof M->app_version);
    scpy(M->host, B->cfg.host, sizeof M->host);
    M->state = SBR_INIT; M->err = 0; M->err_text[0] = 0;   /* what the overlay will report first; the resident ignores the group until the command is acked */
    mbx_cmd(B, SBR_MC_SEARCH);
    B->owned = 1;
    B->state = SBR_INIT; B->st.state = SBR_INIT;
    B->t_state = B->now;
}

static void release_overlay(struct sbr *B)          /* ends whatever the overlay is doing for us (connection closed, identity copy wiped on its side) */
{
    if (B->owned || B->mbx.cmd == SBR_MC_SEARCH) mbx_cmd(B, SBR_MC_CANCEL);
    B->owned = 0;
}

/* ------------------------------------------------------------------ public API */
void sbr_init(struct sbr *B, const struct sbr_cfg *cfg, const struct sbr_ops *ops, void *ctx)
{
    zero(B, sizeof *B);
    bcopy_(&B->cfg, cfg, sizeof B->cfg);
    if (!B->cfg.peer_ms) B->cfg.peer_ms = 8000;
    if (!B->cfg.platform) B->cfg.platform = SBR_PLAT_GC;
    B->ops = ops; B->ctx = ctx;
    B->st.magic = 0x53425231u;                         /* 'SBR1' */
    scpy(B->app_version, B->cfg.app_version, sizeof B->app_version);
    B->mbx.magic = SBR_MBX_MAGIC; B->mbx.ver = SBR_MBX_VER; B->mbx.size = (u32)sizeof B->mbx;
}

void sbr_identity(struct sbr *B, const struct sbr_ident *id)
{
    B->id = id;
    B->id_ok = id && id->uid[0] && id->play_key[0] && sbr_code_valid(id->code) && id->name[0];
}

void sbr_set_net(struct sbr *B, int up, u32 lan_ip) { B->net_up = up; B->cfg.lan_ip = lan_ip; }

int sbr_search(struct sbr *B, u32 now, u32 mode, const char *code)
{
    B->now = now;
    if (B->state != SBR_IDLE) return -SBR_E_BUSY;
    B->err = 0; B->err_text[0] = 0;
    B->st.searches++;
    B->st.t_search_ms = now;
    B->reticket = 0;
    B->retry_wait = 0;
    if (B->outdated) { set_error(B, SBR_E_OUTDATED, SBRW_TXT_OUTDATED); return -SBR_E_OUTDATED; }   /* Slippi told us this session: no second search */
    if (!B->app_version[0]) { set_error(B, SBR_E_GATE, "No Slippi version configured"); return -SBR_E_GATE; }
    if (mode != SBR_MODE_UNRANKED && mode != SBR_MODE_DIRECT) { set_error(B, SBR_E_MODE, "Only Unranked and Direct are available on the Slippi bridge"); return -SBR_E_MODE; }
    if (!B->id_ok) { set_error(B, SBR_E_NO_ID, "No Slippi identity on the memory card"); return -SBR_E_NO_ID; }
    B->mode = mode;
    B->st.mode = mode;
    B->code[0] = 0;
    if (mode == SBR_MODE_DIRECT) {
        if (!sbr_code_valid(code)) { set_error(B, SBR_E_CODE, "That is not a valid connect code"); return -SBR_E_CODE; }
        if (seq(code, B->id->code)) { set_error(B, SBR_E_CODE, "That is your own connect code"); return -SBR_E_CODE; }
        scpy(B->code, code, sizeof B->code);
    }
    if (!B->net_up) { set_error(B, SBR_E_NO_NET, "No network. Check the cable."); return -SBR_E_NO_NET; }
    start_search(B);
    return 0;
}

static void fail_x(struct sbr *B, u32 now, int e, const char *text, int fault)
{
    if (now - B->now < 0x80000000u) B->now = now;
    if (B->state == SBR_IDLE) { B->st.searches++; B->st.t_search_ms = now; }
    set_error_x(B, e, text, fault);
    release_overlay(B);
}
void sbr_fail(struct sbr *B, u32 now, int e, const char *text) { fail_x(B, now, e, text, fault_code(e)); }
void sbr_fail_fault(struct sbr *B, u32 now, int e, const char *text) { fail_x(B, now, e, text, 1); }

void sbr_cancel(struct sbr *B)
{
    B->retry_wait = 0;
    if (B->state != SBR_IDLE) B->st.cancels++;
    release_overlay(B);
    zero(&B->match, sizeof B->match);
    B->state = SBR_IDLE; B->st.state = SBR_IDLE;
    B->err = 0; B->st.err = 0; B->err_text[0] = 0;
}

/* the overlay's group of the mailbox -> our state (only after it acked the command: before that its words are the previous search's) */
static void take_mailbox(struct sbr *B)
{
    const struct sbr_mbx *M = &B->mbx;
    B->st.tickets = M->n_joins;
    B->st.srv_connects = M->n_joins + M->n_resumes;
    B->st.rx_msgs = M->n_rx_msgs;
    B->st.rx_bad = M->n_rx_bad;
    switch (M->state) {
    case SBR_INIT: case SBR_MM:
        B->state = (int)M->state; B->st.state = M->state;
        break;
    case SBR_OPP_CONN:
        if (B->state == SBR_OPP_CONN) break;
        bcopy_(&B->match, &M->match, sizeof B->match);
        B->match.seq = B->st.assigned + 1u;
        B->match.ready = 1;                            /* the overlay closed the matchmaker socket before it published the match */
        B->st.assigned++;
        B->st.t_assign_ms = B->now - B->st.t_search_ms;
        B->state = SBR_OPP_CONN; B->st.state = SBR_OPP_CONN;
        B->t_state = B->now;                           /* the peer budget starts now */
        B->owned = 0;                                  /* the overlay is done; it returns to idle by itself and is freed by the glue */
        break;
    case SBR_ERROR:
        if (M->err == SBR_E_OUTDATED) { B->outdated = 1; scpy(B->latest, M->latest, sizeof B->latest); }
        set_error(B, (int)M->err, M->err_text);
        B->owned = 0;
        break;
    default: break;                                    /* IDLE: the overlay has not started yet or was cancelled: nothing to take */
    }
}

void sbr_tick(struct sbr *B, u32 now)
{
    if (now - B->now < 0x80000000u) B->now = now;
    if (B->owned) {
        if (B->mbx.ack_seq == B->mbx.cmd_seq) take_mailbox(B);
        else if (B->now - B->t_cmd >= SBR_MBX_ACK_MS) {     /* nobody took the command: the overlay is missing or could not load */
            set_error(B, SBR_E_CONNECT, "Online files missing on the memory card");
            release_overlay(B);
        }
    }
    if (B->state == SBR_INIT && B->retry_wait && B->now - B->t_state >= SBR_RETICKET_MS) {   /* the pause between two searches is over: search again, silently */
        B->retry_wait = 0;
        start_search(B);
    }
    if (B->state == SBR_OPP_CONN && B->match.ready && B->now - B->t_state >= B->cfg.peer_ms) sbr_peer_result(B, B->now, 0);
}

void sbr_peer_result(struct sbr *B, u32 now, int ok)
{
    if (now - B->now < 0x80000000u) B->now = now;
    if (B->state != SBR_OPP_CONN) return;
    if (ok) { B->st.peer_ok++; B->state = SBR_CONNECTED; B->st.state = SBR_CONNECTED; return; }
    B->st.peer_fail++;
    if ((u32)B->reticket < B->cfg.reticket_max) {     /* stock: a fresh search, silently; ours is bounded (R11) */
        B->reticket++;
        B->st.retickets++;
        zero(&B->match, sizeof B->match);
        B->state = SBR_INIT; B->st.state = SBR_INIT;
        B->t_state = B->now;
        B->retry_wait = 1;                             /* tick sends the next search command after SBR_RETICKET_MS */
        return;
    }
    set_error(B, SBR_E_PEER, "Couldn't connect to your opponent. Try again.");
}
