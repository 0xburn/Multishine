/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* qc_gc: the Melee side of the D-pad quick chat in the card-route engine (qc_gc.h has the data flow). Original work; the drawing code is 
   (hardware-proven, the same placement and look as Slippi's own chat) with the lab modes (self check, in-match test traffic, delayed texts, the image
   blocks) taken out.  Soft-float build: no FPU instruction in this file; floats are stored as bit patterns and the one call that takes float arguments goes through
   bh_sis_add (on_bhold_sis.S).  Everything is compiled only with -DMS_QCHAT=1 (-DMS_QCHAT_DEMO=1 adds the single-console test mode of the lab image). */
#ifdef MS_QCHAT
#include "qc_gc.h"
#include "../../../chat/gc/chat_layout.h"
#include "qc_win.h"                              /* the window layout shared with the Game Night lobby picker (pitch, paddings, column, cut rule) */
#include "../sign/toast_pill.h"                         /* the rounded plate: behind every row and the window */

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef QC_DATA
#define QC_DATA __attribute__((section(".data")))      /* the flat image has no .bss: state lives.data */
#endif

struct qc_status qc_st QC_DATA = {{[QS_MAGIC] = 0x51435354u /* 'QCST' */, [QS_CANARY] = 0x5A5AC0DEu}};
#define ST qc_st.w

/* Placement of the engine build, px of the 640 x 528 frame
   (the CSS draws 1 px = 1 text unit at font 0.1, chat_layout.h; the retail frame is 528 high).  Everything lives in ONE free band: right of the player's card (card x 45..205), below the
   right-hand status block (Direct Mode / Select your character ... Hold Z to disconnect / Use D-Pad to Chat, last line ends y ~436 in every state seen), the lower edge 14 px above the frame's end.
   The window is a 2 x 2 grid (header, then Up | Left / Right | Down) so that it fits the 70 px of the band; the message rows (Slippi: top left over the character icons) stack in the same band, oldest at
   the top, newest at the bottom (Slippi's order), the newest QL_NROWS of them; while the window is open the rows give way (it is open for a second or two: the choice closes it). */
#define QL_X 218
#define QL_BOTTOM 490                                   /* the one lower edge of the message block and of the window: 38 px above the frame's end, clear of the CSS frame line (cube: 514 cut through it) */
#define QL_NROWS 3
#define QL_LINE_STEP QW_PITCH                                 /* one pitch for the message block and the window: even spacing */
#define QL_PAD_X QW_PAD_X                                     /* text left of the plate's edge; the plate's right padding is the same (width = widest line + 2 x this) */
#define QL_PAD_Y QW_PAD_Y                                      /* extra plate height over n lines (n x pitch + 2 x this): the glyph cell has its own air */
#define QL_MAX_W QW_MAX_W                                    /* x 218..598 */
#define QL_WIN_W QW_MAX_W
#define QL_BLK_H(n) QW_BLK_H(n)
#define QL_WIN_H QL_BLK_H(3)                            /* header + two rows: the window is as high as a block of three messages and ends on the same line */
#define QL_WIN_Y (QL_BOTTOM - QL_WIN_H)
#define QL_WIN_DX QL_PAD_X                              /* the window's left text edge = the block's */
#define QL_TEXT_DY (LAY_TEXT_DY_PX + QL_PAD_Y - 3)      /* the first line's y inside the plate: cube-free Dolphin shots p1 had the glyph block 13 px under the plate top and 4 over its bottom: 4 px up evens them (9 / 8) */

#define QL_COL QW_COL                                      /* second column starts 180 px after the first */
#define QL_Q16_PER_PX 69350                             /* LAY_U(1 px) x 65536: plate widths are made at run time with tp_f2 (no float arithmetic) */

