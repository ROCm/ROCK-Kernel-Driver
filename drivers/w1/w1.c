/*
 * 	w1.c
 *
 * Copyright (c) 2004 Evgeniy Polyakov <johnpol@2ka.mipt.ru>
 * 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <asm/atomic.h>

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/suspend.h>

#include "w1.h"
#include "w1_io.h"
#include "w1_log.h"
#include "w1_int.h"
#include "w1_family.h"
#include "w1_netlink.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <johnpol@2ka.mipt.ru>");
MODULE_DESCRIPTION("Driver for 1-wire Dallas network protocol.");

static int w1_timeout = 5 * HZ;
int w1_max_slave_count = 10;

module_param_named(timeout, w1_timeout, int, 0);
module_param_named(max_slave_count, w1_max_slave_count, int, 0);

spinlock_t w1_mlock = SPIN_LOCK_UNLOCKED;
LIST_HEAD(w1_masters);

static pid_t control_thread;
static int control_needs_exit;
static DECLARE_COMPLETION(w1_control_complete);
static DECLARE_WAIT_QUEUE_HEAD(w1_control_wait);

static int w1_master_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int w1_master_probe(struct device *dev)
{
	return -ENODEV;
}

static int w1_master_remove(struct device *dev)
{
	return 0;
}

static void w1_master_release(struct device *dev)
{
	struct w1_master *md = container_of(dev, struct w1_master, dev);

	complete(&md->dev_released);
}

static void w1_slave_release(struct device *dev)
{
	struct w1_slave *sl = container_of(dev, struct w1_slave, dev);

	complete(&sl->dev_released);
}

static ssize_t w1_default_read_name(struct device *dev, char *buf)
{
	return sprintf(buf, "No family registered.\n");
}

static ssize_t w1_default_read_bin(struct kobject *kobj, char *buf, loff_t off,
		     size_t count)
{
	return sprintf(buf, "No family registered.\n");
}

struct bus_type w1_bus_type = {
	.name = "w1",
	.match = w1_master_match,
};

struct device_driver w1_driver = {
	.name = "w1_driver",
	.bus = &w1_bus_type,
	.probe = w1_master_probe,
	.remove = w1_master_remove,
};

struct device w1_device = {
	.parent = NULL,
	.bus = &w1_bus_type,
	.bus_id = "w1 bus master",
	.driver = &w1_driver,
	.release = &w1_master_release
};

static struct device_attribute w1_slave_attribute = {
	.attr = {
			.name = "name",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_default_read_name,
};

static struct device_attribute w1_slave_attribute_val = {
	.attr = {
			.name = "value",
			.mode = S_IRUGO,
			.owner = THIS_MODULE
	},
	.show = &w1_default_read_name,
};

static ssize_t w1_master_attribute_show(struct device *dev, char *buf)
{
	return sprintf(buf, "please fix me\n");
#if 0
	struct w1_master *md = container_of(dev, struct w1_master, dev);
	int c = PAGE_SIZE;

	if (down_interruptible(&md->mutex))
		return -EBUSY;

	c -= snprintf(buf + PAGE_SIZE - c, c, "%s\n", md->name);
	c -= snprintf(buf + PAGE_SIZE - c, c,
		       "bus_master=0x%p, timeout=%d, max_slave_count=%d, attempts=%lu\n",
		       md->bus_master, w1_timeout, md->max_slave_count,
		       md->attempts);
	c -= snprintf(buf + PAGE_SIZE - c, c, "%d slaves: ",
		       md->slave_count);
	if (md->slave_count == 0)
		c -= snprintf(buf + PAGE_SIZE - c, c, "no.\n");
	else {
		struct list_head *ent, *n;
		struct w1_slave *sl;

		list_for_each_safe(ent, n, &md->slist) {
			sl = list_entry(ent, struct w1_slave, w1_slave_entry);

			c -= snprintf(buf + PAGE_SIZE - c, c, "%s[%p] ",
				       sl->name, sl);
		}
		c -= snprintf(buf + PAGE_SIZE - c, c, "\n");
	}

	up(&md->mutex);

	return PAGE_SIZE - c;
#endif
}

struct device_attribute w1_master_attribute = {
	.attr = {
			.name = "w1_master_stats",
			.mode = S_IRUGO,
			.owner = THIS_MODULE,
	},
	.show = &w1_master_attribute_show,
};

static struct bin_attribute w1_slave_bin_attribute = {
	.attr = {
		 	.name = "w1_slave",
		 	.mode = S_IRUGO,
			.owner = THIS_MODULE,
	},
	.size = W1_SLAVE_DATA_SIZE,
	.read = &w1_default_read_bin,
};

static int __w1_attach_slave_device(struct w1_slave *sl)
{
	int err;

	sl->dev.parent = &sl->master->dev;
	sl->dev.driver = sl->master->driver;
	sl->dev.bus = &w1_bus_type;
	sl->dev.release = &w1_slave_release;

	snprintf(&sl->dev.bus_id[0], sizeof(sl->dev.bus_id),
		  "%x-%llx",
		  (unsigned int) sl->reg_num.family,
		  (unsigned long long) sl->reg_num.id);
	snprintf (&sl->name[0], sizeof(sl->name),
		  "%x-%llx",
		  (unsigned int) sl->reg_num.family,
		  (unsigned long long) sl->reg_num.id);

	dev_dbg(&sl->dev, "%s: registering %s.\n", __func__,
		&sl->dev.bus_id[0]);

	err = device_register(&sl->dev);
	if (err < 0) {
		dev_err(&sl->dev,
			 "Device registration [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		return err;
	}

	w1_slave_bin_attribute.read = sl->family->fops->rbin;
	w1_slave_attribute.show = sl->family->fops->rname;
	w1_slave_attribute_val.show = sl->family->fops->rval;
	w1_slave_attribute_val.attr.name = sl->family->fops->rvalname;

	err = device_create_file(&sl->dev, &w1_slave_attribute);
	if (err < 0) {
		dev_err(&sl->dev,
			 "sysfs file creation for [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		device_unregister(&sl->dev);
		return err;
	}

	err = device_create_file(&sl->dev, &w1_slave_attribute_val);
	if (err < 0) {
		dev_err(&sl->dev,
			 "sysfs file creation for [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		device_remove_file(&sl->dev, &w1_slave_attribute);
		device_unregister(&sl->dev);
		return err;
	}

	err = sysfs_create_bin_file(&sl->dev.kobj, &w1_slave_bin_attribute);
	if (err < 0) {
		dev_err(&sl->dev,
			 "sysfs file creation for [%s] failed. err=%d\n",
			 sl->dev.bus_id, err);
		device_remove_file(&sl->dev, &w1_slave_attribute);
		device_remove_file(&sl->dev, &w1_slave_attribute_val);
		device_unregister(&sl->dev);
		return err;
	}

	list_add_tail(&sl->w1_slave_entry, &sl->master->slist);

	return 0;
}

static int w1_attach_slave_device(struct w1_master *dev, struct w1_reg_num *rn)
{
	struct w1_slave *sl;
	struct w1_family *f;
	int err;

	sl = kmalloc(sizeof(struct w1_slave), GFP_KERNEL);
	if (!sl) {
		dev_err(&dev->dev,
			 "%s: failed to allocate new slave device.\n",
			 __func__);
		return -ENOMEM;
	}

	memset(sl, 0, sizeof(*sl));

	sl->owner = THIS_MODULE;
	sl->master = dev;

	memcpy(&sl->reg_num, rn, sizeof(sl->reg_num));
	atomic_set(&sl->refcnt, 0);
	init_completion(&sl->dev_released);

	spin_lock(&w1_flock);
	f = w1_family_registered(rn->family);
	if (!f) {
		spin_unlock(&w1_flock);
		dev_info(&dev->dev, "Family %x is not registered.\n",
			  rn->family);
		kfree(sl);
		return -ENODEV;
	}
	__w1_family_get(f);
	spin_unlock(&w1_flock);

	sl->family = f;


	err = __w1_attach_slave_device(sl);
	if (err < 0) {
		dev_err(&dev->dev, "%s: Attaching %s failed.\n", __func__,
			 sl->name);
		w1_family_put(sl->family);
		kfree(sl);
		return err;
	}

	dev->slave_count++;

	return 0;
}

static void w1_slave_detach(struct w1_slave *sl)
{
	dev_info(&sl->dev, "%s: detaching %s.\n", __func__, sl->name);

	while (atomic_read(&sl->refcnt))
		schedule_timeout(10);

	sysfs_remove_bin_file(&sl->dev.kobj, &w1_slave_bin_attribute);
	device_remove_file(&sl->dev, &w1_slave_attribute);
	device_unregister(&sl->dev);
	w1_family_put(sl->family);
}

static void w1_search(struct w1_master *dev)
{
	u64 last, rn, tmp;
	int i, count = 0, slave_count;
	int last_family_desc, last_zero, last_device;
	int search_bit, id_bit, comp_bit, desc_bit;
	struct list_head *ent;
	struct w1_slave *sl;
	int family_found = 0;
	struct w1_netlink_msg msg;

	dev->attempts++;

	memset(&msg, 0, sizeof(msg));

	search_bit = id_bit = comp_bit = 0;
	rn = tmp = last = 0;
	last_device = last_zero = last_family_desc = 0;

	desc_bit = 64;

	while (!(id_bit && comp_bit) && !last_device
		&& count++ < dev->max_slave_count) {
		last = rn;
		rn = 0;

		last_family_desc = 0;

		/*
		 * Reset bus and all 1-wire device state machines
		 * so they can respond to our requests.
		 *
		 * Return 0 - device(s) present, 1 - no devices present.
		 */
		if (w1_reset_bus(dev)) {
			dev_info(&dev->dev, "No devices present on the wire.\n");
			break;
		}

