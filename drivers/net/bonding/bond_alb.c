/*
 * Copyright(c) 1999 - 2003 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 *
 * Changes:
 *
 * 2003/06/25 - Shmulik Hen <shmulik.hen at intel dot com>
 *	- Fixed signed/unsigned calculation errors that caused load sharing
 *	  to collapse to one slave under very heavy UDP Tx stress.
 *
 * 2003/08/06 - Amir Noam <amir.noam at intel dot com>
 *	- Add support for setting bond's MAC address with special
 *	  handling required for ALB/TLB.
 */

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/pkt_sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/if_bonding.h>
#include <net/ipx.h>
#include <net/arp.h>
#include <asm/byteorder.h>
#include "bonding.h"
#include "bond_alb.h"


#define ALB_TIMER_TICKS_PER_SEC	    10	/* should be a divisor of HZ */
#define BOND_TLB_REBALANCE_INTERVAL 10	/* in seconds, periodic re-balancing
					 * used for division - never set
					 * to zero !!!
					 */
#define BOND_ALB_LP_INTERVAL	    1	/* in seconds periodic send of
					 * learning packets to the switch
					 */

#define BOND_TLB_REBALANCE_TICKS (BOND_TLB_REBALANCE_INTERVAL \
				  * ALB_TIMER_TICKS_PER_SEC)

#define BOND_ALB_LP_TICKS (BOND_ALB_LP_INTERVAL \
			   * ALB_TIMER_TICKS_PER_SEC)

#define TLB_HASH_TABLE_SIZE 256	/* The size of the clients hash table.
				 * Note that this value MUST NOT be smaller
				 * because the key hash table BYTE wide !
				 */


#define TLB_NULL_INDEX		0xffffffff
#define MAX_LP_RETRY		3

/* rlb defs */
#define RLB_HASH_TABLE_SIZE	256
#define RLB_NULL_INDEX		0xffffffff
#define RLB_UPDATE_DELAY	2*ALB_TIMER_TICKS_PER_SEC /* 2 seconds */
#define RLB_ARP_BURST_SIZE	2
#define RLB_UPDATE_RETRY	3	/* 3-ticks - must be smaller than the rlb
					 * rebalance interval (5 min).
					 */
/* RLB_PROMISC_TIMEOUT = 10 sec equals the time that the current slave is
 * promiscuous after failover
 */
#define RLB_PROMISC_TIMEOUT	10*ALB_TIMER_TICKS_PER_SEC

#pragma pack(1)
struct learning_pkt {
	u8 mac_dst[ETH_ALEN];
	u8 mac_src[ETH_ALEN];
	u16 type;
	u8 padding[ETH_ZLEN - (2*ETH_ALEN + 2)];
};

struct arp_pkt {
	u16     hw_addr_space;
	u16     prot_addr_space;
	u8      hw_addr_len;
	u8      prot_addr_len;
	u16     op_code;
	u8      mac_src[ETH_ALEN];	/* sender hardware address */
	u32     ip_src;			/* sender IP address */
	u8      mac_dst[ETH_ALEN];	/* target hardware address */
	u32     ip_dst;			/* target IP address */
};
#pragma pack()

/* Forward declaration */
static void alb_send_learning_packets(struct slave *slave, u8 mac_addr[]);

static inline u8
_simple_hash(u8 *hash_start, int hash_size)
{
	int i;
	u8 hash = 0;

	for (i=0; i<hash_size; i++) {
		hash ^= hash_start[i];
	}

	return hash;
}

/*********************** tlb specific functions ***************************/

static inline void
_lock_tx_hashtbl(struct bonding *bond)
{
	spin_lock(&(BOND_ALB_INFO(bond).tx_hashtbl_lock));
}

static inline void
_unlock_tx_hashtbl(struct bonding *bond)
{
	spin_unlock(&(BOND_ALB_INFO(bond).tx_hashtbl_lock));
}

/* Caller must hold tx_hashtbl lock */
static inline void
tlb_init_table_entry(struct bonding *bond, u8 index, u8 save_load)
{
	struct tlb_client_info *entry;

	if (BOND_ALB_INFO(bond).tx_hashtbl == NULL) {
		return;
	}

	entry = &(BOND_ALB_INFO(bond).tx_hashtbl[index]);
	/* at end of cycle, save the load that was transmitted to the client
	 * during the cycle, and set the tx_bytes counter to 0 for counting
	 * the load during the next cycle
	 */
	if (save_load) {
		entry->load_history = 1 + entry->tx_bytes /
			BOND_TLB_REBALANCE_INTERVAL;
		entry->tx_bytes = 0;
	}
	entry->tx_slave = NULL;
	entry->next = TLB_NULL_INDEX;
	entry->prev = TLB_NULL_INDEX;
}

static inline void
tlb_init_slave(struct slave *slave)
{
	struct tlb_slave_info *slave_info = &(SLAVE_TLB_INFO(slave));

	slave_info->load = 0;
	slave_info->head = TLB_NULL_INDEX;
}

/* Caller must hold bond lock for read */
static inline void
tlb_clear_slave(struct bonding *bond, struct slave *slave, u8 save_load)
{
	struct tlb_client_info *tx_hash_table = NULL;
	u32 index, next_index;

	/* clear slave from tx_hashtbl */
	_lock_tx_hashtbl(bond);
	tx_hash_table = BOND_ALB_INFO(bond).tx_hashtbl;

	if (tx_hash_table) {
		index = SLAVE_TLB_INFO(slave).head;
		while (index != TLB_NULL_INDEX) {
			next_index = tx_hash_table[index].next;
			tlb_init_table_entry(bond, index, save_load);
			index = next_index;
		}
	}
	_unlock_tx_hashtbl(bond);

	tlb_init_slave(slave);
}

/* Must be called before starting the monitor timer */
static int
tlb_initialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	int i;
	size_t size;

#if(TLB_HASH_TABLE_SIZE != 256)
	/* Key to the hash table is byte wide. Check the size! */
	#error Hash Table size is wrong.
