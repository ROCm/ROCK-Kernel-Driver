/*
 * $Id: s.ipv6_tunnel.h 1.11 03/09/22 16:45:03+03:00 vnuorval@amber.hut.mediapoli.com $
 */

#ifndef _NET_IPV6_TUNNEL_H
#define _NET_IPV6_TUNNEL_H

#include <linux/ipv6.h>
#include <linux/netdevice.h>
#include <linux/ipv6_tunnel.h>
#include <linux/skbuff.h>
#include <asm/atomic.h>

/* capable of sending packets */
#define IP6_TNL_F_CAP_XMIT 0x10000
/* capable of receiving packets */
#define IP6_TNL_F_CAP_RCV 0x20000

#define IP6_TNL_MAX 128

/* IPv6 tunnel */

struct ip6_tnl {
	struct ip6_tnl *next;	/* next tunnel in list */
	struct net_device *dev;	/* virtual device associated with tunnel */
	struct net_device_stats stat;	/* statistics for tunnel device */
	int recursion;		/* depth of hard_start_xmit recursion */
	struct ip6_tnl_parm parms;	/* tunnel configuration paramters */
	struct flowi fl;	/* flowi template for xmit */
	atomic_t refcnt;        /* nr of identical tunnels used by kernel */
	struct socket *sock;
	struct dst_entry *dst_cache;
	struct module *owner;
};

#define IP6_TNL_PRE_ENCAP 0
#define IP6_TNL_PRE_DECAP 1
#define IP6_TNL_MAXHOOKS 2

#define IP6_TNL_DROP 0
#define IP6_TNL_ACCEPT 1

typedef int ip6_tnl_hookfn(struct ip6_tnl *t, struct sk_buff *skb);

struct ip6_tnl_hook_ops {
	struct list_head list;
	unsigned int hooknum;
	int priority;
	ip6_tnl_hookfn *hook;
};

enum ip6_tnl_hook_priorities {
	IP6_TNL_PRI_FIRST = INT_MIN,
	IP6_TNL_PRI_LAST = INT_MAX
};

/* Tunnel encapsulation limit destination sub-option */

struct ipv6_tlv_tnl_enc_lim {
	__u8 type;		/* type-code for option         */
	__u8 length;		/* option length                */
	__u8 encap_limit;	/* tunnel encapsulation limit   */
} __attribute__ ((packed));

#ifdef __KERNEL__
extern int ip6ip6_tnl_create(struct ip6_tnl_parm *p, struct ip6_tnl **pt);

extern struct ip6_tnl *ip6ip6_tnl_lookup(struct in6_addr *remote,
					 struct in6_addr *local);

void ip6ip6_tnl_change(struct ip6_tnl *t, struct ip6_tnl_parm *p);

extern int ip6ip6_kernel_tnl_add(struct ip6_tnl_parm *p);

extern int ip6ip6_kernel_tnl_del(struct ip6_tnl *t);

extern unsigned int ip6ip6_tnl_inc_max_kdev_count(unsigned int n);

extern unsigned int ip6ip6_tnl_dec_max_kdev_count(unsigned int n);

extern unsigned int ip6ip6_tnl_inc_min_kdev_count(unsigned int n);

extern unsigned int ip6ip6_tnl_dec_min_kdev_count(unsigned int n);

extern void ip6ip6_tnl_register_hook(struct ip6_tnl_hook_ops *reg);

extern void ip6ip6_tnl_unregister_hook(struct ip6_tnl_hook_ops *reg);

#ifdef CONFIG_IPV6_TUNNEL
extern int __init ip6_tunnel_init(void);
extern void ip6_tunnel_cleanup(void);
#endif
#endif
#endif
