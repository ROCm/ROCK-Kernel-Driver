/* Connection state tracking for netfilter.  This is separated from,
   but required by, the NAT layer; it can also be used by an iptables
   extension. */

/* (c) 1999 Paul `Rusty' Russell.  Licenced under the GNU General
   Public Licence. */

#ifdef MODULE
#define __NO_VERSION__
#endif
#include <linux/version.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/brlock.h>
#include <net/checksum.h>
#include <linux/stddef.h>
#include <linux/sysctl.h>
#include <linux/slab.h>
/* For ERR_PTR().  Yeah, I know... --RR */
#include <linux/fs.h>

/* This rwlock protects the main hash table, protocol/helper/expected
   registrations, conntrack timers*/
#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_conntrack_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

DECLARE_RWLOCK(ip_conntrack_lock);

void (*ip_conntrack_destroyed)(struct ip_conntrack *conntrack) = NULL;
LIST_HEAD(expect_list);
LIST_HEAD(protocol_list);
static LIST_HEAD(helpers);
unsigned int ip_conntrack_htable_size = 0;
static int ip_conntrack_max = 0;
static atomic_t ip_conntrack_count = ATOMIC_INIT(0);
struct list_head *ip_conntrack_hash;
static kmem_cache_t *ip_conntrack_cachep;

extern struct ip_conntrack_protocol ip_conntrack_generic_protocol;

static inline int proto_cmpfn(const struct ip_conntrack_protocol *curr,
			      u_int8_t protocol)
{
	return protocol == curr->proto;
}

struct ip_conntrack_protocol *__find_proto(u_int8_t protocol)
{
	struct ip_conntrack_protocol *p;

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	p = LIST_FIND(&protocol_list, proto_cmpfn,
		      struct ip_conntrack_protocol *, protocol);
	if (!p)
		p = &ip_conntrack_generic_protocol;

	return p;
}

struct ip_conntrack_protocol *find_proto(u_int8_t protocol)
{
	struct ip_conntrack_protocol *p;

	READ_LOCK(&ip_conntrack_lock);
	p = __find_proto(protocol);
	READ_UNLOCK(&ip_conntrack_lock);
	return p;
}

static inline void ip_conntrack_put(struct ip_conntrack *ct)
{
	IP_NF_ASSERT(ct);
	IP_NF_ASSERT(ct->infos[0].master);
	/* nf_conntrack_put wants to go via an info struct, so feed it
           one at random. */
	nf_conntrack_put(&ct->infos[0]);
}

static inline u_int32_t
hash_conntrack(const struct ip_conntrack_tuple *tuple)
{
#if 0
	dump_tuple(tuple);
#endif
	/* ntohl because more differences in low bits. */
	/* To ensure that halves of the same connection don't hash
	   clash, we add the source per-proto again. */
	return (ntohl(tuple->src.ip + tuple->dst.ip
		     + tuple->src.u.all + tuple->dst.u.all
		     + tuple->dst.protonum)
		+ ntohs(tuple->src.u.all))
		% ip_conntrack_htable_size;
}

inline int
get_tuple(const struct iphdr *iph, size_t len,
	  struct ip_conntrack_tuple *tuple,
	  struct ip_conntrack_protocol *protocol)
{
	int ret;

	/* Never happen */
	if (iph->frag_off & htons(IP_OFFSET)) {
		printk("ip_conntrack_core: Frag of proto %u.\n",
		       iph->protocol);
		return 0;
	}
	/* Guarantee 8 protocol bytes: if more wanted, use len param */
	else if (iph->ihl * 4 + 8 > len)
		return 0;

	tuple->src.ip = iph->saddr;
	tuple->dst.ip = iph->daddr;
	tuple->dst.protonum = iph->protocol;

	ret = protocol->pkt_to_tuple((u_int32_t *)iph + iph->ihl,
				     len - 4*iph->ihl,
				     tuple);
	return ret;
}

static int
invert_tuple(struct ip_conntrack_tuple *inverse,
	     const struct ip_conntrack_tuple *orig,
	     const struct ip_conntrack_protocol *protocol)
{
	inverse->src.ip = orig->dst.ip;
	inverse->dst.ip = orig->src.ip;
	inverse->dst.protonum = orig->dst.protonum;

	return protocol->invert_tuple(inverse, orig);
}

static void
clean_from_lists(struct ip_conntrack *ct)
{
	MUST_BE_WRITE_LOCKED(&ip_conntrack_lock);
	/* Remove from both hash lists: must not NULL out next ptrs,
           otherwise we'll look unconfirmed.  Fortunately, LIST_DELETE
           doesn't do this. --RR */
	LIST_DELETE(&ip_conntrack_hash
		    [hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple)],
		    &ct->tuplehash[IP_CT_DIR_ORIGINAL]);
	LIST_DELETE(&ip_conntrack_hash
		    [hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple)],
		    &ct->tuplehash[IP_CT_DIR_REPLY]);
	/* If our expected is in the list, take it out. */
	if (ct->expected.expectant) {
		IP_NF_ASSERT(list_inlist(&expect_list, &ct->expected));
		IP_NF_ASSERT(ct->expected.expectant == ct);
		LIST_DELETE(&expect_list, &ct->expected);
	}
}

