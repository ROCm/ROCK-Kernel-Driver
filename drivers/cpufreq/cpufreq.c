/*
 *  linux/drivers/cpufreq/cpufreq.c
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
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
#include <linux/cpu.h>
#include <linux/completion.h>

/**
 * The "cpufreq driver" - the arch- or hardware-dependend low
 * level driver of CPUFreq support, and its spinlock. This lock
 * also protects the cpufreq_cpu_data array.
 */
static struct cpufreq_driver   	*cpufreq_driver;
static struct cpufreq_policy	*cpufreq_cpu_data[NR_CPUS];
static spinlock_t		cpufreq_driver_lock = SPIN_LOCK_UNLOCKED;

/* internal prototypes */
static int __cpufreq_governor(struct cpufreq_policy *policy, unsigned int event);
static void handle_update(void *data);
static inline void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci);

/**
 * Two notifier lists: the "policy" list is involved in the 
 * validation process for a new CPU frequency policy; the 
 * "transition" list for kernel code that needs to handle
 * changes to devices when the CPU clock speed changes.
 * The mutex locks both lists.
 */
static struct notifier_block    *cpufreq_policy_notifier_list;
static struct notifier_block    *cpufreq_transition_notifier_list;
static DECLARE_RWSEM		(cpufreq_notifier_rwsem);


static LIST_HEAD(cpufreq_governor_list);
static DECLARE_MUTEX		(cpufreq_governor_sem);

static struct cpufreq_policy * cpufreq_cpu_get(unsigned int cpu)
{
	struct cpufreq_policy *data;
	unsigned long flags;

	if (cpu >= NR_CPUS)
		goto err_out;

	/* get the cpufreq driver */
	spin_lock_irqsave(&cpufreq_driver_lock, flags);

	if (!cpufreq_driver)
		goto err_out_unlock;

	if (!try_module_get(cpufreq_driver->owner))
		goto err_out_unlock;


	/* get the CPU */
	data = cpufreq_cpu_data[cpu];

	if (!data)
		goto err_out_put_module;

	if (!kobject_get(&data->kobj))
		goto err_out_put_module;


	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);

	return data;

 err_out_put_module:
	module_put(cpufreq_driver->owner);
 err_out_unlock:
	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);
 err_out:
	return NULL;
}

static void cpufreq_cpu_put(struct cpufreq_policy *data)
{
	kobject_put(&data->kobj);
	module_put(cpufreq_driver->owner);
}

/*********************************************************************
 *                          SYSFS INTERFACE                          *
 *********************************************************************/

/**
 * cpufreq_parse_governor - parse a governor string
 */
