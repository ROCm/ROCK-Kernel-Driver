/*
 * IPVS         An implementation of the IP virtual server support for the
 *              LINUX operating system.  IPVS is now implemented as a module
 *              over the Netfilter framework. IPVS can be used to build a
 *              high-performance and highly available server based on a
 *              cluster of servers.
 *
 * Version:     $Id: ip_vs_core.c,v 1.34 2003/05/10 03:05:23 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Peter Kese <peter.kese@ijs.si>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * The IPVS code for kernel 2.2 was done by Wensong Zhang and Peter Kese,
 * with changes/fixes from Julian Anastasov, Lars Marowsky-Bree, Horms
 * and others.
 *
 * Changes:
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/icmp.h>

#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/icmp.h>                   /* for icmp_send */
#include <net/route.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


EXPORT_SYMBOL(register_ip_vs_scheduler);
EXPORT_SYMBOL(unregister_ip_vs_scheduler);
EXPORT_SYMBOL(ip_vs_skb_replace);
EXPORT_SYMBOL(ip_vs_proto_name);
EXPORT_SYMBOL(ip_vs_conn_new);
EXPORT_SYMBOL(ip_vs_conn_in_get);
EXPORT_SYMBOL(ip_vs_conn_out_get);
EXPORT_SYMBOL(ip_vs_tcp_conn_listen);
EXPORT_SYMBOL(ip_vs_conn_put);
#ifdef CONFIG_IP_VS_DEBUG
EXPORT_SYMBOL(ip_vs_get_debug_level);
#endif
EXPORT_SYMBOL(check_for_ip_vs_out);


/* ID used in ICMP lookups */
#define icmp_id(icmph)          ((icmph->un).echo.id)

const char *ip_vs_proto_name(unsigned proto)
{
	static char buf[20];

	switch (proto) {
	case IPPROTO_IP:
		return "IP";
	case IPPROTO_UDP:
		return "UDP";
	case IPPROTO_TCP:
		return "TCP";
	case IPPROTO_ICMP:
		return "ICMP";
	default:
		sprintf(buf, "IP_%d", proto);
		return buf;
	}
}

void ip_vs_init_hash_table(struct list_head *table, int rows)
{
	while (--rows >= 0)
		INIT_LIST_HEAD(&table[rows]);
}

static inline void
ip_vs_in_stats(struct ip_vs_conn *cp, struct sk_buff *skb)
{
	struct ip_vs_dest *dest = cp->dest;
	if (dest && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		spin_lock(&dest->stats.lock);
		dest->stats.inpkts++;
		dest->stats.inbytes += skb->len;
		spin_unlock(&dest->stats.lock);

		spin_lock(&dest->svc->stats.lock);
		dest->svc->stats.inpkts++;
		dest->svc->stats.inbytes += skb->len;
		spin_unlock(&dest->svc->stats.lock);

		spin_lock(&ip_vs_stats.lock);
		ip_vs_stats.inpkts++;
		ip_vs_stats.inbytes += skb->len;
		spin_unlock(&ip_vs_stats.lock);
	}
}