static void
destroy_conntrack(struct nf_conntrack *nfct)
{
	struct ip_conntrack *ct = (struct ip_conntrack *)nfct;

	IP_NF_ASSERT(atomic_read(&nfct->use) == 0);
	IP_NF_ASSERT(!timer_pending(&ct->timeout));

	if (ct->master.master)
		nf_conntrack_put(&ct->master);

	if (ip_conntrack_destroyed)
		ip_conntrack_destroyed(ct);
	kmem_cache_free(ip_conntrack_cachep, ct);
	atomic_dec(&ip_conntrack_count);
}

static void death_by_timeout(unsigned long ul_conntrack)
{
	struct ip_conntrack *ct = (void *)ul_conntrack;

	WRITE_LOCK(&ip_conntrack_lock);
	clean_from_lists(ct);
	WRITE_UNLOCK(&ip_conntrack_lock);
	ip_conntrack_put(ct);
}

static inline int
conntrack_tuple_cmp(const struct ip_conntrack_tuple_hash *i,
		    const struct ip_conntrack_tuple *tuple,
		    const struct ip_conntrack *ignored_conntrack)
{
	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	return i->ctrack != ignored_conntrack
		&& ip_ct_tuple_equal(tuple, &i->tuple);
}

static struct ip_conntrack_tuple_hash *
__ip_conntrack_find(const struct ip_conntrack_tuple *tuple,
		    const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);
	h = LIST_FIND(&ip_conntrack_hash[hash_conntrack(tuple)],
		      conntrack_tuple_cmp,
		      struct ip_conntrack_tuple_hash *,
		      tuple, ignored_conntrack);
	return h;
}

/* Find a connection corresponding to a tuple. */
struct ip_conntrack_tuple_hash *
ip_conntrack_find_get(const struct ip_conntrack_tuple *tuple,
		      const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	READ_LOCK(&ip_conntrack_lock);
	h = __ip_conntrack_find(tuple, ignored_conntrack);
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip_conntrack_lock);

	return h;
}

static inline struct ip_conntrack *
__ip_conntrack_get(struct nf_ct_info *nfct, enum ip_conntrack_info *ctinfo)
{
	struct ip_conntrack *ct
		= (struct ip_conntrack *)nfct->master;

	/* ctinfo is the index of the nfct inside the conntrack */
	*ctinfo = nfct - ct->infos;
	IP_NF_ASSERT(*ctinfo >= 0 && *ctinfo < IP_CT_NUMBER);
	return ct;
}

/* Return conntrack and conntrack_info given skb->nfct->master */
struct ip_conntrack *
ip_conntrack_get(struct sk_buff *skb, enum ip_conntrack_info *ctinfo)
{
	if (skb->nfct) 
		return __ip_conntrack_get(skb->nfct, ctinfo);
	return NULL;
}

