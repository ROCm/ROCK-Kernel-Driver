
/******************************************************************************
 *
 * Module Name: exstoren - AML Interpreter object store support,
 *                        Store to Node (namespace object)
 *              $Revision: 40 $
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
#include "acparser.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "actables.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exstoren")


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
	acpi_object_type8       target_type,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *source_desc = *source_desc_ptr;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_resolve_object");


	/*
	 * Ensure we have a Source that can be stored in the target
	 */
	switch (target_type) {

	/* This case handles the "interchangeable" types Integer, String, and Buffer. */

	/*
	 * These cases all require only Integers or values that
	 * can be converted to Integers (Strings or Buffers)
	 */
	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:

	/*
	 * Stores into a Field/Region or into a Buffer/String
	 * are all essentially the same.
	 */
	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:


		/* TBD: FIX - check for source==REF, resolve, then check type */

		/*
		 * If Source_desc is not a valid type, try to resolve it to one.
		 */
		if ((source_desc->common.type != ACPI_TYPE_INTEGER)    &&
			(source_desc->common.type != ACPI_TYPE_BUFFER)     &&
			(source_desc->common.type != ACPI_TYPE_STRING)) {
			/*
			 * Initially not a valid type, convert
			 */
			status = acpi_ex_resolve_to_value (source_desc_ptr, walk_state);
			if (ACPI_SUCCESS (status) &&
				(source_desc->common.type != ACPI_TYPE_INTEGER)    &&
				(source_desc->common.type != ACPI_TYPE_BUFFER)     &&
				(source_desc->common.type != ACPI_TYPE_STRING)) {
				/*
				 * Conversion successful but still not a valid type
				 */
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Cannot assign type %s to %s (must be type Int/Str/Buf)\n",
					acpi_ut_get_type_name ((*source_desc_ptr)->common.type),
					acpi_ut_get_type_name (target_type)));
				status = AE_AML_OPERAND_TYPE;
			}
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
 * FUNCTION:    Acpi_ex_store_object
 *
 * PARAMETERS:  Source_desc         - Object to store
 *              Target_type         - Current type of the target
 *              Target_desc_ptr     - Pointer to the target
 *              Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: "Store" an object to another object.  This may include
 *              converting the source type to the target type (implicit
 *              conversion), and a copy of the value of the source to
 *              the target.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object (
	acpi_operand_object     *source_desc,
	acpi_object_type8       target_type,
	acpi_operand_object     **target_desc_ptr,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *target_desc = *target_desc_ptr;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_store_object");


	/*
	 * Perform the "implicit conversion" of the source to the current type
	 * of the target - As per the ACPI specification.
	 *
	 * If no conversion performed, Source_desc is left alone, otherwise it
	 * is updated with a new object.
	 */
	status = acpi_ex_convert_to_target_type (target_type, &source_desc, walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * We now have two objects of identical types, and we can perform a
	 * copy of the *value* of the source object.
	 */
	switch (target_type) {
	case ACPI_TYPE_ANY:
	case INTERNAL_TYPE_DEF_ANY:

		/*
		 * The target namespace node is uninitialized (has no target object),
		 * and will take on the type of the source object
		 */
		*target_desc_ptr = source_desc;
		break;


	case ACPI_TYPE_INTEGER:

		target_desc->integer.value = source_desc->integer.value;

		/* Truncate value if we are executing from a 32-bit ACPI table */

		acpi_ex_truncate_for32bit_table (target_desc, walk_state);
		break;

	case ACPI_TYPE_STRING:

		status = acpi_ex_copy_string_to_string (source_desc, target_desc);
		break;


	case ACPI_TYPE_BUFFER:

		status = acpi_ex_copy_buffer_to_buffer (source_desc, target_desc);
		break;


	case ACPI_TYPE_PACKAGE:

		/*
		 * TBD: [Unhandled] Not real sure what to do here
		 */
		status = AE_NOT_IMPLEMENTED;
		break;


	default:

		/*
		 * All other types come here.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Store into type %s not implemented\n",
			acpi_ut_get_type_name (target_type)));

		status = AE_NOT_IMPLEMENTED;
		break;
	}


	return_ACPI_STATUS (status);
}


