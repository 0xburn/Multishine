/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hub_conn.h: when the menu toasts stay hidden because an online session is connected.  The toasts accept with Z (a Direct invite: tap, a friend request: hold 1.0 s), and while a
 * session is connected Slippi's character screen reads Z held as "Hold Z to disconnect" ( A4/A5): a tester dropped out of a Direct match by
 * holding Z on a friend toast.  Needs od_glue.h's phases.  The toast model keeps running (an invite expires on its own clock), the plate is only not drawn and Z not read, so a
 * toast still valid when the session ends is shown then.  Host test hub/tests/hub_conn_test.c.
 */
#ifndef HUB_CONN_H
#define HUB_CONN_H
static __inline__ int hc_connected(unsigned phase) { return phase == OD_PEER || phase == OD_CONNECT || phase == OD_UP; }
#endif