static inline void
ip_vs_out_stats(struct ip_vs_conn *cp, struct sk_buff *skb)
{
	struct ip_vs_dest *dest = cp->dest;
	if (dest && (dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		spin_lock(&dest->stats.lock);
		dest->stats.outpkts++;
		dest->stats.outbytes += skb->len;
		spin_unlock(&dest->stats.lock);

		spin_lock(&dest->svc->stats.lock);
		dest->svc->stats.outpkts++;
		dest->svc->stats.outbytes += skb->len;
		spin_unlock(&dest->svc->stats.lock);

		spin_lock(&ip_vs_stats.lock);
		ip_vs_stats.outpkts++;
		ip_vs_stats.outbytes += skb->len;
		spin_unlock(&ip_vs_stats.lock);
	}
}


static inline void
ip_vs_conn_stats(struct ip_vs_conn *cp, struct ip_vs_service *svc)
{
	spin_lock(&cp->dest->stats.lock);
	cp->dest->stats.conns++;
	spin_unlock(&cp->dest->stats.lock);

	spin_lock(&svc->stats.lock);
	svc->stats.conns++;
	spin_unlock(&svc->stats.lock);

	spin_lock(&ip_vs_stats.lock);
	ip_vs_stats.conns++;
	spin_unlock(&ip_vs_stats.lock);
}


static inline int
ip_vs_set_state(struct ip_vs_conn *cp, int direction,
		struct iphdr *iph, union ip_vs_tphdr h,
		struct ip_vs_protocol *pp)
{
	if (unlikely(!pp->state_transition))
		return 0;
	return pp->state_transition(cp, direction, iph, h, pp);
}


/*
 *  IPVS persistent scheduling function
 *  It creates a connection entry according to its template if exists,
 *  or selects a server and creates a connection entry plus a template.
 *  Locking: we are svc user (svc->refcnt), so we hold all dests too
 *  Protocols supported: TCP, UDP
 */
static struct ip_vs_conn *
ip_vs_sched_persist(struct ip_vs_service *svc, struct iphdr *iph)
{
	struct ip_vs_conn *cp = NULL;
	struct ip_vs_dest *dest;
	const __u16 *portp;
	struct ip_vs_conn *ct;
	__u16  dport;	 /* destination port to forward */
	__u32  snet;	 /* source network of the client, after masking */

	portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);

	/* Mask saddr with the netmask to adjust template granularity */
	snet = iph->saddr & svc->netmask;

	IP_VS_DBG(6, "p-schedule: src %u.%u.%u.%u:%u dest %u.%u.%u.%u:%u "
		  "mnet %u.%u.%u.%u\n",
		  NIPQUAD(iph->saddr), ntohs(portp[0]),
		  NIPQUAD(iph->daddr), ntohs(portp[1]),
		  NIPQUAD(snet));

	/*
	 * As far as we know, FTP is a very complicated network protocol, and
	 * it uses control connection and data connections. For active FTP,
	 * FTP server initialize data connection to the client, its source port
	 * is often 20. For passive FTP, FTP server tells the clients the port
	 * that it passively listens to,  and the client issues the data
	 * connection. In the tunneling or direct routing mode, the load
	 * balancer is on the client-to-server half of connection, the port
	 * number is unknown to the load balancer. So, a conn template like
	 * <caddr, 0, vaddr, 0, daddr, 0> is created for persistent FTP
	 * service, and a template like <caddr, 0, vaddr, vport, daddr, dport>
	 * is created for other persistent services.
	 */
	if (portp[1] == svc->port) {
		/* Check if a template already exists */
		if (svc->port != FTPPORT)
			ct = ip_vs_conn_in_get(iph->protocol, snet, 0,
					       iph->daddr, portp[1]);
		else
			ct = ip_vs_conn_in_get(iph->protocol, snet, 0,
					       iph->daddr, 0);

		if (!ct || !ip_vs_check_template(ct)) {
			/*
			 * No template found or the dest of the connection
			 * template is not available.
			 */
			dest = svc->scheduler->schedule(svc, iph);
			if (dest == NULL) {
				IP_VS_DBG(1, "p-schedule: no dest found.\n");
				return NULL;
			}

			/*
			 * Create a template like <protocol,caddr,0,
			 * vaddr,vport,daddr,dport> for non-ftp service,
			 * and <protocol,caddr,0,vaddr,0,daddr,0>
			 * for ftp service.
			 */
			if (svc->port != FTPPORT)
				ct = ip_vs_conn_new(iph->protocol,
						    snet, 0,
						    iph->daddr, portp[1],
						    dest->addr, dest->port,
						    0,
						    dest);
			else
				ct = ip_vs_conn_new(iph->protocol,
						    snet, 0,
						    iph->daddr, 0,
						    dest->addr, 0,
						    0,
						    dest);
			if (ct == NULL)
				return NULL;

			ct->timeout = svc->timeout;
		} else {
			/* set destination with the found template */
			dest = ct->dest;
		}
		dport = dest->port;
	} else {
		/*
		 * Note: persistent fwmark-based services and persistent
		 * port zero service are handled here.
		 * fwmark template: <IPPROTO_IP,caddr,0,fwmark,0,daddr,0>
		 * port zero template: <protocol,caddr,0,vaddr,0,daddr,0>
		 */
		if (svc->fwmark)
			ct = ip_vs_conn_in_get(IPPROTO_IP, snet, 0,
					       htonl(svc->fwmark), 0);
		else
			ct = ip_vs_conn_in_get(iph->protocol, snet, 0,
					       iph->daddr, 0);

		if (!ct || !ip_vs_check_template(ct)) {
			/*
			 * If it is not persistent port zero, return NULL,
			 * otherwise create a connection template.
			 */
			if (svc->port)
				return NULL;

			dest = svc->scheduler->schedule(svc, iph);
			if (dest == NULL) {
				IP_VS_DBG(1, "p-schedule: no dest found.\n");
				return NULL;
			}

			/*
			 * Create a template according to the service
			 */
			if (svc->fwmark)
				ct = ip_vs_conn_new(IPPROTO_IP,
						    snet, 0,
						    htonl(svc->fwmark), 0,
						    dest->addr, 0,
						    0,
						    dest);
			else
				ct = ip_vs_conn_new(iph->protocol,
						    snet, 0,
						    iph->daddr, 0,
						    dest->addr, 0,
						    0,
						    dest);
			if (ct == NULL)
				return NULL;

			ct->timeout = svc->timeout;
		} else {
			/* set destination with the found template */
			dest = ct->dest;
		}
		dport = portp[1];
	}

	/*
	 *    Create a new connection according to the template
	 */
	cp = ip_vs_conn_new(iph->protocol,
			    iph->saddr, portp[0],
			    iph->daddr, portp[1],
			    dest->addr, dport,
			    0,
			    dest);
	if (cp == NULL) {
		ip_vs_conn_put(ct);
		return NULL;
	}

	/*
	 *    Add its control
	 */
	ip_vs_control_add(cp, ct);
	ip_vs_conn_put(ct);

	ip_vs_conn_stats(cp, svc);
	return cp;
}


