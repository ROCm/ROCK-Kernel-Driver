/******************************************************************************
 *
 * Module Name: bt.h
 *   $Revision: 13 $
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
 *                            Types & Other Defines
 *****************************************************************************/

/*! [Begin] no source code translation */

#define BT_UNKNOWN		0xFFFFFFFF
#define BT_POWER_UNITS_DEFAULT	"?"
#define BT_POWER_UNITS_WATTS	"mW"
#define BT_POWER_UNITS_AMPS	"mA"

/*! [End] no source code translation */

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
#define BT_HID_CM_BATTERY	"PNP0C0A"


/*
 * BT_CM_BATTERY_INFO:
 * -------------------
 */
typedef struct
{
	u32			power_unit;
	u32			design_capacity;
	u32			last_full_capacity;
	u32			battery_technology;
	u32			design_voltage;
	u32			design_capacity_warning;
	u32			design_capacity_low;
	u32			battery_capacity_granularity_1;
	u32			battery_capacity_granularity_2;
	ACPI_STRING 		model_number;
	ACPI_STRING 		serial_number;
	ACPI_STRING 		battery_type;
	ACPI_STRING 		oem_info;
} BT_BATTERY_INFO;


/*
 * BT_CM_BATTERY_STATUS:
 * ---------------------
 */
typedef struct
{
	u32			state;
	u32			present_rate;
	u32			remaining_capacity;
	u32			present_voltage;
} BT_BATTERY_STATUS;


/*
 * BT_CONTEXT:
 * -----------
 */
typedef struct
{
	BM_HANDLE		device_handle;
	ACPI_HANDLE 		acpi_handle;
	char			uid[9];
	ACPI_STRING 		power_units;
	BOOLEAN 		is_present;
} BT_CONTEXT;


/*****************************************************************************
 *                              Function Prototypes
 *****************************************************************************/

/* bt.c */

ACPI_STATUS
bt_initialize (void);

ACPI_STATUS
bt_terminate (void);

ACPI_STATUS
bt_notify (
	u32			notify_type,
	u32			device,
	void			**context);

ACPI_STATUS
bt_request(
	BM_REQUEST		*request_info,
	void			*context);

ACPI_STATUS
bt_get_status (
	BT_CONTEXT		*battery,
	BT_BATTERY_STATUS	**battery_status);

ACPI_STATUS
bt_get_info (
	BT_CONTEXT		*battery,
	BT_BATTERY_INFO 	**battery_info);

/* Battery OSL */

ACPI_STATUS
bt_osl_add_device (
	BT_CONTEXT		*battery);

ACPI_STATUS
bt_osl_remove_device (
	BT_CONTEXT		*battery);

ACPI_STATUS
bt_osl_generate_event (
	u32			event,
	BT_CONTEXT		*battery);


#endif	/* __BT_H__ */
