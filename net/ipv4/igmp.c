/*
 *	Linux NET3:	Internet Group Management Protocol  [IGMP]
 *
 *	This code implements the IGMP protocol as defined in RFC1112. There has
 *	been a further revision of this protocol since which is now supported.
 *
 *	If you have trouble with this module be careful what gcc you have used,
 *	the older version didn't come out right using gcc 2.5.8, the newer one
 *	seems to fall out with gcc 2.6.2.
 *
 *	Version: $Id: igmp.c,v 1.41 2000/08/31 23:39:12 davem Exp $
 *
 *	Authors:
 *		Alan Cox <Alan.Cox@linux.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Fixes:
 *
 *		Alan Cox	:	Added lots of __inline__ to optimise
 *					the memory usage of all the tiny little
 *					functions.
 *		Alan Cox	:	Dumped the header building experiment.
 *		Alan Cox	:	Minor tweaks ready for multicast routing
 *					and extended IGMP protocol.
 *		Alan Cox	:	Removed a load of inline directives. Gcc 2.5.8
 *					writes utterly bogus code otherwise (sigh)
 *					fixed IGMP loopback to behave in the manner
 *					desired by mrouted, fixed the fact it has been
 *					broken since 1.3.6 and cleaned up a few minor
 *					points.
 *
 *		Chih-Jen Chang	:	Tried to revise IGMP to Version 2
 *		Tsu-Sheng Tsao		E-mail: chihjenc@scf.usc.edu and tsusheng@scf.usc.edu
 *					The enhancements are mainly based on Steve Deering's 
 * 					ipmulti-3.5 source code.
 *		Chih-Jen Chang	:	Added the igmp_get_mrouter_info and
 *		Tsu-Sheng Tsao		igmp_set_mrouter_info to keep track of
 *					the mrouted version on that device.
 *		Chih-Jen Chang	:	Added the max_resp_time parameter to
 *		Tsu-Sheng Tsao		igmp_heard_query(). Using this parameter
 *					to identify the multicast router version
 *					and do what the IGMP version 2 specified.
 *		Chih-Jen Chang	:	Added a timer to revert to IGMP V2 router
 *		Tsu-Sheng Tsao		if the specified time expired.
 *		Alan Cox	:	Stop IGMP from 0.0.0.0 being accepted.
 *		Alan Cox	:	Use GFP_ATOMIC in the right places.
 *		Christian Daudt :	igmp timer wasn't set for local group
 *					memberships but was being deleted, 
 *					which caused a "del_timer() called 
 *					from %p with timer not initialized\n"
 *					message (960131).
 *		Christian Daudt :	removed del_timer from 
 *					igmp_timer_expire function (960205).
 *             Christian Daudt :       igmp_heard_report now only calls
 *                                     igmp_timer_expire if tm->running is
 *                                     true (960216).
 *		Malcolm Beattie :	ttl comparison wrong in igmp_rcv made
 *					igmp_heard_query never trigger. Expiry
 *					miscalculation fixed in igmp_heard_query
 *					and random() made to return unsigned to
 *					prevent negative expiry times.
 *		Alexey Kuznetsov:	Wrong group leaving behaviour, backport
 *					fix from pending 2.1.x patches.
 *		Alan Cox:		Forget to enable FDDI support earlier.
 *		Alexey Kuznetsov:	Fixed leaving groups on device down.
 *		Alexey Kuznetsov:	Accordance to igmp-v2-06 draft.
 */


#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#ifdef CONFIG_IP_MROUTE
#include <linux/mroute.h>
#endif


#define IP_MAX_MEMBERSHIPS 20

#ifdef CONFIG_IP_MULTICAST


/* Parameter names and values are taken from igmp-v2-06 draft */

#define IGMP_V1_Router_Present_Timeout		(400*HZ)
#define IGMP_Unsolicited_Report_Interval	(10*HZ)
#define IGMP_Query_Response_Interval		(10*HZ)
#define IGMP_Unsolicited_Report_Count		2


#define IGMP_Initial_Report_Delay		(1*HZ)

