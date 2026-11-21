/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * tt_glue: OUR line on the title screen, on the console.  Card route only (profile s17-compat-onbc); one call per
 * scene-loop iteration from br_tick (offline/bridge/br_glue.c, a weak call: images without this unit are unchanged).
 *
 * When the title scene (major 0) has loaded its archive (GmTtAll.usd / .dat, gm_Scene_Title_OnEnter -> gmTitle_801A1AC0, which fills 's
 * model_desc_0 at 0x80479B28 with TtlMoji_Top_joint ...), once per title visit:
 *   1. walk the joint descriptors to joint 6 (the legal notice quad, one I4 image 496 x 48) and CHECK every field we rely on against the retail archive: joint flags,
 *      one DObj / MObj / TObj / PObj each, image size and format, the vertex descriptor (POS index16 f32, stride 12; TEX0 index8), the display list (one 4-vertex
 *      triangle strip) and the exact float bit patterns of the four corners;  check Melee's glyph atlas in RAM (ml_font_sum);
 *   2. compose OUR image (tt_core.c: the logo and the line, centred, 496 x 48 I4) into a buffer of the scene heap;
 *   3. build COPIES of the joint / DObj / MObj / material / TObj / image descriptors (next and child cleared, the image pointing at our buffer, the joint at its fixed
 *      place), load them with HSD_JObjLoadJoint into a gobj of our own on the title's text layer (gx link 9, as gmTitle_801A165C does), static: no animation is
 *      attached.  The game's own notice quad is NEVER written (its descriptors are only read, the vertex array and display list are shared read-only);
 *   4. the fade-in: tt_frame sets the runtime material's alpha from the generated table, one entry per frame, then it stays at full.
 * The mark is the console's network indicator (ml_mark_level.h): the mark is its OWN quad (a second copy of the notice's chain) with a mark-only image, the text another
 * image on the first quad.  The mark quad's material colour (diffuse, an integer tint, the image is white coverage) carries the state: link down = dim grey (187 = 11/15), link up = white,
 * link up AND a server answer heard since boot (ms_net_cost 103 true once; the title asks the server once, the update check, and the menus' 16 s silence rule would flip it by itself) =
 * the brand cyan; the colour fades between the states (ml_mark_level.h steps).  No network function in the image: the dim mark.
 * Any failed check, a missing atlas or no heap: nothing is created, the title stays retail.  The scene's gobjs are freed with the scene, the descriptors and the
 * image live in the scene heap: nothing is restored on the way out.
 */
#include "tt_core.h"
#include "tt_text.h"
#include "../ml/ml_mark_level.h"            /* the mark as an online indicator: two brightnesses, the title swaps between two composed images */
/* the loader's status block: list base + 0x3600 of the card layout the engine is built for (stock 0x817F7600, CL_LAYOUT=generic 0x817E3600, low 0x817C3600); MS_CARD_WA_ADDR is the layout's loader work area (release.sh WA_ADDR, card_payload.py) */
#if defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817D4000u
#define MS_CL_STATUS_ADDR 0x817E3600u
#elif defined(MS_CARD_WA_ADDR) && (MS_CARD_WA_ADDR) == 0x817B4000u
#define MS_CL_STATUS_ADDR 0x817C3600u
#else
#define MS_CL_STATUS_ADDR 0x817F7600u
#endif

typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define W(a) (*(volatile u32 *) (a))
#define H(a) (*(volatile u16 *) (a))
#define B(a) (*(volatile u8 *) (a))

#define SCENE_MAJOR B(0x80479D30u)            /* current major scene: 0 = title */
#define TT_MODEL 0x80479B28u                  /* model_desc_0: joint, animjoint, matanim_joint, shapeanim_joint (filled by gmTitle_801A1AC0) */
#define TT_COUNTDOWN W(0x804D6714u)           /* countdown_timer (r13-0x4F8C): 20 at OnEnter, then -1 per frame */
#define TT_FRAMES W(0x804D6718u)              /* frame_count (r13-0x4F88): 0 at OnEnter */
#define HSD_MEMALLOC ((void *(*) (u32)) 0x8037F1E4u)
#define HSD_FREE ((void (*)(void *)) 0x8037F1B0u)
#define DC_STORE ((void (*)(void *, u32)) 0x80344840u)       /* DCStoreRange */
#define GX_INV_TEX ((void (*)(void)) 0x8033F270u)            /* GXInvalidateTexAll */
#define GX_INV_VTX ((void (*)(void)) 0x8033C898u)            /* GXInvalidateVtxCache */
#define TT_FONT_SUM 0xCE6C0CF4u                               /* = ML_FONT_SUM (ml_core.h): the retail atlas (the host test checks the two agree) */
extern unsigned ml_font_sum(const unsigned char *atlas);
extern unsigned ms_net_cost(int i) __attribute__((weak));      /* on_backend.c: 107 = link / server bits, 103 = the server answered within 30 s */

#define J_FLAGS 0x00080008u                   /* joints 6 and 7 of the retail TtlMoji tree */
#define TT_JN 6

/* RAM proof block (read by the Dolphin scenarios): magic, state of this visit (0 waiting, 1 installed, 2 refused), installs, refusals, last refusal step,
   overlap fix applied, buffer address, FNV of the composed image, title visits seen, calls on the title */
enum { T_MAGIC, T_STATE, T_DONE, T_REFUSED, T_WHY, T_FIX, T_BUF, T_SUM, T_VISITS, T_TICKS, T_LEVEL, T_BUF2, T_CHK, T_RGB, T_N };   /* T_LEVEL: the target 1 white (link) / 2 dim / 3 cyan (server answered), T_BUF2: the mark image, T_RGB: the tint now */
volatile u32 tt_st[T_N] __attribute__((section(".data"))) = { 0x54545354u /* 'TTST' */ };

static u8 g_state, g_seen;
static u32 g_fc;

static int okp(u32 p)
{
    return p >= 0x80000000u && p < 0x81800000u && (p & 3u) == 0u;
}

/* the n-th joint descriptor in depth-first order (child +8, next +0xC), as lb_80011E24 counts them */
static u32 jnth(u32 o, int *n, int depth)
{
    while (okp(o) && depth < 8) {
        u32 c, r;
        if ((*n)-- == 0)
            return o;
        c = W(o + 8u);
        if (c) {
            r = jnth(c, n, depth + 1);
            if (r)
                return r;
        }
        o = W(o + 0xCu);
    }
    return 0;
}

struct quad { u32 img, j, d, m, t; u32 pos[4]; };      /* the image descriptor, the joint / DObj / MObj / TObj descriptors, the addresses of the four position entries */

/* joint `jn`: one DObj, one MObj with one TObj (I4 w x h), one PObj with POS (index16, f32 xyz, stride 12) + TEX0 (index8) and one 4-vertex strip */
static int quad_of(u32 root, int jn, u32 w, u32 h, struct quad *q)
{
    int n = jn;
    u32 j = jnth(root, &n, 0), d, m, t, im, p, vd, dl, arr, i, o;
    if (!j || W(j + 4u) != J_FLAGS)
        return 1;
    d = W(j + 0x10u);
    if (!okp(d) || W(d + 4u) != 0u)
        return 2;
    m = W(d + 8u);
    t = okp(m) ? W(m + 8u) : 0u;
    if (!okp(t) || W(t + 4u) != 0u)
        return 3;
    im = W(t + 0x4Cu);
    if (!okp(im) || H(im + 4u) != w || H(im + 6u) != h || W(im + 8u) != 0u || !okp(W(im)))
        return 4;
    p = W(d + 0xCu);
    if (!okp(p) || W(p + 4u) != 0u)
        return 5;
    vd = W(p + 8u);
    if (!okp(vd) || W(vd) != 9u || W(vd + 4u) != 3u || W(vd + 8u) != 1u || W(vd + 0xCu) != 4u || H(vd + 0x12u) != 12u ||
        W(vd + 0x18u) != 13u || W(vd + 0x1Cu) != 2u || W(vd + 0x30u) != 0xFFu)
        return 6;
    arr = W(vd + 0x14u);
    dl = W(p + 0x10u);
    if (!okp(arr) || !dl || dl < 0x80000000u || (B(dl) & 0xF8u) != 0x98u || B(dl + 1u) != 0u || B(dl + 2u) != 4u)
        return 7;
    for (i = 0; i < 4u; i++) {
        o = dl + 3u + i * 3u;
        q->pos[i] = arr + (((u32) B(o) << 8) | B(o + 1u)) * 12u;
    }
    q->img = im;
    q->j = j;
    q->d = d;
    q->m = m;
    q->t = t;
    return 0;
}

/* the four corners hold exactly the retail values: x in {xl, xr} (two each), y in {ylo, yhi} (two each) */
static int corners(const struct quad *q, u32 xl, u32 xr, u32 ylo, u32 yhi)
{
    u32 i, nl = 0, nr = 0, nlo = 0, nhi = 0;
    for (i = 0; i < 4u; i++) {
        u32 x = W(q->pos[i]), y = W(q->pos[i] + 4u);
        nl += x == xl;
        nr += x == xr;
        nlo += y == ylo;
        nhi += y == yhi;
    }
    return nl == 2u && nr == 2u && nlo == 2u && nhi == 2u;
}

#define GOBJ_NEW ((void *(*) (int, int, int)) 0x803901F0u)          /* GObj_Create(classifier, p_link, priority) */
#define GOBJ_OBJ ((void (*) (void *, int, void *)) 0x80390A70u)      /* HSD_GObjObject_80390A70(gobj, kind, obj) */
#define GOBJ_LINK ((void (*) (void *, void *, int, int)) 0x8039069Cu) /* GObj_SetupGXLink(gobj, render, gx_link, priority) */
#define GOBJ_FREE ((void (*) (void *)) 0x80390228u)                   /* HSD_GObjFree */
#define JOBJ_LOAD ((void *(*) (void *)) 0x80370E44u)                  /* HSD_JObjLoadJoint */
#define JOBJ_DRAW 0x80391070u                                         /* HSD_GObj_JObjCallback */
#define JOBJ_KIND B(0x804D7849u)                                      /* HSD_GObj_JObjKind */
#define CLASS_UI 14                                                   /* HSD_GOBJ_CLASS_UI */
#define OUR_FLAGS 0x20080008u                                         /* the notice's joint flags (XLU, classical scaling) plus ROOT_XLU: it is a root now */

static u32 g_mat, g_mat2;   /* the runtime materials of our two quads, text and mark (0: none yet) */
static u32 *g_idw2;         /* the mark quad's image descriptor copy (word 0 = the image address): the normal or, with MARK_BLUE_FADEOFF, the faint-pairs image in the cyan state */
static u32 g_imgm[2];
static u8 g_heard, g_dec;   /* a server answer was heard since boot (sticky: the title has no traffic of its own after the check); the first colour decision was made */
static u32 g_ca, g_cb, g_rgb;   /* the grey level (187 / 255), the cyan mix (0 .. 255) and the tint written last */
static u8 g_fo;

static void level_set(void)         /* once a frame: the target from the link and the server proof, one fade step, the tint written on a change only */
{
    u32 v, link, srv, ta, tb;
    if (!g_mat2)
        return;
    v = ms_net_cost ? ms_net_cost(107) : 1u;
    if (ms_net_cost && ms_net_cost(103))
        g_heard = 1;
    link = ms_net_cost && mkl_link(v);
    srv = ms_net_cost && mkl_title_online(v, g_heard);
    ta = mkl_alpha(link);
    tb = mkl_blue(srv);
    if (!g_dec) {
        g_ca = ta;
        g_cb = tb;
        g_dec = 1;
    } else {
        g_ca = mkl_step(g_ca, ta);
        g_cb = mkl_bstep(g_cb, tb);
    }
    tt_st[T_LEVEL] = srv ? 3u : link ? 1u : 2u;
    if (mkl_rgb(g_ca, g_cb) != g_rgb) {
        g_rgb = mkl_rgb(g_ca, g_cb);
        W(g_mat2 + 4u) = (g_rgb << 8) | 0xFFu;          /* HSD_Material diffuse (RGBA) of the runtime material, as gnb_art.c tint */
        tt_st[T_RGB] = g_rgb;
    }
#if MARK_BLUE_FADEOFF
    {
        u32 want = g_cb >= 128u;
        if (g_idw2 && want != g_fo) {
            g_fo = (u8) want;
            g_idw2[0] = g_imgm[want];
            GX_INV_TEX();
        }
    }
#endif
}
static u32 g_fade;      /* frames since it appeared */

static void cp(u32 *dst, u32 src, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++)
        dst[i] = W(src + 4u * i);
}

