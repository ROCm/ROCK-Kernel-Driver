/*
 *	Pentium 4/Xeon CPU on demand clock modulation/speed scaling
 *	(C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *	(C) 2002 Zwane Mwaikambo <zwane@commfireservices.com>
 *	(C) 2002 Arjan van de Ven <arjanv@redhat.com>
 *	(C) 2002 Tora T. Engstad
 *	All Rights Reserved
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      The author(s) of this software shall not be held liable for damages
 *      of any nature resulting due to the use of this software. This
 *      software is provided AS-IS with no warranties.
 *	
 *	Date		Errata			Description
 *	20020525	N44, O17	12.5% or 25% DC causes lockup
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/sched.h>

#include <asm/processor.h> 
#include <asm/msr.h>
#include <asm/timex.h>

#include "speedstep-lib.h"

#define PFX	"cpufreq: "

/*
 * Duty Cycle (3bits), note DC_DISABLE is not specified in
 * intel docs i just use it to mean disable
 */
enum {
	DC_RESV, DC_DFLT, DC_25PT, DC_38PT, DC_50PT,
	DC_64PT, DC_75PT, DC_88PT, DC_DISABLE
};

#define DC_ENTRIES	8


static int has_N44_O17_errata[NR_CPUS];
static unsigned int stock_freq;
static struct cpufreq_driver p4clockmod_driver;

static int cpufreq_p4_setdc(unsigned int cpu, unsigned int newstate)
{
	u32 l, h;
	cpumask_t cpus_allowed, affected_cpu_map;
	struct cpufreq_freqs freqs;
	int hyperthreading = 0;
	int sibling = 0;

	if (!cpu_online(cpu) || (newstate > DC_DISABLE) || 
		(newstate == DC_RESV))
		return -EINVAL;

	/* switch to physical CPU where state is to be changed*/
	cpus_allowed = current->cpus_allowed;

	/* only run on CPU to be set, or on its sibling */
       affected_cpu_map = cpumask_of_cpu(cpu);
#ifdef CONFIG_X86_HT
	hyperthreading = ((cpu_has_ht) && (smp_num_siblings == 2));
	if (hyperthreading) {
		sibling = cpu_sibling_map[cpu];
                cpu_set(sibling, affected_cpu_map);
	}
#endif
	set_cpus_allowed(current, affected_cpu_map);
        BUG_ON(!cpu_isset(smp_processor_id(), affected_cpu_map));

	/* get current state */
	rdmsr(MSR_IA32_THERM_CONTROL, l, h);
	if (l & 0x10) {
		l = l >> 1;
		l &= 0x7;
	} else
		l = DC_DISABLE;
	
	if (l == newstate) {
		set_cpus_allowed(current, cpus_allowed);
		return 0;
	} else if (l == DC_RESV) {
		printk(KERN_ERR PFX "BIG FAT WARNING: currently in invalid setting\n");
	}

	/* notifiers */
	freqs.old = stock_freq * l / 8;
	freqs.new = stock_freq * newstate / 8;
	freqs.cpu = cpu;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	if (hyperthreading) {
		freqs.cpu = sibling;
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	}

	rdmsr(MSR_IA32_THERM_STATUS, l, h);
#if 0
	if (l & 0x01)
		printk(KERN_DEBUG PFX "CPU#%d currently thermal throttled\n", cpu);
#endif
	if (has_N44_O17_errata[cpu] && (newstate == DC_25PT || newstate == DC_DFLT))
		newstate = DC_38PT;

	rdmsr(MSR_IA32_THERM_CONTROL, l, h);
	if (newstate == DC_DISABLE) {
		/* printk(KERN_INFO PFX "CPU#%d disabling modulation\n", cpu); */
		wrmsr(MSR_IA32_THERM_CONTROL, l & ~(1<<4), h);
	} else {
		/* printk(KERN_INFO PFX "CPU#%d setting duty cycle to %d%%\n",
			cpu, ((125 * newstate) / 10)); */
		/* bits 63 - 5	: reserved 
		 * bit  4	: enable/disable
		 * bits 3-1	: duty cycle
		 * bit  0	: reserved
		 */
		l = (l & ~14);
		l = l | (1<<4) | ((newstate & 0x7)<<1);
		wrmsr(MSR_IA32_THERM_CONTROL, l, h);
	}

	set_cpus_allowed(current, cpus_allowed);

	/* notifiers */
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	if (hyperthreading) {
		freqs.cpu = cpu;
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	}

	return 0;
}


static struct cpufreq_frequency_table p4clockmod_table[] = {
	{DC_RESV, CPUFREQ_ENTRY_INVALID},
	{DC_DFLT, 0},
	{DC_25PT, 0},
	{DC_38PT, 0},
	{DC_50PT, 0},
	{DC_64PT, 0},
	{DC_75PT, 0},
	{DC_88PT, 0},
	{DC_DISABLE, 0},
	{DC_RESV, CPUFREQ_TABLE_END},
};


static int cpufreq_p4_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	unsigned int    newstate = DC_RESV;

	if (cpufreq_frequency_table_target(policy, &p4clockmod_table[0], target_freq, relation, &newstate))
		return -EINVAL;

	cpufreq_p4_setdc(policy->cpu, p4clockmod_table[newstate].index);

	return 0;
}


static int cpufreq_p4_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, &p4clockmod_table[0]);
}


