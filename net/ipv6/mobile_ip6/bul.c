/*
 *      Binding update list
 *
 *      Authors:
 *      Juha Mynttinen            <jmynttin@cc.hut.fi>
 *
 *      $Id: s.bul.c 1.93 03/09/22 16:45:04+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 *	Changes:
 *
 *	Nanno Langstraat	:	Timer code cleaned up
 */

#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/in6.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <net/ipv6.h>
#include <net/mipv6.h>
#include <linux/proc_fs.h>

#include "bul.h"
#include "debug.h"
#include "hashlist.h"
#include "tunnel_mn.h"
#include "mobhdr.h"

#define MIPV6_BUL_HASHSIZE 32

rwlock_t bul_lock = RW_LOCK_UNLOCKED;

struct mipv6_bul {
	struct hashlist *entries;
	struct timer_list callback_timer;
};

static struct mipv6_bul bul;

struct in6_addr_pair {
	struct in6_addr *a1;
	struct in6_addr *a2;
};

/**********************************************************************
 *
 * Private functions
 *
 **********************************************************************/

static int bul_compare(void *data, void *hashkey)
{
	struct in6_addr_pair *p = (struct in6_addr_pair *)hashkey;
	struct mipv6_bul_entry *e = (struct mipv6_bul_entry *)data;

	if (ipv6_addr_cmp(&e->cn_addr, p->a1) == 0
	    && ipv6_addr_cmp(&e->home_addr, p->a2) == 0)
		return 0;
	else
		return -1;
}

struct test_keys {
	struct in6_addr *addr;
	u8 *cookie;
};

static int bul_compare_cookie(void *data, void *keys)
{
	struct test_keys *p = (struct test_keys *)keys;
	struct mipv6_bul_entry *e = (struct mipv6_bul_entry *)data;

	if (ipv6_addr_cmp(&e->cn_addr, p->addr) == 0 && e->rr
	    && memcmp(&e->rr->cot_cookie, p->cookie, 8) == 0)
		return 0;
	else
		return -1;
}

static u32 bul_hash(void *hashkey)
{
	struct in6_addr_pair *p = (struct in6_addr_pair *)hashkey;
	
	return p->a1->s6_addr32[0] ^
		p->a1->s6_addr32[1] ^
		p->a1->s6_addr32[2] ^
		p->a1->s6_addr32[3];
}

static int bul_proc_info(char *buffer, char **start, off_t offset,
			    int length);

static struct mipv6_bul_entry *mipv6_bul_get_entry(void)
{
	DEBUG_FUNC();
	return ((struct mipv6_bul_entry *) 
		hashlist_alloc(bul.entries, SLAB_ATOMIC));
}

static void mipv6_bul_entry_free(struct mipv6_bul_entry *entry)
{
	DEBUG_FUNC();		

	if (entry->rr) {
		if (entry->rr->kbu)
			kfree(entry->rr->kbu);
		kfree(entry->rr);
	}
	if (entry->ops)
		kfree(entry->ops);
	hashlist_free(bul.entries, (void *)entry);
}

static __inline__ int del_bul_entry_tnl(struct mipv6_bul_entry *entry) 
{
	if (entry->flags & MIPV6_BU_F_HOME) {
		return mipv6_mv_tnl_to_ha(&entry->cn_addr, 
                                          &entry->coa,
                                          &entry->home_addr, 0);
	}
	return 0;
}

