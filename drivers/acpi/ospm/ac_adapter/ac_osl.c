/*****************************************************************************
 *
 * Module Name: ac_osl.c
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
#include <acpi.h>
#include "ac.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - AC Adapter Driver");
MODULE_LICENSE("GPL");


#define AC_PROC_ROOT		"ac_adapter"
#define AC_PROC_STATUS		"status"
#define AC_ON_LINE		"on-line"
#define AC_OFF_LINE		"off-line"

extern struct proc_dir_entry	*bm_proc_root;
static struct proc_dir_entry	*ac_proc_root = NULL;


/****************************************************************************
 *
 * FUNCTION:	ac_osl_proc_read_status
 *
 ****************************************************************************/

static int
ac_osl_proc_read_status (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	acpi_status 		status = AE_OK;
	AC_CONTEXT		*ac_adapter = NULL;
	char			*p = page;
	int			len;

	if (!context) {
		goto end;
	}

	ac_adapter = (AC_CONTEXT*)context;

	/* don't get status more than once for a single proc read */
	if (off != 0) {
		goto end;
	}

	status = bm_evaluate_simple_integer(ac_adapter->acpi_handle,
		"_PSR", &(ac_adapter->is_online));
	if (ACPI_FAILURE(status)) {
		p += sprintf(p, "Error reading AC Adapter status\n");
		goto end;
	}

	if (ac_adapter->is_online) {
		p += sprintf(p, "Status:                  %s\n",
			AC_ON_LINE);
	}
	else {
		p += sprintf(p, "Status:                  %s\n",
			AC_OFF_LINE);
	}

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
 * FUNCTION:	ac_osl_add_device
 *
 ****************************************************************************/

acpi_status
ac_osl_add_device(
	AC_CONTEXT		*ac_adapter)
{
	struct proc_dir_entry	*proc_entry = NULL;

	if (!ac_adapter) {
		return(AE_BAD_PARAMETER);
	}

	printk(KERN_INFO "ACPI: AC Adapter found\n");

	proc_entry = proc_mkdir(ac_adapter->uid, ac_proc_root);
	if (!proc_entry) {
		return(AE_ERROR);
	}

	create_proc_read_entry(AC_PROC_STATUS, S_IFREG | S_IRUGO,
		proc_entry, ac_osl_proc_read_status, (void*)ac_adapter);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	ac_osl_remove_device
 *
 ****************************************************************************/

acpi_status
ac_osl_remove_device (
	AC_CONTEXT		*ac_adapter)
{
	char			proc_entry[64];

	if (!ac_adapter) {
		return(AE_BAD_PARAMETER);
	}

	sprintf(proc_entry, "%s/%s", ac_adapter->uid, AC_PROC_STATUS);
	remove_proc_entry(proc_entry, ac_proc_root);

	sprintf(proc_entry, "%s", ac_adapter->uid);
	remove_proc_entry(proc_entry, ac_proc_root);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	ac_osl_generate_event
 *
 ****************************************************************************/

acpi_status
ac_osl_generate_event (
	u32			event,
	AC_CONTEXT		*ac_adapter)
{
	acpi_status		status = AE_OK;

	if (!ac_adapter) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	case AC_NOTIFY_STATUS_CHANGE:
		status = bm_osl_generate_event(ac_adapter->device_handle,
			AC_PROC_ROOT, ac_adapter->uid, event, 0);
		break;

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	ac_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
ac_osl_init (void)
{
	acpi_status		status = AE_OK;

	ac_proc_root = proc_mkdir(AC_PROC_ROOT, bm_proc_root);
	if (!ac_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = ac_initialize();
		if (ACPI_FAILURE(status)) {
			remove_proc_entry(AC_PROC_ROOT, bm_proc_root);
		}

	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:	ac_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
ac_osl_cleanup (void)
{
	ac_terminate();

	if (ac_proc_root) {
		remove_proc_entry(AC_PROC_ROOT, bm_proc_root);
	}

	return;
}


module_init(ac_osl_init);
module_exit(ac_osl_cleanup);
