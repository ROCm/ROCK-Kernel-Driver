/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/net/sctp/sctp_protocol.c,v 1.35 2002/08/16 19:30:49 jgrimm Exp $
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
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */
static char *cvs_id __attribute__ ((unused)) = "$Id: sctp_protocol.c,v 1.35 2002/08/16 19:30:49 jgrimm Exp $";

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
sctp_protocol_t sctp_proto;
struct proc_dir_entry	*proc_net_sctp;

/* This is the global socket data structure used for responding to
 * the Out-of-the-blue (OOTB) packets.  A control sock will be created
 * for this socket at the initialization time.
 */
static struct socket *sctp_ctl_socket;

extern struct net_proto_family inet_family_ops;


/* Return the address of the control sock. */
struct sock *sctp_get_ctl_sock(void)
{
	return(sctp_ctl_socket->sk);

} /* sctp_get_ctl_sock() */

/* Set up the proc fs entry for the SCTP protocol. */
void
sctp_proc_init(void)
{	
	if (!proc_net_sctp) {
		struct proc_dir_entry *ent;
		ent = proc_mkdir("net/sctp", 0);
		if (ent) {
			ent->owner = THIS_MODULE;
			proc_net_sctp= ent;
		}
	}

} /* sctp_proc_init() */

/* Clean up the proc fs entry for the SCTP protocol. */
void
sctp_proc_exit(void)
{
	if (proc_net_sctp) {
		proc_net_sctp= NULL;
		remove_proc_entry("net/sctp", 0);
	}

} /* sctp_proc_exit() */



/* Private helper to extract ipv4 address and stash them in
 * the protocol structure. 
 */
static inline void 
sctp_v4_get_local_addr_list(sctp_protocol_t *proto,
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
		addr = t_new(struct sockaddr_storage_list, GFP_KERNEL);
		if (addr) {
			INIT_LIST_HEAD(&addr->list);
			addr->a.v4.sin_family = AF_INET;
			addr->a.v4.sin_port = 0;
			addr->a.v4.sin_addr.s_addr = ifa->ifa_local;
			list_add_tail(&addr->list, &proto->local_addr_list);
		}
	}
	
	read_unlock(&in_dev->lock);
	read_unlock(&inetdev_lock);
	return;

} /* sctp_v4_get_local_addr_list() */

/* Private helper to extract ipv6 address and stash them in
 * the protocol structure.
 * FIXME: Make this an address family function.   
 */
static inline void
sctp_v6_get_local_addr_list(sctp_protocol_t *proto, struct net_device *dev)
{
#ifdef SCTP_V6_SUPPORT
	/* FIXME: The testframe doesn't support this function. */
#ifndef TEST_FRAME  
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *ifp;
	struct sockaddr_storage_list *addr;
	
        read_lock(&addrconf_lock);	
        if ((in6_dev = __in6_dev_get(dev)) == NULL) {
		read_unlock(&addrconf_lock);
		return;
	}
	
	read_lock_bh(&in6_dev->lock);
	for (ifp=in6_dev->addr_list; ifp; ifp=ifp->if_next) {
		/* Add the address to the local list.  */
		addr = t_new(struct sockaddr_storage_list, GFP_KERNEL);
		if (addr) {
			addr->a.v6.sin6_family = AF_INET6;
			addr->a.v6.sin6_port = 0;
			addr->a.v6.sin6_addr = ifp->addr;
			INIT_LIST_HEAD(&addr->list);
			list_add_tail(&addr->list, &proto->local_addr_list);
		} 
	}

	read_unlock_bh(&in6_dev->lock);
	read_unlock(&addrconf_lock);
	
#endif /* TEST_FRAME */
	return;
#endif /* SCTP_V6_SUPPORT */
} /* sctp_v6_get_local_addr_list() */


/* Extract our IP addresses from the system and stash them in the
 * protocol structure.
 */
