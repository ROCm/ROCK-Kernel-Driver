/* NAT for netfilter; shared with compatibility layer. */

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netfilter_ipv4.h>
#include <linux/vmalloc.h>
#include <net/checksum.h>
#include <net/icmp.h>
#include <net/ip.h>
#include <net/tcp.h>  /* For tcp_prot in getorigdst */
#include <linux/icmp.h>
#include <linux/udp.h>
#include <linux/jhash.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_nat_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_nat_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_nat.h>
#include <linux/netfilter_ipv4/ip_nat_protocol.h>
#include <linux/netfilter_ipv4/ip_nat_core.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_RWLOCK(ip_nat_lock);
DECLARE_RWLOCK_EXTERN(ip_conntrack_lock);

/* Calculated at init based on memory size */
static unsigned int ip_nat_htable_size;

static struct list_head *bysource;
struct ip_nat_protocol *ip_nat_protos[MAX_IP_NAT_PROTO];


/* We keep extra hashes for each conntrack, for fast searching. */
static inline size_t
hash_by_ipsproto(u_int32_t src, u_int32_t dst, u_int16_t proto)
{
	/* Modified src and dst, to ensure we don't create two
           identical streams. */
	return (src + dst + proto) % ip_nat_htable_size;
}

static inline size_t
hash_by_src(const struct ip_conntrack_manip *manip, u_int16_t proto)
{
	/* Original src, to ensure we map it consistently if poss. */
	return (manip->ip + manip->u.all + proto) % ip_nat_htable_size;
}

/* Noone using conntrack by the time this called. */
static void ip_nat_cleanup_conntrack(struct ip_conntrack *conn)
{
	struct ip_nat_info *info = &conn->nat.info;
	unsigned int hs, hp;

	if (!info->initialized)
		return;

	hs = hash_by_src(&conn->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src,
	                 conn->tuplehash[IP_CT_DIR_ORIGINAL]
	                 .tuple.dst.protonum);

	hp = hash_by_ipsproto(conn->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip,
	                      conn->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip,
	                      conn->tuplehash[IP_CT_DIR_REPLY]
	                      .tuple.dst.protonum);

	WRITE_LOCK(&ip_nat_lock);
	list_del(&info->bysource);
	WRITE_UNLOCK(&ip_nat_lock);
}

/* We do checksum mangling, so if they were wrong before they're still
 * wrong.  Also works for incomplete packets (eg. ICMP dest
 * unreachables.) */
u_int16_t
ip_nat_cheat_check(u_int32_t oldvalinv, u_int32_t newval, u_int16_t oldcheck)
{
	u_int32_t diffs[] = { oldvalinv, newval };
	return csum_fold(csum_partial((char *)diffs, sizeof(diffs),
				      oldcheck^0xFFFF));
}

/* Is this tuple already taken? (not by us) */
int
ip_nat_used_tuple(const struct ip_conntrack_tuple *tuple,
		  const struct ip_conntrack *ignored_conntrack)
{
	/* Conntrack tracking doesn't keep track of outgoing tuples; only
	   incoming ones.  NAT means they don't have a fixed mapping,
	   so we invert the tuple and look for the incoming reply.

	   We could keep a separate hash if this proves too slow. */
	struct ip_conntrack_tuple reply;

	invert_tuplepr(&reply, tuple);
	return ip_conntrack_tuple_taken(&reply, ignored_conntrack);
}

/* Before 2.6.11 we did implicit source NAT if required. Warn about change. */
static void warn_if_extra_mangle(u32 dstip, u32 srcip)
{
	static int warned = 0;
	struct flowi fl = { .nl_u = { .ip4_u = { .daddr = dstip } } };
	struct rtable *rt;

	if (ip_route_output_key(&rt, &fl) != 0)
		return;

	if (rt->rt_src != srcip && !warned) {
		printk("NAT: no longer support implicit source local NAT\n");
		printk("NAT: packet src %u.%u.%u.%u -> dst %u.%u.%u.%u\n",
		       NIPQUAD(srcip), NIPQUAD(dstip));
		warned = 1;
	}
	ip_rt_put(rt);
}

