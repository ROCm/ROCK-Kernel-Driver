/*
 * FTP extension for IPv6 connection tracking.
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip_conntrack_ftp.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* FTP extension for IP6 connection tracking. */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/ipv6.h>
#include <linux/ctype.h>
#include <net/checksum.h>
#include <net/tcp.h>
#include <net/ipv6.h>
#include <linux/kernel.h>

#include <linux/netfilter_ipv6/ip6_conntrack.h>
#include <linux/netfilter_ipv4/lockhelp.h>
#include <linux/netfilter_ipv6/ip6_conntrack_helper.h>
#include <linux/netfilter_ipv6/ip6_conntrack_ftp.h>

/* This is slow, but it's simple. --RR */
static char ftp_buffer[65536];

DECLARE_LOCK(ip6_ftp_lock);
struct module *ip6_conntrack_ftp = THIS_MODULE;

#define MAX_PORTS 8
static int ports[MAX_PORTS];
static int ports_c = 0;
#ifdef MODULE_PARM
MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_PORTS) "i");
#endif

static int loose = 0;
MODULE_PARM(loose, "i");

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

struct cmd_info {
	struct in6_addr ip;
	u_int16_t port;
};

static int try_eprt(const char *, size_t, struct cmd_info *, char);
static int try_espv_response(const char *, size_t, struct cmd_info *, char);

static struct ftp_search {
	enum ip6_conntrack_dir dir;
	const char *pattern;
	size_t plen;
	char skip;
	char term;
	enum ip6_ct_ftp_type ftptype;
	int (*getnum)(const char *, size_t, struct cmd_info *, char);
} search[] = {
	{
		IP6_CT_DIR_ORIGINAL,
		"EPRT", sizeof("EPRT") - 1, ' ', '\r',
		IP6_CT_FTP_EPRT,
		try_eprt,
	},
	{
		IP6_CT_DIR_REPLY,
		"229 ", sizeof("229 ") - 1, '(', ')',
		IP6_CT_FTP_EPSV,
		try_espv_response,
	},
};

/* This code is based on inet_pton() in glibc-2.2.4 */

#define NS_IN6ADDRSZ 16
#define NS_INADDRSZ 4
#define NS_INT16SZ 2

/*
 * return the length of string of address parse untill error,
 * dlen or reaching terminal char - kozakai
 */
static int
get_ipv6_addr(const char *src, u_int8_t *dst, size_t dlen, u_int8_t term)
{
        static const char xdigits[] = "0123456789abcdef";
        u_int8_t tmp[NS_IN6ADDRSZ], *tp, *endp, *colonp;
        const char *curtok;
        int ch, saw_xdigit;
        u_int32_t val;
	size_t clen = 0;

        tp = memset(tmp, '\0', NS_IN6ADDRSZ);
        endp = tp + NS_IN6ADDRSZ;
        colonp = NULL;

        /* Leading :: requires some special handling. */
        if (*src == ':'){
                if (*++src != ':')
                        return (0);
		clen++;
	}

	curtok = src;
	saw_xdigit = 0;
	val = 0;
	while ((clen < dlen) && (*src != term)) {
		const char *pch;

		ch = tolower (*src++);
		clen++;

                pch = strchr(xdigits, ch);
                if (pch != NULL) {
                        val <<= 4;
                        val |= (pch - xdigits);
                        if (val > 0xffff)
                                return (0);

			saw_xdigit = 1;
                        continue;
                }
                if (ch == ':') {
                        curtok = src;
			if (!saw_xdigit) {
				if (colonp)
					return (0);
				colonp = tp;
				continue;
			} else if (*src == term) {
				return (0);
			}
			if (tp + NS_INT16SZ > endp)
				return (0);
			*tp++ = (u_int8_t) (val >> 8) & 0xff;
			*tp++ = (u_int8_t) val & 0xff;
			saw_xdigit = 0;
			val = 0;
			continue;
		}
		return (0);
        }
        if (saw_xdigit) {
                if (tp + NS_INT16SZ > endp)
                        return (0);

                *tp++ = (u_int8_t) (val >> 8) & 0xff;
                *tp++ = (u_int8_t) val & 0xff;
        }
        if (colonp != NULL) {
                /*
                 * Since some memmove()'s erroneously fail to handle
                 * overlapping regions, we'll do the shift by hand.
                 */
                const int n = tp - colonp;
                int i;

                if (tp == endp)
                        return (0);

                for (i = 1; i <= n; i++) {
                        endp[- i] = colonp[n - i];
                        colonp[n - i] = 0;
                }
                tp = endp;
        }
        if (tp != endp || (*src != term))
                return (0);

        memcpy(dst, tmp, NS_IN6ADDRSZ);
        return clen;
}

