/* Amanda extension for TCP NAT alteration.
 * (C) 2002 by Brian J. Murrell <netfilter@interlinx.bc.ca>
 * based on a copy of HW's ip_nat_irc.c as well as other modules
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Module load syntax:
 * 	insmod ip_nat_amanda.o
 */

#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/kernel.h>
#include <net/tcp.h>
#include <net/udp.h>

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>


#if 0
#define DEBUGP printk
#define DUMP_OFFSET(x)	printk("offset_before=%d, offset_after=%d, correction_pos=%u\n", x->offset_before, x->offset_after, x->correction_pos);
#else
#define DEBUGP(format, args...)
#define DUMP_OFFSET(x)
#endif

MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda NAT helper");
MODULE_LICENSE("GPL");

/* protects amanda part of conntracks */
DECLARE_LOCK_EXTERN(ip_amanda_lock);

static unsigned int
amanda_nat_expected(struct sk_buff **pskb,
		 unsigned int hooknum,
		 struct ip_conntrack *ct,
		 struct ip_nat_info *info)
{
	struct ip_nat_multi_range mr;
	u_int32_t newdstip, newsrcip, newip;
	u_int16_t port;
	struct ip_ct_amanda_expect *exp_info;
	struct ip_conntrack *master = master_ct(ct);

	IP_NF_ASSERT(info);
	IP_NF_ASSERT(master);

	IP_NF_ASSERT(!(info->initialized & (1 << HOOK2MANIP(hooknum))));

	DEBUGP("nat_expected: We have a connection!\n");
	exp_info = &ct->master->help.exp_amanda_info;

	newdstip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
	newsrcip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
	DEBUGP("nat_expected: %u.%u.%u.%u->%u.%u.%u.%u\n",
	       NIPQUAD(newsrcip), NIPQUAD(newdstip));

	port = exp_info->port;

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC)
		newip = newsrcip;
	else
		newip = newdstip;

	DEBUGP("nat_expected: IP to %u.%u.%u.%u\n", NIPQUAD(newip));

	mr.rangesize = 1;
	/* We don't want to manip the per-protocol, just the IPs. */
	mr.range[0].flags = IP_NAT_RANGE_MAP_IPS;
	mr.range[0].min_ip = mr.range[0].max_ip = newip;

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_DST) {
		mr.range[0].flags |= IP_NAT_RANGE_PROTO_SPECIFIED;
		mr.range[0].min = mr.range[0].max
			= ((union ip_conntrack_manip_proto)
				{ .udp = { htons(port) } });
	}

	return ip_nat_setup_info(ct, &mr, hooknum);
}

static int amanda_data_fixup(struct ip_conntrack *ct,
			  struct sk_buff **pskb,
			  enum ip_conntrack_info ctinfo,
			  struct ip_conntrack_expect *expect)
{
	u_int32_t newip;
	/* DATA 99999 MESG 99999 INDEX 99999 */
	char buffer[6];
	struct ip_conntrack_expect *exp = expect;
	struct ip_ct_amanda_expect *ct_amanda_info = &exp->help.exp_amanda_info;
	struct ip_conntrack_tuple t = exp->tuple;
	u_int16_t port;

	MUST_BE_LOCKED(&ip_amanda_lock);

	newip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
	DEBUGP ("ip_nat_amanda_help: newip = %u.%u.%u.%u\n", NIPQUAD(newip));

	/* Alter conntrack's expectations. */

	/* We can read expect here without conntrack lock, since it's
	   only set in ip_conntrack_amanda, with ip_amanda_lock held
	   writable */

	t.dst.ip = newip;
	for (port = ct_amanda_info->port; port != 0; port++) {
		t.dst.u.tcp.port = htons(port);
		if (ip_conntrack_change_expect(exp, &t) == 0)
			break;
	}

	if (port == 0)
		return 0;

	sprintf(buffer, "%u", port);

	return ip_nat_mangle_udp_packet(pskb, ct, ctinfo, /* XXX exp->seq */ ct_amanda_info->offset, 
					ct_amanda_info->len, buffer, strlen(buffer));
}

