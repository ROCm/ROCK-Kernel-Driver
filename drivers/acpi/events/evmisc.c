/******************************************************************************
 *
 * Module Name: evmisc - Miscellaneous event manager support functions
 *              $Revision: 56 $
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
#include "acevents.h"
#include "acnamesp.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evmisc")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_is_notify_object
 *
 * PARAMETERS:  Node            - Node to check
 *
 * RETURN:      TRUE if notifies allowed on this object
 *
 * DESCRIPTION: Check type of node for a object that supports notifies.
 *
 *              TBD: This could be replaced by a flag bit in the node.
 *
 ******************************************************************************/

u8
acpi_ev_is_notify_object (
	acpi_namespace_node     *node)
{
	switch (node->type) {
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_POWER:
	case ACPI_TYPE_THERMAL:
		/*
		 * These are the ONLY objects that can receive ACPI notifications
		 */
		return (TRUE);

	default:
		return (FALSE);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_get_gpe_register_index
 *
 * PARAMETERS:  Gpe_number      - Raw GPE number
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Returns the register index (index into the GPE register info
 *              table) associated with this GPE.
 *
 ******************************************************************************/

u32
acpi_ev_get_gpe_register_index (
	u32                     gpe_number)
{

	if (gpe_number > acpi_gbl_gpe_number_max) {
		return (ACPI_GPE_INVALID);
	}

	return (ACPI_DIV_8 (acpi_gbl_gpe_number_to_index[gpe_number].number_index));
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_get_gpe_number_index
 *
 * PARAMETERS:  Gpe_number      - Raw GPE number
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Returns the number index (index into the GPE number info table)
 *              associated with this GPE.
 *
 ******************************************************************************/

u32
acpi_ev_get_gpe_number_index (
	u32                     gpe_number)
{

	if (gpe_number > acpi_gbl_gpe_number_max) {
		return (ACPI_GPE_INVALID);
	}

	return (acpi_gbl_gpe_number_to_index[gpe_number].number_index);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_queue_notify_request
 *
 * PARAMETERS:
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Dispatch a device notification event to a previously
 *              installed handler.
 *
 ******************************************************************************/

acpi_status
acpi_ev_queue_notify_request (
	acpi_namespace_node     *node,
	u32                     notify_value)
{
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *handler_obj = NULL;
	acpi_generic_state      *notify_info;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_NAME ("Ev_queue_notify_request");


	/*
	 * For value 1 (Ejection Request), some device method may need to be run.
	 * For value 2 (Device Wake) if _PRW exists, the _PS0 method may need to be run.
	 * For value 0x80 (Status Change) on the power button or sleep button,
	 * initiate soft-off or sleep operation?
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Dispatching Notify(%X) on node %p\n", notify_value, node));

	switch (notify_value) {
	case 0:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Notify value: Re-enumerate Devices\n"));
		break;

	case 1:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Notify value: Ejection Request\n"));
		break;

	case 2:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Notify value: Device Wake\n"));
		break;

	case 0x80:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Notify value: Status Change\n"));
		break;

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Unknown Notify Value: %X \n", notify_value));
		break;
	}

	/*
	 * Get the notify object attached to the NS Node
	 */
	obj_desc = acpi_ns_get_attached_object (node);
	if (obj_desc) {
		/* We have the notify object, Get the right handler */

		switch (node->type) {
		case ACPI_TYPE_DEVICE:
		case ACPI_TYPE_THERMAL:
		case ACPI_TYPE_PROCESSOR:
		case ACPI_TYPE_POWER:

			if (notify_value <= ACPI_MAX_SYS_NOTIFY) {
				handler_obj = obj_desc->common_notify.sys_handler;
			}
			else {
				handler_obj = obj_desc->common_notify.drv_handler;
			}
			break;

		default:
			/* All other types are not supported */
			return (AE_TYPE);
		}
	}

	/* If there is any handler to run, schedule the dispatcher */

	if ((acpi_gbl_sys_notify.handler && (notify_value <= ACPI_MAX_SYS_NOTIFY)) ||
		(acpi_gbl_drv_notify.handler && (notify_value > ACPI_MAX_SYS_NOTIFY)) ||
		handler_obj) {
		notify_info = acpi_ut_create_generic_state ();
		if (!notify_info) {
			return (AE_NO_MEMORY);
		}

		notify_info->common.data_type = ACPI_DESC_TYPE_STATE_NOTIFY;
		notify_info->notify.node      = node;
		notify_info->notify.value     = (u16) notify_value;
		notify_info->notify.handler_obj = handler_obj;

		status = acpi_os_queue_for_execution (OSD_PRIORITY_HIGH,
				  acpi_ev_notify_dispatch, notify_info);
		if (ACPI_FAILURE (status)) {
			acpi_ut_delete_generic_state (notify_info);
		}
	}

	if (!handler_obj) {
		/* There is no per-device notify handler for this device */

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "No notify handler for node %p \n", node));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_notify_dispatch
 *
 * PARAMETERS:
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Dispatch a device notification event to a previously
 *              installed handler.
 *
 ******************************************************************************/

void ACPI_SYSTEM_XFACE
acpi_ev_notify_dispatch (
	void                    *context)
{
	acpi_generic_state      *notify_info = (acpi_generic_state *) context;
	acpi_notify_handler     global_handler = NULL;
	void                    *global_context = NULL;
	acpi_operand_object     *handler_obj;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * We will invoke a global notify handler if installed.
	 * This is done _before_ we invoke the per-device handler attached to the device.
	 */
	if (notify_info->notify.value <= ACPI_MAX_SYS_NOTIFY) {
		/* Global system notification handler */

		if (acpi_gbl_sys_notify.handler) {
			global_handler = acpi_gbl_sys_notify.handler;
			global_context = acpi_gbl_sys_notify.context;
		}
	}
	else {
		/* Global driver notification handler */

		if (acpi_gbl_drv_notify.handler) {
			global_handler = acpi_gbl_drv_notify.handler;
			global_context = acpi_gbl_drv_notify.context;
		}
	}

	/* Invoke the system handler first, if present */

	if (global_handler) {
		global_handler (notify_info->notify.node, notify_info->notify.value, global_context);
	}

	/* Now invoke the per-device handler, if present */

	handler_obj = notify_info->notify.handler_obj;
	if (handler_obj) {
		handler_obj->notify_handler.handler (notify_info->notify.node, notify_info->notify.value,
				  handler_obj->notify_handler.context);
	}

	/* All done with the info object */

	acpi_ut_delete_generic_state (notify_info);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_global_lock_thread
 *
 * RETURN:      None
 *
 * DESCRIPTION: Invoked by SCI interrupt handler upon acquisition of the
 *              Global Lock.  Simply signal all threads that are waiting
 *              for the lock.
 *
 ******************************************************************************/

static void ACPI_SYSTEM_XFACE
acpi_ev_global_lock_thread (
	void                    *context)
{
	acpi_status             status;


	/* Signal threads that are waiting for the lock */

	if (acpi_gbl_global_lock_thread_count) {
		/* Send sufficient units to the semaphore */

		status = acpi_os_signal_semaphore (acpi_gbl_global_lock_semaphore,
				 acpi_gbl_global_lock_thread_count);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Could not signal Global Lock semaphore\n"));
		}
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_global_lock_handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Invoked directly from the SCI handler when a global lock
 *              release interrupt occurs.  Grab the global lock and queue
 *              the global lock thread for execution
 *
 ******************************************************************************/

static u32
acpi_ev_global_lock_handler (
	void                    *context)
{
	u8                      acquired = FALSE;
	acpi_status             status;


	/*
	 * Attempt to get the lock
	 * If we don't get it now, it will be marked pending and we will
	 * take another interrupt when it becomes free.
	 */
	ACPI_ACQUIRE_GLOBAL_LOCK (acpi_gbl_common_fACS.global_lock, acquired);
	if (acquired) {
		/* Got the lock, now wake all threads waiting for it */

		acpi_gbl_global_lock_acquired = TRUE;

		/* Run the Global Lock thread which will signal all waiting threads */

		status = acpi_os_queue_for_execution (OSD_PRIORITY_HIGH,
				  acpi_ev_global_lock_thread, context);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Could not queue Global Lock thread, %s\n",
				acpi_format_exception (status)));

			return (ACPI_INTERRUPT_NOT_HANDLED);
		}
	}

	return (ACPI_INTERRUPT_HANDLED);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_init_global_lock_handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for the global lock release event
 *
 ******************************************************************************/

acpi_status
acpi_ev_init_global_lock_handler (void)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_init_global_lock_handler");


	acpi_gbl_global_lock_present = TRUE;
	status = acpi_install_fixed_event_handler (ACPI_EVENT_GLOBAL,
			  acpi_ev_global_lock_handler, NULL);

	/*
	 * If the global lock does not exist on this platform, the attempt
	 * to enable GBL_STATUS will fail (the GBL_ENABLE bit will not stick)
	 * Map to AE_OK, but mark global lock as not present.
	 * Any attempt to actually use the global lock will be flagged
	 * with an error.
	 */
	if (status == AE_NO_HARDWARE_RESPONSE) {
		acpi_gbl_global_lock_present = FALSE;
		status = AE_OK;
	}

	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ev_acquire_global_lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Attempt to gain ownership of the Global Lock.
 *
 *****************************************************************************/

acpi_status
acpi_ev_acquire_global_lock (
	u32                     timeout)
{
	acpi_status             status = AE_OK;
	u8                      acquired = FALSE;


	ACPI_FUNCTION_TRACE ("Ev_acquire_global_lock");


#ifndef ACPI_APPLICATION
	/* Make sure that we actually have a global lock */

	if (!acpi_gbl_global_lock_present) {
		return_ACPI_STATUS (AE_NO_GLOBAL_LOCK);
	}
#endif

	/* One more thread wants the global lock */

	acpi_gbl_global_lock_thread_count++;

	/* If we (OS side vs. BIOS side) have the hardware lock already, we are done */

	if (acpi_gbl_global_lock_acquired) {
		return_ACPI_STATUS (AE_OK);
	}

	/* We must acquire the actual hardware lock */

	ACPI_ACQUIRE_GLOBAL_LOCK (acpi_gbl_common_fACS.global_lock, acquired);
	if (acquired) {
	   /* We got the lock */

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Acquired the HW Global Lock\n"));

		acpi_gbl_global_lock_acquired = TRUE;
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Did not get the lock.  The pending bit was set above, and we must now
	 * wait until we get the global lock released interrupt.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Waiting for the HW Global Lock\n"));

	/*
	 * Acquire the global lock semaphore first.
	 * Since this wait will block, we must release the interpreter
	 */
	status = acpi_ex_system_wait_semaphore (acpi_gbl_global_lock_semaphore,
			  timeout);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ev_release_global_lock
 *
 * DESCRIPTION: Releases ownership of the Global Lock.
 *
 ******************************************************************************/

acpi_status
acpi_ev_release_global_lock (void)
{
	u8                      pending = FALSE;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ev_release_global_lock");


	if (!acpi_gbl_global_lock_thread_count) {
		ACPI_REPORT_WARNING(("Cannot release HW Global Lock, it has not been acquired\n"));
		return_ACPI_STATUS (AE_NOT_ACQUIRED);
	}

	/* One fewer thread has the global lock */

	acpi_gbl_global_lock_thread_count--;
	if (acpi_gbl_global_lock_thread_count) {
		/* There are still some threads holding the lock, cannot release */

		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * No more threads holding lock, we can do the actual hardware
	 * release
	 */
	ACPI_RELEASE_GLOBAL_LOCK (acpi_gbl_common_fACS.global_lock, pending);
	acpi_gbl_global_lock_acquired = FALSE;

	/*
	 * If the pending bit was set, we must write GBL_RLS to the control
	 * register
	 */
	if (pending) {
		status = acpi_set_register (ACPI_BITREG_GLOBAL_LOCK_RELEASE, 1, ACPI_MTX_LOCK);
	}

	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_ev_terminate
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: free memory allocated for table storage.
 *
 ******************************************************************************/

void
acpi_ev_terminate (void)
{
	NATIVE_UINT_MAX32       i;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ev_terminate");

	/*
	 * Disable all event-related functionality.
	 * In all cases, on error, print a message but obviously we don't abort.
	 */

	/*
	 * Disable all fixed events
	 */
	for (i = 0; i < ACPI_NUM_FIXED_EVENTS; i++) {
		status = acpi_disable_event(i, ACPI_EVENT_FIXED, 0);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Failed to disable fixed event %d.\n", i));
		}
	}

	/*
	 * Disable all GPEs
	 */
	for (i = 0; i < acpi_gbl_gpe_number_max; i++) {
		if (acpi_ev_get_gpe_number_index(i) != ACPI_GPE_INVALID) {
			status = acpi_hw_disable_gpe(i);
			if (ACPI_FAILURE (status)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Failed to disable GPE %d.\n", i));
			}
		}
	}

	/*
	 * Remove SCI handler
	 */
	status = acpi_ev_remove_sci_handler();
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unable to remove SCI handler.\n"));
	}

	/*
	 * Return to original mode if necessary
	 */
	if (acpi_gbl_original_mode == ACPI_SYS_MODE_LEGACY) {
		status = acpi_disable ();
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Acpi_disable failed.\n"));
		}
	}

	/*
	 * Free global tables, etc.
	 */
	if (acpi_gbl_gpe_register_info) {
		ACPI_MEM_FREE (acpi_gbl_gpe_register_info);
		acpi_gbl_gpe_register_info = NULL;
	}

	if (acpi_gbl_gpe_number_info) {
		ACPI_MEM_FREE (acpi_gbl_gpe_number_info);
		acpi_gbl_gpe_number_info = NULL;
	}

	if (acpi_gbl_gpe_number_to_index) {
		ACPI_MEM_FREE (acpi_gbl_gpe_number_to_index);
		acpi_gbl_gpe_number_to_index = NULL;
	}

	return_VOID;
}

