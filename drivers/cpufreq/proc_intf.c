/*
 * linux/drivers/cpufreq/proc_intf.c
 *
 * Copyright (C) 2002 - 2003 Dominik Brodowski
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/ctype.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#warning This module will be removed from the 2.6. kernel series soon after 2005-01-01

#define CPUFREQ_ALL_CPUS		((NR_CPUS))

static unsigned int warning_print = 0;

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

	if (!warning_print) {
		warning_print++;
		printk(KERN_INFO "Access to /proc/cpufreq is deprecated and "
			"will be removed from (new) 2.6. kernels soon "
			"after 2005-01-01\n");
	}

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
		if (policy.policy) {
			switch (policy.policy) {
				case CPUFREQ_POLICY_POWERSAVE:
				p += sprintf(p, "powersave\n");
				break;	
			case CPUFREQ_POLICY_PERFORMANCE:
				p += sprintf(p, "performance\n");
				break;
			default:
				p += sprintf(p, "INVALID\n");
				break;
			} 
		} else
			p += scnprintf(p, CPUFREQ_NAME_LEN, "%s\n", policy.governor->name);
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
        const char		__user *buffer,
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

	if (!warning_print) {
		warning_print++;
		printk(KERN_INFO "Access to /proc/cpufreq is deprecated and "
			"will be removed from (new) 2.6. kernels soon "
			"after 2005-01-01\n");
	}
	
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
static int __init cpufreq_proc_init (void)
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
static void __exit cpufreq_proc_exit (void)
{
	remove_proc_entry("cpufreq", &proc_root);
	return;
}

MODULE_AUTHOR ("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("CPUfreq /proc/cpufreq interface");
MODULE_LICENSE ("GPL");

module_init(cpufreq_proc_init);
module_exit(cpufreq_proc_exit);
