/*
 *	Mobile IPv6 Duplicate Address Detection Functions
 *
 *	Authors:
 *	Krishna Kumar <krkumar@us.ibm.com>
 *
 *      $Id: s.ndisc_ha.c 1.17 03/09/29 19:43:11+03:00 vnuorval@amber.hut.mediapoli.com $
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 */

#include <linux/autoconf.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/in6.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/mipv6.h>

#include "debug.h"
#include "bcache.h"
#include "ha.h" /* mipv6_generate_ll_addr */

extern void mipv6_bu_finish(
	struct inet6_ifaddr *ifp, int ifindex, __u8 ba_status,
	struct in6_addr *saddr, struct in6_addr *daddr,
	struct in6_addr *haddr, struct in6_addr *coa,
	__u32 ba_lifetime, __u16 sequence, __u8 flags, __u8 *k_bu);

/*
 * Binding Updates from MN are cached in this structure till DAD is performed.
 * This structure is used to retrieve a pending Binding Update for the HA to
 * reply to after performing DAD. The first cell is different from the rest as
 * follows :
 * 	1. The first cell is used to chain the remaining cells. 
 *	2. The timeout of the first cell is used to delete expired entries
 *	   in the list of cells, while the timeout of the other cells are
 *	   used for timing out a NS request so as to reply to a BU.
 *	3. The only elements of the first cell that are used are :
 *	   next, prev, and callback_timer.
 *
 * TODO : Don't we need to do pneigh_lookup on the Link Local address ?
 */
struct mipv6_dad_cell {
	/* Information needed for DAD management */
	struct mipv6_dad_cell	*next;	/* Next element on the DAD list */
	struct mipv6_dad_cell	*prev;	/* Prev element on the DAD list */
	__u16			probes;	/* Number of times to probe for addr */
	__u16			flags;	/* Entry flags - see below */
	struct timer_list	callback_timer; /* timeout for entry */

	/* Information needed for performing DAD */
	struct inet6_ifaddr	*ifp;
	int			ifindex;
	struct in6_addr		saddr;
	struct in6_addr		daddr;
	struct in6_addr		haddr;		/* home address */
	struct in6_addr		ll_haddr;	/* Link Local value of haddr */
	struct in6_addr		coa;
	__u32			ba_lifetime;
	__u16			sequence;
	__u8			bu_flags;
};

/* Values for the 'flags' field in the mipv6_dad_cell */
#define	DAD_INIT_ENTRY		0
#define	DAD_DUPLICATE_ADDRESS	1
#define	DAD_UNIQUE_ADDRESS	2

/* Head of the pending DAD list */
static struct mipv6_dad_cell dad_cell_head;

/* Lock to access the pending DAD list */
static rwlock_t dad_lock = RW_LOCK_UNLOCKED;

/* Timer routine which deletes 'expired' entries in the DAD list */
static void mipv6_dad_delete_old_entries(unsigned long unused)
{
	struct mipv6_dad_cell *curr, *next;
	unsigned long next_time = 0;

	write_lock(&dad_lock);
	curr = dad_cell_head.next;
	while (curr != &dad_cell_head) {
		next = curr->next;
		if (curr->flags != DAD_INIT_ENTRY) {
			if (curr->callback_timer.expires <= jiffies) {
				/* Entry has expired, free it up. */
				curr->next->prev = curr->prev;
				curr->prev->next = curr->next;
				in6_ifa_put(curr->ifp);
				kfree(curr);
			} else if (next_time <
				   curr->callback_timer.expires) {
				next_time = curr->callback_timer.expires;
			}
		}
		curr = next;
	}
	write_unlock(&dad_lock);
	if (next_time) {
		/*
		 * Start another timer if more cells need to be removed at
		 * a later stage.
		 */
		dad_cell_head.callback_timer.expires = next_time;
		add_timer(&dad_cell_head.callback_timer);
	}
}

/* 
 * Queue a timeout routine to clean up 'expired' DAD entries.
 */
