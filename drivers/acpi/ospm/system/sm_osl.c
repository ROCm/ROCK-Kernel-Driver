/******************************************************************************
 *
 * Module Name: sm_osl.c
 *   $Revision: 10 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/pm.h>
#include <asm/uaccess.h>

#include <acpi.h>
#include "sm.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - ACPI System Driver");
MODULE_LICENSE("GPL");


#define SM_PROC_INFO		"info"
#define SM_PROC_DSDT		"dsdt"

extern struct proc_dir_entry	*bm_proc_root;
struct proc_dir_entry		*sm_proc_root = NULL;
static void 			(*sm_pm_power_off)(void) = NULL;

static ssize_t sm_osl_read_dsdt(struct file *, char *, size_t, loff_t *);

static struct file_operations proc_dsdt_operations = {
	read:		sm_osl_read_dsdt,
};



/****************************************************************************
 *
 * FUNCTION:	sm_osl_proc_read_info
 *
 ****************************************************************************/

static int
sm_osl_proc_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	ACPI_STATUS		status = AE_OK;
	SM_CONTEXT		*system = NULL;
	char			*p = page;
	int			len;
	ACPI_SYSTEM_INFO	system_info;
	ACPI_BUFFER		buffer;
	u32			i = 0;

	if (!context) {
		goto end;
	}

	system = (SM_CONTEXT*)context;

	/* don't get status more than once for a single proc read */
	if (off != 0) {
		goto end;
	}

	/*
	 * Get ACPI CA Information.
	 */
	buffer.length  = sizeof(system_info);
	buffer.pointer = &system_info;

	status = acpi_get_system_info(&buffer);
	if (ACPI_FAILURE(status)) {
		p += sprintf(p, "ACPI-CA Version:         unknown\n");
	}
	else {
		p += sprintf(p, "ACPI-CA Version:         %x\n", 
			system_info.acpi_ca_version);
	}

	p += sprintf(p, "Sx States Supported:     ");
	for (i=0; i<SM_MAX_SYSTEM_STATES; i++) {
		if (system->states[i]) {
			p += sprintf(p, "S%d ", i);
		}
	}
	p += sprintf(p, "\n");

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return(len);
}

/****************************************************************************
 *
 * FUNCTION:	sm_osl_read_dsdt
 *
 ****************************************************************************/

static ssize_t
sm_osl_read_dsdt(
	struct file		*file, 
	char			*buf, 
	size_t			count, 
	loff_t			*ppos)
{
	ACPI_BUFFER		acpi_buf;
	void			*data;
	size_t			size = 0;

	acpi_buf.length = 0;
	acpi_buf.pointer = NULL;


	/* determine what buffer size we will need */
	if (acpi_get_table(ACPI_TABLE_DSDT, 1, &acpi_buf) != AE_BUFFER_OVERFLOW) {
		return 0;
	}

	acpi_buf.pointer = kmalloc(acpi_buf.length, GFP_KERNEL);
	if (!acpi_buf.pointer) {
		return -ENOMEM;
	}

	/* get the table for real */
	if (!ACPI_SUCCESS(acpi_get_table(ACPI_TABLE_DSDT, 1, &acpi_buf))) {
		kfree(acpi_buf.pointer);
		return 0;
	}

	if (*ppos < acpi_buf.length) {
		data = acpi_buf.pointer + file->f_pos;
		size = acpi_buf.length - file->f_pos;
		if (size > count)
			size = count;
		if (copy_to_user(buf, data, size)) {
			kfree(acpi_buf.pointer);
			return -EFAULT;
		}
	}

	kfree(acpi_buf.pointer);

	*ppos += size;

	return size;
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_power_down
 *
 ****************************************************************************/

void
sm_osl_power_down (void)
{
	ACPI_STATUS		status = AE_OK;

	/* Power down the system (S5 = soft off). */
	status = acpi_enter_sleep_state(ACPI_STATE_S5);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_add_device
 *
 ****************************************************************************/

ACPI_STATUS
sm_osl_add_device(
	SM_CONTEXT		*system)
{
	u32			i = 0;
	struct proc_dir_entry	*bm_proc_dsdt;

	if (!system) {
		return(AE_BAD_PARAMETER);
	}

	printk("ACPI: System firmware supports");
	for (i=0; i<SM_MAX_SYSTEM_STATES; i++) {
		if (system->states[i]) {
			printk(" S%d", i);
		}
	}
	printk("\n");

	if (system->states[ACPI_STATE_S5]) {
		sm_pm_power_off = pm_power_off;
		pm_power_off = sm_osl_power_down; 
	}

	create_proc_read_entry(SM_PROC_INFO, S_IRUGO, 
		sm_proc_root, sm_osl_proc_read_info, (void*)system);

	/* 
	 * This returns more than a page, so we need to use our own file ops,
	 * not proc's generic ones
	 */
	bm_proc_dsdt = create_proc_entry(SM_PROC_DSDT, S_IRUSR, sm_proc_root);
	if (bm_proc_dsdt) {
		bm_proc_dsdt->proc_fops = &proc_dsdt_operations;
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_remove_device
 *
 ****************************************************************************/

ACPI_STATUS
sm_osl_remove_device (
	SM_CONTEXT		*system)
{
	if (!system) {
		return(AE_BAD_PARAMETER);
	}

	remove_proc_entry(SM_PROC_INFO, sm_proc_root);
	remove_proc_entry(SM_PROC_DSDT, sm_proc_root);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_generate_event
 *
 ****************************************************************************/

ACPI_STATUS
sm_osl_generate_event (
	u32			event,
	SM_CONTEXT		*system)
{
	ACPI_STATUS		status = AE_OK;

	if (!system) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init 
sm_osl_init (void)
{
	ACPI_STATUS		status = AE_OK;

	sm_proc_root = bm_proc_root;
	if (!sm_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = sm_initialize();
	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:	sm_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit 
sm_osl_cleanup (void)
{
	sm_terminate();

	return;
}


module_init(sm_osl_init);
module_exit(sm_osl_cleanup);
