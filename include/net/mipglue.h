/*
 *	Glue for Mobility support integration to IPv6
 *
 *	Authors:
 *	Antti Tuominen		<ajtuomin@cc.hut.fi>	
 *
 *	$Id: s.mipglue.h 1.40 03/09/18 15:59:41+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#ifndef _NET_MIPGLUE_H
#define _NET_MIPGLUE_H

#ifndef USE_IPV6_MOBILITY
#if defined(CONFIG_IPV6_MOBILITY) || defined(CONFIG_IPV6_MOBILITY_MODULE)
#define USE_IPV6_MOBILITY
#endif
#endif

/* symbols to indicate whether destination options received should take
 * effect or not (see exthdrs.c, procrcv.c)
 */
#define MIPV6_DSTOPTS_ACCEPT 1
#define MIPV6_DSTOPTS_DISCARD 0

#define MIPV6_IGN_RTR 0
#define MIPV6_ADD_RTR 1
#define MIPV6_CHG_RTR 2

/* MIPV6: Approximate maximum for mobile IPv6 options and headers */
#define MIPV6_HEADERS 48

#ifdef __KERNEL__
#include <linux/sched.h>
#include <linux/in6.h>
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/skbuff.h>

#include <net/mipv6.h>
#include <net/ipv6.h>
#include <net/addrconf.h>

#ifdef USE_IPV6_MOBILITY

/* calls a procedure from mipv6-module */
#define MIPV6_CALLPROC(X) if(mipv6_functions.X) mipv6_functions.X

/* calls a function from mipv6-module, default-value if function not defined
 */
#define MIPV6_CALLFUNC(X,Y) (!mipv6_functions.X)?(Y):mipv6_functions.X

/* sets a handler-function to process a call */
#define MIPV6_SETCALL(X,Y) if(mipv6_functions.X) printk("mipv6: Warning, function assigned twice!\n"); \
                           mipv6_functions.X = Y
#define MIPV6_RESETCALL(X) mipv6_functions.X = NULL

/* pointers to mipv6 callable functions */
struct mipv6_callable_functions {
	void (*mipv6_initialize_dstopt_rcv) (struct sk_buff *skb);
	int (*mipv6_finalize_dstopt_rcv) (int process);
	int (*mipv6_handle_homeaddr) (struct sk_buff *skb, int optoff);
	int (*mipv6_ra_rcv) (struct sk_buff *skb);
	void (*mipv6_icmp_rcv) (struct sk_buff *skb);
	struct ipv6_txoptions * (*mipv6_modify_txoptions) (
		struct sock *sk,
		struct sk_buff *skb, 
		struct ipv6_txoptions *opt,
		struct flowi *fl,
		struct dst_entry **dst);
	void (*mipv6_set_home) (int ifindex, struct in6_addr *homeaddr, int plen,
				struct in6_addr *homeagent, int plen2);
	void (*mipv6_get_home_address) (struct inet6_ifaddr *ifp,
					struct in6_addr *home_addr);
	void (*mipv6_get_care_of_address)(struct in6_addr *homeaddr, 
					  struct in6_addr *coa);
	int (*mipv6_is_home_addr)(struct in6_addr *addr);
	void (*mipv6_change_router)(void);
	void (*mipv6_check_dad)(struct in6_addr *home_addr);      
	void (*mipv6_icmp_handle_homeaddr)(struct sk_buff *skb);
	int (*mipv6_forward)(struct sk_buff *skb);
	int (*mipv6_mn_ha_probe)(struct inet6_ifaddr *ifp, u8 *lladdr);
};

extern struct mipv6_callable_functions mipv6_functions;

extern void mipv6_invalidate_calls(void);

extern int mipv6_handle_dstopt(struct sk_buff *skb, int optoff);

static inline int
ndisc_mip_mn_ha_probe(struct inet6_ifaddr *ifp, u8 *lladdr)
{
	return MIPV6_CALLFUNC(mipv6_mn_ha_probe, 0)(ifp, lladdr);
}

/* Must only be called for HA, no checks here */
static inline int ip6_mipv6_forward(struct sk_buff *skb)
{
	return MIPV6_CALLFUNC(mipv6_forward, 0)(skb);
}

/* 
 * Avoid adding new default routers if the old one is still in use 
 */

static inline  int ndisc_mipv6_ra_rcv(struct sk_buff *skb)
{
	return MIPV6_CALLFUNC(mipv6_ra_rcv, MIPV6_ADD_RTR)(skb);
}

static inline int ipv6_chk_mip_home_addr(struct in6_addr *addr)
{
	return MIPV6_CALLFUNC(mipv6_is_home_addr, 0)(addr);
}

