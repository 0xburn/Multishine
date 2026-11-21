/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Mockups hub and the new mock screens (sg_mockup.h).  Strict C89, no libc.
 * EXAMPLE DATA ONLY: every name, code, number and room below is invented (codes "XMPL n" cannot be real connect codes); nothing is read from the card or the
 * network, nothing is sent or stored.  Text uses the menu font's safe set only: A-Z a-z 0-9, space and. , ' - :
 */
#include "sg_ui.h"

#if SG_MOCKUPS      /* everything but sgm_frd_join (sg_mockup.h) */
/* ---- example data ---- */
struct sgm_room { const char *name, *host; unsigned char mem, cap, flags, stadium; };
#define RF_SPECT 2u
#define RF_PASS 4u
#define RF_RUN 16u
static const struct sgm_room ROOMS[8] = {
    { "Friday Night", "Wobble Pilot", 5, 8, RF_SPECT, 0 },
    { "Wobble Club", "Pocket Fox", 12, 16, RF_SPECT | RF_RUN, 0 },
    { "Low Tiers Only", "Lylat Lou", 3, 8, 0, 2 },
    { "Ditto Dojo", "Cliff Hanger", 8, 8, RF_SPECT | RF_RUN, 0 },
    { "Locals Mirror", "Tipper Tom", 2, 32, RF_PASS, 0 },
    { "Practice Hour", "Wobble Pilot", 6, 12, RF_RUN, 0 },
    { "Frozen Jam", "Pocket Fox", 4, 8, RF_SPECT, 1 },
    { "Late Night", "Lylat Lou", 1, 8, 0, 0 },
};
static const char *const STADIUM[3] = { "Stadium: normal", "Stadium: frozen", "Stadium: removed" };

struct sgm_friend { const char *name, *word, *status; };
static const struct sgm_friend FRIENDS[6] = {
    { "Wobble Pilot", "Menu", "Status: In Menu" },
    { "Pocket Fox", "Play", "Status: Playing" },
    { "Lylat Lou", "Find", "Status: Searching" },
    { "Cliff Hanger", "On", "Status: Online" },
    { "Tipper Tom", "Off", "Status: Offline" },
    { "Practice Bot", "Bot", "Status: Bot, Always On" },
};

/* tiers,  (height 12.. 1) */
struct sgm_tier { const char *name, *who, *share, *cut, *badge; };
static const struct sgm_tier TIERS[12] = {
    { "Grandmaster", "Who: Rank 1 Only", "Share: 1 Player", "Cut-off: None", "Badge: Gold Flare" },
    { "Legend", "Who: Ranks 2 To 10", "Share: 9 Players", "Cut-off: Onyx Floor", "Badge: Magenta" },
    { "Titan", "Who: Top 1 Percent", "Share: Up To 90", "Cut-off: 2120", "Badge: Blue" },
    { "Onyx", "Who: Next 2 Percent", "Share: 2 Percent", "Cut-off: 2020", "Badge: Violet" },
    { "Ruby", "Who: Next 4 Percent", "Share: 4 Percent", "Cut-off: 1900", "Badge: Red" },
    { "Emerald", "Who: Next 8 Percent", "Share: 8 Percent", "Cut-off: 1790", "Badge: Green" },
    { "Platinum", "Who: Next 14 Percent", "Share: 14 Percent", "Cut-off: 1630", "Badge: Teal" },
    { "Gold", "Who: Next 20 Percent", "Share: 20 Percent", "Cut-off: 1450", "Badge: Gold" },
    { "Silver", "Who: Next 22 Percent", "Share: 22 Percent", "Cut-off: 1250", "Badge: Light Grey" },
    { "Bronze", "Who: Next 16 Percent", "Share: 16 Percent", "Cut-off: 1050", "Badge: Bronze" },
    { "Iron", "Who: Next 9 Percent", "Share: 9 Percent", "Cut-off: 900", "Badge: Grey" },
    { "Copper", "Who: The Rest", "Share: 4 Percent", "Cut-off: Below 900", "Badge: Copper" },
};
static const char *const TIER_CUT[12] = { "", "", "2120", "2020", "1900", "1790", "1630", "1450", "1250", "1050", "900", "Below 900" };

