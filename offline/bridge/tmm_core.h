/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TMM_CORE_H
#define TMM_CORE_H
/*
 * tmm_core: the pure decision logic of TIERED MATCHMAKING in Dojo and Mano a mano.  No I/O, no globals, no libc,
 * header only (static functions, like rep_scene.h): br_glue.c (the engine) includes it, tests it on the host. Strict C89.
 *
 * WHAT IT DECIDES
 *   - the tier of a console from what its card holds (no account / Multishine account / Slippi login) and its link (wired / Wi-Fi / unknown),
 *   - whether Dojo may use Slippi Unranked at all (tier 3 AND positively wired), in which order the two networks are searched and for how long the first one gets,
 *   - where a typed Mano a mano code goes (Slippi Direct or our own),
 *   - the search state machine (one network at a time, leave the first cleanly before the second, never two at once), driven by br_glue.c.
 * THE KNOBS (the only place; our may flip them):
 *   MS_TMM                   1 = the feature is built in (default on this branch), 0 = every tmm_* use in br_glue.c compiles away (behaviour of 1282/1283)
 *   TMM_SLIPPI_FIRST         1 = Slippi Unranked first, expand to Multishine when Slippi gives no match within TMM_SLIPPI_WINDOW_MS (default);
 *                            0 = Multishine first for TMM_OURS_WINDOW_MS, then Slippi
 */
#ifndef MS_TMM
#define MS_TMM 1
#endif
#ifndef TMM_SLIPPI_FIRST
#define TMM_SLIPPI_FIRST 1
#endif
#ifndef TMM_SLIPPI_WINDOW_MS
#define TMM_SLIPPI_WINDOW_MS 20000u      /* Slippi first: how long Slippi gets (from the search start: DNS, STUN, TLS and the queue wait are inside it) */
#endif
#ifndef TMM_OURS_WINDOW_MS
#define TMM_OURS_WINDOW_MS 10000u        /* Multishine first: how long our queue gets */
#endif
#ifndef TMM_LEAVE_WAIT_MS
#define TMM_LEAVE_WAIT_MS 4000u          /* at most this long for the first side to confirm it has left before the second side starts anyway */
#endif

#if defined(__GNUC__)
#define TMM_FN static __attribute__((unused))
#else
#define TMM_FN static
#endif

/* networks */
#define TMM_NET_NONE 0
#define TMM_NET_OURS 1                   /* Multishine (our server, Dojo / Mano a mano) */
#define TMM_NET_SLIPPI 2                 /* Slippi (Unranked / Direct through the 3.7.0 matchmaker) */

/* link, as the wired filter reports it: 1 wired, 2 Wi-Fi, 0 unknown (the numbering of mnc_link_cls) */
#define TMM_LINK_UNKNOWN 0
#define TMM_LINK_WIRED 1
#define TMM_LINK_WIFI 2

/* tiers */
#define TMM_T_GUEST 1                    /* no Multishine account, no Slippi login */
#define TMM_T_ACCOUNT 2                  /* linked Multishine account, no Slippi login */
#define TMM_T_SLIPPI 3                   /* Slippi login on the card */

TMM_FN int tmm_tier(int has_account, int has_slippi)
{
    return has_slippi ? TMM_T_SLIPPI : has_account ? TMM_T_ACCOUNT : TMM_T_GUEST;
}

/* Dojo may search Slippi only for tier 3 on a positively wired link.  Wi-Fi (rule) and an unknown link kind (conservative: less contact with Slippi) stay on Multishine. */
TMM_FN int tmm_dojo_slippi_ok(int tier, int link)
{
    return tier == TMM_T_SLIPPI && link == TMM_LINK_WIRED;
}

