/****************************************************************************
 * Solarflare driver for Xen network acceleration
 *
 * Copyright 2006-2008: Solarflare Communications Inc,
 *                      9501 Jeronimo Road, Suite 250,
 *                      Irvine, CA 92618, USA
 *
 * Maintained by Solarflare Communications <linux-xen-drivers@solarflare.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 ****************************************************************************
 */

#include "accel.h"
#include "accel_cuckoo_hash.h"
#include "accel_util.h"
#include "accel_solarflare.h"

#include "driverlink_api.h"

#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/list.h>

/* State stored in the forward table */
struct fwd_struct {
	struct list_head link; /* Forms list */
	void * context;
	__u8 valid;
	__u8 mac[ETH_ALEN];
};

/* Max value we support */
#define NUM_FWDS_BITS 8
#define NUM_FWDS (1 << NUM_FWDS_BITS)
#define FWD_MASK (NUM_FWDS - 1)

struct port_fwd {
	/* Make a list */
	struct list_head link;
	/* Hash table to store the fwd_structs */
	cuckoo_hash_table fwd_hash_table;
	/* The array of fwd_structs */
	struct fwd_struct *fwd_array;
	/* Linked list of entries in use. */
	struct list_head fwd_list;
	/* Could do something clever with a reader/writer lock. */
	spinlock_t fwd_lock;
	/* Make find_free_entry() a bit faster by caching this */
	int last_free_index;
};

/*
 * This is unlocked as it's only called from dl probe and remove,
 * which are themselves synchronised.  Could get rid of it entirely as
 * it's never iterated, but useful for debug
 */
static struct list_head port_fwds;


/* Search the fwd_array for an unused entry */
static int fwd_find_free_entry(struct port_fwd *fwd_set)
{
	int index = fwd_set->last_free_index;

	do {
		if (!fwd_set->fwd_array[index].valid) {
			fwd_set->last_free_index = index;
			return index;
		}
		index++;
		if (index >= NUM_FWDS)
			index = 0;
	} while (index != fwd_set->last_free_index);

	return -ENOMEM;
}


/* Look up a MAC in the hash table. Caller should hold table lock. */
static inline struct fwd_struct *fwd_find_entry(const __u8 *mac,
						struct port_fwd *fwd_set)
{
	cuckoo_hash_value value;
	cuckoo_hash_mac_key key = cuckoo_mac_to_key(mac);

	if (cuckoo_hash_lookup(&fwd_set->fwd_hash_table,
			       (cuckoo_hash_key *)(&key),
			       &value)) {
		struct fwd_struct *fwd = &fwd_set->fwd_array[value];
		DPRINTK_ON(memcmp(fwd->mac, mac, ETH_ALEN) != 0);
		return fwd;
	}

	return NULL;
}


/* Initialise each nic port's fowarding table */
void *netback_accel_init_fwd_port(void) 
{	
	struct port_fwd *fwd_set;

	fwd_set = kzalloc(sizeof(struct port_fwd), GFP_KERNEL);
	if (fwd_set == NULL) {
		return NULL;
	}

	spin_lock_init(&fwd_set->fwd_lock);
	
	fwd_set->fwd_array = kzalloc(sizeof (struct fwd_struct) * NUM_FWDS,
				     GFP_KERNEL);
	if (fwd_set->fwd_array == NULL) {
		kfree(fwd_set);
		return NULL;
	}
	
	if (cuckoo_hash_init(&fwd_set->fwd_hash_table, NUM_FWDS_BITS, 8) != 0) {
		kfree(fwd_set->fwd_array);
		kfree(fwd_set);
		return NULL;
	}
	
	INIT_LIST_HEAD(&fwd_set->fwd_list);
	
	list_add(&fwd_set->link, &port_fwds);

	return fwd_set;
}


void netback_accel_shutdown_fwd_port(void *fwd_priv)
{
	struct port_fwd *fwd_set = (struct port_fwd *)fwd_priv;

	BUG_ON(fwd_priv == NULL);
	
	BUG_ON(list_empty(&port_fwds));
	list_del(&fwd_set->link);

	BUG_ON(!list_empty(&fwd_set->fwd_list));

	cuckoo_hash_destroy(&fwd_set->fwd_hash_table);
	kfree(fwd_set->fwd_array);
	kfree(fwd_set);
}