/* If we source map this tuple so reply looks like reply_tuple, will
 * that meet the constraints of range. */
static int
in_range(const struct ip_conntrack_tuple *tuple,
	 const struct ip_nat_range *range)
{
	struct ip_nat_protocol *proto = ip_nat_find_proto(tuple->dst.protonum);

	/* If we are supposed to map IPs, then we must be in the
	   range specified, otherwise let this drag us onto a new src IP. */
	if (range->flags & IP_NAT_RANGE_MAP_IPS) {
		if (ntohl(tuple->src.ip) < ntohl(range->min_ip)
		    || ntohl(tuple->src.ip) > ntohl(range->max_ip))
			return 0;
	}

	if (!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED)
	    || proto->in_range(tuple, IP_NAT_MANIP_SRC,
			       &range->min, &range->max))
		return 1;

	return 0;
}

static inline int
same_src(const struct ip_conntrack *ct,
	 const struct ip_conntrack_tuple *tuple)
{
	return (ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum
		== tuple->dst.protonum
		&& ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.ip
		== tuple->src.ip
		&& ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u.all
		== tuple->src.u.all);
}

/* Only called for SRC manip */
static int
find_appropriate_src(const struct ip_conntrack_tuple *tuple,
		     struct ip_conntrack_tuple *result,
		     const struct ip_nat_range *range)
{
	unsigned int h = hash_by_src(&tuple->src, tuple->dst.protonum);
	struct ip_conntrack *ct;

	MUST_BE_READ_LOCKED(&ip_nat_lock);

	list_for_each_entry(ct, &bysource[h], nat.info.bysource) {
		if (same_src(ct, tuple)) {
			/* Copy source part from reply tuple. */
			invert_tuplepr(result,
				       &ct->tuplehash[IP_CT_DIR_REPLY].tuple);
			result->dst = tuple->dst;

			if (in_range(result, range))
				return 1;
		}
	}
	return 0;
}

/* For [FUTURE] fragmentation handling, we want the least-used
   src-ip/dst-ip/proto triple.  Fairness doesn't come into it.  Thus
   if the range specifies 1.2.3.4 ports 10000-10005 and 1.2.3.5 ports
   1-65535, we don't do pro-rata allocation based on ports; we choose
   the ip with the lowest src-ip/dst-ip/proto usage.
*/
static void
find_best_ips_proto(struct ip_conntrack_tuple *tuple,
		    const struct ip_nat_range *range,
		    const struct ip_conntrack *conntrack,
		    unsigned int hooknum)
{
	u_int32_t *var_ipp;
	/* Host order */
	u_int32_t minip, maxip, j;

	/* No IP mapping?  Do nothing. */
	if (!(range->flags & IP_NAT_RANGE_MAP_IPS))
		return;

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC)
		var_ipp = &tuple->src.ip;
	else
		var_ipp = &tuple->dst.ip;

	/* Fast path: only one choice. */
	if (range->min_ip == range->max_ip) {
		*var_ipp = range->min_ip;
		return;
	}

	/* Hashing source and destination IPs gives a fairly even
	 * spread in practice (if there are a small number of IPs
	 * involved, there usually aren't that many connections
	 * anyway).  The consistency means that servers see the same
	 * client coming from the same IP (some Internet Backing sites
	 * like this), even across reboots. */
	minip = ntohl(range->min_ip);
	maxip = ntohl(range->max_ip);
	j = jhash_2words(tuple->src.ip, tuple->dst.ip, 0);
	*var_ipp = htonl(minip + j % (maxip - minip + 1));
}

/* Manipulate the tuple into the range given.  For NF_IP_POST_ROUTING,
 * we change the source to map into the range.  For NF_IP_PRE_ROUTING
 * and NF_IP_LOCAL_OUT, we change the destination to map into the
 * range.  It might not be possible to get a unique tuple, but we try.
 * At worst (or if we race), we will end up with a final duplicate in
 * __ip_conntrack_confirm and drop the packet. */