extern volatile unsigned int *const hub_ut_word __attribute__((weak));   /* hub_res.c (-DUPD_TITLE only): &hub_st[HB_UT_STATE], the update check word; absent = no text */
static struct tt_layout g_L;       /* the layout of this visit, kept to compose the two images again when the check text comes or goes */
static struct tt_line g_lines[TT_NLINES];
static char g_bs[10];
static u8 *g_buf[3];       /* the text image, the mark image, (MARK_BLUE_FADEOFF) the faint-pairs mark image */
static u8 g_chk, g_chk_tried;      /* the check text is in the images / the last swap failed for lack of heap (not retried until the state changes) */

static u32 chk_now(void)
{
    return &hub_ut_word && hub_ut_word && tt_check_running(*hub_ut_word);
}

static void lines_set(u32 chk)     /* the second line: the link, or the check text while the check runs */
{
    u32 i;
    for (i = 0; i < TT_NLINES; i++)
        g_lines[i] = tt_lines[i];
    if (chk) {
        g_lines[TT_NLINES - 1].text = TT_CHK_TEXT;
        g_lines[TT_NLINES - 1].cap = TT_CHK_CAP;
        g_lines[TT_NLINES - 1].y = TT_CHK_Y;
    }
}

static void chk_swap(void)         /* the check started or ended: compose both images again into their buffers (same heap, no new block kept), show them */
{
    u32 want = chk_now(), bytes = TT_BYTES;
    u8 *plane;
    if (want == g_chk || !g_buf[1] || g_chk_tried == want + 1u)
        return;
    plane = (u8 *) HSD_MEMALLOC(TT_PLANE_BYTES);
    if (!plane) {
        g_chk_tried = (u8) (want + 1u);
        return;
    }
    lines_set(want);
    g_L.logo = tt_logo;
    g_L.part = 1u;
    if (tt_compose(g_buf[0], (const u8 *) TT_FONT_ATLAS, &g_L, plane) >= 0) {
        g_L.part = 2u;
        tt_compose(g_buf[1], (const u8 *) TT_FONT_ATLAS, &g_L, plane);
#if MARK_BLUE_FADEOFF
        g_L.logo = tt_logo_fo;
        tt_compose(g_buf[2], (const u8 *) TT_FONT_ATLAS, &g_L, plane);
        DC_STORE(g_buf[2], bytes);
        g_L.logo = tt_logo;
#endif
        g_L.part = 0u;
        DC_STORE(g_buf[0], bytes);
        DC_STORE(g_buf[1], bytes);
        GX_INV_TEX();
        g_chk = (u8) want;
        g_chk_tried = 0;
    } else
        g_chk_tried = (u8) (want + 1u);
    HSD_FREE(plane);
    tt_st[T_CHK] = g_chk;
}

