/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* host fake of the CARD SDK entry points used by om_card.c (included by om_card.c under -DOMC_HOST, after its struct definitions) */
s32 h_CARDProbeEx(s32 chan, s32 *mem, s32 *sector);
s32 h_CARDMountAsync(s32 chan, void *wa, void *detach_cb, void *attach_cb);
s32 h_CARDUnmount(s32 chan);
s32 h_CARDOpen(s32 chan, const char *name, struct card_fi *fi);
s32 h_CARDClose(struct card_fi *fi);
s32 h_CARDRead(struct card_fi *fi, void *buf, s32 len, s32 off);
s32 h_CARDWrite(struct card_fi *fi, void *buf, s32 len, s32 off);
s32 h_CARDCreateAsync(s32 chan, const char *name, u32 size, struct card_fi *fi, void *cb);
s32 h_CARDGetStatus(s32 chan, s32 fileNo, struct card_stat *st);
s32 h_CARDSetStatusAsync(s32 chan, s32 fileNo, struct card_stat *st, void *cb);
s32 h_CARDFreeBlocks(s32 chan, s32 *bytes, s32 *files);
s32 h_CARDSync(s32 chan);
void h_VIWaitForRetrace(void);
void h_OSReport(const char *fmt, ...);
int h_melee_idle(void);
/* ENGINE_MEMORY_BREAKDOWN step 2b host fakes (on_hist.c under -DMS_CARD_EXT_WA): the HSD heap, lbHeap arena_hi and the loader's fixed work area; defined by the test */
extern unsigned int h_arena_hi;
extern unsigned char h_wa_fixed[];
void *h_wa_alloc(u32 n);
void h_wa_free(void *p);
