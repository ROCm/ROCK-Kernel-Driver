/*
 *      MIPL Mobile IPv6 Mobile Node header file
 *
 *      $Id: s.mn.h 1.60 03/09/29 15:13:44+03:00 henkku@mart10.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _MN_H
#define _MN_H

#include <linux/in6.h>

/* constants for sending of BUs*/
#define HA_BU_DEF_LIFETIME 10000
#define CN_BU_DEF_LIFETIME 420 /* Max lifetime for RR bindings from mipv6 draft v19 */  
#define DUMB_CN_BU_LIFETIME 600 /* BUL entry lifetime in case of dumb CN */
#define ROUTER_BU_DEF_LIFETIME 30 /* For packet forwarding from previous coa */
#define ERROR_DEF_LIFETIME DUMB_CN_BU_LIFETIME

extern rwlock_t mn_info_lock;

#define MN_NOT_AT_HOME 0
#define MN_RETURNING_HOME 1
#define MN_AT_HOME 2

/*
 * Mobile Node information record
 */
struct mn_info {
	struct in6_addr home_addr;
	struct in6_addr ha;
	__u8 home_plen;
	__u8 is_at_home;
	__u8 has_home_reg;
	__u8 man_conf;
	int ifindex;
	int ifindex_user; 
	unsigned long home_addr_expires;
	unsigned short dhaad_id;
	struct list_head list;
	spinlock_t lock;
};

/* prototypes for interface functions */
int mipv6_mn_init(void);
void mipv6_mn_exit(void);

struct handoff;

/* Interface to movement detection */
int mipv6_mobile_node_moved(struct handoff *ho);

void mipv6_mn_send_home_na(struct in6_addr *haddr);
/* Init home reg. with coa */
int init_home_registration(struct in6_addr *home_addr, struct in6_addr *coa);

/* mn_info functions that require locking by caller */
struct mn_info *mipv6_mninfo_get_by_home(struct in6_addr *haddr);

struct mn_info *mipv6_mninfo_get_by_ha(struct in6_addr *home_agent);

struct mn_info *mipv6_mninfo_get_by_id(unsigned short id);

/* "safe" mn_info functions */
void mipv6_mninfo_add(int ifindex, struct in6_addr *home_addr, int plen, 
		      int isathome, unsigned long lifetime, struct in6_addr *ha, 
		      int ha_plen, unsigned long ha_lifetime, int man_conf);

int mipv6_mninfo_del(struct in6_addr *home_addr, int del_dyn_only);

void mipv6_mn_set_home_reg(struct in6_addr *home_addr, int has_home_reg);

int mipv6_mn_is_at_home(struct in6_addr *addr);

int mipv6_mn_is_home_addr(struct in6_addr *addr);

__u32 mipv6_mn_get_bulifetime(struct in6_addr *home_addr, 
			      struct in6_addr *coa, __u8 flags);
int mn_cn_handoff(void *rawentry, void *args, unsigned long *sortkey);
#endif /* _MN_H */
