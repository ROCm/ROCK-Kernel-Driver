/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 * This file is release under the GPLv2
 *
 */

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pm.h>



static int standby(void)
{
	return 0;
}

static int suspend(void)
{
	return 0;
}

static int hibernate(void)
{
	return 0;
}

#define decl_state(_name) \
	{ .name = __stringify(_name), .fn = _name }

struct pm_state {
	char * name;
	int (*fn)(void);
} pm_states[] = {
	decl_state(standby),
	decl_state(suspend),
	decl_state(hibernate),
	{ NULL },
};


static int enter_state(struct pm_state * state)
{
	return state->fn();
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
 *	'standby' (Power-On Suspend), 'suspend' (Suspend-to-RAM), and
 *	'hibernate' (Suspend-to-Disk).
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
		if (!strcmp(buf,state->name))
			break;
	}
	if (!state)
		return -EINVAL;
	error = enter_state(state);
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