/* Confirm a connection given skb->nfct; places it in hash table */
int
__ip_conntrack_confirm(struct nf_ct_info *nfct)
{
	unsigned int hash, repl_hash;
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;

	ct = __ip_conntrack_get(nfct, &ctinfo);

	/* ipt_REJECT uses ip_conntrack_attach to attach related
	   ICMP/TCP RST packets in other direction.  Actual packet
	   which created connection will be IP_CT_NEW or for an
	   expected connection, IP_CT_RELATED. */
	if (CTINFO2DIR(ctinfo) != IP_CT_DIR_ORIGINAL)
		return NF_ACCEPT;

	hash = hash_conntrack(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
	repl_hash = hash_conntrack(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);

	/* We're not in hash table, and we refuse to set up related
	   connections for unconfirmed conns.  But packet copies and
	   REJECT will give spurious warnings here. */
	/* IP_NF_ASSERT(atomic_read(&ct->ct_general.use) == 1); */

	/* No external references means noone else could have
           confirmed us. */
	IP_NF_ASSERT(!is_confirmed(ct));
	DEBUGP("Confirming conntrack %p\n", ct);

	WRITE_LOCK(&ip_conntrack_lock);
	/* See if there's one in the list already, including reverse:
           NAT could have grabbed it without realizing, since we're
           not in the hash.  If there is, we lost race. */
	if (!LIST_FIND(&ip_conntrack_hash[hash],
		       conntrack_tuple_cmp,
		       struct ip_conntrack_tuple_hash *,
		       &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple, NULL)
	    && !LIST_FIND(&ip_conntrack_hash[repl_hash],
			  conntrack_tuple_cmp,
			  struct ip_conntrack_tuple_hash *,
			  &ct->tuplehash[IP_CT_DIR_REPLY].tuple, NULL)) {
		list_prepend(&ip_conntrack_hash[hash],
			     &ct->tuplehash[IP_CT_DIR_ORIGINAL]);
		list_prepend(&ip_conntrack_hash[repl_hash],
			     &ct->tuplehash[IP_CT_DIR_REPLY]);
		/* Timer relative to confirmation time, not original
		   setting time, otherwise we'd get timer wrap in
		   wierd delay cases. */
		ct->timeout.expires += jiffies;
		add_timer(&ct->timeout);
		atomic_inc(&ct->ct_general.use);
		WRITE_UNLOCK(&ip_conntrack_lock);
		return NF_ACCEPT;
	}

	WRITE_UNLOCK(&ip_conntrack_lock);
	return NF_DROP;
}

/* Returns true if a connection correspondings to the tuple (required
   for NAT). */
int
ip_conntrack_tuple_taken(const struct ip_conntrack_tuple *tuple,
			 const struct ip_conntrack *ignored_conntrack)
{
	struct ip_conntrack_tuple_hash *h;

	READ_LOCK(&ip_conntrack_lock);
	h = __ip_conntrack_find(tuple, ignored_conntrack);
	READ_UNLOCK(&ip_conntrack_lock);

	return h != NULL;
}

/* Returns conntrack if it dealt with ICMP, and filled in skb fields */
struct ip_conntrack *
icmp_error_track(struct sk_buff *skb,
		 enum ip_conntrack_info *ctinfo,
		 unsigned int hooknum)
{
	const struct iphdr *iph;
	struct icmphdr *hdr;
	struct ip_conntrack_tuple innertuple, origtuple;
	struct iphdr *inner;
	size_t datalen;
	struct ip_conntrack_protocol *innerproto;
	struct ip_conntrack_tuple_hash *h;

	IP_NF_ASSERT(iph->protocol == IPPROTO_ICMP);
	IP_NF_ASSERT(skb->nfct == NULL);

	iph = skb->nh.iph;
	hdr = (struct icmphdr *)((u_int32_t *)iph + iph->ihl);
	inner = (struct iphdr *)(hdr + 1);
	datalen = skb->len - iph->ihl*4 - sizeof(*hdr);

	if (skb->len < iph->ihl * 4 + sizeof(*hdr) + sizeof(*iph)) {
		DEBUGP("icmp_error_track: too short\n");
		return NULL;
	}

	if (hdr->type != ICMP_DEST_UNREACH
	    && hdr->type != ICMP_SOURCE_QUENCH
	    && hdr->type != ICMP_TIME_EXCEEDED
	    && hdr->type != ICMP_PARAMETERPROB
	    && hdr->type != ICMP_REDIRECT)
		return NULL;

	/* Ignore ICMP's containing fragments (shouldn't happen) */
	if (inner->frag_off & htons(IP_OFFSET)) {
		DEBUGP("icmp_error_track: fragment of proto %u\n",
		       inner->protocol);
		return NULL;
	}

	/* Ignore it if the checksum's bogus. */
	if (ip_compute_csum((unsigned char *)hdr, sizeof(*hdr) + datalen)) {
		DEBUGP("icmp_error_track: bad csum\n");
		return NULL;
	}

	innerproto = find_proto(inner->protocol);
	/* Are they talking about one of our connections? */
	if (inner->ihl * 4 + 8 > datalen
	    || !get_tuple(inner, datalen, &origtuple, innerproto)) {
		DEBUGP("icmp_error: ! get_tuple p=%u (%u*4+%u dlen=%u)\n",
		       inner->protocol, inner->ihl, 8,
		       datalen);
		return NULL;
	}

	/* Ordinarily, we'd expect the inverted tupleproto, but it's
	   been preserved inside the ICMP. */
	if (!invert_tuple(&innertuple, &origtuple, innerproto)) {
		DEBUGP("icmp_error_track: Can't invert tuple\n");
		return NULL;
	}

	*ctinfo = IP_CT_RELATED;

	h = ip_conntrack_find_get(&innertuple, NULL);
	if (!h) {
		/* Locally generated ICMPs will match inverted if they
		   haven't been SNAT'ed yet */
		/* FIXME: NAT code has to handle half-done double NAT --RR */
		if (hooknum == NF_IP_LOCAL_OUT)
			h = ip_conntrack_find_get(&origtuple, NULL);

		if (!h) {
			DEBUGP("icmp_error_track: no match\n");
			return NULL;
		}
		/* Reverse direction from that found */
		if (DIRECTION(h) != IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	} else {
		if (DIRECTION(h) == IP_CT_DIR_REPLY)
			*ctinfo += IP_CT_IS_REPLY;
	}

	/* Update skb to refer to this connection */
	skb->nfct = &h->ctrack->infos[*ctinfo];
	return h->ctrack;
}

/* There's a small race here where we may free a just-assured
   connection.  Too bad: we're in trouble anyway. */
static inline int unreplied(const struct ip_conntrack_tuple_hash *i)
{
	return !(i->ctrack->status & IPS_ASSURED);
}

static int early_drop(struct list_head *chain)
{
	/* Traverse backwards: gives us oldest, which is roughly LRU */
	struct ip_conntrack_tuple_hash *h;
	int dropped = 0;

	READ_LOCK(&ip_conntrack_lock);
	h = LIST_FIND(chain, unreplied, struct ip_conntrack_tuple_hash *);
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip_conntrack_lock);

	if (!h)
		return dropped;

	if (del_timer(&h->ctrack->timeout)) {
		death_by_timeout((unsigned long)h->ctrack);
		dropped = 1;
	}
	ip_conntrack_put(h->ctrack);
	return dropped;
}

static inline int helper_cmp(const struct ip_conntrack_helper *i,
			     const struct ip_conntrack_tuple *rtuple)
{
	return ip_ct_tuple_mask_cmp(rtuple, &i->tuple, &i->mask);
}

/* Compare parts depending on mask. */
static inline int expect_cmp(const struct ip_conntrack_expect *i,
			     const struct ip_conntrack_tuple *tuple)
{
	return ip_ct_tuple_mask_cmp(tuple, &i->tuple, &i->mask);
}

/* Allocate a new conntrack: we return -ENOMEM if classification
   failed due to stress.  Otherwise it really is unclassifiable. */
static struct ip_conntrack_tuple_hash *
init_conntrack(const struct ip_conntrack_tuple *tuple,
	       struct ip_conntrack_protocol *protocol,
	       struct sk_buff *skb)
{
	struct ip_conntrack *conntrack;
	struct ip_conntrack_tuple repl_tuple;
	size_t hash, repl_hash;
	struct ip_conntrack_expect *expected;
	int i;
	static unsigned int drop_next = 0;

	hash = hash_conntrack(tuple);

	if (ip_conntrack_max &&
	    atomic_read(&ip_conntrack_count) >= ip_conntrack_max) {
		/* Try dropping from random chain, or else from the
                   chain about to put into (in case they're trying to
                   bomb one hash chain). */
		if (drop_next >= ip_conntrack_htable_size)
			drop_next = 0;
		if (!early_drop(&ip_conntrack_hash[drop_next++])
		    && !early_drop(&ip_conntrack_hash[hash])) {
			if (net_ratelimit())
				printk(KERN_WARNING
				       "ip_conntrack: table full, dropping"
				       " packet.\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	if (!invert_tuple(&repl_tuple, tuple, protocol)) {
		DEBUGP("Can't invert tuple.\n");
		return NULL;
	}
	repl_hash = hash_conntrack(&repl_tuple);

	conntrack = kmem_cache_alloc(ip_conntrack_cachep, GFP_ATOMIC);
	if (!conntrack) {
		DEBUGP("Can't allocate conntrack.\n");
		return ERR_PTR(-ENOMEM);
	}

	memset(conntrack, 0, sizeof(struct ip_conntrack));
	atomic_set(&conntrack->ct_general.use, 1);
	conntrack->ct_general.destroy = destroy_conntrack;
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple = *tuple;
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].ctrack = conntrack;
	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = repl_tuple;
	conntrack->tuplehash[IP_CT_DIR_REPLY].ctrack = conntrack;
	for (i=0; i < IP_CT_NUMBER; i++)
		conntrack->infos[i].master = &conntrack->ct_general;

	if (!protocol->new(conntrack, skb->nh.iph, skb->len)) {
		kmem_cache_free(ip_conntrack_cachep, conntrack);
		return NULL;
	}
	/* Don't set timer yet: wait for confirmation */
	init_timer(&conntrack->timeout);
	conntrack->timeout.data = (unsigned long)conntrack;
	conntrack->timeout.function = death_by_timeout;

	/* Mark clearly that it's not in the hash table. */
	conntrack->tuplehash[IP_CT_DIR_ORIGINAL].list.next = NULL;

	/* Write lock required for deletion of expected.  Without
           this, a read-lock would do. */
	WRITE_LOCK(&ip_conntrack_lock);
	conntrack->helper = LIST_FIND(&helpers, helper_cmp,
				      struct ip_conntrack_helper *,
				      &repl_tuple);
	/* Need finding and deleting of expected ONLY if we win race */
	expected = LIST_FIND(&expect_list, expect_cmp,
			     struct ip_conntrack_expect *, tuple);
	/* If master is not in hash table yet (ie. packet hasn't left
	   this machine yet), how can other end know about expected?
	   Hence these are not the droids you are looking for (if
	   master ct never got confirmed, we'd hold a reference to it
	   and weird things would happen to future packets). */
	if (expected && is_confirmed(expected->expectant)) {
		/* Welcome, Mr. Bond.  We've been expecting you... */
		conntrack->status = IPS_EXPECTED;
		conntrack->master.master = &expected->expectant->ct_general;
		IP_NF_ASSERT(conntrack->master.master);
		LIST_DELETE(&expect_list, expected);
		expected->expectant = NULL;
		nf_conntrack_get(&conntrack->master);
	}
	atomic_inc(&ip_conntrack_count);
	WRITE_UNLOCK(&ip_conntrack_lock);

	if (expected && expected->expectfn)
		expected->expectfn(conntrack);
	return &conntrack->tuplehash[IP_CT_DIR_ORIGINAL];
}

/* On success, returns conntrack ptr, sets skb->nfct and ctinfo */
static inline struct ip_conntrack *
resolve_normal_ct(struct sk_buff *skb,
		  struct ip_conntrack_protocol *proto,
		  int *set_reply,
		  unsigned int hooknum,
		  enum ip_conntrack_info *ctinfo)
{
	struct ip_conntrack_tuple tuple;
	struct ip_conntrack_tuple_hash *h;

	IP_NF_ASSERT((skb->nh.iph->frag_off & htons(IP_OFFSET)) == 0);

	if (!get_tuple(skb->nh.iph, skb->len, &tuple, proto))
		return NULL;

	/* look for tuple match */
	h = ip_conntrack_find_get(&tuple, NULL);
	if (!h) {
		h = init_conntrack(&tuple, proto, skb);
		if (!h)
			return NULL;
		if (IS_ERR(h))
			return (void *)h;
	}

	/* It exists; we have (non-exclusive) reference. */
	if (DIRECTION(h) == IP_CT_DIR_REPLY) {
		*ctinfo = IP_CT_ESTABLISHED + IP_CT_IS_REPLY;
		/* Please set reply bit if this packet OK */
		*set_reply = 1;
	} else {
		/* Once we've had two way comms, always ESTABLISHED. */
		if (h->ctrack->status & IPS_SEEN_REPLY) {
			DEBUGP("ip_conntrack_in: normal packet for %p\n",
			       h->ctrack);
		        *ctinfo = IP_CT_ESTABLISHED;
		} else if (h->ctrack->status & IPS_EXPECTED) {
			DEBUGP("ip_conntrack_in: related packet for %p\n",
			       h->ctrack);
			*ctinfo = IP_CT_RELATED;
		} else {
			DEBUGP("ip_conntrack_in: new packet for %p\n",
			       h->ctrack);
			*ctinfo = IP_CT_NEW;
		}
		*set_reply = 0;
	}
	skb->nfct = &h->ctrack->infos[*ctinfo];
	return h->ctrack;
}

/* Netfilter hook itself. */
unsigned int ip_conntrack_in(unsigned int hooknum,
			     struct sk_buff **pskb,
			     const struct net_device *in,
			     const struct net_device *out,
			     int (*okfn)(struct sk_buff *))
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;
	struct ip_conntrack_protocol *proto;
	int set_reply;
	int ret;

	/* FIXME: Do this right please. --RR */
	(*pskb)->nfcache |= NFC_UNKNOWN;

/* Doesn't cover locally-generated broadcast, so not worth it. */
#if 0
	/* Ignore broadcast: no `connection'. */
	if ((*pskb)->pkt_type == PACKET_BROADCAST) {
		printk("Broadcast packet!\n");
		return NF_ACCEPT;
	} else if (((*pskb)->nh.iph->daddr & htonl(0x000000FF)) 
		   == htonl(0x000000FF)) {
		printk("Should bcast: %u.%u.%u.%u->%u.%u.%u.%u (sk=%p, ptype=%u)\n",
		       NIPQUAD((*pskb)->nh.iph->saddr),
		       NIPQUAD((*pskb)->nh.iph->daddr),
		       (*pskb)->sk, (*pskb)->pkt_type);
	}
#endif

	/* Previously seen (loopback)?  Ignore.  Do this before
           fragment check. */
	if ((*pskb)->nfct)
		return NF_ACCEPT;

	/* Gather fragments. */
	if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
		*pskb = ip_ct_gather_frags(*pskb);
		if (!*pskb)
			return NF_STOLEN;
	}

	proto = find_proto((*pskb)->nh.iph->protocol);

	/* It may be an icmp error... */
	if ((*pskb)->nh.iph->protocol == IPPROTO_ICMP 
	    && icmp_error_track(*pskb, &ctinfo, hooknum))
		return NF_ACCEPT;

	if (!(ct = resolve_normal_ct(*pskb, proto,&set_reply,hooknum,&ctinfo)))
		/* Not valid part of a connection */
		return NF_ACCEPT;

	if (IS_ERR(ct))
		/* Too stressed to deal. */
		return NF_DROP;

	IP_NF_ASSERT((*pskb)->nfct);

	ret = proto->packet(ct, (*pskb)->nh.iph, (*pskb)->len, ctinfo);
	if (ret == -1) {
		/* Invalid */
		nf_conntrack_put((*pskb)->nfct);
		(*pskb)->nfct = NULL;
		return NF_ACCEPT;
	}

	if (ret != NF_DROP && ct->helper) {
		ret = ct->helper->help((*pskb)->nh.iph, (*pskb)->len,
				       ct, ctinfo);
		if (ret == -1) {
			/* Invalid */
			nf_conntrack_put((*pskb)->nfct);
			(*pskb)->nfct = NULL;
			return NF_ACCEPT;
		}
	}
	if (set_reply)
		set_bit(IPS_SEEN_REPLY_BIT, &ct->status);

	return ret;
}

