
/******************************************************************************
 *
 * Module Name: hwacpi - ACPI Hardware Initialization/Mode Interface
 *              $Revision: 53 $
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
#include "achware.h"


#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwacpi")


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize and validate various ACPI registers
 *
 ******************************************************************************/

acpi_status
acpi_hw_initialize (
	void)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Hw_initialize");


	/* We must have the ACPI tables by the time we get here */

	if (!acpi_gbl_FADT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "A FADT is not loaded\n"));

		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	/* Sanity check the FADT for valid values */

	status = acpi_ut_validate_fadt ();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_set_mode
 *
 * PARAMETERS:  Mode            - SYS_MODE_ACPI or SYS_MODE_LEGACY
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transitions the system into the requested mode.
 *
 ******************************************************************************/

acpi_status
acpi_hw_set_mode (
	u32                     mode)
{

	acpi_status             status = AE_NO_HARDWARE_RESPONSE;


	ACPI_FUNCTION_TRACE ("Hw_set_mode");


	if (mode == ACPI_SYS_MODE_ACPI) {
		/* BIOS should have disabled ALL fixed and GP events */

		acpi_os_write_port (acpi_gbl_FADT->smi_cmd, acpi_gbl_FADT->acpi_enable, 8);
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Attempting to enable ACPI mode\n"));
	}
	else if (mode == ACPI_SYS_MODE_LEGACY) {
		/*
		 * BIOS should clear all fixed status bits and restore fixed event
		 * enable bits to default
		 */
		acpi_os_write_port (acpi_gbl_FADT->smi_cmd, acpi_gbl_FADT->acpi_disable, 8);
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				 "Attempting to enable Legacy (non-ACPI) mode\n"));
	}

	/* Give the platform some time to react */

	acpi_os_stall (20000);

	if (acpi_hw_get_mode () == mode) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Mode %X successfully enabled\n", mode));
		status = AE_OK;
	}

	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_mode
 *
 * PARAMETERS:  none
 *
 * RETURN:      SYS_MODE_ACPI or SYS_MODE_LEGACY
 *
 * DESCRIPTION: Return current operating state of system.  Determined by
 *              querying the SCI_EN bit.
 *
 ******************************************************************************/

u32
acpi_hw_get_mode (void)
{

	ACPI_FUNCTION_TRACE ("Hw_get_mode");


	if (acpi_hw_bit_register_read (ACPI_BITREG_SCI_ENABLE, ACPI_MTX_LOCK)) {
		return_VALUE (ACPI_SYS_MODE_ACPI);
	}
	else {
		return_VALUE (ACPI_SYS_MODE_LEGACY);
	}
}
