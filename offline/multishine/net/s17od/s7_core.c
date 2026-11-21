/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* S12: copy of at HEAD for; S12 changes marked S12: (the RXB
   opponent block of s3_core_b0 moved into s12_core_rxb_inputs, same bytes). */
/* S7: copy of for stage S7, changes marked S7:. The S3 wire
   (HELLO handshake, 'SDS3' PAD packets, frame-advantage byte, debug trailer) is replaced by Slippi's wire on our ENet
   implementation, taken (S4); the B0 prediction rule (S6b), the gap-aware depth cap
   (S6c) and the stall histogram (S6D) are unchanged.  See s7_core.h.  Project-owned, no libc. */
#include "s7_core.h"
#include "../../enet/s4_script.h"   /* S7: the S4 opponent script (loopback reference), unchanged */

#define ST (c->st)
#define XS (c->xs)                  /* S7 */

static void put32(s3_u8 *p, s3_u32 v) { p[0] = (s3_u8)(v >> 24); p[1] = (s3_u8)(v >> 16); p[2] = (s3_u8)(v >> 8); p[3] = (s3_u8)v; }
static s3_u32 get32(const s3_u8 *p) { return ((s3_u32)p[0] << 24) | ((s3_u32)p[1] << 16) | ((s3_u32)p[2] << 8) | p[3]; }
static void zero(void *p, s3_u32 n) { volatile s3_u8 *b = (volatile s3_u8 *)p; while (n--) *b++ = 0; }
static void copy(s3_u8 *d, const s3_u8 *s, s3_u32 n) { while (n--) *d++ = *s++; }
static void str(s3_u8 *d, const char *s) { while (*s) *d++ = (s3_u8)*s++; }

/* 0x138-byte StartMeleeData captured from a plain offline Fox (P1) / Falco (P2) VS match on Onett in the patched ISO
 * (/): 8:00 timer, 4 stocks, items off, P3/P4 slot type 3. S7: characters, colors and the
 * stage are overwritten from the MATCH_SELECTIONS of both players (s3_core_b3). */
const s3_u32 s3_match_struct[78] = {
    0x3201864Cu, 0xC3000000u, 0x000000FFu, 0xFF6E0009u, 0x000001E0u, 0x00000000u,
    0x00000000u, 0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0x3F800000u,
    0x3F800000u, 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x02000400u, 0x00FF0000u, 0x09007800u, 0xC0000401u, 0x00000000u, 0x00000000u,
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x14000400u, 0x00FF0000u, 0x09007800u,
    0xC0000401u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x3F800000u, 0x3F800000u,
    0x1A030400u, 0x00FF0000u, 0x09007800u, 0x40000401u, 0x00000000u, 0x00000000u,
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x1A030400u, 0x00FF0000u, 0x09007800u,
    0x40000401u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x3F800000u, 0x3F800000u,
    0x21030400u, 0x00FF0000u, 0x09007800u, 0x40000400u, 0x00000000u, 0x00000000u,
    0x3F800000u, 0x3F800000u, 0x3F800000u, 0x21030400u, 0x00FF0000u, 0x09007800u,
    0x40000400u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x3F800000u, 0x3F800000u
};

