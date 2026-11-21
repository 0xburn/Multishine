/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* sgu: the Update row's state machine, see sgu_core.h.  C99-free C89, no libc, no heap of its own. */
#include "sgu_core.h"
#include "sgu_net.h"
#include "../upd/upd_req.h"

#ifndef SGU_FETCH
#define SGU_FETCH mnu_fetch              /* the host test swaps in a fetch that refuses the first N starts (tests/sgu_driver.c) */
#else
int SGU_FETCH(mnu *u, mnc_u32 now, mnc_u32 seq, mnc_u32 size, mnc_u8 *buf, mnc_u32 cap);
#endif
static const char NAME_ACTIVE[] = "MultishineCode";
static const char NAME_PREV[] = "MultishineCodeB";
static const char NAME_STAGE[] = "MultishineCodeN";
static const char NAME_STATE[] = "MultishineState";     /* the floor record and the boot record (ota_core.h BOOT RECORD) */
/* the second OTA kind: MultishineOvl = OUR OWN files (ms/sign.ovl and later our own texts), ABI 2.  The derived pack (MultishinePack) is never delivered. */
static const char OVL_ACTIVE[] = "MultishineOvl";
static const char OVL_PREV[] = "MultishineOvlB";
static const char OVL_STAGE[] = "MultishineOvlN";
#define SGU_OVL_ABI 2u
#define NM_ACTIVE(s) ((s)->kind ? OVL_ACTIVE : NAME_ACTIVE)
#define NM_PREV(s) ((s)->kind ? OVL_PREV : NAME_PREV)
#define NM_STAGE(s) ((s)->kind ? OVL_STAGE : NAME_STAGE)
#define SGU_PACK_ABI 4u    /* the system-files data container (tools/card/pack_data.py): Slippi's public files + our pack header; the console builds MultishinePack from it and its own disc (sgp_core.c) */
#define ABI_OF(s) ((s)->kind == 3 ? SGU_PACK_ABI : (s)->kind == 2 ? NGP_ABI : (s)->kind ? SGU_OVL_ABI : (s)->h->abi)      /* kind 2: the network patch (sgu_net.h) */
/* the fallback file stays as it is: a reinstall of the running build, or ANY install while the running code IS the fallback (the loader refused the active file: automatic rollback,
).  The new file then replaces the active one (stage, delete the active file, rename); the fallback boots at every power cut. */
#define KEEPPREV(s) ((s)->same || (!(s)->kind && (s)->h && (s)->h->from_prev))

const char *sgu_state_name(unsigned st)
{
    static const char *const n[SGU_NSTATE] = { "idle", "checking", "uptodate", "available", "downloading", "verifying", "installing", "restart", "failed", "offline", "done" };
    return st < SGU_NSTATE ? n[st] : "?";
}

static void zero(void *p, unsigned n)
{
    unsigned char *b = (unsigned char *)p;
    unsigned i;
    for (i = 0; i < n; i++) b[i] = 0;
}

/* the build_seq the next update must beat: the running one, or the one installed since boot */
static unsigned eff_seq(const struct sgu *s)
{
    unsigned a, b;
    if (s->kind == 3) return 0;                     /* the system files have no installed build_seq: any published container is offered */
    if (s->kind == 2) return s->net_seq;
    if (s->kind) return s->ovl_seq;
    a = s->h->installed_seq; b = s->h->pending_seq ? *s->h->pending_seq : 0u;
    return b > a ? b : a;
}

/* the 'have' of UPD_CHECK: one below the build that must be beaten, so that the server also reports the installed build itself (equal is accepted, older never) */
static unsigned have_seq(const struct sgu *s)
{
    unsigned e = eff_seq(s);
    return e ? e - 1u : 0u;
}

/* the build_seq of the installed MultishineOvl: read from its header block (0 = no such file: a card made before the split, the first OTA then installs it) */
static void read_ovl_seq(struct sgu *s)
{
    const struct sgu_host *h = s->h;
    mw2_u8 *b;
    s->ovl_seq = 0;
    if (!h->card || !h->card->read || !h->card->exists || !h->card_ready || !h->card_ready()) return;
    if (h->card->exists(h->card->ctx, OVL_ACTIVE)) {
        b = (mw2_u8 *)h->alloc(OTA_BLOCK);
        if (b) {
            if (h->card->read(h->card->ctx, OVL_ACTIVE, 0, b, OTA_BLOCK) == 0 && b[0x1000] == 'M' && b[0x1001] == 'S' && b[0x1002] == 'C' && b[0x1003] == 'F' && b[0x1006] == 0 && b[0x1007] == SGU_OVL_ABI)
                s->ovl_seq = ((unsigned)b[0x1008] << 24) | ((unsigned)b[0x1009] << 16) | ((unsigned)b[0x100A] << 8) | b[0x100B];
            h->free(b);
        }
    }
    if (h->card_done) h->card_done();
}

/* the seq of the last applied network patch, read from the online block when the net leg starts (the block holds the device secret: ngp_stored_seq scrubs it) */
static void read_net_seq(struct sgu *s)
{
    const struct sgu_host *h = s->h;
    mw2_u8 *b;
    s->net_seq = 0xFFFFFFFFu;                              /* no online record (or no usable card): nothing to patch, so no patch is ever newer than this and none is offered */
    if (!h->card || !h->card->read || !h->card->exists || !h->card_ready || !h->card_ready()) return;
    b = h->card->exists(h->card->ctx, "MultishineOnline") ? (mw2_u8 *)h->alloc(NGP_WORK) : 0;       /* nothing is allocated without a record */
    if (b) { unsigned q = 0; if (ngp_stored_seq(h->card, b, &q) == NGP_OK) s->net_seq = q; h->free(b); }
    if (h->card_done) h->card_done();
}

static void release_buf(struct sgu *s)
{
    if (s->buf && s->kind == 2 && s->scratch) { unsigned i; volatile mw2_u8 *v = s->scratch; for (i = 0; i < NGP_WORK; i++) v[i] = 0; }      /* the work blocks held the online record */
    if (s->buf && s->h && s->h->free) s->h->free(s->buf);
    s->buf = 0; s->scratch = 0; s->buf_len = 0;
}

static void release_hold(struct sgu *s)
{
    if (s->hold_buf && s->h && s->h->free) s->h->free(s->hold_buf);
    s->hold_buf = 0; s->hold_len = 0;
}

static void to_failed(struct sgu *s, unsigned reason, unsigned code)
{
    sgp_free(s); release_buf(s); release_hold(s);           /* a held files part is dropped too: nothing of this release reaches the card */
    if (s->pad5 && s->chain_install_ok) { s->pad5 = 0; s->chain_install_ok = 0; s->latest = s->f_latest; s->same = 0; s->state = SGU_RESTART; s->code = 0; return; }       /* a part is already on the card: the failure of the NEXT part never takes that away, restart with what is installed */
    s->pad5 = 0;
    s->state = SGU_FAILED; s->reason = (unsigned char)reason; s->code = code;
}

