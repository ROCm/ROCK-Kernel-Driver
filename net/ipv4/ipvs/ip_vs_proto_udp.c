/*
 * ip_vs_proto_udp.c:	UDP load balancing support for IPVS
 *
 * Version:     $Id: ip_vs_proto_udp.c,v 1.3 2002/11/30 01:50:35 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */

#include <linux/kernel.h>
#include <linux/netfilter.h>

#include <net/ip_vs.h>


static struct ip_vs_conn *
udp_conn_in_get(struct sk_buff *skb, struct ip_vs_protocol *pp,
		struct iphdr *iph, union ip_vs_tphdr h, int inverse)
{
	struct ip_vs_conn *cp;

	if (likely(!inverse)) {
		cp = ip_vs_conn_in_get(iph->protocol,
			iph->saddr, h.portp[0],
			iph->daddr, h.portp[1]);
	} else {
		cp = ip_vs_conn_in_get(iph->protocol,
			iph->daddr, h.portp[1],
			iph->saddr, h.portp[0]);
	}

	return cp;
}


static struct ip_vs_conn *
udp_conn_out_get(struct sk_buff *skb, struct ip_vs_protocol *pp,
		 struct iphdr *iph, union ip_vs_tphdr h, int inverse)
{
	struct ip_vs_conn *cp;

	if (likely(!inverse)) {
		cp = ip_vs_conn_out_get(iph->protocol,
			iph->saddr, h.portp[0],
			iph->daddr, h.portp[1]);
	} else {
		cp = ip_vs_conn_out_get(iph->protocol,
			iph->daddr, h.portp[1],
			iph->saddr, h.portp[0]);
	}

	return cp;
}


static int
udp_conn_schedule(struct sk_buff *skb, struct ip_vs_protocol *pp,
		  struct iphdr *iph, union ip_vs_tphdr h,
		  int *verdict, struct ip_vs_conn **cpp)
{
	struct ip_vs_service *svc;

	if ((svc = ip_vs_service_get(skb->nfmark, iph->protocol,
				     iph->daddr, h.portp[1]))) {
		if (ip_vs_todrop()) {
			/*
			 * It seems that we are very loaded.
			 * We have to drop this packet :(
			 */
			ip_vs_service_put(svc);
			*verdict = NF_DROP;
			return 0;
		}

		/*
		 * Let the virtual server select a real server for the
		 * incoming connection, and create a connection entry.
		 */
		*cpp = ip_vs_schedule(svc, iph);
		if (!*cpp) {
			*verdict = ip_vs_leave(svc, skb, pp, h);
			return 0;
		}
		ip_vs_service_put(svc);
	}
	return 1;
}


static inline void
udp_fast_csum_update(union ip_vs_tphdr *h, u32 oldip, u32 newip,
		     u16 oldport, u16 newport)
{
	h->uh->check =
		ip_vs_check_diff(~oldip, newip,
				 ip_vs_check_diff(oldport ^ 0xFFFF,
						  newport, h->uh->check));
	if (!h->uh->check)
		h->uh->check = 0xFFFF;
}

static int
udp_snat_handler(struct sk_buff *skb,
		 struct ip_vs_protocol *pp, struct ip_vs_conn *cp,
		 struct iphdr *iph, union ip_vs_tphdr h, int size)
{
	int ihl = (char *) h.raw - (char *) iph;

	/* We are sure that we work on first fragment */

	h.portp[0] = cp->vport;

	/*
	 *	Call application helper if needed
	 */
	if (ip_vs_app_pkt_out(cp, skb) != 0) {
		/* skb data has probably changed, update pointers */
		iph = skb->nh.iph;
		h.raw = (char*)iph + ihl;
		size = skb->len - ihl;
	}