#if 1
		memset(&msg, 0, sizeof(msg));

		w1_write_8(dev, W1_SEARCH);
		for (i = 0; i < 64; ++i) {
			/*
			 * Read 2 bits from bus.
			 * All who don't sleep must send ID bit and COMPLEMENT ID bit.
			 * They actually are ANDed between all senders.
			 */
			id_bit = w1_read_bit(dev);
			comp_bit = w1_read_bit(dev);

			if (id_bit && comp_bit)
				break;

			if (id_bit == 0 && comp_bit == 0) {
				if (i == desc_bit)
					search_bit = 1;
				else if (i > desc_bit)
					search_bit = 0;
				else
					search_bit = ((last >> i) & 0x1);

				if (search_bit == 0) {
					last_zero = i;
					if (last_zero < 9)
						last_family_desc = last_zero;
				}

			}
			else
				search_bit = id_bit;

			tmp = search_bit;
			rn |= (tmp << i);

			/*
			 * Write 1 bit to bus
			 * and make all who don't have "search_bit" in "i"'th position
			 * in it's registration number sleep.
			 */
			w1_write_bit(dev, search_bit);

		}
#endif
		msg.id.w1_id = rn;
		msg.val = w1_calc_crc8((u8 *) & rn, 7);
		w1_netlink_send(dev, &msg);

		if (desc_bit == last_zero)
			last_device = 1;

		desc_bit = last_zero;

		slave_count = 0;
		list_for_each(ent, &dev->slist) {
			struct w1_reg_num *tmp;

			tmp = (struct w1_reg_num *) &rn;

			sl = list_entry(ent, struct w1_slave, w1_slave_entry);

			if (sl->reg_num.family == tmp->family &&
			    sl->reg_num.id == tmp->id &&
			    sl->reg_num.crc == tmp->crc)
				break;
			else if (sl->reg_num.family == tmp->family) {
				family_found = 1;
				break;
			}

			slave_count++;
		}

		if (slave_count == dev->slave_count &&
		    msg.val && (*((__u8 *) & msg.val) == msg.id.id.crc)) {
			w1_attach_slave_device(dev, (struct w1_reg_num *) &rn);
		}
	}
}

