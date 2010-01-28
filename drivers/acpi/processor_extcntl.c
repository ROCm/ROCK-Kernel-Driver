/*
 * processor_extcntl.c - channel to external control logic
 *
 *  Copyright (C) 2008, Intel corporation
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/cpu.h>

#include <acpi/processor.h>

#define ACPI_PROCESSOR_CLASS            "processor"
#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_extcntl")

static int processor_extcntl_parse_csd(struct acpi_processor *pr);
static int processor_extcntl_get_performance(struct acpi_processor *pr);
/*
 * External processor control logic may register with its own set of
 * ops to get ACPI related notification. One example is like VMM.
 */
const struct processor_extcntl_ops *processor_extcntl_ops;
EXPORT_SYMBOL(processor_extcntl_ops);

static int processor_notify_smm(void)
{
	acpi_status status;
	static int is_done = 0;

	/* only need successfully notify BIOS once */
	/* avoid double notification which may lead to unexpected result */
	if (is_done)
		return 0;

	/* Can't write pstate_cnt to smi_cmd if either value is zero */
	if (!acpi_gbl_FADT.smi_command || !acpi_gbl_FADT.pstate_control) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,"No SMI port or pstate_cnt\n"));
		return 0;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"Writing pstate_cnt [0x%x] to smi_cmd [0x%x]\n",
		acpi_gbl_FADT.pstate_control, acpi_gbl_FADT.smi_command));

	status = acpi_os_write_port(acpi_gbl_FADT.smi_command,
				    acpi_gbl_FADT.pstate_control, 8);
	if (ACPI_FAILURE(status))
		return status;

	is_done = 1;

	return 0;
}

int processor_notify_external(struct acpi_processor *pr, int event, int type)
{
	int ret = -EINVAL;

	if (!processor_cntl_external())
		return -EINVAL;

	switch (event) {
	case PROCESSOR_PM_INIT:
	case PROCESSOR_PM_CHANGE:
		if ((type >= PM_TYPE_MAX) ||
			!processor_extcntl_ops->pm_ops[type])
			break;

		ret = processor_extcntl_ops->pm_ops[type](pr, event);
		break;
	case PROCESSOR_HOTPLUG:
		if (processor_extcntl_ops->hotplug)
			ret = processor_extcntl_ops->hotplug(pr, type);
		break;
	default:
		printk(KERN_ERR "Unsupport processor events %d.\n", event);
		break;
	}

	return ret;
}

/*
 * External control logic can decide to grab full or part of physical
 * processor control bits. Take a VMM for example, physical processors
 * are owned by VMM and thus existence information like hotplug is
 * always required to be notified to VMM. Similar is processor idle
 * state which is also necessarily controlled by VMM. But for other
 * control bits like performance/throttle states, VMM may choose to
 * control or not upon its own policy.
 */
void processor_extcntl_init(void)
{
	if (!processor_extcntl_ops)
		arch_acpi_processor_init_extcntl(&processor_extcntl_ops);
}

/*
 * This is called from ACPI processor init, and targeted to hold
 * some tricky housekeeping jobs to satisfy external control model.
 * For example, we may put dependency parse stub here for idle
 * and performance state. Those information may be not available
 * if splitting from dom0 control logic like cpufreq driver.
 */
int processor_extcntl_prepare(struct acpi_processor *pr)
{
	/* parse cstate dependency information */
	if (processor_pm_external())
		processor_extcntl_parse_csd(pr);

	/* Initialize performance states */
	if (processor_pmperf_external())
		processor_extcntl_get_performance(pr);

	return 0;
}

/*
 * Currently no _CSD is implemented which is why existing ACPI code
 * doesn't parse _CSD at all. But to keep interface complete with
 * external control logic, we put a placeholder here for future
 * compatibility.
 */
static int processor_extcntl_parse_csd(struct acpi_processor *pr)
{
	int i;

	for (i = 0; i < pr->power.count; i++) {
		if (!pr->power.states[i].valid)
			continue;

		/* No dependency by default */
		pr->power.states[i].domain_info = NULL;
		pr->power.states[i].csd_count = 0;
	}

	return 0;
}

/*
 * Existing ACPI module does parse performance states at some point,
 * when acpi-cpufreq driver is loaded which however is something
 * we'd like to disable to avoid confliction with external control
 * logic. So we have to collect raw performance information here
 * when ACPI processor object is found and started.
 */
