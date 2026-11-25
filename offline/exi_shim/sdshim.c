/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sdshim: console-side Slippi EXI command router.  See sdshim.h and.
 * Project-owned C89.  Wire layouts are cited as Online/Online.s:<line> etc.
 * (all paths relative to third_party/slippi-ssbm-asm/) or E:<line> for
 * .
 */
#include "sdshim.h"
/* LOCAL FORK: the game recorder (rec/rec_gc.c) takes the recording stream; absent (host tests, other images) = dropped as before */
extern void sdshim_record_hook(const sdshim_u8 *p, sdshim_u32 n) __attribute__((weak));

#define SDSHIM_MAGIC 0x53445348u /* "SDSH" */

#ifndef SDSHIM_NO_NAMES
#define E(c, o, rq, rs, n) { c, o, rq, rs, n }
#else
#define E(c, o, rq, rs, n) { c, o, rq, rs }
#endif
#define VAR 0xFFFF
#define L SDSHIM_OWN_LOCAL
#define B SDSHIM_OWN_BACKEND
#define U SDSHIM_OWN_UNSUPPORTED

/* Request/response sizes: callers cited per row.  B0 first (hot path). */
static const struct sdshim_cmd_info g_cmds[] = {
    E(0xB0, B, SDSHIM_TXB_SIZE, SDSHIM_RXB_SIZE, "ONLINE_INPUTS"),          /* TriggerSendInput.asm:176,208,214 */
    E(0xB1, U, 33, 0, "CAPTURE_SAVESTATE"),        /* Static/SaveState.asm:57-66 */
    E(0xB2, U, 33, 0, "LOAD_SAVESTATE"),           /* Static/LoadState.asm:115-124 */
    E(0xB3, B, 1, SDSHIM_MSRB_SIZE, "GET_MATCH_STATE"),         /* Static/LoadMatchState.asm:33-48 */
    E(0xB4, B, 20, 0, "FIND_OPPONENT"),            /* Menus/CSS/HandleInputsOnCSS.asm:374 */
    E(0xB5, B, 10, 0, "SET_MATCH_SELECTIONS"),     /* HandleInputsOnCSS.asm:479 */
    E(0xB6, B, 1, 0, "OPEN_LOGIN"),                /* Menus/TitleMenu/OnMenuPrep.asm:339,377 */
    E(0xB7, B, 1, 0, "LOGOUT"),                    /* OnMenuPrep.asm:818-823 */
    E(0xB8, B, 1, 0, "UPDATE_APP"),                /* OnMenuPrep.asm:357,377 */
    E(0xB9, B, 1, 42, "GET_ONLINE_STATUS"),        /* Static/UserDisplayFunctions.asm:414-427 */
    E(0xBA, B, 1, 0, "CLEANUP_CONNECTION"),        /* HandleInputsOnCSS.asm:548, OnMenuLoad.asm:82 */
    E(0xBB, U, 2, 0, "SEND_CHAT (not emitted)"),   /* Common.s:538 only */
    E(0xBC, L, 1, 4, "GET_NEW_SEED"),              /* Slippi Online Scene/main.asm:847-860 */
    E(0xBD, B, 369, 0, "REPORT_GAME"),             /* Core/InitOnlinePlay.asm:445-452 */
    E(0xBE, B, 32, 30, "FETCH_CODE_SUGGESTION"),   /* TextEntryScreen/CheckTriggersAndZ.asm:151-163 */
    E(0xC2, B, 2, 0, "REPORT_SET_COMPLETION"),     /* Slippi Online Scene/main.asm:572-579 */
    E(0xC4, B, 2, 0, "REPORT_MATCH_STATUS"),       /* Core/InitOnlinePlay.asm:177-184 */
    E(0xD0, L, 0, 0, "LOG_MESSAGE"),               /* Common.s:265-276: 128 bytes [D0,time,level,text] */
    E(0xD1, L, 0, 4, "FILE_LENGTH"),               /* EXIFileLoad/GetFileSize.asm:25-40 */
    E(0xD2, L, 0, VAR, "FILE_LOAD"),               /* EXIFileLoad/TransferFile.asm:62-75 */
    E(0xD3, L, 1, 4, "GCT_LENGTH"),                /* Bootloader/main.asm:42-53 */
    E(0xD4, L, 5, VAR, "GCT_LOAD"),                /* Bootloader/main.asm:78-90 */
    E(0xD5, L, 1, 2, "GET_DELAY"),                 /* UseInGameDelay/InitializeInGameDelay.asm:105-116 */
    E(0xD6, U, 9, 0, "PLAY_MUSIC"),                /* Core/Music/StartSong.asm:53-66 */
    E(0xD7, U, 1, 0, "STOP_MUSIC"),                /* Core/Music/Stop.asm:28-35 */
    E(0xD8, U, 2, 0, "CHANGE_MUSIC_VOLUME"),       /* Core/Music/VolumeChange.asm:57-63 */
    E(0xE1, L, 3, 4, "PREMADE_TEXT_LENGTH"),       /* Static/LoadPremadeText.asm:38,54,77-90 */
    E(0xE2, L, 3, VAR, "PREMADE_TEXT_LOAD")        /* Static/LoadPremadeText.asm:54 */
};

