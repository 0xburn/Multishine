/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * optengine: the in-game options engine (approach B "patch table").
 *
 * Portable C (no Melee or SDK calls): the same file builds for the GameCube image and for the host unit tests.
 *
 * The builder patches every toggleable module into the DOL, then writes, for every hook word an option
 * controls, one table entry {addr, on, off} and leaves the DOL word at the PATCHED DEFAULT state.  So the DOL as loaded is
 * already "applied" for the defaults and nothing runs at boot.  opt_apply rewrites only words whose current value differs
 * from the wanted one, so it is idempotent and never touches a word that already holds the right value.
 *
 * Entry kinds:
 *   OPT_K_BOOL   word = (val[gate] != 0) ? on: off
 *   OPT_K_IMM16  word = (val[gate] != 0) ? (on & 0xFFFF0000) | val[valopt] : off      (an `li rX,N` inside our own payload)
 *   OPT_K_MIN    word = (val[gate] >= valopt) ? on: off     (a level of a multi-state option: L-cancel flash 2 = + white)
 * Option values outside [min,max] are replaced by the default (opt_sanitize) before anything is written.
 */
#ifndef OPTENGINE_H
#define OPTENGINE_H

typedef unsigned char opt_u8;
typedef unsigned short opt_u16;
typedef unsigned int opt_u32;

#define OPT_MAGIC 0x53444F50u /* 'SDOP' */
#define OPT_VERSION 1
#define OPT_MAX 16
#define OPT_TABLE_MAX 48

/* option ids (the order of the value bytes; never renumber, the card file stores them by id) */
enum {
    OPT_WIDESCREEN = 0,   /* Optional: Widescreen 16:9 (13 records) */
    OPT_NO_SHAKE = 1,     /* Optional: Disable Screen Shake */
    OPT_FD_FREEZE = 2,    /* Optional: Freeze Final Destination background */
    OPT_CENTER_HUD = 3,   /* Optional: Center Align 2P HUD */
    OPT_FLASH_RED = 4,    /* L-cancel flash: 0 off, 1 miss (Slippi's optional red flash), 2 miss + hit (also our white flash,
                             offline/codeset/asm/Training/LCancelFlashWhite.asm); old cards hold 0/1 = the same meaning */
    OPT_FRIENDLY = 5,     /* Optional: Show Friendly Player Indicators */
    OPT_UCF = 6,          /* UCF 0.84 group (9 modules, toggled together; default on = Slippi) */
    OPT_DELAY = 7,        /* online delay in frames 1..15 (Slippi default 2) */
    OPT_OFFLINE_DELAY = 8,/* also apply OPT_DELAY offline (Slippi's "Apply Delay to all In-Game Scenes"); default off */
    OPT_QUICK_CHAT = 9,   /* 0 on, 1 direct only, 2 off (Slippi's SlippiEnableQuickChat encoding) */
    OPT_HITBOX = 10,      /* hitbox / hurtbox view in Training Mode (offline/codeset/asm/Training/HitboxView.asm) */
    OPT_TECH_CARD = 11,   /* tech card on the Vs results screen + Melee's results screen back (offline/codeset/asm/Training/
                             TechCard*.asm); training image only, default off = Slippi's flow */
    OPT_QUICK_RESET = 12, /* quick position reset in Training;
                             training image only, default off = D-pad untouched */
    OPT_COUNT = 13
};

enum { OPT_K_BOOL = 0, OPT_K_IMM16 = 1, OPT_K_MIN = 2 };

struct opt_entry {        /* 16 bytes, big-endian on the console */
    opt_u32 addr;
    opt_u32 on;
    opt_u32 off;
    opt_u8 gate;          /* option whose value != 0 selects `on` */
    opt_u8 valopt;        /* OPT_K_IMM16: option whose value goes into the low 16 bits; OPT_K_MIN: the lowest value that
                             selects `on` */
    opt_u8 kind;
    opt_u8 flags;         /* bit0: word lies in a data section (no icache line needed, harmless either way) */
};

