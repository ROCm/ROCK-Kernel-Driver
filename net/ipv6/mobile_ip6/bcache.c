/*
 *      Binding Cache
 *
 *      Authors:
 *      Juha Mynttinen            <jmynttin@cc.hut.fi>
 *
 *      $Id: s.bcache.c 1.92 03/09/29 20:58:50+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Nanno Langstraat	:	Timer code cleaned up, active socket
 *					test rewritten
 */

#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/ipv6_route.h>
#include <linux/module.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/ip6_route.h>
#include <net/mipv6.h>

#include "bcache.h"
#include "hashlist.h"
#include "debug.h"
#include "mobhdr.h"
#include "tunnel.h"
#include "config.h"

#define TIMERDELAY HZ/10

struct mipv6_bcache {
	struct hashlist *entries;
	__u32 size;
	struct timer_list callback_timer;
};

struct in6_addr_pair {
	struct in6_addr *a1;
	struct in6_addr *a2;
};

static rwlock_t bcache_lock = RW_LOCK_UNLOCKED;

static struct mipv6_bcache bcache;

static int bcache_proc_info(char *buffer, char **start, off_t offset,
			    int length);

#define MIPV6_BCACHE_HASHSIZE  32

/* Moment of transmission of a BR, in seconds before bcache entry expiry */
#define BCACHE_BR_SEND_LEAD  3

#define MIPV6_MAX_BRR 3 /* Send 3 BRRs before deleting BC entry */
#define MIPV6_BRR_RATE HZ /* Send BRRs once per second */

/* 
 * Internal functions.
 */

struct cache_entry_iterator_args {
	struct mipv6_bce **entry;
};

static int find_first_cache_entry_iterator(void *data, void *args,
					   unsigned long *lifetime)
{
	struct mipv6_bce *entry =
	    (struct mipv6_bce *) data;
	struct cache_entry_iterator_args *state =
	    (struct cache_entry_iterator_args *) args;

	ASSERT(entry != NULL);

	if (entry->type == CACHE_ENTRY) {
		*(state->entry) = entry;
		return ITERATOR_STOP;	/* stop iteration */
	} else {
		return ITERATOR_CONT;	/* continue iteration */
	}
}


/* 
 * Get memory for a new bcache entry.  If bcache is full, a cache
 * entry may be deleted to get space for a home registration, but not
 * vice versa.
 */
static struct mipv6_bce *mipv6_bce_alloc(__u8 type)
{
	struct mipv6_bce *entry;
	struct cache_entry_iterator_args args;

	DEBUG_FUNC();

	entry = (struct mipv6_bce *)
		hashlist_alloc(bcache.entries, SLAB_ATOMIC);

	/* Cache replacement policy: always replace the CACHE_ENTRY
           closest to expiration.  Type HOME_REGISTRATION entry may
           never be deleted before expiration. */
	if (entry == NULL) {
		/* cache full, try to delete a CACHE_ENTRY */
		args.entry = &entry;
		hashlist_iterate(bcache.entries, &args,
				 find_first_cache_entry_iterator);
		if (entry == NULL)
			return NULL;
		hashlist_delete(bcache.entries,
				(struct hashlist_entry *)entry);
		entry = (struct mipv6_bce *)
			hashlist_alloc(bcache.entries, SLAB_ATOMIC);
	}
	return entry;
}

/*
 * Frees entry's memory allocated with mipv6_bce_alloc
 */
static void mipv6_bce_free(struct mipv6_bce *entry)
{
	hashlist_free(bcache.entries, (void *) entry);
}

/*
 * Removes all expired entries 
 */