/*
 *  IPVS main scheduling function
 *  It selects a server according to the virtual service, and
 *  creates a connection entry.
 *  Protocols supported: TCP, UDP
 */
struct ip_vs_conn *
ip_vs_schedule(struct ip_vs_service *svc, struct iphdr *iph)
{
	struct ip_vs_conn *cp = NULL;
	struct ip_vs_dest *dest;
	const __u16 *portp;

	/*
	 *    Persistent service
	 */
	if (svc->flags & IP_VS_SVC_F_PERSISTENT)
		return ip_vs_sched_persist(svc, iph);

	/*
	 *    Non-persistent service
	 */
	portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	if (!svc->fwmark && portp[1] != svc->port) {
		if (!svc->port)
			IP_VS_ERR("Schedule: port zero only supported "
				  "in persistent services, "
				  "check your ipvs configuration\n");
		return NULL;
	}

	dest = svc->scheduler->schedule(svc, iph);
	if (dest == NULL) {
		IP_VS_DBG(1, "Schedule: no dest found.\n");
		return NULL;
	}

	/*
	 *    Create a connection entry.
	 */
	cp = ip_vs_conn_new(iph->protocol,
			    iph->saddr, portp[0],
			    iph->daddr, portp[1],
			    dest->addr, dest->port?dest->port:portp[1],
			    0,
			    dest);
	if (cp == NULL)
		return NULL;

	IP_VS_DBG(6, "Schedule fwd:%c c:%u.%u.%u.%u:%u v:%u.%u.%u.%u:%u "
		  "d:%u.%u.%u.%u:%u flg:%X cnt:%d\n",
		  ip_vs_fwd_tag(cp),
		  NIPQUAD(cp->caddr), ntohs(cp->cport),
		  NIPQUAD(cp->vaddr), ntohs(cp->vport),
		  NIPQUAD(cp->daddr), ntohs(cp->dport),
		  cp->flags, atomic_read(&cp->refcnt));

	ip_vs_conn_stats(cp, svc);
	return cp;
}


/*
 *  Pass or drop the packet.
 *  Called by ip_vs_in, when the virtual service is available but
 *  no destination is available for a new connection.
 */
int ip_vs_leave(struct ip_vs_service *svc, struct sk_buff *skb,
	       struct ip_vs_protocol *pp, union ip_vs_tphdr h)
{
	struct iphdr *iph = skb->nh.iph;

	/* if it is fwmark-based service, the cache_bypass sysctl is up
	   and the destination is RTN_UNICAST (and not local), then create
	   a cache_bypass connection entry */
	if (sysctl_ip_vs_cache_bypass && svc->fwmark
	    && (inet_addr_type(iph->daddr) == RTN_UNICAST)) {
		int ret, cs;
		struct ip_vs_conn *cp;

		ip_vs_service_put(svc);

		/* create a new connection entry */
		IP_VS_DBG(6, "ip_vs_leave: create a cache_bypass entry\n");
		cp = ip_vs_conn_new(iph->protocol,
				    iph->saddr, h.portp[0],
				    iph->daddr, h.portp[1],
				    0, 0,
				    IP_VS_CONN_F_BYPASS,
				    NULL);
		if (cp == NULL) {
			kfree_skb(skb);
			return NF_STOLEN;
		}

		/* statistics */
		ip_vs_in_stats(cp, skb);

		/* set state */
		cs = ip_vs_set_state(cp, IP_VS_DIR_INPUT, iph, h, pp);

		/* transmit the first SYN packet */
		ret = cp->packet_xmit(skb, cp, pp);

		atomic_inc(&cp->in_pkts);
		ip_vs_conn_put(cp);
		return ret;
	}

	/*
	 * When the virtual ftp service is presented, packets destined
	 * for other services on the VIP may get here (except services
	 * listed in the ipvs table), pass the packets, because it is
	 * not ipvs job to decide to drop the packets.
	 */
	if ((svc->port == FTPPORT) && (h.portp[1] != FTPPORT)) {
		ip_vs_service_put(svc);
		return NF_ACCEPT;
	}

	ip_vs_service_put(svc);