/* S7: pad hashes (same definition as: FNV-1a over (tag u32 BE, 8 pad bytes), tags 1..3600) */
#define S7_HASH_TAG 3600
volatile s3_u32 s7_setup_info[4];   /* MATCH SETUP CONFORMANCE: see s7_core.h (livediag row 7) */
#define S7F_FROZEN_PS 0x100u         /* S19b lab knob (s7_flags): our selection asks for frozen Pokemon Stadium (altStageMode 1) */
static s3_u32 fnv8(s3_u32 h, s3_u32 tag, const s3_u8 *p)
{
    int i;
    for (i = 0; i < 4; i++) { h ^= (tag >> (24 - 8 * i)) & 0xFFu; h *= 16777619u; }
    for (i = 0; i < S3_PAD_BYTES; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* ---- S7: MATCH_SELECTIONS ------------------------------------------------------------------------- */
static void build_sel(struct s3_core *c, s3_u32 sel)
{
    s3_u8 *m = c->lsel;
    m[0] = S7_MSG_SELECTIONS;
    m[1] = (s3_u8)(sel >> 24);                                   /* characterId (external id) */
    m[2] = (s3_u8)(sel >> 16);                                   /* characterColor */
#ifdef OD_SELECT_LOCK
    m[3] = 0;                                                    /* OD: not selected until B5 (the lock-in); the cfg char / colour stay as the fallback */
#else
    m[3] = 1;                                                    /* isCharacterSelected */
#endif
    m[4] = (s3_u8)(c->side < 0 ? 0 : c->side);                   /* playerIdx (absolute port index) */
    m[5] = (s3_u8)(sel >> 8); m[6] = (s3_u8)sel;                 /* stageId u16 */
#ifdef OD_SELECT_LOCK
    m[7] = 0;                                                    /* OD: isStageSelected only when B5 picks one */
#else
    m[7] = 1;                                                    /* isStageSelected */
#endif
    put32(m + 8, c->cfg->rng_offset);                            /* rngOffset */
    m[12] = 0;                                                   /* teamId */
    m[13] = (c->xcfg->s7_flags & S7F_FROZEN_PS) ? 1u : 0u;       /* altStageMode (S19b: lab knob; the online CSS sets it through B5) */
}

static void send_sel(struct s3_core *c, int p)
{
    s3_u8 m[S7_SEL_LEN + 1];
    s3_u32 i, n = S7_SEL_LEN;
    if (p < 0) return;
    c->lsel[4] = (s3_u8)(c->side < 0 ? 0 : c->side);
    for (i = 0; i < S7_SEL_LEN; i++) m[i] = c->lsel[i];
    if (c->rules_have && !s7_core_is_slippi(c)) m[n++] = (s3_u8)S7_SETUP_VER;   /* MATCH SETUP: "I run the setup check" (our peers only; an older peer reads 14 bytes and ignores the rest) */
    if (sdenet_send(&c->enet, p, 0, SDENET_RELIABLE, m, n) == 0) ST->hello_tx++;
    else ST->tx_fail++;
}

/* MATCH SETUP: our canonical setup digest to our own peer (network slice only: B3 runs on the game thread without the lock and only asks for it) */
static void setup_send(struct s3_core *c)
{
    s3_u8 m[6];
    m[0] = S7_MSG_SETUP; m[1] = (s3_u8)S7_SETUP_VER; put32(m + 2, c->dg);
    if (sdenet_send(&c->enet, c->active, 0, SDENET_RELIABLE, m, 6) == 0) { c->dg_sent = c->dg; c->dg_want = 0; }
    else ST->tx_fail++;
}

/* ---- S7: ENet callbacks (as ) ----------------------------------------------- */
static int e_send(void *ctx, sde_u32 ip, sde_u16 port, const sde_u8 *d, sde_u32 len)
{
    struct s3_core *c = (struct s3_core *)ctx;
    int rc = c->send(c->ctx, ip, port, d, len);
    if (rc < 0) ST->tx_fail++;
    return rc;
}

extern s3_u32 s7_punch_mode, s7_punch_gap_ms;
static void e_connect(void *ctx, int p)
{
    struct s3_core *c = (struct s3_core *)ctx;
    XS->enet_connects++;
    if (p >= 0 && p < SDENET_MAX_PEERS) c->up_ms[p] = c->clock_ms(c->ctx) | 1u;   /* REJOIN */
    if (c->active < 0 || sdenet_state(&c->enet, c->active) != SDE_CONNECTED) c->active = p;
    if (c->state != S7_SES_LOST) {
        if (c->state != S3_SES_RUN) ST->sync_frame = ST->calls;
        c->state = S3_SES_RUN;
        c->ever_up = 1;
        ST->state = S3_SES_RUN;
    }
    send_sel(c, p);       /* Slippi: SetMatchSelections once the netplay client exists (prepareOnlineMatchState) */
}

static void why_set(struct s3_core *c, s3_u32 cause, s3_u32 data)
{
    if (c->why[0]) return;                                       /* the first cause stands */
    c->why[0] = cause; c->why[1] = ST->b0_frame; c->why[2] = data; c->why[3] = ST->rx_pkts; c->why[4] = ST->rx_dup; c->why[5] = ST->stall_run; c->why[6] = XS->rx_other; c->why[7] = ST->rx_bad;
}
void s7_core_why(const struct s3_core *c, s3_u32 *o8) { int i; for (i = 0; i < 8; i++) o8[i] = c->why[i]; }

static void e_disconnect(void *ctx, int p, sde_u32 data, int reason)
{
    struct s3_core *c = (struct s3_core *)ctx;
    int i;
    XS->enet_discs++;
    if (reason == SDE_DISC_TIMEOUT) XS->enet_timeouts++;
    if (p != c->active) return;
    c->active = -1;
    for (i = 0; i < SDENET_MAX_PEERS; i++) if (sdenet_state(&c->enet, i) == SDE_CONNECTED) { c->active = i; return; }
    if (c->last_frame > 0 || c->ever_up) {                       /* lost during a match (B0 reports DISCONNECTED) or after the connection was up (between games): the peer is gone */
        why_set(c, reason == SDE_DISC_TIMEOUT ? S7W_ENET_TIMEOUT : S7W_ENET_DISC, data);
        c->disconnected = 1;
        c->state = S7_SES_LOST;
    } else if (c->state != S7_SES_LOST) c->state = S7_SES_CONNECTING;
    ST->state = (s3_u32)c->state;
}

static void compare_cks(struct s3_core *c, s3_s32 f, s3_u32 v)
{
    s3_u32 i;
    if (f <= 0 || (s3_u32)f <= ST->cks_last_frame) return;
    for (i = 0; i < S3_CKS_HIST; i++) {
        if (c->hist_frame[i] == f) {
            ST->cks_last_frame = (s3_u32)f;
            ST->cks_cmp++;
            if (c->hist_cks[i] != v) {
                ST->cks_bad++;
                if (!ST->cks_first_bad) ST->cks_first_bad = (s3_u32)f;
                if (!ST->dz_first) {                             /* DESYNC LOCATOR: the first mismatch of THIS game, both checksums (GAME_RECORD, shutdump FZ11, harness st_dz_*; the rest is derived from the two sides' values) */
                    ST->dz_lcks = c->hist_cks[i]; ST->dz_pcks = v; ST->dz_first = (s3_u32)f;
                    {   /* PQ LOG dz_shift: equal at a shift k != 0 = the inputs are shifted by k frames (a dropped / extra pad-queue entry), not a state divergence */
                        static const signed char ks[6] = { -1, 1, -2, 2, -3, 3 };
                        s3_u32 n, j;
                        ST->dz_shift = 0x7Fu;
                        for (n = 0; n < 6u && ST->dz_shift == 0x7Fu; n++)
                            for (j = 0; j < S3_CKS_HIST; j++)
                                if (c->hist_frame[j] == f + ks[n] && c->hist_cks[j] == v) { ST->dz_shift = (s3_u32)(s3_u8)ks[n]; break; }
                    }
#ifdef __powerpc__
                    __asm__ volatile("dcbst 0,%0; sync" : : "r"(&ST->dz_first) : "memory");   /* the Wii kernel's shutdump (ARM) reads RAM; words 48..55 are one cache line */
#endif
                }
            }
            return;
        }
    }
}

static void store_remote(struct s3_core *c, s3_s32 f, const s3_u8 *pad, s3_u8 rng)
{
    c->rtag[f & (S3_RING - 1)] = f;
    c->rrng[f & (S3_RING - 1)] = rng;                            /* SLIPPI 3.7.0: wire byte 8 */
    copy(c->rpad[f & (S3_RING - 1)], pad, S3_PAD_BYTES);
    if (f <= S7_HASH_TAG) { c->rhash = fnv8(c->rhash, (s3_u32)f, pad); XS->rpad_hash = c->rhash; }
    ST->rx_new++;
}

static void send_ack(struct s3_core *c, int p, s3_s32 frame)
{
    s3_u8 ack[6];
    ack[0] = S7_MSG_PAD_ACK;                                     /* on the connection the pad came from, channel 2 */
    put32(ack + 1, (s3_u32)frame);
    ack[5] = (s3_u8)c->side;
    if (sdenet_send(&c->enet, p, 2, SDENET_UNSEQUENCED, ack, 6) == 0) XS->tx_ack++;
}

/* S7: SlippiNetplay OnData, NP_MSG_SLIPPI_PAD: duplicate-connection rule, time-offset sample, Slippi's receive rule,
   remote checksum, ack. */
static void on_pad(struct s3_core *c, int p, const s3_u8 *d, s3_u32 len)
{
    s3_s32 frame, cf, i;
    s3_u32 n, now, ins;
    s3_u8 port;
    if (len < S7_PAD_HDR) { ST->rx_bad++; return; }
    now = c->clock_us(c->ctx);                                   /* Slippi: time taken first, for the offset sample */
    frame = (s3_s32)get32(d + 1);
    port = d[5];
    cf = (s3_s32)get32(d + 6);
    if (c->side < 0 || port > 3 || port == (s3_u8)c->side) { ST->rx_bad++; return; }
    ST->rx_pkts++;
    ST->peer_frame = (s3_u32)frame;
    /* superfluous simultaneous connections: the lower player index keeps the one the pad arrived on */
    if (c->side < port) {
        int q;
        for (q = 0; q < SDENET_MAX_PEERS; q++)
            if (q != p && sdenet_state(&c->enet, q) != SDE_FREE && sdenet_state(&c->enet, q) != SDE_DISCONNECTING &&
                c->enet.peer[q].ip == c->enet.peer[p].ip && c->enet.peer[q].port == c->enet.peer[p].port) {
                sdenet_disconnect(&c->enet, q, 0);
                XS->dup_disc++;
            }
        c->active = p;
    } else if (p != c->active) c->active = p;                    /* SERIES: the higher index follows the connection the lower one's pads come on: its other link may be one the lower side
                                                                    already dropped (its CONNECTING half reset without a word), and pads sent there die (151 frames, then both sides stalled) */
    {   /* time-offset sample (every packet with a valid header, also old/duplicate ones, before the copy check) */
        s3_s32 tf = c->lft_frame;
        s3_u32 tt = c->lft_us;
        long long off;
        if (!c->game_started) { tf = 0; tt = now; }
        /* 64-bit like the C++ (s64), saturated to +-2^30 us (hostile frame numbers; the C++ truncates to s32) */
        off = (long long)(s3_s32)((now - c->ping_us / 2u) - tt) + (long long)S7_FRAME_US * ((long long)tf - frame);
        if (off > 0x40000000LL) off = 0x40000000LL;
        if (off < -0x40000000LL) off = -0x40000000LL;
        c->ts_buf[c->ts_idx] = (s3_s32)off;
        c->ts_idx = (c->ts_idx + 1) % S7_TS_RING;
        if (c->ts_n < S7_TS_RING) c->ts_n++;
        XS->ts_samples++;
    }
    n = (len - S7_PAD_HDR) / S7_PAD_WIRE;                        /* SLIPPI 3.7.0: 9 bytes per pad (a 3.6.4 sender's 8-byte pads fail the size rule below, as in Slippi's own receiver) */
    /* Slippi: inputsToCopy = frame - headFrame in 64 bits; here in unsigned 32-bit after the sign check (hostile frames) */
    if (frame > c->rlatest) {
        ins = (s3_u32)frame - (s3_u32)c->rlatest;
        if (ins > n || ins > S7_RX_MAX) { ST->rx_dup++; return; }   /* Slippi: break before the checksum is stored */
    } else ins = 0;
    for (i = (s3_s32)ins - 1; i >= 0; i--) store_remote(c, frame - i, d + S7_PAD_HDR + (s3_u32)i * S7_PAD_WIRE, d[S7_PAD_HDR + (s3_u32)i * S7_PAD_WIRE + S3_PAD_BYTES]);
    if (ins) { c->rlatest = frame; ST->rlatest = (s3_u32)frame; }
    else ST->rx_old++;
    c->peer_cks_frame = cf;                                      /* Slippi: remote_checksums = this packet's (also old ones) */
    c->peer_cks = get32(d + 10);
    compare_cks(c, cf, c->peer_cks);
    if (ins) {                                                   /* Slippi: ack only if pads were added */
        if (c->xcfg->s7_flags & S7F_ACK_LATE) { c->ack_q_frame = frame; c->ack_q_peer = p; XS->ack_late_q++; }
        else send_ack(c, p, frame);
    }
}

/* S7: NP_MSG_SLIPPI_PAD_ACK: lastFrameAcked, ackTimers, ping */
static void on_ack(struct s3_core *c, const s3_u8 *d, s3_u32 len)
{
    s3_s32 f;
    if (len < 6 || d[5] > 3 || d[5] == (s3_u8)c->side) { ST->rx_bad++; return; }
    f = (s3_s32)get32(d + 1);
    XS->rx_ack++;
    if (f > c->lacked && f <= c->lnewest) { c->lacked = f; ST->lacked = (s3_u32)f; }
    while (c->at_n && c->at_frame[(c->at_head + S7_ACK_TIMERS - c->at_n) % S7_ACK_TIMERS] < f) c->at_n--;
    if (c->at_n) {
        s3_u32 k = (c->at_head + S7_ACK_TIMERS - c->at_n) % S7_ACK_TIMERS;
        if (c->at_frame[k] == f) {
            c->ping_us = c->clock_us(c->ctx) - c->at_us[k];
            c->at_n--;
            XS->ping_us = c->ping_us;
            XS->ping_n++;
            if (c->ping_us > XS->ping_max_us) XS->ping_max_us = c->ping_us;
        }
    }
}

#ifdef OD_SELECT_LOCK
/* STAGE PER GAME: our route draws a random stage from the decider's rngOffset (B3), and that stayed the cfg's
   one value for the whole match, so every game drew the same stage.  The decider (side 0) alone re-rolls the low byte of its rngOffset from the last game-start clock reading once per game start (new_match) and when it
   is paired (set_peer: game 1 differs between matches after the first); the peer reads it from the decider's next selections message, which is sent after the game ends, so both sides draw
   the same stage. The Slippi route is untouched (setMatchSelections rolls there). Host test. */
#define RNG_ROLL(c) ((c)->lsel[11] ^= (s3_u8)((c)->lft_us >> 4))   /* the low byte of the rngOffset, from the last game-start clock reading: B3 takes rng % pool size, so the stage follows it (no clock call: engine size) */
#endif
/* S7: NP_MSG_SLIPPI_MATCH_SELECTIONS: merge, reset the remote pad queue and hasGameStarted (Slippi OnData).
   D5 (SLIPPI INTEROP): SlippiPlayerSelections::Merge: the random offset is always taken, the stage fields (id, selected flag, alt stage mode)
   only when the message has isStageSelected, the character fields (id, colour, team, selected flag) only when it has isCharacterSelected:
   a choice never un-selects.  The merged state lives in c->rsel (same byte layout as the message); rsel_have = the remote character is
   selected (Slippi remotePlayersReady).  Our own peers always send both selected, where this is the identity. */
static void on_sel(struct s3_core *c, const s3_u8 *d, s3_u32 len)
{
    s3_u32 i;
    if (len < S7_SEL_LEN || d[4] > 3 || d[4] == (s3_u8)c->side) { ST->rx_bad++; return; }
    ST->hello_rx++;
    /* (cube vs PC: 7 game starts in 45 hung on "Ready" until void:disconnect).  The peer's selections can arrive twice (each ENet connection sends them when it comes up, and
       the second connection can come up seconds late).  The first copy of a session resets the queue (sel_reset).  When the peer finished loading first, its pads for frames 1..8 then
       arrive and are ACKED; a late second copy, still before our frame 1, wiped them, and a peer does not send acked pads again: both sides waited at the start.  Such a late copy is
       dropped WHOLE: the peer is already in its game, so its choices were final before, and the decider has by then rolled its rngOffset for the NEXT game (new_match): taking that
       offset made this side build the game with another random seed. */
    if (c->sel_reset && c->rlatest > 0 && c->last_frame < 1) { XS->resv[0]++; return; }
    if (len > S7_SEL_LEN && !s7_core_is_slippi(c)) c->peer_cap = d[S7_SEL_LEN] != 0;   /* MATCH SETUP: the peer runs the setup check */
    c->rsel[0] = d[0];
    c->rsel[4] = d[4];
    for (i = 0; i < 4; i++) c->rsel[8 + i] = d[8 + i];           /* rngOffset: always */
    if (d[7]) { c->rsel[5] = d[5]; c->rsel[6] = d[6]; c->rsel[7] = 1; c->rsel[13] = d[13]; }
    if (d[3] && d[1] < 26u) { c->rsel[1] = d[1]; c->rsel[2] = d[2]; c->rsel[12] = d[12]; c->rsel[3] = 1; }   /*: a character id >= 26 (Master Hand, Crazy Hand, the Random placeholder) is no choice: the remote stays unlocked */
    c->rsel_have = c->rsel[3] != 0;
    XS->rsel = ((s3_u32)c->rsel[1] << 24) | ((s3_u32)c->rsel[2] << 16) | ((s3_u32)c->rsel[5] << 8) | c->rsel[6];
    XS->rsel2 = ((s3_u32)c->rsel[3] << 24) | ((s3_u32)c->rsel[7] << 16) | ((s3_u32)c->rsel[4] << 8) | c->rsel[12];
    XS->rsel_rng = get32(c->rsel + 8);
    /* Deliberate difference: once our match runs (B0 frame >= 1 seen) a further MATCH_SELECTIONS does not reset the
       remote queue.  Both sides connect at once, so each side sends its selections on both ENet connections; the
       second copy can arrive late (reliable retransmission) after frame 1, and Slippi's reset would then wedge the pad
       stream (the peer only re-sends from our last ack).  Slippi's own flow never starts a game that early. */
    if (c->last_frame >= 1) { XS->resv[0]++; return; }          /* sel_late (no reset) */
    c->game_started = 0;
    for (i = 0; i < S3_RING; i++) c->rtag[i] = 0;
    c->rlatest = 0; ST->rlatest = 0; c->peer_cks_frame = 0; c->peer_cks = 0;
    c->rhash = 2166136261u; XS->rpad_hash = c->rhash;
    XS->rx_reset++;
    c->sel_reset = 1;
}

static void e_receive(void *ctx, int p, sde_u8 ch, const sde_u8 *d, sde_u32 len)
{
    struct s3_core *c = (struct s3_core *)ctx;
    (void)ch;
    if (len < 1) { ST->rx_bad++; return; }
    XS->rx_msgs++;
    if (d[0] == S7_MSG_PAD) on_pad(c, p, d, len);
    else if (d[0] == S7_MSG_PAD_ACK) on_ack(c, d, len);
    else if (d[0] == S7_MSG_SELECTIONS) on_sel(c, d, len);
    else if (d[0] == S7_MSG_SETUP && len >= 6 && !s7_core_is_slippi(c)) { c->dg_peer = get32(d + 2); c->dg_peer_have = 1; s7_setup_info[1] = c->dg_peer; }   /* MATCH SETUP */
#ifdef MS_QCHAT
    else if (d[0] == 0x84u) { extern void qc_rx(const s3_u8 *d, s3_u32 len); qc_rx(d, len); XS->rx_other++; }   /* QUICK CHAT: Slippi's NP_MSG_SLIPPI_CHAT_MESSAGE, any channel */
#endif
    else XS->rx_other++;                                         /* 0x83 conn-selected, 0x84 chat, 0x85/0x86 ranked */
}

#ifdef MS_QCHAT
int s7_core_chat_tx(struct s3_core *c, const s3_u8 *d, s3_u32 len)
{
    s3_u32 i;
    if (len != 6u || (c->state != S3_SES_RUN && c->state != S7_SES_CONNECTING) || c->chat_want) return -1;   /* Slippi: slippi_netplay exists from the assignment on, the peer may still be connecting */
    for (i = 0; i < 6u; i++) c->chat_tx[i] = d[i];
    c->chat_tx[6] = (s3_u8)(S7_CHAT_NOW(c) >> 8); c->chat_tx[7] = (s3_u8)S7_CHAT_NOW(c);                          /* when it was queued (16 ms ticks), for the hold limit */
    c->chat_want = 1;
    return 0;
}

void s7_core_chat_cancel(struct s3_core *c) { c->chat_want = 0; }
#endif

void s7_core_rx_msg(struct s3_core *c, int p, const s3_u8 *d, s3_u32 len) { e_receive(c, p, 0, d, len); }

int s7_core_is_slippi(const struct s3_core *c) { return c->slippi || (c->xcfg->s7_flags & S7F_SLIPPI_PEER) != 0; }

static const struct sdenet_ops ENET_OPS = { e_send, e_connect, e_disconnect, e_receive };

static void enet_stats(struct s3_core *c)
{
    const struct sdenet_stats *s = &c->enet.st;
    XS->enet_rx = s->rx_dgrams;
    XS->enet_tx = s->tx_dgrams;
    XS->enet_retx = s->tx_retx;
    XS->enet_drop = s->rx_bad + s->rx_drop_peer + s->rx_drop_session + s->rx_drop_compressed + s->rx_drop_frag + s->rx_drop_allow;
    ST->rx_allow_drop = s->rx_drop_allow;
    if (c->active >= 0) {
        XS->enet_rtt = c->enet.peer[c->active].rtt;
        if (c->rtt_hook && XS->enet_rtt) c->rtt_hook(c->ctx, XS->enet_rtt);
        XS->active = (s3_u32)c->active | ((s3_u32)c->enet.peer[c->active].state << 8);
    } else XS->active = 0xFFu;
}

/* ---- public ---- */
void s7_core_init(struct s3_core *c, const struct s3_cfg *cfg, const struct s7_cfg *xcfg, struct s3_status *st,
                  struct s7_status *xs, struct s3_cks_ring *cks, s7_send_fn send, s3_poll_fn poll,
                  s3_clock_fn clock_us, s3_clock_fn clock_ms, void *ctx)
{
    zero(c, sizeof *c);
    c->cfg = cfg; c->xcfg = xcfg; c->st = st; c->xs = xs; c->cks = cks; c->send = send; c->poll = poll;
    c->clock_us = clock_us; c->clock_ms = clock_ms; c->ctx = ctx;
    c->side = -1;
    c->active = -1;
    c->state = S3_NET_WAIT;
    c->lhash = c->rhash = 2166136261u;
    sdenet_init(&c->enet, &ENET_OPS, c, cfg->rng_offset ^ 0x53374E54u);
    ST->side = 0xFFu;
    XS->magic = 0x53375354u;                                     /* 'S7ST' */
    XS->active = 0xFFu;
    XS->canary = 0x5A5AC0DEu;
}

int s3_core_set_mac(struct s3_core *c, const s3_u8 *mac)
{
    s3_u32 lo = ((s3_u32)mac[3] << 16) | ((s3_u32)mac[4] << 8) | mac[5];
    int i;
    for (i = 0; i < 2; i++) {
        if (c->cfg->peer[i].mac_lo == lo) {
            c->side = (int)(c->cfg->peer[i].side & 1u);
            ST->side = (s3_u32)c->side;
            c->dst_ip = c->cfg->peer[i].dst_ip;
            c->dst_port = c->cfg->peer[i].dst_port;
            sdenet_set_allow(&c->enet, c->cfg->allow_ip ? c->cfg->allow_ip : c->dst_ip, (sde_u16)c->cfg->allow_port,
                             !(c->cfg->flags & S3F_ALLOW_ANY));
            build_sel(c, c->xcfg->sel[c->side & 1]);
            return 0;
        }
    }
    return -1;
}

#ifdef MS_HIL_DEV
/* HIL dev image only: the chip's MAC matched no patched peer entry (no hardware MAC is patched into the repo): act as side 0 and accept any peer */
int s3_core_set_mac_any(struct s3_core *c)
{
    c->side = 0;
    ST->side = 0;
    c->dst_ip = 0;
    c->dst_port = 0;
    sdenet_set_allow(&c->enet, 0, 0, 0);
    build_sel(c, c->xcfg->sel[0]);
    return 0;
}
#endif

/* OD ('Multishine online-direct', offline/multishine/menus/od): the peer comes from the server's ticket instead of the cfg MAC table.
   Same effects as s3_core_set_mac for the matched cfg peer: side, destination, allow-list (the peer's address; port: cfg allow_port, 0 = any),
   the local MATCH_SELECTIONS message.  Returns 0, or -1 for a side other than 0 / 1. */
int s7_core_set_seats(struct s3_core *c, int seats)
{
    if (seats < 0 || seats > S7_SEATS_MAX) return -1;
    c->seats = seats;
    return 0;
}

int s7_core_seats(const struct s3_core *c)
{
    s3_u32 x = c->seats ? (s3_u32)c->seats : c->xcfg->S7X_SEATS;
    return x >= 1 && x <= S7_SEATS_MAX ? (int)(x < 2 ? 2 : x) : 2;
}

int s7_core_rollback_allowed(const struct s3_core *c)
{
    return s7_core_seats(c) <= 2;
}

int s7_core_set_peer(struct s3_core *c, int side, s3_u32 ip, s3_u32 port)
{
    if (side < 0 || side > 1) return -1;
    c->side = side;
    ST->side = (s3_u32)side;
    c->dst_ip = ip;
    c->dst_port = port;
    sdenet_set_allow(&c->enet, ip, (sde_u16)c->cfg->allow_port, !(c->cfg->flags & S3F_ALLOW_ANY));
#ifdef OD_SELECT_LOCK
    {   /* Direct mode: the code-entry callback locks the character in (B5) BEFORE the ticket names a peer; build_sel would wipe that choice */
        s3_u8 keep[S7_SEL_LEN];
        int i;
        for (i = 0; i < S7_SEL_LEN; i++) keep[i] = c->lsel[i];
        build_sel(c, c->xcfg->sel[side]);
        if (keep[3]) { c->lsel[1] = keep[1]; c->lsel[2] = keep[2]; c->lsel[3] = 1; }
        if (keep[7]) { c->lsel[5] = keep[5]; c->lsel[6] = keep[6]; c->lsel[7] = 1; }
        c->lsel[12] = keep[12]; c->lsel[13] = keep[13];
        if (side == 0) RNG_ROLL(c);
    }
#else
    build_sel(c, c->xcfg->sel[side]);
#endif
    return 0;
}

void s7_core_loopback(struct s3_core *c)
{
    c->side = 0;
    ST->side = 0;
    c->state = S7_LOOPBACK;
    ST->state = S7_LOOPBACK;
    build_sel(c, c->xcfg->sel[0]);
}

void s3_core_start(struct s3_core *c, s3_u32 now)
{
    if (c->state >= S7_SES_CONNECTING) return;
    c->peer_cap = 0; c->dg_peer_have = 0; c->dg_peer = 0; c->dg_sent = 0; c->dg_want = 0; c->setup_bad = 0; c->dg_t = 0;   /* MATCH SETUP: per session */
#ifdef MS_QCHAT
    c->chat_want = 0;                                            /* QUICK CHAT: a message still waiting from the last session never goes to the next opponent */
#endif
    c->state = S7_SES_CONNECTING;
    ST->state = S7_SES_CONNECTING;
    c->next_connect_ms = now;
    c->punch_t0 = now; c->punch_last = now; c->punch_n = 0;
    {   /* REJOIN: a connection from the new peer's address that s7_core_end_session_to kept: it is this session's link */
        int i;
        for (i = 0; i < SDENET_MAX_PEERS; i++)
            if (sdenet_state(&c->enet, i) == SDE_CONNECTED && c->enet.peer[i].ip == c->dst_ip && c->enet.peer[i].port == (sde_u16)c->dst_port) { e_connect(c, i); break; }
    }
    s3_core_tick(c, now);
}

/* DELAY_AUTO: the raw round trip of ONE acknowledgement, recovered from the ENet smoothing update without touching the canonical
   sdenet (handle_ack: r = now - echoed sent time; rtt += (r - rtt) / 8, or -= (rtt - r) / 8; var = var - var / 4, then var += |r - rtt'| / 4 with the NEW rtt).
   From the old and new (rtt, var): d = var' - (var - var / 4) = |r - rtt'| / 4 (integer), so r lies in [rtt' + 4d, rtt' + 4d + 3] when rtt rose, in [rtt' - 4d - 3,
   rtt' - 4d] when it fell, and within 7 ms of rtt when it did not move (the smoothing step truncated to 0): centre of the interval.  Pure, no state. */
s3_u32 s7_rtt_derive(s3_u32 rtt0, s3_u32 var0, s3_u32 rtt1, s3_u32 var1)
{
    s3_u32 base = var0 - var0 / 4u, d, m;
    if (var1 < base) return rtt1;                                /* not one plain update (a reset peer, two acks): the smoothed value */
    d = var1 - base;
    if (rtt1 > rtt0) return rtt1 + 4u * d + 1u;
    if (rtt1 < rtt0) { m = 4u * d + 1u; return rtt1 > m ? rtt1 - m : 0u; }
    return rtt0;
}

void s3_core_rx_from(struct s3_core *c, s3_u32 ip, s3_u16 port, const s3_u8 *d, s3_u32 len)
{
    if (c->state < S7_SES_CONNECTING || c->state == S7_LOOPBACK) return;
    if (c->rtt_raw_hook) {                                       /* DELAY_AUTO: snapshot, receive, and when exactly one acknowledgement was matched, derive its round trip */
        s3_u32 r0[SDENET_MAX_PEERS], v0[SDENET_MAX_PEERS], acks0 = c->enet.st.rx_acks, now, ms;
        int i;
        for (i = 0; i < SDENET_MAX_PEERS; i++) { r0[i] = c->enet.peer[i].rtt; v0[i] = c->enet.peer[i].rtt_var; }
        sdenet_rx(&c->enet, c->clock_ms(c->ctx), ip, port, d, len);
        now = c->enet.now;
        if (c->enet.st.rx_acks == acks0 + 1u)
            for (i = 0; i < SDENET_MAX_PEERS; i++) {
                const struct sdenet_peer *P = &c->enet.peer[i];
                if (P->last_ack_ms != now) continue;
                ms = s7_rtt_derive(r0[i], v0[i], P->rtt, P->rtt_var);
                if (ms > 4000u) break;                           /* junk: the connect handshake of a long-lost peer */
                c->rtt_raw_hook(c->ctx, (ms ? ms : 1u) * 1000u);
                break;
            }
        return;
    }
    sdenet_rx(&c->enet, c->clock_ms(c->ctx), ip, port, d, len);
}

void s3_core_tick(struct s3_core *c, s3_u32 now)
{
    int i, busy = 0, up = 0;
    if (c->state < S7_SES_CONNECTING || c->state == S7_LOOPBACK) return;
    for (i = 0; i < SDENET_MAX_PEERS; i++) {                     /* (re)connect while nothing is up or pending */
        int s = sdenet_state(&c->enet, i);
        if (s == SDE_CONNECTED || s == SDE_CONNECTING || s == SDE_ACK_CONNECT) busy = 1;
        if (s == SDE_CONNECTED) up = 1;
    }
    if (!busy && c->state == S7_SES_CONNECTING && !(c->xcfg->s7_flags & S7F_PASSIVE) && (s3_s32)(now - c->next_connect_ms) >= 0) {
        sdenet_connect(&c->enet, c->dst_ip, (sde_u16)c->dst_port, 3, c->xcfg->connect_data, now);
        c->next_connect_ms = now + 1000u;
    }
    /* PUNCH: the address the server reports for the opponent is the port its router used towards the SERVER.  A router that hands out a new outside
       port for every destination ("port hopping") uses another one towards us, and a router that only lets in what it has sent to drops the opponent's packets.  So, when the
       plain connect has not worked after PUNCH_START_MS, one tiny datagram (an ENet header with an unused peer id: every ENet drops it, stock Slippi included) goes to a ring of
       ports next to the reported one every PUNCH_GAP_MS: it opens our router for the opponent's real port (and the other way round on their side), and the connect that is
       already being retried gets through. */
    if (s7_punch_mode && c->state == S7_SES_CONNECTING && c->dst_ip && c->dst_port && !up && (s3_u32)(now - c->punch_t0) >= S7_PUNCH_START_MS
        && (s3_u32)(now - c->punch_t0) < S7_PUNCH_END_MS && (s3_u32)(now - c->punch_last) >= s7_punch_gap_ms) {
        static const signed char OFF[S7_PUNCH_RING] = { 1, 2, 3, 4, 5, 6, 7, 8, -1, 9, 10, 11, 12, -2, 13, 14, 15, 16, 17, 18, 19, 20, -3, 21, 22, 23, 24, -4 };
        s3_u8 dg[2] = { 0x0F, 0xFE };
        s3_s32 off, port;
        if (s7_punch_mode == 1u) { s3_u32 i = c->punch_n % 6u; off = i < 3u ? (s3_s32)i + 1 : -(s3_s32)(i - 2u); }   /* narrow: +1..+3, -1..-3 */
        else if ((s3_u32)(now - c->punch_t0) < S7_PUNCH_WIDE_MS) off = OFF[c->punch_n % S7_PUNCH_RING];
        else { s3_u32 i = c->punch_n % S7_PUNCH_WIDE_N; off = i < 64u ? (s3_s32)i + 1 : -(s3_s32)(i - 63u); }   /* the first ring found nothing: +1 .. +64, then -1 .. -16 */
        port = (s3_s32)c->dst_port + off;
        c->punch_last = now; c->punch_n++;
        if (port >= 1024 && port <= 65535) (void)e_send(c, c->dst_ip, (sde_u16)port, dg, 2u);
    }
    if (c->dg_want && c->state == S3_SES_RUN && c->active >= 0) setup_send(c);   /* MATCH SETUP */
#ifdef MS_QCHAT
    if (c->chat_want) {                                          /* QUICK CHAT: reliable, channel 0, to the connected opponent.  Pressed while the peer is still connecting (the bridge route: the
                                                                    assignment is out, the link is not), the message waits for the connection (S7_CHAT_HOLD_TICKS), never longer, never across sessions */
        if (c->state == S3_SES_RUN && c->active >= 0 && sdenet_state(&c->enet, c->active) == SDE_CONNECTED) {
            if (sdenet_send(&c->enet, c->active, 0, SDENET_RELIABLE, c->chat_tx, 6) != 0) ST->tx_fail++;
            c->chat_want = 0;
        } else if ((c->state != S3_SES_RUN && c->state != S7_SES_CONNECTING) || (s3_u16)(S7_CHAT_NOW(c) - (((s3_u16)c->chat_tx[6] << 8) | c->chat_tx[7])) > S7_CHAT_HOLD_TICKS) c->chat_want = 0;
    }
#endif
    sdenet_service(&c->enet, now);
    enet_stats(c);
}

/* POST-GAME PEER GONE / SESSION END (see the header) */
s3_u32 s7_core_silence_ms(const struct s3_core *c, s3_u32 now_ms)
{
    const struct sdenet_peer *P;
    if (c->state != S3_SES_RUN || c->active < 0) return 0;
    P = &c->enet.peer[c->active];
    if (P->state != SDE_CONNECTED || P->last_rx_ms == 0) return 0;
    return (s3_s32)(now_ms - P->last_rx_ms) > 0 ? now_ms - P->last_rx_ms : 0;
}

void s7_core_peer_gone(struct s3_core *c)
{
    int i;
    for (i = 0; i < SDENET_MAX_PEERS; i++) if (sdenet_state(&c->enet, i) != SDE_FREE) sdenet_disconnect(&c->enet, i, 0);
    c->active = -1;
    why_set(c, S7W_SILENT, 0);
    c->disconnected = 1;
    c->state = S7_SES_LOST;
    ST->state = (s3_u32)c->state;
    XS->enet_discs++;
}

void s7_core_end_session(struct s3_core *c, s3_u32 now_ms) { s7_core_end_session_to(c, now_ms, 0, 0); }
void s7_core_end_session_to(struct s3_core *c, s3_u32 now_ms, s3_u32 ip, s3_u32 port)
{
    int i, keep[SDENET_MAX_PEERS];
    if (c->state == S7_LOOPBACK) return;
    for (i = 0; i < SDENET_MAX_PEERS; i++) {
        const struct sdenet_peer *P = &c->enet.peer[i];
        int same = ip && P->ip == ip && P->port == (sde_u16)port;
        keep[i] = same && sdenet_state(&c->enet, i) == SDE_CONNECTED && c->up_ms[i] && (s3_u32)(now_ms - c->up_ms[i]) <= S7_REJOIN_KEEP_MS;
        if (!same && sdenet_state(&c->enet, i) == SDE_CONNECTED) sdenet_disconnect(&c->enet, i, 0);
    }
    sdenet_service(&c->enet, now_ms);                            /* flush the DISCONNECT datagrams while the peer slots still exist */
    for (i = 0; i < SDENET_MAX_PEERS; i++) if (!keep[i]) { sdenet_reset_peer(&c->enet, i); c->up_ms[i] = 0; }
    sdenet_set_allow(&c->enet, 0, 0, 0);
    c->active = -1;
    c->side = -1;
    ST->side = 0xFFu;
    c->state = S3_NET_WAIT;
    ST->state = (s3_u32)c->state;
    c->disconnected = 0;
    c->dst_ip = 0; c->dst_port = 0;
    for (i = 0; i < S7_SEL_LEN; i++) { c->lsel[i] = 0; c->rsel[i] = 0; }
    c->rsel_have = 0;
    c->slippi = 0; c->fixed_rules = 0; c->n_stages = 0;
    c->rules_have = 0; c->items = 0;                             /* MATCH SETUP: the next ticket gives its settings again */
    c->seats = 0;                                                /* DELAY-ONLY LOBBIES: the next ticket names its seat count again */
    c->last_frame = 0;
    c->ever_up = 0;
    c->game_started = 0;
    c->sel_reset = 0;
    c->ack_q_frame = 0;
    c->next_connect_ms = 0;
#ifdef MS_QCHAT
    c->chat_want = 0;                                            /* QUICK CHAT: nothing waiting survives the session */
#endif
}

/* S7: SlippiNetplay::StartSlippiGame + the frame-1 part of EXI handleOnlineInputs: local queue cleared, frames 1..delay
   queued as empty pads (each one sent, as handleSendInputs does), lastFrameTiming = (0, now), acks reset, skip state
   reset.  The offset ring is NOT cleared (Slippi never flushes it).  The remote queue is kept for the first match (the
   peer may already be sending; Slippi clears it when the selections arrive) and cleared for later matches (no match
   number on the wire; as ). */
s3_u32 s7_punch_mode = 2u;                                      /* RING: 0 off (only the reported port and ENet's CONNECT retries), 1 narrow (+-3), 2 the full ring; the glue sets it */
s3_u32 s7_punch_gap_ms = S7_PUNCH_GAP_MS;                       /* RING: ms between two ring datagrams (the rate cap) */
s3_u32 s7_new_div = 1u;
static s3_u32 s7_stall_t0;                   /* clock_ms at the first call of the current stall run (time guard) */                                        /* 1 = a pad packet for every new frame; the Wii engine sets 2 (a packet carries every unacknowledged frame anyway) */
s3_u32 s7_stall_div = 1u;                                      /* 1 = re-send on every stalled call (Slippi, GameCube); the Wii engine sets 3 */
s3_u32 s7_stall_min_ms = 0u;                                   /* 0 = off (Slippi, GameCube: the call-count rule above alone). Wii: a stalled call re-sends only when this many ms passed since the last stalled re-send (the engine calls this about 17 times a frame while stalled) */
static s3_u32 s7_stall_tx_ms;                                   /* clock_ms of the last stalled re-send */
static s3_u32 s7_pad_n, s7_pad_ms;                              /* PEER LEFT THE GAME: the pad count (rx_pkts) last seen in a stall run and when it last changed */
#ifndef S7_PADLESS_MS
#define S7_PADLESS_MS 7000u
#endif
static void send_pad(struct s3_core *c);
static void new_match(struct s3_core *c, s3_u32 delay)
{
    s3_u32 i, j;
    for (i = 0; i < S3_RING; i++) {
        c->ltag[i] = 0; c->lckf[i] = 0; c->lck[i] = 0;
        for (j = 0; j < S3_PAD_BYTES; j++) c->lpad[i][j] = 0;
        c->lrng[i] = 0;
    }
    for (i = 0; i < S3_CKS_HIST; i++) { c->hist_frame[i] = 0; c->hist_cks[i] = 0; }
    /* (console, 2nd and 3rd game of a boot: both sides stalled at frame 7): a fresh session's peer starts its frames as soon as it is connected, long before this
       console has loaded the stage; those pads arrive after the MATCH_SELECTIONS (on_sel reset the queue) and are ACKED, so the peer never sends them again.  Wiping them here
       left the console waiting for frames the peer considers delivered (stall, then DISCONNECTED).  The wipe stays for a rematch on a live session, where on_sel is ignored
       (last_frame >= 1) and this is the only reset. */
    if (ST->match_no > 0 && !c->sel_reset) {
        for (i = 0; i < S3_RING; i++) c->rtag[i] = 0;
        c->rlatest = 0; c->peer_cks_frame = 0; c->peer_cks = 0; ST->rlatest = 0;
        c->rhash = 2166136261u;
    }
    /* POST-GAME READY FLAGS: the lock-in flags of THIS game stayed set, so after the game the online CSS read
       "both players ready" at once and went on (CSS -> stage select -> CSS ...) with nobody having chosen anything; B on the stage select only bounced.  Slippi resets the
       selections when the game starts (EXI handleOnlineInputs frame 1: localSelections.Reset()).  Only the two "selected" flags are cleared; characters, colours and the
       stage stay (the match block of this game is still read after it), and the peer's NEXT lock-in sets its flag again (D5 merge).
 (live Direct set vs our PC, game 2 desynced at its start): the isStageSelected flags are cleared the same way.  The merge takes the stage of the first
       player BY INDEX who selected one; the peer's flag from game 1 stayed set, so when only OUR player picked the stage for game 2 we still read the peer's old stage
       (Stadium) while Slippi, which resets the selections at game start, used ours (Venom).  Characters and the stage ids stay; the flags come back with the next lock-in. */
    c->lsel[3] = 0; c->rsel[3] = 0; c->lsel[7] = 0; c->rsel[7] = 0;
    c->sel_reset = 0;
    c->disc_run = 0;
    c->dg_peer_have = 0; c->dg_sent = 0; c->dg_want = 0; c->dg_t = 0;   /* MATCH SETUP: the next game exchanges its own digests */
    c->at_n = 0; c->lacked = 0; c->lnewest = 0; c->stall_frame = 0; c->disconnected = c->state == S7_SES_LOST;
    c->game_started = 0; c->lft_frame = 0; c->lft_us = c->clock_us(c->ctx);
#ifdef OD_SELECT_LOCK
    if (c->side == 0 && !c->slippi) RNG_ROLL(c);                 /* our route: a fresh rngOffset (and so a fresh random stage) for every game */
#endif
    c->ts_skip = 0; c->ts_skipping = 0; c->ts_last_corr = 0; c->ts_over = 0;
    c->lhash = 2166136261u;
    ST->lnewest = 0; ST->lacked = 0; ST->stall_run = 0; ST->cks_last_frame = 0;
    ST->dz_first = 0; ST->dz_lcks = 0; ST->dz_pcks = 0; ST->dz_shift = 0;   /* the desync locator is per game */
    ST->match_no++;
    for (i = 1; i <= delay; i++) {
        c->ltag[i & (S3_RING - 1)] = (s3_s32)i;
        c->lhash = fnv8(c->lhash, i, c->lpad[i & (S3_RING - 1)]);
        c->lnewest = (s3_s32)i;
        if (c->state == S3_SES_RUN) send_pad(c);                 /* Slippi sends one packet per queued empty pad */
    }
    ST->lnewest = (s3_u32)c->lnewest;
    XS->lpad_hash = c->lhash;
}

/* S7: SlippiNetplay::SendSlippiPad: every queued pad from the newest down to max(last acked, newest - 128), newest
   first, header frame = newest tag, checksumFrame / checksum stored with the newest queued pad (the finalized frame and
   its checksum from the TXB of the B0 call that queued it; 0/0 for the empty delay pads); lastFrameTiming and ackTimers
   updated with the send time (also for stall re-sends).  Nothing is sent once the session is disconnected (Slippi
   returns early when the connect status is DISCONNECTED/FAILED). */
static void send_pad(struct s3_core *c)
{
    s3_s32 hi = c->lnewest, lo = c->lacked, f;
    s3_u32 win = (c->cfg->window && !s7_core_is_slippi(c)) ? c->cfg->window : S7_WINDOW_MAX, n, i, now;   /* D3: a Slippi peer gets the full window */
    if (c->active < 0 || hi < 1 || c->disconnected) return;
    if (win > S7_WINDOW_MAX) win = S7_WINDOW_MAX;
    if (lo < hi - 128) lo = hi - 128;
    if (lo < 1) lo = 1;
    n = (s3_u32)(hi - lo + 1);
    if (n > win) n = win;                                        /* lab knob only (cfg window); Slippi: never */
    c->pkt[0] = S7_MSG_PAD;
    put32(c->pkt + 1, (s3_u32)hi);
    c->pkt[5] = (s3_u8)c->side;
    put32(c->pkt + 6, (s3_u32)c->lckf[hi & (S3_RING - 1)]);
    put32(c->pkt + 10, c->lck[hi & (S3_RING - 1)]);
    for (i = 0; i < n; i++) {
        f = hi - (s3_s32)i;
        copy(c->pkt + S7_PAD_HDR + i * S7_PAD_WIRE, c->lpad[f & (S3_RING - 1)], S3_PAD_BYTES);
        c->pkt[S7_PAD_HDR + i * S7_PAD_WIRE + S3_PAD_BYTES] = c->lrng[f & (S3_RING - 1)];   /* SLIPPI 3.7.0: the RNG bits of that frame's pad */
    }
    if (n > ST->win_max) ST->win_max = n;
    if (sdenet_send(&c->enet, c->active, 1, SDENET_UNSEQUENCED, c->pkt, S7_PAD_HDR + n * S7_PAD_WIRE) == 0) {
        ST->tx_pkts++;
        now = c->clock_us(c->ctx);
        c->game_started = 1;
        c->lft_frame = hi; c->lft_us = now;
        c->at_frame[c->at_head] = hi; c->at_us[c->at_head] = now;
        c->at_head = (c->at_head + 1) % S7_ACK_TIMERS;
        if (c->at_n < S7_ACK_TIMERS) c->at_n++;
    }
}

/* S7: CalcTimeOffsetUs: sort the ring, mean of the middle third (integer, as the C++), 0 when empty */
s3_s32 s7_time_offset_us(const struct s3_core *c)
{
    s3_s32 b[S7_TS_RING];
    long long sum = 0;
    s3_u32 n = c->ts_n, i, j, lo, hi;
    if (!n) return 0;
    for (i = 0; i < n; i++) {
        s3_s32 v = c->ts_buf[i];
        for (j = i; j > 0 && b[j - 1] > v; j--) b[j] = b[j - 1];
        b[j] = v;
    }
    lo = n / 3; hi = n - lo;                                     /* C++: (int)((1/3.0f) * size) */
    if (hi <= lo) return 0;
    for (i = lo; i < hi; i++) sum += b[i];
    {   /* same quotient as the signed sum / (hi - lo) (C truncates toward zero), divided unsigned with s7_udiv64s: the engine needs neither __divdi3
           nor __udivdi3 (2,060 bytes of libgcc in the card hole; |sum| < 2^36 with 30 s32 samples, so the negation cannot overflow; hi - lo <= 30) */
        unsigned long long q = s7_udiv64s((unsigned long long)(sum < 0 ? -sum : sum), hi - lo);
        return (s3_s32)(sum < 0 ? -(long long)q : (long long)q);
    }
}

/* n / d for 1 <= d < 65536, exact (the C quotient), with 32-bit divisions only: schoolbook long division in four 16-bit digits, the remainder
   (< d < 2^16) shifted into the next digit, so every partial dividend fits 32 bits and every digit quotient fits 16 bits.  It replaces libgcc's
   __udivdi3 (964 bytes) in the engine; its only callers divide by the middle-third count (<= 30) and by 100 (s12_gc.c hw).  Host test
    against the native 64-bit division. */
#define S7_DSTEP(x) (r = ((r << 16) | (x)), q_ = r / d, r -= q_ * d, q_)
unsigned long long s7_udiv64s(unsigned long long n, s3_u32 d)
{
    s3_u32 hi = (s3_u32)(n >> 32), lo = (s3_u32)n, r = 0, q_, qh, ql;
    qh = S7_DSTEP(hi >> 16) << 16; qh |= S7_DSTEP(hi & 0xFFFFu);
    ql = S7_DSTEP(lo >> 16) << 16; ql |= S7_DSTEP(lo & 0xFFFFu);
    return ((unsigned long long)qh << 32) | ql;
}
#undef S7_DSTEP

/* S7: the newest remote frame the game may see, as Slippi's GetSlippiRemotePad after DropOldRemoteInputs(finalized):
   the queue keeps frames >= the finalized frame and at most 7 are handed out, so latestFrame <= finalized + 6.  Used for
   the RXB and for the rollback-limit rule (Slippi's shouldSkipOnlineFrame reads the same capped latestFrame).  Without
   the cap (S6b, cfg s7_flags bit4) a jump of the remote head can make the ASM read an RXB entry 7 frames old (outside
   the 7-entry history); host_gc7 hit that once against a PC model that advanced in bursts. */
static s3_s32 eff_latest(const struct s3_core *c)
{
    s3_s32 rl = c->rlatest, cap = (c->cur_fin > 0 ? c->cur_fin : 0) + S6N_RB_MAX - 1;
    if (!(c->xcfg->s7_flags & S7F_RXB_S6B) && rl > cap) rl = cap;
    if (c->latest_cap) { s3_s32 lc = c->latest_cap(c->ctx, c->cur_fin); if (lc > 0 && rl > lc) rl = lc; }   /* S18 gap guard */
    return rl;
}

/* S6C: may frame `frame` run on prediction?  (S7: on the capped latest frame) */
static int pred_ok(struct s3_core *c, s3_s32 frame, s3_u32 d, int *plain)
{
    s3_s32 rl = eff_latest(c), x = rl + 1, g;
    *plain = rl >= frame - (s3_s32)d;
    if (!*plain || !c->load_frame || x > frame || (c->cfg->s6c_flags & S6CF_NO_GAPCAP)) return *plain;
    g = c->load_frame(c->ctx, x, frame);
    if (g > x) g = x;
    if (frame + 1 - g > (s3_s32)d) return 0;
    if (g < x && frame + 1 - g > S6N_RB_MAX - 1) return 0;
    return 1;
}

/* S7: time sync (replaces the S6b frame-advantage sync).  Called only when the frame could run (Slippi evaluates time
   sync only when no player needs inputs).  Returns 1 when this call must stall.  Modes (s7_cfg ts_mode):
     1 Slippi's own rule: frame % 30 == 0, frame <= 120, offset > 10 ms -> skip (offset - 10 ms) / 16.683 ms + 1 frames
       (max 5), once per sync point (isCurrentlySkipping);
     2 console pair (symmetric): rule 1, plus after frame 120 at every sync point: offset >= thr
       (default 12.5 ms = 0.75 frame) -> skip round(offset / 16.683 ms) frames (1..3), then no correction for `gap`
       frames (default 60: the 30-sample ring is renewed by then).  Only the leading side has a positive offset;
     3 passive (against a PC peer, which can nudge its own speed): rule 1, plus after frame 120: offset >= thr
       (default 25 ms = 1.5 frames) at two consecutive sync points -> skip round(offset / 16.683 ms) - 1 (1..2),
       gap 150 frames.  A lagging console never advances (B0 result 4 is not used by S7). */
static int time_sync(struct s3_core *c, s3_s32 frame)
{
    s3_u32 mode = c->xcfg->ts_mode;
    if (!mode || (c->cfg->flags & S6F_NO_SYNC)) return 0;
    if (frame % 30 == 0 && !c->ts_skipping) {
        s3_s32 off = s7_time_offset_us(c);
        XS->ts_syncs++;
        ST->sync_adv = (s3_u32)off;
        if (frame <= 120) {
            if (off > 10000) {
                s3_s32 k = (off - 10000) / S7_FRAME_US + 1;
                c->ts_skipping = 1;
                c->ts_skip = k > 5 ? 5 : k;
                XS->ts_corr++;
                XS->ts_start_skips += (s3_u32)c->ts_skip;
            }
        } else {
            s3_s32 a = off < 0 ? -off : off, ms = a / 1000;
            s3_u32 b = ms < 2 ? 0 : ms < 4 ? 1 : ms < 8 ? 2 : a < 12500 ? 3 : a < 16683 ? 4 : ms < 25 ? 5 : ms < 50 ? 6 : 7;
            if (!XS->ts_n_run || off < (s3_s32)XS->ts_off_min) XS->ts_off_min = (s3_u32)off;
            if (!XS->ts_n_run || off > (s3_s32)XS->ts_off_max) XS->ts_off_max = (s3_u32)off;
            XS->ts_n_run++;
            XS->ts_abs_sum_ms += (s3_u32)ms;
            XS->ts_hist[b]++;
            if (mode >= 2) {
                s3_s32 thr = c->xcfg->ts_thr_us ? (s3_s32)c->xcfg->ts_thr_us : mode == 2 ? 12500 : 25000;
                s3_s32 gap = c->xcfg->ts_gap ? (s3_s32)c->xcfg->ts_gap : mode == 2 ? 60 : 150;
                if (off >= thr) c->ts_over++; else c->ts_over = 0;
                if (off >= thr && (mode == 2 || c->ts_over >= 2) && (!c->ts_last_corr || frame - c->ts_last_corr >= gap)) {
                    s3_s32 k = (off + S7_FRAME_US / 2) / S7_FRAME_US - (mode == 3 ? 1 : 0), mx = mode == 2 ? 3 : 2;
                    if (k > mx) k = mx;
                    if (k >= 1) {
                        c->ts_skipping = 1;
                        c->ts_skip = k;
                        c->ts_last_corr = frame;
                        c->ts_over = 0;
                        XS->ts_corr++;
                        XS->ts_run_skips += (s3_u32)k;
                    }
                }
            }
        }
    }
    if (c->ts_skip > 0) { c->ts_skip--; return 1; }
    c->ts_skipping = 0;
    return 0;
}

int s3_core_b0(struct s3_core *c, const s3_u8 *txb, s3_u8 *rxb)
{
    s3_s32 frame = (s3_s32)get32(txb + 1), fin = (s3_s32)get32(txb + 5), f;
    s3_u32 cks = get32(txb + 9), delay = txb[13], i, j;
    const s3_u8 *pad = txb + 14;
    int res, have, run;

    ST->b0_calls++;
    ST->b0_frame = (s3_u32)frame;
    if (delay < 1) delay = 1;
    if (delay > 15) delay = 15;                                  /* S7: as */
    c->cur_fin = fin;                                            /* S7: for eff_latest */
    if (frame < 1 || frame > 0x3FFFFFFF) frame = 1;              /* S7: the ASM counts from 1; keeps the tag math in range */
    if (c->ack_q_frame && c->ack_q_peer >= 0) { send_ack(c, c->ack_q_peer, c->ack_q_frame); c->ack_q_frame = 0; }   /* S7F_ACK_LATE */
    if (frame == 1 && c->last_frame != 1) new_match(c, delay);
    c->last_frame = frame;

    if (fin > 0) {                                               /* local Slippi checksum of the finalized frame */
        c->hist_frame[fin & (S3_CKS_HIST - 1)] = fin;
        c->hist_cks[fin & (S3_CKS_HIST - 1)] = cks;
        c->cks->e[fin & 7][0] = 0xFFFFFFFFu;
        c->cks->e[fin & 7][1] = cks;
        c->cks->e[fin & 7][0] = (s3_u32)fin | ((ST->match_no & 0x7Fu) << 24);
    }

    if (c->state == S7_LOOPBACK) {                               /* S7: reference, the script plays the remote side */
        while (c->rlatest < frame) {
            s3_u8 sp[S3_PAD_BYTES];
            s3_s32 t = c->rlatest + 1;
            /* S7: a Slippi peer's ASM zeroes its pads while the start freeze lasts (TriggerSendInput.asm section 1:
               frame < START_SYNC_FRAME (78) - delay, i.e. tags < 78), so the reference sees neutral pads there too */
            s4_script_pad(c->xcfg->script_seed, (t <= (s3_s32)delay || t < S7_FREEZE_TAG) ? 0 : t, sp);
            store_remote(c, t, sp, 0);
            c->rlatest = t;
        }
        ST->rlatest = (s3_u32)c->rlatest;
    }
    /* S7: a Slippi game cannot start before the remote selections are in (prepareOnlineMatchState remotePlayersReady,
       and the received selections reset the remote pad queue): frame 1 waits for them (first_wait stalls). */
    run = (c->state == S3_SES_RUN && (c->rsel_have || frame > 1)) || c->state == S7_LOOPBACK;

    {   /* S6b/S6C prediction rule (unchanged) */
        s3_u32 d = c->cfg->rb_depth > S6N_RB_MAX ? S6N_RB_MAX : c->cfg->rb_depth;
        int plain;
        if (!s7_core_rollback_allowed(c)) d = 0;                 /* DELAY-ONLY LOBBIES: 3-4 human seats = lockstep, never a prediction (2 seats: unchanged) */
        if (c->load_frame && d) d = c->cap > d ? d : c->cap;
        have = run && pred_ok(c, frame, d, &plain);
        if (!have && c->state == S3_SES_RUN && c->cfg->spin_us && c->poll) {
            s3_u32 t0 = c->clock_us(c->ctx);
            while (!pred_ok(c, frame, d, &plain) && c->clock_us(c->ctx) - t0 < c->cfg->spin_us) { c->poll(c->ctx); ST->spin_polls++; }
            have = pred_ok(c, frame, d, &plain);
            if (have) ST->spin_hits++;
        }
        if (have && c->lnewest - c->lacked >= S3_RING - 8 && !s7_core_is_slippi(c)) { have = 0; ST->lfull++; }   /* D3: Slippi has no such stall */
        else if (!have && run && frame > 1 && d) {
            ST->stall_cap++;
            if (plain) ST->stall_gap++;
            if (c->rlatest >= frame - (s3_s32)d && eff_latest(c) < frame - (s3_s32)d) XS->resv[1]++;   /* stall_fin */
        }
    }

    /* S7: time sync from arrival times (Slippi's estimator) instead of the S6b byte */
    c->ts_stalled = 0;
    if (have && c->pq_hold && c->state == S3_SES_RUN) { have = 0; c->pq_holds++; c->ts_stalled = 1; }   /* PQ GUARD: the game's pad queue is 4 deep (its logic thread is blocked): stall, never let the queue drop an accepted frame */
    if (have && c->state == S3_SES_RUN && frame > 1 && time_sync(c, frame)) { have = 0; ST->sync_stalls++; c->ts_stalled = 1; }

    if (txb[S7_TXB_LEAVING] && !c->disconnected && frame > 1) {   /* SLIPPI 3.7.0 (EXI handleOnlineInputs isLeaving): the local player left (party mode pause exit): ForceDisconnect, the game ends as on any disconnect */
        int q;
        why_set(c, S7W_LEAVING, 0);
        for (q = 0; q < SDENET_MAX_PEERS; q++) if (sdenet_state(&c->enet, q) != SDE_FREE) sdenet_disconnect(&c->enet, q, 0);
        c->disconnected = 1;
    }
    if (c->disconnected) {
        res = 3;
    } else if (have) {
        res = 1;
        if (ST->stall_run && frame > 1) {                        /* S6D: length of the stall run that just ended */
            s3_u32 n = ST->stall_run;
            c->stall_hist[n <= 3 ? n - 1 : n <= 5 ? 3 : n <= 10 ? 4 : n <= 30 ? 5 : n <= 120 ? 6 : 7]++;
        }
        ST->stall_run = 0;
        c->disc_run = 0;
        if (eff_latest(c) < frame) {
            s3_s32 rl = eff_latest(c);
            ST->cont_pred++;
            if ((s3_u32)(frame - rl) > ST->pred_max) ST->pred_max = (s3_u32)(frame - rl);
        }
        f = frame + (s3_s32)delay + ((c->xcfg->s7_flags & S7F_DA_NEG) && c->side == 1 ? 1 : 0);   /* local pad for frame F is used remotely at F+delay (DA_NEG, TEST: side B one frame late) */
        if (c->raise_hook && c->state == S3_SES_RUN && s7_core_rollback_allowed(c) && delay < 15u && c->lnewest > 0 && f == c->lnewest + 1 && c->raise_hook(c->ctx, (s3_u32)frame, delay)) {
            /* STALL AUTO DELAY: the glue raised ODB_DELAY_FRAMES by one inside this B0 (a ring slot inserted at the write end), so this pad is tagged one later and the tag in between
               repeats the newest pad, exactly what the ASM's own ring plays at that frame; the wire is unchanged (contiguous tags, a Slippi peer sees one repeated input) */
            c->ltag[f & (S3_RING - 1)] = f;
            copy(c->lpad[f & (S3_RING - 1)], c->lpad[c->lnewest & (S3_RING - 1)], S3_PAD_BYTES);
            c->lrng[f & (S3_RING - 1)] = c->lrng[c->lnewest & (S3_RING - 1)];   /* the ASM's ring repeats the whole report, RNG bits included */
            c->lckf[f & (S3_RING - 1)] = 0; c->lck[f & (S3_RING - 1)] = 0u;
            if (f <= S7_HASH_TAG) { c->lhash = fnv8(c->lhash, (s3_u32)f, c->lpad[f & (S3_RING - 1)]); XS->lpad_hash = c->lhash; }
            c->lnewest = f; ST->lnewest = (s3_u32)f;
            f++;
        }
        if (f > c->lnewest) {
            c->ltag[f & (S3_RING - 1)] = f;
            copy(c->lpad[f & (S3_RING - 1)], pad, S3_PAD_BYTES);
            c->lrng[f & (S3_RING - 1)] = pad[S3_PAD_BYTES];     /* SLIPPI 3.7.0: TXB pad byte 8 = the bits the ASM took from RXB_NEXT_PAD_RNG_BITS */
            c->lckf[f & (S3_RING - 1)] = fin > 0 ? fin : 0;    /* S7: checksum travels with the pad (Slippi) */
            c->lck[f & (S3_RING - 1)] = fin > 0 ? cks : 0u;
            if (f <= S7_HASH_TAG) { c->lhash = fnv8(c->lhash, (s3_u32)f, pad); XS->lpad_hash = c->lhash; }
            c->lnewest = f;
            ST->lnewest = (s3_u32)f;
        }
        if (c->state == S7_LOOPBACK) { c->lacked = c->lnewest; ST->lacked = (s3_u32)c->lacked; }
    } else {
        s3_u32 lim = frame <= 1 ? c->cfg->first_wait : c->cfg->stall_disc;
        res = 2;
        ST->stall_calls++;
        ST->stall_run++;
        if (frame <= 1) ST->first_stalls++;
        else {
            if (c->stall_frame != frame) { c->stall_frame = frame; ST->stall_frames++; }
            if (ST->stall_run > ST->stall_max_run) ST->stall_max_run = ST->stall_run;
        }
        /* D9: Slippi counts rollback-limit stalls only (a time-sync halt happens only when inputs are enough and resets the counter);
           our own peers keep the old rule (every stalled call counts) */
        if (c->ts_stalled) c->disc_run = 0; else c->disc_run++;
        /* time guard: the stall limit counts CALLS, so a console whose calls are slow (the Wii on a slow kernel: minutes) gave up long after the Cube; our own peers also give up after 10 s of wall clock
           (30 s since the experiment, stall_disc 1800).
           PEER LEFT THE GAME: a peer that left its match scene
           (it ended the game on a checksum desync and went to the CSS) sends no DISCONNECT and keeps its ENet link alive (pings, acks), but no PAD message any more.
           A live peer sends pads on every new frame and re-sends while it stalls itself, so: no pad for S7_PADLESS_MS while the link is heard (< 3 s silence) = it left;
           our own peers end the match then.  A blackout (no datagram at all) keeps the 30 s rule (the long-distance case of the experiment). */
        {
            s3_u32 now = c->clock_ms(c->ctx);
            if (ST->stall_run == 1u) s7_stall_t0 = now;
            if (ST->stall_run == 1u || ST->rx_pkts != s7_pad_n) { s7_pad_n = ST->rx_pkts; s7_pad_ms = now; }
            if (lim && (s7_core_is_slippi(c) ? c->disc_run : ST->stall_run) > lim) { why_set(c, frame <= 1 ? S7W_FIRST : S7W_STALL, lim); c->disconnected = 1; res = 3; }
            else if (frame > 1 && !s7_core_is_slippi(c) && ST->stall_run > 1u &&
                     (now - s7_stall_t0 > 30000u || (now - s7_pad_ms > S7_PADLESS_MS && s7_core_silence_ms(c, now) < 3000u))) { why_set(c, S7W_PADLESS, now - s7_stall_t0); c->disconnected = 1; res = 3; }
        }
    }

    if (c->state == S3_SES_RUN && (res == 2 ? (s7_stall_min_ms ? (ST->stall_run == 1u || (s3_u32)(c->clock_ms(c->ctx) - s7_stall_tx_ms) >= s7_stall_min_ms)
                                                                : (s7_stall_div <= 1u || ST->stall_run == 1u || ST->stall_run > 30u || (ST->stall_run % s7_stall_div) == 0u))
                                            : (s7_new_div <= 1u || ((s3_u32)c->lnewest % s7_new_div) == 0u)))
    {
        send_pad(c);
        if (res == 2) s7_stall_tx_ms = c->clock_ms(c->ctx);
    }                                                           /* every call: new pad, or the stall re-send (Slippi); a Wii sends every second new frame and every third stalled call only (its network path takes about 50 packets a second, ALPHA_OPEN_FINDINGS 54) */

    rxb[0] = (s3_u8)res;
    if (res == 1) ST->cont++;
    else if (res == 3) ST->disc++;
    rxb[1] = 1;                                                  /* one remote player */
    /* Slippi: the remote checksum of the last PAD parsed (0/0 while the remote queue is empty) */
    put32(rxb + 2, c->rlatest > 0 ? (s3_u32)c->peer_cks_frame : 0u);
    put32(rxb + 6, c->rlatest > 0 ? c->peer_cks : 0u);
    s12_core_rxb_inputs(c, rxb, frame, 1);                      /* S12: factored out (same bytes, same order) */
    /* SLIPPI 3.7.0 (EXI prepareOpponentInputs: padRngBitsSource & 0xFF): random bits for the ASM's NEXT pad; never from the game's generator (the opponent sees them).
       xorshift32 stirred with the microsecond clock of every call. */
    c->rng_s ^= c->clock_us(c->ctx) + 0x9E3779B9u + ((s3_u32)frame << 16);
    if (!c->rng_s) c->rng_s = 0x6D2B79F5u;
    c->rng_s ^= c->rng_s << 13; c->rng_s ^= c->rng_s >> 17; c->rng_s ^= c->rng_s << 5;
    rxb[S7_RXB_RNG] = (s3_u8)(c->rng_s >> 11);
    (void)i; (void)j;
    if (c->state != S7_LOOPBACK) enet_stats(c);
    return 0;
}

/* S12: the RXB opponent part of s3_core_b0 (frame numbers +26..+41, inputs +42..), unchanged code moved into a function
   so that the S12 early-rollback check (main loop, idle gap) can refresh the RXB from the inputs received since the
   last B0 exactly the way the next B0 would.  `count_neg` = 1 only from B0 (the neg_applied counter). */
void s12_core_rxb_inputs(struct s3_core *c, s3_u8 *rxb, s3_s32 frame, int count_neg)
{
    s3_u32 i, j;
    s3_s32 f;
    {   /* S6b: RXB as prepareOpponentInputs; S7: latest capped at finalized + 6 as Slippi (eff_latest) */
        s3_s32 rl = eff_latest(c), lat = rl < frame ? rl : frame;
        put32(rxb + 26, (s3_u32)lat);
        put32(rxb + 30, (s3_u32)frame);
        put32(rxb + 34, (s3_u32)frame);
        put32(rxb + 38, (s3_u32)lat);
        for (j = 0; j < 7; j++) {
            f = lat - (s3_s32)j;
            if (f >= 1 && c->rtag[f & (S3_RING - 1)] == f) {
                for (i = 0; i < S3_PAD_BYTES; i++) rxb[42 + j * 12 + i] = c->rpad[f & (S3_RING - 1)][i];
                rxb[42 + j * 12 + 8] = c->rrng[f & (S3_RING - 1)];   /* SLIPPI 3.7.0 PAD_RNG_BITS; +9..+11 stay 0 (+11 = PAD_RNG_ROLLBACK_MARKER: always 0 in a remote pad) */
                rxb[42 + j * 12 + 9] = 0; rxb[42 + j * 12 + 10] = 0; rxb[42 + j * 12 + 11] = 0;
                if ((c->cfg->flags & 2u) && c->side == 1 && f >= (s3_s32)c->cfg->neg_frame) {
                    rxb[42 + j * 12] ^= 0x01u;
                    if (count_neg && j == 0 && f == frame) ST->neg_applied++;
                }
            }
        }
        /* SLIPPI 3.7.0 tail (EXI prepareOpponentInputs): despawn frames 0 (1v1: a disconnect ends the game), then the 7 inputs to predict the frames after the latest with:
           the latest received input each (no input suggestions: a console sends none and uses none, which Slippi tolerates), slots 2 and 3 zero. */
        for (i = S7_RXB_DESPAWN; i < S7_RXB_RNG; i++) rxb[i] = 0;
        for (j = 0; j < 7; j++) for (i = 0; i < 12; i++) rxb[S7_RXB_PRED + j * 12 + i] = rxb[42 + i];
    }
}

/* S12: newest remote frame the RXB may show (eff_latest), for the early-rollback check */
s3_s32 s12_core_eff_latest(const struct s3_core *c) { return eff_latest(c); }

/* ---- SLIPPI INTEROP D10: stage pick and rngOffset as CEXISlippi does them (getRandomStage / setMatchSelections) ---- */
static const s3_u16 SLIPPI_STAGES6[6] = { 0x02, 0x03, 0x08, 0x1C, 0x1F, 0x20 };   /* Slippi's built-in list when the ticket names none */
static s3_u32 gen_next(struct s3_core *c)
{
    s3_u32 x = c->gen ? c->gen : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->gen = x;
    return x;
}
/* getRandomStage with a fresh pool: one uniform draw from the allowed list (Slippi removes the draw from a pool that refills when
   empty; our draw happens once per match, so the pool is always fresh) */
static s3_u16 slippi_random_stage(struct s3_core *c)
{
    s3_u32 n = c->n_stages ? c->n_stages : 6u;
    s3_u32 k = gen_next(c) % n;
    return c->n_stages ? c->stages[k] : SLIPPI_STAGES6[k];
}
static int stage_allowed(const struct s3_core *c, s3_u32 st)
{
    s3_u32 i, n = c->n_stages ? c->n_stages : 6u;
    for (i = 0; i < n; i++) if ((c->n_stages ? c->stages[i] : SLIPPI_STAGES6[i]) == st) return 1;
    return 0;
}
int s7_core_slippi_match(struct s3_core *c, const struct s7_slippi_match *m)
{
    s3_u32 i, sel;
    if (!m) return -1;
    c->slippi = 1;
    c->fixed_rules = m->fixed_rules ? 1 : 0;
    c->items = m->items;                                          /* Slippi m_mmResult.items (prepareOnlineMatchState: item switch bits and frequency) */
    c->rules_have = 0;
    c->gen = m->seed ? m->seed : 1u;
    for (i = 0; i < 4; i++) gen_next(c);                          /* spread a small seed */
    c->n_stages = m->n_stages > 16u ? 16u : m->n_stages;
    for (i = 0; i < c->n_stages; i++) c->stages[i] = m->stages[i];
    c->side = m->is_decider ? 0 : 1;                              /* Slippi: localPlayerIndex = isDecider ? 0 : 1 (1v1) */
    ST->side = (s3_u32)c->side;
    c->dst_ip = m->opp_ip; c->dst_port = m->opp_port;
    for (i = 0; i < 8; i++) c->why[i] = 0;
    sdenet_set_allow(&c->enet, m->opp_ip, 0, 1);                  /* the opponent's address, any source port */
    sel = m->char_color ? (m->char_color << 16) : c->xcfg->sel[c->side & 1];
    if (c->lsel[0] != S7_MSG_SELECTIONS) {                         /* nobody built our selection yet: B5 (the CSS lock-in) may have filled fields, keep them */
        s3_u8 keep[S7_SEL_LEN];
        for (i = 0; i < S7_SEL_LEN; i++) keep[i] = c->lsel[i];
        build_sel(c, sel & 0xFFFF0000u);
        if (keep[3]) { c->lsel[1] = keep[1]; c->lsel[2] = keep[2]; c->lsel[3] = 1; }
        if (keep[7]) { c->lsel[5] = keep[5]; c->lsel[6] = keep[6]; c->lsel[7] = 1; }
        c->lsel[12] = keep[12]; c->lsel[13] = keep[13];
        if (m->char_color && (m->char_color >> 8) < 26u) { c->lsel[1] = (s3_u8)(m->char_color >> 8); c->lsel[2] = (s3_u8)m->char_color; c->lsel[3] = 1; }   /* the ticket's character: never the Random placeholder 26 (= Master Hand): that locked a player in without a choice */
#ifdef OD_SELECT_LOCK
        if (!keep[3] && !m->char_color) c->lsel[3] = 0;           /* OD: not selected until B5 */
        if (!keep[7]) c->lsel[7] = 0;
#endif
    } else if (m->char_color && (m->char_color >> 8) < 26u) { c->lsel[1] = (s3_u8)(m->char_color >> 8); c->lsel[2] = (s3_u8)m->char_color; c->lsel[3] = 1; }   /* otherwise keep what B5 locked in */
    c->lsel[4] = (s3_u8)c->side;
    if (c->fixed_rules) {                                         /* Unranked: stageSelectOption 3 = random from the ticket's list; Direct keeps the player's pick */
        s3_u16 stg = slippi_random_stage(c);
        c->lsel[5] = (s3_u8)(stg >> 8); c->lsel[6] = (s3_u8)stg; c->lsel[7] = 1;
    }
    put32(c->lsel + 8, gen_next(c) % 0xFFFFu);                    /* rngOffset = generator % 0xFFFF */
    return 0;
}

/* ---- MATCH SETUP CONFORMANCE  ---------------------------------------------------------------------------------------
   Both sides must build EXACTLY the same match.  Slippi's EXI prepareOnlineMatchState fills a fixed block (its static onlineMatchBlock) and changes only:
   characters / colours / shade / team from the two selections, the stage of the first player (by index) who selected one, the pause bit (allowed only in Direct),
   stock mode 0x32 / score UI 0x4C / 8:00 timer / 4 stocks / 0 % (non-Party; desync-recovery defaults), items off unless the ticket's `items` (frequency high), P3/P4
   absent.  canon_block applies those constants to our template (which differs from Slippi's in P1/P2 byte +5 (0xFF vs 0x00), the P3/P4 filler and the item switch
   bits), so the Slippi route gets Slippi's bytes and our own modes get the same rules from the negotiated settings.  Nothing here reads a local preference. */
s3_u32 s7_setup_digest(const s3_u8 *msrb)
{
    s3_u32 h = 2166136261u, i;
    for (i = 0; i < 0x138u; i++) { h ^= msrb[598 + i]; h *= 16777619u; }
    for (i = 0; i < 4u; i++) { h ^= msrb[5 + i]; h *= 16777619u; }
    h ^= msrb[961]; h *= 16777619u;
    return h;
}
static void canon_block(s3_u8 *mb, s3_u32 items, int pause_off, s3_u32 stocks, s3_u32 time_s)   /* pause_off bit 0 = pause off, bit 1 = S7R_ITEMS_ALL (our route only) */
{
    s3_u32 i;
    mb[0x0] = 0x32; mb[0x3] = 0x4C; mb[0x8] = 0;                 /* stock mode, count-down timer; score UI hidden; not teams */
    mb[0x2] = (pause_off & 1) ? (s3_u8)(mb[0x2] | 0x08u) : (s3_u8)(mb[0x2] & 0xF7u);   /* gameBitField3 bit 3: pause off */
    put32(mb + 0x10, time_s ? time_s : 480u);
    mb[0xB] = items ? 3u : 0xFFu;                                /* item frequency: off, or high with items */
    put32(mb + 0x23, 0xF8000000u | (items >> 4));                /* 0xF80000000F000000 | (u64)items << 28 */
    put32(mb + 0x27, 0x0F000000u | (items << 28));
    if (pause_off & 2) { mb[0xB] = 4u; put32(mb + 0x23, 0xFFFFFFFFu); put32(mb + 0x27, 0xFF000000u); }   /* Wild And Crazy: Very High, every item of Slippi's menu on (0xFFFFFFFFFF000000 = canon with items 0xFFFFFFFF; the low 24 bits are no menu items and have no loaded article: a spawn drawn from them crashes in Item_80267AA8) */
    for (i = 0; i < 4u; i++) {
        mb[0x62 + i * 0x24] = (s3_u8)(stocks ? stocks : 4u);
        mb[0x70 + i * 0x24] = 0; mb[0x71 + i * 0x24] = 0;         /* current health 0 % */
        mb[0x65 + i * 0x24] = i < 2u ? (s3_u8)i : 0xFFu;              /* Slippi: P1 0, P2 1 (our offline-VS template: 0xFF), P3 / P4 0xFF */
    }
    mb[0x61 + 2 * 0x24] = 3; mb[0x61 + 3 * 0x24] = 3;             /* 1v1: P3 / P4 absent */
    mb[0x60 + 2 * 0x24] = 0x15; mb[0x60 + 3 * 0x24] = 0x15;       /* Slippi's P3 / P4 filler (absent players) */
    mb[0x6C + 2 * 0x24] = 0xC0; mb[0x6C + 3 * 0x24] = 0xC0;
}
int s7_core_set_rules(struct s3_core *c, const struct s7_rules *r)
{
    s3_u32 i;
    if (!r) { c->rules_have = 0; return 0; }
    if (r->n_stages > 16u || r->alt_mode > S7R_ALT_ON || r->stocks > 99u) return -1;
    c->rules = *r;
    for (i = r->n_stages; i < 16u; i++) c->rules.stages[i] = 0;
    c->rules_have = 1;
    return 0;
}
static int rules_stage_ok(const struct s3_core *c, s3_u32 st)
{
    s3_u32 i;
    if (!c->rules.n_stages) return 1;
    for (i = 0; i < c->rules.n_stages; i++) if (c->rules.stages[i] == st) return 1;
    return 0;
}
static void setup_refuse(s3_u8 *msrb, int why)
{
    const char *t = why == 2 ? "Stage not allowed here. Press B." : "Settings differ. Press B.";
    s3_u32 i;
    msrb[0] = 5;                                                 /* ERROR_ENCOUNTERED: the CSS shows the text, B leaves */
    msrb[1] = 0; msrb[2] = 0;
    for (i = 0; t[i] && i < 240u; i++) msrb[357 + i] = (s3_u8)t[i];
    msrb[357 + i] = 0;
}
/* the canonical setup digest of this B3 block; on our route with a peer that runs the check: not "remote ready" until the two digests agree, refused with a
   message when they still disagree (or the peer's never came) S7_SETUP_WAIT_MS after both locked in.  The Slippi route only records our digest. */
static void setup_check(struct s3_core *c, s3_u8 *msrb)
{
    s3_u32 d = s7_setup_digest(msrb), bits = S7SU_LOCAL;
    int both = c->lsel[3] && c->rsel_have && c->rsel[3], same = c->dg_peer_have && c->dg_peer == d;
    c->dg = d;
    if (s7_core_is_slippi(c)) bits |= S7SU_SLIPPI;
    else if (c->rules_have) {
        bits |= S7SU_ON;
        if (both && d != c->dg_sent) c->dg_want = 1;             /* sent by the next s3_core_tick */
        if (c->peer_cap && !c->setup_bad) {
            bits |= S7SU_CAP;
            if (both && !same) {
                s3_u32 now = c->clock_ms(c->ctx), key = d ^ (c->dg_peer_have ? c->dg_peer : 0x5E7D0000u);
                if (!c->dg_t || key != c->dg_key) { c->dg_t = now | 1u; c->dg_key = key; }
                else if (now - c->dg_t >= S7_SETUP_WAIT_MS) { c->setup_bad = 1; s7_setup_info[3]++; }
                msrb[2] = 0;                                     /* not ready until the peer's digest matches ours */
            } else c->dg_t = 0;
        }
        if (c->setup_bad) setup_refuse(msrb, c->setup_bad);
    }
    if (c->peer_cap) bits |= S7SU_CAP;
    if (c->dg_peer_have) bits |= S7SU_PEER;
    if (same) bits |= S7SU_MATCH;
    if (c->setup_bad) bits |= S7SU_BAD;
    s7_setup_info[0] = d; s7_setup_info[1] = c->dg_peer_have ? c->dg_peer : 0; s7_setup_info[2] = bits;
}

/* S7: B3 match block from both MATCH_SELECTIONS, the way EXI prepareOnlineMatchState fills it for a 1v1: char/color
   per player index (0x60/0x63 + idx * 0x24), color-collision counter 0x67, team 0, the first player (by index) whose
   stage is selected gives the stage (0x0E), rngOffset of the decider (index 0).  Stocks/time limit stay local lab
   knobs (cfg; Slippi uses fixed values).  Without the remote selections (or with S7F_NO_RSEL on side B, a negative
   test) the block is the template and b3_nosel counts it. */
/* S19b: MSRB_ALT_STAGE_MODE (Online.s: MSRB_MATCH_ID + 51 = 961) carries the stage chooser's altStageMode into the match:
   InitOnlinePlay stores it into IngameCheckIfFrozen's toggle byte (1 = frozen Pokemon Stadium).  It was never written, so every online
   Stadium match ran unfrozen whatever the players chose.  S7F_FROZEN_PS (lab knob in s7_flags) sets our own selection's altStageMode. */
#define S7_MSRB_ALT_STAGE 961
/* FROZEN FD: 1 while the block's stage was derived by the core because nobody picked one (Random); read by the glue at match start (fd_step) */
s3_u8 s7_stage_rand;
int s3_core_b3(struct s3_core *c, s3_u8 *msrb)
{
    s7_stage_rand = 0;
    msrb[S7_MSRB_ALT_STAGE] = (c->xcfg->s7_flags & S7F_FROZEN_PS) ? 1u : 0u;   /* S19b: no selections yet -> the lab knob */
    s3_u32 i, rng;
    const s3_u8 *sp[2];
    s3_u8 *mb = msrb + 598;
    ST->b3_calls++;
    if (c->side < 0) return -1;                                  /* shim null reply (state 0 idle) */
    msrb[0] = 4;                                                 /* MM_STATE_CONNECTION_SUCCESS (start gate = B0 frame 1) */
#ifdef OD_SELECT_LOCK
    msrb[1] = c->lsel[3] ? 1 : 0;                                /* OD: ready = locked in (B5 seen) / the peer's selections say so */
    msrb[2] = (c->rsel_have && c->rsel[3]) ? 1 : 0;
#else
    msrb[1] = 1;
    msrb[2] = 1;
#endif
    msrb[3] = (s3_u8)c->side;
    msrb[4] = (s3_u8)(1 - c->side);
    msrb[9] = (s3_u8)(c->delay_hook ? c->delay_hook(c->ctx, c->cfg->delay) : c->cfg->delay);   /* S18 pacing: the hook changes it only here, at match setup */
    str(msrb + 15, c->side ? "SIDE B" : "SIDE A");
    str(msrb + 46, "SIDE A"); str(msrb + 77, "SIDE B");
    str(msrb + 170, c->side ? "SIDE A" : "SIDE B");
    str(msrb + 201, "SDA"); msrb[204] = 0x81; msrb[205] = 0x94; msrb[206] = '1';
    str(msrb + 211, "SDB"); msrb[214] = 0x81; msrb[215] = 0x94; msrb[216] = '2';
    for (i = 0; i < 78; i++) put32(mb + 4 * i, s3_match_struct[i]);
    if (c->cfg->stage) { mb[0x0E] = (s3_u8)(c->cfg->stage >> 8); mb[0x0F] = (s3_u8)c->cfg->stage; }
    if (s7_core_is_slippi(c)) { mb[0x0E] = 0; mb[0x0F] = 0x1F; }  /* D10: Slippi's default when nobody selected a stage: Battlefield */
    rng = c->cfg->rng_offset;
    if (c->state == S7_LOOPBACK) {                               /* reference: the peer's selections are the cfg's */
        static s3_u8 fake[S7_SEL_LEN];
        s3_u32 s = c->xcfg->sel[1];
        fake[1] = (s3_u8)(s >> 24); fake[2] = (s3_u8)(s >> 16); fake[3] = 1; fake[4] = 1;
        fake[5] = (s3_u8)(s >> 8); fake[6] = (s3_u8)s; fake[7] = 1;
        copy(c->rsel, fake, S7_SEL_LEN); c->rsel_have = 1;
    }
    if (c->rsel_have && !((c->xcfg->s7_flags & S7F_NO_RSEL) && c->side == 1)) {
        sp[c->side] = c->lsel; sp[1 - c->side] = c->rsel;
        for (i = 0; i < 2; i++) {
            mb[0x60 + i * 0x24] = sp[i][1];
            mb[0x63 + i * 0x24] = sp[i][2];
            mb[0x67 + i * 0x24] = 0;
            mb[0x69 + i * 0x24] = 0;
        }
        /* colour collision (Slippi: same char (Sheik counted as Zelda) and colour -> 0x67 = 0, 1, ...) */
        {
            s3_u8 a = mb[0x60] == 0x13 ? 0x12 : mb[0x60], b = mb[0x84] == 0x13 ? 0x12 : mb[0x84];
            if (a == b && mb[0x63] == mb[0x87]) mb[0x67 + 0x24] = 1;
        }
        for (i = 0; i < 2; i++) if (sp[i][7]) { mb[0x0E] = sp[i][5]; mb[0x0F] = sp[i][6]; msrb[S7_MSRB_ALT_STAGE] = sp[i][13]; break; }   /* S19b: + the stage chooser's altStageMode */
        if (c->fixed_rules) msrb[S7_MSRB_ALT_STAGE] = 0;
        if (!s7_core_is_slippi(c) && c->rules_have && c->rules.alt_mode) msrb[S7_MSRB_ALT_STAGE] = c->rules.alt_mode == S7R_ALT_ON ? 1u : 0u;   /* MATCH SETUP: the server's Stadium policy */           /* Slippi prepareOnlineMatchState: altStageMode only in Direct/Teams, forced 0 otherwise (a stranger's frozen-Stadium toggle travels in its Unranked selections: ) */
        rng = get32(sp[0] + 8);                                  /* decider = player index 0 */
#ifdef OD_SELECT_LOCK
        if (!sp[0][7] && !sp[1][7] && !s7_core_is_slippi(c)) {   /* OD: nobody picked (Slippi's "random stage"): both sides derive the same one from the decider's rng */
            static const s3_u8 STG[6] = { 0x02, 0x03, 0x08, 0x1C, 0x1F, 0x20 };   /* Fountain, Stadium, Yoshi's, Dream Land, Battlefield, FD (the server's pool) */
            mb[0x0E] = 0; mb[0x0F] = STG[rng % 6u];
            s7_stage_rand = 1;
            if (c->rules_have && c->rules.n_stages) { s3_u16 st = c->rules.stages[rng % c->rules.n_stages]; mb[0x0E] = (s3_u8)(st >> 8); mb[0x0F] = (s3_u8)st; }   /* MATCH SETUP: the server's pool */
        }
#endif
        if (c->fixed_rules && (mb[0x60] >= 26 || mb[0x84] >= 26 || !stage_allowed(c, ((s3_u32)mb[0x0E] << 8) | mb[0x0F]))) {
            c->disconnected = 1;                                 /* D10: Slippi cleans the lobby up (character >= 26 or a stage outside the list) */
            ST->resv[0]++;                                       /* status word 60: fixed-rules refusals */
        }
        if (!s7_core_is_slippi(c) && c->rules_have && c->lsel[3] && c->rsel[3] && !rules_stage_ok(c, ((s3_u32)mb[0x0E] << 8) | mb[0x0F]) && !c->setup_bad) {
            c->setup_bad = 2; s7_setup_info[3]++;                 /* MATCH SETUP: a stage outside the negotiated list (Slippi's fixed-rules rule, for our modes) */
        }
    } else XS->b3_nosel++;
#ifdef OD_SELECT_LOCK
    /* (a Mano a mano pairing started while one console had not locked a character: the opponent showed MASTER HAND and the Wii crashed):
       a block never carries a fighter id the game cannot play (>= 26: Master Hand, Crazy Hand, the character screen's non-fighter tiles; an unlocked
       selection's character byte is whatever the cursor / the config held), and a side that has not locked a character is never ready, so the match
       cannot start from a half-made selection.  The id is replaced by the cfg's default fighter so nothing downstream loads it. */
    for (i = 0; i < 2; i++) {
        s3_u8 *ch = mb + 0x60 + i * 0x24;
        if (*ch >= 26) { *ch = (s3_u8)(i ? 0x14 : 0x02); msrb[1] = 0; msrb[2] = 0; ST->resv[1]++;     /* status word 61: blocks with a non-fighter character */ }
    }
    if (!s7_core_is_slippi(c) && (!c->lsel[3] || !(c->rsel_have && c->rsel[3]))) { msrb[1] = (s3_u8)(c->lsel[3] ? 1 : 0); msrb[2] = (s3_u8)((c->rsel_have && c->rsel[3]) ? 1 : 0); }
#endif
    put32(msrb + 5, rng);
#ifdef MS_LAB_ITEMS_ALL
    if (s7_core_is_slippi(c)) canon_block(mb, c->items, c->fixed_rules | S7R_ITEMS_ALL, 0, 0);   /* LAB ONLY (option itemsall): the pair rig's Unranked match is the Slippi route; the item soak forces items there too */
#else
    if (s7_core_is_slippi(c)) canon_block(mb, c->items, c->fixed_rules, 0, 0);   /* MATCH SETUP: Slippi's constants; pause allowed only in Direct */
#endif
    else if (c->rules_have) canon_block(mb, c->rules.items, (int)(c->rules.flags & (S7R_PAUSE_OFF | S7R_ITEMS_ALL)), c->rules.stocks, c->rules.time_s);   /* our modes: the negotiated settings */
    else {                                                        /* lab peers only (patched table, loopback): the cfg knobs */
        if (c->cfg->time_limit) put32(mb + 0x10, c->cfg->time_limit);
        if (c->cfg->stocks) { mb[0x62] = (s3_u8)c->cfg->stocks; mb[0x62 + 0x24] = (s3_u8)c->cfg->stocks; }
    }
    XS->b3_last = ((s3_u32)mb[0x60] << 24) | ((s3_u32)mb[0x84] << 16) | ((s3_u32)mb[0x0E] << 8) | mb[0x0F];
    XS->b3_rng = rng;
    str(msrb + 910, "mode.direct-sds7");
    setup_check(c, msrb);
    return 0;
}

/* S7: B5 pstb[10]: teamId, charId, charColor, isCharSel, stageId u16, stageSelectOption, onlineMode, altStageMode
   (EXI setMatchSelections): merge into our MATCH_SELECTIONS and send it (Slippi SetMatchSelections).  Not reached in
   the boot-to-match lab flow (no online CSS modules), kept for completeness. */
#ifdef OD_SELECT_LOCK
/* OD: pstb[0] is the command byte (the shim hands over the whole request): Online.s PSTB_* offsets (team 1, char 2, colour 3, char opt 4,
   stage u16 5-6, stage opt 7, online mode 8, alt stage 9).  Option 0 unset, 1 merge, 2 clear, 3 random (stage only).  The original body below
   read the fields one byte early; it was never reached in the boot-to-match lab flow (no online CSS). */
int s7_core_b5(struct s3_core *c, const s3_u8 *pstb)
{
    if (pstb[4] == 1 && pstb[2] < 26u) { c->lsel[1] = pstb[2]; c->lsel[2] = pstb[3]; c->lsel[3] = 1; }   /* a non-fighter id (>= 26) never locks the player in */
    else if (pstb[4] == 2) c->lsel[3] = 0;
    if (pstb[7] == 1) { c->lsel[5] = pstb[5]; c->lsel[6] = pstb[6]; c->lsel[7] = 1; }
    else if (pstb[7] == 3 && s7_core_is_slippi(c)) {                /* D10: random = a draw from the allowed list, selected (a later B5 must not un-select it: we may be the decider) */
        s3_u16 stg = slippi_random_stage(c);
        c->lsel[5] = (s3_u8)(stg >> 8); c->lsel[6] = (s3_u8)stg; c->lsel[7] = 1;
    }
    else if (pstb[7] == 2 || pstb[7] == 3) c->lsel[7] = 0;
    if (s7_core_is_slippi(c)) put32(c->lsel + 8, gen_next(c) % 0xFFFFu);   /* setMatchSelections: a new rngOffset every time */
    if (pstb[4] == 1) c->lsel[12] = pstb[1];                      /* Slippi Merge: teamId with a character selection, */
    if (pstb[7] == 1 || pstb[7] == 3) c->lsel[13] = pstb[9];      /* altStageMode with a stage selection */
    if (c->state == S3_SES_RUN) send_sel(c, c->active);
    return 0;
}
#else
int s7_core_b5(struct s3_core *c, const s3_u8 *pstb)
{
    if (pstb[3]) { c->lsel[1] = pstb[1]; c->lsel[2] = pstb[2]; c->lsel[3] = 1; }
    if (pstb[6] == 1 || pstb[6] == 3) { c->lsel[5] = pstb[4]; c->lsel[6] = pstb[5]; c->lsel[7] = 1; }
    if (pstb[6] == 3 && s7_core_is_slippi(c)) { s3_u16 stg = slippi_random_stage(c); c->lsel[5] = (s3_u8)(stg >> 8); c->lsel[6] = (s3_u8)stg; }   /* D10: random = a draw from the allowed list */
    if (s7_core_is_slippi(c)) put32(c->lsel + 8, gen_next(c) % 0xFFFFu);   /* setMatchSelections: a new rngOffset every time */
    if (pstb[3]) c->lsel[12] = pstb[0];                           /* Slippi Merge: teamId with a character selection, */
    if (pstb[6] == 1 || pstb[6] == 3) c->lsel[13] = pstb[8];      /* altStageMode with a stage selection */
    if (c->state == S3_SES_RUN) send_sel(c, c->active);
    return 0;
}
#endif
