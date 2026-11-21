/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* : relaxed main-stick compare glue (S17 flag RELAX 0x4000). Original work. Rule and exactness argument:
   ../relax/relax_rule.h,.  Called (1) by the stick-compare stub s17_relax_stub (s12_hooks.S), which the build
   patches over the `cmpw r3, r4` of TriggerSendInput.asm's main/C-stick word compare, and (2) by the early check (s12_early.c inputs_match). */
#ifndef S17_RELAX_H
#define S17_RELAX_H
extern unsigned s17_relax_on;
/* SLIPPI INTEROP D11 (the Slippi interop rules): the relaxed compare is a local rollback decision, not wire; against a real Slippi client it must be OFF so that the
   exact compare (and therefore the rollback pattern) is Slippi's own. Pure decision, used by the glue at match start and by the host test.
   flags = s11_cfg.s17_flags (bit 0x4000 RELAX, 0x10000 only side A, 0x20000 only side B), side = 0/1, slippi = the peer is a real Slippi client. */
static inline int s17_relax_decide(unsigned flags, int side, int slippi)
{
    if (slippi) return 0;
    return (flags & 0x4000u) != 0 && (!(flags & (0x10000u | 0x20000u)) || ((flags & 0x10000u) && side == 0) || ((flags & 0x20000u) && side == 1));
}
extern unsigned s17_relax_neg;                                    /* TEST ONLY negative controls (s17_flags 0x40000 / 0x80000) */                                     /* the stub does nothing but the plain compare while this is 0 */
/* the stick word (bytes 2..5) of the real and the predicted pad differ: 1 = the game cannot tell the difference (the caller treats them as equal),
   0 = strict (roll back).  odb / rxb = Online data buffer and receive buffer, count = remote player index, off = RXB index of the frame (latest - frame) */
int s17_relax_c(const unsigned char *real, const unsigned char *pred, unsigned odb, unsigned rxb, unsigned count, unsigned off);
int s17_relax_core(const unsigned char *real, const unsigned char *pred, unsigned odb, unsigned rxb, unsigned count, unsigned off, int frame, unsigned rd);   /* early check: frame / read index are its local ones */
void s17_relax_restore(int frame);                                /* after a restore to the state at the start of `frame` (patch #2, drop entries >= frame) */
void s17_relax_reset(void);                                       /* match start */
enum { RXT_CALLS, RXT_ACC, RXT_DEN_FIELD, RXT_DEN_DZ, RXT_DEN_NOCTX, RXT_DEN_G, RXT_DEN_PORT, RXT_DEN_P1, RXT_P1, RXT_P2, RXT_P2_SKIP, RXT_DEN_BUSY, RXT_N };
extern volatile unsigned s17_rx_tel[RXT_N];
#endif