/* leaderboard: tier = index into TIERS */
struct sgm_player { const char *name, *main; unsigned short rating, w, l; unsigned char tier, tag; };
static const struct sgm_player BOARD[12] = {
    { "Kestrel", "Fox", 2412, 212, 61, 0, 11 }, { "Marlin", "Falco", 2350, 188, 70, 1, 12 }, { "Juniper", "Marth", 2311, 240, 98, 1, 13 },
    { "Saffron", "Sheik", 2288, 150, 63, 1, 14 }, { "Quartz", "Peach", 2260, 201, 91, 1, 15 }, { "Tundra", "Ice Climbers", 2241, 133, 60, 1, 16 },
    { "Bramble", "Jigglypuff", 2230, 176, 84, 1, 17 }, { "Cobalt", "Captain Falcon", 2214, 99, 47, 1, 18 }, { "Harbor", "Fox", 2201, 160, 82, 1, 19 },
    { "Lantern", "Pikachu", 2190, 121, 64, 1, 20 }, { "Meadow", "Samus", 2172, 140, 77, 2, 21 }, { "Nimbus", "Luigi", 2160, 117, 66, 2, 22 },
};

struct sgm_match { const char *room, *p1, *p2, *stage; unsigned char game, s1, s2, watch; };
static const struct sgm_match MATCHES[4] = {
    { "Ditto Dojo", "Pocket Fox, Fox", "Lylat Lou, Falco", "Final Destination", 2, 1, 0, 3 },
    { "Wobble Club", "Wobble Pilot, Climbers", "Cliff Hanger, Marth", "Battlefield", 1, 0, 0, 7 },
    { "Practice Hour", "Tipper Tom, Marth", "Pocket Fox, Sheik", "Yoshi's Story", 3, 1, 1, 1 },
    { "Frozen Jam", "Lylat Lou, Falco", "Wobble Pilot, Peach", "Stadium, Frozen", 1, 0, 0, 2 },
};

/* ---- the Mockups hub ---- */
struct sgm_row { const char *name, *hint; unsigned char scr; };
static const struct sgm_row HUB[] = {
    { "Rooms Browser", "Game Night rooms: list left, detail right.", SG_MK_ROOMS },
    { "Rooms Browser, Graphic", "Game Night rooms on the Event Match screen.", SG_MK_GNG },
    { "Friend List", "Six example friends and their status.", SG_MK_FRIENDS },
    { "Grandmaster Leaderboard", "Top players, the title holder and your row.", SG_MK_LB },
    { "Rank Tiers", "The 12 tiers from Copper to Grandmaster.", SG_MK_TIERS },
    { "Game Night Room", "Inside a room: members, sides, ready states.", SG_MK_ROOM },
    { "Spectate List", "Matches in Game Night you could watch.", SG_MK_SPEC },
    { "Recorder Indicator", "How a recorded match is shown to you.", SG_MK_REC },
    { "Device identity", "Existing mock: your console key.", SG_IDENT },
    { "Link This Console, QR", "Existing mock: approve this console on a phone.", SG_LINK },
    { "Sign-In Providers", "Existing mock: how an account is attached.", SG_PROV },
    { "Entropy test", "Working test of the random number pool.", SG_ENT },
#if SG_LAB
    { "TCP and TLS test", "Working lab test: TCP and TLS to the lab server.", SG_TLS },
#endif
    { "Update", "Working screen: check for a new version.", SG_UPD },
    { "Controller test", "Working test: the numbers of your pad.", SG_CTL },
    { "Back", "Back to the Main Menu.", SG_MOCK },
};
#define SGM_HUB_N (sizeof HUB / sizeof HUB[0])
#define SGM_NEW 8u                  /* the first rows are the new mock screens */

static unsigned hub_row_of(unsigned scr)
{
    unsigned i;
    if (scr == SG_MAKEKEY) scr = SG_IDENT;
    if (scr == SG_PROV_DETAIL) scr = SG_PROV;
    for (i = 0; i + 1u < SGM_HUB_N; i++) if (HUB[i].scr == scr) return i;
    return 0xFFu;
}

