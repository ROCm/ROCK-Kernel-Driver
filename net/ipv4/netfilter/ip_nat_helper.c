/* ip_nat_mangle.c - generic support functions for NAT helpers 
 *
 * (C) 2000 by Harald Welte <laforge@gnumonks.org>
 *
 * distributed under the terms of GNU GPL
 */
#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <linux/brlock.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#define DUMP_OFFSET(x)	printk("offset_before=%d, offset_after=%d, correction_pos=%u\n", x->offset_before, x->offset_after, x->correction_pos);
#else
#define DEBUGP(format, args...)
#define DUMP_OFFSET(x)
#endif
	
DECLARE_LOCK(ip_nat_seqofs_lock);
			 
static inline int 
ip_nat_resize_packet(struct sk_buff **skb,
		     struct ip_conntrack *ct, 
		     enum ip_conntrack_info ctinfo,
		     int new_size)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	void *data;
	int dir;
	struct ip_nat_seq *this_way, *other_way;

	DEBUGP("ip_nat_resize_packet: old_size = %u, new_size = %u\n",
		(*skb)->len, new_size);

	iph = (*skb)->nh.iph;
	tcph = (void *)iph + iph->ihl*4;
	data = (void *)tcph + tcph->doff*4;

	dir = CTINFO2DIR(ctinfo);

	this_way = &ct->nat.info.seq[dir];
	other_way = &ct->nat.info.seq[!dir];

	if (new_size > (*skb)->len + skb_tailroom(*skb)) {
		struct sk_buff *newskb;
		newskb = skb_copy_expand(*skb, skb_headroom(*skb),
					 new_size - (*skb)->len,
					 GFP_ATOMIC);

		if (!newskb) {
			printk("ip_nat_resize_packet: oom\n");
			return 0;
		} else {
			kfree_skb(*skb);
			*skb = newskb;
		}
	}

	iph = (*skb)->nh.iph;
	tcph = (void *)iph + iph->ihl*4;
	data = (void *)tcph + tcph->doff*4;

	DEBUGP("ip_nat_resize_packet: Seq_offset before: ");
	DUMP_OFFSET(this_way);

	LOCK_BH(&ip_nat_seqofs_lock);

	/* SYN adjust. If it's uninitialized, of this is after last 
	 * correction, record it: we don't handle more than one 
	 * adjustment in the window, but do deal with common case of a 
	 * retransmit */
	if (this_way->offset_before == this_way->offset_after
	    || before(this_way->correction_pos, ntohl(tcph->seq))) {
		this_way->correction_pos = ntohl(tcph->seq);
		this_way->offset_before = this_way->offset_after;
		this_way->offset_after = (int32_t)
			this_way->offset_before + new_size - (*skb)->len;
	}

	UNLOCK_BH(&ip_nat_seqofs_lock);

	DEBUGP("ip_nat_resize_packet: Seq_offset after: ");
	DUMP_OFFSET(this_way);
	
	return 1;
}


/* Generic function for mangling variable-length address changes inside
 * NATed connections (like the PORT XXX,XXX,XXX,XXX,XXX,XXX command in FTP).
 *
 * Takes care about all the nasty sequence number changes, checksumming,
 * skb enlargement, ...
 *
 * */
int 
ip_nat_mangle_tcp_packet(struct sk_buff **skb,
			 struct ip_conntrack *ct,
			 enum ip_conntrack_info ctinfo,
			 unsigned int match_offset,
			 unsigned int match_len,
			 char *rep_buffer,
			 unsigned int rep_len)
{
	struct iphdr *iph = (*skb)->nh.iph;
	struct tcphdr *tcph;
	unsigned char *data;
	u_int32_t tcplen, newlen, newtcplen;

	tcplen = (*skb)->len - iph->ihl*4;
	newtcplen = tcplen - match_len + rep_len;
	newlen = iph->ihl*4 + newtcplen;

	if (newlen > 65535) {
		if (net_ratelimit())
			printk("ip_nat_mangle_tcp_packet: nat'ed packet "
				"exceeds maximum packet size\n");
		return 0;
	}

	if ((*skb)->len != newlen) {
		if (!ip_nat_resize_packet(skb, ct, ctinfo, newlen)) {
			printk("resize_packet failed!!\n");
			return 0;
		}
	}

	/* Alexey says: if a hook changes _data_ ... it can break
	   original packet sitting in tcp queue and this is fatal */
	if (skb_cloned(*skb)) {
		struct sk_buff *nskb = skb_copy(*skb, GFP_ATOMIC);
		if (!nskb) {
			if (net_ratelimit())
				printk("Out of memory cloning TCP packet\n");
			return 0;
		}
		/* Rest of kernel will get very unhappy if we pass it
		   a suddenly-orphaned skbuff */
		if ((*skb)->sk)
			skb_set_owner_w(nskb, (*skb)->sk);
		kfree_skb(*skb);
		*skb = nskb;
	}

	/* skb may be copied !! */
	iph = (*skb)->nh.iph;
	tcph = (void *)iph + iph->ihl*4;
	data = (void *)tcph + tcph->doff*4;

	/* move post-replacement */
	memmove(data + match_offset + rep_len,
		 data + match_offset + match_len,
		 (*skb)->tail - (data + match_offset + match_len));

	/* insert data from buffer */
	memcpy(data + match_offset, rep_buffer, rep_len);

	/* update skb info */
	if (newlen > (*skb)->len) {
		DEBUGP("ip_nat_mangle_tcp_packet: Extending packet by "
			"%u to %u bytes\n", newlen - (*skb)->len, newlen);
		skb_put(*skb, newlen - (*skb)->len);
	} else {
		DEBUGP("ip_nat_mangle_tcp_packet: Shrinking packet from "
			"%u to %u bytes\n", (*skb)->len, newlen);
		skb_trim(*skb, newlen);
	}

	/* fix checksum information */

	iph->tot_len = htons(newlen);
	(*skb)->csum = csum_partial((char *)tcph + tcph->doff*4,
				    newtcplen - tcph->doff*4, 0);

	tcph->check = 0;
	tcph->check = tcp_v4_check(tcph, newtcplen, iph->saddr, iph->daddr,
				   csum_partial((char *)tcph, tcph->doff*4,
					   (*skb)->csum));
	ip_send_check(iph);

	return 1;
}