int invert_tuplepr(struct ip_conntrack_tuple *inverse,
		   const struct ip_conntrack_tuple *orig)
{
	return invert_tuple(inverse, orig, find_proto(orig->dst.protonum));
}

static void unexpect_related(struct ip_conntrack *related_to)
{
	MUST_BE_WRITE_LOCKED(&ip_conntrack_lock);
	list_del(&related_to->expected.list);
	related_to->expected.expectant = NULL;
}

/* Would two expected things clash? */
static inline int expect_clash(const struct ip_conntrack_expect *i,
			       const struct ip_conntrack_expect *new)
{
	/* Part covered by intersection of masks must be unequal,
           otherwise they clash */
	struct ip_conntrack_tuple intersect_mask
		= { { i->mask.src.ip & new->mask.src.ip,
		      { i->mask.src.u.all & new->mask.src.u.all } },
		    { i->mask.dst.ip & new->mask.dst.ip,
		      { i->mask.dst.u.all & new->mask.dst.u.all },
		      i->mask.dst.protonum & new->mask.dst.protonum } };

	return ip_ct_tuple_mask_cmp(&i->tuple, &new->tuple, &intersect_mask);
}

/* Add a related connection. */
int ip_conntrack_expect_related(struct ip_conntrack *related_to,
				const struct ip_conntrack_tuple *tuple,
				const struct ip_conntrack_tuple *mask,
				int (*expectfn)(struct ip_conntrack *))
{
	WRITE_LOCK(&ip_conntrack_lock);
	if (related_to->expected.expectant)
		unexpect_related(related_to);

	related_to->expected.tuple = *tuple;
	related_to->expected.mask = *mask;
	related_to->expected.expectfn = expectfn;

	if (LIST_FIND(&expect_list, expect_clash,
		      struct ip_conntrack_expect *, &related_to->expected)) {
		WRITE_UNLOCK(&ip_conntrack_lock);
		return -EBUSY;
	}

	list_prepend(&expect_list, &related_to->expected);
	related_to->expected.expectant = related_to;
	WRITE_UNLOCK(&ip_conntrack_lock);

	return 0;
}

