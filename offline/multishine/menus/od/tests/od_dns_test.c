/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the hub's server NAME resolution: od_glue.c + sddns.c behind a scripted in-memory DNS network and a virtual
 * clock.  Cases: name rules, the numeric address wins, resolution at menu time through the DHCP-provided resolver, the opponent rule uses the resolved address,
 * cache for the session, re-resolve after a failure and after a "server did not answer", the failure box text (code / tx / rx / ignored / malformed / resolver),
 * no resolver, no DNS in the build, cancel, a replaced lookup, and that nothing is resolved in a match (phase after the ticket).  No sockets, no engine. */
#include "../od_glue.h"
#include "sddns.h"
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define IP4(a, b, c, d) (((unsigned)(a) << 24) | ((unsigned)(b) << 16) | ((unsigned)(c) << 8) | (unsigned)(d))

/* ---- scripted network: one resolver, replies queued by the script ---- */
enum { R_NONE = 0, R_ANSWER, R_NXDOMAIN, R_GARBAGE, R_WRONGTXID };
static int g_mode = R_ANSWER;               /* what the "resolver" does with the next query */
static unsigned g_answer = IP4(203, 0, 113, 7);
static unsigned g_queries, g_last_to, g_last_port;
static char g_last_name[SDDNS_HOST_MAX + 1];
static unsigned char g_rep[600];
static int g_rep_len;

static void decode_qname(const unsigned char *q, unsigned n, char *out)
{
    unsigned i = 0, o = 0;
    while (i < n && q[i]) {
        unsigned l = q[i++];
        if (o) out[o++] = '.';
        while (l-- && i < n) out[o++] = (char)q[i++];
    }
    out[o] = 0;
}
static unsigned g_to_pub;                   /* queries that went to 1.1.1.1 */
static int net_send(void *ctx, sddns_u32 ip, sddns_u16 port, const sddns_u8 *p, sddns_u32 len)
{
    unsigned qlen;
    (void)ctx;
    g_queries++; g_last_to = ip; g_last_port = port;
    if (ip == 0x01010101u) g_to_pub++;
    decode_qname(p + 12, len - 12, g_last_name);
    qlen = len - 12;                                        /* question: name + type + class */
    g_rep_len = 0;
    if (g_mode == R_NONE) return 0;
    if (g_mode == R_GARBAGE) { memset(g_rep, 0x55, 20); g_rep_len = 20; return 0; }
    memcpy(g_rep, p, 2);                                    /* id */
    if (g_mode == R_WRONGTXID) { g_rep[0] ^= 0xFF; }
    g_rep[2] = 0x81; g_rep[3] = g_mode == R_NXDOMAIN ? 0x83 : 0x80;
    g_rep[4] = 0; g_rep[5] = 1; g_rep[6] = 0; g_rep[7] = g_mode == R_NXDOMAIN ? 0 : 1; g_rep[8] = g_rep[9] = g_rep[10] = g_rep[11] = 0;
    memcpy(g_rep + 12, p + 12, qlen);
    g_rep_len = 12 + (int)qlen;
    if (g_mode != R_NXDOMAIN) {
        unsigned char *a = g_rep + g_rep_len;
        a[0] = 0xC0; a[1] = 0x0C; a[2] = 0; a[3] = 1; a[4] = 0; a[5] = 1; a[6] = 0; a[7] = 0; a[8] = 1; a[9] = 0x2C; a[10] = 0; a[11] = 4;
        a[12] = (unsigned char)(g_answer >> 24); a[13] = (unsigned char)(g_answer >> 16); a[14] = (unsigned char)(g_answer >> 8); a[15] = (unsigned char)g_answer;
        g_rep_len += 16;
    }
    return 0;
}
static int net_recv(void *ctx, sddns_u8 *buf, sddns_u32 cap, sddns_u32 *ip, sddns_u16 *port)
{
    (void)ctx;
    if (g_rep_len <= 0 || (unsigned)g_rep_len > cap) return 0;
    memcpy(buf, g_rep, (unsigned)g_rep_len);
    *ip = g_last_to; *port = (sddns_u16)g_last_port;
    { int n = g_rep_len; g_rep_len = 0; return n; }
}

