
/******************************************************************************
 *
 * Module Name: exresop - AML Interpreter operand/object resolution
 *              $Revision: 41 $
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
	 MODULE_NAME         ("exresop")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_check_object_type
 *
 * PARAMETERS:  Type_needed         Object type needed
 *              This_type           Actual object type
 *              Object              Object pointer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check required type against actual type
 *
 ******************************************************************************/

acpi_status
acpi_ex_check_object_type (
	acpi_object_type        type_needed,
	acpi_object_type        this_type,
	void                    *object)
{
	PROC_NAME ("Ex_check_object_type");


	if (type_needed == ACPI_TYPE_ANY) {
		/* All types OK, so we don't perform any typechecks */

		return (AE_OK);
	}

	if (type_needed != this_type) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Needed [%s], found [%s] %p\n",
			acpi_ut_get_type_name (type_needed),
			acpi_ut_get_type_name (this_type), object));

		return (AE_AML_OPERAND_TYPE);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_resolve_operands
 *
 * PARAMETERS:  Opcode              Opcode being interpreted
 *              Stack_ptr           Top of operand stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert stack entries to required types
 *
 *      Each nibble in Arg_types represents one required operand
 *      and indicates the required Type:
 *
 *      The corresponding stack entry will be converted to the
 *      required type if possible, else return an exception
 *
 ******************************************************************************/

