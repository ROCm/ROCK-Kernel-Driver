/*
 * IPv6 Connection Tracking
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv4/netfilter/ip_conntrack_core.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/* (c) 1999 Paul `Rusty' Russell.  Licenced under the GNU General
 * Public Licence.
 *
 * 23 Apr 2001: Harald Welte <laforge@gnumonks.org>
 *     - new API and handling of conntrack/nat helpers
 *     - now capable of multiple expectations for one master
 * 16 Jul 2002: Harald Welte <laforge@gnumonks.org>
 *     - add usage/reference counts to ip_conntrack_expect
 *     - export ip_conntrack[_expect]_{find_get,put} functions
 * */

#include <linux/version.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/icmpv6.h>
#include <linux/ipv6.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <net/checksum.h>
#include <linux/stddef.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/jhash.h>
#include <net/ipv6.h>
#include <net/ip6_checksum.h>
#include <linux/err.h>
#include <linux/kernel.h>

/* This rwlock protects the main hash table, protocol/helper/expected
   registrations, conntrack timers*/
#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip6_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip6_conntrack_lock)

#include <linux/netfilter_ipv6/ip6_conntrack.h>
#include <linux/netfilter_ipv6/ip6_conntrack_protocol.h>
#include <linux/netfilter_ipv6/ip6_conntrack_helper.h>
#include <linux/netfilter_ipv6/ip6_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#define IP6_CONNTRACK_VERSION	"0.1"

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_RWLOCK(ip6_conntrack_lock);
DECLARE_RWLOCK(ip6_conntrack_expect_tuple_lock);

void (*ip6_conntrack_destroyed)(struct ip6_conntrack *conntrack) = NULL;
LIST_HEAD(ip6_conntrack_expect_list);
LIST_HEAD(ip6_protocol_list);
static LIST_HEAD(helpers);
unsigned int ip6_conntrack_htable_size = 0;
static int ip6_conntrack_max = 0;
static atomic_t ip6_conntrack_count = ATOMIC_INIT(0);
struct list_head *ip6_conntrack_hash;
static kmem_cache_t *ip6_conntrack_cachep;

extern struct ip6_conntrack_protocol ip6_conntrack_generic_protocol;

/*
 * Based on ipv6_skip_exthdr() in net/ipv6/exthdr.c
 * 
 * This function parses (probably truncated) exthdr set "hdr"
 * of length "len". "nexthdrp" initially points to some place,
 * where type of the first header can be found.
 *
 * It skips all well-known exthdrs, and returns pointer to the start
 * of unparsable area i.e. the first header with unknown type.
 * if success, *nexthdr is updated by type/protocol of this header.
 *
 * NOTES: - it may return pointer pointing beyond end of packet,
 *	    if the last recognized header is truncated in the middle.
 *        - if packet is truncated, so that all parsed headers are skipped,
 *	    it returns -1.
 *	  - First fragment header is skipped, not-first ones
 *	    are considered as unparsable.
 *	  - ESP is unparsable for now and considered like
 *	    normal payload protocol.
 *	  - Note also special handling of AUTH header. Thanks to IPsec wizards.
 */

static int ip6_ct_skip_exthdr(struct sk_buff *skb, int start, u8 *nexthdrp,
			      int len)
{
	u8 nexthdr = *nexthdrp;

	while (ipv6_ext_hdr(nexthdr)) {
		struct ipv6_opt_hdr hdr;
		int hdrlen;

		if (len < (int)sizeof(struct ipv6_opt_hdr))
			return -1;
		if (nexthdr == NEXTHDR_NONE)
			break;
		if (skb_copy_bits(skb, start, &hdr, sizeof(hdr)))
			BUG();
		if (nexthdr == NEXTHDR_FRAGMENT) {
			struct frag_hdr fhdr;

			if (len < (int)sizeof(struct frag_hdr))
				return -1;
			if (skb_copy_bits(skb, start, &fhdr, sizeof(fhdr)))
				BUG();
			if (ntohs(fhdr.frag_off) & ~0x7)
				return -1;
			hdrlen = 8;
		} else if (nexthdr == NEXTHDR_AUTH)
			hdrlen = (hdr.hdrlen+2)<<2; 
		else
			hdrlen = ipv6_optlen(&hdr); 

		nexthdr = hdr.nexthdr;
		len -= hdrlen;
		start += hdrlen;
	}

	*nexthdrp = nexthdr;
	return start;
}

int ip6_ct_tuple_src_equal(const struct ip6_conntrack_tuple *t1,
			   const struct ip6_conntrack_tuple *t2)
{
	if (ipv6_addr_cmp(&t1->src.ip, &t2->src.ip))
		return 0;

	if (t1->src.u.all != t2->src.u.all)
		return 0;

	if (t1->dst.protonum != t2->dst.protonum)
		return 0;

	return 1;

}

int ip6_ct_tuple_dst_equal(const struct ip6_conntrack_tuple *t1,
			   const struct ip6_conntrack_tuple *t2)
{
	if (ipv6_addr_cmp(&t1->dst.ip, &t2->dst.ip))
		return 0;

	if (t1->dst.u.all != t2->dst.u.all)
		return 0;

	if (t1->dst.protonum != t2->dst.protonum)
		return 0;

	return 1;
}

int ip6_ct_tuple_equal(const struct ip6_conntrack_tuple *t1,
		       const struct ip6_conntrack_tuple *t2)
{
  return ip6_ct_tuple_src_equal(t1, t2) && ip6_ct_tuple_dst_equal(t1, t2);
}

int ip6_ct_tuple_mask_cmp(const struct ip6_conntrack_tuple *t,
			  const struct ip6_conntrack_tuple *tuple,
			  const struct ip6_conntrack_tuple *mask)
{
	int count = 0;

	for (count = 0; count < 8; count++){
		if ((ntohs(t->src.ip.s6_addr16[count]) ^
		     ntohs(tuple->src.ip.s6_addr16[count])) &
		    ntohs(mask->src.ip.s6_addr16[count]))
			return 0;

		if ((ntohs(t->dst.ip.s6_addr16[count]) ^
		     ntohs(tuple->dst.ip.s6_addr16[count])) &
		    ntohs(mask->dst.ip.s6_addr16[count]))
			return 0;
	}

	if ((t->src.u.all ^ tuple->src.u.all) & mask->src.u.all)
		return 0;

	if ((t->dst.u.all ^ tuple->dst.u.all) & mask->dst.u.all)
		return 0;

	if ((t->dst.protonum ^ tuple->dst.protonum) & mask->dst.protonum)
		return 0;

       return 1;
}

static inline int proto_cmpfn(const struct ip6_conntrack_protocol *curr,
			      u_int8_t protocol)
{
	return protocol == curr->proto;
}

struct ip6_conntrack_protocol *__ip6_ct_find_proto(u_int8_t protocol)
{
	struct ip6_conntrack_protocol *p;

	MUST_BE_READ_LOCKED(&ip6_conntrack_lock);
	p = LIST_FIND(&ip6_protocol_list, proto_cmpfn,
		      struct ip6_conntrack_protocol *, protocol);
	if (!p)
		p = &ip6_conntrack_generic_protocol;

	return p;
}

struct ip6_conntrack_protocol *ip6_ct_find_proto(u_int8_t protocol)
{
	struct ip6_conntrack_protocol *p;

