/******************************************************************************
 *
 * Module Name: bt_osl.c
 *   $Revision: 24 $
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

/*
 * Changes:
 * Brendan Burns <bburns@wso.williams.edu> 2000-11-15
 * - added proc battery interface
 * - parse returned data from _BST and _BIF
 * Andy Grover <andrew.grover@intel.com> 2000-12-8
 * - improved proc interface
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <acpi.h>
#include "bt.h"


MODULE_AUTHOR("Andrew Grover");
MODULE_DESCRIPTION("ACPI Component Architecture (CA) - Control Method Battery Driver");
MODULE_LICENSE("GPL");


#define BT_PROC_ROOT		"battery"
#define BT_PROC_STATUS		"status"
#define BT_PROC_INFO		"info"

extern struct proc_dir_entry	*bm_proc_root;
static struct proc_dir_entry	*bt_proc_root = NULL;


/****************************************************************************
 *
 * FUNCTION:	bt_osl_proc_read_info
 *
 ****************************************************************************/

static int
bt_osl_proc_read_info (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	BT_CONTEXT		*battery = NULL;
	BT_BATTERY_INFO 	*battery_info = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	battery = (BT_CONTEXT*)context;

	/*
	 * Battery Present?
	 * ----------------
	 */
	if (!battery->is_present) {
		p += sprintf(p, "Present:                 no\n");
		goto end;
	}
	else {
		p += sprintf(p, "Present:                 yes\n");
	}

	/*
	 * Get Battery Information:
	 * ------------------------
	 */
	if (ACPI_FAILURE(bt_get_info(battery, &battery_info))) {
		p += sprintf(p, "Error reading battery information (_BIF)\n");
		goto end;
	}

	if (battery_info->design_capacity == BT_UNKNOWN) {
		p += sprintf(p, "Design Capacity:         unknown\n");
	}
	else {
		p += sprintf(p, "Design Capacity:         %d %sh\n",
			 (u32)battery_info->design_capacity,
			 battery->power_units);
	}
	
	if (battery_info->last_full_capacity == BT_UNKNOWN) {
		p += sprintf(p, "Last Full Capacity:      unknown\n");
	}
	else {
		p += sprintf(p, "Last Full Capacity:      %d %sh\n",
			 (u32)battery_info->last_full_capacity,
			 battery->power_units);
	}

	if (battery_info->battery_technology == 0) {
		p += sprintf(p, "Battery Technology:      primary (non-rechargeable)\n");
	}
	else if (battery_info->battery_technology == 1) {
		p += sprintf(p, "Battery Technology:      secondary (rechargeable)\n");
	}
	else {
		p += sprintf(p, "Battery Technology:      unknown\n");
	}

	if (battery_info->design_voltage == BT_UNKNOWN) {
		p += sprintf(p, "Design Voltage:          unknown\n");
	}
	else {
		p += sprintf(p, "Design Voltage:          %d mV\n",
			 (u32)battery_info->design_voltage);
	}
	
	p += sprintf(p, "Design Capacity Warning: %d %sh\n",
		(u32)battery_info->design_capacity_warning,
		battery->power_units);
	p += sprintf(p, "Design Capacity Low:     %d %sh\n",
		(u32)battery_info->design_capacity_low,
		battery->power_units);
	p += sprintf(p, "Capacity Granularity 1:  %d %sh\n",
		(u32)battery_info->battery_capacity_granularity_1,
		battery->power_units);
	p += sprintf(p, "Capacity Granularity 2:  %d %sh\n",
		(u32)battery_info->battery_capacity_granularity_2,
		battery->power_units);
	p += sprintf(p, "Model Number:            %s\n",
		battery_info->model_number);
	p += sprintf(p, "Serial Number:           %s\n",
		battery_info->serial_number);
	p += sprintf(p, "Battery Type:            %s\n",
		battery_info->battery_type);
	p += sprintf(p, "OEM Info:                %s\n",
		battery_info->oem_info);
	
end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	acpi_os_free(battery_info);

	return(len);
}


/****************************************************************************
 *
 * FUNCTION:	bt_osl_proc_read_status
 *
 ****************************************************************************/

