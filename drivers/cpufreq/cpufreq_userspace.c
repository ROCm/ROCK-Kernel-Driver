/*
 *  linux/drivers/cpufreq/cpufreq_userspace.c
 *
 *  Copyright (C)  2001 Russell King
 *            (C)  2002 - 2004 Dominik Brodowski <linux@brodo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ctype.h>
#include <linux/cpufreq.h>
#include <linux/sysctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/sysfs.h>

#include <asm/uaccess.h>

#define CTL_CPU_VARS_SPEED_MAX(cpunr) { \
                .ctl_name	= CPU_NR_FREQ_MAX, \
                .data		= &cpu_max_freq[cpunr], \
                .procname	= "speed-max", \
                .maxlen		= sizeof(cpu_max_freq[cpunr]),\
                .mode		= 0444, \
                .proc_handler	= proc_dointvec, }

#define CTL_CPU_VARS_SPEED_MIN(cpunr) { \
                .ctl_name	= CPU_NR_FREQ_MIN, \
                .data		= &cpu_min_freq[cpunr], \
                .procname	= "speed-min", \
                .maxlen		= sizeof(cpu_min_freq[cpunr]),\
                .mode		= 0444, \
                .proc_handler	= proc_dointvec, }

#define CTL_CPU_VARS_SPEED(cpunr) { \
                .ctl_name	= CPU_NR_FREQ, \
                .procname	= "speed", \
                .mode		= 0644, \
                .proc_handler	= cpufreq_procctl, \
                .strategy	= cpufreq_sysctl, \
                .extra1		= (void*) (cpunr), }

#define CTL_TABLE_CPU_VARS(cpunr) static ctl_table ctl_cpu_vars_##cpunr[] = {\
                CTL_CPU_VARS_SPEED_MAX(cpunr), \
                CTL_CPU_VARS_SPEED_MIN(cpunr), \
                CTL_CPU_VARS_SPEED(cpunr),  \
                { .ctl_name = 0, }, }

/* the ctl_table entry for each CPU */
#define CPU_ENUM(s) { \
                .ctl_name	= (CPU_NR + s), \
                .procname	= #s, \
                .mode		= 0555, \
                .child		= ctl_cpu_vars_##s }

/**
 * A few values needed by the userspace governor
 */
static unsigned int	cpu_max_freq[NR_CPUS];
static unsigned int	cpu_min_freq[NR_CPUS];
static unsigned int	cpu_cur_freq[NR_CPUS];
static unsigned int	cpu_is_managed[NR_CPUS];
static struct cpufreq_policy current_policy[NR_CPUS];

static DECLARE_MUTEX	(userspace_sem); 


/* keep track of frequency transitions */
static int 
userspace_cpufreq_notifier(struct notifier_block *nb, unsigned long val,
                       void *data)
{
        struct cpufreq_freqs *freq = data;

	cpu_cur_freq[freq->cpu] = freq->new;

        return 0;
}

static struct notifier_block userspace_cpufreq_notifier_block = {
        .notifier_call  = userspace_cpufreq_notifier
};


/** 
 * cpufreq_set - set the CPU frequency
 * @freq: target frequency in kHz
 * @cpu: CPU for which the frequency is to be set
 *
 * Sets the CPU frequency to freq.
 */
int cpufreq_set(unsigned int freq, unsigned int cpu)
{
	int ret = -EINVAL;

	down(&userspace_sem);
	if (!cpu_is_managed[cpu])
		goto err;

	if (freq < cpu_min_freq[cpu])
		freq = cpu_min_freq[cpu];
	if (freq > cpu_max_freq[cpu])
		freq = cpu_max_freq[cpu];

	/*
	 * We're safe from concurrent calls to ->target() here
	 * as we hold the userspace_sem lock. If we were calling
	 * cpufreq_driver_target, a deadlock situation might occur:
	 * A: cpufreq_set (lock userspace_sem) -> cpufreq_driver_target(lock policy->lock)
	 * B: cpufreq_set_policy(lock policy->lock) -> __cpufreq_governor -> cpufreq_governor_userspace (lock userspace_sem)
	 */
	ret = __cpufreq_driver_target(&current_policy[cpu], freq, 
	      CPUFREQ_RELATION_L);

 err:
	up(&userspace_sem);
	return ret;
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
	if (!cpu_is_managed[cpu] || !cpu_online(cpu))
		return -EINVAL;
	return cpufreq_set(cpu_max_freq[cpu], cpu);
}
EXPORT_SYMBOL_GPL(cpufreq_setmax);


/** 
 * cpufreq_get - get the current CPU frequency (in kHz)
 * @cpu: CPU number
 *
 * Get the CPU current (static) CPU frequency
 */
unsigned int cpufreq_get(unsigned int cpu)
{
	return cpu_cur_freq[cpu];
}
EXPORT_SYMBOL(cpufreq_get);


#ifdef CONFIG_CPU_FREQ_24_API


