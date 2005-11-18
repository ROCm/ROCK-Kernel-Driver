/*
 *  Capabilities Linux Security Module
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/smp_lock.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/ptrace.h>
#include <linux/moduleparam.h>

/* Note: Capabilities are default now, even if CONFIG_SECURITY
 * is enabled and no LSM is loaded. (Previously, the dummy
 * functions would have been called in that case which resulted
 * in a slightly unusable system.)
 * The capability LSM may still be compiled and loaded; it won't
 * make a difference though except for slowing down some operations
 * a tiny bit and (more severly) for disallowing loading another LSM.
 * To have it as LSM may still be useful: It could be stacked on top
 * of another LSM (if the other LSM allows this or if the stacker
 * is used).
 * If the capability LSM is loaded, we do NOT register the 
 * capability_security_ops but a second structure capability_ops
 * that has identical entries. We need to differentiate
 * between capabilities used as default and used as LSM as in
 * the latter case replacing it by just loading another LSM is
 * not possible.
 */

/* Struct from commoncaps */
extern struct security_operations capability_security_ops;
/* Struct to hold the copy */
static struct security_operations capability_ops;

#define MY_NAME __stringify(KBUILD_MODNAME)

/* flag to keep track of how we were registered */
static int secondary;

static int capability_disable;
module_param_named(disable, capability_disable, int, 0);
MODULE_PARM_DESC(disable, "To disable capabilities module set disable = 1");

static int __init capability_init (void)
{
	if (capability_disable) {
		printk(KERN_INFO "Capabilities disabled at initialization\n");
		return 0;
	}
	memcpy(&capability_ops, &capability_security_ops, sizeof(capability_ops));
	/* register ourselves with the security framework */
	if (register_security (&capability_ops)) {
		/* try registering with primary module */
		if (mod_reg_security (MY_NAME, &capability_ops)) {
			printk (KERN_INFO "Failure registering capabilities "
				"with primary security module.\n");
			return -EINVAL;
		}
		secondary = 1;
	}
	printk (KERN_INFO "Capability LSM initialized%s\n",
		secondary ? " as secondary" : "");
	return 0;
}

static void __exit capability_exit (void)
{
	if (capability_disable)
		return;
	/* remove ourselves from the security framework */
	if (secondary) {
		if (mod_unreg_security (MY_NAME, &capability_ops))
			printk (KERN_INFO "Failure unregistering capabilities "
				"with primary module.\n");
		return;
	}

	if (unregister_security (&capability_ops)) {
		printk (KERN_INFO
			"Failure unregistering capabilities with the kernel\n");
	}
}

security_initcall (capability_init);
module_exit (capability_exit);

MODULE_DESCRIPTION("Standard Linux Capabilities Security Module");
MODULE_LICENSE("GPL");
