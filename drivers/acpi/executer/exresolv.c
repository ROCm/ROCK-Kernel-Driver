
/******************************************************************************
 *
 * Module Name: exresolv - AML Interpreter object resolution
 *              $Revision: 101 $
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
#include "amlcode.h"
#include "acparser.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"
#include "acevents.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exresolv")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_get_buffer_field_value
 *
 * PARAMETERS:  *Obj_desc           - Pointer to a Buffer_field
 *              *Result_desc        - Pointer to an empty descriptor which will
 *                                    become an Integer with the field's value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value from a Buffer_field
 *
 ******************************************************************************/

acpi_status
acpi_ex_get_buffer_field_value (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *result_desc)
{
	acpi_status             status;
	u32                     mask;
	u8                      *location;


	FUNCTION_TRACE ("Ex_get_buffer_field_value");


	/*
	 * Parameter validation
	 */
	if (!obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null field pointer\n"));
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
		status = acpi_ds_get_buffer_field_arguments (obj_desc);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	if (!obj_desc->buffer_field.buffer_obj) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null container pointer\n"));
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}

	if (ACPI_TYPE_BUFFER != obj_desc->buffer_field.buffer_obj->common.type) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - container is not a Buffer\n"));
		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}

	if (!result_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null result pointer\n"));
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}


	/* Field location is (base of buffer) + (byte offset) */

	location = obj_desc->buffer_field.buffer_obj->buffer.pointer
			 + obj_desc->buffer_field.base_byte_offset;

	/*
	 * Construct Mask with as many 1 bits as the field width
	 *
	 * NOTE: Only the bottom 5 bits are valid for a shift operation, so
	 *  special care must be taken for any shift greater than 31 bits.
	 *
	 * TBD: [Unhandled] Fields greater than 32 bits will not work.
	 */
	if (obj_desc->buffer_field.bit_length < 32) {
		mask = ((u32) 1 << obj_desc->buffer_field.bit_length) - (u32) 1;
	}
	else {
		mask = ACPI_UINT32_MAX;
	}

	result_desc->integer.type = (u8) ACPI_TYPE_INTEGER;

	/* Get the 32 bit value at the location */

	MOVE_UNALIGNED32_TO_32 (&result_desc->integer.value, location);

	/*
	 * Shift the 32-bit word containing the field, and mask off the
	 * resulting value
	 */
	result_desc->integer.value =
		(result_desc->integer.value >> obj_desc->buffer_field.start_field_bit_offset) & mask;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"** Read from buffer %p byte %d bit %d width %d addr %p mask %08X val %8.8X%8.8X\n",
		obj_desc->buffer_field.buffer_obj->buffer.pointer,
		obj_desc->buffer_field.base_byte_offset,
		obj_desc->buffer_field.start_field_bit_offset,
		obj_desc->buffer_field.bit_length,
		location, mask,
		HIDWORD(result_desc->integer.value),
		LODWORD(result_desc->integer.value)));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_to_value
 *
 * PARAMETERS:  **Stack_ptr         - Points to entry on Obj_stack, which can
 *                                    be either an (acpi_operand_object *)
 *                                    or an acpi_handle.
 *              Walk_state          - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert Reference objects to values
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_to_value (
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ex_resolve_to_value", stack_ptr);


	if (!stack_ptr || !*stack_ptr) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null pointer\n"));
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}


	/*
	 * The entity pointed to by the Stack_ptr can be either
	 * 1) A valid acpi_operand_object, or
	 * 2) A acpi_namespace_node (Named_obj)
	 */
	if (VALID_DESCRIPTOR_TYPE (*stack_ptr, ACPI_DESC_TYPE_INTERNAL)) {
		status = acpi_ex_resolve_object_to_value (stack_ptr, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Object on the stack may have changed if Acpi_ex_resolve_object_to_value()
	 * was called (i.e., we can't use an _else_ here.)
	 */
	if (VALID_DESCRIPTOR_TYPE (*stack_ptr, ACPI_DESC_TYPE_NAMED)) {
		status = acpi_ex_resolve_node_to_value ((acpi_namespace_node **) stack_ptr,
				  walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Resolved object %p\n", *stack_ptr));
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_object_to_value
 *
 * PARAMETERS:  Stack_ptr       - Pointer to a stack location that contains a
 *                                ptr to an internal object.
 *              Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value from an internal object.  The Reference type
 *              uses the associated AML opcode to determine the value.
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_object_to_value (
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *stack_desc;
	void                    *temp_node;
	acpi_operand_object     *obj_desc;
	u16                     opcode;


	FUNCTION_TRACE ("Ex_resolve_object_to_value");


	stack_desc = *stack_ptr;

	/* This is an acpi_operand_object  */

	switch (stack_desc->common.type) {

	case INTERNAL_TYPE_REFERENCE:

		opcode = stack_desc->reference.opcode;

		switch (opcode) {

		case AML_NAME_OP:

			/*
			 * Convert indirect name ptr to a direct name ptr.
			 * Then, Acpi_ex_resolve_node_to_value can be used to get the value
			 */
			temp_node = stack_desc->reference.object;

			/* Delete the Reference Object */

			acpi_ut_remove_reference (stack_desc);

			/* Put direct name pointer onto stack and exit */

			(*stack_ptr) = temp_node;
			break;


		case AML_LOCAL_OP:
		case AML_ARG_OP:

			/*
			 * Get the local from the method's state info
			 * Note: this increments the local's object reference count
			 */
			status = acpi_ds_method_data_get_value (opcode,
					  stack_desc->reference.offset, walk_state, &obj_desc);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			/*
			 * Now we can delete the original Reference Object and
			 * replace it with the resolve value
			 */
			acpi_ut_remove_reference (stack_desc);
			*stack_ptr = obj_desc;

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "[Arg/Local %d] Value_obj is %p\n",
				stack_desc->reference.offset, obj_desc));
			break;


		/*
		 * For constants, we must change the reference/constant object
		 * to a real integer object
		 */
		case AML_ZERO_OP:
		case AML_ONE_OP:
		case AML_ONES_OP:
		case AML_REVISION_OP:

			/* Create a new integer object */

			obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
			if (!obj_desc) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			switch (opcode) {
			case AML_ZERO_OP:
				obj_desc->integer.value = 0;
				break;

			case AML_ONE_OP:
				obj_desc->integer.value = 1;
				break;

			case AML_ONES_OP:
				obj_desc->integer.value = ACPI_INTEGER_MAX;

				/* Truncate value if we are executing from a 32-bit ACPI table */

				acpi_ex_truncate_for32bit_table (obj_desc, walk_state);
				break;

			case AML_REVISION_OP:
				obj_desc->integer.value = ACPI_CA_SUPPORT_LEVEL;
				break;
			}

			/*
			 * Remove a reference from the original reference object
			 * and put the new object in its place
			 */
			acpi_ut_remove_reference (stack_desc);
			*stack_ptr = obj_desc;
			break;


		case AML_INDEX_OP:

			switch (stack_desc->reference.target_type) {
			case ACPI_TYPE_BUFFER_FIELD:

				/* Just return - leave the Reference on the stack */
				break;


			case ACPI_TYPE_PACKAGE:
				obj_desc = *stack_desc->reference.where;
				if (obj_desc) {
					/*
					 * Valid obj descriptor, copy pointer to return value
					 * (i.e., dereference the package index)
					 * Delete the ref object, increment the returned object
					 */
					acpi_ut_remove_reference (stack_desc);
					acpi_ut_add_reference (obj_desc);
					*stack_ptr = obj_desc;
				}

				else {
					/*
					 * A NULL object descriptor means an unitialized element of
					 * the package, can't dereference it
					 */
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Attempt to deref an Index to NULL pkg element Idx=%p\n",
						stack_desc));
					status = AE_AML_UNINITIALIZED_ELEMENT;
				}
				break;

			default:
				/* Invalid reference object */

				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Unknown Target_type %X in Index/Reference obj %p\n",
					stack_desc->reference.target_type, stack_desc));
				status = AE_AML_INTERNAL;
				break;
			}

			break;


		case AML_DEBUG_OP:

			/* Just leave the object as-is */
			break;


		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Reference object subtype %02X in %p\n",
				opcode, stack_desc));
			status = AE_AML_INTERNAL;
			break;

		}   /* switch (Opcode) */

		break; /* case INTERNAL_TYPE_REFERENCE */


	case ACPI_TYPE_BUFFER_FIELD:

		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_ANY);
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		status = acpi_ex_get_buffer_field_value (stack_desc, obj_desc);
		if (ACPI_FAILURE (status)) {
			acpi_ut_remove_reference (obj_desc);
			obj_desc = NULL;
		}

		*stack_ptr = (void *) obj_desc;
		break;


	case INTERNAL_TYPE_BANK_FIELD:

		obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_ANY);
		if (!obj_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* TBD: WRONG! */

		status = acpi_ex_get_buffer_field_value (stack_desc, obj_desc);
		if (ACPI_FAILURE (status)) {
			acpi_ut_remove_reference (obj_desc);
			obj_desc = NULL;
		}

		*stack_ptr = (void *) obj_desc;
		break;


	/* TBD: [Future] - may need to handle Index_field, and Def_field someday */

	default:

		break;

	}   /* switch (Stack_desc->Common.Type) */


	return_ACPI_STATUS (status);
}


