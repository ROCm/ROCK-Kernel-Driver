/******************************************************************************
 *
 * Module Name: evxfevnt - External Interfaces, ACPI event disable/enable
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */


#include <acpi/acpi.h>
#include <acpi/acevents.h>

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evxfevnt")


/*******************************************************************************
 *
 * FUNCTION:    acpi_enable
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfers the system into ACPI mode.
 *
 ******************************************************************************/

acpi_status
acpi_enable (void)
{
	acpi_status                     status = AE_OK;


	ACPI_FUNCTION_TRACE ("acpi_enable");


	/* Make sure we have the FADT*/

	if (!acpi_gbl_FADT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "No FADT information present!\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	if (acpi_hw_get_mode() == ACPI_SYS_MODE_ACPI) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "System is already in ACPI mode\n"));
	}
	else {
		/* Transition to ACPI mode */

		status = acpi_hw_set_mode (ACPI_SYS_MODE_ACPI);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Could not transition to ACPI mode.\n"));
			return_ACPI_STATUS (status);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "Transition to ACPI mode successful\n"));
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_disable
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Transfers the system into LEGACY mode.
 *
 ******************************************************************************/

acpi_status
acpi_disable (void)
{
	acpi_status                     status = AE_OK;


	ACPI_FUNCTION_TRACE ("acpi_disable");

	if (!acpi_gbl_FADT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "No FADT information present!\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	if (acpi_hw_get_mode() == ACPI_SYS_MODE_LEGACY) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "System is already in legacy (non-ACPI) mode\n"));
	}
	else {
		/* Transition to LEGACY mode */

		status = acpi_hw_set_mode (ACPI_SYS_MODE_LEGACY);

		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not exit ACPI mode to legacy mode"));
			return_ACPI_STATUS (status);
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "ACPI mode disabled\n"));
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_enable_event
 *
 * PARAMETERS:  Event           - The fixed event or GPE to be enabled
 *              Type            - The type of event
 *              Flags           - Just enable, or also wake enable?
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enable an ACPI event (fixed and general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_enable_event (
	u32                             event,
	u32                             type,
	u32                             flags)
{
	acpi_status                     status = AE_OK;
	u32                             value;
	struct acpi_gpe_event_info      *gpe_event_info;


	ACPI_FUNCTION_TRACE ("acpi_enable_event");


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_EVENT_MAX) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Enable the requested fixed event (by writing a one to the
		 * enable register bit)
		 */
		status = acpi_set_register (acpi_gbl_fixed_event_info[event].enable_register_id,
				 1, ACPI_MTX_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Make sure that the hardware responded */

		status = acpi_get_register (acpi_gbl_fixed_event_info[event].enable_register_id,
				  &value, ACPI_MTX_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		if (value != 1) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not enable %s event\n", acpi_ut_get_event_name (event)));
			return_ACPI_STATUS (AE_NO_HARDWARE_RESPONSE);
		}
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		gpe_event_info = acpi_ev_get_gpe_event_info (event);
		if (!gpe_event_info) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/* Enable the requested GPE number */

		status = acpi_hw_enable_gpe (gpe_event_info);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		if (flags & ACPI_EVENT_WAKE_ENABLE) {
			acpi_hw_enable_gpe_for_wakeup (gpe_event_info);
		}
		break;


	default:

		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_disable_event
 *
 * PARAMETERS:  Event           - The fixed event or GPE to be enabled
 *              Type            - The type of event, fixed or general purpose
 *              Flags           - Wake disable vs. non-wake disable
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disable an ACPI event (fixed and general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_disable_event (
	u32                             event,
	u32                             type,
	u32                             flags)
{
	acpi_status                     status = AE_OK;
	u32                             value;
	struct acpi_gpe_event_info      *gpe_event_info;


	ACPI_FUNCTION_TRACE ("acpi_disable_event");


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_EVENT_MAX) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Disable the requested fixed event (by writing a zero to the
		 * enable register bit)
		 */
		status = acpi_set_register (acpi_gbl_fixed_event_info[event].enable_register_id,
				 0, ACPI_MTX_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		status = acpi_get_register (acpi_gbl_fixed_event_info[event].enable_register_id,
				 &value, ACPI_MTX_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		if (value != 0) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not disable %s events\n", acpi_ut_get_event_name (event)));
			return_ACPI_STATUS (AE_NO_HARDWARE_RESPONSE);
		}
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		gpe_event_info = acpi_ev_get_gpe_event_info (event);
		if (!gpe_event_info) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Only disable the requested GPE number for wake if specified.
		 * Otherwise, turn it totally off
		 */

		if (flags & ACPI_EVENT_WAKE_DISABLE) {
			acpi_hw_disable_gpe_for_wakeup (gpe_event_info);
		}
		else {
			status = acpi_hw_disable_gpe (gpe_event_info);
		}
		break;


	default:
		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_clear_event
 *
 * PARAMETERS:  Event           - The fixed event or GPE to be cleared
 *              Type            - The type of event
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Clear an ACPI event (fixed and general purpose)
 *
 ******************************************************************************/

acpi_status
acpi_clear_event (
	u32                             event,
	u32                             type)
{
	acpi_status                     status = AE_OK;
	struct acpi_gpe_event_info      *gpe_event_info;


	ACPI_FUNCTION_TRACE ("acpi_clear_event");


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_EVENT_MAX) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/*
		 * Clear the requested fixed event (By writing a one to the
		 * status register bit)
		 */
		status = acpi_set_register (acpi_gbl_fixed_event_info[event].status_register_id,
				1, ACPI_MTX_LOCK);
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		gpe_event_info = acpi_ev_get_gpe_event_info (event);
		if (!gpe_event_info) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		status = acpi_hw_clear_gpe (gpe_event_info);
		break;


	default:

		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_get_event_status
 *
 * PARAMETERS:  Event           - The fixed event or GPE
 *              Type            - The type of event
 *              Status          - Where the current status of the event will
 *                                be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtains and returns the current status of the event
 *
 ******************************************************************************/


acpi_status
acpi_get_event_status (
	u32                             event,
	u32                             type,
	acpi_event_status               *event_status)
{
	acpi_status                     status = AE_OK;
	struct acpi_gpe_event_info      *gpe_event_info;


	ACPI_FUNCTION_TRACE ("acpi_get_event_status");


	if (!event_status) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* The Type must be either Fixed Event or GPE */

	switch (type) {
	case ACPI_EVENT_FIXED:

		/* Decode the Fixed Event */

		if (event > ACPI_EVENT_MAX) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/* Get the status of the requested fixed event */

		status = acpi_get_register (acpi_gbl_fixed_event_info[event].status_register_id,
				  event_status, ACPI_MTX_LOCK);
		break;


	case ACPI_EVENT_GPE:

		/* Ensure that we have a valid GPE number */

		gpe_event_info = acpi_ev_get_gpe_event_info (event);
		if (!gpe_event_info) {
			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}

		/* Obtain status on the requested GPE number */

		status = acpi_hw_get_gpe_status (event, event_status);
		break;


	default:
		status = AE_BAD_PARAMETER;
	}

	return_ACPI_STATUS (status);
}