/* ---- Melee (NTSC 1.02) addresses, config/GALE01/symbols.txt -------------------------------------- */
#define SCENE_WORD (*(volatile u32 *)0x80479D30)        /* [major, pending, prev, minor] */
#define LOADING (*(volatile u32 *)0x80479D64)           /* 0 while a scene runs */
#define CSS_PORT (*(volatile signed char *)0x804D6CF0)  /* mnCharSel_804D6CF0: the 1P-mode CSS player's port */
#define CSS_EXITKIND (*(volatile u8 *)0x804D6CF6)       /* mnCharSel_804D6CF6: != 0 -> leaving for the code screen */
#define PAD_COPY 0x804C20BCu                            /* HSD_PadCopyStatus[4], 0x44 each; +8 = trigger */
#define CSSDT_BUF (*(volatile u32 *)0x80005614)         /* Online.s CSSDT_BUF_ADDR: Slippi's CSS data table */
#define CSSDT_CHAT_WINDOW_OPENED 20                     /* Online.s:407 (u8) */
#define SIS_FREE_HEAD (*(volatile u32 *)0x804D7970)     /* free_head: {next, data, size} blocks */
#define SIS_CANVAS_FIRST (*(volatile u32 *)0x804D797C)  /* HSD_SisLib_804D797C: first text canvas entry */
typedef void *(*fn_text_new)(int font, int canvas);
typedef void (*fn_text_free)(void *t);
typedef void (*fn_sfx_common)(int id);
typedef void (*fn_sfx_full)(int id, int vol, int pan);
#define TEXT_NEW ((fn_text_new)0x803A6754)             /* HSD_SisLib_803A6754(font, canvas) */
#define TEXT_FREE ((fn_text_free)0x803A5CC4)           /* HSD_SisLib_803A5CC4 */
#define SFX_COMMON ((fn_sfx_common)0x80024030)         /* lbAudioAx_80024030: menu sounds (Slippi SFX_PlayCommon) */
#define SFX_FULL ((fn_sfx_full)0x800237A8)             /* lbAudioAx_800237A8(id, 0x7F, 0x40) (Slippi SFX_Play) */
int bh_sis_add(void *text, const u32 *xy_bits, const char *fmt, const char *s);   /* on_bhold_sis.S -> HSD_SisLib_803A6B98(t, x, y, fmt, s) */

/* Slippi's sounds: CHAT_SOUND_OPEN_WINDOW 2 / CHAT_SOUND_BLOCK_MESSAGE 3 (common), CHAT_SOUND_NEW_MESSAGE 0xb7 */
#define SND_OPEN 2
#define SND_BLOCK 3
#define SND_MESSAGE 0xB7

static struct chat_link CL QC_DATA;
static int g_init, g_on_css;
static u32 g_scene;
static void *g_win;
static void *g_blk;                                     /* the message block: ONE text (one plate) holding the visible rows, one line each */
static u32 g_blk_sig;                                   /* what it was built from: the visible rows' sequence numbers, folded */
static u32 g_last_seq;
#ifdef MS_QCHAT_DEMO
static u32 g_demo_frame;
#endif

static int on_online_css(void)
{
    u32 sc = SCENE_WORD;
    return (sc >> 24) == 8u && (sc & 0xFFu) == 0u && LOADING == 0;
}

/* largest free block of SisLib's text heap (Slippi raises it to 0x4800 bytes on the online CSS, IncreaseTextHeap.asm);
   an allocation that does not fit panics ("Memory Empty"), so we never create a text without 1 KB to spare */
static u32 sis_largest(void)
{
    u32 b = SIS_FREE_HEAD, m = 0, n = 0;
    while (b >= 0x80000000u && b < 0x81800000u && n++ < 256) {
        u32 sz = *(volatile u32 *)(b + 8);
        if (sz > m) m = sz;
        b = *(volatile u32 *)b;
    }
    return m;
}

static void put_f(void *t, u32 off, u32 bits) { *(volatile u32 *)((u8 *)t + off) = bits; }

