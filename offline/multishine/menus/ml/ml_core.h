/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Menu layout runtime, pure part: the table of the screens we rebuild and the rules that need no Melee
 * address: which screen a kind is, how the cursor steps over hidden rows, what a Confirm does, how big a label may be drawn.  Strict C89, no
 * libc, no globals, no FPU; host-tested (tests/ml_test.c).  The table itself is generated from our layout file by
 * tools/menu_layout.py cheader (ml_table.h). */
#ifndef ML_CORE_H
#define ML_CORE_H

#define ML_A_NONE 0      /* no row */
#define ML_A_KIND 1      /* open Melee menu kind `arg`, cursor on row `hov` */
#define ML_A_ONLINE 2    /* open Slippi's own online screen (their switch function, kind 8) */
#define ML_A_STUB 3      /* not built yet: nothing happens */
#define ML_A_MODE 4      /* start Slippi's online mode `arg` (0 Ranked, 1 Unranked, 2 Direct, 3 Teams, 4 Party: Slippi's OPTION_*_IDX) */
#define ML_A_SCENE 6     /* leave the menu scene for game mode `arg` (Couch: the stock VS. Mode > Melee row, game mode 2) */
#define ML_A_PAGE 5      /* open the stub text screen `arg` of ml_pages */
#define ML_A_SIGN 7      /* open the sign overlay on its page `arg` (sign/sg_ui.h: hub Game Night SG_GN, hub Standings SG_STAND; the -DML_MOCKUPS=1 Main Menu row Mockups SG_MOCK) */

#ifndef ML_MOCKUPS
#define ML_MOCKUPS 0     /*: the Main Menu row Mockups is out of the real game; -DML_MOCKUPS=1 (development builds only) puts it back as the sixth row (ml_table.h) */
#endif

/* (kind 10, the Online submenu / hub screen, is gone since the flat Main Menu, its modes are rows of the Main Menu; slot 10 is free again) */
#define ML_KIND_MORE 14  /* the More screen */
#define ML_KIND_MISC 11  /* the Original Menu screen (was Miscellaneous: the five retail rows, kind 0's look; the third free slot, tools/menu_layout.py FREE_KINDS) */
#define ML_KIND_NONE 255
#define ML_NFRAMES 18      /* entries of the frame table: 0 none, then the donor frames and Slippi's label frames (tools/menu_layout.py ART_FRAMES) */

struct ml_row {
    const char *label;       /* the name (menu charset) */
    const char *desc;        /* line under the panel while hovered ("" = the retail description stays) */
    unsigned char act, arg, hov;
    unsigned char hidden;    /* our hid it: skipped by the cursor, no plate */
    unsigned char text;      /* 1 = we draw the label (empty plate), 0 = the retail label art is used */
    unsigned char tex;       /* index in ml_tex (our own plate art for this label), 255 = none */
    unsigned char didx;      /* the placeholder image of the label animation that tex goes into (retail image index) */
    unsigned char art;       /* index in the label stub's frame table (ml_stub.S ml_ftab): the frame to request for this row's plate, 0 = no art, draw text */
};

struct ml_screen {
    unsigned char kind;      /* Melee menu kind (mn_803EB6B0 index) */
    unsigned char nrows;
    unsigned char text;      /* every row's label is drawn by us */
    unsigned char parent_kind, parent_pos;   /* Back goes here (ML_KIND_NONE: the title screen) */
    const struct ml_row *rows;
};

/* a stub text screen ( kind T): title, one note line, rows, footer */
struct ml_page {
    const char *title;
    const char *note;
    unsigned char nrows;
    const char *const *rows;
};

/* the generated table */
extern const struct ml_screen *const ml_tab;
extern const unsigned ml_tab_n;
extern const struct ml_page *const ml_page_tab;
extern const unsigned ml_page_n;

extern const char *const *const ml_tex_text;             /* the plate label texts of the generated ml_tex.h, composed at run time (ml_font_label) */
extern const unsigned ml_tex_n;
extern const char *const *const ml_tab_text;               /* title tab texts of ml_tex.h (composed at run time, ml_font_to_i4) */
/* plate label from Melee's own menu font: atlas = HSD_SisLib_FontAtlas (main.dol RAM 0x8040CD40), dst = 5632 bytes (176 x 30 IA4 tiles,
   padded to 32 rows), plane = 5280 scratch bytes.  ml_font_sum(atlas) must equal ML_FONT_SUM before the atlas is trusted.  Returns 0, or -1 for text it cannot draw. */
