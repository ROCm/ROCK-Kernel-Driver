/*
 *	Definitions for the 'struct sk_buff' memory handlers.
 *
 *	Authors:
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Florian La Roche, <rzsfl@rz.uni-sb.de>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
 
#ifndef _LINUX_SKBUFF_H
#define _LINUX_SKBUFF_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>

#include <asm/atomic.h>
#include <asm/types.h>
#include <linux/spinlock.h>

#define HAVE_ALLOC_SKB		/* For the drivers to know */
#define HAVE_ALIGNABLE_SKB	/* Ditto 8)		   */
#define SLAB_SKB 		/* Slabified skbuffs 	   */

#define CHECKSUM_NONE 0
#define CHECKSUM_HW 1
#define CHECKSUM_UNNECESSARY 2

#ifdef __i386__
#define NET_CALLER(arg) (*(((void**)&arg)-1))
#else
#define NET_CALLER(arg) __builtin_return_address(0)
#endif

#ifdef CONFIG_NETFILTER
struct nf_conntrack {
	atomic_t use;
	void (*destroy)(struct nf_conntrack *);
};

struct nf_ct_info {
	struct nf_conntrack *master;
};
#endif

struct sk_buff_head {
	/* These two members must be first. */
	struct sk_buff	* next;
	struct sk_buff	* prev;

	__u32		qlen;
	spinlock_t	lock;
};

struct sk_buff {
	/* These two members must be first. */
	struct sk_buff	* next;			/* Next buffer in list 				*/
	struct sk_buff	* prev;			/* Previous buffer in list 			*/

	struct sk_buff_head * list;		/* List we are on				*/
	struct sock	*sk;			/* Socket we are owned by 			*/
	struct timeval	stamp;			/* Time we arrived				*/
	struct net_device	*dev;		/* Device we arrived on/are leaving by		*/

	/* Transport layer header */
	union
	{
		struct tcphdr	*th;
		struct udphdr	*uh;
		struct icmphdr	*icmph;
		struct igmphdr	*igmph;
		struct iphdr	*ipiph;
		struct spxhdr	*spxh;
		unsigned char	*raw;
	} h;

	/* Network layer header */
	union
	{
		struct iphdr	*iph;
		struct ipv6hdr	*ipv6h;
		struct arphdr	*arph;
		struct ipxhdr	*ipxh;
		unsigned char	*raw;
	} nh;
  
	/* Link layer header */
	union 
	{	
	  	struct ethhdr	*ethernet;
	  	unsigned char 	*raw;
	} mac;

	struct  dst_entry *dst;

	/* 
	 * This is the control buffer. It is free to use for every
	 * layer. Please put your private variables there. If you
	 * want to keep them across layers you have to do a skb_clone()
	 * first. This is owned by whoever has the skb queued ATM.
	 */ 
	char		cb[48];	 

	unsigned int 	len;			/* Length of actual data			*/
	unsigned int	csum;			/* Checksum 					*/
	volatile char 	used;			/* Data moved to user and not MSG_PEEK		*/
	unsigned char	cloned, 		/* head may be cloned (check refcnt to be sure). */
  			pkt_type,		/* Packet class					*/
  			ip_summed;		/* Driver fed us an IP checksum			*/
	__u32		priority;		/* Packet queueing priority			*/
	atomic_t	users;			/* User count - see datagram.c,tcp.c 		*/
	unsigned short	protocol;		/* Packet protocol from driver. 		*/
	unsigned short	security;		/* Security level of packet			*/
	unsigned int	truesize;		/* Buffer size 					*/

	unsigned char	*head;			/* Head of buffer 				*/
	unsigned char	*data;			/* Data head pointer				*/
	unsigned char	*tail;			/* Tail pointer					*/
	unsigned char 	*end;			/* End pointer					*/
	void 		(*destructor)(struct sk_buff *);	/* Destruct function		*/
#ifdef CONFIG_NETFILTER
	/* Can be used for communication between hooks. */
        unsigned long	nfmark;
	/* Cache info */
	__u32		nfcache;
	/* Associated connection, if any */
	struct nf_ct_info *nfct;
#ifdef CONFIG_NETFILTER_DEBUG
        unsigned int nf_debug;
#endif
#endif /*CONFIG_NETFILTER*/

#if defined(CONFIG_HIPPI)
	union{
		__u32	ifield;
	} private;
#endif

#ifdef CONFIG_NET_SCHED
       __u32           tc_index;               /* traffic control index */
#endif
};