static void expire(void)
{
	struct mipv6_bce *entry;
	struct br_addrs {
		struct in6_addr daddr;
		struct in6_addr saddr;
		struct br_addrs *next;
	};
	struct br_addrs *br_info = NULL;

	DEBUG_FUNC();

	write_lock(&bcache_lock);

	while ((entry = (struct mipv6_bce *)
		hashlist_get_first(bcache.entries)) != NULL) {
		struct rt6_info *rt;
		if (time_after_eq(jiffies, entry->callback_time)) {

			DEBUG(DBG_INFO, "an entry expired");

			if (entry->type & HOME_REGISTRATION) {
				mip6_fn.proxy_del(&entry->home_addr, entry);
			}
			hashlist_delete(bcache.entries, (void *)entry);
			mipv6_bce_free(entry);
			entry = NULL;
		} else if (entry->br_callback_time != 0 &&
			   time_after_eq(jiffies, entry->br_callback_time) &&
			   entry->br_count < MIPV6_MAX_BRR &&
			   (rt = rt6_lookup(&entry->home_addr, &entry->our_addr, 0, 0)) != NULL){
			/* Do we have a destination cache entry for the home address */
			if (rt->rt6i_flags & RTF_CACHE) {
				struct br_addrs *tmp;
				tmp = br_info;
				DEBUG(DBG_INFO,
				      "bcache entry recently used. Sending BR.");
				/* queue for sending */
				br_info = kmalloc(sizeof(struct br_addrs),
						  GFP_ATOMIC);
				if (br_info) {
					ipv6_addr_copy(&br_info->saddr,
						       &entry->our_addr);
					ipv6_addr_copy(&br_info->daddr,
						       &entry->home_addr);
					br_info->next = tmp;
					entry->last_br = jiffies;
					entry->br_callback_time = jiffies + MIPV6_BRR_RATE;
					entry->br_count++;
				} else {
					br_info = tmp;
					DEBUG(DBG_ERROR, "Out of memory");
				}
				
			} else
				entry->br_callback_time = 0;	
			dst_release(&rt->u.dst);
		} else {
			entry->br_callback_time = 0;
			break;
		}
	}
	write_unlock(&bcache_lock);

	while (br_info) {
		struct br_addrs *tmp = br_info->next;
		if (mipv6_send_brr(&br_info->saddr, &br_info->daddr, NULL) < 0)
			DEBUG(DBG_WARNING,
			      "BR send for %x:%x:%x:%x:%x:%x:%x:%x failed",
			      NIPV6ADDR(&br_info->daddr));
		kfree(br_info);
		br_info = tmp;
	}
}

static void set_timer(void)
{
	struct mipv6_bce *entry;
	unsigned long callback_time;

	DEBUG_FUNC();

	entry = (struct mipv6_bce *)
		hashlist_get_first(bcache.entries);
	if (entry != NULL) {
		if (entry->br_callback_time > 0 && 
		    time_after(entry->br_callback_time, jiffies))
			callback_time = entry->br_callback_time;
		else if (time_after(entry->callback_time, jiffies))
			callback_time = entry->callback_time;
		else {
			DEBUG(DBG_WARNING, 
			      "bcache timer attempted to schedule"
			      " for a historical jiffies count!");
			callback_time = jiffies + TIMERDELAY;
		}
		
		DEBUG(DBG_INFO, "setting timer to now");
		mod_timer(&bcache.callback_timer, callback_time);
	} else {
		del_timer(&bcache.callback_timer);
		DEBUG(DBG_INFO, "BC empty, not setting a new timer");
	}
}

/* 
 * The function that is scheduled to do the callback functions. May be
 * modified e.g to allow Binding Requests, now only calls expire() and
 * schedules a new timer.
 */
static void timer_handler(unsigned long dummy)
{
	expire();
	write_lock(&bcache_lock);
	set_timer();
	write_unlock(&bcache_lock);
}

/*
 * Interface functions visible to other modules
 */

/**
 * mipv6_bcache_add - add Binding Cache entry
 * @ifindex: interface index
 * @our_addr: own address
 * @home_addr_org: MN's home address
 * @coa: MN's care-of address
 * @lifetime: lifetime for this binding
 * @prefix: prefix length
 * @seq: sequence number
 * @flags: flags received in BU
 * @type: type of entry
 *
 * Adds an entry for this @home_addr_org in the Binding Cache.  If entry
 * already exists, old entry is updated.  @type may be %CACHE_ENTRY or
 * %HOME_REGISTRATION.
 **/