	/*
	 *	Adjust UDP checksums
	 */
	if (!cp->app && (h.uh->check != 0)) {
		/* Only port and addr are changed, do fast csum update */
		udp_fast_csum_update(&h, cp->daddr, cp->vaddr,
				     cp->dport, cp->vport);
		if (skb->ip_summed == CHECKSUM_HW)
			skb->ip_summed = CHECKSUM_NONE;
	} else {
		/* full checksum calculation */
		h.uh->check = 0;
		skb->csum = csum_partial(h.raw, size, 0);
		h.uh->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
						size, iph->protocol,
						skb->csum);
		if (h.uh->check == 0)
			h.uh->check = 0xFFFF;
		IP_VS_DBG(11, "O-pkt: %s O-csum=%d (+%d)\n",
			  pp->name, h.uh->check,
			  (char*)&(h.uh->check) - (char*)h.raw);
	}
	return 1;
}


static int
udp_dnat_handler(struct sk_buff *skb,
		 struct ip_vs_protocol *pp, struct ip_vs_conn *cp,
		 struct iphdr *iph, union ip_vs_tphdr h, int size)
{
	int ihl = (char *) h.raw - (char *) iph;

	/* We are sure that we work on first fragment */

	h.portp[1] = cp->dport;

	/*
	 *	Attempt ip_vs_app call.
	 *	will fix ip_vs_conn and iph ack_seq stuff
	 */
	if (ip_vs_app_pkt_in(cp, skb) != 0) {
		/* skb data has probably changed, update pointers */
		iph = skb->nh.iph;
		h.raw = (char*) iph + ihl;
		size = skb->len - ihl;
	}

	/*
	 *	Adjust UDP checksums
	 */
	if (!cp->app && (h.uh->check != 0)) {
		/* Only port and addr are changed, do fast csum update */
		udp_fast_csum_update(&h, cp->vaddr, cp->daddr,
				     cp->vport, cp->dport);
		if (skb->ip_summed == CHECKSUM_HW)
			skb->ip_summed = CHECKSUM_NONE;
	} else {
		/* full checksum calculation */
		h.uh->check = 0;
		h.uh->check = csum_tcpudp_magic(iph->saddr, iph->daddr,
						size, iph->protocol,
						csum_partial(h.raw, size, 0));
		if (h.uh->check == 0)
			h.uh->check = 0xFFFF;
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	}
	return 1;
}


static int
udp_csum_check(struct sk_buff *skb, struct ip_vs_protocol *pp,
	       struct iphdr *iph, union ip_vs_tphdr h, int size)
{
	if (h.uh->check != 0) {
		switch (skb->ip_summed) {
		case CHECKSUM_NONE:
			skb->csum = csum_partial(h.raw, size, 0);
		case CHECKSUM_HW:
			if (csum_tcpudp_magic(iph->saddr, iph->daddr, size,
					      iph->protocol, skb->csum)) {
				IP_VS_DBG_RL_PKT(0, pp, iph,
						 "Failed checksum for");
				return 0;
			}
			break;
		default:
			/* CHECKSUM_UNNECESSARY */
			break;
		}
	}
	return 1;
}


/*
 *	Note: the caller guarantees that only one of register_app,
 *	unregister_app or app_conn_bind is called each time.
 */

#define	UDP_APP_TAB_BITS	4
#define	UDP_APP_TAB_SIZE	(1 << UDP_APP_TAB_BITS)
#define	UDP_APP_TAB_MASK	(UDP_APP_TAB_SIZE - 1)

static struct list_head udp_apps[UDP_APP_TAB_SIZE];
static spinlock_t udp_app_lock = SPIN_LOCK_UNLOCKED;

static inline __u16 udp_app_hashkey(__u16 port)
{
	return ((port >> UDP_APP_TAB_BITS) ^ port) & UDP_APP_TAB_MASK;
}


static int udp_register_app(struct ip_vs_app *inc)
{
	struct ip_vs_app *i;
	__u16 hash, port = inc->port;
	int ret = 0;

	hash = udp_app_hashkey(port);


	spin_lock_bh(&udp_app_lock);
	list_for_each_entry(i, &udp_apps[hash], p_list) {
		if (i->port == port) {
			ret = -EEXIST;
			goto out;
		}
	}
	list_add(&inc->p_list, &udp_apps[hash]);
	atomic_inc(&ip_vs_protocol_udp.appcnt);

  out:
	spin_unlock_bh(&udp_app_lock);
	return ret;
}