static void timer_update(void)
{
	struct mipv6_bul_entry *entry;

	DEBUG_FUNC();

	entry = hashlist_get_first(bul.entries);

	while (entry && time_after_eq(jiffies, entry->callback_time)) {
		if (time_after_eq(jiffies, entry->expire) ||
		    entry->callback(entry) != 0) {
			/*
			 * Either the entry has expired, or the callback
			 * indicated that it should be deleted.
			 */
			hashlist_delete(bul.entries, (void *)entry);
			
			del_bul_entry_tnl(entry);
			mipv6_bul_entry_free(entry);
			DEBUG(DBG_INFO, "Entry deleted (was expired) from "
			      "binding update list");
		} else {
			/* move entry to its right place in the hashlist */
			DEBUG(DBG_INFO, "Rescheduling");
			hashlist_reposition(bul.entries, (void *)entry,
					    entry->callback_time);
		}
		entry = (struct mipv6_bul_entry *)
			hashlist_get_first(bul.entries);
	}

	if (entry == NULL) {
		DEBUG(DBG_INFO, "bul empty, not setting a new timer");
		del_timer(&bul.callback_timer);
	} else {
		mod_timer(&bul.callback_timer, entry->callback_time);
	}
}

static void timer_handler(unsigned long dummy)
{
	DEBUG_FUNC();

	write_lock(&bul_lock);
	timer_update();
	write_unlock(&bul_lock);
}

/**********************************************************************
 *
 * Public interface functions
 *
 **********************************************************************/

/**
 * mipv6_bul_iterate - apply interator function to all entries
 * @func: function to apply
 * @args: extra arguments for iterator
 *
 * Applies @func for each entry in Binding Update List.  Extra
 * arguments given in @args are also passed to the iterator function.
 * Caller must hold @bul_lock.
 **/
int mipv6_bul_iterate(hashlist_iterator_t func, void *args)
{
	DEBUG_FUNC();

	return hashlist_iterate(bul.entries, args, func);
}

/**
 * mipv6_bul_exists - check if Binding Update List entry exists
 * @cn: address to check
 *
 * Checks if Binding Update List has an entry for @cn.  Returns true
 * if entry exists, false otherwise. Caller may not hold @bul_lock.
 **/
int mipv6_bul_exists(struct in6_addr *cn, struct in6_addr *haddr)
{
	int exists;
	struct in6_addr_pair hashkey;

	DEBUG_FUNC();

	hashkey.a1 = cn;
	hashkey.a2 = haddr;
	
	read_lock_bh(&bul_lock);

	if (unlikely(bul.entries == NULL))
		exists = 0;
	else
		exists = (hashlist_get(bul.entries, &hashkey) != NULL);

	read_unlock_bh(&bul_lock);
	return exists;
}

/**
 * mipv6_bul_get - get Binding Update List entry
 * @cn_addr: CN address to search
 * @home_addr: home address to search
 *
 * Returns Binding Update List entry for @cn_addr if it exists.
 * Otherwise returns %NULL.  Caller must hold @bul_lock.
 **/
struct mipv6_bul_entry *mipv6_bul_get(struct in6_addr *cn_addr, 
				      struct in6_addr *home_addr)
{
	struct mipv6_bul_entry *entry;
	struct in6_addr_pair hashkey;
	
	DEBUG_FUNC();

	if (unlikely(bul.entries == NULL)) {
		return NULL;
	}
	hashkey.a1 = cn_addr;
	hashkey.a2 = home_addr;

	entry = (struct mipv6_bul_entry *) 
		hashlist_get(bul.entries, &hashkey);
		
	return entry;
}

struct mipv6_bul_entry *mipv6_bul_get_by_ccookie(
	struct in6_addr *cn_addr, u8 *cookie)
{
	struct test_keys key;

	DEBUG_FUNC();

	if (unlikely(bul.entries == NULL))
		return NULL;
	key.addr = cn_addr;
	key.cookie = cookie;

	return (struct mipv6_bul_entry *) 
		hashlist_get_ex(bul.entries, &key,
				bul_compare_cookie);
}

/**
 * mipv6_bul_reschedule - reschedule Binding Update List entry
 * @entry: entry to reschedule
 *
 * Reschedules a Binding Update List entry.  Must be called after
 * modifying entry lifetime.  Caller must hold @bul_lock (write).
 **/
void mipv6_bul_reschedule(struct mipv6_bul_entry *entry)
{
	DEBUG_FUNC();

	hashlist_reposition(bul.entries,
			    (void *)entry,
			    entry->callback_time);
	timer_update();
}