/* layout tables (chat_layout.h), folded by the compiler: data only, the soft-float build emits no FPU instruction */
#define BLKY(n) LAY_WY(QL_BOTTOM - QL_BLK_H(n))
#define LINEY(i) LAY_U(QL_TEXT_DY + (i) * QL_LINE_STEP)
static const union { float f[QL_NROWS]; u32 u[QL_NROWS]; } k_blk_y = {{BLKY(1), BLKY(2), BLKY(3)}};          /* the block's top by line count 1..3 (its lower edge is fixed) */
static const union { float f[QL_NROWS]; u32 u[QL_NROWS]; } k_blk_h = {{LAY_U(QL_BLK_H(1)), LAY_U(QL_BLK_H(2)), LAY_U(QL_BLK_H(3))}};
static const union { float f[3]; u32 u[3]; } k_line_y = {{LINEY(0), LINEY(1), LINEY(2)}};
static const union { float f[9]; u32 u[9]; } kt = {{
    LAY_WX(QL_X), LAY_WX(QL_X), LAY_WY(QL_WIN_Y), 0.1f /* font_size: 1 unit = 1 px */, 5.0f /* z */,
    LAY_SCALE, LAY_U(QL_PAD_X), LAY_U(QL_WIN_H), LAY_U(QL_WIN_W)}};
static const union { float f[1]; u32 u[1]; } kx = {{LAY_XSCALE}};
enum { K_ROW_X, K_WIN_X, K_WIN_Y, K_FONT, K_Z, K_SCALE, K_DX, K_WIN_H, K_WIN_W };
#define K_XSCALE_BITS (kx.u[0])
static const union { float f[2]; u32 u[2]; } k_col = {{LAY_U(QL_WIN_DX), LAY_U(QL_WIN_DX + QL_COL)}};   /* the window's two columns */


/* ---- text objects ------------------------------------------------------------------------------------------------ */
static void *text_make(u32 x_bits, u32 y_bits, u32 w_bits, u32 h_bits, u32 rgba_text)
{
    void *t;
    u32 lg = sis_largest();
    if (!ST[QS_SIS_FREE_MIN] || lg < ST[QS_SIS_FREE_MIN]) ST[QS_SIS_FREE_MIN] = lg;
    if (lg < 1024u) { ST[QS_TEXT_SKIP]++; return 0; }
    {   /* font 0, canvas 0 (what Slippi's CSS text uses), drawn in the last GX link the CSS camera renders at the top priority (above every CSS model): the first
           text canvas entry's gx_link/gx_pri bytes are set around the creation, then restored (the same trick as Slippi's own messages) */
        u32 ce = SIS_CANVAS_FIRST;
        u8 ol = 0, op = 0;
        int ok = ce >= 0x80000000u && ce < 0x81800000u;
        if (ok) { ol = *(volatile u8 *)(ce + 0xE); op = *(volatile u8 *)(ce + 0xF); *(volatile u8 *)(ce + 0xE) = LAY_GX_LINK; *(volatile u8 *)(ce + 0xF) = LAY_GX_PRIO; }
        t = TEXT_NEW(0, 0);
        if (ok) { *(volatile u8 *)(ce + 0xE) = ol; *(volatile u8 *)(ce + 0xF) = op; }
    }
    if (!t) { ST[QS_TEXT_SKIP]++; return 0; }
    ST[QS_TEXT_NEW]++;
    put_f(t, 0x00, x_bits);                             /* pos_x (top-left of the backdrop) */
    put_f(t, 0x04, y_bits);                             /* pos_y (down is +) */
    put_f(t, 0x08, kt.u[K_Z]);                          /* pos_z */
    put_f(t, 0x0C, w_bits);                             /* box_size: the backdrop quad is box x font_size (renderer 0x803A84BC draws it when bg_color.a != 0) */
    put_f(t, 0x10, h_bits);
    put_f(t, 0x24, kt.u[K_FONT]);                       /* font_size 0.1: 1 text unit = 1 px of 640x480 */
    put_f(t, 0x28, kt.u[K_FONT]);
    put_f(t, 0x2C, 0);
    tp_attach(t);                                       /* the rounded plate of toast_pill.h (the text's own box) instead of the renderer's black rectangle */
    put_f(t, 0x30, rgba_text);                          /* text_color (encoded into each subtext by 0x803A6B98) */
    put_f(t, 0x34, K_XSCALE_BITS);                     /* glyph x scale of the next subtexts */
    put_f(t, 0x38, kt.u[K_SCALE]);
    ((u8 *)t)[0x49] = 1;                                /* kerning (as Slippi's CSS text) */
    ((u8 *)t)[0x4A] = 0;                                /* left aligned */
    return t;
}

