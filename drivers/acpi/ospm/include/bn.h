/******************************************************************************
 *
 * Module Name: bn.h
 *   $Revision: 12 $
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


#ifndef __BN_H__
#define __BN_H__

#include <actypes.h>
#include <acexcep.h>
#include <bm.h>


/*****************************************************************************
 *                            Types & Other Defines
 *****************************************************************************/

/*
 * Notifications:
 * ---------------------
 */
#define BN_NOTIFY_STATUS_CHANGE	((BM_NOTIFY) 0x80)


/*
 * Types:
 * ------
 */
#define BN_TYPE_POWER_BUTTON	0x01
#define BN_TYPE_POWER_BUTTON_FIXED 0x02
#define BN_TYPE_SLEEP_BUTTON	0x03
#define BN_TYPE_SLEEP_BUTTON_FIXED 0x04
#define BN_TYPE_LID_SWITCH	0x05


/*
 * Hardware IDs:
 * -------------
 * TBD: Power and Sleep button HIDs also exist in <bm.h>.  Should all
 *      HIDs (ACPI well-known devices) exist in one place (e.g.
 *      acpi_hid.h)?
 */
#define BN_HID_POWER_BUTTON	"PNP0C0C"
#define BN_HID_SLEEP_BUTTON	"PNP0C0E"
#define BN_HID_LID_SWITCH	"PNP0C0D"


/*
 * Device Context:
 * ---------------
 */
typedef struct
{
	BM_HANDLE		device_handle;
	acpi_handle		acpi_handle;
	u32			type;
} BN_CONTEXT;


/******************************************************************************
 *                              Function Prototypes
 *****************************************************************************/

acpi_status
bn_initialize (void);

acpi_status
bn_terminate (void);

acpi_status
bn_notify_fixed (
	void			*context);

acpi_status
bn_notify (
	u32			notify_type,
	u32			device,
	void			**context);

acpi_status
bn_request(
	BM_REQUEST		*request_info,
	void			*context);

/* Button OSL */

acpi_status
bn_osl_add_device (
	BN_CONTEXT		*button);

acpi_status
bn_osl_remove_device (
	BN_CONTEXT		*button);

acpi_status
bn_osl_generate_event (
	u32			event,
	BN_CONTEXT		*button);


#endif	/* __BN_H__ */