int cpufreq_parse_governor (char *str_governor, unsigned int *policy,
				struct cpufreq_governor **governor)
{
	if (!cpufreq_driver)
		return -EINVAL;
	if (cpufreq_driver->setpolicy) {
		if (!strnicmp(str_governor, "performance", CPUFREQ_NAME_LEN)) {
			*policy = CPUFREQ_POLICY_PERFORMANCE;
			return 0;
		} else if (!strnicmp(str_governor, "powersave", CPUFREQ_NAME_LEN)) {
			*policy = CPUFREQ_POLICY_POWERSAVE;
			return 0;
		}
		return -EINVAL;
	} else {
		struct cpufreq_governor *t;
		down(&cpufreq_governor_sem);
		if (!cpufreq_driver || !cpufreq_driver->target)
			goto out;
		list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
			if (!strnicmp(str_governor,t->name,CPUFREQ_NAME_LEN)) {
				*governor = t;
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


/* drivers/base/cpu.c */
extern struct sysdev_class cpu_sysdev_class;


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
show_one(scaling_cur_freq, cur);

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
 * show_cpuinfo_cur_freq - current CPU frequency as detected by hardware
 */
static ssize_t show_cpuinfo_cur_freq (struct cpufreq_policy * policy, char *buf)
{
	unsigned int cur_freq = cpufreq_get(policy->cpu);
	if (!cur_freq)
		return sprintf(buf, "<unknown>");
	return sprintf(buf, "%u\n", cur_freq);
}


/**
 * show_scaling_governor - show the current policy for the specified CPU
 */
static ssize_t show_scaling_governor (struct cpufreq_policy * policy, char *buf)
{
	if(policy->policy == CPUFREQ_POLICY_POWERSAVE)
		return sprintf(buf, "powersave\n");
	else if (policy->policy == CPUFREQ_POLICY_PERFORMANCE)
		return sprintf(buf, "performance\n");
	else if (policy->governor)
		return scnprintf(buf, CPUFREQ_NAME_LEN, "%s\n", policy->governor->name);
	return -EINVAL;
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
	return scnprintf(buf, CPUFREQ_NAME_LEN, "%s\n", cpufreq_driver->name);
}

/**
 * show_scaling_available_governors - show the available CPUfreq governors
 */
static ssize_t show_scaling_available_governors (struct cpufreq_policy * policy,
				char *buf)
{
	ssize_t i = 0;
	struct cpufreq_governor *t;

	if (!cpufreq_driver->target) {
		i += sprintf(buf, "performance powersave");
		goto out;
	}

	list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
		if (i >= (ssize_t) ((PAGE_SIZE / sizeof(char)) - (CPUFREQ_NAME_LEN + 2)))
			goto out;
		i += scnprintf(&buf[i], CPUFREQ_NAME_LEN, "%s ", t->name);
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

#define define_one_ro0400(_name) \
struct freq_attr _name = { \
	.attr = { .name = __stringify(_name), .mode = 0400 }, \
	.show = show_##_name, \
}

#define define_one_rw(_name) \
struct freq_attr _name = { \
	.attr = { .name = __stringify(_name), .mode = 0644 }, \
	.show = show_##_name, \
	.store = store_##_name, \
}

define_one_ro0400(cpuinfo_cur_freq);
define_one_ro(cpuinfo_min_freq);
define_one_ro(cpuinfo_max_freq);
define_one_ro(scaling_available_governors);
define_one_ro(scaling_driver);
define_one_ro(scaling_cur_freq);
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
	policy = cpufreq_cpu_get(policy->cpu);
	if (!policy)
		return -EINVAL;
	ret = fattr->show ? fattr->show(policy,buf) : 0;
	cpufreq_cpu_put(policy);
	return ret;
}

static ssize_t store(struct kobject * kobj, struct attribute * attr, 
		     const char * buf, size_t count)
{
	struct cpufreq_policy * policy = to_policy(kobj);
	struct freq_attr * fattr = to_attr(attr);
	ssize_t ret;
	policy = cpufreq_cpu_get(policy->cpu);
	if (!policy)
		return -EINVAL;
	ret = fattr->store ? fattr->store(policy,buf,count) : 0;
	cpufreq_cpu_put(policy);
	return ret;
}

static void cpufreq_sysfs_release(struct kobject * kobj)
{
	struct cpufreq_policy * policy = to_policy(kobj);
	complete(&policy->kobj_unregister);
}

static struct sysfs_ops sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct kobj_type ktype_cpufreq = {
	.sysfs_ops	= &sysfs_ops,
	.default_attrs	= default_attrs,
	.release	= cpufreq_sysfs_release,
};


/**
 * cpufreq_add_dev - add a CPU device
 *
 * Adds the cpufreq interface for a CPU device. 
 */
static int cpufreq_add_dev (struct sys_device * sys_dev)
{
	unsigned int cpu = sys_dev->id;
	int ret = 0;
	struct cpufreq_policy new_policy;
	struct cpufreq_policy *policy;
	struct freq_attr **drv_attr;
	unsigned long flags;

	if (!try_module_get(cpufreq_driver->owner))
		return -EINVAL;

	policy = kmalloc(sizeof(struct cpufreq_policy), GFP_KERNEL);
	if (!policy) {
		ret = -ENOMEM;
		goto nomem_out;
	}
	memset(policy, 0, sizeof(struct cpufreq_policy));

	policy->cpu = cpu;
	init_MUTEX_LOCKED(&policy->lock);
	init_completion(&policy->kobj_unregister);
	INIT_WORK(&policy->update, handle_update, (void *)(long)cpu);

	/* call driver. From then on the cpufreq must be able
	 * to accept all calls to ->verify and ->setpolicy for this CPU
	 */
	ret = cpufreq_driver->init(policy);
	if (ret)
		goto err_out;

	memcpy(&new_policy, policy, sizeof(struct cpufreq_policy));

	/* prepare interface data */
	policy->kobj.parent = &sys_dev->kobj;
	policy->kobj.ktype = &ktype_cpufreq;
	strlcpy(policy->kobj.name, "cpufreq", KOBJ_NAME_LEN);

	ret = kobject_register(&policy->kobj);
	if (ret)
		goto err_out;

	/* set up files for this cpu device */
	drv_attr = cpufreq_driver->attr;
	while ((drv_attr) && (*drv_attr)) {
		sysfs_create_file(&policy->kobj, &((*drv_attr)->attr));
		drv_attr++;
	}
	if (cpufreq_driver->get)
		sysfs_create_file(&policy->kobj, &cpuinfo_cur_freq.attr);
	if (cpufreq_driver->target)
		sysfs_create_file(&policy->kobj, &scaling_cur_freq.attr);

	spin_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_cpu_data[cpu] = policy;
	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);
	policy->governor = NULL; /* to assure that the starting sequence is
				  * run in cpufreq_set_policy */
	up(&policy->lock);
	
	/* set default policy */
	
	ret = cpufreq_set_policy(&new_policy);
	if (ret)
		goto err_out_unregister;

	module_put(cpufreq_driver->owner);
	return 0;


err_out_unregister:
	spin_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_cpu_data[cpu] = NULL;
	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);

	kobject_unregister(&policy->kobj);
	wait_for_completion(&policy->kobj_unregister);

err_out:
	kfree(policy);

nomem_out:
	module_put(cpufreq_driver->owner);
	return ret;
}


/**
 * cpufreq_remove_dev - remove a CPU device
 *
 * Removes the cpufreq interface for a CPU device.
 */
static int cpufreq_remove_dev (struct sys_device * sys_dev)
{
	unsigned int cpu = sys_dev->id;
	unsigned long flags;
	struct cpufreq_policy *data;

	spin_lock_irqsave(&cpufreq_driver_lock, flags);
	data = cpufreq_cpu_data[cpu];

	if (!data) {
		spin_unlock_irqrestore(&cpufreq_driver_lock, flags);
		return -EINVAL;
	}
	cpufreq_cpu_data[cpu] = NULL;
	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);