#endif

	spin_lock_init(&(bond_info->tx_hashtbl_lock));

	_lock_tx_hashtbl(bond);
	if (bond_info->tx_hashtbl != NULL) {
		printk (KERN_ERR "%s: TLB hash table is not NULL\n",
			bond->device->name);
		_unlock_tx_hashtbl(bond);
		return -1;
	}

	size = TLB_HASH_TABLE_SIZE * sizeof(struct tlb_client_info);
	bond_info->tx_hashtbl = kmalloc(size, GFP_KERNEL);
	if (bond_info->tx_hashtbl == NULL) {
		printk (KERN_ERR "%s: Failed to allocate TLB hash table\n",
			bond->device->name);
		_unlock_tx_hashtbl(bond);
		return -1;
	}

	memset(bond_info->tx_hashtbl, 0, size);
	for (i=0; i<TLB_HASH_TABLE_SIZE; i++) {
		tlb_init_table_entry(bond, i, 1);
	}
	_unlock_tx_hashtbl(bond);

	return 0;
}

/* Must be called only after all slaves have been released */
static void
tlb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	_lock_tx_hashtbl(bond);
	if (bond_info->tx_hashtbl == NULL) {
		_unlock_tx_hashtbl(bond);
		return;
	}
	kfree(bond_info->tx_hashtbl);
	bond_info->tx_hashtbl = NULL;
	_unlock_tx_hashtbl(bond);
}

/* Caller must hold bond lock for read */
static struct slave*
tlb_get_least_loaded_slave(struct bonding *bond)
{
	struct slave *slave;
	struct slave *least_loaded;
	s64 curr_gap, max_gap;

	/* Find the first enabled slave */
	slave = bond_get_first_slave(bond);
	while (slave) {
		if (SLAVE_IS_OK(slave)) {
			break;
		}
		slave = bond_get_next_slave(bond, slave);
	}

	if (!slave) {
		return NULL;
	}

	least_loaded = slave;
	max_gap = (s64)(slave->speed * 1000000) -
			(s64)(SLAVE_TLB_INFO(slave).load * 8);

	/* Find the slave with the largest gap */
	slave = bond_get_next_slave(bond, slave);
	while (slave) {
		if (SLAVE_IS_OK(slave)) {
			curr_gap = (s64)(slave->speed * 1000000) -
					(s64)(SLAVE_TLB_INFO(slave).load * 8);
			if (max_gap < curr_gap) {
				least_loaded = slave;
				max_gap = curr_gap;
			}
		}
		slave = bond_get_next_slave(bond, slave);
	}

	return least_loaded;
}

/* Caller must hold bond lock for read */
struct slave*
tlb_choose_channel(struct bonding *bond, u32 hash_index, u32 skb_len)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct tlb_client_info *hash_table = NULL;
	struct slave *assigned_slave = NULL;

	_lock_tx_hashtbl(bond);

	hash_table = bond_info->tx_hashtbl;
	if (hash_table == NULL) {
		printk (KERN_ERR "%s: TLB hash table is NULL\n",
			bond->device->name);
		_unlock_tx_hashtbl(bond);
		return NULL;
	}

	assigned_slave = hash_table[hash_index].tx_slave;
	if (!assigned_slave) {
		assigned_slave = tlb_get_least_loaded_slave(bond);

		if (assigned_slave) {
			struct tlb_slave_info *slave_info =
				&(SLAVE_TLB_INFO(assigned_slave));
			u32 next_index = slave_info->head;

			hash_table[hash_index].tx_slave = assigned_slave;
			hash_table[hash_index].next = next_index;
			hash_table[hash_index].prev = TLB_NULL_INDEX;

			if (next_index != TLB_NULL_INDEX) {
				hash_table[next_index].prev = hash_index;
			}

			slave_info->head = hash_index;
			slave_info->load +=
				hash_table[hash_index].load_history;
		}
	}

	if (assigned_slave) {
		hash_table[hash_index].tx_bytes += skb_len;
	}

	_unlock_tx_hashtbl(bond);

	return assigned_slave;
}

/*********************** rlb specific functions ***************************/
static inline void
_lock_rx_hashtbl(struct bonding *bond)
{
	spin_lock(&(BOND_ALB_INFO(bond).rx_hashtbl_lock));
}

static inline void
_unlock_rx_hashtbl(struct bonding *bond)
{
	spin_unlock(&(BOND_ALB_INFO(bond).rx_hashtbl_lock));
}

/* when an ARP REPLY is received from a client update its info
 * in the rx_hashtbl
 */
static void
rlb_update_entry_from_arp(struct bonding *bond, struct arp_pkt *arp)
{
	u32 hash_index;
	struct rlb_client_info *client_info = NULL;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	_lock_rx_hashtbl(bond);

	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}
	hash_index = _simple_hash((u8*)&(arp->ip_src), 4);
	client_info = &(bond_info->rx_hashtbl[hash_index]);

	if ((client_info->assigned) &&
	    (client_info->ip_src == arp->ip_dst) &&
	    (client_info->ip_dst == arp->ip_src)) {

		/* update the clients MAC address */
		memcpy(client_info->mac_dst, arp->mac_src, ETH_ALEN);
		client_info->ntt = 1;
		bond_info->rx_ntt = 1;
	}

	_unlock_rx_hashtbl(bond);
}

static int
rlb_arp_recv(struct sk_buff *skb,
	     struct net_device *dev,
	     struct packet_type* ptype)
{
	struct bonding *bond = (struct bonding *)dev->priv;
	int ret = NET_RX_DROP;
	struct arp_pkt *arp = (struct arp_pkt *)skb->data;

	if (!(dev->flags & IFF_MASTER)) {
		goto out;
	}

	if (!arp) {
		printk(KERN_ERR "Packet has no ARP data\n");
		goto out;
	}

	if (skb->len < sizeof(struct arp_pkt)) {
		printk(KERN_ERR "Packet is too small to be an ARP\n");
		goto out;
	}

	if (arp->op_code == htons(ARPOP_REPLY)) {
		/* update rx hash table for this ARP */
		rlb_update_entry_from_arp(bond, arp);
		BOND_PRINT_DBG(("Server received an ARP Reply from client"));
	}

	ret = NET_RX_SUCCESS;

out:
	dev_kfree_skb(skb);

	return ret;
}

