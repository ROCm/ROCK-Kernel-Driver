/*
 * acpi_processor_perf.c - ACPI Processor P-States Driver ($Revision: 1.3 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002, 2003 Dominik Brodowski <linux@brodo.de>
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
#define ACPI_PROCESSOR_FILE_PERFORMANCE	"performance"

#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME		("acpi_processor_perf")

MODULE_AUTHOR("Paul Diefenbaugh, Dominik Brodowski");
MODULE_DESCRIPTION(ACPI_PROCESSOR_DRIVER_NAME);
MODULE_LICENSE("GPL");


static struct acpi_processor_performance	*performance;


static int 
acpi_processor_get_performance_control (
	struct acpi_processor_performance *perf)
{
	int			result = 0;
	acpi_status		status = 0;
	struct acpi_buffer	buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object	*pct = NULL;
	union acpi_object	obj = {0};
	struct acpi_pct_register *reg = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_control");

	status = acpi_evaluate_object(perf->pr->handle, "_PCT", NULL, &buffer);
	if(ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PCT\n"));
		return_VALUE(-ENODEV);
	}

	pct = (union acpi_object *) buffer.pointer;
	if (!pct || (pct->type != ACPI_TYPE_PACKAGE) 
		|| (pct->package.count != 2)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PCT data\n"));
		result = -EFAULT;
		goto end;
	}

	/*
	 * control_register
	 */

	obj = pct->package.elements[0];

	if ((obj.type != ACPI_TYPE_BUFFER) 
		|| (obj.buffer.length < sizeof(struct acpi_pct_register)) 
		|| (obj.buffer.pointer == NULL)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Invalid _PCT data (control_register)\n"));
		result = -EFAULT;
		goto end;
	}

	reg = (struct acpi_pct_register *) (obj.buffer.pointer);

	if (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unsupported address space [%d] (control_register)\n",
			(u32) reg->space_id));
		result = -EFAULT;
		goto end;
	}

	perf->control_register = (u16) reg->address;

	/*
	 * status_register
	 */

	obj = pct->package.elements[1];

	if ((obj.type != ACPI_TYPE_BUFFER) 
		|| (obj.buffer.length < sizeof(struct acpi_pct_register)) 
		|| (obj.buffer.pointer == NULL)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, 
			"Invalid _PCT data (status_register)\n"));
		result = -EFAULT;
		goto end;
	}

	reg = (struct acpi_pct_register *) (obj.buffer.pointer);

	if (reg->space_id != ACPI_ADR_SPACE_SYSTEM_IO) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unsupported address space [%d] (status_register)\n",
			(u32) reg->space_id));
		result = -EFAULT;
		goto end;
	}

	perf->status_register = (u16) reg->address;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"control_register[0x%04x] status_register[0x%04x]\n",
		perf->control_register,
		perf->status_register));

end:
	acpi_os_free(buffer.pointer);

	return_VALUE(result);
}


static int 
acpi_processor_get_performance_states (
	struct acpi_processor_performance *	perf)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	struct acpi_buffer	buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer	format = {sizeof("NNNNNN"), "NNNNNN"};
	struct acpi_buffer	state = {0, NULL};
	union acpi_object 	*pss = NULL;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_states");

	status = acpi_evaluate_object(perf->pr->handle, "_PSS", NULL, &buffer);
	if(ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error evaluating _PSS\n"));
		return_VALUE(-ENODEV);
	}

	pss = (union acpi_object *) buffer.pointer;
	if (!pss || (pss->type != ACPI_TYPE_PACKAGE)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PSS data\n"));
		result = -EFAULT;
		goto end;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found %d performance states\n", 
		pss->package.count));

	if (pss->package.count > ACPI_PROCESSOR_MAX_PERFORMANCE) {
		perf->state_count = ACPI_PROCESSOR_MAX_PERFORMANCE;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"Limiting number of states to max (%d)\n", 
			ACPI_PROCESSOR_MAX_PERFORMANCE));
	}
	else
		perf->state_count = pss->package.count;

	if (perf->state_count > 1)
		perf->pr->flags.performance = 1;

	for (i = 0; i < perf->state_count; i++) {

		struct acpi_processor_px *px = &(perf->states[i]);

		state.length = sizeof(struct acpi_processor_px);
		state.pointer = px;

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Extracting state %d\n", i));

		status = acpi_extract_package(&(pss->package.elements[i]), 
			&format, &state);
		if (ACPI_FAILURE(status)) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid _PSS data\n"));
			result = -EFAULT;
			goto end;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"State [%d]: core_frequency[%d] power[%d] transition_latency[%d] bus_master_latency[%d] control[0x%x] status[0x%x]\n",
			i, 
			(u32) px->core_frequency, 
			(u32) px->power, 
			(u32) px->transition_latency, 
			(u32) px->bus_master_latency,
			(u32) px->control, 
			(u32) px->status));
	}

