/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SDDNS_H
#define SDDNS_H

/*
 * sddns: tiny DNS client (IPv4, one A-record question at a time) for the console network stack.
 * Strict C89, no libc, no heap, no float, fixed buffers, never blocks: start a lookup with sddns_start and call sddns_poll
 * from the same frame hook that runs sdnet_poll until it returns SDDNS_DONE or SDDNS_FAILED.
 *
 * The network is two callbacks (sddns_io), so the same code runs on a host socket (the tests) and on the sdnet UDP socket
 * (sddns_sdnet_io, below).  Every reply is untrusted input: see sddns_parse_reply and
 * Addresses are host-order u32 (192.168.1.2 == 0xC0A80102), like sdnet.
 */

typedef unsigned char sddns_u8;
typedef unsigned short sddns_u16;
typedef unsigned int sddns_u32;

/* ---- limits (override with -D) ---- */
#define SDDNS_PORT 53
#define SDDNS_HOST_MAX 128              /* longest host name we resolve (characters, no trailing dot) */
#define SDDNS_WIRE_NAME_MAX 255         /* RFC 1035 limit of an encoded name, enforced on every name in a reply */
#define SDDNS_MAX_PACKET 512            /* classic UDP DNS size; larger datagrams are refused (no EDNS, no TCP) */
#define SDDNS_MAX_ANSWERS 16            /* ANCOUNT above this is refused outright */
#define SDDNS_MAX_CNAME 4               /* CNAME links followed */
#define SDDNS_MAX_JUMPS 8               /* compression pointers followed inside one name */
#define SDDNS_CACHE 4
#define SDDNS_TTL_MIN 30u               /* seconds; a smaller TTL (including 0) is raised to this */
#define SDDNS_TTL_MAX 3600u             /* seconds; a larger one (or one with the top bit set, RFC 2181) is cut to this */
#define SDDNS_TRIES 3                   /* sends per lookup, timeouts base, 2 x base, 4 x base */
#define SDDNS_BASE_MS 1000u
#define SDDNS_RX_BUDGET 4

/* ---- results of sddns_parse_reply (also the values of d->err) ---- */
#define SDDNS_E_NONE 0
#define SDDNS_E_BUSY 1                  /* start refused: a lookup is running */
#define SDDNS_E_ARG 2                   /* start refused: not a valid host name */
#define SDDNS_E_NOSERVER 3              /* no DNS server configured (DHCP gave none, no override) */
#define SDDNS_E_NXDOMAIN 4              /* the name does not exist (authoritative answer, RCODE 3) */
#define SDDNS_E_NODATA 5                /* the name exists but has no (usable) A record */
#define SDDNS_E_SERVFAIL 6              /* every server answered with an error RCODE */
#define SDDNS_E_TIMEOUT 7               /* no (valid) reply after SDDNS_TRIES sends */
#define SDDNS_E_BAD 8                   /* like timeout, but only malformed replies came (parse level: malformed) */

enum sddns_state { SDDNS_IDLE = 0, SDDNS_BUSY = 1, SDDNS_DONE = 2, SDDNS_FAILED = 3 };

typedef struct sddns_io {
    /* send one datagram to ip:port; 0 = handed over, < 0 = could not (counts as lost) */
    int (*send)(void *ctx, sddns_u32 ip, sddns_u16 port, const sddns_u8 *p, sddns_u32 len);
    /* receive one datagram: > 0 length, 0 = none waiting, < 0 = error (treated as none); source in *ip, *port */
    int (*recv)(void *ctx, sddns_u8 *buf, sddns_u32 cap, sddns_u32 *ip, sddns_u16 *port);
    void *ctx;
} sddns_io;

typedef struct sddns_cache_entry {
    sddns_u8 used;
    sddns_u32 ip, stamp, ttl_ms, age;
    char name[SDDNS_HOST_MAX + 1];
} sddns_cache_entry;

typedef struct sddns {
    sddns_io io;
    sddns_u32 server[2];            /* 0 = unused (server[0] 0 = none configured) */
    sddns_u16 port;                 /* the servers' port (53; tests change it) */
    sddns_u32 base_ms;              /* first timeout, doubled per send (tests shorten it) */
    sddns_u32 rng;
    /* lookup in flight */
    int state, err;
    sddns_u32 result_ip, result_ttl;
    int from_cache;
    char name[SDDNS_HOST_MAX + 1];  /* lower case, no trailing dot */
    sddns_u8 qwire[SDDNS_WIRE_NAME_MAX + 1];
    sddns_u32 qwire_len;
    sddns_u16 txid;
    sddns_u32 sent_ms, deadline;
    sddns_u32 tries;
    sddns_u8 sent_mask;             /* bit i: a query went to server[i] */
    sddns_u8 bad_seen, servfail_seen;
    sddns_cache_entry cache[SDDNS_CACHE];
    sddns_u32 age_clock;
    /* statistics */
    sddns_u32 st_sent, st_rx, st_ignored, st_bad;
} sddns;