/* return length of port if succeed. */
static int get_port(const char *data, u_int16_t *port, size_t dlen, char term)
{
	int i;
	u_int16_t tmp_port = 0;

	for(i = 0; i < dlen; i++) {
		/* Finished? */
		if(data[i] == term){
			*port = htons(tmp_port);
			return i;
		}

		if(data[i] < '0' || data[i] > '9')
			return 0;

		tmp_port = tmp_port*10 + (data[i] - '0');
	}
	return 0;
}

/* Returns 0, or length of numbers: |1|132.235.1.2|6275| */
static int try_eprt(const char *data, size_t dlen, struct cmd_info *cmd, 
		    char term)
{
	char delim;
	int len;
	int addr_len;

	/* First character is delimiter, then "1" for IPv4, then
           delimiter again. */

	if (dlen <= 3)
		return 0;

	delim = data[0];

	if (isdigit(delim) || delim < 33 || delim > 126
	    || data[1] != '2' || data[2] != delim){
		return 0;
	}
	DEBUGP("Got %c2%c\n", delim, delim);

	len = 3;

	/* Now we have IP address. */
	addr_len = get_ipv6_addr(&data[len], cmd->ip.s6_addr,
				dlen - len, delim);

	if (addr_len == 0)
		return 0;

	len += addr_len + 1;

	DEBUGP("Got IPv6 address!\n");

	addr_len = get_port(&data[len], &cmd->port, dlen, delim);

	if(addr_len == 0)
		return 0;

	len += addr_len + 1;
	
	return len;
}

/* Returns 0, or length of numbers: |||6446| */
static int try_espv_response(const char *data, size_t dlen,
			     struct cmd_info *cmd, char term)
{
	char delim;
	size_t len;

	/* Three delimiters. */
	if (dlen <= 3)
		return 0;

	delim = data[0];

	if (isdigit(delim) || delim < 33 || delim > 126
	    || data[1] != delim || data[2] != delim)
		return 0;

	len = get_port(&data[3], &cmd->port, dlen, delim);

	if(len == 0)
		return 0;

	return 3 + len + 1;
}

/* Return 1 for match, 0 for accept, -1 for partial. */
static int find_pattern(const char *data, size_t dlen,
			const char *pattern, size_t plen,
			char skip, char term,
			unsigned int *numoff,
			unsigned int *numlen,
			struct cmd_info *cmd,
			int (*getnum)(const char *, size_t, struct cmd_info *,
				      char))
{
	size_t i;

	DEBUGP("find_pattern `%s': dlen = %u\n", pattern, dlen);
	if (dlen == 0)
		return 0;

	if (dlen <= plen) {
		/* Short packet: try for partial? */
		if (strnicmp(data, pattern, dlen) == 0)
			return -1;
		else return 0;
	}

	if (strnicmp(data, pattern, plen) != 0) {
#if 0
		size_t i;

		DEBUGP("ftp: string mismatch\n");
		for (i = 0; i < plen; i++) {
			DEBUGP("ftp:char %u `%c'(%u) vs `%c'(%u)\n",
				i, data[i], data[i],
				pattern[i], pattern[i]);
		}
#endif
		return 0;
	}

	DEBUGP("Pattern matches!\n");
	/* Now we've found the constant string, try to skip
	   to the 'skip' character */
	for (i = plen; data[i] != skip; i++)
		if (i == dlen - 1) return -1;

	/* Skip over the last character */
	i++;

	DEBUGP("Skipped up to `%c'!\n", skip);

	*numoff = i;
	*numlen = getnum(data + i, dlen - i, cmd, term);
	if (!*numlen)
		return -1;

	DEBUGP("Match succeeded!\n");
	return 1;
}