void sgu_init(struct sgu *s, const struct sgu_host *h)
{
    zero(s, sizeof *s);
    s->h = h;
    s->state = SGU_IDLE;
    s->pad5 = 0; s->chain_install_ok = 0; s->comb = 0; s->f_have = 0; s->comb_chk = 0; s->auto_dl = 0; s->dl_retry = 0; s->f_latest = 0; s->f_size = 0; s->retry_at = 0; s->prog_tot = 0; s->prog_base = 0;      /* the chain/combined-offer/restart-countdown state starts clean (the overlay heap is not zeroed) */
    if (h && (h->size != (unsigned)sizeof(struct sgu_host) || h->net_size != (unsigned)sizeof(mnc) || !h->net)) s->h = 0;     /* a resident built from another layout: do not touch its structs */
    if (s->h) {
        s->installed = h->installed_seq; mnu_init(&s->u, h->net); s->dl_window = SGU_DL_WINDOW_MS;
        read_ovl_seq(s); s->boot_run = h->installed_seq > s->ovl_seq ? h->installed_seq : s->ovl_seq;
        /* the screen shows ONE release number from the first frame: the highest of engine and files, not only after a check */
        if (h->pending_seq && *h->pending_seq) { s->state = SGU_RESTART; s->latest = eff_seq(s); }      /* installed earlier in this boot: still waiting for the restart */
    }
}

/* Offline codes (s->code, lab Detail row): who said Offline */
#define SGU_OFF_LINK 91u      /* link_up false: no address, no server address or no setup */
#define SGU_OFF_CHECK 200u    /* the check was refused before sending: 200 + mnu error */

void sgu_close(struct sgu *s)
{
    if (s->state == SGU_CHECKING || s->state == SGU_DOWNLOADING) mnu_abort(&s->u);
    sgp_free(s); release_buf(s); release_hold(s);
    s->state = SGU_IDLE;                                    /* a finished install is remembered in *pending_seq, not here */
}

static void start_check_k(struct sgu *s, unsigned now, unsigned kind)
{
    const struct sgu_host *h = s->h;
    s->kind = (unsigned char)kind;
    s->reason = SGU_R_NONE; s->code = 0; s->hello_tried = 0; s->phase = 0; s->hash_retry = 0; s->same = 0; s->reinstall = 0; s->tier = 0; s->armed = 0;
    if (!h) { to_failed(s, SGU_R_NOHOST, 0); return; }
    if (!h->link_up || !h->link_up()) { s->state = SGU_OFFLINE; s->code = SGU_OFF_LINK; return; }
    if (h->net->state == MNC_BUSY) { to_failed(s, h->net->have_hello ? SGU_R_BUSY : SGU_R_UNREACH, 0); return; }      /* no session yet: the busy client is our own handshake against a server that does not answer, not 'something else' */
    s->state = SGU_CHECKING;
    if (kind == 2) read_net_seq(s);
    else if (kind == 1) read_ovl_seq(s);         /* the files check re-reads the header (the screen already read it once at open for the release number) */
    if (mw2_ready(&h->net->w, now)) {
        mnu_init(&s->u, h->net);
        { int rc = mnu_check(&s->u, now, ABI_OF(s), have_seq(s)); if (rc != 0) { s->state = SGU_OFFLINE; s->code = SGU_OFF_CHECK + (unsigned)rc % 10u; return; } }
        s->phase = 2;
    } else {
        if (!h->hello || !h->poll || h->hello(now) != 0) { to_failed(s, SGU_R_UNREACH, 0); return; }
        s->phase = 1; s->hello_tried = 1;                   /* a fresh handshake is under way: a failed check after it is a real Offline, no second one */
    }
}

static void start_check(struct sgu *s, unsigned now) { s->bplan = 0; s->bshort = 0; release_hold(s); s->pad5 = 0; s->dl_retry = 0; s->dl_pend = 0; s->bak = 0; s->net_leg = 1; s->st_try = 0; s->prog_tot = 0; s->prog_base = 0; s->chain_done = 1; s->comb = 0; s->f_have = 0; s->comb_chk = 0; s->auto_dl = 0; s->f_latest = 0; s->f_size = 0; s->retry_at = 0; start_check_k(s, now, 1); }

/* Which install fits this card (once per offer): staged swap, or in place on a small card behind a warning.  Run when the offer appears (the red small-card warning is then on the
   first screen together with the offer, owner finding MENU_SWEEP 4) and again at the download if the card was not ready then.  Sets s->tier (and s->armed: the warning is on
   screen, so the A that follows is the confirmation).  Returns the ota_plan tier; OTA_TIER_NOROOM leaves tier 0 (the download reports it). */
static int plan_tier(struct sgu *s)
{
    const struct sgu_host *h = s->h;
    int t = ota_plan(h->card, NM_ACTIVE(s), KEEPPREV(s) ? 0 : NM_PREV(s), NM_STAGE(s), s->size);       /* a reinstall keeps the fallback file, so it does not count as free room */
    if (t == OTA_TIER_INPLACE && KEEPPREV(s) && h->card->exists && h->card->exists(h->card->ctx, NM_PREV(s))) t = OTA_TIER_STAGED;     /* a reinstall rewrites the active file and keeps the fallback: no warning needed */
    if (h->card_done) h->card_done();
    if (t == OTA_TIER_NOROOM) return t;
    s->tier = (unsigned char)(t == OTA_TIER_INPLACE ? 2 : 1);
    if (s->tier == 2) s->armed = 1;
    return t;
}

/* No mnu_init here: the download must run on the mnu that did the check, because the check result (size, SHA-256 of the file, the FETCHU flag) lives in that struct and is
   what makes the download hash-checked and, in an anonymous session, fast (mnu.h).  An mnu_init would also clear prefer_signed, which the hash retry sets. */
static int session_failed(struct sgu *s, unsigned now, unsigned err);
static int start_retry(struct sgu *s, unsigned now, unsigned err, unsigned status);
/* the download is lost for good: "The connection was lost".  Detail for a bug report: error + 10 x handshakes made + 100 x the longest silence in seconds
   (e.g. 2554 = timeout, 5 retries, 25 s without a chunk) */