/* ---- list kinds ---- */
static unsigned room_mem(const struct sg *g, unsigned i)
{
    return g->mk.state == SGM_FULL || g->mk.state == SGM_SPECONLY ? ROOMS[i].cap : ROOMS[i].mem;
}
static unsigned room_flags(const struct sg *g, unsigned i)
{
    unsigned f = ROOMS[i].flags;
    if (g->mk.state == SGM_FULL) f &= ~RF_SPECT;
    if (g->mk.state == SGM_SPECONLY) f |= RF_SPECT;
    return f;
}
static void rooms_sub(const struct sg *g, struct sgm_ln *l)
{
    sgm_s(l, "Game Night Rooms   ");
    sgm_u(l, sgm_list_n(g, 0) ? 8u : 0u);
    sgm_s(l, " Listed   Updated Just Now");
}
static void rooms_left(const struct sg *g, unsigned i, struct sgm_ln *l)
{
    sgm_s(l, ROOMS[i].name); sgm_at(l, 17);
    sgm_u(l, room_mem(g, i)); sgm_s(l, " of "); sgm_u(l, ROOMS[i].cap);
}
static void rooms_detail(const struct sg *g, unsigned i, unsigned line, struct sgm_ln *l)
{
    unsigned f = room_flags(g, i), full = room_mem(g, i) >= ROOMS[i].cap;
    switch (line) {
    case 0: sgm_s(l, ROOMS[i].name); break;
    case 1: sgm_s(sgm_s(l, "Host: "), ROOMS[i].host); break;
    case 2: sgm_s(l, "Players: "); sgm_u(l, room_mem(g, i)); sgm_s(l, " of "); sgm_u(l, ROOMS[i].cap); break;
    case 3: sgm_s(l, f & RF_RUN ? "Status: set running" : "Status: waiting"); break;
    case 4: sgm_s(l, f & RF_SPECT ? "Spectators: allowed" : "Spectators: no"); break;
    case 5: sgm_s(l, STADIUM[ROOMS[i].stadium]); break;
    case 6: sgm_s(l, f & RF_PASS ? "Passcode: required" : "Passcode: none"); break;
    case 7: sgm_s(l, "Mode: Winner Stays"); break;
    case 8: if (full) sgm_s(l, "Room is full"); break;
    default: break;
    }
}
static const struct sgm_kind K_ROOMS = { "ROOMS BROWSER", "  No rooms yet.", "  Press Z to create one.", "  The Game Night server is not reachable.",
                                         "A Join  Z Create  Start Refresh  B Back", 8, 9, SGM_NSTATE, rooms_sub, rooms_left, rooms_detail };

static void fr_sub(const struct sg *g, struct sgm_ln *l)
{
    (void)g;
    sgm_s(l, "Friend List   6 Friends   4 Online");
}
static void fr_left(const struct sg *g, unsigned i, struct sgm_ln *l) { (void)g; sgm_s(l, FRIENDS[i].name); sgm_at(l, 17); sgm_s(l, FRIENDS[i].word); }
static void fr_detail(const struct sg *g, unsigned i, unsigned line, struct sgm_ln *l)
{
    (void)g;
    switch (line) {
    case 0: sgm_s(l, FRIENDS[i].name); break;
    case 1: sgm_s(l, "Code: XMPL "); sgm_u(l, i + 1u); break;
    case 2: sgm_s(l, FRIENDS[i].status); break;
    case 3: sgm_s(l, i == 5u ? "Type: Bot" : "Type: Friend"); break;
    case 4: sgm_s(l, "Shares Status: Yes"); break;
    case 6: sgm_s(l, i == 4u ? "Offline: no invite" : "A: Invite, mock"); break;
    default: break;
    }
}
static const struct sgm_kind K_FRIENDS = { "FRIEND LIST", "  No friends yet.", "  Add friends on the phone page.", "  The friends server is not reachable.",
                                           "A Invite  Start Refresh  B Back", 6, 9, 4, fr_sub, fr_left, fr_detail };