static void mipv6_start_dad_head_timer(struct mipv6_dad_cell *cell)
{
	unsigned long expire = jiffies +
	    cell->ifp->idev->nd_parms->retrans_time * 10;

	if (!timer_pending(&dad_cell_head.callback_timer) ||
	    expire < dad_cell_head.callback_timer.expires) {
		/*
		 * Add timer if none pending, or mod the timer if new 
		 * cell needs to be expired before existing timer runs.
		 *
		 * We let the cell remain as long as possible, so that
		 * new BU's as part of retransmissions don't have to go
		 * through DAD before replying.
		 */
		dad_cell_head.callback_timer.expires = expire;

		/*
		 * Keep the cell around for atleast some time to handle
		 * retransmissions or BU's due to fast MN movement. This
		 * is needed otherwise a previous timeout can delete all
		 * expired entries including this new one.
		 */
		cell->callback_timer.expires = jiffies +
		    cell->ifp->idev->nd_parms->retrans_time * 5;
		if (!timer_pending(&dad_cell_head.callback_timer)) {
			add_timer(&dad_cell_head.callback_timer);
		} else {
			mod_timer(&dad_cell_head.callback_timer, expire);
		}
	}
}


/* Join solicited node MC address */
static inline void mipv6_join_sol_mc_addr(struct in6_addr *addr,
					  struct net_device *dev)
{
	struct in6_addr maddr;

	/* Join solicited node MC address */
	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_inc(dev, &maddr);
}

/* Leave solicited node MC address */
static inline void mipv6_leave_sol_mc_addr(struct in6_addr *addr,
					   struct net_device *dev)
{
	struct in6_addr maddr;

	addrconf_addr_solict_mult(addr, &maddr);
	ipv6_dev_mc_dec(dev, &maddr);
}

/* Send a NS */
static inline void mipv6_dad_send_ns(struct inet6_ifaddr *ifp,
				     struct in6_addr *haddr)
{
	struct in6_addr unspec;
	struct in6_addr mcaddr;

	ipv6_addr_set(&unspec, 0, 0, 0, 0);
	addrconf_addr_solict_mult(haddr, &mcaddr);

	/* addr is 'unspec' since we treat this address as transient */
	ndisc_send_ns(ifp->idev->dev, NULL, haddr, &mcaddr, &unspec);
}

/*
 * Search for a home address in the list of pending DAD's. Called from
 * Neighbor Advertisement
 * Return values :
 * 	-1 : No DAD entry found for this advertisement, or entry already
 *	     finished processing.
 *	0  : Entry found waiting for DAD to finish.
 */
static int dad_search_haddr(struct in6_addr *ll_haddr,
			    struct in6_addr *saddr, struct in6_addr *daddr,
			    struct in6_addr *haddr, struct in6_addr *coa,
			    __u16 * seq, struct inet6_ifaddr **ifp)
{
	struct mipv6_dad_cell *cell;

	read_lock(&dad_lock);
	cell = dad_cell_head.next;
	while (cell != &dad_cell_head &&
	       ipv6_addr_cmp(&cell->ll_haddr, ll_haddr)) {
		cell = cell->next;
	}
	if (cell == &dad_cell_head || cell->flags != DAD_INIT_ENTRY) {
		/* Not found element, or element already finished processing */
		if (cell != &dad_cell_head) {
			/*
			 * Set the state to DUPLICATE, even if it was UNIQUE
			 * earlier. It is not needed to setup timer via 
			 * mipv6_start_dad_head_timer since this must have
			 * already been done.
			 */
			cell->flags = DAD_DUPLICATE_ADDRESS;
		}
		read_unlock(&dad_lock);
		return -1;
	}

	/*
	 * The NA found an unprocessed entry in the DAD list. Expire this
	 * entry since another node advertised this address. Caller should
	 * reject BU (DAD failed).
	 */
	ipv6_addr_copy(saddr, &cell->saddr);
	ipv6_addr_copy(daddr, &cell->daddr);
	ipv6_addr_copy(haddr, &cell->haddr);
	ipv6_addr_copy(coa, &cell->coa);
	*seq = cell->sequence;
	*ifp = cell->ifp;

	if (del_timer(&cell->callback_timer) == 0) {
		/* Timer already deleted, race with Timeout Handler */
		/* No action needed */
	}

	cell->flags = DAD_DUPLICATE_ADDRESS;

	/* Now leave this address to avoid future processing of NA's */
	mipv6_leave_sol_mc_addr(&cell->ll_haddr, cell->ifp->idev->dev);

	/* Start dad_head timer to remove this entry */
	mipv6_start_dad_head_timer(cell);

	read_unlock(&dad_lock);

	return 0;
}