end:
	acpi_os_free(buffer.pointer);

	return_VALUE(result);
}


static int
acpi_processor_set_performance (
	struct acpi_processor_performance	*perf,
	int			state)
{
	u16			port = 0;
	u16			value = 0;
	int			i = 0;
	struct cpufreq_freqs    cpufreq_freqs;

	ACPI_FUNCTION_TRACE("acpi_processor_set_performance");

	if (!perf || !perf->pr)
		return_VALUE(-EINVAL);

	if (!perf->pr->flags.performance)
		return_VALUE(-ENODEV);

	if (state >= perf->state_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
			"Invalid target state (P%d)\n", state));
		return_VALUE(-ENODEV);
	}

	if (state < perf->pr->performance_platform_limit) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, 
			"Platform limit (P%d) overrides target state (P%d)\n",
			perf->pr->performance_platform_limit, state));
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
	cpufreq_freqs.cpu = perf->pr->id;
	cpufreq_freqs.old = perf->states[perf->state].core_frequency;
	cpufreq_freqs.new = perf->states[state].core_frequency;

	/* notify cpufreq */
	cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_PRECHANGE);

	/*
	 * First we write the target state's 'control' value to the
	 * control_register.
	 */

	port = perf->control_register;
	value = (u16) perf->states[state].control;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Writing 0x%04x to port 0x%04x\n", value, port));

	outw(value, port); 

	/*
	 * Then we read the 'status_register' and compare the value with the
	 * target state's 'status' to make sure the transition was successful.
	 * Note that we'll poll for up to 1ms (100 cycles of 10us) before
	 * giving up.
	 */

	port = perf->status_register;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
		"Looking for 0x%04x from port 0x%04x\n",
		(u16) perf->states[state].status, port));

	for (i=0; i<100; i++) {
		value = inw(port);
		if (value == (u16) perf->states[state].status)
			break;
		udelay(10);
	}

	/* notify cpufreq */
	cpufreq_notify_transition(&cpufreq_freqs, CPUFREQ_POSTCHANGE);

	if (value != (u16) perf->states[state].status) {
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


#ifdef CONFIG_X86_ACPI_CPUFREQ_PROC_INTF
/* /proc/acpi/processor/../performance interface (DEPRECATED) */

static int acpi_processor_perf_open_fs(struct inode *inode, struct file *file);
static struct file_operations acpi_processor_perf_fops = {
	.open 		= acpi_processor_perf_open_fs,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int acpi_processor_perf_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor	*pr = (struct acpi_processor *)seq->private;
	int			i = 0;

	ACPI_FUNCTION_TRACE("acpi_processor_perf_seq_show");

	if (!pr)
		goto end;

	if (!pr->flags.performance || !pr->performance) {
		seq_puts(seq, "<not supported>\n");
		goto end;
	}

	seq_printf(seq, "state count:             %d\n"
			"active state:            P%d\n",
			pr->performance->state_count,
			pr->performance->state);

	seq_puts(seq, "states:\n");
	for (i = 0; i < pr->performance->state_count; i++)
		seq_printf(seq, "   %cP%d:                  %d MHz, %d mW, %d uS\n",
			(i == pr->performance->state?'*':' '), i,
			(u32) pr->performance->states[i].core_frequency,
			(u32) pr->performance->states[i].power,
			(u32) pr->performance->states[i].transition_latency);

end:
	return 0;
}

static int acpi_processor_perf_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_perf_seq_show,
						PDE(inode)->data);
}

static int
acpi_processor_write_performance (
        struct file		*file,
        const char		__user *buffer,
        size_t			count,
        loff_t			*data)
{
	int			result = 0;
	struct acpi_processor	*pr = (struct acpi_processor *) data;
	char			state_string[12] = {'\0'};
	unsigned int            new_state = 0;
	struct cpufreq_policy   policy;

	ACPI_FUNCTION_TRACE("acpi_processor_write_performance");

	if (!pr || !pr->performance || (count > sizeof(state_string) - 1))
		return_VALUE(-EINVAL);
	
	if (copy_from_user(state_string, buffer, count))
		return_VALUE(-EFAULT);
	
	state_string[count] = '\0';
	new_state = simple_strtoul(state_string, NULL, 0);

	cpufreq_get_policy(&policy, pr->id);

	policy.cpu = pr->id;
	policy.max = pr->performance->states[new_state].core_frequency * 1000;

	result = cpufreq_set_policy(&policy);
	if (result)
		return_VALUE(result);

	return_VALUE(count);
}

