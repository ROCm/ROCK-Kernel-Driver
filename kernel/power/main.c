/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 * This file is release under the GPLv2
 *
 */

#include <linux/suspend.h>
#include <linux/kobject.h>
#include <linux/reboot.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/fs.h>


#include "power.h"

static DECLARE_MUTEX(pm_sem);

static struct pm_ops * pm_ops = NULL;

static u32 pm_disk_mode = PM_DISK_SHUTDOWN;

#ifdef CONFIG_SOFTWARE_SUSPEND
static int have_swsusp = 1;
#else
static int have_swsusp = 0;
#endif

extern long sys_sync(void);


/**
 *	pm_set_ops - Set the global power method table. 
 *	@ops:	Pointer to ops structure.
 */

void pm_set_ops(struct pm_ops * ops)
{
	down(&pm_sem);
	pm_ops = ops;
	if (ops->pm_disk_mode && ops->pm_disk_mode < PM_DISK_MAX)
		pm_disk_mode = ops->pm_disk_mode;
	up(&pm_sem);
}


/**
 *	pm_suspend_standby - Enter 'standby' state.
 *	
 *	'standby' is also known as 'Power-On Suspend'. Here, we power down
 *	devices, disable interrupts, and enter the state.
 */

static int pm_suspend_standby(void)
{
	int error = 0;
	unsigned long flags;

	if (!pm_ops || !pm_ops->enter)
		return -EPERM;

	local_irq_save(flags);
	if ((error = device_pm_power_down(PM_SUSPEND_STANDBY)))
		goto Done;
	error = pm_ops->enter(PM_SUSPEND_STANDBY);
	local_irq_restore(flags);
	device_pm_power_up();
 Done:
	return error;
}


/**
 *	pm_suspend_mem - Enter suspend-to-RAM state.
 *
 *	Identical to pm_suspend_standby() - we power down devices, disable 
 *	interrupts, and enter the low-power state.
 */

static int pm_suspend_mem(void)
{
	int error = 0;
	unsigned long flags;

	if (!pm_ops || !pm_ops->enter)
		return -EPERM;

	local_irq_save(flags);
	if ((error = device_pm_power_down(PM_SUSPEND_STANDBY)))
		goto Done;
	error = pm_ops->enter(PM_SUSPEND_STANDBY);
	local_irq_restore(flags);
	device_pm_power_up();
 Done:
	return error;
}


/**
 *	power_down - Shut machine down for hibernate.
 *	@mode:		Suspend-to-disk mode
 *
 *	Use the platform driver, if configured so, and return gracefully if it
 *	fails. 
 *	Otherwise, try to power off and reboot. If they fail, halt the machine,
 *	there ain't no turning back.
 */

static int power_down(u32 mode)
{
	unsigned long flags;
	int error = 0;

	local_irq_save(flags);
	device_pm_power_down();
	switch(mode) {
	case PM_DISK_PLATFORM:
		error = pm_ops->enter(PM_SUSPEND_DISK);
		if (error) {
			device_pm_power_up();
			local_irq_restore(flags);
			return error;
		}
	case PM_DISK_SHUTDOWN:
		machine_power_off();
		break;
	case PM_DISK_REBOOT:
		machine_restart(NULL);
		break;
	}
	machine_halt();
	return 0;
}


static int in_suspend __nosavedata = 0;


/**
 *	free_some_memory -  Try to free as much memory as possible
 *
 *	... but do not OOM-kill anyone
 *
 *	Notice: all userland should be stopped at this point, or 
 *	livelock is possible.
 */

static void free_some_memory(void)
{
	printk("Freeing memory: ");
	while (shrink_all_memory(10000))
		printk(".");
	printk("|\n");
	blk_run_queues();
}


/**
 *	pm_suspend_disk - The granpappy of power management.
 *	
 *	If we're going through the firmware, then get it over with quickly.
 *
 *	If not, then call swsusp to do it's thing, then figure out how
 *	to power down the system.
 */

static int pm_suspend_disk(void)
{
	int error;

	pr_debug("PM: Attempting to suspend to disk.\n");
	if (pm_disk_mode == PM_DISK_FIRMWARE)
		return pm_ops->enter(PM_SUSPEND_DISK);

	if (!have_swsusp)
		return -EPERM;

	pr_debug("PM: snapshotting memory.\n");
	in_suspend = 1;
	if ((error = swsusp_save()))
		goto Done;

	if (in_suspend) {
		pr_debug("PM: writing image.\n");
		error = swsusp_write();
		if (!error)
			error = power_down(pm_disk_mode);
		pr_debug("PM: Power down failed.\n");
	} else
		pr_debug("PM: Image restored successfully.\n");
	swsusp_free();
 Done:
	return error;
}



