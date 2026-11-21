/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * s4_script: the deterministic opponent input script shared by the PC test peer (peer/s4_script.py, a line-for-line
 * port; tests/script_vectors.c + run_tests.sh check both produce identical bytes) and the in-image loopback reference
 * (s4_core cfg flag S4F_LOOPBACK).  Pads are Slippi's 8-byte wire pads: PADStatus button word (big-endian: A 0x0100,
 * B 0x0200, X 0x0400, R 0x0020), main stick X/Y and C-stick X/Y as signed bytes, analog L, analog R.  Never START or
 * the D-pad.  Inputs change every 6 frames; tag < 1 gives the neutral (all zero) pad.
 */
#ifndef S4_SCRIPT_H
#define S4_SCRIPT_H
unsigned int s4_mix32(unsigned int x);
void s4_script_pad(unsigned int seed, int tag, unsigned char out[8]);
#endif
