/*
 *  linux/kernel/cpufreq.c
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *
 *  $Id: cpufreq.c,v 1.59 2003/01/20 17:31:48 db Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/device.h>
#include <linux/slab.h>

/**
 * The "cpufreq driver" - the arch- or hardware-dependend low
 * level driver of CPUFreq support, and its locking mutex. 
 * cpu_max_freq is in kHz.
 */
static struct cpufreq_driver   	*cpufreq_driver;
static DECLARE_MUTEX            (cpufreq_driver_sem);


/**
 * Two notifier lists: the "policy" list is involved in the 
 * validation process for a new CPU frequency policy; the 
 * "transition" list for kernel code that needs to handle
 * changes to devices when the CPU clock speed changes.
 * The mutex locks both lists. If both cpufreq_driver_sem
 * and cpufreq_notifier_sem need to be hold, get cpufreq_driver_sem
 * first.
 */
static struct notifier_block    *cpufreq_policy_notifier_list;
static struct notifier_block    *cpufreq_transition_notifier_list;
static DECLARE_RWSEM		(cpufreq_notifier_rwsem);


static LIST_HEAD(cpufreq_governor_list);
static DECLARE_MUTEX		(cpufreq_governor_sem);

static struct class_interface cpufreq_interface;

static int cpufreq_cpu_get(unsigned int cpu) {
	if (cpu >= NR_CPUS)
		return 0;

	if (!try_module_get(cpufreq_driver->owner))
		return 0;

	if (!kobject_get(&cpufreq_driver->policy[cpu].kobj)) {
		module_put(cpufreq_driver->owner);
		return 0;
	}

	return 1;
}

static void cpufreq_cpu_put(unsigned int cpu) {
	kobject_put(&cpufreq_driver->policy[cpu].kobj);
	module_put(cpufreq_driver->owner);
}

/*********************************************************************
 *                          SYSFS INTERFACE                          *
 *********************************************************************/

/**
 * cpufreq_parse_governor - parse a governor string
 */
