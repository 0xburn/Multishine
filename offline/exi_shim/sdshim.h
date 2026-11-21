/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * sdshim: console-side replacement for the emulator-side Slippi EXI device.
 *
 * Project-owned C89, no heap, no libc, byte-wise access to caller buffers.
 * Behaviour (not code) follows the Slippi ASM caller convention
 *   third_party/slippi-ssbm-asm/Common/EXITransferBuffer/EXITransferBuffer.asm:1-30
 *   (r3 = buffer, r4 = length, r5 = 1 write / 0 read; installed at 0x800055F0,
 *    Common/Common.s:349)
 * and the emulator semantics
 * (DMAWrite stores command + builds response, E:3418-3430; DMARead copies the
 * response resized to the read length with zero fill, E:3584-3595; a read with
 * no response is a no-op, E:3586-3590).
 *
 * sdshim_transfer(buf, len, is_write) has the PowerPC EABI signature
 * (r3, r4, r5) so it can be branched to directly from 0x800055F0.
 */
#ifndef SDSHIM_H
#define SDSHIM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char sdshim_u8;
typedef unsigned short sdshim_u16;
typedef unsigned int sdshim_u32; /* 32 bits on every supported target */

/* ---- command ids (Common/Common.s:524-561) ------------------------------ */
#define SDSHIM_CMD_ONLINE_INPUTS 0xB0
#define SDSHIM_CMD_CAPTURE_SAVESTATE 0xB1
#define SDSHIM_CMD_LOAD_SAVESTATE 0xB2
#define SDSHIM_CMD_GET_MATCH_STATE 0xB3
#define SDSHIM_CMD_FIND_OPPONENT 0xB4
#define SDSHIM_CMD_SET_MATCH_SELECTIONS 0xB5
#define SDSHIM_CMD_OPEN_LOGIN 0xB6
#define SDSHIM_CMD_LOGOUT 0xB7
#define SDSHIM_CMD_UPDATE_APP 0xB8
#define SDSHIM_CMD_GET_ONLINE_STATUS 0xB9
#define SDSHIM_CMD_CLEANUP_CONNECTION 0xBA
#define SDSHIM_CMD_SEND_CHAT 0xBB
#define SDSHIM_CMD_GET_NEW_SEED 0xBC
#define SDSHIM_CMD_REPORT_GAME 0xBD
#define SDSHIM_CMD_FETCH_SUGGESTION 0xBE
#define SDSHIM_CMD_REPORT_SET_COMPLETE 0xC2
#define SDSHIM_CMD_REPORT_MATCH_STATUS 0xC4
#define SDSHIM_CMD_LOG_MESSAGE 0xD0
#define SDSHIM_CMD_FILE_LENGTH 0xD1
#define SDSHIM_CMD_FILE_LOAD 0xD2
#define SDSHIM_CMD_GCT_LENGTH 0xD3
#define SDSHIM_CMD_GCT_LOAD 0xD4
#define SDSHIM_CMD_GET_DELAY 0xD5
#define SDSHIM_CMD_PLAY_MUSIC 0xD6
#define SDSHIM_CMD_STOP_MUSIC 0xD7
#define SDSHIM_CMD_MUSIC_VOLUME 0xD8
#define SDSHIM_CMD_PREMADE_TEXT_LENGTH 0xE1
#define SDSHIM_CMD_PREMADE_TEXT_LOAD 0xE2

/* ---- wire sizes (Online/Online.s offsets; request = incl. command byte) -- */
#define SDSHIM_TXB_SIZE 28        /* Slippi 3.7.0 (slippi-ssbm-asm d0ce23a Online.s TXB_SIZE: + input source index, + is-leaving; 3.6.4: 26) */
#define SDSHIM_RXB_SIZE 559       /* Slippi 3.7.0 RXB_SIZE (1+1+3*8+3*4+4+3*7*12 + despawn frames 3*4 + predicted inputs 3*7*12 + next-pad RNG bits 1; 3.6.4: 297) */
#define SDSHIM_MSRB_SIZE 1030     /* Slippi 3.7.0 MSRB_SIZE (+ rank info 4*16, + bot flags 4; 3.6.4: 962); the backends fill the first 962 bytes, the rest stays zero */
#define SDSHIM_OSB_SIZE 42        /* Online.s:461-464 */
#define SDSHIM_SSRB_SIZE 33       /* Online.s:131-142 */
#define SDSHIM_PSTB_SIZE 10       /* Online.s:367-376 */
#define SDSHIM_FMTB_SIZE 20       /* Online.s:389-392 */
#define SDSHIM_RGB_SIZE 369       /* Online.s:476-490 (21+4*9+0x138) */
#define SDSHIM_ACTXB_SIZE 32      /* AutoComplete.s:16-22 */
#define SDSHIM_ACRXB_SIZE 30      /* AutoComplete.s:9-13 */
#define SDSHIM_LOG_SIZE 128       /* Common.s:265-276 (logf) */

