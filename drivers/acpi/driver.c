/*
 *  driver.c - ACPI driver
 *
 *  Copyright (C) 2000 Andrew Henroid
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes
 * David Woodhouse <dwmw2@redhat.com> 2000-12-6
 * - Fix interruptible_sleep_on() races
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <asm/uaccess.h>
#include "acpi.h"
#include "driver.h"

#ifdef CONFIG_ACPI_KERNEL_CONFIG
#include <asm/efi.h>
#define ACPI_CAN_USE_EFI_STRUCT
#endif


#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("driver")

struct acpi_run_entry
{
	void (*callback)(void*);
	void *context;
	struct tq_struct task;
};

static spinlock_t acpi_event_lock = SPIN_LOCK_UNLOCKED;
static volatile u32 acpi_event_status = 0;
static volatile acpi_sstate_t acpi_event_state = ACPI_S0;
static DECLARE_WAIT_QUEUE_HEAD(acpi_event_wait);

static volatile int acpi_thread_pid = -1;

/************************************************/
/* DECLARE_TASK_QUEUE is defined in             */
/* /usr/src/linux/include/linux/tqueue.h        */
/* So, acpi_thread_run is a pointer to a        */
/* tq_struct structure,defined in the same file.*/
/************************************************/
static DECLARE_TASK_QUEUE(acpi_thread_run);

static DECLARE_WAIT_QUEUE_HEAD(acpi_thread_wait);

static struct ctl_table_header *acpi_sysctl = NULL;

/*
 * Examine/modify value
 */
static int 
acpi_do_ulong(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	char str[2 * sizeof(unsigned long) + 4], *strend;
	unsigned long val;
	int size;

	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}

		val = *(unsigned long *) ctl->data;
		size = sprintf(str, "0x%08lx\n", val);
		if (*len >= size) {
			copy_to_user(buffer, str, size);
			*len = size;
		}
		else
			*len = 0;
	}
	else {
		size = sizeof(str) - 1;
		if (size > *len)
			size = *len;
		copy_from_user(str, buffer, size);
		str[size] = '\0';
		val = simple_strtoul(str, &strend, 0);
		if (strend == str)
			return -EINVAL;
		*(unsigned long *) ctl->data = val;
	}

	file->f_pos += *len;
	return 0;
}

static int 
acpi_do_pm_timer(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	int size;
	u32 val = 0;

	char str[12];

	if (file->f_pos) {
		*len = 0;
		return 0;
	}

	val = acpi_read_pm_timer();

	size = sprintf(str, "0x%08x\n", val);
	if (*len >= size) {
		copy_to_user(buffer, str, size);
		*len = size;
	}
	else
		*len = 0;

	file->f_pos += *len;

	return 0;
}

/*
 * Handle ACPI event
 */
static u32
acpi_event(void *context)
{
	unsigned long flags;
	int event = (int)(long)context;
	int mask = 0;

	switch (event) {
	case ACPI_EVENT_POWER_BUTTON:
		mask = ACPI_PWRBTN;
		break;
	case ACPI_EVENT_SLEEP_BUTTON:
		mask = ACPI_SLPBTN;
		break;
	default:
		return AE_ERROR;
	}

	if (mask) {
		// notify process waiting on /dev/acpi
		spin_lock_irqsave(&acpi_event_lock, flags);
		acpi_event_status |= mask;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		acpi_event_state = acpi_sleep_state;
		wake_up_interruptible(&acpi_event_wait);
	}

	return AE_OK;
}

/*
 * Wait for next event
 */
static int
acpi_do_event(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	u32 event_status = 0;
	acpi_sstate_t event_state = 0;
	char str[27];
	int size;

	if (write)
		return -EPERM;
	if (*len < sizeof(str)) {
		*len = 0;
		return 0;
	}

	while (!event_status) {
		unsigned long flags;
		DECLARE_WAITQUEUE(wait, current);

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&acpi_event_wait, &wait);

		// we need an atomic exchange here
		spin_lock_irqsave(&acpi_event_lock, flags);
		event_status = acpi_event_status;
		acpi_event_status = 0;
		spin_unlock_irqrestore(&acpi_event_lock, flags);
		event_state = acpi_event_state;

		if (!event_status)
			schedule();

		remove_wait_queue(&acpi_event_wait, &wait);
		set_current_state(TASK_RUNNING);

		if (signal_pending(current))
			return -ERESTARTSYS;
	}

	size = sprintf(str,
		       "0x%08x 0x%08x 0x%01x\n",
		       event_status,
		       0,
		       event_state);
	copy_to_user(buffer, str, size);
	*len = size;
	file->f_pos += size;

	return 0;
}