#define decl_state(_name) \
	{ .name = __stringify(_name), .fn = pm_suspend_##_name }

struct pm_state {
	char * name;
	int (*fn)(void);
} pm_states[] = {
	[PM_SUSPEND_STANDBY]	= decl_state(standby),
	[PM_SUSPEND_MEM]	= decl_state(mem),
	[PM_SUSPEND_DISK]	= decl_state(disk),
	{ NULL },
};


/**
 *	suspend_prepare - Do prep work before entering low-power state.
 *	@state:		State we're entering.
 *
 *	This is common code that is called for each state that we're 
 *	entering. Allocate a console, stop all processes, then make sure
 *	the platform can enter the requested state.
 */

static int suspend_prepare(u32 state)
{
	int error = 0;

	pm_prepare_console();

	sys_sync();
	if (freeze_processes()) {
		error = -EAGAIN;
		goto Thaw;
	}

	if (pm_ops && pm_ops->prepare) {
		if ((error = pm_ops->prepare(state)))
			goto Thaw;
	}

	/* Free memory before shutting down devices. */
	if (state == PM_SUSPEND_DISK)
		free_some_memory();

	if ((error = device_pm_suspend(state)))
		goto Finish;

	return 0;
 Done:
	pm_restore_console();
	return error;
 Finish:
	if (pm_ops && pm_ops->finish)
		pm_ops->finish(state);
 Thaw:
	thaw_processes();
	goto Done;
}


/**
 *	suspend_finish - Do final work before exiting suspend sequence.
 *	@state:		State we're coming out of.
 *
 *	Call platform code to clean up, restart processes, and free the 
 *	console that we've allocated.
 */

static void suspend_finish(u32 state)
{
	device_pm_resume();
	if (pm_ops && pm_ops->finish)
		pm_ops->finish(state);
	thaw_processes();
	pm_restore_console();
}


/**
 *	enter_state - Do common work of entering low-power state.
 *	@state:		pm_state structure for state we're entering.
 *
 *	Make sure we're the only ones trying to enter a sleep state. Fail
 *	if someone has beat us to it, since we don't want anything weird to
 *	happen when we wake up.
 *	Then, do the setup for suspend, enter the state, and cleaup (after
 *	we've woken up).
 */

static int enter_state(u32 state)
{
	int error;
	struct pm_state * s = &pm_states[state];

	if (down_trylock(&pm_sem))
		return -EBUSY;

	/* Suspend is hard to get right on SMP. */
	if (num_online_cpus() != 1) {
		error = -EPERM;
		goto Unlock;
	}

	pr_debug("PM: Preparing system for suspend.\n");
	if ((error = suspend_prepare(state)))
		goto Unlock;

	pr_debug("PM: Entering state.\n");
	error = s->fn();

	pr_debug("PM: Finishing up.\n");
	suspend_finish(state);
 Unlock:
	up(&pm_sem);
	return error;
}


/**
 *	pm_suspend - Externally visible function for suspending system.
 *	@state:		Enumarted value of state to enter.
 *
 *	Determine whether or not value is within range, get state 
 *	structure, and enter (above).
 */

int pm_suspend(u32 state)
{
	if (state > PM_SUSPEND_ON && state < PM_SUSPEND_MAX)
		return enter_state(state);
	return -EINVAL;
}


/**
 *	pm_resume - Resume from a saved image.
 *
 *	Called as a late_initcall (so all devices are discovered and 
 *	initialized), we call swsusp to see if we have a saved image or not.
 *	If so, we quiesce devices, the restore the saved image. We will 
 *	return above (in pm_suspend_disk() ) if everything goes well. 
 *	Otherwise, we fail gracefully and return to the normally 
 *	scheduled program.
 *
 */

static int pm_resume(void)
{
	int error;

	if (!have_swsusp)
		return 0;

	pr_debug("PM: Reading swsusp image.\n");

	if ((error = swsusp_read()))
		goto Done;

	pr_debug("PM: Preparing system for restore.\n");

	if ((error = suspend_prepare(PM_SUSPEND_DISK)))
		goto Free;

	pr_debug("PM: Restoring saved image.\n");
	swsusp_restore();

	pr_debug("PM: Restore failed, recovering.n");
	suspend_finish(PM_SUSPEND_DISK);
 Free:
	swsusp_free();
 Done:
	pr_debug("PM: Resume from disk failed.\n");
	return 0;
}

late_initcall(pm_resume);


decl_subsys(power,NULL,NULL);


#define power_attr(_name) \
static struct subsys_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}