/* Caller must hold bond lock for read */
static struct slave*
rlb_next_rx_slave(struct bonding *bond)
{
	struct slave *rx_slave = NULL, *slave = NULL;
	unsigned int i = 0;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	slave = bond_info->next_rx_slave;
	if (slave == NULL) {
		slave = bond->next;
	}

	/* this loop uses the circular linked list property of the
	 * slave's list to go through all slaves
	 */
	for (i = 0; i < bond->slave_cnt; i++, slave = slave->next) {

		if (SLAVE_IS_OK(slave)) {
			if (!rx_slave) {
				rx_slave = slave;
			}
			else if (slave->speed > rx_slave->speed) {
				rx_slave = slave;
			}
		}
	}

	if (rx_slave) {
		bond_info->next_rx_slave = rx_slave->next;
	}

	return rx_slave;
}

/* teach the switch the mac of a disabled slave
 * on the primary for fault tolerance
 *
 * Caller must hold bond->ptrlock for write or bond lock for write
 */
static void
rlb_teach_disabled_mac_on_primary(struct bonding *bond, u8 addr[])
{
	if (!bond->current_slave) {
		return;
	}
	if (!bond->alb_info.primary_is_promisc) {
		bond->alb_info.primary_is_promisc = 1;
		dev_set_promiscuity(bond->current_slave->dev, 1);
	}
	bond->alb_info.rlb_promisc_timeout_counter = 0;

	alb_send_learning_packets(bond->current_slave, addr);
}

/* slave being removed should not be active at this point
 *
 * Caller must hold bond lock for read
 */
static void
rlb_clear_slave(struct bonding *bond, struct slave *slave)
{
	struct rlb_client_info *rx_hash_table = NULL;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u8 mac_bcast[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	u32 index, next_index;

	/* clear slave from rx_hashtbl */
	_lock_rx_hashtbl(bond);
	rx_hash_table = bond_info->rx_hashtbl;

	if (rx_hash_table == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}

	index = bond_info->rx_hashtbl_head;
	for (; index != RLB_NULL_INDEX; index = next_index) {
		next_index = rx_hash_table[index].next;

		if (rx_hash_table[index].slave == slave) {
			struct slave *assigned_slave = rlb_next_rx_slave(bond);

			if (assigned_slave) {
				rx_hash_table[index].slave = assigned_slave;
				if (memcmp(rx_hash_table[index].mac_dst,
					   mac_bcast, ETH_ALEN)) {
					bond_info->rx_hashtbl[index].ntt = 1;
					bond_info->rx_ntt = 1;
					/* A slave has been removed from the
					 * table because it is either disabled
					 * or being released. We must retry the
					 * update to avoid clients from not
					 * being updated & disconnecting when
					 * there is stress
					 */
					bond_info->rlb_update_retry_counter =
						RLB_UPDATE_RETRY;
				}
			} else {  /* there is no active slave */
				rx_hash_table[index].slave = NULL;
			}
		}
	}

	_unlock_rx_hashtbl(bond);

	write_lock(&bond->ptrlock);
	if (slave != bond->current_slave) {
		rlb_teach_disabled_mac_on_primary(bond, slave->dev->dev_addr);
	}
	write_unlock(&bond->ptrlock);
}

static void
rlb_update_client(struct rlb_client_info *client_info)
{
	int i = 0;

	if (client_info->slave == NULL) {
		return;
	}

	for (i=0; i<RLB_ARP_BURST_SIZE; i++) {
		arp_send(ARPOP_REPLY, ETH_P_ARP,
			 client_info->ip_dst,
			 client_info->slave->dev,
			 client_info->ip_src,
			 client_info->mac_dst,
			 client_info->slave->dev->dev_addr,
			 client_info->mac_dst);
	}
}

/* sends ARP REPLIES that update the clients that need updating */
static void
rlb_update_rx_clients(struct bonding *bond)
{
	u32 hash_index;
	struct rlb_client_info *client_info = NULL;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	_lock_rx_hashtbl(bond);

	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		if (client_info->ntt) {
			rlb_update_client(client_info);
			if (bond_info->rlb_update_retry_counter == 0) {
				client_info->ntt = 0;
			}
		}
	}

	/* do not update the entries again untill this counter is zero so that
	 * not to confuse the clients.
	 */
	bond_info->rlb_update_delay_counter = RLB_UPDATE_DELAY;

	_unlock_rx_hashtbl(bond);
}

/* The slave was assigned a new mac address - update the clients */
static void
rlb_req_update_slave_clients(struct bonding *bond, struct slave *slave)
{
	u32 hash_index;
	u8 ntt = 0;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u8 mac_bcast[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	struct rlb_client_info* client_info = NULL;

	_lock_rx_hashtbl(bond);

	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);

		if ((client_info->slave == slave) &&
		    memcmp(client_info->mac_dst, mac_bcast, ETH_ALEN)) {
			client_info->ntt = 1;
			ntt = 1;
		}
	}

	// update the team's flag only after the whole iteration
	if (ntt) {
		bond_info->rx_ntt = 1;
		//fasten the change
		bond_info->rlb_update_retry_counter = RLB_UPDATE_RETRY;
	}

	_unlock_rx_hashtbl(bond);
}

/* mark all clients using src_ip to be updated */
static void
rlb_req_update_subnet_clients(struct bonding *bond, u32 src_ip)
{
	u32 hash_index;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	u8 mac_bcast[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
	struct rlb_client_info *client_info = NULL;

	_lock_rx_hashtbl(bond);

	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);

		if (!client_info->slave) {
			printk(KERN_ERR "Bonding: Error: found a client with no"
			       " channel in the client's hash table\n");
			continue;
		}
		/*update all clients using this src_ip, that are not assigned
		 * to the team's address (current_slave) and have a known
		 * unicast mac address.
		 */
		if ((client_info->ip_src == src_ip) &&
		    memcmp(client_info->slave->dev->dev_addr,
			   bond->device->dev_addr, ETH_ALEN) &&
		    memcmp(client_info->mac_dst, mac_bcast, ETH_ALEN)) {
			client_info->ntt = 1;
			bond_info->rx_ntt = 1;
		}
	}

	_unlock_rx_hashtbl(bond);
}

