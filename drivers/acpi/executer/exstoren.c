
/******************************************************************************
 *
 * Module Name: exstoren - AML Interpreter object store support,
 *                        Store to Node (namespace object)
 *              $Revision: 51 $
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
#include "acinterp.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exstoren")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_object
 *
 * PARAMETERS:  Source_desc_ptr     - Pointer to the source object
 *              Target_type         - Current type of the target
 *              Walk_state          - Current walk state
 *
 * RETURN:      Status, resolved object in Source_desc_ptr.
 *
 * DESCRIPTION: Resolve an object.  If the object is a reference, dereference
 *              it and return the actual object in the Source_desc_ptr.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_object (
	acpi_operand_object     **source_desc_ptr,
	acpi_object_type        target_type,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *source_desc = *source_desc_ptr;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ex_resolve_object");


	/*
	 * Ensure we have a Target that can be stored to
	 */
	switch (target_type) {
	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:
		/*
		 * These cases all require only Integers or values that
		 * can be converted to Integers (Strings or Buffers)
		 */

	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:

		/*
		 * Stores into a Field/Region or into a Integer/Buffer/String
		 * are all essentially the same.  This case handles the
		 * "interchangeable" types Integer, String, and Buffer.
		 */
		if (ACPI_GET_OBJECT_TYPE (source_desc) == INTERNAL_TYPE_REFERENCE) {
			/* Resolve a reference object first */

			status = acpi_ex_resolve_to_value (source_desc_ptr, walk_state);
			if (ACPI_FAILURE (status)) {
				break;
			}
		}

		/*
		 * Must have a Integer, Buffer, or String
		 */
		if ((ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_INTEGER)    &&
			(ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_BUFFER)     &&
			(ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_TYPE_STRING)) {
			/*
			 * Conversion successful but still not a valid type
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Cannot assign type %s to %s (must be type Int/Str/Buf)\n",
				acpi_ut_get_object_type_name (source_desc),
				acpi_ut_get_type_name (target_type)));
			status = AE_AML_OPERAND_TYPE;
		}
		break;


	case INTERNAL_TYPE_ALIAS:

		/*
		 * Aliases are resolved by Acpi_ex_prep_operands
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Store into Alias - should never happen\n"));
		status = AE_AML_INTERNAL;
		break;


	case ACPI_TYPE_PACKAGE:
	default:

		/*
		 * All other types than Alias and the various Fields come here,
		 * including the untyped case - ACPI_TYPE_ANY.
		 */
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store_object_to_object
 *
 * PARAMETERS:  Source_desc         - Object to store
 *              Dest_desc           - Object to receive a copy of the source
 *              New_desc            - New object if Dest_desc is obsoleted
 *              Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: "Store" an object to another object.  This may include
 *              converting the source type to the target type (implicit
 *              conversion), and a copy of the value of the source to
 *              the target.
 *
 *              The Assignment of an object to another (not named) object
 *              is handled here.
 *              The Source passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              This module allows destination types of Number, String,
 *              Buffer, and Package.
 *
 *              Assumes parameters are already validated.  NOTE: Source_desc
 *              resolution (from a reference object) must be performed by
 *              the caller if necessary.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_object (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc,
	acpi_operand_object     **new_desc,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *actual_src_desc;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE_PTR ("Acpi_ex_store_object_to_object", source_desc);


	actual_src_desc = source_desc;
	if (!dest_desc) {
		/*
		 * There is no destination object (An uninitialized node or
		 * package element), so we can simply copy the source object
		 * creating a new destination object
		 */
		status = acpi_ut_copy_iobject_to_iobject (actual_src_desc, new_desc, walk_state);
		return_ACPI_STATUS (status);
	}

	if (ACPI_GET_OBJECT_TYPE (source_desc) != ACPI_GET_OBJECT_TYPE (dest_desc)) {
		/*
		 * The source type does not match the type of the destination.
		 * Perform the "implicit conversion" of the source to the current type
		 * of the target as per the ACPI specification.
		 *
		 * If no conversion performed, Actual_src_desc = Source_desc.
		 * Otherwise, Actual_src_desc is a temporary object to hold the
		 * converted object.
		 */
		status = acpi_ex_convert_to_target_type (ACPI_GET_OBJECT_TYPE (dest_desc), source_desc,
				  &actual_src_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		if (source_desc == actual_src_desc) {
			/*
			 * No conversion was performed.  Return the Source_desc as the
			 * new object.
			 */
			*new_desc = source_desc;
			return_ACPI_STATUS (AE_OK);
		}
	}

	/*
	 * We now have two objects of identical types, and we can perform a
	 * copy of the *value* of the source object.
	 */
	switch (ACPI_GET_OBJECT_TYPE (dest_desc)) {
	case ACPI_TYPE_INTEGER:

		dest_desc->integer.value = actual_src_desc->integer.value;

		/* Truncate value if we are executing from a 32-bit ACPI table */

		acpi_ex_truncate_for32bit_table (dest_desc);
		break;

	case ACPI_TYPE_STRING:

		status = acpi_ex_store_string_to_string (actual_src_desc, dest_desc);
		break;

	case ACPI_TYPE_BUFFER:

		status = acpi_ex_store_buffer_to_buffer (actual_src_desc, dest_desc);
		break;

	case ACPI_TYPE_PACKAGE:

		status = acpi_ut_copy_iobject_to_iobject (actual_src_desc, &dest_desc, walk_state);
		break;

	default:
		/*
		 * All other types come here.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Store into type %s not implemented\n",
			acpi_ut_get_object_type_name (dest_desc)));

		status = AE_NOT_IMPLEMENTED;
		break;
	}

	if (actual_src_desc != source_desc) {
		/* Delete the intermediate (temporary) source object */

		acpi_ut_remove_reference (actual_src_desc);
	}

	*new_desc = dest_desc;
	return_ACPI_STATUS (status);
}


