/*
 *      Home Agents List
 *
 *      Authors:
 *      Antti Tuominen          <ajtuomin@tml.hut.fi>
 *
 *      $Id: s.halist.c 1.37 03/09/26 11:23:33+03:00 vnuorval@ron.mipl.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#define PREF_BASE 50000

#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <net/ipv6.h>
#include <net/addrconf.h>

#include "hashlist.h"
#include "util.h"
#include "debug.h"

struct mipv6_halist {
	struct hashlist *entries;
	struct timer_list expire_timer;
};

static rwlock_t home_agents_lock = RW_LOCK_UNLOCKED;

static struct mipv6_halist home_agents;

struct mipv6_halist_entry {
	struct hashlist_entry e;
	int ifindex;			 /* Link identifier		*/
	struct in6_addr link_local_addr; /* HA's link-local address	*/
	struct in6_addr global_addr;	 /* HA's Global address 	*/
	int plen;
	long preference;		 /* The preference for this HA	*/
	unsigned long expire;		 /* expiration time (jiffies)	*/
};

static inline void mipv6_ha_ac_add(struct in6_addr *ll_addr, int ifindex,
				   struct in6_addr *glob_addr, int plen)
{
	struct net_device *dev;

	if ((dev = __dev_get_by_index(ifindex)) && ipv6_chk_addr(ll_addr, dev, 0)) {
		struct in6_addr addr;
		mipv6_ha_anycast(&addr, glob_addr, plen);
		ipv6_dev_ac_inc(dev, &addr);
	}
}

static inline void mipv6_ha_ac_del(struct in6_addr *ll_addr, int ifindex,
				   struct in6_addr *glob_addr, int plen)
{
	struct net_device *dev;

	if ((dev = __dev_get_by_index(ifindex)) && ipv6_chk_addr(ll_addr, dev, 0)) {
		struct in6_addr addr;
		mipv6_ha_anycast(&addr, glob_addr, plen);
		ipv6_dev_ac_dec(dev, &addr);
	}
}

struct preflist_iterator_args {
	int count;
	int requested;
	int ifindex;
	struct in6_addr *list;
};

static int preflist_iterator(void *data, void *args,
			     unsigned long *pref)
{
	struct preflist_iterator_args *state =
		(struct preflist_iterator_args *)args;
	struct mipv6_halist_entry *entry =
		(struct mipv6_halist_entry *)data;
	struct in6_addr *newaddr =
		(struct in6_addr *)state->list + state->count;

	if (state->count >= state->requested)
		return ITERATOR_STOP;

	if (time_after(jiffies, entry->expire)) {
		if (!ipv6_addr_any(&entry->link_local_addr)) {
			mipv6_ha_ac_del(&entry->link_local_addr, 
					entry->ifindex, 
					&entry->global_addr, entry->plen);
		}
		return ITERATOR_DELETE_ENTRY;
	}
	if (state->ifindex != entry->ifindex)
		return ITERATOR_CONT;

	ipv6_addr_copy(newaddr, &entry->global_addr);
	state->count++;

	return ITERATOR_CONT;
}

static int gc_iterator(void *data, void *args,
		       unsigned long *pref)
{
	struct mipv6_halist_entry *entry =
		(struct mipv6_halist_entry *)data;

	int *type = (int *)args;

	if (*type == 1 || time_after(jiffies, entry->expire)) {
		if (!ipv6_addr_any(&entry->link_local_addr)) {
			mipv6_ha_ac_del(&entry->link_local_addr, 
					entry->ifindex, 
					&entry->global_addr, entry->plen);
		}
		return ITERATOR_DELETE_ENTRY;
	}

	return ITERATOR_CONT;
}

static int mipv6_halist_gc(int type)
{
	DEBUG_FUNC();
	hashlist_iterate(home_agents.entries, &type, gc_iterator);
	return 0;
}

static void mipv6_halist_expire(unsigned long dummy)
{
	DEBUG_FUNC();

	write_lock(&home_agents_lock);
	mipv6_halist_gc(0);
	write_unlock(&home_agents_lock);
}


static struct mipv6_halist_entry *mipv6_halist_new_entry(void)
{
	struct mipv6_halist_entry *entry;

	DEBUG_FUNC();

	entry = hashlist_alloc(home_agents.entries, SLAB_ATOMIC);

	return entry;
}



/**
 * mipv6_halist_add - Add new home agent to the Home Agents List
 * @ifindex: interface identifier
 * @glob_addr: home agent's global address
 * @ll_addr: home agent's link-local address
 * @pref: relative preference for this home agent
 * @lifetime: lifetime for the entry
 *
 * Adds new home agent to the Home Agents List.  The list is interface
 * specific and @ifindex tells through which interface the home agent
 * was heard.  Returns zero on success and negative on failure.
 **/

int mipv6_halist_add(int ifindex, struct in6_addr *glob_addr, int plen,
		     struct in6_addr *ll_addr, int pref, __u32 lifetime)
{
	int update = 0, ret = 0;
	long mpref;
	struct mipv6_halist_entry *entry = NULL;

	DEBUG_FUNC();

