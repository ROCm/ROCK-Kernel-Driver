/* FTP extension for IP connection tracking. */
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/ip.h>
#include <net/checksum.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/lockhelp.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_ftp.h>

DECLARE_LOCK(ip_ftp_lock);
struct module *ip_conntrack_ftp = THIS_MODULE;

#define SERVER_STRING "227 Entering Passive Mode ("
#define CLIENT_STRING "PORT "

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

static struct {
	const char *pattern;
	size_t plen;
	char term;
} search[2] = {
	[IP_CT_FTP_PORT] { CLIENT_STRING, sizeof(CLIENT_STRING) - 1, '\r' },
	[IP_CT_FTP_PASV] { SERVER_STRING, sizeof(SERVER_STRING) - 1, ')' }
};

/* Returns 0, or length of numbers */
static int try_number(const char *data, size_t dlen, u_int32_t array[6],
		      char term)
{
	u_int32_t i, len;

	/* Keep data pointing at next char. */
	for (i = 0, len = 0; len < dlen; len++, data++) {
		if (*data >= '0' && *data <= '9') {
			array[i] = array[i]*10 + *data - '0';
		}
		else if (*data == ',')
			i++;
		else {
			/* Unexpected character; true if it's the
			   terminator and we're finished. */
			if (*data == term && i == 5)
				return len;

			DEBUGP("Char %u (got %u nums) `%u' unexpected\n",
			       len, i, *data);
			return 0;
		}
	}

	return 0;
}

/* Return 1 for match, 0 for accept, -1 for partial. */
static int find_pattern(const char *data, size_t dlen,
			const char *pattern, size_t plen,
			char term,
			unsigned int *numoff,
			unsigned int *numlen,
			u_int32_t array[6])
{
	if (dlen == 0)
		return 0;

	if (dlen < plen) {
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
			DEBUGFTP("ftp:char %u `%c'(%u) vs `%c'(%u)\n",
				 i, data[i], data[i],
				 pattern[i], pattern[i]);
		}
#endif
		return 0;
	}

	*numoff = plen;
	*numlen = try_number(data + plen, dlen - plen, array, term);
	if (!*numlen)
		return -1;

	return 1;
}

/* FIXME: This should be in userspace.  Later. */
static int help(const struct iphdr *iph, size_t len,
		struct ip_conntrack *ct,
		enum ip_conntrack_info ctinfo)
{
	/* tcplen not negative guaranteed by ip_conntrack_tcp.c */
	struct tcphdr *tcph = (void *)iph + iph->ihl * 4;
	const char *data = (const char *)tcph + tcph->doff * 4;
	unsigned int tcplen = len - iph->ihl * 4;
	unsigned int datalen = tcplen - tcph->doff * 4;
	u_int32_t old_seq_aft_nl;
	int old_seq_aft_nl_set;
	u_int32_t array[6] = { 0 };
	int dir = CTINFO2DIR(ctinfo);
	unsigned int matchlen, matchoff;
	struct ip_conntrack_tuple t, mask;
	struct ip_ct_ftp *info = &ct->help.ct_ftp_info;

	/* Until there's been traffic both ways, don't look in packets. */
	if (ctinfo != IP_CT_ESTABLISHED
	    && ctinfo != IP_CT_ESTABLISHED+IP_CT_IS_REPLY) {
		DEBUGP("ftp: Conntrackinfo = %u\n", ctinfo);
		return NF_ACCEPT;
	}

	/* Not whole TCP header? */
	if (tcplen < sizeof(struct tcphdr) || tcplen < tcph->doff*4) {
		DEBUGP("ftp: tcplen = %u\n", (unsigned)tcplen);
		return NF_ACCEPT;
	}

	/* Checksum invalid?  Ignore. */
	/* FIXME: Source route IP option packets --RR */
	if (tcp_v4_check(tcph, tcplen, iph->saddr, iph->daddr,
			 csum_partial((char *)tcph, tcplen, 0))) {
		DEBUGP("ftp_help: bad csum: %p %u %u.%u.%u.%u %u.%u.%u.%u\n",
		       tcph, tcplen, NIPQUAD(iph->saddr),
		       NIPQUAD(iph->daddr));
		return NF_ACCEPT;
	}

	LOCK_BH(&ip_ftp_lock);
	old_seq_aft_nl_set = info->seq_aft_nl_set[dir];
	old_seq_aft_nl = info->seq_aft_nl[dir];

