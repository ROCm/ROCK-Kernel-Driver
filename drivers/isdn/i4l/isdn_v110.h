/* Linux ISDN subsystem, V.110 related functions
 *
 * Copyright by Thomas Pfeiffer (pfeiffer@pds.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#ifndef ISDN_V110_H
#define ISDN_V110_H

struct isdn_v110 {
  int               v110emu;             /* V.110 emulator-mode 0=none */
  atomic_t          v110use;             /* Usage-Semaphore for stream */
  isdn_v110_stream  *v110;               /* V.110 private data         */
};

/* 
 * isdn_v110_encode will take raw data and encode it using V.110 
 */
extern struct sk_buff *isdn_v110_encode(isdn_v110_stream *, struct sk_buff *);

/* 
 * isdn_v110_decode receives V.110 coded data from the stream and rebuilds
 * frames from them. The source stream doesn't need to be framed.
 */
extern struct sk_buff *isdn_v110_decode(isdn_v110_stream *, struct sk_buff *);

extern void isdn_v110_open(struct isdn_slot *slot, struct isdn_v110 *iv110);

extern void isdn_v110_close(struct isdn_slot *slot, struct isdn_v110 *iv110);

extern int  isdn_v110_bsent(struct isdn_slot *slot, struct isdn_v110 *iv110);

#endif
