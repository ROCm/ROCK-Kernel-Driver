/*
 * IPv6 fragment reassembly for connection tracking
 * Linux INET6 implementation
 *
 * Copyright (C)2003 USAGI/WIDE Project
 *
 * Authors:
 *	Yasuyuki Kozakai	<yasuyuki.kozakai@toshiba.co.jp>
 *
 * Based on: net/ipv6/reassembly.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/jiffies.h>
#include <linux/net.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>
#include <linux/jhash.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/rawv6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <linux/sysctl.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv6.h>
#include <linux/kernel.h>
#include <linux/module.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

#define IP6CT_FRAGS_HIGH_THRESH 262144 /* == 256*1024 */
#define IP6CT_FRAGS_LOW_THRESH 196608  /* == 192*1024 */
#define IP6CT_FRAGS_TIMEOUT IPV6_FRAG_TIMEOUT

static int sysctl_ip6_ct_frag_high_thresh = 256*1024;
static int sysctl_ip6_ct_frag_low_thresh = 192*1024;
static int sysctl_ip6_ct_frag_time = IPV6_FRAG_TIMEOUT;

struct ip6ct_frag_skb_cb
{
	struct inet6_skb_parm	h;
	int			offset;
	struct sk_buff		*orig;
};

#define IP6CT_FRAG6_CB(skb)	((struct ip6ct_frag_skb_cb*)((skb)->cb))


/*
 *	Equivalent of ipv4 struct ipq
 */

struct ip6ct_frag_queue
{
	struct ip6ct_frag_queue	*next;
	struct list_head lru_list;		/* lru list member	*/

	__u32			id;		/* fragment id		*/
	struct in6_addr		saddr;
	struct in6_addr		daddr;

	spinlock_t		lock;
	atomic_t		refcnt;
	struct timer_list	timer;		/* expire timer		*/
	struct sk_buff		*fragments;
	int			len;
	int			meat;
	struct timeval		stamp;
	unsigned int		csum;
	__u8			last_in;	/* has first/last segment arrived? */
#define COMPLETE		4
#define FIRST_IN		2
#define LAST_IN			1
	__u16			nhoffset;
	struct ip6ct_frag_queue	**pprev;
};

/* Hash table. */

#define IP6CT_Q_HASHSZ	64

static struct ip6ct_frag_queue *ip6_ct_frag_hash[IP6CT_Q_HASHSZ];
static rwlock_t ip6_ct_frag_lock = RW_LOCK_UNLOCKED;
static u32 ip6_ct_frag_hash_rnd;
static LIST_HEAD(ip6_ct_frag_lru_list);
int ip6_ct_frag_nqueues = 0;

static __inline__ void __fq_unlink(struct ip6ct_frag_queue *fq)
{
	if(fq->next)
		fq->next->pprev = fq->pprev;
	*fq->pprev = fq->next;
	list_del(&fq->lru_list);
	ip6_ct_frag_nqueues--;
}

static __inline__ void fq_unlink(struct ip6ct_frag_queue *fq)
{
	write_lock(&ip6_ct_frag_lock);
	__fq_unlink(fq);
	write_unlock(&ip6_ct_frag_lock);
}

static unsigned int ip6qhashfn(u32 id, struct in6_addr *saddr,
			       struct in6_addr *daddr)
{
	u32 a, b, c;

	a = saddr->s6_addr32[0];
	b = saddr->s6_addr32[1];
	c = saddr->s6_addr32[2];

	a += JHASH_GOLDEN_RATIO;
	b += JHASH_GOLDEN_RATIO;
	c += ip6_ct_frag_hash_rnd;
	__jhash_mix(a, b, c);

	a += saddr->s6_addr32[3];
	b += daddr->s6_addr32[0];
	c += daddr->s6_addr32[1];
	__jhash_mix(a, b, c);

	a += daddr->s6_addr32[2];
	b += daddr->s6_addr32[3];
	c += id;
	__jhash_mix(a, b, c);