static void text_free(void **t)
{
    if (*t) { TEXT_FREE(*t); ST[QS_TEXT_FREE]++; }
    *t = 0;
}

/* one text of a row (line 0) or of the window (grid row 0..2) at x */
static void text_at(void *t, u32 x, int row, const u8 *raw)
{
    u32 xy[2];
    xy[0] = x;
    xy[1] = k_line_y.u[row < 0 ? 0 : row > 2 ? 2 : row];
    bh_sis_add(t, xy, "%s", (const char *)raw);
}

/* colours: own messages light gold, the opponent by port (Slippi's port colours, lightened for the dark backdrop) */
static u32 row_colour(int pidx, int local, cc_s32 id)
{
    static const u32 port[4] = {0xFF9C9CFFu, 0x9CD2FFFFu, 0xFFDC78FFu, 0x96E696FFu};
    if (id == CP_ID_DISABLED) return 0xDCDCDCFFu;
    if (local) return 0xFFF0A0FFu;
    return port[pidx & 3];
}

static int fit_a(u8 *raw, int max_px) { return qw_fit_a(raw, max_px, 0); }       /* qc_win.h: the cut rule */

static void rows_forget(void)
{
    g_blk = 0; g_blk_sig = 0;
    g_win = 0;
}

/* (re)draw the message block: ONE plate shrink-wrapped to the widest of the newest QL_NROWS lines (QL_PAD_X left and right, the window's line pitch, lower edge fixed so the block grows
   upwards, newest line at the bottom), one line per message in its own colour.  Rebuilt only when the set of visible messages changes (a row expiring or arriving). */
static void rows_draw(void)
{
    int i, first = CL.subs.n > QL_NROWS ? CL.subs.n - QL_NROWS : 0, nv = CL.subs.n - first;
    u32 sig = 0;
    for (i = 0; i < CC_VISIBLE; i++) {
        const struct cc_sub *e = i < CL.subs.n ? &CL.subs.e[i] : 0;
        ST[QS_ROW_ID0 + i] = e ? ((u32)e->id | ((u32)e->pidx << 8) | ((u32)e->local << 12) | (e->seq << 16)) : 0;
    }
    for (i = first; i < CL.subs.n; i++) sig = sig * 31u + CL.subs.e[i].seq + 1u;
    if (g_win || nv <= 0) { text_free(&g_blk); g_blk_sig = 0; }   /* the window is open (rows give way; the entries keep their timers) or nothing to show */
    else if (!g_blk || sig != g_blk_sig) {
        u8 raw[QL_NROWS][CC_LINE_MAX + 4];
        int pidx[QL_NROWS], local[QL_NROWS], px = 0, w;
        u32 wq;
        text_free(&g_blk);
        g_blk_sig = sig;
        for (i = 0; i < nv; i++) {
            int p = 0;
            chat_link_row(&CL, first + i, raw[i], sizeof raw[i], &pidx[i], &local[i]);
            p = fit_a(raw[i], QL_MAX_W - 2 * QL_PAD_X);
            if (p > px) px = p;
        }
        w = px + 2 * QL_PAD_X;
        wq = tp_f2(w * QL_Q16_PER_PX, 16);
        g_blk = text_make(kt.u[K_ROW_X], k_blk_y.u[nv - 1], wq, k_blk_h.u[nv - 1], 0xFFFFFFFFu);
        if (g_blk) {
            for (i = 0; i < nv; i++) {
                const struct cc_sub *e = &CL.subs.e[first + i];
                put_f(g_blk, 0x30, row_colour(pidx[i], local[i], e->id));
                text_at(g_blk, kt.u[K_DX], i, raw[i]);
            }
        }
        {
            const struct cc_sub *e = &CL.subs.e[CL.subs.n - 1];
            if (e->seq != g_last_seq) { g_last_seq = e->seq; ST[QS_LAST_SHOWN] = (u32)e->id | ((u32)pidx[nv - 1] << 8) | ((u32)local[nv - 1] << 12); }
        }
    }
    ST[QS_ROWS] = (u32)CL.subs.n;
}

