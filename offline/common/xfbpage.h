/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * xfbpage.h: a full-screen text page for code that runs inside Melee 1.02 NTSC-U (GALE01), shared by the BBA probe
 * and the LOAD-probe overlay of the online image.
 *
 * Why it exists.  The earlier page was drawn from HSD's VI pre-retrace callback into EVERY frame buffer listed in
 * HSD_VIData, with a 4 ms budget that cut the draw short and went on at the next retrace.  Melee renders to two XFBs
 * (HSD_AllocateXFB(2, ...)) and copies the EFB over one of them every frame (HSD_VICopyXFBAsync), so each fresh frame
 * started without text and only got the rows the callback managed to draw before it was shown; rows drawn into the buffer that
 * was being copied into were lost.  The page therefore came and went per row and per frame: flicker.
 *
 * What it does now: the page OWNS one of the two buffers and the game never writes it.
 *   - claim (VI interrupt, xp_retrace from HSD's user pre-retrace callback): when buffer k is FREE or DISPLAY (no EFB copy can be
 *     pending into it), HSD_VIData.xfb[k].buffer is pointed at the OTHER buffer.  HSD's own state machine (statuses FREE, DRAWING,
 *     WAITDONE, NEXT, DISPLAY) runs on unchanged, but both of its entries now name the same memory, so every EFB copy of the
 *     game lands there and the claimed buffer is never written by the GPU again;
 *   - show (every retrace): after HSD has chosen its next buffer (VISetNextFrameBuffer + VIFlush inside HSD_VIPreRetraceCB) the
 *     user callback calls VISetNextFrameBuffer(own) + VIFlush, so VI always scans the claimed buffer (the SDK writes the registers
 *     after the pre-retrace callback returns);
 *   - draw (game thread, xp_draw): only rows whose text changed are redrawn, whole rows at a time, into a buffer nobody else
 *     writes.  Nothing is drawn per frame; a page switch or the first fill draws the whole screen once.  No budget, never partial.
 * The game keeps running and rendering (to the other buffer, unseen); its own logic is untouched.  Cost: no RAM (the buffer is the
 * game's), about two VI calls per retrace, and the changed rows when the text changes.  xp_release undoes the claim (the overlay
 * calls it on exit; one frame of the old page can stay visible until HSD shows its next buffer).  If the game thread stops calling
 * xp_tick for XP_STALE_RETRACES retraces (a crash screen, a long load), the VI override pauses so HSD / a crash handler can show
 * their own buffer; it resumes with the next tick.
 *
 * Page: 52 columns x 26 rows (a header row and 25 body rows), 5x7 ASCII glyphs (32..126) in 6x8 cells at 2x2 pixels (12 x 16 px,
 * row pitch 17 px), a solid dark panel over the whole frame buffer.  Logical rows (any width) are word-wrapped to 52 columns with a
 * hanging indent and packed into pages so that a block (rows flagged XP_F_BLOCK start one) stays on one page when it fits; rows
 * flagged XP_F_PIN appear at the bottom of every page.  Pages are switched with D-pad Left / Right (alone, edge-triggered:
 * xp_pad), the header shows "page n/m".
 *
 * The includer provides: u8, u16, u32; XP_LD32(addr) / XP_ST32(addr, v) (raw memory access, emulated in host tests);
 * XP_VISETNEXT(fb) / XP_VIFLUSH (SDK VISetNextFrameBuffer 0x80350094 / VIFlush 0x8034FF78); XP_NOW_US (microsecond clock).
 * Layout of HSD_VIData (HSD_VIInfo, vendor/melee/src/sysdolphin/baselib/video.h): current.vi.rmode at +0 (fbWidth u16 +4,
 * xfbHeight u16 +8), xfb[i] at +0x58 + 0x60 i (buffer +0, status +4), nb_xfb +0x1D4, user pre_cb +0x1D8.
 */
#ifndef XFBPAGE_H
#define XFBPAGE_H

