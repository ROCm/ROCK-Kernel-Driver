/*
 *      MIPL Mobile IPv6 Mobility Header send and receive
 *
 *      $Id: s.mobhdr.h 1.51 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _MOBHDR_H
#define _MOBHDR_H

#include <net/mipv6.h>

/* RR states for mipv6_send_bu() */
#define RR_INIT			0x00
#define RR_WAITH		0x01
#define RR_WAITC		0x02
#define RR_WAITHC		0x13
#define RR_DONE			0x10

#define MH_UNKNOWN_CN 1
#define MH_AUTH_FAILED 2
#define MH_SEQUENCE_MISMATCH 3

struct mipv6_bul_entry;

int mipv6_mh_common_init(void);
void mipv6_mh_common_exit(void);
int mipv6_mh_mn_init(void);
void mipv6_mh_mn_exit(void);

struct mipv6_mh_opt {
	struct mipv6_mo_alt_coa		*alt_coa;
	struct mipv6_mo_nonce_indices	*nonce_indices;
	struct mipv6_mo_bauth_data	*auth_data;
	struct mipv6_mo_br_advice	*br_advice;
	int freelen;
	int totlen;
	u8 *next_free;
	u8 data[0];
};

struct mobopt {
	struct mipv6_mo_alt_coa		*alt_coa;
	struct mipv6_mo_nonce_indices	*nonce_indices;
	struct mipv6_mo_bauth_data	*auth_data;
	struct mipv6_mo_br_advice	*br_advice;
};

struct mipv6_mh_opt *alloc_mh_opts(int totlen);
int append_mh_opt(struct mipv6_mh_opt *ops, u8 type, u8 len, void *data);
int parse_mo_tlv(void *mos, int len, struct mobopt *opts);
int mipv6_add_pad(u8 *data, int n);

struct mipv6_auth_parm {
	struct in6_addr *coa;
	struct in6_addr *cn_addr;
	__u8 *k_bu;
};

int send_mh(struct in6_addr *daddr, struct in6_addr *saddr, 
	    u8 msg_type, u8 msg_len, u8 *msg,
	    struct in6_addr *hao_addr, struct in6_addr *rth_addr,
	    struct mipv6_mh_opt *ops, struct mipv6_auth_parm *parm);

int mipv6_mh_register(int type, int (*func)(
	struct in6_addr *, struct in6_addr *, 
	struct in6_addr *, struct in6_addr *, struct mipv6_mh *));

void mipv6_mh_unregister(int type);

int mipv6_send_brr(struct in6_addr *saddr, struct in6_addr *daddr,
		   struct mipv6_mh_opt *ops);

int mipv6_send_bu(struct in6_addr *saddr, struct in6_addr *daddr, 
		  struct in6_addr *coa, __u32 initdelay, 
		  __u32 maxackdelay, __u8 exp, __u8 flags,
		  __u32 lifetime, struct mipv6_mh_opt *ops);

int mipv6_send_be(struct in6_addr *saddr, struct in6_addr *daddr, 
		  struct in6_addr *home, __u8 status);

int mipv6_send_ba(struct in6_addr *saddr, struct in6_addr *daddr,
		  struct in6_addr *coaddr, u8 status, u16 sequence, 
		  u32 lifetime, u8 *k_bu);

/* Binding Authentication Data Option routines */
#define MAX_HASH_LENGTH 20
#define MIPV6_RR_MAC_LENGTH 12

int mipv6_auth_build(struct in6_addr *cn_addr, struct in6_addr *coa, 
		     __u8 *opt, __u8 *aud_data, __u8 *k_bu);

int mipv6_auth_check(struct in6_addr *cn_addr, struct in6_addr *coa, 
		     __u8 *opt, __u8 optlen, struct mipv6_mo_bauth_data *aud, 
		     __u8 *k_bu);
#endif /* _MOBHDR_H */
