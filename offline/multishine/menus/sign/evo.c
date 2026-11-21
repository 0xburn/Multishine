/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The shared open sequence of the Event-view screens: see evo.h.  CONSOLE ONLY (-DGNB_MELEE, ms/sign.ovl).  Retail facts (NTSC 1.02) from the decompilation
 * (MnEventData, mnEvent_8024D0CC, the screen's joints). Original work; no retail code or data is copied.
 */
#ifdef GNB_MELEE
#include "evo.h"

typedef unsigned int u32;
typedef unsigned char u8;
#define W(a) (*(volatile u32 *)(a))
#define B(a) (*(volatile u8 *)(a))
#define MENU_FLOW 0x804A04F0u                  /* MenuFlow: +0 cur_menu, +0x11 entering_menu */

typedef void (*fn_p)(void *);
typedef void (*fn_pi)(void *, int);
typedef void (*fn_pu)(void *, u32);
typedef int (*fn_jobj)(void *, void **, ...);
#define GOBJ_FREE ((fn_p)0x80390228u)          /* HSD_GObjFree */
#define TEXT_FREE ((fn_p)0x803A5CC4u)          /* HSD_SisLib_803A5CC4 */
#define EV_PORTRAIT ((fn_pi)0x8024D0CCu)       /* mnEvent_8024D0CC(screen gobj, ckind) */
#define LB_JOBJ ((fn_jobj)0x80011E24u)         /* lb_80011E24(tree, &jobj, index, -1) */
#define JOBJ_SETFLAGS ((fn_pu)0x80371D9Cu)     /* HSD_JObjSetFlagsAll */
#define JOBJ_HIDDEN 0x10u
#define CHKIND_NONE 0x21                       /* the portrait's frame 25: the empty box */
#define MD_GOBJS 0x08u                         /* MnEventData: +8 gobjs[9], +0x74 name_text, +0x78 desc_text */
#define MD_NAME 0x74u
#define MD_DESC 0x78u

static struct evr R;
static unsigned R_n, R_shown, R_chrome;

static void hide(void *scr, int idx)
{
    void *j = 0;
    (void)LB_JOBJ((void *)W((u32)scr + 0x28u), &j, idx, -1);
    if (j) JOBJ_SETFLAGS(j, JOBJ_HIDDEN);
}

void evo_prep(void *scr, unsigned nvis)
{
    u8 *md = (u8 *)W((u32)scr + 0x2Cu);
    unsigned i;
    for (i = 0; i < nvis; i++) {                                             /* the retail marks go: our rows replace them */
        u32 go = W((u32)md + MD_GOBJS + 4u * i);
        if (go) { GOBJ_FREE((void *)go); W((u32)md + MD_GOBJS + 4u * i) = 0; }
    }
    if (W((u32)md + MD_DESC)) { TEXT_FREE((void *)W((u32)md + MD_DESC)); W((u32)md + MD_DESC) = 0; }
    if (W((u32)md + MD_NAME)) { TEXT_FREE((void *)W((u32)md + MD_NAME)); W((u32)md + MD_NAME) = 0; }
    B(MENU_FLOW + 0x11u) = 1;                                                /* entering_menu, as the retail 1-P think sets it before mnEvent_8024E838: the main menu
                                                                                panel (fn_80229BF4) then plays the Event kind's ENTER range (mn_803EAE8C[7][0], frames
                                                                                1400..1449: the zoom-in) instead of the previous kind's leave range */
    EV_PORTRAIT(scr, CHKIND_NONE);
    hide(scr, 9);                                                            /* the event type label */
    hide(scr, 4);                                                            /* the "Select Character" box (a screen that wants it shows it again) */
    hide(scr, 6);                                                            /* the High Score strip: the screens' headings stand there */
    evr_open(&R);
    R_n = 0; R_shown = 0; R_chrome = 0;
}

int evo_frame(unsigned n_rows)
{
    unsigned k, c;
    evr_tick(&R, n_rows);
    k = evr_rows(&R);
    if (k > n_rows) k = n_rows;
    c = (unsigned)evr_chrome(&R);
    if (k == R_shown && c == R_chrome && n_rows == R_n) return 0;
    R_shown = k; R_chrome = c; R_n = n_rows;
    return 1;
}

unsigned evo_rows(void) { return evr_rows(&R); }
int evo_chrome(void) { return evr_chrome(&R); }
void evo_page(void) { evr_page(&R); }
void evo_skip(void) { evr_skip(&R); }
unsigned evo_proof(void) { return R_shown > 15u ? 15u : R_shown; }
#endif
