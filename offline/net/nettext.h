/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NETTEXT_H
#define NETTEXT_H

/*
 * nettext: the verdict and the words of Settings > Network status > Test connection ( and 7).
 * Pure, strict C89, no libc, no heap.  It only needs the result struct of nettest.h and links without nettest.c (ntx_unpack reads
 * the word nt_pack made).  Result lines are at most NTX_LINE_MAX - 1 = 35 characters, menu-font glyphs only.
 */

#include "nettest.h"

#define NTX_LINE_MAX 36             /* 35 characters + NUL; the lines must also fit 484 px (nt_geom_test) */
#define NTX_RESULT_LINES 5          /* router, internet, lost, verdict (two lines) */
#define NTX_NOTICE_LINES 4
#define NTX_NOTICE_MAX 48           /* longest notice line, characters */

/* ---- thresholds (our numbers, milliseconds; the result fields are tenths of a millisecond) ---- */
#define NTX_ROUTER_SPREAD_MS 25u    /* router spread above this: Wi-Fi looks unstable */
#define NTX_LOSS_PERCENT 10u        /* loss at or above this: unstable (router) / loses packets (internet) */
#define NTX_INET_SLOW_MS 60u        /* internet median above this: slow */

enum ntx_verdict {
    NTX_GOOD = 0, NTX_UNSTABLE, NTX_SLOW, NTX_LOSSY, NTX_NOROUTER, NTX_NOINET, NTX_NOTHING
};

nt_u32 ntx_decode(nt_u32 code);                                  /* 10-bit code of nt_code back to tenths of a ms */
void ntx_unpack(nt_u32 w, nt_leg_result *r);                        /* the inverse of nt_pack */
int ntx_verdict(const nt_leg_result *router, const nt_leg_result *inet);
const char *ntx_word(int verdict);                                  /* the short word on the row ("Good", "Unstable", ...) */
/* the five result lines: 0 router, 1 internet, 2 lost, 3 and 4 the verdict (4 may be ""); every line fits NTX_LINE_MAX */
void ntx_lines(const nt_leg_result *router, const nt_leg_result *inet, char out[NTX_RESULT_LINES][NTX_LINE_MAX]);
const char *ntx_notice(unsigned i);                                 /* the privacy notice shown before and during the test, i 0..3 */
/* "Testing n of 21" into out (at least 20 bytes); returns the length */
unsigned ntx_progress(char *out, unsigned n);

#endif
