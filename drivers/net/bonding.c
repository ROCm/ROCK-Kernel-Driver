/*
 * originally based on the dummy device.
 *
 * Copyright 1999, Thomas Davis, tadavis@lbl.gov.  
 * Licensed under the GPL. Based on dummy.c, and eql.c devices.
 *
 * bond.c: a bonding/etherchannel/sun trunking net driver
 *
 * This is useful to talk to a Cisco 5500, running Etherchannel, aka:
 *	Linux Channel Bonding
 *	Sun Trunking (Solaris)
 *
 * How it works:
 *    ifconfig bond0 ipaddress netmask up
 *      will setup a network device, with an ip address.  No mac address 
 *	will be assigned at this time.  The hw mac address will come from 
 *	the first slave bonded to the channel.  All slaves will then use 
 *	this hw mac address.
 *
 *    ifconfig bond0 down
 *         will release all slaves, marking them as down.
 *
 *    ifenslave bond0 eth0
 *	will attache eth0 to bond0 as a slave.  eth0 hw mac address will either
 *	a: be used as initial mac address
 *	b: if a hw mac address already is there, eth0's hw mac address 
 *	   will then  be set from bond0.
 *
 * v0.1 - first working version.
 * v0.2 - changed stats to be calculated by summing slaves stats.
 * 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/if_bonding.h>

typedef struct slave
{
	struct slave *next;
	struct slave *prev;
	struct net_device *dev;
} slave_t;

typedef struct bonding
{
	slave_t *next;
	slave_t *prev;
	struct net_device *master;

	slave_t *current_slave;
	struct net_device_stats stats;
} bonding_t;


static int bond_xmit(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *bond_get_stats(struct net_device *dev);

static struct net_device *this_bond;

static int bond_open(struct net_device *dev)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static void release_one_slave(struct net_device *master, slave_t *slave)
{
	bonding_t *bond = master->priv;

	spin_lock_bh(&master->xmit_lock);
	if (bond->current_slave == slave)
		bond->current_slave = slave->next;
	slave->next->prev = slave->prev;
	slave->prev->next = slave->next;
	spin_unlock_bh(&master->xmit_lock);

	netdev_set_master(slave->dev, NULL);

	dev_put(slave->dev);
	kfree(slave);
	MOD_DEC_USE_COUNT;
}

static int bond_close(struct net_device *master)
{
	bonding_t *bond = master->priv;
	slave_t *slave;

	while ((slave = bond->next) != (slave_t*)bond)
		release_one_slave(master, slave);

	MOD_DEC_USE_COUNT;
	return 0;
}

static void bond_set_multicast_list(struct net_device *master)
{
	bonding_t *bond = master->priv;
	slave_t *slave;

	for (slave = bond->next; slave != (slave_t*)bond; slave = slave->next) {
		slave->dev->mc_list = master->mc_list;
		slave->dev->mc_count = master->mc_count;
		slave->dev->flags = master->flags;
		slave->dev->set_multicast_list(slave->dev);
	}
}

static int bond_enslave(struct net_device *master, struct net_device *dev)
{
	int err;
	bonding_t *bond = master->priv;
	slave_t *slave;

	if (dev->type != master->type)
		return -ENODEV;

	if ((slave = kmalloc(sizeof(slave_t), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	memset(slave, 0, sizeof(slave_t));

	err = netdev_set_master(dev, master);
	if (err) {
		kfree(slave);
		return err;
	}

	slave->dev = dev;

	spin_lock_bh(&master->xmit_lock);

	dev_hold(dev);

	slave->prev = bond->prev;
	slave->next = (slave_t*)bond;
	slave->prev->next = slave;
	slave->next->prev = slave;

	spin_unlock_bh(&master->xmit_lock);

	MOD_INC_USE_COUNT;
	return 0;
}

static int bond_release(struct net_device *master, struct net_device *dev)
{
	bonding_t *bond = master->priv;
	slave_t *slave;

	if (dev->master != master)
		return -EINVAL;

	for (slave = bond->next; slave != (slave_t*)bond; slave = slave->next) {
		if (slave->dev == dev) {
			release_one_slave(master, slave);
			break;
		}
	}

	return 0;
}

/* It is pretty silly, SIOCSIFHWADDR exists to make this. */