/* the chat window: header + the four messages of the group, our own texts. The plate is shrink-wrapped like the message block's: QL_PAD_X, the left column
   (QL_COL wide: the right column starts there), the widest text of the right column, QL_PAD_X; header in the first line, then Up | Left and Right | Down on the block's line pitch */
static void window_open(int group)
{
    static const char dir[4][7] = {"Up", "Left", "Right", "Down"};
    int g, k, i, w;
    u8 raw[4][CC_LINE_MAX + 4], hdr[CC_LINE_MAX + 4];
    u8 *rp[4];
    struct qw_plan pl;
    text_free(&g_win);
    for (g = 0; g < 4 && cp_menu_bits[g] != group; g++) { }
    if (g == 4) return;
    {   /* header: "Chat: Up" (+ " - off" when chat is off: Slippi opens the window anyway and sends nothing) */
        const char *h = cp_group_header[g];
        int n = 0;
        hdr[0] = 0;
        for (k = 0; h[k]; k++) n = cc_put_char(hdr, n, sizeof hdr, h[k]);
        if (!chat_link_enabled(&CL)) {
            static const char off[] = " - off";
            for (k = 0; off[k]; k++) n = cc_put_char(hdr, n, sizeof hdr, off[k]);
        }
    }
    for (i = 0; i < 4; i++) {
        cc_line(raw[i], sizeof raw[i], dir[i], 0, CL.own[g * 4 + i], cp_ids[g * 4 + i]);
        rp[i] = raw[i];
    }
    qw_plan(&pl, hdr, rp, 0);                           /* qc_win.h: cuts the texts and gives the plate's width */
    w = pl.w;
    g_win = text_make(kt.u[K_WIN_X], kt.u[K_WIN_Y], tp_f2(w * QL_Q16_PER_PX, 16), kt.u[K_WIN_H],
                      chat_link_enabled(&CL) ? (CL.session ? 0xFFEA2FFFu : 0xC9C387FFu) : 0xB4B4B4FFu);
    if (!g_win) return;
    text_at(g_win, k_col.u[0], 0, hdr);
    put_f(g_win, 0x30, 0xFFFFFFFFu);                    /* the four messages in white: Up | Left, then Right | Down */
    for (i = 0; i < 4; i++) text_at(g_win, k_col.u[i & 1], 1 + (i >> 1), raw[i]);
}

static void cssdt_window_flag(int open)
{
    u32 dt = CSSDT_BUF;
    if (dt >= 0x80000000u && dt < 0x81800000u) *(volatile u8 *)(dt + CSSDT_CHAT_WINDOW_OPENED) = (u8)(open ? 1 : 0);
}

/* ---- scene bookkeeping ------------------------------------------------------------------------------------------ */
static void css_enter(void)
{
    cc_subs_clear(&CL.subs);                            /* Slippi's notification objects die with the CSS scene */
    cc_ui_close(&CL.ui);
    rows_forget();                                      /* the old scene's text heap is gone: never free those */
#ifdef MS_QCHAT_DEMO
    g_demo_frame = 0;
#endif
    ST[QS_SCENE_RESETS]++;
}

/* ---- public ------------------------------------------------------------------------------------------------------- */
static void qc_init(void)
{
    if (g_init) return;
    g_init = 1;
    chat_link_init(&CL, CC_CHAT_ON, 0);                 /* our texts: the presets (offline/chat/chat_presets.json) */
    ST[QS_MAGIC] = 0x51435354u;
    ST[QS_CANARY] = 0x5A5AC0DEu;
    ST[QS_PORT] = 0xFFu;
}