void ip_conntrack_unexpect_related(struct ip_conntrack *related_to)
{
	WRITE_LOCK(&ip_conntrack_lock);
	unexpect_related(related_to);
	WRITE_UNLOCK(&ip_conntrack_lock);
}
	
/* Alter reply tuple (maybe alter helper).  If it's already taken,
   return 0 and don't do alteration. */
int ip_conntrack_alter_reply(struct ip_conntrack *conntrack,
			     const struct ip_conntrack_tuple *newreply)
{
	WRITE_LOCK(&ip_conntrack_lock);
	if (__ip_conntrack_find(newreply, conntrack)) {
		WRITE_UNLOCK(&ip_conntrack_lock);
		return 0;
	}
	/* Should be unconfirmed, so not in hash table yet */
	IP_NF_ASSERT(!is_confirmed(conntrack));

	DEBUGP("Altering reply tuple of %p to ", conntrack);
	DUMP_TUPLE(newreply);

	conntrack->tuplehash[IP_CT_DIR_REPLY].tuple = *newreply;
	conntrack->helper = LIST_FIND(&helpers, helper_cmp,
				      struct ip_conntrack_helper *,
				      newreply);
	WRITE_UNLOCK(&ip_conntrack_lock);
	return 1;
}

int ip_conntrack_helper_register(struct ip_conntrack_helper *me)
{
	MOD_INC_USE_COUNT;

	WRITE_LOCK(&ip_conntrack_lock);
	list_prepend(&helpers, me);
	WRITE_UNLOCK(&ip_conntrack_lock);

	return 0;
}

