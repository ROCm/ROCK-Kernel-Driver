/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP fragmentation functionality.
 *		
 * Version:	$Id: ip_fragment.c,v 1.53 2000/12/08 17:15:53 davem Exp $
 *
 * Authors:	Fred N. van Kempen <waltje@uWalt.NL.Mugnet.ORG>
 *		Alan Cox <Alan.Cox@linux.org>
 *
 * Fixes:
 *		Alan Cox	:	Split from ip.c , see ip_input.c for history.
 *		David S. Miller :	Begin massive cleanup...
 *		Andi Kleen	:	Add sysctls.
 *		xxxx		:	Overlapfrag bug.
 *		Ultima          :       ip_expire() kernel panic.
 *		Bill Hawes	:	Frag accounting and evictor fixes.
 *		John McDonald	:	0 length frag bug.
 *		Alexey Kuznetsov:	SMP races, threading, cleanup.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/inet.h>
#include <linux/netfilter_ipv4.h>

/* NOTE. Logic of IP defragmentation is parallel to corresponding IPv6
 * code now. If you change something here, _PLEASE_ update ipv6/reassembly.c
 * as well. Or notify me, at least. --ANK
 */

/* Fragment cache limits. We will commit 256K at one time. Should we
 * cross that limit we will prune down to 192K. This should cope with
 * even the most extreme cases without allowing an attacker to measurably
 * harm machine performance.
 */
int sysctl_ipfrag_high_thresh = 256*1024;
int sysctl_ipfrag_low_thresh = 192*1024;

/* Important NOTE! Fragment queue must be destroyed before MSL expires.
 * RFC791 is wrong proposing to prolongate timer each fragment arrival by TTL.
 */
int sysctl_ipfrag_time = IP_FRAG_TIME;

struct ipfrag_skb_cb
{
	struct inet_skb_parm	h;
	int			offset;
};

#define FRAG_CB(skb)	((struct ipfrag_skb_cb*)((skb)->cb))

/* Describe an entry in the "incomplete datagrams" queue. */
struct ipq {
	struct ipq	*next;		/* linked list pointers			*/
	u32		saddr;
	u32		daddr;
	u16		id;
	u8		protocol;
	u8		last_in;
#define COMPLETE		4
#define FIRST_IN		2
#define LAST_IN			1

	struct sk_buff	*fragments;	/* linked list of received fragments	*/
	int		len;		/* total length of original datagram	*/
	int		meat;
	spinlock_t	lock;
	atomic_t	refcnt;
	struct timer_list timer;	/* when will this queue expire?		*/
	struct ipq	**pprev;
	int		iif;		/* Device index - for icmp replies	*/
};

/* Hash table. */

#define IPQ_HASHSZ	64

/* Per-bucket lock is easy to add now. */
static struct ipq *ipq_hash[IPQ_HASHSZ];
static rwlock_t ipfrag_lock = RW_LOCK_UNLOCKED;
int ip_frag_nqueues = 0;

static __inline__ void __ipq_unlink(struct ipq *qp)
{
	if(qp->next)
		qp->next->pprev = qp->pprev;
	*qp->pprev = qp->next;
	ip_frag_nqueues--;
}

static __inline__ void ipq_unlink(struct ipq *ipq)
{
	write_lock(&ipfrag_lock);
	__ipq_unlink(ipq);
	write_unlock(&ipfrag_lock);
}

/*
 * Was:	((((id) >> 1) ^ (saddr) ^ (daddr) ^ (prot)) & (IPQ_HASHSZ - 1))
 *
 * I see, I see evil hand of bigendian mafia. On Intel all the packets hit
 * one hash bucket with this hash function. 8)
 */
static __inline__ unsigned int ipqhashfn(u16 id, u32 saddr, u32 daddr, u8 prot)
{
	unsigned int h = saddr ^ daddr;

	h ^= (h>>16)^id;
	h ^= (h>>8)^prot;
	return h & (IPQ_HASHSZ - 1);
}


atomic_t ip_frag_mem = ATOMIC_INIT(0);	/* Memory used for fragments */

/* Memory Tracking Functions. */
extern __inline__ void frag_kfree_skb(struct sk_buff *skb)
{
	atomic_sub(skb->truesize, &ip_frag_mem);
	kfree_skb(skb);
}

extern __inline__ void frag_free_queue(struct ipq *qp)
{
	atomic_sub(sizeof(struct ipq), &ip_frag_mem);
	kfree(qp);
}

extern __inline__ struct ipq *frag_alloc_queue(void)
{
	struct ipq *qp = kmalloc(sizeof(struct ipq), GFP_ATOMIC);

	if(!qp)
		return NULL;
	atomic_add(sizeof(struct ipq), &ip_frag_mem);
	return qp;
}


