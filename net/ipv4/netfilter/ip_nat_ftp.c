/* FTP extension for TCP NAT alteration. */
#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/netfilter_ipv4/ip_nat_ftp.h>
#include <linux/netfilter_ipv4/ip_conntrack_ftp.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

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
	if (!ftpinfo->is_ftp) {
		UNLOCK_BH(&ip_ftp_lock);
		DEBUGP("nat_expected: master not ftp\n");
		return 0;
	}

	if (ftpinfo->ftptype == IP_CT_FTP_PORT) {
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

/* This is interesting.  We simply use the port given us by the client
   or server.  In practice it's extremely unlikely to clash; if it
   does, the rule won't be able to get a unique tuple and will drop
   the packets. */
static int
mangle_packet(struct sk_buff **pskb,
	      u_int32_t newip,
	      u_int16_t port,
	      unsigned int matchoff,
	      unsigned int matchlen,
	      struct ip_nat_ftp_info *this_way,
	      struct ip_nat_ftp_info *other_way)
{
	struct iphdr *iph = (*pskb)->nh.iph;
	struct tcphdr *tcph;
	unsigned char *data;
	unsigned int tcplen, newlen, newtcplen;
	char buffer[sizeof("nnn,nnn,nnn,nnn,nnn,nnn")];

	MUST_BE_LOCKED(&ip_ftp_lock);
	sprintf(buffer, "%u,%u,%u,%u,%u,%u",
		NIPQUAD(newip), port>>8, port&0xFF);

	tcplen = (*pskb)->len - iph->ihl * 4;
	newtcplen = tcplen - matchlen + strlen(buffer);
	newlen = iph->ihl*4 + newtcplen;

	/* So there I am, in the middle of my `netfilter-is-wonderful'
	   talk in Sydney, and someone asks `What happens if you try
	   to enlarge a 64k packet here?'.  I think I said something
	   eloquent like `fuck'. */
	if (newlen > 65535) {
		if (net_ratelimit())
			printk("nat_ftp cheat: %u.%u.%u.%u->%u.%u.%u.%u %u\n",
			       NIPQUAD((*pskb)->nh.iph->saddr),
			       NIPQUAD((*pskb)->nh.iph->daddr),
			       (*pskb)->nh.iph->protocol);
		return 0;
	}

	if (newlen > (*pskb)->len + skb_tailroom(*pskb)) {
		struct sk_buff *newskb;
		newskb = skb_copy_expand(*pskb, skb_headroom(*pskb),
					 newlen - (*pskb)->len,
					 GFP_ATOMIC);
		if (!newskb) {
			DEBUGP("ftp: oom\n");
			return 0;
		} else {
			kfree_skb(*pskb);
			*pskb = newskb;
			iph = (*pskb)->nh.iph;
		}
	}

	tcph = (void *)iph + iph->ihl*4;
	data = (void *)tcph + tcph->doff*4;

	DEBUGP("Mapping `%.*s' [%u %u %u] to new `%s' [%u]\n",
		 (int)matchlen, data+matchoff,
		 data[matchoff], data[matchoff+1],
		 matchlen, buffer, strlen(buffer));

	/* SYN adjust.  If it's uninitialized, or this is after last
           correction, record it: we don't handle more than one
           adjustment in the window, but do deal with common case of a
           retransmit. */
	if (this_way->syn_offset_before == this_way->syn_offset_after
	    || before(this_way->syn_correction_pos, ntohl(tcph->seq))) {
		this_way->syn_correction_pos = ntohl(tcph->seq);
		this_way->syn_offset_before = this_way->syn_offset_after;
		this_way->syn_offset_after = (int32_t)
			this_way->syn_offset_before + newlen - (*pskb)->len;
	}

	/* Move post-replacement */
	memmove(data + matchoff + strlen(buffer),
		data + matchoff + matchlen,
		(*pskb)->tail - (data + matchoff + matchlen));
	memcpy(data + matchoff, buffer, strlen(buffer));

	/* Resize packet. */
	if (newlen > (*pskb)->len) {
		DEBUGP("ip_nat_ftp: Extending packet by %u to %u bytes\n",
		       newlen - (*pskb)->len, newlen);
		skb_put(*pskb, newlen - (*pskb)->len);
	} else {
		DEBUGP("ip_nat_ftp: Shrinking packet from %u to %u bytes\n",
		       (*pskb)->len, newlen);
		skb_trim(*pskb, newlen);
	}

	/* Fix checksums */
	iph->tot_len = htons(newlen);
	(*pskb)->csum = csum_partial((char *)tcph + tcph->doff*4,
				     newtcplen - tcph->doff*4, 0);
	tcph->check = 0;
	tcph->check = tcp_v4_check(tcph, newtcplen, iph->saddr, iph->daddr,
				   csum_partial((char *)tcph, tcph->doff*4,
						(*pskb)->csum));
	ip_send_check(iph);
	return 1;
}

/* Grrr... SACK.  Fuck me even harder.  Don't want to fix it on the
   fly, so blow it away. */
static void
delete_sack(struct sk_buff *skb, struct tcphdr *tcph)
{
	unsigned int i;
	u_int8_t *opt = (u_int8_t *)tcph;

	DEBUGP("Seeking SACKPERM in SYN packet (doff = %u).\n",
	       tcph->doff * 4);
	for (i = sizeof(struct tcphdr); i < tcph->doff * 4;) {
		DEBUGP("%u ", opt[i]);
		switch (opt[i]) {
		case TCPOPT_NOP:
		case TCPOPT_EOL:
			i++;
			break;

		case TCPOPT_SACK_PERM:
			goto found_opt;

		default:
			/* Worst that can happen: it will take us over. */
			i += opt[i+1] ?: 1;
		}
	}
	DEBUGP("\n");
	return;

 found_opt:
	DEBUGP("\n");
	DEBUGP("Found SACKPERM at offset %u.\n", i);

	/* Must be within TCP header, and valid SACK perm. */
	if (i + opt[i+1] <= tcph->doff*4 && opt[i+1] == 2) {
		/* Replace with NOPs. */
		tcph->check
			= ip_nat_cheat_check(*((u_int16_t *)(opt + i))^0xFFFF,
					     0, tcph->check);
		opt[i] = opt[i+1] = 0;
	}
	else DEBUGP("Something wrong with SACK_PERM.\n");
}

static int ftp_data_fixup(const struct ip_ct_ftp *ct_ftp_info,
			  struct ip_conntrack *ct,
			  struct ip_nat_ftp_info *ftp,
			  unsigned int datalen,
			  struct sk_buff **pskb)
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
	if (ct_ftp_info->ftptype == IP_CT_FTP_PASV) {
		/* PASV response: must be where client thinks server
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

	if (!mangle_packet(pskb, newip, port,
			   ct_ftp_info->seq - ntohl(tcph->seq),
			   ct_ftp_info->len,
			   &ftp[ct_ftp_info->ftptype],
			   &ftp[!ct_ftp_info->ftptype]))
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
	u_int32_t newseq, newack;
	unsigned int datalen;
	int dir;
	int score;
	struct ip_ct_ftp *ct_ftp_info
		= &ct->help.ct_ftp_info;
	struct ip_nat_ftp_info *ftp
		= &ct->nat.help.ftp_info[0];

	/* Delete SACK_OK on initial TCP SYNs. */
	if (tcph->syn && !tcph->ack)
		delete_sack(*pskb, tcph);

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
			if (!ftp_data_fixup(ct_ftp_info, ct, ftp, datalen,
					    pskb)) {
				UNLOCK_BH(&ip_ftp_lock);
				return NF_DROP;
			}

			/* skb may have been reallocated */
			iph = (*pskb)->nh.iph;
			tcph = (void *)iph + iph->ihl*4;
		}
	}

	/* Sequence adjust */
	if (after(ntohl(tcph->seq), ftp[dir].syn_correction_pos))
		newseq = ntohl(tcph->seq) + ftp[dir].syn_offset_after;
	else
		newseq = ntohl(tcph->seq) + ftp[dir].syn_offset_before;
	newseq = htonl(newseq);

	/* Ack adjust: other dir sees offset seq numbers */
	if (after(ntohl(tcph->ack_seq) - ftp[!dir].syn_offset_before, 
		  ftp[!dir].syn_correction_pos))
		newack = ntohl(tcph->ack_seq) - ftp[!dir].syn_offset_after;
	else
		newack = ntohl(tcph->ack_seq) - ftp[!dir].syn_offset_before;
	newack = htonl(newack);
	UNLOCK_BH(&ip_ftp_lock);

	tcph->check = ip_nat_cheat_check(~tcph->seq, newseq,
					 ip_nat_cheat_check(~tcph->ack_seq,
							    newack,
							    tcph->check));
	tcph->seq = newseq;
	tcph->ack_seq = newack;

	return NF_ACCEPT;
}

static struct ip_nat_helper ftp = { { NULL, NULL },
				    { { 0, { __constant_htons(21) } },
				      { 0, { 0 }, IPPROTO_TCP } },
				    { { 0, { 0xFFFF } },
				      { 0, { 0 }, 0xFFFF } },
				    help, "ftp" };
static struct ip_nat_expect ftp_expect
= { { NULL, NULL }, ftp_nat_expected };

static int __init init(void)
{
	int ret;

	ret = ip_nat_expect_register(&ftp_expect);
	if (ret == 0) {
		ret = ip_nat_helper_register(&ftp);

		if (ret != 0)
			ip_nat_expect_unregister(&ftp_expect);
	}
	return ret;
}

static void __exit fini(void)
{
	ip_nat_helper_unregister(&ftp);
	ip_nat_expect_unregister(&ftp_expect);
}

module_init(init);
module_exit(fini);
