/*
 * ip_vs_proto_icmp.c:	ICMP load balancing support for IP Virtual Server
 *
 * Authors:	Julian Anastasov <ja@ssi.bg>, March 2002
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		version 2 as published by the Free Software Foundation;
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/compiler.h>
#include <linux/vmalloc.h>
#include <linux/icmp.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


static int icmp_timeouts[1] =		{ 1*60*HZ };

static char * icmp_state_name_table[1] = { "ICMP" };

struct ip_vs_conn *
icmp_conn_in_get(struct sk_buff *skb, struct ip_vs_protocol *pp,
		 struct iphdr *iph, union ip_vs_tphdr h, int inverse)
{
#if 0
	struct ip_vs_conn *cp;

	if (likely(!inverse)) {
		cp = ip_vs_conn_in_get(iph->protocol,
			iph->saddr, 0,
			iph->daddr, 0);
	} else {
		cp = ip_vs_conn_in_get(iph->protocol,
			iph->daddr, 0,
			iph->saddr, 0);
	}

	return cp;

#else
	return NULL;
#endif
}

struct ip_vs_conn *
icmp_conn_out_get(struct sk_buff *skb, struct ip_vs_protocol *pp,
		  struct iphdr *iph, union ip_vs_tphdr h, int inverse)
{
#if 0
	struct ip_vs_conn *cp;

	if (likely(!inverse)) {
		cp = ip_vs_conn_out_get(iph->protocol,
			iph->saddr, 0,
			iph->daddr, 0);
	} else {
		cp = ip_vs_conn_out_get(IPPROTO_UDP,
			iph->daddr, 0,
			iph->saddr, 0);
	}

	return cp;
#else
	return NULL;
#endif
}

static int
icmp_conn_schedule(struct sk_buff *skb, struct ip_vs_protocol *pp,
		   struct iphdr *iph, union ip_vs_tphdr h,
		   int *verdict, struct ip_vs_conn **cpp)
{
	*verdict = NF_ACCEPT;
	return 0;
}

static int
icmp_csum_check(struct sk_buff *skb, struct ip_vs_protocol *pp,
		struct iphdr *iph, union ip_vs_tphdr h, int size)
{
	if (!(iph->frag_off & __constant_htons(IP_OFFSET))) {
		if (ip_compute_csum(h.raw, size)) {
			IP_VS_DBG_RL_PKT(0, pp, iph, "Failed checksum for");
			return 0;
		}
	}
	return 1;

}

static void
icmp_debug_packet(struct ip_vs_protocol *pp, struct iphdr *iph, char *msg)
{
	char buf[256];
	union ip_vs_tphdr h;

	h.raw = (char *) iph + iph->ihl * 4;
	if (iph->frag_off & __constant_htons(IP_OFFSET))
		sprintf(buf, "%s %u.%u.%u.%u->%u.%u.%u.%u frag",
			pp->name, NIPQUAD(iph->saddr), NIPQUAD(iph->daddr));
	else
		sprintf(buf, "%s %u.%u.%u.%u->%u.%u.%u.%u T:%d C:%d",
			pp->name, NIPQUAD(iph->saddr), NIPQUAD(iph->daddr),
			h.icmph->type, h.icmph->code);

	printk(KERN_DEBUG "IPVS: %s: %s\n", msg, buf);
}

static int
icmp_state_transition(struct ip_vs_conn *cp,
		      int direction, struct iphdr *iph,
		      union ip_vs_tphdr h, struct ip_vs_protocol *pp)
{
	cp->timeout = pp->timeout_table[IP_VS_ICMP_S_NORMAL];
	return 1;
}

static int
icmp_set_state_timeout(struct ip_vs_protocol *pp, char *sname, int to)
{
	int num;
	char **names;

	num = IP_VS_ICMP_S_LAST;
	names = icmp_state_name_table;
	return ip_vs_set_state_timeout(pp->timeout_table, num, names, sname, to);
}


static void icmp_init(struct ip_vs_protocol *pp)
{
	pp->timeout_table = icmp_timeouts;
}

static void icmp_exit(struct ip_vs_protocol *pp)
{
}

struct ip_vs_protocol ip_vs_protocol_icmp = {
	.name =			"ICMP",
	.protocol =		IPPROTO_ICMP,
	.minhlen =		sizeof(struct icmphdr),
	.minhlen_icmp =		8,
	.dont_defrag =		0,
	.skip_nonexisting =	0,
	.slave =		0,
	.init =			icmp_init,
	.exit =			icmp_exit,
	.conn_schedule =	icmp_conn_schedule,
	.conn_in_get =		icmp_conn_in_get,
	.conn_out_get =		icmp_conn_out_get,
	.snat_handler =		NULL,
	.dnat_handler =		NULL,
	.csum_check =		icmp_csum_check,
	.state_transition =	icmp_state_transition,
	.register_app =		NULL,
	.unregister_app =	NULL,
	.app_conn_bind =	NULL,
	.debug_packet =		icmp_debug_packet,
	.timeout_change =	NULL,
	.set_state_timeout =	icmp_set_state_timeout,
};
