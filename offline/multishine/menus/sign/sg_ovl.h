/* Copyright (C) 2026 Nigzel / Bjerreman. SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * On-demand overlay of the sign-in screens: the file format, the RESIDENT loader logic and the interface between
 * the resident image and the overlay.  Pure C89, no libc, no Melee address (the SDK calls come in through `struct sgo_sdk`), host-tested (tests/sgo_test.c).
 *
 * File layout (all big endian, produced by sg_ovl_pack.py from a flat link of the module):
 *   struct sgo_hdr | image (code, rodata, data: image_size bytes) | nreloc x struct sgo_reloc
 * The image is linked flat at some base (header link_base); bss follows the image in memory (zeroed by the loader).
 * Relocation: every absolute 32-bit address in the image is listed (word, or the HA/LO halves of a lis/addi pair); the loader adds the load address.
 * PC-relative branches inside the image need nothing: the image moves as one block.  The overlay calls back into the resident image only through
 * `struct sgo_env` (function pointers), so the image has no import relocations at all.
 */
#ifndef SG_OVL_H
#define SG_OVL_H
#include "../../../net/sdnet.h"
#include "../../../net/tls/sdent.h"
#include "sg_ui.h"

/* pointer <-> 32-bit address: a cast on the console (32-bit pointers); the host test maps its arena into a 32-bit address space */
#ifndef SGO_P2A
#define SGO_P2A(p) ((sgo_u32)(p))
#define SGO_A2P(a) ((sgo_u8 *)(a))
#endif
typedef unsigned char sgo_u8;
typedef unsigned short sgo_u16;
typedef unsigned int sgo_u32;

#define SGO_MAGIC 0x53474F56u          /* 'SGOV' */
#define SGO_VER 5u                    /* 5: the sprite call carries the shown texel size (code + 4 modules of quiet zone) in bits 16-23 of row0, 0 = the whole texture; 4: the sprite call carries its size in rows in the high byte of row0 (Send Slippi Login draws a 21-row QR); 3: the QR texture is 104 x 104 texels and the resident sprite is 16 rows square (SG_QR_TEX_DIM); 2: struct sgo_env gained `upd` (the Update screen's host services); an overlay and a resident of different versions refuse each other */
#define SGO_R_ADDR32 0u                /* 32-bit word holds an absolute address */
#define SGO_R_HA 1u                    /* 16-bit field holds the high half, adjusted for the sign of the low half (lis) */
#define SGO_R_LO 2u                    /* 16-bit field holds the low half (addi, lwz, stw, ...) */

struct sgo_hdr { sgo_u32 magic, ver, image_size, bss_size, nreloc, api_off, link_base, state_hint; };   /* 32 bytes; link_base = the address the image was linked at */
#define SGO_RELOC_BYTES 8u                 /* per entry, big endian: u32 offset << 2 | type, u32 target = the absolute address (as linked at base 0) the field refers to */
#define SGO_ALIGN 32u                      /* DVD reads: offset, length and destination are multiples of 32; the packer pads image and relocs */

/* the resident menu drawing helpers the overlay draws with (in the product these are the ml module's own text_new_at / ml_sis_add text objects on the menu's front
   canvas, the backdrop text and one GX sprite; handles are opaque; every object is freed through `release` before the overlay's block is) */
struct sgo_gfx {
    void *(*backdrop)(void);                                         /* full-screen dark box (an empty text with a background, as ml's stub pages) */
    void *(*text)(unsigned row, unsigned attr, const char *s);       /* one line of menu-font text at grid row `row` (0..25), colour by SG_A_* attribute */
    void *(*sprite)(void *tex, unsigned row0);       /* row0: low byte = first grid row, bits 8-15 = size in grid rows, bits 16-23 = texels shown (square, 0.9 units a row; 0 = 15) */                       /* GX sprite of the I4 texture (SG_QR_TEX_DIM square) below grid row row0, drawn by the menu camera */
    void (*tex_changed)(void *tex, unsigned bytes);                  /* the texture memory was rewritten: flush the data cache, invalidate the GPU texture cache */
    void (*release)(void *handle);                                   /* free a text, a backdrop or a sprite */
};

/* the Settings look: three more drawing calls, reached through env->gfx2 only when env->gfx2magic is SGO_GFX2MAGIC, so SGO_VER stays 4:
   an older resident has neither (the overlay then draws the plain text view without the mark, the bar and the right aligned values) and an older overlay never looks. */
#define SGO_GFX2MAGIC 0x47465832u                  /* 'GFX2' */
struct sgo_gfx2 {
    void *(*mark)(void);                                             /* the small Multishine mark at the top left of the page (a sprite) */
    void *(*bar)(unsigned row, unsigned big);                        /* the selection bar behind the list row at grid `row` (big: a two row list row); make it before the row's text */
    void *(*value)(unsigned row, unsigned attr, const char *s);      /* a text object right aligned at the right margin of grid `row` */
};