int ml_font_to_i4(unsigned char *dst, unsigned dw, unsigned dh, int x0, int y0, const unsigned char *src, unsigned k16, unsigned maxw, int shear8, unsigned xs16);   /* see ml_core.c: plate label -> I4 image, same menu font */
extern unsigned ml_font_desc16;      /* ml_core.c: descender row density of ml_font_label */
unsigned ml_news_lines(unsigned online, unsigned unread, unsigned req, unsigned inv, char out[5][16]);   /* the Friends panel lines of the Main Menu, see ml_core.c */
#define ML_FONT_ATLAS 0x8040CD40u
#define ML_FONT_SUM 0xCE6C0CF4u
unsigned ml_font_sum(const unsigned char *atlas);
int ml_font_label(unsigned char *dst, const unsigned char *atlas, const char *text, unsigned char *plane);
/* text for HSD_SisLib_803A6B98 with the punctuation of Melee's font that its ASCII encoder lacks (! ? ; [ ] + / % # & * @ $ = < > ~ | _ ^ { }) written as full-width Shift-JIS codes;
   returns the length, 0 when it does not fit in cap bytes */
unsigned ml_sis_expand(char *out, unsigned cap, const char *in);
#define ML_TAB_SLIPPI 0
#define ML_TAB_MAIN 1
#define ML_TAB_MORE 2   /* the More screen (kind 14), */
#define ML_TAB_ORIG 3   /* the Original Menu (kind 11) title strip, */
#define ML_TAB_GRAND 4  /* Event-view headers (sign overlay pages) */
#define ML_TAB_FRIENDS 5

const struct ml_screen *ml_screen_for_kind(unsigned kind);     /* 0 when the kind is not ours */
int ml_visible_rows(const struct ml_screen *s);
/* cursor step: dir -1 up, +1 down, wraps, skips hidden rows; returns the new row (the old one when no row is visible) */
int ml_step(const struct ml_screen *s, int sel, int dir);
int ml_first_visible(const struct ml_screen *s);
/* the row of `s` that starts online mode `mode` (the cursor lands there when the player comes back from the online screen), -1 when none */
int ml_row_of_mode(const struct ml_screen *s, unsigned mode);
/* the row record of (kind, sel), 0 when it is not ours */
const struct ml_row *ml_row_of(unsigned kind, unsigned sel);
/* 1 when the plate of (kind, sel) must be the empty plate (we draw its label), else 0 */
int ml_label_empty(unsigned kind, unsigned sel);
/* unlock rule for Melee's mn_80229938: 0 no opinion, 1 hidden, 2 shown (our own screens), 1 for the retail rows our tree hides (ml_hide_tab) */
int ml_unlock(unsigned kind, unsigned sel);
/* label size class from the number of characters: 0 is the retail row size, higher is smaller (see ml_size_bits in ml_glue.c) */
int ml_size_class(const char *label);
#define ML_SIZE_CLASSES 5

/* The Konami code on the Settings list reveals Diagnostics for the session: up up down down left right left right B A on the pad.  One call per frame with the buttons NEWLY pressed this
   frame (`edge`, HSD bits: left 1, right 2, down 4, up 8, A 0x100, B 0x200) and a frame counter; holding a button counts once (an empty edge changes nothing), a press of the wrong A or B starts over (a wrong direction and extra bits next to the expected button are ignored), and more than ML_KONAMI_GAP frames between two presses starts over.  Returns 1 on the frame the last press completes the code (the state is then reset). */
#define ML_KONAMI_GAP 300u        /* frames of the menu (60 a second): five seconds between two presses */
#define ML_KONAMI_MASK 0x030Fu
struct ml_konami { unsigned char idx; unsigned last; };
unsigned ml_konami_step(struct ml_konami *k, unsigned edge, unsigned now);


