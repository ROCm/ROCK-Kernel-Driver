/*
 *  $Id: powernow-k6.c,v 1.33 2002/09/29 23:43:11 db Exp $
 *  This file was part of Powertweak Linux (http://powertweak.sf.net)
 *  and is shared with the Linux Kernel module.
 *
 *  (C) 2000-2002  Dave Jones, Arjan van de Ven, Janne Pänkälä, Dominik Brodowski.
 *
 *  Licensed under the terms of the GNU GPL License version 2.
 *
 *  BIG FAT DISCLAIMER: Work in progress code. Possibly *dangerous*
 */

#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/ioport.h>
#include <linux/slab.h>

#include <asm/msr.h>
#include <asm/timex.h>
#include <asm/io.h>


#define POWERNOW_IOPORT 0xfff0         /* it doesn't matter where, as long
					  as it is unused */

static struct cpufreq_driver		*powernow_driver;
static unsigned int                     busfreq;   /* FSB, in 10 kHz */
static unsigned int                     max_multiplier;


/* Clock ratio multiplied by 10 - see table 27 in AMD#23446 */
static int clock_ratio[8] = {
	45,  /* 000 -> 4.5x */
	50,  /* 001 -> 5.0x */
	40,  /* 010 -> 4.0x */
	55,  /* 011 -> 5.5x */
	20,  /* 100 -> 2.0x */
	30,  /* 101 -> 3.0x */
	60,  /* 110 -> 6.0x */
	35   /* 111 -> 3.5x */
};


/**
 * powernow_k6_get_cpu_multiplier - returns the current FSB multiplier
 *
 *   Returns the current setting of the frequency multiplier. Core clock
 * speed is frequency of the Front-Side Bus multiplied with this value.
 */
static int powernow_k6_get_cpu_multiplier(void)
{
	u64             invalue = 0;
	u32             msrval;
	
	msrval = POWERNOW_IOPORT + 0x1;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* enable the PowerNow port */
	invalue=inl(POWERNOW_IOPORT + 0x8);
	msrval = POWERNOW_IOPORT + 0x0;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* disable it again */

	return clock_ratio[(invalue >> 5)&7];
}


/**
 * powernow_k6_set_state - set the PowerNow! multiplier
 * @best_i: clock_ratio[best_i] is the target multiplier
 *
 *   Tries to change the PowerNow! multiplier
 */
static void powernow_k6_set_state (unsigned int best_i)
{
	unsigned long           outvalue=0, invalue=0;
	unsigned long           msrval;
	struct cpufreq_freqs    freqs;

	if (!powernow_driver) {
		printk(KERN_ERR "cpufreq: initialization problem or invalid target frequency\n");
		return;
	}

	freqs.old = busfreq * powernow_k6_get_cpu_multiplier();
	freqs.new = busfreq * clock_ratio[best_i];
	freqs.cpu = CPUFREQ_ALL_CPUS; /* powernow-k6.c is UP only driver */
	
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	/* we now need to transform best_i to the BVC format, see AMD#23446 */

	outvalue = (1<<12) | (1<<10) | (1<<9) | (best_i<<5);

	msrval = POWERNOW_IOPORT + 0x1;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* enable the PowerNow port */
	invalue=inl(POWERNOW_IOPORT + 0x8);
	invalue = invalue & 0xf;
	outvalue = outvalue | invalue;
	outl(outvalue ,(POWERNOW_IOPORT + 0x8));
	msrval = POWERNOW_IOPORT + 0x0;
	wrmsr(MSR_K6_EPMR, msrval, 0); /* disable it again */

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return;
}


/**
 * powernow_k6_verify - verifies a new CPUfreq policy
 * @policy: new policy
 *
 * Policy must be within lowest and highest possible CPU Frequency,
 * and at least one possible state must be within min and max.
 */
static void powernow_k6_verify(struct cpufreq_policy *policy)
{
	unsigned int    number_states = 0;
	unsigned int    i, j;

	if (!policy || !busfreq)
		return;

	policy->cpu = 0;
	cpufreq_verify_within_limits(policy, (20 * busfreq),
				     (max_multiplier * busfreq));

	for (i=0; i<8; i++)
		if ((policy->min <= (busfreq * clock_ratio[i])) &&
		    (policy->max >= (busfreq * clock_ratio[i])))
			number_states++;

	if (number_states)
		return;

	/* no state is available within range -- find next larger state */

	j = 6;

	for (i=0; i<8; i++)
		if (((clock_ratio[i] * busfreq) >= policy->min) &&
		    (clock_ratio[i] < clock_ratio[j]))
			j = i;

	policy->max = clock_ratio[j] * busfreq;

	return;
}