#define SK_WMEM_MAX	65535
#define SK_RMEM_MAX	65535

#ifdef __KERNEL__
/*
 *	Handling routines are only of interest to the kernel
 */
#include <linux/malloc.h>

#include <asm/system.h>

extern void			__kfree_skb(struct sk_buff *skb);
extern struct sk_buff *		skb_peek_copy(struct sk_buff_head *list);
extern struct sk_buff *		alloc_skb(unsigned int size, int priority);
extern void			kfree_skbmem(struct sk_buff *skb);
extern struct sk_buff *		skb_clone(struct sk_buff *skb, int priority);
extern struct sk_buff *		skb_copy(const struct sk_buff *skb, int priority);
extern struct sk_buff *		skb_copy_expand(const struct sk_buff *skb, 
						int newheadroom,
						int newtailroom,
						int priority);
#define dev_kfree_skb(a)	kfree_skb(a)
extern void	skb_over_panic(struct sk_buff *skb, int len, void *here);
extern void	skb_under_panic(struct sk_buff *skb, int len, void *here);

/* Backwards compatibility */
#define skb_realloc_headroom(skb, nhr) skb_copy_expand(skb, nhr, skb_tailroom(skb), GFP_ATOMIC)

/* Internal */
static inline atomic_t *skb_datarefp(struct sk_buff *skb)
{
	return (atomic_t *)(skb->end);
}

/**
 *	skb_queue_empty - check if a queue is empty
 *	@list: queue head
 *
 *	Returns true if the queue is empty, false otherwise.
 */
 
static inline int skb_queue_empty(struct sk_buff_head *list)
{
	return (list->next == (struct sk_buff *) list);
}

/**
 *	skb_get - reference buffer
 *	@skb: buffer to reference
 *
 *	Makes another reference to a socket buffer and returns a pointer
 *	to the buffer.
 */
 
static inline struct sk_buff *skb_get(struct sk_buff *skb)
{
	atomic_inc(&skb->users);
	return skb;
}

/*
 * If users==1, we are the only owner and are can avoid redundant
 * atomic change.
 */
 
/**
 *	kfree_skb - free an sk_buff
 *	@skb: buffer to free
 *
 *	Drop a reference to the buffer and free it if the usage count has
 *	hit zero.
 */
 
static inline void kfree_skb(struct sk_buff *skb)
{
	if (atomic_read(&skb->users) == 1 || atomic_dec_and_test(&skb->users))
		__kfree_skb(skb);
}

/* Use this if you didn't touch the skb state [for fast switching] */
static inline void kfree_skb_fast(struct sk_buff *skb)
{
	if (atomic_read(&skb->users) == 1 || atomic_dec_and_test(&skb->users))
		kfree_skbmem(skb);	
}

/**
 *	skb_cloned - is the buffer a clone
 *	@skb: buffer to check
 *
 *	Returns true if the buffer was generated with skb_clone() and is
 *	one of multiple shared copies of the buffer. Cloned buffers are
 *	shared data so must not be written to under normal circumstances.
 */

static inline int skb_cloned(struct sk_buff *skb)
{
	return skb->cloned && atomic_read(skb_datarefp(skb)) != 1;
}

/**
 *	skb_shared - is the buffer shared
 *	@skb: buffer to check
 *
 *	Returns true if more than one person has a reference to this
 *	buffer.
 */
 
static inline int skb_shared(struct sk_buff *skb)
{
	return (atomic_read(&skb->users) != 1);
}

/** 
 *	skb_share_check - check if buffer is shared and if so clone it
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *	
 *	If the buffer is shared the buffer is cloned and the old copy
 *	drops a reference. A new clone with a single reference is returned.
 *	If the buffer is not shared the original buffer is returned. When
 *	being called from interrupt status or with spinlocks held pri must
 *	be GFP_ATOMIC.
 *
 *	NULL is returned on a memory allocation failure.
 */
 