/* one quad of our own: a copy of the notice's joint / DObj / MObj / material / TObj / image descriptors with the image at `img`, loaded into a gobj on the title's text layer.
   -> 0, else 37 / 38 / 39; *idw: the image descriptor copy, *rtmat: the runtime material (0 when it cannot be found) */
static int make_quad(const struct quad *a, u32 mat, u32 img, u32 **idw, u32 *rtmat)
{
    u32 *dsc, *jd, *dd, *md, *mt, *td, *id, jo, dob, mo;
    void *g, *j;
    dsc = (u32 *) HSD_MEMALLOC(70u * 4u);
    if (!dsc)
        return 37;
    jd = dsc;               /* 16 words: the joint */
    dd = dsc + 16;          /* 4: its DObj */
    md = dsc + 20;          /* 8: its MObj (6 used) */
    mt = dsc + 28;          /* 8: the material (5 used) */
    td = dsc + 36;          /* 24: the TObj (23 used) */
    id = dsc + 60;          /* 10: the image (6 used) */
    cp(jd, a->j, 16u);
    cp(dd, a->d, 4u);
    cp(md, a->m, 6u);
    cp(mt, mat, 5u);
    cp(td, a->t, 23u);
    cp(id, a->img, 6u);
    jd[1] = OUR_FLAGS;
    jd[2] = 0;                      /* no child, no sibling: HSD_JObjLoadJoint loads both */
    jd[3] = 0;
    jd[4] = (u32) dd;
    jd[0xB] = TT_POS_X;             /* +0x2C position x, y, z (floats, written as bit patterns) */
    jd[0xC] = TT_POS_Y;
    jd[0xD] = TT_POS_Z;
    dd[1] = 0;
    dd[2] = (u32) md;
    md[2] = (u32) td;
    md[3] = (u32) mt;
    mt[3] = tt_fade[0];
    td[1] = 0;
    td[0x4Cu / 4u] = (u32) id;
    id[0] = img;
    g = GOBJ_NEW(CLASS_UI, 15, 0);
    if (!g)
        return 38;
    j = JOBJ_LOAD(jd);
    if (!j) {
        GOBJ_FREE(g);
        return 39;
    }
    GOBJ_OBJ(g, JOBJ_KIND, j);
    GOBJ_LINK(g, (void *) JOBJ_DRAW, 9, 0);
    jo = (u32) j;
    dob = W(jo + 0x18u);            /* HSD_JObj.u.dobj; HSD_DObj +8 mobj; HSD_MObj +0xC material (the same walk as gnb_art.c tint) */
    mo = okp(dob) ? W(dob + 8u) : 0u;
    *rtmat = okp(mo) ? W(mo + 0xCu) : 0u;
    if (!okp(*rtmat))
        *rtmat = 0;
    *idw = id;
    return 0;
}

