/*
 * acpi-cpufreq-io.c - ACPI Processor P-States Driver ($Revision: 1.3 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002 - 2004 Dominik Brodowski <linux@brodo.de>
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/uaccess.h>

#include <linux/acpi.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT	0x01000000
#define ACPI_PROCESSOR_CLASS		"processor"
#define ACPI_PROCESSOR_DRIVER_NAME	"ACPI Processor P-States Driver"
#define ACPI_PROCESSOR_DEVICE_NAME	"Processor"

#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME		("acpi_processor_perf")

MODULE_AUTHOR("Paul Diefenbaugh, Dominik Brodowski");
MODULE_DESCRIPTION(ACPI_PROCESSOR_DRIVER_NAME);
MODULE_LICENSE("GPL");


static struct acpi_processor_performance	*performance[NR_CPUS];


static int
acpi_processor_write_port(
	u16	port,
	u8	bit_width,
	u32	value)
{
	if (bit_width <= 8) {
		outb(value, port);
	} else if (bit_width <= 16) {
		outw(value, port);
	} else if (bit_width <= 32) {
		outl(value, port);
	} else {
		return -ENODEV;
	}
	return 0;
}

static int
acpi_processor_read_port(
	u16	port,
	u8	bit_width,
	u32	*ret)
{
	*ret = 0;
	if (bit_width <= 8) {
		*ret = inb(port);
	} else if (bit_width <= 16) {
		*ret = inw(port);
	} else if (bit_width <= 32) {
		*ret = inl(port);
	} else {
		return -ENODEV;
	}
	return 0;
}

static int
acpi_processor_set_performance (
	struct acpi_processor_performance	*perf,
	unsigned int		cpu,
	int			state)
{
	u16			port = 0;
	u8			bit_width = 0;
	int			ret = 0;
	u32			value = 0;
	int			i = 0;
	struct cpufreq_freqs    cpufreq_freqs;

	ACPI_FUNCTION_TRACE("acpi_processor_set_performance");

	if (!perf)
		return_VALUE(-EINVAL);

	if (state >= perf->state_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
			"Invalid target state (P%d)\n", state));
		return_VALUE(-ENODEV);
	}

	if (state == perf->state) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"Already at target state (P%d)\n", state));
		return_VALUE(0);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Transitioning from P%d to P%d\n",
		perf->state, state));

	/* cpufreq frequency struct */
	cpufreq_freqs.cpu = cpu;
	cpufreq_freqs.old = perf->states[perf->state].core_frequency * 1000;
	cpufreq_freqs.new = perf->states[state].core_frequency * 1000;

	/* notify cpufreq */
	cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_PRECHANGE);

	/*
	 * First we write the target state's 'control' value to the
	 * control_register.
	 */

	port = perf->control_register;
	bit_width = perf->control_register_bit_width;
	value = (u32) perf->states[state].control;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Writing 0x%08x to port 0x%04x\n", value, port));

	ret = acpi_processor_write_port(port, bit_width, value);
	if (ret) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN,
			"Invalid port width 0x%04x\n", bit_width));
		return_VALUE(ret);
	}

	/*
	 * Then we read the 'status_register' and compare the value with the
	 * target state's 'status' to make sure the transition was successful.
	 * Note that we'll poll for up to 1ms (100 cycles of 10us) before
	 * giving up.
	 */

	port = perf->status_register;
	bit_width = perf->status_register_bit_width;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Looking for 0x%08x from port 0x%04x\n",
		(u32) perf->states[state].status, port));

	for (i=0; i<100; i++) {
		ret = acpi_processor_read_port(port, bit_width, &value);
		if (ret) {	
			ACPI_DEBUG_PRINT((ACPI_DB_WARN,
				"Invalid port width 0x%04x\n", bit_width));
			return_VALUE(ret);
		}
		if (value == (u32) perf->states[state].status)
			break;
		udelay(10);
	}

	/* notify cpufreq */
	cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_POSTCHANGE);

	if (value != (u32) perf->states[state].status) {
		unsigned int tmp = cpufreq_freqs.new;
		cpufreq_freqs.new = cpufreq_freqs.old;
		cpufreq_freqs.old = tmp;
		cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_PRECHANGE);
		cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_POSTCHANGE);
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Transition failed\n"));
		return_VALUE(-ENODEV);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Transition successful after %d microseconds\n",
		i * 10));

	perf->state = state;

	return_VALUE(0);
}