int mipv6_bcache_add(int ifindex,
		     struct in6_addr *our_addr,
		     struct in6_addr *home_addr,
		     struct in6_addr *coa,
		     __u32 lifetime, __u16 seq, __u8 flags, __u8 type)
{
	struct mipv6_bce *entry;
	int update = 0;
	int create_tunnel = 0;
	unsigned long now = jiffies;
	struct in6_addr_pair hashkey;
	int ret = -1;

	DEBUG_FUNC();

	hashkey.a1 = home_addr;
	hashkey.a2 = our_addr;

	write_lock(&bcache_lock);

	if (type == HOME_REGISTRATION && !(mip6node_cnf.capabilities&CAP_HA))
		return 0;

	if (unlikely(bcache.entries == NULL)) {
		ret = -ENOMEM;
		goto err;
	}

	if ((entry = (struct mipv6_bce *)
	     hashlist_get(bcache.entries, &hashkey)) != NULL) {
		/* if an entry for this home_addr exists (with smaller
		 * seq than the new seq), update it by removing it
		 * first
		 */
		if (!MIPV6_SEQ_GT(seq, entry->seq)) {
			DEBUG(DBG_INFO, "smaller seq than existing, not updating");
			goto out;
		}
		DEBUG(DBG_INFO, "updating an existing entry");
		update = 1;

		if ((flags & MIPV6_BU_F_HOME) != (entry->flags & MIPV6_BU_F_HOME)) {
			/* XXX: still open question, should we
                           send error to MN or silently ignore */
			DEBUG(DBG_INFO, "entry/BU flag mismatch");
			goto out;
		}
		if (type == HOME_REGISTRATION) {
			create_tunnel = (ipv6_addr_cmp(&entry->coa, coa) ||
					 entry->ifindex != ifindex);
		}
	} else {
		/* no entry for this home_addr, try to create a new entry */
		DEBUG(DBG_INFO, "creating a new entry");
		update = 0;

		entry = mipv6_bce_alloc(type);
		if (entry == NULL) {
			DEBUG(DBG_INFO, "cache full, entry not added");
			goto err;
		}

		create_tunnel = (type == HOME_REGISTRATION);
	}

	if (create_tunnel) {
		if (update)
			mip6_fn.proxy_del(&entry->home_addr, entry);
		if (mip6_fn.proxy_create(flags, ifindex, coa, our_addr, home_addr) < 0) {
			goto err_proxy;
		}
	}

	ipv6_addr_copy(&(entry->our_addr), our_addr);
	ipv6_addr_copy(&(entry->home_addr), home_addr);
	ipv6_addr_copy(&(entry->coa), coa);
	entry->ifindex = ifindex;
	entry->seq = seq;
	entry->type = type;
	entry->flags = flags;
	
	entry->last_br = 0;
	entry->destunr_count = 0;
	entry->callback_time = now + lifetime * HZ;
	if (entry->type & HOME_REGISTRATION)
		entry->br_callback_time = 0;
	else
		entry->br_callback_time = now +
			(lifetime - BCACHE_BR_SEND_LEAD) * HZ;
	
	if (update) {
		DEBUG(DBG_INFO, "updating entry : %x", entry);
		hashlist_reposition(bcache.entries, (void *)entry, 
				    entry->callback_time);
	} else {
		DEBUG(DBG_INFO, "adding entry: %x", entry);
		if ((hashlist_add(bcache.entries,
				  &hashkey,
				  entry->callback_time, entry)) < 0) {
			
			DEBUG(DBG_ERROR, "Hash add failed");
			goto err_hashlist;
		}
	}
	
	set_timer();
	
out:
	write_unlock(&bcache_lock);
	return 0;

err_hashlist:
	if (create_tunnel) {
		mip6_fn.proxy_del(home_addr, entry);
	}
err_proxy:
	if (update) {
		hashlist_delete(bcache.entries, (void *)entry);
	}
	mipv6_bce_free(entry);
err:
	write_unlock(&bcache_lock);
	return ret;
}

/**
 * mipv6_bcache_delete - delete Binding Cache entry
 * @home_addr: MN's home address
 * @our_addr: our address
 * @type: type of entry
 *
 * Deletes an entry associated with @home_addr from Binding Cache.
 * Valid values for @type are %CACHE_ENTRY, %HOME_REGISTRATION and
 * %ANY_ENTRY.  %ANY_ENTRY deletes any type of entry.
 **/
int mipv6_bcache_delete(struct in6_addr *home_addr,
			struct in6_addr *our_addr, __u8 type)
{
	struct mipv6_bce *entry;
	struct in6_addr_pair hashkey;
	int err = 0;

	DEBUG_FUNC();

	if (home_addr == NULL || our_addr == NULL) {
		DEBUG(DBG_INFO, "error in arguments");
		return -EINVAL;
	}

	hashkey.a1 = home_addr;
	hashkey.a2 = our_addr;

	write_lock(&bcache_lock);