static int bond_sethwaddr(struct net_device *master, struct net_device *slave)
{
	memcpy(master->dev_addr, slave->dev_addr, slave->addr_len);
	return 0;
}

static int bond_ioctl(struct net_device *master, struct ifreq *ifr, int cmd)
{
	struct net_device *slave = __dev_get_by_name(ifr->ifr_slave);

	if (slave == NULL)
		return -ENODEV;

	switch (cmd) {
	case BOND_ENSLAVE:
		return bond_enslave(master, slave);
	case BOND_RELEASE:
		return bond_release(master, slave);
	case BOND_SETHWADDR:
		return bond_sethwaddr(master, slave);
	default:
		return -EOPNOTSUPP;
	}
}

static int bond_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *slave = ptr;

	if (this_bond == NULL ||
	    this_bond == slave ||
	    this_bond != slave->master)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UNREGISTER:
		bond_release(this_bond, slave);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block bond_netdev_notifier={
	bond_event,
	NULL,
	0
};

static int __init bond_init(struct net_device *dev)
{
	bonding_t *bond;

	bond = kmalloc(sizeof(struct bonding), GFP_KERNEL);
	if (bond == NULL)
		return -ENOMEM;

	memset(bond, 0, sizeof(struct bonding));
	bond->next = (slave_t*)bond;
	bond->prev = (slave_t*)bond;
	bond->master = dev;
	bond->current_slave = (slave_t*)bond;
	dev->priv = bond;

	/* Initialize the device structure. */
	dev->hard_start_xmit = bond_xmit;
	dev->get_stats	= bond_get_stats;
	dev->open = bond_open;
	dev->stop = bond_close;
	dev->set_multicast_list = bond_set_multicast_list;
	dev->do_ioctl = bond_ioctl;

	/* Fill in the fields of the device structure with ethernet-generic 
	   values. */
	ether_setup(dev);
	dev->tx_queue_len = 0;
	dev->flags |= IFF_MASTER;

	this_bond = dev;

	register_netdevice_notifier(&bond_netdev_notifier);

	return 0;
}

static int bond_xmit(struct sk_buff *skb, struct net_device *dev)
{
	bonding_t *bond = dev->priv;
	slave_t *slave, *start_at;
	int pkt_len = skb->len;

	slave = start_at = bond->current_slave;

	do {
		if (slave == (slave_t*)bond)
			continue;

		if (netif_running(slave->dev) && netif_carrier_ok(slave->dev)) {
			bond->current_slave = slave->next;
			skb->dev = slave->dev;

			if (dev_queue_xmit(skb)) {
				bond->stats.tx_dropped++;
			} else {
				bond->stats.tx_packets++;
				bond->stats.tx_bytes += pkt_len;
			}
			return 0;
		}
	} while ((slave = slave->next) != start_at);

	bond->stats.tx_dropped++;
	kfree_skb(skb);
	return 0;
}

static struct net_device_stats *bond_get_stats(struct net_device *dev)
{
	bonding_t *bond = dev->priv;

	return &bond->stats;
}

static struct net_device dev_bond = {
		"",
		0, 0, 0, 0,
	 	0x0, 0,
	 	0, 0, 0, NULL, bond_init };

static int __init bonding_init(void)
{
	/* Find a name for this unit */
	int err=dev_alloc_name(&dev_bond,"bond%d");

	if (err<0)
		return err;

	if (register_netdev(&dev_bond) != 0)
		return -EIO;

	return 0;
}

static void __exit bonding_exit(void)
{
	unregister_netdevice_notifier(&bond_netdev_notifier);

	unregister_netdev(&dev_bond);

	kfree(dev_bond.priv);
}

module_init(bonding_init);
module_exit(bonding_exit);

/*
 * Local variables:
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
