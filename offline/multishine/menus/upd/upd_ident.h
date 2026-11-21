/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* upd_ident: the console side of the ONE identity string (section Identity; Python twin: the server's ).
 *   canonical: "Multishine/<build> (Slippi-compat <x.y.z>; contact <contact>)"       About page: "Multishine build N" / "matches Slippi 3.7.0"
 * Header-only, pure C89, no libc.  UPDID_SLIPPI_COMPAT must equal SBRA_APP_VERSION (offline/bridge/sbr_app.h): the server's tests/test_ident.py checks it.
 * The create-ticket sent to Slippi has no free-text field and its appVersion is validated by THEIR server, so nothing here is ever put in the ticket. */
#ifndef UPD_IDENT_H
#define UPD_IDENT_H
#define UPDID_SLIPPI_COMPAT "3.7.0"
#define UPDID_CONTACT_PLACEHOLDER "multishine.link"

static char *updid_s(char *d, char *end, const char *s) { while (*s && d < end) *d++ = *s++; return d; }
static char *updid_u(char *d, char *end, unsigned v)
{
    char t[10];
    unsigned n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n && d < end) *d++ = t[--n];
    return d;
}
/* contact characters allowed: the same set as ms_ident._clean (A-Z a-z 0-9 . _ : / @ # + -), at most 48; empty -> the placeholder */
static __attribute__((unused)) char *updid_contact(char *d, char *end, const char *c)
{
    unsigned n = 0;
    char *d0 = d;
    for (; c && *c && n < 48u && d < end; c++) {
        char ch = *c;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == ':' || ch == '/' || ch == '@' || ch == '#' || ch == '+' || ch == '-') { *d++ = ch; n++; }
    }
    return d == d0 ? updid_s(d, end, UPDID_CONTACT_PLACEHOLDER) : d;
}
/* the canonical string into out[cap], NUL terminated, truncated if cap is too small; build 0 = "dev".  Returns the length. */
static __attribute__((unused)) unsigned updid_identity(char *out, unsigned cap, unsigned build, const char *contact)
{
    char *d = out, *end;
    if (cap < 2u) { if (cap) out[0] = 0; return 0; }
    end = out + cap - 1;
    d = updid_s(d, end, "Multishine/");
    if (build) d = updid_u(d, end, build); else d = updid_s(d, end, "dev");
    d = updid_s(d, end, " (Slippi-compat " UPDID_SLIPPI_COMPAT "; contact ");
    d = updid_contact(d, end, contact);
    d = updid_s(d, end, ")");
    *d = 0;
    return (unsigned)(d - out);
}
#endif