static void lb_sub(const struct sg *g, struct sgm_ln *l)
{
    sgm_s(l, "Season 1   4120 Ranked   Grandmaster: ");
    sgm_s(l, g->mk.state == SGM_EMPTY ? "Vacant" : BOARD[0].name);
}
static void lb_left(const struct sg *g, unsigned i, struct sgm_ln *l)
{
    (void)g;
    if (i + 1u < 10u) sgm_s(l, " ");
    sgm_u(l, i + 1u); sgm_s(l, " "); sgm_s(l, BOARD[i].name); sgm_at(l, 19); sgm_u(l, BOARD[i].rating);
}
static void lb_detail(const struct sg *g, unsigned i, unsigned line, struct sgm_ln *l)
{
    (void)g;
    switch (line) {
    case 0: sgm_s(l, BOARD[i].name); break;
    case 1: sgm_s(l, "Rank "); sgm_u(l, i + 1u); sgm_s(l, " of 4120"); break;
    case 2: sgm_s(l, "Rating: "); sgm_u(l, BOARD[i].rating); break;
    case 3: sgm_s(sgm_s(l, "Tier: "), TIERS[BOARD[i].tier].name); break;
    case 4: sgm_s(l, "Won "); sgm_u(l, BOARD[i].w); sgm_s(l, ", Lost "); sgm_u(l, BOARD[i].l); break;
    case 5: sgm_s(sgm_s(l, "Main: "), BOARD[i].main); break;
    case 6: sgm_s(l, "Tag: XMPL "); sgm_u(l, BOARD[i].tag); break;
    case 7: sgm_s(l, i == 0u ? "Title Held: 12 Days" : "Title Held: Never"); break;
    default: break;
    }
}
static const struct sgm_kind K_LB = { "LEADERBOARD", "  No ranked players yet.", "  The Grandmaster title is vacant.", "  Leaderboard not available yet.",
                                      "Start Refresh  B Back", 12, 9, 4, lb_sub, lb_left, lb_detail };

static void tier_sub(const struct sg *g, struct sgm_ln *l) { (void)g; sgm_s(l, "12 Tiers   Your Tier: Silver   Next: Gold At 1450"); }
static void tier_left(const struct sg *g, unsigned i, struct sgm_ln *l) { (void)g; sgm_s(l, TIERS[i].name); sgm_at(l, 16); sgm_s(l, TIER_CUT[i]); }
static void tier_detail(const struct sg *g, unsigned i, unsigned line, struct sgm_ln *l)
{
    (void)g;
    switch (line) {
    case 0: sgm_s(l, TIERS[i].name); break;
    case 1: sgm_s(l, "Height: "); sgm_u(l, 12u - i); sgm_s(l, " of 12"); break;
    case 2: sgm_s(l, TIERS[i].who); break;
    case 3: sgm_s(l, TIERS[i].share); break;
    case 4: sgm_s(l, TIERS[i].cut); break;
    case 5: sgm_s(l, TIERS[i].badge); break;
    case 7: if (i == 8u) sgm_s(l, "Your tier, mock"); break;
    default: break;
    }
}
static const struct sgm_kind K_TIERS = { "RANK TIERS", "", "", "", "B Back", 12, 12, 1, tier_sub, tier_left, tier_detail };

static void sp_sub(const struct sg *g, struct sgm_ln *l)
{
    sgm_s(l, "Spectate   "); sgm_u(l, sgm_list_n(g, 0) ? 4u : 0u); sgm_s(l, " Matches You Can Watch");
}
static void sp_left(const struct sg *g, unsigned i, struct sgm_ln *l) { (void)g; sgm_s(l, MATCHES[i].room); sgm_at(l, 17); sgm_s(l, "Game "); sgm_u(l, MATCHES[i].game); }
static void sp_detail(const struct sg *g, unsigned i, unsigned line, struct sgm_ln *l)
{
    (void)g;
    switch (line) {
    case 0: sgm_s(l, MATCHES[i].room); break;
    case 1: sgm_s(l, MATCHES[i].p1); break;
    case 2: sgm_s(l, MATCHES[i].p2); break;
    case 3: sgm_s(l, "Game "); sgm_u(l, MATCHES[i].game); sgm_s(l, ", Score "); sgm_u(l, MATCHES[i].s1); sgm_s(l, " - "); sgm_u(l, MATCHES[i].s2); break;
    case 4: sgm_s(l, MATCHES[i].stage); break;
    case 5: sgm_s(l, "Watching: "); sgm_u(l, MATCHES[i].watch); break;
    case 7: sgm_s(l, "A: Watch, mock"); break;
    default: break;
    }
}
static const struct sgm_kind K_SPEC = { "SPECTATE LIST", "  Nobody is playing right now.", "  Matches show here when a set starts.", "  The Game Night server is not reachable.",
                                        "A Watch  Start Refresh  B Back", 4, 9, 4, sp_sub, sp_left, sp_detail };

