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


int (*pm_power_down)(u32 state) = NULL;


static DECLARE_MUTEX(pm_sem);


static int pm_suspend_standby(void)
{
	return 0;
}

static int pm_suspend_mem(void)
{
	return 0;
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


static int suspend_prepare(void)
{
	int error = 0;

	pm_prepare_console();

	if (freeze_processes()) {
		thaw_processes();
		error = -EAGAIN;
		goto Done;
	}

 Done:
	pm_restore_console();
	return error;
}

static void suspend_finish(void)
{
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

static int enter_state(struct pm_state * state)
{
	int error;

	if (down_trylock(&pm_sem))
		return -EBUSY;

	if (!pm_power_down) {
		error = -EPERM;
		goto Unlock;
	}

	if ((error = suspend_prepare()))
		return error;

	error = state->fn();
	suspend_finish();
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
		return enter_state(&pm_states[state]);
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

static ssize_t state_store(struct subsystem * s, const char * buf, size_t n)
{
	struct pm_state * state;
	int error;
	char * end = strchr(buf,'\n');
	
	if (end)
		*end = '\0';

	for (state = &pm_states[0]; state; state++) {
		if (state->name && !strcmp(buf,state->name))
			break;
	}
	if (state)
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