static void
get_unique_tuple(struct ip_conntrack_tuple *tuple,
		 const struct ip_conntrack_tuple *orig_tuple,
		 const struct ip_nat_range *range,
		 struct ip_conntrack *conntrack,
		 unsigned int hooknum)
{
	struct ip_nat_protocol *proto
		= ip_nat_find_proto(orig_tuple->dst.protonum);

	/* 1) If this srcip/proto/src-proto-part is currently mapped,
	   and that same mapping gives a unique tuple within the given
	   range, use that.

	   This is only required for source (ie. NAT/masq) mappings.
	   So far, we don't do local source mappings, so multiple
	   manips not an issue.  */
	if (hooknum == NF_IP_POST_ROUTING) {
		if (find_appropriate_src(orig_tuple, tuple, range)) {
			DEBUGP("get_unique_tuple: Found current src map\n");
			if (!ip_nat_used_tuple(tuple, conntrack))
				return;
		}
	}

	/* 2) Select the least-used IP/proto combination in the given
	   range. */
	*tuple = *orig_tuple;
	find_best_ips_proto(tuple, range, conntrack, hooknum);

	if (hooknum == NF_IP_LOCAL_OUT && tuple->dst.ip != orig_tuple->dst.ip)
		warn_if_extra_mangle(tuple->src.ip, tuple->dst.ip);

	/* 3) The per-protocol part of the manip is made to map into
	   the range to make a unique tuple. */

	/* Only bother mapping if it's not already in range and unique */
	if ((!(range->flags & IP_NAT_RANGE_PROTO_SPECIFIED)
	     || proto->in_range(tuple, HOOK2MANIP(hooknum),
				&range->min, &range->max))
	    && !ip_nat_used_tuple(tuple, conntrack))
		return;

	/* Last change: get protocol to try to obtain unique tuple. */
	proto->unique_tuple(tuple, range, HOOK2MANIP(hooknum), conntrack);
}

/* Where to manip the reply packets (will be reverse manip). */
static unsigned int opposite_hook[NF_IP_NUMHOOKS]
= { [NF_IP_PRE_ROUTING] = NF_IP_POST_ROUTING,
    [NF_IP_POST_ROUTING] = NF_IP_PRE_ROUTING,
    [NF_IP_LOCAL_OUT] = NF_IP_LOCAL_IN,
    [NF_IP_LOCAL_IN] = NF_IP_LOCAL_OUT,
};

