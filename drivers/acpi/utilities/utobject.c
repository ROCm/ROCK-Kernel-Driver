/******************************************************************************
 *
 * Module Name: utobject - ACPI object create/delete/size/cache routines
 *              $Revision: 57 $
 *
 *****************************************************************************/

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
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"
#include "amlcode.h"


#define _COMPONENT          ACPI_UTILITIES
	 MODULE_NAME         ("utobject")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_internal_object_dbg
 *
 * PARAMETERS:  Address             - Address of the memory to deallocate
 *              Component           - Component type of caller
 *              Module              - Source file name of caller
 *              Line                - Line number of caller
 *              Type                - ACPI Type of the new object
 *
 * RETURN:      Object              - The new object.  Null on failure
 *
 * DESCRIPTION: Create and initialize a new internal object.
 *
 * NOTE:        We always allocate the worst-case object descriptor because
 *              these objects are cached, and we want them to be
 *              one-size-satisifies-any-request.  This in itself may not be
 *              the most memory efficient, but the efficiency of the object
 *              cache should more than make up for this!
 *
 ******************************************************************************/

acpi_operand_object  *
acpi_ut_create_internal_object_dbg (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id,
	acpi_object_type8       type)
{
	acpi_operand_object     *object;


	FUNCTION_TRACE_STR ("Ut_create_internal_object_dbg", acpi_ut_get_type_name (type));


	/* Allocate the raw object descriptor */

	object = acpi_ut_allocate_object_desc_dbg (module_name, line_number, component_id);
	if (!object) {
		/* Allocation failure */

		return_PTR (NULL);
	}

	/* Save the object type in the object descriptor */

	object->common.type = type;

	/* Init the reference count */

	object->common.reference_count = 1;

	/* Any per-type initialization should go here */

	return_PTR (object);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_valid_internal_object
 *
 * PARAMETERS:  Operand             - Object to be validated
 *
 * RETURN:      Validate a pointer to be an acpi_operand_object
 *
 ******************************************************************************/

u8
acpi_ut_valid_internal_object (
	void                    *object)
{

	PROC_NAME ("Ut_valid_internal_object");


	/* Check for a null pointer */

	if (!object) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"**** Null Object Ptr\n"));
		return (FALSE);
	}

	/* Check the descriptor type field */

	if (!VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_INTERNAL)) {
		/* Not an ACPI internal object, do some further checking */

		if (VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_NAMED)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				"**** Obj %p is a named obj, not ACPI obj\n", object));
		}

		else if (VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_PARSER)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				"**** Obj %p is a parser obj, not ACPI obj\n", object));
		}

		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				"**** Obj %p is of unknown type\n", object));
		}

		return (FALSE);
	}


	/* The object appears to be a valid acpi_operand_object  */

	return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_allocate_object_desc_dbg
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      Pointer to newly allocated object descriptor.  Null on error
 *
 * DESCRIPTION: Allocate a new object descriptor.  Gracefully handle
 *              error conditions.
 *
 ******************************************************************************/

void *
acpi_ut_allocate_object_desc_dbg (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id)
{
	acpi_operand_object     *object;


	FUNCTION_TRACE ("Ut_allocate_object_desc_dbg");


	object = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_OPERAND);
	if (!object) {
		_REPORT_ERROR (module_name, line_number, component_id,
				  ("Could not allocate an object descriptor\n"));

		return_PTR (NULL);
	}


	/* Mark the descriptor type */

	object->common.data_type = ACPI_DESC_TYPE_INTERNAL;

	ACPI_DEBUG_PRINT ((ACPI_DB_ALLOCATIONS, "%p Size %X\n",
			object, sizeof (acpi_operand_object)));

	return_PTR (object);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_object_desc
 *
 * PARAMETERS:  Object          - Acpi internal object to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an ACPI object descriptor or add it to the object cache
 *
 ******************************************************************************/

void
acpi_ut_delete_object_desc (
	acpi_operand_object     *object)
{
	FUNCTION_TRACE_PTR ("Ut_delete_object_desc", object);


	/* Object must be an acpi_operand_object  */

	if (object->common.data_type != ACPI_DESC_TYPE_INTERNAL) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Obj %p is not an ACPI object\n", object));
		return_VOID;
	}

	acpi_ut_release_to_cache (ACPI_MEM_LIST_OPERAND, object);

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_object_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Purge the global state object cache.  Used during subsystem
 *              termination.
 *
 ******************************************************************************/

void
acpi_ut_delete_object_cache (
	void)
{
	FUNCTION_TRACE ("Ut_delete_object_cache");


	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_OPERAND);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_simple_object_size
 *
 * PARAMETERS:  *Internal_object    - Pointer to the object we are examining
 *              *Ret_length         - Where the length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain a simple object for return to an API user.
 *
 *              The length includes the object structure plus any additional
 *              needed space.
 *
 ******************************************************************************/