int cpufreq_parse_governor (char *str_governor, unsigned int *policy, struct cpufreq_governor **governor)
{
	if (!strnicmp(str_governor, "performance", CPUFREQ_NAME_LEN)) {
		*policy = CPUFREQ_POLICY_PERFORMANCE;
		return 0;
	} else if (!strnicmp(str_governor, "powersave", CPUFREQ_NAME_LEN)) {
		*policy = CPUFREQ_POLICY_POWERSAVE;
		return 0;
	} else 	{
		struct cpufreq_governor *t;
		down(&cpufreq_governor_sem);
		if (!cpufreq_driver || !cpufreq_driver->target)
			goto out;
		list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
			if (!strnicmp(str_governor,t->name,CPUFREQ_NAME_LEN)) {
				*governor = t;
				*policy = CPUFREQ_POLICY_GOVERNOR;
				up(&cpufreq_governor_sem);
				return 0;
			}
		}
	out:
		up(&cpufreq_governor_sem);
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(cpufreq_parse_governor);


/* forward declarations */
static int cpufreq_add_dev (struct class_device * dev);
static void cpufreq_remove_dev (struct class_device * dev);

/* drivers/base/cpu.c */
extern struct device_class cpu_devclass;

static struct class_interface cpufreq_interface = {
        .add =		&cpufreq_add_dev,
        .remove =	&cpufreq_remove_dev,
};

static inline int to_cpu_nr (struct class_device *dev)
{
	struct sys_device * cpu_sys_dev = container_of(dev->dev, struct sys_device, dev);
	return (cpu_sys_dev->id);
}


/**
 * cpufreq_per_cpu_attr_read() / show_##file_name() - print out cpufreq information
 *
 * Write out information from cpufreq_driver->policy[cpu]; object must be
 * "unsigned int".
 */

#define show_one(file_name, object)		 			\
static ssize_t show_##file_name 					\
(struct cpufreq_policy * policy, char *buf)				\
{									\
	return sprintf (buf, "%u\n", policy->object);			\
}

show_one(cpuinfo_min_freq, cpuinfo.min_freq);
show_one(cpuinfo_max_freq, cpuinfo.max_freq);
show_one(scaling_min_freq, min);
show_one(scaling_max_freq, max);

/**
 * cpufreq_per_cpu_attr_write() / store_##file_name() - sysfs write access
 */
#define store_one(file_name, object)			\
static ssize_t store_##file_name					\
(struct cpufreq_policy * policy, const char *buf, size_t count)		\
{									\
	unsigned int ret = -EINVAL;					\
	struct cpufreq_policy new_policy;				\
									\
	ret = cpufreq_get_policy(&new_policy, policy->cpu);		\
	if (ret)							\
		return -EINVAL;						\
									\
	ret = sscanf (buf, "%u", &new_policy.object);			\
	if (ret != 1)							\
		return -EINVAL;						\
									\
	ret = cpufreq_set_policy(&new_policy);				\
									\
	return ret ? ret : count;					\
}

store_one(scaling_min_freq,min);
store_one(scaling_max_freq,max);

/**
 * show_scaling_governor - show the current policy for the specified CPU
 */
static ssize_t show_scaling_governor (struct cpufreq_policy * policy, char *buf)
{
	switch (policy->policy) {
	case CPUFREQ_POLICY_POWERSAVE:
		return sprintf(buf, "powersave\n");
	case CPUFREQ_POLICY_PERFORMANCE:
		return sprintf(buf, "performance\n");
	case CPUFREQ_POLICY_GOVERNOR:
		return snprintf(buf, CPUFREQ_NAME_LEN, "%s\n", policy->governor->name);
	default:
		return -EINVAL;
	}
}


/**
 * store_scaling_governor - store policy for the specified CPU
 */
static ssize_t store_scaling_governor (struct cpufreq_policy * policy, 
				       const char *buf, size_t count) 
{
	unsigned int ret = -EINVAL;
	char	str_governor[16];
	struct cpufreq_policy new_policy;

	ret = cpufreq_get_policy(&new_policy, policy->cpu);
	if (ret)
		return ret;

	ret = sscanf (buf, "%15s", str_governor);
	if (ret != 1)
		return -EINVAL;

	if (cpufreq_parse_governor(str_governor, &new_policy.policy, &new_policy.governor))
		return -EINVAL;

	ret = cpufreq_set_policy(&new_policy);

	return ret ? ret : count;
}

/**
 * show_scaling_driver - show the cpufreq driver currently loaded
 */
static ssize_t show_scaling_driver (struct cpufreq_policy * policy, char *buf)
{
	return snprintf(buf, CPUFREQ_NAME_LEN, "%s\n", cpufreq_driver->name);
}

/**
 * show_scaling_available_governors - show the available CPUfreq governors
 */
static ssize_t show_scaling_available_governors(struct cpufreq_policy * policy, char *buf)
{
	ssize_t i = 0;
	struct cpufreq_governor *t;

	i += sprintf(buf, "performance powersave");

	if (!cpufreq_driver->target)
		goto out;

	list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
		if (i >= (ssize_t) ((PAGE_SIZE / sizeof(char)) - (CPUFREQ_NAME_LEN + 2)))
			goto out;
		i += snprintf(&buf[i], CPUFREQ_NAME_LEN, " %s", t->name);
	}
 out:
	i += sprintf(&buf[i], "\n");
	return i;
}


#define define_one_ro(_name) \
struct freq_attr _name = { \
	.attr = { .name = __stringify(_name), .mode = 0444 }, \
	.show = show_##_name, \
}

#define define_one_rw(_name) \
struct freq_attr _name = { \
	.attr = { .name = __stringify(_name), .mode = 0644 }, \
	.show = show_##_name, \
	.store = store_##_name, \
}

define_one_ro(cpuinfo_min_freq);
define_one_ro(cpuinfo_max_freq);
define_one_ro(scaling_available_governors);
define_one_ro(scaling_driver);
define_one_rw(scaling_min_freq);
define_one_rw(scaling_max_freq);
define_one_rw(scaling_governor);