/* ---- the hub ---- */
static struct od O;
static struct od_status ST;
static struct od_cfg CFG;
static struct sddns D;
static unsigned g_sent;
static int hub_send(void *ctx, const unsigned char *p, unsigned int len) { (void)ctx; (void)p; g_sent++; return (int)len; }

static const unsigned RESOLVER = IP4(192, 168, 1, 1);
static unsigned T;                               /* virtual ms */

static void setup(const char *host, unsigned server_ip, int with_dns)
{
    sddns_io io;
    int i;
    memset(&CFG, 0, sizeof CFG);
    CFG.magic0 = OD_CFG_MAGIC0; CFG.magic1 = OD_CFG_MAGIC1; CFG.build_id = 0x00010000u; CFG.codeset = 0xC0DE5E7u; CFG.rules = 0x201E5u;
    CFG.flags = OD_CFGF_ID | (host ? OD_CFGF_HOST : 0u);
    CFG.server_ip = server_ip;
    for (i = 0; i < 32; i++) CFG.pin[0][i] = (unsigned char)(i + 1);
    strcpy(CFG.code, "AB#1"); strcpy(CFG.name, "Alice");
    for (i = 0; i < 8; i++) CFG.dev[i] = (unsigned char)(0xA0 + i);          /* an all-zero device id would be a guest */
    if (host) strcpy(CFG.host, host);
    memset(&D, 0, sizeof D);
    io.send = net_send; io.recv = net_recv; io.ctx = 0;
    sddns_init(&D, &io, 1234u);
    D.base_ms = 100u;                           /* tests shorten the timeouts: 100, 200, 400 ms */
    g_mode = R_ANSWER; g_queries = 0; g_sent = 0; g_rep_len = 0; T = 1000u; g_answer = IP4(203, 0, 113, 7);
    CHECK(od_init(&O, &CFG, &ST, hub_send, 0, 7u, T) == 0);
    if (with_dns) od_set_dns(&O, &D, RESOLVER);
    od_net_up(&O, IP4(192, 168, 1, 50), IP4(255, 255, 255, 0));
}
static void ticks(unsigned ms)
{
    unsigned end = T + ms;
    while (T < end) { T += 10u; od_tick(&O, T); }
}

static void test_names(void)
{
    static const char *good[] = { "lab.example.org", "a", "A-b.c-d.example", "x1.y2", "multishine.example.com", "0a.b", 0 };
    static const char *bad[] = { "", ".", "a..b", "-a.b", "a-.b", "a.-b", "a.b-", "a.b.", ".a.b", "a_b.c", "a b.c", "a/b", "a:43150", "ünï.example",
                                 "this-label-is-way-too-long-this-label-is-way-too-long-this-label-is.example", 0 };
    char big[64];
    int i;
    for (i = 0; good[i]; i++) CHECK(od_host_ok(good[i]));
    for (i = 0; bad[i]; i++) { if (od_host_ok(bad[i])) printf("accepted bad name '%s'\n", bad[i]); CHECK(!od_host_ok(bad[i])); }
    memset(big, 'a', 47); big[47] = 0;
    CHECK(od_host_ok(big));                                    /* 47 characters (a 47 character single label is fine: 63 max) */
    big[10] = '.';
    CHECK(od_host_ok(big));
    memset(big, 'a', 48); big[48] = 0;
    CHECK(!od_host_ok(big));                                   /* 48 characters do not fit with the NUL */
    CHECK(!od_host_ok(0));
}

static void test_address_wins(void)
{
    setup("lab.example.org", IP4(10, 0, 0, 9), 1);
    CHECK(od_server_ip(&O) == IP4(10, 0, 0, 9));
    CHECK(O.use_host == 0);
    CHECK(O.m.srv_ip == IP4(10, 0, 0, 9));
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(300);
    CHECK(g_queries == 0);                                     /* no DNS packet at all */
    CHECK(O.phase == OD_FIND && g_sent >= 1);
    CHECK(ST.dns_lookups == 0 && ST.srv_ip == IP4(10, 0, 0, 9));
}