/* ENTRY routine called via Neighbor Advertisement */
void mipv6_check_dad(struct in6_addr *ll_haddr)
{
	struct in6_addr saddr, daddr, haddr, coa;
	struct inet6_ifaddr *ifp;
	__u16 seq;

	if (dad_search_haddr(ll_haddr, &saddr, &daddr, &haddr, &coa, &seq,
			     &ifp) < 0) {
		/* 
		 * Didn't find entry, or no action needed (the action has
		 * already been performed).
		 */
		return;
	}

	/*
	 * A DAD cell was present, meaning that there is a pending BU
	 * request for 'haddr' - reject the BU.
	 */
	mipv6_bu_finish(ifp, 0, DUPLICATE_ADDR_DETECT_FAIL,
			&saddr, &daddr, &haddr, &coa, 0, seq, 0, NULL);
	return;
}

/*
 * Check if the passed 'cell' is in the list of pending DAD's. Called from
 * the Timeout Handler.
 *
 * Assumes that the caller is holding the dad_lock in reader mode.
 */
static int dad_search_cell(struct mipv6_dad_cell *cell)
{
	struct mipv6_dad_cell *tmp;

	tmp = dad_cell_head.next;
	while (tmp != &dad_cell_head && tmp != cell) {
		tmp = tmp->next;
	}
	if (tmp == cell) {
		if (cell->flags == DAD_INIT_ENTRY) {
			/* Found valid entry */
			if (--cell->probes == 0) {
				/*
				 * Retransmission's are over - return success.
				 */
				cell->flags = DAD_UNIQUE_ADDRESS;

				/* 
				 * Leave this address to avoid future 
				 * processing of NA's.
				 */
				mipv6_leave_sol_mc_addr(&cell->ll_haddr,
							cell->ifp->idev->
							dev);

				/* start timeout to delete this cell. */
				mipv6_start_dad_head_timer(cell);
				return 0;
			}
			/*
			 * Retransmission not finished, send another NS and
			 * return failure.
			 */
			mipv6_dad_send_ns(cell->ifp, &cell->ll_haddr);
			cell->callback_timer.expires = jiffies +
			    cell->ifp->idev->nd_parms->retrans_time;
			add_timer(&cell->callback_timer);
		} else {
			/*
			 * This means that an NA was received before the
			 * timeout and when the state changed from
			 * DAD_INIT_ENTRY, the BU got failed as a result.
			 * There is nothing to be done.
			 */
		}
	}
	return -1;
}

/* ENTRY routine called via Timeout */
static void mipv6_dad_timeout(unsigned long arg)
{
	__u8 ba_status = SUCCESS;
	struct in6_addr saddr;
	struct in6_addr daddr;
	struct in6_addr haddr;
	struct in6_addr coa;
	struct inet6_ifaddr *ifp;
	int ifindex;
	__u32 ba_lifetime;
	__u16 sequence;
	__u8 flags;
	struct mipv6_dad_cell *cell = (struct mipv6_dad_cell *) arg;

	/*
	 * If entry is not in the list, we have already sent BU Failure
	 * after getting a NA.
	 */
	read_lock(&dad_lock);
	if (dad_search_cell(cell) < 0) {
		/*
		 * 'cell' is no longer valid (may not be in the list or
		 * is already processed, due to NA processing), or NS
		 * retransmissions are not yet over.
		 */
		read_unlock(&dad_lock);
		return;
	}

	/* This is the final Timeout. Send Bind Ack Success */

	ifp = cell->ifp;
	ifindex = cell->ifindex;
	ba_lifetime = cell->ba_lifetime;
	sequence = cell->sequence;
	flags = cell->bu_flags;

	ipv6_addr_copy(&saddr, &cell->saddr);
	ipv6_addr_copy(&daddr, &cell->daddr);
	ipv6_addr_copy(&haddr, &cell->haddr);
	ipv6_addr_copy(&coa, &cell->coa);
	read_unlock(&dad_lock);

	/* Send BU Acknowledgement Success */
	mipv6_bu_finish(ifp, ifindex, ba_status, 
			&saddr, &daddr, &haddr, &coa,
			ba_lifetime, sequence, flags, NULL);
	return;
}