	return c & (IP6CT_Q_HASHSZ - 1);
}

static struct timer_list ip6_ct_frag_secret_timer;
int sysctl_ip6_ct_frag_secret_interval = 10 * 60 * HZ;

static void ip6_ct_frag_secret_rebuild(unsigned long dummy)
{
	unsigned long now = jiffies;
	int i;

	write_lock(&ip6_ct_frag_lock);
	get_random_bytes(&ip6_ct_frag_hash_rnd, sizeof(u32));
	for (i = 0; i < IP6CT_Q_HASHSZ; i++) {
		struct ip6ct_frag_queue *q;

		q = ip6_ct_frag_hash[i];
		while (q) {
			struct ip6ct_frag_queue *next = q->next;
			unsigned int hval = ip6qhashfn(q->id,
						       &q->saddr,
						       &q->daddr);

			if (hval != i) {
				/* Unlink. */
				if (q->next)
					q->next->pprev = q->pprev;
				*q->pprev = q->next;

				/* Relink to new hash chain. */
				if ((q->next = ip6_ct_frag_hash[hval]) != NULL)
					q->next->pprev = &q->next;
				ip6_ct_frag_hash[hval] = q;
				q->pprev = &ip6_ct_frag_hash[hval];
			}

			q = next;
		}
	}
	write_unlock(&ip6_ct_frag_lock);

	mod_timer(&ip6_ct_frag_secret_timer, now + sysctl_ip6_ct_frag_secret_interval);
}

atomic_t ip6_ct_frag_mem = ATOMIC_INIT(0);

/* Memory Tracking Functions. */
static inline void frag_kfree_skb(struct sk_buff *skb)
{
	atomic_sub(skb->truesize, &ip6_ct_frag_mem);
	if (IP6CT_FRAG6_CB(skb)->orig)
		kfree_skb(IP6CT_FRAG6_CB(skb)->orig);

	kfree_skb(skb);
}

static inline void frag_free_queue(struct ip6ct_frag_queue *fq)
{
	atomic_sub(sizeof(struct ip6ct_frag_queue), &ip6_ct_frag_mem);
	kfree(fq);
}

static inline struct ip6ct_frag_queue *frag_alloc_queue(void)
{
	struct ip6ct_frag_queue *fq = kmalloc(sizeof(struct ip6ct_frag_queue), GFP_ATOMIC);

	if(!fq)
		return NULL;
	atomic_add(sizeof(struct ip6ct_frag_queue), &ip6_ct_frag_mem);
	return fq;
}

/* Destruction primitives. */

/* Complete destruction of fq. */
static void ip6_ct_frag_destroy(struct ip6ct_frag_queue *fq)
{
	struct sk_buff *fp;

	BUG_TRAP(fq->last_in&COMPLETE);
	BUG_TRAP(del_timer(&fq->timer) == 0);

	/* Release all fragment data. */
	fp = fq->fragments;
	while (fp) {
		struct sk_buff *xp = fp->next;

		frag_kfree_skb(fp);
		fp = xp;
	}

	frag_free_queue(fq);
}

static __inline__ void fq_put(struct ip6ct_frag_queue *fq)
{
	if (atomic_dec_and_test(&fq->refcnt))
		ip6_ct_frag_destroy(fq);
}

