/*****************************************************************************
 *
 * Module Name: ac.h
 *   $Revision: 6 $
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


#ifndef __AC_H__
#define __AC_H__

#include <actypes.h>
#include <acexcep.h>
#include <bm.h>


/*****************************************************************************
 *                            Types & Other Defines
 *****************************************************************************/

/*
 * Notifications:
 * --------------
 */
#define AC_NOTIFY_STATUS_CHANGE	((BM_NOTIFY) 0x80)

/*
 * Hardware IDs:
 * -------------
 */
#define AC_HID_AC_ADAPTER	"ACPI0003"


/*
 * Device Context:
 * ---------------
 */
typedef struct
{
	BM_HANDLE		device_handle;
	acpi_handle		acpi_handle;
	char			uid[9];
	u32 			is_online;
} AC_CONTEXT;


/*****************************************************************************
 *                              Function Prototypes
 *****************************************************************************/

acpi_status
ac_initialize (void);

acpi_status
ac_terminate (void);

acpi_status
ac_notify (
	u32			notify_type,
	u32 			device,
	void			**context);

acpi_status
ac_request(
	BM_REQUEST		*request_info,
	void			*context);

/* AC Adapter Driver OSL */

acpi_status
ac_osl_add_device (
	AC_CONTEXT		*ac_adapter);

acpi_status
ac_osl_remove_device (
	AC_CONTEXT		*ac_adapter);

acpi_status
ac_osl_generate_event (
	u32			event,
	AC_CONTEXT		*ac_adapter);


#endif	/* __AC_H__ */
