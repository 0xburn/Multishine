/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SOCC_H
#define SOCC_H
/*
 * socc: the console-side client of the social ops the menus need: the menu tick (PRESENCE_SET with the
 * toast block), the Mano A Mano answer (INVITE_RESPOND), the post-fight friend request (FIGHT_FRIEND), the Friends screen (FRIEND_LIST ext / requests, FRIEND_ADD,
 * FRIEND_ANSWER, FRIEND_REMOVE, BLOCK, INVITE_SEND with the toast flag, INVITE_STATUS, MSG_SEND) and the 'friend requests after a fight' setting (SOCIAL_PROFILE_GET/SET
 * with the 5th byte).  Same style as gnc.c: C89, no libc, no heap, never blocks, one request at a time on an mnc's wire v2 session and io (a DEVICE session is needed:
 * the social ops refuse an anonymous one).  A lost reply is asked again with the SAME sealed datagram (same counter): the server answers a repeat from its reply cache.
 * Every reply is hostile input: the parsers check every length, count and character (printable ASCII only) and refuse anything else (the reply is then ignored).
 * The friend list storage belongs to the caller (socc_flist takes a buffer): the hub overlay (menu tick only) carries no list.
 * -DSOCC_TICK_ONLY (the hub overlay): only the tick, INVITE_RESPOND, FRIEND_ANSWER, FIGHT_FRIEND and the profile ops are built.
 */
#include "mnc.h"

#define SOCC_OP_RANKED_STATUS 0x16  /* our Grandmaster rank (ms_ranked.status_view + ms_standings.me_tail): the character screen's Rank block */
#define SOCC_OP_FIGHT_FRIEND 0x19
#define SOCC_OP_PRESENCE 0x20
#define SOCC_OP_FLIST 0x21
#define SOCC_OP_FADD 0x22
#define SOCC_OP_FANSWER 0x23
#define SOCC_OP_FREMOVE 0x24
#define SOCC_OP_BLOCK 0x25
#define SOCC_OP_INVSEND 0x27
#define SOCC_OP_INVRESP 0x29
#define SOCC_OP_INVSTATUS 0x2A
#define SOCC_OP_PROFGET 0x2C
#define SOCC_OP_PROFSET 0x2D
#define SOCC_OP_MSGSEND 0x2E
#define SOCC_OP_MSGFETCH 0x2F

#define SOCC_CAP_SOCIAL 8u          /* HELLO capability bits: social ops */
#define SOCC_CAP_MESSAGES 16u
#define SOCC_CAP_TOASTS 256u
#define SOCC_REGION_N 7u            /* account regions 0..6 = NAE NAW EU LA AS OC NAC (ms_region.CODES, append only), 0xFF none */
#define SOCC_SPS_RG7 1u             /* SOCIAL_PROFILE_SET 8th byte bit 0 (ms_wire.SPS_RG7): this console knows region 6 (NAC); SOCIAL_PROFILE_GET asks with ext bit 2 (SPG_EXT_RG7) */
#define SOCC_CAP_RECENT 2048u       /* FRIEND_LIST kind 4 (recent players) and the FRIEND_REMOVE scope byte */

#define SOCC_PRES_MENUS 2u          /* PRESENCE_SET state: in our menus (the only state that gets toasts and notices) */
#define SOCC_PRES_ONLINE 1u
#define SOCC_NOTICES 3u
#define SOCC_FL_PAGE 8u             /* FRIEND_LIST ext: friends per reply */
#define SOCC_RQ_PAGE 16u            /* FRIEND_LIST kinds 1..3: entries per reply */
#define SOCC_TRIES 5u

#define SOCC_E_NONE 0
#define SOCC_E_BUSY 1
#define SOCC_E_ARG 2
#define SOCC_E_NOSESSION 3          /* no READY session, or the server forgot it: hello again */
#define SOCC_E_TIMEOUT 4
#define SOCC_E_SERVER 5             /* status != 0 in socc.status (ms_wire STATUS numbers) */
#define SOCC_E_BAD_REPLY 6