/**
 * powernow_k6_setpolicy - sets a new CPUFreq policy
 * @policy - new policy
 *
 * sets a new CPUFreq policy
 */
static void powernow_k6_setpolicy (struct cpufreq_policy *policy)
{
	unsigned int    number_states = 0;
	unsigned int    i, j=4;

	if (!powernow_driver)
		return;

	for (i=0; i<8; i++)
		if ((policy->min <= (busfreq * clock_ratio[i])) &&
		    (policy->max >= (busfreq * clock_ratio[i])))
		{
			number_states++;
			j = i;
		}

	if (number_states == 1) {
		/* if only one state is within the limit borders, it
		   is easily detected and set */
		powernow_k6_set_state(j);
		return;
	}

	/* more than one state within limit */
	switch (policy->policy) {
	case CPUFREQ_POLICY_POWERSAVE:
		j = 6;
		for (i=0; i<8; i++)
		if ((policy->min <= (busfreq * clock_ratio[i])) &&
		    (policy->max >= (busfreq * clock_ratio[i])) &&
			    (clock_ratio[i] < clock_ratio[j]))
				j = i;
		break;
	case CPUFREQ_POLICY_PERFORMANCE:
		j = 4;
		for (i=0; i<8; i++)
		if ((policy->min <= (busfreq * clock_ratio[i])) &&
		    (policy->max >= (busfreq * clock_ratio[i])) &&
			    (clock_ratio[i] > clock_ratio[j]))
				j = i;
		break;
	default:
		return;
	}

	if (clock_ratio[i] > max_multiplier)
		BUG();

	powernow_k6_set_state(j);
	return;
}


/**
 * powernow_k6_init - initializes the k6 PowerNow! CPUFreq driver
 *
 *   Initializes the K6 PowerNow! support. Returns -ENODEV on unsupported
 * devices, -EINVAL or -ENOMEM on problems during initiatization, and zero
 * on success.
 */
static int __init powernow_k6_init(void)
{	
	struct cpuinfo_x86      *c = cpu_data;
	struct cpufreq_driver   *driver;
	unsigned int            result;

	if ((c->x86_vendor != X86_VENDOR_AMD) || (c->x86 != 5) ||
		((c->x86_model != 12) && (c->x86_model != 13)))
		return -ENODEV;

	max_multiplier = powernow_k6_get_cpu_multiplier();
	busfreq = cpu_khz / max_multiplier;

	if (!request_region(POWERNOW_IOPORT, 16, "PowerNow!")) {
		printk("cpufreq: PowerNow IOPORT region already used.\n");
		return -EIO;
	}

	/* initialization of main "cpufreq" code*/
	driver = kmalloc(sizeof(struct cpufreq_driver) +
			 NR_CPUS * sizeof(struct cpufreq_freqs), GFP_KERNEL);
	if (!driver) {
		release_region (POWERNOW_IOPORT, 16);
		return -ENOMEM;
	}
	driver->policy = (struct cpufreq_policy *) (driver + 1);

#ifdef CONFIG_CPU_FREQ_24_API
	driver->cpu_min_freq     = busfreq * 20;
	driver->cpu_cur_freq[0]  = busfreq * max_multiplier;
#endif

	driver->verify        = &powernow_k6_verify;
	driver->setpolicy     = &powernow_k6_setpolicy;

	driver->policy[0].cpu    = 0;
	driver->policy[0].min    = busfreq * 20;
	driver->policy[0].max    = busfreq * max_multiplier;
	driver->policy[0].policy = CPUFREQ_POLICY_PERFORMANCE;
	driver->policy[0].max_cpu_freq  = busfreq * max_multiplier;


	result = cpufreq_register(driver);
	if (result) {
		release_region (POWERNOW_IOPORT, 16);
		kfree(driver);
		return result;
	}
	powernow_driver = driver;

	return 0;
}


/**
 * powernow_k6_exit - unregisters AMD K6-2+/3+ PowerNow! support
 *
 *   Unregisters AMD K6-2+ / K6-3+ PowerNow! support.
 */
static void __exit powernow_k6_exit(void)
{
	unsigned int i;

	if (powernow_driver) {
		for (i=0;i<8;i++)
			if (clock_ratio[i] == max_multiplier)
				powernow_k6_set_state(i);		
		cpufreq_unregister();
		kfree(powernow_driver);
	}
}


MODULE_AUTHOR ("Arjan van de Ven <arjanv@redhat.com>, Dave Jones <davej@suse.de>, Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION ("PowerNow! driver for AMD K6-2+ / K6-3+ processors.");
MODULE_LICENSE ("GPL");
module_init(powernow_k6_init);
module_exit(powernow_k6_exit);