unsigned int
ip_nat_setup_info(struct ip_conntrack *conntrack,
		  const struct ip_nat_range *range,
		  unsigned int hooknum)
{
	struct ip_conntrack_tuple new_tuple, inv_tuple, reply;
	struct ip_conntrack_tuple orig_tp;
	struct ip_nat_info *info = &conntrack->nat.info;
	int in_hashes = info->initialized;

	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	IP_NF_ASSERT(hooknum == NF_IP_PRE_ROUTING
		     || hooknum == NF_IP_POST_ROUTING
		     || hooknum == NF_IP_LOCAL_IN
		     || hooknum == NF_IP_LOCAL_OUT);
	IP_NF_ASSERT(info->num_manips < IP_NAT_MAX_MANIPS);
	IP_NF_ASSERT(!(info->initialized & (1 << HOOK2MANIP(hooknum))));

	/* What we've got will look like inverse of reply. Normally
	   this is what is in the conntrack, except for prior
	   manipulations (future optimization: if num_manips == 0,
	   orig_tp =
	   conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple) */
	invert_tuplepr(&orig_tp,
		       &conntrack->tuplehash[IP_CT_DIR_REPLY].tuple);

#if 0
	{
	unsigned int i;

	DEBUGP("Hook %u (%s), ", hooknum,
	       HOOK2MANIP(hooknum)==IP_NAT_MANIP_SRC ? "SRC" : "DST");
	DUMP_TUPLE(&orig_tp);
	DEBUGP("Range %p: ", mr);
	for (i = 0; i < mr->rangesize; i++) {
		DEBUGP("%u:%s%s%s %u.%u.%u.%u - %u.%u.%u.%u %u - %u\n",
		       i,
		       (mr->range[i].flags & IP_NAT_RANGE_MAP_IPS)
		       ? " MAP_IPS" : "",
		       (mr->range[i].flags
			& IP_NAT_RANGE_PROTO_SPECIFIED)
		       ? " PROTO_SPECIFIED" : "",
		       (mr->range[i].flags & IP_NAT_RANGE_FULL)
		       ? " FULL" : "",
		       NIPQUAD(mr->range[i].min_ip),
		       NIPQUAD(mr->range[i].max_ip),
		       mr->range[i].min.all,
		       mr->range[i].max.all);
	}
	}
#endif

	get_unique_tuple(&new_tuple, &orig_tp, range, conntrack, hooknum);

	/* We now have two tuples (SRCIP/SRCPT/DSTIP/DSTPT):
	   the original (A/B/C/D') and the mangled one (E/F/G/H').

	   We're only allowed to work with the SRC per-proto
	   part, so we create inverses of both to start, then
	   derive the other fields we need.  */

	/* Reply connection: simply invert the new tuple
	   (G/H/E/F') */
	invert_tuplepr(&reply, &new_tuple);

	/* Alter conntrack table so will recognize replies. */
	ip_conntrack_alter_reply(conntrack, &reply);

	/* FIXME: We can simply used existing conntrack reply tuple
           here --RR */
	/* Create inverse of original: C/D/A/B' */
	invert_tuplepr(&inv_tuple, &orig_tp);

	/* Has source changed?. */
	if (!ip_ct_tuple_src_equal(&new_tuple, &orig_tp)) {
		IP_NF_ASSERT(HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC);
		IP_NF_ASSERT(ip_ct_tuple_dst_equal(&new_tuple, &orig_tp));

		/* In this direction, a source manip. */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_ORIGINAL, hooknum,
			   IP_NAT_MANIP_SRC, new_tuple.src });

		IP_NF_ASSERT(info->num_manips < IP_NAT_MAX_MANIPS);

		/* In the reverse direction, a destination manip. */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_REPLY, opposite_hook[hooknum],
			   IP_NAT_MANIP_DST, orig_tp.src });
		IP_NF_ASSERT(info->num_manips <= IP_NAT_MAX_MANIPS);
	}

	/* Has destination changed? */
	if (!ip_ct_tuple_dst_equal(&new_tuple, &orig_tp)) {
		IP_NF_ASSERT(HOOK2MANIP(hooknum) == IP_NAT_MANIP_DST);

		/* In this direction, a destination manip */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_ORIGINAL, hooknum,
			   IP_NAT_MANIP_DST, reply.src });

		IP_NF_ASSERT(info->num_manips < IP_NAT_MAX_MANIPS);

		/* In the reverse direction, a source manip. */
		info->manips[info->num_manips++] =
			((struct ip_nat_info_manip)
			 { IP_CT_DIR_REPLY, opposite_hook[hooknum],
			   IP_NAT_MANIP_SRC, inv_tuple.src });
		IP_NF_ASSERT(info->num_manips <= IP_NAT_MAX_MANIPS);
	}

	/* If there's a helper, assign it; based on new tuple. */
	if (!conntrack->master)
		info->helper = __ip_nat_find_helper(&reply);

	/* It's done. */
	info->initialized |= (1 << HOOK2MANIP(hooknum));

	if (in_hashes)
		replace_in_hashes(conntrack, info);
	else
		place_in_hashes(conntrack, info);

	return NF_ACCEPT;
}

void replace_in_hashes(struct ip_conntrack *conntrack,
		       struct ip_nat_info *info)
{
	/* Source has changed, so replace in hashes. */
	unsigned int srchash
		= hash_by_src(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.src,
			      conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.dst.protonum);
	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	list_move(&info->bysource, &bysource[srchash]);
}

void place_in_hashes(struct ip_conntrack *conntrack,
		     struct ip_nat_info *info)
{
	unsigned int srchash
		= hash_by_src(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.src,
			      conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.dst.protonum);
	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	list_add(&info->bysource, &bysource[srchash]);
}