static int processor_extcntl_get_performance(struct acpi_processor *pr)
{
	int ret;
	struct acpi_processor_performance *perf;
	struct acpi_psd_package *pdomain;

	if (pr->performance)
		return -EBUSY;

	perf = kzalloc(sizeof(struct acpi_processor_performance), GFP_KERNEL);
	if (!perf)
		return -ENOMEM;

	pr->performance = perf;
	/* Get basic performance state information */
	ret = acpi_processor_get_performance_info(pr);
	if (ret < 0)
		goto err_out;

	/*
	 * Well, here we need retrieve performance dependency information
	 * from _PSD object. The reason why existing interface is not used
	 * is due to the reason that existing interface sticks to Linux cpu
	 * id to construct some bitmap, however we want to split ACPI
	 * processor objects from Linux cpu id logic. For example, even
	 * when Linux is configured as UP, we still want to parse all ACPI
	 * processor objects to external logic. In this case, it's preferred
	 * to use ACPI ID instead.
	 */
	pdomain = &pr->performance->domain_info;
	pdomain->num_processors = 0;
	ret = acpi_processor_get_psd(pr);
	if (ret < 0) {
		/*
		 * _PSD is optional - assume no coordination if absent (or
		 * broken), matching native kernels' behavior.
		 */
		pdomain->num_entries = ACPI_PSD_REV0_ENTRIES;
		pdomain->revision = ACPI_PSD_REV0_REVISION;
		pdomain->domain = pr->acpi_id;
		pdomain->coord_type = DOMAIN_COORD_TYPE_SW_ALL;
		pdomain->num_processors = 1;
	}

	/* Some sanity check */
	if ((pdomain->revision != ACPI_PSD_REV0_REVISION) ||
	    (pdomain->num_entries != ACPI_PSD_REV0_ENTRIES) ||
	    ((pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ALL) &&
	     (pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ANY) &&
	     (pdomain->coord_type != DOMAIN_COORD_TYPE_HW_ALL))) {
		ret = -EINVAL;
		goto err_out;
	}

	/* Last step is to notify BIOS that external logic exists */
	processor_notify_smm();

	processor_notify_external(pr, PROCESSOR_PM_INIT, PM_TYPE_PERF);

	return 0;
err_out:
	pr->performance = NULL;
	kfree(perf);
	return ret;
}

/*
 * Objects and functions removed in native 2.6.29, and thus moved here.
 */
#ifdef CONFIG_SMP
static void smp_callback(void *v)
{
	/* we already woke the CPU up, nothing more to do */
}

/*
 * This function gets called when a part of the kernel has a new latency
 * requirement.  This means we need to get all processors out of their C-state,
 * and then recalculate a new suitable C-state. Just do a cross-cpu IPI; that
 * wakes them all right up.
 */
static int acpi_processor_latency_notify(struct notifier_block *b,
					 unsigned long l, void *v)
{
	smp_call_function(smp_callback, NULL, 1);
	return NOTIFY_OK;
}

struct notifier_block acpi_processor_latency_notifier = {
	.notifier_call = acpi_processor_latency_notify,
};
#endif

/*
 * bm_history -- bit-mask with a bit per jiffy of bus-master activity
 * 1000 HZ: 0xFFFFFFFF: 32 jiffies = 32ms
 * 800 HZ: 0xFFFFFFFF: 32 jiffies = 40ms
 * 100 HZ: 0x0000000F: 4 jiffies = 40ms
 * reduce history for more aggressive entry into C3
 */
static unsigned int bm_history __read_mostly =
    (HZ >= 800 ? 0xFFFFFFFF : ((1U << (HZ / 25)) - 1));
module_param(bm_history, uint, 0644);

int acpi_processor_set_power_policy(struct acpi_processor *pr)
{
	unsigned int i;
	unsigned int state_is_set = 0;
	struct acpi_processor_cx *lower = NULL;
	struct acpi_processor_cx *higher = NULL;
	struct acpi_processor_cx *cx;


	if (!pr)
		return -EINVAL;

	/*
	 * This function sets the default Cx state policy (OS idle handler).
	 * Our scheme is to promote quickly to C2 but more conservatively
	 * to C3.  We're favoring C2  for its characteristics of low latency
	 * (quick response), good power savings, and ability to allow bus
	 * mastering activity.  Note that the Cx state policy is completely
	 * customizable and can be altered dynamically.
	 */

	/* startup state */
	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (!state_is_set)
			pr->power.state = cx;
		state_is_set++;
		break;
	}

	if (!state_is_set)
		return -ENODEV;

	/* demotion */
	for (i = 1; i < ACPI_PROCESSOR_MAX_POWER; i++) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (lower) {
			cx->demotion.state = lower;
			cx->demotion.threshold.ticks = cx->latency_ticks;
			cx->demotion.threshold.count = 1;
			if (cx->type == ACPI_STATE_C3)
				cx->demotion.threshold.bm = bm_history;
		}

		lower = cx;
	}

	/* promotion */
	for (i = (ACPI_PROCESSOR_MAX_POWER - 1); i > 0; i--) {
		cx = &pr->power.states[i];
		if (!cx->valid)
			continue;

		if (higher) {
			cx->promotion.state = higher;
			cx->promotion.threshold.ticks = cx->latency_ticks;
			if (cx->type >= ACPI_STATE_C2)
				cx->promotion.threshold.count = 4;
			else
				cx->promotion.threshold.count = 10;
			if (higher->type == ACPI_STATE_C3)
				cx->promotion.threshold.bm = bm_history;
		}

		higher = cx;
	}

	return 0;
}
