/******************************************************************************
 *
 * Module Name: evevent - Fixed Event handling and dispatch
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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
#include "acevents.h"

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evevent")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Initialize global data structures for events.
 *
 ******************************************************************************/

acpi_status
acpi_ev_initialize (
	void)
{
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ev_initialize");


	/* Make sure we have ACPI tables */

	if (!acpi_gbl_DSDT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "No ACPI tables present!\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	/*
	 * Initialize the Fixed and General Purpose acpi_events prior. This is
	 * done prior to enabling SCIs to prevent interrupts from occuring
	 * before handers are installed.
	 */
	status = acpi_ev_fixed_event_initialize ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR ((
				"Unable to initialize fixed events, %s\n",
				acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	status = acpi_ev_gpe_initialize ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR ((
				"Unable to initialize general purpose events, %s\n",
				acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_handler_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install handlers for the SCI, Global Lock, and GPEs.
 *
 ******************************************************************************/

acpi_status
acpi_ev_handler_initialize (
	void)
{
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ev_handler_initialize");


	/* Install the SCI handler */

	status = acpi_ev_install_sci_handler ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR ((
				"Unable to install System Control Interrupt Handler, %s\n",
				acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/* Install handlers for control method GPE handlers (_Lxx, _Exx) */

	status = acpi_ev_init_gpe_control_methods ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR ((
				"Unable to initialize GPE control methods, %s\n",
				acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/* Install the handler for the Global Lock */

	status = acpi_ev_init_global_lock_handler ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR ((
				"Unable to initialize Global Lock handler, %s\n",
				acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	acpi_gbl_events_initialized = TRUE;
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_fixed_event_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install the fixed event handlers and enable the fixed events.
 *
 ******************************************************************************/

acpi_status
acpi_ev_fixed_event_initialize (
	void)
{
	acpi_native_uint                i;
	acpi_status                     status;


	/*
	 * Initialize the structure that keeps track of fixed event handlers
	 * and enable the fixed events.
	 */
	for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
		acpi_gbl_fixed_event_handlers[i].handler = NULL;
		acpi_gbl_fixed_event_handlers[i].context = NULL;

		/* Enable the fixed event */

		if (acpi_gbl_fixed_event_info[i].enable_register_id != 0xFF) {
			status = acpi_set_register (acpi_gbl_fixed_event_info[i].enable_register_id,
					 0, ACPI_MTX_LOCK);
			if (ACPI_FAILURE (status)) {
				return (status);
			}
		}
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_fixed_event_detect
 *
 * PARAMETERS:  None
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Checks the PM status register for fixed events
 *
 ******************************************************************************/

u32
acpi_ev_fixed_event_detect (
	void)
{
	u32                             int_status = ACPI_INTERRUPT_NOT_HANDLED;
	u32                             fixed_status;
	u32                             fixed_enable;
	acpi_native_uint                i;


	ACPI_FUNCTION_NAME ("ev_fixed_event_detect");


	/*
	 * Read the fixed feature status and enable registers, as all the cases
	 * depend on their values.  Ignore errors here.
	 */
	(void) acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_STATUS, &fixed_status);
	(void) acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_ENABLE, &fixed_enable);

	ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
		"Fixed acpi_event Block: Enable %08X Status %08X\n",
		fixed_enable, fixed_status));

	/*
	 * Check for all possible Fixed Events and dispatch those that are active
	 */
	for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
		/* Both the status and enable bits must be on for this event */

		if ((fixed_status & acpi_gbl_fixed_event_info[i].status_bit_mask) &&
			(fixed_enable & acpi_gbl_fixed_event_info[i].enable_bit_mask)) {
			/* Found an active (signalled) event */

			int_status |= acpi_ev_fixed_event_dispatch ((u32) i);
		}
	}

	return (int_status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_fixed_event_dispatch
 *
 * PARAMETERS:  Event               - Event type
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Clears the status bit for the requested event, calls the
 *              handler that previously registered for the event.
 *
 ******************************************************************************/

u32
acpi_ev_fixed_event_dispatch (
	u32                             event)
{


	ACPI_FUNCTION_ENTRY ();


	/* Clear the status bit */

	(void) acpi_set_register (acpi_gbl_fixed_event_info[event].status_register_id,
			 1, ACPI_MTX_DO_NOT_LOCK);

	/*
	 * Make sure we've got a handler.  If not, report an error.
	 * The event is disabled to prevent further interrupts.
	 */
	if (NULL == acpi_gbl_fixed_event_handlers[event].handler) {
		(void) acpi_set_register (acpi_gbl_fixed_event_info[event].enable_register_id,
				0, ACPI_MTX_DO_NOT_LOCK);

		ACPI_REPORT_ERROR (
			("ev_gpe_dispatch: No installed handler for fixed event [%08X]\n",
			event));

		return (ACPI_INTERRUPT_NOT_HANDLED);
	}

	/* Invoke the Fixed Event handler */

	return ((acpi_gbl_fixed_event_handlers[event].handler)(
			  acpi_gbl_fixed_event_handlers[event].context));
}


