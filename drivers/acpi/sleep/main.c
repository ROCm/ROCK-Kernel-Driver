/*
 * sleep.c - ACPI sleep support.
 * 
 *  Copyright (c) 2000-2003 Patrick Mochel
 *
 *  Portions are
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 */

#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include "sleep.h"

u8 sleep_states[ACPI_S_STATE_COUNT];

extern void do_suspend_lowlevel_s4bios(int);
extern void do_suspend_lowlevel(int);

static u32 acpi_suspend_states[] = {
	[PM_SUSPEND_ON]		= ACPI_STATE_S0,
	[PM_SUSPEND_STANDBY]	= ACPI_STATE_S1,
	[PM_SUSPEND_MEM]	= ACPI_STATE_S3,
	[PM_SUSPEND_DISK]	= ACPI_STATE_S4,
};

/**
 *	acpi_pm_prepare - Do preliminary suspend work.
 *	@state:		suspend state we're entering.
 *
 */

static int acpi_pm_prepare(u32 state)
{
	int error = 0;
	u32 acpi_state = acpi_suspend_states[state];

	if (!sleep_states[acpi_state])
		return -EPERM;

	/* do we have a wakeup address for S2 and S3? */
	/* Here, we support only S4BIOS, those we set the wakeup address */
	/* S4OS is only supported for now via swsusp.. */
	if (state == PM_SUSPEND_MEM || state == PM_SUSPEND_DISK) {
		if (!acpi_wakeup_address)
			return -EFAULT;
		acpi_set_firmware_waking_vector(
			(acpi_physical_address) acpi_wakeup_address);
	}

	ACPI_FLUSH_CPU_CACHE();

	/* Do arch specific saving of state. */
	if (state > PM_SUSPEND_STANDBY) {
		if ((error = acpi_save_state_mem()))
			goto Err;
	}

	acpi_enter_sleep_state_prep(acpi_state);

	return 0;
 Err:
	acpi_set_firmware_waking_vector(0);
	return error;
}


static int acpi_pm_enter(u32 state)
{
	acpi_status status = AE_OK;
	unsigned long flags = 0;

	ACPI_FLUSH_CPU_CACHE();
	local_irq_save(flags);
	switch (state)
	{
	case PM_SUSPEND_STANDBY:
		barrier();
		status = acpi_enter_sleep_state(acpi_suspend_states[state]);
		break;

	case PM_SUSPEND_MEM:
		do_suspend_lowlevel(0);
		break;

	case PM_SUSPEND_DISK:
		do_suspend_lowlevel_s4bios(0);
		break;
	default:
		return -EINVAL;
	}
	local_irq_restore(flags);
	printk(KERN_DEBUG "Back to C!\n");

	return ACPI_SUCCESS(status) ? 0 : -EFAULT;
}

static int acpi_pm_finish(u32 state)
{
	acpi_leave_sleep_state(state);

	/* restore processor state
	 * We should only be here if we're coming back from STR or STD.
	 * And, in the case of the latter, the memory image should have already
	 * been loaded from disk.
	 */
	if (state > ACPI_STATE_S1)
		acpi_restore_state_mem();

	/* reset firmware waking vector */
	acpi_set_firmware_waking_vector((acpi_physical_address) 0);

	if (dmi_broken & BROKEN_INIT_AFTER_S1) {
		printk("Broken toshiba laptop -> kicking interrupts\n");
		init_8259A(0);
	}
	return 0;
}


static struct pm_ops acpi_pm_ops = {
	.prepare	= acpi_pm_prepare,
	.enter		= acpi_pm_enter,
	.finish		= acpi_pm_finish,
};

static int __init acpi_sleep_init(void)
{
	int			i = 0;

	if (acpi_disabled)
		return 0;

	printk(KERN_INFO PREFIX "(supports");
	for (i=0; i<ACPI_S_STATE_COUNT; i++) {
		acpi_status status;
		u8 type_a, type_b;
		status = acpi_get_sleep_type_data(i, &type_a, &type_b);
		if (ACPI_SUCCESS(status)) {
			sleep_states[i] = 1;
			printk(" S%d", i);
		}
		if (i == ACPI_STATE_S4 && acpi_gbl_FACS->S4bios_f) {
			sleep_states[i] = 1;
			printk(" S4bios");
		}
	}
	printk(")\n");

	pm_set_ops(&acpi_pm_ops);
	return 0;
}

late_initcall(acpi_sleep_init);