/* IGMP_Initial_Report_Delay is not from IGMP specs!
 * IGMP specs require to report membership immediately after
 * joining a group, but we delay the first report by a
 * small interval. It seems more natural and still does not
 * contradict to specs provided this delay is small enough.
 */

#define IGMP_V1_SEEN(in_dev) ((in_dev)->mr_v1_seen && (long)(jiffies - (in_dev)->mr_v1_seen) < 0)

#endif

static void ip_ma_put(struct ip_mc_list *im)
{
	if (atomic_dec_and_test(&im->refcnt)) {
		in_dev_put(im->interface);
		kfree(im);
	}
}

#ifdef CONFIG_IP_MULTICAST

/*
 *	Timer management
 */

static __inline__ void igmp_stop_timer(struct ip_mc_list *im)
{
	spin_lock_bh(&im->lock);
	if (del_timer(&im->timer))
		atomic_dec(&im->refcnt);
	im->tm_running=0;
	im->reporter = 0;
	im->unsolicit_count = 0;
	spin_unlock_bh(&im->lock);
}

/* It must be called with locked im->lock */
static void igmp_start_timer(struct ip_mc_list *im, int max_delay)
{
	int tv=net_random() % max_delay;

	im->tm_running=1;
	if (!mod_timer(&im->timer, jiffies+tv+2))
		atomic_inc(&im->refcnt);
}

static void igmp_mod_timer(struct ip_mc_list *im, int max_delay)
{
	spin_lock_bh(&im->lock);
	im->unsolicit_count = 0;
	if (del_timer(&im->timer)) {
		if ((long)(im->timer.expires-jiffies) < max_delay) {
			add_timer(&im->timer);
			im->tm_running=1;
			spin_unlock_bh(&im->lock);
			return;
		}
		atomic_dec(&im->refcnt);
	}
	igmp_start_timer(im, max_delay);
	spin_unlock_bh(&im->lock);
}


/*
 *	Send an IGMP report.
 */

#define IGMP_SIZE (sizeof(struct igmphdr)+sizeof(struct iphdr)+4)

/* Don't just hand NF_HOOK skb->dst->output, in case netfilter hook
   changes route */
static inline int
output_maybe_reroute(struct sk_buff *skb)
{
	return skb->dst->output(skb);
}

static int igmp_send_report(struct net_device *dev, u32 group, int type)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct igmphdr *ih;
	struct rtable *rt;
	u32	dst;

	/* According to IGMPv2 specs, LEAVE messages are
	 * sent to all-routers group.
	 */
	dst = group;
	if (type == IGMP_HOST_LEAVE_MESSAGE)
		dst = IGMP_ALL_ROUTER;

	if (ip_route_output(&rt, dst, 0, 0, dev->ifindex))
		return -1;
	if (rt->rt_src == 0) {
		ip_rt_put(rt);
		return -1;
	}

	skb=alloc_skb(IGMP_SIZE+dev->hard_header_len+15, GFP_ATOMIC);
	if (skb == NULL) {
		ip_rt_put(rt);
		return -1;
	}

	skb->dst = &rt->u.dst;

	skb_reserve(skb, (dev->hard_header_len+15)&~15);

	skb->nh.iph = iph = (struct iphdr *)skb_put(skb, sizeof(struct iphdr)+4);

	iph->version  = 4;
	iph->ihl      = (sizeof(struct iphdr)+4)>>2;
	iph->tos      = 0;
	iph->frag_off = __constant_htons(IP_DF);
	iph->ttl      = 1;
	iph->daddr    = dst;
	iph->saddr    = rt->rt_src;
	iph->protocol = IPPROTO_IGMP;
	iph->tot_len  = htons(IGMP_SIZE);
	ip_select_ident(iph, &rt->u.dst);
	((u8*)&iph[1])[0] = IPOPT_RA;
	((u8*)&iph[1])[1] = 4;
	((u8*)&iph[1])[2] = 0;
	((u8*)&iph[1])[3] = 0;
	ip_send_check(iph);

	ih = (struct igmphdr *)skb_put(skb, sizeof(struct igmphdr));
	ih->type=type;
	ih->code=0;
	ih->csum=0;
	ih->group=group;
	ih->csum=ip_compute_csum((void *)ih, sizeof(struct igmphdr));

	return NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, skb, NULL, rt->u.dst.dev,
		       output_maybe_reroute);
}