acpi_status
acpi_ut_get_simple_object_size (
	acpi_operand_object     *internal_object,
	u32                     *obj_length)
{
	u32                     length;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_PTR ("Ut_get_simple_object_size", internal_object);


	/* Handle a null object (Could be a uninitialized package element -- which is legal) */

	if (!internal_object) {
		*obj_length = 0;
		return_ACPI_STATUS (AE_OK);
	}


	/* Start with the length of the Acpi object */

	length = sizeof (acpi_object);

	if (VALID_DESCRIPTOR_TYPE (internal_object, ACPI_DESC_TYPE_NAMED)) {
		/* Object is a named object (reference), just return the length */

		*obj_length = (u32) ROUND_UP_TO_NATIVE_WORD (length);
		return_ACPI_STATUS (status);
	}


	/*
	 * The final length depends on the object type
	 * Strings and Buffers are packed right up against the parent object and
	 * must be accessed bytewise or there may be alignment problems on
	 * certain processors
	 */

	switch (internal_object->common.type) {

	case ACPI_TYPE_STRING:

		length += internal_object->string.length + 1;
		break;


	case ACPI_TYPE_BUFFER:

		length += internal_object->buffer.length;
		break;


	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_PROCESSOR:
	case ACPI_TYPE_POWER:

		/*
		 * No extra data for these types
		 */
		break;


	case INTERNAL_TYPE_REFERENCE:

		/*
		 * The only type that should be here is internal opcode NAMEPATH_OP -- since
		 * this means an object reference
		 */
		if (internal_object->reference.opcode != AML_INT_NAMEPATH_OP) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Unsupported Reference opcode=%X in object %p\n",
				internal_object->reference.opcode, internal_object));
			status = AE_TYPE;
		}

		else {
			/*
			 * Get the actual length of the full pathname to this object.
			 * The reference will be converted to the pathname to the object
			 */
			length += ROUND_UP_TO_NATIVE_WORD (acpi_ns_get_pathname_length (internal_object->reference.node));
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unsupported type=%X in object %p\n",
			internal_object->common.type, internal_object));
		status = AE_TYPE;
		break;
	}


	/*
	 * Account for the space required by the object rounded up to the next
	 * multiple of the machine word size.  This keeps each object aligned
	 * on a machine word boundary. (preventing alignment faults on some
	 * machines.)
	 */
	*obj_length = (u32) ROUND_UP_TO_NATIVE_WORD (length);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_element_length
 *
 * PARAMETERS:  ACPI_PKG_CALLBACK
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: Get the length of one package element.
 *
 ******************************************************************************/

acpi_status
acpi_ut_get_element_length (
	u8                      object_type,
	acpi_operand_object     *source_object,
	acpi_generic_state      *state,
	void                    *context)
{
	acpi_status             status = AE_OK;
	acpi_pkg_info           *info = (acpi_pkg_info *) context;
	u32                     object_space;


	switch (object_type) {
	case 0:

		/*
		 * Simple object - just get the size (Null object/entry is handled
		 * here also) and sum it into the running package length
		 */
		status = acpi_ut_get_simple_object_size (source_object, &object_space);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		info->length += object_space;
		break;


	case 1:
		/* Package - nothing much to do here, let the walk handle it */

		info->num_packages++;
		state->pkg.this_target_obj = NULL;
		break;

	default:
		return (AE_BAD_PARAMETER);
	}


	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_package_object_size
 *
 * PARAMETERS:  *Internal_object    - Pointer to the object we are examining
 *              *Ret_length         - Where the length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain a package object for return to an API user.
 *
 *              This is moderately complex since a package contains other
 *              objects including packages.
 *
 ******************************************************************************/

acpi_status
acpi_ut_get_package_object_size (
	acpi_operand_object     *internal_object,
	u32                     *obj_length)
{
	acpi_status             status;
	acpi_pkg_info           info;


	FUNCTION_TRACE_PTR ("Ut_get_package_object_size", internal_object);


	info.length      = 0;
	info.object_space = 0;
	info.num_packages = 1;

	status = acpi_ut_walk_package_tree (internal_object, NULL,
			 acpi_ut_get_element_length, &info);

	/*
	 * We have handled all of the objects in all levels of the package.
	 * just add the length of the package objects themselves.
	 * Round up to the next machine word.
	 */
	info.length += ROUND_UP_TO_NATIVE_WORD (sizeof (acpi_object)) *
			  info.num_packages;

	/* Return the total package length */

	*obj_length = info.length;
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_object_size
 *
 * PARAMETERS:  *Internal_object    - Pointer to the object we are examining
 *              *Ret_length         - Where the length will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to determine the space required to
 *              contain an object for return to an API user.
 *
 ******************************************************************************/

acpi_status
acpi_ut_get_object_size(
	acpi_operand_object     *internal_object,
	u32                     *obj_length)
{
	acpi_status             status;


	FUNCTION_ENTRY ();


	if ((VALID_DESCRIPTOR_TYPE (internal_object, ACPI_DESC_TYPE_INTERNAL)) &&
		(IS_THIS_OBJECT_TYPE (internal_object, ACPI_TYPE_PACKAGE))) {
		status = acpi_ut_get_package_object_size (internal_object, obj_length);
	}

	else {
		status = acpi_ut_get_simple_object_size (internal_object, obj_length);
	}

	return (status);
}