	/*
	 * Notify the client that the destination is unreachable, and
	 * release the socket buffer.
	 * Since it is in IP layer, the TCP socket is not actually
	 * created, the TCP RST packet cannot be sent, instead that
	 * ICMP_PORT_UNREACH is sent here no matter it is TCP/UDP. --WZ
	 */
	icmp_send(skb, ICMP_DEST_UNREACH, ICMP_PORT_UNREACH, 0);
	kfree_skb(skb);
	return NF_STOLEN;
}


/*
 *      It is hooked before NF_IP_PRI_NAT_SRC at the NF_IP_POST_ROUTING
 *      chain, and is used for VS/NAT.
 *      It detects packets for VS/NAT connections and sends the packets
 *      immediately. This can avoid that iptable_nat mangles the packets
 *      for VS/NAT.
 */
static unsigned int ip_vs_post_routing(unsigned int hooknum,
				       struct sk_buff **skb_p,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	struct sk_buff  *skb = *skb_p;

	if (!(skb->nfcache & NFC_IPVS_PROPERTY))
		return NF_ACCEPT;

	/* The packet was sent from IPVS, exit this chain */
	(*okfn)(skb);

	return NF_STOLEN;
}


/*
 *	Handle ICMP messages in the inside-to-outside direction (outgoing).
 *	Find any that might be relevant, check against existing connections,
 *	forward to the right destination host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded.
 *	(Only used in VS/NAT)
 */
static int ip_vs_out_icmp(struct sk_buff **skb_p, int *related)
{
	struct sk_buff	*skb   = *skb_p;
	struct iphdr	*iph;
	struct icmphdr	*icmph;
	struct iphdr	*ciph;	/* The ip header contained within the ICMP */
	unsigned short	ihl;
	unsigned short	len;
	unsigned short	clen, cihl;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	union ip_vs_tphdr h;

	*related = 1;

	/* reassemble IP fragments, but will it happen in ICMP packets?? */
	if (skb->nh.iph->frag_off & __constant_htons(IP_MF|IP_OFFSET)) {
		skb = ip_defrag(skb);
		if (!skb)
			return NF_STOLEN;
		*skb_p = skb;
	}

	if (skb_is_nonlinear(skb)) {
		if (skb_linearize(skb, GFP_ATOMIC) != 0)
			return NF_DROP;
		ip_send_check(skb->nh.iph);
	}

	iph = skb->nh.iph;
	ihl = iph->ihl << 2;
	icmph = (struct icmphdr *)((char *)iph + ihl);
	len   = ntohs(iph->tot_len) - ihl;
	if (len < sizeof(struct icmphdr))
		return NF_DROP;

	IP_VS_DBG(12, "outgoing ICMP (%d,%d) %u.%u.%u.%u->%u.%u.%u.%u\n",
		  icmph->type, ntohs(icmp_id(icmph)),
		  NIPQUAD(iph->saddr), NIPQUAD(iph->daddr));

	/*
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that means easy
	 * things are checked first to speed up processing.... however
	 * this means that some packets will manage to get a long way
	 * down this stack and then be rejected, but that's life.
	 */
	if ((icmph->type != ICMP_DEST_UNREACH) &&
	    (icmph->type != ICMP_SOURCE_QUENCH) &&
	    (icmph->type != ICMP_TIME_EXCEEDED)) {
		*related = 0;
		return NF_ACCEPT;
	}

	/* Now find the contained IP header */
	clen = len - sizeof(struct icmphdr);
	if (clen < sizeof(struct iphdr))
		return NF_DROP;
	ciph = (struct iphdr *) (icmph + 1);
	cihl = ciph->ihl << 2;
	if (clen < cihl)
		return NF_DROP;

	pp = ip_vs_proto_get(ciph->protocol);
	if (!pp)
		return NF_ACCEPT;

	/* Is the embedded protocol header present? */
	if (unlikely(ciph->frag_off & __constant_htons(IP_OFFSET) &&
		     (pp->minhlen || pp->dont_defrag)))
		return NF_ACCEPT;

	/* We need at least TCP/UDP ports here */
	if (clen < cihl + pp->minhlen_icmp)
		return NF_DROP;

	h.raw = (char *) ciph + cihl;

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len)) {
		/* Failed checksum! */
		IP_VS_DBG(1, "forward ICMP: failed checksum from %d.%d.%d.%d!\n",
			  NIPQUAD(iph->saddr));
		return NF_DROP;
	}

	IP_VS_DBG_PKT(11, pp, ciph, "Handling outgoing ICMP for");

	/* ciph content is actually <protocol, caddr, cport, daddr, dport> */
	cp = pp->conn_out_get(skb, pp, ciph, h, 1);
	if (!cp)
		return NF_ACCEPT;

	if (IP_VS_FWD_METHOD(cp) != 0) {
		IP_VS_ERR("shouldn't reach here, because the box is on the"
			  "half connection in the tun/dr module.\n");
	}

	/* Now we do real damage to this packet...! */
	/* First change the source IP address, and recalc checksum */
	iph->saddr = cp->vaddr;
	ip_send_check(iph);

	/* Now change the *dest* address in the contained IP */
	ciph->daddr = cp->vaddr;
	ip_send_check(ciph);

	/* the TCP/UDP dest port - cannot redo check */
	if (IPPROTO_TCP == ciph->protocol || IPPROTO_UDP == ciph->protocol)
		h.portp[1] = cp->vport;

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* do the statistics and put it back */
	ip_vs_out_stats(cp, skb);
	__ip_vs_conn_put(cp);

	IP_VS_DBG_PKT(11, pp, ciph, "Forwarding correct outgoing ICMP");

	skb->nfcache |= NFC_IPVS_PROPERTY;

	return NF_ACCEPT;
}