/**
 * mipv6_bul_add - add binding update to Binding Update List
 * @cn_addr: IPv6 address where BU was sent
 * @home_addr: Home address for this binding
 * @coa: Care-of address for this binding
 * @lifetime: expiration time of the binding in seconds
 * @seq: sequence number of the BU
 * @flags: %MIPV6_BU_F_* flags
 * @callback: callback function called on expiration
 * @callback_time: expiration time for callback
 * @state: binding send state
 * @delay: retransmission delay
 * @maxdelay: retransmission maximum delay
 * @ops: Mobility header options for BU
 * @rr: Return routability information
 *
 * Adds a binding update sent to @cn_addr for @home_addr to the
 * Binding Update List.  If entry already exists, it is updated.
 * Entry is set to expire in @lifetime seconds.  Entry has a callback
 * function @callback that is called at @callback_time.  Entry @state
 * controls resending of this binding update and it can be set to
 * %ACK_OK, %RESEND_EXP or %ACK_ERROR.  Returns a pointer to the newly
 * created or updated entry.  Caller must hold @bul_lock (write).
 **/
struct mipv6_bul_entry *mipv6_bul_add(
	struct in6_addr *cn_addr, struct in6_addr *home_addr,
	struct in6_addr *coa, 
	__u32 lifetime,	__u16 seq, __u8 flags,
	int (*callback)(struct mipv6_bul_entry *entry),
	__u32 callback_time, 
	__u8 state, __u32 delay, __u32 maxdelay,
	struct mipv6_mh_opt *ops, 
	struct mipv6_rr_info *rr)
{
	struct mipv6_bul_entry *entry;
	int update = 0;
	struct in6_addr_pair hashkey;

	DEBUG_FUNC();

	if (unlikely(bul.entries == NULL))
		return NULL;

	if (cn_addr == NULL || home_addr == NULL || coa == NULL || 
	    lifetime < 0 || callback == NULL || callback_time < 0 || 
	    (state != ACK_OK && state != RESEND_EXP && state != ACK_ERROR) ||
	    delay < 0 || maxdelay < 0) {
		DEBUG(DBG_ERROR, "invalid arguments");
		return NULL;
	}
	DEBUG(DBG_INFO, "cn_addr: %x:%x:%x:%x:%x:%x:%x:%x, "
	      "home_addr: %x:%x:%x:%x:%x:%x:%x:%x"
	      "coaddr: %x:%x:%x:%x:%x:%x:%x:%x", NIPV6ADDR(cn_addr), 
	       NIPV6ADDR(home_addr), NIPV6ADDR(coa));
	hashkey.a1 = cn_addr;
	hashkey.a2 = home_addr;
	
	/* 
	 * decide whether to add a new entry or update existing, also
	 * check if there's room for a new entry when adding a new
	 * entry (latter is handled by mipv6_bul_get_entry() 
	 */
	if ((entry = (struct mipv6_bul_entry *)
	     hashlist_get(bul.entries, &hashkey)) != NULL) {
		/* if an entry for this cn_addr exists (with smaller
		 * seq than the new entry's seq), update it */
		
		if (MIPV6_SEQ_GT(seq, entry->seq)) {
			DEBUG(DBG_INFO, "updating an existing entry");
			update = 1;
		} else {
			DEBUG(DBG_INFO, "smaller seq than existing, not updating");
			return NULL;
		}
	} else {
		entry = mipv6_bul_get_entry();
		if (entry == NULL) {
			DEBUG(DBG_WARNING, "binding update list full, can't add!!!");
			return NULL;
		}
		memset(entry, 0, sizeof(*entry));
		/* First BU send happens here, save count in the entry */
		entry->consecutive_sends = 1;
	}

	if (!update) {
		ipv6_addr_copy(&(entry->cn_addr), cn_addr);
		ipv6_addr_copy(&(entry->home_addr), home_addr);
		entry->ops = ops;
	}
	/* Add Return Routability info to bul entry */
	if (rr) {
		if(entry->rr)
			kfree(entry->rr); 
		entry->rr = rr;
	}