	write_lock(&home_agents_lock);

	if (glob_addr == NULL || lifetime <= 0) {
		DEBUG(DBG_WARNING, "invalid arguments");
		ret = -EINVAL;
		goto out;
	}
	mpref = PREF_BASE - pref;
	if ((entry = (struct mipv6_halist_entry *)
	     hashlist_get(home_agents.entries, glob_addr)) != NULL) {
		if (entry->ifindex == ifindex) {
			DEBUG(DBG_DATADUMP, "updating old entry");
			update = 1;
		} else {
			update = 0;
		}
	}
	if (update) {
		entry->expire = jiffies + lifetime * HZ;
		if (entry->preference != mpref) {
			entry->preference = mpref;
			ret = hashlist_reposition(home_agents.entries, 
						  (void *)entry, mpref);
		}
	} else {
		entry = mipv6_halist_new_entry();
		if (entry == NULL) {
			DEBUG(DBG_INFO, "list full");
			ret = -ENOMEM;
			goto out;
		}
		entry->ifindex = ifindex;
		if (ll_addr) {
			ipv6_addr_copy(&entry->link_local_addr, ll_addr);
			mipv6_ha_ac_add(ll_addr, ifindex, glob_addr, plen);
		} else
			ipv6_addr_set(&entry->link_local_addr, 0, 0, 0, 0);

		ipv6_addr_copy(&entry->global_addr, glob_addr);
		entry->plen = plen;
		entry->preference = mpref;
		entry->expire = jiffies + lifetime * HZ;
		ret = hashlist_add(home_agents.entries, glob_addr, mpref, 
				   entry);
	}
out:
	write_unlock(&home_agents_lock);
	return ret;
}

/**
 * mipv6_halist_delete - delete home agent from Home Agents List
 * @glob_addr: home agent's global address
 *
 * Deletes entry for home agent @glob_addr from the Home Agent List.
 **/
int mipv6_halist_delete(struct in6_addr *glob_addr)
{
	struct hashlist_entry *e;
	struct mipv6_halist_entry *entry;
	DEBUG_FUNC();

	if (glob_addr == NULL) {
		DEBUG(DBG_WARNING, "invalid glob addr");
		return -EINVAL;
	}
	write_lock(&home_agents_lock);
	if ((e = hashlist_get(home_agents.entries, glob_addr)) == NULL) {
		write_unlock(&home_agents_lock);
		return -ENOENT;
	}
	hashlist_delete(home_agents.entries, e);
	entry = (struct mipv6_halist_entry *)e;
	if (!ipv6_addr_any(&entry->link_local_addr)) {
		mipv6_ha_ac_del(&entry->link_local_addr, entry->ifindex, 
				&entry->global_addr, entry->plen);
	}
	hashlist_free(home_agents.entries, e);
	write_unlock(&home_agents_lock);
	return 0;
}

/**
 * mipv6_ha_get_pref_list - Get list of preferred home agents
 * @ifindex: interface identifier
 * @addrs: pointer to a buffer to store the list
 * @max: maximum number of home agents to return
 *
 * Creates a list of @max preferred (or all known if less than @max)
 * home agents.  Home Agents List is interface specific so you must
 * supply @ifindex.  Stores list in addrs and returns number of home
 * agents stored.  On failure, returns a negative value.
 **/
int mipv6_ha_get_pref_list(int ifindex, struct in6_addr **addrs, int max)
{
	struct preflist_iterator_args args;

	if (max <= 0) {
		*addrs = NULL;
		return 0;
	}

	args.count = 0;
	args.requested = max;
	args.ifindex = ifindex;
	args.list = kmalloc(max * sizeof(struct in6_addr), GFP_ATOMIC);

	if (args.list == NULL) return -ENOMEM;

	read_lock(&home_agents_lock);
	hashlist_iterate(home_agents.entries, &args, preflist_iterator);
	read_unlock(&home_agents_lock);

	if (args.count >= 0) {
		*addrs = args.list;
	} else {
		kfree(args.list);
		*addrs = NULL;
	}

	return args.count;
}

struct getaddr_iterator_args {
	struct net_device *dev;
	struct in6_addr *addr;
};

static int getaddr_iterator(void *data, void *args,
	     unsigned long *pref)
{
	struct mipv6_halist_entry *entry =
		(struct mipv6_halist_entry *)data;
	struct getaddr_iterator_args *state =
		(struct getaddr_iterator_args *)args;

	if (entry->ifindex != state->dev->ifindex)
		return ITERATOR_CONT;

	if (ipv6_chk_addr(&entry->global_addr, state->dev, 0)) {
		ipv6_addr_copy(state->addr, &entry->global_addr);
		return ITERATOR_STOP;
	}
	return ITERATOR_CONT;
}

/*
 * Get Home Agent Address for given interface.  If node is not serving
 * as a HA for this interface returns negative error value.
 */