static int install(void)
{
    struct quad a;
    struct tt_layout L;
    u32 root = W(TT_MODEL), bytes = TT_BYTES, mat, *idw1;
    u8 *buf, *plane, *bufm, *buff = 0;
    char *bs = g_bs;
    int rc, rc2;
    if (!okp(root))
        return 10;
    rc = quad_of(root, TT_JN, TT_W, TT_IH, &a);
    if (rc)
        return 10 + rc;
    if (!corners(&a, TT_J6_XL, TT_J6_XR, TT_J6_YBOT, TT_J6_YTOP))
        return 31;
    if (ml_font_sum((const u8 *) TT_FONT_ATLAS) != TT_FONT_SUM)
        return 33;
    mat = W(a.m + 0xCu);
    if (!okp(mat))
        return 36;
    L.text_x = TT_TEXT_X;
    L.text_w = TT_TEXT_W;
    g_chk = (u8) chk_now();
    g_chk_tried = 0;
    lines_set(g_chk);
    L.lines = g_lines;
    L.nlines = TT_NLINES;
    L.logo = tt_logo;
    L.logo_w = TT_LOGO_W;
    L.logo_h = TT_LOGO_H;
    L.logo_x = TT_LOGO_X;
    L.logo_y = TT_LOGO_Y;
    L.logo_gap = TT_LOGO_GAP;
    L.centre = TT_CENTRE;
    L.right = 0;
    L.right_cap = TT_RIGHT_CAP;
    L.right_y = TT_RIGHT_Y;
    L.right_edge = TT_RIGHT_EDGE;
    {   /* the running build as the Update screen sees it: the loader's status block (upd_host.c boot_read: finished successful load by a verifying loader, build_seq in word 18) */
        volatile u32 *st = (volatile u32 *) MS_CL_STATUS_ADDR;
        u32 n = st[18], d = 100000u, k = 0;
        if (st[0] == 0x434C0001u && st[1] == 7u && st[2] == 0u && (st[16] & 1u) && n != 0u && n < 100000u) {
            bs[k++] = 'R'; bs[k++] = 'e'; bs[k++] = 'l'; bs[k++] = ' ';            /* "Rel 494" (release number, not b494) */
            while (d > 1u && n < d)
                d /= 10u;
            for (; d; d /= 10u) {
                u32 c = 0;
                while (n >= d) { n -= d; c++; }
                bs[k++] = (char) ('0' + c);
            }
            bs[k] = 0;
            L.right = bs;
        }
    }
    buf = (u8 *) HSD_MEMALLOC(bytes + 32u);          /* the text image, then the mark's own image (its quad is tinted by the state) */
    bufm = (u8 *) HSD_MEMALLOC(bytes + 32u);
    if (!buf || !bufm)
        return 34;
    buf = (u8 *) (((u32) buf + 31u) & ~31u);
    bufm = (u8 *) (((u32) bufm + 31u) & ~31u);
#if MARK_BLUE_FADEOFF
    buff = (u8 *) HSD_MEMALLOC(bytes + 32u);
    if (!buff)
        return 34;
    buff = (u8 *) (((u32) buff + 31u) & ~31u);
#endif
    plane = (u8 *) HSD_MEMALLOC(TT_PLANE_BYTES);
    if (!plane)
        return 35;          /* the images stay in the scene heap until the scene ends; nothing points at them */
    L.part = 1u;
    rc = tt_compose(buf, (const u8 *) TT_FONT_ATLAS, &L, plane);
    L.part = 2u;
    rc2 = tt_compose(bufm, (const u8 *) TT_FONT_ATLAS, &L, plane);
#if MARK_BLUE_FADEOFF
    L.logo = tt_logo_fo;
    if (tt_compose(buff, (const u8 *) TT_FONT_ATLAS, &L, plane) < 0)
        rc2 = -1;
    L.logo = tt_logo;
#endif
    HSD_FREE(plane);
    if (rc < 0)
        return 40 - rc;
    if (rc2 < 0)
        return 50 - rc2;
    L.part = 0u;
    g_L = L;
    g_L.right = L.right ? g_bs : 0;
    tt_st[T_FIX] = 0;
    tt_st[T_SUM] = tt_fnv(buf, bytes);
    tt_st[T_BUF] = (u32) buf;
    DC_STORE(buf, bytes);
    DC_STORE(bufm, bytes);
    g_buf[0] = buf;
    g_buf[1] = bufm;
    g_buf[2] = buff;
    g_imgm[0] = (u32) bufm;
    g_imgm[1] = (u32) (buff ? buff : bufm);
    if (buff)
        DC_STORE(buff, bytes);
    tt_st[T_BUF2] = (u32) bufm;
    rc = make_quad(&a, mat, (u32) buf, &idw1, &g_mat);
    if (rc)
        return rc;
    rc = make_quad(&a, mat, (u32) bufm, &g_idw2, &g_mat2);
    if (rc)
        return rc;
    g_fade = 0;
    g_dec = 0;
    g_fo = 0;
    g_rgb = 0xFFFFFFFFu;
    level_set();                    /* the right tint from the first frame (no white flash on a console that is not known online) */
    tt_st[T_FIX] = g_mat;
    GX_INV_TEX();
    return 0;
}

