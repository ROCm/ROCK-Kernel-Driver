/*
 *  Immunix NetDomain
 *
 *  Network (INET/INET6) confinement of processes
 *
 *  Original 2.2 (non LSM) version by Chris Wright.
 *
 *  Rewritten for 2.4 (LSM) by Tony Jones, tony@immunix.com
 *  Ported to 2.6 by Tony Jones, tony@immunix.com
 *
 *  Copyright 2004 Immunix Inc
 * 
 */

/* ISSUES:
 *
 * 1:  Linux 2.6
 * Due to certain LSM hooks not being accepted into 2.6 there is a bug
 * in accept processing whereby task->security is overwritten after 
 * being allocated.   In order to solve this we need to hook into the
 * ipv{4/6}_specific.syn_recv_sock vtable.
 *
 * The exact details of the problem is that the standard syn recv handler
 * tcp_v4_syn_recv_sock calls tcp_create_openreq_child which overwrites (via 
 * memcpy) the sk->security previously allocated by security_sk_alloc (via 
 * sk_alloc). The 2.4 code used to preserve sk->security around the memcpy.
 *
 *
 * 2:  Incoming packets/connections:
 * Due to lack of good LSM networking hooks (in 2.4LSM and 2.6) the choices
 * for processing incoming UDP packets (and TCP incoming connections) is to:
 * a) perform the operation in soft interrupt context
 * b) patch into protocol specific vtables 
 *
 * The problem with interrupt context processing is no decision about which
 * task should receive the information has not yet been made, therefore it is
 * problematic (due to changehat, profile replacement, multiple tasks in
 * differing domains sharing a single labelled socket) to do sain mediation.
 * 
 * The old soft intr context code is still present (NETDOMAIN_UDPSOFTINTR)
 * but is disabled. Instead, for incoming UDP we patch into dgram_ops.recvmsg 
 * for each proto and then check the request in process context. If Netdomain
 * rejects data it is necessary to "undo" the effects of copying the buffer
 * to userspace and to avoid copying remote endpoint information to userpspace.
 * Downside of this approach is that the data is lost and cannot be presented
 * to another task (assuming of course there is one, unlikely) and also that
 * if the task was awakened via select and it is a blocking socket, we block
 * the recv.   This later issue isn't terrible as per recent lkml discussion
 * a similar situation (select wakes process, read blocks) can occur if the
 * udp packet is corrupt.
 *
 * Netdomain authentication of incoming TCP connections (SYN) still takes 
 * place in soft interrupt context.  A restriction is imposed that the name
 * of the profile labelling the socket (name of profile in effect when 
 * confined task created socket) must equal the name of the profile of the
 * confined task calling accept. 
 */

#include <linux/version.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define SK_FAMILY(sk) (sk)->sk_family
#define SK_SECURITY(sk) (sk)->sk_security
#define SK_STATE(sk) (sk)->sk_state
#define SK_TYPE(sk) (sk)->sk_type
#define SK_RCV_SADDR(sk) inet_sk((sk))->rcv_saddr
#define SK_SPORT(sk) inet_sk((sk))->sport
#define SK_DADDR(sk) inet_sk((sk))->daddr
#define SK_DPORT(sk) inet_sk((sk))->dport
#define SK
#else
#define SK_FAMILY(sk) (sk)->family
#define SK_SECURITY(sk) (sk)->security
#define SK_STATE(sk) (sk)->state
#define SK_TYPE(sk) (sk)->type
#define SK_RCV_SADDR(sk) (sk)->rcv_saddr
#define SK_SPORT(sk) (sk)->sport
#define SK_DADDR(sk) (sk)->daddr
#define SK_DPORT(sk) (sk)->dport
#endif

/*
 * ND lock for maintaining list of netdomains (struct sock) using a profile
 */
static rwlock_t nd_lock = RW_LOCK_UNLOCKED;
#define ND_RLOCK      read_lock(&nd_lock)
#define ND_RUNLOCK    read_unlock(&nd_lock)
#define ND_WLOCK      write_lock(&nd_lock)
#define ND_WUNLOCK    write_unlock(&nd_lock)


#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
int ipv6_enabled;
static struct socket *ipv6_sockp;
#endif

/* For hooking into dgram_recvmsg vtables if we are NOT doing interrupt
 * processing of incoming UDP. See Issue #2 above.
 */
#ifndef NETDOMAIN_UDPSOFTINTR
extern struct proto_ops inet_dgram_ops;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
int (*old_ipv4_dgram_recvmsg) (struct kiocb *iocb, struct socket *sock,
                               struct msghdr *m, size_t total_len,
                               int flags) = NULL;
#else
int (*old_ipv4_dgram_recvmsg) (struct socket *sock, struct msghdr *m, 
			       int total_len, int flags, 
			       struct scm_cookie *scm) = NULL;
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
int (*old_ipv6_dgram_recvmsg) (struct kiocb *iocb, struct socket *sock,
                               struct msghdr *m, size_t total_len,
                               int flags) = NULL;
#else
int (*old_ipv6_dgram_recvmsg) (struct socket *sock, struct msghdr *m, 
                               int total_len, int flags, 
                               struct scm_cookie *scm) = NULL;
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#endif // defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#endif // ifndef NETDOMAIN_UDPSOFTINTR


/* For 2.6 systems. Allowing us to hook into ipv{4,6}_specific vtables.
 * See Issue #1 above.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
/* defined and exported in net/ipv4/tcp_ipv4.c */
extern struct tcp_func ipv4_specific;

struct sock* (*old_ipv4_syn_recv_sock) (struct sock *sk,
			       	    	 struct sk_buff *skb,
				    	 struct open_request *req,
				    	 struct dst_entry *dst) = NULL;

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
struct sock * (*old_ipv6_syn_recv_sock) (struct sock *sk,
			       	    	 struct sk_buff *skb,
				    	 struct open_request *req,
				    	 struct dst_entry *dst) = NULL;
