/*
 * perfmon_hotplug.c: handling of CPU hotplug
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
#include <linux/perfmon_kern.h>
#include <linux/cpu.h>
#include "perfmon_priv.h"

#ifndef CONFIG_HOTPLUG_CPU
void pfm_cpu_disable(void)
{}

int __init pfm_init_hotplug(void)
{
	return 0;
}
#else /* CONFIG_HOTPLUG_CPU */
/*
 * CPU hotplug event nofication callback
 *
 * We use the callback to do manage the sysfs interface.
 * Note that the actual shutdown of monitoring on the CPU
 * is done in pfm_cpu_disable(), see comments there for more
 * information.
 */
static int pfm_cpu_notify(struct notifier_block *nfb,
			  unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	/* no PMU description loaded */
	if (pfm_pmu_conf_get(0))
		return NOTIFY_OK;

	switch (action) {
	case CPU_ONLINE:
		pfm_debugfs_add_cpu(cpu);
		PFM_INFO("CPU%d is online", cpu);
		break;
	case CPU_UP_PREPARE:
		PFM_INFO("CPU%d prepare online", cpu);
		break;
	case CPU_UP_CANCELED:
		pfm_debugfs_del_cpu(cpu);
		PFM_INFO("CPU%d is up canceled", cpu);
		break;
	case CPU_DOWN_PREPARE:
		PFM_INFO("CPU%d prepare offline", cpu);
		break;
	case CPU_DOWN_FAILED:
		PFM_INFO("CPU%d is down failed", cpu);
		break;
	case CPU_DEAD:
		pfm_debugfs_del_cpu(cpu);
		PFM_INFO("CPU%d is offline", cpu);
		break;
	}
	/*
	 * call PMU module handler if any
	 */
	if (pfm_pmu_conf->hotplug_handler)
		pfm_pmu_conf->hotplug_handler(action, cpu);

	pfm_pmu_conf_put();
	return NOTIFY_OK;
}

/*
 * called from cpu_disable() to detach the perfmon context
 * from the CPU going down.
 *
 * We cannot use the cpu hotplug notifier because we MUST run
 * on the CPU that is going down to save the PMU state
 */
void pfm_cpu_disable(void)
{
	struct pfm_context *ctx;
	unsigned long flags;
	int is_system, release_info = 0;
	u32 cpu;
	int r;

	ctx = __get_cpu_var(pmu_ctx);
	if (ctx == NULL)
		return;

	is_system = ctx->flags.system;
	cpu = ctx->cpu;

	/*
	 * context is LOADED or MASKED
	 *
	 * we unload from CPU. That stops monitoring and does
	 * all the bookeeping of saving values and updating duration
	 */
	spin_lock_irqsave(&ctx->lock, flags);
	if (is_system)
		__pfm_unload_context(ctx, &release_info);
	spin_unlock_irqrestore(&ctx->lock, flags);

	/*
	 * cancel timer
	 */
	if (release_info & 0x2) {
		r = hrtimer_cancel(&__get_cpu_var(pfm_hrtimer));
		PFM_DBG("timeout cancel=%d", r);
	}

	if (release_info & 0x1)
		pfm_session_release(is_system, cpu);
}

static struct notifier_block pfm_cpu_notifier = {
	.notifier_call = pfm_cpu_notify
};

int __init pfm_init_hotplug(void)
{
	int ret = 0;
	/*
	 * register CPU hotplug event notifier
	 */
	ret = register_cpu_notifier(&pfm_cpu_notifier);
	if (!ret)
		PFM_LOG("CPU hotplug support enabled");
	return ret;
}
#endif /* CONFIG_HOTPLUG_CPU */