/* Caller must hold both bond and ptr locks for read */
struct slave*
rlb_choose_channel(struct bonding *bond, struct arp_pkt *arp)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct rlb_client_info *client_info = NULL;
	u32 hash_index = 0;
	struct slave *assigned_slave = NULL;
	u8 mac_bcast[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};

	_lock_rx_hashtbl(bond);

	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return NULL;
	}

	hash_index = _simple_hash((u8 *)&arp->ip_dst, 4);
	client_info = &(bond_info->rx_hashtbl[hash_index]);

	if (client_info->assigned == 1) {
		if ((client_info->ip_src == arp->ip_src) &&
		    (client_info->ip_dst == arp->ip_dst)) {
			/* the entry is already assigned to this client */

			if (memcmp(arp->mac_dst, mac_bcast, ETH_ALEN)) {
				/* update mac address from arp */
				memcpy(client_info->mac_dst, arp->mac_dst, ETH_ALEN);
			}

			assigned_slave = client_info->slave;
			if (assigned_slave) {
				_unlock_rx_hashtbl(bond);
				return assigned_slave;
			}
		} else {
			/* the entry is already assigned to some other client,
			 * move the old client to primary (current_slave) so
			 * that the new client can be assigned to this entry.
			 */
			if (bond->current_slave &&
			    client_info->slave != bond->current_slave) {
				client_info->slave = bond->current_slave;
				rlb_update_client(client_info);
			}
		}
	}
	/* assign a new slave */
	assigned_slave = rlb_next_rx_slave(bond);

	if (assigned_slave) {
		client_info->ip_src = arp->ip_src;
		client_info->ip_dst = arp->ip_dst;
		/* arp->mac_dst is broadcast for arp reqeusts.
		 * will be updated with clients actual unicast mac address
		 * upon receiving an arp reply.
		 */
		memcpy(client_info->mac_dst, arp->mac_dst, ETH_ALEN);
		client_info->slave = assigned_slave;

		if (memcmp(client_info->mac_dst, mac_bcast, ETH_ALEN)) {
			client_info->ntt = 1;
			bond->alb_info.rx_ntt = 1;
		}
		else {
			client_info->ntt = 0;
		}

		if (!client_info->assigned) {
			u32 prev_tbl_head = bond_info->rx_hashtbl_head;
			bond_info->rx_hashtbl_head = hash_index;
			client_info->next = prev_tbl_head;
			if (prev_tbl_head != RLB_NULL_INDEX) {
				bond_info->rx_hashtbl[prev_tbl_head].prev =
					hash_index;
			}
			client_info->assigned = 1;
		}
	}

	_unlock_rx_hashtbl(bond);

	return assigned_slave;
}

/* chooses (and returns) transmit channel for arp reply
 * does not choose channel for other arp types since they are
 * sent on the current_slave
 */
static struct slave*
rlb_arp_xmit(struct sk_buff *skb, struct bonding *bond)
{
	struct arp_pkt *arp = (struct arp_pkt *)skb->nh.raw;
	struct slave *tx_slave = NULL;

	if (arp->op_code == __constant_htons(ARPOP_REPLY)) {
		/* the arp must be sent on the selected
		* rx channel
		*/
		tx_slave = rlb_choose_channel(bond, arp);
		if (tx_slave) {
			memcpy(arp->mac_src,tx_slave->dev->dev_addr, ETH_ALEN);
		}
		BOND_PRINT_DBG(("Server sent ARP Reply packet"));
	} else if (arp->op_code == __constant_htons(ARPOP_REQUEST)) {

		/* Create an entry in the rx_hashtbl for this client as a
		 * place holder.
		 * When the arp reply is received the entry will be updated
		 * with the correct unicast address of the client.
		 */
		rlb_choose_channel(bond, arp);

		/* The ARP relpy packets must be delayed so that
		 * they can cancel out the influence of the ARP request.
		 */
		bond->alb_info.rlb_update_delay_counter = RLB_UPDATE_DELAY;

		/* arp requests are broadcast and are sent on the primary
		 * the arp request will collapse all clients on the subnet to
		 * the primary slave. We must register these clients to be
		 * updated with their assigned mac.
		 */
		rlb_req_update_subnet_clients(bond, arp->ip_src);
		BOND_PRINT_DBG(("Server sent ARP Request packet"));
	}

	return tx_slave;
}

/* Caller must hold bond lock for read */
static void
rlb_rebalance(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *assigned_slave = NULL;
	u32 hash_index;
	struct rlb_client_info *client_info = NULL;
	u8 ntt = 0;

	_lock_rx_hashtbl(bond);

	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}

	hash_index = bond_info->rx_hashtbl_head;
	for (; hash_index != RLB_NULL_INDEX; hash_index = client_info->next) {
		client_info = &(bond_info->rx_hashtbl[hash_index]);
		assigned_slave = rlb_next_rx_slave(bond);
		if (assigned_slave && (client_info->slave != assigned_slave)){
			client_info->slave = assigned_slave;
			client_info->ntt = 1;
			ntt = 1;
		}
	}

	/* update the team's flag only after the whole iteration */
	if (ntt) {
		bond_info->rx_ntt = 1;
	}
	_unlock_rx_hashtbl(bond);
}

/* Caller must hold rx_hashtbl lock */
static inline void
rlb_init_table_entry(struct rlb_client_info *entry)
{
	entry->next = RLB_NULL_INDEX;
	entry->prev = RLB_NULL_INDEX;
	entry->assigned = 0;
	entry->ntt = 0;
}

static int
rlb_initialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct packet_type *pk_type = &(BOND_ALB_INFO(bond).rlb_pkt_type);
	int i;
	size_t size;

	spin_lock_init(&(bond_info->rx_hashtbl_lock));

	_lock_rx_hashtbl(bond);
	if (bond_info->rx_hashtbl != NULL) {
		printk (KERN_ERR "%s: RLB hash table is not NULL\n",
			bond->device->name);
		_unlock_rx_hashtbl(bond);
		return -1;
	}

	size = RLB_HASH_TABLE_SIZE * sizeof(struct rlb_client_info);
	bond_info->rx_hashtbl = kmalloc(size, GFP_KERNEL);
	if (bond_info->rx_hashtbl == NULL) {
		printk (KERN_ERR "%s: Failed to allocate"
			" RLB hash table\n", bond->device->name);
		_unlock_rx_hashtbl(bond);
		return -1;
	}

	bond_info->rx_hashtbl_head = RLB_NULL_INDEX;

	for (i=0; i<RLB_HASH_TABLE_SIZE; i++) {
		rlb_init_table_entry(bond_info->rx_hashtbl + i);
	}
	_unlock_rx_hashtbl(bond);

	/* register to receive ARPs */

	/*initialize packet type*/
	pk_type->type = __constant_htons(ETH_P_ARP);
	pk_type->dev = bond->device;
	pk_type->func = rlb_arp_recv;

	dev_add_pack(pk_type);

	return 0;
}