#endif // defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#ifdef NETDOMAIN_SKUSERS
static inline void nd_skusers_add(struct netdomain *);
static inline void nd_skusers_del(struct netdomain *);
#endif



/* GLOBAL FUNCTIONS */

#ifdef NETDOMAIN_SKUSERS
void nd_skusers_exch(struct sdprofile *old, struct sdprofile *new, int all)
{
	struct list_head *lh, *tmp; 
	
	if (!old){
		return;
	}

	ND_WLOCK;

	list_for_each_safe(lh, tmp, &old->sk_users){
		struct netdomain *nd = list_entry(lh, struct netdomain, sk_users_next);

		BUG_ON(!nd_is_valid(nd));
		BUG_ON(nd->active != old);

		/* If !all, just look at sockets which may be referenced by
		 * soft interrupt context -- these are the ones where for
		 * incoming data we cannot auto detect that profile is out of 
		 * date 
		 */
		if (all || SK_TYPE(nd->sk) == SOCK_DGRAM || 
		    (SK_TYPE(nd->sk) == SOCK_STREAM && 
		      nd->tcpmode == nd_mode_listening)){

			struct sdprofile *oldprofile;

			ND_DEBUG("%s: Replacing profile for sock %p profile %s(%p) replacing with %p\n",
				__FUNCTION__,
				nd->sk,
				nd->active ? nd->active->name : "null",
				nd->active,
				new);
				
			oldprofile=nd->active;

			if (new){
				nd->active=get_sdprofile(new);
				list_move(lh, &new->sk_users);
			}else{
				BUG_ON(lh != &nd->sk_users_next);

				nd->active=NULL;
				nd_skusers_del(nd);
			}

			/* Have to put after we remove from sk_users
			 * else we may trigger BUG check in free_sdprofile
			 */
			put_sdprofile(oldprofile);
		}
	}

	ND_WUNLOCK;
}
#endif // NETDOMAIN_SKUSERS

/* UTILITY FUNCTIONS */

#ifdef NETDOMAIN_SKUSERS
static inline void nd_skusers_add(struct netdomain *nd)
{
	list_add(&nd->sk_users_next, &nd->active->sk_users);
}

static inline void nd_skusers_del(struct netdomain *nd)
{
	list_del_init(&nd->sk_users_next);
}
#endif // NETDOMAIN_SKUSERS

static inline void _debug_sk(struct sock *sk)
{
	ND_WARN("%s: sk=%p family=%d ipv6_only=%d\n",
		__FUNCTION__,
		sk, 
		sk ? SK_FAMILY(sk) : -1,
		sk && SK_FAMILY(sk) == AF_INET6 ? __ipv6_only_sock(sk) : -1);
}

static inline int _sk_isinet(struct sock *sk)
{
	return (sk && 
		(SK_FAMILY(sk) == AF_INET ||
		 (SK_FAMILY(sk) == AF_INET6 && !__ipv6_only_sock(sk))
		)
	       );
}

static inline void _nd_copylabel(struct sock *sk, 
			 struct sock *newsk)
{
	/* New incoming TCP connection (ACK) being setup.
	 * Copy label from parent socket to new socket.
	 *
	 * Netdomain was previously allocated by nd_alloc
	 */

	if (_sk_isinet(sk)){
	struct netdomain *listening, *accepted;
										
		ND_WLOCK;
		listening = ND_NETDOMAIN(SK_SECURITY(sk));
		accepted = ND_NETDOMAIN(SK_SECURITY(newsk));

		if (nd_is_valid(listening)){
			if (nd_is_valid(accepted)){
				accepted->active = get_sdprofile(listening->active);
				accepted->tcpmode = nd_mode_accepted;

				/* iface was checked (at SYN arrival)
		 		 * by nd_rcv
		 		 */
				accepted->iface_checked = TRUE;

#ifdef NETDOMAIN_SKUSERS
				if (nd_is_confined(accepted)){
					nd_skusers_add(accepted);
				}
#endif

			}else{
				ND_WARN("%s: Accepted sock %p has no netdomain\n",
					__FUNCTION__,
					newsk);
			}
		}
		ND_WUNLOCK;
	}
}

/*
 * _nd_send_reset
 * Send an TCP RST
 *
 * For Linux 2.6 tcp_v4_send_reset (net/ipv4/tcp_ipv4.c) is not exported
 * So have to form reset and attach manually. This code based on 
 * netfilter::ipt_REJECT.c::send_reset()
 *
 * For Linux 2.4 we require a kernel patch to load this module so we export
 * required function.
 */
