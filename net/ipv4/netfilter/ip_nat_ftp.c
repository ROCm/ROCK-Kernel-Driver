/* FTP extension for TCP NAT alteration. */
#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_conntrack_ftp.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define MAX_PORTS 8
static int ports[MAX_PORTS];
static int ports_c = 0;

#ifdef MODULE_PARM
MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_PORTS) "i");
#endif

DECLARE_LOCK_EXTERN(ip_ftp_lock);

/* FIXME: Time out? --RR */

static int
ftp_nat_expected(struct sk_buff **pskb,
		 unsigned int hooknum,
		 struct ip_conntrack *ct,
		 struct ip_nat_info *info,
		 struct ip_conntrack *master,
		 struct ip_nat_info *masterinfo,
		 unsigned int *verdict)
{
	struct ip_nat_multi_range mr;
	u_int32_t newdstip, newsrcip, newip;
	struct ip_ct_ftp *ftpinfo;

	IP_NF_ASSERT(info);
	IP_NF_ASSERT(master);
	IP_NF_ASSERT(masterinfo);

	IP_NF_ASSERT(!(info->initialized & (1<<HOOK2MANIP(hooknum))));

	DEBUGP("nat_expected: We have a connection!\n");
	/* Master must be an ftp connection */
	ftpinfo = &master->help.ct_ftp_info;

	LOCK_BH(&ip_ftp_lock);
	if (ftpinfo->is_ftp != 21) {
		UNLOCK_BH(&ip_ftp_lock);
		DEBUGP("nat_expected: master not ftp\n");
		return 0;
	}

	if (ftpinfo->ftptype == IP_CT_FTP_PORT
	    || ftpinfo->ftptype == IP_CT_FTP_EPRT) {
		/* PORT command: make connection go to the client. */
		newdstip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
		newsrcip = master->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
		DEBUGP("nat_expected: PORT cmd. %u.%u.%u.%u->%u.%u.%u.%u\n",
		       NIPQUAD(newsrcip), NIPQUAD(newdstip));
	} else {
		/* PASV command: make the connection go to the server */
		newdstip = master->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
		newsrcip = master->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
		DEBUGP("nat_expected: PASV cmd. %u.%u.%u.%u->%u.%u.%u.%u\n",
		       NIPQUAD(newsrcip), NIPQUAD(newdstip));
	}
	UNLOCK_BH(&ip_ftp_lock);

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC)
		newip = newsrcip;
	else
		newip = newdstip;

	DEBUGP("nat_expected: IP to %u.%u.%u.%u\n", NIPQUAD(newip));

	mr.rangesize = 1;
	/* We don't want to manip the per-protocol, just the IPs... */
	mr.range[0].flags = IP_NAT_RANGE_MAP_IPS;
	mr.range[0].min_ip = mr.range[0].max_ip = newip;

	/* ... unless we're doing a MANIP_DST, in which case, make
	   sure we map to the correct port */
	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_DST) {
		mr.range[0].flags |= IP_NAT_RANGE_PROTO_SPECIFIED;
		mr.range[0].min = mr.range[0].max
			= ((union ip_conntrack_manip_proto)
				{ htons(ftpinfo->port) });
	}
	*verdict = ip_nat_setup_info(ct, &mr, hooknum);

	return 1;
}

static int
mangle_rfc959_packet(struct sk_buff **pskb,
		     u_int32_t newip,
		     u_int16_t port,
		     unsigned int matchoff,
		     unsigned int matchlen,
		     struct ip_conntrack *ct,
		     enum ip_conntrack_info ctinfo)
{
	char buffer[sizeof("nnn,nnn,nnn,nnn,nnn,nnn")];

	MUST_BE_LOCKED(&ip_ftp_lock);

	sprintf(buffer, "%u,%u,%u,%u,%u,%u",
		NIPQUAD(newip), port>>8, port&0xFF);

	DEBUGP("calling ip_nat_mangle_tcp_packet\n");

	return ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff, 
					matchlen, buffer, strlen(buffer));
}

/* |1|132.235.1.2|6275| */
static int
mangle_eprt_packet(struct sk_buff **pskb,
		   u_int32_t newip,
		   u_int16_t port,
		   unsigned int matchoff,
		   unsigned int matchlen,
		   struct ip_conntrack *ct,
		   enum ip_conntrack_info ctinfo)
{
	char buffer[sizeof("|1|255.255.255.255|65535|")];