static void net_failed(struct sgu *s, unsigned err)
{
    unsigned gap = s->u.st_gap_ms / 1000u;
    s->dl_pend = 0;
    if (s->h && s->h->net) mw2_drop(&s->h->net->w);          /* a lost download leaves no session the server may have forgotten: Try again handshakes afresh, as re-entering the screen does */
    to_failed(s, SGU_R_NETWORK, err % 10u + 10u * (s->dl_retry > 9u ? 9u : s->dl_retry) + 100u * (gap > 99u ? 99u : gap));
}
static void start_download(struct sgu *s, unsigned now)
{
    const struct sgu_host *h = s->h;
    unsigned total;
    s->reason = SGU_R_NONE; s->code = 0;
    if (!h->card_ready || !h->card_ready()) { to_failed(s, SGU_R_NOCARD, 0); return; }
    if (s->size == 0 || s->size % OTA_BLOCK != 0 || s->size > SGU_MAX_FILE) { to_failed(s, SGU_R_MEMORY, MNU_E_TOO_BIG); return; }
    if (!s->tier && s->kind < 2 && !s->prog_tot) {      /* a combined release is planned as a whole (bundle_plan), not file by file */
        int t = plan_tier(s);
        if (t == OTA_TIER_NOROOM) { to_failed(s, SGU_R_FULL, 0); return; }
        if (s->tier == 2) { s->state = SGU_AVAILABLE; return; }      /* not planned at the offer (the card was busy then): the warning is shown now; the next A downloads */
    }
    total = s->size + (s->kind == 2 ? NGP_WORK : OTA_BLOCK);          /* the file, then the scratch (one block for the install's read-back; the network patch needs three blocks) */
    if (s->buf && s->buf_len == total && s->u.resumable) {          /* a stalled download goes on: same buffer, the received chunks kept (mnu_resume checks it is the same file, else starts over) */
        s->scratch = s->buf + s->size;
        s->code = (unsigned)mnu_resume(&s->u, now, s->latest, s->size, s->buf, s->size);
    } else {
        release_buf(s);
        s->buf = (mw2_u8 *)h->alloc(total);
        if (!s->buf) { to_failed(s, SGU_R_MEMORY, 0); return; }
        s->buf_len = total; s->scratch = s->buf + s->size;
        s->code = (unsigned)SGU_FETCH(&s->u, now, s->latest, s->size, s->buf, s->size);
    }
    if (s->code != 0) {
        if (session_failed(s, now, s->code)) return;
        if (s->code == MNU_E_TOO_BIG) { to_failed(s, SGU_R_MEMORY, s->code); return; }
        if (start_retry(s, now, s->code, 0u)) return;
        net_failed(s, s->code);
        return;
    }
    s->state = SGU_DOWNLOADING;
}

/* A downloaded file did not match the SHA-256 of the signed check (MNU_E_HASH).: ONE automatic retry, with a fresh signed check (the offer may
   have changed) and then the slow signed UPD_FETCH (prefer_signed: set on the mnu that the check and the fetch share, so nothing clears it).  The staging buffer holds
   untrusted bytes and is dropped; the second failure ends in SGU_R_HASH.  Either way nothing was written to the card (the installer never ran). */
static void hash_failed(struct sgu *s, unsigned now)
{
    const struct sgu_host *h = s->h;
    release_buf(s);
    if (s->hash_retry || !h->net || !h->link_up || !h->link_up()) { to_failed(s, SGU_R_HASH, MNU_E_HASH); return; }
    s->hash_retry = 1;
    s->reason = SGU_R_NONE; s->code = 0; s->hello_tried = 0;
    s->u.prefer_signed = 1;
    s->state = SGU_CHECKING;
    if (mw2_ready(&h->net->w, now)) {
        if (mnu_check(&s->u, now, ABI_OF(s), have_seq(s)) != 0) { to_failed(s, SGU_R_HASH, MNU_E_HASH); return; }
        s->phase = 2;
    } else {
        if (!h->hello || !h->poll || h->hello(now) != 0) { to_failed(s, SGU_R_HASH, MNU_E_HASH); return; }
        s->phase = 1;
    }
}

/* The stale-session retry: the check rode on a session that looked ready, but the server restarted and forgot it (SESSION_UNKNOWN, or silence).  ONE fresh
   handshake and check follows, after 800 to 1200 ms (1000 ms +-20%, the spread taken from the client nonce of the last handshake, which is random per console, mixed with the
   clock), so that a server restart does not bring every console back in the same instant. */
static unsigned retry_delay(const struct sgu *s, unsigned now)
{
    const mw2_u8 *cn = s->h->net->w.cn;
    unsigned x = (((unsigned)cn[0] << 8) | cn[1]) + now * 40503u;
    return 800u + ((x >> 4) % 41u) * 10u;
}

/* A download that failed on a dead session (cube after about two hours online the session the check rode on was gone, and Download failed at once,
   "The connection was lost", until the console was restarted) or stalled for MNU_STALL_MS: a fresh handshake and check, as the
   stale-session retry of the check, then the download goes on by itself, KEEPING the chunks already received (mnu_resume).  Rounds follow each other for SGU_DL_WINDOW_MS. */
static int session_failed(struct sgu *s, unsigned now, unsigned err)
{
    const struct sgu_host *h = s->h;
    if ((err != MNU_E_TIMEOUT && err != MNU_E_NOSESSION) || !h || !h->net || !h->hello || !h->poll || !h->link_up) return 0;      /* a link that is down is waited for in phase 3 */
    if (!s->dl_retry) s->dl_t0 = now;
    else if (now - s->dl_t0 >= s->dl_window || s->dl_retry >= SGU_DL_TRIES) return 0;
    mw2_drop(&h->net->w);
    s->dl_retry++; s->dl_pend = 1;
    s->reason = SGU_R_NONE; s->code = 0; s->hello_tried = 1;
    s->state = SGU_CHECKING; s->phase = 3; s->retry_at = now + retry_delay(s, now);
    return 1;
}


/* Start-class failures: the client is still busy with
   something, a reply that does not parse, a server that is busy or rate-limiting.  None of these says the update is impossible: up to SGU_ST_TRIES attempts per press / boot offer
   (the first one plus two retries) with a fresh handshake after 2 s and then 5 s, no key needed; the failure screen comes only after the last one and names the code. */
#define SGU_ST_TRIES 3u
static const unsigned short st_wait_ms[2] = { 2000u, 5000u };
static int st_transient(unsigned err, unsigned status)
{
    return err == MNU_E_BUSY || err == MNU_E_BAD_REPLY || (err == MNU_E_SERVER && (status == MNU_ST_RATE_LIMITED || status == (unsigned)MNC_ST_BUSY_10 || status == (unsigned)MNC_ST_BUSY_30 || status == (unsigned)MNC_ST_SERVER_FULL || status == (unsigned)MNC_ST_BUSY_120));      /* (no mnc_busy_hint: the overlay does not link mnc.c) */
}
/* a download start / its re-check failed transiently: a fresh handshake and check after the pause, then start_download again (dl_pend).  0 = no retry left or not transient */
static int start_retry(struct sgu *s, unsigned now, unsigned err, unsigned status)
{
    const struct sgu_host *h = s->h;
    if (!st_transient(err, status) || s->st_try >= SGU_ST_TRIES - 1u || !h || !h->net || !h->hello || !h->poll || !h->link_up) return 0;
    if (err == MNU_E_BUSY) mnu_abort(&s->u);                 /* a leftover busy state of the client: dropped (a fresh fetch follows) */
    if (!s->dl_retry && !s->dl_pend) s->dl_t0 = now;
    mw2_drop(&h->net->w);
    s->reason = SGU_R_NONE; s->code = 0; s->hello_tried = 1; s->dl_pend = 1;
    s->state = SGU_CHECKING; s->phase = 3; s->retry_at = now + st_wait_ms[s->st_try]; s->st_try++;
    return 1;
}
/* the same for a check (UPD_CHECK or the hello) that met a busy / rate-limiting server */
static int check_retry(struct sgu *s, unsigned now, unsigned status)
{
    const struct sgu_host *h = s->h;
    if (!st_transient(MNU_E_SERVER, status) || s->st_try >= SGU_ST_TRIES - 1u || !h || !h->net || !h->hello || !h->poll || !h->link_up || !h->link_up()) return 0;
    mw2_drop(&h->net->w);
    s->hello_tried = 1;
    s->state = SGU_CHECKING; s->phase = 3; s->retry_at = now + st_wait_ms[s->st_try]; s->st_try++;
    return 1;
}

