/*******************************************************************************
 *
 * Module Name: nsxfobj - Public interfaces to the ACPI subsystem
 *                         ACPI Object oriented interfaces
 *              $Revision: 112 $
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
#include "acnamesp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsxfobj")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_evaluate_object_typed
 *
 * PARAMETERS:  Handle              - Object handle (optional)
 *              *Pathname           - Object pathname (optional)
 *              **External_params   - List of parameters to pass to method,
 *                                    terminated by NULL.  May be NULL
 *                                    if no parameters are being passed.
 *              *Return_buffer      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *              Return_type         - Expected type of return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and evaluate the given object, passing the given
 *              parameters if necessary.  One of "Handle" or "Pathname" must
 *              be valid (non-null)
 *
 ******************************************************************************/

acpi_status
acpi_evaluate_object_typed (
	acpi_handle             handle,
	acpi_string             pathname,
	acpi_object_list        *external_params,
	acpi_buffer             *return_buffer,
	acpi_object_type        return_type)
{
	acpi_status             status;
	u8                      must_free = FALSE;


	ACPI_FUNCTION_TRACE ("Acpi_evaluate_object_typed");


	/* Return buffer must be valid */

	if (!return_buffer) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (return_buffer->length == ACPI_ALLOCATE_BUFFER) {
		must_free = TRUE;
	}

	/* Evaluate the object */

	status = acpi_evaluate_object (handle, pathname, external_params, return_buffer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Type ANY means "don't care" */

	if (return_type == ACPI_TYPE_ANY) {
		return_ACPI_STATUS (AE_OK);
	}

	if (return_buffer->length == 0) {
		/* Error because caller specifically asked for a return value */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"No return value\n"));

		return_ACPI_STATUS (AE_NULL_OBJECT);
	}

	/* Examine the object type returned from Evaluate_object */

	if (((acpi_object *) return_buffer->pointer)->type == return_type) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Return object type does not match requested type */

	ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
		"Incorrect return type [%s] requested [%s]\n",
		acpi_ut_get_type_name (((acpi_object *) return_buffer->pointer)->type),
		acpi_ut_get_type_name (return_type)));

	if (must_free) {
		/* Caller used ACPI_ALLOCATE_BUFFER, free the return buffer */

		acpi_os_free (return_buffer->pointer);
		return_buffer->pointer = NULL;
	}

	return_buffer->length = 0;
	return_ACPI_STATUS (AE_TYPE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_evaluate_object
 *
 * PARAMETERS:  Handle              - Object handle (optional)
 *              *Pathname           - Object pathname (optional)
 *              **External_params   - List of parameters to pass to method,
 *                                    terminated by NULL.  May be NULL
 *                                    if no parameters are being passed.
 *              *Return_buffer      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and evaluate the given object, passing the given
 *              parameters if necessary.  One of "Handle" or "Pathname" must
 *              be valid (non-null)
 *
 ******************************************************************************/

acpi_status
acpi_evaluate_object (
	acpi_handle             handle,
	acpi_string             pathname,
	acpi_object_list        *external_params,
	acpi_buffer             *return_buffer)
{
	acpi_status             status;
	acpi_operand_object     **internal_params = NULL;
	acpi_operand_object     *internal_return_obj = NULL;
	ACPI_SIZE               buffer_space_needed;
	u32                     i;


	ACPI_FUNCTION_TRACE ("Acpi_evaluate_object");


	/*
	 * If there are parameters to be passed to the object
	 * (which must be a control method), the external objects
	 * must be converted to internal objects
	 */
	if (external_params && external_params->count) {
		/*
		 * Allocate a new parameter block for the internal objects
		 * Add 1 to count to allow for null terminated internal list
		 */
		internal_params = ACPI_MEM_CALLOCATE (((ACPI_SIZE) external_params->count + 1) *
				  sizeof (void *));
		if (!internal_params) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/*
		 * Convert each external object in the list to an
		 * internal object
		 */
		for (i = 0; i < external_params->count; i++) {
			status = acpi_ut_copy_eobject_to_iobject (&external_params->pointer[i],
					 &internal_params[i]);
			if (ACPI_FAILURE (status)) {
				acpi_ut_delete_internal_object_list (internal_params);
				return_ACPI_STATUS (status);
			}
		}
		internal_params[external_params->count] = NULL;
	}

	/*
	 * Three major cases:
	 * 1) Fully qualified pathname
	 * 2) No handle, not fully qualified pathname (error)
	 * 3) Valid handle
	 */
	if ((pathname) &&
		(acpi_ns_valid_root_prefix (pathname[0]))) {
		/*
		 *  The path is fully qualified, just evaluate by name
		 */
		status = acpi_ns_evaluate_by_name (pathname, internal_params,
				 &internal_return_obj);
	}
	else if (!handle) {
		/*
		 * A handle is optional iff a fully qualified pathname
		 * is specified.  Since we've already handled fully
		 * qualified names above, this is an error
		 */
		if (!pathname) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Both Handle and Pathname are NULL\n"));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Handle is NULL and Pathname is relative\n"));
		}

		status = AE_BAD_PARAMETER;
	}
	else {
		/*
		 * We get here if we have a handle -- and if we have a
		 * pathname it is relative.  The handle will be validated
		 * in the lower procedures
		 */
		if (!pathname) {
			/*
			 * The null pathname case means the handle is for
			 * the actual object to be evaluated
			 */
			status = acpi_ns_evaluate_by_handle (handle, internal_params,
					  &internal_return_obj);
		}
		else {
		   /*
			* Both a Handle and a relative Pathname
			*/
			status = acpi_ns_evaluate_relative (handle, pathname, internal_params,
					  &internal_return_obj);
		}
	}


	/*
	 * If we are expecting a return value, and all went well above,
	 * copy the return value to an external object.
	 */
	if (return_buffer) {
		if (!internal_return_obj) {
			return_buffer->length = 0;
		}
		else {
			if (ACPI_GET_DESCRIPTOR_TYPE (internal_return_obj) == ACPI_DESC_TYPE_NAMED) {
				/*
				 * If we received a NS Node as a return object, this means that
				 * the object we are evaluating has nothing interesting to
				 * return (such as a mutex, etc.)  We return an error because
				 * these types are essentially unsupported by this interface.
				 * We don't check up front because this makes it easier to add
				 * support for various types at a later date if necessary.
				 */
				status = AE_TYPE;
				internal_return_obj = NULL; /* No need to delete a NS Node */
				return_buffer->length = 0;
			}

			if (ACPI_SUCCESS (status)) {
				/*
				 * Find out how large a buffer is needed
				 * to contain the returned object
				 */
				status = acpi_ut_get_object_size (internal_return_obj,
						   &buffer_space_needed);
				if (ACPI_SUCCESS (status)) {
					/* Validate/Allocate/Clear caller buffer */

					status = acpi_ut_initialize_buffer (return_buffer, buffer_space_needed);
					if (ACPI_FAILURE (status)) {
						/*
						 * Caller's buffer is too small or a new one can't be allocated
						 */
						ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
							"Needed buffer size %X, %s\n",
							(u32) buffer_space_needed, acpi_format_exception (status)));
					}
					else {
						/*
						 *  We have enough space for the object, build it
						 */
						status = acpi_ut_copy_iobject_to_eobject (internal_return_obj,
								  return_buffer);
					}
				}
			}
		}
	}

	/* Delete the return and parameter objects */

	if (internal_return_obj) {
		/*
		 * Delete the internal return object. (Or at least
		 * decrement the reference count by one)
		 */
		acpi_ut_remove_reference (internal_return_obj);
	}

	/*
	 * Free the input parameter list (if we created one),
	 */
	if (internal_params) {
		/* Free the allocated parameter block */

		acpi_ut_delete_internal_object_list (internal_params);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_next_object
 *
 * PARAMETERS:  Type            - Type of object to be searched for
 *              Parent          - Parent object whose children we are getting
 *              Last_child      - Previous child that was found.
 *                                The NEXT child will be returned
 *              Ret_handle      - Where handle to the next object is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the next peer object within the namespace.  If Handle is
 *              valid, Scope is ignored.  Otherwise, the first object within
 *              Scope is returned.
 *
 ******************************************************************************/

acpi_status
acpi_get_next_object (
	acpi_object_type        type,
	acpi_handle             parent,
	acpi_handle             child,
	acpi_handle             *ret_handle)
{
	acpi_status             status;
	acpi_namespace_node     *node;
	acpi_namespace_node     *parent_node = NULL;
	acpi_namespace_node     *child_node = NULL;


	/* Parameter validation */

	if (type > ACPI_TYPE_MAX) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* If null handle, use the parent */

	if (!child) {
		/* Start search at the beginning of the specified scope */

		parent_node = acpi_ns_map_handle_to_node (parent);
		if (!parent_node) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}
	else {
		/* Non-null handle, ignore the parent */
		/* Convert and validate the handle */

		child_node = acpi_ns_map_handle_to_node (child);
		if (!child_node) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}

	/* Internal function does the real work */

	node = acpi_ns_get_next_node (type, parent_node, child_node);
	if (!node) {
		status = AE_NOT_FOUND;
		goto unlock_and_exit;
	}

	if (ret_handle) {
		*ret_handle = acpi_ns_convert_entry_to_handle (node);
	}


unlock_and_exit:

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_type
 *
 * PARAMETERS:  Handle          - Handle of object whose type is desired
 *              *Ret_type       - Where the type will be placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine returns the type associatd with a particular handle
 *
 ******************************************************************************/

acpi_status
acpi_get_type (
	acpi_handle             handle,
	acpi_object_type        *ret_type)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	/* Parameter Validation */

	if (!ret_type) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Special case for the predefined Root Node
	 * (return type ANY)
	 */
	if (handle == ACPI_ROOT_OBJECT) {
		*ret_type = ACPI_TYPE_ANY;
		return (AE_OK);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return (AE_BAD_PARAMETER);
	}

	*ret_type = node->type;


	status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_parent
 *
 * PARAMETERS:  Handle          - Handle of object whose parent is desired
 *              Ret_handle      - Where the parent handle will be placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns a handle to the parent of the object represented by
 *              Handle.
 *
 ******************************************************************************/

acpi_status
acpi_get_parent (
	acpi_handle             handle,
	acpi_handle             *ret_handle)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	if (!ret_handle) {
		return (AE_BAD_PARAMETER);
	}

	/* Special case for the predefined Root Node (no parent) */

	if (handle == ACPI_ROOT_OBJECT) {
		return (AE_NULL_ENTRY);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Get the parent entry */

	*ret_handle =
		acpi_ns_convert_entry_to_handle (acpi_ns_get_parent_node (node));

	/* Return exeption if parent is null */

	if (!acpi_ns_get_parent_node (node)) {
		status = AE_NULL_ENTRY;
	}


unlock_and_exit:

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_walk_namespace
 *
 * PARAMETERS:  Type                - acpi_object_type to search for
 *              Start_object        - Handle in namespace where search begins
 *              Max_depth           - Depth to which search is to reach
 *              User_function       - Called when an object of "Type" is found
 *              Context             - Passed to user function
 *              Return_value        - Location where return value of
 *                                    User_function is put if terminated early
 *
 * RETURNS      Return value from the User_function if terminated early.
 *              Otherwise, returns NULL.
 *
 * DESCRIPTION: Performs a modified depth-first walk of the namespace tree,
 *              starting (and ending) at the object specified by Start_handle.
 *              The User_function is called whenever an object that matches
 *              the type parameter is found.  If the user function returns
 *              a non-zero value, the search is terminated immediately and this
 *              value is returned to the caller.
 *
 *              The point of this procedure is to provide a generic namespace
 *              walk routine that can be called from multiple places to
 *              provide multiple services;  the User Function can be tailored
 *              to each task, whether it is a print function, a compare
 *              function, etc.
 *
 ******************************************************************************/

acpi_status
acpi_walk_namespace (
	acpi_object_type        type,
	acpi_handle             start_object,
	u32                     max_depth,
	acpi_walk_callback      user_function,
	void                    *context,
	void                    **return_value)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_walk_namespace");


	/* Parameter validation */

	if ((type > ACPI_TYPE_MAX)  ||
		(!max_depth)            ||
		(!user_function)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Lock the namespace around the walk.
	 * The namespace will be unlocked/locked around each call
	 * to the user function - since this function
	 * must be allowed to make Acpi calls itself.
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_ns_walk_namespace (type, start_object, max_depth, ACPI_NS_WALK_UNLOCK,
			  user_function, context, return_value);

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_device_callback
 *
 * PARAMETERS:  Callback from Acpi_get_device
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes callbacks from Walk_namespace and filters out all non-
 *              present devices, or if they specified a HID, it filters based
 *              on that.
 *
 ******************************************************************************/

static acpi_status
acpi_ns_get_device_callback (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_status             status;
	acpi_namespace_node     *node;
	u32                     flags;
	acpi_device_id          hid;
	acpi_device_id          cid;
	acpi_get_devices_info   *info;


	info = context;

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node (obj_handle);
	status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (!node) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Run _STA to determine if device is present
	 */
	status = acpi_ut_execute_STA (node, &flags);
	if (ACPI_FAILURE (status)) {
		return (AE_CTRL_DEPTH);
	}

	if (!(flags & 0x01)) {
		/* Don't return at the device or children of the device if not there */
		return (AE_CTRL_DEPTH);
	}

	/*
	 * Filter based on device HID & CID
	 */
	if (info->hid != NULL) {
		status = acpi_ut_execute_HID (node, &hid);
		if (status == AE_NOT_FOUND) {
			return (AE_OK);
		}
		else if (ACPI_FAILURE (status)) {
			return (AE_CTRL_DEPTH);
		}

		if (ACPI_STRNCMP (hid.buffer, info->hid, sizeof (hid.buffer)) != 0) {
			status = acpi_ut_execute_CID (node, &cid);
			if (status == AE_NOT_FOUND) {
				return (AE_OK);
			}
			else if (ACPI_FAILURE (status)) {
				return (AE_CTRL_DEPTH);
			}

			/* TBD: Handle CID packages */

			if (ACPI_STRNCMP (cid.buffer, info->hid, sizeof (cid.buffer)) != 0) {
				return (AE_OK);
			}
		}
	}

	status = info->user_function (obj_handle, nesting_level, info->context, return_value);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_devices
 *
 * PARAMETERS:  HID                 - HID to search for. Can be NULL.
 *              User_function       - Called when a matching object is found
 *              Context             - Passed to user function
 *              Return_value        - Location where return value of
 *                                    User_function is put if terminated early
 *
 * RETURNS      Return value from the User_function if terminated early.
 *              Otherwise, returns NULL.
 *
 * DESCRIPTION: Performs a modified depth-first walk of the namespace tree,
 *              starting (and ending) at the object specified by Start_handle.
 *              The User_function is called whenever an object that matches
 *              the type parameter is found.  If the user function returns
 *              a non-zero value, the search is terminated immediately and this
 *              value is returned to the caller.
 *
 *              This is a wrapper for Walk_namespace, but the callback performs
 *              additional filtering. Please see Acpi_get_device_callback.
 *
 ******************************************************************************/

acpi_status
acpi_get_devices (
	NATIVE_CHAR             *HID,
	acpi_walk_callback      user_function,
	void                    *context,
	void                    **return_value)
{
	acpi_status             status;
	acpi_get_devices_info   info;


	ACPI_FUNCTION_TRACE ("Acpi_get_devices");


	/* Parameter validation */

	if (!user_function) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * We're going to call their callback from OUR callback, so we need
	 * to know what it is, and their context parameter.
	 */
	info.context      = context;
	info.user_function = user_function;
	info.hid          = HID;

	/*
	 * Lock the namespace around the walk.
	 * The namespace will be unlocked/locked around each call
	 * to the user function - since this function
	 * must be allowed to make Acpi calls itself.
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_ns_walk_namespace (ACPI_TYPE_DEVICE,
			   ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			   ACPI_NS_WALK_UNLOCK,
			   acpi_ns_get_device_callback, &info,
			   return_value);

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_attach_data
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_attach_data (
	acpi_handle             obj_handle,
	ACPI_OBJECT_HANDLER     handler,
	void                    *data)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	/* Parameter validation */

	if (!obj_handle ||
		!handler    ||
		!data) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ns_attach_data (node, handler, data);

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_detach_data
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_detach_data (
	acpi_handle             obj_handle,
	ACPI_OBJECT_HANDLER     handler)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	/* Parameter validation */

	if (!obj_handle ||
		!handler) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ns_detach_data (node, handler);

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_data
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_get_data (
	acpi_handle             obj_handle,
	ACPI_OBJECT_HANDLER     handler,
	void                    **data)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	/* Parameter validation */

	if (!obj_handle ||
		!handler    ||
		!data) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	status = acpi_ns_get_attached_data (node, handler, data);

unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


