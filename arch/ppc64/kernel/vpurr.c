/*
 * PPC64 Cpu util performace monitoring.
 *
 * Manish Ahuja mahuja@us.ibm.com
 *    Copyright (c) 2004 Manish Ahuja IBM CORP.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *	This file will also report many of the perf values for 2.6 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/hvcall.h>
#include <asm/cputable.h>
#include "vpurr.h"

#define SAMPLE_TICK HZ

DEFINE_PER_CPU(struct cpu_util_store, cpu_util_sampler);

static void collect_startpurr(int cpu);

/*
 * This is a timer handler.  There is on per CPU. It gets scheduled
 * every SAMPLE_TICK ticks.
 */

static void util_timer_func(unsigned long data)
{
	struct cpu_util_store * cus = &__get_cpu_var(cpu_util_sampler);
	struct timer_list *tl = &cus->cpu_util_timer;

	cus->current_purr = mfspr(PURR);
	cus->tb = mftb();

	/*printk(KERN_INFO "PURR VAL %ld %lld %lld\n", data, cus->current_purr, cus->tb);*/

	mod_timer(tl, jiffies + SAMPLE_TICK);
}

/*
 * One time function that gets called when all the cpu's are online 
 * to start collection. It adds the timer to each cpu on the system.
 * start_purr is collected during smp_init time in __cpu_up code
 */

static void start_util_timer(int cpu)
{
	struct cpu_util_store * cus = &per_cpu(cpu_util_sampler, cpu);
	struct timer_list *tl = &cus->cpu_util_timer;

	if (tl->function != NULL)
		return;

	init_timer(tl);
	tl->expires = jiffies + SAMPLE_TICK;
	tl->data = cpu;
	tl->function = util_timer_func;
	add_timer_on(tl, cpu);
}

static int __init cpu_util_init(void)
{
	int cpu;

	if (PVR_VER(systemcfg->processor) == PV_POWER5) {
		for_each_online_cpu(cpu){
			collect_startpurr(cpu);
			start_util_timer(cpu);
		}
	}

	return 0;
}

__initcall(cpu_util_init);

/* Collect starting purr, to collect starting purr from the
 * cpu in question, we make a call to get that cpu and then run
 */

static void collect_startpurr(int cpu)
{
	struct cpu_util_store * cus = &per_cpu(cpu_util_sampler, cpu);	

	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	BUG_ON(smp_processor_id() != cpu);

	cus->start_purr = mfspr(PURR);
	cus->tb = mftb();
}