static struct attribute * default_attrs[] = {
	&cpuinfo_min_freq.attr,
	&cpuinfo_max_freq.attr,
	&scaling_min_freq.attr,
	&scaling_max_freq.attr,
	&scaling_governor.attr,
	&scaling_driver.attr,
	&scaling_available_governors.attr,
	NULL
};

#define to_policy(k) container_of(k,struct cpufreq_policy,kobj)
#define to_attr(a) container_of(a,struct freq_attr,attr)

static ssize_t show(struct kobject * kobj, struct attribute * attr ,char * buf)
{
	struct cpufreq_policy * policy = to_policy(kobj);
	struct freq_attr * fattr = to_attr(attr);
	ssize_t ret;
	if (!cpufreq_cpu_get(policy->cpu))
		return -EINVAL;
	ret = fattr->show ? fattr->show(policy,buf) : 0;
	cpufreq_cpu_put(policy->cpu);
	return ret;
}

static ssize_t store(struct kobject * kobj, struct attribute * attr, 
		     const char * buf, size_t count)
{
	struct cpufreq_policy * policy = to_policy(kobj);
	struct freq_attr * fattr = to_attr(attr);
	ssize_t ret;
	if (!cpufreq_cpu_get(policy->cpu))
		return -EINVAL;
	ret = fattr->store ? fattr->store(policy,buf,count) : 0;
	cpufreq_cpu_put(policy->cpu);
	return ret;
}

static struct sysfs_ops sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct kobj_type ktype_cpufreq = {
	.sysfs_ops	= &sysfs_ops,
	.default_attrs	= default_attrs,
};


/**
 * cpufreq_add_dev - add a CPU device
 *
 * Adds the cpufreq interface for a CPU device. 
 */
static int cpufreq_add_dev (struct class_device * class_dev)
{
	unsigned int cpu = to_cpu_nr(class_dev);
	int ret = 0;
	struct cpufreq_policy new_policy;
	struct cpufreq_policy *policy;
	struct freq_attr **drv_attr;

	if (!try_module_get(cpufreq_driver->owner))
		return -EINVAL;

	/* call driver. From then on the cpufreq must be able
	 * to accept all calls to ->verify and ->setpolicy for this CPU
	 */
	policy = &cpufreq_driver->policy[cpu];
	policy->cpu = cpu;
	if (cpufreq_driver->init) {
		ret = cpufreq_driver->init(policy);
		if (ret)
			goto out;
	}

	/* set default policy on this CPU */
	down(&cpufreq_driver_sem);
	memcpy(&new_policy, 
	       policy, 
	       sizeof(struct cpufreq_policy));
	class_set_devdata(class_dev, policy);
	up(&cpufreq_driver_sem);

	init_MUTEX(&policy->lock);
	/* prepare interface data */
	policy->kobj.parent = &class_dev->kobj;
	policy->kobj.ktype = &ktype_cpufreq;
//	policy->dev = dev->dev;
	strncpy(policy->kobj.name, "cpufreq", KOBJ_NAME_LEN);

	ret = kobject_register(&policy->kobj);
	if (ret)
		goto out;

	drv_attr = cpufreq_driver->attr;
	while ((drv_attr) && (*drv_attr)) {
		sysfs_create_file(&policy->kobj, &((*drv_attr)->attr));
		drv_attr++;
	}
	/* set up files for this cpu device */

	
	/* set default policy */
	ret = cpufreq_set_policy(&new_policy);
	if (ret)
		kobject_unregister(&policy->kobj);

 out:
	module_put(cpufreq_driver->owner);
	return ret;
}


/**
 * cpufreq_remove_dev - remove a CPU device
 *
 * Removes the cpufreq interface for a CPU device.
 */
