/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * CANONICAL copy (offline/common/sdenet/sdenet.h, roadmap step 1; inventory and drift).
 * Merged (SHA-1 b937adb4, the base), (chat copy: fragments,
 * clock_back) (S9 copy: the same clock fix with a global counter). Same API and include guard as
 * every copy, so a stage switches by pointing its #include / source path here (or by -include of this header).  Behaviour:
 *   - monotonic host clock: an older `now` passed to sdenet_connect / sdenet_rx / sdenet_service is clamped to h->now and
 *     counted in st.clock_back (chat/S9 fix, run p2_nodraw); the clamp applies only after the FIRST clock reading
 *     (h->clock_set), so a first `now` >= 2^31 no longer freezes the clock at 0 (defect of the chat and S9 copies);
 *     -DSDENET_S9_CLOCK_GLOBAL also bumps the global sdenet9_clock_back that S9's glue reads;
 *   - reliable FRAGMENTS (ENet SEND_FRAGMENT) both ways, ONLY on a host that set a buffer with sdenet_set_frag_buf (chat's
 *     lobby host).  A host without one behaves exactly like the base copy: reliable sends above SDENET_REL_DATA return
 *     -3 and received fragments are dropped and counted (the chat copy fragmented sends on every host).  Only the next
 *     packet of a channel is reassembled, one at a time per host; its fragments may come in any order; fragments of a
 *     later packet are not acknowledged (the sender repeats them).  -DSDENET_NO_FRAGMENTS compiles the fragment code out
 *     (about 1.2 KB of PowerPC code; for netplay images that never set a buffer; behaviour unchanged for them).
 * The time-base -> milliseconds conversion the glue needs lives in sdenet_clock.h (rate read at run time).
 *
 * sdenet: a minimal, project-owned implementation of the ENet 1.3 wire protocol (the subset Slippi's netplay uses).
 *
 * Written from the protocol as documented by ENet's public headers and checked against the real library (host tests
 * link lsalzman ENet 1.3.13 as vendored by Ishiiruka, MIT; the Docker test peer uses Debian's libenet7).  No ENet
 * code is copied.  C99 subset, no libc, no heap, fixed buffers, not reentrant (the GC glue calls it with interrupts
 * disabled).
 *
 * Supported: protocol header (peer id, session bits, optional sentTime), CONNECT / VERIFY_CONNECT handshake both as
 * initiator and as acceptor (simultaneous connects give two peers; the caller keeps one), ACKNOWLEDGE (with RTT
 * estimate from the echoed sentTime), reliable sends with retransmission (RTO = rtt + 4 var, doubled per attempt) and
 * ENet's timeout rule (limit 32 x RTO and >= 5 s, or 30 s), in-order delivery of reliable commands per channel
 * (up to SDENET_HOLD_SLOTS out-of-order ones are held and acknowledged, further ones are dropped unacknowledged so
 * the sender retransmits them), SEND_UNSEQUENCED with the 1024-entry
 * duplicate window, SEND_UNRELIABLE (delivered as is), PING (sent after 500 ms without an acknowledgement, like ENet),
 * DISCONNECT (both directions, with the 32-bit data), BANDWIDTH_LIMIT / THROTTLE_CONFIGURE (acknowledged, ignored).
 * Not supported (dropped and counted): fragments (unless a buffer is set), compressed packets, packets with a CRC (the sender would need
 * enet_host_compress / enet_host_checksum; Slippi's netplay uses neither).
 */
#ifndef SDENET_H
#define SDENET_H

typedef unsigned int sde_u32;
typedef int sde_s32;
typedef unsigned short sde_u16;
typedef unsigned char sde_u8;

#ifndef SDENET_MAX_PEERS
#define SDENET_MAX_PEERS 2           /* our connect + the peer's simultaneous connect */
#endif
#ifndef SDENET_MTU
#define SDENET_MTU 1200              /* advertised MTU = largest datagram we send or accept */
#endif
#ifndef SDENET_REL_SLOTS
#define SDENET_REL_SLOTS 8           /* reliable commands in flight per peer */
#endif
#ifndef SDENET_REL_DATA
#define SDENET_REL_DATA 192          /* max reliable payload (Slippi selections are 16 B) */
#endif
#ifndef SDENET_HOLD_SLOTS
#define SDENET_HOLD_SLOTS 8          /* out-of-order reliable commands held (and acknowledged) until in order */
#endif
#ifndef SDENET_ACK_SLOTS
#define SDENET_ACK_SLOTS 32
#endif
#define SDENET_CHANNELS 3            /* channel limit (Slippi: enet_host_create(..., 3, ...)) */

/* protocol constants (ENet 1.3 protocol) */
#define SDE_CMD_ACK 1
#define SDE_CMD_CONNECT 2
#define SDE_CMD_VERIFY_CONNECT 3
#define SDE_CMD_DISCONNECT 4
#define SDE_CMD_PING 5
#define SDE_CMD_SEND_RELIABLE 6
#define SDE_CMD_SEND_UNRELIABLE 7
#define SDE_CMD_SEND_FRAGMENT 8
#define SDE_CMD_SEND_UNSEQUENCED 9
#define SDE_CMD_BANDWIDTH_LIMIT 10
#define SDE_CMD_THROTTLE_CONFIGURE 11
#define SDE_CMD_SEND_UNRELIABLE_FRAGMENT 12
#define SDE_CMD_COUNT 13
#define SDE_FLAG_ACK 0x80u
#define SDE_FLAG_UNSEQ 0x40u
#define SDE_HDR_SENT_TIME 0x8000u
#define SDE_HDR_COMPRESSED 0x4000u
#define SDE_HDR_SESSION_MASK 0x3000u
#define SDE_HDR_SESSION_SHIFT 12
#define SDE_PEER_ID_NONE 0xFFFu

/* send flags */
#define SDENET_RELIABLE 1
#define SDENET_UNSEQUENCED 2

enum {
    SDE_FREE = 0,
    SDE_CONNECTING = 1,              /* CONNECT sent, waiting VERIFY_CONNECT */
    SDE_ACK_CONNECT = 2,             /* CONNECT received, VERIFY_CONNECT sent, waiting its acknowledgement */
    SDE_CONNECTED = 3,
    SDE_DISCONNECTING = 4            /* DISCONNECT sent, waiting its acknowledgement */
};

/* disconnect reasons reported to on_disconnect */
enum { SDE_DISC_REMOTE = 1, SDE_DISC_TIMEOUT = 2, SDE_DISC_LOCAL = 3, SDE_DISC_REFUSED = 4 };

struct sdenet_ops {
    /* send one datagram; >= 0 ok */
    int (*send)(void *ctx, sde_u32 ip, sde_u16 port, const sde_u8 *data, sde_u32 len);
    void (*on_connect)(void *ctx, int peer);
    void (*on_disconnect)(void *ctx, int peer, sde_u32 data, int reason);
    void (*on_receive)(void *ctx, int peer, sde_u8 channel, const sde_u8 *data, sde_u32 len);
};

struct sdenet_rel {                  /* one outgoing reliable command */
    sde_u8 used;
    sde_u8 channel;                  /* 0xFF = peer-level command */
    sde_u8 cmdlen;                   /* bytes of the fixed command part in cmd[] */
    sde_u8 sends;
    sde_u8 retx;                     /* due for retransmission */
    sde_u16 seq;
    sde_u16 datalen;
    sde_u32 order;                   /* allocation order: commands go out in queue order */
    sde_u32 sent_ms;
    sde_u32 rto, rto_limit;
    sde_u8 cmd[48];
    sde_u8 data[SDENET_REL_DATA];
};

struct sdenet_ack { sde_u8 channel; sde_u8 cmd; sde_u16 seq; sde_u16 sent_time; };

struct sdenet_hold { sde_u8 used, channel; sde_u16 seq; sde_u16 len; sde_u8 data[SDENET_REL_DATA]; };

struct sdenet_peer {
    sde_u8 state;
    sde_u8 initiator;                /* 1 = we sent CONNECT */
    sde_u8 in_session, out_session;  /* 0xFF until negotiated; kept across resets like ENet */
    sde_u16 out_peer_id;             /* the remote's id for us (SDE_PEER_ID_NONE until known) */
    sde_u16 mtu;
    sde_u32 ip;
    sde_u16 port;
    sde_u8 channels;
    sde_u32 connect_id;              /* opaque, echoed byte for byte */
    sde_u32 connect_data;
    sde_u32 throttle[3];             /* interval, acceleration, deceleration (echoed in VERIFY_CONNECT) */
    sde_u16 out_rel_seq;             /* peer-level (channel 0xFF) reliable sequence */
    sde_u16 ch_out_rel[SDENET_CHANNELS];
    sde_u16 ch_in_rel[SDENET_CHANNELS];
    sde_u16 out_unseq_group;
    sde_u16 in_unseq_group;
    sde_u32 unseq_window[1024 / 32];
    sde_u32 rtt, rtt_var;            /* ms */
    sde_u32 last_ack_ms;             /* last acknowledgement received (ENet's lastReceiveTime) */
    sde_u32 last_rx_ms;              /* any valid datagram */
    sde_u32 earliest_timeout;        /* 0 = none */
    sde_u32 disc_data;
    sde_u32 nacks;
    struct sdenet_ack acks[SDENET_ACK_SLOTS];
    struct sdenet_rel rel[SDENET_REL_SLOTS];
    struct sdenet_hold hold[SDENET_HOLD_SLOTS];
};

struct sdenet_frag {                 /* reassembly of one fragmented reliable packet */
    sde_u8 *buf;                     /* caller's buffer (0: fragments are dropped, the original behaviour) */
    sde_u32 cap;
    sde_u8 active, channel;
    sde_s32 peer;
    sde_u16 start;                   /* reliable sequence number of its first fragment */
    sde_u32 count, total, have_n;
    sde_u32 have[2];                 /* bit per fragment number (SDENET_FRAG_COUNT_MAX <= 64) */
};
#ifndef SDENET_FRAG_COUNT_MAX
#define SDENET_FRAG_COUNT_MAX 16     /* fragments per packet we accept (libenet: up to 1 MB) */
#endif
#if SDENET_FRAG_COUNT_MAX > 64 || SDENET_FRAG_COUNT_MAX < 1
#error "SDENET_FRAG_COUNT_MAX must be 1..64 (have[] bitmap)"
#endif

struct sdenet_stats {
    sde_u32 rx_dgrams, rx_bad, rx_cmds, rx_unseq, rx_unseq_dup, rx_rel, rx_rel_dup, rx_rel_ooo, rx_rel_held, rx_unrel;
    sde_u32 rx_drop_frag, rx_drop_compressed, rx_drop_session, rx_drop_peer, rx_acks;
    sde_u32 tx_dgrams, tx_fail, tx_acks, tx_rel, tx_retx, tx_unseq, tx_pings, rel_full, ack_full;
    sde_u32 connects_in, connects_out, timeouts, disc_rx;
    sde_u32 rx_ack_nomatch;          /* ACKs that matched no command in flight (ignored for RTT/timeouts) */
    sde_u32 rx_drop_allow;           /* CONNECT from an address outside the allow-list (sdenet_set_allow) */
    sde_u32 rx_frag, rx_frag_done, rx_frag_bad, rx_frag_later, tx_frag;
    sde_u32 clock_back;              /* calls whose `now` was older than the host clock (clamped) */
    sde_u32 last_drop_ip;            /* WHY-IT-FAILED record: the source of the last CONNECT dropped by the allow-list (0 = none): the peer's real outside address if it was seen */
    sde_u16 last_drop_port;
    sde_u16 rx_accept_pool;          /* CARRIER POOL: CONNECTs accepted from ANOTHER address of the allowed one's /24 (a 5G CGNAT pool sends from a neighbour); was the pad */
};

struct sdenet_host {
    const struct sdenet_ops *ops;
    void *ctx;
    sde_u32 now;                     /* ms, set by sdenet_rx / sdenet_service */
    sde_u32 seed;
    sde_u32 order;
    sde_u8 clock_set;                /* 1 once `now` was read (the monotonic clamp starts after the first reading) */
    sde_u8 allow_on;                 /* 1: only CONNECTs from allow_ip (and allow_port unless 0) are accepted */
    sde_u16 allow_port;
    sde_u32 allow_ip;
    struct sdenet_peer peer[SDENET_MAX_PEERS];
    struct sdenet_frag frag;
    struct sdenet_stats st;
    sde_u8 tx[SDENET_MTU];
    sde_u32 txlen, txcmds;
    sde_u16 txflags;
};

void sdenet_init(struct sdenet_host *h, const struct sdenet_ops *ops, void *ctx, sde_u32 seed);
/* Start a connection (CONNECT is sent at the next sdenet_service / flush).  Returns the peer index or -1. */
int sdenet_connect(struct sdenet_host *h, sde_u32 ip, sde_u16 port, sde_u8 channels, sde_u32 data, sde_u32 now);
/* Queue (reliable) or send now (unsequenced, acks and due reliable commands piggyback).  0 ok, < 0 error. */
int sdenet_send(struct sdenet_host *h, int peer, sde_u8 channel, int flags, const sde_u8 *data, sde_u32 len);
/* Graceful disconnect (reliable DISCONNECT when connected, else an unsequenced one and an immediate reset). */
void sdenet_disconnect(struct sdenet_host *h, int peer, sde_u32 data);
/* Forget a peer without telling the remote. */
void sdenet_reset_peer(struct sdenet_host *h, int peer);
/* Enable reliable fragments (send and receive) with buf as the reassembly buffer (cap bytes = the largest packet we
   accept); buf 0 = off (default after sdenet_init): sends above SDENET_REL_DATA fail, received fragments are dropped. */
void sdenet_set_frag_buf(struct sdenet_host *h, sde_u8 *buf, sde_u32 cap);
/* Peer allow-list (security review N1/N3): with enable != 0 an incoming CONNECT is accepted only from ip (and from
   `port` unless it is 0 = any port); everything else is dropped and counted in st.rx_drop_allow.  Datagrams for
   already established peers are matched on ip:port as before, so no other host can ever hold a peer slot.  With
   enable == 0 (the library default) every address is accepted.  The addresses we connect to are not affected. */
void sdenet_set_allow(struct sdenet_host *h, sde_u32 ip, sde_u16 port, int enable);
/* One received datagram. */
void sdenet_rx(struct sdenet_host *h, sde_u32 now, sde_u32 ip, sde_u16 port, const sde_u8 *d, sde_u32 len);
/* Timers (retransmit, ping, timeout) and flush of pending acknowledgements / reliable commands. */
void sdenet_service(struct sdenet_host *h, sde_u32 now);
int sdenet_state(const struct sdenet_host *h, int peer);
#ifdef SDENET_S9_CLOCK_GLOBAL
extern sde_u32 sdenet9_clock_back;   /* S9 compatibility: mirrors st.clock_back summed over all hosts */
#endif

#endif
