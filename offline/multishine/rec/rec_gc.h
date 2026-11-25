/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LOCAL FORK: the game recorder's console half (rec_gc.c, part of s12_gc.c): what the upload side (menus/rep) sees of it. */
#ifndef REC_GC_H
#define REC_GC_H
#define REC_CHUNK 4096u
/* one finished recording.  Resident, so an upload interrupted by a match or a scene change goes on where the server says it stopped. */
struct rec_pub {
    unsigned int used, seq, off, len, t_ms, frames, raw;             /* off = its place in the ARAM block; t_ms = engine clock at the game's start */
    unsigned int sent, sha_ok, fails;                                /* the upload side's: bytes the server confirmed, sha valid, failed sessions */
    unsigned char sha[32];
};
/* the oldest finished recording not uploaded yet (0 = none, or a match is being recorded) */
struct rec_pub *ms_rec_pending(void);
/* REC_CHUNK bytes (or the rest) at `off` (a multiple of REC_CHUNK) of recording `seq` into buf: n = bytes copied (verified against the CRC taken in the match), 0 = not yet (call again next pass),
   -1 = gone (its ARAM was overwritten).  One reader at a time. */
int ms_rec_read(unsigned int seq, unsigned int off, unsigned char *buf);
void ms_rec_forget(unsigned int seq, int why);    /* uploaded (why 0) or given up */
#endif
