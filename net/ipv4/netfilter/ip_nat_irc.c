/* IRC extension for TCP NAT alteration.
 * (C) 2000-2001 by Harald Welte <laforge@gnumonks.org>
 * based on a copy of RR's ip_nat_ftp.c
 *
 * ip_nat_irc.c,v 1.16 2001/12/06 07:42:10 laforge Exp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Module load syntax:
 * 	insmod ip_nat_irc.o ports=port1,port2,...port<MAX_PORTS>
 *	
 * 	please give the ports of all IRC servers You wish to connect to.
 *	If You don't specify ports, the default will be port 6667
 */

#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/kernel.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_irc.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define MAX_PORTS 8
static int ports[MAX_PORTS];
static int ports_c = 0;

MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("IRC (DCC) network address translation module");
MODULE_LICENSE("GPL");
#ifdef MODULE_PARM
MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_PORTS) "i");
MODULE_PARM_DESC(ports, "port numbers of IRC servers");
#endif

/* protects irc part of conntracks */
DECLARE_LOCK_EXTERN(ip_irc_lock);

/* FIXME: Time out? --RR */

static int
irc_nat_expected(struct sk_buff **pskb,
		 unsigned int hooknum,
		 struct ip_conntrack *ct,
		 struct ip_nat_info *info,
		 struct ip_conntrack *master,
		 struct ip_nat_info *masterinfo, unsigned int *verdict)
{
	struct ip_nat_multi_range mr;
	u_int32_t newdstip, newsrcip, newip;
	struct ip_ct_irc *ircinfo;

	IP_NF_ASSERT(info);
	IP_NF_ASSERT(master);
	IP_NF_ASSERT(masterinfo);

	IP_NF_ASSERT(!(info->initialized & (1 << HOOK2MANIP(hooknum))));

	DEBUGP("nat_expected: We have a connection!\n");

	/* Master must be an irc connection */
	ircinfo = &master->help.ct_irc_info;
	LOCK_BH(&ip_irc_lock);
	if (ircinfo->is_irc != IP_CONNTR_IRC) {
		UNLOCK_BH(&ip_irc_lock);
		DEBUGP("nat_expected: master not irc\n");
		return 0;
	}

	newdstip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
	newsrcip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
	DEBUGP("nat_expected: DCC cmd. %u.%u.%u.%u->%u.%u.%u.%u\n",
	       NIPQUAD(newsrcip), NIPQUAD(newdstip));

	UNLOCK_BH(&ip_irc_lock);

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC)
		newip = newsrcip;
	else
		newip = newdstip;

	DEBUGP("nat_expected: IP to %u.%u.%u.%u\n", NIPQUAD(newip));

	mr.rangesize = 1;
	/* We don't want to manip the per-protocol, just the IPs. */
	mr.range[0].flags = IP_NAT_RANGE_MAP_IPS;
	mr.range[0].min_ip = mr.range[0].max_ip = newip;

	*verdict = ip_nat_setup_info(ct, &mr, hooknum);

	return 1;
}

static int irc_data_fixup(const struct ip_ct_irc *ct_irc_info,
			  struct ip_conntrack *ct,
			  unsigned int datalen,
			  struct sk_buff **pskb,
			  enum ip_conntrack_info ctinfo)
{
	u_int32_t newip;
	struct ip_conntrack_tuple t;
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph = (void *) iph + iph->ihl * 4;
	int port;

	/* "4294967296 65635 " */
	char buffer[18];

	MUST_BE_LOCKED(&ip_irc_lock);

	DEBUGP("IRC_NAT: info (seq %u + %u) packet(seq %u + %u)\n",
	       ct_irc_info->seq, ct_irc_info->len,
	       ntohl(tcph->seq), datalen);

	newip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;

	/* Alter conntrack's expectations. */

	/* We can read expect here without conntrack lock, since it's
	   only set in ip_conntrack_irc, with ip_irc_lock held
	   writable */

	t = ct->expected.tuple;
	t.dst.ip = newip;
	for (port = ct_irc_info->port; port != 0; port++) {
		t.dst.u.tcp.port = htons(port);
		if (ip_conntrack_expect_related(ct, &t,
						&ct->expected.mask,
						NULL) == 0) {
			DEBUGP("using port %d", port);
			break;
		}

	}
	if (port == 0)
		return 0;

	/*      strlen("\1DCC CHAT chat AAAAAAAA P\1\n")=27
	 *      strlen("\1DCC SCHAT chat AAAAAAAA P\1\n")=28
	 *      strlen("\1DCC SEND F AAAAAAAA P S\1\n")=26
	 *      strlen("\1DCC MOVE F AAAAAAAA P S\1\n")=26
	 *      strlen("\1DCC TSEND F AAAAAAAA P S\1\n")=27
	 *              AAAAAAAAA: bound addr (1.0.0.0==16777216, min 8 digits,
	 *                      255.255.255.255==4294967296, 10 digits)
	 *              P:         bound port (min 1 d, max 5d (65635))
	 *              F:         filename   (min 1 d )
	 *              S:         size       (min 1 d )
	 *              0x01, \n:  terminators
	 */

	sprintf(buffer, "%u %u", ntohl(newip), port);
	DEBUGP("ip_nat_irc: Inserting '%s' == %u.%u.%u.%u, port %u\n",
	       buffer, NIPQUAD(newip), port);