#define XP_VIDATA 0x804C1D80u
#define XP_XFB_BUF(i) (XP_VIDATA + 0x58u + 0x60u * (u32)(i))
#define XP_XFB_ST(i) (XP_VIDATA + 0x5Cu + 0x60u * (u32)(i))
#define XP_NBXFB (XP_VIDATA + 0x1D4u)
#define XP_LD16(a) ((u16)(XP_LD32((a) & ~3u) >> (((a) & 2u) ? 0 : 16)))
enum { XP_ST_FREE = 2, XP_ST_DRAWING = 3, XP_ST_WAITDONE = 4, XP_ST_DRAWDONE = 5, XP_ST_NEXT = 6, XP_ST_DISPLAY = 7 };

#define XP_COLS 52
#define XP_ROWS 26                   /* header + XP_BODY */
#define XP_BODY (XP_ROWS - 1)
#define XP_PITCH 17                  /* lines per text row: 7 glyph rows x 2 + 3 blank */
#define XP_CW 6                      /* words per character cell: 5 glyph columns + 1 gap, one 32-bit word = 2 pixels */
#define XP_STALE_RETRACES 180u
#define XP_F_BLOCK 1u                /* this logical row starts a block (kept on one page when it fits) */
#define XP_F_PIN 2u                  /* shown at the bottom of every page */
#define XP_F_HI 4u                   /* highlighted (yellow text) */
#define XP_A_BODY 0u
#define XP_A_HEAD 1u
#define XP_A_HI 2u
/* YCbYCr words (two pixels of one colour) */
#define XP_BG 0x1C981C7Au            /* dark blue-black panel */
#define XP_FG 0xEB80EB80u            /* white */
#define XP_HI 0xD210D292u            /* yellow */
#define XP_HEAD_BG 0xD210D292u       /* header bar: yellow ... */
#define XP_HEAD_FG 0x10801080u       /* ... with black text */

