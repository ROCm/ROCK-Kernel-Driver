/*
 *      MIPL Mobile IPv6 Binding Update List header file
 *
 *      $Id: s.bul.h 1.27 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _BUL_H
#define _BUL_H

#include "hashlist.h"

#define ACK_OK          0x01
#define RESEND_EXP      0x02
#define ACK_ERROR       0x04

#define HOME_RESEND_EXPIRE 3600
#define MIPV6_COOKIE_LEN 8
struct mipv6_rr_info {
	/* RR information */
	u16 rr_state;                /* State of the RR */
	u16 rr_flags;                /* Flags for the RR */
	u8 hot_cookie[MIPV6_COOKIE_LEN];    /* HoT Cookie */
	u8 cot_cookie[MIPV6_COOKIE_LEN];    /* CoT Cookie */
	u8 home_cookie[MIPV6_COOKIE_LEN];   /* Home Cookie */
	u8 careof_cookie[MIPV6_COOKIE_LEN]; /* Careof Cookie */
	u32 lastsend_hoti;	      /* When HoTI was last sent (jiffies) */
	u32 lastsend_coti;            /* When CoTI was last sent (jiffies) */
	u32 home_time;	              /* when Care-of cookie was received */
	u32 careof_time;	      /* when Home cookie was received */
	int home_nonce_index;         /* Home cookie nonce index */
	int careof_nonce_index;       /* Care-of cookie nonce index */
	u8 *kbu;                      /* Binding authentication key */
};
struct mipv6_bul_entry {
	struct hashlist_entry e;
	struct in6_addr cn_addr;	/* CN to which BU was sent */
	struct in6_addr home_addr;	/* home address of this binding */
	struct in6_addr coa;		/* care-of address of the sent BU */

	unsigned long expire;		/* entry's expiration time (jiffies) */ 
	__u32 lifetime;			/* lifetime sent in this BU */
	__u32 lastsend;			/* last time when BU sent (jiffies) */
	__u32 consecutive_sends;	/* Number of consecutive BU's sent */
	__u16 seq;			/* sequence number of the latest BU */
	__u8 flags;			/* BU send flags */
	__u8 state;			/* resend state */
	__u32 initdelay;		/* initial ack wait */
	__u32 delay;			/* current ack wait */
	__u32 maxdelay;			/* maximum ack wait */

	struct mipv6_rr_info *rr;
	struct mipv6_mh_opt *ops;	/* saved option values */

	unsigned long callback_time;
	int (*callback)(struct mipv6_bul_entry *entry);
};

extern rwlock_t bul_lock;

int mipv6_bul_init(__u32 size);

void mipv6_bul_exit(void);

struct mipv6_bul_entry *mipv6_bul_add(
	struct in6_addr *cn_addr, struct in6_addr *home_addr,
	struct in6_addr *coa, __u32 lifetime, __u16 seq, __u8 flags,
	int (*callback)(struct mipv6_bul_entry *entry), __u32 callback_time,
	__u8 state, __u32 delay, __u32 maxdelay, struct mipv6_mh_opt *ops,
	struct mipv6_rr_info *rr);

int mipv6_bul_delete(struct in6_addr *cn_addr, struct in6_addr *home_addr);

int mipv6_bul_exists(struct in6_addr *cnaddr, struct in6_addr *home_addr);

struct mipv6_bul_entry *mipv6_bul_get(struct in6_addr *cnaddr,
				      struct in6_addr *home_addr);
struct mipv6_bul_entry *mipv6_bul_get_by_ccookie(struct in6_addr *cn_addr,
						 u8 *cookie);

void mipv6_bul_reschedule(struct mipv6_bul_entry *entry);

int mipv6_bul_iterate(int (*func)(void *, void *, unsigned long *), void *args);

#endif /* BUL_H */
