/*
 *	Userspace interface
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_if.c,v 1.7 2001/12/24 00:59:55 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/if_arp.h>
#include <linux/if_bridge.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include "br_private.h"

static int br_initial_port_cost(struct net_device *dev)
{
	if (!strncmp(dev->name, "lec", 3))
		return 7;

	if (!strncmp(dev->name, "eth", 3))
		return 100;			/* FIXME handle 100Mbps */

	if (!strncmp(dev->name, "plip", 4))
		return 2500;

	return 100;
}

static void destroy_nbp(void *arg)
{
	struct net_bridge_port *p = arg;
	dev_put(p->dev);
	kfree(p);
}

/* called under bridge lock */
static void del_nbp(struct net_bridge_port *p)
{
	struct net_device *dev = p->dev;

	br_stp_disable_port(p);

	dev_set_promiscuity(dev, -1);
	dev->br_port = NULL;

	list_del_rcu(&p->list);

	br_fdb_delete_by_port(p->br, p);

	call_rcu(&p->rcu, destroy_nbp, p);
}

static void del_ifs(struct net_bridge *br)
{
	struct list_head *p, *n;

	spin_lock_bh(&br->lock);
	list_for_each_safe(p, n, &br->port_list) {
		del_nbp(list_entry(p, struct net_bridge_port, list));
	}
	spin_unlock_bh(&br->lock);
}

static struct net_bridge *new_nb(const char *name)
{
	struct net_bridge *br;
	struct net_device *dev;

	if ((br = kmalloc(sizeof(*br), GFP_KERNEL)) == NULL)
		return NULL;

	memset(br, 0, sizeof(*br));
	dev = &br->dev;

	init_timer(&br->tick);

	strncpy(dev->name, name, IFNAMSIZ);
	dev->priv = br;
	dev->priv_flags = IFF_EBRIDGE;
	ether_setup(dev);
	br_dev_setup(dev);

	br->lock = SPIN_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&br->port_list);
	br->hash_lock = RW_LOCK_UNLOCKED;

	br->bridge_id.prio[0] = 0x80;
	br->bridge_id.prio[1] = 0x00;
	memset(br->bridge_id.addr, 0, ETH_ALEN);

	br->stp_enabled = 1;
	br->designated_root = br->bridge_id;
	br->root_path_cost = 0;
	br->root_port = 0;
	br->bridge_max_age = br->max_age = 20 * HZ;
	br->bridge_hello_time = br->hello_time = 2 * HZ;
	br->bridge_forward_delay = br->forward_delay = 15 * HZ;
	br->topology_change = 0;
	br->topology_change_detected = 0;
	br_timer_clear(&br->hello_timer);
	br_timer_clear(&br->tcn_timer);
	br_timer_clear(&br->topology_change_timer);

	br->ageing_time = 300 * HZ;
	br->gc_interval = 4 * HZ;

	return br;
}

/* called under bridge lock */
static struct net_bridge_port *new_nbp(struct net_bridge *br, struct net_device *dev)
{
	int i;
	struct net_bridge_port *p;

	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == NULL)
		return p;

	memset(p, 0, sizeof(*p));
	p->br = br;
	p->dev = dev;
	p->path_cost = br_initial_port_cost(dev);
	p->priority = 0x80;

	for (i=1;i<255;i++)
		if (br_get_port(br, i) == NULL)
			break;

	if (i == 255) {
		kfree(p);
		return NULL;
	}

	dev->br_port = p;

	p->port_no = i;
	br_init_port(p);
	p->state = BR_STATE_DISABLED;

	list_add_rcu(&p->list, &br->port_list);

	return p;
}

int br_add_bridge(const char *name)
{
	struct net_bridge *br;
	int ret;

	if ((br = new_nb(name)) == NULL) 
		return -ENOMEM;

	ret = register_netdev(&br->dev);
	if (ret)
		kfree(br);
	return ret;
}

int br_del_bridge(const char *name)
{
	struct net_device *dev;
	int ret = 0;

	dev = dev_get_by_name(name);
	if (dev == NULL) 
		return -ENXIO; 	/* Could not find device */

	if (!(dev->priv_flags & IFF_EBRIDGE)) {
		/* Attempt to delete non bridge device! */
		ret = -EPERM;
	}

	else if (dev->flags & IFF_UP) {
		/* Not shutdown yet. */
		ret = -EBUSY;
	} 

	else {
		del_ifs((struct net_bridge *) dev->priv);
	
		unregister_netdev(dev);
	}

	dev_put(dev);
	return ret;
}

int br_add_if(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;

	if (dev->br_port != NULL)
		return -EBUSY;

	if (dev->flags & IFF_LOOPBACK || dev->type != ARPHRD_ETHER)
		return -EINVAL;

	if (dev->hard_start_xmit == br_dev_xmit)
		return -ELOOP;

	dev_hold(dev);
	spin_lock_bh(&br->lock);
	if ((p = new_nbp(br, dev)) == NULL) {
		spin_unlock_bh(&br->lock);
		dev_put(dev);
		return -EXFULL;
	}

	dev_set_promiscuity(dev, 1);

	br_stp_recalculate_bridge_id(br);
	br_fdb_insert(br, p, dev->dev_addr, 1);
	if ((br->dev.flags & IFF_UP) && (dev->flags & IFF_UP))
		br_stp_enable_port(p);
	spin_unlock_bh(&br->lock);

	return 0;
}

int br_del_if(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;
	int retval = 0;

	spin_lock_bh(&br->lock);
	if ((p = dev->br_port) == NULL || p->br != br)
		retval = -EINVAL;
	else {
		del_nbp(p);
		br_stp_recalculate_bridge_id(br);
	}
	spin_unlock_bh(&br->lock);

	return retval;
}

int br_get_bridge_ifindices(int *indices, int num)
{
	struct net_device *dev;
	int i = 0;

	rtnl_shlock();
	for (dev = dev_base; dev && i < num; dev = dev->next) {
		if (dev->priv_flags & IFF_EBRIDGE) 
			indices[i++] = dev->ifindex;
	}
	rtnl_shunlock();

	return i;
}

void br_get_port_ifindices(struct net_bridge *br, int *ifindices)
{
	struct net_bridge_port *p;

	rcu_read_lock();
	list_for_each_entry_rcu(p, &br->port_list, list) {
		ifindices[p->port_no] = p->dev->ifindex;
	}
	rcu_read_unlock();
}


void __exit br_cleanup_bridges(void)
{
	struct net_device *dev, *nxt;

	rtnl_lock();
	for (dev = dev_base; dev; dev = nxt) {
		nxt = dev->next;
		if ((dev->priv_flags & IFF_EBRIDGE)
		    && dev->owner == THIS_MODULE) {
			pr_debug("cleanup %s\n", dev->name);

			del_ifs((struct net_bridge *) dev->priv);
			
			unregister_netdevice(dev);
		}
	}
	rtnl_unlock();

}