#define SOCC_ST_RATE_LIMITED 3
#define SOCC_ST_AUTH_FAILED 4
#define SOCC_ST_NOT_FOUND 16
#define SOCC_ST_NOT_SUPPORTED 18
#define SOCC_ST_UNKNOWN_OP 2
#define SOCC_ST_LIMIT_REACHED 33
#define SOCC_ST_INVITE_PENDING 34
#define SOCC_ST_INVITE_GONE 35
#define SOCC_ST_ACCOUNT_NEEDED 46

/* invite flags (toast block) */
#define SOCC_IF_BOT 1u
#define SOCC_IF_WEB 2u
#define SOCC_IF_SEARCH 4u
#define SOCC_IF_FRIENDS 8u
/* notice kinds */
#define SOCC_N_REQUEST 1u
#define SOCC_N_ACCEPTED 2u
#define SOCC_N_DIRECT 3u            /* your own console: Slippi Direct, code = the Slippi code to search (the website's 'Invite on Slippi Direct') */
#define SOCC_N_ROOM_INVITE 4u       /* a friend invites you to their Game Night room: code = the 4-digit room code, name = the inviter */
#define SOCC_N_ROOM_JOIN 5u         /* the website's 'Join room' note to your own console: code = the room code, name = the room's host */

struct socc_notice { mnc_u8 kind; char code[9], name[13]; mnc_u16 age; };
struct socc_tick {                  /* PRESENCE_SET reply with ext bit 1 */
    mnc_u32 invite;                 /* the old mail word (an invite id of the doorbell, 0 = none) */
    mnc_u8 requests;                /* incoming friend requests pending */
    mnc_u8 msgs;                    /* unread messages in total (ext bit 0; the Main Menu's Friends row and bar say so) */
    mnc_u8 online, online_ok;       /* friends online as this player sees them (ext bit 2, the last byte of the reply); online_ok 0 = the server sent none (older server) */
    mnc_u8 has;                     /* bit 0 invite toast, bit 1 GO */
    mnc_u32 id;
    char from[9], name[13];         /* the challenger's connect code and display name */
    mnc_u8 flags, mode;
    mnc_u16 ms_left;                /* accept window left at the fetch (10000 = the full 10 s) */
    char go[9];                     /* GO: the peer this console's own challenge was accepted by (start a Direct search of it) */
    mnc_u8 nn;
    struct socc_notice n[SOCC_NOTICES];
};
struct socc_friend {                /* FRIEND_LIST entry; the ext fields (seen, scode, sname) only in a friends ext list */
    char code[9], name[13];
    mnc_u8 presence, flags, seen;   /* presence 0 offline .. 4 in a match; flags bit 0 bot; seen = days since last seen (255 hidden / unknown) */
    char scode[9], sname[16];       /* the player-typed Slippi code and name (NOT verified) */
    char room[5];                   /* flags bit 2: the 4-digit code of the Game Night room this friend is in AND this console may join ("" = none) */
    mnc_u8 rflags;                  /* ... bit 0 the passcode is asked of this console, bit 1 the room has a passcode */
    mnc_u8 gmode, gflags;           /* flags bit 5 (only when the request asked for it): the friend is in a Dojo / Grandmaster / Mano a mano match: mode u8 (ms_wire.MODES), game flags u8 (bit 0 = watchable for you) */
};
#define SOCC_FL_GAME 2u             /* FRIEND_LIST ext byte bit 1 (with bit 0): ask for the game tail */
#define SOCC_FF_GAME 32u            /* entry flag: a game tail follows */
#define SOCC_MODE_RANKED 0u         /* ms_wire.MODES values the tail uses (Grandmaster, Dojo, Mano a mano) */
#define SOCC_MODE_UNRANKED 1u
#define SOCC_MODE_DIRECT 2u
struct socc_inv {                   /* FRIEND_LIST kind 5 entry (the Friends screen's Invites): a Mano a mano invite or a Game Night room invite that may still be accepted */
    mnc_u8 type;                    /* 1 Mano a mano (code = the sender's connect code), 2 Game Night room invite (code = the 4-digit room code) */
    mnc_u32 id;                     /* INVITE_RESPOND takes it (flags bit 0 for an answer from the list) */
    char code[9], name[13];
    mnc_u16 age, left;              /* seconds since the invite came / seconds it can still be accepted */
};
#define SOCC_INV_PAGE 6u            /* kind 5 entries per reply (ms_social.INV_PAGE) */
struct socc_msg {                   /* MSG_FETCH entry (an unread message of a friend): id is the cursor, age seconds, the text 1..60 printable characters */
    mnc_u32 id;
    char from[9], name[13];
    mnc_u16 age;
    char text[61];
};
#define SOCC_MSG_PAGE 4u            /* messages per MSG_FETCH reply (ms_social.MSG_FETCH_MAX) */
struct socc_prof { mnc_u8 privacy, policy, flags, msg_policy, fight, have_fight, region, watch, have_acct; };   /* region 0..6 (6 NAC) or 0xFF none; watch 0 nobody / 1 friends; have_acct = the server sent them */

