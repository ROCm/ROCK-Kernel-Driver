/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * Initialization/cleanup for SCTP protocol support.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson <karl@athena.chicago.il.us>
 *    Jon Grimm <jgrimm@us.ibm.com>
 *    Sridhar Samudrala <sri@us.ibm.com>
 *    Daisy Chang <daisyc@us.ibm.com>
 *    Ardelle Fan <ardelle.fan@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/sctp/sctp.h>
#include <net/addrconf.h>
#include <net/inet_common.h>

/* Global data structures. */
struct sctp_protocol sctp_proto;
struct proc_dir_entry	*proc_net_sctp;
DEFINE_SNMP_STAT(struct sctp_mib, sctp_statistics);

/* This is the global socket data structure used for responding to
 * the Out-of-the-blue (OOTB) packets.  A control sock will be created
 * for this socket at the initialization time.
 */
static struct socket *sctp_ctl_socket;

static struct sctp_pf *sctp_pf_inet6_specific;
static struct sctp_pf *sctp_pf_inet_specific;
static struct sctp_af *sctp_af_v4_specific;
static struct sctp_af *sctp_af_v6_specific;

extern struct net_proto_family inet_family_ops;

extern int sctp_snmp_proc_init(void);
extern int sctp_snmp_proc_exit(void);

/* Return the address of the control sock. */
struct sock *sctp_get_ctl_sock(void)
{
	return sctp_ctl_socket->sk;
}

/* Set up the proc fs entry for the SCTP protocol. */
__init int sctp_proc_init(void)
{
	int rc = 0;

	if (!proc_net_sctp) {
		struct proc_dir_entry *ent;
		ent = proc_mkdir("net/sctp", 0);
		if (ent) {
			ent->owner = THIS_MODULE;
			proc_net_sctp = ent;
		} else
			rc = -ENOMEM;
	}

	if (sctp_snmp_proc_init())
		rc = -ENOMEM;

	return rc;
}

/* Clean up the proc fs entry for the SCTP protocol. */
void sctp_proc_exit(void)
{

	sctp_snmp_proc_exit();

	if (proc_net_sctp) {
		proc_net_sctp = NULL;
		remove_proc_entry("net/sctp", 0);
	}
}

/* Private helper to extract ipv4 address and stash them in
 * the protocol structure.
 */
static void sctp_v4_copy_addrlist(struct list_head *addrlist,
				  struct net_device *dev)
{
	struct in_device *in_dev;
	struct in_ifaddr *ifa;
	struct sockaddr_storage_list *addr;

	read_lock(&inetdev_lock);
	if ((in_dev = __in_dev_get(dev)) == NULL) {
		read_unlock(&inetdev_lock);
		return;
	}

	read_lock(&in_dev->lock);
	for (ifa = in_dev->ifa_list; ifa; ifa = ifa->ifa_next) {
		/* Add the address to the local list.  */
		addr = t_new(struct sockaddr_storage_list, GFP_ATOMIC);
		if (addr) {
			addr->a.v4.sin_family = AF_INET;
			addr->a.v4.sin_port = 0;
			addr->a.v4.sin_addr.s_addr = ifa->ifa_local;
			list_add_tail(&addr->list, addrlist);
		}
	}

	read_unlock(&in_dev->lock);
	read_unlock(&inetdev_lock);
}

/* Extract our IP addresses from the system and stash them in the
 * protocol structure.
 */
static void __sctp_get_local_addr_list(struct sctp_protocol *proto)
{
	struct net_device *dev;
	struct list_head *pos;
	struct sctp_af *af;

	read_lock(&dev_base_lock);
	for (dev = dev_base; dev; dev = dev->next) {
		list_for_each(pos, &proto->address_families) {
			af = list_entry(pos, struct sctp_af, list);
			af->copy_addrlist(&proto->local_addr_list, dev);
		}
	}
	read_unlock(&dev_base_lock);
}

static void sctp_get_local_addr_list(struct sctp_protocol *proto)
{
	unsigned long flags;

	sctp_spin_lock_irqsave(&sctp_proto.local_addr_lock, flags);
	__sctp_get_local_addr_list(&sctp_proto);
	sctp_spin_unlock_irqrestore(&sctp_proto.local_addr_lock, flags);
}

/* Free the existing local addresses.  */
static void __sctp_free_local_addr_list(struct sctp_protocol *proto)
{
	struct sockaddr_storage_list *addr;
	struct list_head *pos, *temp;

	list_for_each_safe(pos, temp, &proto->local_addr_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		list_del(pos);
		kfree(addr);
	}
}