/*********************** cpufreq_sysctl interface ********************/
static int
cpufreq_procctl(ctl_table *ctl, int write, struct file *filp,
		void __user *buffer, size_t *lenp)
{
	char buf[16], *p;
	int cpu = (long) ctl->extra1;
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
cpufreq_sysctl(ctl_table *table, int __user *name, int nlen,
	       void __user *oldval, size_t __user *oldlenp,
	       void __user *newval, size_t newlen, void **context)
{
	int cpu = (long) table->extra1;

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
#endif /* CONFIG_CPU_FREQ_24API */


/************************** sysfs interface ************************/
static ssize_t show_speed (struct cpufreq_policy *policy, char *buf)
{
	return sprintf (buf, "%u\n", cpu_cur_freq[policy->cpu]);
}

static ssize_t 
store_speed (struct cpufreq_policy *policy, const char *buf, size_t count) 
{
	unsigned int freq = 0;
	unsigned int ret;

	ret = sscanf (buf, "%u", &freq);
	if (ret != 1)
		return -EINVAL;

	cpufreq_set(freq, policy->cpu);

	return count;
}

static struct freq_attr freq_attr_scaling_setspeed = {
	.attr = { .name = "scaling_setspeed", .mode = 0644 },
	.show = show_speed,
	.store = store_speed,
};

static int cpufreq_governor_userspace(struct cpufreq_policy *policy,
				   unsigned int event)
{
	unsigned int cpu = policy->cpu;
	switch (event) {
	case CPUFREQ_GOV_START:
		if ((!cpu_online(cpu)) || (!try_module_get(THIS_MODULE)))
			return -EINVAL;
		BUG_ON(!policy->cur);
		down(&userspace_sem);
		cpu_is_managed[cpu] = 1;		
		cpu_min_freq[cpu] = policy->min;
		cpu_max_freq[cpu] = policy->max;
		cpu_cur_freq[cpu] = policy->cur;
		sysfs_create_file (&policy->kobj, &freq_attr_scaling_setspeed.attr);
		memcpy (&current_policy[cpu], policy, sizeof(struct cpufreq_policy));
		up(&userspace_sem);
		break;
	case CPUFREQ_GOV_STOP:
		down(&userspace_sem);
		cpu_is_managed[cpu] = 0;
		cpu_min_freq[cpu] = 0;
		cpu_max_freq[cpu] = 0;
		sysfs_remove_file (&policy->kobj, &freq_attr_scaling_setspeed.attr);
		up(&userspace_sem);
		module_put(THIS_MODULE);
		break;
	case CPUFREQ_GOV_LIMITS:
		down(&userspace_sem);
		cpu_min_freq[cpu] = policy->min;
		cpu_max_freq[cpu] = policy->max;
		if (policy->max < cpu_cur_freq[cpu])
			__cpufreq_driver_target(&current_policy[cpu], policy->max, 
			      CPUFREQ_RELATION_H);
		else if (policy->min > cpu_cur_freq[cpu])
			__cpufreq_driver_target(&current_policy[cpu], policy->min, 
			      CPUFREQ_RELATION_L);
		memcpy (&current_policy[cpu], policy, sizeof(struct cpufreq_policy));
		up(&userspace_sem);
		break;
	}
	return 0;
}

/* on ARM SA1100 we need to rely on the values of cpufreq_get() - because 
 * of this, cpu_cur_freq[] needs to be set early.
 */
#if defined(CONFIG_ARM) && defined(CONFIG_ARCH_SA1100)
extern unsigned int sa11x0_getspeed(void);

static void cpufreq_sa11x0_compat(void)
{
	cpu_cur_freq[0] = sa11x0_getspeed();
}
#else
#define cpufreq_sa11x0_compat() do {} while(0)
#endif


struct cpufreq_governor cpufreq_gov_userspace = {
	.name		= "userspace",
	.governor	= cpufreq_governor_userspace,
	.owner		= THIS_MODULE,
};
EXPORT_SYMBOL(cpufreq_gov_userspace);

static int already_init = 0;

int cpufreq_gov_userspace_init(void)
{
	if (!already_init) {
		down(&userspace_sem);
		cpufreq_sa11x0_compat();
		cpufreq_sysctl_init();
		cpufreq_register_notifier(&userspace_cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
		already_init = 1;
		up(&userspace_sem);
	}
	return cpufreq_register_governor(&cpufreq_gov_userspace);
}
EXPORT_SYMBOL(cpufreq_gov_userspace_init);


static void __exit cpufreq_gov_userspace_exit(void)
{
	cpufreq_unregister_governor(&cpufreq_gov_userspace);
        cpufreq_unregister_notifier(&userspace_cpufreq_notifier_block, CPUFREQ_TRANSITION_NOTIFIER);
	cpufreq_sysctl_exit();
}


MODULE_AUTHOR ("Dominik Brodowski <linux@brodo.de>, Russell King <rmk@arm.linux.org.uk>");
MODULE_DESCRIPTION ("CPUfreq policy governor 'userspace'");
MODULE_LICENSE ("GPL");

fs_initcall(cpufreq_gov_userspace_init);
module_exit(cpufreq_gov_userspace_exit);