static int
acpi_cpufreq_target (
	struct cpufreq_policy   *policy,
	unsigned int target_freq,
	unsigned int relation)
{
	struct acpi_processor_performance *perf = performance[policy->cpu];
	unsigned int next_state = 0;
	unsigned int result = 0;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_setpolicy");

	result = cpufreq_frequency_table_target(policy, 
			perf->freq_table,
			target_freq,
			relation,
			&next_state);
	if (result)
		return_VALUE(result);

	result = acpi_processor_set_performance (perf, policy->cpu, next_state);

	return_VALUE(result);
}


static int
acpi_cpufreq_verify (
	struct cpufreq_policy   *policy)
{
	unsigned int result = 0;
	struct acpi_processor_performance *perf = performance[policy->cpu];

	ACPI_FUNCTION_TRACE("acpi_cpufreq_verify");

	result = cpufreq_frequency_table_verify(policy, 
			perf->freq_table);

	cpufreq_verify_within_limits(
		policy, 
		perf->states[perf->state_count - 1].core_frequency * 1000,
		perf->states[0].core_frequency * 1000);

	return_VALUE(result);
}


static int
acpi_cpufreq_cpu_init (
	struct cpufreq_policy   *policy)
{
	unsigned int		i;
	unsigned int		cpu = policy->cpu;
	struct acpi_processor_performance *perf;
	unsigned int		result = 0;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_cpu_init");

	perf = kmalloc(sizeof(struct acpi_processor_performance), GFP_KERNEL);
	if (!perf)
		return_VALUE(-ENOMEM);
	memset(perf, 0, sizeof(struct acpi_processor_performance));

	performance[cpu] = perf;

	result = acpi_processor_register_performance(perf, cpu);
	if (result)
		goto err_free;

	/* capability check */
	if (perf->state_count <= 1)
		goto err_unreg;

	/* detect transition latency */
	policy->cpuinfo.transition_latency = 0;
	for (i=0; i<perf->state_count; i++) {
		if ((perf->states[i].transition_latency * 1000) > policy->cpuinfo.transition_latency)
			policy->cpuinfo.transition_latency = perf->states[i].transition_latency * 1000;
	}
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	/* 
	 * The current speed is unknown and not detectable by ACPI... argh! Assume 
	 * it's P0, it will be set to this value later during initialization.
	 */
	policy->cur = perf->states[0].core_frequency * 1000;

	/* table init */
	for (i=0; i<=perf->state_count; i++)
	{
		perf->freq_table[i].index = i;
		if (i<perf->state_count)
			perf->freq_table[i].frequency = perf->states[i].core_frequency * 1000;
		else
			perf->freq_table[i].frequency = CPUFREQ_TABLE_END;
	}

	result = cpufreq_frequency_table_cpuinfo(policy, &perf->freq_table[0]);

	printk(KERN_INFO "cpufreq: CPU%u - ACPI performance management activated.\n",
	       cpu);
	for (i = 0; i < perf->state_count; i++)
		printk(KERN_INFO "cpufreq: %cP%d: %d MHz, %d mW, %d uS\n",
			(i == perf->state?'*':' '), i,
			(u32) perf->states[i].core_frequency,
			(u32) perf->states[i].power,
			(u32) perf->states[i].transition_latency);

	return_VALUE(result);

 err_unreg:
	acpi_processor_unregister_performance(perf, cpu);
 err_free:
	kfree(perf);
	performance[cpu] = NULL;

	return_VALUE(result);
}


static int
acpi_cpufreq_cpu_exit (
	struct cpufreq_policy   *policy)
{
	struct acpi_processor_performance  *perf = performance[policy->cpu];

	ACPI_FUNCTION_TRACE("acpi_cpufreq_cpu_exit");

	if (perf) {
		performance[policy->cpu] = NULL;
		acpi_processor_unregister_performance(perf, policy->cpu);
		kfree(perf);
	}

	return_VALUE(0);
}


static struct cpufreq_driver acpi_cpufreq_driver = {
	.verify 	= acpi_cpufreq_verify,
	.target 	= acpi_cpufreq_target,
	.init		= acpi_cpufreq_cpu_init,
	.exit		= acpi_cpufreq_cpu_exit,
	.name		= "acpi-cpufreq",
	.owner		= THIS_MODULE,
};


static int __init
acpi_cpufreq_init (void)
{
	int                     result = 0;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_init");

 	result = cpufreq_register_driver(&acpi_cpufreq_driver);
	
	return_VALUE(result);
}


static void __exit
acpi_cpufreq_exit (void)
{
	ACPI_FUNCTION_TRACE("acpi_cpufreq_exit");

	cpufreq_unregister_driver(&acpi_cpufreq_driver);

	return_VOID;
}


late_initcall(acpi_cpufreq_init);
module_exit(acpi_cpufreq_exit);