/* Free the existing local addresses.  */
static void sctp_free_local_addr_list(struct sctp_protocol *proto)
{
	unsigned long flags;

	sctp_spin_lock_irqsave(&proto->local_addr_lock, flags);
	__sctp_free_local_addr_list(proto);
	sctp_spin_unlock_irqrestore(&proto->local_addr_lock, flags);
}

/* Copy the local addresses which are valid for 'scope' into 'bp'.  */
int sctp_copy_local_addr_list(struct sctp_protocol *proto,
			      struct sctp_bind_addr *bp, sctp_scope_t scope,
			      int priority, int copy_flags)
{
	struct sockaddr_storage_list *addr;
	int error = 0;
	struct list_head *pos;
	unsigned long flags;

	sctp_spin_lock_irqsave(&proto->local_addr_lock, flags);
	list_for_each(pos, &proto->local_addr_list) {
		addr = list_entry(pos, struct sockaddr_storage_list, list);
		if (sctp_in_scope(&addr->a, scope)) {
			/* Now that the address is in scope, check to see if
			 * the address type is really supported by the local
			 * sock as well as the remote peer.
			 */
			if ((((AF_INET == addr->a.sa.sa_family) &&
			      (copy_flags & SCTP_ADDR4_PEERSUPP))) ||
			    (((AF_INET6 == addr->a.sa.sa_family) &&
			      (copy_flags & SCTP_ADDR6_ALLOWED) &&
			      (copy_flags & SCTP_ADDR6_PEERSUPP)))) {
				error = sctp_add_bind_addr(bp, &addr->a,
							   priority);
				if (error)
					goto end_copy;
			}
		}
	}

end_copy:
	sctp_spin_unlock_irqrestore(&proto->local_addr_lock, flags);
	return error;
}

/* Initialize a sctp_addr from in incoming skb.  */
static void sctp_v4_from_skb(union sctp_addr *addr, struct sk_buff *skb,
			     int is_saddr)
{
	void *from;
	__u16 *port;
	struct sctphdr *sh;

	port = &addr->v4.sin_port;
	addr->v4.sin_family = AF_INET;

	sh = (struct sctphdr *) skb->h.raw;
	if (is_saddr) {
		*port  = ntohs(sh->source);
		from = &skb->nh.iph->saddr;
	} else {
		*port = ntohs(sh->dest);
		from = &skb->nh.iph->daddr;
	}
	memcpy(&addr->v4.sin_addr.s_addr, from, sizeof(struct in_addr));
}

/* Initialize an sctp_addr from a socket. */
static void sctp_v4_from_sk(union sctp_addr *addr, struct sock *sk)
{
	addr->v4.sin_family = AF_INET;
	addr->v4.sin_port = inet_sk(sk)->num;
	addr->v4.sin_addr.s_addr = inet_sk(sk)->rcv_saddr;
}

/* Initialize sk->rcv_saddr from sctp_addr. */
static void sctp_v4_to_sk(union sctp_addr *addr, struct sock *sk)
{
	inet_sk(sk)->rcv_saddr = addr->v4.sin_addr.s_addr;
}


/* Initialize a sctp_addr from a dst_entry. */
static void sctp_v4_dst_saddr(union sctp_addr *saddr, struct dst_entry *dst,
			      unsigned short port)
{
	struct rtable *rt = (struct rtable *)dst;
	saddr->v4.sin_family = AF_INET;
	saddr->v4.sin_port = port;
	saddr->v4.sin_addr.s_addr = rt->rt_src;
}

/* Compare two addresses exactly. */
static int sctp_v4_cmp_addr(const union sctp_addr *addr1,
			    const union sctp_addr *addr2)
{
	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;
	if (addr1->v4.sin_port != addr2->v4.sin_port)
		return 0;
	if (addr1->v4.sin_addr.s_addr != addr2->v4.sin_addr.s_addr)
		return 0;

	return 1;
}

/* Initialize addr struct to INADDR_ANY. */
static void sctp_v4_inaddr_any(union sctp_addr *addr, unsigned short port)
{
	addr->v4.sin_family = AF_INET;
	addr->v4.sin_addr.s_addr = INADDR_ANY;
	addr->v4.sin_port = port;
}

/* Is this a wildcard address? */
static int sctp_v4_is_any(const union sctp_addr *addr)
{
	return INADDR_ANY == addr->v4.sin_addr.s_addr;
}

/* This function checks if the address is a valid address to be used for
 * SCTP binding.
 *
 * Output:
 * Return 0 - If the address is a non-unicast or an illegal address.
 * Return 1 - If the address is a unicast.
 */