/* The Dojo plan: *first / *second are TMM_NET_*; second NONE = a single-network search (tiers 1 and 2, tier 3 on Wi-Fi). */
TMM_FN void tmm_dojo_plan(int tier, int link, int *first, int *second)
{
    if (!tmm_dojo_slippi_ok(tier, link)) { *first = TMM_NET_OURS; *second = TMM_NET_NONE; return; }
#if TMM_SLIPPI_FIRST
    *first = TMM_NET_SLIPPI; *second = TMM_NET_OURS;
#else
    *first = TMM_NET_OURS; *second = TMM_NET_SLIPPI;
#endif
}

TMM_FN unsigned int tmm_window_ms(int first)
{
    return first == TMM_NET_SLIPPI ? (unsigned int)TMM_SLIPPI_WINDOW_MS : (unsigned int)TMM_OURS_WINDOW_MS;
}

/* MANO A MANO CODE ROUTING.  code18 = the code-entry buffer as Slippi's B4 hands it over: up to 9 full-width Shift-JIS characters, 2 bytes each, cut at the first 0.  The separator tells
   the side (codehist.h NAMESPACES): '#' (0x8194) = a Slippi connect code, '@' (0x8197) = a Multishine code.  RULES, in this order:
     1. '#' form AND the card holds a Slippi login AND Slippi may be used for Direct now (slippi_ok)  -> Slippi Direct
     2. everything else ('@' form, no separator, a '#' code without a Slippi login or while Slippi is switched off) -> our own Mano a mano
   Never both: one search goes to one side.  A code of unknown form is NOT tried on Slippi as a fallback (less contact); our server answers 'Connect code not found'. */
TMM_FN int tmm_code_sep(const unsigned char *code18)
{
    unsigned int i;
    for (i = 0; i < 9u; i++) {
        unsigned int hi = code18[2u * i], lo = code18[2u * i + 1u];
        if (hi == 0u && lo == 0u) break;
        if (hi == 0x81u && lo == 0x94u) return '#';
        if (hi == 0x81u && lo == 0x97u) return '@';
    }
    return 0;
}
TMM_FN int tmm_route_code(const unsigned char *code18, int has_slippi, int slippi_ok)
{
    return (tmm_code_sep(code18) == '#' && has_slippi && slippi_ok) ? TMM_NET_SLIPPI : TMM_NET_OURS;
}

/* ---- the Dojo search state machine ----
   Driven by the dispatcher (br_glue.c): tmm_begin at B4, tmm_step on every B3 poll with what the active side reports, tmm_reset at BA / scene change.  It never touches a backend; it returns
   what the dispatcher must do.  Observations of the side that currently searches (from the MSRB state byte): */
#define TMM_O_NONE 0                     /* idle / nothing yet (the first frames after B4 read idle: not a failure) */
#define TMM_O_SEARCHING 1                /* initialising or in the queue */
#define TMM_O_FOUND 2                    /* opponent assigned / connecting / connected */
#define TMM_O_ERROR 3                    /* the side ended with an error */
/* actions */
#define TMM_A_NONE 0
#define TMM_A_LEAVE_FIRST 1              /* cancel the first network now (proper leave on that side); keep answering "searching" to the screen */
#define TMM_A_START_SECOND 2             /* the first side has left (or the wait ran out): start the second network with the same arguments */
/* phases */
#define TMM_P_OFF 0
#define TMM_P_FIRST 1
#define TMM_P_LEAVING 2
#define TMM_P_SECOND 3
#define TMM_P_LOCKED 4                   /* an opponent was found on `net`: no more switching */
/* notes (what the player is told, tmm_note) */
#define TMM_N_NONE 0
#define TMM_N_FOUND_SLIPPI 1             /* "Opponent found on Slippi" */
#define TMM_N_FOUND_OURS 2               /* "Opponent found on Multishine" */
#define TMM_N_SLIPPI_DOWN 3              /* "Slippi not available. Searching Multishine." (the Slippi side ended with an error before any match) */
/* why the first side was left */
#define TMM_W_NONE 0
#define TMM_W_WINDOW 1
#define TMM_W_ERROR 2

