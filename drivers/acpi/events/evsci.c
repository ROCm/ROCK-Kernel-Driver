/*******************************************************************************
 *
 * Module Name: evsci - System Control Interrupt configuration and
 *                      legacy to ACPI mode state transition functions
 *
 ******************************************************************************/

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
#include "acevents.h"


#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evsci")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_sci_handler
 *
 * PARAMETERS:  Context   - Calling Context
 *
 * RETURN:      Status code indicates whether interrupt was handled.
 *
 * DESCRIPTION: Interrupt handler that will figure out what function or
 *              control method to call to deal with a SCI.  Installed
 *              using BU interrupt support.
 *
 ******************************************************************************/

static u32 ACPI_SYSTEM_XFACE
acpi_ev_sci_handler (
	void                    *context)
{
	u32                     interrupt_handled = ACPI_INTERRUPT_NOT_HANDLED;
	u32                     value;
	acpi_status             status;


	ACPI_FUNCTION_TRACE("Ev_sci_handler");


	/*
	 * Make sure that ACPI is enabled by checking SCI_EN.  Note that we are
	 * required to treat the SCI interrupt as sharable, level, active low.
	 */
	status = acpi_get_register (ACPI_BITREG_SCI_ENABLE, &value, ACPI_MTX_DO_NOT_LOCK);
	if (ACPI_FAILURE (status)) {
		return (ACPI_INTERRUPT_NOT_HANDLED);
	}

	if (!value) {
		/* ACPI is not enabled;  this interrupt cannot be for us */

		return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
	}

	/*
	 * Fixed Acpi_events:
	 * -------------
	 * Check for and dispatch any Fixed Acpi_events that have occurred
	 */
	interrupt_handled |= acpi_ev_fixed_event_detect ();

	/*
	 * GPEs:
	 * -----
	 * Check for and dispatch any GPEs that have occurred
	 */
	interrupt_handled |= acpi_ev_gpe_detect ();

	return_VALUE (interrupt_handled);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ev_install_sci_handler
 *
 * PARAMETERS:  none
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Installs SCI handler.
 *
 ******************************************************************************/

u32
acpi_ev_install_sci_handler (void)
{
	u32                     status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ev_install_sci_handler");


	status = acpi_os_install_interrupt_handler ((u32) acpi_gbl_FADT->sci_int,
			   acpi_ev_sci_handler, NULL);
	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ev_remove_sci_handler
 *
 * PARAMETERS:  none
 *
 * RETURN:      E_OK if handler uninstalled OK, E_ERROR if handler was not
 *              installed to begin with
 *
 * DESCRIPTION: Remove the SCI interrupt handler.  No further SCIs will be
 *              taken.
 *
 * Note:  It doesn't seem important to disable all events or set the event
 *        enable registers to their original values.  The OS should disable
 *        the SCI interrupt level when the handler is removed, so no more
 *        events will come in.
 *
 ******************************************************************************/

acpi_status
acpi_ev_remove_sci_handler (void)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_remove_sci_handler");


	/* Just let the OS remove the handler and disable the level */

	status = acpi_os_remove_interrupt_handler ((u32) acpi_gbl_FADT->sci_int,
			   acpi_ev_sci_handler);

	return_ACPI_STATUS (status);
}