	READ_LOCK(&ip6_conntrack_lock);
	p = __ip6_ct_find_proto(protocol);
	READ_UNLOCK(&ip6_conntrack_lock);
	return p;
}

inline void
ip6_conntrack_put(struct ip6_conntrack *ct)
{
	IP6_NF_ASSERT(ct);
	IP6_NF_ASSERT(ct->infos[0].master);
	/* nf_conntrack_put wants to go via an info struct, so feed it
           one at random. */
	nf_conntrack_put(&ct->infos[0]);
}

static int ip6_conntrack_hash_rnd_initted;
static unsigned int ip6_conntrack_hash_rnd;
static u_int32_t
hash_conntrack(const struct ip6_conntrack_tuple *tuple)
{
	u32 a, b, c;

	a = tuple->src.ip.s6_addr32[0];
	b = tuple->src.ip.s6_addr32[1];
	c = tuple->src.ip.s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += ip6_conntrack_hash_rnd;
	__jhash_mix(a, b, c);

	a += tuple->src.ip.s6_addr32[3];
	b += tuple->dst.ip.s6_addr32[0];
	c += tuple->dst.ip.s6_addr32[1];
	__jhash_mix(a, b, c);

	a += tuple->dst.ip.s6_addr32[2];
	b += tuple->dst.ip.s6_addr32[3];
	c += tuple->src.u.all | (tuple->dst.u.all << 16);
	__jhash_mix(a, b, c);

	a += tuple->dst.protonum;
	__jhash_mix(a, b, c);

	return c % ip6_conntrack_htable_size;
}

int
ip6_get_tuple(const struct ipv6hdr *ipv6h,
	      const struct sk_buff *skb,
	      unsigned int dataoff,
	      u_int8_t protonum,
	      struct ip6_conntrack_tuple *tuple,
	      const struct ip6_conntrack_protocol *protocol)
{
	/* Should I check that this packet is'nt fragmented
	   like IPv4 conntrack? - kozakai */

	ipv6_addr_copy(&tuple->src.ip, &ipv6h->saddr);
	ipv6_addr_copy(&tuple->dst.ip, &ipv6h->daddr);

	tuple->dst.protonum = protonum;

	return protocol->pkt_to_tuple(skb, dataoff, tuple);
}

static int
invert_tuple(struct ip6_conntrack_tuple *inverse,
	     const struct ip6_conntrack_tuple *orig,
	     const struct ip6_conntrack_protocol *protocol)
{
	ipv6_addr_copy(&inverse->src.ip, &orig->dst.ip);
	ipv6_addr_copy(&inverse->dst.ip, &orig->src.ip);
	inverse->dst.protonum = orig->dst.protonum;

	return protocol->invert_tuple(inverse, orig);
}


/* ip6_conntrack_expect helper functions */

/* Compare tuple parts depending on mask. */
static inline int expect_cmp(const struct ip6_conntrack_expect *i,
			     const struct ip6_conntrack_tuple *tuple)
{
	MUST_BE_READ_LOCKED(&ip6_conntrack_expect_tuple_lock);
	return ip6_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask);
}

static void
destroy_expect(struct ip6_conntrack_expect *exp)
{
	DEBUGP("destroy_expect(%p) use=%d\n", exp, atomic_read(&exp->use));
	IP6_NF_ASSERT(atomic_read(&exp->use));
	IP6_NF_ASSERT(!timer_pending(&exp->timeout));

	kfree(exp);
}


inline void ip6_conntrack_expect_put(struct ip6_conntrack_expect *exp)
{
	IP6_NF_ASSERT(exp);

	if (atomic_dec_and_test(&exp->use)) {
		/* usage count dropped to zero */
		destroy_expect(exp);
	}
}

static inline struct ip6_conntrack_expect *
__ip6_ct_expect_find(const struct ip6_conntrack_tuple *tuple)
{
	MUST_BE_READ_LOCKED(&ip6_conntrack_lock);
	MUST_BE_READ_LOCKED(&ip6_conntrack_expect_tuple_lock);
	return LIST_FIND(&ip6_conntrack_expect_list, expect_cmp, 
			 struct ip6_conntrack_expect *, tuple);
}

/* Find a expectation corresponding to a tuple. */
struct ip6_conntrack_expect *
ip6_conntrack_expect_find_get(const struct ip6_conntrack_tuple *tuple)
{
	struct ip6_conntrack_expect *exp;

	READ_LOCK(&ip6_conntrack_lock);
	READ_LOCK(&ip6_conntrack_expect_tuple_lock);
	exp = __ip6_ct_expect_find(tuple);
	if (exp)
		atomic_inc(&exp->use);
	READ_UNLOCK(&ip6_conntrack_expect_tuple_lock);
	READ_UNLOCK(&ip6_conntrack_lock);

	return exp;
}

/* remove one specific expectation from all lists and drop refcount,
 * does _NOT_ delete the timer. */
static void __unexpect_related(struct ip6_conntrack_expect *expect)
{
	DEBUGP("unexpect_related(%p)\n", expect);
	MUST_BE_WRITE_LOCKED(&ip6_conntrack_lock);

	/* we're not allowed to unexpect a confirmed expectation! */
	IP6_NF_ASSERT(!expect->sibling);

	/* delete from global and local lists */
	list_del(&expect->list);
	list_del(&expect->expected_list);

	/* decrement expect-count of master conntrack */
	if (expect->expectant)
		expect->expectant->expecting--;

	ip6_conntrack_expect_put(expect);
}

/* remove one specific expecatation from all lists, drop refcount
 * and expire timer. 
 * This function can _NOT_ be called for confirmed expects! */
static void unexpect_related(struct ip6_conntrack_expect *expect)
{
	IP6_NF_ASSERT(expect->expectant);
	IP6_NF_ASSERT(expect->expectant->helper);
	/* if we are supposed to have a timer, but we can't delete
	 * it: race condition.  __unexpect_related will
	 * be calledd by timeout function */
	if (expect->expectant->helper->timeout
	    && !del_timer(&expect->timeout))
		return;

	__unexpect_related(expect);
}

/* delete all unconfirmed expectations for this conntrack */
static void remove_expectations(struct ip6_conntrack *ct, int drop_refcount)
{
	struct list_head *exp_entry, *next;
	struct ip6_conntrack_expect *exp;

	DEBUGP("remove_expectations(%p)\n", ct);

	list_for_each_safe(exp_entry, next, &ct->sibling_list) {
		exp = list_entry(exp_entry, struct ip6_conntrack_expect,
				 expected_list);

		/* we skip established expectations, as we want to delete
		 * the un-established ones only */
		if (exp->sibling) {
			DEBUGP("remove_expectations: skipping established %p of %p\n", exp->sibling, ct);
			if (drop_refcount) {
				/* Indicate that this expectations parent is dead */
				ip6_conntrack_put(exp->expectant);
				exp->expectant = NULL;
			}
			continue;
		}

		IP6_NF_ASSERT(list_inlist(&ip6_conntrack_expect_list, exp));
		IP6_NF_ASSERT(exp->expectant == ct);

		/* delete expectation from global and private lists */
		unexpect_related(exp);
	}
}

