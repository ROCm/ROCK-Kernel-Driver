/*
 * (ext8022.c)
 * 
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the 
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/brlock.h>

typedef int (*func_type)(struct sk_buff *skb, struct net_device *dev,
			 struct packet_type *pt);
static int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *);

static func_type llc_sap_table[128];
static int llc_users;

static struct packet_type llc_packet_type = {
	type:	__constant_htons(ETH_P_802_2),
	func:	llc_rcv,
};
static struct packet_type llc_tr_packet_type = {
	type:	__constant_htons(ETH_P_TR_802_2),
	func:	llc_rcv,
};

static int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt)
{
 	unsigned char n = (*(skb->h.raw)) >> 1;

	br_read_lock(BR_LLC_LOCK);
	if (llc_sap_table[n])
            llc_sap_table[n](skb, dev, pt);
        else
            kfree_skb(skb);
	br_read_unlock(BR_LLC_LOCK);
        return 0;
}

void llc_register_sap(unsigned char sap, func_type rcvfunc)
{
	sap >>= 1;
	br_write_lock_bh(BR_LLC_LOCK);
	llc_sap_table[sap] = rcvfunc;            
	if (!llc_users) {
		dev_add_pack(&llc_packet_type);
		dev_add_pack(&llc_tr_packet_type);
        }
	llc_users++;
	br_write_unlock_bh(BR_LLC_LOCK);
}

void llc_unregister_sap(unsigned char sap)
{
	sap >>= 1;
	br_write_lock_bh(BR_LLC_LOCK);
        llc_sap_table[sap] = NULL;
	if (!--llc_users) {
		dev_remove_pack(&llc_packet_type);
		dev_remove_pack(&llc_tr_packet_type);
        } 
	br_write_unlock_bh(BR_LLC_LOCK);
}

EXPORT_SYMBOL(llc_register_sap);
EXPORT_SYMBOL(llc_unregister_sap);