#define SGO_UPDMAGIC 0x55504443u                   /* 'UPDC' */
/* what the card-route resident hands the overlay for the Update row: plain pointers only (no header of the update code is needed in the resident image) */
struct sgo_updcfg {
    void *net;                                     /* the resident hub client's mnc (its io is the BBA UDP socket to the server) */
    int (*hello)(unsigned now_ms);                 /* mnc_hello on net, 0 = started; also arms the resident's send queue (sends from the menu thread are queued and flushed by the network slice) */
    int (*poll)(unsigned now_ms);                  /* mnc_poll on net: MNC_BUSY / DONE / FAILED */
    int (*link_up)(void);                          /* 1 when the BBA has an address */
    void *(*alloc)(unsigned n);                    /* HSD heap block, 0 when it does not fit */
    void (*free)(void *p);
    void (*reset)(void);                           /* soft reset, or 0: the screen then asks the player to switch the console off (card route: no software reset is wired yet) */
    const volatile unsigned *cl_status;            /* the loader's status block (CL_STATUS of this card layout) */
    unsigned abi;                                  /* MSCF_ABI */
    unsigned char *(*wa_acquire)(unsigned owner);  /* the shared CARD work area (ms_card_wa_acquire), 0 = refused */
    void (*wa_release)(void);
    volatile unsigned *proof;                      /* 3 resident words (ml_st[61..63], RAM proof): state | reason << 8 | pct << 16, the build on offer or just installed, the detail code; or 0 */
    unsigned *pending;                             /* resident word (0 at boot): the build_seq installed since boot, so a second visit to the screen does not install it again (the overlay's own data is freed on leaving) */
};

struct sgu_host;                                   /* sgu_core.h: what the Update screen needs from the console (wire client, card, heap, reset) */
struct sgo_idcfg;                                  /* below: the identity and options of the Online page */

/* what the resident image gives the overlay: function pointers instead of import relocations */
struct sgo_env {
    sgo_u32 abi;                                   /* SGO_VER */
    sgo_u32 sdnet_size;                            /* sizeof(struct sdnet) of the resident build: the overlay refuses a different layout */
    struct sdnet *net;
    struct sdent *pool;                            /* the resident entropy pool (session level draws for TLS) */
    sgo_u32 (*now_us)(void);
    sgo_u32 (*now_ms)(void);
    sgo_u32 (*csum_add)(sgo_u32 sum, const sgo_u8 *p, sgo_u32 len);
    sgo_u16 (*csum_fold)(sgo_u32 sum);
    int (*tx_ip)(struct sdnet *s, sgo_u8 proto, sgo_u32 dst_ip, sgo_u16 plen);
    sgo_u32 lab_ip;                                /* TLS test target and its pinned key (resident config) */
    sgo_u16 tcp_port, tls_port;
    const sgo_u8 *pin;
    sgo_u32 bulk_bytes;
    const struct sgo_gfx *gfx;
    const struct sgu_host *upd;                    /* the Update row's services, or 0 (a build without the update client: the row says so and does nothing) */
    sgo_u32 updmagic;                              /* SGO_UPDMAGIC when updcfg is valid (card-route resident only; the overlay built with -DSG_UPDHOST checks it, so an older/other resident is never misread) */
    const struct sgo_updcfg *updcfg;               /* card route: the resident's raw services; the OVERLAY builds the Update row's host from them (upd_host.c + upd_card.c are linked into the overlay) */
    /* appended an older resident has neither field, so the overlay reads idcfg only when idmagic is SGO_IDMAGIC */
    sgo_u32 idmagic;
    const struct sgo_idcfg *idcfg;
    /* appended (lab diagnostic only): valid only when pollmagic is SGO_POLLMAGIC */
    sgo_u32 pollmagic;
    void (*net_poll)(struct sdnet *s);             /* one sdnet_poll + drain from the menu thread (EXI lock handled by the resident): the 1 ms busy-poll variant */
    int (*net_begin)(void);                        /* also valid under pollmagic: interrupts off + EXI lock for a stretch of overlay TCP work (1 = entered, 0 = lock busy: skip this frame); the segments the overlay builds went to the NIC unlocked and were lost (cube run of diag 472: every send from the menu thread died, RTO backoff 1/2/4/8 s) */
    void (*net_end)(void);
    /* appended (the Settings look): valid only when gfx2magic is SGO_GFX2MAGIC */
    sgo_u32 gfx2magic;
    const struct sgo_gfx2 *gfx2;
    /* appended (lab/dev netcfg=on builds only, the on-screen network diagnostic): valid only when netmagic is SGO_NETMAGIC; the field exists in every layout but a release resident never sets it */
    sgo_u32 netmagic;
    void (*netdbg)(unsigned *w);                   /* fills 10 words (s12_gc.c ms_net_dbg): glue state, ip, mask, gateway, dns, dhcp, frames sent / received, last DHCP ACK ms, drops */
};
#define SGO_NETMAGIC 0x4E455444u                   /* 'NETD' */
#define SGO_POLLMAGIC 0x504F4C4Cu                  /* 'POLL' */

