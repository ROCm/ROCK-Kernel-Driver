/* (C) 2001-2002 Magnus Boden <mb@ozaba.mine.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Version: 0.0.7
 *
 * Thu 21 Mar 2002 Harald Welte <laforge@gnumonks.org>
 * 	- port to newnat API
 *
 */

#include <linux/module.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_tftp.h>

MODULE_AUTHOR("Magnus Boden <mb@ozaba.mine.nu>");
MODULE_DESCRIPTION("tftp connection tracking helper");
MODULE_LICENSE("GPL");

#define MAX_PORTS 8
static int ports[MAX_PORTS];
static int ports_c;
#ifdef MODULE_PARM
MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_PORTS) "i");
MODULE_PARM_DESC(ports, "port numbers of tftp servers");
#endif

#if 0
#define DEBUGP(format, args...) printk("%s:%s:" format, \
                                       __FILE__, __FUNCTION__ , ## args)
#else
#define DEBUGP(format, args...)
#endif

static int tftp_help(struct sk_buff *skb,
		     struct ip_conntrack *ct,
		     enum ip_conntrack_info ctinfo)
{
	struct tftphdr tftph;
	struct ip_conntrack_expect *exp;

	if (skb_copy_bits(skb, skb->nh.iph->ihl * 4 + sizeof(struct udphdr),
			  &tftph, sizeof(tftph)) != 0)
		return NF_ACCEPT;

	switch (ntohs(tftph.opcode)) {
	/* RRQ and WRQ works the same way */
	case TFTP_OPCODE_READ:
	case TFTP_OPCODE_WRITE:
		DEBUGP("");
		DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

		exp = ip_conntrack_expect_alloc();
		if (exp == NULL)
			return NF_ACCEPT;

		exp->tuple = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		exp->mask.src.ip = 0xffffffff;
		exp->mask.dst.ip = 0xffffffff;
		exp->mask.dst.u.udp.port = 0xffff;
		exp->mask.dst.protonum = 0xffff;
		exp->expectfn = NULL;

		DEBUGP("expect: ");
		DUMP_TUPLE(&exp->tuple);
		DUMP_TUPLE(&exp->mask);
		ip_conntrack_expect_related(exp, ct);
		break;
	case TFTP_OPCODE_DATA:
	case TFTP_OPCODE_ACK:
		DEBUGP("Data/ACK opcode\n");
		break;
	case TFTP_OPCODE_ERROR:
		DEBUGP("Error opcode\n");
		break;
	default:
		DEBUGP("Unknown opcode\n");
	}
	return NF_ACCEPT;
}

static struct ip_conntrack_helper tftp[MAX_PORTS];
static char tftp_names[MAX_PORTS][10];

static void fini(void)
{
	int i;

	for (i = 0 ; i < ports_c; i++) {
		DEBUGP("unregistering helper for port %d\n",
			ports[i]);
		ip_conntrack_helper_unregister(&tftp[i]);
	} 
}

static int __init init(void)
{
	int i, ret;
	char *tmpname;

	if (!ports[0])
		ports[0]=TFTP_PORT;

	for (i = 0 ; (i < MAX_PORTS) && ports[i] ; i++) {
		/* Create helper structure */
		memset(&tftp[i], 0, sizeof(struct ip_conntrack_helper));

		tftp[i].tuple.dst.protonum = IPPROTO_UDP;
		tftp[i].tuple.src.u.udp.port = htons(ports[i]);
		tftp[i].mask.dst.protonum = 0xFFFF;
		tftp[i].mask.src.u.udp.port = 0xFFFF;
		tftp[i].max_expected = 1;
		tftp[i].timeout = 0;
		tftp[i].flags = IP_CT_HELPER_F_REUSE_EXPECT;
		tftp[i].me = THIS_MODULE;
		tftp[i].help = tftp_help;

		tmpname = &tftp_names[i][0];
		if (ports[i] == TFTP_PORT)
			sprintf(tmpname, "tftp");
		else
			sprintf(tmpname, "tftp-%d", i);
		tftp[i].name = tmpname;

		DEBUGP("port #%d: %d\n", i, ports[i]);

		ret=ip_conntrack_helper_register(&tftp[i]);
		if (ret) {
			printk("ERROR registering helper for port %d\n",
				ports[i]);
			fini();
			return(ret);
		}
		ports_c++;
	}
	return(0);
}

PROVIDES_CONNTRACK(tftp);

module_init(init);
module_exit(fini);