/*
 *	It is hooked at the NF_IP_FORWARD chain, used only for VS/NAT.
 *	Check if outgoing packet belongs to the established ip_vs_conn,
 *      rewrite addresses of the packet and send it on its way...
 */
static unsigned int
ip_vs_out(unsigned int hooknum, struct sk_buff **skb_p,
	  const struct net_device *in, const struct net_device *out,
	  int (*okfn)(struct sk_buff *))
{
	struct sk_buff  *skb = *skb_p;
	struct iphdr	*iph;
	struct ip_vs_protocol *pp;
	union ip_vs_tphdr h;
	struct ip_vs_conn *cp;
	int size, ihl, firstfrag;

	EnterFunction(11);

	if (skb->nfcache & NFC_IPVS_PROPERTY)
		return NF_ACCEPT;

	iph = skb->nh.iph;
	if (unlikely(iph->protocol == IPPROTO_ICMP)) {
		int related, verdict = ip_vs_out_icmp(skb_p, &related);

		if (related)
			return verdict;
	}

	pp = ip_vs_proto_get(iph->protocol);
	if (unlikely(!pp))
		return NF_ACCEPT;

	/* reassemble IP fragments */
	if (unlikely(iph->frag_off & __constant_htons(IP_MF|IP_OFFSET) &&
		     !pp->dont_defrag)) {
		skb = ip_defrag(skb);
		if (!skb)
			return NF_STOLEN;
		iph = skb->nh.iph;
		*skb_p = skb;
	}

	/* make sure that protocol header is available in skb data area,
	   note that skb data area may be reallocated. */
	ihl = iph->ihl << 2;
	firstfrag = !(iph->frag_off & __constant_htons(IP_OFFSET));
	/*
	 * WARNING: we can work with !firstfrag packets, make sure
	 * each protocol handler checks for firstfrag
	 */
	if (firstfrag &&
	    !pskb_may_pull(skb, ihl+pp->minhlen))
		return NF_DROP;
	iph = skb->nh.iph;
	h.raw = (char*) iph + ihl;

	/*
	 * Check if the packet belongs to an existing entry
	 */
	cp = pp->conn_out_get(skb, pp, iph, h, 0);

	if (unlikely(!cp)) {
		if (sysctl_ip_vs_nat_icmp_send &&
		    (pp->protocol == IPPROTO_TCP ||
		     pp->protocol == IPPROTO_UDP) &&
		    ip_vs_lookup_real_service(iph->protocol,
					      iph->saddr, h.portp[0])) {
			/*
			 * Notify the real server: there is no existing
			 * entry if it is not RST packet or not TCP packet.
			 */
			if (!h.th->rst || iph->protocol != IPPROTO_TCP) {
				icmp_send(skb, ICMP_DEST_UNREACH,
					  ICMP_PORT_UNREACH, 0);
				kfree_skb(skb);
				return NF_STOLEN;
			}
		}
		IP_VS_DBG_PKT(12, pp, iph,
			      "packet continues traversal as normal");
		if (!pp->dont_defrag)
			ip_send_check(iph);
		return NF_ACCEPT;
	}

	/*
	 * If it has ip_vs_app helper, the helper may change the payload,
	 * so it needs full checksum checking and checksum calculation.
	 * If not, only the header (addr/port) is changed, so it is fast
	 * to do incremental checksum update, and let the destination host
	 * do final checksum checking.
	 */

	if (unlikely(cp->app && !pp->slave && skb_is_nonlinear(skb))) {
		if (skb_linearize(skb, GFP_ATOMIC) != 0) {
			ip_vs_conn_put(cp);
			return NF_DROP;
		}
		iph = skb->nh.iph;
		h.raw = (char*) iph + ihl;
	}

	size = skb->len - ihl;
	IP_VS_DBG(11, "O-pkt: %s size=%d\n", pp->name, size);

	/* do TCP/UDP checksum checking if it has application helper */
	if (unlikely(cp->app && pp->csum_check && !pp->slave)) {
		if (!pp->csum_check(skb, pp, iph, h, size)) {
			ip_vs_conn_put(cp);
			return NF_DROP;
		}
	}

	IP_VS_DBG_PKT(11, pp, iph, "Outgoing packet");

	/* mangle the packet */
	iph->saddr = cp->vaddr;
	if (pp->snat_handler) {
		pp->snat_handler(skb, pp, cp, iph, h, size);
		iph = skb->nh.iph;
		h.raw = (char*) iph + ihl;
	}
	ip_send_check(iph);

	IP_VS_DBG_PKT(10, pp, iph, "After SNAT");

	ip_vs_out_stats(cp, skb);
	ip_vs_set_state(cp, IP_VS_DIR_OUTPUT, iph, h, pp);
	ip_vs_conn_put(cp);

	skb->nfcache |= NFC_IPVS_PROPERTY;

	LeaveFunction(11);
	return NF_ACCEPT;
}