int netback_accel_init_fwd()
{
	INIT_LIST_HEAD(&port_fwds);
	return 0;
}


void netback_accel_shutdown_fwd()
{
	BUG_ON(!list_empty(&port_fwds));
}


/*
 * Add an entry to the forwarding table.  Returns -ENOMEM if no
 * space.
 */
int netback_accel_fwd_add(const __u8 *mac, void *context, void *fwd_priv)
{
	struct fwd_struct *fwd;
	int rc = 0, index;
	unsigned long flags;
	cuckoo_hash_mac_key key = cuckoo_mac_to_key(mac);
	struct port_fwd *fwd_set = (struct port_fwd *)fwd_priv;
	DECLARE_MAC_BUF(buf);

	BUG_ON(fwd_priv == NULL);

	DPRINTK("Adding mac %s\n", print_mac(buf, mac));
       
	spin_lock_irqsave(&fwd_set->fwd_lock, flags);
	
	if ((rc = fwd_find_free_entry(fwd_set)) < 0 ) {
		spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);
		return rc;
	}

	index = rc;

	/* Shouldn't already be in the table */
	if (cuckoo_hash_lookup(&fwd_set->fwd_hash_table,
			       (cuckoo_hash_key *)(&key), &rc) != 0) {
		spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);
		EPRINTK("MAC address %s already accelerated.\n",
			print_mac(buf, mac));
		return -EEXIST;
	}

	if ((rc = cuckoo_hash_add(&fwd_set->fwd_hash_table,
				  (cuckoo_hash_key *)(&key), index, 1)) == 0) {
		fwd = &fwd_set->fwd_array[index];
		fwd->valid = 1;
		fwd->context = context;
		memcpy(fwd->mac, mac, ETH_ALEN);
		list_add(&fwd->link, &fwd_set->fwd_list);
		NETBACK_ACCEL_STATS_OP(global_stats.num_fwds++);
	}

	spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);

	/*
	 * No need to tell frontend that this mac address is local -
	 * it should auto-discover through packets on fastpath what is
	 * local and what is not, and just being on same server
	 * doesn't make it local (it could be on a different
	 * bridge)
	 */

	return rc;
}


/* remove an entry from the forwarding tables. */
void netback_accel_fwd_remove(const __u8 *mac, void *fwd_priv)
{
	struct fwd_struct *fwd;
	unsigned long flags;
	cuckoo_hash_mac_key key = cuckoo_mac_to_key(mac);
	struct port_fwd *fwd_set = (struct port_fwd *)fwd_priv;
	DECLARE_MAC_BUF(buf);

	DPRINTK("Removing mac %s\n", print_mac(buf, mac));

	BUG_ON(fwd_priv == NULL);

	spin_lock_irqsave(&fwd_set->fwd_lock, flags);

	fwd = fwd_find_entry(mac, fwd_set);
	if (fwd != NULL) {
		BUG_ON(list_empty(&fwd_set->fwd_list));
		list_del(&fwd->link);

		fwd->valid = 0;
		cuckoo_hash_remove(&fwd_set->fwd_hash_table, 
				   (cuckoo_hash_key *)(&key));
		NETBACK_ACCEL_STATS_OP(global_stats.num_fwds--);
	}
	spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);

	/*
	 * No need to tell frontend that this is no longer present -
	 * the frontend is currently only interested in remote
	 * addresses and it works these out (mostly) by itself
	 */
}


/* Set the context pointer for a hash table entry. */
int netback_accel_fwd_set_context(const __u8 *mac, void *context, 
				  void *fwd_priv)
{
	struct fwd_struct *fwd;
	unsigned long flags;
	int rc = -ENOENT;
	struct port_fwd *fwd_set = (struct port_fwd *)fwd_priv;

	BUG_ON(fwd_priv == NULL);

	spin_lock_irqsave(&fwd_set->fwd_lock, flags);
	fwd = fwd_find_entry(mac, fwd_set);
	if (fwd != NULL) {
		fwd->context = context;
		rc = 0;
	}
	spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);
	return rc;
}


/**************************************************************************
 * Process a received packet
 **************************************************************************/

/*
 * Returns whether or not we have a match in our forward table for the
 * this skb. Must be called with appropriate fwd_lock already held
 */
static struct netback_accel *for_a_vnic(struct netback_pkt_buf *skb, 
					struct port_fwd *fwd_set)
{
	struct fwd_struct *fwd;
	struct netback_accel *retval = NULL;

