/******************************************************************************
 *
 * Module Name: evgpe - General Purpose Event handling and dispatch
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
#include <acpi/acnamesp.h>

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evgpe")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_get_gpe_event_info
 *
 * PARAMETERS:  gpe_number      - Raw GPE number
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Returns the event_info struct
 *              associated with this GPE.
 *
 * TBD: this function will go away when full support of GPE block devices
 *      is implemented!
 *
 ******************************************************************************/

struct acpi_gpe_event_info *
acpi_ev_get_gpe_event_info (
	u32                             gpe_number)
{
	struct acpi_gpe_block_info      *gpe_block;


	/* Examine GPE Block 0 */

	gpe_block = acpi_gbl_gpe_block_list_head;
	if (!gpe_block) {
		return (NULL);
	}

	if ((gpe_number >= gpe_block->block_base_number) &&
		(gpe_number < gpe_block->block_base_number + (gpe_block->register_count * 8))) {
		return (&gpe_block->event_info[gpe_number - gpe_block->block_base_number]);
	}

	/* Examine GPE Block 1 */

	gpe_block = gpe_block->next;
	if (!gpe_block) {
		return (NULL);
	}

	if ((gpe_number >= gpe_block->block_base_number) &&
		(gpe_number < gpe_block->block_base_number + (gpe_block->register_count * 8))) {
		return (&gpe_block->event_info[gpe_number - gpe_block->block_base_number]);
	}

	return (NULL);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_detect
 *
 * PARAMETERS:  None
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Detect if any GP events have occurred.  This function is
 *              executed at interrupt level.
 *
 ******************************************************************************/

u32
acpi_ev_gpe_detect (void)
{
	u32                             int_status = ACPI_INTERRUPT_NOT_HANDLED;
	u32                             i;
	u32                             j;
	u8                              enabled_status_byte;
	u8                              bit_mask;
	struct acpi_gpe_register_info   *gpe_register_info;
	u32                             in_value;
	acpi_status                     status;
	struct acpi_gpe_block_info      *gpe_block;


	ACPI_FUNCTION_NAME ("ev_gpe_detect");


	/* Examine all GPE blocks attached to this interrupt level */

	gpe_block = acpi_gbl_gpe_block_list_head;
	while (gpe_block) {
		/*
		 * Read all of the 8-bit GPE status and enable registers
		 * in this GPE block, saving all of them.
		 * Find all currently active GP events.
		 */
		for (i = 0; i < gpe_block->register_count; i++) {
			/* Get the next status/enable pair */

			gpe_register_info = &gpe_block->register_info[i];

			status = acpi_hw_low_level_read (ACPI_GPE_REGISTER_WIDTH, &in_value,
					 &gpe_register_info->status_address, 0);
			gpe_register_info->status = (u8) in_value;
			if (ACPI_FAILURE (status)) {
				return (ACPI_INTERRUPT_NOT_HANDLED);
			}

			status = acpi_hw_low_level_read (ACPI_GPE_REGISTER_WIDTH, &in_value,
					 &gpe_register_info->enable_address, 0);
			gpe_register_info->enable = (u8) in_value;
			if (ACPI_FAILURE (status)) {
				return (ACPI_INTERRUPT_NOT_HANDLED);
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_INTERRUPTS,
				"GPE block at %8.8X%8.8X - Values: Enable %02X Status %02X\n",
				ACPI_HIDWORD (gpe_register_info->enable_address.address),
				ACPI_LODWORD (gpe_register_info->enable_address.address),
				gpe_register_info->enable,
				gpe_register_info->status));

			/* First check if there is anything active at all in this register */

			enabled_status_byte = (u8) (gpe_register_info->status &
					   gpe_register_info->enable);
			if (!enabled_status_byte) {
				/* No active GPEs in this register, move on */

				continue;
			}

			/* Now look at the individual GPEs in this byte register */

			for (j = 0, bit_mask = 1; j < ACPI_GPE_REGISTER_WIDTH; j++, bit_mask <<= 1) {
				/* Examine one GPE bit */

				if (enabled_status_byte & bit_mask) {
					/*
					 * Found an active GPE. Dispatch the event to a handler
					 * or method.
					 */
					int_status |= acpi_ev_gpe_dispatch (
							  &gpe_block->event_info[(i * ACPI_GPE_REGISTER_WIDTH) +j]);
				}
			}
		}

		gpe_block = gpe_block->next;
	}

	return (int_status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_asynch_execute_gpe_method
 *
 * PARAMETERS:  gpe_event_info - Info for this GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Perform the actual execution of a GPE control method.  This
 *              function is called from an invocation of acpi_os_queue_for_execution
 *              (and therefore does NOT execute at interrupt level) so that
 *              the control method itself is not executed in the context of
 *              the SCI interrupt handler.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
acpi_ev_asynch_execute_gpe_method (
	void                            *context)
{
	struct acpi_gpe_event_info      *gpe_event_info = (void *) context;
	u32                             gpe_number = 0;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ev_asynch_execute_gpe_method");


	/*
	 * Take a snapshot of the GPE info for this level - we copy the
	 * info to prevent a race condition with remove_handler.
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_VOID;
	}

	status = acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_VOID;
	}

	if (gpe_event_info->method_node) {
		/*
		 * Invoke the GPE Method (_Lxx, _Exx):
		 * (Evaluate the _Lxx/_Exx control method that corresponds to this GPE.)
		 */
		status = acpi_ns_evaluate_by_handle (gpe_event_info->method_node, NULL, NULL);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("%s while evaluating method [%4.4s] for GPE[%2.2X]\n",
				acpi_format_exception (status),
				gpe_event_info->method_node->name.ascii, gpe_number));
		}
	}

	if (gpe_event_info->type & ACPI_EVENT_LEVEL_TRIGGERED) {
		/*
		 * GPE is level-triggered, we clear the GPE status bit after handling
		 * the event.
		 */
		status = acpi_hw_clear_gpe (gpe_event_info);
		if (ACPI_FAILURE (status)) {
			return_VOID;
		}
	}

	/* Enable this GPE */

	(void) acpi_hw_enable_gpe (gpe_event_info);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ev_gpe_dispatch
 *
 * PARAMETERS:  gpe_event_info - info for this GPE
 *
 * RETURN:      INTERRUPT_HANDLED or INTERRUPT_NOT_HANDLED
 *
 * DESCRIPTION: Dispatch a General Purpose Event to either a function (e.g. EC)
 *              or method (e.g. _Lxx/_Exx) handler.  This function executes
 *              at interrupt level.
 *
 ******************************************************************************/