static inline void _nd_send_reset(struct sk_buff *orig_skb)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
struct sk_buff *new_skb;
struct tcphdr orig_tcph, *new_tcph;
struct rtable *rt;
struct iphdr *new_iph;
struct flowi fl = {};
struct dst_entry *tmp_dst;
u_int16_t tmp_port;
u_int32_t tmp_addr;
int needs_ack;
int hh_len;

	/* IP header checks: fragment. */
	if (orig_skb->nh.iph->frag_off & htons(IP_OFFSET))
		goto end;

	if (skb_copy_bits(orig_skb, orig_skb->nh.iph->ihl*4,
			  &orig_tcph, sizeof(orig_tcph)) < 0)
 		goto end;

	/* No RST for RST. */
	if (orig_tcph.rst)
		goto end;

	/* COMPUTE ROUTE */
	new_iph = orig_skb->nh.iph;
	fl.nl_u.ip4_u.daddr = new_iph->daddr;
	if (ip_route_output_key(&rt, &fl) != 0)
		goto end;
                                                                               
	tmp_dst = orig_skb->dst; /* save off dst */

	if (ip_route_input(orig_skb, new_iph->saddr, new_iph->daddr,
	    RT_TOS(new_iph->tos), rt->u.dst.dev) != 0) {
		dst_release(&rt->u.dst);
		goto end;
	}

	dst_release(&rt->u.dst);
	rt = (struct rtable *)orig_skb->dst;

	orig_skb->dst = tmp_dst; /* restore old dst */

	if (rt->u.dst.error) {
		dst_release(&rt->u.dst);
		goto end;
	}
	/* END COMPUTE ROUTE */

	hh_len = LL_RESERVED_SPACE(rt->u.dst.dev);

	/* We need a linear, writeable skb.  We also need to expand
	   headroom in case hh_len of incoming interface < hh_len of
	   outgoing interface */
	new_skb = skb_copy_expand(orig_skb, hh_len, skb_tailroom(orig_skb),
			       GFP_ATOMIC);
	if (!new_skb) {
		dst_release(&rt->u.dst);
		goto end;
	}

	dst_release(new_skb->dst);
	new_skb->dst = &rt->u.dst;

	/* zero netfilter */
	new_skb->nfct = NULL;
        new_skb->nfcache = 0;


	new_tcph = (struct tcphdr *)((u_int32_t*)new_skb->nh.iph + new_skb->nh.iph->ihl);

	/* Swap source and dest */
	tmp_addr = new_skb->nh.iph->saddr;
	new_skb->nh.iph->saddr = new_skb->nh.iph->daddr;
	new_skb->nh.iph->daddr = tmp_addr;
	tmp_port = new_tcph->source;
	new_tcph->source = new_tcph->dest;
	new_tcph->dest = tmp_port;

	/* Truncate to length (no data) */
	new_tcph->doff = sizeof(struct tcphdr)/4;
	skb_trim(new_skb, new_skb->nh.iph->ihl*4 + sizeof(struct tcphdr));
	new_skb->nh.iph->tot_len = htons(new_skb->len);

	if (new_tcph->ack) {
		needs_ack = 0;
		new_tcph->seq = orig_tcph.ack_seq;
		new_tcph->ack_seq = 0;
	} else {
		needs_ack = 1;
		new_tcph->ack_seq = htonl(ntohl(orig_tcph.seq) + orig_tcph.syn + orig_tcph.fin
				      + orig_skb->len - orig_skb->nh.iph->ihl*4
				      - (orig_tcph.doff<<2));
		new_tcph->seq = 0;
	}

	/* Reset flags */
	((u_int8_t *)new_tcph)[13] = 0;
	new_tcph->rst = 1;
	new_tcph->ack = needs_ack;

	new_tcph->window = 0;
	new_tcph->urg_ptr = 0;

	/* Adjust TCP checksum */
	new_tcph->check = 0;
	new_tcph->check = tcp_v4_check(new_tcph, sizeof(struct tcphdr),
				   new_skb->nh.iph->saddr,
				   new_skb->nh.iph->daddr,
				   csum_partial((char *)new_tcph,
						sizeof(struct tcphdr), 0));

	/* Adjust IP TTL, DF */
	new_skb->nh.iph->ttl = MAXTTL;
	/* Set DF, id = 0 */
	new_skb->nh.iph->frag_off = htons(IP_DF);
	new_skb->nh.iph->id = 0;

	/* Adjust IP checksum */
	new_skb->nh.iph->check = 0;
	new_skb->nh.iph->check = ip_fast_csum((unsigned char *)new_skb->nh.iph, 
					   new_skb->nh.iph->ihl);

	/* "Never happens" */
	if (new_skb->len > dst_pmtu(new_skb->dst))
		goto free_new_skb;

	/* attach packet */
	(void)ip_finish_output(new_skb);

end:
	return;

free_new_skb:
	kfree_skb(new_skb);
#else
	/* exported from net/ipv4 */
	imnx_tcp_v4_send_reset(orig_skb);
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
}

/* ALLOCATION FUNCTIONS */

static inline int nd_alloc(struct sock *sk, int family, int priority)
{
struct netdomain *nd;
int ret=0;

	if ((family == AF_INET || family == AF_INET6)){

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		/* BEGIN UGLINESS
		 *
	 	 * AF_INET && GFP_ATOMIC is our "hint" of being called from 
		 * tcp_create_openreq_child.  Due to sk->security being 
		 * overwritten (see ISSUES above) we do not allocate here, 
		 * rather bail and allocate later in nd_syn_recv.
	 	 */

		if (priority == GFP_ATOMIC){
			SK_SECURITY(sk)=NULL;
			return 0;
		}else if (priority == 0){
			/* We manually set priority==0 when re-calling from
			 * nd_syn_recv to distinguish from above. Need to set
			 * back to ATOMIC
			 */
			priority=GFP_ATOMIC;
		}

		/* END UGLINESS */
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

		
		ND_DEBUG("%s: 0x%p %d\n" , __FUNCTION__, sk, priority);
	
		/* allocate memory for netdomain
		 * N.B priority may be GFP atomic (accept case),
		 * as we will be called in soft interupt context
		 */
		nd =    (struct netdomain*)
			kmalloc(sizeof(struct netdomain), priority);
											
		if (nd){
			nd->nd_magic = ND_ID_MAGIC;
			nd->tcpmode = nd_mode_none;
			nd->active = NULL;
			nd->iface_checked=FALSE;	/* for socket_check_tcp */
#ifdef NETDOMAIN_SKUSERS
			nd->sk=sk;
			INIT_LIST_HEAD(&nd->sk_users_next);
#endif // NETDOMAIN_SKUSERS


			/* nd->active set later in:
			 *      connect: nd_create
			 *      accept (parent): nd_create
			 *      accept (child): _nd_copylabel
			 *
			 * nd->tcpmode set later in:
			 *      connect: nd_connect
			 *      accept (parent): nd_listen
			 *      accept (child): _nd_copylabel
			 * 
			 * sk_users_next set later in:
			 *      nd_create
			 * 	listening TCP: _nd_copylabel
			 */

			SK_SECURITY(sk) = nd;
		}

		ret = (nd == NULL);
	}

	return ret;
}