static void
acpi_cpufreq_add_file (
	struct acpi_processor *pr)
{
	struct proc_dir_entry	*entry = NULL;
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_addfile");

	if (acpi_bus_get_device(pr->handle, &device))
		return_VOID;

	/* add file 'performance' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_PERFORMANCE,
		  S_IFREG|S_IRUGO|S_IWUSR, acpi_device_dir(device));
	if (!entry)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			"Unable to create '%s' fs entry\n",
			ACPI_PROCESSOR_FILE_PERFORMANCE));
	else {
		entry->proc_fops = &acpi_processor_perf_fops;
		entry->proc_fops->write = acpi_processor_write_performance;
		entry->data = acpi_driver_data(device);
	}
	return_VOID;
}

static void
acpi_cpufreq_remove_file (
	struct acpi_processor *pr)
{
	struct acpi_device	*device = NULL;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_addfile");

	if (acpi_bus_get_device(pr->handle, &device))
		return_VOID;

	/* remove file 'performance' */
	remove_proc_entry(ACPI_PROCESSOR_FILE_PERFORMANCE,
		  acpi_device_dir(device));

	return_VOID;
}

#else
static void acpi_cpufreq_add_file (struct acpi_processor *pr) { return; }
static void acpi_cpufreq_remove_file (struct acpi_processor *pr) { return; }
#endif /* CONFIG_X86_ACPI_CPUFREQ_PROC_INTF */


static int
acpi_cpufreq_target (
	struct cpufreq_policy   *policy,
	unsigned int target_freq,
	unsigned int relation)
{
	struct acpi_processor_performance *perf = &performance[policy->cpu];
	unsigned int next_state = 0;
	unsigned int result = 0;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_setpolicy");

	result = cpufreq_frequency_table_target(policy, 
			&perf->freq_table[perf->pr->limit.state.px],
			target_freq,
			relation,
			&next_state);
	if (result)
		return_VALUE(result);

	result = acpi_processor_set_performance (perf, next_state);

	return_VALUE(result);
}


static int
acpi_cpufreq_verify (
	struct cpufreq_policy   *policy)
{
	unsigned int result = 0;
	struct acpi_processor_performance *perf = &performance[policy->cpu];

	ACPI_FUNCTION_TRACE("acpi_cpufreq_verify");

	result = cpufreq_frequency_table_verify(policy, 
			&perf->freq_table[perf->pr->limit.state.px]);

	cpufreq_verify_within_limits(
		policy, 
		perf->states[perf->state_count - 1].core_frequency * 1000,
		perf->states[perf->pr->limit.state.px].core_frequency * 1000);

	return_VALUE(result);
}


static int
acpi_processor_get_performance_info (
	struct acpi_processor_performance	*perf)
{
	int			result = 0;
	acpi_status		status = AE_OK;
	acpi_handle		handle = NULL;

	ACPI_FUNCTION_TRACE("acpi_processor_get_performance_info");

	if (!perf || !perf->pr || !perf->pr->handle)
		return_VALUE(-EINVAL);

	status = acpi_get_handle(perf->pr->handle, "_PCT", &handle);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, 
			"ACPI-based processor performance control unavailable\n"));
		return_VALUE(-ENODEV);
	}

	result = acpi_processor_get_performance_control(perf);
	if (result)
		return_VALUE(result);

	result = acpi_processor_get_performance_states(perf);
	if (result)
		return_VALUE(result);

	result = acpi_processor_get_platform_limit(perf->pr);
	if (result)
		return_VALUE(result);

	return_VALUE(0);
}


static int
acpi_cpufreq_cpu_init (
	struct cpufreq_policy   *policy)
{
	unsigned int		i;
	unsigned int		cpu = policy->cpu;
	struct acpi_processor	*pr = NULL;
	struct acpi_processor_performance *perf = &performance[policy->cpu];
	struct acpi_device	*device;
	unsigned int		result = 0;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_cpu_init");

	acpi_processor_register_performance(perf, &pr, cpu);

	pr = performance[cpu].pr;
	if (!pr)
		return_VALUE(-ENODEV);

	result = acpi_processor_get_performance_info(perf);
	if (result)
		return_VALUE(-ENODEV);

	/* capability check */
	if (!pr->flags.performance)
		return_VALUE(-ENODEV);