/*
 * Check if original home address exists in our DAD pending list, if so return
 * the cell.
 *
 * Assumes that the caller is holding the dad_lock in writer mode.
 */
static struct mipv6_dad_cell *mipv6_dad_get_cell(struct in6_addr *haddr)
{
	struct mipv6_dad_cell *cell;

	cell = dad_cell_head.next;
	while (cell != &dad_cell_head
	       && ipv6_addr_cmp(&cell->haddr, haddr)) {
		cell = cell->next;
	}
	if (cell == &dad_cell_head) {
		/* Not found element */
		return NULL;
	}
	return cell;
}

/*
 * Save all parameters needed for doing a Bind Ack in the mipv6_dad_cell 
 * structure.
 */
static void mipv6_dad_save_cell(struct mipv6_dad_cell *cell,
				struct inet6_ifaddr *ifp, int ifindex,
				struct in6_addr *saddr,
				struct in6_addr *daddr,
				struct in6_addr *haddr,
				struct in6_addr *coa, __u32 ba_lifetime,
				__u16 sequence, __u8 flags)
{
	in6_ifa_hold(ifp);
	cell->ifp = ifp;
	cell->ifindex = ifindex;

	ipv6_addr_copy(&cell->saddr, saddr);
	ipv6_addr_copy(&cell->daddr, daddr);
	ipv6_addr_copy(&cell->haddr, haddr);
	ipv6_addr_copy(&cell->coa, coa);

	/* Convert cell->ll_haddr to Link Local address */
	if (flags & MIPV6_BU_F_LLADDR) 
		mipv6_generate_ll_addr(&cell->ll_haddr, haddr);
	else 
		ipv6_addr_copy(&cell->ll_haddr, haddr);

	cell->ba_lifetime = ba_lifetime;
	cell->sequence = sequence;
	cell->bu_flags = flags;
}

/*
 * Top level DAD routine for performing DAD.
 *
 * Return values
 *	0     : Don't need to do DAD.
 *	1     : Need to do DAD.
 *	-n    : Error, where 'n' is the reason for the error.
 *
 * Assumption : DAD process has been optimized by using cached values upto
 * some time. However sometimes this can cause problems. Eg. when the first
 * BU was received, DAD might have failed. Before the second BU arrived,
 * the node using MN's home address might have stopped using it, but still
 * we will return DAD_DUPLICATE_ADDRESS based on the first DAD's result. Or 
 * this can go the other way around. However, it is a very small possibility
 * and thus optimization is turned on by default. It is possible to change
 * this feature (needs a little code-rewriting in this routine), but 
 * currently DAD result is being cached for performance reasons.
 */