static void
clean_from_lists(struct ip6_conntrack *ct)
{
	unsigned int ho, hr;

	DEBUGP("clean_from_lists(%p)\n", ct);
	MUST_BE_WRITE_LOCKED(&ip6_conntrack_lock);

	ho = hash_conntrack(&ct->tuplehash[IP6_CT_DIR_ORIGINAL].tuple);
	hr = hash_conntrack(&ct->tuplehash[IP6_CT_DIR_REPLY].tuple);

	LIST_DELETE(&ip6_conntrack_hash[ho],
		    &ct->tuplehash[IP6_CT_DIR_ORIGINAL]);
	LIST_DELETE(&ip6_conntrack_hash[hr],
		    &ct->tuplehash[IP6_CT_DIR_REPLY]);

	/* Destroy all un-established, pending expectations */
	remove_expectations(ct, 1);
}

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct ip6_conntrack *ct = (struct ip6_conntrack *)nfct, *master = NULL;
	struct ip6_conntrack_protocol *proto;

	DEBUGP("destroy_conntrack(%p)\n", ct);
	IP6_NF_ASSERT(atomic_read(&nfct->use) == 0);
	IP6_NF_ASSERT(!timer_pending(&ct->timeout));

	/* To make sure we don't get any weird locking issues here:
	 * destroy_conntrack() MUST NOT be called with a write lock
	 * to ip6_conntrack_lock!!! -HW */
	proto = ip6_ct_find_proto(ct->tuplehash[IP6_CT_DIR_REPLY].tuple.dst.protonum);
	if (proto && proto->destroy)
		proto->destroy(ct);

	if (ip6_conntrack_destroyed)
		ip6_conntrack_destroyed(ct);

	WRITE_LOCK(&ip6_conntrack_lock);
	/* Delete us from our own list to prevent corruption later */
	list_del(&ct->sibling_list);

	/* Delete our master expectation */
	if (ct->master) {
		if (ct->master->expectant) {
			/* can't call __unexpect_related here,
			 * since it would screw up expect_list */
			list_del(&ct->master->expected_list);
			master = ct->master->expectant;
		}
		kfree(ct->master);
	}
	WRITE_UNLOCK(&ip6_conntrack_lock);

	if (master)
		ip6_conntrack_put(master);

	DEBUGP("destroy_conntrack: returning ct=%p to slab\n", ct);
	kmem_cache_free(ip6_conntrack_cachep, ct);
	atomic_dec(&ip6_conntrack_count);
}

static void death_by_timeout(unsigned long ul_conntrack)
{
	struct ip6_conntrack *ct = (void *)ul_conntrack;

	WRITE_LOCK(&ip6_conntrack_lock);
	clean_from_lists(ct);
	WRITE_UNLOCK(&ip6_conntrack_lock);
	ip6_conntrack_put(ct);
}

static inline int
conntrack_tuple_cmp(const struct ip6_conntrack_tuple_hash *i,
		    const struct ip6_conntrack_tuple *tuple,
		    const struct ip6_conntrack *ignored_conntrack)
{
	MUST_BE_READ_LOCKED(&ip6_conntrack_lock);
	return i->ctrack != ignored_conntrack
		&& ip6_ct_tuple_equal(tuple, &i->tuple);
}

static struct ip6_conntrack_tuple_hash *
__ip6_conntrack_find(const struct ip6_conntrack_tuple *tuple,
		    const struct ip6_conntrack *ignored_conntrack)
{
	struct ip6_conntrack_tuple_hash *h;
	unsigned int hash = hash_conntrack(tuple);

	MUST_BE_READ_LOCKED(&ip6_conntrack_lock);
	h = LIST_FIND(&ip6_conntrack_hash[hash],
		      conntrack_tuple_cmp,
		      struct ip6_conntrack_tuple_hash *,
		      tuple, ignored_conntrack);
	return h;
}

/* Find a connection corresponding to a tuple. */
struct ip6_conntrack_tuple_hash *
ip6_conntrack_find_get(const struct ip6_conntrack_tuple *tuple,
		      const struct ip6_conntrack *ignored_conntrack)
{
	struct ip6_conntrack_tuple_hash *h;

	READ_LOCK(&ip6_conntrack_lock);
	h = __ip6_conntrack_find(tuple, ignored_conntrack);
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip6_conntrack_lock);

	return h;
}

static inline struct ip6_conntrack *
__ip6_conntrack_get(struct nf_ct_info *nfct, enum ip6_conntrack_info *ctinfo)
{
	struct ip6_conntrack *ct
		= (struct ip6_conntrack *)nfct->master;

	/* ctinfo is the index of the nfct inside the conntrack */
	*ctinfo = nfct - ct->infos;
	IP6_NF_ASSERT(*ctinfo >= 0 && *ctinfo < IP6_CT_NUMBER);
	return ct;
}

/* Return conntrack and conntrack_info given skb->nfct->master */
struct ip6_conntrack *
ip6_conntrack_get(struct sk_buff *skb, enum ip6_conntrack_info *ctinfo)
{
	if (skb->nfct) 
		return __ip6_conntrack_get(skb->nfct, ctinfo);
	return NULL;
}

