/*
 * Root Plug sample LSM module
 *
 * Originally written for a Linux Journal.
 *
 * Copyright (C) 2002 Greg Kroah-Hartman <greg@kroah.com>
 *
 * Prevents any programs running with egid == 0 if a specific USB device
 * is not present in the system.  Yes, it can be gotten around, but is a
 * nice starting point for people to play with, and learn the LSM
 * interface.
 *
 * If you want to turn this into something with a semblance of security,
 * you need to hook the task_* functions also.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2 of the
 *	License.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/security.h>
#include <linux/usb.h>

/* flag to keep track of how we were registered */
static int secondary;

/* default is a generic type of usb to serial converter */
static int vendor_id = 0x0557;
static int product_id = 0x2008;

MODULE_PARM(vendor_id, "h");
MODULE_PARM_DESC(vendor_id, "USB Vendor ID of device to look for");

MODULE_PARM(product_id, "h");
MODULE_PARM_DESC(product_id, "USB Product ID of device to look for");

/* should we print out debug messages */
static int debug = 0;

MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

#if defined(CONFIG_SECURITY_ROOTPLUG_MODULE)
#define MY_NAME THIS_MODULE->name
#else
#define MY_NAME "root_plug"
#endif

#define dbg(fmt, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: %s: " fmt ,	\
				MY_NAME , __FUNCTION__ , 	\
				## arg);			\
	} while (0)

extern struct list_head usb_bus_list;
extern struct semaphore usb_bus_list_lock;

static int match_device (struct usb_device *dev)
{
	int retval = -ENODEV;
	int child;

	dbg ("looking at vendor %d, product %d\n",
	     dev->descriptor.idVendor,
	     dev->descriptor.idProduct);

	/* see if this device matches */
	if ((dev->descriptor.idVendor == vendor_id) &&
	    (dev->descriptor.idProduct == product_id)) {
		dbg ("found the device!\n");
		retval = 0;
		goto exit;
	}

	/* look through all of the children of this device */
	for (child = 0; child < dev->maxchild; ++child) {
		if (dev->children[child]) {
			retval = match_device (dev->children[child]);
			if (retval == 0)
				goto exit;
		}
	}
exit:
	return retval;
}

static int find_usb_device (void)
{
	struct list_head *buslist;
	struct usb_bus *bus;
	int retval = -ENODEV;
	
	down (&usb_bus_list_lock);
	for (buslist = usb_bus_list.next;
	     buslist != &usb_bus_list; 
	     buslist = buslist->next) {
		bus = container_of (buslist, struct usb_bus, bus_list);
		retval = match_device(bus->root_hub);
		if (retval == 0)
			goto exit;
	}
exit:
	up (&usb_bus_list_lock);
	return retval;
}
	

static int rootplug_bprm_check_security (struct linux_binprm *bprm)
{
	dbg ("file %s, e_uid = %d, e_gid = %d\n",
	     bprm->filename, bprm->e_uid, bprm->e_gid);

	if (bprm->e_gid == 0) {
		if (find_usb_device() != 0) {
			dbg ("e_gid = 0, and device not found, "
				"task not allowed to run...\n");
			return -EPERM;
		}
	}

	return 0;
}

static struct security_operations rootplug_security_ops = {
	/* Use the capability functions for some of the hooks */
	.ptrace =			cap_ptrace,
	.capget =			cap_capget,
	.capset_check =			cap_capset_check,
	.capset_set =			cap_capset_set,
	.capable =			cap_capable,

	.bprm_compute_creds =		cap_bprm_compute_creds,
	.bprm_set_security =		cap_bprm_set_security,

	.task_post_setuid =		cap_task_post_setuid,
	.task_kmod_set_label =		cap_task_kmod_set_label,
	.task_reparent_to_init =	cap_task_reparent_to_init,

	.bprm_check_security =		rootplug_bprm_check_security,
};

static int __init rootplug_init (void)
{
	/* register ourselves with the security framework */
	if (register_security (&rootplug_security_ops)) {
		printk (KERN_INFO 
			"Failure registering Root Plug module with the kernel\n");
		/* try registering with primary module */
		if (mod_reg_security (MY_NAME, &rootplug_security_ops)) {
			printk (KERN_INFO "Failure registering Root Plug "
				" module with primary security module.\n");
			return -EINVAL;
		}
		secondary = 1;
	}
	printk (KERN_INFO "Root Plug module initialized, "
		"vendor_id = %4.4x, product id = %4.4x\n", vendor_id, product_id);
	return 0;
}

static void __exit rootplug_exit (void)
{
	/* remove ourselves from the security framework */
	if (secondary) {
		if (mod_unreg_security (MY_NAME, &rootplug_security_ops))
			printk (KERN_INFO "Failure unregistering Root Plug "
				" module with primary module.\n");
	} else { 
		if (unregister_security (&rootplug_security_ops)) {
			printk (KERN_INFO "Failure unregistering Root Plug "
				"module with the kernel\n");
		}
	}
	printk (KERN_INFO "Root Plug module removed\n");
}

module_init (rootplug_init);
module_exit (rootplug_exit);

MODULE_DESCRIPTION("Root Plug sample LSM module, written for Linux Journal article");
MODULE_LICENSE("GPL");

