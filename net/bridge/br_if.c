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
#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/if_arp.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <net/sock.h>

#include "br_private.h"

/* Limited to 256 ports because of STP protocol pdu */
#define  BR_MAX_PORTS	256

/*
 * Determine initial path cost based on speed.
 * using recommendations from 802.1d standard
 *
 * Need to simulate user ioctl because not all device's that support
 * ethtool, use ethtool_ops.  Also, since driver might sleep need to
 * not be holding any locks.
 */
static int br_initial_port_cost(struct net_device *dev)
{

	struct ethtool_cmd ecmd = { ETHTOOL_GSET };
	struct ifreq ifr;
	mm_segment_t old_fs;
	int err;

	strncpy(ifr.ifr_name, dev->name, IFNAMSIZ);
	ifr.ifr_data = (void *) &ecmd;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = dev_ethtool(&ifr);
	set_fs(old_fs);
	
	if (!err) {
		switch(ecmd.speed) {
		case SPEED_100:
			return 19;
		case SPEED_1000:
			return 4;
		case SPEED_10000:
			return 2;
		case SPEED_10:
			return 100;
		default:
			pr_info("bridge: can't decode speed from %s: %d\n",
				dev->name, ecmd.speed);
			return 100;
		}
	}

	/* Old silly heuristics based on name */
	if (!strncmp(dev->name, "lec", 3))
		return 7;

	if (!strncmp(dev->name, "plip", 4))
		return 2500;

	return 100;	/* assume old 10Mbps */
}

static void destroy_nbp(void *arg)
{
	struct net_bridge_port *p = arg;

	p->dev->br_port = NULL;

	BUG_ON(timer_pending(&p->message_age_timer));
	BUG_ON(timer_pending(&p->forward_delay_timer));
	BUG_ON(timer_pending(&p->hold_timer));

	dev_put(p->dev);
	kfree(p);
}

/* called under bridge lock */
static void del_nbp(struct net_bridge_port *p)
{
	struct net_device *dev = p->dev;

	br_stp_disable_port(p);

	dev_set_promiscuity(dev, -1);

	list_del_rcu(&p->list);

	br_fdb_delete_by_port(p->br, p);

	del_timer(&p->message_age_timer);
	del_timer(&p->forward_delay_timer);
	del_timer(&p->hold_timer);
	
	call_rcu(&p->rcu, destroy_nbp, p);
}

static void del_br(struct net_bridge *br)
{
	struct list_head *p, *n;

	spin_lock_bh(&br->lock);
	list_for_each_safe(p, n, &br->port_list) {
		del_nbp(list_entry(p, struct net_bridge_port, list));
	}
	spin_unlock_bh(&br->lock);

	del_timer_sync(&br->gc_timer);

 	unregister_netdevice(br->dev);
}

static struct net_bridge *new_nb(const char *name)
{
	struct net_bridge *br;
	struct net_device *dev;

	dev = alloc_netdev(sizeof(struct net_bridge), name,
			   br_dev_setup);
	
	if (!dev)
		return NULL;

	br = dev->priv;
	br->dev = dev;

	br->lock = SPIN_LOCK_UNLOCKED;
	INIT_LIST_HEAD(&br->port_list);
	br->hash_lock = RW_LOCK_UNLOCKED;

	br->bridge_id.prio[0] = 0x80;
	br->bridge_id.prio[1] = 0x00;
	memset(br->bridge_id.addr, 0, ETH_ALEN);

	br->stp_enabled = 0;
	br->designated_root = br->bridge_id;
	br->root_path_cost = 0;
	br->root_port = 0;
	br->bridge_max_age = br->max_age = 20 * HZ;
	br->bridge_hello_time = br->hello_time = 2 * HZ;
	br->bridge_forward_delay = br->forward_delay = 15 * HZ;
	br->topology_change = 0;
	br->topology_change_detected = 0;
	br->ageing_time = 300 * HZ;
	INIT_LIST_HEAD(&br->age_list);

	br_stp_timer_init(br);

	return br;
}