static inline struct sk_buff *skb_share_check(struct sk_buff *skb, int pri)
{
	if (skb_shared(skb)) {
		struct sk_buff *nskb;
		nskb = skb_clone(skb, pri);
		kfree_skb(skb);
		return nskb;
	}
	return skb;
}


/*
 *	Copy shared buffers into a new sk_buff. We effectively do COW on
 *	packets to handle cases where we have a local reader and forward
 *	and a couple of other messy ones. The normal one is tcpdumping
 *	a packet thats being forwarded.
 */
 
/**
 *	skb_unshare - make a copy of a shared buffer
 *	@skb: buffer to check
 *	@pri: priority for memory allocation
 *
 *	If the socket buffer is a clone then this function creates a new
 *	copy of the data, drops a reference count on the old copy and returns
 *	the new copy with the reference count at 1. If the buffer is not a clone
 *	the original buffer is returned. When called with a spinlock held or
 *	from interrupt state @pri must be %GFP_ATOMIC
 *
 *	%NULL is returned on a memory allocation failure.
 */
 
static inline struct sk_buff *skb_unshare(struct sk_buff *skb, int pri)
{
	struct sk_buff *nskb;
	if(!skb_cloned(skb))
		return skb;
	nskb=skb_copy(skb, pri);
	kfree_skb(skb);		/* Free our shared copy */
	return nskb;
}

/**
 *	skb_peek
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the head element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */
 
static inline struct sk_buff *skb_peek(struct sk_buff_head *list_)
{
	struct sk_buff *list = ((struct sk_buff *)list_)->next;
	if (list == (struct sk_buff *)list_)
		list = NULL;
	return list;
}

/**
 *	skb_peek_tail
 *	@list_: list to peek at
 *
 *	Peek an &sk_buff. Unlike most other operations you _MUST_
 *	be careful with this one. A peek leaves the buffer on the
 *	list and someone else may run off with it. You must hold
 *	the appropriate locks or have a private queue to do this.
 *
 *	Returns %NULL for an empty list or a pointer to the tail element.
 *	The reference count is not incremented and the reference is therefore
 *	volatile. Use with caution.
 */

static inline struct sk_buff *skb_peek_tail(struct sk_buff_head *list_)
{
	struct sk_buff *list = ((struct sk_buff *)list_)->prev;
	if (list == (struct sk_buff *)list_)
		list = NULL;
	return list;
}

/**
 *	skb_queue_len	- get queue length
 *	@list_: list to measure
 *
 *	Return the length of an &sk_buff queue. 
 */
 
static inline __u32 skb_queue_len(struct sk_buff_head *list_)
{
	return(list_->qlen);
}

static inline void skb_queue_head_init(struct sk_buff_head *list)
{
	spin_lock_init(&list->lock);
	list->prev = (struct sk_buff *)list;
	list->next = (struct sk_buff *)list;
	list->qlen = 0;
}

/*
 *	Insert an sk_buff at the start of a list.
 *
 *	The "__skb_xxxx()" functions are the non-atomic ones that
 *	can only be called with interrupts disabled.
 */

/**
 *	__skb_queue_head - queue a buffer at the list head
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the start of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */	
 
static inline void __skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk)
{
	struct sk_buff *prev, *next;

	newsk->list = list;
	list->qlen++;
	prev = (struct sk_buff *)list;
	next = prev->next;
	newsk->next = next;
	newsk->prev = prev;
	next->prev = newsk;
	prev->next = newsk;
}


/**
 *	skb_queue_head - queue a buffer at the list head
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the start of the list. This function takes the
 *	list lock and can be used safely with other locking &sk_buff functions
 *	safely.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */	

static inline void skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_head(list, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}

/**
 *	__skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the end of a list. This function takes no locks
 *	and you must therefore hold required locks before calling it.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */	
 

static inline void __skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk)
{
	struct sk_buff *prev, *next;

	newsk->list = list;
	list->qlen++;
	next = (struct sk_buff *)list;
	prev = next->prev;
	newsk->next = next;
	newsk->prev = prev;
	next->prev = newsk;
	prev->next = newsk;
}