static void test_resolve_and_cache(void)
{
    setup("Lab.Example.org", 0, 1);
    CHECK(O.use_host == 1 && O.cfg_ok == 1);
    CHECK(od_server_ip(&O) == 0);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    CHECK(O.phase == OD_WAIT_NET);
    ticks(20);
    CHECK(g_queries == 1);
    CHECK(g_last_to == RESOLVER && g_last_port == 53);         /* the DHCP-provided resolver, port 53 */
    CHECK(strcmp(g_last_name, "lab.example.org") == 0 || strcmp(g_last_name, "Lab.Example.org") == 0);   /* the wire keeps case or lowers it; both are the same name */
    CHECK(od_server_ip(&O) == g_answer);
    CHECK(O.m.srv_ip == g_answer);                             /* the "opponent must not be the server" rule and the mnc use the resolved address */
    ticks(100);
    CHECK(O.phase == OD_FIND && g_sent >= 1);                  /* the usual search follows */
    CHECK(ST.dns_state == OD_DNS_OK && ST.dns_lookups == 1 && ST.srv_ip == g_answer && ST.dns_rx == 1 && ST.dns_sent == 1);
    /* the opponent rule: a ticket naming the server address as the peer is refused by the mnc (srv_ip) -- the field is the resolved one */
    CHECK(O.m.srv_ip != IP4(10, 0, 0, 9));
    /* second search in the same session: cached, no packet */
    od_cancel(&O, T);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(300);
    CHECK(g_queries == 1);
    CHECK(O.phase == OD_FIND);
}

static void test_failure_boxes(void)
{
    unsigned char msrb[700];
    const char *t;
    /* no answer at all: e7, 3 sends, nothing received */
    setup("lab.example.org", 0, 1);
    g_mode = R_NONE;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(1500);
    CHECK(O.phase == OD_ERR && O.err == OD_E_DNS);
    t = od_err_text(&O);
    CHECK(strcmp(t, "Server name not found (DNS e7 t3 r0 i0 b0 192.168.1.1)") == 0);
    if (strcmp(t, "Server name not found (DNS e7 t3 r0 i0 b0 192.168.1.1)")) printf("text: %s\n", t);
    CHECK(g_sent == 0);                                        /* nothing sent to any server: no fallback address */
    CHECK(od_server_ip(&O) == 0 && O.m.srv_ip == 0);
    CHECK(ST.dns_state == OD_DNS_FAILED && ST.dns_err == 7 && ST.dns_sent == 3 && ST.dns_server == RESOLVER);
    memset(msrb, 0, sizeof msrb);
    CHECK(od_b3(&O, msrb) == 1 && msrb[0] == 5 && strcmp((const char *)msrb + 357, t) == 0);   /* the same text in the CSS error box */
    /* next hub entry re-resolves, and this time works */
    g_mode = R_ANSWER;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(300);
    CHECK(g_queries == 4 && O.srv_ip == g_answer && O.phase == OD_FIND);
    /* NXDOMAIN */
    setup("nosuch.example.org", 0, 1);
    g_mode = R_NXDOMAIN;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(300);
    CHECK(O.err == OD_E_DNS);
    CHECK(strcmp(od_err_text(&O), "Server name not found (DNS e4 t1 r1 i0 b0 192.168.1.1)") == 0);
    /* only malformed answers */
    setup("lab.example.org", 0, 1);
    g_mode = R_GARBAGE;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(1500);
    CHECK(O.err == OD_E_DNS);
    t = od_err_text(&O);
    CHECK(strstr(t, "(DNS e8 t3 r") != 0 && strstr(t, " b3 192.168.1.1)") != 0);
    /* answers with a wrong transaction id are ignored, then the timeout */
    setup("lab.example.org", 0, 1);
    g_mode = R_WRONGTXID;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(1500);
    CHECK(O.err == OD_E_DNS);
    CHECK(strstr(od_err_text(&O), "(DNS e") != 0);
    CHECK(ST.dns_ignored + ST.dns_bad >= 1);
    /* no resolver from DHCP */
    setup("lab.example.org", 0, 0);
    od_set_dns(&O, &D, 0);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(100);
    CHECK(O.err == OD_E_DNS && strcmp(od_err_text(&O), "No DNS server") == 0 && g_queries == 0);
    /* a build without a resolver object */
    setup("lab.example.org", 0, 0);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(100);
    CHECK(O.err == OD_E_DNS && strstr(od_err_text(&O), "needs DNS") != 0 && g_queries == 0);
}