	if (!kobject_get(&data->kobj))
		return -EFAULT;

	if (cpufreq_driver->target)
		__cpufreq_governor(data, CPUFREQ_GOV_STOP);

	kobject_unregister(&data->kobj);

	kobject_put(&data->kobj);

	/* we need to make sure that the underlying kobj is actually
	 * not referenced anymore by anybody before we proceed with 
	 * unloading.
	 */
	wait_for_completion(&data->kobj_unregister);

	if (cpufreq_driver->exit)
		cpufreq_driver->exit(data);

	kfree(data);

	return 0;
}


static void handle_update(void *data)
{
	unsigned int cpu = (unsigned int)(long)data;
	cpufreq_update_policy(cpu);
}

/**
 *	cpufreq_out_of_sync - If actual and saved CPU frequency differs, we're in deep trouble.
 *	@cpu: cpu number
 *	@old_freq: CPU frequency the kernel thinks the CPU runs at
 *	@new_freq: CPU frequency the CPU actually runs at
 *
 *	We adjust to current frequency first, and need to clean up later. So either call
 *	to cpufreq_update_policy() or schedule handle_update()).
 */
static void cpufreq_out_of_sync(unsigned int cpu, unsigned int old_freq, unsigned int new_freq)
{
	struct cpufreq_freqs freqs;

	if (cpufreq_driver->flags & CPUFREQ_PANIC_OUTOFSYNC)
		panic("CPU Frequency is out of sync.");

	printk(KERN_WARNING "Warning: CPU frequency out of sync: cpufreq and timing "
	       "core thinks of %u, is %u kHz.\n", old_freq, new_freq);

	freqs.cpu = cpu;
	freqs.old = old_freq;
	freqs.new = new_freq;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
}


/** 
 * cpufreq_get - get the current CPU frequency (in kHz)
 * @cpu: CPU number
 *
 * Get the CPU current (static) CPU frequency
 */
