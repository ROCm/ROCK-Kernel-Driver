/* IRC extension for IP connection tracking, Version 1.21
 * (C) 2000-2002 by Harald Welte <laforge@gnumonks.org>
 * based on RR's ip_conntrack_ftp.c	
 *
 * ip_conntrack_irc.c,v 1.21 2002/02/05 14:49:26 laforge Exp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 **
 *	Module load syntax:
 * 	insmod ip_conntrack_irc.o ports=port1,port2,...port<MAX_PORTS>
 *	
 * 	please give the ports of all IRC servers You wish to connect to.
 *	If You don't specify ports, the default will be port 6667
 *
 */

#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/lockhelp.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_irc.h>

#define MAX_PORTS 8
static int ports[MAX_PORTS];
static int ports_n_c = 0;

MODULE_AUTHOR("Harald Welte <laforge@gnumonks.org>");
MODULE_DESCRIPTION("IRC (DCC) connection tracking module");
MODULE_LICENSE("GPL");
#ifdef MODULE_PARM
MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_PORTS) "i");
MODULE_PARM_DESC(ports, "port numbers of IRC servers");
#endif

#define NUM_DCCPROTO 	5
struct dccproto dccprotos[NUM_DCCPROTO] = {
	{"SEND ", 5},
	{"CHAT ", 5},
	{"MOVE ", 5},
	{"TSEND ", 6},
	{"SCHAT ", 6}
};
#define MAXMATCHLEN	6

DECLARE_LOCK(ip_irc_lock);
struct module *ip_conntrack_irc = THIS_MODULE;

#if 0
#define DEBUGP(format, args...) printk(KERN_DEBUG __FILE__ ":" __FUNCTION__ \
					":" format, ## args)
#else
#define DEBUGP(format, args...)
#endif

int parse_dcc(char *data, char *data_end, u_int32_t * ip, u_int16_t * port,
	      char **ad_beg_p, char **ad_end_p)
/* tries to get the ip_addr and port out of a dcc command
   return value: -1 on failure, 0 on success 
	data		pointer to first byte of DCC command data
	data_end	pointer to last byte of dcc command data
	ip		returns parsed ip of dcc command
	port		returns parsed port of dcc command
	ad_beg_p	returns pointer to first byte of addr data
	ad_end_p	returns pointer to last byte of addr data */
{

	/* at least 12: "AAAAAAAA P\1\n" */
	while (*data++ != ' ')
		if (data > data_end - 12)
			return -1;

	*ad_beg_p = data;
	*ip = simple_strtoul(data, &data, 10);

	/* skip blanks between ip and port */
	while (*data == ' ')
		data++;


	*port = simple_strtoul(data, &data, 10);
	*ad_end_p = data;

	return 0;
}


/* FIXME: This should be in userspace.  Later. */
static int help(const struct iphdr *iph, size_t len,
		struct ip_conntrack *ct, enum ip_conntrack_info ctinfo)
{
	/* tcplen not negative guarenteed by ip_conntrack_tcp.c */
	struct tcphdr *tcph = (void *) iph + iph->ihl * 4;
	const char *data = (const char *) tcph + tcph->doff * 4;
	const char *_data = data;
	char *data_limit;
	u_int32_t tcplen = len - iph->ihl * 4;
	u_int32_t datalen = tcplen - tcph->doff * 4;
	int dir = CTINFO2DIR(ctinfo);
	struct ip_conntrack_tuple t, mask;

	u_int32_t dcc_ip;
	u_int16_t dcc_port;
	int i;
	char *addr_beg_p, *addr_end_p;

	struct ip_ct_irc *info = &ct->help.ct_irc_info;

	mask = ((struct ip_conntrack_tuple)
		{ { 0, { 0 } },
		  { 0xFFFFFFFF, { 0xFFFF }, 0xFFFF }});

	DEBUGP("entered\n");
	/* Can't track connections formed before we registered */
	if (!info)
		return NF_ACCEPT;

	/* If packet is coming from IRC server */
	if (dir == IP_CT_DIR_REPLY)
		return NF_ACCEPT;

	/* Until there's been traffic both ways, don't look in packets. */
	if (ctinfo != IP_CT_ESTABLISHED
	    && ctinfo != IP_CT_ESTABLISHED + IP_CT_IS_REPLY) {
		DEBUGP("Conntrackinfo = %u\n", ctinfo);
		return NF_ACCEPT;
	}

	/* Not whole TCP header? */
	if (tcplen < sizeof(struct tcphdr) || tcplen < tcph->doff * 4) {
		DEBUGP("tcplen = %u\n", (unsigned) tcplen);
		return NF_ACCEPT;
	}