/*
 * Enter system sleep state
 */
static int 
acpi_do_sleep(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	if (!write) {
		if (file->f_pos) {
			*len = 0;
			return 0;
		}
	}
	else {
		int status = acpi_enter_sx(ACPI_S1);
		if (status)
			return status;
	}
	file->f_pos += *len;
	return 0;
}


/*
 * Output important ACPI tables to proc
 */
static int 
acpi_do_table(ctl_table * ctl,
	      int write,
	      struct file *file,
	      void *buffer,
	      size_t * len)
{
	u32 table_type;
	size_t size;
	ACPI_BUFFER buf;
	u8* data;

	table_type = (u32) ctl->data;
	size = 0;
	buf.length = 0;
	buf.pointer = NULL;

	/* determine what buffer size we will need */
	if (acpi_get_table(table_type, 1, &buf) != AE_BUFFER_OVERFLOW) {
		*len = 0;
		return 0;
	}

	buf.pointer = kmalloc(buf.length, GFP_KERNEL);
	if (!buf.pointer) {
		return -ENOMEM;
	}

	/* get the table for real */
	if (!ACPI_SUCCESS(acpi_get_table(table_type, 1, &buf))) {
		kfree(buf.pointer);
		*len = 0;
		return 0;
	}

	if (file->f_pos < buf.length) {
		data = buf.pointer + file->f_pos;
		size = buf.length - file->f_pos;
		if (size > *len)
			size = *len;
		if (copy_to_user(buffer, data, size))
			return -EFAULT;
	}

	kfree(buf.pointer);

	*len = size;
	file->f_pos += size;
	return 0;
}

/********************************************************************/
/*              R U N    Q U E U E D   C A L L B A C K              */
/*                                                                  */
/* The "callback" function address that was tramped through via     */
/* "acpi_run" below is finally called and executed. If we trace all */
/* this down, the function is acpi_ev_asynch_execute_gpe_method, in */ 
/* evevent.c   The only other function that is ever queued is       */
/* acpi_ev_global_lock_thread in evmisc.c.                          */
/********************************************************************/
static void
acpi_run_exec(void *context)
{
	struct acpi_run_entry *entry
		= (struct acpi_run_entry*) context;
	(*entry->callback)(entry->context);
	kfree(entry);
}

/*
 * Queue for execution by the ACPI thread
 */
int
acpi_run(void (*callback)(void*), void *context)
{
	struct acpi_run_entry *entry;

	entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
	if (!entry)
		return -1;

	memset(entry, 0, sizeof(entry));
	entry->callback = callback;
	entry->context = context;
	entry->task.routine = acpi_run_exec;
	entry->task.data = entry;

	queue_task(&entry->task, &acpi_thread_run);

	if (waitqueue_active(&acpi_thread_wait))
		wake_up(&acpi_thread_wait);

	return 0;
}

