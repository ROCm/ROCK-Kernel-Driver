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
static struct list_head *byipsproto;
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
	list_del(&info->byipsproto);
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

/* If we source map this tuple so reply looks like reply_tuple, will
 * that meet the constraints of mr. */
static int
in_range(const struct ip_conntrack_tuple *tuple,
	 const struct ip_nat_multi_range *mr)
{
	struct ip_nat_protocol *proto = ip_nat_find_proto(tuple->dst.protonum);
	unsigned int i;

	for (i = 0; i < mr->rangesize; i++) {
		/* If we are supposed to map IPs, then we must be in the
		   range specified. */
		if (mr->range[i].flags & IP_NAT_RANGE_MAP_IPS) {
			if (ntohl(tuple->src.ip) < ntohl(mr->range[i].min_ip)
			    || (ntohl(tuple->src.ip)
				> ntohl(mr->range[i].max_ip)))
				continue;
		}

		if (!(mr->range[i].flags & IP_NAT_RANGE_PROTO_SPECIFIED)
		    || proto->in_range(tuple, IP_NAT_MANIP_SRC,
				       &mr->range[i].min, &mr->range[i].max))
			return 1;
	}
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
		     const struct ip_nat_multi_range *mr)
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

			if (in_range(result, mr))
				return 1;
		}
	}
	return 0;
}

#ifdef CONFIG_IP_NF_NAT_LOCAL
/* If it's really a local destination manip, it may need to do a
   source manip too. */
static int
do_extra_mangle(u_int32_t var_ip, u_int32_t *other_ipp)
{
	struct flowi fl = { .nl_u = { .ip4_u = { .daddr = var_ip } } };
	struct rtable *rt;

	/* FIXME: IPTOS_TOS(iph->tos) --RR */
	if (ip_route_output_key(&rt, &fl) != 0) {
		DEBUGP("do_extra_mangle: Can't get route to %u.%u.%u.%u\n",
		       NIPQUAD(var_ip));
		return 0;
	}

	*other_ipp = rt->rt_src;
	ip_rt_put(rt);
	return 1;
}
#endif

/* Simple way to iterate through all. */
static inline int fake_cmp(const struct ip_conntrack *ct,
			   u_int32_t src, u_int32_t dst, u_int16_t protonum,
			   unsigned int *score, const struct ip_conntrack *ct2)
{
	/* Compare backwards: we're dealing with OUTGOING tuples, and
           inside the conntrack is the REPLY tuple.  Don't count this
           conntrack. */
	if (ct != ct2
	    && ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.ip == dst
	    && ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.ip == src
	    && (ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.protonum == protonum))
		(*score)++;
	return 0;
}

static inline unsigned int
count_maps(u_int32_t src, u_int32_t dst, u_int16_t protonum,
	   const struct ip_conntrack *conntrack)
{
	struct ip_conntrack *ct;
	unsigned int score = 0;
	unsigned int h;

	MUST_BE_READ_LOCKED(&ip_nat_lock);
	h = hash_by_ipsproto(src, dst, protonum);
	list_for_each_entry(ct, &byipsproto[h], nat.info.byipsproto)
		fake_cmp(ct, src, dst, protonum, &score, conntrack);

	return score;
}

/* For [FUTURE] fragmentation handling, we want the least-used
   src-ip/dst-ip/proto triple.  Fairness doesn't come into it.  Thus
   if the range specifies 1.2.3.4 ports 10000-10005 and 1.2.3.5 ports
   1-65535, we don't do pro-rata allocation based on ports; we choose
   the ip with the lowest src-ip/dst-ip/proto usage.

   If an allocation then fails (eg. all 6 ports used in the 1.2.3.4
   range), we eliminate that and try again.  This is not the most
   efficient approach, but if you're worried about that, don't hand us
   ranges you don't really have.  */