static int sctp_v4_addr_valid(union sctp_addr *addr)
{
	/* Is this a non-unicast address or a unusable SCTP address? */
	if (IS_IPV4_UNUSABLE_ADDRESS(&addr->v4.sin_addr.s_addr))
		return 0;

	return 1;
}

/* Should this be available for binding?   */
static int sctp_v4_available(const union sctp_addr *addr)
{
	int ret = inet_addr_type(addr->v4.sin_addr.s_addr);

	/* FIXME: ip_nonlocal_bind sysctl support. */

	if (addr->v4.sin_addr.s_addr != INADDR_ANY && ret != RTN_LOCAL)
		return 0;
	return 1;
}

/* Checking the loopback, private and other address scopes as defined in
 * RFC 1918.   The IPv4 scoping is based on the draft for SCTP IPv4
 * scoping <draft-stewart-tsvwg-sctp-ipv4-00.txt>.
 *
 * Level 0 - unusable SCTP addresses
 * Level 1 - loopback address
 * Level 2 - link-local addresses
 * Level 3 - private addresses.
 * Level 4 - global addresses
 * For INIT and INIT-ACK address list, let L be the level of
 * of requested destination address, sender and receiver
 * SHOULD include all of its addresses with level greater
 * than or equal to L.
 */
static sctp_scope_t sctp_v4_scope(union sctp_addr *addr)
{
	sctp_scope_t retval;

	/* Should IPv4 scoping be a sysctl configurable option
	 * so users can turn it off (default on) for certain
	 * unconventional networking environments?
	 */

	/* Check for unusable SCTP addresses. */
	if (IS_IPV4_UNUSABLE_ADDRESS(&addr->v4.sin_addr.s_addr)) {
		retval =  SCTP_SCOPE_UNUSABLE;
	} else if (LOOPBACK(addr->v4.sin_addr.s_addr)) {
		retval = SCTP_SCOPE_LOOPBACK;
	} else if (IS_IPV4_LINK_ADDRESS(&addr->v4.sin_addr.s_addr)) {
		retval = SCTP_SCOPE_LINK;
	} else if (IS_IPV4_PRIVATE_ADDRESS(&addr->v4.sin_addr.s_addr)) {
		retval = SCTP_SCOPE_PRIVATE;
	} else {
		retval = SCTP_SCOPE_GLOBAL;
	}

	return retval;
}

/* Returns a valid dst cache entry for the given source and destination ip
 * addresses. If an association is passed, trys to get a dst entry with a
 * source address that matches an address in the bind address list.
 */
struct dst_entry *sctp_v4_get_dst(struct sctp_association *asoc,
				  union sctp_addr *daddr,
				  union sctp_addr *saddr)
{
	struct rtable *rt;
	struct flowi fl;
	sctp_bind_addr_t *bp;
	rwlock_t *addr_lock;
	struct sockaddr_storage_list *laddr;
	struct list_head *pos;
	struct dst_entry *dst = NULL;
	union sctp_addr dst_saddr;

	memset(&fl, 0x0, sizeof(struct flowi));
	fl.fl4_dst  = daddr->v4.sin_addr.s_addr;
	fl.proto = IPPROTO_SCTP;

	if (saddr)
		fl.fl4_src = saddr->v4.sin_addr.s_addr;

	SCTP_DEBUG_PRINTK("%s: DST:%u.%u.%u.%u, SRC:%u.%u.%u.%u - ",
			  __FUNCTION__, NIPQUAD(fl.fl4_dst),
			  NIPQUAD(fl.fl4_src));

	if (!ip_route_output_key(&rt, &fl)) {
		dst = &rt->u.dst;
	}

	/* If there is no association or if a source address is passed, no
	 * more validation is required.
	 */
	if (!asoc || saddr)
		goto out;

	bp = &asoc->base.bind_addr;
	addr_lock = &asoc->base.addr_lock;

	if (dst) {
		/* Walk through the bind address list and look for a bind
		 * address that matches the source address of the returned dst.
		 */
		sctp_read_lock(addr_lock);
		list_for_each(pos, &bp->address_list) {
			laddr = list_entry(pos, struct sockaddr_storage_list,
					   list);
			sctp_v4_dst_saddr(&dst_saddr, dst, bp->port);
			if (sctp_v4_cmp_addr(&dst_saddr, &laddr->a))
				goto out_unlock;
		}
		sctp_read_unlock(addr_lock);

		/* None of the bound addresses match the source address of the
		 * dst. So release it.
		 */
		dst_release(dst);
		dst = NULL;
	}

