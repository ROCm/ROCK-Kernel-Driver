// SPDX-License-Identifier: GPL-2.0-only
/* Kernel thread helper functions.
 *   Copyright (C) 2004 IBM Corporation, Rusty Russell.
 *   Copyright (C) 2009 Red Hat, Inc.
 *
 * Creation is done via kthreadd, so that we get a clean environment
 * even if we're invoked from userspace (think modprobe, hotplug cpu,
 * etc.).
 */

/*
* FIXME: implement below API when kernel version < 4.2
*/
#include <linux/printk.h>
#include <linux/version.h>
#include <kcl/kcl_kthread.h>

#if !defined(HAVE___KTHREAD_SHOULD_PARK)
bool __kcl_kthread_should_park(struct task_struct *k)
{
	pr_warn_once("This kernel version not support API: __kthread_should_park!\n");
	return false;
}
EXPORT_SYMBOL(__kcl_kthread_should_park);
#endif