static void
rlb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	dev_remove_pack(&(bond_info->rlb_pkt_type));

	_lock_rx_hashtbl(bond);
	if (bond_info->rx_hashtbl == NULL) {
		_unlock_rx_hashtbl(bond);
		return;
	}
	kfree(bond_info->rx_hashtbl);
	bond_info->rx_hashtbl = NULL;
	_unlock_rx_hashtbl(bond);
}

/*********************** tlb/rlb shared functions *********************/

static void
alb_send_learning_packets(struct slave *slave, u8 mac_addr[])
{
	struct sk_buff *skb = NULL;
	struct learning_pkt pkt;
	char *data = NULL;
	int i;
	unsigned int size = sizeof(struct learning_pkt);

	memset(&pkt, 0, size);
	memcpy(pkt.mac_dst, mac_addr, ETH_ALEN);
	memcpy(pkt.mac_src, mac_addr, ETH_ALEN);
	pkt.type = __constant_htons(ETH_P_LOOP);

	for (i=0; i < MAX_LP_RETRY; i++) {
		skb = NULL;
		skb = dev_alloc_skb(size);
		if (!skb) {
			return;
		}

		data = skb_put(skb, size);
		memcpy(data, &pkt, size);
		skb->mac.raw = data;
		skb->nh.raw = data + ETH_HLEN;
		skb->protocol = pkt.type;
		skb->priority = TC_PRIO_CONTROL;
		skb->dev = slave->dev;
		dev_queue_xmit(skb);
	}

}

/* hw is a boolean parameter that determines whether we should try and
 * set the hw address of the device as well as the hw address of the
 * net_device
 */
static int
alb_set_slave_mac_addr(struct slave *slave, u8 addr[], int hw)
{
	struct net_device *dev = NULL;
	struct sockaddr s_addr;

	dev = slave->dev;

	if (!hw) {
		memcpy(dev->dev_addr, addr, dev->addr_len);
		return 0;
	}

	/* for rlb each slave must have a unique hw mac addresses so that */
	/* each slave will receive packets destined to a different mac */
	memcpy(s_addr.sa_data, addr, dev->addr_len);
	s_addr.sa_family = dev->type;
	if (dev->set_mac_address(dev, &s_addr)) {
		printk(KERN_DEBUG "bonding: Error: alb_set_slave_mac_addr:"
				  " dev->set_mac_address of dev %s failed!"
				  " ALB mode requires that the base driver"
				  " support setting the hw address also when"
				  " the network device's interface is open\n",
				  dev->name);
		return -EOPNOTSUPP;
	}
	return 0;
}

/* Caller must hold bond lock for write or ptrlock for write*/
static void
alb_swap_mac_addr(struct bonding *bond,
		  struct slave *slave1,
		  struct slave *slave2)
{
	u8 tmp_mac_addr[ETH_ALEN];
	struct slave *disabled_slave = NULL;
	u8 slaves_state_differ;

	slaves_state_differ = (SLAVE_IS_OK(slave1) != SLAVE_IS_OK(slave2));

	memcpy(tmp_mac_addr, slave1->dev->dev_addr, ETH_ALEN);
	alb_set_slave_mac_addr(slave1, slave2->dev->dev_addr, bond->alb_info.rlb_enabled);
	alb_set_slave_mac_addr(slave2, tmp_mac_addr, bond->alb_info.rlb_enabled);

	/* fasten the change in the switch */
	if (SLAVE_IS_OK(slave1)) {
		alb_send_learning_packets(slave1, slave1->dev->dev_addr);
		if (bond->alb_info.rlb_enabled) {
			/* inform the clients that the mac address
			 * has changed
			 */
			rlb_req_update_slave_clients(bond, slave1);
		}
	}
	else {
		disabled_slave = slave1;
	}

	if (SLAVE_IS_OK(slave2)) {
		alb_send_learning_packets(slave2, slave2->dev->dev_addr);
		if (bond->alb_info.rlb_enabled) {
			/* inform the clients that the mac address
			 * has changed
			 */
			rlb_req_update_slave_clients(bond, slave2);
		}
	}
	else {
		disabled_slave = slave2;
	}

	if (bond->alb_info.rlb_enabled && slaves_state_differ) {
			/* A disabled slave was assigned an active mac addr */
			rlb_teach_disabled_mac_on_primary(bond,
				disabled_slave->dev->dev_addr);
	}
}

/**
 * alb_change_hw_addr_on_detach
 * @bond: bonding we're working on
 * @slave: the slave that was just detached
 *
 * We assume that @slave was already detached from the slave list.
 *
 * If @slave's permanent hw address is different both from its current
 * address and from @bond's address, then somewhere in the bond there's
 * a slave that has @slave's permanet address as its current address.
 * We'll make sure that that slave no longer uses @slave's permanent address.
 *
 * Caller must hold bond lock
 */
static void
alb_change_hw_addr_on_detach(struct bonding *bond, struct slave *slave)
{
	struct slave *tmp_slave;
	int perm_curr_diff;
	int perm_bond_diff;

	perm_curr_diff = memcmp(slave->perm_hwaddr,
				slave->dev->dev_addr,
				ETH_ALEN);
	perm_bond_diff = memcmp(slave->perm_hwaddr,
				bond->device->dev_addr,
				ETH_ALEN);
	if (perm_curr_diff && perm_bond_diff) {
		tmp_slave = bond_get_first_slave(bond);
		while (tmp_slave) {
			if (!memcmp(slave->perm_hwaddr,
				   tmp_slave->dev->dev_addr,
				   ETH_ALEN)) {
				break;
			}
			tmp_slave = bond_get_next_slave(bond, tmp_slave);
		}

		if (tmp_slave) {
			alb_swap_mac_addr(bond, slave, tmp_slave);
		}
	}
}

