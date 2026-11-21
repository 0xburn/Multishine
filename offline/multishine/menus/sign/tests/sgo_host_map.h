/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test mapping of SGO_P2A / SGO_A2P: a 64-bit host cannot hold pointers in 32 bits, so the test's arena is mapped at 0x10000000. */
#ifndef SGO_HOST_MAP_H
#define SGO_HOST_MAP_H
extern unsigned char *sgo_host_arena;
#define SGO_P2A(p) ((unsigned int)(0x10000000u + (unsigned int)((const unsigned char *)(p) - sgo_host_arena)))
#define SGO_A2P(a) (sgo_host_arena + ((a) - 0x10000000u))
#endif