	/* Walk through the bind address list and try to get a dst that
	 * matches a bind address as the source address.
	 */
	sctp_read_lock(addr_lock);
	list_for_each(pos, &bp->address_list) {
		laddr = list_entry(pos, struct sockaddr_storage_list, list);

		if (AF_INET == laddr->a.sa.sa_family) {
			fl.fl4_src = laddr->a.v4.sin_addr.s_addr;
			if (!ip_route_output_key(&rt, &fl)) {
				dst = &rt->u.dst;
				goto out_unlock;
			}
		}
	}

out_unlock:
	sctp_read_unlock(addr_lock);
out:
	if (dst)
		SCTP_DEBUG_PRINTK("rt_dst:%u.%u.%u.%u, rt_src:%u.%u.%u.%u\n",
			  	  NIPQUAD(rt->rt_dst), NIPQUAD(rt->rt_src));
	else
		SCTP_DEBUG_PRINTK("NO ROUTE\n");

	return dst;
}

/* For v4, the source address is cached in the route entry(dst). So no need
 * to cache it separately and hence this is an empty routine.
 */
void sctp_v4_get_saddr(sctp_association_t *asoc,
		       struct dst_entry *dst,
		       union sctp_addr *daddr,
		       union sctp_addr *saddr)
{

}

/* What interface did this skb arrive on? */
int sctp_v4_skb_iif(const struct sk_buff *skb) 
{
     	return ((struct rtable *)skb->dst)->rt_iif;  
}

/* Create and initialize a new sk for the socket returned by accept(). */ 
struct sock *sctp_v4_create_accept_sk(struct sock *sk,
				      struct sctp_association *asoc)
{
	struct sock *newsk;
	struct inet_opt *inet = inet_sk(sk);
	struct inet_opt *newinet;

	newsk = sk_alloc(PF_INET, GFP_KERNEL, sizeof(struct sctp_sock),
			 sk->slab);
	if (!newsk)
		goto out;

	sock_init_data(NULL, newsk);

	newsk->type = SOCK_STREAM;

	newsk->prot = sk->prot;
	newsk->no_check = sk->no_check;
	newsk->reuse = sk->reuse;

	newsk->destruct = inet_sock_destruct;
	newsk->zapped = 0;
	newsk->family = PF_INET;
	newsk->protocol = IPPROTO_SCTP;
	newsk->backlog_rcv = sk->prot->backlog_rcv;
	
	newinet = inet_sk(newsk);
	newinet->sport = inet->sport;
	newinet->saddr = inet->saddr;
	newinet->rcv_saddr = inet->saddr;
	newinet->dport = asoc->peer.port;
	newinet->daddr = asoc->peer.primary_addr.v4.sin_addr.s_addr;
	newinet->pmtudisc = inet->pmtudisc;
      	newinet->id = 0;
	
	newinet->ttl = sysctl_ip_default_ttl;
	newinet->mc_loop = 1;
	newinet->mc_ttl = 1;
	newinet->mc_index = 0;
	newinet->mc_list = NULL;

#ifdef INET_REFCNT_DEBUG
	atomic_inc(&inet_sock_nr);
#endif

	if (0 != newsk->prot->init(newsk)) {
		inet_sock_release(newsk);
		newsk = NULL;
	}

out:
	return newsk;
}

/* Event handler for inet address addition/deletion events.
 * Basically, whenever there is an event, we re-build our local address list.
 */
static int sctp_inetaddr_event(struct notifier_block *this, unsigned long ev,
			       void *ptr)
{
	unsigned long flags;

	sctp_spin_lock_irqsave(&sctp_proto.local_addr_lock, flags);
	__sctp_free_local_addr_list(&sctp_proto);
	__sctp_get_local_addr_list(&sctp_proto);
	sctp_spin_unlock_irqrestore(&sctp_proto.local_addr_lock, flags);

	return NOTIFY_DONE;
}

/*
 * Initialize the control inode/socket with a control endpoint data
 * structure.  This endpoint is reserved exclusively for the OOTB processing.
 */
int sctp_ctl_sock_init(void)
{
	int err;
	sa_family_t family;

	if (sctp_get_pf_specific(PF_INET6))
		family = PF_INET6;
	else 
		family = PF_INET;

	err = sock_create(family, SOCK_SEQPACKET, IPPROTO_SCTP,
			  &sctp_ctl_socket);
	if (err < 0) {
		printk(KERN_ERR
		       "SCTP: Failed to create the SCTP control socket.\n");
		return err;
	}
	sctp_ctl_socket->sk->allocation = GFP_ATOMIC;
	inet_sk(sctp_ctl_socket->sk)->ttl = MAXTTL;

	return 0;
}