static unsigned int help(struct ip_conntrack *ct,
			 struct ip_conntrack_expect *exp,
			 struct ip_nat_info *info,
			 enum ip_conntrack_info ctinfo,
			 unsigned int hooknum,
			 struct sk_buff **pskb)
{
	int dir;

	if (!exp)
		DEBUGP("ip_nat_amanda: no exp!!");
		
	/* Only mangle things once: original direction in POST_ROUTING
	   and reply direction on PRE_ROUTING. */
	dir = CTINFO2DIR(ctinfo);
	if (!((hooknum == NF_IP_POST_ROUTING && dir == IP_CT_DIR_ORIGINAL)
	      || (hooknum == NF_IP_PRE_ROUTING && dir == IP_CT_DIR_REPLY))) {
		DEBUGP("ip_nat_amanda_help: Not touching dir %s at hook %s\n",
		       dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY",
		       hooknum == NF_IP_POST_ROUTING ? "POSTROUTING"
		       : hooknum == NF_IP_PRE_ROUTING ? "PREROUTING"
		       : hooknum == NF_IP_LOCAL_OUT ? "OUTPUT"
		       : hooknum == NF_IP_LOCAL_IN ? "INPUT" : "???");
		return NF_ACCEPT;
	}
	DEBUGP("ip_nat_amanda_help: got beyond not touching: dir %s at hook %s for expect: ",
		   dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY",
		   hooknum == NF_IP_POST_ROUTING ? "POSTROUTING"
		   : hooknum == NF_IP_PRE_ROUTING ? "PREROUTING"
		     : hooknum == NF_IP_LOCAL_OUT ? "OUTPUT"
		       : hooknum == NF_IP_LOCAL_IN ? "INPUT" : "???");
	DUMP_TUPLE(&exp->tuple);

	LOCK_BH(&ip_amanda_lock);
// XXX	if (exp->seq != 0)
	if (exp->help.exp_amanda_info.offset != 0)
		/*  if this packet has a "seq" it needs to have it's content mangled */
		if (!amanda_data_fixup(ct, pskb, ctinfo, exp)) {
			UNLOCK_BH(&ip_amanda_lock);
			DEBUGP("ip_nat_amanda: NF_DROP\n");
			return NF_DROP;
		}
	exp->help.exp_amanda_info.offset = 0;
	UNLOCK_BH(&ip_amanda_lock);

	DEBUGP("ip_nat_amanda: NF_ACCEPT\n");
	return NF_ACCEPT;
}

static struct ip_nat_helper ip_nat_amanda_helper;

/* This function is intentionally _NOT_ defined as  __exit, because
 * it is needed by init() */
static void fini(void)
{
	DEBUGP("ip_nat_amanda: unregistering nat helper\n");
	ip_nat_helper_unregister(&ip_nat_amanda_helper);
}

static int __init init(void)
{
	int ret = 0;
	struct ip_nat_helper *hlpr;

	hlpr = &ip_nat_amanda_helper;
	memset(hlpr, 0, sizeof(struct ip_nat_helper));

	hlpr->tuple.dst.protonum = IPPROTO_UDP;
	hlpr->tuple.src.u.udp.port = htons(10080);
	hlpr->mask.src.u.udp.port = 0xFFFF;
	hlpr->mask.dst.protonum = 0xFFFF;
	hlpr->help = help;
	hlpr->flags = 0;
	hlpr->me = THIS_MODULE;
	hlpr->expect = amanda_nat_expected;

	hlpr->name = "amanda";

	DEBUGP
	    ("ip_nat_amanda: Trying to register nat helper\n");
	ret = ip_nat_helper_register(hlpr);

	if (ret) {
		printk
		    ("ip_nat_amanda: error registering nat helper\n");
		fini();
		return 1;
	}
	return ret;
}

NEEDS_CONNTRACK(amanda);
module_init(init);
module_exit(fini);
