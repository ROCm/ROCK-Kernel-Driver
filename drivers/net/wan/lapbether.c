/*
 *	"LAPB via ethernet" driver release 001
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	This is a "pseudo" network driver to allow LAPB over Ethernet.
 *
 *	This driver can use any ethernet destination address, and can be 
 *	limited to accept frames from one dedicated ethernet card only.
 *
 *	History
 *	LAPBETH 001	Jonathan Naylor		Cloned from bpqether.c
 *	2000-10-29	Henner Eisen	lapb_data_indication() return status.
 *	2000-11-14	Henner Eisen	dev_hold/put, NETDEV_GOING_DOWN support
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/netfilter.h>
#include <linux/module.h>
#include <linux/lapb.h>
#include <linux/init.h>

static char bcast_addr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

/* If this number is made larger, check that the temporary string buffer
 * in lapbeth_new_device is large enough to store the probe device name.*/
#define MAXLAPBDEV 100

struct lapbethdev {
	struct list_head	node;
	char			ethname[14];	/* ether device name */
	struct net_device	*ethdev;	/* link to ethernet device */
	struct net_device	axdev;		/* lapbeth device (lapb#) */
	struct net_device_stats stats;		/* some statistics */
	atomic_t		refcnt;
};

static struct list_head lapbeth_devices = LIST_HEAD_INIT(lapbeth_devices);
static rwlock_t lapbeth_devices_lock = RW_LOCK_UNLOCKED;

static __inline__ void lapbeth_hold(struct lapbethdev *lapbeth)
{
	atomic_inc(&lapbeth->refcnt);
}

static __inline__ void lapbeth_put(struct lapbethdev *lapbeth)
{
	if (atomic_dec_and_test(&lapbeth->refcnt))
		kfree(lapbeth);
}
/* ------------------------------------------------------------------------ */

/*
 *	Get the LAPB device for the ethernet device
 */
static __inline__ struct lapbethdev *lapbeth_get_x25_dev(struct net_device *dev)
{
	struct list_head *entry;
	struct lapbethdev *lapbeth, *use = NULL;

	read_lock(&lapbeth_devices_lock);

	list_for_each(entry, &lapbeth_devices) {
		lapbeth = list_entry(entry, struct lapbethdev, node);
		if (lapbeth->ethdev == dev) {
			use = lapbeth;
			break;
		}
	}
	if (use)
		lapbeth_hold(use);

	read_unlock(&lapbeth_devices_lock);
	return use;
}

static __inline__ int dev_is_ethdev(struct net_device *dev)
{
	return dev->type == ARPHRD_ETHER && strncmp(dev->name, "dummy", 5);
}

/*
 *	Sanity check: remove all devices that ceased to exists and
 *	return '1' if the given LAPB device was affected.
 */
static int lapbeth_check_devices(struct net_device *dev)
{
	struct lapbethdev *lapbeth;
	struct list_head *entry, *tmp;
	int result = 0;

	write_lock(&lapbeth_devices_lock);

	list_for_each_safe(entry, tmp, &lapbeth_devices) {
		lapbeth = list_entry(entry, struct lapbethdev, node);

		if (!dev_get(lapbeth->ethname)) {
			if (&lapbeth->axdev == dev)
				result = 1;

			unregister_netdev(&lapbeth->axdev);
			dev_put(lapbeth->ethdev);
			list_del(&lapbeth->node);
			lapbeth_put(lapbeth);
		}
	}
	write_unlock(&lapbeth_devices_lock);

	return result;
}

/* ------------------------------------------------------------------------ */

/*
 *	Receive a LAPB frame via an ethernet interface.
 */
static int lapbeth_rcv(struct sk_buff *skb, struct net_device *dev, struct packet_type *ptype)
{
	int len, err;
	struct lapbethdev *lapbeth;

	skb->sk = NULL;		/* Initially we don't know who it's for */

	lapbeth = lapbeth_get_x25_dev(dev);

	if (!lapbeth)
		goto drop;
	if (!netif_running(&lapbeth->axdev))
		goto put_drop;

	lapbeth->stats.rx_packets++;

	len = skb->data[0] + skb->data[1] * 256;

	skb_pull(skb, 2);	/* Remove the length bytes */
	skb_trim(skb, len);	/* Set the length of the data */

	if ((err = lapb_data_received(lapbeth, skb)) != LAPB_OK) {
		printk(KERN_DEBUG "lapbether: lapb_data_received err - %d\n", err);
		goto put_drop;
	}
	lapbeth_put(lapbeth);
out:
	return 0;
put_drop:
	lapbeth_put(lapbeth);
drop:
	kfree_skb(skb);
	goto out;
}

static int lapbeth_data_indication(void *token, struct sk_buff *skb)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)token;
	unsigned char *ptr;

	ptr  = skb_push(skb, 1);
	*ptr = 0x00;

	skb->dev      = &lapbeth->axdev;
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw  = skb->data;
	skb->pkt_type = PACKET_HOST;

	skb->dev->last_rx = jiffies;
	return netif_rx(skb);
}