/* RXB byte 0 (Online.s:563-566) */
#define SDSHIM_RESP_NORMAL 1
#define SDSHIM_RESP_SKIP 2
#define SDSHIM_RESP_DISCONNECTED 3
#define SDSHIM_RESP_ADVANCE 4

/* MSRB connection state (Online.s:308-313) and error message field */
#define SDSHIM_MM_IDLE 0
#define SDSHIM_MM_ERROR 5
#define SDSHIM_MSRB_ERROR_OFFSET 357 /* MSRB_ERROR_MSG, ERROR_MESSAGE_LEN 241 */
#define SDSHIM_MSRB_ERROR_LEN 241

#define SDSHIM_NAME_MAX 128       /* longest file name accepted (incl. NUL) */
#define SDSHIM_RESP_MAX 1056      /* >= largest buffered response (MSRB 1030) */
#define SDSHIM_REQ_MAX 384        /* >= largest request copied (RGB 369) */

/* ---- command table ------------------------------------------------------ */
#define SDSHIM_OWN_LOCAL 0
#define SDSHIM_OWN_BACKEND 1
#define SDSHIM_OWN_UNSUPPORTED 2

struct sdshim_cmd_info {
    sdshim_u8 cmd;
    sdshim_u8 owner;
    sdshim_u16 req_size;  /* total write length incl. command byte; 0 = variable */
    sdshim_u16 resp_size; /* bytes the ASM reads back; 0 = none; 0xFFFF = variable */
#ifndef SDSHIM_NO_NAMES
    const char *name;
#endif
};

/* Returns the table entry for cmd, or 0 if the shim does not know it. */
const struct sdshim_cmd_info *sdshim_cmd_lookup(sdshim_u8 cmd);
sdshim_u32 sdshim_cmd_count(void);
const struct sdshim_cmd_info *sdshim_cmd_at(sdshim_u32 index);

/* ---- file / text provider (LOCAL D1 D2 D3 D4 E1 E2) ---------------------- */
struct sdshim_files {
    void *ctx;
    /* Size of replacement file `name`, 0 = no replacement.  name == 0 means
       the Gecko code list (D3/D4).  May be NULL: every size is 0. */
    sdshim_u32 (*file_size)(void *ctx, const char *name);
    /* Copy up to max bytes from `offset` into dst; returns bytes copied. */
    sdshim_u32 (*file_read)(void *ctx, const char *name, sdshim_u32 offset,
                            sdshim_u8 *dst, sdshim_u32 max);
    /* Premade text data for (id, param).  dst == 0: return length only.
       Otherwise copy up to max bytes and return the bytes copied. */
    sdshim_u32 (*premade_text)(void *ctx, sdshim_u8 id, sdshim_u8 param,
                               sdshim_u8 *dst, sdshim_u32 max);
};

/* Simple static-table provider (names compared exactly, case-sensitive). */
struct sdshim_file_entry {
    const char *name;
    const sdshim_u8 *data;
    sdshim_u32 size;
};
struct sdshim_file_table {
    const struct sdshim_file_entry *entries;
    sdshim_u32 count;
    const sdshim_u8 *gct; /* optional code list for D3/D4 */
    sdshim_u32 gct_size;
};
void sdshim_files_from_table(struct sdshim_files *out,
                             const struct sdshim_file_table *tbl);

/* ---- backend vtable (BACKEND commands) ------------------------------------
 * Every pointer may be NULL (or the whole backend may be NULL): the shim then
 * produces the "not connected" response documented in.  All request
 * pointers are aligned, shim-owned copies (safe to read as words); all
 * response pointers are aligned shim-owned buffers of the stated size, already
 * zeroed.  Return 0 on success, <0 to make the shim fall back to the
 * null-backend response for that command.
 */