/**
 *	skb_queue_tail - queue a buffer at the list tail
 *	@list: list to use
 *	@newsk: buffer to queue
 *
 *	Queue a buffer at the tail of the list. This function takes the
 *	list lock and can be used safely with other locking &sk_buff functions
 *	safely.
 *
 *	A buffer cannot be placed on two lists at the same time.
 */	

static inline void skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_tail(list, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}

/**
 *	__skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The head item is
 *	returned or %NULL if the list is empty.
 */

static inline struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
	struct sk_buff *next, *prev, *result;

	prev = (struct sk_buff *) list;
	next = prev->next;
	result = NULL;
	if (next != prev) {
		result = next;
		next = next->next;
		list->qlen--;
		next->prev = prev;
		prev->next = next;
		result->next = NULL;
		result->prev = NULL;
		result->list = NULL;
	}
	return result;
}

/**
 *	skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. The list lock is taken so the function
 *	may be used safely with other locking list functions. The head item is
 *	returned or %NULL if the list is empty.
 */

static inline struct sk_buff *skb_dequeue(struct sk_buff_head *list)
{
	long flags;
	struct sk_buff *result;

	spin_lock_irqsave(&list->lock, flags);
	result = __skb_dequeue(list);
	spin_unlock_irqrestore(&list->lock, flags);
	return result;
}

/*
 *	Insert a packet on a list.
 */

static inline void __skb_insert(struct sk_buff *newsk,
	struct sk_buff * prev, struct sk_buff *next,
	struct sk_buff_head * list)
{
	newsk->next = next;
	newsk->prev = prev;
	next->prev = newsk;
	prev->next = newsk;
	newsk->list = list;
	list->qlen++;
}

/**
 *	skb_insert	-	insert a buffer
 *	@old: buffer to insert before
 *	@newsk: buffer to insert
 *
 *	Place a packet before a given packet in a list. The list locks are taken
 *	and this function is atomic with respect to other list locked calls
 *	A buffer cannot be placed on two lists at the same time.
 */

static inline void skb_insert(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	spin_lock_irqsave(&old->list->lock, flags);
	__skb_insert(newsk, old->prev, old, old->list);
	spin_unlock_irqrestore(&old->list->lock, flags);
}

/*
 *	Place a packet after a given packet in a list.
 */

static inline void __skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	__skb_insert(newsk, old, old->next, old->list);
}

/**
 *	skb_append	-	append a buffer
 *	@old: buffer to insert after
 *	@newsk: buffer to insert
 *
 *	Place a packet after a given packet in a list. The list locks are taken
 *	and this function is atomic with respect to other list locked calls.
 *	A buffer cannot be placed on two lists at the same time.
 */


static inline void skb_append(struct sk_buff *old, struct sk_buff *newsk)
{
	unsigned long flags;

	spin_lock_irqsave(&old->list->lock, flags);
	__skb_append(old, newsk);
	spin_unlock_irqrestore(&old->list->lock, flags);
}

/*
 * remove sk_buff from list. _Must_ be called atomically, and with
 * the list known..
 */
 
static inline void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
	struct sk_buff * next, * prev;

	list->qlen--;
	next = skb->next;
	prev = skb->prev;
	skb->next = NULL;
	skb->prev = NULL;
	skb->list = NULL;
	next->prev = prev;
	prev->next = next;
}

/**
 *	skb_unlink	-	remove a buffer from a list
 *	@skb: buffer to remove
 *
 *	Place a packet after a given packet in a list. The list locks are taken
 *	and this function is atomic with respect to other list locked calls
 *	
 *	Works even without knowing the list it is sitting on, which can be 
 *	handy at times. It also means that THE LIST MUST EXIST when you 
 *	unlink. Thus a list must have its contents unlinked before it is
 *	destroyed.
 */

static inline void skb_unlink(struct sk_buff *skb)
{
	struct sk_buff_head *list = skb->list;

	if(list) {
		unsigned long flags;

		spin_lock_irqsave(&list->lock, flags);
		if(skb->list == list)
			__skb_unlink(skb, skb->list);
		spin_unlock_irqrestore(&list->lock, flags);
	}
}

/* XXX: more streamlined implementation */

/**
 *	__skb_dequeue_tail - remove from the tail of the queue
 *	@list: list to dequeue from
 *
 *	Remove the tail of the list. This function does not take any locks
 *	so must be used with appropriate locks held only. The tail item is
 *	returned or %NULL if the list is empty.
 */