static struct ip_nat_range *
find_best_ips_proto(struct ip_conntrack_tuple *tuple,
		    const struct ip_nat_multi_range *mr,
		    const struct ip_conntrack *conntrack,
		    unsigned int hooknum)
{
	unsigned int i;
	struct {
		const struct ip_nat_range *range;
		unsigned int score;
		struct ip_conntrack_tuple tuple;
	} best = { NULL,  0xFFFFFFFF };
	u_int32_t *var_ipp, *other_ipp, saved_ip, orig_dstip;
	static unsigned int randomness;

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC) {
		var_ipp = &tuple->src.ip;
		saved_ip = tuple->dst.ip;
		other_ipp = &tuple->dst.ip;
	} else {
		var_ipp = &tuple->dst.ip;
		saved_ip = tuple->src.ip;
		other_ipp = &tuple->src.ip;
	}
	/* Don't do do_extra_mangle unless necessary (overrides
           explicit socket bindings, for example) */
	orig_dstip = tuple->dst.ip;

	IP_NF_ASSERT(mr->rangesize >= 1);
	for (i = 0; i < mr->rangesize; i++) {
		/* Host order */
		u_int32_t minip, maxip, j;

		/* Don't do ranges which are already eliminated. */
		if (mr->range[i].flags & IP_NAT_RANGE_FULL) {
			continue;
		}

		if (mr->range[i].flags & IP_NAT_RANGE_MAP_IPS) {
			minip = ntohl(mr->range[i].min_ip);
			maxip = ntohl(mr->range[i].max_ip);
		} else
			minip = maxip = ntohl(*var_ipp);

		randomness++;
		for (j = 0; j < maxip - minip + 1; j++) {
			unsigned int score;

			*var_ipp = htonl(minip + (randomness + j) 
					 % (maxip - minip + 1));

			/* Reset the other ip in case it was mangled by
			 * do_extra_mangle last time. */
			*other_ipp = saved_ip;

#ifdef CONFIG_IP_NF_NAT_LOCAL
			if (hooknum == NF_IP_LOCAL_OUT
			    && *var_ipp != orig_dstip
			    && !do_extra_mangle(*var_ipp, other_ipp)) {
				DEBUGP("Range %u %u.%u.%u.%u rt failed!\n",
				       i, NIPQUAD(*var_ipp));
				/* Can't route?  This whole range part is
				 * probably screwed, but keep trying
				 * anyway. */
				continue;
			}
#endif

			/* Count how many others map onto this. */
			score = count_maps(tuple->src.ip, tuple->dst.ip,
					   tuple->dst.protonum, conntrack);
			if (score < best.score) {
				/* Optimization: doesn't get any better than
				   this. */
				if (score == 0)
					return (struct ip_nat_range *)
						&mr->range[i];

				best.score = score;
				best.tuple = *tuple;
				best.range = &mr->range[i];
			}
		}
	}
	*tuple = best.tuple;

	/* Discard const. */
	return (struct ip_nat_range *)best.range;
}

/* Fast version doesn't iterate through hash chains, but only handles
   common case of single IP address (null NAT, masquerade) */
static struct ip_nat_range *
find_best_ips_proto_fast(struct ip_conntrack_tuple *tuple,
			 const struct ip_nat_multi_range *mr,
			 const struct ip_conntrack *conntrack,
			 unsigned int hooknum)
{
	if (mr->rangesize != 1
	    || (mr->range[0].flags & IP_NAT_RANGE_FULL)
	    || ((mr->range[0].flags & IP_NAT_RANGE_MAP_IPS)
		&& mr->range[0].min_ip != mr->range[0].max_ip))
		return find_best_ips_proto(tuple, mr, conntrack, hooknum);

	if (mr->range[0].flags & IP_NAT_RANGE_MAP_IPS) {
		if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC)
			tuple->src.ip = mr->range[0].min_ip;
		else {
			/* Only do extra mangle when required (breaks
                           socket binding) */
#ifdef CONFIG_IP_NF_NAT_LOCAL
			if (tuple->dst.ip != mr->range[0].min_ip
			    && hooknum == NF_IP_LOCAL_OUT
			    && !do_extra_mangle(mr->range[0].min_ip,
						&tuple->src.ip))
				return NULL;
#endif
			tuple->dst.ip = mr->range[0].min_ip;
		}
	}

	/* Discard const. */
	return (struct ip_nat_range *)&mr->range[0];
}