acpi_status
acpi_ex_resolve_operands (
	u16                     opcode,
	acpi_operand_object     **stack_ptr,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status = AE_OK;
	u8                      object_type;
	void                    *temp_node;
	u32                     arg_types;
	const acpi_opcode_info  *op_info;
	u32                     this_arg_type;
	acpi_object_type        type_needed;


	FUNCTION_TRACE_U32 ("Ex_resolve_operands", opcode);


	op_info = acpi_ps_get_opcode_info (opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		return_ACPI_STATUS (AE_AML_BAD_OPCODE);
	}


	arg_types = op_info->runtime_args;
	if (arg_types == ARGI_INVALID_OPCODE) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - %X is not a valid AML opcode\n",
			opcode));

		return_ACPI_STATUS (AE_AML_INTERNAL);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Opcode %X Operand_types=%X \n",
		opcode, arg_types));


	/*
	 * Normal exit is with (Arg_types == 0) at end of argument list.
	 * Function will return an exception from within the loop upon
	 * finding an entry which is not (or cannot be converted
	 * to) the required type; if stack underflows; or upon
	 * finding a NULL stack entry (which should not happen).
	 */
	while (GET_CURRENT_ARG_TYPE (arg_types)) {
		if (!stack_ptr || !*stack_ptr) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Internal - null stack entry at %p\n",
				stack_ptr));

			return_ACPI_STATUS (AE_AML_INTERNAL);
		}

		/* Extract useful items */

		obj_desc = *stack_ptr;

		/* Decode the descriptor type */

		if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED)) {
			/* Node */

			object_type = ((acpi_namespace_node *) obj_desc)->type;
		}

		else if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_INTERNAL)) {
			/* ACPI internal object */

			object_type = obj_desc->common.type;

			/* Check for bad acpi_object_type */

			if (!acpi_ex_validate_object_type (object_type)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Bad operand object type [%X]\n",
					object_type));

				return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
			}

			if (object_type == (u8) INTERNAL_TYPE_REFERENCE) {
				/*
				 * Decode the Reference
				 */
				op_info = acpi_ps_get_opcode_info (opcode);
				if (op_info->class == AML_CLASS_UNKNOWN) {
					return_ACPI_STATUS (AE_AML_BAD_OPCODE);
				}


				switch (obj_desc->reference.opcode) {
				case AML_ZERO_OP:
				case AML_ONE_OP:
				case AML_ONES_OP:
				case AML_DEBUG_OP:
				case AML_NAME_OP:
				case AML_INDEX_OP:
				case AML_ARG_OP:
				case AML_LOCAL_OP:
				case AML_REVISION_OP:

					DEBUG_ONLY_MEMBERS (ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
						"Reference Opcode: %s\n", op_info->name)));
					break;

				default:
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
						"Reference Opcode: Unknown [%02x]\n",
						obj_desc->reference.opcode));

					return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
					break;
				}
			}
		}

		else {
			/* Invalid descriptor */

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Bad descriptor type %X in Obj %p\n",
				obj_desc->common.data_type, obj_desc));

			return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
		}


		/*
		 * Get one argument type, point to the next
		 */
		this_arg_type = GET_CURRENT_ARG_TYPE (arg_types);
		INCREMENT_ARG_LIST (arg_types);


		/*
		 * Handle cases where the object does not need to be
		 * resolved to a value
		 */
		switch (this_arg_type) {

		case ARGI_REFERENCE:            /* References */
		case ARGI_INTEGER_REF:
		case ARGI_OBJECT_REF:
		case ARGI_DEVICE_REF:
		case ARGI_TARGETREF:            /* TBD: must implement implicit conversion rules before store */
		case ARGI_FIXED_TARGET:         /* No implicit conversion before store to target */
		case ARGI_SIMPLE_TARGET:        /* Name, Local, or Arg - no implicit conversion */

			/* Need an operand of type INTERNAL_TYPE_REFERENCE */

			if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED))            /* direct name ptr OK as-is */ {
				goto next_operand;
			}

			status = acpi_ex_check_object_type (INTERNAL_TYPE_REFERENCE,
					  object_type, obj_desc);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}


			if (AML_NAME_OP == obj_desc->reference.opcode) {
				/*
				 * Convert an indirect name ptr to direct name ptr and put
				 * it on the stack
				 */
				temp_node = obj_desc->reference.object;
				acpi_ut_remove_reference (obj_desc);
				(*stack_ptr) = temp_node;
			}

			goto next_operand;
			break;


		case ARGI_ANYTYPE:

			/*
			 * We don't want to resolve Index_op reference objects during
			 * a store because this would be an implicit De_ref_of operation.
			 * Instead, we just want to store the reference object.
			 * -- All others must be resolved below.
			 */
			if ((opcode == AML_STORE_OP) &&
				((*stack_ptr)->common.type == INTERNAL_TYPE_REFERENCE) &&
				((*stack_ptr)->reference.opcode == AML_INDEX_OP)) {
				goto next_operand;
			}
			break;
		}


		/*
		 * Resolve this object to a value
		 */
		status = acpi_ex_resolve_to_value (stack_ptr, walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}


		/*
		 * Check the resulting object (value) type
		 */
		switch (this_arg_type) {
		/*
		 * For the simple cases, only one type of resolved object
		 * is allowed
		 */
		case ARGI_MUTEX:

			/* Need an operand of type ACPI_TYPE_MUTEX */

			type_needed = ACPI_TYPE_MUTEX;
			break;

		case ARGI_EVENT:

			/* Need an operand of type ACPI_TYPE_EVENT */

			type_needed = ACPI_TYPE_EVENT;
			break;

		case ARGI_REGION:

			/* Need an operand of type ACPI_TYPE_REGION */

			type_needed = ACPI_TYPE_REGION;
			break;

		case ARGI_IF:   /* If */

			/* Need an operand of type INTERNAL_TYPE_IF */

			type_needed = INTERNAL_TYPE_IF;
			break;

		case ARGI_PACKAGE:   /* Package */

			/* Need an operand of type ACPI_TYPE_PACKAGE */

			type_needed = ACPI_TYPE_PACKAGE;
			break;

		case ARGI_ANYTYPE:

			/* Any operand type will do */

			type_needed = ACPI_TYPE_ANY;
			break;


		/*
		 * The more complex cases allow multiple resolved object types
		 */
		case ARGI_INTEGER:   /* Number */

			/*
			 * Need an operand of type ACPI_TYPE_INTEGER,
			 * But we can implicitly convert from a STRING or BUFFER
			 */
			status = acpi_ex_convert_to_integer (*stack_ptr, stack_ptr, walk_state);
			if (ACPI_FAILURE (status)) {
				if (status == AE_TYPE) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
						"Needed [Integer/String/Buffer], found [%s] %p\n",
						acpi_ut_get_type_name ((*stack_ptr)->common.type), *stack_ptr));

					return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
				}

				return_ACPI_STATUS (status);
			}

			goto next_operand;
			break;


		case ARGI_BUFFER:

			/*
			 * Need an operand of type ACPI_TYPE_BUFFER,
			 * But we can implicitly convert from a STRING or INTEGER
			 */
			status = acpi_ex_convert_to_buffer (*stack_ptr, stack_ptr, walk_state);
			if (ACPI_FAILURE (status)) {
				if (status == AE_TYPE) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
						"Needed [Integer/String/Buffer], found [%s] %p\n",
						acpi_ut_get_type_name ((*stack_ptr)->common.type), *stack_ptr));

					return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
				}

				return_ACPI_STATUS (status);
			}

			goto next_operand;
			break;


		case ARGI_STRING:

			/*
			 * Need an operand of type ACPI_TYPE_STRING,
			 * But we can implicitly convert from a BUFFER or INTEGER
			 */
			status = acpi_ex_convert_to_string (*stack_ptr, stack_ptr, 16, ACPI_UINT32_MAX, walk_state);
			if (ACPI_FAILURE (status)) {
				if (status == AE_TYPE) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
						"Needed [Integer/String/Buffer], found [%s] %p\n",
						acpi_ut_get_type_name ((*stack_ptr)->common.type), *stack_ptr));

					return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
				}

				return_ACPI_STATUS (status);
			}

			goto next_operand;
			break;


		case ARGI_COMPUTEDATA:

			/* Need an operand of type INTEGER, STRING or BUFFER */

			if ((ACPI_TYPE_INTEGER != (*stack_ptr)->common.type) &&
				(ACPI_TYPE_STRING != (*stack_ptr)->common.type) &&
				(ACPI_TYPE_BUFFER != (*stack_ptr)->common.type)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
					"Needed [Integer/String/Buffer], found [%s] %p\n",
					acpi_ut_get_type_name ((*stack_ptr)->common.type), *stack_ptr));

				return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
			}
			goto next_operand;
			break;


		case ARGI_DATAOBJECT:
			/*
			 * ARGI_DATAOBJECT is only used by the Size_of operator.
			 *
			 * The ACPI specification allows Size_of to return the size of
			 *  a Buffer, String or Package.  However, the MS ACPI.SYS AML
			 *  Interpreter also allows an Node reference to return without
			 *  error with a size of 4.
			 */

			/* Need a buffer, string, package or Node reference */

			if (((*stack_ptr)->common.type != ACPI_TYPE_BUFFER) &&
				((*stack_ptr)->common.type != ACPI_TYPE_STRING) &&
				((*stack_ptr)->common.type != ACPI_TYPE_PACKAGE) &&
				((*stack_ptr)->common.type != INTERNAL_TYPE_REFERENCE)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
					"Needed [Buf/Str/Pkg/Ref], found [%s] %p\n",
					acpi_ut_get_type_name ((*stack_ptr)->common.type), *stack_ptr));

				return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
			}

			/*
			 * If this is a reference, only allow a reference to an Node.
			 */
			if ((*stack_ptr)->common.type == INTERNAL_TYPE_REFERENCE) {
				if (!(*stack_ptr)->reference.node) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
						"Needed [Node Reference], found [%p]\n",
						*stack_ptr));

					return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
				}
			}
			goto next_operand;
			break;


		case ARGI_COMPLEXOBJ:

			/* Need a buffer or package or (ACPI 2.0) String */

			if (((*stack_ptr)->common.type != ACPI_TYPE_BUFFER) &&
				((*stack_ptr)->common.type != ACPI_TYPE_STRING) &&
				((*stack_ptr)->common.type != ACPI_TYPE_PACKAGE)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
					"Needed [Buf/Pkg], found [%s] %p\n",
					acpi_ut_get_type_name ((*stack_ptr)->common.type), *stack_ptr));

				return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
			}
			goto next_operand;
			break;


		default:

			/* Unknown type */

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Internal - Unknown ARGI type %X\n",
				this_arg_type));

			return_ACPI_STATUS (AE_BAD_PARAMETER);
		}


		/*
		 * Make sure that the original object was resolved to the
		 * required object type (Simple cases only).
		 */
		status = acpi_ex_check_object_type (type_needed,
				  (*stack_ptr)->common.type, *stack_ptr);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}


next_operand:
		/*
		 * If more operands needed, decrement Stack_ptr to point
		 * to next operand on stack
		 */
		if (GET_CURRENT_ARG_TYPE (arg_types)) {
			stack_ptr--;
		}

	}   /* while (*Types) */


	return_ACPI_STATUS (status);
}