/**
 * alb_handle_addr_collision_on_attach
 * @bond: bonding we're working on
 * @slave: the slave that was just attached
 *
 * checks uniqueness of slave's mac address and handles the case the
 * new slave uses the bonds mac address.
 *
 * If the permanent hw address of @slave is @bond's hw address, we need to
 * find a different hw address to give @slave, that isn't in use by any other
 * slave in the bond. This address must be, of course, one of the premanent
 * addresses of the other slaves.
 *
 * We go over the slave list, and for each slave there we compare its
 * permanent hw address with the current address of all the other slaves.
 * If no match was found, then we've found a slave with a permanent address
 * that isn't used by any other slave in the bond, so we can assign it to
 * @slave.
 *
 * assumption: this function is called before @slave is attached to the
 * 	       bond slave list.
 *
 * caller must hold the bond lock for write since the mac addresses are compared
 * and may be swapped.
 */
static int
alb_handle_addr_collision_on_attach(struct bonding *bond, struct slave *slave)
{
	struct slave *tmp_slave1, *tmp_slave2;

	if (bond->slave_cnt == 0) {
		/* this is the first slave */
		return 0;
	}

	/* if slave's mac address differs from bond's mac address
	 * check uniqueness of slave's mac address against the other
	 * slaves in the bond.
	 */
	if (memcmp(slave->perm_hwaddr, bond->device->dev_addr, ETH_ALEN)) {
		tmp_slave1 = bond_get_first_slave(bond);
		for (; tmp_slave1; tmp_slave1 = bond_get_next_slave(bond, tmp_slave1)) {
			if (!memcmp(tmp_slave1->dev->dev_addr, slave->dev->dev_addr,
				    ETH_ALEN)) {
				break;
			}
		}
		if (tmp_slave1) {
			/* a slave was found that is using the mac address
			 * of the new slave
			 */
			printk(KERN_ERR "bonding: Warning: the hw address "
			       "of slave %s is not unique - cannot enslave it!"
			       , slave->dev->name);
			return -EINVAL;
		}
		return 0;
	}

	/* the slave's address is equal to the address of the bond
	 * search for a spare address in the bond for this slave.
	 */
	tmp_slave1 = bond_get_first_slave(bond);
	for (; tmp_slave1; tmp_slave1 = bond_get_next_slave(bond, tmp_slave1)) {

		tmp_slave2 = bond_get_first_slave(bond);
		for (; tmp_slave2; tmp_slave2 = bond_get_next_slave(bond, tmp_slave2)) {

			if (!memcmp(tmp_slave1->perm_hwaddr,
				    tmp_slave2->dev->dev_addr,
				    ETH_ALEN)) {

				break;
			}
		}

		if (!tmp_slave2) {
			/* no slave has tmp_slave1's perm addr
			 * as its curr addr
			 */
			break;
		}
	}

	if (tmp_slave1) {
		alb_set_slave_mac_addr(slave, tmp_slave1->perm_hwaddr,
				       bond->alb_info.rlb_enabled);

		printk(KERN_WARNING "bonding: Warning: the hw address "
		       "of slave %s is in use by the bond; "
		       "giving it the hw address of %s\n",
		       slave->dev->name, tmp_slave1->dev->name);
	} else {
		printk(KERN_CRIT "bonding: Error: the hw address "
		       "of slave %s is in use by the bond; "
		       "couldn't find a slave with a free hw "
		       "address to give it (this should not have "
		       "happened)\n", slave->dev->name);
		return -EFAULT;
	}

	return 0;
}

/**
 * alb_set_mac_address
 * @bond:
 * @addr:
 *
 * In TLB mode all slaves are configured to the bond's hw address, but set
 * their dev_addr field to different addresses (based on their permanent hw
 * addresses).
 *
 * For each slave, this function sets the interface to the new address and then
 * changes its dev_addr field to its previous value.
 * 
 * Unwinding assumes bond's mac address has not yet changed.
 */
static inline int
alb_set_mac_address(struct bonding *bond, void *addr)
{
	struct sockaddr sa;
	struct slave *slave;
	char tmp_addr[ETH_ALEN];
	int error;

	if (bond->alb_info.rlb_enabled) {
		return 0;
	}

	slave = bond_get_first_slave(bond);
	for (; slave; slave = bond_get_next_slave(bond, slave)) {
		if (slave->dev->set_mac_address == NULL) {
			error = -EOPNOTSUPP;
			goto unwind;
		}

		/* save net_device's current hw address */
		memcpy(tmp_addr, slave->dev->dev_addr, ETH_ALEN);

		error = slave->dev->set_mac_address(slave->dev, addr);

		/* restore net_device's hw address */
		memcpy(slave->dev->dev_addr, tmp_addr, ETH_ALEN);

		if (error) {
			goto unwind;
		}
	}

	return 0;

unwind:
	memcpy(sa.sa_data, bond->device->dev_addr, bond->device->addr_len);
	sa.sa_family = bond->device->type;
	slave = bond_get_first_slave(bond);
	for (; slave; slave = bond_get_next_slave(bond, slave)) {
		memcpy(tmp_addr, slave->dev->dev_addr, ETH_ALEN);
		slave->dev->set_mac_address(slave->dev, &sa);
		memcpy(slave->dev->dev_addr, tmp_addr, ETH_ALEN);
	}

	return error;
}

/************************ exported alb funcions ************************/

int
bond_alb_initialize(struct bonding *bond, int rlb_enabled)
{
	int res;

	res = tlb_initialize(bond);
	if (res) {
		return res;
	}

	if (rlb_enabled) {
		bond->alb_info.rlb_enabled = 1;
		/* initialize rlb */
		res = rlb_initialize(bond);
		if (res) {
			tlb_deinitialize(bond);
			return res;
		}
	}

	return 0;
}

void
bond_alb_deinitialize(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	tlb_deinitialize(bond);

	if (bond_info->rlb_enabled) {
		rlb_deinitialize(bond);
	}
}

