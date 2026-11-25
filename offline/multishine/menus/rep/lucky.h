/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LUCKY_H
#define LUCKY_H
/* LOCAL FORK: the Lucky Stats report key, card file "MultishineLucky" (written by tools/lucky/lucky_provision.py).
   Payload at file offset 0x1000: "LKYS" | u8 version 1 | 3 x 0 | key[72] (NUL padded, "l7u_" + 64 hex) | u32 CRC-32 of everything before it. */
#define LUCKY_FILE "MultishineLucky"
#define LUCKY_FILE_OFF 0x1000u
#define LUCKY_KEY_CAP 72u
#define LUCKY_KEY_LEN 68u
#define LUCKY_BODY (8u + LUCKY_KEY_CAP)
#define LUCKY_LEN (LUCKY_BODY + 4u)
/* 1 = valid, key copied (NUL terminated, cap >= LUCKY_KEY_CAP); 0 = refused */
int lucky_parse(const unsigned char *p, unsigned int n, char *key);
#endif