/* Returns true if succeeded. */
static int
manip_pkt(u_int16_t proto,
	  struct sk_buff **pskb,
	  unsigned int iphdroff,
	  const struct ip_conntrack_manip *manip,
	  enum ip_nat_manip_type maniptype)
{
	struct iphdr *iph;

	(*pskb)->nfcache |= NFC_ALTERED;
	if (!skb_ip_make_writable(pskb, iphdroff+sizeof(*iph)))
		return 0;

	iph = (void *)(*pskb)->data + iphdroff;

	/* Manipulate protcol part. */
	if (!ip_nat_find_proto(proto)->manip_pkt(pskb, iphdroff,
	                                         manip, maniptype))
		return 0;

	iph = (void *)(*pskb)->data + iphdroff;

	if (maniptype == IP_NAT_MANIP_SRC) {
		iph->check = ip_nat_cheat_check(~iph->saddr, manip->ip,
						iph->check);
		iph->saddr = manip->ip;
	} else {
		iph->check = ip_nat_cheat_check(~iph->daddr, manip->ip,
						iph->check);
		iph->daddr = manip->ip;
	}
	return 1;
}

/* Do packet manipulations according to binding. */
unsigned int
do_bindings(struct ip_conntrack *ct,
	    enum ip_conntrack_info ctinfo,
	    struct ip_nat_info *info,
	    unsigned int hooknum,
	    struct sk_buff **pskb)
{
	int i, ret = NF_ACCEPT;
	enum ip_conntrack_dir dir = CTINFO2DIR(ctinfo);
	int proto = (*pskb)->nh.iph->protocol;

	/* Need nat lock to protect against modification, but neither
	   conntrack (referenced) and helper (deleted with
	   synchronize_bh()) can vanish. */
	READ_LOCK(&ip_nat_lock);
	for (i = 0; i < info->num_manips; i++) {
		if (info->manips[i].direction == dir
		    && info->manips[i].hooknum == hooknum) {
			DEBUGP("Mangling %p: %s to %u.%u.%u.%u %u\n",
			       *pskb,
			       info->manips[i].maniptype == IP_NAT_MANIP_SRC
			       ? "SRC" : "DST",
			       NIPQUAD(info->manips[i].manip.ip),
			       htons(info->manips[i].manip.u.all));
			if (!manip_pkt(proto, pskb, 0,
				       &info->manips[i].manip,
				       info->manips[i].maniptype)) {
				READ_UNLOCK(&ip_nat_lock);
				return NF_DROP;
			}
		}
	}

	if (info->helper) {
		DEBUGP("do_bindings: helper existing for (%p)\n", ct);

		/* Always defragged for helpers */
		IP_NF_ASSERT(!((*pskb)->nh.iph->frag_off
			       & htons(IP_MF|IP_OFFSET)));

		ret = info->helper->help(ct, NULL, info, ctinfo, hooknum,pskb);
	}
	READ_UNLOCK(&ip_nat_lock);

	/* FIXME: NAT/conntrack helpers should set ctinfo &
	 * CT_INFO_RESYNC on packets, so we don't have to adjust all
	 * connections with conntrack helpers --RR */
	if (ct->helper
	    && proto == IPPROTO_TCP
	    && (hooknum == NF_IP_POST_ROUTING || hooknum == NF_IP_LOCAL_IN)) {
		DEBUGP("ip_nat_core: adjusting sequence number\n");
		/* future: put this in a l4-proto specific function,
		 * and call this function here. */
		if (!ip_nat_seq_adjust(pskb, ct, ctinfo))
			ret = NF_DROP;
	}

	return ret;
}

static inline int tuple_src_equal_dst(const struct ip_conntrack_tuple *t1,
                                      const struct ip_conntrack_tuple *t2)
{
	if (t1->dst.protonum != t2->dst.protonum || t1->src.ip != t2->dst.ip)
		return 0;
	if (t1->dst.protonum != IPPROTO_ICMP)
		return t1->src.u.all == t2->dst.u.all;
	else {
		struct ip_conntrack_tuple inv;

		/* ICMP tuples are asymetric */
		invert_tuplepr(&inv, t1);
		return inv.src.u.all == t2->src.u.all &&
		       inv.dst.u.all == t2->dst.u.all;
	}
}