static inline struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
	struct sk_buff *skb = skb_peek_tail(list); 
	if (skb)
		__skb_unlink(skb, list);
	return skb;
}

/**
 *	skb_dequeue - remove from the head of the queue
 *	@list: list to dequeue from
 *
 *	Remove the head of the list. The list lock is taken so the function
 *	may be used safely with other locking list functions. The tail item is
 *	returned or %NULL if the list is empty.
 */

static inline struct sk_buff *skb_dequeue_tail(struct sk_buff_head *list)
{
	long flags;
	struct sk_buff *result;

	spin_lock_irqsave(&list->lock, flags);
	result = __skb_dequeue_tail(list);
	spin_unlock_irqrestore(&list->lock, flags);
	return result;
}

/*
 *	Add data to an sk_buff
 */
 
static inline unsigned char *__skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp=skb->tail;
	skb->tail+=len;
	skb->len+=len;
	return tmp;
}

/**
 *	skb_put - add data to a buffer
 *	@skb: buffer to use 
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer. If this would
 *	exceed the total buffer size the kernel will panic. A pointer to the
 *	first byte of the extra data is returned.
 */
 
static inline unsigned char *skb_put(struct sk_buff *skb, unsigned int len)
{
	unsigned char *tmp=skb->tail;
	skb->tail+=len;
	skb->len+=len;
	if(skb->tail>skb->end) {
		skb_over_panic(skb, len, current_text_addr());
	}
	return tmp;
}

static inline unsigned char *__skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data-=len;
	skb->len+=len;
	return skb->data;
}

/**
 *	skb_push - add data to the start of a buffer
 *	@skb: buffer to use 
 *	@len: amount of data to add
 *
 *	This function extends the used data area of the buffer at the buffer
 *	start. If this would exceed the total buffer headroom the kernel will
 *	panic. A pointer to the first byte of the extra data is returned.
 */

static inline unsigned char *skb_push(struct sk_buff *skb, unsigned int len)
{
	skb->data-=len;
	skb->len+=len;
	if(skb->data<skb->head) {
		skb_under_panic(skb, len, current_text_addr());
	}
	return skb->data;
}

static inline char *__skb_pull(struct sk_buff *skb, unsigned int len)
{
	skb->len-=len;
	return 	skb->data+=len;
}

/**
 *	skb_pull - remove data from the start of a buffer
 *	@skb: buffer to use 
 *	@len: amount of data to remove
 *
 *	This function removes data from the start of a buffer, returning
 *	the memory to the headroom. A pointer to the next data in the buffer
 *	is returned. Once the data has been pulled future pushes will overwrite
 *	the old data.
 */

static inline unsigned char * skb_pull(struct sk_buff *skb, unsigned int len)
{	
	if (len > skb->len)
		return NULL;
	return __skb_pull(skb,len);
}

/**
 *	skb_headroom - bytes at buffer head
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the head of an &sk_buff.
 */
 
static inline int skb_headroom(const struct sk_buff *skb)
{
	return skb->data-skb->head;
}

/**
 *	skb_tailroom - bytes at buffer end
 *	@skb: buffer to check
 *
 *	Return the number of bytes of free space at the tail of an sk_buff
 */

static inline int skb_tailroom(const struct sk_buff *skb)
{
	return skb->end-skb->tail;
}

/**
 *	skb_reserve - adjust headroom
 *	@skb: buffer to alter
 *	@len: bytes to move
 *
 *	Increase the headroom of an empty &sk_buff by reducing the tail
 *	room. This is only allowed for an empty buffer.
 */

static inline void skb_reserve(struct sk_buff *skb, unsigned int len)
{
	skb->data+=len;
	skb->tail+=len;
}


static inline void __skb_trim(struct sk_buff *skb, unsigned int len)
{
	skb->len = len;
	skb->tail = skb->data+len;
}

/**
 *	skb_trim - remove end from a buffer
 *	@skb: buffer to alter
 *	@len: new length
 *
 *	Cut the length of a buffer down by removing data from the tail. If
 *	the buffer is already under the length specified it is not modified.
 */

