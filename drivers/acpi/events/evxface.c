/******************************************************************************
 *
 * Module Name: evxface - External interfaces for ACPI events
 *              $Revision: 130 $
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
#include "acevents.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_EVENTS
	 ACPI_MODULE_NAME    ("evxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_install_fixed_event_handler
 *
 * PARAMETERS:  Event           - Event type to enable.
 *              Handler         - Pointer to the handler function for the
 *                                event
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Saves the pointer to the handler function and then enables the
 *              event.
 *
 ******************************************************************************/

acpi_status
acpi_install_fixed_event_handler (
	u32                     event,
	acpi_event_handler      handler,
	void                    *context)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_install_fixed_event_handler");


	/* Parameter validation */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Don't allow two handlers. */

	if (NULL != acpi_gbl_fixed_event_handlers[event].handler) {
		status = AE_ALREADY_EXISTS;
		goto cleanup;
	}

	/* Install the handler before enabling the event */

	acpi_gbl_fixed_event_handlers[event].handler = handler;
	acpi_gbl_fixed_event_handlers[event].context = context;

	status = acpi_enable_event (event, ACPI_EVENT_FIXED, 0);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Could not enable fixed event.\n"));

		/* Remove the handler */

		acpi_gbl_fixed_event_handlers[event].handler = NULL;
		acpi_gbl_fixed_event_handlers[event].context = NULL;
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Enabled fixed event %X, Handler=%p\n", event, handler));
	}


cleanup:
	(void) acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_remove_fixed_event_handler
 *
 * PARAMETERS:  Event           - Event type to disable.
 *              Handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Disables the event and unregisters the event handler.
 *
 ******************************************************************************/

acpi_status
acpi_remove_fixed_event_handler (
	u32                     event,
	acpi_event_handler      handler)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Acpi_remove_fixed_event_handler");


	/* Parameter validation */

	if (event > ACPI_EVENT_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Disable the event before removing the handler */

	status = acpi_disable_event(event, ACPI_EVENT_FIXED, 0);

	/* Always Remove the handler */

	acpi_gbl_fixed_event_handlers[event].handler = NULL;
	acpi_gbl_fixed_event_handlers[event].context = NULL;

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN,
			"Could not write to fixed event enable register.\n"));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Disabled fixed event %X.\n", event));
	}

	(void) acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_install_notify_handler
 *
 * PARAMETERS:  Device          - The device for which notifies will be handled
 *              Handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: System_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: Driver_handler (80-ff)
 *              Handler         - Address of the handler
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for notifies on an ACPI device
 *
 ******************************************************************************/

