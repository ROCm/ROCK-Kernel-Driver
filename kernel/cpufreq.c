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

#ifdef CONFIG_CPU_FREQ_PROC_INTF
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#endif

#ifdef CONFIG_CPU_FREQ_24_API
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#endif

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
static DECLARE_MUTEX            (cpufreq_notifier_sem);


#ifdef CONFIG_CPU_FREQ_24_API
/**
 * A few values needed by the 2.4.-compatible API
 */
static unsigned int     cpu_max_freq[NR_CPUS];
static unsigned int     cpu_min_freq[NR_CPUS];
static unsigned int     cpu_cur_freq[NR_CPUS];
#endif

LIST_HEAD(cpufreq_governor_list);

static int cpufreq_governor(unsigned int cpu, unsigned int event);

/*********************************************************************
 *                          SYSFS INTERFACE                          *
 *********************************************************************/

/**
 * cpufreq_parse_governor - parse a governor string
 */
static int cpufreq_parse_governor (char *str_governor, unsigned int *policy, struct cpufreq_governor **governor)
{
	if (!strnicmp(str_governor, "performance", CPUFREQ_NAME_LEN)) {
		*policy = CPUFREQ_POLICY_PERFORMANCE;
		return 0;
	} else if (!strnicmp(str_governor, "powersave", CPUFREQ_NAME_LEN)) {
		*policy = CPUFREQ_POLICY_POWERSAVE;
		return 0;
	} else 	{
		struct cpufreq_governor *t;
		down(&cpufreq_driver_sem);
		if (!cpufreq_driver || !cpufreq_driver->target)
			goto out;
		list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
			if (!strnicmp(str_governor,t->name,CPUFREQ_NAME_LEN)) {
				*governor = t;
				*policy = CPUFREQ_POLICY_GOVERNOR;
				up(&cpufreq_driver_sem);
				return 0;
			}
		}
	out:
		up(&cpufreq_driver_sem);
	}
	return -EINVAL;
}


/* forward declarations */
static int cpufreq_add_dev (struct device * dev);
static int cpufreq_remove_dev (struct intf_data * dev);

/* drivers/base/cpu.c */
extern struct device_class cpu_devclass;

static struct device_interface cpufreq_interface = {
        .name = "cpufreq",
        .devclass = &cpu_devclass,
        .add_device = &cpufreq_add_dev,
        .remove_device = &cpufreq_remove_dev,
	.kset = { .subsys = &cpu_devclass.subsys, },
        .devnum = 0,
};

static inline int to_cpu_nr (struct device *dev)
{
	struct sys_device * cpu_sys_dev = container_of(dev, struct sys_device, dev);
	return (cpu_sys_dev->id);
}


/**
 * cpufreq_per_cpu_attr_read() / show_##file_name() - print out cpufreq information
 *
 * Write out information from cpufreq_driver->policy[cpu]; object must be
 * "unsigned int".
 */

#define cpufreq_per_cpu_attr_read(file_name, object) 			\
static ssize_t show_##file_name 					\
(struct device *dev, char *buf)						\
{									\
	unsigned int value = 0;						\
									\
	if (!dev)							\
		return 0;						\
									\
	down(&cpufreq_driver_sem);					\
	if (cpufreq_driver)						\
		value = cpufreq_driver->policy[to_cpu_nr(dev)].object;	\
	up(&cpufreq_driver_sem);					\
									\
	return sprintf (buf, "%u\n", value);				\
}


/**
 * cpufreq_per_cpu_attr_write() / store_##file_name() - sysfs write access
 */
#define cpufreq_per_cpu_attr_write(file_name, object)			\
static ssize_t store_##file_name					\
(struct device *dev, const char *buf, size_t count)			\
{									\
	unsigned int ret = -EINVAL;					\
	struct cpufreq_policy policy;					\
									\
	if (!dev)							\
		return 0;						\
									\
	ret = cpufreq_get_policy(&policy, to_cpu_nr(dev));		\
	if (ret)							\
		return ret;						\
									\
	ret = sscanf (buf, "%u", &policy.object);			\
	if (ret != 1)							\
		return -EINVAL;						\
									\
	ret = cpufreq_set_policy(&policy);				\
	if (ret)							\
		return ret;						\
									\
	return count;							\
}


/**
 * show_scaling_governor - show the current policy for the specified CPU
 */
static ssize_t show_scaling_governor (struct device *dev, char *buf)
{
	unsigned int value = 0;
	char value2[CPUFREQ_NAME_LEN];


	if (!dev)
		return 0;

	down(&cpufreq_driver_sem);
	if (cpufreq_driver)
		value = cpufreq_driver->policy[to_cpu_nr(dev)].policy;
	if (value == CPUFREQ_POLICY_GOVERNOR)
		strncpy(value2, cpufreq_driver->policy[to_cpu_nr(dev)].governor->name, CPUFREQ_NAME_LEN);
	up(&cpufreq_driver_sem);

	switch (value) {
	case CPUFREQ_POLICY_POWERSAVE:
		return sprintf(buf, "powersave\n");
	case CPUFREQ_POLICY_PERFORMANCE:
		return sprintf(buf, "performance\n");
	case CPUFREQ_POLICY_GOVERNOR:
		return sprintf(buf, "%s\n", value2);
	}

	return -EINVAL;
}