static void igmp_timer_expire(unsigned long data)
{
	struct ip_mc_list *im=(struct ip_mc_list *)data;
	struct in_device *in_dev = im->interface;
	int err;

	spin_lock(&im->lock);
	im->tm_running=0;

	if (IGMP_V1_SEEN(in_dev))
		err = igmp_send_report(in_dev->dev, im->multiaddr, IGMP_HOST_MEMBERSHIP_REPORT);
	else
		err = igmp_send_report(in_dev->dev, im->multiaddr, IGMP_HOST_NEW_MEMBERSHIP_REPORT);

	/* Failed. Retry later. */
	if (err) {
		if (!in_dev->dead)
			igmp_start_timer(im, IGMP_Unsolicited_Report_Interval);
		goto out;
	}

	if (im->unsolicit_count) {
		im->unsolicit_count--;
		igmp_start_timer(im, IGMP_Unsolicited_Report_Interval);
	}
	im->reporter = 1;
out:
	spin_unlock(&im->lock);
	ip_ma_put(im);
}

static void igmp_heard_report(struct in_device *in_dev, u32 group)
{
	struct ip_mc_list *im;

	/* Timers are only set for non-local groups */

	if (group == IGMP_ALL_HOSTS)
		return;

	read_lock(&in_dev->lock);
	for (im=in_dev->mc_list; im!=NULL; im=im->next) {
		if (im->multiaddr == group) {
			igmp_stop_timer(im);
			break;
		}
	}
	read_unlock(&in_dev->lock);
}

static void igmp_heard_query(struct in_device *in_dev, unsigned char max_resp_time,
			     u32 group)
{
	struct ip_mc_list	*im;
	int			max_delay;

	max_delay = max_resp_time*(HZ/IGMP_TIMER_SCALE);

	if (max_resp_time == 0) {
		/* Alas, old v1 router presents here. */

		max_delay = IGMP_Query_Response_Interval;
		in_dev->mr_v1_seen = jiffies + IGMP_V1_Router_Present_Timeout;
		group = 0;
	}

	/*
	 * - Start the timers in all of our membership records
	 *   that the query applies to for the interface on
	 *   which the query arrived excl. those that belong
	 *   to a "local" group (224.0.0.X)
	 * - For timers already running check if they need to
	 *   be reset.
	 * - Use the igmp->igmp_code field as the maximum
	 *   delay possible
	 */
	read_lock(&in_dev->lock);
	for (im=in_dev->mc_list; im!=NULL; im=im->next) {
		if (group && group != im->multiaddr)
			continue;
		if (im->multiaddr == IGMP_ALL_HOSTS)
			continue;
		igmp_mod_timer(im, max_delay);
	}
	read_unlock(&in_dev->lock);
}

int igmp_rcv(struct sk_buff *skb, unsigned short len)
{
	/* This basically follows the spec line by line -- see RFC1112 */
	struct igmphdr *ih = skb->h.igmph;
	struct in_device *in_dev = in_dev_get(skb->dev);

	if (in_dev==NULL) {
		kfree_skb(skb);
		return 0;
	}

	if (len < sizeof(struct igmphdr) || ip_compute_csum((void *)ih, len)) {
		in_dev_put(in_dev);
		kfree_skb(skb);
		return 0;
	}
	
	switch (ih->type) {
	case IGMP_HOST_MEMBERSHIP_QUERY:
		igmp_heard_query(in_dev, ih->code, ih->group);
		break;
	case IGMP_HOST_MEMBERSHIP_REPORT:
	case IGMP_HOST_NEW_MEMBERSHIP_REPORT:
		/* Is it our report looped back? */
		if (((struct rtable*)skb->dst)->key.iif == 0)
			break;
		igmp_heard_report(in_dev, ih->group);
		break;
	case IGMP_PIM:
#ifdef CONFIG_IP_PIMSM_V1
		in_dev_put(in_dev);
		return pim_rcv_v1(skb, len);
#endif
	case IGMP_DVMRP:
	case IGMP_TRACE:
	case IGMP_HOST_LEAVE_MESSAGE:
	case IGMP_MTRACE:
	case IGMP_MTRACE_RESP:
		break;
	default:
		NETDEBUG(printk(KERN_DEBUG "New IGMP type=%d, why we do not know about it?\n", ih->type));
	}
	in_dev_put(in_dev);
	kfree_skb(skb);
	return 0;
}