unsigned int cpufreq_get(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);
	unsigned int ret = 0;

	if (!policy)
		return 0;

	if (!cpufreq_driver->get)
		goto out;

	down(&policy->lock);

	ret = cpufreq_driver->get(cpu);

	if (ret && policy->cur && !(cpufreq_driver->flags & CPUFREQ_CONST_LOOPS)) 
	{
		/* verify no discrepancy between actual and saved value exists */
		if (unlikely(ret != policy->cur)) {
			cpufreq_out_of_sync(cpu, policy->cur, ret);
			schedule_work(&policy->update);
		}
	}

	up(&policy->lock);

 out:
	cpufreq_cpu_put(policy);

	return (ret);
}
EXPORT_SYMBOL(cpufreq_get);


/**
 *	cpufreq_resume -  restore proper CPU frequency handling after resume
 *
 *	1.) resume CPUfreq hardware support (cpufreq_driver->resume())
 *	2.) if ->target and !CPUFREQ_CONST_LOOPS: verify we're in sync
 *	3.) schedule call cpufreq_update_policy() ASAP as interrupts are restored.
 */
static int cpufreq_resume(struct sys_device * sysdev)
{
	int cpu = sysdev->id;
	unsigned int ret = 0;
	struct cpufreq_policy *cpu_policy;

	if (!cpu_online(cpu))
		return 0;

	/* we may be lax here as interrupts are off. Nonetheless
	 * we need to grab the correct cpu policy, as to check
	 * whether we really run on this CPU.
	 */

	cpu_policy = cpufreq_cpu_get(cpu);
	if (!cpu_policy)
		return -EINVAL;

	if (!(cpufreq_driver->flags & CPUFREQ_CONST_LOOPS)) {
		unsigned int cur_freq = 0;

		if (cpufreq_driver->get)
			cur_freq = cpufreq_driver->get(cpu_policy->cpu);

		if (!cur_freq || !cpu_policy->cur) {
			printk(KERN_ERR "cpufreq: resume failed to assert current frequency is what timing core thinks it is.\n");
			goto out;
		}

		if (unlikely(cur_freq != cpu_policy->cur)) {
			struct cpufreq_freqs freqs;

			if (cpufreq_driver->flags & CPUFREQ_PANIC_RESUME_OUTOFSYNC)
				panic("CPU Frequency is out of sync.");

			printk(KERN_WARNING "Warning: CPU frequency out of sync: cpufreq and timing"
			       "core thinks of %u, is %u kHz.\n", cpu_policy->cur, cur_freq);

			freqs.cpu = cpu;
			freqs.old = cpu_policy->cur;
			freqs.new = cur_freq;

			notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_RESUMECHANGE, &freqs);
			adjust_jiffies(CPUFREQ_RESUMECHANGE, &freqs);
		}
	}

out:
	schedule_work(&cpu_policy->update);
	cpufreq_cpu_put(cpu_policy);
	return ret;
}

static struct sysdev_driver cpufreq_sysdev_driver = {
	.add		= cpufreq_add_dev,
	.remove		= cpufreq_remove_dev,
	.resume		= cpufreq_resume,
};


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


int __cpufreq_driver_target(struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	int retval = -EINVAL;
	lock_cpu_hotplug();
	if (cpu_online(policy->cpu))
		retval = cpufreq_driver->target(policy, target_freq, relation);
	unlock_cpu_hotplug();
	return retval;
}
EXPORT_SYMBOL_GPL(__cpufreq_driver_target);


int cpufreq_driver_target(struct cpufreq_policy *policy,
			  unsigned int target_freq,
			  unsigned int relation)
{
	unsigned int ret;

	policy = cpufreq_cpu_get(policy->cpu);
	if (!policy)
		return -EINVAL;

	down(&policy->lock);

	ret = __cpufreq_driver_target(policy, target_freq, relation);

	up(&policy->lock);

	cpufreq_cpu_put(policy);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_target);


static int __cpufreq_governor(struct cpufreq_policy *policy, unsigned int event)
{
	int ret = -EINVAL;

	if (!try_module_get(policy->governor->owner))
		return -EINVAL;

	ret = policy->governor->governor(policy, event);

	/* we keep one module reference alive for each CPU governed by this CPU */
	if ((event != CPUFREQ_GOV_START) || ret)
		module_put(policy->governor->owner);
	if ((event == CPUFREQ_GOV_STOP) && !ret)
		module_put(policy->governor->owner);

	return ret;
}