static void check_done(struct sgu *s, unsigned now)
{
    mnu *u = &s->u;
    if (u->state == MNU_DONE) {
        if ((u->flags & 2u) && !(u->flags & 1u)) { if (s->kind) { s->state = SGU_UPTODATE; return; } to_failed(s, SGU_R_ABI, u->srv_abi); return; }    /* a server that knows no own-files kind: nothing to offer */
        if ((u->flags & 1u) && (s->kind == 2 ? u->latest > eff_seq(s) : u->latest >= eff_seq(s)) && u->size && (s->kind || !s->h->tried_seq || u->latest != s->h->tried_seq)) {
            s->latest = u->latest; s->size = u->size;
            s->same = (unsigned char)(s->kind != 2 && u->latest == eff_seq(s));
            s->reinstall = s->same;
            if (s->hash_retry || s->dl_pend) { s->dl_pend = 0; s->state = SGU_AVAILABLE; start_download(s, now); }      /* the retry goes straight on, no second press of A */
            else s->state = s->same ? SGU_UPTODATE : SGU_AVAILABLE;       /* the installed build itself is on offer: "up to date", X reinstalls it */
            if (s->kind == 2) s->tier = 1;
            if (s->state == SGU_AVAILABLE && s->kind < 2 && !s->hash_retry && s->size % OTA_BLOCK == 0 && s->size <= SGU_MAX_FILE && s->h->card_ready && s->h->card_ready())
                (void)plan_tier(s);                           /* a small card: the red warning and 'install anyway' are on this very screen, not behind a second A (MENU_SWEEP 4) */
        } else if (s->hash_retry) to_failed(s, SGU_R_HASH, MNU_E_HASH);
        else s->state = SGU_UPTODATE;
        return;
    }
    if (s->hash_retry) { to_failed(s, SGU_R_HASH, MNU_E_HASH); return; }
    if (s->dl_pend) {                                       /* the check of a download retry failed: another round while there are tries left, else the download failed */
        s->dl_pend = 0;
        if (session_failed(s, now, (unsigned)u->err)) return;
        if (start_retry(s, now, (unsigned)u->err, (unsigned)u->status)) return;
        net_failed(s, (unsigned)u->err); return;
    }
    if ((u->err == MNU_E_TIMEOUT || u->err == MNU_E_NOSESSION) && !s->hello_tried && s->h->hello && s->h->poll && s->h->link_up && s->h->link_up()) {
        mw2_drop(&s->h->net->w);                            /* the old session is dead: the hello must start a new handshake, not seal a request on it */
        s->hello_tried = 1; s->phase = 3; s->retry_at = now + retry_delay(s, now);
        return;
    }
    if (u->err == MNU_E_SERVER && check_retry(s, now, (unsigned)u->status)) return;
    if (u->err == MNU_E_SERVER) to_failed(s, u->status == 18 ? SGU_R_NOFILES : SGU_R_SERVER, (unsigned)u->status);     /* 18 = NOT_SUPPORTED: the server has no update directory */
    else if (u->err == MNU_E_TIMEOUT || u->err == MNU_E_NOSESSION) { s->state = SGU_OFFLINE; s->code = (unsigned)u->err; }
    else to_failed(s, SGU_R_SERVER, (unsigned)u->err);
}

static void policy(const struct sgu *s, struct ota_policy *p)
{
    const struct sgu_host *h = s->h;
    unsigned lo = eff_seq(s);                           /* newer OR EQUAL: an equal signed build is a reinstall, an older one is a rollback and stays refused */
    p->pubs = h->pubs; p->npub = h->npub; p->abi = ABI_OF(s);
    p->min_seq = s->kind == 2 ? lo + 1u : (!s->kind && h->floor_seq > lo) ? h->floor_seq : lo;       /* a network patch must be strictly newer */
    p->allow_unsigned = h->allow_unsigned;
}

static void do_verify(struct sgu *s)
{
    struct ota_policy pol;
    ota_u32 seq = 0;
    int r;
    policy(s, &pol);
    r = ota_verify(s->buf, s->size, &pol, &seq);
    if (r != OTA_OK) { to_failed(s, SGU_R_BADFILE, (unsigned)r); return; }
    s->latest = seq;
    s->state = SGU_INSTALLING; s->settle = SGU_SETTLE;
}

static void start_check_k(struct sgu *s, unsigned now, unsigned kind);
static void held_alone(struct sgu *s, unsigned now);

static unsigned fail_reason(int r) { return r == OTA_E_HALF ? SGU_R_HALF : r == OTA_E_LOST ? SGU_R_LOST : r == OTA_E_SPACE ? SGU_R_FULL : r == OTA_E_VERIFY ? SGU_R_CARDBITS : r == OTA_E_IO ? SGU_R_WRITE : SGU_R_BADFILE; }

/* "Keep previous build as backup" is OFF: the install just verified and swapped in the new file, so the file it replaced (the B copy) is deleted again and no boot record is armed for it
   (nothing to fall back to: a build that cannot start leaves plain Melee, never a broken card).  Never for a reinstall or while running as the fallback. */
static int keep_off(const struct sgu *s) { return s->h && s->h->keep_prev && !s->h->keep_prev(); }
static void drop_prev(const struct sgu *s, const char *a, const char *b)
{
    const struct ota_card_ops *c = s->h->card;
    if (!c->del || !c->exists) return;
    if (a && c->exists(c->ctx, a)) (void)c->del(c->ctx, a);
    if (b && c->exists(c->ctx, b)) (void)c->del(c->ctx, b);
}

/* one verified file of kind s->kind from s->buf onto the card (staged swap, in place, or a reinstall that keeps the fallback); the card is unmounted after it */
static int install_one(struct sgu *s)
{
    int r;
    if (s->latest == eff_seq(s) || KEEPPREV(s)) r = ota_reinstall_verified(s->h->card, NM_ACTIVE(s), NM_PREV(s), NM_STAGE(s), s->buf, s->size, s->scratch);        /* same build: the fallback stays as it is */
    else if (s->tier == 2) r = ota_install_inplace_verified(s->h->card, NM_ACTIVE(s), NM_PREV(s), NM_STAGE(s), s->buf, s->size, s->scratch);
    else r = ota_install_verified(s->h->card, NM_ACTIVE(s), NM_PREV(s), NM_STAGE(s), s->buf, s->size, s->scratch);
    if (r == OTA_OK && s->kind < 2 && !KEEPPREV(s) && s->latest != eff_seq(s) && keep_off(s)) drop_prev(s, NM_PREV(s), 0);
    if (r == OTA_OK && !s->kind && s->h->card->exists && s->h->card->exists(s->h->card->ctx, NAME_PREV))
        (void)bootrec_arm(s->h->card, NAME_STATE, s->scratch, s->latest);        /* the new build is a candidate with 0 tries; only worth a card block when a fallback exists; best effort (the engine also counts and self-heals) */
    if (s->h->card_done) s->h->card_done();                 /* the card is unmounted before anything else happens, whatever the outcome */
    if (r == OTA_OK) {
        if (s->h->pending_seq && !s->kind) *s->h->pending_seq = s->latest;
        else if (s->kind == 1) s->ovl_seq = s->latest;
    }
    return r;
}