/* Register address family specific functions. */
int sctp_register_af(struct sctp_af *af)
{
	switch (af->sa_family) {
	case AF_INET:
		if (sctp_af_v4_specific)
			return 0;
		sctp_af_v4_specific = af;
		break;
	case AF_INET6:
		if (sctp_af_v6_specific)
			return 0;
		sctp_af_v6_specific = af;
		break;
	default:
		return 0;
	}

	INIT_LIST_HEAD(&af->list);
	list_add_tail(&af->list, &sctp_proto.address_families);
	return 1;
}

/* Get the table of functions for manipulating a particular address
 * family.
 */
struct sctp_af *sctp_get_af_specific(sa_family_t family)
{
	switch (family) {
	case AF_INET:
		return sctp_af_v4_specific;
	case AF_INET6:
		return sctp_af_v6_specific;
	default:
		return NULL;
	}
}

/* Common code to initialize a AF_INET msg_name. */
static void sctp_inet_msgname(char *msgname, int *addr_len)
{
	struct sockaddr_in *sin;

	sin = (struct sockaddr_in *)msgname;
	*addr_len = sizeof(struct sockaddr_in);
	sin->sin_family = AF_INET;
	memset(sin->sin_zero, 0, sizeof(sin->sin_zero));
}

/* Copy the primary address of the peer primary address as the msg_name. */
static void sctp_inet_event_msgname(struct sctp_ulpevent *event, char *msgname,
				    int *addr_len)
{
	struct sockaddr_in *sin, *sinfrom;

	if (msgname) {
		sctp_inet_msgname(msgname, addr_len);
		sin = (struct sockaddr_in *)msgname;
		sinfrom = &event->asoc->peer.primary_addr.v4;
		sin->sin_port = htons(event->asoc->peer.port);
		sin->sin_addr.s_addr = sinfrom->sin_addr.s_addr;
	}
}

/* Initialize and copy out a msgname from an inbound skb. */
static void sctp_inet_skb_msgname(struct sk_buff *skb, char *msgname, int *len)
{
	struct sctphdr *sh;
	struct sockaddr_in *sin;

	if (msgname) {
		sctp_inet_msgname(msgname, len);
		sin = (struct sockaddr_in *)msgname;
		sh = (struct sctphdr *)skb->h.raw;
		sin->sin_port = sh->source;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
	}
}

/* Do we support this AF? */
static int sctp_inet_af_supported(sa_family_t family)
{
	/* PF_INET only supports AF_INET addresses. */
	return (AF_INET == family);
}

/* Address matching with wildcards allowed. */
static int sctp_inet_cmp_addr(const union sctp_addr *addr1,
			      const union sctp_addr *addr2,
			      struct sctp_opt *opt)
{
	/* PF_INET only supports AF_INET addresses. */
	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;
	if (INADDR_ANY == addr1->v4.sin_addr.s_addr ||
	    INADDR_ANY == addr2->v4.sin_addr.s_addr)
		return 1;
	if (addr1->v4.sin_addr.s_addr == addr2->v4.sin_addr.s_addr)
		return 1;

	return 0;
}

/* Verify that provided sockaddr looks bindable.  Common verification has
 * already been taken care of.
 */
static int sctp_inet_bind_verify(struct sctp_opt *opt, union sctp_addr *addr)
{
	return sctp_v4_available(addr);
}

/* Verify that sockaddr looks sendable.  Common verification has already 
 * been taken care of.
 */
static int sctp_inet_send_verify(struct sctp_opt *opt, union sctp_addr *addr)
{
	return 1;
}

/* Fill in Supported Address Type information for INIT and INIT-ACK
 * chunks.  Returns number of addresses supported.
 */
static int sctp_inet_supported_addrs(const struct sctp_opt *opt, 
				     __u16 *types)
{
	types[0] = SCTP_PARAM_IPV4_ADDRESS;
	return 1;
}

/* Wrapper routine that calls the ip transmit routine. */
static inline int sctp_v4_xmit(struct sk_buff *skb,
			       struct sctp_transport *transport, int ipfragok)
{
	SCTP_DEBUG_PRINTK("%s: skb:%p, len:%d, "
			  "src:%u.%u.%u.%u, dst:%u.%u.%u.%u\n",
			  __FUNCTION__, skb, skb->len,
			  NIPQUAD(((struct rtable *)skb->dst)->rt_src),
			  NIPQUAD(((struct rtable *)skb->dst)->rt_dst));

	SCTP_INC_STATS(SctpOutSCTPPacks);
	return ip_queue_xmit(skb, ipfragok);
}

struct sctp_af sctp_ipv4_specific;

