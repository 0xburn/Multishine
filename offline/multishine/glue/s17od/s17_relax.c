/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : relaxed main-stick compare glue. Original work; reads of the UCF buffer layout from
   third_party/slippi-ssbm-asm/External/UCF 0.84/UCF/UCF Pad Buffer + 1.0 Cardinals.asm (disassembled: ring of 4 raw (x, y) halfwords per player at
   +0..+7, write index byte at +8, counter byte at +9, 12 bytes per player, buffer = body + 4, the player = fp->x618_player_id = the port). */
#include "s17_relax.h"
#include "../relax/relax_rule.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef int s32;
typedef signed char s8;
#ifdef S17_HOST                                                   /* host test (relax/relax_glue_test.c): the game's memory is the array s17_hm, address 0x80000000 = index 0 */
extern unsigned char s17_hm[];
#define PB(p) ((volatile u8 *)(s17_hm + ((p) - 0x80000000u)))
#else
#define PB(p) ((volatile u8 *)(p))
#endif
#define RB(p) (*(volatile const u8 *)PB(p))

extern u32 s17_relax_busy(void);                                  /* glue: the engine logic is running (the UCF ring may be half updated) */
extern u32 sd_ucf_body;                                           /* engine: payload address of the UCF Pad Buffer hook (0 = not found) */

unsigned s17_relax_on;
unsigned s17_relax_neg;                                           /* TEST ONLY: 1 = dead zone widened to |x| <= 60 (the game sees the difference), 2 = no guards and no patches */
volatile unsigned s17_rx_tel[RXT_N];

/* Online.s offsets used here (same numbers as net/s12_early.h) */
#define ODB_LOCAL_PLAYER_INDEX 0x000
#define ODB_FRAME 0x003
#define ODB_PRED_READ_IDXS 0x18E
#define ODB_PRED_WRITE_IDXS 0x191
#define ODB_PRED_INPUTS 0x194
#define ODB_PLAYER_SAVESTATE_FRAME 0x391
#define RXB_OPNT_INPUTS 42
#define PAD 12
#define RXB_IN_N 7
#define PRED_N 14
#define STATIC_PLAYERS 0x80453080u                                /* StaticPlayer[6], 0xE90 each; +0xB0 = the fighter entity */
#define STATIC_STRIDE 0xE90u

static u32 be32(u32 p) { return ((u32)RB(p) << 24) | ((u32)RB(p + 1) << 16) | ((u32)RB(p + 2) << 8) | RB(p + 3); }

struct lg { s32 f; s8 ux, uy, rx, ry; u8 on; };
static struct lg LG[3][8];

void s17_relax_reset(void)
{
    u32 i, j;
    for (i = 0; i < 3; i++) for (j = 0; j < 8; j++) LG[i][j].on = 0;
}

static struct lg *lg_find(u32 count, s32 f)
{
    u32 j;
    for (j = 0; j < 8; j++) if (LG[count][j].on && LG[count][j].f == f) return &LG[count][j];
    return 0;
}
static struct lg *lg_new(u32 count, s32 f)
{
    u32 j, k = 0;
    for (j = 0; j < 8; j++) {
        if (!LG[count][j].on) { k = j; break; }
        if (LG[count][j].f < LG[count][k].f) k = j;                /* none free: replace the oldest */
    }
    LG[count][k].on = 1; LG[count][k].f = f;
    return &LG[count][k];
}

/* the in-game port of remote player `count` (1v1: the other active port); -1 when it cannot be told */
static int remote_port(u32 odb, u32 count)
{
    u32 local = RB(odb + ODB_LOCAL_PLAYER_INDEX), p, n = 0;
    int found = -1;
    if (count != 0) return -1;                                    /* only 1v1 is handled */
    for (p = 0; p < 4; p++) {
        if (be32(STATIC_PLAYERS + STATIC_STRIDE * p + 0xB0) == 0) continue;
        n++;
        if (p != local) found = (int)p;
    }
    /* a second fighter entity (Ice Climbers' Nana, Zelda / Sheik) may write the player's UCF ring a second time per frame: not handled, strict */
    if (found >= 0 && be32(STATIC_PLAYERS + STATIC_STRIDE * (u32)found + 0xB4) != 0) return -1;
    return n == 2 ? found : -1;
}

/* UCF ring of a port: slot of the frame `back` frames before the newest written one, or 0 */
static volatile u8 *ring_slot(int port, int back)
{
    u32 base = sd_ucf_body + 4u + 12u * (u32)port, idx = RB(base + 8);
    if (!sd_ucf_body || back < 0 || back > 3) return 0;
    return PB(base + 2u * ((idx - (u32)back) & 3u));
}