int cpufreq_governor(unsigned int cpu, unsigned int event)
{
	int ret = 0;
	struct cpufreq_policy *policy = cpufreq_cpu_get(cpu);

	if (!policy)
		return -EINVAL;

	down(&policy->lock);
	ret = __cpufreq_governor(policy, event);
	up(&policy->lock);

	cpufreq_cpu_put(policy);

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_governor);


int cpufreq_register_governor(struct cpufreq_governor *governor)
{
	struct cpufreq_governor *t;

	if (!governor)
		return -EINVAL;

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
	if (!governor)
		return;

	down(&cpufreq_governor_sem);
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
	struct cpufreq_policy *cpu_policy;
	if (!policy)
		return -EINVAL;

	cpu_policy = cpufreq_cpu_get(cpu);
	if (!cpu_policy)
		return -EINVAL;

	down(&cpu_policy->lock);
	memcpy(policy, cpu_policy, sizeof(struct cpufreq_policy));
	up(&cpu_policy->lock);

	cpufreq_cpu_put(cpu_policy);

	return 0;
}
EXPORT_SYMBOL(cpufreq_get_policy);


static int __cpufreq_set_policy(struct cpufreq_policy *data, struct cpufreq_policy *policy)
{
	int ret = 0;

	memcpy(&policy->cpuinfo, 
	       &data->cpuinfo, 
	       sizeof(struct cpufreq_cpuinfo));

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

	data->min    = policy->min;
	data->max    = policy->max;

	if (cpufreq_driver->setpolicy) {
		data->policy = policy->policy;
		ret = cpufreq_driver->setpolicy(policy);
	} else {
		if (policy->governor != data->governor) {
			/* save old, working values */
			struct cpufreq_governor *old_gov = data->governor;

			/* end old governor */
			if (data->governor)
				__cpufreq_governor(data, CPUFREQ_GOV_STOP);

			/* start new governor */
			data->governor = policy->governor;
			if (__cpufreq_governor(data, CPUFREQ_GOV_START)) {
				/* new governor failed, so re-start old one */
				if (old_gov) {
					data->governor = old_gov;
					__cpufreq_governor(data, CPUFREQ_GOV_START);
				}
				ret = -EINVAL;
				goto error_out;
			}
			/* might be a policy change, too, so fall through */
		}
		__cpufreq_governor(data, CPUFREQ_GOV_LIMITS);
	}

 error_out:
	return ret;
}

/**
 *	cpufreq_set_policy - set a new CPUFreq policy
 *	@policy: policy to be set.
 *
 *	Sets a new CPU frequency and voltage scaling policy.
 */
int cpufreq_set_policy(struct cpufreq_policy *policy)
{
	int ret = 0;
	struct cpufreq_policy *data;

	if (!policy)
		return -EINVAL;

	data = cpufreq_cpu_get(policy->cpu);
	if (!data)
		return -EINVAL;

	/* lock this CPU */
	down(&data->lock);

	ret = __cpufreq_set_policy(data, policy);
	data->user_policy.min = data->min;
	data->user_policy.max = data->max;
	data->user_policy.policy = data->policy;
	data->user_policy.governor = data->governor;

	up(&data->lock);
	cpufreq_cpu_put(data);

	return ret;
}
EXPORT_SYMBOL(cpufreq_set_policy);


/**
 *	cpufreq_update_policy - re-evaluate an existing cpufreq policy
 *	@cpu: CPU which shall be re-evaluated
 *
 *	Usefull for policy notifiers which have different necessities
 *	at different times.
 */
int cpufreq_update_policy(unsigned int cpu)
{
	struct cpufreq_policy *data = cpufreq_cpu_get(cpu);
	struct cpufreq_policy policy;
	int ret = 0;

	if (!data)
		return -ENODEV;

	down(&data->lock);

	memcpy(&policy, 
	       data,
	       sizeof(struct cpufreq_policy));
	policy.min = data->user_policy.min;
	policy.max = data->user_policy.max;
	policy.policy = data->user_policy.policy;
	policy.governor = data->user_policy.governor;

	ret = __cpufreq_set_policy(data, &policy);

	up(&data->lock);

	cpufreq_cpu_put(data);
	return ret;
}
EXPORT_SYMBOL(cpufreq_update_policy);


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
static unsigned long l_p_j_ref;
static unsigned int  l_p_j_ref_freq;