#undef E
#undef VAR
#undef L
#undef B
#undef U

#define NCMDS ((sdshim_u32)(sizeof(g_cmds) / sizeof(g_cmds[0])))

const struct sdshim_cmd_info *sdshim_cmd_lookup(sdshim_u8 cmd)
{
    sdshim_u32 i;
    for (i = 0; i < NCMDS; i++) {
        if (g_cmds[i].cmd == cmd) {
            return &g_cmds[i];
        }
    }
    return 0;
}

sdshim_u32 sdshim_cmd_count(void) { return NCMDS; }

const struct sdshim_cmd_info *sdshim_cmd_at(sdshim_u32 i)
{
    return i < NCMDS ? &g_cmds[i] : 0;
}

/* ---- helpers (no libc) ----------------------------------------------------- */
static void zero(sdshim_u8 *p, sdshim_u32 n)
{
    volatile sdshim_u8 *v = p; /* volatile: keep the compiler from emitting memset */
    while (n--) {
        *v++ = 0;
    }
}

static void copy(sdshim_u8 *d, const sdshim_u8 *s, sdshim_u32 n)
{
    volatile sdshim_u8 *v = d;
    while (n--) {
        *v++ = *s++;
    }
}

static void put32(sdshim_u8 *p, sdshim_u32 v)
{
    p[0] = (sdshim_u8)(v >> 24);
    p[1] = (sdshim_u8)(v >> 16);
    p[2] = (sdshim_u8)(v >> 8);
    p[3] = (sdshim_u8)v;
}

static sdshim_u32 get32(const sdshim_u8 *p)
{
    return ((sdshim_u32)p[0] << 24) | ((sdshim_u32)p[1] << 16) |
           ((sdshim_u32)p[2] << 8) | (sdshim_u32)p[3];
}

/* ---- table file provider -------------------------------------------------- */
static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static const struct sdshim_file_entry *tbl_find(const struct sdshim_file_table *t,
                                                const char *name)
{
    sdshim_u32 i;
    if (!name) {
        return 0;
    }
    for (i = 0; i < t->count; i++) {
        if (t->entries[i].name && str_eq(t->entries[i].name, name)) {
            return &t->entries[i];
        }
    }
    return 0;
}

static sdshim_u32 tbl_size(void *ctx, const char *name)
{
    const struct sdshim_file_table *t = (const struct sdshim_file_table *)ctx;
    const struct sdshim_file_entry *e;
    if (!name) {
        return t->gct ? t->gct_size : 0;
    }
    e = tbl_find(t, name);
    return e ? e->size : 0;
}

static sdshim_u32 tbl_read(void *ctx, const char *name, sdshim_u32 off,
                           sdshim_u8 *dst, sdshim_u32 max)
{
    const struct sdshim_file_table *t = (const struct sdshim_file_table *)ctx;
    const sdshim_u8 *data;
    sdshim_u32 size, n;
    if (!name) {
        data = t->gct;
        size = t->gct ? t->gct_size : 0;
    } else {
        const struct sdshim_file_entry *e = tbl_find(t, name);
        data = e ? e->data : 0;
        size = e ? e->size : 0;
    }
    if (!data || off >= size) {
        return 0;
    }
    n = size - off;
    if (n > max) {
        n = max;
    }
    copy(dst, data + off, n);
    return n;
}

void sdshim_files_from_table(struct sdshim_files *out, const struct sdshim_file_table *tbl)
{
    out->ctx = (void *)tbl;
    out->file_size = tbl_size;
    out->file_read = tbl_read;
    out->premade_text = 0;
}

/* ---- instance ----------------------------------------------------------------- */
void sdshim_init(struct sdshim *s)
{
    zero((sdshim_u8 *)s, (sdshim_u32)sizeof(*s));
    s->magic = SDSHIM_MAGIC;
    s->delay_frames = 2; /* Slippi default (ConfigManager), clamped 0..15 by the ASM */
    s->rng = 0x2545F491u;
}