/*
 *      Check if the packet is for VS/NAT connections, then send it
 *      immediately.
 *      Called by ip_fw_compact to detect packets for VS/NAT before
 *      they are changed by ipchains masquerading code.
 */
unsigned int
check_for_ip_vs_out(struct sk_buff **skb_p, int (*okfn)(struct sk_buff *))
{
	unsigned int ret;

	ret = ip_vs_out(NF_IP_FORWARD, skb_p, NULL, NULL, NULL);
	if (ret != NF_ACCEPT) {
		return ret;
	} else {
		/* send the packet immediately if it is already mangled
		   by ip_vs_out */
		if ((*skb_p)->nfcache & NFC_IPVS_PROPERTY) {
			(*okfn)(*skb_p);
			return NF_STOLEN;
		}
	}
	return NF_ACCEPT;
}


/*
 *	Handle ICMP messages in the outside-to-inside direction (incoming).
 *	Find any that might be relevant, check against existing connections,
 *	forward to the right destination host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */
static int ip_vs_in_icmp(struct sk_buff **skb_p, int *related)
{
	struct sk_buff	*skb = *skb_p;
	struct iphdr    *iph;
	struct icmphdr  *icmph;
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	unsigned short	len;
	unsigned short	clen, cihl;
	struct ip_vs_conn *cp;
	struct ip_vs_protocol *pp;
	union ip_vs_tphdr h;
	int	rc;

	*related = 1;
	if (skb_is_nonlinear(skb)) {
		if (skb_linearize(skb, GFP_ATOMIC) != 0)
			return NF_DROP;
		ip_send_check(skb->nh.iph);
	}

	iph = skb->nh.iph;
	icmph = (struct icmphdr *)((char *)iph+(iph->ihl<<2));
	len = ntohs(iph->tot_len) - (iph->ihl<<2);
	if (len < sizeof(struct icmphdr))
		return NF_DROP;

	IP_VS_DBG(12, "icmp in (%d,%d) %u.%u.%u.%u -> %u.%u.%u.%u\n",
		  icmph->type, ntohs(icmp_id(icmph)),
		  NIPQUAD(iph->saddr), NIPQUAD(iph->daddr));

	if ((icmph->type != ICMP_DEST_UNREACH) &&
	    (icmph->type != ICMP_SOURCE_QUENCH) &&
	    (icmph->type != ICMP_TIME_EXCEEDED)) {
		*related = 0;
		return NF_ACCEPT;
	}

	/*
	 * If we get here we have an ICMP error of one of the above 3 types
	 * Now find the contained IP header
	 */
	clen = len - sizeof(struct icmphdr);
	if (clen < sizeof(struct iphdr))
		return NF_DROP;
	ciph = (struct iphdr *) (icmph + 1);
	cihl = ciph->ihl << 2;
	if (clen < cihl)
		return NF_DROP;

	pp = ip_vs_proto_get(ciph->protocol);
	if (!pp)
		return NF_ACCEPT;

	/* Is the embedded protocol header present? */
	if (unlikely(ciph->frag_off & __constant_htons(IP_OFFSET) &&
		     (pp->minhlen || pp->dont_defrag)))
		return NF_ACCEPT;

	/* We need at least TCP/UDP ports here */
	if (clen < cihl + pp->minhlen_icmp)
		return NF_DROP;

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len)) {
		/* Failed checksum! */
		IP_VS_ERR_RL("incoming ICMP: failed checksum from "
			     "%d.%d.%d.%d!\n", NIPQUAD(iph->saddr));
		return NF_DROP;
	}

	h.raw = (char *) ciph + cihl;

	IP_VS_DBG_PKT(11, pp, ciph, "Handling incoming ICMP for");

	/* This is pretty much what ip_vs_conn_in_get() does,
	   except parameters are in the reverse order */
	cp = pp->conn_in_get(skb, pp, ciph, h, 1);
	if (cp == NULL)
		return NF_ACCEPT;

	ip_vs_in_stats(cp, skb);
	rc = ip_vs_icmp_xmit(skb, cp, pp);
	__ip_vs_conn_put(cp);
	return rc;
}


