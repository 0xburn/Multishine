/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* hub_ovl: the OVERLAY's own glue (hub_ovl.h).  The overlay may not import a resident symbol (the packer refuses import relocations), so every resident service the
 * overlay's code calls by name (on_card.c, codehist.c, mw2_crypto.c, sddns.c, s12_gc.c) is defined here as a one-line forward through struct hub_env.  The sources
 * that move (od_glue.c, on_backend.c with -DHUB_OVL, mnc.c, mw2.c, ms_sha256.c) are compiled unchanged against these names.  Plus the two libc routines the compiler
 * may emit calls to.  Strict C99, no libc, no state except the env pointer (zero at every load; set by hub_enter). */
#include "hub_ovl.h"
#include "on_card.h"
#include "mw2_crypto.h"
#include "sddns.h"

const struct hub_env *hub_E;

unsigned int od_clock_ms(void) { return hub_E->clock_ms(); }
int od_core_final(unsigned int *frame, unsigned int *cks) { return hub_E->core_final(frame, cks); }
void od_core_stats(unsigned int *w) { unsigned int k; if (hub_E->gs_magic == HUB_GS_MAGIC && hub_E->core_stats) hub_E->core_stats(w); else for (k = 0; k < 12u; k++) w[k] = 0; }
int onc_on_status(struct od_cfg *cfg) { return hub_E->onc_on_status(cfg); }
int onc_final(void) { return hub_E->onc_final(); }
unsigned int onc_backup(void) { return hub_E->onc_backup(); }
int onc_save_ident(const struct od_cfg *c) { return hub_E->onc_save_ident(c); }
void omc_on_status(void) { hub_E->omc_on_status(); }
void omc_on_code(const char *code) { hub_E->omc_on_code(code); }
int omc_answer(const unsigned char *req, unsigned char *rsp) { return hub_E->omc_answer(req, rsp); }
unsigned int ch_from_melee(const unsigned char *b, unsigned int len, char *out) { return hub_E->ch_from_melee(b, len, out); }

void mw2_sha512(const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[64]) { hub_E->sha512(msg, n, out); }
void mw2_hmac(const mw2_u8 key[32], const mw2_u8 *msg, mw2_u32 n, mw2_u8 out[32]) { hub_E->hmac(key, msg, n, out); }
int mw2_ed25519_verify(const mw2_u8 pub[32], const mw2_u8 *msg, mw2_u32 n, const mw2_u8 sig[64]) { return hub_E->ed_verify(pub, msg, n, sig); }
int mw2_ct_equal(const mw2_u8 *a, const mw2_u8 *b, mw2_u32 n) { return hub_E->ct_equal(a, b, n); }

void sddns_set_servers(sddns *d, sddns_u32 primary, sddns_u32 secondary) { hub_E->dns_set_servers(d, primary, secondary); }
void sddns_cache_flush(sddns *d) { hub_E->dns_cache_flush(d); }
int sddns_start(sddns *d, const char *host, sddns_u32 now) { return hub_E->dns_start(d, host, now); }
int sddns_poll(sddns *d, sddns_u32 now) { return hub_E->dns_poll(d, now); }
void sddns_abort(sddns *d) { hub_E->dns_abort(d); }
int sddns_parse_dotted(const char *s, sddns_u32 *ip) { return hub_E->dns_parse_dotted(s, ip); }
int sddns_normalize_host(const char *host, char *out) { return hub_E->dns_normalize_host(host, out); }

void *memcpy(void *dst, const void *src, __SIZE_TYPE__ n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *dst, int c, __SIZE_TYPE__ n)
{
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}
