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
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pm.h>


static DECLARE_MUTEX(pm_sem);

static struct pm_ops * pm_ops = NULL;

/**
 *	pm_set_ops - Set the global power method table. 
 *	@ops:	Pointer to ops structure.
 */

void pm_set_ops(struct pm_ops * ops)
{
	down(&pm_sem);
	pm_ops = ops;
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

	if ((error = device_pm_power_down(PM_SUSPEND_STANDBY)))
		goto Done;
	local_irq_save(flags);
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

	if ((error = device_pm_power_down(PM_SUSPEND_STANDBY)))
		goto Done;
	local_irq_save(flags);
	error = pm_ops->enter(PM_SUSPEND_STANDBY);
	local_irq_restore(flags);
	device_pm_power_up();
 Done:
	return error;
}

static int pm_suspend_disk(void)
{
	return 0;
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

	if (freeze_processes()) {
		error = -EAGAIN;
		goto Thaw;
	}

	if (pm_ops && pm_ops->prepare) {
		if ((error = pm_ops->prepare(state)))
			goto Thaw;
	}
 Done:
	pm_restore_console();
	return error;
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

	if ((error = suspend_prepare(state)))
		goto Unlock;

	if ((error = device_pm_suspend(state)))
		goto Finish;

	error = s->fn();

	device_pm_resume();
 Finish:
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
	char * end = strchr(buf,'\n');
	
	if (end)
		*end = '\0';

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
