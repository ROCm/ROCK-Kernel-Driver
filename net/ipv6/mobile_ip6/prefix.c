/**
 * Prefix solicitation and advertisement
 *
 * Authors:
 * Jaakko Laine <medved@iki.fi>
 *
 * $Id: s.prefix.c 1.30 03/04/10 13:09:54+03:00 anttit@jon.mipl.mediapoli.com $
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
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
#include "prefix.h"
#include "config.h"

#define INFINITY 0xffffffff

struct timer_list pfx_timer;

struct list_head pfx_list;
rwlock_t pfx_list_lock = RW_LOCK_UNLOCKED;

int compare_pfx_list_entry(const void *data1, const void *data2,
			   int datalen)
{
	struct pfx_list_entry *e1 = (struct pfx_list_entry *) data1;
	struct pfx_list_entry *e2 = (struct pfx_list_entry *) data2;

	return ((ipv6_addr_cmp(&e1->daddr, &e2->daddr) == 0)
		&& (e2->ifindex == -1 || e1->ifindex == e2->ifindex));
}

/**
 * mipv6_pfx_cancel_send - cancel pending items to daddr from saddr
 * @daddr: Destination address
 * @ifindex: pending items on this interface will be canceled
 *
 * if ifindex == -1, all items to daddr will be removed
 */
void mipv6_pfx_cancel_send(struct in6_addr *daddr, int ifindex)
{
	unsigned long tmp;
	struct pfx_list_entry entry;

	DEBUG_FUNC();

	/* We'll just be comparing these parts... */
	memcpy(&entry.daddr, daddr, sizeof(struct in6_addr));
	entry.ifindex = ifindex;

	write_lock_bh(&pfx_list_lock);

	while (mipv6_slist_del_item(&pfx_list, &entry,
				    compare_pfx_list_entry) == 0)
		;

	if ((tmp = mipv6_slist_get_first_key(&pfx_list)))
		mod_timer(&pfx_timer, tmp);

	write_unlock_bh(&pfx_list_lock);
}

/**
 * mipv6_pfx_add_ha - add a new HA to send prefix solicitations to
 * @daddr: address of HA
 * @saddr: our address to use as source address
 * @ifindex: interface index
 */
void mipv6_pfx_add_ha(struct in6_addr *daddr, struct in6_addr *saddr,
		      int ifindex)
{
	unsigned long tmp;
	struct pfx_list_entry entry;

	DEBUG_FUNC();

	memcpy(&entry.daddr, daddr, sizeof(struct in6_addr));
	memcpy(&entry.saddr, saddr, sizeof(struct in6_addr));
	entry.retries = 0;
	entry.ifindex = ifindex;

	write_lock_bh(&pfx_list_lock);
	if (mipv6_slist_modify(&pfx_list, &entry, sizeof(struct pfx_list_entry),
			       jiffies + INITIAL_SOLICIT_TIMER * HZ,
			       compare_pfx_list_entry))
		DEBUG(DBG_WARNING, "Cannot add new HA to pfx list");

	if ((tmp = mipv6_slist_get_first_key(&pfx_list)))
		mod_timer(&pfx_timer, tmp);
	write_unlock_bh(&pfx_list_lock);
}

int mipv6_pfx_add_home(int ifindex, struct in6_addr *saddr, 
		       struct in6_addr *daddr, unsigned long min_expire)
{
	unsigned long tmp;

	write_lock(&pfx_list_lock);

	if (min_expire != INFINITY) {
		unsigned long expire;
		struct pfx_list_entry entry;
		
		memcpy(&entry.daddr, saddr, sizeof(struct in6_addr));
		memcpy(&entry.saddr, daddr, sizeof(struct in6_addr));
		entry.retries = 0;
		entry.ifindex = ifindex;

		/* This is against the draft, but we need to set
		 * a minimum interval for a prefix solicitation.
		 * Otherwise a prefix solicitation storm will
		 * result if valid lifetime of the prefix is
		 * smaller than MAX_PFX_ADV_DELAY
		 */
		min_expire -= MAX_PFX_ADV_DELAY;
		min_expire = min_expire < MIN_PFX_SOL_DELAY ? MIN_PFX_SOL_DELAY : min_expire;

		expire = jiffies + min_expire * HZ;

		if (mipv6_slist_modify(&pfx_list, &entry,
				       sizeof(struct pfx_list_entry),
				       expire,
				       compare_pfx_list_entry) != 0)
			DEBUG(DBG_WARNING, "Cannot add new entry to pfx_list");
	}

	if ((tmp = mipv6_slist_get_first_key(&pfx_list)))
		mod_timer(&pfx_timer, tmp);

	write_unlock(&pfx_list_lock);

	return 0;
}

/**
 * set_ha_pfx_list - manipulate pfx_list for HA when timer goes off
 * @entry: pfx_list_entry that is due
 */
static void set_ha_pfx_list(struct pfx_list_entry *entry)
{
}

/**
 * set_mn_pfx_list - manipulate pfx_list for MN when timer goes off
 * @entry: pfx_list_entry that is due
 */
static void set_mn_pfx_list(struct pfx_list_entry *entry)
{
}

/**
 * pfx_timer_handler - general timer handler
 * @dummy: dummy
 *
 * calls set_ha_pfx_list and set_mn_pfx_list to do the thing when
 * a timer goes off
 */
static void pfx_timer_handler(unsigned long dummy)
{
	unsigned long tmp;
	struct pfx_list_entry *entry;

	DEBUG_FUNC();

	write_lock(&pfx_list_lock);
	if (!(entry = mipv6_slist_get_first(&pfx_list)))
		goto out;

	if (mip6node_cnf.capabilities & CAP_HA)
		set_ha_pfx_list(entry);
	if (mip6node_cnf.capabilities & CAP_MN)
		set_mn_pfx_list(entry);
	if ((tmp = mipv6_slist_get_first_key(&pfx_list)))
		mod_timer(&pfx_timer, tmp);

 out:
	write_unlock(&pfx_list_lock);
}

int mipv6_initialize_pfx_icmpv6(void)
{
	INIT_LIST_HEAD(&pfx_list);

	init_timer(&pfx_timer);
	pfx_timer.function = pfx_timer_handler;

	return 0;
}

void mipv6_shutdown_pfx_icmpv6(void)
{
	struct prefix_info *tmp;

	if (timer_pending(&pfx_timer))
		del_timer(&pfx_timer);

	write_lock_bh(&pfx_list_lock);
	while ((tmp = mipv6_slist_del_first(&pfx_list)))
		kfree(tmp);
	write_unlock_bh(&pfx_list_lock);
}

EXPORT_SYMBOL(mipv6_initialize_pfx_icmpv6);
EXPORT_SYMBOL(mipv6_pfx_add_home);
EXPORT_SYMBOL(mipv6_shutdown_pfx_icmpv6);
EXPORT_SYMBOL(compare_pfx_list_entry);
EXPORT_SYMBOL(pfx_list);
EXPORT_SYMBOL(pfx_timer);
EXPORT_SYMBOL(mipv6_pfx_cancel_send);
EXPORT_SYMBOL(pfx_list_lock);
