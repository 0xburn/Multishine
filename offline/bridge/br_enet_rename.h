/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* Second copy of the ENet client (offline/common/sdenet/sdenet.c) for the Slippi bridge inside the online image: the engine's copy is compiled
   without fragments and with a small reliable payload (-DSDENET_NO_FRAGMENTS, SDENET_REL_DATA 192) and must stay byte for byte what it was; the bridge
   needs fragments (a real get-ticket-resp is about 1.6 KB) and SDENET_REL_DATA 512.  Both objects are compiled from the same source with this header
   forced in (-include) so that the bridge's copy exports sbre_* names: no symbol of the engine's copy is touched, no struct is shared. */
#ifndef BR_ENET_RENAME_H
#define BR_ENET_RENAME_H
#define sdenet_init sbre_init
#define sdenet_connect sbre_connect
#define sdenet_send sbre_send
#define sdenet_disconnect sbre_disconnect
#define sdenet_reset_peer sbre_reset_peer
#define sdenet_set_frag_buf sbre_set_frag_buf
#define sdenet_set_allow sbre_set_allow
#define sdenet_rx sbre_rx
#define sdenet_service sbre_service
#define sdenet_state sbre_state
#endif