int w1_control(void *data)
{
	struct w1_slave *sl;
	struct w1_master *dev;
	struct list_head *ent, *ment, *n, *mn;
	int err, have_to_wait = 0, timeout;

	daemonize("w1_control");
	allow_signal(SIGTERM);

	while (!control_needs_exit || have_to_wait) {
		have_to_wait = 0;

		timeout = w1_timeout;
		do {
			timeout = interruptible_sleep_on_timeout(&w1_control_wait, timeout);
			if (current->flags & PF_FREEZE)
				refrigerator(PF_FREEZE);
		} while (!signal_pending(current) && (timeout > 0));

		if (signal_pending(current))
			flush_signals(current);

		list_for_each_safe(ment, mn, &w1_masters) {
			dev = list_entry(ment, struct w1_master, w1_master_entry);

			if (!control_needs_exit && !dev->need_exit)
				continue;
			/*
			 * Little race: we can create thread but not set the flag.
			 * Get a chance for external process to set flag up.
			 */
			if (!dev->initialized) {
				have_to_wait = 1;
				continue;
			}

			spin_lock(&w1_mlock);
			list_del(&dev->w1_master_entry);
			spin_unlock(&w1_mlock);

			if (control_needs_exit) {
				dev->need_exit = 1;

				err = kill_proc(dev->kpid, SIGTERM, 1);
				if (err)
					dev_err(&dev->dev,
						 "Failed to send signal to w1 kernel thread %d.\n",
						 dev->kpid);
			}

			wait_for_completion(&dev->dev_exited);

			list_for_each_safe(ent, n, &dev->slist) {
				sl = list_entry(ent, struct w1_slave, w1_slave_entry);

				if (!sl)
					dev_warn(&dev->dev,
						  "%s: slave entry is NULL.\n",
						  __func__);
				else {
					list_del(&sl->w1_slave_entry);

					w1_slave_detach(sl);
					kfree(sl);
				}
			}
			device_remove_file(&dev->dev, &w1_master_attribute);
			atomic_dec(&dev->refcnt);
		}
	}

	complete_and_exit(&w1_control_complete, 0);
}

