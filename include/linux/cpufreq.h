/*
 *  linux/include/linux/cpufreq.h
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 Dominik Brodowski <linux@brodo.de>
 *            
 *
 * $Id: cpufreq.h,v 1.26 2002/09/21 09:05:29 db Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_CPUFREQ_H
#define _LINUX_CPUFREQ_H

#include <linux/config.h>
#include <linux/notifier.h>
#include <linux/threads.h>


/*********************************************************************
 *                     CPUFREQ NOTIFIER INTERFACE                    *
 *********************************************************************/

int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list);
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list);

#define CPUFREQ_TRANSITION_NOTIFIER     (0)
#define CPUFREQ_POLICY_NOTIFIER         (1)

#define CPUFREQ_ALL_CPUS        ((NR_CPUS))


/********************** cpufreq policy notifiers *********************/

#define CPUFREQ_POLICY_POWERSAVE        (1)
#define CPUFREQ_POLICY_PERFORMANCE      (2)

/* values here are CPU kHz so that hardware which doesn't run with some
 * frequencies can complain without having to guess what per cent / per
 * mille means. */
struct cpufreq_policy {
	unsigned int            cpu;    /* cpu nr or CPUFREQ_ALL_CPUS */
	unsigned int            min;    /* in kHz */
	unsigned int            max;    /* in kHz */
        unsigned int            policy; /* see above */
	unsigned int            max_cpu_freq; /* for information */
};

#define CPUFREQ_ADJUST          (0)
#define CPUFREQ_INCOMPATIBLE    (1)
#define CPUFREQ_NOTIFY          (2)


/******************** cpufreq transition notifiers *******************/

#define CPUFREQ_PRECHANGE	(0)
#define CPUFREQ_POSTCHANGE	(1)

struct cpufreq_freqs {
	unsigned int cpu;      /* cpu nr or CPUFREQ_ALL_CPUS */
	unsigned int old;
	unsigned int new;
};


/**
 * cpufreq_scale - "old * mult / div" calculation for large values (32-bit-arch safe)
 * @old:   old value
 * @div:   divisor
 * @mult:  multiplier
 *
 * Needed for loops_per_jiffy and similar calculations.  We do it 
 * this way to avoid math overflow on 32-bit machines.  This will
 * become architecture dependent once high-resolution-timer is
 * merged (or any other thing that introduces sc_math.h).
 *
 *    new = old * mult / div
 */
static inline unsigned long cpufreq_scale(unsigned long old, u_int div, u_int mult)
{
	unsigned long val, carry;

	mult /= 100;
	div  /= 100;
        val   = (old / div) * mult;
        carry = old % div;
	carry = carry * mult / div;

	return carry + val;
};


/*********************************************************************
 *                      DYNAMIC CPUFREQ INTERFACE                    *
 *********************************************************************/
#ifdef CONFIG_CPU_FREQ_DYNAMIC
/* TBD */
#endif /* CONFIG_CPU_FREQ_DYNAMIC */


/*********************************************************************
 *                      CPUFREQ DRIVER INTERFACE                     *
 *********************************************************************/

typedef void (*cpufreq_policy_t)          (struct cpufreq_policy *policy);

struct cpufreq_driver {
	/* needed by all drivers */
	cpufreq_policy_t        verify;
	cpufreq_policy_t        setpolicy;
	struct cpufreq_policy   *policy;
#ifdef CONFIG_CPU_FREQ_DYNAMIC
	/* TBD */
#endif
	/* 2.4. compatible API */
#ifdef CONFIG_CPU_FREQ_24_API
	unsigned int            cpu_min_freq;
	unsigned int            cpu_cur_freq[NR_CPUS];
#endif
};

int cpufreq_register(struct cpufreq_driver *driver_data);
int cpufreq_unregister(void);

void cpufreq_notify_transition(struct cpufreq_freqs *freqs, unsigned int state);


static inline void cpufreq_verify_within_limits(struct cpufreq_policy *policy, unsigned int min, unsigned int max) 
{
	if (policy->min < min)
		policy->min = min;
	if (policy->max < min)
		policy->max = min;
	if (policy->min > max)
		policy->min = max;
	if (policy->max > max)
		policy->max = max;
	if (policy->min > policy->max)
		policy->min = policy->max;
	return;
}

/*********************************************************************
 *                        CPUFREQ 2.6. INTERFACE                     *
 *********************************************************************/
int cpufreq_set_policy(struct cpufreq_policy *policy);
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu);

#ifdef CONFIG_CPU_FREQ_26_API
#ifdef CONFIG_PM
int cpufreq_restore(void);
#endif
#endif


#endif /* _LINUX_CPUFREQ_H */
