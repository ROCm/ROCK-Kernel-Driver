
/******************************************************************************
 *
 * Module Name: exstore - AML Interpreter object store support
 *              $Revision: 150 $
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
	 MODULE_NAME         ("exstore")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store
 *
 * PARAMETERS:  *Source_desc        - Value to be stored
 *              *Dest_desc          - Where to store it.  Must be an NS node
 *                                    or an acpi_operand_object of type
 *                                    Reference;
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value described by Source_desc into the location
 *              described by Dest_desc. Called by various interpreter
 *              functions to store the result of an operation into
 *              the destination operand.
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


	FUNCTION_TRACE_PTR ("Ex_store", dest_desc);


	/* Validate parameters */

	if (!source_desc || !dest_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null pointer\n"));
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	/* Dest_desc can be either a namespace node or an ACPI object */

	if (VALID_DESCRIPTOR_TYPE (dest_desc, ACPI_DESC_TYPE_NAMED)) {
		/*
		 * Dest is a namespace node,
		 * Storing an object into a Name "container"
		 */
		status = acpi_ex_store_object_to_node (source_desc,
				 (acpi_namespace_node *) dest_desc, walk_state);

		/* All done, that's it */

		return_ACPI_STATUS (status);
	}


	/* Destination object must be an object of type Reference */

	if (dest_desc->common.type != INTERNAL_TYPE_REFERENCE) {
		/* Destination is not an Reference */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Destination is not a Reference_obj [%p]\n", dest_desc));

		DUMP_STACK_ENTRY (source_desc);
		DUMP_STACK_ENTRY (dest_desc);
		DUMP_OPERANDS (&dest_desc, IMODE_EXECUTE, "Ex_store",
				  2, "Target is not a Reference_obj");

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}


	/*
	 * Examine the Reference opcode.  These cases are handled:
	 *
	 * 1) Store to Name (Change the object associated with a name)
	 * 2) Store to an indexed area of a Buffer or Package
	 * 3) Store to a Method Local or Arg
	 * 4) Store to the debug object
	 * 5) Store to a constant -- a noop
	 */
	switch (ref_desc->reference.opcode) {

	case AML_NAME_OP:

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
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "**** Write to Debug Object: ****:\n\n"));

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "[ACPI Debug] %s: ",
				  acpi_ut_get_type_name (source_desc->common.type)));

		switch (source_desc->common.type) {
		case ACPI_TYPE_INTEGER:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "0x%X (%d)\n",
				(u32) source_desc->integer.value, (u32) source_desc->integer.value));
			break;


		case ACPI_TYPE_BUFFER:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "Length 0x%X\n",
				(u32) source_desc->buffer.length));
			break;


		case ACPI_TYPE_STRING:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "%s\n", source_desc->string.pointer));
			break;


		case ACPI_TYPE_PACKAGE:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "Elements - 0x%X\n",
				(u32) source_desc->package.elements));
			break;


		default:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_DEBUG_OBJECT, "@0x%p\n", source_desc));
			break;
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));
		break;


	case AML_ZERO_OP:
	case AML_ONE_OP:
	case AML_ONES_OP:
	case AML_REVISION_OP:

		/*
		 * Storing to a constant is a no-op -- see ACPI Specification
		 * Delete the reference descriptor, however
		 */
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - Unknown Reference subtype %02x\n",
			ref_desc->reference.opcode));

		/* TBD: [Restructure] use object dump routine !! */

		DUMP_BUFFER (ref_desc, sizeof (acpi_operand_object));

		status = AE_AML_INTERNAL;
		break;

	}   /* switch (Ref_desc->Reference.Opcode) */


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store_object_to_index
 *
 * PARAMETERS:  *Source_desc          - Value to be stored
 *              *Node               - Named object to receive the value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_index (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;
	u32                     length;
	u32                     i;
	u8                      value = 0;


	FUNCTION_TRACE ("Ex_store_object_to_index");


	/*
	 * Destination must be a reference pointer, and
	 * must point to either a buffer or a package
	 */
	switch (dest_desc->reference.target_type) {
	case ACPI_TYPE_PACKAGE:
		/*
		 * Storing to a package element is not simple.  The source must be
		 * evaluated and converted to the type of the destination and then the
		 * source is copied into the destination - we can't just point to the
		 * source object.
		 */
		if (dest_desc->reference.target_type == ACPI_TYPE_PACKAGE) {
			/*
			 * The object at *(Dest_desc->Reference.Where) is the
			 * element within the package that is to be modified.
			 */
			obj_desc = *(dest_desc->reference.where);
			if (obj_desc) {
				/*
				 * If the Destination element is a package, we will delete
				 *  that object and construct a new one.
				 *
				 * TBD: [Investigate] Should both the src and dest be required
				 *      to be packages?
				 *       && (Source_desc->Common.Type == ACPI_TYPE_PACKAGE)
				 */
				if (obj_desc->common.type == ACPI_TYPE_PACKAGE) {
					/* Take away the reference for being part of a package */

					acpi_ut_remove_reference (obj_desc);
					obj_desc = NULL;
				}
			}

			if (!obj_desc) {
				/*
				 * If the Obj_desc is NULL, it means that an uninitialized package
				 * element has been used as a destination (this is OK), therefore,
				 * we must create the destination element to match the type of the
				 * source element NOTE: Source_desccan be of any type.
				 */
				obj_desc = acpi_ut_create_internal_object (source_desc->common.type);
				if (!obj_desc) {
					return_ACPI_STATUS (AE_NO_MEMORY);
				}

				/*
				 * If the source is a package, copy the source to the new dest
				 */
				if (ACPI_TYPE_PACKAGE == obj_desc->common.type) {
					status = acpi_ut_copy_ipackage_to_ipackage (source_desc, obj_desc, walk_state);
					if (ACPI_FAILURE (status)) {
						acpi_ut_remove_reference (obj_desc);
						return_ACPI_STATUS (status);
					}
				}

				/* Install the new descriptor into the package */

				*(dest_desc->reference.where) = obj_desc;
			}

			if (ACPI_TYPE_PACKAGE != obj_desc->common.type) {
				/*
				 * The destination element is not a package, so we need to
				 * convert the contents of the source (Source_desc) and copy into
				 * the destination (Obj_desc)
				 */
				status = acpi_ex_store_object_to_object (source_desc, obj_desc,
						  walk_state);
				if (ACPI_FAILURE (status)) {
					/*
					 * An error occurrered when copying the internal object
					 * so delete the reference.
					 */
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Unable to copy the internal object\n"));
					return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
				}
			}
		}
		break;


	case ACPI_TYPE_BUFFER_FIELD:


		/* TBD: can probably call the generic Buffer/Field routines */

		/*
		 * Storing into a buffer at a location defined by an Index.
		 *
		 * Each 8-bit element of the source object is written to the
		 * 8-bit Buffer Field of the Index destination object.
		 */

		/*
		 * Set the Obj_desc to the destination object and type check.
		 */
		obj_desc = dest_desc->reference.object;
		if (obj_desc->common.type != ACPI_TYPE_BUFFER) {
			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}

		/*
		 * The assignment of the individual elements will be slightly
		 * different for each source type.
		 */
		switch (source_desc->common.type) {
		case ACPI_TYPE_INTEGER:
			/*
			 * Type is Integer, assign bytewise
			 * This loop to assign each of the elements is somewhat
			 * backward because of the Big Endian-ness of IA-64
			 */
			length = sizeof (acpi_integer);
			for (i = length; i != 0; i--) {
				value = (u8)(source_desc->integer.value >> (MUL_8 (i - 1)));
				obj_desc->buffer.pointer[dest_desc->reference.offset] = value;
			}
			break;


		case ACPI_TYPE_BUFFER:
			/*
			 * Type is Buffer, the Length is in the structure.
			 * Just loop through the elements and assign each one in turn.
			 */
			length = source_desc->buffer.length;
			for (i = 0; i < length; i++) {
				value = source_desc->buffer.pointer[i];
				obj_desc->buffer.pointer[dest_desc->reference.offset] = value;
			}
			break;


		case ACPI_TYPE_STRING:
			/*
			 * Type is String, the Length is in the structure.
			 * Just loop through the elements and assign each one in turn.
			 */
			length = source_desc->string.length;
			for (i = 0; i < length; i++) {
				value = source_desc->string.pointer[i];
				obj_desc->buffer.pointer[dest_desc->reference.offset] = value;
			}
			break;


		default:

			/* Other types are invalid */

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Source must be Number/Buffer/String type, not %X\n",
				source_desc->common.type));
			status = AE_AML_OPERAND_TYPE;
			break;
		}
		break;


	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Target is not a Package or Buffer_field\n"));
		status = AE_AML_OPERAND_TYPE;
		break;
	}


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store_object_to_node
 *
 * PARAMETERS:  *Source_desc           - Value to be stored
 *              *Node                  - Named object to receive the value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 *              The Assignment of an object to a named object is handled here
 *              The val passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              NOTE: the global lock is acquired early.  This will result
 *              in the global lock being held a bit longer.  Also, if the
 *              function fails during set up we may get the lock when we
 *              don't really need it.  I don't think we care.
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
	acpi_object_type8       target_type = ACPI_TYPE_ANY;


	FUNCTION_TRACE ("Ex_store_object_to_node");


	/*
	 * Assuming the parameters were already validated
	 */

	/*
	 * Get current type of the node, and object attached to Node
	 */
	target_type = acpi_ns_get_type (node);
	target_desc = acpi_ns_get_attached_object (node);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Storing %p(%s) into node %p(%s)\n",
		node, acpi_ut_get_type_name (source_desc->common.type),
		source_desc, acpi_ut_get_type_name (target_type)));


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
		status = acpi_ex_store_object (source_desc, target_type, &target_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Store the new Target_desc as the new value of the Name, and set
		 * the Name's type to that of the value being stored in it.
		 * Source_desc reference count is incremented by Attach_object.
		 */
		status = acpi_ns_attach_object (node, target_desc, target_type);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Store %s into %s via Convert/Attach\n",
			acpi_ut_get_type_name (target_desc->common.type),
			acpi_ut_get_type_name (target_type)));
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Storing %s (%p) directly into node (%p), no implicit conversion\n",
			acpi_ut_get_type_name (source_desc->common.type), source_desc, node));

		/* No conversions for all other types.  Just attach the source object */

		status = acpi_ns_attach_object (node, source_desc, source_desc->common.type);
		break;
	}


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_store_object_to_object
 *
 * PARAMETERS:  *Source_desc           - Value to be stored
 *              *Dest_desc          - Object to receive the value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store an object to another object.
 *
 *              The Assignment of an object to another (not named) object
 *              is handled here.
 *              The val passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              This module allows destination types of Number, String,
 *              and Buffer.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_object_to_object (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_object_type8       destination_type = dest_desc->common.type;


	FUNCTION_TRACE ("Ex_store_object_to_object");


	/*
	 *  Assuming the parameters are valid!
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Storing %p(%s) to %p(%s)\n",
			  source_desc, acpi_ut_get_type_name (source_desc->common.type),
			  dest_desc, acpi_ut_get_type_name (dest_desc->common.type)));


	/*
	 * From this interface, we only support Integers/Strings/Buffers
	 */
	switch (destination_type) {
	case ACPI_TYPE_INTEGER:
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:
		break;

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Store into %s not implemented\n",
			acpi_ut_get_type_name (dest_desc->common.type)));

		return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
	}


	/*
	 * Resolve the source object to an actual value
	 * (If it is a reference object)
	 */
	status = acpi_ex_resolve_object (&source_desc, destination_type, walk_state);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}


	/*
	 * Copy and/or convert the source object to the destination object
	 */
	status = acpi_ex_store_object (source_desc, destination_type, &dest_desc, walk_state);


	return_ACPI_STATUS (status);
}