/**
 * store_scaling_governor - store policy for the specified CPU
 */
static ssize_t 
store_scaling_governor (struct device *dev, const char *buf, size_t count) 
{
	unsigned int ret = -EINVAL;
	char	str_governor[16];
	struct cpufreq_policy policy;

	if (!dev)
		return 0;

	ret = cpufreq_get_policy(&policy, to_cpu_nr(dev));
	if (ret)
		return ret;

	ret = sscanf (buf, "%15s", str_governor);
	if (ret != 1)
		return -EINVAL;

	if (cpufreq_parse_governor(str_governor, &policy.policy, &policy.governor))
		return -EINVAL;

	ret = cpufreq_set_policy(&policy);
	if (ret)
		return ret;

	return count;
}


/**
 * show_scaling_governor - show the current policy for the specified CPU
 */
static ssize_t show_scaling_driver (struct device *dev, char *buf)
{
	char value[CPUFREQ_NAME_LEN];

	if (!dev)
		return 0;

	down(&cpufreq_driver_sem);
	if (cpufreq_driver)
		strncpy(value, cpufreq_driver->name, CPUFREQ_NAME_LEN);
	up(&cpufreq_driver_sem);

	return sprintf(buf, "%s\n", value);
}

/**
 * show_available_govs - show the available CPUfreq governors
 */
static ssize_t show_available_govs(struct device *dev, char *buf)
{
	ssize_t i = 0;
	struct cpufreq_governor *t;

	if (!dev)
		return 0;

	i += sprintf(buf, "performance powersave");

	down(&cpufreq_driver_sem);
	if (!cpufreq_driver || !cpufreq_driver->target)
		goto out;

	list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
		if (i >= (ssize_t) ((PAGE_SIZE / sizeof(char)) - (CPUFREQ_NAME_LEN + 2)))
			goto out;
		i += snprintf(&buf[i], CPUFREQ_NAME_LEN, " %s", t->name);
	}
 out:
	up(&cpufreq_driver_sem);
	i += sprintf(&buf[i], "\n");
	return i;
}


/**
 * cpufreq_per_cpu_attr_ro - read-only cpufreq per-CPU file
 */
