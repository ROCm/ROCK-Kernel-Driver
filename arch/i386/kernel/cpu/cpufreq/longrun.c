/*
 *  $Id: longrun.c,v 1.12 2002/09/29 23:43:10 db Exp $
 *
 * (C) 2002  Dominik Brodowski <linux@brodo.de>
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/cpufreq.h>

#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/timex.h>

static struct cpufreq_driver	*longrun_driver;

/**
 * longrun_{low,high}_freq is needed for the conversion of cpufreq kHz 
 * values into per cent values. In TMTA microcode, the following is valid:
 * performance_pctg = (current_freq - low_freq)/(high_freq - low_freq)
 */
static unsigned int longrun_low_freq, longrun_high_freq;


/**
 * longrun_get_policy - get the current LongRun policy
 * @policy: struct cpufreq_policy where current policy is written into
 *
 * Reads the current LongRun policy by access to MSR_TMTA_LONGRUN_FLAGS
 * and MSR_TMTA_LONGRUN_CTRL
 */
static void longrun_get_policy(struct cpufreq_policy *policy)
{
	u32 msr_lo, msr_hi;

	if (!longrun_driver)
		return;

	rdmsr(MSR_TMTA_LONGRUN_FLAGS, msr_lo, msr_hi);
	if (msr_lo & 0x01)
		policy->policy = CPUFREQ_POLICY_PERFORMANCE;
	else
		policy->policy = CPUFREQ_POLICY_POWERSAVE;
	
	rdmsr(MSR_TMTA_LONGRUN_CTRL, msr_lo, msr_hi);
	msr_lo &= 0x0000007F;
	msr_hi &= 0x0000007F;

	policy->min = longrun_low_freq + msr_lo * 
		((longrun_high_freq - longrun_low_freq) / 100);
	policy->min = longrun_low_freq + msr_hi * 
		((longrun_high_freq - longrun_low_freq) / 100);
	policy->cpu = 0;
}


/**
 * longrun_set_policy - sets a new CPUFreq policy
 * @policy - new policy
 *
 * Sets a new CPUFreq policy on LongRun-capable processors. This function
 * has to be called with cpufreq_driver locked.
 */
static void longrun_set_policy(struct cpufreq_policy *policy)
{
	u32 msr_lo, msr_hi;
	u32 pctg_lo, pctg_hi;

	if (!longrun_driver || !policy)
		return;

	pctg_lo = (policy->min - longrun_low_freq) / 
		((longrun_high_freq - longrun_low_freq) / 100);
	pctg_hi = (policy->max - longrun_low_freq) / 
		((longrun_high_freq - longrun_low_freq) / 100);

	if (pctg_hi > 100)
		pctg_hi = 100;
	if (pctg_lo > pctg_hi)
		pctg_lo = pctg_hi;

	/* performance or economy mode */
	rdmsr(MSR_TMTA_LONGRUN_FLAGS, msr_lo, msr_hi);
	msr_lo &= 0xFFFFFFFE;
	switch (policy->policy) {
	case CPUFREQ_POLICY_PERFORMANCE:
		msr_lo |= 0x00000001;
		break;
	case CPUFREQ_POLICY_POWERSAVE:
		break;
	}
	wrmsr(MSR_TMTA_LONGRUN_FLAGS, msr_lo, msr_hi);

	/* lower and upper boundary */
	rdmsr(MSR_TMTA_LONGRUN_CTRL, msr_lo, msr_hi);
	msr_lo &= 0xFFFFFF80;
	msr_hi &= 0xFFFFFF80;
	msr_lo |= pctg_lo;
	msr_hi |= pctg_hi;
	wrmsr(MSR_TMTA_LONGRUN_CTRL, msr_lo, msr_hi);

	return;
}


/**
 * longrun_verify_poliy - verifies a new CPUFreq policy
 *
 * Validates a new CPUFreq policy. This function has to be called with 
 * cpufreq_driver locked.
 */
static void longrun_verify_policy(struct cpufreq_policy *policy)
{
	if (!policy || !longrun_driver)
		return;

	policy->cpu = 0;
	cpufreq_verify_within_limits(policy, 0, 
		longrun_driver->policy[0].max_cpu_freq);

	return;
}


/**
 * longrun_determine_freqs - determines the lowest and highest possible core frequency
 *
 * Determines the lowest and highest possible core frequencies on this CPU.
 * This is neccessary to calculate the performance percentage according to
 * TMTA rules:
 * performance_pctg = (target_freq - low_freq)/(high_freq - low_freq)
 */