u32
acpi_ev_gpe_dispatch (
	struct acpi_gpe_event_info      *gpe_event_info)
{
	u32                             gpe_number = 0; /* TBD: remove */
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ev_gpe_dispatch");


	/*
	 * If edge-triggered, clear the GPE status bit now.  Note that
	 * level-triggered events are cleared after the GPE is serviced.
	 */
	if (gpe_event_info->type & ACPI_EVENT_EDGE_TRIGGERED) {
		status = acpi_hw_clear_gpe (gpe_event_info);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("acpi_ev_gpe_dispatch: Unable to clear GPE[%2.2X]\n",
				gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	/*
	 * Dispatch the GPE to either an installed handler, or the control
	 * method associated with this GPE (_Lxx or _Exx).
	 * If a handler exists, we invoke it and do not attempt to run the method.
	 * If there is neither a handler nor a method, we disable the level to
	 * prevent further events from coming in here.
	 */
	if (gpe_event_info->handler) {
		/* Invoke the installed handler (at interrupt level) */

		gpe_event_info->handler (gpe_event_info->context);
	}
	else if (gpe_event_info->method_node) {
		/*
		 * Disable GPE, so it doesn't keep firing before the method has a
		 * chance to run.
		 */
		status = acpi_hw_disable_gpe (gpe_event_info);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("acpi_ev_gpe_dispatch: Unable to disable GPE[%2.2X]\n",
				gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}

		/*
		 * Execute the method associated with the GPE.
		 */
		if (ACPI_FAILURE (acpi_os_queue_for_execution (OSD_PRIORITY_GPE,
				 acpi_ev_asynch_execute_gpe_method,
				 gpe_event_info))) {
			ACPI_REPORT_ERROR ((
				"acpi_ev_gpe_dispatch: Unable to queue handler for GPE[%2.2X], event is disabled\n",
				gpe_number));
		}
	}
	else {
		/* No handler or method to run! */

		ACPI_REPORT_ERROR ((
			"acpi_ev_gpe_dispatch: No handler or method for GPE[%2.2X], disabling event\n",
			gpe_number));

		/*
		 * Disable the GPE.  The GPE will remain disabled until the ACPI
		 * Core Subsystem is restarted, or the handler is reinstalled.
		 */
		status = acpi_hw_disable_gpe (gpe_event_info);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("acpi_ev_gpe_dispatch: Unable to disable GPE[%2.2X]\n",
				gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	/*
	 * It is now safe to clear level-triggered evnets.
	 */
	if (gpe_event_info->type & ACPI_EVENT_LEVEL_TRIGGERED) {
		status = acpi_hw_clear_gpe (gpe_event_info);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("acpi_ev_gpe_dispatch: Unable to clear GPE[%2.2X]\n",
				gpe_number));
			return_VALUE (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	return_VALUE (ACPI_INTERRUPT_HANDLED);
}

