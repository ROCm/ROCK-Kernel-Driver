/* Amanda extension for IP connection tracking, Version 0.2
 * (C) 2002 by Brian J. Murrell <netfilter@interlinx.bc.ca>
 * based on HW's ip_conntrack_irc.c as well as other modules
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	Module load syntax:
 * 	insmod ip_conntrack_amanda.o [master_timeout=n]
 *	
 *	Where master_timeout is the timeout (in seconds) of the master
 *	connection (port 10080).  This defaults to 5 minutes but if
 *	your clients take longer than 5 minutes to do their work
 *	before getting back to the Amanda server, you can increase
 *	this value.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/udp.h>

#include <linux/netfilter_ipv4/lockhelp.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_amanda.h>

static unsigned int master_timeout = 300;

MODULE_AUTHOR("Brian J. Murrell <netfilter@interlinx.bc.ca>");
MODULE_DESCRIPTION("Amanda connection tracking module");
MODULE_LICENSE("GPL");
MODULE_PARM(master_timeout, "i");
MODULE_PARM_DESC(master_timeout, "timeout for the master connection");

static char *conns[] = { "DATA ", "MESG ", "INDEX " };

/* This is slow, but it's simple. --RR */
static char amanda_buffer[65536];
static DECLARE_LOCK(amanda_buffer_lock);

static int help(struct sk_buff *skb,
                struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	struct ip_conntrack_expect *exp;
	struct ip_ct_amanda_expect *exp_amanda_info;
	char *data, *data_limit, *tmp;
	unsigned int dataoff, i;
	u_int16_t port, len;

	/* Only look at packets from the Amanda server */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	/* increase the UDP timeout of the master connection as replies from
	 * Amanda clients to the server can be quite delayed */
	ip_ct_refresh(ct, master_timeout * HZ);

	/* No data? */
	dataoff = skb->nh.iph->ihl*4 + sizeof(struct udphdr);
	if (dataoff >= skb->len) {
		if (net_ratelimit())
			printk("amanda_help: skblen = %u\n", skb->len);
		return NF_ACCEPT;
	}

	LOCK_BH(&amanda_buffer_lock);
	skb_copy_bits(skb, dataoff, amanda_buffer, skb->len - dataoff);
	data = amanda_buffer;
	data_limit = amanda_buffer + skb->len - dataoff;
	*data_limit = '\0';

	/* Search for the CONNECT string */
	data = strstr(data, "CONNECT ");
	if (!data)
		goto out;
	data += strlen("CONNECT ");

	/* Only search first line. */	
	if ((tmp = strchr(data, '\n')))
		*tmp = '\0';

	for (i = 0; i < ARRAY_SIZE(conns); i++) {
		char *match = strstr(data, conns[i]);
		if (!match)
			continue;
		tmp = data = match + strlen(conns[i]);
		port = simple_strtoul(data, &data, 10);
		len = data - tmp;
		if (port == 0 || len > 5)
			break;

		exp = ip_conntrack_expect_alloc();
		if (exp == NULL)
			goto out;

		exp->tuple.src.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
		exp->tuple.dst.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
		exp->tuple.dst.protonum = IPPROTO_TCP;
		exp->mask.src.ip = 0xFFFFFFFF;
		exp->mask.dst.ip = 0xFFFFFFFF;
		exp->mask.dst.protonum = 0xFFFF;
		exp->mask.dst.u.tcp.port = 0xFFFF;

		exp_amanda_info = &exp->help.exp_amanda_info;
		exp_amanda_info->offset = tmp - amanda_buffer;
		exp_amanda_info->port   = port;
		exp_amanda_info->len    = len;

		exp->tuple.dst.u.tcp.port = htons(port);

		ip_conntrack_expect_related(exp, ct);
	}

out:
	UNLOCK_BH(&amanda_buffer_lock);
	return NF_ACCEPT;
}

static struct ip_conntrack_helper amanda_helper = {
	.max_expected = ARRAY_SIZE(conns),
	.timeout = 180,
	.flags = IP_CT_HELPER_F_REUSE_EXPECT,
	.me = THIS_MODULE,
	.help = help,
	.name = "amanda",

	.tuple = { .src = { .u = { __constant_htons(10080) } },
		   .dst = { .protonum = IPPROTO_UDP },
	},
	.mask = { .src = { .u = { 0xFFFF } },
		 .dst = { .protonum = 0xFFFF },
	},
};

static void __exit fini(void)
{
	ip_conntrack_helper_unregister(&amanda_helper);
}

static int __init init(void)
{
	return ip_conntrack_helper_register(&amanda_helper);
}

PROVIDES_CONNTRACK(amanda);
module_init(init);
module_exit(fini);