/* Kill fq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static __inline__ void fq_kill(struct ip6ct_frag_queue *fq)
{
	if (del_timer(&fq->timer))
		atomic_dec(&fq->refcnt);

	if (!(fq->last_in & COMPLETE)) {
		fq_unlink(fq);
		atomic_dec(&fq->refcnt);
		fq->last_in |= COMPLETE;
	}
}

static void ip6_ct_frag_evictor(void)
{
	struct ip6ct_frag_queue *fq;
	struct list_head *tmp;

	for(;;) {
		if (atomic_read(&ip6_ct_frag_mem) <= sysctl_ip6_ct_frag_low_thresh)
			return;
		read_lock(&ip6_ct_frag_lock);
		if (list_empty(&ip6_ct_frag_lru_list)) {
			read_unlock(&ip6_ct_frag_lock);
			return;
		}
		tmp = ip6_ct_frag_lru_list.next;
		fq = list_entry(tmp, struct ip6ct_frag_queue, lru_list);
		atomic_inc(&fq->refcnt);
		read_unlock(&ip6_ct_frag_lock);

		spin_lock(&fq->lock);
		if (!(fq->last_in&COMPLETE))
			fq_kill(fq);
		spin_unlock(&fq->lock);

		fq_put(fq);
	}
}

static void ip6_ct_frag_expire(unsigned long data)
{
	struct ip6ct_frag_queue *fq = (struct ip6ct_frag_queue *) data;

	spin_lock(&fq->lock);

	if (fq->last_in & COMPLETE)
		goto out;

	fq_kill(fq);

out:
	spin_unlock(&fq->lock);
	fq_put(fq);
}

/* Creation primitives. */


static struct ip6ct_frag_queue *ip6_ct_frag_intern(unsigned int hash,
					  struct ip6ct_frag_queue *fq_in)
{
	struct ip6ct_frag_queue *fq;

	write_lock(&ip6_ct_frag_lock);
#ifdef CONFIG_SMP
	for (fq = ip6_ct_frag_hash[hash]; fq; fq = fq->next) {
		if (fq->id == fq_in->id && 
		    !ipv6_addr_cmp(&fq_in->saddr, &fq->saddr) &&
		    !ipv6_addr_cmp(&fq_in->daddr, &fq->daddr)) {
			atomic_inc(&fq->refcnt);
			write_unlock(&ip6_ct_frag_lock);
			fq_in->last_in |= COMPLETE;
			fq_put(fq_in);
			return fq;
		}
	}
#endif
	fq = fq_in;

	if (!mod_timer(&fq->timer, jiffies + sysctl_ip6_ct_frag_time))
		atomic_inc(&fq->refcnt);

	atomic_inc(&fq->refcnt);
	if((fq->next = ip6_ct_frag_hash[hash]) != NULL)
		fq->next->pprev = &fq->next;
	ip6_ct_frag_hash[hash] = fq;
	fq->pprev = &ip6_ct_frag_hash[hash];
	INIT_LIST_HEAD(&fq->lru_list);
	list_add_tail(&fq->lru_list, &ip6_ct_frag_lru_list);
	ip6_ct_frag_nqueues++;
	write_unlock(&ip6_ct_frag_lock);
	return fq;
}


static struct ip6ct_frag_queue *
ip6_ct_frag_create(unsigned int hash, u32 id, struct in6_addr *src, struct in6_addr *dst)
{
	struct ip6ct_frag_queue *fq;

	if ((fq = frag_alloc_queue()) == NULL) {
		DEBUGP("Can't alloc new queue\n");
		goto oom;
	}

	memset(fq, 0, sizeof(struct ip6ct_frag_queue));

	fq->id = id;
	ipv6_addr_copy(&fq->saddr, src);
	ipv6_addr_copy(&fq->daddr, dst);

	init_timer(&fq->timer);
	fq->timer.function = ip6_ct_frag_expire;
	fq->timer.data = (long) fq;
	fq->lock = SPIN_LOCK_UNLOCKED;
	atomic_set(&fq->refcnt, 1);

	return ip6_ct_frag_intern(hash, fq);

oom:
	return NULL;
}

static __inline__ struct ip6ct_frag_queue *
fq_find(u32 id, struct in6_addr *src, struct in6_addr *dst)
{
	struct ip6ct_frag_queue *fq;
	unsigned int hash = ip6qhashfn(id, src, dst);

	read_lock(&ip6_ct_frag_lock);
	for(fq = ip6_ct_frag_hash[hash]; fq; fq = fq->next) {
		if (fq->id == id && 
		    !ipv6_addr_cmp(src, &fq->saddr) &&
		    !ipv6_addr_cmp(dst, &fq->daddr)) {
			atomic_inc(&fq->refcnt);
			read_unlock(&ip6_ct_frag_lock);
			return fq;
		}
	}
	read_unlock(&ip6_ct_frag_lock);

	return ip6_ct_frag_create(hash, id, src, dst);
}


