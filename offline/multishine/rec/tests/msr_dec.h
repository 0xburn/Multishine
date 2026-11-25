/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host-only MSR1 decoder (tests): returns the raw byte count, 0 on any error. */
unsigned msr_decode(const unsigned char *in, unsigned n, unsigned char *out, unsigned cap, unsigned *frames);
