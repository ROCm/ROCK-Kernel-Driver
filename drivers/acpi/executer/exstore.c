
/******************************************************************************
 *
 * Module Name: exstore - AML Interpreter object store support
 *              $Revision: 168 $
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
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exstore")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store
 *
 * PARAMETERS:  *Source_desc        - Value to be stored
 *              *Dest_desc          - Where to store it.  Must be an NS node
 *                                    or an acpi_operand_object of type
 *                                    Reference;
 *              Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value described by Source_desc into the location
 *              described by Dest_desc. Called by various interpreter
 *              functions to store the result of an operation into
 *              the destination operand -- not just simply the actual "Store"
 *              ASL operator.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *ref_desc = dest_desc;


	ACPI_FUNCTION_TRACE_PTR ("Ex_store", dest_desc);


	/* Validate parameters */

	if (!source_desc || !dest_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null parameter\n"));
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	/* Dest_desc can be either a namespace node or an ACPI object */

	if (ACPI_GET_DESCRIPTOR_TYPE (dest_desc) == ACPI_DESC_TYPE_NAMED) {
		/*
		 * Dest is a namespace node,
		 * Storing an object into a Name "container"
		 */
		status = acpi_ex_store_object_to_node (source_desc,
				 (acpi_namespace_node *) dest_desc, walk_state);

		return_ACPI_STATUS (status);
	}

	/* Destination object must be a Reference or a Constant object */

	switch (ACPI_GET_OBJECT_TYPE (dest_desc)) {
	case INTERNAL_TYPE_REFERENCE:
		break;

	case ACPI_TYPE_INTEGER:

		/* Allow stores to Constants -- a Noop as per ACPI spec */

		if (dest_desc->common.flags & AOPOBJ_AML_CONSTANT) {
			return_ACPI_STATUS (AE_OK);
		}

		/*lint: -fallthrough */

	default:

		/* Destination is not an Reference */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Destination is not a Reference or Constant object [%p]\n", dest_desc));

		ACPI_DUMP_STACK_ENTRY (source_desc);
		ACPI_DUMP_STACK_ENTRY (dest_desc);
		ACPI_DUMP_OPERANDS (&dest_desc, ACPI_IMODE_EXECUTE, "Ex_store",
				  2, "Target is not a Reference or Constant object");

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}

	/*
	 * Examine the Reference opcode.  These cases are handled:
	 *
	 * 1) Store to Name (Change the object associated with a name)
	 * 2) Store to an indexed area of a Buffer or Package
	 * 3) Store to a Method Local or Arg
	 * 4) Store to the debug object
	 */
	switch (ref_desc->reference.opcode) {
	case AML_NAME_OP:
	case AML_REF_OF_OP:

		/* Storing an object into a Name "container" */

		status = acpi_ex_store_object_to_node (source_desc, ref_desc->reference.object,
				  walk_state);
		break;


	case AML_INDEX_OP:

		/* Storing to an Index (pointer into a packager or buffer) */

		status = acpi_ex_store_object_to_index (source_desc, ref_desc, walk_state);
		break;


	case AML_LOCAL_OP:
	case AML_ARG_OP:

		/* Store to a method local/arg  */

		status = acpi_ds_store_object_to_local (ref_desc->reference.opcode,
				  ref_desc->reference.offset, source_desc, walk_state);
		break;


	case AML_DEBUG_OP:

		/*
		 * Storing to the Debug object causes the value stored to be
		 * displayed and otherwise has no effect -- see ACPI Specification
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "**** Write to Debug Object: ****:\n\n"));

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "[ACPI Debug] %s: ",
				  acpi_ut_get_object_type_name (source_desc)));

		switch (ACPI_GET_OBJECT_TYPE (source_desc)) {
		case ACPI_TYPE_INTEGER:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "%8.8X%8.8X\n",
					ACPI_HIWORD (source_desc->integer.value),
					ACPI_LOWORD (source_desc->integer.value)));
			break;


		case ACPI_TYPE_BUFFER:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "Length %.2X\n",
					(u32) source_desc->buffer.length));
			break;


		case ACPI_TYPE_STRING:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "%s\n", source_desc->string.pointer));
			break;


		case ACPI_TYPE_PACKAGE:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "Elements Ptr - %p\n",
					source_desc->package.elements));
			break;


		default:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "Type %s %p\n",
					acpi_ut_get_object_type_name (source_desc), source_desc));
			break;
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_EXEC, "\n"));
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Reference opcode %X\n",
			ref_desc->reference.opcode));
		ACPI_DUMP_ENTRY (ref_desc, ACPI_LV_ERROR);

		status = AE_AML_INTERNAL;
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store_object_to_index
 *
 * PARAMETERS:  *Source_desc            - Value to be stored
 *              *Dest_desc              - Named object to receive the value
 *              Walk_state              - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to indexed Buffer or Package element
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_index (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *index_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;
	acpi_operand_object     *new_desc;
	u8                      value = 0;


	ACPI_FUNCTION_TRACE ("Ex_store_object_to_index");


	/*
	 * Destination must be a reference pointer, and
	 * must point to either a buffer or a package
	 */
	switch (index_desc->reference.target_type) {
	case ACPI_TYPE_PACKAGE:
		/*
		 * Storing to a package element is not simple.  The source must be
		 * evaluated and converted to the type of the destination and then the
		 * source is copied into the destination - we can't just point to the
		 * source object.
		 */
		/*
		 * The object at *(Index_desc->Reference.Where) is the
		 * element within the package that is to be modified.
		 */
		obj_desc = *(index_desc->reference.where);

		/* Do the conversion/store */

		status = acpi_ex_store_object_to_object (source_desc, obj_desc, &new_desc,
				  walk_state);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not store object to indexed package element\n"));
			return_ACPI_STATUS (status);
		}

		/*
		 * If a new object was created, we must install it as the new
		 * package element
		 */
		if (new_desc != obj_desc) {
			acpi_ut_remove_reference (obj_desc);
			*(index_desc->reference.where) = new_desc;
		}
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		/*
		 * Store into a Buffer (not actually a real Buffer_field) at a
		 * location defined by an Index.
		 *
		 * The first 8-bit element of the source object is written to the
		 * 8-bit Buffer location defined by the Index destination object,
		 * according to the ACPI 2.0 specification.
		 */

		/*
		 * Make sure the target is a Buffer
		 */
		obj_desc = index_desc->reference.object;
		if (ACPI_GET_OBJECT_TYPE (obj_desc) != ACPI_TYPE_BUFFER) {
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/*
		 * The assignment of the individual elements will be slightly
		 * different for each source type.
		 */
		switch (ACPI_GET_OBJECT_TYPE (source_desc)) {
		case ACPI_TYPE_INTEGER:

			/* Use the least-significant byte of the integer */

			value = (u8) (source_desc->integer.value);
			break;

		case ACPI_TYPE_BUFFER:

			value = source_desc->buffer.pointer[0];
			break;

		case ACPI_TYPE_STRING:

			value = (u8) source_desc->string.pointer[0];
			break;

		default:

			/* All other types are invalid */

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Source must be Integer/Buffer/String type, not %s\n",
				acpi_ut_get_object_type_name (source_desc)));
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/* Store the source value into the target buffer byte */

		obj_desc->buffer.pointer[index_desc->reference.offset] = value;
		break;


	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Target is not a Package or Buffer_field\n"));
		status = AE_AML_OPERAND_TYPE;
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store_object_to_node
 *
 * PARAMETERS:  Source_desc             - Value to be stored
 *              Node                    - Named object to receive the value
 *              Walk_state              - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 *              The Assignment of an object to a named object is handled here
 *              The value passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              Assumes parameters are already validated.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_node (
	acpi_operand_object     *source_desc,
	acpi_namespace_node     *node,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *target_desc;
	acpi_operand_object     *new_desc;
	acpi_object_type        target_type;


	ACPI_FUNCTION_TRACE_PTR ("Ex_store_object_to_node", source_desc);


	/*
	 * Get current type of the node, and object attached to Node
	 */
	target_type = acpi_ns_get_type (node);
	target_desc = acpi_ns_get_attached_object (node);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Storing %p(%s) into node %p(%s)\n",
		source_desc, acpi_ut_get_object_type_name (source_desc),
			  node, acpi_ut_get_type_name (target_type)));

	/*
	 * Resolve the source object to an actual value
	 * (If it is a reference object)
	 */
	status = acpi_ex_resolve_object (&source_desc, target_type, walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Do the actual store operation
	 */
	switch (target_type) {
	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:

		/*
		 * For fields, copy the source data to the target field.
		 */
		status = acpi_ex_write_data_to_field (source_desc, target_desc);
		break;


	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:

		/*
		 * These target types are all of type Integer/String/Buffer, and
		 * therefore support implicit conversion before the store.
		 *
		 * Copy and/or convert the source object to a new target object
		 */
		status = acpi_ex_store_object_to_object (source_desc, target_desc, &new_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		if (new_desc != target_desc) {
			/*
			 * Store the new New_desc as the new value of the Name, and set
			 * the Name's type to that of the value being stored in it.
			 * Source_desc reference count is incremented by Attach_object.
			 */
			status = acpi_ns_attach_object (node, new_desc, target_type);

			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
				"Store %s into %s via Convert/Attach\n",
				acpi_ut_get_object_type_name (source_desc),
				acpi_ut_get_object_type_name (new_desc)));
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
			"Storing %s (%p) directly into node (%p), no implicit conversion\n",
			acpi_ut_get_object_type_name (source_desc), source_desc, node));

		/* No conversions for all other types.  Just attach the source object */

		status = acpi_ns_attach_object (node, source_desc, ACPI_GET_OBJECT_TYPE (source_desc));
		break;
	}

	return_ACPI_STATUS (status);
}