static inline int unhelp(struct ip_conntrack_tuple_hash *i,
			 const struct ip_conntrack_helper *me)
{
	if (i->ctrack->helper == me) {
		i->ctrack->helper = NULL;
		/* Get rid of any expected. */
		if (i->ctrack->expected.expectant) {
			IP_NF_ASSERT(i->ctrack->expected.expectant
				     == i->ctrack);
			LIST_DELETE(&expect_list, &i->ctrack->expected);
			i->ctrack->expected.expectant = NULL;
		}
	}
	return 0;
}

void ip_conntrack_helper_unregister(struct ip_conntrack_helper *me)
{
	unsigned int i;

	/* Need write lock here, to delete helper. */
	WRITE_LOCK(&ip_conntrack_lock);
	LIST_DELETE(&helpers, me);

	/* Get rid of expecteds, set helpers to NULL. */
	for (i = 0; i < ip_conntrack_htable_size; i++)
		LIST_FIND_W(&ip_conntrack_hash[i], unhelp,
			    struct ip_conntrack_tuple_hash *, me);
	WRITE_UNLOCK(&ip_conntrack_lock);

	/* Someone could be still looking at the helper in a bh. */
	br_write_lock_bh(BR_NETPROTO_LOCK);
	br_write_unlock_bh(BR_NETPROTO_LOCK);

	MOD_DEC_USE_COUNT;
}

/* Refresh conntrack for this many jiffies. */
void ip_ct_refresh(struct ip_conntrack *ct, unsigned long extra_jiffies)
{
	IP_NF_ASSERT(ct->timeout.data == (unsigned long)ct);

	WRITE_LOCK(&ip_conntrack_lock);
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
	WRITE_UNLOCK(&ip_conntrack_lock);
}