/* TCP sequence number adjustment */
int 
ip_nat_seq_adjust(struct sk_buff *skb, 
		  struct ip_conntrack *ct, 
		  enum ip_conntrack_info ctinfo)
{
	struct iphdr *iph;
	struct tcphdr *tcph;
	int dir, newseq, newack;
	struct ip_nat_seq *this_way, *other_way;	
	
	iph = skb->nh.iph;
	tcph = (void *)iph + iph->ihl*4;

	dir = CTINFO2DIR(ctinfo);

	this_way = &ct->nat.info.seq[dir];
	other_way = &ct->nat.info.seq[!dir];
	
	if (after(ntohl(tcph->seq), this_way->correction_pos))
		newseq = ntohl(tcph->seq) + this_way->offset_after;
	else
		newseq = ntohl(tcph->seq) + this_way->offset_before;
	newseq = htonl(newseq);

	if (after(ntohl(tcph->ack_seq) - other_way->offset_before,
		  other_way->correction_pos))
		newack = ntohl(tcph->ack_seq) - other_way->offset_after;
	else
		newack = ntohl(tcph->ack_seq) - other_way->offset_before;
	newack = htonl(newack);

	tcph->check = ip_nat_cheat_check(~tcph->seq, newseq,
					 ip_nat_cheat_check(~tcph->ack_seq, 
					 		    newack, 
							    tcph->check));

	DEBUGP("Adjusting sequence number from %u->%u, ack from %u->%u\n",
		ntohl(tcph->seq), ntohl(newseq), ntohl(tcph->ack_seq),
		ntohl(newack));

	tcph->seq = newseq;
	tcph->ack_seq = newack;

	return 0;
}

/* Grrr... SACK.  Fuck me even harder.  Don't want to fix it on the
   fly, so blow it away. */
void
ip_nat_delete_sack(struct sk_buff *skb, struct tcphdr *tcph)
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
					     (TCPOPT_NOP<<8)|TCPOPT_NOP, tcph->check);
		opt[i] = opt[i+1] = TCPOPT_NOP;
	}
	else DEBUGP("Something wrong with SACK_PERM.\n");
}

static inline int
helper_cmp(const struct ip_nat_helper *helper,
	   const struct ip_conntrack_tuple *tuple)
{
	return ip_ct_tuple_mask_cmp(tuple, &helper->tuple, &helper->mask);
}

int ip_nat_helper_register(struct ip_nat_helper *me)
{
	int ret = 0;

	WRITE_LOCK(&ip_nat_lock);
	if (LIST_FIND(&helpers, helper_cmp, struct ip_nat_helper *,&me->tuple))
		ret = -EBUSY;
	else {
		list_prepend(&helpers, me);
		MOD_INC_USE_COUNT;
	}
	WRITE_UNLOCK(&ip_nat_lock);

	return ret;
}

static int
kill_helper(const struct ip_conntrack *i, void *helper)
{
	int ret;

	READ_LOCK(&ip_nat_lock);
	ret = (i->nat.info.helper == helper);
	READ_UNLOCK(&ip_nat_lock);

	return ret;
}

void ip_nat_helper_unregister(struct ip_nat_helper *me)
{
	WRITE_LOCK(&ip_nat_lock);
	LIST_DELETE(&helpers, me);
	WRITE_UNLOCK(&ip_nat_lock);

	/* Someone could be still looking at the helper in a bh. */
	br_write_lock_bh(BR_NETPROTO_LOCK);
	br_write_unlock_bh(BR_NETPROTO_LOCK);

	/* Find anything using it, and umm, kill them.  We can't turn
	   them into normal connections: if we've adjusted SYNs, then
	   they'll ackstorm.  So we just drop it.  We used to just
	   bump module count when a connection existed, but that
	   forces admins to gen fake RSTs or bounce box, either of
	   which is just a long-winded way of making things
	   worse. --RR */
	ip_ct_selective_cleanup(kill_helper, me);

	MOD_DEC_USE_COUNT;
}