static struct sctp_pf sctp_pf_inet = {
	.event_msgname = sctp_inet_event_msgname,
	.skb_msgname   = sctp_inet_skb_msgname,
	.af_supported  = sctp_inet_af_supported,
	.cmp_addr      = sctp_inet_cmp_addr,
	.bind_verify   = sctp_inet_bind_verify,
	.send_verify   = sctp_inet_send_verify,
	.supported_addrs = sctp_inet_supported_addrs,
	.create_accept_sk = sctp_v4_create_accept_sk,
	.af            = &sctp_ipv4_specific,
};

/* Notifier for inetaddr addition/deletion events.  */
struct notifier_block sctp_inetaddr_notifier = {
	.notifier_call = sctp_inetaddr_event,
};

/* Socket operations.  */
struct proto_ops inet_seqpacket_ops = {
	.family      = PF_INET,
	.release     = inet_release,       /* Needs to be wrapped... */
	.bind        = inet_bind,
	.connect     = inet_dgram_connect,
	.socketpair  = sock_no_socketpair,
	.accept      = inet_accept,
	.getname     = inet_getname,      /* Semantics are different.  */
	.poll        = sctp_poll,
	.ioctl       = inet_ioctl,
	.listen      = sctp_inet_listen,
	.shutdown    = inet_shutdown,     /* Looks harmless.  */
	.setsockopt  = inet_setsockopt,   /* IP_SOL IP_OPTION is a problem. */
	.getsockopt  = inet_getsockopt,
	.sendmsg     = inet_sendmsg,
	.recvmsg     = inet_recvmsg,
	.mmap        = sock_no_mmap,
	.sendpage    = sock_no_sendpage,
};

/* Registration with AF_INET family.  */
static struct inet_protosw sctp_seqpacket_protosw = {
	.type       = SOCK_SEQPACKET,
	.protocol   = IPPROTO_SCTP,
	.prot       = &sctp_prot,
	.ops        = &inet_seqpacket_ops,
	.capability = -1,
	.no_check   = 0,
	.flags      = SCTP_PROTOSW_FLAG
};
static struct inet_protosw sctp_stream_protosw = {
	.type       = SOCK_STREAM,
	.protocol   = IPPROTO_SCTP,
	.prot       = &sctp_prot,
	.ops        = &inet_seqpacket_ops,
	.capability = -1,
	.no_check   = 0,
	.flags      = SCTP_PROTOSW_FLAG
};

/* Register with IP layer.  */
static struct inet_protocol sctp_protocol = {
	.handler     = sctp_rcv,
	.err_handler = sctp_v4_err,
	.no_policy   = 1,
};

/* IPv4 address related functions.  */
struct sctp_af sctp_ipv4_specific = {
	.sctp_xmit      = sctp_v4_xmit,
	.setsockopt     = ip_setsockopt,
	.getsockopt     = ip_getsockopt,
	.get_dst	= sctp_v4_get_dst,
	.get_saddr	= sctp_v4_get_saddr,
	.copy_addrlist  = sctp_v4_copy_addrlist,
	.from_skb       = sctp_v4_from_skb,
	.from_sk        = sctp_v4_from_sk,
	.to_sk          = sctp_v4_to_sk,
	.dst_saddr      = sctp_v4_dst_saddr,
	.cmp_addr       = sctp_v4_cmp_addr,
	.addr_valid     = sctp_v4_addr_valid,
	.inaddr_any     = sctp_v4_inaddr_any,
	.is_any         = sctp_v4_is_any,
	.available      = sctp_v4_available,
	.scope          = sctp_v4_scope,
	.skb_iif        = sctp_v4_skb_iif,
	.net_header_len = sizeof(struct iphdr),
	.sockaddr_len   = sizeof(struct sockaddr_in),
	.sa_family      = AF_INET,
};

struct sctp_pf *sctp_get_pf_specific(sa_family_t family) {

	switch (family) {
	case PF_INET:
		return sctp_pf_inet_specific;
	case PF_INET6:
		return sctp_pf_inet6_specific;
	default:
		return NULL;
	}
}

/* Register the PF specific function table.  */
int sctp_register_pf(struct sctp_pf *pf, sa_family_t family)
{
	switch (family) {
	case PF_INET:
		if (sctp_pf_inet_specific)
			return 0;
		sctp_pf_inet_specific = pf;
		break;
	case PF_INET6:
		if (sctp_pf_inet6_specific)
			return 0;
		sctp_pf_inet6_specific = pf;
		break;
	default:
		return 0;
	}
	return 1;
}