int mipv6_dad_start(struct inet6_ifaddr *ifp, int ifindex,
		    struct in6_addr *saddr, struct in6_addr *daddr,
		    struct in6_addr *haddr, struct in6_addr *coa,
		    __u32 ba_lifetime, __u16 sequence, __u8 flags)
{
	int found;
	struct mipv6_dad_cell *cell;
	struct mipv6_bce bc_entry;

	if (ifp->idev->cnf.dad_transmits == 0) {
		/* DAD is not configured on the HA, return SUCCESS */
		return 0;
	}

	if (mipv6_bcache_get(haddr, daddr, &bc_entry) == 0) {
		/*
		 * We already have an entry in our cache - don't need to 
		 * do DAD as we are already defending this home address.
		 */
		return 0;
	}

	write_lock(&dad_lock);
	if ((cell = mipv6_dad_get_cell(haddr)) != NULL) {
		/*
		 * An existing entry for BU was found in our cache due
		 * to retransmission of the BU or a new COA registration.
		 */
		switch (cell->flags) {
		case DAD_INIT_ENTRY:
			/* Old entry is waiting for DAD to complete */
			break;
		case DAD_UNIQUE_ADDRESS:
			/* DAD is finished successfully - return success. */
			write_unlock(&dad_lock);
			return 0;
		case DAD_DUPLICATE_ADDRESS:
			/*
			 * DAD is finished and we got a NA while doing BU -
			 * return failure.
			 */
			write_unlock(&dad_lock);
			return -DUPLICATE_ADDR_DETECT_FAIL;
		default:
			/* Unknown state - should never happen */
			DEBUG(DBG_WARNING,
			      "cell entry in unknown state : %d",
			      cell->flags);
			write_unlock(&dad_lock);
			return -REASON_UNSPECIFIED;
		}
		found = 1;
	} else {
		if ((cell = (struct mipv6_dad_cell *)
		     kmalloc(sizeof(struct mipv6_dad_cell), GFP_ATOMIC))
		    == NULL) {
			return -INSUFFICIENT_RESOURCES;
		}
		found = 0;
	}

	mipv6_dad_save_cell(cell, ifp, ifindex, saddr, daddr, haddr, coa,
			    ba_lifetime, sequence, flags);

	if (!found) {
		cell->flags = DAD_INIT_ENTRY;
		cell->probes = ifp->idev->cnf.dad_transmits;

		/* Insert element on dad_cell_head list */
		dad_cell_head.prev->next = cell;
		cell->next = &dad_cell_head;
		cell->prev = dad_cell_head.prev;
		dad_cell_head.prev = cell;
		write_unlock(&dad_lock);

		/* join the solicited node MC of the homeaddr. */
		mipv6_join_sol_mc_addr(&cell->ll_haddr, ifp->idev->dev);

		/* Send a NS */
		mipv6_dad_send_ns(ifp, &cell->ll_haddr);

		/* Initialize timer for this cell to timeout the NS. */
		init_timer(&cell->callback_timer);
		cell->callback_timer.data = (unsigned long) cell;
		cell->callback_timer.function = mipv6_dad_timeout;
		cell->callback_timer.expires = jiffies +
		    ifp->idev->nd_parms->retrans_time;
		add_timer(&cell->callback_timer);
	} else {
		write_unlock(&dad_lock);
	}
	return 1;
}

void __init mipv6_dad_init(void)
{
	dad_cell_head.next = dad_cell_head.prev = &dad_cell_head;
	init_timer(&dad_cell_head.callback_timer);
	dad_cell_head.callback_timer.data = 0;
	dad_cell_head.callback_timer.function =
	    mipv6_dad_delete_old_entries;
}

void __exit mipv6_dad_exit(void)
{
	struct mipv6_dad_cell *curr, *next;

	write_lock_bh(&dad_lock);
	del_timer(&dad_cell_head.callback_timer);

	curr = dad_cell_head.next;
	while (curr != &dad_cell_head) {
		next = curr->next;
		del_timer(&curr->callback_timer);
		if (curr->flags == DAD_INIT_ENTRY) {
			/*
			 * We were in DAD_INIT state and listening to the
			 * solicited node MC address - need to stop that.
			 */
			mipv6_leave_sol_mc_addr(&curr->ll_haddr,
						curr->ifp->idev->dev);
		}
		in6_ifa_put(curr->ifp);
		kfree(curr);
		curr = next;
	}
	dad_cell_head.next = dad_cell_head.prev = &dad_cell_head;
	write_unlock_bh(&dad_lock);
}
