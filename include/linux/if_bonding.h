/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
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
 */

#ifndef _LINUX_IF_BONDING_H
#define _LINUX_IF_BONDING_H

#ifdef __KERNEL__
#include <linux/timer.h>
#include <linux/if.h>
#include <linux/proc_fs.h>
#endif /* __KERNEL__ */

#include <linux/types.h>

/*
 * We can remove these ioctl definitions in 2.5.  People should use the
 * SIOC*** versions of them instead
 */
#define BOND_ENSLAVE_OLD		(SIOCDEVPRIVATE)
#define BOND_RELEASE_OLD		(SIOCDEVPRIVATE + 1)
#define BOND_SETHWADDR_OLD		(SIOCDEVPRIVATE + 2)
#define BOND_SLAVE_INFO_QUERY_OLD	(SIOCDEVPRIVATE + 11)
#define BOND_INFO_QUERY_OLD		(SIOCDEVPRIVATE + 12)
#define BOND_CHANGE_ACTIVE_OLD		(SIOCDEVPRIVATE + 13)

#define BOND_CHECK_MII_STATUS	(SIOCGMIIPHY)

#define BOND_MODE_ROUNDROBIN    0
#define BOND_MODE_ACTIVEBACKUP  1
#define BOND_MODE_XOR           2 

/* each slave's link has 4 states */
#define BOND_LINK_UP    0           /* link is up and running */
#define BOND_LINK_FAIL  1           /* link has just gone down */
#define BOND_LINK_DOWN  2           /* link has been down for too long time */
#define BOND_LINK_BACK  3           /* link is going back */

/* each slave has several states */
#define BOND_STATE_ACTIVE       0   /* link is active */
#define BOND_STATE_BACKUP       1   /* link is backup */

#define BOND_DEFAULT_MAX_BONDS  1   /* Default maximum number of devices to support */

typedef struct ifbond {
	__s32 bond_mode;
	__s32 num_slaves;
	__s32 miimon;
} ifbond;

typedef struct ifslave
{
	__s32 slave_id; /* Used as an IN param to the BOND_SLAVE_INFO_QUERY ioctl */
	char slave_name[IFNAMSIZ];
	char link;
	char state;
	__u32  link_failure_count;
} ifslave;

#ifdef __KERNEL__
typedef struct slave {
	struct slave *next;
	struct slave *prev;
	struct net_device *dev;
	short  delay;
	char   link;    /* one of BOND_LINK_XXXX */
	char   state;   /* one of BOND_STATE_XXXX */
	unsigned short original_flags;
	u32 link_failure_count;
} slave_t;

/*
 * Here are the locking policies for the two bonding locks:
 *
 * 1) Get bond->lock when reading/writing slave list.
 * 2) Get bond->ptrlock when reading/writing bond->current_slave.
 *    (It is unnecessary when the write-lock is put with bond->lock.)
 * 3) When we lock with bond->ptrlock, we must lock with bond->lock
 *    beforehand.
 */
typedef struct bonding {
	slave_t *next;
	slave_t *prev;
	slave_t *current_slave;
	__s32 slave_cnt;
	rwlock_t lock;
	rwlock_t ptrlock;
	struct timer_list mii_timer;
	struct timer_list arp_timer;
	struct net_device_stats *stats;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *bond_proc_dir;
	struct proc_dir_entry *bond_proc_info_file;
#endif /* CONFIG_PROC_FS */
	struct bonding *next_bond;
	struct net_device *device;
	struct dev_mc_list *mc_list;
	unsigned short flags;
} bonding_t;
#endif /* __KERNEL__ */

#endif /* _LINUX_BOND_H */

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
