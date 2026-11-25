/* SPDX-License-Identifier: GPL-3.0-or-later */
/* LOCAL FORK: rec_up, the upload of a finished game recording (rec/rec_gc.c) to Lucky Stats, part of ms/rep.ovl.  Menus and the idle online character screen only, after the result reports.
 *   POST /api/integrations/v1/console/recordings, one request per piece of at most UP_PIECE bytes on a keep-alive session:
 *     X-Recording: <sha256 of the whole MSR1 stream>   X-Recording-Length   X-Recording-Offset   X-Recording-Age: seconds since the game started (the console clock has no time zone)
 *   answer 200 {"nextOffset":N,...,"complete":true|false}: the server says where to go on, so a lost answer or an interrupted session never sends a finished piece twice and never leaves a hole.
 *   4xx other than 408 / 409 / 425 / 429 can never succeed on a retry: the recording is dropped.  409 = start over at the offset the answer names. */
#ifndef REC_UP_H
#define REC_UP_H
#include "rep.h"
#include "../../rec/rec_gc.h"

#define UP_PATH "/api/integrations/v1/console/recordings"
#define UP_PIECE 0x10000u
#define UP_HEAD 768u
#define UP_RSP 384u
#define UP_FAILS_MAX 8u
#define UP_ANSWER_MS 15000u

struct up_arena { struct sdsec sec; struct sdtls_web web; rep_u8 rx[SDTLS_WEB_RX]; rep_u8 buf[REC_CHUNK]; rep_u8 head[UP_HEAD]; rep_u8 body[UP_RSP]; };
struct up_env {
    struct rep_env *e;                                  /* clock, entropy, dns, scene, key, tcp, anchors: the report client's */
    struct rep_keep *k;                                 /* back-off and the web cool-down are shared: the same host */
    struct rec_pub *(*pending)(void);
    int (*read)(unsigned int seq, unsigned int off, unsigned char *buf);
    void (*forget)(unsigned int seq, int why);
    void *(*get)(rep_u32 n);
    void (*put)(void *p);
};
void up_init(const struct up_env *e);
void up_poll(int allow_heavy);
int up_busy(void);
void up_abort(void);
extern rep_u32 up_st[16];        /* [0] sessions [1] requests [2] answers 200 [3] last status [4] bytes sent [5] uploaded [6] dropped by the server [7] transport failures [8] lost (ARAM) [9] state [10] gave up */
#endif