static const struct sgm_kind *kind_of(unsigned scr)
{
    switch (scr) {
    case SG_MK_ROOMS: return &K_ROOMS;
    case SG_MK_FRIENDS: return &K_FRIENDS;
    case SG_MK_LB: return &K_LB;
    case SG_MK_TIERS: return &K_TIERS;
    case SG_MK_SPEC: return &K_SPEC;
    default: return 0;
    }
}
#endif /* SG_MOCKUPS */

/* Y on a Room row of the Friends list: leave the list for the Game Night browser, which joins the friend's room at once (or asks for the passcode first, when the
   server says this console is not the host's friend).  The same overlay and the same screen the Multishine > Game Night row opens (SG_GN), so the join, the lobby and the match
   hand-off are the browser's own. */
void sgm_frd_join(struct sg *g)
{
    char room[5];
    unsigned rf = g->fr.join_rf, i;
    for (i = 0; i < 4u; i++) room[i] = g->fr.join_code[i];
    room[4] = 0;
    g->fr.join = 0; g->fr.exit = 0;
    sg_goto(g, SG_GN);
    gnb_join_friend(&g->gn, room, rf);
    g->gn.frd = 1;                                            /* leaving that room returns to the Friends list (gnb_net_left, gn_glue) */
}

#if SG_MOCKUPS
/* ---- navigation ---- */
void sgm_enter(struct sg *g, unsigned from)
{
    struct sgm *m = &g->mk;
    m->msg = 0;
    if (g->scr == SG_MOCK) {
        unsigned r = hub_row_of(from);
        m->on = 1;
        if (r != 0xFFu) m->hub = (unsigned char)r;
        return;
    }
    if (g->scr == SG_GN || g->scr == SG_MK_GNG) { gnb_enter_keep(&g->gn, g->scr == SG_GN); return; }     /* the Game Night browser (gnb.c) */
    if (g->scr == SG_MK_ROOMS && from == SG_MK_ROOM) { m->cur = m->room; m->state = SGM_LIVE; m->timer = 0; return; }   /* back from the room: on the same row */
    m->cur = 0; m->top = 0; m->state = SGM_LIVE; m->timer = 0;
    if (g->scr == SG_MK_ROOM) {
        m->ret = (unsigned char)(from == SG_MK_ROOMS);
        if (!m->ret) m->room = 0;
        m->ready = 3u;                                        /* Wobble Pilot and Pocket Fox ready, You not yet */
        m->phase = 0;
    }
    if (g->scr == SG_MK_REC) { m->rec = 1; m->recmode = 0; }
}

static void rooms_a(struct sg *g)
{
    struct sgm *m = &g->mk;
    unsigned i = m->cur, f = room_flags(g, i);
    if (room_mem(g, i) >= ROOMS[i].cap) m->msg = f & RF_SPECT ? "Room is full. Spectators allowed: watch, mock." : "Room is full.";
    else if (f & RF_PASS) m->msg = "Passcode required. Mock: no keyboard yet.";
    else { m->room = (unsigned char)i; sg_goto(g, SG_MK_ROOM); }          /* join, mock: the room screen with this room's name */
}