#define SGO_IDMAGIC 0x49444331u                    /* 'IDC1' */
/* the console identity and the options the Online page needs from the resident (card route): plain pointers and functions only */
struct sgo_idcfg {
    void *cfg;                                     /* the resident struct od_cfg (od_glue.h): server, pinned keys, device id and secret, code, name; the overlay writes the identity fields only
                                                      after the card holds them (link) or after they were cleared on the card (delete) */
    int (*opt)(int id, int v);                     /* MultishineOptions value `id` (ml_set.h MSET_ID_*): v < 0 reads, else writes (saved on the card by the resident's settings layer); -1 = unknown id */
    int (*seed)(unsigned char rec[44], int write); /* on_set.c ons_seed: the stored entropy seed (SDSD, 44 bytes) of MultishineOptions; 1 done, 0 none, -1 not read yet */
    int (*save)(const void *cfg);                  /* on_card.c onc_save_ident: the identity fields of cfg into the card's MultishineOnline record, read back and checked; 0 saved, 1 card busy
                                                      (try again), -1 no usable record on the card, -2 the write or its check failed */
};                                                 /* the hub client (updcfg->net) takes a new identity from the overlay itself (mnc identity fields; the next request drops the old session) */

/* the overlay's entry table: found at hdr.api_off in the image */
struct sgo_api {
    sgo_u32 abi;
    int (*enter)(const struct sgo_env *env);       /* 0 = ready */
    void (*frame)(const struct sdent_pad *p, sgo_u32 tb);      /* once per rendered frame: input, tests, network steps */
    const struct sg_view *(*draw)(void);           /* the view of this frame (host mocks) */
    void (*present)(void);                         /* draw this frame through env->gfx: only rows that changed, the QR texture when the code changed */
    const struct sg *(*state)(void);               /* read only (autopilot, proofs) */
    void (*goto_screen)(sgo_u32 scr);              /* test hook */
    void (*mock)(sgo_u32 scr, sgo_u32 sub, sgo_u32 id_stage, sgo_u32 signed_in, sgo_u32 prov);   /* test hook: put the mock screens in a given state (the tour of the Dolphin run) */
    void (*hide)(void);                            /* free every drawn object now (they are redrawn by the next present): the lab test image clears the heavy backdrop while the TCP test runs */
    int (*quit)(void);                             /* 1 once the player left the hub */
    void (*leave)(void);                           /* free every drawn object through env->gfx, abort a running TLS session, wipe key material, clear state: the caller frees the block after this */
};

/* ---- resident loader ---- */
#define SGO_E_NONE 0
#define SGO_E_FORMAT (-1)
#define SGO_E_NOMEM (-2)
#define SGO_E_READ (-3)
#define SGO_E_ABI (-4)
#define SGO_E_NOFILE (-5)

struct sgo_sdk {                                   /* the retail Dolphin SDK / HSD calls, supplied by the glue (host test: fakes) */
    void *(*alloc)(sgo_u32 n);                     /* OSAllocFromHeap(HSD_GetHeap, n), 0 when the heap cannot give it (never halts) */
    void (*free)(void *p);                         /* OSFreeToHeap */
    int (*open)(const char *path, sgo_u32 *len);   /* DVDConvertPathToEntrynum + DVDFastOpen: 0 ok, <0 no such file */
    int (*read_begin)(void *dst, sgo_u32 len, sgo_u32 offset);   /* DVDReadAsyncPrio, 0 started; all three multiples of 32 */
    int (*read_poll)(void);                        /* 0 busy, 1 done, <0 error */
    void (*close)(void);                           /* DVDClose */
    void (*flush)(void *p, sgo_u32 n);             /* DCStoreRange + ICInvalidateRange */
};

enum { SGO_S_IDLE, SGO_S_HDR, SGO_S_IMG, SGO_S_REL, SGO_S_READY, SGO_S_FAILED };
struct sgo_loader {
    const struct sgo_sdk *sdk;
    sgo_u8 state;
    int err;
    sgo_u8 *hbuf;                                  /* 64-byte scratch block for the header read, freed once the header is parsed */
    struct sgo_hdr hdr;
    sgo_u8 *buf;                                   /* the block: [image | bss, which first holds the reloc table while it is applied] */
    sgo_u32 buf_len, file_len, image_size, bss_size, load_addr, rel_len;
    const struct sgo_api *api;
    sgo_u32 peak_bytes;                            /* the block size: the whole memory cost while inside */
};

/* check a header against the file length */
int sgo_check(const struct sgo_hdr *h, sgo_u32 file_len);
/* fix every listed address of the image for its load address `load` (img is the image there; rel = n raw entries as in the file; the words hold values
   linked at `link_base`: a word gets + (load - link_base), a lis/addi half is recomputed from target + load) */
void sgo_relocate(sgo_u8 *img, const sgo_u8 *rel, sgo_u32 n, sgo_u32 load, sgo_u32 link_base);
/* one-call stages for the resident glue: start (alloc + read begin), step (poll; on done verify, relocate, flush: state READY), release (free; state IDLE) */
int sgo_begin(struct sgo_loader *l, const struct sgo_sdk *sdk, const char *path);
int sgo_step(struct sgo_loader *l);                /* returns the state */
void sgo_release(struct sgo_loader *l);
#endif