int mipv6_ha_get_addr(int ifindex, struct in6_addr *addr)
{
	struct getaddr_iterator_args args;
	struct net_device *dev;

	if (ifindex <= 0)
		return -EINVAL;

	if ((dev = dev_get_by_index(ifindex)) == NULL)
		return -ENODEV;

	memset(addr, 0, sizeof(struct in6_addr));
	args.dev = dev;
	args.addr = addr;
	read_lock(&home_agents_lock);
	hashlist_iterate(home_agents.entries, &args, getaddr_iterator);
	read_unlock(&home_agents_lock);
	dev_put(dev);

	if (ipv6_addr_any(addr))
		return -ENOENT;
	
	return 0;
}

#define HALIST_INFO_LEN 81

struct procinfo_iterator_args {
	char *buffer;
	int offset;
	int length;
	int skip;
	int len;
};

static int procinfo_iterator(void *data, void *args,
			     unsigned long *pref)
{
	struct procinfo_iterator_args *arg =
		(struct procinfo_iterator_args *)args;
	struct mipv6_halist_entry *entry =
		(struct mipv6_halist_entry *)data;
	unsigned long int expire;

	DEBUG_FUNC();

	if (entry == NULL) return ITERATOR_ERR;

	if (time_after(jiffies, entry->expire)) {
		if (!ipv6_addr_any(&entry->link_local_addr)) {
			mipv6_ha_ac_del(&entry->link_local_addr, 
					entry->ifindex, 
					&entry->global_addr, entry->plen);
		}
		return ITERATOR_DELETE_ENTRY;
	}
	if (arg->skip < arg->offset / HALIST_INFO_LEN) {
		arg->skip++;
		return ITERATOR_CONT;
	}

	if (arg->len >= arg->length)
		return ITERATOR_CONT;

	expire = (entry->expire - jiffies) / HZ;

	arg->len += sprintf(arg->buffer + arg->len, 
			    "%02d %08x%08x%08x%08x %08x%08x%08x%08x %05ld %05ld\n",
			    entry->ifindex,
			    ntohl(entry->global_addr.s6_addr32[0]),
			    ntohl(entry->global_addr.s6_addr32[1]),
			    ntohl(entry->global_addr.s6_addr32[2]),
			    ntohl(entry->global_addr.s6_addr32[3]),
			    ntohl(entry->link_local_addr.s6_addr32[0]),
			    ntohl(entry->link_local_addr.s6_addr32[1]),
			    ntohl(entry->link_local_addr.s6_addr32[2]),
			    ntohl(entry->link_local_addr.s6_addr32[3]),
			    -(entry->preference - PREF_BASE), expire);

	return ITERATOR_CONT;
}

static int halist_proc_info(char *buffer, char **start, off_t offset,
                            int length)
{
	struct procinfo_iterator_args args;

	DEBUG_FUNC();

	args.buffer = buffer;
	args.offset = offset;
	args.length = length;
	args.skip = 0;
	args.len = 0;

	read_lock_bh(&home_agents_lock);
	hashlist_iterate(home_agents.entries, &args, procinfo_iterator);
	read_unlock_bh(&home_agents_lock);

	*start = buffer;
	if (offset)
		*start += offset % HALIST_INFO_LEN;

	args.len -= offset % HALIST_INFO_LEN;

	if (args.len > length)
		args.len = length;
	if (args.len < 0)
		args.len = 0;
	
	return args.len;
}

static int halist_compare(void *data, void *hashkey)
{
	struct mipv6_halist_entry *e = (struct mipv6_halist_entry *)data;
	struct in6_addr *key = (struct in6_addr *)hashkey;

	return ipv6_addr_cmp(&e->global_addr, key);
}

static __u32 halist_hash(void *hashkey)
{
	struct in6_addr *key = (struct in6_addr *)hashkey;
	__u32 hash;

	hash = key->s6_addr32[0] ^
                key->s6_addr32[1] ^
                key->s6_addr32[2] ^
                key->s6_addr32[3];

	return hash;
}

int __init mipv6_halist_init(__u32 size)
{
	DEBUG_FUNC();

	if (size <= 0) {
		DEBUG(DBG_ERROR, "size must be at least 1");
		return -EINVAL;
	}
	init_timer(&home_agents.expire_timer);
	home_agents.expire_timer.data = 0;
	home_agents.expire_timer.function = mipv6_halist_expire;
	home_agents_lock = RW_LOCK_UNLOCKED;

	home_agents.entries = hashlist_create(16, size, sizeof(struct mipv6_halist_entry),
					       "mip6_halist", NULL, NULL,
					       halist_compare, halist_hash);
	if (home_agents.entries == NULL) {
		DEBUG(DBG_ERROR, "Failed to initialize hashlist");
		return -ENOMEM;
	}

	proc_net_create("mip6_home_agents", 0, halist_proc_info);
	DEBUG(DBG_INFO, "Home Agents List initialized");
	return 0;
}

void __exit mipv6_halist_exit(void)
{
	DEBUG_FUNC();
	proc_net_remove("mip6_home_agents");
	write_lock_bh(&home_agents_lock);
	DEBUG(DBG_INFO, "Stopping the halist timer");
	del_timer(&home_agents.expire_timer);
	mipv6_halist_gc(1);
	write_unlock_bh(&home_agents_lock);
	hashlist_destroy(home_agents.entries);
}