static struct ctl_table acpi_table[] =
{
	{ACPI_P_LVL2_LAT, "c2_exit_latency",
	 &acpi_c2_exit_latency, sizeof(acpi_c2_exit_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL2_LAT, "c2_enter_latency",
	 &acpi_c2_enter_latency, sizeof(acpi_c2_enter_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_P_LVL3_LAT, "c3_exit_latency",
	 &acpi_c3_exit_latency, sizeof(acpi_c3_exit_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_ENTER_LVL3_LAT, "c3_enter_latency",
	 &acpi_c3_enter_latency, sizeof(acpi_c3_enter_latency),
	 0644, NULL, &acpi_do_ulong},

	{ACPI_SLEEP, "sleep", NULL, 0, 0600, NULL, &acpi_do_sleep},

	{ACPI_EVENT, "event", NULL, 0, 0400, NULL, &acpi_do_event},

	{ACPI_FADT, "fadt", (void *) ACPI_TABLE_FADT, sizeof(int),
	 0444, NULL, &acpi_do_table},
	
	{ACPI_DSDT, "dsdt", (void *) ACPI_TABLE_DSDT, sizeof(int),
	 0444, NULL, &acpi_do_table},

	{ACPI_FACS, "facs", (void *) ACPI_TABLE_FACS, sizeof(int),
	 0444, NULL, &acpi_do_table},

	{ACPI_XSDT, "xsdt", (void *) ACPI_TABLE_XSDT, sizeof(int),
	 0444, NULL, &acpi_do_table},

	{ACPI_PMTIMER, "pm_timer", NULL, 0, 0444, NULL, &acpi_do_pm_timer},
	
	{0}
};

static struct ctl_table acpi_dir_table[] =
{
	{CTL_ACPI, "acpi", NULL, 0, 0555, acpi_table},
	{0}
};

/*
 * Initialize and run interpreter within a kernel thread
 */
static int
acpi_thread(void *context)
{
	ACPI_PHYSICAL_ADDRESS rsdp_phys;

	/*
	 * initialize
	 */
	daemonize();
	strcpy(current->comm, "kacpid");

	if (!ACPI_SUCCESS(acpi_initialize_subsystem())) {
		printk(KERN_ERR "ACPI: Driver initialization failed\n");
		return -ENODEV;
	}

#ifndef ACPI_CAN_USE_EFI_STRUCT
	if (!ACPI_SUCCESS(acpi_find_root_pointer(&rsdp_phys))) {
		printk(KERN_ERR "ACPI: System description tables not found\n");
		return -ENODEV;
	}
#else
	rsdp_phys = efi.acpi;
#endif
		
	printk(KERN_ERR "ACPI: System description tables found\n");
	
	if (!ACPI_SUCCESS(acpi_find_and_load_tables(rsdp_phys)))
		return -ENODEV;

	if (PM_IS_ACTIVE()) {
		printk(KERN_NOTICE "ACPI: APM is already active, exiting\n");
		acpi_terminate();
		return -ENODEV;
	}

	if (!ACPI_SUCCESS(acpi_enable_subsystem(ACPI_FULL_INITIALIZATION))) {
		printk(KERN_ERR "ACPI: Subsystem enable failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	printk(KERN_ERR "ACPI: Subsystem enabled\n");

	pm_active = 1;

	acpi_cpu_init();
	acpi_sys_init();
	acpi_ec_init();
	acpi_power_init();

	/* 
	 * Non-intuitive: 0 means pwr and sleep are implemented using the fixed
	 * feature model, so we install handlers. 1 means a control method
	 * implementation, or none at all, so do nothing. See ACPI spec.
	 */
	if (acpi_fadt.pwr_button == 0) {
		if (!ACPI_SUCCESS(acpi_install_fixed_event_handler(
			ACPI_EVENT_POWER_BUTTON,
			acpi_event,
			(void *) ACPI_EVENT_POWER_BUTTON))) {
			printk(KERN_ERR "ACPI: power button enable failed\n");
		}
	}

	if (acpi_fadt.sleep_button == 0) {
		if (!ACPI_SUCCESS(acpi_install_fixed_event_handler(
			ACPI_EVENT_SLEEP_BUTTON,
			acpi_event,
			(void *) ACPI_EVENT_SLEEP_BUTTON))) {
			printk(KERN_ERR "ACPI: sleep button enable failed\n");
		}
	}

	acpi_sysctl = register_sysctl_table(acpi_dir_table, 1);

	/*
	 * run
	 */
	for (;;) {
		DECLARE_WAITQUEUE(wait, current);

		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&acpi_thread_wait, &wait);

		if (list_empty(&acpi_thread_run))
			schedule();

		remove_wait_queue(&acpi_thread_wait, &wait);
		set_current_state(TASK_RUNNING);

		if (signal_pending(current))
			break;

		run_task_queue(&acpi_thread_run);
	}

	/*
	 * terminate
	 */
	unregister_sysctl_table(acpi_sysctl);

	/* do not terminate, because we need acpi in order to shut down */
	/*acpi_terminate();*/

	acpi_thread_pid = -1;

	return 0;
}

/*
 * Start the interpreter
 */
int __init
acpi_init(void)
{
	acpi_thread_pid = kernel_thread(acpi_thread,
					NULL,
					(CLONE_FS | CLONE_FILES
					 | CLONE_SIGHAND | SIGCHLD));
	return ((acpi_thread_pid >= 0) ? 0:-ENODEV);
}

/*
 * Terminate the interpreter
 */
void __exit
acpi_exit(void)
{
	int count;

	if (!kill_proc(acpi_thread_pid, SIGTERM, 1)) {
		// wait until thread terminates (at most 5 seconds)
		count = 5 * HZ;
		while (acpi_thread_pid >= 0 && --count) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
	}

	pm_idle = NULL;
	pm_power_off = NULL;
	pm_active = 0;
}

module_init(acpi_init);
module_exit(acpi_exit);
