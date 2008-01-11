/******************************************************************************
 * drivers/xen/netback/accel.c
 *
 * Interface between backend virtual network device and accelerated plugin. 
 * 
 * Copyright (C) 2007 Solarflare Communications, Inc
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <linux/list.h>
#include <asm/atomic.h>
#include <xen/xenbus.h>
#include <linux/mutex.h>

#include "common.h"

#if 0
#undef DPRINTK
#define DPRINTK(fmt, args...)						\
	printk("netback/accel (%s:%d) " fmt ".\n", __FUNCTION__, __LINE__, ##args)
#endif

/* 
 * A list of available netback accelerator plugin modules (each list
 * entry is of type struct netback_accelerator) 
 */ 
static struct list_head accelerators_list;
/* Lock used to protect access to accelerators_list */
DEFINE_MUTEX(accelerators_mutex);

/* 
 * Compare a backend to an accelerator, and decide if they are
 * compatible (i.e. if the accelerator should be used by the
 * backend) 
 */
static int match_accelerator(struct xenbus_device *xendev,
			     struct backend_info *be, 
			     struct netback_accelerator *accelerator)
{
	int rc = 0;
	char *eth_name = xenbus_read(XBT_NIL, xendev->nodename, "accel", NULL);
	
	if (IS_ERR(eth_name)) {
		/* Probably means not present */
		DPRINTK("%s: no match due to xenbus_read accel error %ld\n",
			__FUNCTION__, PTR_ERR(eth_name));
		return 0;
	} else {
		if (!strcmp(eth_name, accelerator->eth_name))
			rc = 1;
		kfree(eth_name);
		return rc;
	}
}


static void do_probe(struct backend_info *be, 
		     struct netback_accelerator *accelerator,
		     struct xenbus_device *xendev) 
{
	be->accelerator = accelerator;
	atomic_inc(&be->accelerator->use_count);
	if (be->accelerator->hooks->probe(xendev) != 0) {
		atomic_dec(&be->accelerator->use_count);
		module_put(be->accelerator->hooks->owner);
		be->accelerator = NULL;
	}
}


/*
 * Notify suitable backends that a new accelerator is available and
 * connected.  This will also notify the accelerator plugin module
 * that it is being used for a device through the probe hook.
 */
static int netback_accelerator_probe_backend(struct device *dev, void *arg)
{
	struct netback_accelerator *accelerator = 
		(struct netback_accelerator *)arg;
	struct xenbus_device *xendev = to_xenbus_device(dev);

	if (!strcmp("vif", xendev->devicetype)) {
		struct backend_info *be = xendev->dev.driver_data;

		if (match_accelerator(xendev, be, accelerator) &&
		    try_module_get(accelerator->hooks->owner)) {
			do_probe(be, accelerator, xendev);
		}
	}
	return 0;
}


/*
 * Notify suitable backends that an accelerator is unavailable.
 */
static int netback_accelerator_remove_backend(struct device *dev, void *arg)
{
	struct xenbus_device *xendev = to_xenbus_device(dev);
	struct netback_accelerator *accelerator = 
		(struct netback_accelerator *)arg;
	
	if (!strcmp("vif", xendev->devicetype)) {
		struct backend_info *be = xendev->dev.driver_data;

		if (be->accelerator == accelerator) {
			be->accelerator->hooks->remove(xendev);
			atomic_dec(&be->accelerator->use_count);
			module_put(be->accelerator->hooks->owner);
			be->accelerator = NULL;
		}
	}
	return 0;
}



/*
 * Entry point for an netback accelerator plugin module.  Called to
 * advertise its presence, and connect to any suitable backends.
 */
int netback_connect_accelerator(unsigned version, int id, const char *eth_name, 
				struct netback_accel_hooks *hooks)
{
	struct netback_accelerator *new_accelerator;
	unsigned eth_name_len;

	if (version != NETBACK_ACCEL_VERSION) {
		if (version > NETBACK_ACCEL_VERSION) {
			/* Caller has higher version number, leave it
			   up to them to decide whether to continue.
			   They can recall with a lower number if
			   they're happy to be compatible with us */
			return NETBACK_ACCEL_VERSION;
		} else {
			/* We have a more recent version than caller.
			   Currently reject, but may in future be able
			   to be backwardly compatible */
			return -EPROTO;
		}
	}

	new_accelerator = 
		kmalloc(sizeof(struct netback_accelerator), GFP_KERNEL);
	if (!new_accelerator) {
		DPRINTK("%s: failed to allocate memory for accelerator\n",
			__FUNCTION__);
		return -ENOMEM;
	}

	new_accelerator->id = id;
	
	eth_name_len = strlen(eth_name)+1;
	new_accelerator->eth_name = kmalloc(eth_name_len, GFP_KERNEL);
	if (!new_accelerator->eth_name) {
		DPRINTK("%s: failed to allocate memory for eth_name string\n",
			__FUNCTION__);
		kfree(new_accelerator);
		return -ENOMEM;
	}
	strlcpy(new_accelerator->eth_name, eth_name, eth_name_len);
	
	new_accelerator->hooks = hooks;

	atomic_set(&new_accelerator->use_count, 0);
	
	mutex_lock(&accelerators_mutex);
	list_add(&new_accelerator->link, &accelerators_list);
	
	/* tell existing backends about new plugin */
	xenbus_for_each_backend(new_accelerator, 
				netback_accelerator_probe_backend);

	mutex_unlock(&accelerators_mutex);

	return 0;

}
EXPORT_SYMBOL_GPL(netback_connect_accelerator);


/* 
 * Disconnect an accelerator plugin module that has previously been
 * connected.
 */
void netback_disconnect_accelerator(int id, const char *eth_name)
{
	struct netback_accelerator *accelerator, *next;

	mutex_lock(&accelerators_mutex);
	list_for_each_entry_safe(accelerator, next, &accelerators_list, link) {
		if (!strcmp(eth_name, accelerator->eth_name)) {
			xenbus_for_each_backend
				(accelerator, netback_accelerator_remove_backend);
			BUG_ON(atomic_read(&accelerator->use_count) != 0);
			list_del(&accelerator->link);				
			kfree(accelerator->eth_name);
			kfree(accelerator);
			break;
		}
	}
	mutex_unlock(&accelerators_mutex);
}
EXPORT_SYMBOL_GPL(netback_disconnect_accelerator);


void netback_probe_accelerators(struct backend_info *be,
				struct xenbus_device *dev)
{
	struct netback_accelerator *accelerator;

	/* 
	 * Check list of accelerators to see if any is suitable, and
	 * use it if it is.
	 */
	mutex_lock(&accelerators_mutex);
	list_for_each_entry(accelerator, &accelerators_list, link) { 
		if (match_accelerator(dev, be, accelerator) &&
		    try_module_get(accelerator->hooks->owner)) {
			do_probe(be, accelerator, dev);
			break;
		}
	}
	mutex_unlock(&accelerators_mutex);
}


void netback_remove_accelerators(struct backend_info *be,
				 struct xenbus_device *dev)
{
	mutex_lock(&accelerators_mutex);
	/* Notify the accelerator (if any) of this device's removal */
	if (be->accelerator != NULL) {
		be->accelerator->hooks->remove(dev);
		atomic_dec(&be->accelerator->use_count);
		module_put(be->accelerator->hooks->owner);
		be->accelerator = NULL;
	}
	mutex_unlock(&accelerators_mutex);
}


void netif_accel_init(void)
{
	INIT_LIST_HEAD(&accelerators_list);
}