/* ---- online character select header names  ----
 * Slippi's CSS text module (hook 0x80264534, codeset flag ms-names) carries a marker word at body+8: 0 = Slippi's own flow (its header words stay), 1 = Grandmaster,
 * 2 = Dojo, 3 = Mano a mano, 4 = Game Night (headers "<name> Mode").  A hub row that starts an online mode writes the marker of its mode, every menu scene entry clears it.  The body is
 * found through the hook word (`b body`); it is accepted only when it has the generated header: a `blrl` at body+4 and "Gran" at body+12 (any other codeset: nothing is written). */
#define ML_CSS_HOOK 0x80264534u
#define ML_CSS_BLRL 0x4E800021u
#define ML_CSS_GRAN 0x4772616Eu
/* In-match delay HUD (codeset flag ms-hud-delay, InitInGame.asm C2): flag word at body+8 (byte 11), 0 = no "Delay: %df" text, 1 = shown; blrl at body+4 and 'MSHD' at body+12 identify it. */
#define ML_IGD_HOOK 0x802F6694u
#define ML_IGD_MAGIC 0x4D534844u
#define ML_IGD_SCAN 0x80u            /* bytes after the header searched for the delay text's float pair */
#define ML_IGD_SIZE_STD 0x3EA8F5C3u  /* 0.33f: Slippi's delay text size (DOFST_TEXT_SIZE) */
#define ML_IGD_SIZE_BIG 0x3ED70A3Du  /* 0.42f: the size of the Delay and Ping lines */
#define ML_IGD_X_STD 0x43870000u     /* 270.0f: Slippi's x of the delay text (right edge) */
#define ML_IGD_X_MOVED 0x43260000u   /* 166.0f (was 180, size 0.33): the delay text's x when the ping line is on too (see bh_ping_tick: the ping line takes 270.0) */
#define ML_IGD_Y 0x434F0000u         /* 207.0f */
#define ML_IGD_XC_PING 0xC1180000u   /* -9.5f (was -7, size 0.33):  Delay right edge when it shares a centred row with the Ping line (above / below the timer) */
#define ML_IGD_XC_ALONE 0x422C0000u  /* 43.0f (was 36, size 0.33):  Delay right edge when it is alone in the centred row */
#define ML_IGD_Y_ABOVE 0xC3650000u   /* -229.0f (was -233):  above the timer */
#define ML_IGD_Y_BELOW 0xC32B0000u   /* -171.0f (was -191, -188):  below the timer */
/* Raise HUD (codeset flag ms-hud-raise): level byte at body+11, blrl at body+4, 'MSHR' at body+12 */
#define ML_HUR_HOOK 0x802F368Cu      /* the last instruction (blr) of ifAll_802F343C, after the HUD position table is filled */
#define ML_HUR_MAGIC 0x4D534852u
#define ML_CSS_GAME_NIGHT 4u                                         /* set by the Game Night call-up (go_gn_play), not by a hub mode */
unsigned ml_css_marker(unsigned mode);                              /* online mode (0 Ranked, 1 Unranked, 2 Direct, 3 Teams, 4 Party) -> marker 1..3, 0 for Teams / Party / unknown */
unsigned ml_css_body(unsigned hook_addr, unsigned hook_word);
unsigned ml_online_data_missing(unsigned panel);                    /* 1 when ms_net_cost(107) says the Multishine system files (slpCSS.dat) or the pack are unusable: Dojo, Grandmaster, Mano a mano and the Game Night call-up must not enter the online scene */       /* body address a relative `b` at hook_addr jumps to (RAM only), 0 for anything else */

#endif

/* Centring: the whole row is centred on the timer's centre x = +10 canvas units (measured: timer "07:50 73" centre, 12.7 units right of the old group centre -3).
   Typical text widths (units): "Delay: 2f" 52, "Ping: 83 ms" 69, gap 17.  Both: group centre = T. See tests/test_hudpos.py.  Right edges: Delay -7 / Ping +79 (both), Delay 36 alone, Ping 44.5 alone. */
#define ML_IGD_PX_PING 0x42BF0000u   /* 95.5f (was 79, size 0.33): Ping right edge with the Delay text on */
#define ML_IGD_PX_ALONE 0x42580000u  /* 54.0f (was 44.5, size 0.33): Ping right edge alone */