int qc_win_open(void) { return g_init && CL.ui.open; }

void qc_session(int up, int local_idx, int remote_idx, const char *own_name, const char *opp_name, int direct)
{
    qc_init();
    CL.direct = direct;
    if (up) {
        chat_link_new_session(&CL, local_idx, remote_idx);
        chat_link_set_name(&CL, local_idx, own_name);
        chat_link_set_name(&CL, remote_idx, opp_name);
    } else if (CL.session) {
        chat_link_end_session(&CL);
    }
    ST[QS_SESSION] = (u32)CL.session | ((u32)CL.local_idx << 8) | ((u32)CL.remote_idx << 16);
}

void qc_rx(const cc_u8 *d, cc_u32 len)
{
    int r;
    qc_init();
    r = chat_link_rx(&CL, d, len);
    if (r == CC_RX_OK) { ST[QS_RX_OK]++; ST[QS_LAST_RX_ID] = (u32)CL.net.remote_id | ((u32)CL.net.remote_pidx << 8); }
    else ST[QS_RX_BAD]++;
    ST[QS_RX_OVERWRITTEN] = CL.net.rx_overwritten;
}

/* Slippi's prepareOnlineMatchState runs at every B3 (every CSS frame): own echo first, else the received id; chat off -> the automatic reply 0x10 */
static void poll_frame(void)
{
    cc_u8 reply[CC_WIRE_LEN];
    int r, n0 = (int)CL.shown;
    ST[QS_B3]++;
    r = chat_link_b3(&CL, 0, reply);
    if (r) { ST[QS_REPLIES]++; ST[QS_LAST_TX_ID] = CP_ID_DISABLED; if (qc_tx_hook(reply, CC_WIRE_LEN) == 0) ST[QS_TX_OK]++; else ST[QS_TX_FAIL]++; }
    if ((int)CL.shown != n0) {                          /* [C] CreateAndAddChatMessage: SFX_Play(CHAT_SOUND_NEW_MESSAGE) */
        ST[QS_SHOWN] = CL.shown;
        if (CSS_EXITKIND == 0) SFX_FULL(SND_MESSAGE, 0x7F, 0x40);
    }
    ST[QS_DROP_FULL] = CL.subs.dropped_full;
    ST[QS_DROP_CAP] = CL.subs.dropped_cap;
    ST[QS_DROP_INVALID] = CL.subs.dropped_invalid;
}

#ifndef MS_QCHAT_DEMO
static void tx(const cc_u8 *d, int len)
{
    if (len <= 0) return;
    if (qc_tx_hook(d, (cc_u32)len) == 0) ST[QS_TX_OK]++;
    else ST[QS_TX_FAIL]++;
    ST[QS_LAST_TX_ID] = d[4];
}
#endif

#ifdef MS_QCHAT_DEMO
/* TEST MODE (lab, single console, no network): every preset as a subtitle, the notice, the four window pages and a worst-case line, one step per 150 frames */
static void demo_frame(void)
{
    int arg = 0, kind = cc_demo_step(g_demo_frame++, 150u, &arg);
    if (kind == CC_DEMO_NONE) return;
    ST[QS_DEMO_STEP]++;
    text_free(&g_win);
    if (kind == CC_DEMO_SUB_OWN || kind == CC_DEMO_SUB_OPP) {
        cc_subs_add(&CL.subs, kind == CC_DEMO_SUB_OWN ? CL.local_idx : CL.remote_idx, cp_ids[arg], kind == CC_DEMO_SUB_OWN);
    } else if (kind == CC_DEMO_NOTICE) {
        cc_subs_add(&CL.subs, CL.remote_idx, CP_ID_DISABLED, 0);
    } else if (kind == CC_DEMO_WINDOW) {                /* the page bottom left and, at the same time, its first message as our own subtitle top left */
        int g;
        window_open(arg);
        for (g = 0; g < 4 && cp_menu_bits[g] != arg; g++) { }
        if (g < 4) cc_subs_add(&CL.subs, CL.local_idx, cp_ids[g * 4], 1);
    } else if (kind == CC_DEMO_LONG) {                  /* a worst-case custom text from the opponent: 25 wide characters */
        int k;
        for (k = 0; k < CP_N; k++) {
            int c;
            for (c = 0; c < CP_MAX_CHARS; c++) CL.str.text[CL.remote_idx][k][c] = (char)(c % 2 ? 'M' : 'W');
            CL.str.text[CL.remote_idx][k][CP_MAX_CHARS] = 0;
        }
        CL.str.have[CL.remote_idx] = 1;
        chat_link_set_name(&CL, CL.remote_idx, "MMMMMMMMMMMMMMM");
        cc_subs_add(&CL.subs, CL.remote_idx, cp_ids[0], 0);
    }
}
#endif