	MUST_BE_LOCKED(&ip_ftp_lock);

	sprintf(buffer, "|1|%u.%u.%u.%u|%u|", NIPQUAD(newip), port);

	DEBUGP("calling ip_nat_mangle_tcp_packet\n");

	return ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff, 
					matchlen, buffer, strlen(buffer));
}

/* |1|132.235.1.2|6275| */
static int
mangle_epsv_packet(struct sk_buff **pskb,
		   u_int32_t newip,
		   u_int16_t port,
		   unsigned int matchoff,
		   unsigned int matchlen,
		   struct ip_conntrack *ct,
		   enum ip_conntrack_info ctinfo)
{
	char buffer[sizeof("|||65535|")];

	MUST_BE_LOCKED(&ip_ftp_lock);

	sprintf(buffer, "|||%u|", port);

	DEBUGP("calling ip_nat_mangle_tcp_packet\n");

	return ip_nat_mangle_tcp_packet(pskb, ct, ctinfo, matchoff, 
					matchlen, buffer, strlen(buffer));
}

static int (*mangle[])(struct sk_buff **, u_int32_t, u_int16_t,
		     unsigned int,
		     unsigned int,
		     struct ip_conntrack *,
		     enum ip_conntrack_info)
= { [IP_CT_FTP_PORT] mangle_rfc959_packet,
    [IP_CT_FTP_PASV] mangle_rfc959_packet,
    [IP_CT_FTP_EPRT] mangle_eprt_packet,
    [IP_CT_FTP_EPSV] mangle_epsv_packet
};

static int ftp_data_fixup(const struct ip_ct_ftp *ct_ftp_info,
			  struct ip_conntrack *ct,
			  unsigned int datalen,
			  struct sk_buff **pskb,
			  enum ip_conntrack_info ctinfo)
{
	u_int32_t newip;
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph = (void *)iph + iph->ihl*4;
	u_int16_t port;
	struct ip_conntrack_tuple tuple;
	/* Don't care about source port */
	const struct ip_conntrack_tuple mask
		= { { 0xFFFFFFFF, { 0 } },
		    { 0xFFFFFFFF, { 0xFFFF }, 0xFFFF } };

	memset(&tuple, 0, sizeof(tuple));
	MUST_BE_LOCKED(&ip_ftp_lock);
	DEBUGP("FTP_NAT: seq %u + %u in %u + %u\n",
	       ct_ftp_info->seq, ct_ftp_info->len,
	       ntohl(tcph->seq), datalen);

	/* Change address inside packet to match way we're mapping
	   this connection. */
	if (ct_ftp_info->ftptype == IP_CT_FTP_PASV
	    || ct_ftp_info->ftptype == IP_CT_FTP_EPSV) {
		/* PASV/EPSV response: must be where client thinks server
		   is */
		newip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
		/* Expect something from client->server */
		tuple.src.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip;
		tuple.dst.ip = ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.ip;
	} else {
		/* PORT command: must be where server thinks client is */
		newip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
		/* Expect something from server->client */
		tuple.src.ip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip;
		tuple.dst.ip = ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip;
	}
	tuple.dst.protonum = IPPROTO_TCP;

	/* Try to get same port: if not, try to change it. */
	for (port = ct_ftp_info->port; port != 0; port++) {
		tuple.dst.u.tcp.port = htons(port);

		if (ip_conntrack_expect_related(ct, &tuple, &mask, NULL) == 0)
			break;
	}
	if (port == 0)
		return 0;

	if (!mangle[ct_ftp_info->ftptype](pskb, newip, port,
					  ct_ftp_info->seq - ntohl(tcph->seq),
					  ct_ftp_info->len, ct, ctinfo))
		return 0;

	return 1;
}