static void
__sctp_get_local_addr_list(sctp_protocol_t *proto) 
{
	struct net_device *dev;
	
	read_lock(&dev_base_lock);
	
	for (dev=dev_base; dev; dev = dev->next) {
		sctp_v4_get_local_addr_list(proto, dev);
		sctp_v6_get_local_addr_list(proto, dev);
        }
	
	read_unlock(&dev_base_lock);

} /* __sctp_get_local_addr_list() */

 
static void 
sctp_get_local_addr_list(sctp_protocol_t *proto)
{
	long flags __attribute__ ((unused));
 
 	sctp_spin_lock_irqsave(&sctp_proto.local_addr_lock, flags);
 	__sctp_get_local_addr_list(&sctp_proto);
 	sctp_spin_unlock_irqrestore(&sctp_proto.local_addr_lock, flags);       
 
} /* sctp_get_local_addr_list() */


/* Free the existing local addresses. */
static void
__sctp_free_local_addr_list(sctp_protocol_t *proto) 
{
  	struct sockaddr_storage_list *addr;
 	list_t *pos, *temp;
	
	list_for_each_safe(pos, temp, &proto->local_addr_list) {
 		addr = list_entry(pos, struct sockaddr_storage_list, list);
 		list_del(pos);
  		kfree(addr);
  	}
  	
} /* __sctp_free_local_addr_list() */
 
/* Free the existing local addresses. */
static void
sctp_free_local_addr_list(sctp_protocol_t *proto) 
{
 	long flags __attribute__ ((unused));	
 	
 	sctp_spin_lock_irqsave(&proto->local_addr_lock, flags);
 	__sctp_free_local_addr_list(proto);
 	sctp_spin_unlock_irqrestore(&proto->local_addr_lock, flags);
 	
} /* sctp_free_local_addr_list() */


/* Copy the local addresses which are valid for 'scope' into 'bp'.  */
int
sctp_copy_local_addr_list(sctp_protocol_t *proto, sctp_bind_addr_t *bp, 
			  sctp_scope_t scope, int priority, int copy_flags) 
{
	
	struct sockaddr_storage_list *addr;
	int error = 0;
	list_t *pos;
	long flags __attribute__ ((unused));
	
	sctp_spin_lock_irqsave(&proto->local_addr_lock, flags);
 	list_for_each(pos, &proto->local_addr_list) {
 		addr = list_entry(pos, struct sockaddr_storage_list, list);
		if (sctp_in_scope(&addr->a, scope)) {
			/* Now that the address is in scope, check to see if 
			 * the address type is really supported by the local
			 * sock as well as the remote peer.
			 */
			if ((((AF_INET == addr->a.sa.sa_family) 
			      && (copy_flags & SCTP_ADDR4_PEERSUPP)))
				|| (((AF_INET6 == addr->a.sa.sa_family)
				     && (copy_flags & SCTP_ADDR6_ALLOWED) 
				     && (copy_flags & SCTP_ADDR6_PEERSUPP)))) {
				
				error = sctp_add_bind_addr(bp, 
							   &addr->a, 
							   priority);
				if (0 != error) { goto end_copy; }
			}
 		}						 
 	}

end_copy:
	sctp_spin_unlock_irqrestore(&proto->local_addr_lock, flags);

	return(error);
	
} /* sctp_copy_local_addr_list() */


/* Returns the mtu for the given v4 destination address. */
int
sctp_v4_get_dst_mtu(const sockaddr_storage_t *address)
{
	int dst_mtu = SCTP_DEFAULT_MAXSEGMENT;
	
	struct rtable *rt;
	struct rt_key key = { 
		.dst   = address->v4.sin_addr.s_addr,
		.src   = 0,
		.iif   = 0,
		.oif   = 0,
		.tos   = 0,
		.scope = 0
	};
	
	if (ip_route_output_key(&rt, &key)) {
		SCTP_DEBUG_PRINTK("sctp_v4_get_dst_mtu:ip_route_output_key"
				  " failed, returning %d as dst_mtu\n", 
				  dst_mtu);
	} else {
		dst_mtu = rt->u.dst.pmtu;
		SCTP_DEBUG_PRINTK("sctp_v4_get_dst_mtu: "
				  "ip_route_output_key: dev:%s pmtu:%d\n",
				  rt->u.dst.dev->name, dst_mtu);
		ip_rt_put(rt);
	}

	return (dst_mtu);

} /* sctp_v4_get_dst_mtu() */