#endif


/*
 *	Add a filter to a device
 */

static void ip_mc_filter_add(struct in_device *in_dev, u32 addr)
{
	char buf[MAX_ADDR_LEN];
	struct net_device *dev = in_dev->dev;

	/* Checking for IFF_MULTICAST here is WRONG-WRONG-WRONG.
	   We will get multicast token leakage, when IFF_MULTICAST
	   is changed. This check should be done in dev->set_multicast_list
	   routine. Something sort of:
	   if (dev->mc_list && dev->flags&IFF_MULTICAST) { do it; }
	   --ANK
	   */
	if (arp_mc_map(addr, buf, dev, 0) == 0)
		dev_mc_add(dev,buf,dev->addr_len,0);
}

/*
 *	Remove a filter from a device
 */

static void ip_mc_filter_del(struct in_device *in_dev, u32 addr)
{
	char buf[MAX_ADDR_LEN];
	struct net_device *dev = in_dev->dev;

	if (arp_mc_map(addr, buf, dev, 0) == 0)
		dev_mc_delete(dev,buf,dev->addr_len,0);
}

static void igmp_group_dropped(struct ip_mc_list *im)
{
#ifdef CONFIG_IP_MULTICAST
	int reporter;
#endif

	if (im->loaded) {
		im->loaded = 0;
		ip_mc_filter_del(im->interface, im->multiaddr);
	}

#ifdef CONFIG_IP_MULTICAST
	if (im->multiaddr == IGMP_ALL_HOSTS)
		return;

	reporter = im->reporter;
	igmp_stop_timer(im);

	if (reporter && !IGMP_V1_SEEN(im->interface))
		igmp_send_report(im->interface->dev, im->multiaddr, IGMP_HOST_LEAVE_MESSAGE);
#endif
}

static void igmp_group_added(struct ip_mc_list *im)
{
	if (im->loaded == 0) {
		im->loaded = 1;
		ip_mc_filter_add(im->interface, im->multiaddr);
	}

#ifdef CONFIG_IP_MULTICAST
	if (im->multiaddr == IGMP_ALL_HOSTS)
		return;

	spin_lock_bh(&im->lock);
	igmp_start_timer(im, IGMP_Initial_Report_Delay);
	spin_unlock_bh(&im->lock);
#endif
}


/*
 *	Multicast list managers
 */


/*
 *	A socket has joined a multicast group on device dev.
 */

void ip_mc_inc_group(struct in_device *in_dev, u32 addr)
{
	struct ip_mc_list *im;

	ASSERT_RTNL();

	for (im=in_dev->mc_list; im; im=im->next) {
		if (im->multiaddr == addr) {
			im->users++;
			goto out;
		}
	}

	im = (struct ip_mc_list *)kmalloc(sizeof(*im), GFP_KERNEL);
	if (!im)
		goto out;

	im->users=1;
	im->interface=in_dev;
	in_dev_hold(in_dev);
	im->multiaddr=addr;
	atomic_set(&im->refcnt, 1);
	spin_lock_init(&im->lock);
#ifdef  CONFIG_IP_MULTICAST
	im->tm_running=0;
	init_timer(&im->timer);
	im->timer.data=(unsigned long)im;
	im->timer.function=&igmp_timer_expire;
	im->unsolicit_count = IGMP_Unsolicited_Report_Count;
	im->reporter = 0;
	im->loaded = 0;
#endif
	write_lock_bh(&in_dev->lock);
	im->next=in_dev->mc_list;
	in_dev->mc_list=im;
	write_unlock_bh(&in_dev->lock);
	igmp_group_added(im);
	if (in_dev->dev->flags & IFF_UP)
		ip_rt_multicast_event(in_dev);
out:
	return;
}

/*
 *	A socket has left a multicast group on device dev
 */