int
bond_alb_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bonding *bond = (struct bonding *) dev->priv;
	struct ethhdr *eth_data = (struct ethhdr *)skb->data;
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *tx_slave = NULL;
	char do_tx_balance = 1;
	int hash_size = 0;
	u32 hash_index = 0;
	u8 *hash_start = NULL;
	u8 mac_bcast[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};

	if (!IS_UP(dev)) { /* bond down */
		dev_kfree_skb(skb);
		return 0;
	}

	/* make sure that the current_slave and the slaves list do
	 * not change during tx
	 */
	read_lock(&bond->lock);

	if (bond->slave_cnt == 0) {
		/* no suitable interface, frame not sent */
		dev_kfree_skb(skb);
		read_unlock(&bond->lock);
		return 0;
	}

	read_lock(&bond->ptrlock);

	switch (ntohs(skb->protocol)) {
	case ETH_P_IP:
		if ((memcmp(eth_data->h_dest, mac_bcast, ETH_ALEN) == 0) ||
		    (skb->nh.iph->daddr == 0xffffffff)) {
			do_tx_balance = 0;
			break;
		}
		hash_start = (char*)&(skb->nh.iph->daddr);
		hash_size = 4;
		break;

	case ETH_P_IPV6:
		if (memcmp(eth_data->h_dest, mac_bcast, ETH_ALEN) == 0) {
			do_tx_balance = 0;
			break;
		}

		hash_start = (char*)&(skb->nh.ipv6h->daddr);
		hash_size = 16;
		break;

	case ETH_P_IPX:
		if (ipx_hdr(skb)->ipx_checksum !=
		    __constant_htons(IPX_NO_CHECKSUM)) {
			/* something is wrong with this packet */
			do_tx_balance = 0;
			break;
		}

		if (ipx_hdr(skb)->ipx_type !=
		    __constant_htons(IPX_TYPE_NCP)) {
			/* The only protocol worth balancing in
			 * this family since it has an "ARP" like
			 * mechanism
			 */
			do_tx_balance = 0;
			break;
		}

		hash_start = (char*)eth_data->h_dest;
		hash_size = ETH_ALEN;
		break;

	case ETH_P_ARP:
		do_tx_balance = 0;
		if (bond_info->rlb_enabled) {
			tx_slave = rlb_arp_xmit(skb, bond);
		}
		break;

	default:
		do_tx_balance = 0;
		break;
	}

	if (do_tx_balance) {
		hash_index = _simple_hash(hash_start, hash_size);
		tx_slave = tlb_choose_channel(bond, hash_index, skb->len);
	}

	if (!tx_slave) {
		/* unbalanced or unassigned, send through primary */
		tx_slave = bond->current_slave;
		bond_info->unbalanced_load += skb->len;
	}

	if (tx_slave && SLAVE_IS_OK(tx_slave)) {
		skb->dev = tx_slave->dev;
		if (tx_slave != bond->current_slave) {
			memcpy(eth_data->h_source,
				tx_slave->dev->dev_addr,
				ETH_ALEN);
		}
		dev_queue_xmit(skb);
	} else {
		/* no suitable interface, frame not sent */
		if (tx_slave) {
			tlb_clear_slave(bond, tx_slave, 0);
		}
		dev_kfree_skb(skb);
	}

	read_unlock(&bond->ptrlock);
	read_unlock(&bond->lock);
	return 0;
}

void
bond_alb_monitor(struct bonding *bond)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));
	struct slave *slave = NULL;

	read_lock(&bond->lock);

	if ((bond->slave_cnt == 0) || !(bond->device->flags & IFF_UP)) {
		bond_info->tx_rebalance_counter = 0;
		bond_info->lp_counter = 0;
		goto out;
	}

	bond_info->tx_rebalance_counter++;
	bond_info->lp_counter++;

	/* send learning packets */
	if (bond_info->lp_counter >= BOND_ALB_LP_TICKS) {
		/* change of current_slave involves swapping of mac addresses.
		 * in order to avoid this swapping from happening while
		 * sending the learning packets, the ptrlock must be held for
		 * read.
		 */
		read_lock(&bond->ptrlock);
		slave = bond_get_first_slave(bond);
		while (slave) {
			alb_send_learning_packets(slave,slave->dev->dev_addr);
			slave = bond_get_next_slave(bond, slave);
		}
		read_unlock(&bond->ptrlock);

		bond_info->lp_counter = 0;
	}

	/* rebalance tx traffic */
	if (bond_info->tx_rebalance_counter >= BOND_TLB_REBALANCE_TICKS) {
		read_lock(&bond->ptrlock);
		slave = bond_get_first_slave(bond);
		while (slave) {
			tlb_clear_slave(bond, slave, 1);
			if (slave == bond->current_slave) {
				SLAVE_TLB_INFO(slave).load =
					bond_info->unbalanced_load /
						BOND_TLB_REBALANCE_INTERVAL;
				bond_info->unbalanced_load = 0;
			}
			slave = bond_get_next_slave(bond, slave);
		}
		read_unlock(&bond->ptrlock);
		bond_info->tx_rebalance_counter = 0;
	}

	/* handle rlb stuff */
	if (bond_info->rlb_enabled) {
		/* the following code changes the promiscuity of the
		 * the current_slave. It needs to be locked with a
		 * write lock to protect from other code that also
		 * sets the promiscuity.
		 */
		write_lock(&bond->ptrlock);
		if (bond_info->primary_is_promisc &&
		    (++bond_info->rlb_promisc_timeout_counter >=
			RLB_PROMISC_TIMEOUT)) {

			bond_info->rlb_promisc_timeout_counter = 0;

			/* If the primary was set to promiscuous mode
			 * because a slave was disabled then
			 * it can now leave promiscuous mode.
			 */
			dev_set_promiscuity(bond->current_slave->dev, -1);
			bond_info->primary_is_promisc = 0;
		}
		write_unlock(&bond->ptrlock);

		if (bond_info->rlb_rebalance == 1) {
			bond_info->rlb_rebalance = 0;
			rlb_rebalance(bond);
		}

		/* check if clients need updating */
		if (bond_info->rx_ntt) {
			if (bond_info->rlb_update_delay_counter) {
				--bond_info->rlb_update_delay_counter;
			} else {
				rlb_update_rx_clients(bond);
				if (bond_info->rlb_update_retry_counter) {
					--bond_info->rlb_update_retry_counter;
				} else {
					bond_info->rx_ntt = 0;
				}
			}
		}
	}