	DEBUGP("conntrack_ftp: datalen %u\n", datalen);
	if ((datalen > 0) && (data[datalen-1] == '\n')) {
		DEBUGP("conntrack_ftp: datalen %u ends in \\n\n", datalen);
		if (!old_seq_aft_nl_set
		    || after(ntohl(tcph->seq) + datalen, old_seq_aft_nl)) {
			DEBUGP("conntrack_ftp: updating nl to %u\n",
			       ntohl(tcph->seq) + datalen);
			info->seq_aft_nl[dir] = ntohl(tcph->seq) + datalen;
			info->seq_aft_nl_set[dir] = 1;
		}
	}
	UNLOCK_BH(&ip_ftp_lock);

	if(!old_seq_aft_nl_set ||
			(ntohl(tcph->seq) != old_seq_aft_nl)) {
		DEBUGP("ip_conntrack_ftp_help: wrong seq pos %s(%u)\n",
		       old_seq_aft_nl_set ? "":"(UNSET) ", old_seq_aft_nl);
		return NF_ACCEPT;
	}

	switch (find_pattern(data, datalen,
			     search[dir].pattern,
			     search[dir].plen, search[dir].term,
			     &matchoff, &matchlen,
			     array)) {
	case -1: /* partial */
		/* We don't usually drop packets.  After all, this is
		   connection tracking, not packet filtering.
		   However, it is neccessary for accurate tracking in
		   this case. */
		if (net_ratelimit())
			printk("conntrack_ftp: partial %u+%u\n",
			       ntohl(tcph->seq), datalen);
		return NF_DROP;

	case 0: /* no match */
		DEBUGP("ip_conntrack_ftp_help: no match\n");
		return NF_ACCEPT;
	}

	DEBUGP("conntrack_ftp: match `%.*s' (%u bytes at %u)\n",
	       (int)matchlen, data + matchoff,
	       matchlen, ntohl(tcph->seq) + matchoff);

	/* Update the ftp info */
	LOCK_BH(&ip_ftp_lock);
	if (htonl((array[0] << 24) | (array[1] << 16) | (array[2] << 8) | array[3])
	    == ct->tuplehash[dir].tuple.src.ip) {
		info->is_ftp = 1;
		info->seq = ntohl(tcph->seq) + matchoff;
		info->len = matchlen;
		info->ftptype = dir;
		info->port = array[4] << 8 | array[5];
	} else {
		/* Enrico Scholz's passive FTP to partially RNAT'd ftp
		   server: it really wants us to connect to a
		   different IP address.  Simply don't record it for
		   NAT. */
		DEBUGP("conntrack_ftp: NOT RECORDING: %u,%u,%u,%u != %u.%u.%u.%u\n",
		       array[0], array[1], array[2], array[3],
		       NIPQUAD(ct->tuplehash[dir].tuple.src.ip));
	}

	t = ((struct ip_conntrack_tuple)
		{ { ct->tuplehash[!dir].tuple.src.ip,
		    { 0 } },
		  { htonl((array[0] << 24) | (array[1] << 16)
			  | (array[2] << 8) | array[3]),
		    { htons(array[4] << 8 | array[5]) },
		    IPPROTO_TCP }});
	mask = ((struct ip_conntrack_tuple)
		{ { 0xFFFFFFFF, { 0 } },
		  { 0xFFFFFFFF, { 0xFFFF }, 0xFFFF }});
	/* Ignore failure; should only happen with NAT */
	ip_conntrack_expect_related(ct, &t, &mask, NULL);
	UNLOCK_BH(&ip_ftp_lock);

	return NF_ACCEPT;
}

static struct ip_conntrack_helper ftp = { { NULL, NULL },
					  { { 0, { __constant_htons(21) } },
					    { 0, { 0 }, IPPROTO_TCP } },
					  { { 0, { 0xFFFF } },
					    { 0, { 0 }, 0xFFFF } },
					  help };

static int __init init(void)
{
	return ip_conntrack_helper_register(&ftp);
}

static void __exit fini(void)
{
	ip_conntrack_helper_unregister(&ftp);
}

EXPORT_SYMBOL(ip_ftp_lock);
EXPORT_SYMBOL(ip_conntrack_ftp);

module_init(init);
module_exit(fini);