static int
get_unique_tuple(struct ip_conntrack_tuple *tuple,
		 const struct ip_conntrack_tuple *orig_tuple,
		 const struct ip_nat_multi_range *mrr,
		 struct ip_conntrack *conntrack,
		 unsigned int hooknum)
{
	struct ip_nat_protocol *proto
		= ip_nat_find_proto(orig_tuple->dst.protonum);
	struct ip_nat_range *rptr;
	unsigned int i;
	int ret;

	/* We temporarily use flags for marking full parts, but we
	   always clean up afterwards */
	struct ip_nat_multi_range *mr = (void *)mrr;

	/* 1) If this srcip/proto/src-proto-part is currently mapped,
	   and that same mapping gives a unique tuple within the given
	   range, use that.

	   This is only required for source (ie. NAT/masq) mappings.
	   So far, we don't do local source mappings, so multiple
	   manips not an issue.  */
	if (hooknum == NF_IP_POST_ROUTING) {
		if (find_appropriate_src(orig_tuple, tuple, mr)) {
			DEBUGP("get_unique_tuple: Found current src map\n");
			if (!ip_nat_used_tuple(tuple, conntrack))
				return 1;
		}
	}

	/* 2) Select the least-used IP/proto combination in the given
	   range.
	*/
	*tuple = *orig_tuple;
	while ((rptr = find_best_ips_proto_fast(tuple, mr, conntrack, hooknum))
	       != NULL) {
		DEBUGP("Found best for "); DUMP_TUPLE(tuple);
		/* 3) The per-protocol part of the manip is made to
		   map into the range to make a unique tuple. */

		/* Only bother mapping if it's not already in range
		   and unique */
		if ((!(rptr->flags & IP_NAT_RANGE_PROTO_SPECIFIED)
		     || proto->in_range(tuple, HOOK2MANIP(hooknum),
					&rptr->min, &rptr->max))
		    && !ip_nat_used_tuple(tuple, conntrack)) {
			ret = 1;
			goto clear_fulls;
		} else {
			if (proto->unique_tuple(tuple, rptr,
						HOOK2MANIP(hooknum),
						conntrack)) {
				/* Must be unique. */
				IP_NF_ASSERT(!ip_nat_used_tuple(tuple,
								conntrack));
				ret = 1;
				goto clear_fulls;
			} else if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_DST) {
				/* Try implicit source NAT; protocol
                                   may be able to play with ports to
                                   make it unique. */
				struct ip_nat_range r
					= { IP_NAT_RANGE_MAP_IPS, 
					    tuple->src.ip, tuple->src.ip,
					    { 0 }, { 0 } };
				DEBUGP("Trying implicit mapping\n");
				if (proto->unique_tuple(tuple, &r,
							IP_NAT_MANIP_SRC,
							conntrack)) {
					/* Must be unique. */
					IP_NF_ASSERT(!ip_nat_used_tuple
						     (tuple, conntrack));
					ret = 1;
					goto clear_fulls;
				}
			}
			DEBUGP("Protocol can't get unique tuple %u.\n",
			       hooknum);
		}

		/* Eliminate that from range, and try again. */
		rptr->flags |= IP_NAT_RANGE_FULL;
		*tuple = *orig_tuple;
	}

	ret = 0;

 clear_fulls:
	/* Clear full flags. */
	IP_NF_ASSERT(mr->rangesize >= 1);
	for (i = 0; i < mr->rangesize; i++)
		mr->range[i].flags &= ~IP_NAT_RANGE_FULL;

	return ret;
}

/* Where to manip the reply packets (will be reverse manip). */
static unsigned int opposite_hook[NF_IP_NUMHOOKS]
= { [NF_IP_PRE_ROUTING] = NF_IP_POST_ROUTING,
    [NF_IP_POST_ROUTING] = NF_IP_PRE_ROUTING,
#ifdef CONFIG_IP_NF_NAT_LOCAL
    [NF_IP_LOCAL_OUT] = NF_IP_LOCAL_IN,
    [NF_IP_LOCAL_IN] = NF_IP_LOCAL_OUT,
#endif
};