out:
	read_unlock(&bond->lock);

	if (bond->device->flags & IFF_UP) {
		/* re-arm the timer */
		mod_timer(&(bond_info->alb_timer),
			jiffies + (HZ/ALB_TIMER_TICKS_PER_SEC));
	}
}

/* assumption: called before the slave is attched to the bond
 * and not locked by the bond lock
 */
int
bond_alb_init_slave(struct bonding *bond, struct slave *slave)
{
	int err = 0;

	err = alb_set_slave_mac_addr(slave, slave->perm_hwaddr,
				     bond->alb_info.rlb_enabled);
	if (err) {
		return err;
	}

	/* caller must hold the bond lock for write since the mac addresses
	 * are compared and may be swapped.
	 */
	write_lock_bh(&bond->lock);

	err = alb_handle_addr_collision_on_attach(bond, slave);

	write_unlock_bh(&bond->lock);

	if (err) {
		return err;
	}

	tlb_init_slave(slave);

	/* order a rebalance ASAP */
	bond->alb_info.tx_rebalance_counter = BOND_TLB_REBALANCE_TICKS;

	if (bond->alb_info.rlb_enabled) {
		bond->alb_info.rlb_rebalance = 1;
	}

	return 0;
}

/* Caller must hold bond lock for write */
void
bond_alb_deinit_slave(struct bonding *bond, struct slave *slave)
{
	if (bond->slave_cnt > 1) {
		alb_change_hw_addr_on_detach(bond, slave);
	}

	tlb_clear_slave(bond, slave, 0);

	if (bond->alb_info.rlb_enabled) {
		bond->alb_info.next_rx_slave = NULL;
		rlb_clear_slave(bond, slave);
	}
}

/* Caller must hold bond lock for read */
void
bond_alb_handle_link_change(struct bonding *bond, struct slave *slave,
			    char link)
{
	struct alb_bond_info *bond_info = &(BOND_ALB_INFO(bond));

	if (link == BOND_LINK_DOWN) {
		tlb_clear_slave(bond, slave, 0);
		if (bond->alb_info.rlb_enabled) {
			rlb_clear_slave(bond, slave);
		}
	} else if (link == BOND_LINK_UP) {
		/* order a rebalance ASAP */
		bond_info->tx_rebalance_counter = BOND_TLB_REBALANCE_TICKS;
		if (bond->alb_info.rlb_enabled) {
			bond->alb_info.rlb_rebalance = 1;
			/* If the updelay module parameter is smaller than the
			 * forwarding delay of the switch the rebalance will
			 * not work because the rebalance arp replies will
			 * not be forwarded to the clients..
			 */
		}
	}
}

/**
 * bond_alb_assign_current_slave - assign new current_slave
 * @bond: our bonding struct
 * @new_slave: new slave to assign
 *
 * Set the bond->current_slave to @new_slave and handle
 * mac address swapping and promiscuity changes as needed.
 *
 * Caller must hold bond ptrlock for write (or bond lock for write)
 */
void
bond_alb_assign_current_slave(struct bonding *bond, struct slave *new_slave)
{
	struct slave *swap_slave = bond->current_slave;

	if (bond->current_slave == new_slave) {
		return;
	}

	if (bond->current_slave && bond->alb_info.primary_is_promisc) {
		dev_set_promiscuity(bond->current_slave->dev, -1);
		bond->alb_info.primary_is_promisc = 0;
		bond->alb_info.rlb_promisc_timeout_counter = 0;
	}

	bond->current_slave = new_slave;

	if (!new_slave || (bond->slave_cnt == 0)) {
		return;
	}

	/* set the new current_slave to the bonds mac address
	 * i.e. swap mac addresses of old current_slave and new current_slave
	 */
	if (!swap_slave) {
		/* find slave that is holding the bond's mac address */
		swap_slave = bond_get_first_slave(bond);
		while (swap_slave) {
			if (!memcmp(swap_slave->dev->dev_addr,
				bond->device->dev_addr, ETH_ALEN)) {
				break;
			}
			swap_slave = bond_get_next_slave(bond, swap_slave);
		}
	}

	/* current_slave must be set before calling alb_swap_mac_addr */
	if (swap_slave) {
		/* swap mac address */
		alb_swap_mac_addr(bond, swap_slave, new_slave);
	} else {
		/* set the new_slave to the bond mac address */
		alb_set_slave_mac_addr(new_slave, bond->device->dev_addr,
				       bond->alb_info.rlb_enabled);
		/* fasten bond mac on new current slave */
		alb_send_learning_packets(new_slave, bond->device->dev_addr);
	}
}

int
bond_alb_set_mac_address(struct net_device *dev, void *addr)
{
	struct bonding *bond = dev->priv;
	struct sockaddr *sa = addr;
	struct slave *swap_slave = NULL;
	int error = 0;

	if (!is_valid_ether_addr(sa->sa_data)) {
		return -EADDRNOTAVAIL;
	}

	error = alb_set_mac_address(bond, addr);
	if (error) {
		return error;
	}

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	/* If there is no current_slave there is nothing else to do.
	 * Otherwise we'll need to pass the new address to it and handle
	 * duplications.
	 */
	if (bond->current_slave == NULL) {
		return 0;
	}

	swap_slave = bond_get_first_slave(bond);
	while (swap_slave) {
		if (!memcmp(swap_slave->dev->dev_addr, dev->dev_addr, ETH_ALEN)) {
			break;
		}
		swap_slave = bond_get_next_slave(bond, swap_slave);
	}

	if (swap_slave) {
		alb_swap_mac_addr(bond, swap_slave, bond->current_slave);
	} else {
		alb_set_slave_mac_addr(bond->current_slave, dev->dev_addr,
				       bond->alb_info.rlb_enabled);

		alb_send_learning_packets(bond->current_slave, dev->dev_addr);
		if (bond->alb_info.rlb_enabled) {
			/* inform clients mac address has changed */
			rlb_req_update_slave_clients(bond, bond->current_slave);
		}
	}

	return 0;
}

