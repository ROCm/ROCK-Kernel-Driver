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
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/cpufreq.h>

#include <asm/leds.h>
#include <asm/system.h>

/*
 * Tell the linker that pm_do_suspend may not be present.
 */
extern int pm_do_suspend(void) __attribute__((weak));

int suspend(void)
{
	int ret;

	if (!pm_do_suspend)
		return -ENOSYS;

	/*
	 * Suspend "legacy" devices.
	 */
	ret = pm_send_all(PM_SUSPEND, (void *)3);
	if (ret != 0)
		goto out;

	/*
	 * Tell LDM devices we're going to suspend.
	 */
	ret = device_suspend(4, SUSPEND_NOTIFY);
	if (ret != 0)
		goto resume_legacy;

	/*
	 * Disable, devices, and save state.
	 */
	device_suspend(4, SUSPEND_DISABLE);
	device_suspend(4, SUSPEND_SAVE_STATE);

	/*
	 * Tell devices that they're going to be powered off.
	 */
	device_suspend(4, SUSPEND_POWER_DOWN);

	local_irq_disable();
	leds_event(led_stop);

	ret = pm_do_suspend();

	leds_event(led_start);
	local_irq_enable();

	/*
	 * Tell devices that they now have power.
	 */
	device_resume(RESUME_POWER_ON);

	/*
	 * Restore the CPU frequency settings.
	 */
#ifdef CONFIG_CPU_FREQ
	cpufreq_restore();
#endif

	/*
	 * Resume LDM devices.
	 */
	device_resume(RESUME_RESTORE_STATE);
	device_resume(RESUME_ENABLE);

 resume_legacy:
	/*
	 * Resume "legacy" devices.
	 */
	pm_send_all(PM_RESUME, (void *)0);

 out:
	return ret;
}

#ifdef CONFIG_SYSCTL
/*
 * We really want this to die.  It's a disgusting hack using unallocated
 * sysctl numbers.  We should be using a real interface.
 */

#include <linux/init.h>
#include <linux/sysctl.h>

static int
pm_sysctl_proc_handler(ctl_table *ctl, int write, struct file *filp,
		       void *buffer, size_t *lenp)
{
	int ret = -EIO;
	printk("PM: task %s (pid %d) uses deprecated sysctl PM interface\n",
		current->comm, current->pid);
	if (write)
		ret = suspend();
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