static inline void nd_free(struct sock *sk)
{
	struct netdomain *nd;
										
	ND_DEBUG("%s: 0x%p\n" , __FUNCTION__, sk);
										
	ND_WLOCK;
 	nd = ND_NETDOMAIN(SK_SECURITY(sk));
	if (nd_is_valid(nd)){

#ifdef NETDOMAIN_SKUSERS
		/* Remove nd from list of netdomains using this profile. */
		nd_skusers_del(nd);
#endif

		put_sdprofile(nd->active);
		kfree(nd);
	}
	ND_WUNLOCK;
}

/* SOCKET CREATION FUNCTIONS */

static inline void nd_create(struct socket * sock, int family,
                            int type, int protocol)
{
        /* New socket being created
         *
         * Netdomain was previously allocated by nd_alloc
         *
         * N.B We currenntly use *PROFILE* from controlling tasks
         * subdomain (not active). 
         */
	if (_sk_isinet(sock->sk)){
		struct subdomain sdcopy,
                		 *sd;
		struct netdomain *nd;
                                                                                
        	sd = get_sdcopy(&sdcopy);

		ND_WLOCK;
        	nd = ND_NETDOMAIN(SK_SECURITY(sock->sk));
                                                                                
        	if (nd_is_valid(nd) && __sd_is_confined(sd)){
                	nd->active = get_sdprofile(sd->profile);

#ifdef NETDOMAIN_SKUSERS
			nd_skusers_add(nd);
#endif
        	}
		ND_WUNLOCK;
                                                                                
        	put_sdcopy(sd);
	}
}

/* CONNECTION FUNCTIONS */

static inline int nd_connect(struct socket *sock, 
		     struct sockaddr *address,
		     int addrlen)
{
int error = 0;

	/* We only check SOCK_STREAM (tcp) connects.
	 * Don't check SOCK_DGRAM (udp) connects as there is no
	 * way to determine whether socket is being connected for
	 * KERN_COD_UDP_SEND, KERN_COD_UDP_RECEIVE or both.
	 * We will mediate UDP at send/recv.
	 */

	if (_sk_isinet(sock->sk) &&
	    sock->type == SOCK_STREAM){

		struct subdomain sdcopy,
				 *sd;

		ND_DEBUG("%s: 0x%p 0x%p %d\n" , 
			__FUNCTION__, 
			sock, address, addrlen);

		sd = get_sdcopy(&sdcopy);

		if (__sd_is_confined(sd)){
			ifname_t name;
			char *iface;
			struct netdomain *nd;
			struct sockaddr_in *sin;

			iface = get_ifname(sock->sk, name);

			/* no lock needed as we don't access active or 
			 * sk_users */
			nd = ND_NETDOMAIN(SK_SECURITY(sock->sk));

			sin=(struct sockaddr_in*) address;

			ND_DEBUG("%s: sock src=%u.%u.%u.%u:%hu dest=%u.%u.%u.%u:%hu iface=%s\n",
				__FUNCTION__,
				NIPQUAD(SK_RCV_SADDR(sock->sk)), 
				ntohs(SK_SPORT(sock->sk)), 
				NIPQUAD(sin->sin_addr.s_addr),
				ntohs(sin->sin_port),
				iface ? iface : "n/a");

			error=nd_network_perm(sd, 
					SK_RCV_SADDR(sock->sk),
					SK_SPORT(sock->sk),
					sin->sin_addr.s_addr,
					sin->sin_port,
					iface,
					KERN_COD_TCP_CONNECT);

			if (error == 0){
			       if (nd_is_valid(nd)){
					nd->tcpmode = nd_mode_connected;

					/* iface is probably NULL here */
					nd->iface_checked = (iface != NULL);
				}else{
					ND_ERROR("%s: Invalid netdomain sd (%s:%d): %s(0x%p) src=%u.%u.%u.%u:%hu dest=%u.%u.%u.%u:%hu iface=%s\n",
						__FUNCTION__,
						current->comm,
						current->pid,
						sd->active->name,
						sd->active,
						NIPQUAD(SK_RCV_SADDR(sock->sk)), 
						ntohs(SK_SPORT(sock->sk)), 
						NIPQUAD(sin->sin_addr.s_addr),
						ntohs(sin->sin_port),
						iface ? iface : "n/a");
				}
			}
		}

		put_sdcopy(sd);
	}

	return error;
}

static inline int nd_accept(struct socket *sock, struct socket *newsock)
{
int error = 0;

	/* Pre-screen accept calls
	 * Look for any rule that allows us to accept connections via the
	 * local addr/port.  
	 *
	 * If no rule, we can immediately deny the accept.
	 * N.B if INADDR_ANY (0) was specified when binding the source 
	 * address, then we will match any accept rule present.
	 *
	 * While interface is checked, at this point, it is doubftul that
	 * sk->dest_cache contains a value to check. 
	 * 
	 * In order to reject connections requiring the remote (dest) address
	 * or interface to be matched, we need to defer to nd_rcv (TCP SYN)
	 */
	if (_sk_isinet(sock->sk)){
		struct subdomain sdcopy,
				 *sd;

		ND_DEBUG("%s: 0x%p 0x%p\n" , 
			__FUNCTION__, 
			sock, newsock);

		/* N.B newsock->sk is very NULL at this point.  
		 * It isn't allocated until the new connection (SYN) arrives.
		 */

		sd = get_sdcopy(&sdcopy);

		if (__sd_is_confined(sd)){
			ifname_t name;
			struct netdomain *nd;
			char *iface;

			iface = get_ifname(sock->sk, name);

			ND_RLOCK;
			nd = ND_NETDOMAIN(SK_SECURITY(sock->sk));

			if (nd_is_valid(nd)){
				/* Subdomain (profile) of accepting task must
				 * equal netdomain label on socket (profile of
				 * task that created socket).
				 */
				if (strcmp(nd->active->name, sd->profile->name) == 0){
					ND_DEBUG("%s: sock src=%u.%u.%u.%u:%hu dest=%u.%u.%u.%u:%hu iface=%s\n",
						__FUNCTION__,
						NIPQUAD(SK_RCV_SADDR(sock->sk)), 
						ntohs(SK_SPORT(sock->sk)), 
						NIPQUAD(SK_DADDR(sock->sk)), 
						ntohs(SK_DPORT(sock->sk)), 
						iface ? iface : "n/a");

					error=nd_network_perm(sd, 
						SK_RCV_SADDR(sock->sk),
						SK_SPORT(sock->sk),
						INADDR_ANY,
						0,
						iface,
						KERN_COD_TCP_ACCEPT);
				}else{
					ND_WARN("%s: Socket/task label mismatch. Rejecting accept (%s:%d): sd %s(0x%p) nd %s(0x%p)\n",
						__FUNCTION__,
						current->comm,
						current->pid,
						sd->active->name,
						sd->active,
						nd->active->name,
						nd->active);

					error = -EACCES;
				}
			}else{
				ND_ERROR("%s: Invalid netdomain sd (%s:%d): %s(0x%p) src=%u.%u.%u.%u:%hu dest=%u.%u.%u.%u:%hu iface=%s\n",
					__FUNCTION__,
					current->comm,
					current->pid,
					sd->active->name,
					sd->active,
					NIPQUAD(SK_RCV_SADDR(sock->sk)), 
					ntohs(SK_SPORT(sock->sk)), 
					NIPQUAD(SK_DADDR(sock->sk)), 
					ntohs(SK_DPORT(sock->sk)), 
					iface ? iface : "n/a");
			}
		}

		put_sdcopy(sd);
	}

        return error;              
}