static int
bt_osl_proc_read_status (
	char			*page,
	char			**start,
	off_t			off,
	int 			count,
	int 			*eof,
	void			*context)
{
	BT_CONTEXT		*battery = NULL;
	BT_BATTERY_STATUS	*battery_status = NULL;
	char			*p = page;
	int 			len = 0;

	if (!context || (off != 0)) {
		goto end;
	}

	battery = (BT_CONTEXT*)context;

	/*
	 * Battery Present?
	 * ----------------
	 */
	if (!battery->is_present) {
		p += sprintf(p, "Present:                 no\n");
		goto end;
	}
	else {
		p += sprintf(p, "Present:                 yes\n");
	}

	/*
	 * Get Battery Status:
	 * -------------------
	 */
	if (ACPI_FAILURE(bt_get_status(battery, &battery_status))) {
		p += sprintf(p, "Error reading battery status (_BST)\n");
		goto end;
	}

	/*
	 * Store Data:
	 * -----------
	 */

	if (!battery_status->state) {
		p += sprintf(p, "State:                   ok\n");
	}
	else {
		if (battery_status->state & 0x1)
			p += sprintf(p, "State:                   discharging\n");
		if (battery_status->state & 0x2)
			p += sprintf(p, "State:                   charging\n");
		if (battery_status->state & 0x4)
			p += sprintf(p, "State:                   critically low\n");
	}

	if (battery_status->present_rate == BT_UNKNOWN) {
		p += sprintf(p, "Present Rate:            unknown\n");
	}
	else {
		p += sprintf(p, "Present Rate:            %d %s\n",
			(u32)battery_status->present_rate,
			battery->power_units);
	}

	if (battery_status->remaining_capacity == BT_UNKNOWN) {
		p += sprintf(p, "Remaining Capacity:      unknown\n");
	}
	else {
		p += sprintf(p, "Remaining Capacity:      %d %sh\n",
			(u32)battery_status->remaining_capacity,
			battery->power_units);
	}

	if (battery_status->present_voltage == BT_UNKNOWN) {
		p += sprintf(p, "Battery Voltage:         unknown\n");
	}
	else {
		p += sprintf(p, "Battery Voltage:         %d mV\n",
			(u32)battery_status->present_voltage);
	}

end:
	len = (p - page);
	if (len <= off+count) *eof = 1;
	*start = page + off;
	len -= off;
	if (len>count) len = count;
	if (len<0) len = 0;

	acpi_os_free(battery_status);

	return(len);
}


/****************************************************************************
 *
 * FUNCTION:	bt_osl_add_device
 *
 ****************************************************************************/

acpi_status
bt_osl_add_device(
	BT_CONTEXT		*battery)
{
	struct proc_dir_entry	*proc_entry = NULL;

	if (!battery) {
		return(AE_BAD_PARAMETER);
	}

	if (battery->is_present) {
		printk("ACPI: Battery socket found, battery present\n");
	}
	else {
		printk("ACPI: Battery socket found, battery absent\n");
	}

	proc_entry = proc_mkdir(battery->uid, bt_proc_root);
	if (!proc_entry) {
		return(AE_ERROR);
	}

	create_proc_read_entry(BT_PROC_STATUS, S_IFREG | S_IRUGO,
		proc_entry, bt_osl_proc_read_status, (void*)battery);

	create_proc_read_entry(BT_PROC_INFO, S_IFREG | S_IRUGO,
		proc_entry, bt_osl_proc_read_info, (void*)battery);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	bt_osl_remove_device
 *
 ****************************************************************************/

acpi_status
bt_osl_remove_device (
	BT_CONTEXT		*battery)
{
	char			proc_entry[64];

	if (!battery) {
		return(AE_BAD_PARAMETER);
	}

	sprintf(proc_entry, "%s/%s", battery->uid, BT_PROC_INFO);
	remove_proc_entry(proc_entry, bt_proc_root);

	sprintf(proc_entry, "%s/%s", battery->uid, BT_PROC_STATUS);
	remove_proc_entry(proc_entry, bt_proc_root);

	sprintf(proc_entry, "%s", battery->uid);
	remove_proc_entry(proc_entry, bt_proc_root);

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	bt_osl_generate_event
 *
 ****************************************************************************/

acpi_status
bt_osl_generate_event (
	u32			event,
	BT_CONTEXT		*battery)
{
	acpi_status		status = AE_OK;

	if (!battery) {
		return(AE_BAD_PARAMETER);
	}

	switch (event) {

	case BT_NOTIFY_STATUS_CHANGE:
	case BT_NOTIFY_INFORMATION_CHANGE:
		status = bm_osl_generate_event(battery->device_handle,
			BT_PROC_ROOT, battery->uid, event, 0);
		break;

	default:
		return(AE_BAD_PARAMETER);
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	bt_osl_init
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	0: Success
 *
 * DESCRIPTION: Module initialization.
 *
 ****************************************************************************/

static int __init
bt_osl_init (void)
{
	acpi_status		status = AE_OK;

	/* abort if no busmgr */
	if (!bm_proc_root)
		return -ENODEV;

	bt_proc_root = proc_mkdir(BT_PROC_ROOT, bm_proc_root);
	if (!bt_proc_root) {
		status = AE_ERROR;
	}
	else {
		status = bt_initialize();
		if (ACPI_FAILURE(status)) {
			remove_proc_entry(BT_PROC_ROOT, bm_proc_root);
		}
	}

	return (ACPI_SUCCESS(status)) ? 0 : -ENODEV;
}


/****************************************************************************
 *
 * FUNCTION:	bt_osl_cleanup
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	<none>
 *
 * DESCRIPTION: Module cleanup.
 *
 ****************************************************************************/

static void __exit
bt_osl_cleanup (void)
{
	bt_terminate();

	if (bt_proc_root) {
		remove_proc_entry(BT_PROC_ROOT, bm_proc_root);
	}

	return;
}


module_init(bt_osl_init);
module_exit(bt_osl_cleanup);