int
icmp_reply_translation(struct sk_buff **pskb,
		       struct ip_conntrack *conntrack,
		       unsigned int hooknum,
		       int dir)
{
	struct {
		struct icmphdr icmp;
		struct iphdr ip;
	} *inside;
	unsigned int i;
	struct ip_nat_info *info = &conntrack->nat.info;
	struct ip_conntrack_tuple *cttuple, innertuple;
	int hdrlen;

	if (!skb_ip_make_writable(pskb,(*pskb)->nh.iph->ihl*4+sizeof(*inside)))
		return 0;
	inside = (void *)(*pskb)->data + (*pskb)->nh.iph->ihl*4;

	/* We're actually going to mangle it beyond trivial checksum
	   adjustment, so make sure the current checksum is correct. */
	if ((*pskb)->ip_summed != CHECKSUM_UNNECESSARY) {
		hdrlen = (*pskb)->nh.iph->ihl * 4;
		if ((u16)csum_fold(skb_checksum(*pskb, hdrlen,
						(*pskb)->len - hdrlen, 0)))
			return 0;
	}

	/* Must be RELATED */
	IP_NF_ASSERT((*pskb)->nfctinfo == IP_CT_RELATED ||
		     (*pskb)->nfctinfo == IP_CT_RELATED+IP_CT_IS_REPLY);

	/* Redirects on non-null nats must be dropped, else they'll
           start talking to each other without our translation, and be
           confused... --RR */
	if (inside->icmp.type == ICMP_REDIRECT) {
		/* Don't care about races here. */
		if (info->initialized
		    != ((1 << IP_NAT_MANIP_SRC) | (1 << IP_NAT_MANIP_DST))
		    || info->num_manips != 0)
			return 0;
	}

	DEBUGP("icmp_reply_translation: translating error %p hook %u dir %s\n",
	       *pskb, hooknum, dir == IP_CT_DIR_ORIGINAL ? "ORIG" : "REPLY");
	/* Note: May not be from a NAT'd host, but probably safest to
	   do translation always as if it came from the host itself
	   (even though a "host unreachable" coming from the host
	   itself is a bit weird).

	   More explanation: some people use NAT for anonymizing.
	   Also, CERT recommends dropping all packets from private IP
	   addresses (although ICMP errors from internal links with
	   such addresses are not too uncommon, as Alan Cox points
	   out) */

	if (!ip_ct_get_tuple(&inside->ip, *pskb, (*pskb)->nh.iph->ihl*4 +
	                     sizeof(struct icmphdr) + inside->ip.ihl*4,
	                     &innertuple,
	                     ip_ct_find_proto(inside->ip.protocol)))
		return 0;
	cttuple = &conntrack->tuplehash[dir].tuple;

	READ_LOCK(&ip_nat_lock);
	for (i = 0; i < info->num_manips; i++) {
		DEBUGP("icmp_reply: manip %u dir %s hook %u\n",
		       i, info->manips[i].direction == IP_CT_DIR_ORIGINAL ?
		       "ORIG" : "REPLY", info->manips[i].hooknum);

		if (info->manips[i].direction != dir)
			continue;

		/* Mapping the inner packet is just like a normal packet, except
		 * it was never src/dst reversed, so where we would normally
		 * apply a dst manip, we apply a src, and vice versa. */

		/* Only true for forwarded packets, locally generated packets
		 * never hit PRE_ROUTING, we need to apply their PRE_ROUTING
		 * manips in LOCAL_OUT. */
		if (hooknum == NF_IP_LOCAL_OUT &&
		    info->manips[i].hooknum == NF_IP_PRE_ROUTING)
			hooknum = info->manips[i].hooknum;

		if (info->manips[i].hooknum != hooknum)
			continue;

		/* ICMP errors may be generated locally for packets that
		 * don't have all NAT manips applied yet. Verify manips
		 * have been applied before reversing them */
		if (info->manips[i].maniptype == IP_NAT_MANIP_SRC) {
			if (!tuple_src_equal_dst(cttuple, &innertuple))
				continue;
		} else {
			if (!tuple_src_equal_dst(&innertuple, cttuple))
				continue;
		}

		DEBUGP("icmp_reply: inner %s -> %u.%u.%u.%u %u\n",
		       info->manips[i].maniptype == IP_NAT_MANIP_SRC
		       ? "DST" : "SRC", NIPQUAD(info->manips[i].manip.ip),
		       ntohs(info->manips[i].manip.u.udp.port));
		if (!manip_pkt(inside->ip.protocol, pskb,
			       (*pskb)->nh.iph->ihl*4 + sizeof(inside->icmp),
			       &info->manips[i].manip,
			       !info->manips[i].maniptype))
			goto unlock_fail;

		/* Outer packet needs to have IP header NATed like
                   it's a reply. */

		/* Use mapping to map outer packet: 0 give no
                          per-proto mapping */
		DEBUGP("icmp_reply: outer %s -> %u.%u.%u.%u\n",
		       info->manips[i].maniptype == IP_NAT_MANIP_SRC
		       ? "SRC" : "DST", NIPQUAD(info->manips[i].manip.ip));
		if (!manip_pkt(0, pskb, 0, &info->manips[i].manip,
			       info->manips[i].maniptype))
			goto unlock_fail;
	}
	READ_UNLOCK(&ip_nat_lock);

	hdrlen = (*pskb)->nh.iph->ihl * 4;

	inside = (void *)(*pskb)->data + (*pskb)->nh.iph->ihl*4;

	inside->icmp.checksum = 0;
	inside->icmp.checksum = csum_fold(skb_checksum(*pskb, hdrlen,
						       (*pskb)->len - hdrlen,
						       0));
	return 1;

 unlock_fail:
	READ_UNLOCK(&ip_nat_lock);
	return 0;
}