static void
udp_unregister_app(struct ip_vs_app *inc)
{
	spin_lock_bh(&udp_app_lock);
	atomic_dec(&ip_vs_protocol_udp.appcnt);
	list_del(&inc->p_list);
	spin_unlock_bh(&udp_app_lock);
}


static int udp_app_conn_bind(struct ip_vs_conn *cp)
{
	int hash;
	struct ip_vs_app *inc;
	int result = 0;

	/* Default binding: bind app only for NAT */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ)
		return 0;

	/* Lookup application incarnations and bind the right one */
	hash = udp_app_hashkey(cp->vport);

	spin_lock(&udp_app_lock);
	list_for_each_entry(inc, &udp_apps[hash], p_list) {
		if (inc->port == cp->vport) {
			if (unlikely(!ip_vs_app_inc_get(inc)))
				break;
			spin_unlock(&udp_app_lock);

			IP_VS_DBG(9, "%s: Binding conn %u.%u.%u.%u:%u->"
				  "%u.%u.%u.%u:%u to app %s on port %u\n",
				  __FUNCTION__,
				  NIPQUAD(cp->caddr), ntohs(cp->cport),
				  NIPQUAD(cp->vaddr), ntohs(cp->vport),
				  inc->name, ntohs(inc->port));
			cp->app = inc;
			if (inc->init_conn)
				result = inc->init_conn(inc, cp);
			goto out;
		}
	}
	spin_unlock(&udp_app_lock);

  out:
	return result;
}


static int udp_timeouts[IP_VS_UDP_S_LAST+1] = {
	[IP_VS_UDP_S_NORMAL]		=	5*60*HZ,
	[IP_VS_UDP_S_LAST]		=	2*HZ,
};

static char * udp_state_name_table[IP_VS_UDP_S_LAST+1] = {
	[IP_VS_UDP_S_NORMAL]		=	"UDP",
	[IP_VS_UDP_S_LAST]		=	"BUG!",
};


static int
udp_set_state_timeout(struct ip_vs_protocol *pp, char *sname, int to)
{
	return ip_vs_set_state_timeout(pp->timeout_table, IP_VS_UDP_S_LAST,
				       udp_state_name_table, sname, to);
}

static const char * udp_state_name(int state)
{
	if (state >= IP_VS_UDP_S_LAST)
		return "ERR!";
	return udp_state_name_table[state] ? udp_state_name_table[state] : "?";
}

static int
udp_state_transition(struct ip_vs_conn *cp,
		     int direction, struct iphdr *iph,
		     union ip_vs_tphdr h, struct ip_vs_protocol *pp)
{
	cp->timeout = pp->timeout_table[IP_VS_UDP_S_NORMAL];
	return 1;
}

static void udp_init(struct ip_vs_protocol *pp)
{
	IP_VS_INIT_HASH_TABLE(udp_apps);
	pp->timeout_table = udp_timeouts;
}

static void udp_exit(struct ip_vs_protocol *pp)
{
}


extern void
tcpudp_debug_packet(struct ip_vs_protocol *pp, struct iphdr *iph, char *msg);

struct ip_vs_protocol ip_vs_protocol_udp = {
	.name =			"UDP",
	.protocol =		IPPROTO_UDP,
	.minhlen =		sizeof(struct udphdr),
	.minhlen_icmp =		8,
	.dont_defrag =		0,
	.skip_nonexisting =	0,
	.slave =		0,
	.init =			udp_init,
	.exit =			udp_exit,
	.conn_schedule =	udp_conn_schedule,
	.conn_in_get =		udp_conn_in_get,
	.conn_out_get =		udp_conn_out_get,
	.snat_handler =		udp_snat_handler,
	.dnat_handler =		udp_dnat_handler,
	.csum_check =		udp_csum_check,
	.state_transition =	udp_state_transition,
	.state_name =		udp_state_name,
	.register_app =		udp_register_app,
	.unregister_app =	udp_unregister_app,
	.app_conn_bind =	udp_app_conn_bind,
	.debug_packet =		tcpudp_debug_packet,
	.timeout_change =	NULL,
	.set_state_timeout =	udp_set_state_timeout,
};