/* the held files part onto the card, with the engine's own fields put aside and back; the held buffer is freed whatever the outcome */
static int install_held(struct sgu *s)
{
    mw2_u8 *b = s->buf, *sc = s->scratch;
    unsigned bl = s->buf_len, sz = s->size, la = s->latest;
    unsigned char k = s->kind, ti = s->tier, sa = s->same;
    int r;
    s->kind = 1; s->buf = s->hold_buf; s->buf_len = s->hold_len; s->size = s->hold_size; s->latest = s->hold_latest; s->tier = s->hold_tier; s->same = s->hold_same;
    s->scratch = s->buf + s->size;
    r = install_one(s);
    s->buf = b; s->scratch = sc; s->buf_len = bl; s->size = sz; s->latest = la; s->kind = k; s->tier = ti; s->same = sa;
    release_hold(s);
    return r;
}

/* ---- the combined release as ONE unit: engine + files go on the card through ota_bundle_*; the plan is made before anything is written ---- */
static void bundle_fill(const struct sgu *s, struct ota_bundle *b, unsigned code_n, unsigned ovl_n)
{
    b->code = NAME_ACTIVE; b->code_prev = NAME_PREV; b->code_stage = NAME_STAGE;
    b->ovl = OVL_ACTIVE; b->ovl_prev = OVL_PREV; b->ovl_stage = OVL_STAGE;
    b->code_n = code_n; b->ovl_n = ovl_n;
    b->keep_code_prev = s->h && s->h->from_prev ? 1 : 0;
}

/* the plan for a combined release of these sizes (0 when the card is busy: not known yet) */
static int bundle_plan(struct sgu *s, unsigned code_n, unsigned ovl_n)
{
    const struct sgu_host *h = s->h;
    struct ota_bundle b; struct ota_bplan p;
    int t;
    s->bplan = 0; s->bshort = 0;
    if (!h->card_ready || !h->card_ready()) return 0;
    bundle_fill(s, &b, code_n, ovl_n);
    t = ota_bundle_plan(h->card, &b, &p);
    if (h->card_done) h->card_done();
    s->bplan = (unsigned char)t; s->bshort = t == OTA_PLAN_C ? p.shortfall : 0u;
    return t;
}

/* both verified files onto the card: plan A (staged beside the old ones, four directory operations) or plan B (small card: the old files make the room), else refused with nothing written.
   Plan B reads the old files copy into RAM first when the heap allows, so that a write error with the power on puts the old pair back. */
static int install_bundle(struct sgu *s)
{
    const struct sgu_host *h = s->h;
    struct ota_bundle b; struct ota_bplan p;
    mw2_u8 *rb = 0;
    unsigned rcap = 0;
    int plan, r;
    bundle_fill(s, &b, s->size, s->hold_size);
    plan = ota_bundle_plan(h->card, &b, &p);
    s->bplan = (unsigned char)plan; s->bshort = plan == OTA_PLAN_C ? p.shortfall : 0u;
    if (plan == OTA_PLAN_C) { if (h->card_done) h->card_done(); release_hold(s); return OTA_E_SPACE; }
    if (plan == OTA_PLAN_B && p.ovl_old) { rcap = p.ovl_old * OTA_BLOCK; rb = (mw2_u8 *)h->alloc(rcap); if (!rb) rcap = 0; }
    r = ota_bundle_install(h->card, &b, plan, s->buf, s->hold_buf, s->scratch, rb, rcap);
    if (rb) h->free(rb);
    if (r == OTA_OK && !b.keep_code_prev && keep_off(s)) drop_prev(s, NAME_PREV, OVL_PREV);
    if (r == OTA_OK && h->card->exists && h->card->exists(h->card->ctx, NAME_PREV))
        (void)bootrec_arm(h->card, NAME_STATE, s->scratch, s->latest);        /* a candidate with 0 tries; without a spare engine (nothing to fall back to) no record is made */
    if (h->card_done) h->card_done();
    if (r == OTA_OK) { if (h->pending_seq) *h->pending_seq = s->latest; s->ovl_seq = s->hold_latest; }
    else if (r == OTA_E_HALF) s->ovl_seq = s->hold_latest;                  /* the new files are on the card: the next check must not offer them again */
    release_hold(s);
    return r;
}

/* the system files (kind 3): the build runs one slice per frame, the write is the last slice.  Nothing but MultishinePack is touched on the card (and it only by the final rename). */
static void pk_install(struct sgu *s)
{
    unsigned pct = s->sysf_pad1, code = 0;
    int r;
    if (!s->pk) {
        r = sgp_begin(s);
        if (r) { to_failed(s, (unsigned)r, 0); return; }
        s->sysf_pad1 = 0;
        return;
    }
    r = sgp_step(s, &pct, &code);
    s->sysf_pad1 = (unsigned char)(pct > 100u ? 100u : pct);
    if (r == 0) return;
    if (r == 1) { sgp_free(s); release_buf(s); s->state = SGU_DONE; s->code = 0; s->reason = SGU_R_NONE; return; }
    to_failed(s, (unsigned)r, code);
}

/* the automatic set-up: check the pack-data kind, download it as soon as it is offered, verify, build, write.  Used by the Update screen when the glue opens it for the system files. */
static void start_sysfiles(struct sgu *s, unsigned now)
{
    sgp_free(s);
    s->bplan = 0; s->bshort = 0; release_hold(s); s->pad5 = 0; s->dl_retry = 0; s->dl_pend = 0; s->bak = 0; s->net_leg = 0; s->prog_tot = 0; s->prog_base = 0; s->chain_done = 0; s->comb = 0;
    s->f_have = 0; s->comb_chk = 0; s->f_latest = 0; s->f_size = 0; s->retry_at = 0; s->sysf = 1; s->sysf_pad1 = 0; s->auto_dl = 1;
    start_check_k(s, now, 3);
}