static inline void nd_post_accept(struct socket *sock, struct socket *newsock)
{
	_nd_copylabel(sock->sk, newsock->sk);
}


static inline int nd_listen(struct socket *sock, int backlog)
{
	if (_sk_isinet(sock->sk)){
		struct netdomain *nd;

		/* no lock needed as we don't access active or sk_users */
        	nd = ND_NETDOMAIN(SK_SECURITY(sock->sk));

		if (nd_is_valid(nd)){
			/* nd_mode_listening just a debugging value */
			nd->tcpmode = nd_mode_listening;
		}
	}

	return 0;
}

/* DATA IO FUNCTIONS */

static inline int _nd_tcpio(int send, char *iface, 
			   struct subdomain *sd,
			   struct sock *sk)
{
	int error = 0, sdconfined;
	struct sdprofile *ndprofile;

	/* Recheck permissions on extablished TCP connection
	 * if profile has changed i.e changehat, or replacement
	 * or domain transition (shared socket)
	 */

	struct netdomain *nd;

	ND_RLOCK;

	nd = ND_NETDOMAIN(SK_SECURITY(sk));

	ndprofile = nd_is_valid(nd) ? get_sdprofile(nd->active) : NULL;

	ND_RUNLOCK;

	sdconfined=__sd_is_confined(sd);

	if (!ndprofile || !sdconfined){
		if (!(ndprofile && sdconfined)){
			ND_ERROR("%s: ND/SD mismatch ND=%s(%p) SD=%s(%p)\n",
				__FUNCTION__,
				ndprofile ? ndprofile->name : "null",
				ndprofile,
				sd->profile ? sd->profile->name : "null",
				sd->profile);
		}
		goto out;
	}

	/* Determination that profile has changed is by comparing
	 * cached active in netdomain (bound at socket creation) with 
	 * PROFILE from subdomain (current task).
	 *
	 * Also, if we have not checked the interface (usually because
	 * it was not available at connect time) check it now.
	 */

	if ((ndprofile != sd->profile || 
	     (iface && !nd->iface_checked))){

	    	int mode;

		if (nd->tcpmode == nd_mode_accepted ||
	 	       nd->tcpmode == nd_mode_connected){

			nd->iface_checked = TRUE;

			/* Need to check permissions using same mode as 
			 * originally used to verify access.
		         *
			 * Active task (sd) and cached (nd) may not match due
			 * to profile replacement of sd and also because of a
			 * domain transition (exec) where sd now refers to
			 * a new profile.  In this later case we want to check 
			 * access using TCP_ACCEPTED/TCP_CONNECTED rather than 
			 * TCP_ACCEPT/TCP_CONNECT in order to allow access to 
			 * existing connections without requiring that the
			 * profile allow new connections.
			 *
			 * Eventually refcounted string names will allow us to
			 * make this decision with a simple pointer comparison
			 * but for now, we need to do a strcmp
			 */

			if (strcmp(ndprofile->name, sd->profile->name) == 0){
				mode=(nd->tcpmode == nd_mode_accepted ?
					KERN_COD_TCP_ACCEPT :
					KERN_COD_TCP_CONNECT);
			}else{
				mode=(nd->tcpmode == nd_mode_accepted ?
					KERN_COD_TCP_ACCEPTED :
					KERN_COD_TCP_CONNECTED);
			}

			/* Add in non checking flag for SEND/RECV
		 	 * This is used to log as a send/recv rather
		 	 * than as a accept'connect.
		 	 */
			mode |= (send ? KERN_COD_LOGTCP_SEND : KERN_COD_LOGTCP_RECEIVE);

			error=nd_network_perm(	sd, 

				SK_RCV_SADDR(sk),
				SK_SPORT(sk),
				SK_DADDR(sk),
				SK_DPORT(sk),
				iface,
				mode);
		}else{
			ND_ERROR("%s: invalid tcpmode (%d) sd (%s:%d): %s(0x%p) saddr=%u.%u.%u.%u:%hu daddr=%u.%u.%u.%u:%hu iface=%s\n",
				__FUNCTION__,
				nd->tcpmode,
				current->comm,
				current->pid,
				sd->active->name,
				sd->active,
				NIPQUAD(SK_RCV_SADDR(sk)), 
				ntohs(SK_SPORT(sk)), 
				NIPQUAD(SK_DADDR(sk)), 
				ntohs(SK_DPORT(sk)), 
				iface ? iface : "n/a");
		}
	}