static int ip6_ct_frag_queue(struct ip6ct_frag_queue *fq, struct sk_buff *skb, 
			      struct frag_hdr *fhdr, int nhoff)
{
	struct sk_buff *prev, *next;
	int offset, end;

	if (fq->last_in & COMPLETE) {
		DEBUGP("Allready completed\n");
		goto err;
	}

	offset = ntohs(fhdr->frag_off) & ~0x7;
	end = offset + (ntohs(skb->nh.ipv6h->payload_len) -
			((u8 *) (fhdr + 1) - (u8 *) (skb->nh.ipv6h + 1)));

	if ((unsigned int)end > IPV6_MAXPLEN) {
		DEBUGP("offset is too large.\n");
 		return -1;
	}

 	if (skb->ip_summed == CHECKSUM_HW)
 		skb->csum = csum_sub(skb->csum,
 				     csum_partial(skb->nh.raw, (u8*)(fhdr+1)-skb->nh.raw, 0));

	/* Is this the final fragment? */
	if (!(fhdr->frag_off & htons(IP6_MF))) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrupted.
		 */
		if (end < fq->len ||
		    ((fq->last_in & LAST_IN) && end != fq->len)) {
			DEBUGP("already received last fragment\n");
			goto err;
		}
		fq->last_in |= LAST_IN;
		fq->len = end;
	} else {
		/* Check if the fragment is rounded to 8 bytes.
		 * Required by the RFC.
		 */
		if (end & 0x7) {
			/* RFC2460 says always send parameter problem in
			 * this case. -DaveM
			 */
			DEBUGP("the end of this message is not rounded to 8 bytes.\n");
			return -1;
		}
		if (end > fq->len) {
			/* Some bits beyond end -> corruption. */
			if (fq->last_in & LAST_IN) {
				DEBUGP("last packet already reached.\n");
				goto err;
			}
			fq->len = end;
		}
	}

	if (end == offset)
		goto err;

	/* Point into the IP datagram 'data' part. */
	if (!pskb_pull(skb, (u8 *) (fhdr + 1) - skb->data)) {
		DEBUGP("queue: message is too short.\n");
		goto err;
	}
	if (end-offset < skb->len) {
		if (pskb_trim(skb, end - offset)) {
			DEBUGP("Can't trim\n");
			goto err;
		}
		if (skb->ip_summed != CHECKSUM_UNNECESSARY)
			skb->ip_summed = CHECKSUM_NONE;
	}

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = NULL;
	for(next = fq->fragments; next != NULL; next = next->next) {
		if (IP6CT_FRAG6_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	if (prev) {
		int i = (IP6CT_FRAG6_CB(prev)->offset + prev->len) - offset;

		if (i > 0) {
			offset += i;
			if (end <= offset) {
				DEBUGP("overlap\n");
				goto err;
			}
			if (!pskb_pull(skb, i)) {
				DEBUGP("Can't pull\n");
				goto err;
			}
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}

	/* Look for overlap with succeeding segments.
	 * If we can merge fragments, do it.
	 */
	while (next && IP6CT_FRAG6_CB(next)->offset < end) {
		int i = end - IP6CT_FRAG6_CB(next)->offset; /* overlap is 'i' bytes */

		if (i < next->len) {
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			DEBUGP("Eat head of the overlapped parts.: %d", i);
			if (!pskb_pull(next, i))
				goto err;
			IP6CT_FRAG6_CB(next)->offset += i;	/* next fragment */
			fq->meat -= i;
			if (next->ip_summed != CHECKSUM_UNNECESSARY)
				next->ip_summed = CHECKSUM_NONE;
			break;
		} else {
			struct sk_buff *free_it = next;

			/* Old fragmnet is completely overridden with
			 * new one drop it.
			 */
			next = next->next;

			if (prev)
				prev->next = next;
			else
				fq->fragments = next;

			fq->meat -= free_it->len;
			frag_kfree_skb(free_it);
		}
	}

	IP6CT_FRAG6_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		fq->fragments = skb;

	skb->dev = NULL;
	fq->stamp = skb->stamp;
	fq->meat += skb->len;
	atomic_add(skb->truesize, &ip6_ct_frag_mem);

	/* The first fragment.
	 * nhoffset is obtained from the first fragment, of course.
	 */
	if (offset == 0) {
		fq->nhoffset = nhoff;
		fq->last_in |= FIRST_IN;
	}
	write_lock(&ip6_ct_frag_lock);
	list_move_tail(&fq->lru_list, &ip6_ct_frag_lru_list);
	write_unlock(&ip6_ct_frag_lock);
	return 0;

err:
	return -1;
}

/*
 *	Check if this packet is complete.
 *	Returns NULL on failure by any reason, and pointer
 *	to current nexthdr field in reassembled frame.
 *
 *	It is called with locked fq, and caller must check that
 *	queue is eligible for reassembly i.e. it is not COMPLETE,
 *	the last and the first frames arrived and all the bits are here.
 */
static struct sk_buff *
ip6_ct_frag_reasm(struct ip6ct_frag_queue *fq, struct net_device *dev)
{
	struct sk_buff *fp, *op, *head = fq->fragments;
	int    payload_len;

	fq_kill(fq);

	BUG_TRAP(head != NULL);
	BUG_TRAP(IP6CT_FRAG6_CB(head)->offset == 0);

	/* Unfragmented part is taken from the first segment. */
	payload_len = (head->data - head->nh.raw) - sizeof(struct ipv6hdr) + fq->len - sizeof(struct frag_hdr);
	if (payload_len > IPV6_MAXPLEN) {
		DEBUGP("payload len is too large.\n");
		goto out_oversize;
	}

	/* Head of list must not be cloned. */
	if (skb_cloned(head) && pskb_expand_head(head, 0, 0, GFP_ATOMIC)) {
		DEBUGP("skb is cloned but can't expand head");
		goto out_oom;
	}

	/* If the first fragment is fragmented itself, we split
	 * it to two chunks: the first with data and paged part
	 * and the second, holding only fragments. */
	if (skb_shinfo(head)->frag_list) {
		struct sk_buff *clone;
		int i, plen = 0;

		if ((clone = alloc_skb(0, GFP_ATOMIC)) == NULL) {
			DEBUGP("Can't alloc skb\n");
			goto out_oom;
		}
		clone->next = head->next;
		head->next = clone;
		skb_shinfo(clone)->frag_list = skb_shinfo(head)->frag_list;
		skb_shinfo(head)->frag_list = NULL;
		for (i=0; i<skb_shinfo(head)->nr_frags; i++)
			plen += skb_shinfo(head)->frags[i].size;
		clone->len = clone->data_len = head->data_len - plen;
		head->data_len -= clone->len;
		head->len -= clone->len;
		clone->csum = 0;
		clone->ip_summed = head->ip_summed;

		IP6CT_FRAG6_CB(clone)->orig = NULL;
		atomic_add(clone->truesize, &ip6_ct_frag_mem);
	}

	/* We have to remove fragment header from datagram and to relocate
	 * header in order to calculate ICV correctly. */
	head->nh.raw[fq->nhoffset] = head->h.raw[0];
	memmove(head->head + sizeof(struct frag_hdr), head->head, 
		(head->data - head->head) - sizeof(struct frag_hdr));
	head->mac.raw += sizeof(struct frag_hdr);
	head->nh.raw += sizeof(struct frag_hdr);

	skb_shinfo(head)->frag_list = head->next;
	head->h.raw = head->data;
	skb_push(head, head->data - head->nh.raw);
	atomic_sub(head->truesize, &ip6_ct_frag_mem);

	for (fp=head->next; fp; fp = fp->next) {
		head->data_len += fp->len;
		head->len += fp->len;
		if (head->ip_summed != fp->ip_summed)
			head->ip_summed = CHECKSUM_NONE;
		else if (head->ip_summed == CHECKSUM_HW)
			head->csum = csum_add(head->csum, fp->csum);
		head->truesize += fp->truesize;
		atomic_sub(fp->truesize, &ip6_ct_frag_mem);
	}

	head->next = NULL;
	head->dev = dev;
	head->stamp = fq->stamp;
	head->nh.ipv6h->payload_len = ntohs(payload_len);

	/* Yes, and fold redundant checksum back. 8) */
	if (head->ip_summed == CHECKSUM_HW)
		head->csum = csum_partial(head->nh.raw, head->h.raw-head->nh.raw, head->csum);

	fq->fragments = NULL;

	/* all original skbs are linked into the IP6CT_FRAG6_CB(head).orig */
	fp = skb_shinfo(head)->frag_list;
	if (IP6CT_FRAG6_CB(fp)->orig == NULL)
		/* at above code, head skb is divided into two skbs. */
		fp = fp->next;

	op = IP6CT_FRAG6_CB(head)->orig;
	for (; fp; fp = fp->next) {
		struct sk_buff *orig = IP6CT_FRAG6_CB(fp)->orig;

		op->next = orig;
		op = orig;
		IP6CT_FRAG6_CB(fp)->orig = NULL;
	}

	return head;

out_oversize:
	if (net_ratelimit())
		printk(KERN_DEBUG "ip6_ct_frag_reasm: payload len = %d\n", payload_len);
	goto out_fail;
out_oom:
	if (net_ratelimit())
		printk(KERN_DEBUG "ip6_ct_frag_reasm: no memory for reassembly\n");
out_fail:
	return NULL;
}

/*
 * find the header just before Fragment Header.
 *
 * if success return 0 and set ...
 * (*prevhdrp): the value of "Next Header Field" in the header
 *		just before Fragment Header.
 * (*prevhoff): the offset of "Next Header Field" in the header
 *		just before Fragment Header.
 * (*fhoff)   : the offset of Fragment Header.
 *
 * Based on ipv6_skip_hdr() in net/ipv6/exthdr.c
 *
 */
static int
find_prev_fhdr(struct sk_buff *skb, u8 *prevhdrp, int *prevhoff, int *fhoff)
{
        u8 nexthdr = skb->nh.ipv6h->nexthdr;
	u8 prev_nhoff = (u8 *)&skb->nh.ipv6h->nexthdr - skb->data;
	int start = (u8 *)(skb->nh.ipv6h+1) - skb->data;
	int len = skb->len - start;
	u8 prevhdr = NEXTHDR_IPV6;

        while (nexthdr != NEXTHDR_FRAGMENT) {
                struct ipv6_opt_hdr hdr;
                int hdrlen;

		if (!ipv6_ext_hdr(nexthdr)) {
			return -1;
		}
                if (len < (int)sizeof(struct ipv6_opt_hdr)) {
			DEBUGP("too short\n");
			return -1;
		}
                if (nexthdr == NEXTHDR_NONE) {
			DEBUGP("next header is none\n");
			return -1;
		}
                if (skb_copy_bits(skb, start, &hdr, sizeof(hdr)))
                        BUG();
                if (nexthdr == NEXTHDR_AUTH)
                        hdrlen = (hdr.hdrlen+2)<<2;
                else
                        hdrlen = ipv6_optlen(&hdr);

		prevhdr = nexthdr;
		prev_nhoff = start;

                nexthdr = hdr.nexthdr;
                len -= hdrlen;
                start += hdrlen;
        }

	if (len < 0)
		return -1;

	*prevhdrp = prevhdr;
	*prevhoff = prev_nhoff;
	*fhoff = start;

	return 0;
}

struct sk_buff *ip6_ct_gather_frags(struct sk_buff *skb)
{
	struct sk_buff *clone; 
	struct net_device *dev = skb->dev;
	struct frag_hdr *fhdr;
	struct ip6ct_frag_queue *fq;
	struct ipv6hdr *hdr;
	int fhoff, nhoff;
	u8 prevhdr;
	struct sk_buff *ret_skb = NULL;

	/* Jumbo payload inhibits frag. header */
	if (skb->nh.ipv6h->payload_len == 0) {
		DEBUGP("payload len = 0\n");
		return skb;
	}

	if (find_prev_fhdr(skb, &prevhdr, &nhoff, &fhoff) < 0)
		return skb;

	clone = skb_clone(skb, GFP_ATOMIC);
	if (clone == NULL) {
		DEBUGP("Can't clone skb\n");
		return skb;
	}

	IP6CT_FRAG6_CB(clone)->orig = skb;

	if (!pskb_may_pull(clone, fhoff + sizeof(*fhdr))) {
		DEBUGP("message is too short.\n");
		goto ret_orig;
	}

	clone->h.raw = clone->data + fhoff;
	hdr = clone->nh.ipv6h;
	fhdr = (struct frag_hdr *)clone->h.raw;

	if (!(fhdr->frag_off & htons(0xFFF9))) {
		DEBUGP("Invalid fragment offset\n");
		/* It is not a fragmented frame */
		goto ret_orig;
	}

	if (atomic_read(&ip6_ct_frag_mem) > sysctl_ip6_ct_frag_high_thresh)
		ip6_ct_frag_evictor();

	if ((fq = fq_find(fhdr->identification, &hdr->saddr, &hdr->daddr)) == NULL) {
		DEBUGP("Can't find and can't create new queue\n");
		goto ret_orig;
	}

	spin_lock(&fq->lock);

	if (ip6_ct_frag_queue(fq, clone, fhdr, nhoff) < 0) {
		spin_unlock(&fq->lock);
		DEBUGP("Can't insert skb to queue\n");
		fq_put(fq);
		goto ret_orig;
	}

	if (fq->last_in == (FIRST_IN|LAST_IN) &&
	    fq->meat == fq->len) {
		ret_skb = ip6_ct_frag_reasm(fq, dev);

		if (ret_skb == NULL)
			DEBUGP("Can't reassemble fragmented packets\n");
	}
	spin_unlock(&fq->lock);

	fq_put(fq);
	return ret_skb;

ret_orig:
	kfree_skb(clone);
	return skb;
}

int ip6_ct_output_frags(struct sk_buff *skb, struct nf_info *info)
{
	struct sk_buff *s, *s2;
	struct nf_info *copy_info;

	for (s = IP6CT_FRAG6_CB(skb)->orig; s;) {
		if (skb->nfct)
			nf_conntrack_get(skb->nfct);
		s->nfct = skb->nfct;
		s->nfcache = skb->nfcache;

		/* 
		 * nf_reinject() frees copy_info,
		 * so I have to copy it every time. (T-T
		 */
		copy_info = kmalloc(sizeof(*copy_info), GFP_ATOMIC);
		if (copy_info == NULL) {
			DEBUGP("Can't kmalloc() for nf_info\n");
			return -1;
		}

		copy_info->pf = info->pf;
		copy_info->hook = info->hook;
		copy_info->indev = info->indev;
		copy_info->outdev = info->outdev;
		copy_info->okfn = info->okfn;
		copy_info->elem = info->elem;

		/*
		 * nf_reinject() put the module "ip6_conntrack".
		 */
		if (!try_module_get(info->elem->owner)) {
			DEBUGP("Can't get module.\n");
			kfree_skb(s);
			continue;
		}

		if (copy_info->indev)
			dev_hold(copy_info->indev);
		if (copy_info->outdev)
			dev_hold(copy_info->outdev);

		s2 = s->next;
		nf_reinject(s, copy_info, NF_ACCEPT);
		s = s2;
	}

	kfree_skb(skb);

	return 0;
}

int ip6_ct_kfree_frags(struct sk_buff *skb)
{
	struct sk_buff *s, *s2;

	for (s = IP6CT_FRAG6_CB(skb)->orig; s; s = s2) {

		s2 = s->next;
		kfree_skb(s);
	}

	kfree_skb(skb);

	return 0;
}

#ifdef CONFIG_SYSCTL

#define IP6CT_HIGH_THRESH_NAME "ip6ct_frags_high_thresh"
#define IP6CT_LOW_THRESH_NAME "ip6ct_frags_low_thresh"
#define IP6CT_TIMEOUT_NAME "ip6ct_frags_timeout"

static struct ctl_table_header *ip6_ct_frags_sysctl_header;

static ctl_table ip6_ct_frags_table[] = {
	{
		.ctl_name	= IP6CT_FRAGS_HIGH_THRESH,
		.procname	= IP6CT_HIGH_THRESH_NAME,
		.data		= &sysctl_ip6_ct_frag_high_thresh,
		.maxlen		= sizeof(sysctl_ip6_ct_frag_high_thresh),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
        },
	{
		.ctl_name	= IP6CT_FRAGS_LOW_THRESH,
		.procname	= IP6CT_LOW_THRESH_NAME,
		.data		= &sysctl_ip6_ct_frag_low_thresh,
		.maxlen		= sizeof(sysctl_ip6_ct_frag_high_thresh),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
        },
	{
		.ctl_name	= IP6CT_FRAGS_TIMEOUT,
		.procname	= IP6CT_TIMEOUT_NAME,
		.data		= &sysctl_ip6_ct_frag_time,
		.maxlen		= sizeof(sysctl_ip6_ct_frag_time),
		.mode		= 0644,
		.proc_handler	= proc_dointvec
        },
	{ .ctl_name = 0 }
};

static ctl_table ip6_ct_frags_dir_table[] = {
	{
		.ctl_name	= NET_IPV6,
		.procname	= "ipv6", NULL,
		.mode		= 0555,
		.child		= ip6_ct_frags_table
	},
	{ .ctl_name = 0 }
};

static ctl_table ip6_ct_frags_root_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555,
		.child		= ip6_ct_frags_dir_table
	},
	{ .ctl_name = 0 }
};

