/******************************************************************************
 *
 * Module Name: bt.h
 *   $Revision: 18 $
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


#ifndef __BT_H__
#define __BT_H__

#include <actypes.h>
#include <acexcep.h>
#include <bm.h>


/*****************************************************************************
 *                Types & Other Defines
 *****************************************************************************/

/*! [Begin] no source code translation */

#define BT_UNKNOWN		0xFFFFFFFF
#define BT_POWER_UNITS_DEFAULT  "?"
#define BT_POWER_UNITS_WATTS    "mW"
#define BT_POWER_UNITS_AMPS	"mA"

/*! [End] no source code translation !*/

/*
 * Battery Notifications:
 * ----------------------
 */
#define BT_NOTIFY_STATUS_CHANGE ((BM_NOTIFY) 0x80)
#define BT_NOTIFY_INFORMATION_CHANGE ((BM_NOTIFY) 0x81)


/*
 * Hardware IDs:
 * -------------
 */
#define BT_HID_CM_BATTERY   "PNP0C0A"


/*
 * BT_CM_BATTERY_INFO:
 * -------------------
 */
typedef struct
{
	acpi_integer    power_unit;
	acpi_integer    design_capacity;
	acpi_integer    last_full_capacity;
	acpi_integer    battery_technology;
	acpi_integer    design_voltage;
	acpi_integer    design_capacity_warning;
	acpi_integer    design_capacity_low;
	acpi_integer    battery_capacity_granularity_1;
	acpi_integer    battery_capacity_granularity_2;
	acpi_string     model_number;
	acpi_string     serial_number;
	acpi_string     battery_type;
	acpi_string     oem_info;

} BT_BATTERY_INFO;


/*
 * BT_CM_BATTERY_STATUS:
 * ---------------------
 */
typedef struct
{
	acpi_integer    state;
	acpi_integer    present_rate;
	acpi_integer    remaining_capacity;
	acpi_integer    present_voltage;

} BT_BATTERY_STATUS;


/*
 * BT_CONTEXT:
 * -----------
 */
typedef struct
{
	BM_HANDLE       device_handle;
	acpi_handle     acpi_handle;
	char            uid[9];
	acpi_string     power_units;
	u8              is_present;

} BT_CONTEXT;


/*****************************************************************************
 *              Function Prototypes
 *****************************************************************************/

/* bt.c */

acpi_status
bt_initialize (void);

acpi_status
bt_terminate (void);

acpi_status
bt_notify (
	u32         notify_type,
	u32         device,
	void        **context);

acpi_status
bt_request(
	BM_REQUEST  *request_info,
	void        *context);

acpi_status
bt_get_status (
	BT_CONTEXT		*battery,
	BT_BATTERY_STATUS   **battery_status);

acpi_status
bt_get_info (
	BT_CONTEXT      *battery,
	BT_BATTERY_INFO **battery_info);

/* Battery OSL */

acpi_status
bt_osl_add_device (
	BT_CONTEXT      *battery);

acpi_status
bt_osl_remove_device (
	BT_CONTEXT      *battery);

acpi_status
bt_osl_generate_event (
	u32         event,
	BT_CONTEXT      *battery);


#endif  /* __BT_H__ */
