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
struct module *ip_conntrack_amanda = THIS_MODULE;

#define MAXMATCHLEN	6
struct conn conns[NUM_MSGS] = {
	{"DATA ", 5},
	{"MESG ", 5},
	{"INDEX ", 6},
};

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif


/* FIXME: This should be in userspace.  Later. */
static int help(const struct iphdr *iph, size_t len,
		struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	struct udphdr *udph = (void *)iph + iph->ihl * 4;
	u_int32_t udplen = len - iph->ihl * 4;
	u_int32_t datalen = udplen - sizeof(struct udphdr);
	char *data = (char *)udph + sizeof(struct udphdr);
	char *datap = data;
	char *data_limit = (char *) data + datalen;
	int dir = CTINFO2DIR(ctinfo);
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

	/* Not whole UDP header? */
	if (udplen < sizeof(struct udphdr)) {
		printk("ip_conntrack_amanda_help: udplen = %u\n",
		       (unsigned)udplen);
		return NF_ACCEPT;
	}

	/* Checksum invalid?  Ignore. */
	if (csum_tcpudp_magic(iph->saddr, iph->daddr, udplen, IPPROTO_UDP,
			      csum_partial((char *)udph, udplen, 0))) {
		DEBUGP("ip_ct_talk_help: bad csum: %p %u %u.%u.%u.%u "
		       "%u.%u.%u.%u\n",
		       udph, udplen, NIPQUAD(iph->saddr),
		       NIPQUAD(iph->daddr));
		return NF_ACCEPT;
	}
	
	/* Search for the CONNECT string */
	while (data < data_limit) {
		if (!memcmp(data, "CONNECT ", 8)) {
			break;
		}
		data++;
	}
	if (memcmp(data, "CONNECT ", 8))
		return NF_ACCEPT;

	DEBUGP("ip_conntrack_amanda_help: CONNECT found in connection "
		   "%u.%u.%u.%u:%u %u.%u.%u.%u:%u\n",
		   NIPQUAD(iph->saddr), htons(udph->source),
		   NIPQUAD(iph->daddr), htons(udph->dest));
	data += 8;
	while (*data != 0x0a && data < data_limit) {

		int i;

		for (i = 0; i < NUM_MSGS; i++) {
			if (!memcmp(data, conns[i].match,
				   conns[i].matchlen)) {

				char *portchr;
				struct ip_conntrack_expect expect;
				struct ip_ct_amanda_expect
				    *exp_amanda_info =
					&expect.help.exp_amanda_info;

				memset(&expect, 0, sizeof(expect));

				data += conns[i].matchlen;
				/* this is not really tcp, but let's steal an
				 * idea from a tcp stream helper :-)
				 */
				// XXX expect.seq = data - datap;
				exp_amanda_info->offset = data - datap;
// XXX DEBUGP("expect.seq = %p - %p = %d\n", data, datap, expect.seq);
DEBUGP("exp_amanda_info->offset = %p - %p = %d\n", data, datap, exp_amanda_info->offset);
				portchr = data;
				exp_amanda_info->port =
				    simple_strtoul(data, &data, 10);
				exp_amanda_info->len = data - portchr;

				/* eat whitespace */
				while (*data == ' ')
					data++;
				DEBUGP ("ip_conntrack_amanda_help: "
					"CONNECT %s request with port "
					"%u found\n", conns[i].match,
					exp_amanda_info->port);

				LOCK_BH(&ip_amanda_lock);

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
				if (ip_conntrack_expect_related(ct, &expect) ==
				    -EEXIST) {
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
				UNLOCK_BH(&ip_amanda_lock);
			} /* if memcmp(conns) */
		} /* for .. NUM_MSGS */
		data++;
	} /* while (*data != 0x0a && data < data_limit) */

	return NF_ACCEPT;
}

static struct ip_conntrack_helper amanda_helper;

static void fini(void)
{
	DEBUGP("ip_ct_amanda: unregistering helper for port 10080\n");
	ip_conntrack_helper_unregister(&amanda_helper);
}

static int __init init(void)
{
	int ret;

	memset(&amanda_helper, 0, sizeof(struct ip_conntrack_helper));
	amanda_helper.tuple.src.u.udp.port = htons(10080);
	amanda_helper.tuple.dst.protonum = IPPROTO_UDP;
	amanda_helper.mask.src.u.udp.port = 0xFFFF;
	amanda_helper.mask.dst.protonum = 0xFFFF;
	amanda_helper.max_expected = NUM_MSGS;
	amanda_helper.timeout = 180;
	amanda_helper.flags = IP_CT_HELPER_F_REUSE_EXPECT;
	amanda_helper.me = ip_conntrack_amanda;
	amanda_helper.help = help;
	amanda_helper.name = "amanda";

	DEBUGP("ip_ct_amanda: registering helper for port 10080\n");

	ret = ip_conntrack_helper_register(&amanda_helper);

	if (ret) {
		printk("ip_ct_amanda: ERROR registering helper\n");
		fini();
		return -EBUSY;
	}
	return 0;
}

module_init(init);
module_exit(fini);
