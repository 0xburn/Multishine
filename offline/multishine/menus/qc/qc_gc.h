/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * qc_gc: Slippi-style D-pad quick chat on the online character screen of the card-route engine.  Original work, a port of
 * (the S14 line's lab image, proven on the console) with the lab modes taken out and the glue of THIS engine:
 *
 *   rules, wire format, window, subtitle queue   offline/chat/chat_core.c, chat_link.c (used unchanged, in place; host tested)
 *   drawing, sounds, pad edges                   this file (Melee's own SisLib text, soft-float; the two float-taking Melee calls go through bh_sis_add of on_bhold_sis.S)
 *   transport                                    the engine's ENet session with the opponent (s7_core.c): 0x84 [s32 id][u8 sender] reliable on channel 0, exactly
 *                                                Slippi's NP_MSG_SLIPPI_CHAT_MESSAGE, so a real Slippi peer (Unranked / Direct through the bridge) and one of ours speak
 *                                                the same bytes
 *
 * Compiled only with -DMS_QCHAT=1 (MANIFEST option qchat); without it this file is empty and the image is byte for byte the one without the option.
 *
 * Calls (all from the game thread, outside the network lock):
 *   s12_gc.c s3_tick_body  -> qc_session(...) when the ENet session comes up / goes away, then qc_iter every engine pass
 *   s7_core.c e_receive    -> qc_rx(d, len)        an ENet payload that starts with 0x84 (any channel, as Slippi's OnData)
 *   qc_iter                -> qc_tx_hook(d, 6)     (s12_gc.c -> s7_core_chat_tx): the six bytes wait in the core and go out with its next network pass
 *   on_bhold.c bh_tick     -> qc_win_open        while the chat window is open B closes it (the hold-B exit stays quiet)
 */
#ifndef QC_GC_H
#define QC_GC_H

#include "../../../chat/chat_link.h"

/* the status words (RAM proof, qc_status_addr): the same indices the lab image used, so style readers still apply */
enum {
    QS_MAGIC, QS_ITERS, QS_CSS_ITERS, QS_PORT, QS_PRESSED, QS_UI_OPENS, QS_SENT, QS_TX_OK, QS_TX_FAIL, QS_RX_OK, QS_RX_BAD,
    QS_RX_OVERWRITTEN, QS_REPLIES, QS_SHOWN, QS_DROP_FULL, QS_DROP_CAP, QS_DROP_INVALID, QS_EXPIRED, QS_ROWS,
    QS_TEXT_NEW, QS_TEXT_FREE, QS_TEXT_SKIP, QS_SIS_FREE_MIN, QS_LAST_TX_ID, QS_LAST_RX_ID,
    QS_LAST_SHOWN, QS_SESSION, QS_ENABLED, QS_WINDOW, QS_DEMO_STEP, QS_SCENE_RESETS,
    QS_ROW_ID0, QS_ROW_ID4 = QS_ROW_ID0 + 4, QS_NAME_ENTRY, QS_B3,
    QS_CANARY = 63
};
struct qc_status { volatile cc_u32 w[64]; };

void qc_iter(void);
void qc_session(int up, int local_idx, int remote_idx, const char *own_name, const char *opp_name, int direct);
void qc_rx(const cc_u8 *d, cc_u32 len);
int qc_win_open(void);
/* implemented by the glue that owns the netplay core (s12_gc.c): queue 6 bytes for the opponent (reliable, channel 0); 0 = queued */
int qc_tx_hook(const cc_u8 *d, cc_u32 len);
/* the character screen was left: a message that still waits for the connection is dropped (s12_gc.c -> s7_core_chat_cancel) */
void qc_tx_cancel_hook(void);

#endif