static inline void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci)
{
	if (ci->flags & CPUFREQ_CONST_LOOPS)
		return;

	if (!l_p_j_ref_freq) {
		l_p_j_ref = loops_per_jiffy;
		l_p_j_ref_freq = ci->old;
	}
	if ((val == CPUFREQ_PRECHANGE  && ci->old < ci->new) ||
	    (val == CPUFREQ_POSTCHANGE && ci->old > ci->new) ||
	    (val == CPUFREQ_RESUMECHANGE))
		loops_per_jiffy = cpufreq_scale(l_p_j_ref, l_p_j_ref_freq, ci->new);
}
#else
static inline void adjust_jiffies(unsigned long val, struct cpufreq_freqs *ci) { return; }
#endif


/**
 * cpufreq_notify_transition - call notifier chain and adjust_jiffies on frequency transition
 *
 * This function calls the transition notifiers and the "adjust_jiffies" function. It is called
 * twice on all CPU frequency changes that have external effects. 
 */
void cpufreq_notify_transition(struct cpufreq_freqs *freqs, unsigned int state)
{
	BUG_ON(irqs_disabled());

	freqs->flags = cpufreq_driver->flags;

	down_read(&cpufreq_notifier_rwsem);
	switch (state) {
	case CPUFREQ_PRECHANGE:
		/* detect if the driver reported a value as "old frequency" which
		 * is not equal to what the cpufreq core thinks is "old frequency".
		 */
		if (!(cpufreq_driver->flags & CPUFREQ_CONST_LOOPS)) {
			if ((likely(cpufreq_cpu_data[freqs->cpu]->cur)) &&
			    (unlikely(freqs->old != cpufreq_cpu_data[freqs->cpu]->cur)))
			{
				if (cpufreq_driver->flags & CPUFREQ_PANIC_OUTOFSYNC)
					panic("CPU Frequency is out of sync.");

				printk(KERN_WARNING "Warning: CPU frequency out of sync: "
				       "cpufreq and timing core thinks of %u, is %u kHz.\n", 
				       cpufreq_cpu_data[freqs->cpu]->cur, freqs->old);
				freqs->old = cpufreq_cpu_data[freqs->cpu]->cur;
			}
		}
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_PRECHANGE, freqs);
		adjust_jiffies(CPUFREQ_PRECHANGE, freqs);
		break;
	case CPUFREQ_POSTCHANGE:
		adjust_jiffies(CPUFREQ_POSTCHANGE, freqs);
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_POSTCHANGE, freqs);
		cpufreq_cpu_data[freqs->cpu]->cur = freqs->new;
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
	unsigned long flags;
	int ret;

	if (!driver_data || !driver_data->verify || !driver_data->init ||
	    ((!driver_data->setpolicy) && (!driver_data->target)))
		return -EINVAL;

	if (driver_data->setpolicy)
		driver_data->flags |= CPUFREQ_CONST_LOOPS;

	spin_lock_irqsave(&cpufreq_driver_lock, flags);
	if (cpufreq_driver) {
		spin_unlock_irqrestore(&cpufreq_driver_lock, flags);
		return -EBUSY;
	}
	cpufreq_driver = driver_data;
	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);

	ret = sysdev_driver_register(&cpu_sysdev_class,&cpufreq_sysdev_driver);

	if ((!ret) && !(cpufreq_driver->flags & CPUFREQ_STICKY)) {
		int i;
		ret = -ENODEV;

		/* check for at least one working CPU */
		for (i=0; i<NR_CPUS; i++)
			if (cpufreq_cpu_data[i])
				ret = 0;

		/* if all ->init() calls failed, unregister */
		if (ret) {
			sysdev_driver_unregister(&cpu_sysdev_class, &cpufreq_sysdev_driver);

			spin_lock_irqsave(&cpufreq_driver_lock, flags);
			cpufreq_driver = NULL;
			spin_unlock_irqrestore(&cpufreq_driver_lock, flags);
		}
	}

	return (ret);
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
	unsigned long flags;

	if (!cpufreq_driver || (driver != cpufreq_driver))
		return -EINVAL;

	sysdev_driver_unregister(&cpu_sysdev_class, &cpufreq_sysdev_driver);

	spin_lock_irqsave(&cpufreq_driver_lock, flags);
	cpufreq_driver = NULL;
	spin_unlock_irqrestore(&cpufreq_driver_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_unregister_driver);