/* RANKED_STATUS reply (our Grandmaster only; never a Slippi service): shown10 = the shown rating in tenths, rank 0 = no place, tier: 0.. = the server's tier list from the lowest (the names come from stb_tiers.h, the tier data file), SOCC_TIER_GM, SOCC_TIER_PLACEMENT; pl_left = placement games still to play */
#define SOCC_TIER_GM 254u
#define SOCC_TIER_PLACEMENT 255u
struct socc_rank { int shown10; mnc_u16 rank, total, games; mnc_u8 pl_left, tier, have; };

enum { SOCC_IDLE = 0, SOCC_BUSY = 1, SOCC_DONE = 2, SOCC_FAILED = 3 };

typedef struct socc {
    mnc *c;
    int state, err, status;
    mnc_u8 op, kind, fgame;
    mnc_u32 timeout_ms, sent_ms, ctr;
    mnc_u8 tries;
    mnc_u8 pkt[112];                /* the sealed request, resent as is (MSG_SEND: 9 + 61 + header) */
    mnc_u32 pkt_len;
    /* results */
    struct socc_tick tick;          /* PRESENCE_SET */
    char peer[9];                   /* INVITE_RESPOND, INVITE_STATUS */
    mnc_u8 result;                  /* FRIEND_ADD / FIGHT_FRIEND: 0 requested, 1 friends now, 2 already friends; INVITE_STATUS: 0 pending 1 accepted 2 expired */
    mnc_u8 secs, hold;              /* INVITE_STATUS seconds left; INVITE_SEND hold seconds */
    mnc_u32 inv_id;                 /* INVITE_SEND */
    struct socc_prof prof;          /* SOCIAL_PROFILE_GET / SET */
    struct socc_rank rk;            /* RANKED_STATUS */
    mnc_u8 nm;                      /* MSG_FETCH: messages in msg[] */
    struct socc_msg msg[SOCC_MSG_PAGE];
    mnc_u8 total, n;                /* FRIEND_LIST */
    struct socc_friend *fl;         /* caller's buffer for FRIEND_LIST (cap entries) */
    mnc_u8 fl_cap;
    struct socc_inv *il;            /* caller's buffer for FRIEND_LIST kind 5 (socc_ilist) */
    mnc_u32 st_sent, st_resent, st_bad;
} socc;