static void test_other_paths(void)
{
    /* a host-only config without the HOST flag, an invalid host and no address: no setup */
    setup("lab.example.org", 0, 1);
    CFG.flags &= ~OD_CFGF_HOST;
    CHECK(od_apply_config(&O) == OD_E_NOCFG);
    CFG.flags |= OD_CFGF_HOST; strcpy(CFG.host, "bad name");
    CHECK(od_apply_config(&O) == OD_E_NOCFG);
    strcpy(CFG.host, "lab.example.org");
    CHECK(od_apply_config(&O) == 0 && O.use_host == 1);
    /* cancel during a lookup aborts it */
    g_mode = R_NONE;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(50);
    CHECK(O.dns_state == OD_DNS_BUSY && D.state == SDDNS_BUSY);
    od_cancel(&O, T);
    CHECK(O.phase == OD_IDLE && D.state == SDDNS_IDLE && O.dns_state == OD_DNS_NONE);
    /* the shared resolver is taken away (aborted) by the other user: the hub starts again */
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(30);
    sddns_abort(&D);
    g_mode = R_ANSWER;
    ticks(100);
    CHECK(O.srv_ip == g_answer && O.phase == OD_FIND);
    /* the server did not answer: the next entry looks the name up again (the address may have changed) */
    g_answer = IP4(198, 51, 100, 9);
    O.err = OD_E_SERVER; O.phase = OD_ERR;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    CHECK(O.srv_ip == 0);
    ticks(300);
    CHECK(O.srv_ip == IP4(198, 51, 100, 9) && O.m.srv_ip == O.srv_ip && O.phase == OD_FIND);
    /* a numeric host in the host field is taken as an address by sddns without a packet */
    setup("203.0.113.99", 0, 1);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(100);
    CHECK(g_queries == 0 && O.srv_ip == IP4(203, 0, 113, 99) && O.phase == OD_FIND);
    /* nothing is resolved after the ticket (in a match): phase PEER has no lookup */
    setup("lab.example.org", 0, 1);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    ticks(100);
    { unsigned q = g_queries; ticks(2000); CHECK(g_queries == q); }
}

/* DNS fallback of a release build: address AND name patched, the address first; the name only after OD_BK_AFTER "no answer" failures.
   The fake server never answers (hub_send swallows everything): every search attempt times out, which is also what a resolver lie looks like (the pinned key refuses it). */