/* The options block, at a fixed offset of the image (+0x40).  The builder fills magic..table_n, dflt, minv, maxv, val. */
struct opt_block {
    opt_u32 magic;
    opt_u8 version;
    opt_u8 count;         /* OPT_COUNT */
    opt_u8 table_n;       /* entries used in the table */
    opt_u8 dirty;         /* set by the menu when val[] changed since the last apply */
    opt_u8 val[OPT_MAX];      /* wanted values */
    opt_u8 applied[OPT_MAX];  /* values the DOL/RAM words currently reflect (builder: = dflt) */
    opt_u8 dflt[OPT_MAX];     /* patched defaults */
    opt_u8 minv[OPT_MAX];
    opt_u8 maxv[OPT_MAX];
    opt_u32 apply_count;  /* opt_apply calls that ran */
    opt_u32 words_written;/* total words rewritten */
    opt_u32 last_written; /* words rewritten by the last apply */
    opt_u32 sanitized;    /* values reset to default by opt_sanitize */
    opt_u32 ticks;        /* GameCube glue: tick calls */
    opt_u32 menu_opens;
    opt_u32 menu_state;   /* 0 closed, 1 open */
    opt_u32 cursor;
    opt_u32 text_ok;      /* last text object created (pointer, 0 none) */
    opt_u32 last_scene;
    opt_u32 chat_last_id; /* reserved for the quick-chat glue */
    opt_u32 status;       /* bit0 table ok, bit1 last apply ok, bit2 menu suppressed input this frame, bit3 chat table ok,
                             bit4 card write pending, bit5 boot read ran, bit6 boot apply ok */
    /* M2 persistence (optstore.h); +0x88 */
    opt_u8 card_raw[OPT_MAX]; /* value bytes as stored in the card file (0xFF = use the build default) */
    opt_u8 rd_status;     /* OPT_RD_* of the boot read */
    opt_u8 rd_slot;       /* 0 none, 1 slot A, 2 slot B: where our file is (read at boot or written since) */
    opt_u8 wr_status;     /* OPT_WR_* of the last write */
    opt_u8 wr_slot;       /* slot of the last write attempt that reached a card (0 none) */
    opt_u32 rd_res;       /* (SDK result slot A & 0xFFFF) << 16 | (SDK result slot B & 0xFFFF) of the boot read */
    opt_u32 wr_res;       /* last SDK result of the write */
    opt_u32 card_reads;   /* boot read passes */
    opt_u32 card_writes;  /* successful writes */
    opt_u32 wr_tries;     /* write attempts (pending writes that ran) */
    opt_u32 card_ticks;   /* TB ticks of the last card pass */
    opt_u32 card_wa;      /* builder: address of the 0xC000-byte card buffer (patch reserve: work area + one block) */
};

/* boot read result (rd_status) */
enum {
    OPT_RD_NOTRUN = 0, OPT_RD_RUNNING = 1, OPT_RD_OK = 2, OPT_RD_NOCARD = 3, OPT_RD_NOFILE = 4, OPT_RD_DAMAGED = 5,
    OPT_RD_NEWER = 6, OPT_RD_ERROR = 7, OPT_RD_BUSY = 8
};
/* write result (wr_status) */
enum {
    OPT_WR_NONE = 0, OPT_WR_PENDING = 1, OPT_WR_OK = 2, OPT_WR_NOCARD = 3, OPT_WR_FULL = 4, OPT_WR_RESERVE = 5,
    OPT_WR_NEWER = 6, OPT_WR_ERROR = 7
};

struct opt_mem {          /* memory access used by opt_apply (GameCube: direct + cache maintenance; host: a fake RAM) */
    opt_u32 (*read)(void* ctx, opt_u32 addr);
    void (*write)(void* ctx, opt_u32 addr, opt_u32 word, int data);
    void* ctx;
};

int opt_block_ok(const struct opt_block* b);
int opt_sanitize(struct opt_block* b);
opt_u32 opt_entry_word(const struct opt_block* b, const struct opt_entry* e);
int opt_apply(struct opt_block* b, const struct opt_entry* t, const struct opt_mem* m);
/* menu helpers: value step with wrap inside [min,max] */
void opt_step(struct opt_block* b, int opt, int dir);

#endif