/* 5x7 font, ASCII 32..126, five column bytes per glyph, bit 0 = top row */
static const u8 xp_font[95][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0x5F, 0x00, 0x00 }, { 0x00, 0x07, 0x00, 0x07, 0x00 }, { 0x14, 0x7F, 0x14, 0x7F, 0x14 },
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, { 0x23, 0x13, 0x08, 0x64, 0x62 }, { 0x36, 0x49, 0x55, 0x22, 0x50 }, { 0x00, 0x05, 0x03, 0x00, 0x00 },
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, { 0x00, 0x41, 0x22, 0x1C, 0x00 }, { 0x14, 0x08, 0x3E, 0x08, 0x14 }, { 0x08, 0x08, 0x3E, 0x08, 0x08 },
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, { 0x08, 0x08, 0x08, 0x08, 0x08 }, { 0x00, 0x60, 0x60, 0x00, 0x00 }, { 0x20, 0x10, 0x08, 0x04, 0x02 },
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 }, { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 }, { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E }, { 0x00, 0x36, 0x36, 0x00, 0x00 }, { 0x00, 0x56, 0x36, 0x00, 0x00 },
    { 0x08, 0x14, 0x22, 0x41, 0x00 }, { 0x14, 0x14, 0x14, 0x14, 0x14 }, { 0x00, 0x41, 0x22, 0x14, 0x08 }, { 0x02, 0x01, 0x51, 0x09, 0x06 },
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, { 0x7E, 0x11, 0x11, 0x11, 0x7E }, { 0x7F, 0x49, 0x49, 0x49, 0x36 }, { 0x3E, 0x41, 0x41, 0x41, 0x22 },
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, { 0x7F, 0x49, 0x49, 0x49, 0x41 }, { 0x7F, 0x09, 0x09, 0x09, 0x01 }, { 0x3E, 0x41, 0x49, 0x49, 0x7A },
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, { 0x00, 0x41, 0x7F, 0x41, 0x00 }, { 0x20, 0x40, 0x41, 0x3F, 0x01 }, { 0x7F, 0x08, 0x14, 0x22, 0x41 },
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, { 0x7F, 0x04, 0x08, 0x10, 0x7F }, { 0x3E, 0x41, 0x41, 0x41, 0x3E },
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, { 0x3E, 0x41, 0x51, 0x21, 0x5E }, { 0x7F, 0x09, 0x19, 0x29, 0x46 }, { 0x46, 0x49, 0x49, 0x49, 0x31 },
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, { 0x3F, 0x40, 0x40, 0x40, 0x3F }, { 0x1F, 0x20, 0x40, 0x20, 0x1F }, { 0x3F, 0x40, 0x38, 0x40, 0x3F },
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, { 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 }, { 0x00, 0x7F, 0x41, 0x41, 0x00 },
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, { 0x00, 0x41, 0x41, 0x7F, 0x00 }, { 0x04, 0x02, 0x01, 0x02, 0x04 }, { 0x40, 0x40, 0x40, 0x40, 0x40 },
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, { 0x20, 0x54, 0x54, 0x54, 0x78 }, { 0x7F, 0x48, 0x44, 0x44, 0x38 }, { 0x38, 0x44, 0x44, 0x44, 0x20 },
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, { 0x38, 0x54, 0x54, 0x54, 0x18 }, { 0x08, 0x7E, 0x09, 0x01, 0x02 }, { 0x0C, 0x52, 0x52, 0x52, 0x3E },
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, { 0x00, 0x44, 0x7D, 0x40, 0x00 }, { 0x20, 0x40, 0x44, 0x3D, 0x00 }, { 0x7F, 0x10, 0x28, 0x44, 0x00 },
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, { 0x7C, 0x04, 0x18, 0x04, 0x78 }, { 0x7C, 0x08, 0x04, 0x04, 0x78 }, { 0x38, 0x44, 0x44, 0x44, 0x38 },
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, { 0x08, 0x14, 0x14, 0x18, 0x7C }, { 0x7C, 0x08, 0x04, 0x04, 0x08 }, { 0x48, 0x54, 0x54, 0x54, 0x20 },
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, { 0x3C, 0x40, 0x40, 0x20, 0x7C }, { 0x1C, 0x20, 0x40, 0x20, 0x1C }, { 0x3C, 0x40, 0x30, 0x40, 0x3C },
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, { 0x0C, 0x50, 0x50, 0x50, 0x3C }, { 0x44, 0x64, 0x54, 0x4C, 0x44 }, { 0x00, 0x08, 0x36, 0x41, 0x00 },
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, { 0x00, 0x41, 0x36, 0x08, 0x00 }, { 0x08, 0x04, 0x08, 0x10, 0x08 } };

static struct {
    u32 own;                         /* claimed buffer index + 1 (0: none) */
    u32 own_fb, alias_fb;            /* the claimed buffer, the game's other buffer (both HSD entries point at it while claimed) */
    u32 clean;                       /* the claimed buffer holds the panel */
    u32 page, npages, pad_prev, stale, paused;
    u32 claims, releases, vi_sets, draws, last_us, max_us, rows_drawn;
    char want[XP_ROWS][XP_COLS], shown[XP_ROWS][XP_COLS];
    u8 want_attr[XP_ROWS], shown_attr[XP_ROWS];
} XP;

static int xp_geom(u32 *fbw, u32 *h)
{
    *fbw = XP_LD16(XP_VIDATA + 4u);
    *h = XP_LD16(XP_VIDATA + 8u);
    return *fbw >= 2u * XP_COLS * XP_CW && *fbw <= 720u && (*fbw & 15u) == 0 && *h >= XP_ROWS * XP_PITCH && *h <= 576u;
}
static int xp_fb_ok(u32 fb) { return fb >= 0x80000000u && fb < 0x81800000u && !(fb & 31u); }

