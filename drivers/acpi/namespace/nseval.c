/*******************************************************************************
 *
 * Module Name: nseval - Object evaluation interfaces -- includes control
 *                       method lookup and execution.
 *              $Revision: 102 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
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
#include "amlcode.h"
#include "acparser.h"
#include "acinterp.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nseval")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_evaluate_relative
 *
 * PARAMETERS:  Handle              - The relative containing object
 *              *Pathname           - Name of method to execute, If NULL, the
 *                                    handle is the object to execute
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *              *Return_object      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and execute the requested method using the handle as a
 *              scope
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_evaluate_relative (
	acpi_namespace_node     *handle,
	NATIVE_CHAR             *pathname,
	acpi_operand_object     **params,
	acpi_operand_object     **return_object)
{
	acpi_namespace_node     *prefix_node;
	acpi_status             status;
	acpi_namespace_node     *node = NULL;
	NATIVE_CHAR             *internal_path = NULL;
	acpi_generic_state      scope_info;


	FUNCTION_TRACE ("Ns_evaluate_relative");


	/*
	 * Must have a valid object handle
	 */
	if (!handle) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Build an internal name string for the method */

	status = acpi_ns_internalize_name (pathname, &internal_path);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get the prefix handle and Node */

	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

	prefix_node = acpi_ns_map_handle_to_node (handle);
	if (!prefix_node) {
		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		status = AE_BAD_PARAMETER;
		goto cleanup;
	}

	/* Lookup the name in the namespace */

	scope_info.scope.node = prefix_node;
	status = acpi_ns_lookup (&scope_info, internal_path, ACPI_TYPE_ANY,
			 IMODE_EXECUTE, NS_NO_UPSEARCH, NULL,
			 &node);

	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Object [%s] not found [%s]\n",
			pathname, acpi_format_exception (status)));
		goto cleanup;
	}

	/*
	 * Now that we have a handle to the object, we can attempt
	 * to evaluate it.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%s [%p] Value %p\n",
		pathname, node, node->object));

	status = acpi_ns_evaluate_by_handle (node, params, return_object);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "*** Completed eval of object %s ***\n",
		pathname));

cleanup:

	ACPI_MEM_FREE (internal_path);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_evaluate_by_name
 *
 * PARAMETERS:  Pathname            - Fully qualified pathname to the object
 *              *Return_object      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find and execute the requested method passing the given
 *              parameters
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_evaluate_by_name (
	NATIVE_CHAR             *pathname,
	acpi_operand_object     **params,
	acpi_operand_object     **return_object)
{
	acpi_status             status;
	acpi_namespace_node     *node = NULL;
	NATIVE_CHAR             *internal_path = NULL;


	FUNCTION_TRACE ("Ns_evaluate_by_name");


	/* Build an internal name string for the method */

	status = acpi_ns_internalize_name (pathname, &internal_path);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

	/* Lookup the name in the namespace */

	status = acpi_ns_lookup (NULL, internal_path, ACPI_TYPE_ANY,
			 IMODE_EXECUTE, NS_NO_UPSEARCH, NULL,
			 &node);

	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Object at [%s] was not found, status=%.4X\n",
			pathname, status));
		goto cleanup;
	}

	/*
	 * Now that we have a handle to the object, we can attempt
	 * to evaluate it.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%s [%p] Value %p\n",
		pathname, node, node->object));

	status = acpi_ns_evaluate_by_handle (node, params, return_object);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "*** Completed eval of object %s ***\n",
		pathname));


cleanup:

	/* Cleanup */

	if (internal_path) {
		ACPI_MEM_FREE (internal_path);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_evaluate_by_handle
 *
 * PARAMETERS:  Handle              - Method Node to execute
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *              *Return_object      - Where to put method's return value (if
 *                                    any).  If NULL, no value is returned.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute the requested method passing the given parameters
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_evaluate_by_handle (
	acpi_namespace_node     *handle,
	acpi_operand_object     **params,
	acpi_operand_object     **return_object)
{
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_operand_object     *local_return_object;


	FUNCTION_TRACE ("Ns_evaluate_by_handle");


	/* Check if namespace has been initialized */

	if (!acpi_gbl_root_node) {
		return_ACPI_STATUS (AE_NO_NAMESPACE);
	}

	/* Parameter Validation */

	if (!handle) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	if (return_object) {
		/* Initialize the return value to an invalid object */

		*return_object = NULL;
	}

	/* Get the prefix handle and Node */

	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/*
	 * Two major cases here:
	 * 1) The object is an actual control method -- execute it.
	 * 2) The object is not a method -- just return it's current
	 *      value
	 *
	 * In both cases, the namespace is unlocked by the
	 *  Acpi_ns* procedure
	 */
	if (acpi_ns_get_type (node) == ACPI_TYPE_METHOD) {
		/*
		 * Case 1) We have an actual control method to execute
		 */
		status = acpi_ns_execute_control_method (node, params,
				 &local_return_object);
	}

	else {
		/*
		 * Case 2) Object is NOT a method, just return its
		 * current value
		 */
		status = acpi_ns_get_object_value (node, &local_return_object);
	}


	/*
	 * Check if there is a return value on the stack that must
	 * be dealt with
	 */
	if (status == AE_CTRL_RETURN_VALUE) {
		/*
		 * If the Method returned a value and the caller
		 * provided a place to store a returned value, Copy
		 * the returned value to the object descriptor provided
		 * by the caller.
		 */
		if (return_object) {
			/*
			 * Valid return object, copy the pointer to
			 * the returned object
			 */
			*return_object = local_return_object;
		}


		/* Map AE_RETURN_VALUE to AE_OK, we are done with it */

		if (status == AE_CTRL_RETURN_VALUE) {
			status = AE_OK;
		}
	}

	/*
	 * Namespace was unlocked by the handling Acpi_ns* function,
	 * so we just return
	 */
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_execute_control_method
 *
 * PARAMETERS:  Method_node     - The object/method
 *              **Params            - List of parameters to pass to the method,
 *                                    terminated by NULL.  Params itself may be
 *                                    NULL if no parameters are being passed.
 *              **Return_obj_desc   - List of result objects to be returned
 *                                    from the method.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute the requested method passing the given parameters
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/

acpi_status
acpi_ns_execute_control_method (
	acpi_namespace_node     *method_node,
	acpi_operand_object     **params,
	acpi_operand_object     **return_obj_desc)
{
	acpi_status             status;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE ("Ns_execute_control_method");


	/* Verify that there is a method associated with this object */

	obj_desc = acpi_ns_get_attached_object (method_node);
	if (!obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No attached method object\n"));

		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return_ACPI_STATUS (AE_NULL_OBJECT);
	}


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Control method at Offset %p Length %x]\n",
		obj_desc->method.aml_start + 1, obj_desc->method.aml_length - 1));

	DUMP_PATHNAME (method_node, "Ns_execute_control_method: Executing",
		ACPI_LV_NAMES, _COMPONENT);

	ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "At offset %p\n",
			obj_desc->method.aml_start + 1));


	/*
	 * Unlock the namespace before execution.  This allows namespace access
	 * via the external Acpi* interfaces while a method is being executed.
	 * However, any namespace deletion must acquire both the namespace and
	 * interpreter locks to ensure that no thread is using the portion of the
	 * namespace that is being deleted.
	 */
	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

	/*
	 * Execute the method via the interpreter.  The interpreter is locked
	 * here before calling into the AML parser
	 */
	status = acpi_ex_enter_interpreter ();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_psx_execute (method_node, params, return_obj_desc);
	acpi_ex_exit_interpreter ();

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_object_value
 *
 * PARAMETERS:  Node         - The object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the current value of the object
 *
 * MUTEX:       Assumes namespace is locked
 *
 ******************************************************************************/