/* Destruction primitives. */

/* Complete destruction of ipq. */
static void ip_frag_destroy(struct ipq *qp)
{
	struct sk_buff *fp;

	BUG_TRAP(qp->last_in&COMPLETE);
	BUG_TRAP(del_timer(&qp->timer) == 0);

	/* Release all fragment data. */
	fp = qp->fragments;
	while (fp) {
		struct sk_buff *xp = fp->next;

		frag_kfree_skb(fp);
		fp = xp;
	}

	/* Finally, release the queue descriptor itself. */
	frag_free_queue(qp);
}

static __inline__ void ipq_put(struct ipq *ipq)
{
	if (atomic_dec_and_test(&ipq->refcnt))
		ip_frag_destroy(ipq);
}

/* Kill ipq entry. It is not destroyed immediately,
 * because caller (and someone more) holds reference count.
 */
static __inline__ void ipq_kill(struct ipq *ipq)
{
	if (del_timer(&ipq->timer))
		atomic_dec(&ipq->refcnt);

	if (!(ipq->last_in & COMPLETE)) {
		ipq_unlink(ipq);
		atomic_dec(&ipq->refcnt);
		ipq->last_in |= COMPLETE;
	}
}

/* Memory limiting on fragments.  Evictor trashes the oldest 
 * fragment queue until we are back under the low threshold.
 */
static void ip_evictor(void)
{
	int i, progress;

	do {
		if (atomic_read(&ip_frag_mem) <= sysctl_ipfrag_low_thresh)
			return;
		progress = 0;
		/* FIXME: Make LRU queue of frag heads. -DaveM */
		for (i = 0; i < IPQ_HASHSZ; i++) {
			struct ipq *qp;
			if (ipq_hash[i] == NULL)
				continue;

			write_lock(&ipfrag_lock);
			if ((qp = ipq_hash[i]) != NULL) {
				/* find the oldest queue for this hash bucket */
				while (qp->next)
					qp = qp->next;
				__ipq_unlink(qp);
				write_unlock(&ipfrag_lock);

				spin_lock(&qp->lock);
				if (del_timer(&qp->timer))
					atomic_dec(&qp->refcnt);
				qp->last_in |= COMPLETE;
				spin_unlock(&qp->lock);

				ipq_put(qp);
				IP_INC_STATS_BH(IpReasmFails);
				progress = 1;
				continue;
			}
			write_unlock(&ipfrag_lock);
		}
	} while (progress);
}

/*
 * Oops, a fragment queue timed out.  Kill it and send an ICMP reply.
 */
static void ip_expire(unsigned long arg)
{
	struct ipq *qp = (struct ipq *) arg;

	spin_lock(&qp->lock);

	if (qp->last_in & COMPLETE)
		goto out;

	ipq_kill(qp);

	IP_INC_STATS_BH(IpReasmTimeout);
	IP_INC_STATS_BH(IpReasmFails);

	if ((qp->last_in&FIRST_IN) && qp->fragments != NULL) {
		struct sk_buff *head = qp->fragments;

		/* Send an ICMP "Fragment Reassembly Timeout" message. */
		if ((head->dev = dev_get_by_index(qp->iif)) != NULL) {
			icmp_send(head, ICMP_TIME_EXCEEDED, ICMP_EXC_FRAGTIME, 0);
			dev_put(head->dev);
		}
	}
out:
	spin_unlock(&qp->lock);
	ipq_put(qp);
}

/* Creation primitives. */

static struct ipq *ip_frag_intern(unsigned int hash, struct ipq *qp_in)
{
	struct ipq *qp;

	write_lock(&ipfrag_lock);
#ifdef CONFIG_SMP
	/* With SMP race we have to recheck hash table, because
	 * such entry could be created on other cpu, while we
	 * promoted read lock to write lock.
	 */
	for(qp = ipq_hash[hash]; qp; qp = qp->next) {
		if(qp->id == qp_in->id		&&
		   qp->saddr == qp_in->saddr	&&
		   qp->daddr == qp_in->daddr	&&
		   qp->protocol == qp_in->protocol) {
			atomic_inc(&qp->refcnt);
			write_unlock(&ipfrag_lock);
			qp_in->last_in |= COMPLETE;
			ipq_put(qp_in);
			return qp;
		}
	}
#endif
	qp = qp_in;

	if (!mod_timer(&qp->timer, jiffies + sysctl_ipfrag_time))
		atomic_inc(&qp->refcnt);

	atomic_inc(&qp->refcnt);
	if((qp->next = ipq_hash[hash]) != NULL)
		qp->next->pprev = &qp->next;
	ipq_hash[hash] = qp;
	qp->pprev = &ipq_hash[hash];
	ip_frag_nqueues++;
	write_unlock(&ipfrag_lock);
	return qp;
}