void sdshim_set_backend(struct sdshim *s, const struct sdshim_backend *b) { s->backend = b; }
void sdshim_set_files(struct sdshim *s, const struct sdshim_files *f) { s->files = f; }

void sdshim_set_delay(struct sdshim *s, sdshim_u32 frames)
{
    s->delay_frames = frames > 15 ? 15 : frames; /* OFFLINE_MAX_DELAY_FRAMES */
}

void sdshim_set_seed(struct sdshim *s, sdshim_u32 seed)
{
    s->rng = seed ? seed : 0x2545F491u;
}

static sdshim_u32 next_rand(struct sdshim *s) /* xorshift32 */
{
    sdshim_u32 x = s->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->rng = x ? x : 0x2545F491u;
    return s->rng;
}

static sdshim_u8 *resp_buf(struct sdshim *s) { return (sdshim_u8 *)s->resp; }
static sdshim_u8 *req_buf(struct sdshim *s) { return (sdshim_u8 *)s->req; }

/* Zero `n` response bytes and make them the pending response. */
static sdshim_u8 *set_resp(struct sdshim *s, sdshim_u32 n)
{
    zero(resp_buf(s), n);
    s->pend = SDSHIM_PEND_BUF;
    s->resp_len = n;
    return resp_buf(s);
}

/* ---- command handlers ---------------------------------------------------------- */
static void do_b0(struct sdshim *s, const struct sdshim_backend *b)
{
    sdshim_u8 *r = set_resp(s, SDSHIM_RXB_SIZE);
    int ok = -1;
    s->stats.frames++;
    s->stats.last_frame = get32(req_buf(s) + 1);
    if (b && b->online_inputs) {
        s->stats.backend_calls++;
        ok = b->online_inputs(b->ctx, req_buf(s), r);
    }
    if (ok < 0) {
        /* Not connected: a clean, non-stalling disconnect (TriggerSendInput.asm:263-266
           sets ODB_IS_DISCONNECTED and continues the frame). */
        zero(r, SDSHIM_RXB_SIZE);
        r[0] = SDSHIM_RESP_DISCONNECTED;
        s->stats.null_replies++;
    }
    switch (r[0]) {
    case SDSHIM_RESP_NORMAL: s->stats.resp_continue++; break;
    case SDSHIM_RESP_SKIP: s->stats.resp_skip++; break;
    case SDSHIM_RESP_ADVANCE: s->stats.resp_advance++; break;
    case SDSHIM_RESP_DISCONNECTED: s->stats.resp_disc++; break;
    default:
        /* Out-of-range result: the ASM would treat it as "continue" (falls through
           the compares at TriggerSendInput.asm:229-239); we refuse to guess. */
        zero(r, SDSHIM_RXB_SIZE);
        r[0] = SDSHIM_RESP_DISCONNECTED;
        s->stats.resp_disc++;
        break;
    }
}

static void do_b3(struct sdshim *s, const struct sdshim_backend *b)
{
    sdshim_u8 *r = set_resp(s, SDSHIM_MSRB_SIZE);
    static const char msg[] = "No network backend (offline build)";
    sdshim_u32 i;
    if (b && b->match_state) {
        s->stats.backend_calls++;
        if (b->match_state(b->ctx, r) >= 0) {
            return;
        }
        zero(r, SDSHIM_MSRB_SIZE);
    }
    s->stats.null_replies++;
    if (s->find_latched) {
        r[0] = SDSHIM_MM_ERROR; /* Online.s:313; message shown by the CSS error path */
        for (i = 0; i + 1 < sizeof(msg); i++) {
            r[SDSHIM_MSRB_ERROR_OFFSET + i] = (sdshim_u8)msg[i];
        }
    } else {
        r[0] = SDSHIM_MM_IDLE;
    }
}

/* Copies the NUL-terminated name starting at p[1]; 0 if it does not fit. */
static int take_name(struct sdshim *s, const sdshim_u8 *p, sdshim_u32 avail)
{
    sdshim_u32 i;
    for (i = 1; i < avail && i - 1 < SDSHIM_NAME_MAX; i++) {
        s->name[i - 1] = (char)p[i];
        if (p[i] == 0) {
            return 1;
        }
    }
    s->name[0] = 0;
    return 0;
}

static sdshim_u32 file_size_of(struct sdshim *s, const char *name)
{
    const struct sdshim_files *f = s->files;
    return (f && f->file_size) ? f->file_size(f->ctx, name) : 0;
}