#define UNTIL(c, max) do { unsigned e_ = T + (max); while (!(c) && T < e_) { T += 10u; od_tick(&O, T); } } while (0)
static const unsigned BAKED = IP4(136, 148, 214, 38);
static void test_name_fallback(void)
{
    unsigned q;
    /* 1286 shape without a name: never a lookup, the address stays (today's behaviour) */
    setup(0, BAKED, 1);
    CHECK(O.use_host == 0 && od_server_ip(&O) == BAKED);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.phase == OD_ERR, 400000u);
    CHECK(O.phase == OD_ERR && O.err == OD_E_SERVER && g_queries == 0 && O.srv_ip == BAKED && O.on_bk == 0);
    /* address + name: no packet to DNS while the address is tried; after two failures the name is looked up, the search goes on at the answer */
    setup("play.example.org", BAKED, 1);
    CHECK(O.use_host == 0 && od_server_ip(&O) == BAKED && O.cfg_ok == 1);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.phase == OD_FIND, 1000u);
    CHECK(O.phase == OD_FIND && g_queries == 0 && O.m.srv_ip == BAKED);          /* the address first, no DNS */
    UNTIL(O.on_bk != 0, 400000u);
    CHECK(O.on_bk == 2u && O.srv_ip == 0 && ST.on_bk == 2u);
    UNTIL(O.srv_ip != 0, 70000u);                                                  /* after the retry backoff */
    CHECK(O.srv_ip == g_answer && O.m.srv_ip == g_answer && g_queries == 1 && g_last_to == RESOLVER && ST.dns_state == OD_DNS_OK);
    /* the answer never proves the key (a lie, or still not our server): after two more failures back to the address */
    UNTIL(O.on_bk == 0 || O.phase == OD_ERR, 400000u);
    CHECK(O.srv_ip == BAKED || O.phase == OD_ERR);
    if (O.phase != OD_ERR) CHECK(O.on_bk == 0 && O.m.srv_ip == BAKED);
    UNTIL(O.phase == OD_ERR, 400000u);
    CHECK(O.phase == OD_ERR && O.err == OD_E_SERVER);                              /* today's box at the end */
    /* the next search starts on the address again */
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    CHECK(O.on_bk == 0 && O.srv_ip == BAKED);
    od_cancel(&O, T);
    /* DNS dead: the lookup (DHCP resolver, then 1.1.1.1) fails, the address is in force again and the box is today's "Cannot reach the server" */
    setup("play.example.org", BAKED, 1);
    g_mode = R_NONE; g_to_pub = 0;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.on_bk != 0, 400000u);
    q = g_queries;
    UNTIL(O.phase == OD_ERR, 70000u);
    CHECK(O.phase == OD_ERR && O.err == OD_E_SERVER && O.on_bk == 0 && O.srv_ip == BAKED && O.m.srv_ip == BAKED);
    CHECK(q == 0 && g_queries == 3 && g_to_pub >= 1 && ST.dns_state == OD_DNS_FAILED && ST.dns_err == SDDNS_E_TIMEOUT);
    /* a private answer is refused in the fallback (a LAN resolver cannot steer the console into the LAN) */
    setup("play.example.org", BAKED, 1);
    g_answer = IP4(192, 168, 1, 9);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.phase == OD_ERR, 400000u);
    CHECK(O.err == OD_E_SERVER && O.srv_ip == BAKED && O.on_bk == 0 && g_queries == 1);
    /* NXDOMAIN (the record was removed): the same */
    setup("play.example.org", BAKED, 1);
    g_mode = R_NXDOMAIN;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.phase == OD_ERR, 400000u);
    CHECK(O.err == OD_E_SERVER && O.srv_ip == BAKED && O.on_bk == 0 && g_queries == 1 && ST.dns_err == SDDNS_E_NXDOMAIN);
    /* a resolved address that answered (OD_BK_PROVEN) stays for the next search; an unproven one does not */
    setup("play.example.org", BAKED, 1);
    O.on_bk = 2u | 4u; O.srv_ip = g_answer;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    CHECK(O.srv_ip == g_answer && (O.on_bk & 4u));
    od_cancel(&O, T);
    O.on_bk = 2u; O.srv_ip = g_answer;
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    CHECK(O.srv_ip == BAKED && O.on_bk == 0);
    od_cancel(&O, T);
    /* a backup address on the card keeps its old meaning (no lookup) */
    setup("play.example.org", BAKED, 1);
    od_set_backup(&O, IP4(198, 51, 100, 2));
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.on_bk != 0, 400000u);
    CHECK(O.on_bk == 1u && O.srv_ip == IP4(198, 51, 100, 2) && g_queries == 0);
    /* no resolver object in the build: no fallback */
    setup("play.example.org", BAKED, 0);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    UNTIL(O.phase == OD_ERR, 400000u);
    CHECK(O.err == OD_E_SERVER && O.on_bk == 0 && O.srv_ip == BAKED);
}