#define cpufreq_per_cpu_attr_ro(file_name, object)			\
cpufreq_per_cpu_attr_read(file_name, object) 				\
static DEVICE_ATTR(file_name, S_IRUGO, show_##file_name, NULL);


/**
 * cpufreq_per_cpu_attr_rw - read-write cpufreq per-CPU file
 */
#define cpufreq_per_cpu_attr_rw(file_name, object) 			\
cpufreq_per_cpu_attr_read(file_name, object) 				\
cpufreq_per_cpu_attr_write(file_name, object) 				\
static DEVICE_ATTR(file_name, (S_IRUGO | S_IWUSR), show_##file_name, store_##file_name);


/* create the file functions */
cpufreq_per_cpu_attr_ro(cpuinfo_min_freq, cpuinfo.min_freq);
cpufreq_per_cpu_attr_ro(cpuinfo_max_freq, cpuinfo.max_freq);
cpufreq_per_cpu_attr_rw(scaling_min_freq, min);
cpufreq_per_cpu_attr_rw(scaling_max_freq, max);

static DEVICE_ATTR(scaling_governor, (S_IRUGO | S_IWUSR), show_scaling_governor, store_scaling_governor);
static DEVICE_ATTR(scaling_driver, S_IRUGO, show_scaling_driver, NULL);
static DEVICE_ATTR(available_scaling_governors, S_IRUGO, show_available_govs, NULL);


/**
 * cpufreq_add_dev - add a CPU device
 *
 * Adds the cpufreq interface for a CPU device. 
 */
static int cpufreq_add_dev (struct device * dev)
{
	unsigned int cpu = to_cpu_nr(dev);
	int ret = 0;
	struct cpufreq_policy policy;

	down(&cpufreq_driver_sem);
	if (!cpufreq_driver) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	/* call driver. From then on the cpufreq must be able
	 * to accept all calls to ->verify and ->setpolicy for this CPU
	 */
	cpufreq_driver->policy[cpu].cpu = cpu;
	if (cpufreq_driver->init) {
		ret = cpufreq_driver->init(&cpufreq_driver->policy[cpu]);
		if (ret) {
			up(&cpufreq_driver_sem);
			return -ENODEV;
		}
	}

	/* set default policy on this CPU */
	memcpy(&policy, 
	       &cpufreq_driver->policy[cpu], 
	       sizeof(struct cpufreq_policy));

	if (cpufreq_driver->target)
		cpufreq_governor(cpu, CPUFREQ_GOV_START);

	up(&cpufreq_driver_sem);
	ret = cpufreq_set_policy(&policy);
	if (ret)
		return -EINVAL;
	down(&cpufreq_driver_sem);

	/* 2.4-API init for this CPU */
#ifdef CONFIG_CPU_FREQ_24_API
	cpu_min_freq[cpu] = cpufreq_driver->policy[cpu].cpuinfo.min_freq;
	cpu_max_freq[cpu] = cpufreq_driver->policy[cpu].cpuinfo.max_freq;
	cpu_cur_freq[cpu] = cpufreq_driver->cpu_cur_freq[cpu];
#endif

	/* prepare interface data */
	cpufreq_driver->policy[cpu].intf.dev  = dev;
	cpufreq_driver->policy[cpu].intf.intf = &cpufreq_interface;
	strncpy(cpufreq_driver->policy[cpu].intf.kobj.name, cpufreq_interface.name, KOBJ_NAME_LEN);
	cpufreq_driver->policy[cpu].intf.kobj.parent = &(dev->kobj);
	cpufreq_driver->policy[cpu].intf.kobj.kset = &(cpufreq_interface.kset);

	/* add interface */
	/* currently commented out due to deadlock */
	//ret = interface_add_data(&(cpufreq_driver->policy[cpu].intf));
	if (ret) {
		up(&cpufreq_driver_sem);
		return ret;
	}

	/* create files */
	device_create_file (dev, &dev_attr_cpuinfo_min_freq);
	device_create_file (dev, &dev_attr_cpuinfo_max_freq);
	device_create_file (dev, &dev_attr_scaling_min_freq);
	device_create_file (dev, &dev_attr_scaling_max_freq);
	device_create_file (dev, &dev_attr_scaling_governor);
	device_create_file (dev, &dev_attr_scaling_driver);
	device_create_file (dev, &dev_attr_available_scaling_governors);

	up(&cpufreq_driver_sem);
	return ret;
}


/**
 * cpufreq_remove_dev - remove a CPU device
 *
 * Removes the cpufreq interface for a CPU device. Is called with
 * cpufreq_driver_sem locked.
 */
static int cpufreq_remove_dev (struct intf_data *intf)
{
	struct device * dev = intf->dev;
	unsigned int cpu = to_cpu_nr(dev);

	if (cpufreq_driver->target)
		cpufreq_governor(cpu, CPUFREQ_GOV_STOP);

	if (cpufreq_driver->exit)
		cpufreq_driver->exit(&cpufreq_driver->policy[cpu]);

	device_remove_file (dev, &dev_attr_cpuinfo_min_freq);
	device_remove_file (dev, &dev_attr_cpuinfo_max_freq);
	device_remove_file (dev, &dev_attr_scaling_min_freq);
	device_remove_file (dev, &dev_attr_scaling_max_freq);
	device_remove_file (dev, &dev_attr_scaling_governor);
	device_remove_file (dev, &dev_attr_scaling_driver);
	device_remove_file (dev, &dev_attr_available_scaling_governors);

	return 0;
}


/*********************************************************************
 *                      /proc/cpufreq INTERFACE                      *
 *********************************************************************/

#ifdef CONFIG_CPU_FREQ_PROC_INTF

/**
 * cpufreq_parse_policy - parse a policy string
 * @input_string: the string to parse.
 * @policy: the policy written inside input_string
 *
 * This function parses a "policy string" - something the user echo'es into
 * /proc/cpufreq or gives as boot parameter - into a struct cpufreq_policy.
 * If there are invalid/missing entries, they are replaced with current
 * cpufreq policy.
 */
static int cpufreq_parse_policy(char input_string[42], struct cpufreq_policy *policy)
{
	unsigned int            min = 0;
	unsigned int            max = 0;
	unsigned int            cpu = 0;
	char			str_governor[16];
	struct cpufreq_policy   current_policy;
	unsigned int            result = -EFAULT;

	if (!policy)
		return -EINVAL;

	policy->min = 0;
	policy->max = 0;
	policy->policy = 0;
	policy->cpu = CPUFREQ_ALL_CPUS;

	if (sscanf(input_string, "%d:%d:%d:%15s", &cpu, &min, &max, str_governor) == 4) 
	{
		policy->min = min;
		policy->max = max;
		policy->cpu = cpu;
		result = 0;
		goto scan_policy;
	}
	if (sscanf(input_string, "%d%%%d%%%d%%%15s", &cpu, &min, &max, str_governor) == 4)
	{
		if (!cpufreq_get_policy(&current_policy, cpu)) {
			policy->min = (min * current_policy.cpuinfo.max_freq) / 100;
			policy->max = (max * current_policy.cpuinfo.max_freq) / 100;
			policy->cpu = cpu;
			result = 0;
			goto scan_policy;
		}
	}

	if (sscanf(input_string, "%d:%d:%15s", &min, &max, str_governor) == 3) 
	{
		policy->min = min;
		policy->max = max;
		result = 0;
		goto scan_policy;
	}

	if (sscanf(input_string, "%d%%%d%%%15s", &min, &max, str_governor) == 3)
	{
		if (!cpufreq_get_policy(&current_policy, cpu)) {
			policy->min = (min * current_policy.cpuinfo.max_freq) / 100;
			policy->max = (max * current_policy.cpuinfo.max_freq) / 100;
			result = 0;
			goto scan_policy;
		}
	}

	return -EINVAL;

scan_policy:
	result = cpufreq_parse_governor(str_governor, &policy->policy, &policy->governor);

	return result;
}

/**
 * cpufreq_proc_read - read /proc/cpufreq
 *
 * This function prints out the current cpufreq policy.
 */
static int cpufreq_proc_read (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*data)
{
	char			*p = page;
	int			len = 0;
	struct cpufreq_policy   policy;
	unsigned int            min_pctg = 0;
	unsigned int            max_pctg = 0;
	unsigned int            i = 0;

	if (off != 0)
		goto end;

	p += sprintf(p, "          minimum CPU frequency  -  maximum CPU frequency  -  policy\n");
	for (i=0;i<NR_CPUS;i++) {
		if (!cpu_online(i))
			continue;

		if (cpufreq_get_policy(&policy, i))
			continue;

		if (!policy.cpuinfo.max_freq)
			continue;

		min_pctg = (policy.min * 100) / policy.cpuinfo.max_freq;
		max_pctg = (policy.max * 100) / policy.cpuinfo.max_freq;

		p += sprintf(p, "CPU%3d    %9d kHz (%3d %%)  -  %9d kHz (%3d %%)  -  ",
			     i , policy.min, min_pctg, policy.max, max_pctg);
		switch (policy.policy) {
		case CPUFREQ_POLICY_POWERSAVE:
			p += sprintf(p, "powersave\n");
			break;
		case CPUFREQ_POLICY_PERFORMANCE:
			p += sprintf(p, "performance\n");
			break;
		case CPUFREQ_POLICY_GOVERNOR:
			p += snprintf(p, CPUFREQ_NAME_LEN, "%s\n", policy.governor->name);
			break;
		default:
			p += sprintf(p, "INVALID\n");
			break;
		}
	}
end:
	len = (p - page);
	if (len <= off+count) 
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) 
		len = count;
	if (len<0) 
		len = 0;

	return len;
}


/**
 * cpufreq_proc_write - handles writing into /proc/cpufreq
 *
 * This function calls the parsing script and then sets the policy
 * accordingly.
 */
static int cpufreq_proc_write (
        struct file		*file,
        const char		*buffer,
        unsigned long		count,
        void			*data)
{
	int                     result = 0;
	char			proc_string[42] = {'\0'};
	struct cpufreq_policy   policy;
	unsigned int            i = 0;


	if ((count > sizeof(proc_string) - 1))
		return -EINVAL;
	
	if (copy_from_user(proc_string, buffer, count))
		return -EFAULT;
	
	proc_string[count] = '\0';

	result = cpufreq_parse_policy(proc_string, &policy);
	if (result)
		return -EFAULT;

	if (policy.cpu == CPUFREQ_ALL_CPUS)
	{
		for (i=0; i<NR_CPUS; i++) 
		{
			policy.cpu = i;
			if (cpu_online(i))
				cpufreq_set_policy(&policy);
		}
	} 
	else
		cpufreq_set_policy(&policy);

	return count;
}


/**
 * cpufreq_proc_init - add "cpufreq" to the /proc root directory
 *
 * This function adds "cpufreq" to the /proc root directory.
 */
static unsigned int cpufreq_proc_init (void)
{
	struct proc_dir_entry *entry = NULL;

        /* are these acceptable values? */
	entry = create_proc_entry("cpufreq", S_IFREG|S_IRUGO|S_IWUSR, 
				  &proc_root);

	if (!entry) {
		printk(KERN_ERR "unable to create /proc/cpufreq entry\n");
		return -EIO;
	} else {
		entry->read_proc = cpufreq_proc_read;
		entry->write_proc = cpufreq_proc_write;
	}

	return 0;
}


/**
 * cpufreq_proc_exit - removes "cpufreq" from the /proc root directory.
 *
 * This function removes "cpufreq" from the /proc root directory.
 */
static void cpufreq_proc_exit (void)
{
	remove_proc_entry("cpufreq", &proc_root);
	return;
}
#else
#define cpufreq_proc_init() do {} while(0)
#define cpufreq_proc_exit() do {} while(0)
#endif /* CONFIG_CPU_FREQ_PROC_INTF */



/*********************************************************************
 *                      /proc/sys/cpu/ INTERFACE                     *
 *********************************************************************/

#ifdef CONFIG_CPU_FREQ_24_API
/** 
 * cpufreq_set - set the CPU frequency
 * @freq: target frequency in kHz
 * @cpu: CPU for which the frequency is to be set
 *
 * Sets the CPU frequency to freq.
 */
int cpufreq_set(unsigned int freq, unsigned int cpu)
{
	struct cpufreq_policy policy;
	down(&cpufreq_driver_sem);
	if (!cpufreq_driver || !freq || (cpu > NR_CPUS)) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	policy.min = freq;
	policy.max = freq;
	policy.policy = CPUFREQ_POLICY_POWERSAVE;
	policy.cpu = cpu;
	
	up(&cpufreq_driver_sem);

	if (policy.cpu == CPUFREQ_ALL_CPUS)
	{
		unsigned int i;
		unsigned int ret = 0;
		for (i=0; i<NR_CPUS; i++) 
		{
			policy.cpu = i;
			if (cpu_online(i))
				ret |= cpufreq_set_policy(&policy);
		}
		return ret;
	} 
	else
		return cpufreq_set_policy(&policy);
}
EXPORT_SYMBOL_GPL(cpufreq_set);


/** 
 * cpufreq_setmax - set the CPU to the maximum frequency
 * @cpu - affected cpu;
 *
 * Sets the CPU frequency to the maximum frequency supported by
 * this CPU.
 */
int cpufreq_setmax(unsigned int cpu)
{
	if (!cpu_online(cpu) && (cpu != CPUFREQ_ALL_CPUS))
		return -EINVAL;
	return cpufreq_set(cpu_max_freq[cpu], cpu);
}
EXPORT_SYMBOL_GPL(cpufreq_setmax);


/** 
 * cpufreq_get - get the current CPU frequency (in kHz)
 * @cpu: CPU number - currently without effect.
 *
 * Get the CPU current (static) CPU frequency
 */
unsigned int cpufreq_get(unsigned int cpu)
{
	if (!cpu_online(cpu))
		return -EINVAL;
	return cpu_cur_freq[cpu];
}
EXPORT_SYMBOL(cpufreq_get);


#ifdef CONFIG_SYSCTL


/*********************** cpufreq_sysctl interface ********************/
static int
cpufreq_procctl(ctl_table *ctl, int write, struct file *filp,
		void *buffer, size_t *lenp)
{
	char buf[16], *p;
	int cpu = (int) ctl->extra1;
	int len, left = *lenp;

	if (!left || (filp->f_pos && !write) || !cpu_online(cpu)) {
		*lenp = 0;
		return 0;
	}

	if (write) {
		unsigned int freq;

		len = left;
		if (left > sizeof(buf))
			left = sizeof(buf);
		if (copy_from_user(buf, buffer, left))
			return -EFAULT;
		buf[sizeof(buf) - 1] = '\0';

		freq = simple_strtoul(buf, &p, 0);
		cpufreq_set(freq, cpu);
	} else {
		len = sprintf(buf, "%d\n", cpufreq_get(cpu));
		if (len > left)
			len = left;
		if (copy_to_user(buffer, buf, len))
			return -EFAULT;
	}

	*lenp = len;
	filp->f_pos += len;
	return 0;
}

static int
cpufreq_sysctl(ctl_table *table, int *name, int nlen,
	       void *oldval, size_t *oldlenp,
	       void *newval, size_t newlen, void **context)
{
	int cpu = (int) table->extra1;

	if (!cpu_online(cpu))
		return -EINVAL;

	if (oldval && oldlenp) {
		size_t oldlen;

		if (get_user(oldlen, oldlenp))
			return -EFAULT;

		if (oldlen != sizeof(unsigned int))
			return -EINVAL;

		if (put_user(cpufreq_get(cpu), (unsigned int *)oldval) ||
		    put_user(sizeof(unsigned int), oldlenp))
			return -EFAULT;
	}
	if (newval && newlen) {
		unsigned int freq;

		if (newlen != sizeof(unsigned int))
			return -EINVAL;

		if (get_user(freq, (unsigned int *)newval))
			return -EFAULT;

		cpufreq_set(freq, cpu);
	}
	return 1;
}

/* ctl_table ctl_cpu_vars_{0,1,...,(NR_CPUS-1)} */
/* due to NR_CPUS tweaking, a lot of if/endifs are required, sorry */
        CTL_TABLE_CPU_VARS(0);
#if NR_CPUS > 1
	CTL_TABLE_CPU_VARS(1);
#endif
#if NR_CPUS > 2
	CTL_TABLE_CPU_VARS(2);
#endif
#if NR_CPUS > 3
	CTL_TABLE_CPU_VARS(3);
#endif
#if NR_CPUS > 4
	CTL_TABLE_CPU_VARS(4);
#endif
#if NR_CPUS > 5
	CTL_TABLE_CPU_VARS(5);
#endif
#if NR_CPUS > 6
	CTL_TABLE_CPU_VARS(6);
#endif
#if NR_CPUS > 7
	CTL_TABLE_CPU_VARS(7);
#endif
#if NR_CPUS > 8
	CTL_TABLE_CPU_VARS(8);
#endif
#if NR_CPUS > 9
	CTL_TABLE_CPU_VARS(9);
#endif
#if NR_CPUS > 10
	CTL_TABLE_CPU_VARS(10);
#endif
#if NR_CPUS > 11
	CTL_TABLE_CPU_VARS(11);
#endif
#if NR_CPUS > 12
	CTL_TABLE_CPU_VARS(12);
#endif
#if NR_CPUS > 13
	CTL_TABLE_CPU_VARS(13);
#endif
#if NR_CPUS > 14
	CTL_TABLE_CPU_VARS(14);
#endif
#if NR_CPUS > 15
	CTL_TABLE_CPU_VARS(15);
#endif
#if NR_CPUS > 16
	CTL_TABLE_CPU_VARS(16);
#endif
#if NR_CPUS > 17
	CTL_TABLE_CPU_VARS(17);
#endif
#if NR_CPUS > 18
	CTL_TABLE_CPU_VARS(18);
#endif
#if NR_CPUS > 19
	CTL_TABLE_CPU_VARS(19);
#endif
#if NR_CPUS > 20
	CTL_TABLE_CPU_VARS(20);
#endif
#if NR_CPUS > 21
	CTL_TABLE_CPU_VARS(21);
#endif
#if NR_CPUS > 22
	CTL_TABLE_CPU_VARS(22);
#endif
#if NR_CPUS > 23
	CTL_TABLE_CPU_VARS(23);
#endif
#if NR_CPUS > 24
	CTL_TABLE_CPU_VARS(24);
#endif
#if NR_CPUS > 25
	CTL_TABLE_CPU_VARS(25);
#endif
#if NR_CPUS > 26
	CTL_TABLE_CPU_VARS(26);
#endif
#if NR_CPUS > 27
	CTL_TABLE_CPU_VARS(27);
#endif
#if NR_CPUS > 28
	CTL_TABLE_CPU_VARS(28);
#endif
#if NR_CPUS > 29
	CTL_TABLE_CPU_VARS(29);
#endif
#if NR_CPUS > 30
	CTL_TABLE_CPU_VARS(30);
#endif
#if NR_CPUS > 31
	CTL_TABLE_CPU_VARS(31);
#endif
#if NR_CPUS > 32
#error please extend CPU enumeration
#endif

/* due to NR_CPUS tweaking, a lot of if/endifs are required, sorry */
static ctl_table ctl_cpu_table[NR_CPUS + 1] = {
	CPU_ENUM(0),
#if NR_CPUS > 1
	CPU_ENUM(1),
#endif
#if NR_CPUS > 2
	CPU_ENUM(2),
#endif
#if NR_CPUS > 3
	CPU_ENUM(3),
#endif
#if NR_CPUS > 4
	CPU_ENUM(4),
#endif
#if NR_CPUS > 5
	CPU_ENUM(5),
#endif
#if NR_CPUS > 6
	CPU_ENUM(6),
#endif
#if NR_CPUS > 7
	CPU_ENUM(7),
#endif
#if NR_CPUS > 8
	CPU_ENUM(8),
#endif
#if NR_CPUS > 9
	CPU_ENUM(9),
#endif
#if NR_CPUS > 10
	CPU_ENUM(10),
#endif
#if NR_CPUS > 11
	CPU_ENUM(11),
#endif
#if NR_CPUS > 12
	CPU_ENUM(12),
#endif
#if NR_CPUS > 13
	CPU_ENUM(13),
#endif
#if NR_CPUS > 14
	CPU_ENUM(14),
#endif
#if NR_CPUS > 15
	CPU_ENUM(15),
#endif
#if NR_CPUS > 16
	CPU_ENUM(16),
#endif
#if NR_CPUS > 17
	CPU_ENUM(17),
#endif
#if NR_CPUS > 18
	CPU_ENUM(18),
#endif
#if NR_CPUS > 19
	CPU_ENUM(19),
#endif
#if NR_CPUS > 20
	CPU_ENUM(20),
#endif
#if NR_CPUS > 21
	CPU_ENUM(21),
#endif
#if NR_CPUS > 22
	CPU_ENUM(22),
#endif
#if NR_CPUS > 23
	CPU_ENUM(23),
#endif
#if NR_CPUS > 24
	CPU_ENUM(24),
#endif
#if NR_CPUS > 25
	CPU_ENUM(25),
#endif
#if NR_CPUS > 26
	CPU_ENUM(26),
#endif
#if NR_CPUS > 27
	CPU_ENUM(27),
#endif
#if NR_CPUS > 28
	CPU_ENUM(28),
#endif
#if NR_CPUS > 29
	CPU_ENUM(29),
#endif
#if NR_CPUS > 30
	CPU_ENUM(30),
#endif
#if NR_CPUS > 31
	CPU_ENUM(31),
#endif
#if NR_CPUS > 32
#error please extend CPU enumeration
#endif
	{
		.ctl_name	= 0,
	}
};

static ctl_table ctl_cpu[2] = {
	{
		.ctl_name	= CTL_CPU,
		.procname	= "cpu",
		.mode		= 0555,
		.child		= ctl_cpu_table,
	},
	{
		.ctl_name	= 0,
	}
};

struct ctl_table_header *cpufreq_sysctl_table;

static inline void cpufreq_sysctl_init(void)
{
	cpufreq_sysctl_table = register_sysctl_table(ctl_cpu, 0);
}

static inline void cpufreq_sysctl_exit(void)
{
	unregister_sysctl_table(cpufreq_sysctl_table);
}

#else
#define cpufreq_sysctl_init() do {} while(0)
#define cpufreq_sysctl_exit() do {} while(0)
#endif /* CONFIG_SYSCTL */
#endif /* CONFIG_CPU_FREQ_24_API */



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

	down(&cpufreq_notifier_sem);
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
	up(&cpufreq_notifier_sem);

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

	down(&cpufreq_notifier_sem);
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
	up(&cpufreq_notifier_sem);

	return ret;
}
EXPORT_SYMBOL(cpufreq_unregister_notifier);