int w1_process(void *data)
{
	struct w1_master *dev = (struct w1_master *) data;
	unsigned long timeout;

	daemonize("%s", dev->name);
	allow_signal(SIGTERM);

	while (!dev->need_exit) {
		timeout = w1_timeout;
		do {
			timeout = interruptible_sleep_on_timeout(&dev->kwait, timeout);
			if (current->flags & PF_FREEZE)
				refrigerator(PF_FREEZE);
		} while (!signal_pending(current) && (timeout > 0));

		if (signal_pending(current))
			flush_signals(current);

		if (dev->need_exit)
			break;

		if (!dev->initialized)
			continue;

		if (down_interruptible(&dev->mutex))
			continue;
		w1_search(dev);
		up(&dev->mutex);
	}

	atomic_dec(&dev->refcnt);
	complete_and_exit(&dev->dev_exited, 0);

	return 0;
}

int w1_init(void)
{
	int retval;

	printk(KERN_INFO "Driver for 1-wire Dallas network protocol.\n");

	retval = bus_register(&w1_bus_type);
	if (retval) {
		printk(KERN_ERR "Failed to register bus. err=%d.\n", retval);
		goto err_out_exit_init;
	}

	retval = driver_register(&w1_driver);
	if (retval) {
		printk(KERN_ERR
			"Failed to register master driver. err=%d.\n",
			retval);
		goto err_out_bus_unregister;
	}

	control_thread = kernel_thread(&w1_control, NULL, 0);
	if (control_thread < 0) {
		printk(KERN_ERR "Failed to create control thread. err=%d\n",
			control_thread);
		retval = control_thread;
		goto err_out_driver_unregister;
	}

	return 0;

err_out_driver_unregister:
	driver_unregister(&w1_driver);

err_out_bus_unregister:
	bus_unregister(&w1_bus_type);

err_out_exit_init:
	return retval;
}

void w1_fini(void)
{
	struct w1_master *dev;
	struct list_head *ent, *n;

	list_for_each_safe(ent, n, &w1_masters) {
		dev = list_entry(ent, struct w1_master, w1_master_entry);
		__w1_remove_master_device(dev);
	}

	control_needs_exit = 1;

	wait_for_completion(&w1_control_complete);

	driver_unregister(&w1_driver);
	bus_unregister(&w1_bus_type);
}

module_init(w1_init);
module_exit(w1_fini);