#endif /*CONFIG_SYSCTL*/

int __init ip6_ct_frags_init(void)
{
#ifdef CONFIG_SYSCTL
	ip6_ct_frags_sysctl_header = register_sysctl_table(ip6_ct_frags_root_table, 0);

	if (ip6_ct_frags_sysctl_header == NULL) {
		printk("ip6_ct_frags_init: Can't register sysctl tables.\n");
		return -ENOMEM;
	}
#endif

	ip6_ct_frag_hash_rnd = (u32) ((num_physpages ^ (num_physpages>>7)) ^
				   (jiffies ^ (jiffies >> 6)));

	init_timer(&ip6_ct_frag_secret_timer);
	ip6_ct_frag_secret_timer.function = ip6_ct_frag_secret_rebuild;
	ip6_ct_frag_secret_timer.expires = jiffies + sysctl_ip6_ct_frag_secret_interval;
	add_timer(&ip6_ct_frag_secret_timer);

	return 0;
}

void ip6_ct_frags_cleanup(void)
{
	del_timer(&ip6_ct_frag_secret_timer);
#ifdef CONFIG_SYSCTL
	unregister_sysctl_table(ip6_ct_frags_sysctl_header);
#endif
	sysctl_ip6_ct_frag_low_thresh = 0;
	ip6_ct_frag_evictor();
}