/*********************************************************************
 *                              GOVERNORS                            *
 *********************************************************************/

inline int cpufreq_driver_target_l(struct cpufreq_policy *policy,
				   unsigned int target_freq,
				   unsigned int relation)
{
	unsigned int ret;
	down(&cpufreq_driver_sem);
	if (!cpufreq_driver)
		ret = -EINVAL;
	else
		ret = cpufreq_driver->target(policy, target_freq, relation);
	up(&cpufreq_driver_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_driver_target_l);


inline int cpufreq_driver_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation)
{
	return cpufreq_driver->target(policy, target_freq, relation);
}
EXPORT_SYMBOL_GPL(cpufreq_driver_target);


static int cpufreq_governor(unsigned int cpu, unsigned int event)
{
	int ret = 0;
	struct cpufreq_policy *policy = &cpufreq_driver->policy[cpu];

	switch (policy->policy) {
	case CPUFREQ_POLICY_POWERSAVE: 
		if ((event == CPUFREQ_GOV_LIMITS) || (event == CPUFREQ_GOV_START))
			ret = cpufreq_driver->target(policy, policy->min, CPUFREQ_RELATION_L);
		break;
	case CPUFREQ_POLICY_PERFORMANCE:
		if ((event == CPUFREQ_GOV_LIMITS) || (event == CPUFREQ_GOV_START))
			ret = cpufreq_driver->target(policy, policy->max, CPUFREQ_RELATION_H);
		break;
	case CPUFREQ_POLICY_GOVERNOR:
		ret = -EINVAL;
		if (event == CPUFREQ_GOV_START)
			if (!try_module_get(cpufreq_driver->policy[cpu].governor->owner))
				break;
		ret = cpufreq_driver->policy[cpu].governor->governor(policy, event);
		if ((event == CPUFREQ_GOV_STOP) ||
			(ret && (event == CPUFREQ_GOV_START)))
			module_put(cpufreq_driver->policy[cpu].governor->owner);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}


int cpufreq_governor_l(unsigned int cpu, unsigned int event)
{
	int ret = 0;
	down(&cpufreq_driver_sem);
	ret = cpufreq_governor(cpu, event);
	up(&cpufreq_driver_sem);
	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_governor_l);


int cpufreq_register_governor(struct cpufreq_governor *governor)
{
	struct cpufreq_governor *t;

	if (!governor)
		return -EINVAL;

	if (!strnicmp(governor->name,"powersave",CPUFREQ_NAME_LEN))
		return -EBUSY;
	if (!strnicmp(governor->name,"performance",CPUFREQ_NAME_LEN))
		return -EBUSY;

	down(&cpufreq_driver_sem);
	
	list_for_each_entry(t, &cpufreq_governor_list, governor_list) {
		if (!strnicmp(governor->name,t->name,CPUFREQ_NAME_LEN)) {
			up(&cpufreq_driver_sem);
			return -EBUSY;
		}
	}
	list_add(&governor->governor_list, &cpufreq_governor_list);
 	up(&cpufreq_driver_sem);

	return 0;
}
EXPORT_SYMBOL_GPL(cpufreq_register_governor);


void cpufreq_unregister_governor(struct cpufreq_governor *governor)
{
	unsigned int i;
	
	if (!governor)
		return;

	down(&cpufreq_driver_sem);
	/* 
	 * Unless the user uses rmmod -f, we can be safe. But we never
	 * know, so check whether if it's currently used. If so,
	 * stop it and replace it with the default governor.
	 */
	for (i=0; i<NR_CPUS; i++)
	{
		if (cpufreq_driver && 
		    (cpufreq_driver->policy[i].policy == CPUFREQ_POLICY_GOVERNOR) && 
		    (cpufreq_driver->policy[i].governor == governor)) {
			cpufreq_governor(i, CPUFREQ_GOV_STOP);
			cpufreq_driver->policy[i].policy = CPUFREQ_POLICY_PERFORMANCE;
			cpufreq_governor(i, CPUFREQ_GOV_START);
		}
	}
	/* now we can safely remove it from the list */
	list_del(&governor->governor_list);
	up(&cpufreq_driver_sem);
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
	down(&cpufreq_driver_sem);
	if (!cpufreq_driver  || !policy || 
	    (cpu >= NR_CPUS) || (!cpu_online(cpu))) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	memcpy(policy, 
	       &cpufreq_driver->policy[cpu], 
	       sizeof(struct cpufreq_policy));
	
	up(&cpufreq_driver_sem);

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
	int ret;

	down(&cpufreq_driver_sem);
	if (!cpufreq_driver || !policy ||
	    (policy->cpu >= NR_CPUS) || (!cpu_online(policy->cpu))) {
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	memcpy(&policy->cpuinfo, 
	       &cpufreq_driver->policy[policy->cpu].cpuinfo, 
	       sizeof(struct cpufreq_cpuinfo));

	/* verify the cpu speed can be set within this limit */
	ret = cpufreq_driver->verify(policy);
	if (ret) {
		up(&cpufreq_driver_sem);
		return ret;
	}

	down(&cpufreq_notifier_sem);

	/* adjust if neccessary - all reasons */
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_ADJUST,
			    policy);

	/* adjust if neccessary - hardware incompatibility*/
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_INCOMPATIBLE,
			    policy);

	/* verify the cpu speed can be set within this limit,
	   which might be different to the first one */
	ret = cpufreq_driver->verify(policy);
	if (ret) {
		up(&cpufreq_notifier_sem);
		up(&cpufreq_driver_sem);
		return ret;
	}

	/* notification of the new policy */
	notifier_call_chain(&cpufreq_policy_notifier_list, CPUFREQ_NOTIFY,
			    policy);

	up(&cpufreq_notifier_sem);

	cpufreq_driver->policy[policy->cpu].min    = policy->min;
	cpufreq_driver->policy[policy->cpu].max    = policy->max;

#ifdef CONFIG_CPU_FREQ_24_API
	cpu_cur_freq[policy->cpu] = policy->max;
#endif

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
	down(&cpufreq_notifier_sem);
	switch (state) {
	case CPUFREQ_PRECHANGE:
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_PRECHANGE, freqs);
		adjust_jiffies(CPUFREQ_PRECHANGE, freqs);		
		break;
	case CPUFREQ_POSTCHANGE:
		adjust_jiffies(CPUFREQ_POSTCHANGE, freqs);
		notifier_call_chain(&cpufreq_transition_notifier_list, CPUFREQ_POSTCHANGE, freqs);
#ifdef CONFIG_CPU_FREQ_24_API
		cpu_cur_freq[freqs->cpu] = freqs->new;
#endif
		break;
	}
	up(&cpufreq_notifier_sem);
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
	int ret = 0;

	if (cpufreq_driver)
		return -EBUSY;
	
	if (!driver_data || !driver_data->verify || 
	    ((!driver_data->setpolicy) && (!driver_data->target)))
		return -EINVAL;

	down(&cpufreq_driver_sem);

	cpufreq_driver = driver_data;

	if (!cpufreq_driver->policy) {
		/* then we need per-CPU init */
		if (!cpufreq_driver->init) {
			up(&cpufreq_driver_sem);
			return -EINVAL;
		}
		cpufreq_driver->policy = kmalloc(NR_CPUS * sizeof(struct cpufreq_policy), GFP_KERNEL);
		if (!cpufreq_driver->policy) {
			up(&cpufreq_driver_sem);
			return -ENOMEM;
		}
		memset(cpufreq_driver->policy, 0, NR_CPUS * sizeof(struct cpufreq_policy));
	}
	
	up(&cpufreq_driver_sem);

	cpufreq_proc_init();