/* ---- setup ---- */
void sddns_init(sddns *d, const sddns_io *io, sddns_u32 seed);
/* primary (e.g. the DHCP-provided s->dns, or the config override) and optional secondary; 0 = none */
void sddns_set_servers(sddns *d, sddns_u32 primary, sddns_u32 secondary);
void sddns_cache_flush(sddns *d);

/* ---- lookups ---- */
/* 0 = started or already finished (a dotted quad or a cache hit leaves state DONE at once, no packet), else SDDNS_E_BUSY / E_ARG.
   A name whose last label is all digits but is not a valid dotted quad is refused (E_ARG): never ambiguous numbers.  If no server
   is configured the lookup is FAILED with SDDNS_E_NOSERVER (0 returned, read d->err). */
int sddns_start(sddns *d, const char *host, sddns_u32 now);
/* frame-loop entry: at most SDDNS_RX_BUDGET receives and one send per call; returns d->state.  Datagrams that arrive while
   idle are drained and counted as ignored. */
int sddns_poll(sddns *d, sddns_u32 now);
void sddns_abort(sddns *d);                          /* state -> IDLE; late replies are then ignored */
/* cache only, no packet: 1 and *ip if a live entry exists */
int sddns_cache_lookup(sddns *d, const char *host, sddns_u32 now, sddns_u32 *ip);

/* ---- codec (public for the tests) ---- */
/* 1 and the address if s is a valid dotted quad (four decimals 0..255, no leading zeros, nothing else), else 0 */
int sddns_parse_dotted(const char *s, sddns_u32 *ip);
/* validates and lower-cases host (a single trailing dot is dropped) into out[SDDNS_HOST_MAX + 1]; returns its length or -1.
   Labels 1..63 of [A-Za-z0-9_-] (a hyphen at a label edge is refused), at least one label, at most SDDNS_HOST_MAX characters. */
int sddns_normalize_host(const char *host, char *out);
/* encodes a normalised host into wire labels (with the final 0); returns the length or -1 */
int sddns_encode_name(const char *host, sddns_u8 *out, sddns_u32 cap);
/* a query (header + one question, type A, class IN, RD set); returns the length or -1 */
int sddns_build_query(const sddns_u8 *qwire, sddns_u32 qlen, sddns_u16 txid, sddns_u8 *out, sddns_u32 cap);
/* Validate a reply to the query (txid, qwire).  Returns SDDNS_E_NONE with *ip / *ttl (clamped, seconds) set, SDDNS_E_NXDOMAIN,
   SDDNS_E_NODATA, SDDNS_E_SERVFAIL (RCODE other than 0 and 3), or SDDNS_E_BAD for anything malformed or not matching.
   Rules: 12 <= len <= 512; id equal; QR=1, OPCODE 0, TC 0; QDCOUNT 1 and the question equals ours (case-insensitive, A, IN);
   ANCOUNT <= 16; names expanded with at most 8 backward-only compression pointers and at most 255 octets, labels <= 63, no
   reserved label types; every RR must fit the datagram and its RDLENGTH must match (A: 4, CNAME: the name's exact length);
   only the answer section is read; an A record is accepted only if its owner is the question name or the end of a CNAME chain of
   at most 4 links that starts at it (records for other names are skipped, never trusted); the first matching address wins; the
   address must not be 0.0.0.0 nor >= 224.0.0.0; TTL is the smallest of the chain, clamped to [30, 3600] s. */
int sddns_parse_reply(const sddns_u8 *p, sddns_u32 len, sddns_u16 txid, const sddns_u8 *qwire, sddns_u32 qlen,
                      sddns_u32 *ip, sddns_u32 *ttl);

/* ---- sdnet binding (omit with -DSDDNS_NO_SDNET) ---- */
#ifndef SDDNS_NO_SDNET
struct sdnet;
/* Binds a UDP socket on src_port (use an ephemeral port picked from the entropy you have; refused ports as in sdnet_bind) and
   fills io; returns the socket id >= 0 or a negative SDNET_E_*.  The io context is the small struct below, owned by the caller. */
struct sddns_sdnet_ctx { struct sdnet *net; int sock; };
int sddns_sdnet_bind(sddns_io *io, struct sddns_sdnet_ctx *ctx, struct sdnet *net, sddns_u16 src_port);
#endif

#endif
