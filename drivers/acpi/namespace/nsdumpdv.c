/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
 *              $Revision: 4 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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


#include "acpi.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsdumpdv")


#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_one_device
 *
 * PARAMETERS:  Handle              - Node to be dumped
 *              Level               - Nesting level of the handle
 *              Context             - Passed into Walk_namespace
 *
 * DESCRIPTION: Dump a single Node that represents a device
 *              This procedure is a User_function called by Acpi_ns_walk_namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ns_dump_one_device (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	acpi_device_info        info;
	acpi_status             status;
	u32                     i;


	ACPI_FUNCTION_NAME ("Ns_dump_one_device");


	status = acpi_ns_dump_one_object (obj_handle, level, context, return_value);

	status = acpi_get_object_info (obj_handle, &info);
	if (ACPI_SUCCESS (status)) {
		for (i = 0; i < level; i++) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " "));
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "    HID: %s, ADR: %8.8X%8.8X, Status: %X\n",
				  info.hardware_id,
				  ACPI_HIDWORD (info.address), ACPI_LODWORD (info.address),
				  info.current_status));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_root_devices
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Dump all objects of type "device"
 *
 ******************************************************************************/

void
acpi_ns_dump_root_devices (void)
{
	acpi_handle             sys_bus_handle;
	acpi_status             status;


	ACPI_FUNCTION_NAME ("Ns_dump_root_devices");


	/* Only dump the table if tracing is enabled */

	if (!(ACPI_LV_TABLES & acpi_dbg_level)) {
		return;
	}

	status = acpi_get_handle (0, ACPI_NS_SYSTEM_BUS, &sys_bus_handle);
	if (ACPI_FAILURE (status)) {
		return;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "Display of all devices in the namespace:\n"));

	status = acpi_ns_walk_namespace (ACPI_TYPE_DEVICE, sys_bus_handle,
			 ACPI_UINT32_MAX, ACPI_NS_WALK_NO_UNLOCK,
			 acpi_ns_dump_one_device, NULL, NULL);
}

#endif