/* Returns new sk_buff, or NULL */
struct sk_buff *
ip_ct_gather_frags(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;
#ifdef CONFIG_NETFILTER_DEBUG
	unsigned int olddebug = skb->nf_debug;
#endif
	if (sk) {
		sock_hold(sk);
		skb_orphan(skb);
	}

	local_bh_disable(); 
	skb = ip_defrag(skb);
	local_bh_enable();

	if (!skb) {
		if (sk) sock_put(sk);
		return skb;
	} else if (skb_is_nonlinear(skb) && skb_linearize(skb, GFP_ATOMIC) != 0) {
		kfree_skb(skb);
		if (sk) sock_put(sk);
		return NULL;
	}

	if (sk) {
		skb_set_owner_w(skb, sk);
		sock_put(sk);
	}

	ip_send_check(skb->nh.iph);
	skb->nfcache |= NFC_ALTERED;
#ifdef CONFIG_NETFILTER_DEBUG
	/* Packet path as if nothing had happened. */
	skb->nf_debug = olddebug;
#endif
	return skb;
}

/* Used by ipt_REJECT. */
static void ip_conntrack_attach(struct sk_buff *nskb, struct nf_ct_info *nfct)
{
	struct ip_conntrack *ct;
	enum ip_conntrack_info ctinfo;

	ct = __ip_conntrack_get(nfct, &ctinfo);

	/* This ICMP is in reverse direction to the packet which
           caused it */
	if (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL)
		ctinfo = IP_CT_RELATED + IP_CT_IS_REPLY;
	else
		ctinfo = IP_CT_RELATED;

	/* Attach new skbuff, and increment count */
	nskb->nfct = &ct->infos[ctinfo];
	atomic_inc(&ct->ct_general.use);
}

static inline int
do_kill(const struct ip_conntrack_tuple_hash *i,
	int (*kill)(const struct ip_conntrack *i, void *data),
	void *data)
{
	return kill(i->ctrack, data);
}

/* Bring out ya dead! */
static struct ip_conntrack_tuple_hash *
get_next_corpse(int (*kill)(const struct ip_conntrack *i, void *data),
		void *data)
{
	struct ip_conntrack_tuple_hash *h = NULL;
	unsigned int i;

	READ_LOCK(&ip_conntrack_lock);
	for (i = 0; !h && i < ip_conntrack_htable_size; i++) {
		h = LIST_FIND(&ip_conntrack_hash[i], do_kill,
			      struct ip_conntrack_tuple_hash *, kill, data);
	}
	if (h)
		atomic_inc(&h->ctrack->ct_general.use);
	READ_UNLOCK(&ip_conntrack_lock);

	return h;
}

void
ip_ct_selective_cleanup(int (*kill)(const struct ip_conntrack *i, void *data),
			void *data)
{
	struct ip_conntrack_tuple_hash *h;

	/* This is order n^2, by the way. */
	while ((h = get_next_corpse(kill, data)) != NULL) {
		/* Time to push up daises... */
		if (del_timer(&h->ctrack->timeout))
			death_by_timeout((unsigned long)h->ctrack);
		/* ... else the timer will get him soon. */

		ip_conntrack_put(h->ctrack);
	}
}

/* Fast function for those who don't want to parse /proc (and I don't
   blame them). */
/* Reversing the socket's dst/src point of view gives us the reply
   mapping. */