static int help(const struct sk_buff *skb,
		unsigned int protoff,
		struct ip6_conntrack *ct,
		enum ip6_conntrack_info ctinfo)
{
	unsigned int dataoff, datalen;
	struct tcphdr tcph;
	u_int32_t old_seq_aft_nl;
	int old_seq_aft_nl_set, ret;
	int dir = CTINFO2DIR(ctinfo);
	unsigned int matchlen, matchoff;
	struct ip6_ct_ftp_master *ct_ftp_info = &ct->help.ct_ftp_info;
	struct ip6_conntrack_expect expect, *exp = &expect;
	struct ip6_ct_ftp_expect *exp_ftp_info = &exp->help.exp_ftp_info;

	unsigned int i;
	int found = 0;

	struct ipv6hdr *ipv6h = skb->nh.ipv6h;
	struct ip6_conntrack_tuple *t = &exp->tuple, *mask = &exp->mask;
	struct cmd_info cmd;
	unsigned int csum;

	/* Until there's been traffic both ways, don't look in packets. */
	if (ctinfo != IP6_CT_ESTABLISHED
	    && ctinfo != IP6_CT_ESTABLISHED+IP6_CT_IS_REPLY) {
		DEBUGP("ftp: Conntrackinfo = %u\n", ctinfo);
		return NF_ACCEPT;
	}

	if (skb_copy_bits(skb, protoff, &tcph, sizeof(tcph)) != 0) 
		return NF_ACCEPT;

	dataoff = protoff + tcph.doff * 4;
	/* No data? */
	if (dataoff >= skb->len) {
		DEBUGP("ftp: dataoff(%u) >= skblen(%u)\n", dataoff, skb->len);
		return NF_ACCEPT;
	}
	datalen = skb->len - dataoff;

	LOCK_BH(&ip6_ftp_lock);

	csum = skb_copy_and_csum_bits(skb, dataoff, ftp_buffer,
				      skb->len - dataoff, 0);
	csum = skb_checksum(skb, protoff, tcph.doff * 4, csum);

	/* Checksum invalid?  Ignore. */
	/* FIXME: Source route IP option packets --RR */
	if (csum_ipv6_magic(&ipv6h->saddr, &ipv6h->daddr, skb->len - protoff,
			    IPPROTO_TCP, csum)) {
		DEBUGP("ftp_help: bad csum: %p %u\n"
		       "%x:%x:%x:%x:%x:%x:%x:%x -> %x:%x:%x:%x:%x:%x:%x:%x\n",
		       &tcph, skb->len - protoff, NIP6(ipv6h->saddr),
		       NIP6(ipv6h->daddr));
		ret = NF_ACCEPT;
		goto out;
	}

	old_seq_aft_nl_set = ct_ftp_info->seq_aft_nl_set[dir];
	old_seq_aft_nl = ct_ftp_info->seq_aft_nl[dir];

	DEBUGP("conntrack_ftp: datalen %u\n", datalen);
	if (ftp_buffer[datalen - 1] == '\n') {
		DEBUGP("conntrack_ftp: datalen %u ends in \\n\n", datalen);
		if (!old_seq_aft_nl_set
		    || after(ntohl(tcph.seq) + datalen, old_seq_aft_nl)) {
			DEBUGP("conntrack_ftp: updating nl to %u\n",
			       ntohl(tcph.seq) + datalen);
			ct_ftp_info->seq_aft_nl[dir] = 
						ntohl(tcph.seq) + datalen;
			ct_ftp_info->seq_aft_nl_set[dir] = 1;
		}
	}

	if(!old_seq_aft_nl_set ||
			(ntohl(tcph.seq) != old_seq_aft_nl)) {
		DEBUGP("ip6_conntrack_ftp_help: wrong seq pos %s(%u)\n",
		       old_seq_aft_nl_set ? "":"(UNSET) ", old_seq_aft_nl);
		ret = NF_ACCEPT;
		goto out;
	}

	/* Initialize IP array to expected address (it's not mentioned
           in EPSV responses) */
	ipv6_addr_copy(&cmd.ip, &ct->tuplehash[dir].tuple.src.ip);