/* Event handler for inet device events. 
 * Basically, whenever there is an event, we re-build our local address list.
 */
static int
sctp_netdev_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	long flags __attribute__ ((unused));

	sctp_spin_lock_irqsave(&sctp_proto.local_addr_lock, flags);
	__sctp_free_local_addr_list(&sctp_proto);
	__sctp_get_local_addr_list(&sctp_proto);
	sctp_spin_unlock_irqrestore(&sctp_proto.local_addr_lock, flags);

	return NOTIFY_DONE;

} /* sctp_netdev_event() */

/*
 * Initialize the control inode/socket with a control endpoint data
 * structure.  This endpoint is reserved exclusively for the OOTB processing.
 */
int 
sctp_ctl_sock_init(void)
{
	int err = 0;
	int family = PF_INET;

	SCTP_V6(family = PF_INET6;)

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

} /* sctp_ctl_sock_init() */

/* Get the table of functions for manipulating a particular address
 * family.
 */
sctp_func_t *
sctp_get_af_specific(const sockaddr_storage_t *address)
{
	list_t *pos;
        sctp_protocol_t *proto = sctp_get_protocol();
        sctp_func_t *retval, *af;
 
	retval = NULL;

	/* Cycle through all AF specific functions looking for a 
	 * match.
	 */
	list_for_each(pos, &proto->address_families) {
		af = list_entry(pos, sctp_func_t, list);

                if (address->sa.sa_family == af->sa_family) {
                        retval = af;
                        break;
                }
        }

        return retval;

} /* sctp_get_af_specific() */

/* Registration for netdev events. */
struct notifier_block sctp_netdev_notifier = {
	.notifier_call = sctp_netdev_event,
};

/* Socket operations. */
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

/* Registration with AF_INET family. */
struct inet_protosw sctp_protosw =
{
	.type       = SOCK_SEQPACKET,	
	.protocol   = IPPROTO_SCTP,
	.prot       = &sctp_prot,
	.ops        = &inet_seqpacket_ops,
	.capability = -1,
	.no_check   = 0,
	.flags      = SCTP_PROTOSW_FLAG
};

/* Register with IP layer. */
static struct inet_protocol sctp_protocol =
{
	.handler     = sctp_rcv,               /* SCTP input handler.  */
	.err_handler = sctp_v4_err,            /* SCTP error control   */
	.protocol    = IPPROTO_SCTP,           /* protocol ID          */
	.name        = "SCTP"                  /* name                 */
};

/* IPv4 address related functions. */
sctp_func_t sctp_ipv4_specific = {
        .queue_xmit     = ip_queue_xmit,
        .setsockopt     = ip_setsockopt,
        .getsockopt     = ip_getsockopt,
	.get_dst_mtu    = sctp_v4_get_dst_mtu,
	.net_header_len = sizeof(struct iphdr),
        .sockaddr_len   = sizeof(struct sockaddr_in),
	.sa_family      = AF_INET,
}; 