/* VI interrupt (HSD user pre-retrace callback, interrupts off). */
static void xp_retrace(void)
{
    if (!XP.own) {
        u32 fbw, h, k, s0, s1, b0, b1;
        if (XP_LD32(XP_NBXFB) != 2u || !xp_geom(&fbw, &h)) return;
        b0 = XP_LD32(XP_XFB_BUF(0)); b1 = XP_LD32(XP_XFB_BUF(1));
        s0 = XP_LD32(XP_XFB_ST(0)); s1 = XP_LD32(XP_XFB_ST(1));
        if (!xp_fb_ok(b0) || !xp_fb_ok(b1) || b0 == b1) return;
        /* the buffer to keep: one no EFB copy can be pending into (FREE or DISPLAY); prefer the one not on screen */
        k = s0 == XP_ST_FREE ? 0u : s1 == XP_ST_FREE ? 1u : s0 == XP_ST_DISPLAY ? 0u : s1 == XP_ST_DISPLAY ? 1u : 2u;
        if (k > 1u) return;
        XP.own_fb = k ? b1 : b0; XP.alias_fb = k ? b0 : b1;
        XP_ST32(XP_XFB_BUF(k), XP.alias_fb);
        XP.own = k + 1u; XP.clean = 0; XP.claims++;
        /* shown from now on: until the game thread has filled it, it still holds the game's last frame there (a still picture) */
    }
    if (XP.stale < XP_STALE_RETRACES) XP.stale++;
    if (XP.stale >= XP_STALE_RETRACES) { XP.paused = 1; return; }
    XP.paused = 0;
    XP_VISETNEXT(XP.own_fb);
    XP_VIFLUSH();
    XP.vi_sets++;
}

/* Give the buffer back to the game (interrupts off).  VI keeps the old page until HSD sets its next buffer (one frame). */
static __attribute__((unused)) void xp_release(void)
{
    if (!XP.own) return;
    if (XP_LD32(XP_XFB_BUF(XP.own - 1u)) == XP.alias_fb) XP_ST32(XP_XFB_BUF(XP.own - 1u), XP.own_fb);
    XP.own = 0; XP.clean = 0; XP.releases++;
}

/* Game thread, once per frame: keeps the VI override alive. */
static void xp_tick(void) { XP.stale = 0; }

/* D-pad Left / Right alone switch pages (L, R, Z not held); returns 1 when the page changed. */
static int xp_pad(u32 btn)
{
    u32 edge = btn & ~XP.pad_prev, n = XP.npages ? XP.npages : 1u;
    XP.pad_prev = btn;
    if (btn & 0x70u) return 0;
    if (edge & 0x2u) { XP.page = (XP.page + 1u) % n; return 1; }
    if (edge & 0x1u) { XP.page = (XP.page + n - 1u) % n; return 1; }
    return 0;
}

/* ---- layout: logical rows -> word-wrapped display lines -> pages ---- */
static u32 xp_len(const char *s, u32 max) { u32 n = 0; while (n < max && s[n]) n++; while (n && s[n - 1] == ' ') n--; return n; }
/* one display line of a logical row: from s+pos, returns the next position (0 = row done); ind = indent of continuation lines */
static u32 xp_wrap(const char *s, u32 n, u32 pos, char *out, u32 *outn)
{
    u32 ind = 0, room, end, brk, k = 0;
    while (ind < n && s[ind] == ' ') ind++;
    if (pos) { u32 j; ind = ind + 2u > 12u ? 12u : ind + 2u; for (j = 0; j < ind; j++) out[k++] = ' '; while (pos < n && s[pos] == ' ') pos++; }
    room = XP_COLS - k;
    if (n - pos <= room) { end = n; brk = n; }
    else {
        end = pos + room;
        brk = end;                                    /* prefer a column gap (two blanks), then any blank, in the last two thirds */
        while (brk > pos + room / 3u && !(s[brk] == ' ' && s[brk - 1] == ' ')) brk--;
        if (!(s[brk] == ' ' && s[brk - 1] == ' ')) {
            brk = end;
            while (brk > pos + room / 3u && s[brk] != ' ') brk--;
        }
        if (s[brk] != ' ') brk = end;                 /* no blank at all: hard break */
        end = brk;
    }
    while (pos < end) out[k++] = s[pos++];
    *outn = k;
    return pos < n ? pos : 0;
}
static u32 xp_rowlines(const char *s, u32 w)
{
    char t[XP_COLS];
    u32 n = xp_len(s, w), pos = 0, c = 0, k;
    if (!n) return 1;
    do { pos = xp_wrap(s, n, pos, t, &k); c++; } while (pos);
    return c;
}
static void xp_put(u32 row, const char *s, u32 n, u8 attr)
{
    u32 j;
    if (row >= XP_ROWS) return;
    for (j = 0; j < XP_COLS; j++) XP.want[row][j] = j < n ? s[j] : ' ';
    XP.want_attr[row] = attr;
}
static u32 xp_emit(u32 row, const char *s, u32 w, u8 attr)
{
    char t[XP_COLS];
    u32 n = xp_len(s, w), pos = 0, k;
    if (!n) { xp_put(row, "", 0, attr); return row + 1u; }
    do { pos = xp_wrap(s, n, pos, t, &k); xp_put(row++, t, k, attr); } while (pos);
    return row;
}
/* rows: nrows logical rows of stride bytes (NUL-terminated or space-padded; an empty row is skipped); flags per row (may be 0);
   title: the header's left part ("page n/m  D-pad </>" is added on the right).  Builds the selected page into XP.want. */