	for (i = 0; i < ARRAY_SIZE(search); i++) {
		if (search[i].dir != dir) continue;

		found = find_pattern(ftp_buffer, datalen,
				     search[i].pattern,
				     search[i].plen,
				     search[i].skip,
				     search[i].term,
				     &matchoff, &matchlen,
				     &cmd,
				     search[i].getnum);
		if (found) break;
	}
	if (found == -1) {
		/* We don't usually drop packets.  After all, this is
		   connection tracking, not packet filtering.
		   However, it is neccessary for accurate tracking in
		   this case. */
		if (net_ratelimit())
			printk("conntrack_ftp: partial %s %u+%u\n",
			       search[i].pattern,
			       ntohl(tcph.seq), datalen);
		ret = NF_DROP;
		goto out;
	} else if (found == 0) { /* No match */
		ret = NF_ACCEPT;
		goto out;
	}

	DEBUGP("conntrack_ftp: match `%.*s' (%u bytes at %u)\n",
	       (int)matchlen, ftp_buffer + matchoff,
	       matchlen, ntohl(tcph.seq) + matchoff);

	memset(&expect, 0, sizeof(expect));

	/* Update the ftp info */
	if (!ipv6_addr_cmp(&cmd.ip, &ct->tuplehash[dir].tuple.src.ip)) {
		exp->seq = ntohl(tcph.seq) + matchoff;
		exp_ftp_info->len = matchlen;
		exp_ftp_info->ftptype = search[i].ftptype;
		exp_ftp_info->port = cmd.port;
	} else {
		/*
		  This situation is occurred with NAT.
		 */
		if (!loose) {
			ret = NF_ACCEPT;
			goto out;
		}
	}

	ipv6_addr_copy(&t->src.ip, &ct->tuplehash[!dir].tuple.src.ip);
	ipv6_addr_copy(&t->dst.ip, &cmd.ip);
	t->src.u.tcp.port = 0;
	t->dst.u.tcp.port = cmd.port;
	t->dst.protonum = IPPROTO_TCP;

	ipv6_addr_set(&mask->src.ip, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF);
	mask->src.u.tcp.port = 0;
	mask->dst.u.tcp.port = 0xFFFF;
	mask->dst.protonum = 0xFFFF;

	exp->expectfn = NULL;

	/* Ignore failure; should only happen with NAT */
	ip6_conntrack_expect_related(ct, &expect);
	ret = NF_ACCEPT;
 out:
	UNLOCK_BH(&ip6_ftp_lock);
	return ret;
}

static struct ip6_conntrack_helper ftp[MAX_PORTS];
static char ftp_names[MAX_PORTS][10];

/* Not __exit: called from init() */
static void fini(void)
{
	int i;
	for (i = 0; i < ports_c; i++) {
		DEBUGP("ip6_ct_ftp: unregistering helper for port %d\n",
				ports[i]);
		ip6_conntrack_helper_unregister(&ftp[i]);
	}
}

static int __init init(void)
{
	int i, ret;
	char *tmpname;

	if (ports[0] == 0)
		ports[0] = FTP_PORT;

	for (i = 0; (i < MAX_PORTS) && ports[i]; i++) {
		memset(&ftp[i], 0, sizeof(struct ip6_conntrack_helper));
		ftp[i].tuple.src.u.tcp.port = htons(ports[i]);
		ftp[i].tuple.dst.protonum = IPPROTO_TCP;
		ftp[i].mask.src.u.tcp.port = 0xFFFF;
		ftp[i].mask.dst.protonum = 0xFFFF;
		ftp[i].max_expected = 1;
		ftp[i].timeout = 0;
		ftp[i].flags = IP6_CT_HELPER_F_REUSE_EXPECT;
		ftp[i].me = ip6_conntrack_ftp;
		ftp[i].help = help;

		tmpname = &ftp_names[i][0];
		if (ports[i] == FTP_PORT)
			sprintf(tmpname, "ftp");
		else
			sprintf(tmpname, "ftp-%d", ports[i]);
		ftp[i].name = tmpname;

		DEBUGP("ip6_ct_ftp: registering helper for port %d\n", 
				ports[i]);
		ret = ip6_conntrack_helper_register(&ftp[i]);

		if (ret) {
			fini();
			return ret;
		}
		ports_c++;
	}
	return 0;
}


PROVIDES_CONNTRACK6(ftp);
EXPORT_SYMBOL(ip6_ftp_lock);
MODULE_LICENSE("GPL");
module_init(init);
module_exit(fini);