	if (unlikely(bcache.entries == NULL) ||
	    (entry = (struct mipv6_bce *)
	     hashlist_get(bcache.entries, &hashkey)) == NULL ||
	    !(entry->type & type)) {
		DEBUG(DBG_INFO, "No matching entry found");
		err = -ENOENT;
		goto out;
	}

	hashlist_delete(bcache.entries, (void *) entry);
	mipv6_bce_free(entry);

	set_timer();
out:
	write_unlock(&bcache_lock);
	return err;
}

/**
 * mipv6_bcache_exists - check if entry exists
 * @home_addr: home address to check
 * @our_addr: our address
 *
 * Determines if a binding exists for @home_addr.  Returns type of the
 * entry or negative if entry does not exist.
 **/
int mipv6_bcache_exists(struct in6_addr *home_addr,
			struct in6_addr *our_addr)
{
	struct mipv6_bce *entry;
	struct in6_addr_pair hashkey;
	int type = -ENOENT;

	DEBUG_FUNC();

	if (home_addr == NULL || our_addr == NULL)
		return -EINVAL;

	hashkey.a1 = home_addr;
	hashkey.a2 = our_addr;

	read_lock(&bcache_lock);
	if (likely(bcache.entries != NULL) &&
	    (entry = (struct mipv6_bce *)
	     hashlist_get(bcache.entries, &hashkey)) != NULL) {
		type = entry->type;
	}
	read_unlock(&bcache_lock);

	return type;
}

/**
 * mipv6_bcache_get - get entry from Binding Cache
 * @home_addr: home address to search
 * @our_addr: our address
 * @entry: pointer to buffer
 *
 * Gets a copy of Binding Cache entry for @home_addr. If entry 
 * exists entry is copied to @entry and zero is returned.  
 * Otherwise returns negative.
 **/
int mipv6_bcache_get(struct in6_addr *home_addr,
		     struct in6_addr *our_addr,
		     struct mipv6_bce *entry)
{
	struct mipv6_bce *entry2;
	struct in6_addr_pair hashkey;
	int ret = -ENOENT;

	DEBUG_FUNC();

	if (home_addr == NULL || our_addr == NULL || entry == NULL)
		return -EINVAL;

	hashkey.a1 = home_addr;
	hashkey.a2 = our_addr;

	read_lock_bh(&bcache_lock);

	entry2 = (struct mipv6_bce *)
		hashlist_get(bcache.entries, &hashkey);
	if (entry2 != NULL) {
		memcpy(entry, entry2, sizeof(struct mipv6_bce));
		ret = 0;
	}
	read_unlock_bh(&bcache_lock);
	return ret;
}

int mipv6_bcache_iterate(hashlist_iterator_t func, void *args)
{
	int ret;

	read_lock_bh(&bcache_lock);
	ret = hashlist_iterate(bcache.entries, args, func);
	read_unlock_bh(&bcache_lock);

	return ret;
}

/*
 * Proc-filesystem functions
 */

#define BC_INFO_LEN 80

struct procinfo_iterator_args {
	char *buffer;
	int offset;
	int length;
	int skip;
	int len;
};

static int procinfo_iterator(void *data, void *args, unsigned long *pref)
{
	struct procinfo_iterator_args *arg =
	    (struct procinfo_iterator_args *) args;
	struct mipv6_bce *entry =
	    (struct mipv6_bce *) data;

	ASSERT(entry != NULL);

	if (arg->skip < arg->offset / BC_INFO_LEN) {
		arg->skip++;
		return ITERATOR_CONT;
	}

	if (arg->len >= arg->length)
		return ITERATOR_CONT;

	/* HoA CoA CallbackInSecs Type */
	arg->len += sprintf(arg->buffer + arg->len,
			    "%08x%08x%08x%08x %08x%08x%08x%08x %010lu %02d\n",
			    ntohl(entry->home_addr.s6_addr32[0]),
			    ntohl(entry->home_addr.s6_addr32[1]),
			    ntohl(entry->home_addr.s6_addr32[2]),
			    ntohl(entry->home_addr.s6_addr32[3]),
			    ntohl(entry->coa.s6_addr32[0]),
			    ntohl(entry->coa.s6_addr32[1]),
			    ntohl(entry->coa.s6_addr32[2]),
			    ntohl(entry->coa.s6_addr32[3]),
			    ((entry->callback_time) - jiffies) / HZ,
			    (int) entry->type);

	return ITERATOR_CONT;
}

 /*
  * Callback function for proc filesystem.
  */