void qc_iter(void)
{
    int css, f;
    u32 sc = SCENE_WORD;
    qc_init();
    ST[QS_ITERS]++;
    ST[QS_ENABLED] = (u32)chat_link_enabled(&CL);
    css = on_online_css();
    if (css && (!g_on_css || sc != g_scene)) css_enter();
    if (!css && g_on_css) { rows_forget(); cc_ui_close(&CL.ui); qc_tx_cancel_hook(); }   /* the scene owns the texts: let it free them; a message still waiting for the connection is dropped */
    g_on_css = css;
    g_scene = sc;
    if (!css) return;                                   /* in a match nothing is drawn and the D-pad is game input (Slippi has no in-match chat either) */
    if (CSS_EXITKIND != 0) {
        /* the connect-code name entry runs inside the CSS scene (stc_css_exitkind != 0): nothing of ours over its keyboard (hardware photo chat9_hw_20): close the
           window, free every text (the scene and its text heap stay alive, so freeing is valid); messages keep their timers and come back on the CSS */
        if (g_win) text_free(&g_win);
        text_free(&g_blk); g_blk_sig = 0;
        if (CL.ui.open) cc_ui_close(&CL.ui);
        cssdt_window_flag(0);
        chat_link_tick(&CL);
        ST[QS_NAME_ENTRY]++;
        return;
    }
    ST[QS_CSS_ITERS]++;
    poll_frame();
#ifdef MS_QCHAT_DEMO
    demo_frame();
#else
    {                                                   /* UpdateOnlineCSS: chat input only on the CSS itself */
        cc_u32 pressed = 0;
        cc_u8 out[CC_WIRE_LEN];
        int len = 0, port = CSS_PORT;
        if (port >= 0 && port < 4) pressed = *(volatile u32 *)(PAD_COPY + 0x44u * (u32)port + 8u);   /* trigger */
        ST[QS_PORT] = (u32)port & 0xFFu;
        if (pressed & 0x20Fu) ST[QS_PRESSED] = pressed;
        if (!CL.session && !CL.ui.open) pressed = 0;      /*: no chat window while nobody is paired (searching / waiting in Mano a mano, Dojo, Grandmaster); the session is up from the assignment on */
        f = chat_link_frame(&CL, pressed & 0x20Fu, out, &len);
        if (f & CC_UI_OPENED) { SFX_COMMON(SND_OPEN); ST[QS_UI_OPENS]++; }
        if (f & CC_UI_BLOCKED) SFX_COMMON(SND_BLOCK);
        if (f & CC_UI_SENT) { ST[QS_SENT]++; SFX_FULL(SND_MESSAGE, 0x7F, 0x40); tx(out, len); }
        if (CL.ui.open && CL.ui.built && !g_win) window_open(CL.ui.group);   /* [C] the first window frame builds it */
        if (!CL.ui.open && g_win) text_free(&g_win);
        cssdt_window_flag(CL.ui.open);
    }
#endif
    chat_link_tick(&CL);
    ST[QS_EXPIRED] = CL.subs.expired;
    ST[QS_WINDOW] = (u32)CL.ui.open | ((u32)CL.ui.group << 8);
    rows_draw();
}
#endif /* MS_QCHAT */