	/* detect transition latency */
	policy->cpuinfo.transition_latency = 0;
	for (i=0;i<perf->state_count;i++) {
		if (perf->states[i].transition_latency > policy->cpuinfo.transition_latency)
			policy->cpuinfo.transition_latency = perf->states[i].transition_latency;
	}
	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;
	policy->cur = perf->states[pr->limit.state.px].core_frequency * 1000;

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

	acpi_cpufreq_add_file(pr);

	if (acpi_bus_get_device(pr->handle, &device))
		device = NULL;
		
	printk(KERN_INFO "cpufreq: %s - ACPI performance management activated.\n",
		device ? acpi_device_bid(device) : "CPU??");
	for (i = 0; i < pr->performance->state_count; i++)
		printk(KERN_INFO "cpufreq: %cP%d: %d MHz, %d mW, %d uS\n",
			(i == pr->performance->state?'*':' '), i,
			(u32) pr->performance->states[i].core_frequency,
			(u32) pr->performance->states[i].power,
			(u32) pr->performance->states[i].transition_latency);
	return_VALUE(result);
}


static int
acpi_cpufreq_cpu_exit (
	struct cpufreq_policy   *policy)
{
	struct acpi_processor  *pr = performance[policy->cpu].pr;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_cpu_exit");

	acpi_cpufreq_remove_file(pr);

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
	int                     current_state = 0;
	int                     i = 0;
	struct acpi_processor   *pr = NULL;
	struct acpi_processor_performance *perf = NULL;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_init");

	/* alloc memory */
	if (performance)
		return_VALUE(-EBUSY);

	performance = kmalloc(NR_CPUS * sizeof(struct acpi_processor_performance), GFP_KERNEL);
	if (!performance)
		return_VALUE(-ENOMEM);
	memset(performance, 0, NR_CPUS * sizeof(struct acpi_processor_performance));

	/* register struct acpi_processor_performance performance */
	for (i=0; i<NR_CPUS; i++) {
		if (cpu_online(i))
			acpi_processor_register_performance(&performance[i], &pr, i);
	}

	/* initialize  */
	for (i=0; i<NR_CPUS; i++) {
		if (cpu_online(i) && performance[i].pr)
			result = acpi_processor_get_performance_info(&performance[i]);
	}

	/* test it on one CPU */
	for (i=0; i<NR_CPUS; i++) {
		if (!cpu_online(i))
			continue;
		pr = performance[i].pr;
		if (pr && pr->flags.performance)
			goto found_capable_cpu;
	}
	result = -ENODEV;
	goto err0;

 found_capable_cpu:
	
 	result = cpufreq_register_driver(&acpi_cpufreq_driver);
	if (result) 
		goto err0;
	
	perf = pr->performance;
	current_state = perf->state;

	if (current_state == pr->limit.state.px) {
		result = acpi_processor_set_performance(perf, (perf->state_count - 1));
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Disabled P-States due to failure while switching.\n"));
			result = -ENODEV;
			goto err1;
		}
	}

	result = acpi_processor_set_performance(perf, pr->limit.state.px);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Disabled P-States due to failure while switching.\n"));
		result = -ENODEV;
		goto err1;
	}
	
	if (current_state != 0) {
		result = acpi_processor_set_performance(perf, current_state);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Disabled P-States due to failure while switching.\n"));
			result = -ENODEV;
			goto err1;
		}
	}

	return_VALUE(0);

	/* error handling */
 err1:
	cpufreq_unregister_driver(&acpi_cpufreq_driver);
	
 err0:
	/* unregister struct acpi_processor_performance performance */
	for (i=0; i<NR_CPUS; i++) {
		if (performance[i].pr) {
			performance[i].pr->flags.performance = 0;
			performance[i].pr->performance = NULL;
			performance[i].pr = NULL;
		}
	}
	kfree(performance);
	
	printk(KERN_INFO "cpufreq: No CPUs supporting ACPI performance management found.\n");
	return_VALUE(result);
}


static void __exit
acpi_cpufreq_exit (void)
{
	int                     i = 0;

	ACPI_FUNCTION_TRACE("acpi_cpufreq_exit");

	for (i=0; i<NR_CPUS; i++) {
		if (performance[i].pr)
			performance[i].pr->flags.performance = 0;
	}

	 cpufreq_unregister_driver(&acpi_cpufreq_driver);

	/* unregister struct acpi_processor_performance performance */
	for (i=0; i<NR_CPUS; i++) {
		if (performance[i].pr) {
			performance[i].pr->flags.performance = 0;
			performance[i].pr->performance = NULL;
			performance[i].pr = NULL;
		}
	}

	kfree(performance);

	return_VOID;
}


late_initcall(acpi_cpufreq_init);
module_exit(acpi_cpufreq_exit);
