/*
 * Suspend support specific for i386.
 *
 * Distribute under GPLv2
 *
 * Copyright (c) 2002 Pavel Machek <pavel@suse.cz>
 */

#define ACPI_C
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/sysrq.h>
#include <linux/compatmac.h>
#include <linux/proc_fs.h>
#include <linux/irq.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <asm/uaccess.h>
#include <asm/acpi.h>


void do_suspend_lowlevel(int resume)
{
/*
 * FIXME: This function should really be written in assembly. Actually
 * requirement is that it does not touch stack, because %esp will be
 * wrong during resume before restore_processor_context(). Check
 * assembly if you modify this.
 */
	if (!resume) {
		save_processor_context();
		acpi_save_register_state((unsigned long)&&acpi_sleep_done);
		acpi_enter_sleep_state(3);
		return;
	}
acpi_sleep_done:
	restore_processor_context();
}