int s17_relax_core(const u8 *real, const u8 *pred, u32 odb, u32 rxb, u32 count, u32 off, s32 h, u32 rd)
{
    struct rx_ctx c;
    s32 F, i;
    int port, y, xp, xr, back;
    volatile u8 *slot;
    s17_rx_tel[RXT_CALLS]++;
    if (!s17_relax_on || !odb || !rxb || count != 0 || !sd_ucf_body) return 0;
    if (s17_relax_busy()) { s17_rx_tel[RXT_DEN_BUSY]++; return 0; }          /* an interrupt inside the logic: the ring index and its entry may be out of step */
    if (real[3] != pred[3] || real[4] != pred[4] || real[5] != pred[5]) { s17_rx_tel[RXT_DEN_FIELD]++; return 0; }   /* only the raw main X differs */
    xp = (s8)pred[2]; xr = (s8)real[2]; y = (s8)real[3];
    F = (s32)be32(odb + ODB_FRAME);                                /* the frame about to be set up: frames < F were simulated */
    if (lg_find(count, h)) return 1;                               /* accepted (and patched) by an earlier look at the same frame */
    if ((s17_relax_neg & 1u) && xp >= -60 && xp <= 60 && xr >= -60 && xr <= 60) { s17_rx_tel[RXT_ACC]++; return 1; }     /* NEGATIVE CONTROL 1 */
    if (s17_relax_neg & 2u) {                                                                                           /* NEGATIVE CONTROL 2: the dead-zone-only rule */
        if (xp >= -RX_DZ && xp <= RX_DZ && xr >= -RX_DZ && xr <= RX_DZ && xp * xp + y * y <= RX_CLAMP2 && xr * xr + y * y <= RX_CLAMP2) { s17_rx_tel[RXT_ACC]++; return 1; }
        s17_rx_tel[RXT_DEN_DZ]++; return 0;
    }
    if (xp < -RX_DZ || xp > RX_DZ || xr < -RX_DZ || xr > RX_DZ || xp * xp + y * y > RX_CLAMP2 || xr * xr + y * y > RX_CLAMP2) { s17_rx_tel[RXT_DEN_DZ]++; return 0; }
    port = remote_port(odb, count);
    if (port < 0) { s17_rx_tel[RXT_DEN_PORT]++; return 0; }
    c.have_m2 = c.have_q2 = 0; c.xm2_used = c.xm2_real = c.ym2 = c.xq2 = c.yq2 = 0;
    if (off + 2 <= RXB_IN_N - 1u && h >= 2) {                      /* frame h-2: the RXB holds latest..latest-6 (index = latest - frame) */
        u32 q = rxb + RXB_OPNT_INPUTS + (off + 2) * PAD + count * RXB_IN_N * PAD;
        struct lg *l = lg_find(count, h - 2);
        c.have_m2 = 1;
        c.xm2_real = (s8)RB(q + 2); c.ym2 = (s8)RB(q + 3);
        c.xm2_used = l ? l->ux : c.xm2_real;
    }
    if (!c.have_m2) { s17_rx_tel[RXT_DEN_NOCTX]++; return 0; }
    {   /* frame h+2 was simulated before this look: it read either its predicted pad (ring entry rd + 2) or, when its real input was already there, the real one; test every candidate */
        s32 xq[2], yq[2], nq = 0;
        if (h + 2 <= F - 1) {
            u32 wr = RB(odb + ODB_PRED_WRITE_IDXS + count);
            if (((wr + PRED_N - rd) % PRED_N) > 2u) {              /* the predicted ring has an entry for frame h + 2 */
                u32 e = odb + ODB_PRED_INPUTS + ((rd + 2u) % PRED_N) * PAD + count * PRED_N * PAD;
                xq[nq] = (s8)RB(e + 2); yq[nq] = (s8)RB(e + 3); nq++;
            }
            if (off >= 2u) {                                       /* its real input has arrived by now */
                u32 q = rxb + RXB_OPNT_INPUTS + (off - 2u) * PAD + count * RXB_IN_N * PAD;
                xq[nq] = (s8)RB(q + 2); yq[nq] = (s8)RB(q + 3); nq++;
            }
            if (!nq) { s17_rx_tel[RXT_DEN_NOCTX]++; return 0; }
            for (i = 0; i < nq; i++) {
                c.have_q2 = 1; c.xq2 = xq[i]; c.yq2 = yq[i];
                if (!rx_main_ok(xp, xr, y, &c)) { s17_rx_tel[RXT_DEN_G]++; return 0; }
            }
        } else {
            if (!rx_main_ok(xp, xr, y, &c)) { s17_rx_tel[RXT_DEN_G]++; return 0; }
        }
    }
    back = (F - 1) - h;                                            /* patch #1: the ring entry of frame h holds the predicted pad; make it the real one */
    slot = ring_slot(port, back);
    if (slot && slot[0] == (u8)xp && slot[1] == (u8)y) { slot[0] = (u8)xr; s17_rx_tel[RXT_P1]++; }
    else if (h + 2 > F - 1) { s17_rx_tel[RXT_DEN_P1]++; return 0; }     /* frame h+2 will read it and it could not be patched: strict */
    {
        struct lg *l = lg_new(count, h);
        l->ux = (s8)xp; l->uy = (s8)y; l->rx = (s8)xr; l->ry = (s8)y;
    }
    s17_rx_tel[RXT_ACC]++;
    return 1;
}

/* entry of the ASM stub: the frame and the predicted-input read index are the ASM's own current ones */
int s17_relax_c(const u8 *real, const u8 *pred, u32 odb, u32 rxb, u32 count, u32 off)
{
    if (!s17_relax_on || !odb || count > 2) return 0;
    return s17_relax_core(real, pred, odb, rxb, count, off, (s32)be32(odb + ODB_PLAYER_SAVESTATE_FRAME + 4 * count), RB(odb + ODB_PRED_READ_IDXS + count));
}

void s17_relax_restore(int s)
{
    u32 j;
    int port, f;
    u32 odb;
    extern u32 s17_relax_odb(void);
    odb = s17_relax_odb();
    port = odb ? remote_port(odb, 0) : -1;
    for (j = 0; j < 8; j++) {
        struct lg *l = &LG[0][j];
        if (!l->on) continue;
        f = l->f;
        if (f >= s) { l->on = 0; continue; }                       /* re-simulated with the real pad now */
        if (port >= 0 && f >= s - 2) {                             /* patch #2: the frames from s on still read this entry */
            volatile u8 *slot = ring_slot(port, (s - 1) - f);
            if (slot && slot[0] == (u8)l->ux && slot[1] == (u8)l->uy) { slot[0] = (u8)l->rx; s17_rx_tel[RXT_P2]++; }
            else s17_rx_tel[RXT_P2_SKIP]++;
        }
    }
}
