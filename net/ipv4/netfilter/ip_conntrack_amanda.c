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

DECLARE_LOCK(ip_amanda_lock);

char *conns[] = { "DATA ", "MESG ", "INDEX " };

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

/* This is slow, but it's simple. --RR */
static char amanda_buffer[65536];

static int help(struct sk_buff *skb,
		struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	char *data, *data_limit;
	int dir = CTINFO2DIR(ctinfo);
	unsigned int dataoff, i;
	struct ip_ct_amanda *info =
				(struct ip_ct_amanda *)&ct->help.ct_ftp_info;

	/* Can't track connections formed before we registered */
	if (!info)
		return NF_ACCEPT;

	/* increase the UDP timeout of the master connection as replies from
	 * Amanda clients to the server can be quite delayed */
	ip_ct_refresh(ct, master_timeout * HZ);

	/* If packet is coming from Amanda server */
	if (dir == IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	/* No data? */
	dataoff = skb->nh.iph->ihl*4 + sizeof(struct udphdr);
	if (dataoff >= skb->len) {
		if (net_ratelimit())
			printk("ip_conntrack_amanda_help: skblen = %u\n",
			       (unsigned)skb->len);
		return NF_ACCEPT;
	}

	LOCK_BH(&ip_amanda_lock);
	skb_copy_bits(skb, dataoff, amanda_buffer, skb->len - dataoff);
	data = amanda_buffer;
	data_limit = amanda_buffer + skb->len - dataoff;
	*data_limit = '\0';

	/* Search for the CONNECT string */
	data = strstr(data, "CONNECT ");
	if (!data)
		goto out;

	DEBUGP("ip_conntrack_amanda_help: CONNECT found in connection "
		   "%u.%u.%u.%u:%u %u.%u.%u.%u:%u\n",
		   NIPQUAD(iph->saddr), htons(udph->source),
		   NIPQUAD(iph->daddr), htons(udph->dest));
	data += strlen("CONNECT ");

	/* Only search first line. */	
	if (strchr(data, '\n'))
		*strchr(data, '\n') = '\0';

	for (i = 0; i < ARRAY_SIZE(conns); i++) {
		char *match = strstr(data, conns[i]);
		if (match) {
			char *portchr;
			struct ip_conntrack_expect expect;
			struct ip_ct_amanda_expect *exp_amanda_info =
				&expect.help.exp_amanda_info;

			memset(&expect, 0, sizeof(expect));

			data += strlen(conns[i]);
			/* this is not really tcp, but let's steal an
			 * idea from a tcp stream helper :-) */
			// XXX expect.seq = data - amanda_buffer;
			exp_amanda_info->offset = data - amanda_buffer;
// XXX DEBUGP("expect.seq = %p - %p = %d\n", data, amanda_buffer, expect.seq);
DEBUGP("exp_amanda_info->offset = %p - %p = %d\n", data, amanda_buffer, exp_amanda_info->offset);
			portchr = data;
			exp_amanda_info->port = simple_strtoul(data, &data,10);
			exp_amanda_info->len = data - portchr;

			/* eat whitespace */
			while (*data == ' ')
				data++;
			DEBUGP("ip_conntrack_amanda_help: "
			       "CONNECT %s request with port "
			       "%u found\n", conns[i],
			       exp_amanda_info->port);

			expect.tuple = ((struct ip_conntrack_tuple)
				{ { ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip,
				    { 0 } },
				  { ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip,
				    { htons(exp_amanda_info->port) },
				    IPPROTO_TCP }});
			expect.mask = ((struct ip_conntrack_tuple)
				{ { 0, { 0 } },
				  { 0xFFFFFFFF, { 0xFFFF }, 0xFFFF }});

			expect.expectfn = NULL;

			DEBUGP ("ip_conntrack_amanda_help: "
				"expect_related: %u.%u.%u.%u:%u - "
				"%u.%u.%u.%u:%u\n",
				NIPQUAD(expect.tuple.src.ip),
				ntohs(expect.tuple.src.u.tcp.port),
				NIPQUAD(expect.tuple.dst.ip),
				ntohs(expect.tuple.dst.u.tcp.port));
			if (ip_conntrack_expect_related(ct, &expect)
			    == -EEXIST) {
				;
				/* this must be a packet being resent */
				/* XXX - how do I get the
				 *       ip_conntrack_expect that
				 *       already exists so that I can
				 *       update the .seq so that the
				 *       nat module rewrites the port
				 *       numbers?
				 *       Perhaps I should use the
				 *       exp_amanda_info instead of
				 *       .seq.
				 */
			}
		}
	}
 out:
	UNLOCK_BH(&ip_amanda_lock);
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

static void fini(void)
{
	DEBUGP("ip_ct_amanda: unregistering helper for port 10080\n");
	ip_conntrack_helper_unregister(&amanda_helper);
}

static int __init init(void)
{
	int ret;

	DEBUGP("ip_ct_amanda: registering helper for port 10080\n");
	ret = ip_conntrack_helper_register(&amanda_helper);

	if (ret) {
		printk("ip_ct_amanda: ERROR registering helper\n");
		fini();
		return -EBUSY;
	}
	return 0;
}

PROVIDES_CONNTRACK(amanda);
EXPORT_SYMBOL(ip_amanda_lock);

module_init(init);
module_exit(fini);
