/*
 * net-sysfs.c - network device class and attributes
 *
 * Copyright (c) 2003 Stephen Hemminber <shemminger@osdl.org>
 * 
 *
 * TODO:
 * last_tx
 * last_rx
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>

#define to_net_dev(class) container_of((class), struct net_device, class_dev)

/* generate a show function for  simple field */
#define NETDEVICE_SHOW(field, format_string)				\
static ssize_t show_##field(struct class_device *dev, char *buf)	\
{									\
	return sprintf(buf, format_string, to_net_dev(dev)->field);	\
}

/* generate a store function for a field with locking */
#define NETDEVICE_STORE(field)						\
static ssize_t 								\
store_##field(struct class_device *dev, const char *buf, size_t len)	\
{									\
	char *endp;							\
	long new = simple_strtol(buf, &endp, 16);			\
									\
	if (endp == buf || new < 0)					\
		return -EINVAL;						\
									\
	if (!capable(CAP_NET_ADMIN))					\
		return -EPERM;						\
									\
	rtnl_lock();							\
	to_net_dev(dev)->field = new;					\
	rtnl_unlock();							\
	return len;							\
}

/* generate a read-only network device class attribute */
#define NETDEVICE_ATTR(field, format_string)				\
NETDEVICE_SHOW(field, format_string)					\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_##field, NULL)		\

NETDEVICE_ATTR(addr_len, "%d\n");
NETDEVICE_ATTR(iflink, "%d\n");
NETDEVICE_ATTR(ifindex, "%d\n");
NETDEVICE_ATTR(features, "%#x\n");
NETDEVICE_ATTR(type, "%d\n");

static ssize_t format_addr(char *buf, const unsigned char *addr, int len)
{
	int i;
	char *cp = buf;

	read_lock(&dev_base_lock);
	for (i = 0; i < len; i++)
		cp += sprintf(cp, "%02x%c", addr[i],
			      i == (len - 1) ? '\n' : ':');
	read_unlock(&dev_base_lock);
	return cp - buf;
}

static ssize_t show_address(struct class_device *dev, char *buf)
{
	struct net_device *net = to_net_dev(dev);
	return format_addr(buf, net->dev_addr, net->addr_len);
}

static ssize_t show_broadcast(struct class_device *dev, char *buf)
{
	struct net_device *net = to_net_dev(dev);
	return format_addr(buf, net->broadcast, net->addr_len);
}

static CLASS_DEVICE_ATTR(address, S_IRUGO, show_address, NULL);
static CLASS_DEVICE_ATTR(broadcast, S_IRUGO, show_broadcast, NULL);

/* read-write attributes */
NETDEVICE_SHOW(mtu, "%d\n");

static ssize_t store_mtu(struct class_device *dev, const char *buf, size_t len)
{
	char *endp;
	int new_mtu;
	int err;

	new_mtu = simple_strtoul(buf, &endp, 10);
	if (endp == buf) 
		return -EINVAL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	
	rtnl_lock();
	err = dev_set_mtu(to_net_dev(dev), new_mtu);
	rtnl_unlock();

	return err == 0 ? len : err;
}

static CLASS_DEVICE_ATTR(mtu, S_IRUGO | S_IWUSR, show_mtu, store_mtu);

NETDEVICE_SHOW(flags, "%#x\n");

static ssize_t store_flags(struct class_device *dev, const char *buf, size_t len)
{
	unsigned long new_flags;
	char *endp;
	int err = 0;

	new_flags = simple_strtoul(buf, &endp, 16);
	if (endp == buf)
		return -EINVAL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	
	rtnl_lock();
	err = dev_change_flags(to_net_dev(dev), new_flags);
	rtnl_unlock();

	return err ? err : len;
}

static CLASS_DEVICE_ATTR(flags, S_IRUGO | S_IWUSR, show_flags, store_flags);

NETDEVICE_SHOW(tx_queue_len, "%lu\n");
NETDEVICE_STORE(tx_queue_len);
static CLASS_DEVICE_ATTR(tx_queue_len, S_IRUGO | S_IWUSR, show_tx_queue_len, 
			 store_tx_queue_len);

static struct class net_class = {
	.name = "net",
};


static struct class_device_attribute *net_class_attributes[] = {
	&class_device_attr_ifindex,
	&class_device_attr_iflink,
	&class_device_attr_addr_len,
	&class_device_attr_tx_queue_len,
	&class_device_attr_features,
	&class_device_attr_mtu,
	&class_device_attr_flags,
	&class_device_attr_type,
	&class_device_attr_address,
	&class_device_attr_broadcast,
	NULL
};

struct netstat_fs_entry {
	struct attribute attr;
	ssize_t (*show)(const struct net_device_stats *, char *);
	ssize_t (*store)(struct net_device_stats *, const char *, size_t);
};

static ssize_t net_device_stat_show(unsigned long var, char *buf)
{
	return sprintf(buf, "%ld\n", var);
}