void sgm_frame(struct sg *g, unsigned edge, int up, int down)
{
    struct sgm *m = &g->mk;
    const struct sgm_kind *k = kind_of(g->scr);
    if (g->scr == SG_GN || g->scr == SG_MK_GNG) {
        unsigned held = g->prev_btn | (g->stick_x < -40 ? GNB_B_LEFT : 0u) | (g->stick_x > 40 ? GNB_B_RIGHT : 0u);   /* the stick pushed sideways counts as the d-pad */
        g->gn.real_held = (unsigned short)g->prev_btn; g->gn.real_on = 1;      /* the real buttons, without the stick as d-pad (quick chat opens only from the real D-Pad) */
        gnb_frame(&g->gn, held, up, down);                            /* held buttons: sg_frame stored them in prev_btn before calling us */
        if (g->gn.exit && !g->gn.gfx) {                               /* text view: leave at once (the Event screen front end leaves through the console glue) */
            if (g->gn.back_frd) { g->gn.back_frd = 0; g->gn.exit = 0; sg_goto(g, SG_FRD); }
            else if (g->scr == SG_MK_GNG) sg_goto(g, SG_MOCK); else g->quit = 1;
        }
        (void)edge;
        return;
    }
    if (m->timer && --m->timer == 0) m->msg = "Updated just now. Mock: the same example data.";
    if (g->scr == SG_MOCK) {
        if ((edge & SG_B_DOWN) || down) m->hub = (unsigned char)((m->hub + 1u) % SGM_HUB_N);
        if ((edge & SG_B_UP) || up) m->hub = (unsigned char)((m->hub + SGM_HUB_N - 1u) % SGM_HUB_N);
        if (edge & SG_B_A) {
            if (m->hub + 1u >= SGM_HUB_N) g->quit = 1;
            else sg_goto(g, HUB[m->hub].scr);
        } else if (edge & SG_B_B) g->quit = 1;
        return;
    }
    if (k) {
        sgm_list_nav(m, sgm_list_n(g, k), k->vis, (edge & SG_B_UP) || up, (edge & SG_B_DOWN) || down);
        if (edge & SG_B_X) {
            m->state = (unsigned char)((m->state + 1u) % k->nstates);
            m->cur = 0; m->top = 0; m->timer = 0; m->msg = 0;
        }
        if ((edge & SG_B_START) && k->nstates > 1u) {
            if (m->state == SGM_UNREACH || m->state == SGM_LOADING) m->state = SGM_LIVE;
            m->timer = SGM_LOAD_FRAMES; m->msg = 0;
        }
        if ((edge & SG_B_A) && sgm_list_n(g, k)) {
            if (g->scr == SG_MK_ROOMS) { rooms_a(g); return; }
            if (g->scr == SG_MK_FRIENDS) m->msg = m->cur == 4u ? "Tipper Tom is offline. Mock." : m->cur == 5u ? "Practice Bot: real bot games are in Dojo." : "Invite sent. Mock: nothing left this console.";
            if (g->scr == SG_MK_SPEC) m->msg = "Watch: mock, spectating is not built yet.";
        }
        if ((edge & SG_B_Z) && g->scr == SG_MK_ROOMS) m->msg = "Create room: mock, nothing is sent.";
        if (edge & SG_B_B) sg_goto(g, SG_MOCK);
        return;
    }
    if (g->scr == SG_MK_ROOM) {
        if (edge & SG_B_A) m->ready ^= 4u;
        if (edge & SG_B_X) m->phase = (unsigned char)((m->phase + 1u) % 3u);
        if (edge & SG_B_B) sg_goto(g, m->ret ? SG_MK_ROOMS : SG_MOCK);
        return;
    }
    if (g->scr == SG_MK_REC) {
        if ((edge & (SG_B_A | SG_B_RIGHT)) != 0u) m->rec = (unsigned char)((m->rec + 1u) % 3u);
        if (edge & SG_B_LEFT) m->rec = (unsigned char)((m->rec + 2u) % 3u);
        if (edge & SG_B_X) m->recmode = (unsigned char)((m->recmode + 1u) % 3u);
        if (edge & SG_B_B) sg_goto(g, SG_MOCK);
        return;
    }
    if (edge & SG_B_B) sg_goto(g, SG_MOCK);
}