static int bcache_proc_info(char *buffer, char **start, off_t offset,
			    int length)
{
	struct procinfo_iterator_args args;

	DEBUG_FUNC();

	args.buffer = buffer;
	args.offset = offset;
	args.length = length;
	args.skip = 0;
	args.len = 0;

	read_lock_bh(&bcache_lock);
	hashlist_iterate(bcache.entries, &args, procinfo_iterator);
	read_unlock_bh(&bcache_lock);

	*start = buffer;
	if (offset)
		*start += offset % BC_INFO_LEN;

	args.len -= offset % BC_INFO_LEN;

	if (args.len > length)
		args.len = length;
	if (args.len < 0)
		args.len = 0;

	return args.len;
}

static int bcache_compare(void *data, void *hashkey)
{
	struct in6_addr_pair *p = (struct in6_addr_pair *) hashkey;
	struct mipv6_bce *e = (struct mipv6_bce *) data;

	if (ipv6_addr_cmp(&e->home_addr, p->a1) == 0
	    && ipv6_addr_cmp(&e->our_addr, p->a2) == 0)
		return 0;
	else
		return -1;
}

static __u32 bcache_hash(void *hashkey)
{
	struct in6_addr_pair *p = (struct in6_addr_pair *) hashkey;

	return p->a1->s6_addr32[0] ^ p->a1->s6_addr32[1] ^
		p->a2->s6_addr32[2] ^ p->a2->s6_addr32[3];
}

/* 
 * Initialization and shutdown functions
 */

int __init mipv6_bcache_init(__u32 size)
{
	if (size < 1) {
		DEBUG(DBG_ERROR, "Binding cache size must be at least 1");
		return -EINVAL;
	}
	bcache.entries = hashlist_create(MIPV6_BCACHE_HASHSIZE, size,
					 sizeof(struct mipv6_bce),
					 "mip6_bcache", NULL, NULL,
					 bcache_compare, bcache_hash);

	if (bcache.entries == NULL) {
		DEBUG(DBG_ERROR, "Failed to initialize hashlist");
		return -ENOMEM;
	}

	init_timer(&bcache.callback_timer);
	bcache.callback_timer.data = 0;
	bcache.callback_timer.function = timer_handler;
	bcache.size = size;

	proc_net_create("mip6_bcache", 0, bcache_proc_info);

	DEBUG(DBG_INFO, "Binding cache initialized");
	return 0;
}

static int 
bce_cleanup_iterator(void *rawentry, void *args, unsigned long *sortkey)
{
	int type = (int) args;
	struct mipv6_bce *entry = (struct mipv6_bce *) rawentry;
	if (entry->type == type) {
		if (entry->type & HOME_REGISTRATION) {
			if (unlikely(mip6_fn.proxy_del == NULL))
				DEBUG(DBG_ERROR, "proxy_del unitialized");
			else
				mip6_fn.proxy_del(&entry->home_addr, entry);
		}
		return ITERATOR_DELETE_ENTRY;
	}
	return ITERATOR_CONT;

}

void mipv6_bcache_cleanup(int type)
{
	write_lock_bh(&bcache_lock);
	hashlist_iterate(bcache.entries,(void *) type, bce_cleanup_iterator);
	write_unlock_bh(&bcache_lock);
}

int __exit mipv6_bcache_exit(void)
{
	struct hashlist *entries;

	DEBUG_FUNC();

	proc_net_remove("mip6_bcache");

	write_lock_bh(&bcache_lock);
	DEBUG(DBG_INFO, "Stopping the bcache timer");
	del_timer(&bcache.callback_timer);
	hashlist_iterate(bcache.entries,(void *)CACHE_ENTRY, 
			 bce_cleanup_iterator);

	entries = bcache.entries;
	bcache.entries = NULL;
	write_unlock_bh(&bcache_lock);

	hashlist_destroy(entries);
	return 0;
}

EXPORT_SYMBOL(mipv6_bcache_add);
EXPORT_SYMBOL(mipv6_bcache_cleanup);
EXPORT_SYMBOL(mipv6_bcache_delete);
EXPORT_SYMBOL(mipv6_bcache_get);
EXPORT_SYMBOL(mipv6_bcache_iterate);