static int __init init_sctp_mibs(void)
{
	int i;

	sctp_statistics[0] = kmalloc_percpu(sizeof (struct sctp_mib),
					    GFP_KERNEL);
	if (!sctp_statistics[0])
		return -ENOMEM;
	sctp_statistics[1] = kmalloc_percpu(sizeof (struct sctp_mib),
					    GFP_KERNEL);
	if (!sctp_statistics[1]) {
		kfree_percpu(sctp_statistics[0]);
		return -ENOMEM;
	}

	/* Zero all percpu versions of the mibs */
	for (i = 0; i < NR_CPUS; i++) {
		if (cpu_possible(i)) {
			memset(per_cpu_ptr(sctp_statistics[0], i), 0,
					sizeof (struct sctp_mib));
			memset(per_cpu_ptr(sctp_statistics[1], i), 0,
					sizeof (struct sctp_mib));
		}
	}
	return 0;

}

static void cleanup_sctp_mibs(void)
{
	kfree_percpu(sctp_statistics[0]);
	kfree_percpu(sctp_statistics[1]);
}

/* Initialize the universe into something sensible.  */
__init int sctp_init(void)
{
	int i;
	int status = 0;

	/* SCTP_DEBUG sanity check. */
	if (!sctp_sanity_check())
		return -EINVAL;

	/* Add SCTP to inet_protos hash table.  */
	if (inet_add_protocol(&sctp_protocol, IPPROTO_SCTP) < 0)
		return -EAGAIN;

	/* Add SCTP(TCP and UDP style) to inetsw linked list.  */
	inet_register_protosw(&sctp_seqpacket_protosw);
	inet_register_protosw(&sctp_stream_protosw);

	/* Allocate and initialise sctp mibs.  */
	status = init_sctp_mibs();
	if (status)
		goto err_init_mibs;

	/* Initialize proc fs directory.  */
	sctp_proc_init();

	/* Initialize object count debugging.  */
	sctp_dbg_objcnt_init();

	/* Initialize the SCTP specific PF functions. */
	sctp_register_pf(&sctp_pf_inet, PF_INET);
	/*
	 * 14. Suggested SCTP Protocol Parameter Values
	 */
	/* The following protocol parameters are RECOMMENDED:  */
	/* RTO.Initial              - 3  seconds */
	sctp_proto.rto_initial		= SCTP_RTO_INITIAL;
	/* RTO.Min                  - 1  second */
	sctp_proto.rto_min	 	= SCTP_RTO_MIN;
	/* RTO.Max                 -  60 seconds */
	sctp_proto.rto_max 		= SCTP_RTO_MAX;
	/* RTO.Alpha                - 1/8 */
	sctp_proto.rto_alpha	        = SCTP_RTO_ALPHA;
	/* RTO.Beta                 - 1/4 */
	sctp_proto.rto_beta		= SCTP_RTO_BETA;

	/* Valid.Cookie.Life        - 60  seconds */
	sctp_proto.valid_cookie_life	= 60 * HZ;

	/* Whether Cookie Preservative is enabled(1) or not(0) */
	sctp_proto.cookie_preserve_enable = 1;

	/* Max.Burst		    - 4 */
	sctp_proto.max_burst = SCTP_MAX_BURST;

	/* Association.Max.Retrans  - 10 attempts
	 * Path.Max.Retrans         - 5  attempts (per destination address)
	 * Max.Init.Retransmits     - 8  attempts
	 */
	sctp_proto.max_retrans_association = 10;
	sctp_proto.max_retrans_path	= 5;
	sctp_proto.max_retrans_init	= 8;

	/* HB.interval              - 30 seconds */
	sctp_proto.hb_interval		= 30 * HZ;

	/* Implementation specific variables. */

	/* Initialize default stream count setup information. */
	sctp_proto.max_instreams    = SCTP_DEFAULT_INSTREAMS;
	sctp_proto.max_outstreams   = SCTP_DEFAULT_OUTSTREAMS;

	/* Allocate and initialize the association hash table.  */
	sctp_proto.assoc_hashsize = 4096;
	sctp_proto.assoc_hashbucket = (sctp_hashbucket_t *)
		kmalloc(4096 * sizeof(sctp_hashbucket_t), GFP_KERNEL);
	if (!sctp_proto.assoc_hashbucket) {
		printk(KERN_ERR "SCTP: Failed association hash alloc.\n");
		status = -ENOMEM;
		goto err_ahash_alloc;
	}
	for (i = 0; i < sctp_proto.assoc_hashsize; i++) {
		sctp_proto.assoc_hashbucket[i].lock = RW_LOCK_UNLOCKED;
		sctp_proto.assoc_hashbucket[i].chain = NULL;
	}

	/* Allocate and initialize the endpoint hash table.  */
	sctp_proto.ep_hashsize = 64;
	sctp_proto.ep_hashbucket = (sctp_hashbucket_t *)
		kmalloc(64 * sizeof(sctp_hashbucket_t), GFP_KERNEL);
	if (!sctp_proto.ep_hashbucket) {
		printk(KERN_ERR "SCTP: Failed endpoint_hash alloc.\n");
		status = -ENOMEM;
		goto err_ehash_alloc;
	}

	for (i = 0; i < sctp_proto.ep_hashsize; i++) {
		sctp_proto.ep_hashbucket[i].lock = RW_LOCK_UNLOCKED;
		sctp_proto.ep_hashbucket[i].chain = NULL;
	}

	/* Allocate and initialize the SCTP port hash table.  */
	sctp_proto.port_hashsize = 4096;
	sctp_proto.port_hashtable = (sctp_bind_hashbucket_t *)
		kmalloc(4096 * sizeof(sctp_bind_hashbucket_t), GFP_KERNEL);
	if (!sctp_proto.port_hashtable) {
		printk(KERN_ERR "SCTP: Failed bind hash alloc.");
		status = -ENOMEM;
		goto err_bhash_alloc;
	}

	sctp_proto.port_alloc_lock = SPIN_LOCK_UNLOCKED;
	sctp_proto.port_rover = sysctl_local_port_range[0] - 1;
	for (i = 0; i < sctp_proto.port_hashsize; i++) {
		sctp_proto.port_hashtable[i].lock = SPIN_LOCK_UNLOCKED;
		sctp_proto.port_hashtable[i].chain = NULL;
	}

	sctp_sysctl_register();

	INIT_LIST_HEAD(&sctp_proto.address_families);
	sctp_register_af(&sctp_ipv4_specific);

	status = sctp_v6_init();
	if (status)
		goto err_v6_init;

	/* Initialize the control inode/socket for handling OOTB packets.  */
	if ((status = sctp_ctl_sock_init())) {
		printk (KERN_ERR
			"SCTP: Failed to initialize the SCTP control sock.\n");
		goto err_ctl_sock_init;
	}

	/* Initialize the local address list. */
	INIT_LIST_HEAD(&sctp_proto.local_addr_list);
	sctp_proto.local_addr_lock = SPIN_LOCK_UNLOCKED;

	/* Register notifier for inet address additions/deletions. */
	register_inetaddr_notifier(&sctp_inetaddr_notifier);

	sctp_get_local_addr_list(&sctp_proto);

	__unsafe(THIS_MODULE);
	return 0;

err_ctl_sock_init:
	sctp_v6_exit();
err_v6_init:
	sctp_sysctl_unregister();
	list_del(&sctp_ipv4_specific.list);
	kfree(sctp_proto.port_hashtable);
err_bhash_alloc:
	kfree(sctp_proto.ep_hashbucket);
err_ehash_alloc:
	kfree(sctp_proto.assoc_hashbucket);
err_ahash_alloc:
	sctp_dbg_objcnt_exit();
	sctp_proc_exit();
	cleanup_sctp_mibs();
err_init_mibs:
	inet_del_protocol(&sctp_protocol, IPPROTO_SCTP);
	inet_unregister_protosw(&sctp_seqpacket_protosw);
	inet_unregister_protosw(&sctp_stream_protosw);
	return status;
}