static unsigned int cpufreq_p4_get_frequency(struct cpuinfo_x86 *c)
{
	if ((c->x86 == 0x06) && (c->x86_model == 0x09)) {
		/* Pentium M */
		printk(KERN_WARNING PFX "Warning: Pentium M detected. "
		       "The speedstep_centrino module offers voltage scaling"
		       " in addition of frequency scaling. You should use "
		       "that instead of p4-clockmod, if possible.\n");
		return speedstep_get_processor_frequency(SPEEDSTEP_PROCESSOR_PM);
	}

	if (c->x86 != 0xF) {
		printk(KERN_WARNING PFX "Unknown p4-clockmod-capable CPU. Please send an e-mail to <linux@brodo.de>\n");
		return 0;
	}

	/* on P-4s, the TSC runs with constant frequency independent wether
	 * throttling is active or not. */
	p4clockmod_driver.flags |= CPUFREQ_CONST_LOOPS;

	if (speedstep_detect_processor() == SPEEDSTEP_PROCESSOR_P4M) {
		printk(KERN_WARNING PFX "Warning: Pentium 4-M detected. "
		       "The speedstep-ich or acpi cpufreq modules offer "
		       "voltage scaling in addition of frequency scaling. "
		       "You should use either one instead of p4-clockmod, "
		       "if possible.\n");
		return speedstep_get_processor_frequency(SPEEDSTEP_PROCESSOR_P4M);
	}

	return speedstep_get_processor_frequency(SPEEDSTEP_PROCESSOR_P4D);
}

 

static int cpufreq_p4_cpu_init(struct cpufreq_policy *policy)
{
	struct cpuinfo_x86 *c = &cpu_data[policy->cpu];
	int cpuid = 0;
	unsigned int i;

	/* Errata workaround */
	cpuid = (c->x86 << 8) | (c->x86_model << 4) | c->x86_mask;
	switch (cpuid) {
	case 0x0f07:
	case 0x0f0a:
	case 0x0f11:
	case 0x0f12:
		has_N44_O17_errata[policy->cpu] = 1;
	}
	
	/* get max frequency */
	stock_freq = cpufreq_p4_get_frequency(c);
	if (!stock_freq)
		return -EINVAL;

	/* table init */
	for (i=1; (p4clockmod_table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if ((i<2) && (has_N44_O17_errata[policy->cpu]))
			p4clockmod_table[i].frequency = CPUFREQ_ENTRY_INVALID;
		else
			p4clockmod_table[i].frequency = (stock_freq * i)/8;
	}
	cpufreq_frequency_table_get_attr(p4clockmod_table, policy->cpu);
	
	/* cpuinfo and default policy values */
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = 1000000; /* assumed */
	policy->cur = stock_freq;

	return cpufreq_frequency_table_cpuinfo(policy, &p4clockmod_table[0]);
}


static int cpufreq_p4_cpu_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_put_attr(policy->cpu);    
	return 0;
}

static unsigned int cpufreq_p4_get(unsigned int cpu)
{
	unsigned int hyperthreading;
	cpumask_t cpus_allowed, affected_cpu_map;
	u32 l, h;

	hyperthreading = 0;

	/* only run on CPU to be set, or on its sibling */
	cpus_allowed = current->cpus_allowed;
	affected_cpu_map = cpumask_of_cpu(cpu);
#ifdef CONFIG_X86_HT
	hyperthreading = ((cpu_has_ht) && (smp_num_siblings == 2));
	if (hyperthreading) {
		sibling = cpu_sibling_map[cpu];
		cpu_set(sibling, affected_cpu_map);
	}
#endif
	set_cpus_allowed(current, affected_cpu_map);
        BUG_ON(!cpu_isset(smp_processor_id(), affected_cpu_map));

	rdmsr(MSR_IA32_THERM_CONTROL, l, h);

	set_cpus_allowed(current, cpus_allowed);

	if (l & 0x10) {
		l = l >> 1;
		l &= 0x7;
	} else
		l = DC_DISABLE;

	if (l != DC_DISABLE)
		return (stock_freq * l / 8);

	return stock_freq;
}

static struct freq_attr* p4clockmod_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver p4clockmod_driver = {
	.verify 	= cpufreq_p4_verify,
	.target		= cpufreq_p4_target,
	.init		= cpufreq_p4_cpu_init,
	.exit		= cpufreq_p4_cpu_exit,
	.get		= cpufreq_p4_get,
	.name		= "p4-clockmod",
	.owner		= THIS_MODULE,
	.attr		= p4clockmod_attr,
};


static int __init cpufreq_p4_init(void)
{	
	struct cpuinfo_x86 *c = cpu_data;

	/*
	 * THERM_CONTROL is architectural for IA32 now, so 
	 * we can rely on the capability checks
	 */
	if (c->x86_vendor != X86_VENDOR_INTEL)
		return -ENODEV;

	if (!test_bit(X86_FEATURE_ACPI, c->x86_capability) ||
		!test_bit(X86_FEATURE_ACC, c->x86_capability))
		return -ENODEV;

	printk(KERN_INFO PFX "P4/Xeon(TM) CPU On-Demand Clock Modulation available\n");

	return cpufreq_register_driver(&p4clockmod_driver);
}


static void __exit cpufreq_p4_exit(void)
{
	cpufreq_unregister_driver(&p4clockmod_driver);
}


MODULE_AUTHOR ("Zwane Mwaikambo <zwane@commfireservices.com>");
MODULE_DESCRIPTION ("cpufreq driver for Pentium(TM) 4/Xeon(TM)");
MODULE_LICENSE ("GPL");

late_initcall(cpufreq_p4_init);
module_exit(cpufreq_p4_exit);

