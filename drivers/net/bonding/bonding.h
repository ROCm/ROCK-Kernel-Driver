/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 * 
 *
 * 2003/03/18 - Amir Noam <amir.noam at intel dot com>,
 *		Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for IEEE 802.3ad Dynamic link aggregation mode.
 *
 * 2003/05/01 - Tsippy Mendelson <tsippy.mendelson at intel dot com> and
 *		Amir Noam <amir.noam at intel dot com>
 *	- Code beautification and style changes (mainly in comments).
 *
 * 2003/05/01 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Added support for Transmit load balancing mode.
 */
 
#ifndef _LINUX_BONDING_H
#define _LINUX_BONDING_H

#include <linux/timer.h>
#include <linux/proc_fs.h>
#include "bond_3ad.h"
#include "bond_alb.h"

#define DRV_VERSION	"2.4.1"
#define DRV_RELDATE	"September 15, 2003"
#define DRV_NAME	"bonding"
#define DRV_DESCRIPTION	"Ethernet Channel Bonding Driver"

#ifdef BONDING_DEBUG
#define dprintk(fmt, args...) \
	printk(KERN_DEBUG     \
	       DRV_NAME ": %s() %d: " fmt, __FUNCTION__, __LINE__ , ## args )
#else
#define dprintk(fmt, args...)
#endif /* BONDING_DEBUG */

#define IS_UP(dev)  ((((dev)->flags & (IFF_UP)) == (IFF_UP)) && \
		     (netif_running(dev) && netif_carrier_ok(dev)))

/* Checks whether the dev is ready for transmit. We do not check netif_running
 * since a device can be stopped by the driver for short periods of time for
 * maintainance. dev_queue_xmit() handles this by queing the packet until the
 * the dev is running again. Keeping packets ordering requires sticking the
 * same dev as much as possible
 */
#define SLAVE_IS_OK(slave) \
		     ((((slave)->dev->flags & (IFF_UP)) == (IFF_UP)) && \
		     netif_carrier_ok((slave)->dev) && \
		     ((slave)->link == BOND_LINK_UP) && \
		     ((slave)->state == BOND_STATE_ACTIVE))


struct slave {
	struct slave *next;
	struct slave *prev;
	struct net_device *dev;
	short  delay;
	unsigned long jiffies;
	char   link;    /* one of BOND_LINK_XXXX */
	char   state;   /* one of BOND_STATE_XXXX */
	unsigned short original_flags;
	u32 link_failure_count;
	u16    speed;
	u8     duplex;
	u8     perm_hwaddr[ETH_ALEN];
	struct ad_slave_info ad_info; /* HUGE - better to dynamically alloc */
	struct tlb_slave_info tlb_info;
};

/*
 * Here are the locking policies for the two bonding locks:
 *
 * 1) Get bond->lock when reading/writing slave list.
 * 2) Get bond->ptrlock when reading/writing bond->current_slave.
 *    (It is unnecessary when the write-lock is put with bond->lock.)
 * 3) When we lock with bond->ptrlock, we must lock with bond->lock
 *    beforehand.
 */
struct bonding {
	struct slave *first_slave;
	struct slave *current_slave;
	struct slave *primary_slave;
	struct slave *current_arp_slave;
	int slave_cnt; /* never change this value outside the attach/detach wrappers */
	rwlock_t lock;
	rwlock_t ptrlock;
	struct timer_list mii_timer;
	struct timer_list arp_timer;
	int kill_timers;
	struct net_device_stats stats;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *bond_proc_file;
	char procdir_name[IFNAMSIZ];
#endif /* CONFIG_PROC_FS */
	struct list_head bond_list;
	struct net_device *device;
	struct dev_mc_list *mc_list;
	unsigned short flags;
	struct ad_bond_info ad_info;
	struct alb_bond_info alb_info;
};

/**
 * bond_for_each_slave_from - iterate the slaves list from a starting point
 * @bond:	the bond holding this list.
 * @pos:	current slave.
 * @cnt:	counter for max number of moves
 * @start:	starting point.
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave_from(bond, pos, cnt, start)	\
	for (cnt = 0, pos = start;			\
	     cnt < (bond)->slave_cnt;			\
             cnt++, pos = (pos)->next)

/**
 * bond_for_each_slave_from_to - iterate the slaves list from start point to stop point
 * @bond:	the bond holding this list.
 * @pos:	current slave.
 * @cnt:	counter for number max of moves
 * @start:	start point.
 * @stop:	stop point.
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave_from_to(bond, pos, cnt, start, stop)	\
	for (cnt = 0, pos = start;					\
	     ((cnt < (bond)->slave_cnt) && (pos != (stop)->next));	\
             cnt++, pos = (pos)->next)

/**
 * bond_for_each_slave - iterate the slaves list from head
 * @bond:	the bond holding this list.
 * @pos:	current slave.
 * @cnt:	counter for max number of moves
 *
 * Caller must hold bond->lock
 */
#define bond_for_each_slave(bond, pos, cnt)	\
		bond_for_each_slave_from(bond, pos, cnt, (bond)->first_slave)

/**
 * Returns NULL if the net_device does not belong to any of the bond's slaves
 *
 * Caller must hold bond lock for read
 */
extern inline struct slave *
bond_get_slave_by_dev(struct bonding *bond, struct net_device *slave_dev)
{
	struct slave *slave = NULL;
	int i;

	bond_for_each_slave(bond, slave, i) {
		if (slave->dev == slave_dev) {
			break;
		}
	}

	return slave;
}

extern inline struct bonding *
bond_get_bond_by_slave(struct slave *slave)
{
	if (!slave || !slave->dev->master) {
		return NULL;
	}

	return (struct bonding *)slave->dev->master->priv;
}

/* Forward declarations */
void bond_set_slave_active_flags(struct slave *slave);
void bond_set_slave_inactive_flags(struct slave *slave);

#endif /* _LINUX_BONDING_H */