static int
getorigdst(struct sock *sk, int optval, void *user, int *len)
{
	struct ip_conntrack_tuple_hash *h;
	struct ip_conntrack_tuple tuple = { { sk->rcv_saddr, { sk->sport } },
					    { sk->daddr, { sk->dport },
					      IPPROTO_TCP } };

	/* We only do TCP at the moment: is there a better way? */
	if (strcmp(sk->prot->name, "TCP") != 0) {
		DEBUGP("SO_ORIGINAL_DST: Not a TCP socket\n");
		return -ENOPROTOOPT;
	}

	if ((unsigned int) *len < sizeof(struct sockaddr_in)) {
		DEBUGP("SO_ORIGINAL_DST: len %u not %u\n",
		       *len, sizeof(struct sockaddr_in));
		return -EINVAL;
	}

	h = ip_conntrack_find_get(&tuple, NULL);
	if (h) {
		struct sockaddr_in sin;

		sin.sin_family = AF_INET;
		sin.sin_port = h->ctrack->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.u.tcp.port;
		sin.sin_addr.s_addr = h->ctrack->tuplehash[IP_CT_DIR_ORIGINAL]
			.tuple.dst.ip;

		DEBUGP("SO_ORIGINAL_DST: %u.%u.%u.%u %u\n",
		       NIPQUAD(sin.sin_addr.s_addr), ntohs(sin.sin_port));
		ip_conntrack_put(h->ctrack);
		if (copy_to_user(user, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		else
			return 0;
	}
	DEBUGP("SO_ORIGINAL_DST: Can't find %u.%u.%u.%u/%u-%u.%u.%u.%u/%u.\n",
	       NIPQUAD(tuple.src.ip), ntohs(tuple.src.u.tcp.port),
	       NIPQUAD(tuple.dst.ip), ntohs(tuple.dst.u.tcp.port));
	return -ENOENT;
}

static struct nf_sockopt_ops so_getorigdst
= { { NULL, NULL }, PF_INET,
    0, 0, NULL, /* Setsockopts */
    SO_ORIGINAL_DST, SO_ORIGINAL_DST+1, &getorigdst,
    0, NULL };

#define NET_IP_CONNTRACK_MAX 2089
#define NET_IP_CONNTRACK_MAX_NAME "ip_conntrack_max"

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *ip_conntrack_sysctl_header;

static ctl_table ip_conntrack_table[] = {
	{ NET_IP_CONNTRACK_MAX, NET_IP_CONNTRACK_MAX_NAME, &ip_conntrack_max,
	  sizeof(ip_conntrack_max), 0644,  NULL, proc_dointvec },
 	{ 0 }
};

static ctl_table ip_conntrack_dir_table[] = {
	{NET_IPV4, "ipv4", NULL, 0, 0555, ip_conntrack_table, 0, 0, 0, 0, 0},
	{ 0 }
};

static ctl_table ip_conntrack_root_table[] = {
	{CTL_NET, "net", NULL, 0, 0555, ip_conntrack_dir_table, 0, 0, 0, 0, 0},
	{ 0 }
};
#endif /*CONFIG_SYSCTL*/

static int kill_all(const struct ip_conntrack *i, void *data)
{
	return 1;
}

/* Mishearing the voices in his head, our hero wonders how he's
   supposed to kill the mall. */
void ip_conntrack_cleanup(void)
{
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(ip_conntrack_sysctl_header);
#endif
	ip_ct_attach = NULL;
	/* This makes sure all current packets have passed through
           netfilter framework.  Roll on, two-stage module
           delete... */
	br_write_lock_bh(BR_NETPROTO_LOCK);
	br_write_unlock_bh(BR_NETPROTO_LOCK);
 
 i_see_dead_people:
	ip_ct_selective_cleanup(kill_all, NULL);
	if (atomic_read(&ip_conntrack_count) != 0) {
		schedule();
		goto i_see_dead_people;
	}

	kmem_cache_destroy(ip_conntrack_cachep);
	vfree(ip_conntrack_hash);
	nf_unregister_sockopt(&so_getorigdst);
}

static int hashsize = 0;
MODULE_PARM(hashsize, "i");

int __init ip_conntrack_init(void)
{
	unsigned int i;
	int ret;

	/* Idea from tcp.c: use 1/16384 of memory.  On i386: 32MB
	 * machine has 256 buckets.  >= 1GB machines have 8192 buckets. */
 	if (hashsize) {
 		ip_conntrack_htable_size = hashsize;
 	} else {
		ip_conntrack_htable_size
			= (((num_physpages << PAGE_SHIFT) / 16384)
			   / sizeof(struct list_head));
		if (num_physpages > (1024 * 1024 * 1024 / PAGE_SIZE))
			ip_conntrack_htable_size = 8192;
		if (ip_conntrack_htable_size < 16)
			ip_conntrack_htable_size = 16;
	}
	ip_conntrack_max = 8 * ip_conntrack_htable_size;

	printk("ip_conntrack (%u buckets, %d max)\n",
	       ip_conntrack_htable_size, ip_conntrack_max);

	ret = nf_register_sockopt(&so_getorigdst);
	if (ret != 0)
		return ret;

	ip_conntrack_hash = vmalloc(sizeof(struct list_head)
				    * ip_conntrack_htable_size);
	if (!ip_conntrack_hash) {
		nf_unregister_sockopt(&so_getorigdst);
		return -ENOMEM;
	}

	ip_conntrack_cachep = kmem_cache_create("ip_conntrack",
	                                        sizeof(struct ip_conntrack), 0,
	                                        SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!ip_conntrack_cachep) {
		printk(KERN_ERR "Unable to create ip_conntrack slab cache\n");
		vfree(ip_conntrack_hash);
		nf_unregister_sockopt(&so_getorigdst);
		return -ENOMEM;
	}
	
	/* Don't NEED lock here, but good form anyway. */
	WRITE_LOCK(&ip_conntrack_lock);
	/* Sew in builtin protocols. */
	list_append(&protocol_list, &ip_conntrack_protocol_tcp);
	list_append(&protocol_list, &ip_conntrack_protocol_udp);
	list_append(&protocol_list, &ip_conntrack_protocol_icmp);
	WRITE_UNLOCK(&ip_conntrack_lock);

	for (i = 0; i < ip_conntrack_htable_size; i++)
		INIT_LIST_HEAD(&ip_conntrack_hash[i]);

/* This is fucking braindead.  There is NO WAY of doing this without
   the CONFIG_SYSCTL unless you don't want to detect errors.
   Grrr... --RR */
#ifdef CONFIG_SYSCTL
	ip_conntrack_sysctl_header
		= register_sysctl_table(ip_conntrack_root_table, 0);
	if (ip_conntrack_sysctl_header == NULL) {
		kmem_cache_destroy(ip_conntrack_cachep);
		vfree(ip_conntrack_hash);
		nf_unregister_sockopt(&so_getorigdst);
		return -ENOMEM;
	}
#endif /*CONFIG_SYSCTL*/

	/* For use by ipt_REJECT */
	ip_ct_attach = ip_conntrack_attach;
	return ret;
}