static void cpufreq_remove_dev (struct class_device * class_dev)
{
	unsigned int cpu = to_cpu_nr(class_dev);

	if (!kobject_get(&cpufreq_driver->policy[cpu].kobj))
		return;

	down(&cpufreq_driver_sem);
	if ((cpufreq_driver->target) && 
	    (cpufreq_driver->policy[cpu].policy == CPUFREQ_POLICY_GOVERNOR)) {
		cpufreq_driver->policy[cpu].governor->governor(&cpufreq_driver->policy[cpu], CPUFREQ_GOV_STOP);
		module_put(cpufreq_driver->policy[cpu].governor->owner);
	}

	/* we may call driver->exit here without checking for try_module_exit
	 * as it's either the driver which wants to unload or we have a CPU
	 * removal AND driver removal at the same time...
	 */
	if (cpufreq_driver->exit)
		cpufreq_driver->exit(&cpufreq_driver->policy[cpu]);

	kobject_unregister(&cpufreq_driver->policy[cpu].kobj);

	up(&cpufreq_driver_sem);
	kobject_put(&cpufreq_driver->policy[cpu].kobj);
	return;
}


/*********************************************************************
 *                     NOTIFIER LISTS INTERFACE                      *
 *********************************************************************/

/**
 *	cpufreq_register_notifier - register a driver with cpufreq
 *	@nb: notifier function to register
 *      @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER
 *
 *	Add a driver to one of two lists: either a list of drivers that 
 *      are notified about clock rate changes (once before and once after
 *      the transition), or a list of drivers that are notified about
 *      changes in cpufreq policy.
 *
 *	This function may sleep, and has the same return conditions as
 *	notifier_chain_register.
 */
int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	down_write(&cpufreq_notifier_rwsem);
	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		ret = notifier_chain_register(&cpufreq_transition_notifier_list, nb);
		break;
	case CPUFREQ_POLICY_NOTIFIER:
		ret = notifier_chain_register(&cpufreq_policy_notifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}
	up_write(&cpufreq_notifier_rwsem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_register_notifier);


/**
 *	cpufreq_unregister_notifier - unregister a driver with cpufreq
 *	@nb: notifier block to be unregistered
 *      @list: CPUFREQ_TRANSITION_NOTIFIER or CPUFREQ_POLICY_NOTIFIER
 *
 *	Remove a driver from the CPU frequency notifier list.
 *
 *	This function may sleep, and has the same return conditions as
 *	notifier_chain_unregister.
 */
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list)
{
	int ret;

	down_write(&cpufreq_notifier_rwsem);
	switch (list) {
	case CPUFREQ_TRANSITION_NOTIFIER:
		ret = notifier_chain_unregister(&cpufreq_transition_notifier_list, nb);
		break;
	case CPUFREQ_POLICY_NOTIFIER:
		ret = notifier_chain_unregister(&cpufreq_policy_notifier_list, nb);
		break;
	default:
		ret = -EINVAL;
	}
	up_write(&cpufreq_notifier_rwsem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_unregister_notifier);


/*********************************************************************
 *                              GOVERNORS                            *
 *********************************************************************/

inline int cpufreq_driver_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation)
{
	unsigned int ret;
	unsigned int cpu = policy->cpu;

	if (!cpufreq_cpu_get(cpu))
		return -EINVAL;

	down(&cpufreq_driver->policy[cpu].lock);

	ret = cpufreq_driver->target(policy, target_freq, relation);

	up(&cpufreq_driver->policy[cpu].lock);

	cpufreq_cpu_put(cpu);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_target);


int cpufreq_governor(unsigned int cpu, unsigned int event)
{
	int ret = 0;
	struct cpufreq_policy *policy = &cpufreq_driver->policy[cpu];

	if (!cpufreq_cpu_get(cpu))
		return -EINVAL;

	switch (policy->policy) {
	case CPUFREQ_POLICY_POWERSAVE: 
		if ((event == CPUFREQ_GOV_LIMITS) || (event == CPUFREQ_GOV_START)) {
			down(&cpufreq_driver->policy[cpu].lock);
			ret = cpufreq_driver->target(policy, policy->min, CPUFREQ_RELATION_L);
			up(&cpufreq_driver->policy[cpu].lock);
		}
		break;
	case CPUFREQ_POLICY_PERFORMANCE:
		if ((event == CPUFREQ_GOV_LIMITS) || (event == CPUFREQ_GOV_START)) {
			down(&cpufreq_driver->policy[cpu].lock);
			ret = cpufreq_driver->target(policy, policy->max, CPUFREQ_RELATION_H);
			up(&cpufreq_driver->policy[cpu].lock);
		}
		break;
	case CPUFREQ_POLICY_GOVERNOR:
		ret = -EINVAL;
		if (!try_module_get(cpufreq_driver->policy[cpu].governor->owner))
			break;
		ret = cpufreq_driver->policy[cpu].governor->governor(policy, event);
		/* we keep one module reference alive for each CPU governed by this CPU */
		if ((event != CPUFREQ_GOV_START) || ret)
			module_put(cpufreq_driver->policy[cpu].governor->owner);
		if ((event == CPUFREQ_GOV_STOP) && !ret)
			module_put(cpufreq_driver->policy[cpu].governor->owner);
		break;
	default:
		ret = -EINVAL;
	}

	cpufreq_cpu_put(cpu);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_governor);