	/* Checksum invalid?  Ignore. */
	/* FIXME: Source route IP option packets --RR */
	if (tcp_v4_check(tcph, tcplen, iph->saddr, iph->daddr,
			 csum_partial((char *) tcph, tcplen, 0))) {
		DEBUGP("bad csum: %p %u %u.%u.%u.%u %u.%u.%u.%u\n",
		     tcph, tcplen, NIPQUAD(iph->saddr),
		     NIPQUAD(iph->daddr));
		return NF_ACCEPT;
	}

	data_limit = (char *) data + datalen;
	while (data < (data_limit - (22 + MAXMATCHLEN))) {
		if (memcmp(data, "\1DCC ", 5)) {
			data++;
			continue;
		}

		data += 5;

		DEBUGP("DCC found in master %u.%u.%u.%u:%u %u.%u.%u.%u:%u...\n",
			NIPQUAD(iph->saddr), ntohs(tcph->source),
			NIPQUAD(iph->daddr), ntohs(tcph->dest));

		for (i = 0; i < NUM_DCCPROTO; i++) {
			if (memcmp(data, dccprotos[i].match,
				   dccprotos[i].matchlen)) {
				/* no match */
				continue;
			}

			DEBUGP("DCC %s detected\n", dccprotos[i].match);
			data += dccprotos[i].matchlen;
			if (parse_dcc((char *) data, data_limit, &dcc_ip,
				       &dcc_port, &addr_beg_p, &addr_end_p)) {
				/* unable to parse */
				DEBUGP("unable to parse dcc command\n");
				continue;
			}
			DEBUGP("DCC bound ip/port: %u.%u.%u.%u:%u\n",
				HIPQUAD(dcc_ip), dcc_port);

			if (ct->tuplehash[dir].tuple.src.ip != htonl(dcc_ip)) {
				if (net_ratelimit())
					printk(KERN_WARNING
						"Forged DCC command from "
						"%u.%u.%u.%u: %u.%u.%u.%u:%u\n",
				NIPQUAD(ct->tuplehash[dir].tuple.src.ip),
						HIPQUAD(dcc_ip), dcc_port);

				continue;
			}

			LOCK_BH(&ip_irc_lock);

			/* save position of address in dcc string,
			 * neccessary for NAT */
			info->is_irc = IP_CONNTR_IRC;
			DEBUGP("tcph->seq = %u\n", tcph->seq);
			info->seq = ntohl(tcph->seq) + (addr_beg_p - _data);
			info->len = (addr_end_p - addr_beg_p);
			info->port = dcc_port;
			DEBUGP("wrote info seq=%u (ofs=%u), len=%d\n",
				info->seq, (addr_end_p - _data), info->len);

			memset(&t, 0, sizeof(t));
			t.src.ip = 0;
			t.src.u.tcp.port = 0;
			t.dst.ip = htonl(dcc_ip);
			t.dst.u.tcp.port = htons(info->port);
			t.dst.protonum = IPPROTO_TCP;

			DEBUGP("expect_related %u.%u.%u.%u:%u-%u.%u.%u.%u:%u\n",
				NIPQUAD(t.src.ip),
				ntohs(t.src.u.tcp.port),
				NIPQUAD(t.dst.ip),
				ntohs(t.dst.u.tcp.port));

			ip_conntrack_expect_related(ct, &t, &mask, NULL);
			UNLOCK_BH(&ip_irc_lock);

			return NF_ACCEPT;
		} /* for .. NUM_DCCPROTO */
	} /* while data < ... */

	return NF_ACCEPT;
}

static struct ip_conntrack_helper irc_helpers[MAX_PORTS];

static void fini(void);

static int __init init(void)
{
	int i, ret;

	/* If no port given, default to standard irc port */
	if (ports[0] == 0)
		ports[0] = 6667;

	for (i = 0; (i < MAX_PORTS) && ports[i]; i++) {
		memset(&irc_helpers[i], 0,
		       sizeof(struct ip_conntrack_helper));
		irc_helpers[i].tuple.src.u.tcp.port = htons(ports[i]);
		irc_helpers[i].tuple.dst.protonum = IPPROTO_TCP;
		irc_helpers[i].mask.src.u.tcp.port = 0xFFFF;
		irc_helpers[i].mask.dst.protonum = 0xFFFF;
		irc_helpers[i].help = help;

		DEBUGP("port #%d: %d\n", i, ports[i]);

		ret = ip_conntrack_helper_register(&irc_helpers[i]);

		if (ret) {
			printk("ip_conntrack_irc: ERROR registering port %d\n",
				ports[i]);
			fini();
			return -EBUSY;
		}
		ports_n_c++;
	}
	return 0;
}

/* This function is intentionally _NOT_ defined as __exit, because 
 * it is needed by the init function */
static void fini(void)
{
	int i;
	for (i = 0; (i < MAX_PORTS) && ports[i]; i++) {
		DEBUGP("unregistering port %d\n",
		       ports[i]);
		ip_conntrack_helper_unregister(&irc_helpers[i]);
	}
}

module_init(init);
module_exit(fini);