	fwd = fwd_find_entry(skb->mac.raw, fwd_set);
	if (fwd != NULL)
		retval = fwd->context;
	return retval;
}


static inline int packet_is_arp_reply(struct sk_buff *skb)
{
	return skb->protocol == ntohs(ETH_P_ARP) 
		&& arp_hdr(skb)->ar_op == ntohs(ARPOP_REPLY);
}


static inline void hdr_to_filt(struct ethhdr *ethhdr, struct iphdr *ip,
			       struct netback_accel_filter_spec *spec)
{
	spec->proto = ip->protocol;
	spec->destip_be = ip->daddr;
	memcpy(spec->mac, ethhdr->h_source, ETH_ALEN);

	if (ip->protocol == IPPROTO_TCP) {
		struct tcphdr *tcp = (struct tcphdr *)((char *)ip + 4 * ip->ihl);
		spec->destport_be = tcp->dest;
	} else {
		struct udphdr *udp = (struct udphdr *)((char *)ip + 4 * ip->ihl);
		EPRINTK_ON(ip->protocol != IPPROTO_UDP);
		spec->destport_be = udp->dest;
	}
}


static inline int netback_accel_can_filter(struct netback_pkt_buf *skb) 
{
	return (skb->protocol == htons(ETH_P_IP) && 
		((skb->nh.iph->protocol == IPPROTO_TCP) ||
		 (skb->nh.iph->protocol == IPPROTO_UDP)));
}


static inline void netback_accel_filter_packet(struct netback_accel *bend,
					       struct netback_pkt_buf *skb)
{
	struct netback_accel_filter_spec fs;
	struct ethhdr *eh = (struct ethhdr *)(skb->mac.raw);

	hdr_to_filt(eh, skb->nh.iph, &fs);
	
	netback_accel_filter_check_add(bend, &fs);
}


/*
 * Receive a packet and do something appropriate with it. Return true
 * to take exclusive ownership of the packet.  This is verging on
 * solarflare specific
 */
void netback_accel_rx_packet(struct netback_pkt_buf *skb, void *fwd_priv)
{
	struct netback_accel *bend;
	struct port_fwd *fwd_set = (struct port_fwd *)fwd_priv;
	unsigned long flags;

	BUG_ON(fwd_priv == NULL);

	/* Checking for bcast is cheaper so do that first */
	if (is_broadcast_ether_addr(skb->mac.raw)) {
		/* pass through the slow path by not claiming ownership */
		return;
	} else if (is_multicast_ether_addr(skb->mac.raw)) {
		/* pass through the slow path by not claiming ownership */
		return;
	} else {
		/* It is unicast */
		spin_lock_irqsave(&fwd_set->fwd_lock, flags);
		/* We insert filter to pass it off to a VNIC */
		if ((bend = for_a_vnic(skb, fwd_set)) != NULL)
			if (netback_accel_can_filter(skb))
				netback_accel_filter_packet(bend, skb);
		spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);
	}
	return;
}


void netback_accel_tx_packet(struct sk_buff *skb, void *fwd_priv) 
{
	__u8 *mac;
	unsigned long flags;
	struct port_fwd *fwd_set = (struct port_fwd *)fwd_priv;
	struct fwd_struct *fwd;

	BUG_ON(fwd_priv == NULL);

	if (is_broadcast_ether_addr(skb_mac_header(skb))
	    && packet_is_arp_reply(skb)) {
		DECLARE_MAC_BUF(buf);

		/*
		 * update our fast path forwarding to reflect this
		 * gratuitous ARP
		 */ 
		mac = skb_mac_header(skb)+ETH_ALEN;

		DPRINTK("%s: found gratuitous ARP for %s\n",
			__FUNCTION__, print_mac(buf, mac));

		spin_lock_irqsave(&fwd_set->fwd_lock, flags);
		/*
		 * Might not be local, but let's tell them all it is,
		 * and they can restore the fastpath if they continue
		 * to get packets that way
		 */
		list_for_each_entry(fwd, &fwd_set->fwd_list, link) {
			struct netback_accel *bend = fwd->context;
			if (bend != NULL)
				netback_accel_msg_tx_new_localmac(bend, mac);
		}

		spin_unlock_irqrestore(&fwd_set->fwd_lock, flags);
	}
	return;
}