	ipv6_addr_copy(&(entry->coa), coa);
	entry->lifetime = lifetime;
	if (lifetime)
		entry->expire = jiffies + lifetime * HZ;
	else if (flags & MIPV6_BU_F_ACK)
		entry->expire = jiffies + HOME_RESEND_EXPIRE * HZ;
	entry->seq = seq;
	entry->flags = flags;
	entry->lastsend = jiffies; /* current time = last use of the entry */
	entry->state = state;
	entry->delay = delay;
	entry->maxdelay = maxdelay;
	entry->callback_time = jiffies + callback_time * HZ;
	entry->callback = callback;

	if (flags & MIPV6_BU_F_HOME && 
	    mipv6_mv_tnl_to_ha(cn_addr, coa, home_addr, 1)) {
		DEBUG(DBG_ERROR, "reconfiguration of the tunnel failed");
	}
	if (update) {
		DEBUG(DBG_INFO, "updating entry: %x", entry);
		hashlist_reposition(bul.entries, (void *)entry,
				    entry->callback_time);
	} else {
		DEBUG(DBG_INFO, "adding entry: %x", entry);

		hashkey.a1 = &entry->cn_addr;
		hashkey.a2 = &entry->home_addr;

		if ((hashlist_add(bul.entries, &hashkey,
				  entry->callback_time,
				  entry)) < 0) {
			DEBUG(DBG_ERROR, "Hash add failed");
			mipv6_bul_entry_free(entry);			
			return NULL;
		}
	}
	timer_update();	

	return entry;
}

/**
 * mipv6_bul_delete - delete Binding Update List entry
 * @cn_addr: address for entry to delete
 *
 * Deletes the entry for @cn_addr from the Binding Update List.
 * Returns zero if entry was deleted succesfully, otherwise returns
 * negative.  Caller may not hold @bul_lock.
 **/
int mipv6_bul_delete(struct in6_addr *cn_addr, struct in6_addr *home_addr)
{
	struct mipv6_bul_entry *entry;
	struct in6_addr_pair hashkey;

	DEBUG_FUNC();

	hashkey.a1 = cn_addr;
	hashkey.a2 = home_addr;

	write_lock(&bul_lock);

	if (unlikely(bul.entries == NULL) ||  
	    (entry = (struct mipv6_bul_entry *)
	     hashlist_get(bul.entries, &hashkey)) == NULL) {
		write_unlock(&bul_lock);
		DEBUG(DBG_INFO, "No such entry");
		return -ENOENT;
	}

	hashlist_delete(bul.entries, (void *)entry);

	del_bul_entry_tnl(entry);

	mipv6_bul_entry_free(entry);
	timer_update();
	write_unlock(&bul_lock);

	DEBUG(DBG_INFO, "Binding update list entry deleted");

	return 0;
}

/**********************************************************************
 *
 * Proc interface functions
 *
 **********************************************************************/

#define BUL_INFO_LEN 152

struct procinfo_iterator_args {
	char *buffer;
	int offset;
	int length;
	int skip;
	int len;
};