	put_sdprofile(ndprofile);

out:
	return error;
}

static inline int nd_sendmsg(struct socket *sock, 
				struct msghdr *msg, 
				int size)
{
int error = 0;

	if (_sk_isinet(sock->sk)){
		struct subdomain sdcopy,
			 	*sd;

		ND_DEBUG("%s: 0x%p 0x%p %d\n" , 
			__FUNCTION__, 
			sock, msg, size);
	

		sd = get_sdcopy(&sdcopy);

		if (__sd_is_confined(sd)){
			ifname_t name;
			char *iface;

			iface = get_ifname(sock->sk, name);

			if (sock->type == SOCK_STREAM && 
		 	    SK_STATE(sock->sk) == TCP_ESTABLISHED){
				error = _nd_tcpio(1, iface, sd, sock->sk);
				if (error != 0){
					/* close connection
					 * Doubtful we can't call sock_close 
					 * here without an oops at subsequent
					 * filp close so we will shutdown.
					 */
					ND_DEBUG("%s: sending shutdown\n",
						__FUNCTION__);
					(void)sock->ops->shutdown(sock, 
						SEND_SHUTDOWN|RCV_SHUTDOWN);
				}
	
			}else if (sock->type == SOCK_DGRAM){
				u32 daddr = INADDR_NONE;
				u16 dport = 0;
	
				/* Check to see if UDP socket was connected */
				if (SK_STATE(sock->sk) == TCP_ESTABLISHED){
					daddr = SK_DADDR(sock->sk);
					dport = SK_DPORT(sock->sk);
	
				}else if (msg->msg_name &&
				    msg->msg_namelen == sizeof(struct sockaddr_in)){
					struct sockaddr_in *sin;
				       
					/* iface is almost certainly NULL for
					 * unconnected UDP send */
	
					sin = (struct sockaddr_in *) msg->msg_name;
	
					daddr = sin->sin_addr.s_addr;
					dport = sin->sin_port;
	
				}else{
					ND_ERROR("%s: Invalid UDP msghdr (0x%p %d %d) sd (%s:%d): %s(0x%p) src=%u.%u.%u.%u:%hu\n",
						__FUNCTION__,
						msg->msg_name,
						(int) sizeof(struct sockaddr_in),
						msg->msg_namelen,
						current->comm,
						current->pid,
						sd->active->name,
						sd->active,
						NIPQUAD(SK_RCV_SADDR(sock->sk)), 
						ntohs(SK_SPORT(sock->sk)));
				}
	
				if (daddr != INADDR_NONE){
					/* Source address is likely NULL here 
					 * unless sender did the unlikely and 
					 * explicitly bind()ed to a local 
					 * address.
				 	 */
					error=nd_network_perm(  sd,
						SK_RCV_SADDR(sock->sk),	
						SK_SPORT(sock->sk),	
						daddr,		
						dport,		
						iface,	
						KERN_COD_UDP_SEND);
				}
			}
		}

		put_sdcopy(sd);
	}

	return error;
}

static inline int nd_recvmsg (struct socket *sock, 
				struct msghdr *msg,
			 	int size, int flags)
{
int error = 0;

	if (_sk_isinet(sock->sk)){
		struct subdomain sdcopy,
			 	*sd;

		ND_DEBUG("%s: 0x%p 0x%p %d %d\n" , 
			__FUNCTION__, 
			sock, msg, size, flags);

		sd = get_sdcopy(&sdcopy);

		if (__sd_is_confined(sd)){
			ifname_t name;
			char *iface;

			iface = get_ifname(sock->sk, name);
	
			if (sock->type == SOCK_STREAM &&
		 	    SK_STATE(sock->sk) == TCP_ESTABLISHED){
				error = _nd_tcpio(0, iface, sd, sock->sk);
				if (error != 0){
					// close connection
					ND_DEBUG("%s: sending shutdown\n",
						__FUNCTION__);
					(void)sock->ops->shutdown(sock, 
						SEND_SHUTDOWN|RCV_SHUTDOWN);
				}
	
			}else if (sock->type == SOCK_DGRAM){
				/* Pre-screen UDP recv calls.
		 	 	 * Look for any rule that allows us to receive 
				 * messages via the local addr/port.
				 *
				 * If no rule, we can immediately deny the 
				 * recv request.
				 *
		 	 	 * If the UDP socket is not connected, then 
				 * rejection of incoming packets based on 
				 * remote (dest) address is deferred to nd_rcv
			 	 */
	
				u32 daddr;
				u16 dport;
	
				/* Check to see if UDP socket was connected */
				if (SK_STATE(sock->sk) == TCP_ESTABLISHED){
					daddr = SK_DADDR(sock->sk);
					dport = SK_DPORT(sock->sk);
				}else{
					/* iface is almost certainly NULL for
					 * unconnected UDP recv */
	
					/* match any remote addr/port */
					daddr = INADDR_ANY; 
					dport = 0;
				}
	
				error=nd_network_perm(	sd, 
					SK_RCV_SADDR(sock->sk),
					SK_SPORT(sock->sk),
					daddr,
					dport,
					iface,
					KERN_COD_UDP_RECEIVE);
			}
		}

		put_sdcopy(sd);
	}

	return error;
}

/* PACKET IO FUNCTIONS (interrupt context) */