/* Initialize the universe into something sensible.  */
int
sctp_init(void)
{
	int i;
	int status = 0;
	
	/* Add SCTP to inetsw linked list.  */ 
	inet_register_protosw(&sctp_protosw);
	
	/* Add SCTP to inet_protos hash table. */
	inet_add_protocol(&sctp_protocol);

	/* Initialize proc fs directory. */
	sctp_proc_init();

	/* Initialize object count debugging. */
	sctp_dbg_objcnt_init();

	/*
         * 14. Suggested SCTP Protocol Parameter Values
         */
        /* The following protocol parameters are RECOMMENDED: */
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

        /* Valid.Cookie.Life        - 60  seconds
         */
        sctp_proto.valid_cookie_life	= 60 * HZ;

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

	/* Initialize default stream count setup information.  
	 * Note: today the stream accounting data structures are very 
	 * fixed size, so one really does need to make sure that these have 
	 * upper/lower limits when changing. 
	 */
        sctp_proto.max_instreams    = SCTP_MAX_STREAM;
	sctp_proto.max_outstreams   = SCTP_MAX_STREAM;

	/* Allocate and initialize the association hash table. */
	sctp_proto.assoc_hashsize = 4096;
	sctp_proto.assoc_hashbucket = (sctp_hashbucket_t *)
		kmalloc(4096 * sizeof(sctp_hashbucket_t), GFP_KERNEL);
	if (!sctp_proto.assoc_hashbucket) {
 		printk (KERN_ERR "SCTP: Failed association hash alloc.\n");
		status = -ENOMEM;
		goto err_ahash_alloc;		
	}
	for (i = 0; i < sctp_proto.assoc_hashsize; i++) {
		sctp_proto.assoc_hashbucket[i].lock = RW_LOCK_UNLOCKED;
		sctp_proto.assoc_hashbucket[i].chain = NULL;
	}

        /* Allocate and initialize the endpoint hash table. */
	sctp_proto.ep_hashsize = 64;
	sctp_proto.ep_hashbucket = (sctp_hashbucket_t *)
		kmalloc(64 * sizeof(sctp_hashbucket_t), GFP_KERNEL);
	if (!sctp_proto.ep_hashbucket) {
 		printk (KERN_ERR "SCTP: Failed endpoint_hash alloc.\n");
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
 		printk (KERN_ERR "SCTP: Failed bind hash alloc.");
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
	INIT_LIST_HEAD(&sctp_ipv4_specific.list);
	list_add_tail(&sctp_ipv4_specific.list, &sctp_proto.address_families);
	
	status = sctp_v6_init();
	if (status != 0) {
		goto err_v6_init;
	}

	/* Initialize the control inode/socket for handling OOTB packets. */
	if (0 != (status = sctp_ctl_sock_init())) {
 		printk (KERN_ERR 
			"SCTP: Failed to initialize the SCTP control sock.\n");
		goto err_ctl_sock_init;		
	}
        
	/* Initialize the local address list. */
	INIT_LIST_HEAD(&sctp_proto.local_addr_list);
	sctp_proto.local_addr_lock = SPIN_LOCK_UNLOCKED;

	register_inetaddr_notifier(&sctp_netdev_notifier);
	sctp_get_local_addr_list(&sctp_proto);

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
	inet_del_protocol(&sctp_protocol);
	inet_unregister_protosw(&sctp_protosw);
	return status;

} /* sctp_init() */


/* Exit handler for the SCTP protocol. */
void
sctp_exit(void)
{
        /* BUG.  This should probably do something useful like clean
         * up all the remaining associations and all that memory.
         */
	
	/* Free the local address list. */
	unregister_inetaddr_notifier(&sctp_netdev_notifier);
	sctp_free_local_addr_list(&sctp_proto);

	/* Free the control endpoint. */
	sock_release(sctp_ctl_socket);

	sctp_v6_exit();
	sctp_sysctl_unregister();
        list_del(&sctp_ipv4_specific.list);

	kfree(sctp_proto.assoc_hashbucket);
	kfree(sctp_proto.ep_hashbucket);
	kfree(sctp_proto.port_hashtable);
        
	sctp_dbg_objcnt_exit();
	sctp_proc_exit();

	inet_del_protocol(&sctp_protocol);
	inet_unregister_protosw(&sctp_protosw);

} /* sctp_exit() */

module_init(sctp_init);
module_exit(sctp_exit);

MODULE_AUTHOR("Linux Kernel SCTP developers <lksctp-developers@lists.sourceforge.net>");
MODULE_DESCRIPTION("Support for the SCTP protocol (RFC2960)");
MODULE_LICENSE("GPL");