int cpufreq_register_governor(struct cpufreq_governor *governor)
{
	struct cpufreq_governor *t;

	if (!governor)
		return -EINVAL;

	if (!strnicmp(governor->name,"powersave",CPUFREQ_NAME_LEN))
		return -EBUSY;
	if (!strnicmp(governor->name,"performance",CPUFREQ_NAME_LEN))
		return -EBUSY;

	down(&cpufreq_governor_sem);
	
	list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
		if (!strnicmp(governor->name,t->name,CPUFREQ_NAME_LEN)) {
			up(&cpufreq_governor_sem);
			return -EBUSY;
		}
	}
	list_add(&governor->governor_list, &cpufreq_governor_list);

 	up(&cpufreq_governor_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_register_governor);


void cpufreq_unregister_governor(struct cpufreq_governor *governor)
{
	unsigned int i;
	
	if (!governor)
		return;

	down(&cpufreq_governor_sem);

	/* 
	 * Unless the user uses rmmod -f, we can be safe. But we never
	 * know, so check whether if it's currently used. If so,
	 * stop it and replace it with the default governor.
	 */
	for (i=0; i<NR_CPUS; i++)
	{
		if (!cpufreq_cpu_get(i))
			continue;
		if ((cpufreq_driver->policy[i].policy == CPUFREQ_POLICY_GOVERNOR) && 
		    (cpufreq_driver->policy[i].governor == governor)) {
			cpufreq_governor(i, CPUFREQ_GOV_STOP);
			cpufreq_driver->policy[i].policy = CPUFREQ_POLICY_PERFORMANCE;
			cpufreq_governor(i, CPUFREQ_GOV_START);
			cpufreq_governor(i, CPUFREQ_GOV_LIMITS);
		}
		cpufreq_cpu_put(i);
	}

	/* now we can safely remove it from the list */
	list_del(&governor->governor_list);
	up(&cpufreq_governor_sem);
	return;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_governor);



/*********************************************************************
 *                          POLICY INTERFACE                         *
 *********************************************************************/

/**
 * cpufreq_get_policy - get the current cpufreq_policy
 * @policy: struct cpufreq_policy into which the current cpufreq_policy is written
 *
 * Reads the current cpufreq policy.
 */
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu)
{
	if (!policy || !cpufreq_cpu_get(cpu))
		return -EINVAL;

	down(&cpufreq_driver_sem);
	memcpy(policy, 
	       &cpufreq_driver->policy[cpu], 
	       sizeof(struct cpufreq_policy));
	up(&cpufreq_driver_sem);

	cpufreq_cpu_put(cpu);

	return 0;
}
EXPORT_SYMBOL(cpufreq_get_policy);


/**
 *	cpufreq_set_policy - set a new CPUFreq policy
 *	@policy: policy to be set.
 *
 *	Sets a new CPU frequency and voltage scaling policy.
 */