int ip_mc_dec_group(struct in_device *in_dev, u32 addr)
{
	int err = -ESRCH;
	struct ip_mc_list *i, **ip;
	
	ASSERT_RTNL();
	
	for (ip=&in_dev->mc_list; (i=*ip)!=NULL; ip=&i->next) {
		if (i->multiaddr==addr) {
			if (--i->users == 0) {
				write_lock_bh(&in_dev->lock);
				*ip = i->next;
				write_unlock_bh(&in_dev->lock);
				igmp_group_dropped(i);

				if (in_dev->dev->flags & IFF_UP)
					ip_rt_multicast_event(in_dev);

				ip_ma_put(i);
				return 0;
			}
			err = 0;
			break;
		}
	}
	return -ESRCH;
}

/* Device going down */

void ip_mc_down(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	for (i=in_dev->mc_list; i; i=i->next)
		igmp_group_dropped(i);

	ip_mc_dec_group(in_dev, IGMP_ALL_HOSTS);
}

/* Device going up */

void ip_mc_up(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	ip_mc_inc_group(in_dev, IGMP_ALL_HOSTS);

	for (i=in_dev->mc_list; i; i=i->next)
		igmp_group_added(i);
}

/*
 *	Device is about to be destroyed: clean up.
 */

void ip_mc_destroy_dev(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	write_lock_bh(&in_dev->lock);
	while ((i = in_dev->mc_list) != NULL) {
		in_dev->mc_list = i->next;
		write_unlock_bh(&in_dev->lock);

		igmp_group_dropped(i);
		ip_ma_put(i);

		write_lock_bh(&in_dev->lock);
	}
	write_unlock_bh(&in_dev->lock);
}

static struct in_device * ip_mc_find_dev(struct ip_mreqn *imr)
{
	struct rtable *rt;
	struct net_device *dev = NULL;
	struct in_device *idev = NULL;

	if (imr->imr_address.s_addr) {
		dev = ip_dev_find(imr->imr_address.s_addr);
		if (!dev)
			return NULL;
		__dev_put(dev);
	}

	if (!dev && !ip_route_output(&rt, imr->imr_multiaddr.s_addr, 0, 0, 0)) {
		dev = rt->u.dst.dev;
		ip_rt_put(rt);
	}
	if (dev) {
		imr->imr_ifindex = dev->ifindex;
		idev = __in_dev_get(dev);
	}
	return idev;
}

/*
 *	Join a socket to a group
 */
int sysctl_igmp_max_memberships = IP_MAX_MEMBERSHIPS;

int ip_mc_join_group(struct sock *sk , struct ip_mreqn *imr)
{
	int err;
	u32 addr = imr->imr_multiaddr.s_addr;
	struct ip_mc_socklist *iml, *i;
	struct in_device *in_dev;
	int count = 0;

	if (!MULTICAST(addr))
		return -EINVAL;

	rtnl_shlock();

	if (!imr->imr_ifindex)
		in_dev = ip_mc_find_dev(imr);
	else {
		in_dev = inetdev_by_index(imr->imr_ifindex);
		if (in_dev)
			__in_dev_put(in_dev);
	}

	if (!in_dev) {
		iml = NULL;
		err = -ENODEV;
		goto done;
	}

	iml = (struct ip_mc_socklist *)sock_kmalloc(sk, sizeof(*iml), GFP_KERNEL);

	err = -EADDRINUSE;
	for (i=sk->protinfo.af_inet.mc_list; i; i=i->next) {
		if (memcmp(&i->multi, imr, sizeof(*imr)) == 0) {
			/* New style additions are reference counted */
			if (imr->imr_address.s_addr == 0) {
				i->count++;
				err = 0;
			}
			goto done;
		}
		count++;
	}
	err = -ENOBUFS;
	if (iml == NULL || count >= sysctl_igmp_max_memberships)
		goto done;
	memcpy(&iml->multi, imr, sizeof(*imr));
	iml->next = sk->protinfo.af_inet.mc_list;
	iml->count = 1;
	sk->protinfo.af_inet.mc_list = iml;
	ip_mc_inc_group(in_dev, addr);
	iml = NULL;
	err = 0;

done:
	rtnl_shunlock();
	if (iml)
		sock_kfree_s(sk, iml, sizeof(*iml));
	return err;
}