/* Add an entry to the 'ipq' queue for a newly received IP datagram. */
static struct ipq *ip_frag_create(unsigned hash, struct iphdr *iph)
{
	struct ipq *qp;

	if ((qp = frag_alloc_queue()) == NULL)
		goto out_nomem;

	qp->protocol = iph->protocol;
	qp->last_in = 0;
	qp->id = iph->id;
	qp->saddr = iph->saddr;
	qp->daddr = iph->daddr;
	qp->len = 0;
	qp->meat = 0;
	qp->fragments = NULL;
	qp->iif = 0;

	/* Initialize a timer for this entry. */
	init_timer(&qp->timer);
	qp->timer.data = (unsigned long) qp;	/* pointer to queue	*/
	qp->timer.function = ip_expire;		/* expire function	*/
	qp->lock = SPIN_LOCK_UNLOCKED;
	atomic_set(&qp->refcnt, 1);

	return ip_frag_intern(hash, qp);

out_nomem:
	NETDEBUG(printk(KERN_ERR "ip_frag_create: no memory left !\n"));
	return NULL;
}

/* Find the correct entry in the "incomplete datagrams" queue for
 * this IP datagram, and create new one, if nothing is found.
 */
static inline struct ipq *ip_find(struct iphdr *iph)
{
	__u16 id = iph->id;
	__u32 saddr = iph->saddr;
	__u32 daddr = iph->daddr;
	__u8 protocol = iph->protocol;
	unsigned int hash = ipqhashfn(id, saddr, daddr, protocol);
	struct ipq *qp;

	read_lock(&ipfrag_lock);
	for(qp = ipq_hash[hash]; qp; qp = qp->next) {
		if(qp->id == id		&&
		   qp->saddr == saddr	&&
		   qp->daddr == daddr	&&
		   qp->protocol == protocol) {
			atomic_inc(&qp->refcnt);
			read_unlock(&ipfrag_lock);
			return qp;
		}
	}
	read_unlock(&ipfrag_lock);

	return ip_frag_create(hash, iph);
}