/*
 *	Check if it's for virtual services, look it up,
 *	and send it on its way...
 */
static unsigned int
ip_vs_in(unsigned int hooknum, struct sk_buff **skb_p,
	 const struct net_device *in, const struct net_device *out,
	 int (*okfn)(struct sk_buff *))
{
	struct sk_buff	*skb = *skb_p;
	struct iphdr	*iph = skb->nh.iph;
	struct ip_vs_protocol *pp = ip_vs_proto_get(iph->protocol);
	union ip_vs_tphdr h;
	struct ip_vs_conn *cp;
	int ihl, ret, restart;
	int firstfrag;

	/*
	 *	Big tappo: only PACKET_HOST (neither loopback nor mcasts)
	 *	... don't know why 1st test DOES NOT include 2nd (?)
	 */
	if (unlikely(skb->pkt_type != PACKET_HOST || skb->dev == &loopback_dev)) {
		IP_VS_DBG(12, "packet type=%d proto=%d daddr=%d.%d.%d.%d ignored\n",
			  skb->pkt_type,
			  iph->protocol,
			  NIPQUAD(iph->daddr));
		return NF_ACCEPT;
	}

	if (unlikely(iph->protocol == IPPROTO_ICMP)) {
		int related, verdict = ip_vs_in_icmp(skb_p, &related);

		if (related)
			return verdict;
	}

	/* Protocol supported? */
	if (unlikely(!pp))
		return NF_ACCEPT;

	/* make sure that protocol header is available in skb data area,
	   note that skb data area may be reallocated. */
	ihl = iph->ihl << 2;
#if 0
	/* Enable this when not in LOCAL_IN */
	firstfrag = !(iph->frag_off & __constant_htons(IP_OFFSET));
	/*
	 * WARNING: we can work with !firstfrag packets, make sure
	 * each protocol handler checks for firstfrag
	 */
#else
	firstfrag = 1;
#endif
	if (firstfrag &&
	    !pskb_may_pull(skb, ihl+pp->minhlen))
		return NF_DROP;
	iph = skb->nh.iph;
	h.raw = (char*) iph + ihl;

	/*
	 * Check if the packet belongs to an existing connection entry
	 */
	cp = pp->conn_in_get(skb, pp, iph, h, 0);

	if (unlikely(!cp)) {
		int v;

		if (!pp->conn_schedule(skb, pp, iph, h, &v, &cp)) {
			return v;
		}
	}

	if (unlikely(!cp)) {
		/* sorry, all this trouble for a no-hit :) */
		IP_VS_DBG_PKT(12, pp, iph,
			"packet continues traversal as normal");
		return NF_ACCEPT;
	}

	IP_VS_DBG_PKT(11, pp, iph, "Incoming packet");

	/* Check the server status */
	if (cp->dest && !(cp->dest->flags & IP_VS_DEST_F_AVAILABLE)) {
		/* the destination server is not availabe */

		if (sysctl_ip_vs_expire_nodest_conn) {
			/* try to expire the connection immediately */
			ip_vs_conn_expire_now(cp);
		} else {
			/* don't restart its timer, and silently
			   drop the packet. */
			__ip_vs_conn_put(cp);
		}
		return NF_DROP;
	}

	ip_vs_in_stats(cp, skb);
	restart = ip_vs_set_state(cp, IP_VS_DIR_INPUT, iph, h, pp);
	if (cp->packet_xmit)
		ret = cp->packet_xmit(skb, cp, pp);
	else {
		IP_VS_DBG_RL("warning: packet_xmit is null");
		ret = NF_ACCEPT;
	}

	/* increase its packet counter and check if it is needed
	   to be synchronized */
	atomic_inc(&cp->in_pkts);
	if (ip_vs_sync_state == IP_VS_STATE_MASTER &&
	    (cp->protocol != IPPROTO_TCP ||
	     cp->state == IP_VS_TCP_S_ESTABLISHED) &&
	    (atomic_read(&cp->in_pkts) % sysctl_ip_vs_sync_threshold[1]
	     == sysctl_ip_vs_sync_threshold[0]))
		ip_vs_sync_conn(cp);

	ip_vs_conn_put(cp);
	return ret;
}


/*
 *	It is hooked at the NF_IP_FORWARD chain, in order to catch ICMP
 *      packets destined for 0.0.0.0/0.
 *      When fwmark-based virtual service is used, such as transparent
 *      cache cluster, TCP packets can be marked and routed to ip_vs_in,
 *      but ICMP destined for 0.0.0.0/0 cannot not be easily marked and
 *      sent to ip_vs_in_icmp. So, catch them at the NF_IP_FORWARD chain
 *      and send them to ip_vs_in_icmp.
 */