static int nd_rcv(struct sock *sk, struct sk_buff *skb)
{
int error = 0;

	ND_DEBUG("%s: 0x%p 0x%p %d %d %d\n" , 
		__FUNCTION__, 
		sk, skb, SK_FAMILY(sk), SK_TYPE(sk), 
		SK_FAMILY(sk) == AF_INET6 ? __ipv6_only_sock(sk) : -1);

	if (_sk_isinet(sk) &&
	    (
#ifdef NETDOMAIN_UDPSOFTINTR
	    	/* UDP */
		SK_TYPE(sk) == SOCK_DGRAM ||
#endif

		/* TCP SYN */
	        (SK_TYPE(sk) == SOCK_STREAM &&
	         SK_STATE(sk) == TCP_LISTEN &&
	         skb->h.th->syn &&
	         !skb->h.th->ack
	        )
	    )){
		struct netdomain *nd;
		struct subdomain sdcopy,
				 *sd = NULL;
		struct task_struct *waiting_tsk;
		char *iface;

		struct iphdr  *iph = skb->nh.iph;
		struct tcphdr *tcph = skb->h.th;
#ifdef NETDOMAIN_UDPSOFTINTR
		struct udphdr *udph = skb->h.uh;
#endif
		__u32 saddr, daddr;
		__u16 sport, dport;
	       
		/* Get src/dest addresses.
		 * Reverse since skbuff is for an incoming packet.
		 */
		
		saddr = iph->daddr;
		daddr = iph->saddr;
		if (SK_TYPE(sk) == SOCK_STREAM){
			sport = tcph->dest;
			dport = tcph->source;	
		}
#ifdef NETDOMAIN_UDPSOFTINTR
		else{
			sport = udph->dest;
			dport = udph->source;	
		}
#endif

		/* Sanity check that the socks local address equals the value 
		 * we got from the skbuff
		 */
		BUG_ON(SK_RCV_SADDR(sk) != INADDR_ANY && 
			SK_RCV_SADDR(sk) != saddr);
		BUG_ON(SK_SPORT(sk) != sport);


		/* MAIN CODE */

		ND_RLOCK;
		nd = ND_NETDOMAIN(SK_SECURITY(sk));
	
		/* Bail out if socket says we are not confined */
		if (!nd_is_confined(nd)){
			ND_DEBUG("%s: not confined\n", __FUNCTION__);
			ND_RUNLOCK;
			goto done;
		}

		/* We have a netdomain but lack access to
		 * the task in order to obtain subdomain.
		 *
		 * Fake out a subdomain (sdcopy) that 
		 * put_sdcopy can release below.  
		 *
		 * Use the profile cached in the netdomain
		 * which is based on the sd->profile of the
		 * task that created the socket. Obtain inside
		 * ND_LOCK in case profile replacement is trying
		 * to update nd->active
	 	 */

		sdcopy.sd_magic = SD_ID_MAGIC;

		sdcopy.active = sdcopy.profile = get_sdprofile(nd->active);
		ND_RUNLOCK;

		sd=&sdcopy;

		ND_DEBUG("%s: sd %s(0x%p)\n",
			__FUNCTION__,
			sd->active->name,
			sd->active);

		/* value in sk->dst_cache not yet set */
		iface = skb->dev ? skb->dev->name : NULL;

		ND_DEBUG("%s: interface name = %s\n",
			__FUNCTION__,
			iface);

		read_lock(&tasklist_lock);

		waiting_tsk = get_waitingtask(sk);

		if (!waiting_tsk){
			read_unlock(&tasklist_lock);
		}

		if (SK_TYPE(sk) == SOCK_STREAM){
			/* connection request (SYN) */
	
			ND_DEBUG("%s: TCP SYN\n" , __FUNCTION__);
	
			error=__nd_network_perm(waiting_tsk,
				sd, 
				saddr,
				sport,
				daddr,
				dport,
				iface,
				KERN_COD_TCP_ACCEPT);
	
			if (error != 0){
				/* Send TCP RST */
				_nd_send_reset(skb);
			}
		}
#ifdef NETDOMAIN_UDPSOFTINTR
		else{ /* SK_TYPE(sk) == SOCK_DGRAM) */
	
			/* UDP dagagram */
	
			ND_DEBUG("%s: UDP RECV\n" , 
				__FUNCTION__);

			error=__nd_network_perm(waiting_tsk,
					sd, 
					saddr,
					sport,
					daddr,
					dport,
					iface,
					KERN_COD_UDP_RECEIVE);
		}
#endif
	
		/* Unlock task struct that was not unlocked earlier */
		if (waiting_tsk){
			read_unlock(&tasklist_lock);
		}

		put_sdcopy(sd);
	}

done:
	return error;
}


/* HANDLER FUNCTIONS */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
/* nd_syn_recv
 * TCP SYN receive handler
 *
 * See ISSUES at head of file as to why this code is necessary for 2.6
 *
 * nd_alloc, if it detects it is being called from tcp_create_openreq_child,
 * will not allocate (to avoid leaking) and we redo the allocation here 
 */
static struct sock *nd_syn_recv(struct sock *sk,
			struct sk_buff *skb,
			struct open_request *req,
			struct dst_entry *dst)
{
	struct sock *newsk;

	/* wrap std call */
	if (SK_FAMILY(sk) == AF_INET){
		newsk=(*old_ipv4_syn_recv_sock)(sk, skb, req, dst);
	}else if (SK_FAMILY(sk) == AF_INET6){
		newsk=(*old_ipv6_syn_recv_sock)(sk, skb, req, dst);
	}else{
		ND_ERROR("%s: Unable to dispatch to parent syn_recv_sock handler. Unknown family %d\n", 
			__FUNCTION__,
			SK_FAMILY(sk));
		return NULL;
	}

	if (_sk_isinet(newsk)){
		/* Temp HACK - do nd_alloc that was previously deferred.
		 * Priority=0 is a special flag/hint value
		 */
		if (nd_alloc(newsk, SK_FAMILY(sk), 0)){
			sk_free(newsk);
			return NULL;
		}
		/* END Temp HACK */
	}

	return newsk;
}
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#ifndef NETDOMAIN_UDPSOFTINTR
static int nd_dgram_recvmsg
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
			   (struct kiocb *iocb, struct socket *sock,
			    struct msghdr *m, size_t total_len,
			    int flags)
#else
			   (struct socket *sock, struct msghdr *m, 
			       int total_len, int flags, 
			       struct scm_cookie *scm)
