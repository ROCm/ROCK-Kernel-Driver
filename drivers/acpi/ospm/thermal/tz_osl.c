/******************************************************************************
 *
 * Module Name: tz_osl.c
 *   $Revision: 21 $
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
#include "tz.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - Thermal Zone Driver");
MODULE_LICENSE("GPL");

int TZP = 0;
MODULE_PARM(TZP, "i");
MODULE_PARM_DESC(TZP, "Thermal zone polling frequency, in 1/10 seconds.\n");


#define TZ_PROC_ROOT		"thermal"
#define TZ_PROC_STATUS		"status"
#define TZ_PROC_INFO		"info"

extern struct proc_dir_entry	*bm_proc_root;
static struct proc_dir_entry	*tz_proc_root = NULL;


/****************************************************************************
 *
 * FUNCTION:	tz_osl_proc_read_info
 *
 ****************************************************************************/

static int
tz_osl_proc_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	TZ_CONTEXT		*thermal_zone = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	thermal_zone = (TZ_CONTEXT*)context;

	p += sprintf(p, "<TBD>\n");

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
 * FUNCTION:	tz_osl_proc_read_status
 *
 ****************************************************************************/

static int
tz_osl_proc_read_status (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	TZ_CONTEXT		*thermal_zone = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	thermal_zone = (TZ_CONTEXT*)context;

	p += sprintf(p, "Temperature:             %d (1/10th degrees Kelvin)\n",
		thermal_zone->policy.temperature);

	p += sprintf(p, "State:                   ");
	if (thermal_zone->policy.state & TZ_STATE_ACTIVE) {
		p += sprintf(p, "active[%d] ", thermal_zone->policy.state & 0x07);
	}
	if (thermal_zone->policy.state & TZ_STATE_PASSIVE) {
		p += sprintf(p, "passive ");
	}
	if (thermal_zone->policy.state & TZ_STATE_CRITICAL) {
		p += sprintf(p, "critical ");
	}
	if (thermal_zone->policy.state == 0) {
		p += sprintf(p, "ok ");
	}
	p += sprintf(p, "\n");

	p += sprintf(p, "Cooling Mode:            ");
	switch (thermal_zone->policy.cooling_mode) {
	case TZ_COOLING_MODE_ACTIVE:
		p += sprintf(p, "active (noisy)\n");
		break;
	case TZ_COOLING_MODE_PASSIVE:
		p += sprintf(p, "passive (quiet)\n");
		break;
	default:
		p += sprintf(p, "unknown\n");
		break;
	}

	p += sprintf(p, "Polling Frequency:       ");
	switch (thermal_zone->policy.polling_freq) {
	case 0:
		p += sprintf(p, "n/a\n");
		break;
	default:
		p += sprintf(p, "%d (1/10th seconds)\n", thermal_zone->policy.polling_freq);
		break;
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
 * FUNCTION:	tz_osl_add_device
 *
 ****************************************************************************/

acpi_status
tz_osl_add_device(
	TZ_CONTEXT		*thermal_zone)
{
	struct proc_dir_entry	*proc_entry = NULL, *proc;

	if (!thermal_zone) {
		return(AE_BAD_PARAMETER);
	}

	printk("Thermal Zone: found\n");

	proc_entry = proc_mkdir(thermal_zone->uid, tz_proc_root);
	if (!proc_entry)
		return(AE_ERROR);

	proc = create_proc_read_entry(TZ_PROC_STATUS, S_IFREG | S_IRUGO,
		proc_entry, tz_osl_proc_read_status, (void*)thermal_zone);
	if (!proc)
		return(AE_ERROR);

	proc = create_proc_read_entry(TZ_PROC_INFO, S_IFREG | S_IRUGO,
		proc_entry, tz_osl_proc_read_info, (void*)thermal_zone);
	if (!proc)
		return(AE_ERROR);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	tz_osl_remove_device
 *
 ****************************************************************************/

acpi_status
tz_osl_remove_device (
	TZ_CONTEXT		*thermal_zone)
{
	char			proc_entry[64];

	if (!thermal_zone) {
		return(AE_BAD_PARAMETER);
	}

	sprintf(proc_entry, "%s/%s", thermal_zone->uid, TZ_PROC_INFO);
	remove_proc_entry(proc_entry, tz_proc_root);

	sprintf(proc_entry, "%s/%s", thermal_zone->uid, TZ_PROC_STATUS);
	remove_proc_entry(proc_entry, tz_proc_root);

	sprintf(proc_entry, "%s", thermal_zone->uid);
	remove_proc_entry(proc_entry, tz_proc_root);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	tz_osl_generate_event
 *
 ****************************************************************************/

acpi_status
tz_osl_generate_event (
	u32			event,
	TZ_CONTEXT		*thermal_zone)
{
	acpi_status		status = AE_OK;

	if (!thermal_zone) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	case TZ_NOTIFY_TEMPERATURE_CHANGE:
		status = bm_osl_generate_event(thermal_zone->device_handle,
			TZ_PROC_ROOT, thermal_zone->uid, event,
			thermal_zone->policy.temperature);
		break;

	case TZ_NOTIFY_THRESHOLD_CHANGE:
	case TZ_NOTIFY_DEVICE_LISTS_CHANGE:
		status = bm_osl_generate_event(thermal_zone->device_handle,
			TZ_PROC_ROOT, thermal_zone->uid, event, 0);
		break;

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	tz_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
tz_osl_init (void)
{
	acpi_status		status = AE_OK;

	/* abort if no busmgr */
	if (!bm_proc_root)
		return -ENODEV;

	tz_proc_root = proc_mkdir(TZ_PROC_ROOT, bm_proc_root);
	if (!tz_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = tz_initialize();
		if (ACPI_FAILURE(status)) {
			remove_proc_entry(TZ_PROC_ROOT, bm_proc_root);
		}

	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:	tz_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
tz_osl_cleanup (void)
{
	tz_terminate();

	if (tz_proc_root) {
		remove_proc_entry(TZ_PROC_ROOT, bm_proc_root);
	}

	return;
}


module_init(tz_osl_init);
module_exit(tz_osl_cleanup);