/*
 *	Ask a socket to leave a group.
 */

int ip_mc_leave_group(struct sock *sk, struct ip_mreqn *imr)
{
	struct ip_mc_socklist *iml, **imlp;

	rtnl_lock();
	for (imlp=&sk->protinfo.af_inet.mc_list; (iml=*imlp)!=NULL; imlp=&iml->next) {
		if (iml->multi.imr_multiaddr.s_addr==imr->imr_multiaddr.s_addr &&
		    iml->multi.imr_address.s_addr==imr->imr_address.s_addr &&
		    (!imr->imr_ifindex || iml->multi.imr_ifindex==imr->imr_ifindex)) {
			struct in_device *in_dev;
			if (--iml->count) {
				rtnl_unlock();
				return 0;
			}

			*imlp = iml->next;

			in_dev = inetdev_by_index(iml->multi.imr_ifindex);
			if (in_dev) {
				ip_mc_dec_group(in_dev, imr->imr_multiaddr.s_addr);
				in_dev_put(in_dev);
			}
			rtnl_unlock();
			sock_kfree_s(sk, iml, sizeof(*iml));
			return 0;
		}
	}
	rtnl_unlock();
	return -EADDRNOTAVAIL;
}

/*
 *	A socket is closing.
 */

void ip_mc_drop_socket(struct sock *sk)
{
	struct ip_mc_socklist *iml;

	if (sk->protinfo.af_inet.mc_list == NULL)
		return;

	rtnl_lock();
	while ((iml=sk->protinfo.af_inet.mc_list) != NULL) {
		struct in_device *in_dev;
		sk->protinfo.af_inet.mc_list = iml->next;

		if ((in_dev = inetdev_by_index(iml->multi.imr_ifindex)) != NULL) {
			ip_mc_dec_group(in_dev, iml->multi.imr_multiaddr.s_addr);
			in_dev_put(in_dev);
		}
		sock_kfree_s(sk, iml, sizeof(*iml));

	}
	rtnl_unlock();
}

int ip_check_mc(struct in_device *in_dev, u32 mc_addr)
{
	struct ip_mc_list *im;

	read_lock(&in_dev->lock);
	for (im=in_dev->mc_list; im; im=im->next) {
		if (im->multiaddr == mc_addr) {
			read_unlock(&in_dev->lock);
			return 1;
		}
	}
	read_unlock(&in_dev->lock);
	return 0;
}


#ifdef CONFIG_IP_MULTICAST
 
int ip_mc_procinfo(char *buffer, char **start, off_t offset, int length)
{
	off_t pos=0, begin=0;
	struct ip_mc_list *im;
	int len=0;
	struct net_device *dev;

	len=sprintf(buffer,"Idx\tDevice    : Count Querier\tGroup    Users Timer\tReporter\n");  

	read_lock(&dev_base_lock);
	for(dev = dev_base; dev; dev = dev->next) {
		struct in_device *in_dev = in_dev_get(dev);
		char   *querier = "NONE";

		if (in_dev == NULL)
			continue;

		querier = IGMP_V1_SEEN(in_dev) ? "V1" : "V2";

		len+=sprintf(buffer+len,"%d\t%-10s: %5d %7s\n",
			     dev->ifindex, dev->name, dev->mc_count, querier);

		read_lock(&in_dev->lock);
		for (im = in_dev->mc_list; im; im = im->next) {
			len+=sprintf(buffer+len,
				     "\t\t\t\t%08lX %5d %d:%08lX\t\t%d\n",
				     im->multiaddr, im->users,
				     im->tm_running, im->timer.expires-jiffies, im->reporter);

			pos=begin+len;
			if(pos<offset)
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length) {
				read_unlock(&in_dev->lock);
				in_dev_put(in_dev);
				goto done;
			}
		}
		read_unlock(&in_dev->lock);
		in_dev_put(in_dev);
	}
done:
	read_unlock(&dev_base_lock);

	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	if(len<0)
		len=0;
	return len;
}
#endif