#endif
{
int ret;
struct sockaddr_in udp_remote;
void *udp_old_msgname = NULL;
int   udp_old_msgnamelen = 0;

	/* We need remote endpoint, even if caller doesn't.
	 * aio_read for example always passes NULL/0
	 */
	if ((SK_FAMILY(sock->sk) == AF_INET || SK_FAMILY(sock->sk) == AF_INET6) &&
	     SK_TYPE(sock->sk) == SOCK_DGRAM &&
	     m->msg_namelen < sizeof(struct sockaddr_in)){

		udp_old_msgname = m->msg_name;
		udp_old_msgnamelen = m->msg_namelen;

		m->msg_name = &udp_remote;
		m->msg_namelen = sizeof(struct sockaddr_in);
	}

        if (SK_FAMILY(sock->sk) == AF_INET){
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
                ret=(*old_ipv4_dgram_recvmsg)(iocb, sock, m, total_len, flags);
#else
                ret=(*old_ipv4_dgram_recvmsg)(sock, m, total_len, flags, scm);
#endif
	}
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
        else if (SK_FAMILY(sock->sk) == AF_INET6){
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
                ret=(*old_ipv6_dgram_recvmsg)(iocb, sock, m, total_len, flags);
#else
                ret=(*old_ipv6_dgram_recvmsg)(sock, m, total_len, flags, scm);
#endif
	}
#endif
        else{
                ND_ERROR("%s: Unable to dispatch to parent dgram_recvmsg  handler. Unknown family %d\n",
                        __FUNCTION__,
                        SK_FAMILY(sock->sk));
                return -EINVAL;
        }

	/* Already verified conncted UDP sockets in nd_recvmsg */
	if (ret > 0 && 
	    SK_TYPE(sock->sk) == SOCK_DGRAM &&
	    SK_STATE(sock->sk) != TCP_ESTABLISHED){
		struct subdomain sdcopy,
                                 *sd;

		sd = get_sdcopy(&sdcopy);

		if (__sd_is_confined(sd)){
			struct sockaddr_in *sin;
			ifname_t name;
			char *iface;
			int error;

			sin = (struct sockaddr_in *)m->msg_name;
			iface = get_ifname(sock->sk, name);

			error=nd_network_perm(sd, 
					      SK_RCV_SADDR(sock->sk),
					      SK_SPORT(sock->sk),	
					      sin->sin_addr.s_addr,
					      sin->sin_port,
					      iface,
					      KERN_COD_UDP_RECEIVE);

			if (error != 0){
				/* clear data copied into buffer by udp_recvmsg
				 * (net/ipv[46]/udp.c)
				 */
				__clear_user((char*)(m->msg_iov->iov_base) - ret, ret);

				/* prevent remote address from being copied
				 * to userspace
				 */
				udp_old_msgnamelen = m->msg_namelen = 0;

				ret = 0; 
			}
		}
	}

	if (m->msg_name == &udp_remote){
		memcpy((char*)udp_old_msgname, (char*)&udp_remote, 
			udp_old_msgnamelen);

		m->msg_name = udp_old_msgname;
                m->msg_namelen = udp_old_msgnamelen;
	}

	return ret;
}
#endif

static inline void nd_set_handlers(void)
{
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	int err;

	/* af_specific and inet_dgram vtables are not exported in IPV6
	 * need a socket to get at them.  Yes, mucho fugly.
	 */
	err= sock_create(AF_INET6, SOCK_STREAM, 0, &ipv6_sockp);

	if (err == 0){
		ipv6_enabled=1;
	}else{
		ipv6_enabled=0;

		ND_WARN("%s: Unable to patch IPV6 handlers -- error %d.\n",
			__FUNCTION__, err);
		ND_WARN("%s: Netdomain is not enabled for IPV6\n", 
			__FUNCTION__);
	}
#endif
		
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (ipv6_enabled){
		struct tcp_opt *tp = tcp_sk(ipv6_sockp->sk);
		old_ipv6_syn_recv_sock = tp->af_specific->syn_recv_sock;
		tp->af_specific->syn_recv_sock = nd_syn_recv;
	}
#endif // defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

	old_ipv4_syn_recv_sock = ipv4_specific.syn_recv_sock;
	ipv4_specific.syn_recv_sock = nd_syn_recv;
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#ifndef NETDOMAIN_UDPSOFTINTR
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (ipv6_enabled){
		old_ipv6_dgram_recvmsg = ipv6_sockp->ops->recvmsg;
		ipv6_sockp->ops->recvmsg = nd_dgram_recvmsg;
	}
#endif // defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

	old_ipv4_dgram_recvmsg = inet_dgram_ops.recvmsg;
	inet_dgram_ops.recvmsg = nd_dgram_recvmsg;
#endif // !NETDOMAIN_UDPSOFTINTR
}

static inline void nd_restore_handlers(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (old_ipv6_syn_recv_sock){
		struct tcp_opt *tp;

		tp = tcp_sk(ipv6_sockp->sk);

		tp->af_specific->syn_recv_sock = old_ipv6_syn_recv_sock;
		old_ipv6_syn_recv_sock = NULL;
	}
#endif // defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)

	ipv4_specific.syn_recv_sock = old_ipv4_syn_recv_sock;
	old_ipv4_syn_recv_sock = NULL;
#endif // LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)

#ifndef NETDOMAIN_UDPSOFTINTR
#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (old_ipv6_dgram_recvmsg){
		ipv6_sockp->ops->recvmsg = old_ipv6_dgram_recvmsg;
		old_ipv6_dgram_recvmsg = NULL;
	}
#endif // defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	inet_dgram_ops.recvmsg = old_ipv4_dgram_recvmsg;
	old_ipv4_dgram_recvmsg = NULL;
#endif // !NETDOMAIN_UDPSOFTINTR

#if defined(CONFIG_IPV6) || defined(CONFIG_IPV6_MODULE)
	if (ipv6_enabled){
		sock_release(ipv6_sockp);
	}
#endif
}