acpi_status
acpi_install_notify_handler (
	acpi_handle             device,
	u32                     handler_type,
	acpi_notify_handler     handler,
	void                    *context)
{
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *notify_obj;
	acpi_namespace_node     *node;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_install_notify_handler");


	/* Parameter validation */

	if ((!device)  ||
		(!handler) ||
		(handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Convert and validate the device handle */

	node = acpi_ns_map_handle_to_node (device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * Root Object:
	 * Registering a notify handler on the root object indicates that the
	 * caller wishes to receive notifications for all objects.  Note that
	 * only one <external> global handler can be regsitered (per notify type).
	 */
	if (device == ACPI_ROOT_OBJECT) {
		/* Make sure the handler is not already installed */

		if (((handler_type == ACPI_SYSTEM_NOTIFY) &&
			  acpi_gbl_sys_notify.handler) ||
			((handler_type == ACPI_DEVICE_NOTIFY) &&
			  acpi_gbl_drv_notify.handler)) {
			status = AE_ALREADY_EXISTS;
			goto unlock_and_exit;
		}

		if (handler_type == ACPI_SYSTEM_NOTIFY) {
			acpi_gbl_sys_notify.node = node;
			acpi_gbl_sys_notify.handler = handler;
			acpi_gbl_sys_notify.context = context;
		}
		else /* ACPI_DEVICE_NOTIFY */ {
			acpi_gbl_drv_notify.node = node;
			acpi_gbl_drv_notify.handler = handler;
			acpi_gbl_drv_notify.context = context;
		}

		/* Global notify handler installed */
	}

	/*
	 * All Other Objects:
	 * Caller will only receive notifications specific to the target object.
	 * Note that only certain object types can receive notifications.
	 */
	else {
		/* Notifies allowed on this object? */

		if (!acpi_ev_is_notify_object (node)) {
			status = AE_TYPE;
			goto unlock_and_exit;
		}

		/* Check for an existing internal object */

		obj_desc = acpi_ns_get_attached_object (node);
		if (obj_desc) {

			/* Object exists - make sure there's no handler */

			if (((handler_type == ACPI_SYSTEM_NOTIFY) &&
				  obj_desc->common_notify.sys_handler) ||
				((handler_type == ACPI_DEVICE_NOTIFY) &&
				  obj_desc->common_notify.drv_handler)) {
				status = AE_ALREADY_EXISTS;
				goto unlock_and_exit;
			}
		}
		else {
			/* Create a new object */

			obj_desc = acpi_ut_create_internal_object (node->type);
			if (!obj_desc) {
				status = AE_NO_MEMORY;
				goto unlock_and_exit;
			}

			/* Attach new object to the Node */

			status = acpi_ns_attach_object (device, obj_desc, node->type);
			if (ACPI_FAILURE (status)) {
				goto unlock_and_exit;
			}
		}

		/* Install the handler */

		notify_obj = acpi_ut_create_internal_object (INTERNAL_TYPE_NOTIFY);
		if (!notify_obj) {
			status = AE_NO_MEMORY;
			goto unlock_and_exit;
		}

		notify_obj->notify_handler.node = node;
		notify_obj->notify_handler.handler = handler;
		notify_obj->notify_handler.context = context;

		if (handler_type == ACPI_SYSTEM_NOTIFY) {
			obj_desc->common_notify.sys_handler = notify_obj;
		}
		else /* ACPI_DEVICE_NOTIFY */ {
			obj_desc->common_notify.drv_handler = notify_obj;
		}
	}


unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_remove_notify_handler
 *
 * PARAMETERS:  Device          - The device for which notifies will be handled
 *              Handler_type    - The type of handler:
 *                                  ACPI_SYSTEM_NOTIFY: System_handler (00-7f)
 *                                  ACPI_DEVICE_NOTIFY: Driver_handler (80-ff)
 *              Handler         - Address of the handler
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for notifies on an ACPI device
 *
 ******************************************************************************/

acpi_status
acpi_remove_notify_handler (
	acpi_handle             device,
	u32                     handler_type,
	acpi_notify_handler     handler)
{
	acpi_operand_object     *notify_obj;
	acpi_operand_object     *obj_desc;
	acpi_namespace_node     *node;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_remove_notify_handler");


	/* Parameter validation */

	if ((!device)  ||
		(!handler) ||
		(handler_type > ACPI_MAX_NOTIFY_HANDLER_TYPE)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Convert and validate the device handle */

	node = acpi_ns_map_handle_to_node (device);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/*
	 * Root Object
	 */
	if (device == ACPI_ROOT_OBJECT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing notify handler for ROOT object.\n"));

		if (((handler_type == ACPI_SYSTEM_NOTIFY) &&
			  !acpi_gbl_sys_notify.handler) ||
			((handler_type == ACPI_DEVICE_NOTIFY) &&
			  !acpi_gbl_drv_notify.handler)) {
			status = AE_NOT_EXIST;
			goto unlock_and_exit;
		}

		if (handler_type == ACPI_SYSTEM_NOTIFY) {
			acpi_gbl_sys_notify.node  = NULL;
			acpi_gbl_sys_notify.handler = NULL;
			acpi_gbl_sys_notify.context = NULL;
		}
		else {
			acpi_gbl_drv_notify.node  = NULL;
			acpi_gbl_drv_notify.handler = NULL;
			acpi_gbl_drv_notify.context = NULL;
		}
	}

	/*
	 * All Other Objects
	 */
	else {
		/* Notifies allowed on this object? */

		if (!acpi_ev_is_notify_object (node)) {
			status = AE_TYPE;
			goto unlock_and_exit;
		}

		/* Check for an existing internal object */

		obj_desc = acpi_ns_get_attached_object (node);
		if (!obj_desc) {
			status = AE_NOT_EXIST;
			goto unlock_and_exit;
		}

		/* Object exists - make sure there's an existing handler */

		if (handler_type == ACPI_SYSTEM_NOTIFY) {
			notify_obj = obj_desc->common_notify.sys_handler;
		}
		else {
			notify_obj = obj_desc->common_notify.drv_handler;
		}

		if ((!notify_obj) ||
			(notify_obj->notify_handler.handler != handler)) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}

		/* Remove the handler */

		if (handler_type == ACPI_SYSTEM_NOTIFY) {
			obj_desc->common_notify.sys_handler = NULL;
		}
		else {
			obj_desc->common_notify.drv_handler = NULL;
		}

		acpi_ut_remove_reference (notify_obj);
	}


unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_install_gpe_handler
 *
 * PARAMETERS:  Gpe_number      - The GPE number.  The numbering scheme is
 *                                bank 0 first, then bank 1.
 *              Type            - Whether this GPE should be treated as an
 *                                edge- or level-triggered interrupt.
 *              Handler         - Address of the handler
 *              Context         - Value passed to the handler on each GPE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Install a handler for a General Purpose Event.
 *
 ******************************************************************************/

acpi_status
acpi_install_gpe_handler (
	u32                     gpe_number,
	u32                     type,
	acpi_gpe_handler        handler,
	void                    *context)
{
	acpi_status             status;
	u32                     gpe_number_index;


	ACPI_FUNCTION_TRACE ("Acpi_install_gpe_handler");


	/* Parameter validation */

	if (!handler) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Ensure that we have a valid GPE number */

	gpe_number_index = acpi_ev_get_gpe_number_index (gpe_number);
	if (gpe_number_index == ACPI_GPE_INVALID) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Make sure that there isn't a handler there already */

	if (acpi_gbl_gpe_number_info[gpe_number_index].handler) {
		status = AE_ALREADY_EXISTS;
		goto cleanup;
	}

	/* Install the handler */

	acpi_gbl_gpe_number_info[gpe_number_index].handler = handler;
	acpi_gbl_gpe_number_info[gpe_number_index].context = context;
	acpi_gbl_gpe_number_info[gpe_number_index].type = (u8) type;

	/* Clear the GPE (of stale events), the enable it */

	status = acpi_hw_clear_gpe (gpe_number);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	status = acpi_hw_enable_gpe (gpe_number);


cleanup:
	(void) acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_remove_gpe_handler
 *
 * PARAMETERS:  Gpe_number      - The event to remove a handler
 *              Handler         - Address of the handler
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a handler for a General Purpose Acpi_event.
 *
 ******************************************************************************/

acpi_status
acpi_remove_gpe_handler (
	u32                     gpe_number,
	acpi_gpe_handler        handler)
{
	acpi_status             status;
	u32                     gpe_number_index;


	ACPI_FUNCTION_TRACE ("Acpi_remove_gpe_handler");


	/* Parameter validation */

	if (!handler) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Ensure that we have a valid GPE number */

	gpe_number_index = acpi_ev_get_gpe_number_index (gpe_number);
	if (gpe_number_index == ACPI_GPE_INVALID) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Disable the GPE before removing the handler */

	status = acpi_hw_disable_gpe (gpe_number);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_EVENTS);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Make sure that the installed handler is the same */

	if (acpi_gbl_gpe_number_info[gpe_number_index].handler != handler) {
		(void) acpi_hw_enable_gpe (gpe_number);
		status = AE_BAD_PARAMETER;
		goto cleanup;
	}

	/* Remove the handler */

	acpi_gbl_gpe_number_info[gpe_number_index].handler = NULL;
	acpi_gbl_gpe_number_info[gpe_number_index].context = NULL;


cleanup:
	(void) acpi_ut_release_mutex (ACPI_MTX_EVENTS);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_acquire_global_lock
 *
 * PARAMETERS:  Timeout         - How long the caller is willing to wait
 *              Out_handle      - A handle to the lock if acquired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire the ACPI Global Lock
 *
 ******************************************************************************/

acpi_status
acpi_acquire_global_lock (
	u32                     timeout,
	u32                     *handle)
{
	acpi_status             status;


	if (!handle) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ex_enter_interpreter ();
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	status = acpi_ev_acquire_global_lock (timeout);
	acpi_ex_exit_interpreter ();

	if (ACPI_SUCCESS (status)) {
		acpi_gbl_global_lock_handle++;
		*handle = acpi_gbl_global_lock_handle;
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_release_global_lock
 *
 * PARAMETERS:  Handle      - Returned from Acpi_acquire_global_lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release the ACPI Global Lock
 *
 ******************************************************************************/

acpi_status
acpi_release_global_lock (
	u32                     handle)
{
	acpi_status             status;


	if (handle != acpi_gbl_global_lock_handle) {
		return (AE_NOT_ACQUIRED);
	}

	status = acpi_ev_release_global_lock ();
	return (status);
}