int cpufreq_set_policy(struct cpufreq_policy *policy)
{
	int ret = 0;

	if (!policy || !cpufreq_cpu_get(policy->cpu))
		return -EINVAL;

	down(&cpufreq_driver_sem);
	memcpy(&policy->cpuinfo, 
	       &cpufreq_driver->policy[policy->cpu].cpuinfo, 
	       sizeof(struct cpufreq_cpuinfo));
	up(&cpufreq_driver_sem);

	/* verify the cpu speed can be set within this limit */
	ret = cpufreq_driver->verify(policy);
	if (ret)
		goto error_out;

	down_read(&cpufreq_notifier_rwsem);

	/* adjust if necessary - all reasons */
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_ADJUST,
			    policy);

	/* adjust if necessary - hardware incompatibility*/
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_INCOMPATIBLE,
			    policy);

	/* verify the cpu speed can be set within this limit,
	   which might be different to the first one */
	ret = cpufreq_driver->verify(policy);
	if (ret) {
		up_read(&cpufreq_notifier_rwsem);
		goto error_out;
	}

	/* notification of the new policy */
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_NOTIFY,
			    policy);

	up_read(&cpufreq_notifier_rwsem);

	/* from here on we limit it to one limit and/or governor change running at the moment */
	down(&cpufreq_driver_sem);
	cpufreq_driver->policy[policy->cpu].min    = policy->min;
	cpufreq_driver->policy[policy->cpu].max    = policy->max;

	if (cpufreq_driver->setpolicy) {
		cpufreq_driver->policy[policy->cpu].policy = policy->policy;
		ret = cpufreq_driver->setpolicy(policy);
	} else {
		if ((policy->policy != cpufreq_driver->policy[policy->cpu].policy) || 
		    ((policy->policy == CPUFREQ_POLICY_GOVERNOR) && (policy->governor != cpufreq_driver->policy[policy->cpu].governor))) {
			unsigned int old_pol = cpufreq_driver->policy[policy->cpu].policy;
			struct cpufreq_governor *old_gov = cpufreq_driver->policy[policy->cpu].governor;
			/* end old governor */
			cpufreq_governor(policy->cpu, CPUFREQ_GOV_STOP);
			cpufreq_driver->policy[policy->cpu].policy = policy->policy;
			cpufreq_driver->policy[policy->cpu].governor = policy->governor;
			/* start new governor */
			if (cpufreq_governor(policy->cpu, CPUFREQ_GOV_START)) {
				cpufreq_driver->policy[policy->cpu].policy = old_pol;
				cpufreq_driver->policy[policy->cpu].governor = old_gov;
				cpufreq_governor(policy->cpu, CPUFREQ_GOV_START);
			}
			/* might be a policy change, too */
			cpufreq_governor(policy->cpu, CPUFREQ_GOV_LIMITS);
		} else {
			cpufreq_governor(policy->cpu, CPUFREQ_GOV_LIMITS);
		}
	}
	up(&cpufreq_driver_sem);

 error_out:	
	cpufreq_cpu_put(policy->cpu);

	return ret;
}
EXPORT_SYMBOL(cpufreq_set_policy);



/*********************************************************************
 *            EXTERNALLY AFFECTING FREQUENCY CHANGES                 *
 *********************************************************************/

/**
 * adjust_jiffies - adjust the system "loops_per_jiffy"
 *
 * This function alters the system "loops_per_jiffy" for the clock
 * speed change. Note that loops_per_jiffy cannot be updated on SMP
 * systems as each CPU might be scaled differently. So, use the arch 
 * per-CPU loops_per_jiffy value wherever possible.
 */
#ifndef CONFIG_SMP
static unsigned long l_p_j_ref = 0;
static unsigned int  l_p_j_ref_freq = 0;

static inline void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
	if (!l_p_j_ref_freq) {
		l_p_j_ref = loops_per_jiffy;
		l_p_j_ref_freq = ci->old;
	}
	if ((val == CPUFREQ_PRECHANGE  && ci->old < ci->new) ||
	    (val == CPUFREQ_POSTCHANGE && ci->old > ci->new))
		loops_per_jiffy = cpufreq_scale(l_p_j_ref, l_p_j_ref_freq, ci->new);
}
#else
#define adjust_jiffies(x...) do {} while (0)
#endif


/**
 * cpufreq_notify_transition - call notifier chain and adjust_jiffies on frequency transition
 *
 * This function calls the transition notifiers and the "adjust_jiffies" function. It is called
 * twice on all CPU frequency changes that have external effects. 
 */