static int free_port(struct net_bridge *br)
{
	int index;
	struct net_bridge_port *p;
	long inuse[BR_MAX_PORTS/(sizeof(long)*8)];

	/* find free port number */
	memset(inuse, 0, sizeof(inuse));
	list_for_each_entry(p, &br->port_list, list) {
		set_bit(p->port_no, inuse);
	}

	index = find_first_zero_bit(inuse, BR_MAX_PORTS);
	if (index >= BR_MAX_PORTS)
		return -EXFULL;

	return index;
}

/* called under bridge lock */
static struct net_bridge_port *new_nbp(struct net_bridge *br, 
				       struct net_device *dev,
				       unsigned long cost)
{
	int index;
	struct net_bridge_port *p;
	
	index = free_port(br);
	if (index < 0)
		return ERR_PTR(index);

	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (p == NULL)
		return ERR_PTR(-ENOMEM);

	memset(p, 0, sizeof(*p));
	p->br = br;
	dev_hold(dev);
	p->dev = dev;
	p->path_cost = cost;
	p->priority = 0x80;
	dev->br_port = p;
	p->port_no = index;
	br_init_port(p);
	p->state = BR_STATE_DISABLED;

	return p;
}

int br_add_bridge(const char *name)
{
	struct net_bridge *br;
	int ret;

	if ((br = new_nb(name)) == NULL) 
		return -ENOMEM;

	ret = register_netdev(br->dev);
	if (ret)
		free_netdev(br->dev);
	return ret;
}

int br_del_bridge(const char *name)
{
	struct net_device *dev;
	int ret = 0;

	rtnl_lock();
	dev = __dev_get_by_name(name);
	if (dev == NULL) 
		ret =  -ENXIO; 	/* Could not find device */

	else if (!(dev->priv_flags & IFF_EBRIDGE)) {
		/* Attempt to delete non bridge device! */
		ret = -EPERM;
	}

	else if (dev->flags & IFF_UP) {
		/* Not shutdown yet. */
		ret = -EBUSY;
	} 

	else 
		del_br(dev->priv);

	rtnl_unlock();
	return ret;
}

int br_add_if(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;
	unsigned long cost;
	int err = 0;

	if (dev->flags & IFF_LOOPBACK || dev->type != ARPHRD_ETHER)
		return -EINVAL;

	if (dev->hard_start_xmit == br_dev_xmit)
		return -ELOOP;

	cost = br_initial_port_cost(dev);

	spin_lock_bh(&br->lock);
	if (dev->br_port != NULL)
		err = -EBUSY;

	else if (IS_ERR(p = new_nbp(br, dev, cost)))
		err = PTR_ERR(p);

 	else if ((err = br_fdb_insert(br, p, dev->dev_addr, 1)))
 		 destroy_nbp(p);
 
	else {
		dev_set_promiscuity(dev, 1);

		list_add_rcu(&p->list, &br->port_list);

		br_stp_recalculate_bridge_id(br);
		if ((br->dev->flags & IFF_UP) && (dev->flags & IFF_UP))
			br_stp_enable_port(p);

	}
	spin_unlock_bh(&br->lock);
	return err;
}

int br_del_if(struct net_bridge *br, struct net_device *dev)
{
	struct net_bridge_port *p;
	int err = 0;

	spin_lock_bh(&br->lock);
	p = dev->br_port;
	if (!p || p->br != br) 
		err = -EINVAL;
	else {
		del_nbp(p);
		br_stp_recalculate_bridge_id(br);
	}
	spin_unlock_bh(&br->lock);

	return err;
}

int br_get_bridge_ifindices(int *indices, int num)
{
	struct net_device *dev;
	int i = 0;

	read_lock(&dev_base_lock);
	for (dev = dev_base; dev && i < num; dev = dev->next) {
		if (dev->priv_flags & IFF_EBRIDGE) 
			indices[i++] = dev->ifindex;
	}
	read_unlock(&dev_base_lock);

	return i;
}

void br_get_port_ifindices(struct net_bridge *br, int *ifindices, int num)
{
	struct net_bridge_port *p;

	rcu_read_lock();
	list_for_each_entry_rcu(p, &br->port_list, list) {
		if (p->port_no < num)
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
		if (dev->priv_flags & IFF_EBRIDGE)
			del_br(dev->priv);
	}
	rtnl_unlock();

}