/* Confirm a connection given skb->nfct; places it in hash table */
int
__ip6_conntrack_confirm(struct nf_ct_info *nfct)
{
	unsigned int hash, repl_hash;
	struct ip6_conntrack *ct;
	enum ip6_conntrack_info ctinfo;

	ct = __ip6_conntrack_get(nfct, &ctinfo);

	/* ip6t_REJECT uses ip6_conntrack_attach to attach related
	   ICMP/TCP RST packets in other direction.  Actual packet
	   which created connection will be IP6_CT_NEW or for an
	   expected connection, IP6_CT_RELATED. */
	if (CTINFO2DIR(ctinfo) != IP6_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	hash = hash_conntrack(&ct->tuplehash[IP6_CT_DIR_ORIGINAL].tuple);
	repl_hash = hash_conntrack(&ct->tuplehash[IP6_CT_DIR_REPLY].tuple);

	/* We're not in hash table, and we refuse to set up related
	   connections for unconfirmed conns.  But packet copies and
	   REJECT will give spurious warnings here. */
	/* IP6_NF_ASSERT(atomic_read(&ct->ct_general.use) == 1); */

	/* No external references means noone else could have
           confirmed us. */
	IP6_NF_ASSERT(!is_confirmed(ct));
	DEBUGP("Confirming conntrack %p\n", ct);

	WRITE_LOCK(&ip6_conntrack_lock);
	/* See if there's one in the list already, including reverse:
           NAT could have grabbed it without realizing, since we're
           not in the hash.  If there is, we lost race. */
	if (!LIST_FIND(&ip6_conntrack_hash[hash],
		       conntrack_tuple_cmp,
		       struct ip6_conntrack_tuple_hash *,
		       &ct->tuplehash[IP6_CT_DIR_ORIGINAL].tuple, NULL)
	    && !LIST_FIND(&ip6_conntrack_hash[repl_hash],
			  conntrack_tuple_cmp,
			  struct ip6_conntrack_tuple_hash *,
			  &ct->tuplehash[IP6_CT_DIR_REPLY].tuple, NULL)) {
		list_prepend(&ip6_conntrack_hash[hash],
			     &ct->tuplehash[IP6_CT_DIR_ORIGINAL]);
		list_prepend(&ip6_conntrack_hash[repl_hash],
			     &ct->tuplehash[IP6_CT_DIR_REPLY]);
		/* Timer relative to confirmation time, not original
		   setting time, otherwise we'd get timer wrap in
		   wierd delay cases. */
		ct->timeout.expires += jiffies;
		add_timer(&ct->timeout);
		atomic_inc(&ct->ct_general.use);
		set_bit(IP6S_CONFIRMED_BIT, &ct->status);
		WRITE_UNLOCK(&ip6_conntrack_lock);
		return NF_ACCEPT;
	}

	WRITE_UNLOCK(&ip6_conntrack_lock);
	return NF_DROP;
}

/* Is this needed ? this code is for NAT. - kozakai */
/* Returns true if a connection correspondings to the tuple (required
   for NAT). */
int
ip6_conntrack_tuple_taken(const struct ip6_conntrack_tuple *tuple,
			 const struct ip6_conntrack *ignored_conntrack)
{
	struct ip6_conntrack_tuple_hash *h;

	READ_LOCK(&ip6_conntrack_lock);
	h = __ip6_conntrack_find(tuple, ignored_conntrack);
	READ_UNLOCK(&ip6_conntrack_lock);

	return h != NULL;
}

/* Returns conntrack if it dealt with ICMP, and filled in skb fields */
struct ip6_conntrack *
icmp6_error_track(struct sk_buff *skb,
		  unsigned int icmp6off,
		  enum ip6_conntrack_info *ctinfo,
		  unsigned int hooknum)
{
	struct ip6_conntrack_tuple intuple, origtuple;
	struct ip6_conntrack_tuple_hash *h;
	struct ipv6hdr *ip6h;
	struct icmp6hdr hdr;
	struct ipv6hdr inip6h;
	unsigned int inip6off;
	struct ip6_conntrack_protocol *inproto;
	u_int8_t inprotonum;
	unsigned int inprotoff;

	IP6_NF_ASSERT(skb->nfct == NULL);

	ip6h = skb->nh.ipv6h;
	if (skb_copy_bits(skb, icmp6off, &hdr, sizeof(hdr)) != 0) {
		DEBUGP("icmp_error_track: Can't copy ICMPv6 hdr.\n");
		return NULL;
	}

	if (hdr.icmp6_type >= 128)
		return NULL;

	/*
	 * Should I ignore invalid ICMPv6 error here ?
	 * ex) ICMPv6 error in ICMPv6 error, Fragmented packet, and so on.
	 * - kozakai
	 */

	/* Why not check checksum in IPv4 conntrack ? - kozakai */
	/* Ignore it if the checksum's bogus. */

	if (csum_ipv6_magic(&ip6h->saddr, &ip6h->daddr, skb->len - icmp6off,
			    IPPROTO_ICMPV6,
			    skb_checksum(skb, icmp6off,
					 skb->len - icmp6off, 0))) {
		DEBUGP("ICMPv6 checksum failed\n");
		return NULL;
	}

	inip6off = icmp6off + sizeof(hdr);
	if (skb_copy_bits(skb, inip6off, &inip6h, sizeof(inip6h)) != 0) {
		DEBUGP("Can't copy inner IPv6 hdr.\n");
		return NULL;
	}

	inprotonum = inip6h.nexthdr;
	inprotoff = ip6_ct_skip_exthdr(skb, inip6off + sizeof(inip6h),
				       &inprotonum,
				       skb->len - inip6off - sizeof(inip6h));

	if (inprotoff < 0 || inprotoff > skb->len
	    || inprotonum == NEXTHDR_FRAGMENT) {
		DEBUGP("icmp6_error: Can't find protocol header in ICMPv6 payload.\n");
		return NULL;
	}

	inproto = ip6_ct_find_proto(inprotonum);

	/* Are they talking about one of our connections? */
	if (!ip6_get_tuple(&inip6h, skb, inprotoff, inprotonum,
			   &origtuple, inproto)) {
		DEBUGP("icmp6_error: ! get_tuple p=%u\n", inprotonum);
		return NULL;
	}

	/* Ordinarily, we'd expect the inverted tupleproto, but it's
	   been preserved inside the ICMP. */
	if (!invert_tuple(&intuple, &origtuple, inproto)) {
		DEBUGP("icmp6_error_track: Can't invert tuple\n");
		return NULL;
	}

	*ctinfo = IP6_CT_RELATED;

	h = ip6_conntrack_find_get(&intuple, NULL);
	if (!h) {
		DEBUGP("icmp6_error_track: no match\n");
		return NULL;
	} else {
		if (DIRECTION(h) == IP6_CT_DIR_REPLY)
			*ctinfo += IP6_CT_IS_REPLY;
	}

	/* Update skb to refer to this connection */
	skb->nfct = &h->ctrack->infos[*ctinfo];
	return h->ctrack;
}

/* There's a small race here where we may free a just-assured
   connection.  Too bad: we're in trouble anyway. */
static inline int unreplied(const struct ip6_conntrack_tuple_hash *i)
{
	return !(test_bit(IP6S_ASSURED_BIT, &i->ctrack->status));
}

static int early_drop(struct list_head *chain)
{
	/* Traverse backwards: gives us oldest, which is roughly LRU */
	struct ip6_conntrack_tuple_hash *h;
	int dropped = 0;

	READ_LOCK(&ip6_conntrack_lock);
	h = LIST_FIND_B(chain, unreplied, struct ip6_conntrack_tuple_hash *);
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip6_conntrack_lock);

	if (!h)
		return dropped;

	if (del_timer(&h->ctrack->timeout)) {
		death_by_timeout((unsigned long)h->ctrack);
		dropped = 1;
	}
	ip6_conntrack_put(h->ctrack);
	return dropped;
}

static inline int helper_cmp(const struct ip6_conntrack_helper *i,
			     const struct ip6_conntrack_tuple *rtuple)
{
	return ip6_ct_tuple_mask_cmp(rtuple, &i->tuple, &i->mask);
}

struct ip6_conntrack_helper *
ip6_ct_find_helper(const struct ip6_conntrack_tuple *tuple){

	MUST_BE_READ_LOCKED(&ip6_conntrack_lock);
	return LIST_FIND(&helpers, helper_cmp,
			 struct ip6_conntrack_helper *,
			 tuple);
}

