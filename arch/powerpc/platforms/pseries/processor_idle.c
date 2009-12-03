/*
 *  processor_idle - idle state cpuidle driver.
 *  Adapted from drivers/acpi/processor_idle.c
 *
 *  Arun R Bharadwaj <arun@linux.vnet.ibm.com>
 *
 *  Copyright (C) 2009 IBM Corporation.
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cpuidle.h>

#include <asm/paca.h>
#include <asm/reg.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <asm/firmware.h>

#include "plpar_wrappers.h"
#include "pseries.h"

MODULE_AUTHOR("Arun R Bharadwaj");
MODULE_DESCRIPTION("pSeries Idle State Driver");
MODULE_LICENSE("GPL");

struct cpuidle_driver pseries_idle_driver = {
	.name =		"pseries_idle",
	.owner =	THIS_MODULE,
};

DEFINE_PER_CPU(struct cpuidle_device, pseries_dev);

#define IDLE_STATE_COUNT	2

/* pSeries Idle state Flags */
#define	PSERIES_DEDICATED_SNOOZE	(0x01)
#define	PSERIES_DEDICATED_CEDE		(0x02)
#define	PSERIES_SHARED_CEDE		(0x03)

static int pseries_idle_init(struct cpuidle_device *dev)
{
	return cpuidle_register_device(dev);
}

static void shared_cede_loop(void)
{
	get_lppaca()->idle = 1;
	cede_processor();
	get_lppaca()->idle = 0;
}

static void dedicated_snooze_loop(void)
{
	local_irq_enable();
	set_thread_flag(TIF_POLLING_NRFLAG);
	while (!need_resched()) {
		ppc64_runlatch_off();
		HMT_low();
		HMT_very_low();
	}
	HMT_medium();
	clear_thread_flag(TIF_POLLING_NRFLAG);
	smp_mb();
	local_irq_disable();
}

static void dedicated_cede_loop(void)
{
	ppc64_runlatch_off();
	HMT_medium();
	cede_processor();
}

static void pseries_cpuidle_loop(struct cpuidle_device *dev,
				struct cpuidle_state *st)
{
	unsigned long in_purr, out_purr;

	get_lppaca()->idle = 1;
	get_lppaca()->donate_dedicated_cpu = 1;
	in_purr = mfspr(SPRN_PURR);

	if (st->flags & PSERIES_SHARED_CEDE)
		shared_cede_loop();
	else if (st->flags & PSERIES_DEDICATED_SNOOZE)
		dedicated_snooze_loop();
	else
		dedicated_cede_loop();

	out_purr = mfspr(SPRN_PURR);
	get_lppaca()->wait_state_cycles += out_purr - in_purr;
	get_lppaca()->donate_dedicated_cpu = 0;
	get_lppaca()->idle = 0;
}

static int pseries_setup_cpuidle(struct cpuidle_device *dev, int cpu)
{
	int i;
	struct cpuidle_state *state;

	dev->cpu = cpu;

	if (get_lppaca()->shared_proc) {
		state = &dev->states[0];
		snprintf(state->name, CPUIDLE_NAME_LEN, "IDLE");
		state->enter = pseries_cpuidle_loop;
		strncpy(state->desc, "shared_cede", CPUIDLE_DESC_LEN);
		state->flags = PSERIES_SHARED_CEDE;
		state->exit_latency = 0;
		state->target_residency = 0;
		return 0;
	}

	for (i = 0; i < IDLE_STATE_COUNT; i++) {
		state = &dev->states[i];

		snprintf(state->name, CPUIDLE_NAME_LEN, "CEDE%d", i);
		state->enter = pseries_cpuidle_loop;

		switch (i) {
		case 0:
			strncpy(state->desc, "snooze", CPUIDLE_DESC_LEN);
			state->flags = PSERIES_DEDICATED_SNOOZE;
			state->exit_latency = 0;
			state->target_residency = 0;
			break;

		case 1:
			strncpy(state->desc, "cede", CPUIDLE_DESC_LEN);
			state->flags = PSERIES_DEDICATED_CEDE;
			state->exit_latency = 1;
			state->target_residency =
					__get_cpu_var(smt_snooze_delay);
			break;
		}
	}
	dev->state_count = IDLE_STATE_COUNT;

	return 0;
}

void update_smt_snooze_delay(int snooze)
{
	int cpu;
	for_each_online_cpu(cpu)
		per_cpu(pseries_dev, cpu).states[0].target_residency = snooze;
}

static int __init pseries_processor_idle_init(void)
{
	int cpu;
	int result;

	if (boot_option_idle_override) {
		printk(KERN_DEBUG "Using default idle\n");
		return 0;
	}

	result = cpuidle_register_driver(&pseries_idle_driver);

	if (result < 0)
		return result;

	printk(KERN_DEBUG "pSeries idle driver registered\n");

	if (!firmware_has_feature(FW_FEATURE_SPLPAR)) {
		printk(KERN_DEBUG "Using default idle\n");
		return 0;
	}

	for_each_online_cpu(cpu) {
		pseries_setup_cpuidle(&per_cpu(pseries_dev, cpu), cpu);
		pseries_idle_init(&per_cpu(pseries_dev, cpu));
	}

	printk(KERN_DEBUG "Using cpuidle idle loop\n");

	return 0;
}

device_initcall(pseries_processor_idle_init);