static unsigned int
ip_vs_forward_icmp(unsigned int hooknum, struct sk_buff **skb_p,
		   const struct net_device *in, const struct net_device *out,
		   int (*okfn)(struct sk_buff *))
{
	struct sk_buff	*skb = *skb_p;
	struct iphdr	*iph = skb->nh.iph;
	int r;

	if (iph->protocol != IPPROTO_ICMP)
		return NF_ACCEPT;

	if (iph->frag_off & __constant_htons(IP_MF|IP_OFFSET)) {
		skb = ip_defrag(skb);
		if (!skb)
			return NF_STOLEN;
		*skb_p = skb;
	}

	return ip_vs_in_icmp(skb_p, &r);
}


/* After packet filtering, forward packet through VS/DR, VS/TUN,
   or VS/NAT(change destination), so that filtering rules can be
   applied to IPVS. */
static struct nf_hook_ops ip_vs_in_ops = {
	.hook		= ip_vs_in,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum        = NF_IP_LOCAL_IN,
	.priority       = 100,
};

/* After packet filtering, change source only for VS/NAT */
static struct nf_hook_ops ip_vs_out_ops = {
	.hook		= ip_vs_out,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum        = NF_IP_FORWARD,
	.priority       = 100,
};

/* After packet filtering (but before ip_vs_out_icmp), catch icmp
   destined for 0.0.0.0/0, which is for incoming IPVS connections */
static struct nf_hook_ops ip_vs_forward_icmp_ops = {
	.hook		= ip_vs_forward_icmp,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum        = NF_IP_FORWARD,
	.priority       = 99,
};

/* Before the netfilter connection tracking, exit from POST_ROUTING */
static struct nf_hook_ops ip_vs_post_routing_ops = {
	.hook		= ip_vs_post_routing,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum        = NF_IP_POST_ROUTING,
	.priority       = NF_IP_PRI_NAT_SRC-1,
};


/*
 *	Initialize IP Virtual Server
 */
static int __init ip_vs_init(void)
{
	int ret;

	ret = ip_vs_control_init();
	if (ret < 0) {
		IP_VS_ERR("can't setup control.\n");
		goto cleanup_nothing;
	}

	ip_vs_sltimer_init();
	ip_vs_protocol_init();

	ret = ip_vs_app_init();
	if (ret < 0) {
		IP_VS_ERR("can't setup application helper.\n");
		goto cleanup_protocol;
	}

	ret = ip_vs_conn_init();
	if (ret < 0) {
		IP_VS_ERR("can't setup connection table.\n");
		goto cleanup_app;
	}

	ret = nf_register_hook(&ip_vs_in_ops);
	if (ret < 0) {
		IP_VS_ERR("can't register in hook.\n");
		goto cleanup_conn;
	}

	ret = nf_register_hook(&ip_vs_out_ops);
	if (ret < 0) {
		IP_VS_ERR("can't register out hook.\n");
		goto cleanup_inops;
	}
	ret = nf_register_hook(&ip_vs_post_routing_ops);
	if (ret < 0) {
		IP_VS_ERR("can't register post_routing hook.\n");
		goto cleanup_outops;
	}
	ret = nf_register_hook(&ip_vs_forward_icmp_ops);
	if (ret < 0) {
		IP_VS_ERR("can't register forward_icmp hook.\n");
		goto cleanup_postroutingops;
	}

	IP_VS_INFO("ipvs loaded.\n");
	return ret;

  cleanup_postroutingops:
	nf_unregister_hook(&ip_vs_post_routing_ops);
  cleanup_outops:
	nf_unregister_hook(&ip_vs_out_ops);
  cleanup_inops:
	nf_unregister_hook(&ip_vs_in_ops);
  cleanup_conn:
	ip_vs_conn_cleanup();
  cleanup_app:
	ip_vs_app_cleanup();
  cleanup_protocol:
	ip_vs_protocol_cleanup();
	ip_vs_sltimer_cleanup();
	ip_vs_control_cleanup();
  cleanup_nothing:
	return ret;
}

static void __exit ip_vs_cleanup(void)
{
	nf_unregister_hook(&ip_vs_forward_icmp_ops);
	nf_unregister_hook(&ip_vs_post_routing_ops);
	nf_unregister_hook(&ip_vs_out_ops);
	nf_unregister_hook(&ip_vs_in_ops);
	ip_vs_conn_cleanup();
	ip_vs_app_cleanup();
	ip_vs_protocol_cleanup();
	ip_vs_sltimer_cleanup();
	ip_vs_control_cleanup();
	IP_VS_INFO("ipvs unloaded.\n");
}

module_init(ip_vs_init);
module_exit(ip_vs_cleanup);
MODULE_LICENSE("GPL");
