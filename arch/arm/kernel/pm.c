/*
 *  linux/arch/arm/kernel/suspend.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License.
 *
 *  This is the common support code for suspending an ARM machine.
 *  pm_do_suspend() is responsible for actually putting the CPU to
 *  sleep.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/pm.h>
#include <linux/errno.h>
#include <linux/sched.h>

#ifdef CONFIG_SYSCTL
/*
 * We really want this to die.  It's a disgusting hack using unallocated
 * sysctl numbers.  We should be using a real interface.
 */

static int
pm_sysctl_proc_handler(ctl_table *ctl, int write, struct file *filp,
		       void *buffer, size_t *lenp)
{
	int ret = -EIO;
	printk("PM: task %s (pid %d) uses deprecated sysctl PM interface\n",
		current->comm, current->pid);
	if (write)
		ret = pm_suspend(PM_SUSPEND_MEM);
	return ret;
}

/*
 * This came from arch/arm/mach-sa1100/pm.c:
 * Copyright (c) 2001 Cliff Brake <cbrake@accelent.com>
 *  with modifications by Nicolas Pitre and Russell King.
 *
 * ARGH!  ACPI people defined CTL_ACPI in linux/acpi.h rather than
 * linux/sysctl.h.
 *
 * This means our interface here won't survive long - it needs a new
 * interface.  Quick hack to get this working - use sysctl id 9999.
 */
#warning ACPI broke the kernel, this interface needs to be fixed up.
#define CTL_ACPI 9999
#define ACPI_S1_SLP_TYP 19

static struct ctl_table pm_table[] =
{
	{
		.ctl_name	= ACPI_S1_SLP_TYP,
		.procname	= "suspend",
		.mode		= 0200,
		.proc_handler	= pm_sysctl_proc_handler,
	},
	{0}
};

static struct ctl_table pm_dir_table[] =
{
	{
		.ctl_name	= CTL_ACPI,
		.procname	= "pm",
		.mode		= 0555,
		.child		= pm_table,
	},
	{0}
};

/*
 * Initialize power interface
 */
static int __init pm_init(void)
{
	register_sysctl_table(pm_dir_table, 1);
	return 0;
}

fs_initcall(pm_init);

#endif