static int procinfo_iterator(void *data, void *args,
			     unsigned long *sortkey)
{
	struct procinfo_iterator_args *arg =
		(struct procinfo_iterator_args *)args;
	struct mipv6_bul_entry *entry =
		(struct mipv6_bul_entry *)data;
	unsigned long callback_seconds;

	DEBUG_FUNC();

	if (entry == NULL) return ITERATOR_ERR;

	if (time_after(jiffies, entry->callback_time))
		callback_seconds = 0;
	else
		callback_seconds = (entry->callback_time - jiffies) / HZ;

	if (arg->skip < arg->offset / BUL_INFO_LEN) {
		arg->skip++;
		return ITERATOR_CONT;
	}

	if (arg->len >= arg->length)
		return ITERATOR_CONT;

	/* CN HoA CoA ExpInSecs SeqNum State Delay MaxDelay CallbackInSecs */
	arg->len += sprintf(arg->buffer + arg->len,
			    "%08x%08x%08x%08x %08x%08x%08x%08x %08x%08x%08x%08x\n"
			    "%010lu %05d %02d %010d %010d %010lu\n",
			    ntohl(entry->cn_addr.s6_addr32[0]),
			    ntohl(entry->cn_addr.s6_addr32[1]),
			    ntohl(entry->cn_addr.s6_addr32[2]),
			    ntohl(entry->cn_addr.s6_addr32[3]),
			    ntohl(entry->home_addr.s6_addr32[0]),
			    ntohl(entry->home_addr.s6_addr32[1]),
			    ntohl(entry->home_addr.s6_addr32[2]),
			    ntohl(entry->home_addr.s6_addr32[3]),
			    ntohl(entry->coa.s6_addr32[0]),
			    ntohl(entry->coa.s6_addr32[1]),
			    ntohl(entry->coa.s6_addr32[2]),
			    ntohl(entry->coa.s6_addr32[3]),
			    (entry->expire - jiffies) / HZ,
			    entry->seq, entry->state, entry->delay, 
			    entry->maxdelay, callback_seconds);

	return ITERATOR_CONT;
}


/*
 * Callback function for proc filesystem.
 */
static int bul_proc_info(char *buffer, char **start, off_t offset,
                            int length)
{
	struct procinfo_iterator_args args;

	DEBUG_FUNC();

	args.buffer = buffer;
	args.offset = offset;
	args.length = length;
	args.skip = 0;
	args.len = 0;

	read_lock_bh(&bul_lock);
	hashlist_iterate(bul.entries, &args, procinfo_iterator);
	read_unlock_bh(&bul_lock);

	*start = buffer;
	if (offset)
		*start += offset % BUL_INFO_LEN;

	args.len -= offset % BUL_INFO_LEN;

	if (args.len > length)
		args.len = length;
	if (args.len < 0)
		args.len = 0;
	
	return args.len;
}

/**********************************************************************
 *
 * Code module init/fini functions
 *
 **********************************************************************/

int __init mipv6_bul_init(__u32 size)
{
	DEBUG_FUNC();

	if (size < 1) {
		DEBUG(DBG_CRITICAL, 
		      "Binding update list size must be at least 1");
		return -EINVAL;
	}
	bul.entries = hashlist_create(MIPV6_BUL_HASHSIZE, size, 
				       sizeof(struct mipv6_bul_entry),
				       "mip6_bul", NULL, NULL,
				       bul_compare, bul_hash);

	if (bul.entries == NULL) {
		DEBUG(DBG_CRITICAL, "Couldn't allocate memory for "
		      "hashlist when creating a binding update list");
		return -ENOMEM;
	}
	init_timer(&bul.callback_timer);
	bul.callback_timer.data = 0;
	bul.callback_timer.function = timer_handler;
	proc_net_create("mip6_bul", 0, bul_proc_info);
	DEBUG(DBG_INFO, "Binding update list initialized");
	return 0;
}

void __exit mipv6_bul_exit()
{
	struct mipv6_bul_entry *entry;
	struct hashlist *entries;

	DEBUG_FUNC();

	proc_net_remove("mip6_bul");

	write_lock_bh(&bul_lock);

	DEBUG(DBG_INFO, "Stopping the bul timer");
	del_timer(&bul.callback_timer);

	while ((entry = (struct mipv6_bul_entry *) 
		hashlist_get_first(bul.entries)) != NULL) {
		hashlist_delete(bul.entries, (void *)entry);
		
		del_bul_entry_tnl(entry);
		
		mipv6_bul_entry_free(entry);
	}
	entries = bul.entries;
	bul.entries = NULL;
	write_unlock_bh(&bul_lock); 

	hashlist_destroy(entries);

	DEBUG(DBG_INFO, "binding update list destroyed");
}