static inline void ndisc_mipv6_change_router(int change_rtr)
{
	if (change_rtr == MIPV6_CHG_RTR)
		MIPV6_CALLPROC(mipv6_change_router)();
}

static inline void ndisc_check_mipv6_dad(struct in6_addr *target)
{
	MIPV6_CALLPROC(mipv6_check_dad)(target);
}

static inline void icmpv6_handle_mipv6_homeaddr(struct sk_buff *skb)
{
	MIPV6_CALLPROC(mipv6_icmp_handle_homeaddr)(skb);
}

static inline void mipv6_icmp_rcv(struct sk_buff *skb)
{
	MIPV6_CALLPROC(mipv6_icmp_rcv)(skb);
}

static inline int tcp_v6_get_mipv6_header_len(void)
{
	return MIPV6_HEADERS;
}

static inline struct in6_addr *
mipv6_get_fake_hdr_daddr(struct in6_addr *hdaddr, struct in6_addr *daddr)
{
	return daddr;
}

static inline void 
addrconf_set_mipv6_mn_home(int ifindex, struct in6_addr *homeaddr, int plen,
			   struct in6_addr *homeagent, int plen2)
{
	MIPV6_CALLPROC(mipv6_set_home)(ifindex, homeaddr, plen, homeagent, plen2);
}

static inline void addrconf_get_mipv6_home_address(struct inet6_ifaddr *ifp, 
						   struct in6_addr *saddr)
{
	MIPV6_CALLPROC(mipv6_get_home_address)(ifp, saddr);
}

static inline struct ipv6_txoptions *
ip6_add_mipv6_txoptions(struct sock *sk, struct sk_buff *skb, 
			struct ipv6_txoptions *opt, struct flowi *fl,
			struct dst_entry **dst)
{
	return MIPV6_CALLFUNC(mipv6_modify_txoptions, opt)(sk, skb, opt, fl, dst); 

}

static inline void
ip6_mark_mipv6_packet(struct ipv6_txoptions *txopt, struct sk_buff *skb)
{
	struct inet6_skb_parm *opt;
	if (txopt) {
		opt = (struct inet6_skb_parm *)skb->cb;
		opt->mipv6_flags = txopt->mipv6_flags;
	}
}

static inline void 
ip6_free_mipv6_txoptions(struct ipv6_txoptions *opt,
			 struct ipv6_txoptions *orig_opt) 
{
	if (opt && opt != orig_opt)
		kfree(opt);
}

#else /* USE_IPV6_MOBILITY */

#define mipv6_handle_dstopt ip6_tlvopt_unknown

static inline int
ndisc_mip_mn_ha_probe(struct inet6_ifaddr *ifp, u8 *lladdr)
{
	return 0;
}

static inline int ip6_mipv6_forward(struct sk_buff *skb)
{
	return 0;
}

static inline int ndisc_mipv6_ra_rcv(struct sk_buff *skb)
{
	return MIPV6_ADD_RTR;
}

static inline int ipv6_chk_mip_home_addr(struct in6_addr *addr)
{
	return 0;
}

static inline void ndisc_mipv6_change_router(int change_rtr) {}

static inline void ndisc_check_mipv6_dad(struct in6_addr *target) {}

static inline void icmpv6_handle_mipv6_homeaddr(struct sk_buff *skb) {}

static inline void mipv6_icmp_rcv(struct sk_buff *skb) {}

static inline int tcp_v6_get_mipv6_header_len(void)
{
	return 0;
}

static inline struct in6_addr *
mipv6_get_fake_hdr_daddr(struct in6_addr *hdaddr, struct in6_addr *daddr)
{
	return hdaddr;
}

static inline void 
addrconf_set_mipv6_mn_home(int ifindex, struct in6_addr *homeaddr, int plen,
			   struct in6_addr *homeagent, int plen2) {}

static inline void addrconf_get_mipv6_home_address(struct inet6_ifaddr *ifp, 
						   struct in6_addr *saddr) {}

static inline struct ipv6_txoptions *
ip6_add_mipv6_txoptions(struct sock *sk, struct sk_buff *skb,
			struct ipv6_txoptions *opt, struct flowi *fl,
			struct dst_entry **dst)
{
	return opt;
}

static inline void
ip6_mark_mipv6_packet(struct ipv6_txoptions *txopt, struct sk_buff *skb) {}

static inline void 
ip6_free_mipv6_txoptions(struct ipv6_txoptions *opt, 
			 struct ipv6_txoptions *orig_opt) {}

#endif /* USE_IPV6_MOBILITY */
#endif /* __KERNEL__ */
#endif /* _NET_MIPGLUE_H */
