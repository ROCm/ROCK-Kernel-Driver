/******************************************************************************
 *
 * Module Name: tz_osl.c
 *   $Revision: 25 $
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
	acpi_status		status = AE_OK;
	char			name[5];
	acpi_buffer		buffer = {sizeof(name), &name};
	TZ_CONTEXT		*tz = NULL;
	TZ_THRESHOLDS		*thresholds = NULL;
	char			*p = page;
	int 			len = 0;
	u32			i,j;
	u32			t = 0;

	if (!context || (off != 0))
		goto end;

	tz = (TZ_CONTEXT*)context;

	thresholds = &(tz->policy.thresholds);

	p += sprintf(p, "critical (S5): trip=%d\n", thresholds->critical.temperature);
	
	if (thresholds->hot.is_valid)
		p += sprintf(p, "critical (S4): trip=%d\n", thresholds->hot.temperature);

	if (thresholds->passive.is_valid) {
		p += sprintf(p, "passive:       trip=%d tc1=%d tc2=%d tsp=%d devices=", thresholds->passive.temperature, thresholds->passive.tc1, thresholds->passive.tc2, thresholds->passive.tsp);
		for (j=0; j<thresholds->passive.devices.count; j++)
			p += sprintf(p, "%08x%c", thresholds->passive.devices.handles[j], (j==thresholds->passive.devices.count-1)?'\n':',');
	}

	for (i=0; i<TZ_MAX_ACTIVE_THRESHOLDS; i++) {
		if (!(thresholds->active[i].is_valid))
			break;
		p += sprintf(p, "active[%d]:     trip=%d devices=", i, thresholds->active[i].temperature);
		for (j=0; j<thresholds->active[i].devices.count; j++)
			p += sprintf(p, "%08x%c", thresholds->active[i].devices.handles[j], (j==thresholds->passive.devices.count-1)?'\n':',');
	}

	p += sprintf(p, "cooling mode:  ");
	switch (tz->policy.cooling_mode) {
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

	p += sprintf(p, "polling:       ");
	switch (tz->policy.polling_freq) {
	case 0:
		p += sprintf(p, "disabled\n");
		break;
	default:
		p += sprintf(p, "%d dS\n", tz->policy.polling_freq);
		break;
	}

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}


/****************************************************************************
 *
 * FUNCTION:	tz_osl_proc_write_info
 *
 ****************************************************************************/

static int tz_osl_proc_write_info (
        struct file		*file, 
        const char		*buffer, 
        unsigned long		count, 
        void			*data)
{
	TZ_CONTEXT		*tz = NULL;
	u32			state = 0;
	u32			size = 0;

	if (!buffer || (count==0) || !data) {
		goto end;
	}

	tz = (TZ_CONTEXT*)data;

	size = strlen(buffer);
	if (size < 4)
		goto end;
	
	/* Cooling preference: "scp=0" (active) or "scp=1" (passive) */
	if (0 == strncmp(buffer, "scp=", 4)) {
		tz_set_cooling_preference(tz, (buffer[4] - '0'));
	}

	/* Polling frequency: "tzp=X" (poll every X [0-9] seconds) */
	else if (0 == strncmp(buffer, "tzp=", 4)) {
		tz->policy.polling_freq = (buffer[4] - '0') * 10;
		tz_policy_check(tz);
	}

end:
        return count;
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
	TZ_CONTEXT		*tz = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	tz = (TZ_CONTEXT*)context;

	/* Temperature */

	tz_get_temperature(tz);

	p += sprintf(p, "temperature:   %d dK\n", tz->policy.temperature);

	p += sprintf(p, "state:         ");
	if (tz->policy.state == 0)
		p += sprintf(p, "ok\n");
	else if (tz->policy.state & TZ_STATE_CRITICAL)
		p += sprintf(p, "critical\n");
	else if (tz->policy.state & TZ_STATE_HOT)
		p += sprintf(p, "hot\n");
	else {
		if (tz->policy.state & TZ_STATE_ACTIVE)
			p += sprintf(p, "active[%d] ", tz->policy.state & 0x07);
		if (tz->policy.state & TZ_STATE_PASSIVE)
			p += sprintf(p, "passive ");
		p += sprintf(p, "\n");
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
	TZ_CONTEXT		*tz)
{
	struct proc_dir_entry	*proc_entry = NULL;
	struct proc_dir_entry	*proc_child_entry = NULL;

	if (!tz) {
		return(AE_BAD_PARAMETER);
	}

	printk("ACPI: Thermal Zone found\n");

	proc_entry = proc_mkdir(tz->uid, tz_proc_root);
	if (!proc_entry) 
		return(AE_ERROR);

	proc_child_entry = create_proc_read_entry(TZ_PROC_STATUS, S_IFREG | S_IRUGO, proc_entry, tz_osl_proc_read_status, (void*)tz);
	if (!proc_child_entry) 
		return(AE_ERROR);

	proc_child_entry = create_proc_entry(TZ_PROC_INFO, S_IFREG | 0644, proc_entry);
	if (!proc_child_entry)
		return(AE_ERROR);

	proc_child_entry->read_proc = tz_osl_proc_read_info;
	proc_child_entry->write_proc = tz_osl_proc_write_info;
	proc_child_entry->data = (void*)tz;

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	tz_osl_remove_device
 *
 ****************************************************************************/

acpi_status
tz_osl_remove_device (
	TZ_CONTEXT		*tz)
{
	char			proc_entry[64];

	if (!tz) {
		return(AE_BAD_PARAMETER);
	}

	sprintf(proc_entry, "%s/%s", tz->uid, TZ_PROC_INFO);
	remove_proc_entry(proc_entry, tz_proc_root);

	sprintf(proc_entry, "%s/%s", tz->uid, TZ_PROC_STATUS);
	remove_proc_entry(proc_entry, tz_proc_root);

	sprintf(proc_entry, "%s", tz->uid);
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
	TZ_CONTEXT		*tz)
{
	acpi_status		status = AE_OK;

	if (!tz) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	case TZ_NOTIFY_TEMPERATURE_CHANGE:
		status = bm_osl_generate_event(tz->device_handle,
			TZ_PROC_ROOT, tz->uid, event,
			tz->policy.temperature);
		break;

	case TZ_NOTIFY_THRESHOLD_CHANGE:
	case TZ_NOTIFY_DEVICE_LISTS_CHANGE:
		status = bm_osl_generate_event(tz->device_handle,
			TZ_PROC_ROOT, tz->uid, event, 0);
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