static void xp_layout(const char *rows, u32 stride, u32 nrows, const u8 *flags, const char *title)
{
    u32 r, pin = 0, room, page = 0, used = 0, sel, j;
    for (r = 0; r < nrows; r++) if (rows[r * stride] && flags && (flags[r] & XP_F_PIN)) pin += xp_rowlines(rows + r * stride, stride);
    if (pin > XP_BODY / 2u) pin = XP_BODY / 2u;
    room = XP_BODY - pin;
    sel = XP.page;
    for (j = 1; j < XP_ROWS; j++) xp_put(j, "", 0, XP_A_BODY);
    for (r = 0; r < nrows; ) {                       /* one block per pass: the row r and the following rows up to the next block start */
        u32 e = r + 1u, bl = 0, q;
        const char *s = rows + r * stride;
        if (!*s || (flags && (flags[r] & XP_F_PIN))) { r++; continue; }
        while (e < nrows && !(flags && (flags[e] & XP_F_BLOCK))) e++;
        for (q = r; q < e; q++) if (rows[q * stride] && !(flags && (flags[q] & XP_F_PIN))) bl += xp_rowlines(rows + q * stride, stride);
        if (used && used + bl > room) { page++; used = 0; }         /* the block does not fit on this page: start the next */
        for (q = r; q < e; q++) {
            const char *t = rows + q * stride;
            u32 nl;
            if (!*t || (flags && (flags[q] & XP_F_PIN))) continue;
            nl = xp_rowlines(t, stride);
            if (used && used + nl > room) { page++; used = 0; }   /* a block longer than a page: split between rows */
            if (page == sel) (void)xp_emit(1u + used, t, stride, flags && (flags[q] & XP_F_HI) ? XP_A_HI : XP_A_BODY);
            used += nl;
        }
        r = e;
    }
    XP.npages = page + 1u;
    if (XP.page >= XP.npages) { XP.page = XP.npages - 1u; xp_layout(rows, stride, nrows, flags, title); return; }
    if (pin) {                                       /* pinned rows at the bottom of every page */
        u32 at = XP_ROWS - pin;
        for (r = 0; r < nrows && at < XP_ROWS; r++)
            if (rows[r * stride] && flags && (flags[r] & XP_F_PIN)) at = xp_emit(at, rows + r * stride, stride, XP_A_HI);
    }
    {   /* header: title on the left, "page n/m  D-pad </>" on the right */
        char h[XP_COLS + 1], tail[24], *p = tail;
        u32 tn, n = 0, k;
        const char *a = "page ";
        while (*a) *p++ = *a++;
        if (XP.page + 1u >= 10u) *p++ = (char)('0' + (XP.page + 1u) / 10u % 10u);
        *p++ = (char)('0' + (XP.page + 1u) % 10u); *p++ = '/';
        if (XP.npages >= 10u) *p++ = (char)('0' + XP.npages / 10u % 10u);
        *p++ = (char)('0' + XP.npages % 10u);
        a = "  D-pad </>";
        while (*a) *p++ = *a++;
        tn = (u32)(p - tail);
        for (k = 0; k < XP_COLS; k++) h[k] = ' ';
        while (title && title[n] && n < XP_COLS - tn - 1u) { h[n] = title[n]; n++; }
        for (k = 0; k < tn; k++) h[XP_COLS - tn + k] = tail[k];
        xp_put(0, h, XP_COLS, XP_A_HEAD);
    }
}

