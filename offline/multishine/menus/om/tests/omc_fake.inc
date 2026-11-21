/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/* A fake memory card behind the CARD SDK entry points om_card.c calls (included into the test after om_card.c).  Two slots, a flat file table,
   fault injection, and the invariants the real SDK has: a channel must be mounted for any call, every pass must end unmounted. */
#include <stdarg.h>
#include <string.h>

#define FK_FILES 8
struct fk_file { int used; char name[33]; unsigned char data[0x2000]; unsigned int commentAddr, iconAddr; unsigned short iconFormat, iconSpeed; };
struct fk_slot {
    int present, sector, mounted;
    int broken;                         /* per-slot: mount says BROKEN (-6), an unformatted / zero-filled card; ENCODING via `enc` */
    int enc;
    struct fk_file f[FK_FILES];
    int free_blocks, free_files;
};
static struct fk_slot K[2];
static struct {
    int idle, fail_mount, fail_read, fail_write, fail_create, corrupt_write, probe_busy, mounts, unmounts, writes, creates, reads, open_files, bad_use, probes;
    int fail_setstat;
} KF;

static void fk_reset(void)
{
    int i;
    memset(K, 0, sizeof K);
    memset(&KF, 0, sizeof KF);
    KF.idle = 1;
    for (i = 0; i < 2; i++) { K[i].sector = 0x2000; K[i].free_blocks = 59 - 5; K[i].free_files = 120; }
}
static struct fk_file *fk_find(int chan, const char *name)
{
    int i;
    for (i = 0; i < FK_FILES; i++) if (K[chan].f[i].used && strcmp(K[chan].f[i].name, name) == 0) return &K[chan].f[i];
    return 0;
}
static struct fk_file *fk_put(int chan, const char *name, const unsigned char *block)
{
    int i;
    for (i = 0; i < FK_FILES; i++) if (!K[chan].f[i].used) {
        K[chan].f[i].used = 1; strncpy(K[chan].f[i].name, name, 32);
        if (block) memcpy(K[chan].f[i].data, block, 0x2000);
        return &K[chan].f[i];
    }
    return 0;
}
int h_melee_idle(void) { return KF.idle; }
void h_VIWaitForRetrace(void) {}
void h_OSReport(const char *fmt, ...) { (void)fmt; }
s32 h_CARDProbeEx(s32 chan, s32 *mem, s32 *sector)
{
    KF.probes++;
    if (KF.probe_busy > 0) { KF.probe_busy--; return R_BUSY; }
    if (!K[chan].present) return R_NOCARD;
    *mem = 59; *sector = K[chan].sector;
    return R_READY;
}
s32 h_CARDMountAsync(s32 chan, void *wa, void *d, void *a)
{
    (void)d; (void)a;
    if (!wa || ((unsigned long)wa & 31)) KF.bad_use++;
    if (K[chan].mounted) KF.bad_use++;
    KF.mounts++;
    if (KF.fail_mount || K[chan].broken) { K[chan].mounted = 1; return -6; }   /* BROKEN leaves it mounted */
    if (K[chan].enc) { K[chan].mounted = 1; return -13; }
    K[chan].mounted = 1;
    return 0;
}
s32 h_CARDSync(s32 chan) { return (KF.fail_mount || K[chan].broken) ? -6 : (K[chan].enc ? -13 : 0); }
s32 h_CARDUnmount(s32 chan) { if (!K[chan].mounted) KF.bad_use++; K[chan].mounted = 0; KF.unmounts++; return 0; }
s32 h_CARDOpen(s32 chan, const char *name, struct card_fi *fi)
{
    struct fk_file *f;
    if (!K[chan].mounted) { KF.bad_use++; return -3; }
    f = fk_find(chan, name);
    if (!f) return R_NOFILE;
    fi->chan = chan; fi->fileNo = (int)(f - K[chan].f);
    KF.open_files++;
    return 0;
}
s32 h_CARDClose(struct card_fi *fi) { (void)fi; KF.open_files--; return 0; }
static void (*fk_read_hook)(void);      /* step 2b tests: runs inside a CARDRead (a second card user trying to take the work area mid-operation) */
s32 h_CARDRead(struct card_fi *fi, void *buf, s32 len, s32 off)
{
    KF.reads++;
    if (fk_read_hook) fk_read_hook();
    if ((len != 0x2000 && len != 0x800) || off != 0 || ((unsigned long)buf & 31)) KF.bad_use++;   /* 0x800 = the icon stamp reading block 0 of MultishineCode */
    if (KF.fail_read) return -5;
    memcpy(buf, K[fi->chan].f[fi->fileNo].data, (size_t)len);
    return 0;
}
s32 h_CARDWrite(struct card_fi *fi, void *buf, s32 len, s32 off)
{
    if (len != 0x2000 || off != 0 || ((unsigned long)buf & 31)) KF.bad_use++;
    if (KF.fail_write) return -5;
    memcpy(K[fi->chan].f[fi->fileNo].data, buf, (size_t)len);
    if (KF.corrupt_write) K[fi->chan].f[fi->fileNo].data[0x1000 + 0x20] ^= 0x40;
    KF.writes++;
    return 0;
}
s32 h_CARDCreateAsync(s32 chan, const char *name, u32 size, struct card_fi *fi, void *cb)
{
    struct fk_file *f;
    (void)cb;
    if (size != 0x2000 || strlen(name) > 31) KF.bad_use++;
    if (KF.fail_create) return -5;
    if (K[chan].free_blocks < 1) return R_INSSPACE;
    if (K[chan].free_files < 1) return R_NOENT;
    f = fk_put(chan, name, 0);
    if (!f) return R_NOENT;
    K[chan].free_blocks--; K[chan].free_files--;
    fi->chan = chan; fi->fileNo = (int)(f - K[chan].f);
    KF.open_files++; KF.creates++;
    return 0;
}
s32 h_CARDGetStatus(s32 chan, s32 fileNo, struct card_stat *st) { (void)chan; (void)fileNo; memset(st, 0, sizeof *st); return 0; }
s32 h_CARDSetStatusAsync(s32 chan, s32 fileNo, struct card_stat *st, void *cb)
{
    (void)cb;
    if (KF.fail_setstat) return -5;
    K[chan].f[fileNo].commentAddr = st->commentAddr; K[chan].f[fileNo].iconAddr = st->iconAddr;
    K[chan].f[fileNo].iconFormat = st->iconFormat; K[chan].f[fileNo].iconSpeed = st->iconSpeed;
    return 0;
}
s32 h_CARDFreeBlocks(s32 chan, s32 *bytes, s32 *files) { *bytes = K[chan].free_blocks * 0x2000; *files = K[chan].free_files; return 0; }