static void do_install(struct sgu *s, unsigned now)
{
    int r;
    if (!s->h->card_ready || !s->h->card_ready()) { to_failed(s, SGU_R_NOCARD, 0); return; }
    if (s->kind != 2 && s->bak != SGU_BAK_DONE && s->h->backup) {         /* the whole card to slot B first, once per release (a combined release installs two parts); the result never stops the install */
        s->bak = (unsigned char)s->h->backup();
        if (s->bak == SGU_BAK_NONE) s->bak = SGU_BAK_FAIL;
        if (!s->h->card_ready || !s->h->card_ready()) { to_failed(s, SGU_R_NOCARD, 0); return; }     /* the backup unmounted the card: the installer needs it ready again */
    }
    if (s->kind == 2) {                                     /* network patch: read-modify-write of the allow-listed fields only (sgu_net.c); the container's payload is block 1 of the file */
        unsigned plen = ((unsigned)s->buf[OTA_BLOCK + 6] << 8) | s->buf[OTA_BLOCK + 7];
        r = ngp_apply(s->h->card, s->buf + OTA_BLOCK, plen, s->latest, s->scratch);
        if (s->h->card_done) s->h->card_done();
        if (r == NGP_OK) { s->net_seq = s->latest; release_buf(s); s->state = SGU_RESTART; s->code = 0; s->same = 0; return; }
        to_failed(s, r == NGP_E_NOREC || r == NGP_E_NOBRIDGE ? SGU_R_NONET : r == NGP_E_IO || r == NGP_E_NOROOM ? SGU_R_WRITE : r == NGP_E_VERIFY ? SGU_R_CARDBITS : SGU_R_BADFILE, 100u + (unsigned)r);
        return;
    }
    if (s->kind == 1 && !s->pad5) {                        /* the files part is verified: hold it, check (and fetch) the engine, write nothing yet */
        if (s->h->link_up && s->h->link_up()) {
            s->hold_buf = s->buf; s->hold_len = s->buf_len; s->hold_size = s->size; s->hold_latest = s->latest; s->hold_tier = s->tier; s->hold_same = s->same;
            s->buf = 0; s->scratch = 0; s->buf_len = 0;
            if (s->prog_tot) s->prog_base = s->size;         /* the bar goes on from here: the files part is in */
            s->pad5 = 1; s->chain_install_ok = 0; s->f_latest = s->latest;
            start_check_k(s, now, 0);                       /* a check that ends at once (offline) is handled by chain_after_check, this frame */
            return;
        }
        if (s->prog_tot) { to_failed(s, SGU_R_NETWORK, MNU_E_TIMEOUT); return; }     /* a two-part release with the link gone: nothing is written */
    }
    if (s->hold_buf && !s->kind && s->latest != eff_seq(s) && s->hold_latest != s->ovl_seq && !s->hold_same && !s->same) {      /* a new engine AND new files: ONE unit, planned before the first write */
        r = install_bundle(s);
        if (r == OTA_OK) { release_buf(s); s->state = SGU_RESTART; s->code = 0; s->pad5 = 0; s->chain_install_ok = 0; return; }
        to_failed(s, fail_reason(r), r == OTA_E_SPACE && s->bplan == OTA_PLAN_C ? s->bshort : (unsigned)r);
        return;
    }
    if (s->hold_buf && !s->kind) {                          /* the engine is verified: the held files go on the card first, then the engine */
        r = install_held(s);
        if (r != OTA_OK) { to_failed(s, fail_reason(r), (unsigned)r); return; }
        s->chain_install_ok = 1;                            /* from here a failure of the engine write restarts on what is installed (to_failed) */
    }
    r = install_one(s);
    if (r == OTA_OK) {
        release_buf(s); s->state = SGU_RESTART; s->code = 0;
        s->pad5 = 0; s->chain_install_ok = 0;
        return;
    }
    to_failed(s, fail_reason(r), (unsigned)r);
}

/* the engine is up to date (a files-only release), or a small card wants its own confirmation for the engine: the held files are written alone */
static void held_alone(struct sgu *s, unsigned now)
{
    int r;
    (void)now;
    if (!s->hold_buf) return;
    if (!s->h->card_ready || !s->h->card_ready()) { to_failed(s, SGU_R_NOCARD, 0); return; }
    r = install_held(s);
    if (r != OTA_OK) { to_failed(s, fail_reason(r), (unsigned)r); return; }
    s->pad5 = 0; s->chain_install_ok = 0; s->latest = s->f_latest; s->same = 0; s->state = SGU_RESTART; s->code = 0;
}

/* A check looks at OUR OWN files first (MultishineOvl): an offer there is shown at once; "up to date" (or a server that knows no such kind) goes on to the engine's check, whose result
   then stands (the download of the engine file needs the check result of the engine, so it runs last).  An Offline / Failed result of the first leg is final: no second timeout. */
static void chain_after_check(struct sgu *s, unsigned now)
{
    if (s->state == SGU_CHECKING || !s->h) return;
    if (s->pad5) {                                          /* the chained engine check after a files install */
        if (s->state == SGU_AVAILABLE && !s->armed) { s->hash_retry = 0; start_download(s, now); return; }
        if (s->state == SGU_AVAILABLE) return;             /* a small card needs its own confirmation: the offer is shown, the files stay held; the A that follows installs both */
        if (s->state == SGU_UPTODATE) { held_alone(s, now); return; }      /* a files-only release */
        if (s->state == SGU_OFFLINE || s->state == SGU_FAILED) {             /* the engine could not be checked */
            if (s->prog_tot) { unsigned c = s->code; to_failed(s, SGU_R_NETWORK, c); return; }    /* a two-part release: nothing is written */
            held_alone(s, now); return;                     /* the offer was the files alone: write them */
        }
    }
    if (s->comb_chk == 1 && !s->pad5) {                    /* the engine check of a combined offer came back */
        if (s->state == SGU_AVAILABLE) { s->comb_chk = 0; s->comb = 1; (void)bundle_plan(s, s->size, s->f_size); return; }       /* both parts offered: ONE offer on screen, highest release, summed size; the card is asked now whether both fit */
        if (s->state == SGU_UPTODATE || s->state == SGU_FAILED || s->state == SGU_OFFLINE) { s->comb_chk = 2; start_check_k(s, now, 1); return; }    /* only the files part: check it again so its download state is current, then show it as usual */
    }
    if (s->comb_chk == 2 && s->state != SGU_CHECKING) { s->comb_chk = 0; }
    /* The network patch is the LAST leg of a check (standalone offer, 'Network settings'): it runs when the files and the engine are up to date, so it never disturbs the release offer,
       the combined offer or the chained install.  Its result is only shown when a patch is on offer; otherwise (up to date, an old server, offline, failed) the engine's result is put back. */
    if (s->net_leg == 2 && s->state != SGU_CHECKING && s->kind == 2) {
        s->net_leg = 0;
        if (s->state != SGU_AVAILABLE) { s->kind = 0; s->same = s->c_same; s->latest = s->c_latest; s->size = s->c_size; s->state = SGU_UPTODATE; s->reason = SGU_R_NONE; s->code = 0; s->u = s->u_eng; }
        return;
    }
    if (s->net_leg == 1 && !s->kind && s->state == SGU_UPTODATE && !s->pad5 && !s->comb_chk && !s->hash_retry && s->h->link_up && s->h->link_up()) {
        s->net_leg = 2; s->c_same = s->same; s->c_latest = s->latest; s->c_size = s->size; s->u_eng = s->u;
        start_check_k(s, now, 2);
        if (s->state != SGU_CHECKING) { s->state = SGU_UPTODATE; s->kind = 0; s->same = s->c_same; s->net_leg = 0; s->u = s->u_eng; }
        return;
    }
    if (s->auto_dl && !s->pad5 && s->kind && s->state == SGU_AVAILABLE && !s->armed) { s->auto_dl = 0; s->hash_retry = 0; start_download(s, now); return; }
    if (s->kind && s->chain_done && s->state == SGU_AVAILABLE && !s->comb_chk && !s->comb && !s->pad5 && !s->auto_dl && !s->f_have) {      /* the files part is offered: look at the engine too before showing anything */
        s->f_have = 1; s->f_latest = s->latest; s->f_size = s->size; s->comb_chk = 1;
        start_check_k(s, now, 0);
        return;
    }
    if (s->kind && s->chain_done && s->state == SGU_UPTODATE && !s->hash_retry) {
        s->chain_done = 0;
        start_check_k(s, now, 0);
    }
}