acpi_status
acpi_ns_get_object_value (
	acpi_namespace_node     *node,
	acpi_operand_object     **return_obj_desc)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *source_desc;


	FUNCTION_TRACE ("Ns_get_object_value");


	/*
	 *  We take the value from certain objects directly
	 */
	if ((node->type == ACPI_TYPE_PROCESSOR) ||
		(node->type == ACPI_TYPE_POWER)) {
		/*
		 *  Create a Reference object to contain the object
		 */
		obj_desc = acpi_ut_create_internal_object (node->type);
		if (!obj_desc) {
		   status = AE_NO_MEMORY;
		   goto unlock_and_exit;
		}

		/*
		 *  Get the attached object
		 */
		source_desc = acpi_ns_get_attached_object (node);
		if (!source_desc) {
			status = AE_NULL_OBJECT;
			goto unlock_and_exit;
		}

		/*
		 * Just copy from the original to the return object
		 *
		 * TBD: [Future] - need a low-level object copy that handles
		 * the reference count automatically.  (Don't want to copy it)
		 */
		MEMCPY (obj_desc, source_desc, sizeof (acpi_operand_object));
		obj_desc->common.reference_count = 1;
		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	}


	/*
	 * Other objects require a reference object wrapper which we
	 * then attempt to resolve.
	 */
	else {
		/* Create an Reference object to contain the object */

		obj_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_REFERENCE);
		if (!obj_desc) {
		   status = AE_NO_MEMORY;
		   goto unlock_and_exit;
		}

		/* Construct a descriptor pointing to the name */

		obj_desc->reference.opcode = (u8) AML_NAME_OP;
		obj_desc->reference.object = (void *) node;

		/*
		 * Use Resolve_to_value() to get the associated value. This call
		 * always deletes Obj_desc (allocated above).
		 *
		 * NOTE: we can get away with passing in NULL for a walk state
		 * because Obj_desc is guaranteed to not be a reference to either
		 * a method local or a method argument
		 *
		 * Even though we do not directly invoke the interpreter
		 * for this, we must enter it because we could access an opregion.
		 * The opregion access code assumes that the interpreter
		 * is locked.
		 *
		 * We must release the namespace lock before entering the
		 * intepreter.
		 */
		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		status = acpi_ex_enter_interpreter ();
		if (ACPI_SUCCESS (status)) {
			status = acpi_ex_resolve_to_value (&obj_desc, NULL);

			acpi_ex_exit_interpreter ();
		}
	}

	/*
	 * If Acpi_ex_resolve_to_value() succeeded, the return value was
	 * placed in Obj_desc.
	 */
	if (ACPI_SUCCESS (status)) {
		status = AE_CTRL_RETURN_VALUE;

		*return_obj_desc = obj_desc;
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Returning obj %p\n", *return_obj_desc));
	}

	/* Namespace is unlocked */

	return_ACPI_STATUS (status);


unlock_and_exit:

	/* Unlock the namespace */

	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}
