/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* chat_link: see chat_link.h.  Original work, no third-party code. */
#include "chat_link.h"
#include "chat_ticket.h"

static void zero(void *p, cc_u32 n)
{
    cc_u8 *b = (cc_u8 *)p;
    while (n--) *b++ = 0;
}

void chat_link_init(struct chat_link *l, cc_u32 setting, const char own[CP_N][CC_TXT])
{
    int i, k;
    zero(l, sizeof *l);
    l->setting = setting;
    l->direct = 1;
    l->local_idx = 0;
    l->remote_idx = 1;
    cc_net_reset(&l->net);
    cc_subs_clear(&l->subs);
    cc_strings_clear(&l->str);
    for (i = 0; i < CP_N; i++) {               /* our texts: sanitised again (the image block may have been patched) */
        const char *src = own && own[i][0] ? own[i] : cp_default_text[i];
        int n = 0;
        while (src[n] && n < CC_TXT - 1) n++;
        if (cc_sanitize((const cc_u8 *)src, (cc_u32)n, l->own[i]) == 0)
            for (k = 0; k < CC_TXT && (l->own[i][k] = cp_default_text[i][k]) != 0; k++) { }
    }
}

void chat_link_new_session(struct chat_link *l, int local_idx, int remote_idx)
{
    cc_net_reset(&l->net);                     /* a new netplay client: no selection, nothing sent */
    l->session = 1;
    l->local_idx = local_idx & 3;
    l->remote_idx = remote_idx & 3;
}

void chat_link_end_session(struct chat_link *l)
{
    cc_net_reset(&l->net);
    cc_ui_close(&l->ui);
    l->session = 0;
}

static void own_into(struct chat_link *l, int pidx)
{
    int i, k;
    for (i = 0; i < CP_N; i++)
        for (k = 0; k < CC_TXT; k++) l->str.text[pidx][i][k] = l->own[i][k];
    l->str.have[pidx] = 1;
}

int chat_link_strings(struct chat_link *l, const cc_u8 *json, cc_u32 len)
{
    struct ct_result r;
    int rc = ct_read_players(json, len, &l->str, &r);   /* clears first; a malformed reply leaves everybody on defaults */
    if (rc < 0) cc_strings_clear(&l->str);
    own_into(l, l->local_idx);
    return rc;
}

void chat_link_set_name(struct chat_link *l, int pidx, const char *name)
{
    int n = 0, k = 0;
    if (pidx < 0 || pidx > 3) return;
    while (name && name[k] && n < 15) {
        if (cc_drawable((cc_u8)name[k])) l->name[pidx][n++] = name[k];
        k++;
    }
    l->name[pidx][n] = 0;
}

int chat_link_enabled(const struct chat_link *l) { return cc_enabled(l->setting, l->direct); }

int chat_link_frame(struct chat_link *l, cc_u32 pressed, cc_u8 out[CC_WIRE_LEN], int *tx_len)
{
    cc_s32 id = 0;
    int f = cc_ui_frame(&l->ui, pressed, cc_can_send_local(&l->subs, 1), &id);
    *tx_len = 0;
    if (f & CC_UI_OPENED) l->ui_opens++;
    if (f & CC_UI_BLOCKED) l->blocked++;
    if (f & CC_UI_SENT) {
        *tx_len = cc_send(&l->net, id, chat_link_enabled(l), l->session, (cc_u8)l->local_idx, out);
        if (*tx_len) l->sent++;
    }
    return f;
}

int chat_link_rx(struct chat_link *l, const cc_u8 *d, cc_u32 len) { return cc_on_packet(&l->net, d, len); }

int chat_link_b3(struct chat_link *l, cc_u8 *msrb, cc_u8 reply[CC_WIRE_LEN])
{
    struct cc_poll_out o;
    int r;
    if (!l->session) {                         /* Slippi: `if (slippi_netplay)`: the bytes stay 0 */
        if (msrb) msrb[CL_MSRB_USER_ID] = msrb[CL_MSRB_OPP_ID] = msrb[CL_MSRB_PIDX] = 0;
        return 0;
    }
    r = cc_poll(&l->net, chat_link_enabled(l), (cc_u8)l->local_idx, (cc_u8)l->remote_idx, &o, reply);
    if (r) l->replies++;
    if (msrb) {
        msrb[CL_MSRB_USER_ID] = o.usr_id;
        msrb[CL_MSRB_OPP_ID] = o.opp_id;
        msrb[CL_MSRB_PIDX] = o.pidx;
    }
    if (cc_subs_from_poll(&l->subs, &o, (cc_u8)l->local_idx) >= 0) l->shown++;
    return r;
}

int chat_link_tick(struct chat_link *l) { return cc_subs_tick(&l->subs); }

int chat_link_row(const struct chat_link *l, int i, cc_u8 *raw, int cap, int *pidx, int *local)
{
    const struct cc_sub *e;
    if (i < 0 || i >= l->subs.n || i >= CC_VISIBLE) return 0;
    e = &l->subs.e[i];
    *pidx = e->pidx;
    *local = e->local;
    cc_line(raw, cap, l->name[e->pidx & 3], e->pidx, cc_text_for(&l->str, e->pidx, e->id), e->id);
    return 1;
}