/* Handle one command at p (avail bytes). *used = bytes consumed. */
static int do_cmd(struct sdshim *s, const sdshim_u8 *p, sdshim_u32 avail, sdshim_u32 *used)
{
    const struct sdshim_cmd_info *ci;
    const struct sdshim_backend *b = s->backend;
    sdshim_u8 c = p[0];
    sdshim_u8 *r;

    *used = avail;
    if ((c >= 0x35 && c <= 0x41) || c == 0x60) {
        s->stats.sunk++; /* recording stream (Recording/ *.asm): host writes .slp, we drop */
        if (sdshim_record_hook) sdshim_record_hook(p, avail);
        return 0;
    }
    ci = sdshim_cmd_lookup(c);
    if (!ci) {
        s->stats.unknown++;
        return -2;
    }
    if (ci->req_size) {
        if (avail < ci->req_size) {
            s->stats.truncated++;
            return -3;
        }
        *used = ci->req_size;
        copy(req_buf(s), p, ci->req_size);
    }
    s->stats.commands++;

    switch (c) {
    case 0xB0: do_b0(s, b); break;
    case 0xB1:
    case 0xB2:
        s->stats.unsupported++;
        if (b && b->savestate) {
            s->stats.backend_calls++;
            b->savestate(b->ctx, c, req_buf(s));
        }
        break;
    case 0xB3: do_b3(s, b); break;
    case 0xB4:
        if (b && b->find_opponent) {
            s->stats.backend_calls++;
            if (b->find_opponent(b->ctx, req_buf(s)[1], req_buf(s) + 2) >= 0) {
                s->find_latched = 0;
                break;
            }
        }
        s->find_latched = 1;
        s->stats.null_replies++;
        break;
    case 0xB5:
        if (b && b->set_selections) {
            s->stats.backend_calls++;
            (void)b->set_selections(b->ctx, req_buf(s));
        } else {
            s->stats.null_replies++;
        }
        break;
    case 0xB6:
    case 0xB7:
    case 0xB8:
        if (b && b->ui_action) {
            s->stats.backend_calls++;
            b->ui_action(b->ctx, c);
        } else {
            s->stats.null_replies++;
        }
        break;
    case 0xB9:
        r = set_resp(s, SDSHIM_OSB_SIZE);
        if (b && b->online_status) {
            s->stats.backend_calls++;
            if (b->online_status(b->ctx, r) < 0) {
                zero(r, SDSHIM_OSB_SIZE);
                s->stats.null_replies++;
            }
        } else {
            s->stats.null_replies++; /* appState 0 = not logged in, empty name/code */
        }
        break;
    case 0xBA:
        s->find_latched = 0;
        if (b && b->cleanup) {
            s->stats.backend_calls++;
            b->cleanup(b->ctx);
        }
        break;
    case 0xBB: s->stats.unsupported++; break;
    case 0xBC:
        r = set_resp(s, 4);
        put32(r, next_rand(s) % 0xFFFFFFFFu); /* E:3094 generator % 0xFFFFFFFF */
        break;
    case 0xBD:
        if (b && b->report_game) {
            s->stats.backend_calls++;
            b->report_game(b->ctx, req_buf(s));
        } else {
            s->stats.null_replies++;
        }
        break;
    case 0xBE:
        r = set_resp(s, SDSHIM_ACRXB_SIZE);
        if (b && b->fetch_suggestion) {
            s->stats.backend_calls++;
            if (b->fetch_suggestion(b->ctx, req_buf(s), r) < 0) {
                zero(r, SDSHIM_ACRXB_SIZE);
                s->stats.null_replies++;
            }
        } else {
            s->stats.null_replies++; /* has_suggestion = 0 */
        }
        break;
    case 0xC2:
    case 0xC4:
        if (b && b->report_status) {
            s->stats.backend_calls++;
            b->report_status(b->ctx, c, req_buf(s)[1]);
        } else {
            s->stats.null_replies++;
        }
        break;
    case 0xD0: {
        char line[SDSHIM_LOG_SIZE];
        sdshim_u32 i = 0, n = avail > SDSHIM_LOG_SIZE ? SDSHIM_LOG_SIZE : avail;
        if (b && b->log && n >= 3) {
            for (; i + 3 < n && p[3 + i]; i++) {
                line[i] = (char)p[3 + i];
            }
            line[i] = 0;
            b->log(b->ctx, p[2], line);
        }
        break; /* variable: consumes the whole write */
    }
    case 0xD1:
        r = set_resp(s, 4);
        if (avail >= 2 && take_name(s, p, avail)) {
            put32(r, file_size_of(s, s->name));
        }
        break;
    case 0xD2:
        s->pend = SDSHIM_PEND_NONE;
        if (avail >= 2 && take_name(s, p, avail) && file_size_of(s, s->name) > 0) {
            s->pend = SDSHIM_PEND_FILE;
        }
        break;
    case 0xD3:
        r = set_resp(s, 4);
        put32(r, file_size_of(s, 0));
        break;
    case 0xD4:
        /* req[1..4] = destination address; the emulator also patches 0x80001F58/5C
           (E:2838-2858).  Patched builds never send D3/D4; nothing to patch here. */
        s->pend = file_size_of(s, 0) > 0 ? SDSHIM_PEND_GCT : SDSHIM_PEND_NONE;
        break;
    case 0xD5:
        r = set_resp(s, 2);
        r[0] = 1; /* "real" flag, E:3177 */
        r[1] = (sdshim_u8)s->delay_frames;
        break;
    case 0xD6:
    case 0xD7:
    case 0xD8: s->stats.unsupported++; break;
    case 0xE1: {
        const struct sdshim_files *f = s->files;
        r = set_resp(s, 4);
        if (f && f->premade_text) {
            put32(r, f->premade_text(f->ctx, req_buf(s)[1], req_buf(s)[2], 0, 0));
        }
        break;
    }
    case 0xE2:
        s->pend = SDSHIM_PEND_NONE;
        if (s->files && s->files->premade_text) {
            s->pend = SDSHIM_PEND_PREMADE;
            s->pend_a = req_buf(s)[1];
            s->pend_b = req_buf(s)[2];
        }
        break;
    default: break;
    }
    return 0;
}