static unsigned int __init longrun_determine_freqs(unsigned int *low_freq, 
						   unsigned int *high_freq)
{
	u32 msr_lo, msr_hi;
	u32 save_lo, save_hi;
	u32 eax, ebx, ecx, edx;
	struct cpuinfo_x86 *c = cpu_data;

	if (!low_freq || !high_freq)
		return -EINVAL;

	if (cpu_has(c, X86_FEATURE_LRTI)) {
		/* if the LongRun Table Interface is present, the
		 * detection is a bit easier: 
		 * For minimum frequency, read out the maximum
		 * level (msr_hi), write that into "currently 
		 * selected level", and read out the frequency.
		 * For maximum frequency, read out level zero.
		 */
		/* minimum */
		rdmsr(MSR_TMTA_LRTI_READOUT, msr_lo, msr_hi);
		wrmsr(MSR_TMTA_LRTI_READOUT, msr_hi, msr_hi);
		rdmsr(MSR_TMTA_LRTI_VOLT_MHZ, msr_lo, msr_hi);
		*low_freq = msr_lo * 1000; /* to kHz */

		/* maximum */
		wrmsr(MSR_TMTA_LRTI_READOUT, 0, msr_hi);
		rdmsr(MSR_TMTA_LRTI_VOLT_MHZ, msr_lo, msr_hi);
		*high_freq = msr_lo * 1000; /* to kHz */

		if (*low_freq > *high_freq)
			*low_freq = *high_freq;
		return 0;
	}

	/* set the upper border to the value determined during TSC init */
	*high_freq = (cpu_khz / 1000);
	*high_freq = *high_freq * 1000;

	/* get current borders */
	rdmsr(MSR_TMTA_LONGRUN_CTRL, msr_lo, msr_hi);
	save_lo = msr_lo & 0x0000007F;
	save_hi = msr_hi & 0x0000007F;

	/* if current perf_pctg is larger than 90%, we need to decrease the
	 * upper limit to make the calculation more accurate.
	 */
	cpuid(0x80860007, &eax, &ebx, &ecx, &edx);
	if (ecx > 90) {
		/* set to 0 to 80 perf_pctg */
		msr_lo &= 0xFFFFFF80;
		msr_hi &= 0xFFFFFF80;
		msr_lo |= 0;
		msr_hi |= 80;
		wrmsr(MSR_TMTA_LONGRUN_CTRL, msr_lo, msr_hi);

		/* read out current core MHz and current perf_pctg */
		cpuid(0x80860007, &eax, &ebx, &ecx, &edx);

		/* restore values */
		wrmsr(MSR_TMTA_LONGRUN_CTRL, save_lo, save_hi);	
	}

	/* performance_pctg = (current_freq - low_freq)/(high_freq - low_freq)
	 * eqals
	 * low_freq * ( 1 - perf_pctg) = (cur_freq - high_freq * perf_pctg)
	 *
	 * high_freq * perf_pctg is stored tempoarily into "ebx".
	 */
	ebx = (((cpu_khz / 1000) * ecx) / 100); /* to MHz */

	if ((ecx > 95) || (ecx == 0) || (eax < ebx))
		return -EIO;

	edx = (eax - ebx) / (100 - ecx); 
	*low_freq = edx * 1000; /* back to kHz */

	if (*low_freq > *high_freq)
		*low_freq = *high_freq;

	return 0;
}


/**
 * longrun_init - initializes the Transmeta Crusoe LongRun CPUFreq driver
 *
 * Initializes the LongRun support.
 */
static int __init longrun_init(void)
{
	int                     result;
	struct cpufreq_driver   *driver;
	struct cpuinfo_x86 *c = cpu_data;

	if (c->x86_vendor != X86_VENDOR_TRANSMETA || 
	    !cpu_has(c, X86_FEATURE_LONGRUN))
		return 0;

	/* initialization of main "cpufreq" code*/
	driver = kmalloc(sizeof(struct cpufreq_driver) + 
			 NR_CPUS * sizeof(struct cpufreq_policy), GFP_KERNEL);
	if (!driver)
		return -ENOMEM;

	driver->policy = (struct cpufreq_policy *) (driver + 1);

	if (longrun_determine_freqs(&longrun_low_freq, &longrun_high_freq)) {
		kfree(driver);
		return -EIO;
	}
	driver->policy[0].max_cpu_freq  = longrun_high_freq;

	longrun_get_policy(&driver->policy[0]);

	driver->verify         = &longrun_verify_policy;
	driver->setpolicy      = &longrun_set_policy;
	result = cpufreq_register(driver);
	if (result) {
		kfree(driver);
		return result;
	}
	longrun_driver = driver;

	return 0;
}


/**
 * longrun_exit - unregisters LongRun support
 */
static void __exit longrun_exit(void)
{
	if (longrun_driver) {
		cpufreq_unregister();
		kfree(longrun_driver);
	}
}


MODULE_AUTHOR ("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("LongRun driver for Transmeta Crusoe processors.");
MODULE_LICENSE ("GPL");
module_init(longrun_init);
module_exit(longrun_exit);