/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static struct ip6_conntrack_tuple_hash *
init_conntrack(const struct ip6_conntrack_tuple *tuple,
	       struct ip6_conntrack_protocol *protocol,
	       struct sk_buff *skb,
	       unsigned int protoff)
{
	struct ip6_conntrack *conntrack;
	struct ip6_conntrack_tuple repl_tuple;
	size_t hash;
	struct ip6_conntrack_expect *expected;
	int i;
	static unsigned int drop_next = 0;

	if (!ip6_conntrack_hash_rnd_initted) {
		get_random_bytes(&ip6_conntrack_hash_rnd, 4);
		ip6_conntrack_hash_rnd_initted = 1;
	}

	hash = hash_conntrack(tuple);

	if (ip6_conntrack_max &&
	    atomic_read(&ip6_conntrack_count) >= ip6_conntrack_max) {
		/* Try dropping from random chain, or else from the
                   chain about to put into (in case they're trying to
                   bomb one hash chain). */
		unsigned int next = (drop_next++)%ip6_conntrack_htable_size;

		if (!early_drop(&ip6_conntrack_hash[next])
		    && !early_drop(&ip6_conntrack_hash[hash])) {
			if (net_ratelimit())
				printk(KERN_WARNING
				       "ip6_conntrack: table full, dropping"
				       " packet.\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	if (!invert_tuple(&repl_tuple, tuple, protocol)) {
		DEBUGP("Can't invert tuple.\n");
		return NULL;
	}

	conntrack = kmem_cache_alloc(ip6_conntrack_cachep, GFP_ATOMIC);
	if (!conntrack) {
		DEBUGP("Can't allocate conntrack.\n");
		return ERR_PTR(-ENOMEM);
	}

	memset(conntrack, 0, sizeof(*conntrack));
	atomic_set(&conntrack->ct_general.use, 1);
	conntrack->ct_general.destroy = destroy_conntrack;
	conntrack->tuplehash[IP6_CT_DIR_ORIGINAL].tuple = *tuple;
	conntrack->tuplehash[IP6_CT_DIR_ORIGINAL].ctrack = conntrack;
	conntrack->tuplehash[IP6_CT_DIR_REPLY].tuple = repl_tuple;
	conntrack->tuplehash[IP6_CT_DIR_REPLY].ctrack = conntrack;
	for (i=0; i < IP6_CT_NUMBER; i++)
		conntrack->infos[i].master = &conntrack->ct_general;

	if (!protocol->new(conntrack, skb, protoff)) {
		kmem_cache_free(ip6_conntrack_cachep, conntrack);
		return NULL;
	}
	/* Don't set timer yet: wait for confirmation */
	init_timer(&conntrack->timeout);
	conntrack->timeout.data = (unsigned long)conntrack;
	conntrack->timeout.function = death_by_timeout;

	INIT_LIST_HEAD(&conntrack->sibling_list);

	WRITE_LOCK(&ip6_conntrack_lock);
	/* Need finding and deleting of expected ONLY if we win race */
	READ_LOCK(&ip6_conntrack_expect_tuple_lock);
	expected = LIST_FIND(&ip6_conntrack_expect_list, expect_cmp,
			     struct ip6_conntrack_expect *, tuple);
	READ_UNLOCK(&ip6_conntrack_expect_tuple_lock);

	/* If master is not in hash table yet (ie. packet hasn't left
	   this machine yet), how can other end know about expected?
	   Hence these are not the droids you are looking for (if
	   master ct never got confirmed, we'd hold a reference to it
	   and weird things would happen to future packets). */
	if (expected && !is_confirmed(expected->expectant))
		expected = NULL;

	/* Look up the conntrack helper for master connections only */
	if (!expected)
		conntrack->helper = ip6_ct_find_helper(&repl_tuple);

	/* If the expectation is dying, then this is a loser. */
	if (expected
	    && expected->expectant->helper->timeout
	    && ! del_timer(&expected->timeout))
		expected = NULL;

	if (expected) {
		DEBUGP("conntrack: expectation arrives ct=%p exp=%p\n",
			conntrack, expected);
		/* Welcome, Mr. Bond.  We've been expecting you... */
		IP6_NF_ASSERT(master_ct6(conntrack));
		__set_bit(IP6S_EXPECTED_BIT, &conntrack->status);
		conntrack->master = expected;
		expected->sibling = conntrack;
		LIST_DELETE(&ip6_conntrack_expect_list, expected);
		expected->expectant->expecting--;
		nf_conntrack_get(&master_ct6(conntrack)->infos[0]);
	}
	atomic_inc(&ip6_conntrack_count);
	WRITE_UNLOCK(&ip6_conntrack_lock);

	if (expected && expected->expectfn)
		expected->expectfn(conntrack);
	return &conntrack->tuplehash[IP6_CT_DIR_ORIGINAL];
}

/* On success, returns conntrack ptr, sets skb->nfct and ctinfo */
static inline struct ip6_conntrack *
resolve_normal_ct(struct sk_buff *skb,
		  unsigned int protoff,
		  u_int16_t protonum,
		  struct ip6_conntrack_protocol *proto,
		  int *set_reply,
		  unsigned int hooknum,
		  enum ip6_conntrack_info *ctinfo)
{
	struct ip6_conntrack_tuple tuple;
	struct ip6_conntrack_tuple_hash *h;

	if (!ip6_get_tuple(skb->nh.ipv6h, skb, protoff, protonum, &tuple, proto))
		return NULL;

	/* look for tuple match */
	h = ip6_conntrack_find_get(&tuple, NULL);
	if (!h) {
		h = init_conntrack(&tuple, proto, skb, protoff);
		if (!h)
			return NULL;
		if (IS_ERR(h))
			return (void *)h;
	}

	/* It exists; we have (non-exclusive) reference. */
	if (DIRECTION(h) == IP6_CT_DIR_REPLY) {
		*ctinfo = IP6_CT_ESTABLISHED + IP6_CT_IS_REPLY;
		/* Please set reply bit if this packet OK */
		*set_reply = 1;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (test_bit(IP6S_SEEN_REPLY_BIT, &h->ctrack->status)) {
			DEBUGP("ip6_conntrack_in: normal packet for %p\n",
			       h->ctrack);
		        *ctinfo = IP6_CT_ESTABLISHED;
		} else if (test_bit(IP6S_EXPECTED_BIT, &h->ctrack->status)) {
			DEBUGP("ip6_conntrack_in: related packet for %p\n",
			       h->ctrack);
			*ctinfo = IP6_CT_RELATED;
		} else {
			DEBUGP("ip6_conntrack_in: new packet for %p\n",
			       h->ctrack);
			*ctinfo = IP6_CT_NEW;
		}
		*set_reply = 0;
	}
	skb->nfct = &h->ctrack->infos[*ctinfo];
	return h->ctrack;
}

/* Netfilter hook itself. */
unsigned int ip6_conntrack_in(unsigned int hooknum,
			     struct sk_buff **pskb,
			     const struct net_device *in,
			     const struct net_device *out,
			     int (*okfn)(struct sk_buff *))
{
	struct ip6_conntrack *ct;
	enum ip6_conntrack_info ctinfo;
	struct ip6_conntrack_protocol *proto;
	int set_reply;
	int ret;
	u_int8_t protonum;
	int len;
	int daddr_type;
	int protoff, extoff;

	/* FIXME: Do this right please. --RR */
	(*pskb)->nfcache |= NFC_UNKNOWN;

	/* Ignore multicast - kozakai */
	daddr_type = ipv6_addr_type(&(*pskb)->nh.ipv6h->daddr);
	if (daddr_type & IPV6_ADDR_MULTICAST)
		return NF_ACCEPT;

	/* Previously seen (loopback)?  Ignore.  Do this before
           fragment check. */
	if ((*pskb)->nfct)
		return NF_ACCEPT;

	extoff = (u8*)((*pskb)->nh.ipv6h+1) - (*pskb)->data;
	len = (*pskb)->len - extoff;

	/* Verify that a protocol is present and get the protocol handler
	   we need */
	protonum = (*pskb)->nh.ipv6h->nexthdr;
	protoff = ip6_ct_skip_exthdr(*pskb, extoff, &protonum, len);

	/*
	 * Notice! (protoff == (*pskb)->len) mean that this packet doesn't
	 * have no data except of IPv6 & ext headers. but tracked anyway.
	 * - kozakai
	 */
	if (protoff < 0 || protoff > (*pskb)->len
	    || protonum == NEXTHDR_FRAGMENT) {
		DEBUGP("ip6_conntrack_core: can't find proto in pkt\n");
		return NF_ACCEPT;
	}

	/* It may be an icmp error... */
	if (protonum == IPPROTO_ICMPV6
	    && icmp6_error_track(*pskb, protoff, &ctinfo, hooknum))
		return NF_ACCEPT;

	proto = ip6_ct_find_proto(protonum);

	if (!(ct = resolve_normal_ct(*pskb, protoff, protonum, proto,
				     &set_reply, hooknum,&ctinfo)))
		/* Not valid part of a connection */
		return NF_ACCEPT;

	if (IS_ERR(ct))
		/* Too stressed to deal. */
		return NF_DROP;

	IP6_NF_ASSERT((*pskb)->nfct);

	ret = proto->packet(ct, *pskb, protoff, ctinfo);
	if (ret == -1) {
		/* Invalid */
		nf_conntrack_put((*pskb)->nfct);
		(*pskb)->nfct = NULL;
		return NF_ACCEPT;
	}

	if (ret != NF_DROP && ct->helper) {
		ret = ct->helper->help(*pskb, protoff, ct, ctinfo);
		if (ret == -1) {
			/* Invalid */
			nf_conntrack_put((*pskb)->nfct);
			(*pskb)->nfct = NULL;
			return NF_ACCEPT;
		}
	}
	if (set_reply)
		set_bit(IP6S_SEEN_REPLY_BIT, &ct->status);

	return ret;
}

int ip6_invert_tuplepr(struct ip6_conntrack_tuple *inverse,
		       const struct ip6_conntrack_tuple *orig)
{
	return invert_tuple(inverse, orig, ip6_ct_find_proto(orig->dst.protonum));
}

static inline int resent_expect(const struct ip6_conntrack_expect *i,
				const struct ip6_conntrack_tuple *tuple,
				const struct ip6_conntrack_tuple *mask)
{
	DEBUGP("resent_expect\n");
	DEBUGP("   tuple:   "); DUMP_TUPLE(&i->tuple);
	DEBUGP("test tuple: "); DUMP_TUPLE(tuple);
	return (ip6_ct_tuple_equal(&i->tuple, tuple)
		&& ip6_ct_tuple_equal(&i->mask, mask));
}

static struct in6_addr *
or_addr6_bits(struct in6_addr *result, const struct in6_addr *one,
	      const struct in6_addr *two)
{

       int count = 0;

       for (count = 0; count < 8; count++)
               result->s6_addr16[count] = ntohs(one->s6_addr16[count])
					& ntohs(two->s6_addr16[count]);

       return result;
}

/* Would two expected things clash? */
static inline int expect_clash(const struct ip6_conntrack_expect *i,
			       const struct ip6_conntrack_tuple *tuple,
			       const struct ip6_conntrack_tuple *mask)
{
	/* Part covered by intersection of masks must be unequal,
           otherwise they clash */
	struct ip6_conntrack_tuple intersect_mask;

	intersect_mask.src.u.all =  i->mask.src.u.all & mask->src.u.all;
	intersect_mask.dst.u.all =  i->mask.dst.u.all & mask->dst.u.all;
	intersect_mask.dst.protonum = i->mask.dst.protonum
					& mask->dst.protonum;

	or_addr6_bits(&intersect_mask.src.ip, &i->mask.src.ip,
		      &mask->src.ip);
	or_addr6_bits(&intersect_mask.dst.ip, &i->mask.dst.ip,
		      &mask->dst.ip);

	return ip6_ct_tuple_mask_cmp(&i->tuple, tuple, &intersect_mask);
}

inline void ip6_conntrack_unexpect_related(struct ip6_conntrack_expect *expect)
{
	WRITE_LOCK(&ip6_conntrack_lock);
	unexpect_related(expect);
	WRITE_UNLOCK(&ip6_conntrack_lock);
}

static void expectation_timed_out(unsigned long ul_expect)
{
	struct ip6_conntrack_expect *expect = (void *) ul_expect;

	DEBUGP("expectation %p timed out\n", expect);	
	WRITE_LOCK(&ip6_conntrack_lock);
	__unexpect_related(expect);
	WRITE_UNLOCK(&ip6_conntrack_lock);
}

/* Add a related connection. */
int ip6_conntrack_expect_related(struct ip6_conntrack *related_to,
				struct ip6_conntrack_expect *expect)
{
	struct ip6_conntrack_expect *old, *new;
	int ret = 0;

	WRITE_LOCK(&ip6_conntrack_lock);
	/* Because of the write lock, no reader can walk the lists,
	 * so there is no need to use the tuple lock too */

	DEBUGP("ip6_conntrack_expect_related %p\n", related_to);
	DEBUGP("tuple: "); DUMP_TUPLE(&expect->tuple);
	DEBUGP("mask:  "); DUMP_TUPLE(&expect->mask);

	old = LIST_FIND(&ip6_conntrack_expect_list, resent_expect,
		        struct ip6_conntrack_expect *, &expect->tuple, 
			&expect->mask);
	if (old) {
		/* Helper private data may contain offsets but no pointers
		   pointing into the payload - otherwise we should have to copy 
		   the data filled out by the helper over the old one */
		DEBUGP("expect_related: resent packet\n");
		if (related_to->helper->timeout) {
			if (!del_timer(&old->timeout)) {
				/* expectation is dying. Fall through */
				old = NULL;
			} else {
				old->timeout.expires = jiffies + 
					related_to->helper->timeout * HZ;
				add_timer(&old->timeout);
			}
		}

		if (old) {
			WRITE_UNLOCK(&ip6_conntrack_lock);
			return -EEXIST;
		}
	} else if (related_to->helper->max_expected && 
		   related_to->expecting >= related_to->helper->max_expected) {
		struct list_head *cur_item;
		/* old == NULL */
		if (!(related_to->helper->flags & 
		      IP6_CT_HELPER_F_REUSE_EXPECT)) {
			WRITE_UNLOCK(&ip6_conntrack_lock);
 		    	if (net_ratelimit())
 			    	printk(KERN_WARNING
				       "ip6_conntrack: max number of expected "
				       "connections %i of %s for "
				       "%x:%x:%x:%x:%x:%x:%x:%x->%x:%x:%x:%x:%x:%x:%x:%x\n",
				       related_to->helper->max_expected,
				       related_to->helper->name,
				       NIP6(related_to->tuplehash[IP6_CT_DIR_ORIGINAL].tuple.src.ip),
				       NIP6(related_to->tuplehash[IP6_CT_DIR_ORIGINAL].tuple.dst.ip));
			return -EPERM;
		}
		DEBUGP("ip6_conntrack: max number of expected "
		       "connections %i of %s reached for "
		       "%x:%x:%x:%x:%x:%x:%x:%x->%x:%x:%x:%x:%x:%x:%x:%x, reusing\n",
 		       related_to->helper->max_expected,
		       related_to->helper->name,
		       NIP6(related_to->tuplehash[IP6_CT_DIR_ORIGINAL].tuple.src.ip),
		       NIP6(related_to->tuplehash[IP6_CT_DIR_ORIGINAL].tuple.dst.ip));
 
		/* choose the the oldest expectation to evict */
		list_for_each(cur_item, &related_to->sibling_list) { 
			struct ip6_conntrack_expect *cur;

			cur = list_entry(cur_item, 
					 struct ip6_conntrack_expect,
					 expected_list);
			if (cur->sibling == NULL) {
				old = cur;
				break;
			}
		}

		/* (!old) cannot happen, since related_to->expecting is the
		 * number of unconfirmed expects */
		IP6_NF_ASSERT(old);

		/* newnat14 does not reuse the real allocated memory
		 * structures but rather unexpects the old and
		 * allocates a new.  unexpect_related will decrement
		 * related_to->expecting. 
		 */
		unexpect_related(old);
		ret = -EPERM;
	} else if (LIST_FIND(&ip6_conntrack_expect_list, expect_clash,
			     struct ip6_conntrack_expect *, &expect->tuple, 
			     &expect->mask)) {
		WRITE_UNLOCK(&ip6_conntrack_lock);
		DEBUGP("expect_related: busy!\n");
		return -EBUSY;
	}
	
	new = (struct ip6_conntrack_expect *) 
	      kmalloc(sizeof(struct ip6_conntrack_expect), GFP_ATOMIC);
	if (!new) {
		WRITE_UNLOCK(&ip6_conntrack_lock);
		DEBUGP("expect_relaed: OOM allocating expect\n");
		return -ENOMEM;
	}
	
	DEBUGP("new expectation %p of conntrack %p\n", new, related_to);
	memcpy(new, expect, sizeof(*expect));
	new->expectant = related_to;
	new->sibling = NULL;
	atomic_set(&new->use, 1);
	
	/* add to expected list for this connection */	
	list_add(&new->expected_list, &related_to->sibling_list);
	/* add to global list of expectations */
	list_prepend(&ip6_conntrack_expect_list, &new->list);
	/* add and start timer if required */
	if (related_to->helper->timeout) {
		init_timer(&new->timeout);
		new->timeout.data = (unsigned long)new;
		new->timeout.function = expectation_timed_out;
		new->timeout.expires = jiffies + 
					related_to->helper->timeout * HZ;
		add_timer(&new->timeout);
	}
	related_to->expecting++;

	WRITE_UNLOCK(&ip6_conntrack_lock);

	return ret;
}


/* Is this code needed ? this is for NAT. - kozakai */
/* Alter reply tuple (maybe alter helper).  If it's already taken,
   return 0 and don't do alteration. */
int ip6_conntrack_alter_reply(struct ip6_conntrack *conntrack,
			     const struct ip6_conntrack_tuple *newreply)
{
	WRITE_LOCK(&ip6_conntrack_lock);
	if (__ip6_conntrack_find(newreply, conntrack)) {
		WRITE_UNLOCK(&ip6_conntrack_lock);
		return 0;
	}
	/* Should be unconfirmed, so not in hash table yet */
	IP6_NF_ASSERT(!is_confirmed(conntrack));

	DEBUGP("Altering reply tuple of %p to ", conntrack);
	DUMP_TUPLE(newreply);

	conntrack->tuplehash[IP6_CT_DIR_REPLY].tuple = *newreply;
	if (!conntrack->master)
		conntrack->helper = ip6_ct_find_helper(newreply);
	WRITE_UNLOCK(&ip6_conntrack_lock);

	return 1;
}

int ip6_conntrack_helper_register(struct ip6_conntrack_helper *me)
{
	WRITE_LOCK(&ip6_conntrack_lock);
	list_prepend(&helpers, me);
	WRITE_UNLOCK(&ip6_conntrack_lock);

	return 0;
}

static inline int unhelp(struct ip6_conntrack_tuple_hash *i,
			 const struct ip6_conntrack_helper *me)
{
	if (i->ctrack->helper == me) {
		/* Get rid of any expected. */
		remove_expectations(i->ctrack, 0);
		/* And *then* set helper to NULL */
		i->ctrack->helper = NULL;
	}
	return 0;
}

void ip6_conntrack_helper_unregister(struct ip6_conntrack_helper *me)
{
	unsigned int i;

	/* Need write lock here, to delete helper. */
	WRITE_LOCK(&ip6_conntrack_lock);
	LIST_DELETE(&helpers, me);

	/* Get rid of expecteds, set helpers to NULL. */
	for (i = 0; i < ip6_conntrack_htable_size; i++)
		LIST_FIND_W(&ip6_conntrack_hash[i], unhelp,
			    struct ip6_conntrack_tuple_hash *, me);
	WRITE_UNLOCK(&ip6_conntrack_lock);

	/* Someone could be still looking at the helper in a bh. */
	synchronize_net();
}

/* Refresh conntrack for this many jiffies. */
void ip6_ct_refresh(struct ip6_conntrack *ct, unsigned long extra_jiffies)
{
	IP6_NF_ASSERT(ct->timeout.data == (unsigned long)ct);

	WRITE_LOCK(&ip6_conntrack_lock);
	/* If not in hash table, timer will not be active yet */
	if (!is_confirmed(ct))
		ct->timeout.expires = extra_jiffies;
	else {
		/* Need del_timer for race avoidance (may already be dying). */
		if (del_timer(&ct->timeout)) {
			ct->timeout.expires = jiffies + extra_jiffies;
			add_timer(&ct->timeout);
		}
	}
	WRITE_UNLOCK(&ip6_conntrack_lock);
}

/* Used by ip6t_REJECT. */
static void ip6_conntrack_attach(struct sk_buff *nskb, struct nf_ct_info *nfct)
{
	struct ip6_conntrack *ct;
	enum ip6_conntrack_info ctinfo;

	ct = __ip6_conntrack_get(nfct, &ctinfo);

	/* This ICMP is in reverse direction to the packet which
           caused it */
	if (CTINFO2DIR(ctinfo) == IP6_CT_DIR_ORIGINAL)
		ctinfo = IP6_CT_RELATED + IP6_CT_IS_REPLY;
	else
		ctinfo = IP6_CT_RELATED;

	/* Attach new skbuff, and increment count */
	nskb->nfct = &ct->infos[ctinfo];
	atomic_inc(&ct->ct_general.use);
}

static inline int
do_kill(const struct ip6_conntrack_tuple_hash *i,
	int (*kill)(const struct ip6_conntrack *i, void *data),
	void *data)
{
	return kill(i->ctrack, data);
}

/* Bring out ya dead! */
static struct ip6_conntrack_tuple_hash *
get_next_corpse(int (*kill)(const struct ip6_conntrack *i, void *data),
		void *data)
{
	struct ip6_conntrack_tuple_hash *h = NULL;
	unsigned int i;

	READ_LOCK(&ip6_conntrack_lock);
	for (i = 0; !h && i < ip6_conntrack_htable_size; i++) {
		h = LIST_FIND(&ip6_conntrack_hash[i], do_kill,
			      struct ip6_conntrack_tuple_hash *, kill, data);
	}
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip6_conntrack_lock);

	return h;
}

void
ip6_ct_selective_cleanup(int (*kill)(const struct ip6_conntrack *i, void *data),
			void *data)
{
	struct ip6_conntrack_tuple_hash *h;

	/* This is order n^2, by the way. */
	while ((h = get_next_corpse(kill, data)) != NULL) {
		/* Time to push up daises... */
		if (del_timer(&h->ctrack->timeout))
			death_by_timeout((unsigned long)h->ctrack);
		/* ... else the timer will get him soon. */

		ip6_conntrack_put(h->ctrack);
	}
}

/* Fast function for those who don't want to parse /proc (and I don't
   blame them). */
/* Reversing the socket's dst/src point of view gives us the reply
   mapping. */
static int
getorigdst(struct sock *sk, int optval, void *user, int *len)
{
	struct inet_opt *inet = inet_sk(sk);
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct ip6_conntrack_tuple_hash *h;
	struct ip6_conntrack_tuple tuple;

	memset(&tuple, 0, sizeof(tuple));
	ipv6_addr_copy(&tuple.src.ip, &np->rcv_saddr);
	ipv6_addr_copy(&tuple.dst.ip, &np->daddr);
	tuple.src.u.tcp.port = inet->sport;
	tuple.dst.u.tcp.port = inet->dport;
	tuple.dst.protonum = IPPROTO_TCP;

	/* We only do TCP at the moment: is there a better way? */
	if (strcmp(sk->sk_prot->name, "TCP")) {
		DEBUGP("IPV6_NF_ORIGINAL_DST: Not a TCP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int) *len < sizeof(struct sockaddr_in)) {
		DEBUGP("IPV6_NF_ORIGINAL_DST: len %u not %u\n",
		       *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = ip6_conntrack_find_get(&tuple, NULL);
	if (h) {
		struct sockaddr_in6 sin;

		sin.sin6_family = AF_INET6;
		sin.sin6_port = h->ctrack->tuplehash[IP6_CT_DIR_ORIGINAL]
				.tuple.dst.u.tcp.port;
		ipv6_addr_copy(&sin.sin6_addr,
			       &h->ctrack->tuplehash[IP6_CT_DIR_ORIGINAL]
				.tuple.dst.ip);

		DEBUGP("IPV6_NF_ORIGINAL_DST: %x:%x:%x:%x:%x:%x:%x:%x %u\n",
		       NIP6(sin.sin6_addr), ntohs(sin.sin6_port));
		ip6_conntrack_put(h->ctrack);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	DEBUGP("IPV6_NF_ORIGINAL_DST: Can't find %x:%x:%x:%x:%x:%x:%x:%x/%u-%x:%x:%x:%x:%x:%x:%x:%x/%u.\n",
	       NIP6(tuple.src.ip), ntohs(tuple.src.u.tcp.port),
	       NIP6(tuple.dst.ip), ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

static struct nf_sockopt_ops so_getorigdst = {
	.pf		= PF_INET6,
	.get_optmin	= IPV6_NF_ORIGINAL_DST,
	.get_optmax	= IPV6_NF_ORIGINAL_DST+1,
	.get		= &getorigdst,
};

#define NET_IP6_CONNTRACK_MAX 2089
#define NET_IP6_CONNTRACK_MAX_NAME "ip6_conntrack_max"

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *ip6_conntrack_sysctl_header;

static ctl_table ip6_conntrack_table[] = {
	{
		.ctl_name	= NET_IP6_CONNTRACK_MAX,
		.procname	= NET_IP6_CONNTRACK_MAX_NAME,
		.data		= &ip6_conntrack_max,
		.maxlen		= sizeof(ip6_conntrack_max),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
	},
 	{ .ctl_name = 0 }
};

static ctl_table ip6_conntrack_dir_table[] = {
	{
		.ctl_name	= NET_IPV6,
		.procname	= "ipv6", NULL,
		.mode		= 0555,
		.child		= ip6_conntrack_table
	},
	{ .ctl_name = 0 }
};

static ctl_table ip6_conntrack_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= ip6_conntrack_dir_table
	},
	{ .ctl_name = 0 }
};
#endif /*CONFIG_SYSCTL*/

static int kill_all(const struct ip6_conntrack *i, void *data)
{
	return 1;
}

/* Mishearing the voices in his head, our hero wonders how he's
   supposed to kill the mall. */
void ip6_conntrack_cleanup(void)
{
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(ip6_conntrack_sysctl_header);
#endif
	ip6_ct_attach = NULL;
	/* This makes sure all current packets have passed through
           netfilter framework.  Roll on, two-stage module
           delete... */
	synchronize_net();
 
 i_see_dead_people:
	ip6_ct_selective_cleanup(kill_all, NULL);
	if (atomic_read(&ip6_conntrack_count) != 0) {
		schedule();
		goto i_see_dead_people;
	}

	kmem_cache_destroy(ip6_conntrack_cachep);
	vfree(ip6_conntrack_hash);
	nf_unregister_sockopt(&so_getorigdst);
}

static int hashsize = 0;
MODULE_PARM(hashsize, "i");

int __init ip6_conntrack_init(void)
{
	unsigned int i;
	int ret;

	/* Idea from tcp.c: use 1/16384 of memory.  On i386: 32MB
	 * machine has 256 buckets.  >= 1GB machines have 8192 buckets. */
 	if (hashsize) {
 		ip6_conntrack_htable_size = hashsize;
 	} else {
		ip6_conntrack_htable_size
			= (((num_physpages << PAGE_SHIFT) / 16384)
			   / sizeof(struct list_head));
		if (num_physpages > (1024 * 1024 * 1024 / PAGE_SIZE))
			ip6_conntrack_htable_size = 8192;
		if (ip6_conntrack_htable_size < 16)
			ip6_conntrack_htable_size = 16;
	}
	ip6_conntrack_max = 8 * ip6_conntrack_htable_size;

	printk("ip6_conntrack version %s (%u buckets, %d max)"
	       " - %Zd bytes per conntrack\n", IP6_CONNTRACK_VERSION,
	       ip6_conntrack_htable_size, ip6_conntrack_max,
	       sizeof(struct ip6_conntrack));

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret != 0) {
		printk(KERN_ERR "Unable to register netfilter socket option\n");
		return ret;
	}

	ip6_conntrack_hash = vmalloc(sizeof(struct list_head)
				    * ip6_conntrack_htable_size);
	if (!ip6_conntrack_hash) {
		printk(KERN_ERR "Unable to create ip6_conntrack_hash\n");
		goto err_unreg_sockopt;
	}

	ip6_conntrack_cachep = kmem_cache_create("ip6_conntrack",
	                                        sizeof(struct ip6_conntrack), 0,
	                                        SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ip6_conntrack_cachep) {
		printk(KERN_ERR "Unable to create ip6_conntrack slab cache\n");
		goto err_free_hash;
	}
	/* Don't NEED lock here, but good form anyway. */
	WRITE_LOCK(&ip6_conntrack_lock);
	/* Sew in builtin protocols. */
	list_append(&ip6_protocol_list, &ip6_conntrack_protocol_tcp);
	list_append(&ip6_protocol_list, &ip6_conntrack_protocol_udp);
	list_append(&ip6_protocol_list, &ip6_conntrack_protocol_icmpv6);
	WRITE_UNLOCK(&ip6_conntrack_lock);

	for (i = 0; i < ip6_conntrack_htable_size; i++)
		INIT_LIST_HEAD(&ip6_conntrack_hash[i]);

/* This is fucking braindead.  There is NO WAY of doing this without
   the CONFIG_SYSCTL unless you don't want to detect errors.
   Grrr... --RR */
#ifdef CONFIG_SYSCTL
	ip6_conntrack_sysctl_header
		= register_sysctl_table(ip6_conntrack_root_table, 0);
	if (ip6_conntrack_sysctl_header == NULL) {
		goto err_free_ct_cachep;
	}
#endif /*CONFIG_SYSCTL*/

	/* For use by ip6t_REJECT */
	ip6_ct_attach = ip6_conntrack_attach;
	return ret;

#ifdef CONFIG_SYSCTL
err_free_ct_cachep:
	kmem_cache_destroy(ip6_conntrack_cachep);
#endif /*CONFIG_SYSCTL*/
err_free_hash:
	vfree(ip6_conntrack_hash);
err_unreg_sockopt:
	nf_unregister_sockopt(&so_getorigdst);

	return -ENOMEM;
}