int __init ip_nat_init(void)
{
	size_t i;

	/* Leave them the same for the moment. */
	ip_nat_htable_size = ip_conntrack_htable_size;

	/* One vmalloc for both hash tables */
	bysource = vmalloc(sizeof(struct list_head) * ip_nat_htable_size);
	if (!bysource)
		return -ENOMEM;

	/* Sew in builtin protocols. */
	WRITE_LOCK(&ip_nat_lock);
	for (i = 0; i < MAX_IP_NAT_PROTO; i++)
		ip_nat_protos[i] = &ip_nat_unknown_protocol;
	ip_nat_protos[IPPROTO_TCP] = &ip_nat_protocol_tcp;
	ip_nat_protos[IPPROTO_UDP] = &ip_nat_protocol_udp;
	ip_nat_protos[IPPROTO_ICMP] = &ip_nat_protocol_icmp;
	WRITE_UNLOCK(&ip_nat_lock);

	for (i = 0; i < ip_nat_htable_size; i++) {
		INIT_LIST_HEAD(&bysource[i]);
	}

	/* FIXME: Man, this is a hack.  <SIGH> */
	IP_NF_ASSERT(ip_conntrack_destroyed == NULL);
	ip_conntrack_destroyed = &ip_nat_cleanup_conntrack;
	
	/* Initialize fake conntrack so that NAT will skip it */
	ip_conntrack_untracked.nat.info.initialized |= 
		(1 << IP_NAT_MANIP_SRC) | (1 << IP_NAT_MANIP_DST);

	return 0;
}

/* Clear NAT section of all conntracks, in case we're loaded again. */
static int clean_nat(struct ip_conntrack *i, void *data)
{
	memset(&i->nat, 0, sizeof(i->nat));
	return 0;
}

/* Not __exit: called from ip_nat_standalone.c:init_or_cleanup() --RR */
void ip_nat_cleanup(void)
{
	ip_ct_iterate_cleanup(&clean_nat, NULL);
	ip_conntrack_destroyed = NULL;
	vfree(bysource);
}
