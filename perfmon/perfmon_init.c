/*
 * perfmon.c: perfmon2 global initialization functions
 *
 * This file implements the perfmon2 interface which
 * provides access to the hardware performance counters
 * of the host processor.
 *
 *
 * The initial version of perfmon.c was written by
 * Ganesh Venkitachalam, IBM Corp.
 *
 * Then it was modified for perfmon-1.x by Stephane Eranian and
 * David Mosberger, Hewlett Packard Co.
 *
 * Version Perfmon-2.x is a complete rewrite of perfmon-1.x
 * by Stephane Eranian, Hewlett Packard Co.
 *
 * Copyright (c) 1999-2006 Hewlett-Packard Development Company, L.P.
 * Contributed by Stephane Eranian <eranian@hpl.hp.com>
 *                David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * More information about perfmon available at:
 * 	http://perfmon2.sf.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/perfmon_kern.h>
#include "perfmon_priv.h"

/*
 * external variables
 */
DEFINE_PER_CPU(struct task_struct *, pmu_owner);
DEFINE_PER_CPU(struct pfm_context  *, pmu_ctx);
DEFINE_PER_CPU(u64, pmu_activation_number);
DEFINE_PER_CPU(struct pfm_stats, pfm_stats);
DEFINE_PER_CPU(struct hrtimer, pfm_hrtimer);

EXPORT_PER_CPU_SYMBOL(pmu_ctx);

int perfmon_disabled;	/* >0 if perfmon is disabled */

/*
 * called from cpu_init() and pfm_pmu_register()
 */
void __pfm_init_percpu(void *dummy)
{
	struct hrtimer *h;

	h = &__get_cpu_var(pfm_hrtimer);

	pfm_arch_init_percpu();

	/*
	 * initialize per-cpu high res timer
	 */
	hrtimer_init(h, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
#ifdef CONFIG_HIGH_RES_TIMERS
	/*
	 * avoid potential deadlock on the runqueue lock
	 * during context switch when multiplexing. Situation
	 * arises on architectures which run switch_to() with
	 * the runqueue lock held, e.g., x86. On others, e.g.,
	 * IA-64, the problem does not exist.
	 * Setting the callback mode to HRTIMER_CB_IRQSAFE_UNOCKED
	 * such that the callback routine is only called on hardirq
	 * context not on softirq, thus the context switch will not
	 * end up trying to wakeup the softirqd
	 */
	//h->cb_mode = HRTIMER_CB_IRQSAFE_UNLOCKED;
#endif
	h->function = pfm_handle_switch_timeout;
}

/*
 * global initialization routine, executed only once
 */
int __init pfm_init(void)
{
	PFM_LOG("version %u.%u", PFM_VERSION_MAJ, PFM_VERSION_MIN);

	if (pfm_init_ctx())
		goto error_disable;


	if (pfm_init_sets())
		goto error_disable;

	if (pfm_init_sysfs())
		goto error_disable;

	if (pfm_init_control())
		goto error_disable;

	/* not critical, so no error checking */
	pfm_init_debugfs();

	/*
	 * one time, arch-specific global initialization
	 */
	if (pfm_arch_init())
		goto error_disable;

	if (pfm_init_hotplug())
		goto error_disable;
	return 0;

error_disable:
	PFM_ERR("perfmon is disabled due to initialization error");
	perfmon_disabled = 1;
	return -1;
}

/*
 * must use subsys_initcall() to ensure that the perfmon2 core
 * is initialized before any PMU description module when they are
 * compiled in.
 */
subsys_initcall(pfm_init);