/* ---- drawing (game thread) ---- */
static u32 xp_lw[360];                                       /* one frame-buffer line (fbWidth <= 720) */
static void xp_line(u32 a, u32 n) { u32 i; for (i = 0; i < n; i++) XP_ST32(a + 4u * i, xp_lw[i]); }
static void xp_draw_row(u32 fb, u32 fbw, u32 y0, u32 row)
{
    u32 words = fbw / 2u, x0 = (words - XP_COLS * XP_CW) / 2u, l, j, c, fx, built = 99u;
    u8 attr = XP.want_attr[row];
    u32 bg = attr == XP_A_HEAD ? XP_HEAD_BG : XP_BG, fg = attr == XP_A_HEAD ? XP_HEAD_FG : attr == XP_A_HI ? XP_HI : XP_FG;
    for (l = 0; l < XP_PITCH; l++) {
        u32 key = (l >= 1u && l <= 14u) ? (l - 1u) / 2u : 7u;          /* glyph row 0..6 (two lines each), 7 = blank line */
        if (key != built) {
            for (j = 0; j < words; j++) xp_lw[j] = bg;
            if (key < 7u)
                for (c = 0; c < XP_COLS; c++) {
                    u32 ch = (u8)XP.want[row][c];
                    const u8 *g;
                    if (ch == ' ') continue;
                    g = xp_font[ch >= 32u && ch <= 126u ? ch - 32u : '?' - 32u];
                    for (fx = 0; fx < 5u; fx++) if ((g[fx] >> key) & 1u) xp_lw[x0 + c * XP_CW + fx] = fg;
                }
            built = key;
        }
        xp_line(fb + (y0 + row * XP_PITCH + l) * fbw * 2u, words);
    }
}
/* Draws what changed (all of it after a claim) into the claimed buffer; returns the microseconds spent (0: nothing to do). */
static u32 xp_draw(void)
{
    u32 fbw, h, fb, y0, r, t0, any = 0;
    if (!XP.own || !xp_geom(&fbw, &h)) return 0;
    fb = (XP.own_fb & 0x01FFFFFFu) | 0xC0000000u;           /* uncached mirror: VI reads memory, no cache maintenance needed */
    y0 = (h - XP_ROWS * XP_PITCH) / 2u;
    t0 = XP_NOW_US();
    if (!XP.clean) {                                         /* first fill: the whole buffer dark, every row drawn below */
        u32 j, y;
        for (j = 0; j < fbw / 2u; j++) xp_lw[j] = XP_BG;
        for (y = 0; y < h; y++) if (y < y0 || y >= y0 + XP_ROWS * XP_PITCH) xp_line(fb + y * fbw * 2u, fbw / 2u);
        for (r = 0; r < XP_ROWS; r++) XP.shown_attr[r] = 0xFF;
    }
    for (r = 0; r < XP_ROWS; r++) {
        u32 j, same = XP.shown_attr[r] == XP.want_attr[r];
        for (j = 0; j < XP_COLS && same; j++) same = XP.shown[r][j] == XP.want[r][j];
        if (same) continue;
        xp_draw_row(fb, fbw, y0, r);
        for (j = 0; j < XP_COLS; j++) XP.shown[r][j] = XP.want[r][j];
        XP.shown_attr[r] = XP.want_attr[r];
        XP.rows_drawn++; any = 1;
    }
    XP.clean = 1;
    if (!any) return 0;
    {
        u32 d = XP_NOW_US() - t0;
        if (!d) d = 1;
        XP.draws++; XP.last_us = d; if (d > XP.max_us) XP.max_us = d;
        return d;
    }
}
#endif