/*
 *	Send a LAPB frame via an ethernet interface
 */
static int lapbeth_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)dev->priv;
	int err = -ENODEV;

	/*
	 * Just to be *really* sure not to send anything if the interface
	 * is down, the ethernet device may have gone.
	 */
	if (!netif_running(dev)) {
		lapbeth_check_devices(dev);
		goto drop;
	}

	switch (skb->data[0]) {
	case 0x00:
		err = 0;
		break;
	case 0x01:
		if ((err = lapb_connect_request(lapbeth)) != LAPB_OK)
			printk(KERN_ERR "lapbeth: lapb_connect_request "
			       "error: %d\n", err);
		goto drop_ok;
	case 0x02:
		if ((err = lapb_disconnect_request(lapbeth)) != LAPB_OK)
			printk(KERN_ERR "lapbeth: lapb_disconnect_request "
			       "err: %d\n", err);
		/* Fall thru */
	default:
		goto drop_ok;
	}

	skb_pull(skb, 1);

	if ((err = lapb_data_request(lapbeth, skb)) != LAPB_OK) {
		printk(KERN_ERR "lapbeth: lapb_data_request error - %d\n", err);
		err = -ENOMEM;
		goto drop;
	}
	err = 0;
out:
	return err;
drop_ok:
	err = 0;
drop:
	kfree_skb(skb);
	goto out;
}

static void lapbeth_data_transmit(void *token, struct sk_buff *skb)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)token;
	unsigned char *ptr;
	struct net_device *dev;
	int size = skb->len;

	skb->protocol = htons(ETH_P_X25);

	ptr = skb_push(skb, 2);

	*ptr++ = size % 256;
	*ptr++ = size / 256;

	lapbeth->stats.tx_packets++;

	skb->dev = dev = lapbeth->ethdev;

	dev->hard_header(skb, dev, ETH_P_DEC, bcast_addr, NULL, 0);

	dev_queue_xmit(skb);
}

static void lapbeth_connected(void *token, int reason)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)token;
	unsigned char *ptr;
	struct sk_buff *skb = dev_alloc_skb(1);

	if (!skb) {
		printk(KERN_ERR "lapbeth: out of memory\n");
		return;
	}

	ptr  = skb_put(skb, 1);
	*ptr = 0x01;

	skb->dev      = &lapbeth->axdev;
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw  = skb->data;
	skb->pkt_type = PACKET_HOST;

	skb->dev->last_rx = jiffies;
	netif_rx(skb);
}

static void lapbeth_disconnected(void *token, int reason)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)token;
	unsigned char *ptr;
	struct sk_buff *skb = dev_alloc_skb(1);

	if (!skb) {
		printk(KERN_ERR "lapbeth: out of memory\n");
		return;
	}

	ptr  = skb_put(skb, 1);
	*ptr = 0x02;

	skb->dev      = &lapbeth->axdev;
	skb->protocol = htons(ETH_P_X25);
	skb->mac.raw  = skb->data;
	skb->pkt_type = PACKET_HOST;

	skb->dev->last_rx = jiffies;
	netif_rx(skb);
}

/*
 *	Statistics
 */
static struct net_device_stats *lapbeth_get_stats(struct net_device *dev)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)dev->priv;
	return &lapbeth->stats;
}

/*
 *	Set AX.25 callsign
 */
static int lapbeth_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = (struct sockaddr *)addr;
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	return 0;
}

/*
 * open/close a device
 */
static int lapbeth_open(struct net_device *dev)
{
	struct lapb_register_struct lapbeth_callbacks;
	struct lapbethdev *lapbeth;
	int err;

	if (lapbeth_check_devices(dev))
		return -ENODEV;		/* oops, it's gone */

	lapbeth = (struct lapbethdev *)dev->priv;

	lapbeth_callbacks.connect_confirmation    = lapbeth_connected;
	lapbeth_callbacks.connect_indication      = lapbeth_connected;
	lapbeth_callbacks.disconnect_confirmation = lapbeth_disconnected;
	lapbeth_callbacks.disconnect_indication   = lapbeth_disconnected;
	lapbeth_callbacks.data_indication         = lapbeth_data_indication;
	lapbeth_callbacks.data_transmit           = lapbeth_data_transmit;

	if ((err = lapb_register(lapbeth, &lapbeth_callbacks)) != LAPB_OK) {
		printk(KERN_ERR "lapbeth: lapb_register error - %d\n", err);
		return -ENODEV;
	}

	netif_start_queue(dev);
	return 0;
}

static int lapbeth_close(struct net_device *dev)
{
	struct lapbethdev *lapbeth = (struct lapbethdev *)dev->priv;
	int err;

	netif_stop_queue(dev);

	if ((err = lapb_unregister(lapbeth)) != LAPB_OK)
		printk(KERN_ERR "lapbeth: lapb_unregister error - %d\n", err);

	return 0;
}

/* ------------------------------------------------------------------------ */

/*
 *	Setup a new device.
 */