/* generate a read-only statistics attribute */
#define NETDEVICE_STAT(_NAME)						\
static ssize_t show_stat_##_NAME(const struct net_device_stats *stats,	\
				 char *buf)				\
{									\
	return net_device_stat_show(stats->_NAME, buf);			\
}									\
static struct netstat_fs_entry net_stat_##_NAME  = {		   	\
	.attr = {.name = __stringify(_NAME), .mode = S_IRUGO },		\
	.show = show_stat_##_NAME,					\
}

NETDEVICE_STAT(rx_packets);
NETDEVICE_STAT(tx_packets);
NETDEVICE_STAT(rx_bytes);
NETDEVICE_STAT(tx_bytes);
NETDEVICE_STAT(rx_errors);
NETDEVICE_STAT(tx_errors);
NETDEVICE_STAT(rx_dropped);
NETDEVICE_STAT(tx_dropped);
NETDEVICE_STAT(multicast);
NETDEVICE_STAT(collisions);
NETDEVICE_STAT(rx_length_errors);
NETDEVICE_STAT(rx_over_errors);
NETDEVICE_STAT(rx_crc_errors);
NETDEVICE_STAT(rx_frame_errors);
NETDEVICE_STAT(rx_fifo_errors);
NETDEVICE_STAT(rx_missed_errors);
NETDEVICE_STAT(tx_aborted_errors);
NETDEVICE_STAT(tx_carrier_errors);
NETDEVICE_STAT(tx_fifo_errors);
NETDEVICE_STAT(tx_heartbeat_errors);
NETDEVICE_STAT(tx_window_errors);
NETDEVICE_STAT(rx_compressed);
NETDEVICE_STAT(tx_compressed);

static struct attribute *default_attrs[] = {
	&net_stat_rx_packets.attr,
	&net_stat_tx_packets.attr,
	&net_stat_rx_bytes.attr,
	&net_stat_tx_bytes.attr,
	&net_stat_rx_errors.attr,
	&net_stat_tx_errors.attr,
	&net_stat_rx_dropped.attr,
	&net_stat_tx_dropped.attr,
	&net_stat_multicast.attr,
	&net_stat_collisions.attr,
	&net_stat_rx_length_errors.attr,
	&net_stat_rx_over_errors.attr,
	&net_stat_rx_crc_errors.attr,
	&net_stat_rx_frame_errors.attr,
	&net_stat_rx_fifo_errors.attr,
	&net_stat_rx_missed_errors.attr,
	&net_stat_tx_aborted_errors.attr,
	&net_stat_tx_carrier_errors.attr,
	&net_stat_tx_fifo_errors.attr,
	&net_stat_tx_heartbeat_errors.attr,
	&net_stat_tx_window_errors.attr,
	&net_stat_rx_compressed.attr,
	&net_stat_tx_compressed.attr,
	NULL
};


static ssize_t
netstat_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct netstat_fs_entry *entry
		= container_of(attr, struct netstat_fs_entry, attr);
	struct class_device *class_dev
		= container_of(kobj->parent, struct class_device, kobj);
	struct net_device *dev
		= to_net_dev(class_dev);
	struct net_device_stats *stats 
		= dev->get_stats ? dev->get_stats(dev) : NULL;

	if (stats && entry->show) 
		return entry->show(stats, buf);
	return -EINVAL;
}

static struct sysfs_ops netstat_sysfs_ops = {
	.show = netstat_attr_show,
};

static struct kobj_type netstat_ktype = {
	.sysfs_ops	= &netstat_sysfs_ops,
	.default_attrs  = default_attrs,
};

/* Create sysfs entries for network device. */
int netdev_register_sysfs(struct net_device *net)
{
	struct class_device *class_dev = &(net->class_dev);
	int i;
	struct class_device_attribute *attr;
	int ret;

	class_dev->class = &net_class;
	class_dev->class_data = net;

	strlcpy(class_dev->class_id, net->name, BUS_ID_SIZE);
	if ((ret = class_device_register(class_dev)))
		goto out;

	for (i = 0; (attr = net_class_attributes[i]); i++) {
		if ((ret = class_device_create_file(class_dev, attr)))
		    goto out_unreg;
	}

	
	net->stats_kobj.parent = NULL;
	if (net->get_stats) {
		struct kobject *k = &net->stats_kobj;

		k->parent = kobject_get(&class_dev->kobj);
		if (!k->parent) {
			ret = -EBUSY;
			goto out_unreg;
		}

		strlcpy(k->name, "statistics", KOBJ_NAME_LEN);
		k->ktype = &netstat_ktype;

		if((ret = kobject_register(k))) 
			 goto out_unreg; 
	}

out:
	return ret;
out_unreg:
	printk(KERN_WARNING "%s: sysfs attribute registration failed %d\n",
	       net->name, ret);
	class_device_unregister(class_dev);
	goto out;
}

void netdev_unregister_sysfs(struct net_device *net)
{
	if (net->stats_kobj.parent) 
		kobject_unregister(&net->stats_kobj);

	class_device_unregister(&net->class_dev);
}

int netdev_sysfs_init(void)
{
	return class_register(&net_class);
}