static void do_read(struct sdshim *s, sdshim_u8 *buf, sdshim_u32 len)
{
    sdshim_u32 n = 0;
    const struct sdshim_files *f = s->files;
    switch (s->pend) {
    case SDSHIM_PEND_BUF:
        n = s->resp_len < len ? s->resp_len : len;
        copy(buf, resp_buf(s), n);
        break;
    case SDSHIM_PEND_FILE:
        if (f && f->file_read) {
            n = f->file_read(f->ctx, s->name, 0, buf, len);
        }
        break;
    case SDSHIM_PEND_GCT:
        if (f && f->file_read) {
            n = f->file_read(f->ctx, 0, 0, buf, len);
        }
        break;
    case SDSHIM_PEND_PREMADE:
        if (f && f->premade_text) {
            n = f->premade_text(f->ctx, s->pend_a, s->pend_b, buf, len);
        }
        break;
    default:
        s->stats.empty_reads++; /* E:3586: empty queue, destination untouched */
        return;
    }
    if (n > len) {
        n = len; /* defensive against a misbehaving provider */
    }
    zero(buf + n, len - n); /* E:3592 resize(size, 0) */
}

int sdshim_transfer_ctx(struct sdshim *s, void *vbuf, sdshim_u32 len, int is_write)
{
    sdshim_u8 *buf = (sdshim_u8 *)vbuf;
    sdshim_u32 pos, used;
    int rc = 0, r;

    if (!s || !buf) {
        return -1;
    }
    if (!is_write) {
        s->stats.reads++;
        if (len) {
            do_read(s, buf, len);
        }
        return 0;
    }
    s->stats.writes++;
    s->pend = SDSHIM_PEND_NONE; /* new write replaces the response queue */
    pos = 0;
    while (pos < len) {
        r = do_cmd(s, buf + pos, len - pos, &used);
        if (r < 0) {
            /* E:3418-3422: an invalid command byte aborts the rest of the write */
            rc = r;
            break;
        }
        if (used == 0) {
            break;
        }
        pos += used;
    }
    return rc;
}

#ifndef SDSHIM_NO_GLOBAL
#ifdef SDSHIM_CONFIG_HOOK
extern void SDSHIM_CONFIG_HOOK(struct sdshim *);
#endif

/* For a flat-binary (objcopy -O binary) image the state must live in a PROGBITS
   section: build with -DSDSHIM_G_ATTR='__attribute__((section(".data")))'.  The
   magic check below initialises it on first use whatever the section holds. */
#ifndef SDSHIM_G_ATTR
#define SDSHIM_G_ATTR
#endif
SDSHIM_G_ATTR struct sdshim sdshim_g;

void sdshim_transfer(void *buf, sdshim_u32 len, int is_write)
{
    if (sdshim_g.magic != SDSHIM_MAGIC) {
        sdshim_init(&sdshim_g);
#ifdef SDSHIM_CONFIG_HOOK
        SDSHIM_CONFIG_HOOK(&sdshim_g);
#endif
    }
    (void)sdshim_transfer_ctx(&sdshim_g, buf, len, is_write);
}
#endif
