/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* on_dlog: the network report file "MultishineDiag" (on_card.h odl_*).  #included by on_card.c under -DONS_DLOG after on_set.c, so the CARD work area, the block
 * buffer, card_mount, room_ok, melee_save_slot, slot_unusable and omc_melee_idle are the proven ones of the code-history layer.
 *  - what: ONE 8 KB block, no secrets (no identity, no server key, no public address), the words of ms_net_dbg (glue state, link byte, our addresses, frame counts,
 *    adapter MAC) as plain ASCII lines at 0x1014 so a tester who can copy a GCI off the card can send it and we read it with  (or a hex viewer).
 *  - when: only when the player asks (Settings > Network status > Test connection), from the menu scene, never from a match, never at boot.  The bring-up of the
 *    adapter is NOT touched: the report is the state the engine reached, so a crash before the engine runs cannot be logged (it leaves no file or an old one).
 *  - how: overwrite in place, else create (needs room for Melee's save when that is missing: room_ok).  Read back and compared.  Busy card = 1, no retry loop here.
 * Layout of the payload at 0x1000: "SDDG" (0x1000), u32 version 1 (0x1004), u32 crc32 of the text (0x1008), u32 len of the text (0x100C, 0..ODL_TEXT_MAX), u32 save count since boot (0x1010), text (0x1014). */
#define ODL_NAME "MultishineDiag"
#define ODL_LINE1 "Multishine"
#define ODL_LINE2 "network report, no secrets"
#define ODL_TEXT_OFF 0x1014u
#define ODL_TEXT_MAX 0x7ECu
#define ODL_WORDS 16u                 /* ms_net_dbg fills at least 16 words */

static unsigned g_dl_n OM_DATA;       /* saves since boot */

static unsigned dl_put(char *o, unsigned n, const char *s) { while (*s && n < ODL_TEXT_MAX) o[n++] = *s++; return n; }
static unsigned dl_hex(char *o, unsigned n, unsigned v, unsigned digits)
{
    int i;
    for (i = (int)digits * 4 - 4; i >= 0 && n < ODL_TEXT_MAX; i -= 4) { unsigned d = (v >> i) & 15u; o[n++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); }
    return n;
}
static unsigned dl_dec(char *o, unsigned n, unsigned v)
{
    char t[10];
    unsigned k = 0;
    if (!v) t[k++] = '0';
    while (v && k < sizeof t) { t[k++] = (char)('0' + v % 10u); v /= 10u; }
    while (k && n < ODL_TEXT_MAX) o[n++] = t[--k];
    return n;
}
static unsigned dl_line(char *o, unsigned n, const char *key, unsigned v, unsigned digits)
{
    n = dl_put(o, n, key); n = dl_put(o, n, " "); n = dl_hex(o, n, v, digits);
    return dl_put(o, n, "\n");
}
static unsigned dl_ip(char *o, unsigned n, const char *key, unsigned v)
{
    n = dl_put(o, n, key); n = dl_put(o, n, " ");
    n = dl_dec(o, n, v >> 24); n = dl_put(o, n, "."); n = dl_dec(o, n, (v >> 16) & 255u); n = dl_put(o, n, ".");
    n = dl_dec(o, n, (v >> 8) & 255u); n = dl_put(o, n, "."); n = dl_dec(o, n, v & 255u);
    return dl_put(o, n, "\n");
}

/* the text of one report from the words of ms_net_dbg (see s12_gc.c): glue state (0x80 no adapter id, 0x81 init failed, 0x82 no link), link byte (NWAYS: bit0 10M, bit1 100M),
   dhcp state, stack running, our frame counts, the adapter MAC.  w[12] (public address) is left out on purpose.  Returns the length, no terminating NUL needed. */
unsigned odl_format(char *o, const unsigned *w, unsigned seq)
{
    unsigned n = 0;
    n = dl_put(o, n, "Multishine network report\n");
    n = dl_put(o, n, "seq "); n = dl_dec(o, n, seq); n = dl_put(o, n, "\n");
    n = dl_line(o, n, "state", w[0], 2);
    n = dl_line(o, n, "link", (w[5] >> 16) & 255u, 2);
    n = dl_line(o, n, "dhcp", w[5] & 255u, 2);
    n = dl_line(o, n, "dhcp_tries", (w[5] >> 8) & 255u, 2);
    n = dl_line(o, n, "stack", (w[5] >> 24) & 255u, 2);
    n = dl_ip(o, n, "ip", w[1]);
    n = dl_ip(o, n, "mask", w[2]);
    n = dl_ip(o, n, "gw", w[3]);
    n = dl_ip(o, n, "dns", w[4]);
    n = dl_line(o, n, "tx", w[6], 8);
    n = dl_line(o, n, "rx", w[7], 8);
    n = dl_line(o, n, "dhcp_ack_ms", w[8], 8);
    n = dl_line(o, n, "rx_other", w[9] & 0xFFFFu, 4);
    n = dl_line(o, n, "sock_drop", w[9] >> 16, 4);
    n = dl_put(o, n, "mac "); n = dl_hex(o, n, w[10], 8); n = dl_hex(o, n, w[11] & 0xFFFFu, 4); n = dl_put(o, n, "\n");
    n = dl_line(o, n, "reconnect", w[13], 2);
    n = dl_line(o, n, "exi_id", w[14], 8);                      /* the id the adapter answered when the last bring-up ended (04020200 = a BBA); 0 = not read yet */
    n = dl_line(o, n, "init_rc", w[15], 2);                     /* FF none yet, 00 ok, FF-n = BBA_E_* as unsigned (FF is also -1 = no BBA id) */
    return n;
}

static void dl_put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static void dl_fill(u8 *b, const unsigned *w)
{
    u32 i, len;
    for (i = 0; i < CH_BLOCK; i++) b[i] = 0;
    for (i = 0; i < 31 && ODL_LINE1[i]; i++) b[i] = (u8)ODL_LINE1[i];
    for (i = 0; i < 31 && ODL_LINE2[i]; i++) b[32 + i] = (u8)ODL_LINE2[i];
    len = odl_format((char *)b + ODL_TEXT_OFF, w, g_dl_n + 1u);
    b[0x1000] = 'S'; b[0x1001] = 'D'; b[0x1002] = 'D'; b[0x1003] = 'G';
    dl_put32(b + 0x1004, 1u);
    dl_put32(b + 0x1008, ch_crc32(b + ODL_TEXT_OFF, len));
    dl_put32(b + 0x100C, len);
    dl_put32(b + 0x1010, g_dl_n + 1u);
}

static int dl_write_chan(s32 chan, const unsigned *w, s32 *res)
{
    struct card_stat st;
    s32 r = card_mount(chan);
    int out = -1;
    u8 *b = buf();
    *res = r;
    if (r != 0) return -1;
    r = CARDOpen(chan, ODL_NAME, &g_fi);
    if (r == R_NOFILE && room_ok(chan, &r)) {
        r = CARDCreateAsync(chan, ODL_NAME, CH_BLOCK, &g_fi, CARDSyncCallback);
        if (r >= 0) r = CARDSync(chan);
        if (r == R_READY) {
            dl_fill(b, w);
            r = CARDWrite(&g_fi, b, CH_BLOCK, 0);
            if (r == R_READY) r = CARDGetStatus(chan, g_fi.fileNo, &st);
            if (r == R_READY) {
                st.commentAddr = 0; st.iconAddr = 0xFFFFFFFFu; st.iconFormat = 0; st.iconSpeed = 0; st.bannerFormat = 0;
                r = CARDSetStatusAsync(chan, g_fi.fileNo, &st, CARDSyncCallback);
                if (r >= 0) r = CARDSync(chan);
            }
            CARDClose(&g_fi);
            if (r == R_READY) r = CARDOpen(chan, ODL_NAME, &g_fi);   /* reopen for the read back */
            else goto done;
        }
    }
    if (r == R_READY) {
        dl_fill(b, w);
        r = CARDWrite(&g_fi, b, CH_BLOCK, 0);
        if (r == R_READY) {
            u32 i;
            u8 want[16];
            for (i = 0; i < 16; i++) want[i] = b[0x1000 + i];
            for (i = 0; i < CH_BLOCK; i++) b[i] = 0;
            r = CARDRead(&g_fi, b, CH_BLOCK, 0);
            if (r == R_READY) {
                out = 0;
                for (i = 0; i < 16; i++) if (b[0x1000 + i] != want[i]) out = -1;
            }
        }
        CARDClose(&g_fi);
    }
done:
    *res = r;
    CARDUnmount(chan);
    return out;
}

/* Writes the report from the words of ms_net_dbg: 0 saved and verified, 1 Melee's card layer is busy (try again later, nothing was touched), -1 no card / full / error */
int odl_save(const unsigned *w)
{
    int k, out = -1;
    s32 res = 0, first;
    if (!omc_melee_idle() || !WA_GET(1)) return 1;
    first = melee_save_slot();                                  /* probes the cards: only after the idle check */
    if (first < 0) first = 0;
    for (k = 0; k < 2 && out != 0; k++) {
        s32 chan = k == 0 ? first : 1 - first;
        out = dl_write_chan(chan, w, &res);
    }
    WA_PUT();
    if (out == 0) g_dl_n++;
    return out;
}