void socc_init(socc *s, mnc *c);
int socc_tick(socc *s, mnc_u32 now, unsigned state);                         /* PRESENCE_SET sver 1 | state | 0 | ext 2 */
int socc_invite_respond(socc *s, mnc_u32 now, mnc_u32 id, int accept);
int socc_fight_friend(socc *s, mnc_u32 now, const mnc_u8 match_id[8]);
int socc_fight_check(socc *s, mnc_u32 now, const mnc_u8 match_id[8]);   /* the question before the post-fight prompt: result 0 = may show, 2 = already friends, 5 = do not show */
int socc_flist(socc *s, mnc_u32 now, unsigned kind, unsigned offset, struct socc_friend *out, unsigned cap);   /* kind 0: ext list (8 per page); kind 4: recent players (plain entries, 16 per page, needs SOCC_CAP_RECENT) */
int socc_flist_x(socc *s, mnc_u32 now, unsigned kind, unsigned offset, struct socc_friend *out, unsigned cap, unsigned game);   /* as socc_flist; game 0: ask without the game tail (a server that does not know the bit) */
int socc_ilist(socc *s, mnc_u32 now, struct socc_inv *out, unsigned cap);       /* FRIEND_LIST kind 5: my pending invites (an older server refuses it: BAD_REQUEST, the caller leaves the list empty); answer in out[0..s->n), s->total */
int socc_invite_respond_l(socc *s, mnc_u32 now, mnc_u32 id, int accept);        /* INVITE_RESPOND with flags bit 0: answered from the Invites list (an invite that ran out unanswered is still acceptable for 5 minutes); the reply's peer is the sender's code or the room code */
int socc_friend_add(socc *s, mnc_u32 now, const char *code);
int socc_friend_answer(socc *s, mnc_u32 now, const char *code, int accept);
int socc_friend_remove(socc *s, mnc_u32 now, const char *code);
int socc_recent_remove(socc *s, mnc_u32 now, const char *code);              /* FRIEND_REMOVE with scope 1: only this player leaves MY recent list (needs SOCC_CAP_RECENT) */
int socc_block(socc *s, mnc_u32 now, const char *code);
int socc_invite_send(socc *s, mnc_u32 now, const char *code);                /* Mano A Mano with the toast flag (mode 2, flags 1) */
int socc_invite_status(socc *s, mnc_u32 now, mnc_u32 id);
int socc_msg_send(socc *s, mnc_u32 now, const char *code, const char *text);
int socc_msg_fetch(socc *s, mnc_u32 now, mnc_u32 after, int show_only);     /* MSG_FETCH, needs SOCC_CAP_MESSAGES; answers up to SOCC_MSG_PAGE messages (oldest first) in s->msg / s->nm.  show_only = 0: marks every message up to `after` read first, answers the unread ones above it
                                                                               (the old behaviour).  show_only = 1 (flag 1,, the console's toast): reads NOTHING, answers the unread messages the console has not shown yet, each once; `after` is unused */
int socc_prof_get(socc *s, mnc_u32 now);                                    /* with the fight byte (ext bit 0) */
int socc_prof_set_fight(socc *s, mnc_u32 now, const struct socc_prof *cur, int on);   /* the other four bytes as read: only the 5th changes */
int socc_prof_set_acct(socc *s, mnc_u32 now, const struct socc_prof *cur, int fight, unsigned region, unsigned watch);   /* 8 bytes (7 + SOCC_SPS_RG7): the Online page's Region and Who can watch rows */
int socc_rank_me(socc *s, mnc_u32 now);                                    /* RANKED_STATUS (no request body): the Rank block of the Grandmaster character screen; the answer is in s->rk (rk.have 0 = a server without the tail: draw nothing) */
int socc_parse_rank(const mnc_u8 *b, mnc_u32 n, struct socc_rank *out);   /* 0 ok (out->have = the tail was there), < 0 refused */
int socc_poll(socc *s, mnc_u32 now);                                        /* -> state */
void socc_abort(socc *s);

/* the parsers (host-tested against the server's encoders): 0 ok, < 0 refused */
int socc_parse_tick(const mnc_u8 *b, mnc_u32 n, struct socc_tick *t);
int socc_parse_flist(const mnc_u8 *b, mnc_u32 n, int ext, mnc_u8 *total, mnc_u8 *count, struct socc_friend *out, unsigned cap);
int socc_parse_flist_g(const mnc_u8 *b, mnc_u32 n, int ext, int game, mnc_u8 *total, mnc_u8 *count, struct socc_friend *out, unsigned cap);   /* game 1: entries with flag 32 carry the game tail */
int socc_parse_ilist(const mnc_u8 *b, mnc_u32 n, mnc_u8 *total, mnc_u8 *count, struct socc_inv *out, unsigned cap);
int socc_parse_msgs(const mnc_u8 *b, mnc_u32 n, mnc_u8 *count, struct socc_msg *out);
int socc_parse_peer(const mnc_u8 *b, mnc_u32 n, char peer[9]);
#endif