#ifdef CONFIG_CPU_FREQ_24_API
	cpufreq_sysctl_init();
#endif

	ret = interface_register(&cpufreq_interface);

	return ret;
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
	down(&cpufreq_driver_sem);

	if (!cpufreq_driver || 
	    ((driver != cpufreq_driver) && (driver != NULL))) { /* compat */
		up(&cpufreq_driver_sem);
		return -EINVAL;
	}

	cpufreq_proc_exit();

#ifdef CONFIG_CPU_FREQ_24_API
	cpufreq_sysctl_exit();
#endif

	/* remove this workaround as soon as interface_add_data works */
	{
		unsigned int i;
		for (i=0; i<NR_CPUS; i++) {
			if (cpu_online(i)) 
				cpufreq_remove_dev(&cpufreq_driver->policy[i].intf);
		}
	}

	interface_unregister(&cpufreq_interface);

	if (driver)
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

	for (i=0;i<NR_CPUS;i++) {
		if (!cpu_online(i))
			continue;

		down(&cpufreq_driver_sem);
		if (!cpufreq_driver) {
			up(&cpufreq_driver_sem);
			return 0;
		}

		memcpy(&policy, &cpufreq_driver->policy[i], sizeof(struct cpufreq_policy));
		up(&cpufreq_driver_sem);

		ret += cpufreq_set_policy(&policy);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cpufreq_restore);
#else
#define cpufreq_restore() do {} while (0)
#endif /* CONFIG_PM */