struct tmm {
    unsigned char phase, first, second, net, why;
    unsigned int t0, t1;                 /* search start (or second-side start) / leave start, ms */
};

TMM_FN void tmm_reset(struct tmm *t)
{
    t->phase = TMM_P_OFF; t->first = t->second = t->net = TMM_NET_NONE; t->why = TMM_W_NONE; t->t0 = t->t1 = 0u;
}

/* B4: a tiered search starts on `first`; `second` NONE = single network (the machine then only watches for the note). */
TMM_FN void tmm_begin(struct tmm *t, unsigned int now, int first, int second)
{
    t->phase = TMM_P_FIRST; t->first = (unsigned char)first; t->second = (unsigned char)second; t->net = (unsigned char)first;
    t->why = TMM_W_NONE; t->t0 = now; t->t1 = 0u;
}

/* B3 poll.  obs = what the active side reports, left = 1 when the first side has confirmed it left (only read in LEAVING).  Elapsed times use unsigned wrap-safe subtraction. */
TMM_FN int tmm_step(struct tmm *t, unsigned int now, int obs, int left)
{
    switch (t->phase) {
    case TMM_P_FIRST:
        if (obs == TMM_O_FOUND) { t->phase = TMM_P_LOCKED; return TMM_A_NONE; }
        if (t->second == TMM_NET_NONE) return TMM_A_NONE;
        if (obs == TMM_O_ERROR) { t->why = TMM_W_ERROR; t->phase = TMM_P_LEAVING; t->t1 = now; return TMM_A_LEAVE_FIRST; }
        if ((unsigned int)(now - t->t0) >= tmm_window_ms(t->first)) { t->why = TMM_W_WINDOW; t->phase = TMM_P_LEAVING; t->t1 = now; return TMM_A_LEAVE_FIRST; }
        return TMM_A_NONE;
    case TMM_P_LEAVING:
        if (left || (unsigned int)(now - t->t1) >= (unsigned int)TMM_LEAVE_WAIT_MS) {
            t->phase = TMM_P_SECOND; t->net = t->second; t->t0 = now;
            return TMM_A_START_SECOND;
        }
        return TMM_A_NONE;
    case TMM_P_SECOND:
        if (obs == TMM_O_FOUND) t->phase = TMM_P_LOCKED;
        return TMM_A_NONE;
    default:
        return TMM_A_NONE;
    }
}

/* the MSRB state byte of the active side -> an observation (0 idle, 1 initialising, 2 matchmaking, 3 opponent connecting, 4 connected, 5 error) */
TMM_FN int tmm_obs(int rc, unsigned int msrb_state)
{
    if (rc < 0) return TMM_O_NONE;
    if (msrb_state == 1u || msrb_state == 2u) return TMM_O_SEARCHING;
    if (msrb_state == 3u || msrb_state == 4u) return TMM_O_FOUND;
    if (msrb_state == 5u) return TMM_O_ERROR;
    return TMM_O_NONE;
}

/* what the player is told.  Nothing for a search that has no Slippi in it (slippi_possible 0). */
TMM_FN int tmm_note(const struct tmm *t, int slippi_possible)
{
    if (!slippi_possible) return TMM_N_NONE;
    if (t->phase == TMM_P_LOCKED) return t->net == TMM_NET_SLIPPI ? TMM_N_FOUND_SLIPPI : TMM_N_FOUND_OURS;
    if ((t->phase == TMM_P_LEAVING || t->phase == TMM_P_SECOND) && t->first == TMM_NET_SLIPPI && t->why == TMM_W_ERROR) return TMM_N_SLIPPI_DOWN;
    return TMM_N_NONE;
}

TMM_FN const char *tmm_note_text(int note)
{
    return note == TMM_N_FOUND_SLIPPI ? "Opponent found on Slippi" :
           note == TMM_N_FOUND_OURS ? "Opponent found on Multishine" :
           note == TMM_N_SLIPPI_DOWN ? "Slippi not available. Searching Multishine." : 0;
}
#endif
