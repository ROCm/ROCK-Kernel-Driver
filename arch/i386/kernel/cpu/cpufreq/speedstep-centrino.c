/*
 * cpufreq driver for Enhanced SpeedStep, as found in Intel's Pentium
 * M (part of the Centrino chipset).
 *
 * Despite the "SpeedStep" in the name, this is almost entirely unlike
 * traditional SpeedStep.
 *
 * Modelled on speedstep.c
 *
 * Copyright (C) 2003 Jeremy Fitzhardinge <jeremy@goop.org>
 *
 * WARNING WARNING WARNING
 * 
 * This driver manipulates the PERF_CTL MSR, which is only somewhat
 * documented.  While it seems to work on my laptop, it has not been
 * tested anywhere else, and it may not work for you, do strange
 * things or simply crash.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/cpufeature.h>

#define PFX		"speedstep-centrino: "
#define MAINTAINER	"Jeremy Fitzhardinge <jeremy@goop.org>"

/*#define CENTRINO_DEBUG*/

#ifdef CENTRINO_DEBUG
#define dprintk(msg...) printk(msg)
#else
#define dprintk(msg...) do { } while(0)
#endif

struct cpu_model
{
	const char	*model_name;
	unsigned	max_freq; /* max clock in kHz */

	struct cpufreq_frequency_table *op_points; /* clock/voltage pairs */
};

/* Operating points for current CPU */
static const struct cpu_model *centrino_model;

/* Computes the correct form for IA32_PERF_CTL MSR for a particular
   frequency/voltage operating point; frequency in MHz, volts in mV.
   This is stored as "index" in the structure. */
#define OP(mhz, mv)							\
	{								\
		.frequency = (mhz) * 1000,				\
		.index = (((mhz)/100) << 8) | ((mv - 700) / 16)		\
	}

/* 
 * These voltage tables were derived from the Intel Pentium M
 * datasheet, document 25261202.pdf, Table 5.  I have verified they
 * are consistent with my IBM ThinkPad X31, which has a 1.3GHz Pentium
 * M.
 */