/* Add new segment to existing queue. */
static void ip_frag_queue(struct ipq *qp, struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct sk_buff *prev, *next;
	int flags, offset;
	int ihl, end;

	if (qp->last_in & COMPLETE)
		goto err;

	offset = ntohs(iph->frag_off);
	flags = offset & ~IP_OFFSET;
	offset &= IP_OFFSET;
	offset <<= 3;		/* offset is in 8-byte chunks */
	ihl = iph->ihl * 4;

	/* Determine the position of this fragment. */
	end = offset + (ntohs(iph->tot_len) - ihl);

	/* Is this the final fragment? */
	if ((flags & IP_MF) == 0) {
		/* If we already have some bits beyond end
		 * or have different end, the segment is corrrupted.
		 */
		if (end < qp->len ||
		    ((qp->last_in & LAST_IN) && end != qp->len))
			goto err;
		qp->last_in |= LAST_IN;
		qp->len = end;
	} else {
		if (end&7) {
			end &= ~7;
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
		if (end > qp->len) {
			/* Some bits beyond end -> corruption. */
			if (qp->last_in & LAST_IN)
				goto err;
			qp->len = end;
		}
	}
	if (end == offset)
		goto err;

	/* Point into the IP datagram 'data' part. */
	skb_pull(skb, (skb->nh.raw+ihl) - skb->data);
	skb_trim(skb, end - offset);

	/* Find out which fragments are in front and at the back of us
	 * in the chain of fragments so far.  We must know where to put
	 * this fragment, right?
	 */
	prev = NULL;
	for(next = qp->fragments; next != NULL; next = next->next) {
		if (FRAG_CB(next)->offset >= offset)
			break;	/* bingo! */
		prev = next;
	}

	/* We found where to put this one.  Check for overlap with
	 * preceding fragment, and, if needed, align things so that
	 * any overlaps are eliminated.
	 */
	if (prev) {
		int i = (FRAG_CB(prev)->offset + prev->len) - offset;

		if (i > 0) {
			offset += i;
			if (end <= offset)
				goto err;
			skb_pull(skb, i);
			if (skb->ip_summed != CHECKSUM_UNNECESSARY)
				skb->ip_summed = CHECKSUM_NONE;
		}
	}

	while (next && FRAG_CB(next)->offset < end) {
		int i = end - FRAG_CB(next)->offset; /* overlap is 'i' bytes */

		if (i < next->len) {
			/* Eat head of the next overlapped fragment
			 * and leave the loop. The next ones cannot overlap.
			 */
			FRAG_CB(next)->offset += i;
			skb_pull(next, i);
			qp->meat -= i;
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
				qp->fragments = next;

			qp->meat -= free_it->len;
			frag_kfree_skb(free_it);
		}
	}

	FRAG_CB(skb)->offset = offset;

	/* Insert this fragment in the chain of fragments. */
	skb->next = next;
	if (prev)
		prev->next = skb;
	else
		qp->fragments = skb;

	if (skb->dev)
		qp->iif = skb->dev->ifindex;
	skb->dev = NULL;
	qp->meat += skb->len;
	atomic_add(skb->truesize, &ip_frag_mem);
	if (offset == 0)
		qp->last_in |= FIRST_IN;

	return;

err:
	kfree_skb(skb);
}


/* Build a new IP datagram from all its fragments.
 *
 * FIXME: We copy here because we lack an effective way of handling lists
 * of bits on input. Until the new skb data handling is in I'm not going
 * to touch this with a bargepole. 
 */
static struct sk_buff *ip_frag_reasm(struct ipq *qp, struct net_device *dev)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct sk_buff *fp, *head = qp->fragments;
	int len;
	int ihlen;

	ipq_kill(qp);

	BUG_TRAP(head != NULL);
	BUG_TRAP(FRAG_CB(head)->offset == 0);

	/* Allocate a new buffer for the datagram. */
	ihlen = head->nh.iph->ihl*4;
	len = ihlen + qp->len;

	if(len > 65535)
		goto out_oversize;

	skb = dev_alloc_skb(len);
	if (!skb)
		goto out_nomem;

	/* Fill in the basic details. */
	skb->mac.raw = skb->data;
	skb->nh.raw = skb->data;
	FRAG_CB(skb)->h = FRAG_CB(head)->h;
	skb->ip_summed = head->ip_summed;
	skb->csum = 0;

	/* Copy the original IP headers into the new buffer. */
	memcpy(skb_put(skb, ihlen), head->nh.iph, ihlen);

	/* Copy the data portions of all fragments into the new buffer. */
	for (fp=head; fp; fp = fp->next) {
		memcpy(skb_put(skb, fp->len), fp->data, fp->len);

		if (skb->ip_summed != fp->ip_summed)
			skb->ip_summed = CHECKSUM_NONE;
		else if (skb->ip_summed == CHECKSUM_HW)
			skb->csum = csum_add(skb->csum, fp->csum);
	}

	skb->dst = dst_clone(head->dst);
	skb->pkt_type = head->pkt_type;
	skb->protocol = head->protocol;
	skb->dev = dev;

	/*
	*  Clearly bogus, because security markings of the individual
	*  fragments should have been checked for consistency before
	*  gluing, and intermediate coalescing of fragments may have
	*  taken place in ip_defrag() before ip_glue() ever got called.
	*  If we're not going to do the consistency checking, we might
	*  as well take the value associated with the first fragment.
	*	--rct
	*/
	skb->security = head->security;

#ifdef CONFIG_NETFILTER
	/* Connection association is same as fragment (if any). */
	skb->nfct = head->nfct;
	nf_conntrack_get(skb->nfct);
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = head->nf_debug;
#endif
#endif

	/* Done with all fragments. Fixup the new IP header. */
	iph = skb->nh.iph;
	iph->frag_off = 0;
	iph->tot_len = htons(len);
	IP_INC_STATS_BH(IpReasmOKs);
	return skb;

out_nomem:
 	NETDEBUG(printk(KERN_ERR 
			"IP: queue_glue: no memory for gluing queue %p\n",
			qp));
	goto out_fail;
out_oversize:
	if (net_ratelimit())
		printk(KERN_INFO
			"Oversized IP packet from %d.%d.%d.%d.\n",
			NIPQUAD(qp->saddr));
out_fail:
	IP_INC_STATS_BH(IpReasmFails);
	return NULL;
}

/* Process an incoming IP datagram fragment. */
struct sk_buff *ip_defrag(struct sk_buff *skb)
{
	struct iphdr *iph = skb->nh.iph;
	struct ipq *qp;
	struct net_device *dev;
	
	IP_INC_STATS_BH(IpReasmReqds);

	/* Start by cleaning up the memory. */
	if (atomic_read(&ip_frag_mem) > sysctl_ipfrag_high_thresh)
		ip_evictor();

	dev = skb->dev;

	/* Lookup (or create) queue header */
	if ((qp = ip_find(iph)) != NULL) {
		struct sk_buff *ret = NULL;

		spin_lock(&qp->lock);

		ip_frag_queue(qp, skb);

		if (qp->last_in == (FIRST_IN|LAST_IN) &&
		    qp->meat == qp->len)
			ret = ip_frag_reasm(qp, dev);

		spin_unlock(&qp->lock);
		ipq_put(qp);
		return ret;
	}

	IP_INC_STATS_BH(IpReasmFails);
	kfree_skb(skb);
	return NULL;
}