void cpufreq_notify_transition(struct cpufreq_freqs *freqs, unsigned int state)
{
	down_read(&cpufreq_notifier_rwsem);
	switch (state) {
	case CPUFREQ_PRECHANGE:
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_PRECHANGE, freqs);
		adjust_jiffies(CPUFREQ_PRECHANGE, freqs);		
		break;
	case CPUFREQ_POSTCHANGE:
		adjust_jiffies(CPUFREQ_POSTCHANGE, freqs);
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_POSTCHANGE, freqs);
		cpufreq_driver->policy[freqs->cpu].cur = freqs->new;
		break;
	}
	up_read(&cpufreq_notifier_rwsem);
}
EXPORT_SYMBOL_GPL(cpufreq_notify_transition);



/*********************************************************************
 *               REGISTER / UNREGISTER CPUFREQ DRIVER                *
 *********************************************************************/

/**
 * cpufreq_register_driver - register a CPU Frequency driver
 * @driver_data: A struct cpufreq_driver containing the values#
 * submitted by the CPU Frequency driver.
 *
 *   Registers a CPU Frequency driver to this core code. This code 
 * returns zero on success, -EBUSY when another driver got here first
 * (and isn't unregistered in the meantime). 
 *
 */
int cpufreq_register_driver(struct cpufreq_driver *driver_data)
{
	if (!driver_data || !driver_data->verify || !driver_data->init ||
	    ((!driver_data->setpolicy) && (!driver_data->target)))
		return -EINVAL;

	down(&cpufreq_driver_sem);
	if (cpufreq_driver) {
		up(&cpufreq_driver_sem);		
		return -EBUSY;
	}
	cpufreq_driver = driver_data;
	up(&cpufreq_driver_sem);

	cpufreq_driver->policy = kmalloc(NR_CPUS * sizeof(struct cpufreq_policy), GFP_KERNEL);
	if (!cpufreq_driver->policy) {
		cpufreq_driver = NULL;
		return -ENOMEM;
	}

	memset(cpufreq_driver->policy, 0, NR_CPUS * sizeof(struct cpufreq_policy));

	return class_interface_register(&cpufreq_interface);
}
EXPORT_SYMBOL_GPL(cpufreq_register_driver);


/**
 * cpufreq_unregister_driver - unregister the current CPUFreq driver
 *
 *    Unregister the current CPUFreq driver. Only call this if you have 
 * the right to do so, i.e. if you have succeeded in initialising before!
 * Returns zero if successful, and -EINVAL if the cpufreq_driver is
 * currently not initialised.
 */
int cpufreq_unregister_driver(struct cpufreq_driver *driver)
{
	if (!cpufreq_driver || (driver != cpufreq_driver))
		return -EINVAL;

	class_interface_unregister(&cpufreq_interface);

	down(&cpufreq_driver_sem);
	kfree(cpufreq_driver->policy);
	cpufreq_driver = NULL;
	up(&cpufreq_driver_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_driver);


#ifdef CONFIG_PM
/**
 *	cpufreq_restore - restore the CPU clock frequency after resume
 *
 *	Restore the CPU clock frequency so that our idea of the current
 *	frequency reflects the actual hardware.
 */
int cpufreq_restore(void)
{
	struct cpufreq_policy policy;
	unsigned int i;
	unsigned int ret = 0;

	if (in_interrupt())
		panic("cpufreq_restore() called from interrupt context!");

	if (!try_module_get(cpufreq_driver->owner))
		goto error_out;

	for (i=0;i<NR_CPUS;i++) {
		if (!cpu_online(i) || !cpufreq_cpu_get(i))
			continue;

		down(&cpufreq_driver_sem);
		memcpy(&policy, &cpufreq_driver->policy[i], sizeof(struct cpufreq_policy));
		up(&cpufreq_driver_sem);
		ret += cpufreq_set_policy(&policy);

		cpufreq_cpu_put(i);
	}

	module_put(cpufreq_driver->owner);
 error_out:
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_restore);
#else
#define cpufreq_restore() do {} while (0)
#endif /* CONFIG_PM */