static int lapbeth_new_device(struct net_device *dev)
{
	unsigned char buf[14];
	struct lapbethdev *lapbeth;
	int k, rc = -ENOMEM;

	if ((lapbeth = kmalloc(sizeof(struct lapbethdev), GFP_ATOMIC)) == NULL)
		goto out;

	memset(lapbeth, 0, sizeof(struct lapbethdev));

	dev_hold(dev);
	lapbeth->ethdev = dev;

	strncpy(lapbeth->ethname, dev->name, sizeof(lapbeth->ethname) - 1);
	lapbeth->ethname[sizeof(lapbeth->ethname) - 1] = '\0';
	atomic_set(&lapbeth->refcnt, 1);

	dev = &lapbeth->axdev;
	SET_MODULE_OWNER(dev);

	for (k = 0; k < MAXLAPBDEV; k++) {
		struct net_device *odev;

		sprintf(buf, "lapb%d", k);

		if ((odev = __dev_get_by_name(buf)) == NULL ||
		    lapbeth_check_devices(odev))
			break;
	}

	rc = -ENODEV;
	if (k == MAXLAPBDEV)
		goto fail;

	dev->priv = (void *)lapbeth;	/* pointer back */
	strcpy(dev->name, buf);

	rc = -EIO;
	if (register_netdev(dev))
		goto fail;

	dev->hard_start_xmit = lapbeth_xmit;
	dev->open	     = lapbeth_open;
	dev->stop	     = lapbeth_close;
	dev->set_mac_address = lapbeth_set_mac_address;
	dev->get_stats	     = lapbeth_get_stats;
	dev->type            = ARPHRD_X25;
	dev->hard_header_len = 3;
	dev->mtu             = 1000;
	dev->addr_len        = 0;

	write_lock(&lapbeth_devices_lock);
	list_add(&lapbeth->node, &lapbeth_devices);
	lapbeth_hold(lapbeth);
	write_unlock(&lapbeth_devices_lock);
	rc = 0;
out:
	return rc;
fail:
	dev_put(dev);
	kfree(lapbeth);
	goto out;
}

/*
 *	Handle device status changes.
 */
static int lapbeth_device_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct lapbethdev *lapbeth;
	struct net_device *dev = (struct net_device *)ptr;

	if (!dev_is_ethdev(dev))
		return NOTIFY_DONE;

	lapbeth_check_devices(NULL);

	switch (event) {
	case NETDEV_UP:
		/*
		 * New ethernet device -> new LAPB interface
		 */
		lapbeth = lapbeth_get_x25_dev(dev);

		if (lapbeth)
			lapbeth_put(lapbeth);
		else
			lapbeth_new_device(dev);
		break;
	case NETDEV_GOING_DOWN:
	case NETDEV_DOWN:	/* ethernet device closed -> close LAPB interface */
		lapbeth = lapbeth_get_x25_dev(dev);

		if (lapbeth) {
			dev_close(lapbeth->ethdev);
			lapbeth_put(lapbeth);
		}
		break;
	}

	return NOTIFY_DONE;
}

/* ------------------------------------------------------------------------ */

static struct packet_type lapbeth_packet_type = {
	.type = __constant_htons(ETH_P_DEC),
	.func = lapbeth_rcv,
};

static struct notifier_block lapbeth_dev_notifier = {
	.notifier_call = lapbeth_device_event,
};

static char banner[] __initdata = KERN_INFO "LAPB Ethernet driver version 0.02\n";

static int __init lapbeth_init_driver(void)
{
	struct net_device *dev;

	dev_add_pack(&lapbeth_packet_type);

	register_netdevice_notifier(&lapbeth_dev_notifier);

	printk(banner);

	read_lock_bh(&dev_base_lock);
	for (dev = dev_base; dev; dev = dev->next) {
		if (dev_is_ethdev(dev)) {
			read_unlock_bh(&dev_base_lock);
			lapbeth_new_device(dev);
			read_lock_bh(&dev_base_lock);
		}
	}
	read_unlock_bh(&dev_base_lock);

	return 0;
}
module_init(lapbeth_init_driver);

static void __exit lapbeth_cleanup_driver(void)
{
	struct lapbethdev *lapbeth;
	struct list_head *entry, *tmp;

	dev_remove_pack(&lapbeth_packet_type);
	unregister_netdevice_notifier(&lapbeth_dev_notifier);

	write_lock(&lapbeth_devices_lock);

	list_for_each_safe(entry, tmp, &lapbeth_devices) {
		lapbeth = list_entry(entry, struct lapbethdev, node);
		unregister_netdev(&lapbeth->axdev);
		list_del(&lapbeth->node);
		lapbeth_put(lapbeth);
	}

	write_unlock(&lapbeth_devices_lock);
}
module_exit(lapbeth_cleanup_driver);

MODULE_AUTHOR("Jonathan Naylor <g4klx@g4klx.demon.co.uk>");
MODULE_DESCRIPTION("The unofficial LAPB over Ethernet driver");
MODULE_LICENSE("GPL");
