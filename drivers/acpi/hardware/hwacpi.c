
/******************************************************************************
 *
 * Module Name: hwacpi - ACPI Hardware Initialization/Mode Interface
 *              $Revision: 60 $
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

	acpi_status             status;
	u32                     retry;


	ACPI_FUNCTION_TRACE ("Hw_set_mode");

	/*
	 * ACPI 2.0 clarified that if SMI_CMD in FADT is zero,
	 * system does not support mode transition.
	 */
	if (!acpi_gbl_FADT->smi_cmd) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No SMI_CMD in FADT, mode transition failed.\n"));
		return_ACPI_STATUS (AE_NO_HARDWARE_RESPONSE);
	}

	/*
	 * ACPI 2.0 clarified the meaning of ACPI_ENABLE and ACPI_DISABLE
	 * in FADT: If it is zero, enabling or disabling is not supported.
	 * As old systems may have used zero for mode transition,
	 * we make sure both the numbers are zero to determine these
	 * transitions are not supported.
	 */
	if (!acpi_gbl_FADT->acpi_enable && !acpi_gbl_FADT->acpi_disable) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "No mode transition supported in this system.\n"));
		return_ACPI_STATUS (AE_OK);
	}

	switch (mode) {
	case ACPI_SYS_MODE_ACPI:

		/* BIOS should have disabled ALL fixed and GP events */

		status = acpi_os_write_port (acpi_gbl_FADT->smi_cmd,
				  (acpi_integer) acpi_gbl_FADT->acpi_enable, 8);
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Attempting to enable ACPI mode\n"));
		break;

	case ACPI_SYS_MODE_LEGACY:

		/*
		 * BIOS should clear all fixed status bits and restore fixed event
		 * enable bits to default
		 */
		status = acpi_os_write_port (acpi_gbl_FADT->smi_cmd,
				 (acpi_integer) acpi_gbl_FADT->acpi_disable, 8);
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				 "Attempting to enable Legacy (non-ACPI) mode\n"));
		break;

	default:
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Some hardware takes a LONG time to switch modes. Give them 3 sec to
	 * do so, but allow faster systems to proceed more quickly.
	 */
	retry = 3000;
	while (retry) {
		status = AE_NO_HARDWARE_RESPONSE;

		if (acpi_hw_get_mode() == mode) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Mode %X successfully enabled\n", mode));
			status = AE_OK;
			break;
		}
		acpi_os_stall(1000);
		retry--;
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
	acpi_status             status;
	u32                     value;


	ACPI_FUNCTION_TRACE ("Hw_get_mode");

	status = acpi_get_register (ACPI_BITREG_SCI_ENABLE, &value, ACPI_MTX_LOCK);
	if (ACPI_FAILURE (status)) {
		return_VALUE (ACPI_SYS_MODE_LEGACY);
	}

	if (value) {
		return_VALUE (ACPI_SYS_MODE_ACPI);
	}
	else {
		return_VALUE (ACPI_SYS_MODE_LEGACY);
	}
}