static unsigned int help(struct ip_conntrack *ct,
			 struct ip_nat_info *info,
			 enum ip_conntrack_info ctinfo,
			 unsigned int hooknum,
			 struct sk_buff **pskb)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph = (void *)iph + iph->ihl*4;
	unsigned int datalen;
	int dir;
	int score;
	struct ip_ct_ftp *ct_ftp_info
		= &ct->help.ct_ftp_info;

	/* Delete SACK_OK on initial TCP SYNs. */
	if (tcph->syn && !tcph->ack)
		ip_nat_delete_sack(*pskb, tcph);

	/* Only mangle things once: original direction in POST_ROUTING
	   and reply direction on PRE_ROUTING. */
	dir = CTINFO2DIR(ctinfo);
	if (!((hooknum == NF_IP_POST_ROUTING && dir == IP_CT_DIR_ORIGINAL)
	      || (hooknum == NF_IP_PRE_ROUTING && dir == IP_CT_DIR_REPLY))) {
		DEBUGP("nat_ftp: Not touching dir %s at hook %s\n",
		       dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY",
		       hooknum == NF_IP_POST_ROUTING ? "POSTROUTING"
		       : hooknum == NF_IP_PRE_ROUTING ? "PREROUTING"
		       : hooknum == NF_IP_LOCAL_OUT ? "OUTPUT" : "???");
		return NF_ACCEPT;
	}

	datalen = (*pskb)->len - iph->ihl * 4 - tcph->doff * 4;
	score = 0;
	LOCK_BH(&ip_ftp_lock);
	if (ct_ftp_info->len) {
		/* If it's in the right range... */
		score += between(ct_ftp_info->seq, ntohl(tcph->seq),
				 ntohl(tcph->seq) + datalen);
		score += between(ct_ftp_info->seq + ct_ftp_info->len,
				 ntohl(tcph->seq),
				 ntohl(tcph->seq) + datalen);
		if (score == 1) {
			/* Half a match?  This means a partial retransmisison.
			   It's a cracker being funky. */
			if (net_ratelimit()) {
				printk("FTP_NAT: partial packet %u/%u in %u/%u\n",
				       ct_ftp_info->seq, ct_ftp_info->len,
				       ntohl(tcph->seq),
				       ntohl(tcph->seq) + datalen);
			}
			UNLOCK_BH(&ip_ftp_lock);
			return NF_DROP;
		} else if (score == 2) {
			if (!ftp_data_fixup(ct_ftp_info, ct, datalen,
					    pskb, ctinfo)) {
				UNLOCK_BH(&ip_ftp_lock);
				return NF_DROP;
			}
			/* skb may have been reallocated */
			iph = (*pskb)->nh.iph;
			tcph = (void *)iph + iph->ihl*4;
		}
	}

	UNLOCK_BH(&ip_ftp_lock);

	ip_nat_seq_adjust(*pskb, ct, ctinfo);

	return NF_ACCEPT;
}

static struct ip_nat_helper ftp[MAX_PORTS];
static char ftp_names[MAX_PORTS][6];

static struct ip_nat_expect ftp_expect
= { { NULL, NULL }, ftp_nat_expected };

/* Not __exit: called from init() */
static void fini(void)
{
	int i;

	for (i = 0; (i < MAX_PORTS) && ports[i]; i++) {
		DEBUGP("ip_nat_ftp: unregistering port %d\n", ports[i]);
		ip_nat_helper_unregister(&ftp[i]);
	}

	ip_nat_expect_unregister(&ftp_expect);
}

static int __init init(void)
{
	int i, ret;
	char *tmpname;

	ret = ip_nat_expect_register(&ftp_expect);
	if (ret == 0) {
		if (ports[0] == 0)
			ports[0] = 21;

		for (i = 0; (i < MAX_PORTS) && ports[i]; i++) {

			memset(&ftp[i], 0, sizeof(struct ip_nat_helper));

			ftp[i].tuple.dst.protonum = IPPROTO_TCP;
			ftp[i].tuple.src.u.tcp.port = htons(ports[i]);
			ftp[i].mask.dst.protonum = 0xFFFF;
			ftp[i].mask.src.u.tcp.port = 0xFFFF;
			ftp[i].help = help;

			tmpname = &ftp_names[i][0];
			sprintf(tmpname, "ftp%2.2d", i);
			ftp[i].name = tmpname;

			DEBUGP("ip_nat_ftp: Trying to register for port %d\n",
					ports[i]);
			ret = ip_nat_helper_register(&ftp[i]);

			if (ret) {
				printk("ip_nat_ftp: error registering helper for port %d\n", ports[i]);
				fini();
				return ret;
			}
			ports_c++;
		}

	} else {
		ip_nat_expect_unregister(&ftp_expect);
	}
	return ret;
}

module_init(init);
module_exit(fini);
MODULE_LICENSE("GPL");