	return ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, 
					ct_irc_info->seq - ntohl(tcph->seq),
					ct_irc_info->len, buffer, 
					strlen(buffer));
}

static unsigned int help(struct ip_conntrack *ct,
			 struct ip_nat_info *info,
			 enum ip_conntrack_info ctinfo,
			 unsigned int hooknum, struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph = (void *) iph + iph->ihl * 4;
	unsigned int datalen;
	int dir;
	int score;
	struct ip_ct_irc *ct_irc_info = &ct->help.ct_irc_info;

	/* Delete SACK_OK on initial TCP SYNs. */
	if (tcph->syn && !tcph->ack)
		ip_nat_delete_sack(*pskb, tcph);

	/* Only mangle things once: original direction in POST_ROUTING
	   and reply direction on PRE_ROUTING. */
	dir = CTINFO2DIR(ctinfo);
	if (!((hooknum == NF_IP_POST_ROUTING && dir == IP_CT_DIR_ORIGINAL)
	      || (hooknum == NF_IP_PRE_ROUTING && dir == IP_CT_DIR_REPLY))) {
		DEBUGP("nat_irc: Not touching dir %s at hook %s\n",
		       dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY",
		       hooknum == NF_IP_POST_ROUTING ? "POSTROUTING"
		       : hooknum == NF_IP_PRE_ROUTING ? "PREROUTING"
		       : hooknum == NF_IP_LOCAL_OUT ? "OUTPUT" : "???");
		return NF_ACCEPT;
	}
	DEBUGP("got beyond not touching\n");

	datalen = (*pskb)->len - iph->ihl * 4 - tcph->doff * 4;
	score = 0;
	LOCK_BH(&ip_irc_lock);
	if (ct_irc_info->len) {
		DEBUGP("got beyond ct_irc_info->len\n");

		/* If it's in the right range... */
		score += between(ct_irc_info->seq, ntohl(tcph->seq),
				 ntohl(tcph->seq) + datalen);
		score += between(ct_irc_info->seq + ct_irc_info->len,
				 ntohl(tcph->seq),
				 ntohl(tcph->seq) + datalen);
		if (score == 1) {
			/* Half a match?  This means a partial retransmisison.
			   It's a cracker being funky. */
			if (net_ratelimit()) {
				printk
				    ("IRC_NAT: partial packet %u/%u in %u/%u\n",
				     ct_irc_info->seq, ct_irc_info->len,
				     ntohl(tcph->seq),
				     ntohl(tcph->seq) + datalen);
			}
			UNLOCK_BH(&ip_irc_lock);
			return NF_DROP;
		} else if (score == 2) {
			DEBUGP("IRC_NAT: score=2, calling fixup\n");
			if (!irc_data_fixup(ct_irc_info, ct, datalen,
					    pskb, ctinfo)) {
				UNLOCK_BH(&ip_irc_lock);
				return NF_DROP;
			}
			/* skb may have been reallocated */
			iph = (*pskb)->nh.iph;
			tcph = (void *) iph + iph->ihl * 4;
		}
	}

	UNLOCK_BH(&ip_irc_lock);

	ip_nat_seq_adjust(*pskb, ct, ctinfo);

	return NF_ACCEPT;
}

static struct ip_nat_helper ip_nat_irc_helpers[MAX_PORTS];
static char ip_nih_names[MAX_PORTS][6];

static struct ip_nat_expect irc_expect
    = { {NULL, NULL}, irc_nat_expected };


/* This function is intentionally _NOT_ defined as  __exit, because
 * it is needed by init() */
static void fini(void)
{
	int i;

	for (i = 0; i < ports_c; i++) {
		DEBUGP("ip_nat_irc: unregistering helper for port %d\n",
		       ports[i]);
		ip_nat_helper_unregister(&ip_nat_irc_helpers[i]);
	}
	ip_nat_expect_unregister(&irc_expect);
}
static int __init init(void)
{
	int ret;
	int i;
	struct ip_nat_helper *hlpr;
	char *tmpname;

	ret = ip_nat_expect_register(&irc_expect);
	if (ret == 0) {

		if (ports[0] == 0) {
			ports[0] = 6667;
		}

		for (i = 0; (i < MAX_PORTS) && ports[i] != 0; i++) {
			hlpr = &ip_nat_irc_helpers[i];
			memset(hlpr, 0,
			       sizeof(struct ip_nat_helper));

			hlpr->tuple.dst.protonum = IPPROTO_TCP;
			hlpr->tuple.src.u.tcp.port = htons(ports[i]);
			hlpr->mask.src.u.tcp.port = 0xFFFF;
			hlpr->mask.dst.protonum = 0xFFFF;
			hlpr->help = help;

			tmpname = &ip_nih_names[i][0];
			sprintf(tmpname, "irc%2.2d", i);

			hlpr->name = tmpname;
			DEBUGP
			    ("ip_nat_irc: Trying to register helper for port %d: name %s\n",
			     ports[i], hlpr->name);
			ret = ip_nat_helper_register(hlpr);

			if (ret) {
				printk
				    ("ip_nat_irc: error registering helper for port %d\n",
				     ports[i]);
				fini();
				return 1;
			}
			ports_c++;
		}
	}
	return ret;
}


module_init(init);
module_exit(fini);