/* the hub's own HELLO / WHOAMI (hub_soc.c) outside a search: the same rule (found in the Dolphin lab: a dead address closes the menu rows before any search) */
static void test_idle_fallback(void)
{
    unsigned q;
    setup("play.example.org", BAKED, 1);
    CHECK(od_idle_resolve(&O, T) == 1 && g_queries == 0);                        /* the address is in force: nothing to do */
    od_idle_noanswer(&O);
    CHECK(O.on_bk == 0 && O.srv_ip == BAKED && g_queries == 0);                  /* one failure: nothing yet */
    od_idle_noanswer(&O);
    CHECK(O.on_bk == 2u && O.srv_ip == 0);                                       /* two: the name */
    q = 0; while (!od_idle_resolve(&O, T) && q++ < 500) T += 10u;
    CHECK(O.srv_ip == g_answer && g_queries == 1 && O.phase == OD_IDLE && O.m.srv_ip == g_answer);   /* resolved, no box */
    od_idle_ok(&O);
    CHECK(O.on_bk == 6u && O.fails == 0);                                        /* answered: proven, stays */
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0 && O.srv_ip == g_answer);   /* a search keeps it */
    od_cancel(&O, T);
    /* the resolved address stops answering: back to the address after two failures */
    od_idle_noanswer(&O); od_idle_noanswer(&O);
    CHECK(O.on_bk == 0 && O.srv_ip == BAKED);
    /* DNS dead: the lookup ends, the address is back, no error box, nothing sent meanwhile is the caller's rule */
    setup("play.example.org", BAKED, 1);
    g_mode = R_NONE;
    od_idle_noanswer(&O); od_idle_noanswer(&O);
    CHECK(O.on_bk == 2u);
    q = 0; while (!od_idle_resolve(&O, T) && q++ < 1000) T += 10u;
    CHECK(O.on_bk == 0 && O.srv_ip == BAKED && O.phase == OD_IDLE && O.err == OD_E_NONE && g_queries == 3);
    /* the 1286 shape (no name): never a lookup */
    setup(0, BAKED, 1);
    od_idle_noanswer(&O); od_idle_noanswer(&O); od_idle_noanswer(&O);
    CHECK(O.on_bk == 0 && O.srv_ip == BAKED && g_queries == 0 && od_idle_resolve(&O, T) == 1);
    /* od_tick counts every failed request of the mnc once (whoever polled it) and runs the lookup itself */
    setup("play.example.org", BAKED, 1);
    O.m.state = MNC_FAILED; O.m.err = MNC_E_TIMEOUT; O.m.sent_ms = 5000u;
    ticks(50);
    CHECK(O.fails == 1 && O.on_bk == 0);                                        /* counted once, not per tick */
    O.m.sent_ms = 21000u;                                                         /* the next request failed too */
    ticks(50);
    CHECK(O.on_bk == 2u || O.srv_ip == g_answer);
    q = 0; while (O.srv_ip != g_answer && q++ < 500) { T += 10u; od_tick(&O, T); }
    CHECK(O.srv_ip == g_answer && O.on_bk == 2u && g_queries == 1 && O.phase == OD_IDLE);   /* resolved from od_tick, no box */
    O.m.state = MNC_DONE;
    ticks(20);
    CHECK(O.on_bk == 6u && O.fails == 0);
    O.m.state = MNC_IDLE;
    /* during a search the idle path does nothing (the search owns the swaps) */
    setup("play.example.org", BAKED, 1);
    CHECK(od_request(&O, T, MNC_MODE_DIRECT, "CD#2") == 0);
    od_idle_noanswer(&O); od_idle_noanswer(&O);
    CHECK(O.on_bk == 0 && O.fails == 0);
    od_cancel(&O, T);
}

int main(void)
{
    test_names();
    test_address_wins();
    test_resolve_and_cache();
    test_failure_boxes();
    test_other_paths();
    test_name_fallback();
    test_idle_fallback();
    printf("od_dns_test: %s (%d checks, %d failures)\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