/* Exit handler for the SCTP protocol.  */
__exit void sctp_exit(void)
{
	/* BUG.  This should probably do something useful like clean
	 * up all the remaining associations and all that memory.
	 */

	/* Unregister notifier for inet address additions/deletions. */
	unregister_inetaddr_notifier(&sctp_inetaddr_notifier);

	/* Free the local address list.  */
	sctp_free_local_addr_list(&sctp_proto);

	/* Free the control endpoint.  */
	sock_release(sctp_ctl_socket);

	sctp_v6_exit();
	sctp_sysctl_unregister();
	list_del(&sctp_ipv4_specific.list);

	kfree(sctp_proto.assoc_hashbucket);
	kfree(sctp_proto.ep_hashbucket);
	kfree(sctp_proto.port_hashtable);

	sctp_dbg_objcnt_exit();
	sctp_proc_exit();
	cleanup_sctp_mibs();

	inet_del_protocol(&sctp_protocol, IPPROTO_SCTP);
	inet_unregister_protosw(&sctp_seqpacket_protosw);
	inet_unregister_protosw(&sctp_stream_protosw);
}

module_init(sctp_init);
module_exit(sctp_exit);

MODULE_AUTHOR("Linux Kernel SCTP developers <lksctp-developers@lists.sourceforge.net>");
MODULE_DESCRIPTION("Support for the SCTP protocol (RFC2960)");
MODULE_LICENSE("GPL");