/* ---- drawing ---- */
static void d_hub(const struct sg *g, struct sg_view *v)
{
    unsigned i;
    struct sgm_ln l;
    sgm_head(v, "MOCKUPS");
    sgm_put(v, 1, SG_A_DIM, "Screens to try out. New mockups first.");
    for (i = 0; i < SGM_HUB_N; i++) {
        sgm_init_ln(&l);
        sgm_s(&l, i == g->mk.hub ? "- " : "  ");
        sgm_s(&l, HUB[i].name);
        sgm_put(v, 3u + i, i == g->mk.hub ? SG_A_HI : (i < SGM_NEW || i + 1u == SGM_HUB_N ? SG_A_BODY : SG_A_DIM), l.b);
    }
    sgm_put(v, 4u + SGM_HUB_N, SG_A_DIM, HUB[g->mk.hub % SGM_HUB_N].hint);
    sgm_put(v, 21, SG_A_DIM, "Example data only: the names are invented.");
    sgm_put(v, 22, SG_A_DIM, "Nothing on these screens is sent or stored.");
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, "A: Open  B: Back To Main Menu");
}

static const char *const MEMBER[5] = { "Wobble Pilot", "Pocket Fox", "You", "Lylat Lou", "Cliff Hanger" };
static const char *const ROLE[5] = { "Queued 1", "On stage", "Challenger", "Queued 2", "Spectating" };
static const char *const PHASE[3] = { "Phase: Waiting. Next: Pocket Fox vs You.", "Phase: Call-Up. Press A when ready, 60 s.", "Phase: Playing. Pocket Fox vs You, Game 1." };

static void d_room(const struct sg *g, struct sg_view *v)
{
    const struct sgm *m = &g->mk;
    const struct sgm_room *r = &ROOMS[m->room < 8u ? m->room : 0u];
    unsigned i;
    struct sgm_ln l;
    sgm_head(v, "GAME NIGHT ROOM");
    sgm_init_ln(&l);
    sgm_s(&l, r->name); sgm_s(&l, "   Host: "); sgm_s(&l, r->host); sgm_s(&l, "   "); sgm_u(&l, r->mem); sgm_s(&l, " of "); sgm_u(&l, r->cap);
    sgm_put(v, 1, SG_A_DIM, l.b);
    sgm_put(v, 3, m->phase == 1u ? SG_A_HI : SG_A_BODY, PHASE[m->phase % 3u]);
    sgm_put(v, 4, SG_A_BODY, "Mode: Winner Stays   Stage Holder: Pocket Fox");
    sgm_init_ln(&l);
    sgm_s(&l, STADIUM[r->stadium]); sgm_s(&l, "   Streak: 2   Crowns: 1");
    sgm_put(v, 5, SG_A_BODY, l.b);
    sgm_put(v, 7, SG_A_DIM, "Member          Place         Ready");
    for (i = 0; i < 5u; i++) {
        sgm_init_ln(&l);
        sgm_s(&l, MEMBER[i]); sgm_at(&l, 16); sgm_s(&l, ROLE[i]); sgm_at(&l, 30);
        sgm_s(&l, i == 4u ? "-" : (m->ready >> i) & 1u ? "Ready" : "Not Ready");
        sgm_put(v, 8u + i, i == 2u ? SG_A_HI : (i == 4u ? SG_A_DIM : ((m->ready >> i) & 1u ? SG_A_GOOD : SG_A_BODY)), l.b);
    }
    sgm_put(v, 15, SG_A_DIM, "Member names only: no codes, no identities.");
    sgm_put(v, 16, SG_A_DIM, "A no-show at call-up is a forfeit after 60 s.");
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, "X: Next Mock Phase");
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, "A: Ready  B: Back");
}

static const char *const REC_SET[3] = { "Send Match Inputs: Off", "Send Match Inputs: Ranked Only", "Send Match Inputs: On" };
static const char *const REC_MODE[3] = { "Preview Mode: Grandmaster", "Preview Mode: Dojo", "Preview Mode: Game Night" };