struct sdshim_backend {
    void *ctx;
    /* B0: txb[26] in, rxb[297] out (rxb[0] must be one of SDSHIM_RESP_*;
       anything else is rewritten to DISCONNECTED).  Called synchronously from
       the write; it may block (this is the per-frame pad exchange). */
    int (*online_inputs)(void *ctx, const sdshim_u8 *txb, sdshim_u8 *rxb);
    /* B3: msrb[962] out */
    int (*match_state)(void *ctx, sdshim_u8 *msrb);
    /* B4: mode (0 ranked,1 unranked,2 direct,3 teams,4 party), code[18] SJIS */
    int (*find_opponent)(void *ctx, sdshim_u8 mode, const sdshim_u8 *code);
    /* B5: pstb[10] */
    int (*set_selections)(void *ctx, const sdshim_u8 *pstb);
    /* B9: osb[42] out */
    int (*online_status)(void *ctx, sdshim_u8 *osb);
    /* BA */
    void (*cleanup)(void *ctx);
    /* B6 / B7 / B8: cmd is the command byte */
    void (*ui_action)(void *ctx, sdshim_u8 cmd);
    /* BE: req[32] in, rsp[30] out */
    int (*fetch_suggestion)(void *ctx, const sdshim_u8 *req, sdshim_u8 *rsp);
    /* BD: rgb[369] */
    void (*report_game)(void *ctx, const sdshim_u8 *rgb);
    /* C2 / C4: cmd and the one payload byte (set index / status index) */
    void (*report_status)(void *ctx, sdshim_u8 cmd, sdshim_u8 arg);
    /* B1 / B2: ssrb[33] */
    void (*savestate)(void *ctx, sdshim_u8 cmd, const sdshim_u8 *ssrb);
    /* D0 (LOCAL, optional): level, NUL-terminated text */
    void (*log)(void *ctx, sdshim_u8 level, const char *text);
};

/* ---- statistics ----------------------------------------------------------- */
struct sdshim_stats {
    sdshim_u32 writes, reads;
    sdshim_u32 commands;       /* commands dispatched */
    sdshim_u32 unknown;        /* unknown command byte, write ignored */
    sdshim_u32 truncated;      /* known command but write shorter than required */
    sdshim_u32 empty_reads;    /* read with no pending response (buffer untouched) */
    sdshim_u32 unsupported;    /* UNSUPPORTED commands accepted and dropped */
    sdshim_u32 sunk;           /* recording commands 0x35-0x41,0x60 consumed and dropped */
    sdshim_u32 backend_calls;
    sdshim_u32 null_replies;   /* BACKEND commands answered by the null response */
    sdshim_u32 frames;         /* B0 requests */
    sdshim_u32 resp_continue, resp_skip, resp_disc, resp_advance;
    sdshim_u32 last_frame;     /* frame field of the last B0 */
};

/* ---- instance --------------------------------------------------------------- */
#define SDSHIM_PEND_NONE 0
#define SDSHIM_PEND_BUF 1
#define SDSHIM_PEND_FILE 2
#define SDSHIM_PEND_GCT 3
#define SDSHIM_PEND_PREMADE 4

struct sdshim {
    sdshim_u32 magic;
    const struct sdshim_backend *backend; /* may be NULL */
    const struct sdshim_files *files;     /* may be NULL */
    sdshim_u32 delay_frames;              /* D5 answer, default 2 */
    sdshim_u32 rng;                       /* BC generator state, nonzero */
    int find_latched;                     /* B4 seen with no backend: B3 reports error */
    int pend;                             /* SDSHIM_PEND_* */
    sdshim_u32 resp_len;                  /* valid for PEND_BUF */
    sdshim_u8 pend_a, pend_b;             /* premade id / param */
    char name[SDSHIM_NAME_MAX];           /* file name for PEND_FILE */
    struct sdshim_stats stats;
    sdshim_u32 req[SDSHIM_REQ_MAX / 4];   /* aligned request copy */
    sdshim_u32 resp[SDSHIM_RESP_MAX / 4]; /* aligned response */
};

void sdshim_init(struct sdshim *s);
void sdshim_set_backend(struct sdshim *s, const struct sdshim_backend *b);
void sdshim_set_files(struct sdshim *s, const struct sdshim_files *f);
void sdshim_set_delay(struct sdshim *s, sdshim_u32 frames); /* clamped 0..15 */
void sdshim_set_seed(struct sdshim *s, sdshim_u32 seed);

/* Returns 0 ok, -1 bad arguments, -2 unknown command, -3 truncated request. */
int sdshim_transfer_ctx(struct sdshim *s, void *buf, sdshim_u32 len, int is_write);

#ifndef SDSHIM_NO_GLOBAL
/* Single global instance, lazily initialised on first use.  Optional build hook:
   -DSDSHIM_CONFIG_HOOK=fn, `void fn(struct sdshim *)`, runs once after init so
   the patched build can attach its file table / backend without a boot stub. */
extern struct sdshim sdshim_g;
void sdshim_transfer(void *buf, sdshim_u32 len, int is_write);
#endif

#ifdef __cplusplus
}
#endif
#endif
