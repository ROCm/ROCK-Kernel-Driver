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


#ifdef CONFIG_CPU_FREQ_24_API
/*********************************************************************
 *                        CPUFREQ 2.4. INTERFACE                     *
 *********************************************************************/
int cpufreq_setmax(unsigned int cpu);
#ifdef CONFIG_PM
int cpufreq_restore(void);
#endif
int cpufreq_set(unsigned int kHz, unsigned int cpu);
unsigned int cpufreq_get(unsigned int cpu);

/* /proc/sys/cpu */
enum {
	CPU_NR   = 1,           /* compatibilty reasons */
	CPU_NR_0 = 1,
	CPU_NR_1 = 2,
	CPU_NR_2 = 3,
	CPU_NR_3 = 4,
	CPU_NR_4 = 5,
	CPU_NR_5 = 6,
	CPU_NR_6 = 7,
	CPU_NR_7 = 8,
	CPU_NR_8 = 9,
	CPU_NR_9 = 10,
	CPU_NR_10 = 11,
	CPU_NR_11 = 12,
	CPU_NR_12 = 13,
	CPU_NR_13 = 14,
	CPU_NR_14 = 15,
	CPU_NR_15 = 16,
	CPU_NR_16 = 17,
	CPU_NR_17 = 18,
	CPU_NR_18 = 19,
	CPU_NR_19 = 20,
	CPU_NR_20 = 21,
	CPU_NR_21 = 22,
	CPU_NR_22 = 23,
	CPU_NR_23 = 24,
	CPU_NR_24 = 25,
	CPU_NR_25 = 26,
	CPU_NR_26 = 27,
	CPU_NR_27 = 28,
	CPU_NR_28 = 29,
	CPU_NR_29 = 30,
	CPU_NR_30 = 31,
	CPU_NR_31 = 32,
};

/* /proc/sys/cpu/{0,1,...,(NR_CPUS-1)} */
enum {
	CPU_NR_FREQ_MAX = 1,
	CPU_NR_FREQ_MIN = 2,
	CPU_NR_FREQ = 3,
};

#define CTL_CPU_VARS_SPEED_MAX { \
                ctl_name: CPU_NR_FREQ_MAX, \
                data: &cpu_max_freq, \
                procname: "speed-max", \
                maxlen:	sizeof(cpu_max_freq),\
                mode: 0444, \
                proc_handler: proc_dointvec, }

#define CTL_CPU_VARS_SPEED_MIN { \
                ctl_name: CPU_NR_FREQ_MIN, \
                data: &cpu_min_freq, \
                procname: "speed-min", \
                maxlen:	sizeof(cpu_min_freq),\
                mode: 0444, \
                proc_handler: proc_dointvec, }

#define CTL_CPU_VARS_SPEED(cpunr) { \
                ctl_name: CPU_NR_FREQ, \
                procname: "speed", \
                mode: 0644, \
                proc_handler: cpufreq_procctl, \
                strategy: cpufreq_sysctl, \
                extra1: (void*) (cpunr), }

#define CTL_TABLE_CPU_VARS(cpunr) static ctl_table ctl_cpu_vars_##cpunr[] = {\
                CTL_CPU_VARS_SPEED_MAX, \
                CTL_CPU_VARS_SPEED_MIN, \
                CTL_CPU_VARS_SPEED(cpunr),  \
                { ctl_name: 0, }, }

/* the ctl_table entry for each CPU */
#define CPU_ENUM(s) { \
                ctl_name: (CPU_NR + s), \
                procname: #s, \
                mode: 0555, \
                child: ctl_cpu_vars_##s }

#endif /* CONFIG_CPU_FREQ_24_API */

#endif /* _LINUX_CPUFREQ_H */