static void d_rec(const struct sg *g, struct sg_view *v)
{
    const struct sgm *m = &g->mk;
    unsigned on = m->recmode == 0u || m->rec == 2u;         /* Ranked (Grandmaster) is always recorded for the ladder; the rest follows the setting */
    sgm_head(v, "RECORDER INDICATOR");
    sgm_put(v, 1, SG_A_DIM, "What you would see when a match is recorded.");
    sgm_put(v, 3, SG_A_HI, REC_SET[m->rec % 3u]);
    sgm_put(v, 4, SG_A_BODY, REC_MODE[m->recmode % 3u]);
    sgm_put(v, 6, SG_A_BODY, "Character Select And Stage Select");
    sgm_put(v, 7, on ? SG_A_BAD : SG_A_DIM, on ? "  REC   Inputs go to the server." : "  no indicator: not recorded");
    sgm_put(v, 9, SG_A_BODY, "At Game Start, Once");
    sgm_put(v, 10, on ? SG_A_BODY : SG_A_DIM, on ? "  This match is recorded." : "  nothing is shown");
    sgm_put(v, 12, SG_A_BODY, "During The Game");
    sgm_put(v, 13, on ? SG_A_BAD : SG_A_DIM, on ? "  REC next to the pacing text, no frame cost" : "  nothing is shown");
    sgm_put(v, 15, SG_A_DIM, "Grandmaster: always recorded, for the ladder.");
    sgm_put(v, 16, SG_A_DIM, "Dojo, Mano a mano: off unless you turn it on.");
    sgm_put(v, 17, SG_A_DIM, "Game Night: the room's rules, shown before join.");
    sgm_put(v, 19, SG_A_DIM, "The recorder is not built: preview only.");
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, "X: Next Mock Mode");
    sgm_put(v, SG_ROWS - 2u, SG_A_DIM, "A: Change Setting  B: Back");
}

void sgm_draw(const struct sg *g, struct sg_view *v)
{
    const struct sgm_kind *k = kind_of(g->scr);
    if (g->scr == SG_GN || g->scr == SG_MK_GNG) { gnb_text(&g->gn, v, g->scr == SG_MK_GNG); return; }
    if (k) {
        sgm_list_draw(g, v, k);
        if (g->scr == SG_MK_LB && sgm_list_n(g, k))
            sgm_put(v, SGM_LIST_ROW0 + k->vis + 2u, SG_A_GOOD, "You: Rank 312, Silver, 1512, Won 41, Lost 33");
        return;
    }
    switch (g->scr) {
    case SG_MOCK: d_hub(g, v); break;
    case SG_MK_ROOM: d_room(g, v); break;
    case SG_MK_REC: d_rec(g, v); break;
    default: break;
    }
}
#endif /* SG_MOCKUPS */

#if !SG_MOCKUPS
/* The release build (SG_MOCKUPS 0) keeps ONE of the screens that live behind the Mockups numbers: the Game Night browser (SG_GN, opened by the Game Night row, a toast's Open Game Night and the Friends list's Join Room).  Its enter / input / draw dispatch is these three small functions; every other number >= SG_MOCK lands on the Accounts hub. */
void sgm_enter(struct sg *g, unsigned from)
{
    (void)from;
    if (g->scr == SG_GN || g->scr == SG_MK_GNG) { gnb_enter_keep(&g->gn, g->scr == SG_GN); return; }
    g->scr = SG_HUB;
}

void sgm_frame(struct sg *g, unsigned edge, int up, int down)
{
    if (g->scr == SG_GN || g->scr == SG_MK_GNG) {
        unsigned held = g->prev_btn | (g->stick_x < -40 ? GNB_B_LEFT : 0u) | (g->stick_x > 40 ? GNB_B_RIGHT : 0u);   /* the stick pushed sideways counts as the d-pad */
        g->gn.real_held = (unsigned short)g->prev_btn; g->gn.real_on = 1;
        gnb_frame(&g->gn, held, up, down);
        if (g->gn.exit && !g->gn.gfx) {
            if (g->gn.back_frd) { g->gn.back_frd = 0; g->gn.exit = 0; sg_goto(g, SG_FRD); }
            else g->quit = 1;
        }
        (void)edge;
        return;
    }
    g->scr = SG_HUB;
}

void sgm_draw(const struct sg *g, struct sg_view *v)
{
    if (g->scr == SG_GN || g->scr == SG_MK_GNG) gnb_text(&g->gn, v, g->scr == SG_MK_GNG);
}
#endif /* !SG_MOCKUPS */