void tt_frame(void)
{
    int why;
    u32 snap = (TT_COUNTDOWN << 20) ^ TT_FRAMES;   /* the title's own counters: they move only while its OnFrame runs, i.e. after its OnEnter loaded the archive */
    if (SCENE_MAJOR != 0u) {                 /* any other scene: the next title entry loads a fresh archive and gets the band again */
        g_state = 0;
        g_mat = 0;
        g_mat2 = 0;
        g_idw2 = 0;
        g_seen = 1;
        g_fc = snap;
        return;
    }
    tt_st[T_TICKS]++;
    if (g_state) {
        chk_swap();                          /* "Checking for update..." while the update check runs */
        level_set();                         /* the mark's tint follows the link / server state (dim white, white, cyan; a write on a change only) */
        TT_FRAMES = 0;                       /* no attract demo, intro or How to Play from the title: leaves for the demo when frame_count passes 600;
                                                held at 0 once our OnEnter-detection is done (it reads the counters only before g_state is set) */
#if TT_FADE_N
        if (g_mat && g_fade < TT_FADE_N) {   /* the fade-in: our quad's material alpha, one table entry per frame, then it stays at full */
            g_fade++;
            W(g_mat + 0xCu) = tt_fade[g_fade];
            if (g_mat2)
                W(g_mat2 + 0xCu) = tt_fade[g_fade];
        }
#endif
        return;
    }
    if (!g_seen || snap == g_fc) {           /* no baseline yet (the engine's first tick), or the counters have not moved: the title's OnEnter may not have run yet
                                                (model_desc_0 would still name the previous visit's freed archive); wait for the next frame */
        g_seen = 1;
        g_fc = snap;
        return;
    }
    tt_st[T_VISITS]++;
    why = install();
    if (why) {
        g_state = 2;
        tt_st[T_REFUSED]++;
        tt_st[T_WHY] = (u32) why;
    } else {
        g_state = 1;
        tt_st[T_DONE]++;
    }
    tt_st[T_STATE] = g_state;
}