static inline void skb_trim(struct sk_buff *skb, unsigned int len)
{
	if (skb->len > len) {
		__skb_trim(skb, len);
	}
}

/**
 *	skb_orphan - orphan a buffer
 *	@skb: buffer to orphan
 *
 *	If a buffer currently has an owner then we call the owner's
 *	destructor function and make the @skb unowned. The buffer continues
 *	to exist but is no longer charged to its former owner.
 */


static inline void skb_orphan(struct sk_buff *skb)
{
	if (skb->destructor)
		skb->destructor(skb);
	skb->destructor = NULL;
	skb->sk = NULL;
}

/**
 *	skb_purge - empty a list
 *	@list: list to empty
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function takes the list
 *	lock and is atomic with respect to other list locking functions.
 */


static inline void skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb=skb_dequeue(list))!=NULL)
		kfree_skb(skb);
}

/**
 *	__skb_purge - empty a list
 *	@list: list to empty
 *
 *	Delete all buffers on an &sk_buff list. Each buffer is removed from
 *	the list and one reference dropped. This function does not take the
 *	list lock and the caller must hold the relevant locks to use it.
 */


static inline void __skb_queue_purge(struct sk_buff_head *list)
{
	struct sk_buff *skb;
	while ((skb=__skb_dequeue(list))!=NULL)
		kfree_skb(skb);
}

/**
 *	dev_alloc_skb - allocate an skbuff for sending
 *	@length: length to allocate
 *
 *	Allocate a new &sk_buff and assign it a usage count of one. The
 *	buffer has unspecified headroom built in. Users should allocate
 *	the headroom they think they need without accounting for the
 *	built in space. The built in space is used for optimisations.
 *
 *	%NULL is returned in there is no free memory. Although this function
 *	allocates memory it can be called from an interrupt.
 */
 
static inline struct sk_buff *dev_alloc_skb(unsigned int length)
{
	struct sk_buff *skb;

	skb = alloc_skb(length+16, GFP_ATOMIC);
	if (skb)
		skb_reserve(skb,16);
	return skb;
}

/**
 *	skb_cow - copy a buffer if need be
 *	@skb: buffer to copy
 *	@headroom: needed headroom
 *
 *	If the buffer passed lacks sufficient headroom or is a clone then
 *	it is copied and the additional headroom made available. If there
 *	is no free memory %NULL is returned. The new buffer is returned if
 *	a copy was made (and the old one dropped a reference). The existing
 *	buffer is returned otherwise.
 *
 *	This function primarily exists to avoid making two copies when making
 *	a writable copy of a buffer and then growing the headroom.
 */
 

static inline struct sk_buff *
skb_cow(struct sk_buff *skb, unsigned int headroom)
{
	headroom = (headroom+15)&~15;

	if ((unsigned)skb_headroom(skb) < headroom || skb_cloned(skb)) {
		struct sk_buff *skb2 = skb_realloc_headroom(skb, headroom);
		kfree_skb(skb);
		skb = skb2;
	}
	return skb;
}

#define skb_queue_walk(queue, skb) \
		for (skb = (queue)->next;			\
		     (skb != (struct sk_buff *)(queue));	\
		     skb=skb->next)


extern struct sk_buff *		skb_recv_datagram(struct sock *sk,unsigned flags,int noblock, int *err);
extern unsigned int		datagram_poll(struct file *file, struct socket *sock, struct poll_table_struct *wait);
extern int			skb_copy_datagram(struct sk_buff *from, int offset, char *to,int size);
extern int			skb_copy_datagram_iovec(struct sk_buff *from, int offset, struct iovec *to,int size);
extern void			skb_free_datagram(struct sock * sk, struct sk_buff *skb);

extern void skb_init(void);
extern void skb_add_mtu(int mtu);

#ifdef CONFIG_NETFILTER
static inline void
nf_conntrack_put(struct nf_ct_info *nfct)
{
	if (nfct && atomic_dec_and_test(&nfct->master->use))
		nfct->master->destroy(nfct->master);
}
static inline void
nf_conntrack_get(struct nf_ct_info *nfct)
{
	if (nfct)
		atomic_inc(&nfct->master->use);
}
#endif

#endif	/* __KERNEL__ */
#endif	/* _LINUX_SKBUFF_H */
