
/******************************************************************************
 *
 * Module Name: amstore - AML Interpreter object store support
 *              $Revision: 117 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
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


#define _COMPONENT          INTERPRETER
	 MODULE_NAME         ("amstore")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_exec_store
 *
 * PARAMETERS:  *Val_desc           - Value to be stored
 *              *Dest_desc          - Where to store it 0 Must be (ACPI_HANDLE)
 *                                    or an ACPI_OPERAND_OBJECT  of type
 *                                    Reference; if the latter the descriptor
 *                                    will be either reused or deleted.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value described by Val_desc into the location
 *              described by Dest_desc. Called by various interpreter
 *              functions to store the result of an operation into
 *              the destination operand.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_exec_store (
	ACPI_OPERAND_OBJECT     *val_desc,
	ACPI_OPERAND_OBJECT     *dest_desc,
	ACPI_WALK_STATE         *walk_state)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_OPERAND_OBJECT     *delete_dest_desc = NULL;
	ACPI_OPERAND_OBJECT     *tmp_desc;
	ACPI_NAMESPACE_NODE     *node = NULL;
	u8                      value = 0;
	u32                     length;
	u32                     i;


	/* Validate parameters */

	if (!val_desc || !dest_desc) {
		return (AE_AML_NO_OPERAND);
	}

	/* Examine the datatype of the Dest_desc */

	if (VALID_DESCRIPTOR_TYPE (dest_desc, ACPI_DESC_TYPE_NAMED)) {
		/* Dest is an ACPI_HANDLE, create a new object */

		node = (ACPI_NAMESPACE_NODE *) dest_desc;
		dest_desc = acpi_cm_create_internal_object (INTERNAL_TYPE_REFERENCE);
		if (!dest_desc) {
			/* Allocation failure  */

			return (AE_NO_MEMORY);
		}

		/* Build a new Reference wrapper around the handle */

		dest_desc->reference.op_code = AML_NAME_OP;
		dest_desc->reference.object = node;
	}


	/* Destination object must be of type Reference */

	if (dest_desc->common.type != INTERNAL_TYPE_REFERENCE) {
		/* Destination is not an Reference */

		return (AE_AML_OPERAND_TYPE);
	}

	/* Examine the Reference opcode */

	switch (dest_desc->reference.op_code)
	{

	case AML_NAME_OP:

		/*
		 *  Storing into a Name
		 */
		delete_dest_desc = dest_desc;
		status = acpi_aml_store_object_to_node (val_desc, dest_desc->reference.object,
				  walk_state);

		break;  /* Case Name_op */


	case AML_INDEX_OP:

		delete_dest_desc = dest_desc;

		/*
		 * Valid source value and destination reference pointer.
		 *
		 * ACPI Specification 1.0B section 15.2.3.4.2.13:
		 * Destination should point to either a buffer or a package
		 */

		/*
		 * Actually, storing to a package is not so simple.  The source must be
		 * evaluated and converted to the type of the destination and then the
		 * source is copied into the destination - we can't just point to the
		 * source object.
		 */
		if (dest_desc->reference.target_type == ACPI_TYPE_PACKAGE) {
			/*
			 * The object at *(Dest_desc->Reference.Where) is the
			 *  element within the package that is to be modified.
			 */
			tmp_desc = *(dest_desc->reference.where);
			if (tmp_desc) {
				/*
				 * If the Destination element is a package, we will delete
				 *  that object and construct a new one.
				 *
				 * TBD: [Investigate] Should both the src and dest be required
				 *      to be packages?
				 *       && (Val_desc->Common.Type == ACPI_TYPE_PACKAGE)
				 */
				if (tmp_desc->common.type == ACPI_TYPE_PACKAGE) {
					/*
					 * Take away the reference for being part of a package and
					 * delete
					 */
					acpi_cm_remove_reference (tmp_desc);
					acpi_cm_remove_reference (tmp_desc);

					tmp_desc = NULL;
				}
			}

			if (!tmp_desc) {
				/*
				 * If the Tmp_desc is NULL, that means an uninitialized package
				 * has been used as a destination, therefore, we must create
				 * the destination element to match the type of the source
				 * element NOTE: Val_desc can be of any type.
				 */
				tmp_desc = acpi_cm_create_internal_object (val_desc->common.type);
				if (!tmp_desc) {
					status = AE_NO_MEMORY;
					goto cleanup;
				}

				/*
				 * If the source is a package, copy the source to the new dest
				 */
				if (ACPI_TYPE_PACKAGE == tmp_desc->common.type) {
					status = acpi_aml_build_copy_internal_package_object (
							 val_desc, tmp_desc, walk_state);
					if (ACPI_FAILURE (status)) {
						acpi_cm_remove_reference (tmp_desc);
						tmp_desc = NULL;
						goto cleanup;
					}
				}

				/*
				 * Install the new descriptor into the package and add a
				 * reference to the newly created descriptor for now being
				 * part of the parent package
				 */

				*(dest_desc->reference.where) = tmp_desc;
				acpi_cm_add_reference (tmp_desc);
			}

			if (ACPI_TYPE_PACKAGE != tmp_desc->common.type) {
				/*
				 * The destination element is not a package, so we need to
				 * convert the contents of the source (Val_desc) and copy into
				 * the destination (Tmp_desc)
				 */
				status = acpi_aml_store_object_to_object (val_desc, tmp_desc,
						  walk_state);
				if (ACPI_FAILURE (status)) {
					/*
					 * An error occurrered when copying the internal object
					 * so delete the reference.
					 */
					status = AE_AML_OPERAND_TYPE;
				}
			}

			break;
		}

		/*
		 * Check that the destination is a Buffer Field type
		 */
		if (dest_desc->reference.target_type != ACPI_TYPE_BUFFER_FIELD) {
			status = AE_AML_OPERAND_TYPE;
			break;
		}

		/*
		 * Storing into a buffer at a location defined by an Index.
		 *
		 * Each 8-bit element of the source object is written to the
		 * 8-bit Buffer Field of the Index destination object.
		 */

		/*
		 * Set the Tmp_desc to the destination object and type check.
		 */
		tmp_desc = dest_desc->reference.object;

		if (tmp_desc->common.type != ACPI_TYPE_BUFFER) {
			status = AE_AML_OPERAND_TYPE;
			break;
		}

		/*
		 * The assignment of the individual elements will be slightly
		 * different for each source type.
		 */

		switch (val_desc->common.type)
		{
		/*
		 * If the type is Integer, the Length is 4.
		 * This loop to assign each of the elements is somewhat
		 *  backward because of the Big Endian-ness of IA-64
		 */
		case ACPI_TYPE_NUMBER:
			length = 4;
			for (i = length; i != 0; i--) {
				value = (u8)(val_desc->number.value >> (MUL_8 (i - 1)));
				tmp_desc->buffer.pointer[dest_desc->reference.offset] = value;
			}
			break;

		/*
		 * If the type is Buffer, the Length is in the structure.
		 * Just loop through the elements and assign each one in turn.
		 */
		case ACPI_TYPE_BUFFER:
			length = val_desc->buffer.length;
			for (i = 0; i < length; i++) {
				value = *(val_desc->buffer.pointer + i);
				tmp_desc->buffer.pointer[dest_desc->reference.offset] = value;
			}
			break;

		/*
		 * If the type is String, the Length is in the structure.
		 * Just loop through the elements and assign each one in turn.
		 */
		case ACPI_TYPE_STRING:
			length = val_desc->string.length;
			for (i = 0; i < length; i++) {
				value = *(val_desc->string.pointer + i);
				tmp_desc->buffer.pointer[dest_desc->reference.offset] = value;
			}
			break;

		/*
		 * If source is not a valid type so return an error.
		 */
		default:
			status = AE_AML_OPERAND_TYPE;
			break;
		}

		/*
		 * If we had an error, break out of this case statement.
		 */
		if (ACPI_FAILURE (status)) {
			break;
		}

		/*
		 * Set the return pointer
		 */
		dest_desc = tmp_desc;

		break;

	case AML_ZERO_OP:
	case AML_ONE_OP:
	case AML_ONES_OP:

		/*
		 * Storing to a constant is a no-op -- see ACPI Specification
		 * Delete the result descriptor.
		 */

		delete_dest_desc = dest_desc;
		break;


	case AML_LOCAL_OP:

		status = acpi_ds_method_data_set_value (MTH_TYPE_LOCAL,
				  (dest_desc->reference.offset), val_desc, walk_state);
		delete_dest_desc = dest_desc;
		break;


	case AML_ARG_OP:

		status = acpi_ds_method_data_set_value (MTH_TYPE_ARG,
				  (dest_desc->reference.offset), val_desc, walk_state);
		delete_dest_desc = dest_desc;
		break;


	case AML_DEBUG_OP:

		/*
		 * Storing to the Debug object causes the value stored to be
		 * displayed and otherwise has no effect -- see ACPI Specification
		 */

		delete_dest_desc = dest_desc;
		break;


	default:

		/* TBD: [Restructure] use object dump routine !! */

		delete_dest_desc = dest_desc;
		status = AE_AML_INTERNAL;

	}   /* switch(Dest_desc->Reference.Op_code) */


cleanup:

	/* Cleanup and exit*/

	if (delete_dest_desc) {
		acpi_cm_remove_reference (delete_dest_desc);
	}

	return (status);
}