void sgu_frame(struct sgu *s, struct sgu_view *v, unsigned now)
{
    unsigned req = v->req;
    v->req = 0;
    /* FORCED UPDATE: the screen was opened because the server's minimum build is above ours: the check starts by itself, once per open (the player then sees
       'Update required ... A Update now'); no network or a failed check leaves the usual states, and the gate in ms_offline_server keeps refusing online modes meanwhile */
    if (s->state == SGU_IDLE && !s->forced_try && s->h && s->h->adv && s->kind == 0 && updreq_required(s->h->adv, eff_seq(s))) { s->forced_try = 1; start_check(s, now); }
    /* ---- requests: only the ones that fit the state do anything ---- */
    if (req & SGU_REQ_CANCEL) {
        if (s->state == SGU_CHECKING) { mnu_abort(&s->u); if (s->dl_pend) { s->dl_pend = 0; release_buf(s); } s->state = SGU_IDLE; s->reason = SGU_R_NONE; s->net_leg = 0; }
        else if (s->state == SGU_DOWNLOADING) { mnu_abort(&s->u); release_buf(s); s->state = SGU_AVAILABLE; s->reason = SGU_R_NONE; }      /* the staging buffer is dropped; the offer stands */
    } else if (req & SGU_REQ_SYSFILES) {
        if (s->state == SGU_IDLE || s->state == SGU_UPTODATE || s->state == SGU_FAILED || s->state == SGU_OFFLINE) start_sysfiles(s, now);
    } else if ((req & SGU_REQ_CHECK) && s->sysf) {
        if (s->state == SGU_IDLE || s->state == SGU_UPTODATE || s->state == SGU_FAILED || s->state == SGU_OFFLINE) start_sysfiles(s, now);
    } else if (req & SGU_REQ_COPYB) {
        if ((s->state == SGU_IDLE || s->state == SGU_UPTODATE || s->state == SGU_FAILED || s->state == SGU_OFFLINE) && s->cpy != SGU_CPY_WORK) {
            if (s->h && s->h->copy_b) { s->cpy = SGU_CPY_WORK; s->cpy_wait = 2; }      /* two frames of 'Copying...' on screen first: the copy blocks */
            else s->cpy = SGU_BAK_FAIL;
        }
    } else if (req & SGU_REQ_CHECK) {
        if (s->state == SGU_IDLE || s->state == SGU_UPTODATE || s->state == SGU_FAILED || s->state == SGU_OFFLINE) start_check(s, now);
    } else if (req & SGU_REQ_DOWNLOAD) {
        if (s->state == SGU_AVAILABLE && s->comb && bundle_plan(s, s->size, s->f_size) == OTA_PLAN_C) { to_failed(s, SGU_R_FULL, s->bshort); s->comb = 0; }      /* does not fit even on the small-card plan: nothing is downloaded, nothing is written */
        else if (s->state == SGU_AVAILABLE && s->comb) { s->comb = 0; s->auto_dl = 1; s->chain_done = 0; s->prog_tot = s->size + s->f_size; s->prog_base = 0; start_check_k(s, now, 1); }      /* one A installs the whole release: files first, the engine chained after them */
        else if (s->state == SGU_AVAILABLE) { s->hash_retry = 0; s->st_try = 0; start_download(s, now); }
    } else if (req & SGU_REQ_REINSTALL) {
        if (s->state == SGU_UPTODATE && s->same) { s->reinstall = 1; s->hash_retry = 0; s->state = SGU_AVAILABLE; start_download(s, now); }
    } else if (req & SGU_REQ_RESTART) {
        if (s->state == SGU_RESTART && s->h && s->h->reset) { s->reset_calls++; s->h->reset(); }
    }
    /* ---- the player's own copy to slot B: after its 'Copying...' frames, one blocking call; nothing else is running in these states ---- */
    if (s->cpy == SGU_CPY_WORK) {
        if (s->cpy_wait) s->cpy_wait--;
        else if (s->state == SGU_IDLE || s->state == SGU_UPTODATE || s->state == SGU_FAILED || s->state == SGU_OFFLINE) s->cpy = (unsigned char)s->h->copy_b();
        else s->cpy = 0;
    }
    /* ---- RESTART: the new build is on the card, so the only way on is the restart: automatic after SGU_RESTART_MS, A restarts at once  ---- */
    if (s->state == SGU_RESTART && s->h && s->h->reset) {
        if (!s->retry_at) s->retry_at = (now + SGU_RESTART_MS) | 1u;
        else if ((int)(now - s->retry_at) >= 0) { s->reset_calls++; s->h->reset(); }
    }
    /* ---- one step of the running state ---- */
    switch (s->state) {
    case SGU_CHECKING:
        if (s->phase == 1) {                                  /* wire hello */
            mnc *c = s->h->net;
            (void)s->h->poll(now);
            if (c->state == MNC_DONE) {
                if (!s->hash_retry && !s->dl_pend) mnu_init(&s->u, c);      /* a hash retry keeps prefer_signed; a download retry keeps the received chunks */
                { int rc = mnu_check(&s->u, now, ABI_OF(s), have_seq(s)); if (rc != 0) { s->state = SGU_OFFLINE; s->code = SGU_OFF_CHECK + (unsigned)rc % 10u; } else s->phase = 2; }
            } else if (c->state == MNC_FAILED) {
                if (s->hash_retry) to_failed(s, SGU_R_HASH, MNU_E_HASH);
                else if (s->dl_pend) { s->dl_pend = 0; if (!session_failed(s, now, MNU_E_TIMEOUT)) net_failed(s, MNU_E_TIMEOUT); }     /* no handshake yet: the link is still down */
                else if (c->err == MNC_E_SERVER) { if (!check_retry(s, now, (unsigned)c->status)) to_failed(s, SGU_R_SERVER, (unsigned)c->status); }
                else { s->state = SGU_OFFLINE; s->code = (unsigned)c->err; }
            }
        } else if (s->phase == 2) {
            if (mnu_poll(&s->u, now) != MNU_BUSY) check_done(s, now);
        } else if (s->phase == 3) {                          /* waiting out the jittered pause before the one retry */
            if ((int)(now - s->retry_at) >= 0 && s->dl_pend && !s->h->link_up()) {      /* a download retry with the cable out: wait for the link, a second at a time, within the window */
                if (now - s->dl_t0 >= s->dl_window) net_failed(s, MNU_E_TIMEOUT);
                else s->retry_at = now + 1000u;
            } else if ((int)(now - s->retry_at) >= 0) {
                if (s->h->hello(now) != 0) { if (s->dl_pend) { s->dl_pend = 0; if (!session_failed(s, now, MNU_E_TIMEOUT)) net_failed(s, MNU_E_TIMEOUT); } else s->state = SGU_OFFLINE; }
                else s->phase = 1;
            }
        }
        break;
    case SGU_DOWNLOADING:
        if (mnu_poll(&s->u, now) == MNU_BUSY) break;
        if (s->u.state == MNU_DONE) { s->state = SGU_VERIFYING; s->settle = SGU_SETTLE; s->dl_retry = 0; }      /* the next file of a chain gets its own retry */
        else if (s->u.err == MNU_E_HASH) hash_failed(s, now);
        else if (s->u.err == MNU_E_SERVER) { if (!start_retry(s, now, MNU_E_SERVER, (unsigned)s->u.status)) to_failed(s, SGU_R_SERVER, (unsigned)s->u.status); }
        else if (session_failed(s, now, (unsigned)s->u.err)) {}
        else if (start_retry(s, now, (unsigned)s->u.err, (unsigned)s->u.status)) {}
        else if (s->u.err == MNU_E_TOO_BIG) to_failed(s, SGU_R_MEMORY, (unsigned)s->u.err);
        else net_failed(s, (unsigned)s->u.err);
        break;
    case SGU_VERIFYING:
        if (s->settle) s->settle--; else do_verify(s);
        break;
    case SGU_INSTALLING:
        if (s->settle) s->settle--;
        else if (s->kind == 3) pk_install(s);
        else do_install(s, now);
        break;
    default: break;
    }
    chain_after_check(s, now);
    if (s->kind == 3 && s->state == SGU_UPTODATE) to_failed(s, SGU_R_NOFILES, 0);          /* the server publishes no system-files data (or is too old to know the kind): the set-up failed, the fallback text says so */
    /* ---- the view ---- */
    v->kind = s->kind; v->state = s->state; v->reason = s->reason; v->installed = s->kind == 2 ? s->net_seq : s->kind ? s->ovl_seq : (s->h ? s->h->installed_seq : 0u); v->other = s->kind == 2 ? 0u : s->kind ? (s->h ? s->h->installed_seq : 0u) : s->ovl_seq; v->latest = s->latest; v->code = s->code;
    if (s->state == SGU_RESTART && s->f_latest > v->latest) v->latest = s->f_latest;      /* files then engine: the restart screen names the highest release installed */
    v->run_rel = s->boot_run;
    v->restart_s = 255u;
    if (s->state == SGU_RESTART && s->h && s->h->reset) {
        int left = s->retry_at ? (int)(s->retry_at - now) : (int)SGU_RESTART_MS;
        if (left < 0) left = 0;
        v->restart_s = (unsigned char)((left + 999) / 1000);
    }
    v->size_kb = (s->size + 1023u) / 1024u;
    if (s->comb && s->state == SGU_AVAILABLE) {                  /* ONE release to the player: the highest of the two parts, the sizes added */
        v->latest = s->latest > s->f_latest ? s->latest : s->f_latest;
        v->size_kb = (s->size + s->f_size + 1023u) / 1024u;
    }
    v->backup = s->bak; v->copyb = s->cpy;
    v->noreset = (unsigned char)(s->h && !s->h->reset);
    v->tier = s->tier; v->armed = s->armed;
    if (s->bplan == OTA_PLAN_B && (s->state == SGU_AVAILABLE ? s->comb : s->state == SGU_INSTALLING)) v->tier = 3;      /* combined release on a small card: one plain line */
    else if (s->bplan == OTA_PLAN_C && s->state == SGU_AVAILABLE && s->comb) { v->tier = 4; v->code = s->bshort; }
    v->same = (unsigned char)(s->same || (s->kind != 2 && s->state == SGU_RESTART && s->h && s->latest == s->h->installed_seq));
    v->req_min = 0; v->note_ours = SGU_NOTE_NONE; v->note_slippi = 0; v->slippi_new[0] = v->slippi_new[1] = v->slippi_new[2] = 0;
    if (s->h && s->h->adv && s->h->adv->valid) {
        const struct sgu_adv *a = s->h->adv;
        unsigned eff = eff_seq(s);
        v->note_ours = (unsigned char)(a->ours_min > eff ? SGU_NOTE_TOO_OLD : a->ours_latest > eff ? SGU_NOTE_NEWER : SGU_NOTE_NONE);
        if (v->note_ours == SGU_NOTE_TOO_OLD) v->req_min = a->ours_min;
        v->note_slippi = a->slippi_state > 2u ? 0u : a->slippi_state;
        v->slippi_new[0] = a->slippi_new[0]; v->slippi_new[1] = a->slippi_new[1]; v->slippi_new[2] = a->slippi_new[2];
    }
    if (s->state == SGU_CHECKING && s->dl_pend && s->u.nchunks) {     /* a download retry (handshake + check) is under way: the screen stays on "Downloading" at its percentage */
        v->state = SGU_DOWNLOADING;
        if (s->prog_tot) { unsigned part = s->size / 1000u * ((s->u.got * 1000u) / s->u.nchunks), pc = (s->prog_base + part) / (s->prog_tot / 100u + 1u); v->pct = (unsigned char)(pc > 99u ? 99u : pc); }
        else v->pct = (unsigned char)((s->u.got * 100u) / s->u.nchunks);
    } else
    if (s->state == SGU_CHECKING && s->prog_tot) {            /* the check between the two parts of a combined release is not shown: the screen stays on "Downloading" with the one bar */
        unsigned pc = s->prog_base / (s->prog_tot / 100u + 1u);
        v->state = SGU_DOWNLOADING; v->pct = (unsigned char)(pc > 99u ? 99u : pc);
    } else
    if (s->state == SGU_DOWNLOADING && s->u.nchunks) {
        if (s->prog_tot) {                  /* one bar for the whole release: bytes of the finished part plus the share of this part */
            unsigned part = s->size / 1000u * ((s->u.got * 1000u) / s->u.nchunks);
            unsigned pc = (s->prog_base + part) / (s->prog_tot / 100u + 1u);
            v->pct = (unsigned char)(pc > 99u ? 99u : pc);
        } else v->pct = (unsigned char)((s->u.got * 100u) / s->u.nchunks);
    }
    else if ((s->state == SGU_VERIFYING || s->state == SGU_INSTALLING) && s->prog_tot && !s->pad5) {      /* a part of a combined release is being checked / written: the ONE bar stays on the screen, at the end of this part */
        unsigned pc = (s->prog_base + s->size) / (s->prog_tot / 100u + 1u);
        v->pct = (unsigned char)(pc > 99u ? 99u : pc);
    }
    else if (s->state == SGU_VERIFYING || s->state == SGU_INSTALLING || s->state == SGU_RESTART) v->pct = 100;
    else v->pct = 0;
    if (s->kind == 3 && s->state == SGU_INSTALLING) { v->pct = s->sysf_pad1; v->tier = 0; v->armed = 0; }       /* the system files: the build's own bar */
}