static char * pm_disk_modes[] = {
	[PM_DISK_FIRMWARE]	= "firmware",
	[PM_DISK_PLATFORM]	= "platform",
	[PM_DISK_SHUTDOWN]	= "shutdown",
	[PM_DISK_REBOOT]	= "reboot",
};

/**
 *	disk - Control suspend-to-disk mode
 *
 *	Suspend-to-disk can be handled in several ways. The greatest 
 *	distinction is who writes memory to disk - the firmware or the OS.
 *	If the firmware does it, we assume that it also handles suspending 
 *	the system.
 *	If the OS does it, then we have three options for putting the system
 *	to sleep - using the platform driver (e.g. ACPI or other PM registers),
 *	powering off the system or rebooting the system (for testing). 
 *
 *	The system will support either 'firmware' or 'platform', and that is
 *	known a priori (and encoded in pm_ops). But, the user may choose
 *	'shutdown' or 'reboot' as alternatives.
 *
 *	show() will display what the mode is currently set to. 
 *	store() will accept one of
 *
 *	'firmware'
 *	'platform'
 *	'shutdown'
 *	'reboot'
 *
 *	It will only change to 'firmware' or 'platform' if the system
 *	supports it (as determined from pm_ops->pm_disk_mode).
 */

static ssize_t disk_show(struct subsystem * subsys, char * buf)
{
	return sprintf(buf,"%s\n",pm_disk_modes[pm_disk_mode]);
}


static ssize_t disk_store(struct subsystem * s, const char * buf, size_t n)
{
	int error = 0;
	int i;
	u32 mode = 0;

	down(&pm_sem);
	for (i = PM_DISK_FIRMWARE; i < PM_DISK_MAX; i++) {
		if (!strcmp(buf,pm_disk_modes[i])) {
			mode = i;
			break;
		}
	}
	if (mode) {
		if (mode == PM_DISK_SHUTDOWN || mode == PM_DISK_REBOOT)
			pm_disk_mode = mode;
		else {
			if (pm_ops && pm_ops->enter && 
			    (mode == pm_ops->pm_disk_mode))
				pm_disk_mode = mode;
			else
				error = -EINVAL;
		}
	} else
		error = -EINVAL;

	pr_debug("PM: suspend-to-disk mode set to '%s'\n",
		 pm_disk_modes[mode]);
	up(&pm_sem);
	return error ? error : n;
}

power_attr(disk);

/**
 *	state - control system power state.
 *
 *	show() returns what states are supported, which is hard-coded to
 *	'standby' (Power-On Suspend), 'mem' (Suspend-to-RAM), and
 *	'disk' (Suspend-to-Disk).
 *
 *	store() accepts one of those strings, translates it into the 
 *	proper enumerated value, and initiates a suspend transition.
 */

static ssize_t state_show(struct subsystem * subsys, char * buf)
{
	struct pm_state * state;
	char * s = buf;

	for (state = &pm_states[0]; state->name; state++)
		s += sprintf(s,"%s ",state->name);
	s += sprintf(s,"\n");
	return (s - buf);
}

static ssize_t state_store(struct subsystem * subsys, const char * buf, size_t n)
{
	u32 state;
	struct pm_state * s;
	int error;

	for (state = 0; state < PM_SUSPEND_MAX; state++) {
		s = &pm_states[state];
		if (s->name && !strcmp(buf,s->name))
			break;
	}
	if (s)
		error = enter_state(state);
	else
		error = -EINVAL;
	return error ? error : n;
}

power_attr(state);

static struct attribute * g[] = {
	&state_attr.attr,
	&disk_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};


static int pm_init(void)
{
	int error = subsystem_register(&power_subsys);
	if (!error)
		error = sysfs_create_group(&power_subsys.kset.kobj,&attr_group);
	return error;
}

core_initcall(pm_init);
