/**
 * Prefix advertisement for Home Agent
 *
 * Authors:
 * Jaakko Laine <medved@iki.fi>
 *
 * $Id: s.prefix_ha.c 1.4 03/04/10 13:02:40+03:00 anttit@jon.mipl.mediapoli.com $
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/icmpv6.h>
#include <linux/net.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/mipv6.h>

#include "mipv6_icmp.h"
#include "debug.h"
#include "sortedlist.h"
#include "util.h"
#include "bcache.h"
#include "config.h"
#include "prefix.h"

/**
 * pfx_adv_iterator - modify pfx_list entries according to new prefix info
 * @data: MN's home registration bcache_entry
 * @args: new prefix info
 * @sortkey: ignored
 */
static int pfx_adv_iterator(void *data, void *args, unsigned long sortkey)
{
	struct mipv6_bce *bc_entry = (struct mipv6_bce *) data;
	struct prefix_info *pinfo = (struct prefix_info *) args;

	if (mipv6_prefix_compare(&bc_entry->coa, &pinfo->prefix,
				 pinfo->prefix_len) == 0) {
		struct pfx_list_entry pfx_entry;

		memcpy(&pfx_entry.daddr, &bc_entry->coa,
		       sizeof(struct in6_addr));
		memcpy(&pfx_entry.daddr, &bc_entry->our_addr,
		       sizeof(struct in6_addr));
		pfx_entry.retries = 0;
		pfx_entry.ifindex = bc_entry->ifindex;

		mipv6_slist_modify(&pfx_list, &pfx_entry,
				   sizeof(struct pfx_list_entry),
				   jiffies +
				   net_random() % (MAX_PFX_ADV_DELAY * HZ),
				   compare_pfx_list_entry);
	}

	return 0;
}

struct homereg_iterator_args {
	struct list_head *head;
	int count;
};

static int homereg_iterator(void *data, void *args, unsigned long *sortkey)
{
	struct mipv6_bce *entry = (struct mipv6_bce *) data;
	struct homereg_iterator_args *state =
		(struct homereg_iterator_args *) args;

	if (entry->type == HOME_REGISTRATION) {
		mipv6_slist_add(state->head, entry,
				sizeof(struct mipv6_bce),
				state->count);
		state->count++;
	}
	return 0;
}

static int mipv6_bcache_get_homeregs(struct list_head *head)
{
	struct homereg_iterator_args args;

	DEBUG_FUNC();

	args.count = 0;
	args.head = head;

	mipv6_bcache_iterate(homereg_iterator, &args);
	return args.count;
}

/**
 * mipv6_prefix_added - prefix was added to interface, act accordingly
 * @pinfo: prefix_info that was added
 * @ifindex: interface index
 */
void mipv6_pfxs_modified(struct prefix_info *pinfo, int ifindex)
{
	int count;
	unsigned long tmp;
	struct list_head home_regs;
	extern rwlock_t pfx_list_lock;

	DEBUG_FUNC();

	INIT_LIST_HEAD(&home_regs);

	if (!(count = mipv6_bcache_get_homeregs(&home_regs)))
		return;

	write_lock_bh(&pfx_list_lock);
	mipv6_slist_for_each(&home_regs, pinfo, pfx_adv_iterator);
	if ((tmp = mipv6_slist_get_first_key(&pfx_list)))
		mod_timer(&pfx_timer, tmp);
	write_unlock_bh(&pfx_list_lock);
}
