/*****************************************************************************
 *
 * Module Name: sm.h
 *   $Revision: 3 $
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


#ifndef __SM_H__
#define __SM_H__

#include <actypes.h>
#include <acexcep.h>
#include <bm.h>


/*****************************************************************************
 *                            Types & Other Defines
 *****************************************************************************/

#define SM_MAX_SYSTEM_STATES	6	/* S0-S5 */


 /*
 * Device Context:
 * ---------------
 */
typedef struct
{
	BM_HANDLE		device_handle;
	ACPI_HANDLE 		acpi_handle;
	u8			states[SM_MAX_SYSTEM_STATES];
} SM_CONTEXT;


/*****************************************************************************
 *                              Function Prototypes
 *****************************************************************************/

ACPI_STATUS
sm_initialize (void);

ACPI_STATUS
sm_terminate (void);

ACPI_STATUS
sm_notify (
	u32			notify_type,
	u32 			device,
	void			**context);

ACPI_STATUS
sm_request(
	BM_REQUEST		*request_info,
	void			*context);

/* System Driver OSL */

ACPI_STATUS
sm_osl_add_device (
	SM_CONTEXT		*system);

ACPI_STATUS
sm_osl_remove_device (
	SM_CONTEXT		*system);

ACPI_STATUS
sm_osl_generate_event (
	u32			event,
	SM_CONTEXT		*system);


#endif	/* __SM_H__ */
