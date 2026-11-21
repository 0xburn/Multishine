/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* The MAC of the network interface the Wii is really using (Wi-Fi or the USB LAN adapter), for Settings > Network > Adapter (pure, host-tested:
 * ). The user binds THIS MAC in the router.
 *
 * The chip MAC the guest's BBA driver reads at init is the console's Wi-Fi MAC (ncd GetWirelessMacAddress, early); the MAC of the interface IOS actually
 * brings up is known only when the address is up (ip/top getinterfaceopt 0x1004, about 30 s into the boot).  The vBBA in Nintendont (Multishine patch 0027)
 * publishes it into four uncached shared words (ARM 0x13026680 = PPC 0xD3026680) the moment ip/top answers; this reader prefers it when it is there.
 * Nothing here exists on a GameCube (real BBA): the words are only read when the OS bus clock word says Wii (>= 200 MHz: 162 MHz on a GameCube / Dolphin,
 * 243 MHz on a Wii), so a GameCube never touches 0xD3000000, and without the magic / the valid flag the caller keeps the adapter's own MAC.
 *   w0 'VBMC' 0x56424D43      w1 sequence: odd while the ARM writes, even when stable (0 = nothing published)
 *   w2 = m0<<24 | m1<<16 | m2<<8 | m3      w3 = m4<<24 | m5<<16 | kind<<8 | 1  (kind: 1 wireless, 2 wired, 0 unknown; bit 0 = valid) */
#ifndef S12_MACPUB_H
#define S12_MACPUB_H
#define MACPUB_ADDR 0xD3026680u
#define MACPUB_MAGIC 0x56424D43u
#define MACPUB_BUS_WII 200000000u

/* b = the four words (volatile, not touched on a GameCube), bus = the OS bus clock word 0x800000F8; returns 1 and fills mac[6] / *kind when a stable valid MAC is published */
static __inline__ int ms_macpub_read(const volatile unsigned int *b, unsigned int bus, unsigned char *mac, unsigned int *kind)
{
    unsigned int s1, s2, w2, w3;
    if (bus < MACPUB_BUS_WII)
        return 0;
    if (b[0] != MACPUB_MAGIC)
        return 0;
    s1 = b[1];
    if ((s1 & 1u) || !s1)
        return 0;                                            /* being written, or nothing published */
    w2 = b[2];
    w3 = b[3];
    s2 = b[1];
    if (s2 != s1 || !(w3 & 1u))
        return 0;                                            /* changed while we read, or no valid flag */
    mac[0] = (unsigned char)(w2 >> 24); mac[1] = (unsigned char)(w2 >> 16); mac[2] = (unsigned char)(w2 >> 8); mac[3] = (unsigned char)w2;
    mac[4] = (unsigned char)(w3 >> 24); mac[5] = (unsigned char)(w3 >> 16);
    if (kind)
        *kind = (w3 >> 8) & 255u;
    return 1;
}
/* WII ON WI-FI: the Slippi modes are closed only for a VALID published kind 1 (wireless); nothing published, kind 0 (unknown) or 2 (wired) and a
   GameCube (ms_macpub_read 0) stay open.  ok = ms_macpub_read's result. */
static __inline__ int ms_macpub_wifi(int ok, unsigned int kind) { return ok && kind == 1u; }
#endif