unsigned int
ip_nat_setup_info(struct ip_conntrack *conntrack,
		  const struct ip_nat_multi_range *mr,
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

	do {
		if (!get_unique_tuple(&new_tuple, &orig_tp, mr, conntrack,
				      hooknum)) {
			DEBUGP("ip_nat_setup_info: Can't get unique for %p.\n",
			       conntrack);
			return NF_DROP;
		}

#if 0
		DEBUGP("Hook %u (%s) %p\n", hooknum,
		       HOOK2MANIP(hooknum)==IP_NAT_MANIP_SRC ? "SRC" : "DST",
		       conntrack);
		DEBUGP("Original: ");
		DUMP_TUPLE(&orig_tp);
		DEBUGP("New: ");
		DUMP_TUPLE(&new_tuple);
#endif

		/* We now have two tuples (SRCIP/SRCPT/DSTIP/DSTPT):
		   the original (A/B/C/D') and the mangled one (E/F/G/H').

		   We're only allowed to work with the SRC per-proto
		   part, so we create inverses of both to start, then
		   derive the other fields we need.  */

		/* Reply connection: simply invert the new tuple
                   (G/H/E/F') */
		invert_tuplepr(&reply, &new_tuple);

		/* Alter conntrack table so it recognizes replies.
                   If fail this race (reply tuple now used), repeat. */
	} while (!ip_conntrack_alter_reply(conntrack, &reply));

	/* FIXME: We can simply used existing conntrack reply tuple
           here --RR */
	/* Create inverse of original: C/D/A/B' */
	invert_tuplepr(&inv_tuple, &orig_tp);

	/* Has source changed?. */
	if (!ip_ct_tuple_src_equal(&new_tuple, &orig_tp)) {
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
	/* We place packet as seen OUTGOUNG in byips_proto hash
           (ie. reverse dst and src of reply packet. */
	unsigned int ipsprotohash
		= hash_by_ipsproto(conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.src.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.protonum);

	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	list_move(&info->bysource, &bysource[srchash]);
	list_move(&info->byipsproto, &byipsproto[ipsprotohash]);
}

void place_in_hashes(struct ip_conntrack *conntrack,
		     struct ip_nat_info *info)
{
	unsigned int srchash
		= hash_by_src(&conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.src,
			      conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			      .tuple.dst.protonum);
	/* We place packet as seen OUTGOUNG in byips_proto hash
           (ie. reverse dst and src of reply packet. */
	unsigned int ipsprotohash
		= hash_by_ipsproto(conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.src.ip,
				   conntrack->tuplehash[IP_CT_DIR_REPLY]
				   .tuple.dst.protonum);

	MUST_BE_WRITE_LOCKED(&ip_nat_lock);
	list_add(&info->bysource, &bysource[srchash]);
	list_add(&info->byipsproto, &byipsproto[ipsprotohash]);
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
	if (!skb_ip_make_writable(pskb, iphdroff+sizeof(iph)))
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

static inline int exp_for_packet(struct ip_conntrack_expect *exp,
			         struct sk_buff *skb)
{
	struct ip_conntrack_protocol *proto;
	int ret = 1;

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	proto = ip_ct_find_proto(skb->nh.iph->protocol);
	if (proto->exp_matches_pkt)
		ret = proto->exp_matches_pkt(exp, skb);

	return ret;
}

/* Do packet manipulations according to binding. */
unsigned int
do_bindings(struct ip_conntrack *ct,
	    enum ip_conntrack_info ctinfo,
	    struct ip_nat_info *info,
	    unsigned int hooknum,
	    struct sk_buff **pskb)
{
	unsigned int i;
	struct ip_nat_helper *helper;
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
	helper = info->helper;
	READ_UNLOCK(&ip_nat_lock);

	if (helper) {
		struct ip_conntrack_expect *exp = NULL;
		struct list_head *cur_item;
		int ret = NF_ACCEPT;
		int helper_called = 0;

		DEBUGP("do_bindings: helper existing for (%p)\n", ct);

		/* Always defragged for helpers */
		IP_NF_ASSERT(!((*pskb)->nh.iph->frag_off
			       & htons(IP_MF|IP_OFFSET)));

		/* Have to grab read lock before sibling_list traversal */
		READ_LOCK(&ip_conntrack_lock);
		list_for_each_prev(cur_item, &ct->sibling_list) { 
			exp = list_entry(cur_item, struct ip_conntrack_expect, 
					 expected_list);
					 
			/* if this expectation is already established, skip */
			if (exp->sibling)
				continue;

			if (exp_for_packet(exp, *pskb)) {
				/* FIXME: May be true multiple times in the
				 * case of UDP!! */
				DEBUGP("calling nat helper (exp=%p) for packet\n", exp);
				ret = helper->help(ct, exp, info, ctinfo, 
						   hooknum, pskb);
				if (ret != NF_ACCEPT) {
					READ_UNLOCK(&ip_conntrack_lock);
					return ret;
				}
				helper_called = 1;
			}
		}
		/* Helper might want to manip the packet even when there is no
		 * matching expectation for this packet */
		if (!helper_called && helper->flags & IP_NAT_HELPER_F_ALWAYS) {
			DEBUGP("calling nat helper for packet without expectation\n");
			ret = helper->help(ct, NULL, info, ctinfo, 
					   hooknum, pskb);
			if (ret != NF_ACCEPT) {
				READ_UNLOCK(&ip_conntrack_lock);
				return ret;
			}
		}
		READ_UNLOCK(&ip_conntrack_lock);
		
		/* Adjust sequence number only once per packet 
		 * (helper is called at all hooks) */
		if (proto == IPPROTO_TCP
		    && (hooknum == NF_IP_POST_ROUTING
			|| hooknum == NF_IP_LOCAL_IN)) {
			DEBUGP("ip_nat_core: adjusting sequence number\n");
			/* future: put this in a l4-proto specific function,
			 * and call this function here. */
			if (!ip_nat_seq_adjust(pskb, ct, ctinfo))
				ret = NF_DROP;
		}

		return ret;

	} else 
		return NF_ACCEPT;

	/* not reached */
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

	READ_LOCK(&ip_nat_lock);
	for (i = 0; i < info->num_manips; i++) {
		DEBUGP("icmp_reply: manip %u dir %s hook %u\n",
		       i, info->manips[i].direction == IP_CT_DIR_ORIGINAL ?
		       "ORIG" : "REPLY", info->manips[i].hooknum);

		if (info->manips[i].direction != dir)
			continue;

		/* Mapping the inner packet is just like a normal
		   packet, except it was never src/dst reversed, so
		   where we would normally apply a dst manip, we apply
		   a src, and vice versa. */
		if (info->manips[i].hooknum == hooknum) {
			DEBUGP("icmp_reply: inner %s -> %u.%u.%u.%u %u\n",
			       info->manips[i].maniptype == IP_NAT_MANIP_SRC
			       ? "DST" : "SRC",
			       NIPQUAD(info->manips[i].manip.ip),
			       ntohs(info->manips[i].manip.u.udp.port));
			if (!manip_pkt(inside->ip.protocol, pskb,
				       (*pskb)->nh.iph->ihl*4
				       + sizeof(inside->icmp),
				       &info->manips[i].manip,
				       !info->manips[i].maniptype))
				goto unlock_fail;

			/* Outer packet needs to have IP header NATed like
	                   it's a reply. */

			/* Use mapping to map outer packet: 0 give no
                           per-proto mapping */
			DEBUGP("icmp_reply: outer %s -> %u.%u.%u.%u\n",
			       info->manips[i].maniptype == IP_NAT_MANIP_SRC
			       ? "SRC" : "DST",
			       NIPQUAD(info->manips[i].manip.ip));
			if (!manip_pkt(0, pskb, 0,
				       &info->manips[i].manip,
				       info->manips[i].maniptype))
				goto unlock_fail;
		}
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
	bysource = vmalloc(sizeof(struct list_head) * ip_nat_htable_size*2);
	if (!bysource) {
		return -ENOMEM;
	}
	byipsproto = bysource + ip_nat_htable_size;

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
		INIT_LIST_HEAD(&byipsproto[i]);
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
static int clean_nat(const struct ip_conntrack *i, void *data)
{
	memset((void *)&i->nat, 0, sizeof(i->nat));
	return 0;
}

/* Not __exit: called from ip_nat_standalone.c:init_or_cleanup() --RR */
void ip_nat_cleanup(void)
{
	ip_ct_selective_cleanup(&clean_nat, NULL);
	ip_conntrack_destroyed = NULL;
	vfree(bysource);
}