/* Ultra Low Voltage Intel Pentium M processor 900MHz */
static struct cpufreq_frequency_table op_900[] =
{
	OP(600,  844),
	OP(800,  988),
	OP(900, 1004),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Low Voltage Intel Pentium M processor 1.10GHz */
static struct cpufreq_frequency_table op_1100[] =
{
	OP( 600,  956),
	OP( 800, 1020),
	OP( 900, 1100),
	OP(1000, 1164),
	OP(1100, 1180),
	{ .frequency = CPUFREQ_TABLE_END }
};


/* Low Voltage Intel Pentium M processor 1.20GHz */
static struct cpufreq_frequency_table op_1200[] =
{
	OP( 600,  956),
	OP( 800, 1004),
	OP( 900, 1020),
	OP(1000, 1100),
	OP(1100, 1164),
	OP(1200, 1180),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.30GHz */
static struct cpufreq_frequency_table op_1300[] = 
{
	OP( 600,  956),
	OP( 800, 1260),
	OP(1000, 1292),
	OP(1200, 1356),
	OP(1300, 1388),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.40GHz */
static struct cpufreq_frequency_table op_1400[] = 
{
	OP( 600,  956),
	OP( 800, 1180),
	OP(1000, 1308),
	OP(1200, 1436),
	OP(1400, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.50GHz */
static struct cpufreq_frequency_table op_1500[] = 
{
	OP( 600,  956),
	OP( 800, 1116),
	OP(1000, 1228),
	OP(1200, 1356),
	OP(1400, 1452),
	OP(1500, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.60GHz */
static struct cpufreq_frequency_table op_1600[] = 
{
	OP( 600,  956),
	OP( 800, 1036),
	OP(1000, 1164),
	OP(1200, 1276),
	OP(1400, 1420),
	OP(1600, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};

/* Intel Pentium M processor 1.70GHz */
static struct cpufreq_frequency_table op_1700[] =
{
	OP( 600,  956),
	OP( 800, 1004),
	OP(1000, 1116),
	OP(1200, 1228),
	OP(1400, 1308),
	OP(1700, 1484),
	{ .frequency = CPUFREQ_TABLE_END }
};
#undef OP

#define _CPU(max, name)	\
	{ "Intel(R) Pentium(R) M processor " name "MHz", (max)*1000, op_##max }
#define CPU(max)	_CPU(max, #max)

/* CPU models, their operating frequency range, and freq/voltage
   operating points */
static const struct cpu_model models[] = 
{
       _CPU( 900, " 900"),
	CPU(1100),
	CPU(1200),
	CPU(1300),
	CPU(1400),
	CPU(1500),
	CPU(1600),
	CPU(1700),
	{ 0, }
};
#undef CPU

/* Extract clock in kHz from PERF_CTL value */
static unsigned extract_clock(unsigned msr)
{
	msr = (msr >> 8) & 0xff;
	return msr * 100000;
}

/* Return the current CPU frequency in kHz */
static unsigned get_cur_freq(void)
{
	unsigned l, h;

	rdmsr(MSR_IA32_PERF_STATUS, l, h);
	return extract_clock(l);
}

static int centrino_cpu_init(struct cpufreq_policy *policy)
{
	unsigned freq;

	if (policy->cpu != 0 || centrino_model == NULL)
		return -ENODEV;

	freq = get_cur_freq();

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cpuinfo.transition_latency = 10; /* 10uS transition latency */
	policy->cur = freq;

	dprintk(KERN_INFO PFX "centrino_cpu_init: policy=%d cur=%dkHz\n",
		policy->policy, policy->cur);
	
	return cpufreq_frequency_table_cpuinfo(policy, centrino_model->op_points);
}

/**
 * centrino_verify - verifies a new CPUFreq policy
 * @freq: new policy
 *
 * Limit must be within this model's frequency range at least one
 * border included.
 */
static int centrino_verify (struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, centrino_model->op_points);
}

/**
 * centrino_setpolicy - set a new CPUFreq policy
 * @policy: new policy
 *
 * Sets a new CPUFreq policy.
 */
static int centrino_target (struct cpufreq_policy *policy,
			    unsigned int target_freq,
			    unsigned int relation)
{
	unsigned int    newstate = 0;
	unsigned int	msr, oldmsr, h;
	struct cpufreq_freqs	freqs;

	if (centrino_model == NULL)
		return -ENODEV;

	if (cpufreq_frequency_table_target(policy, centrino_model->op_points, target_freq,
					   relation, &newstate))
		return -EINVAL;

	msr = centrino_model->op_points[newstate].index;
	rdmsr(MSR_IA32_PERF_CTL, oldmsr, h);

	if (msr == (oldmsr & 0xffff))
		return 0;

	/* Hm, old frequency can either be the last value we put in
	   PERF_CTL, or whatever it is now. The trouble is that TM2
	   can change it behind our back, which means we never get to
	   see the speed change.  Reading back the current speed would
	   tell us something happened, but it may leave the things on
	   the notifier chain confused; we therefore stick to using
	   the last programmed speed rather than the current speed for
	   "old". 

	   TODO: work out how the TCC interrupts work, and try to
	   catch the CPU changing things under us.
	*/
	freqs.cpu = 0;
	freqs.old = extract_clock(oldmsr);
	freqs.new = extract_clock(msr);
	
	dprintk(KERN_INFO PFX "target=%dkHz old=%d new=%d msr=%04x\n",
		target_freq, freqs.old, freqs.new, msr);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);	

	/* all but 16 LSB are "reserved", so treat them with
	   care */
	oldmsr &= ~0xffff;
	msr &= 0xffff;
	oldmsr |= msr;
	
	wrmsr(MSR_IA32_PERF_CTL, oldmsr, h);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static struct cpufreq_driver centrino_driver = {
	.name		= "centrino", /* should be speedstep-centrino, 
					 but there's a 16 char limit */
	.init		= centrino_cpu_init,
	.verify 	= centrino_verify,
	.target 	= centrino_target,
	.owner		= THIS_MODULE,
};


/**
 * centrino_init - initializes the Enhanced SpeedStep CPUFreq driver
 *
 * Initializes the Enhanced SpeedStep support. Returns -ENODEV on
 * unsupported devices, -ENOENT if there's no voltage table for this
 * particular CPU model, -EINVAL on problems during initiatization,
 * and zero on success.
 *
 * This is quite picky.  Not only does the CPU have to advertise the
 * "est" flag in the cpuid capability flags, we look for a specific
 * CPU model and stepping, and we need to have the exact model name in
 * our voltage tables.  That is, be paranoid about not releasing
 * someone's valuable magic smoke.
 */
static int __init centrino_init(void)
{
	struct cpuinfo_x86 *cpu = cpu_data;
	const struct cpu_model *model;
	unsigned l, h;

	if (!cpu_has(cpu, X86_FEATURE_EST))
		return -ENODEV;

	/* Only Intel Pentium M stepping 5 for now - add new CPUs as
	   they appear after making sure they use PERF_CTL in the same
	   way. */
	if (cpu->x86_vendor != X86_VENDOR_INTEL ||
	    cpu->x86        != 6 ||
	    cpu->x86_model  != 9 ||
	    cpu->x86_mask   != 5) {
		printk(KERN_INFO PFX "found unsupported CPU with Enhanced SpeedStep: "
		       "send /proc/cpuinfo to " MAINTAINER "\n");
		return -ENODEV;
	}

	/* Check to see if Enhanced SpeedStep is enabled, and try to
	   enable it if not. */
	rdmsr(MSR_IA32_MISC_ENABLE, l, h);
		
	if (!(l & (1<<16))) {
		l |= (1<<16);
		wrmsr(MSR_IA32_MISC_ENABLE, l, h);
		
		/* check to see if it stuck */
		rdmsr(MSR_IA32_MISC_ENABLE, l, h);
		if (!(l & (1<<16))) {
			printk(KERN_INFO PFX "couldn't enable Enhanced SpeedStep\n");
			return -ENODEV;
		}
	}

	for(model = models; model->model_name != NULL; model++)
		if (strcmp(cpu->x86_model_id, model->model_name) == 0)
			break;
	if (model->model_name == NULL) {
		printk(KERN_INFO PFX "no support for CPU model \"%s\": "
		       "send /proc/cpuinfo to " MAINTAINER "\n",
		       cpu->x86_model_id);
		return -ENOENT;
	}

	centrino_model = model;
		
	printk(KERN_INFO PFX "found \"%s\": max frequency: %dkHz\n",
	       model->model_name, model->max_freq);

	return cpufreq_register_driver(&centrino_driver);
}

static void __exit centrino_exit(void)
{
	cpufreq_unregister_driver(&centrino_driver);
}

MODULE_AUTHOR ("Jeremy Fitzhardinge <jeremy@goop.org>");
MODULE_DESCRIPTION ("Enhanced SpeedStep driver for Intel Pentium M processors.");
MODULE_LICENSE ("GPL");

module_init(centrino_init);
module_exit(centrino_exit);
