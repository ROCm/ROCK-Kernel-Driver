
/******************************************************************************
 *
 * Module Name: exoparg1 - AML execution - opcodes with 1 argument
 *              $Revision: 120 $
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


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exoparg1")


/*!
 * Naming convention for AML interpreter execution routines.
 *
 * The routines that begin execution of AML opcodes are named with a common
 * convention based upon the number of arguments, the number of target operands,
 * and whether or not a value is returned:
 *
 *      AcpiExOpcode_xA_yT_zR
 *
 * Where:
 *
 * xA - ARGUMENTS:    The number of arguments (input operands) that are
 *                    required for this opcode type (1 through 6 args).
 * yT - TARGETS:      The number of targets (output operands) that are required
 *                    for this opcode type (0, 1, or 2 targets).
 * zR - RETURN VALUE: Indicates whether this opcode type returns a value
 *                    as the function return (0 or 1).
 *
 * The AcpiExOpcode* functions are called via the Dispatcher component with
 * fully resolved operands.
!*/


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_1A_0T_0R
 *
 * PARAMETERS:  Walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute Type 1 monadic operator with numeric operand on
 *              object stack
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_1A_0T_0R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_STR ("Ex_opcode_1A_0T_0R", acpi_ps_get_opcode_name (walk_state->opcode));


	/* Examine the opcode */

	switch (walk_state->opcode) {
	case AML_RELEASE_OP:    /*  Release (Mutex_object) */

		status = acpi_ex_release_mutex (operand[0], walk_state);
		break;


	case AML_RESET_OP:      /*  Reset (Event_object) */

		status = acpi_ex_system_reset_event (operand[0]);
		break;


	case AML_SIGNAL_OP:     /*  Signal (Event_object) */

		status = acpi_ex_system_signal_event (operand[0]);
		break;


	case AML_SLEEP_OP:      /*  Sleep (Msec_time) */

		acpi_ex_system_do_suspend ((u32) operand[0]->integer.value);
		break;


	case AML_STALL_OP:      /*  Stall (Usec_time) */

		acpi_ex_system_do_stall ((u32) operand[0]->integer.value);
		break;


	case AML_UNLOAD_OP:     /*  Unload (Handle) */

		status = acpi_ex_unload_table (operand[0]);
		break;


	default:                /*  Unknown opcode  */

		REPORT_ERROR (("Acpi_ex_opcode_1A_0T_0R: Unknown opcode %X\n",
			walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_1A_1T_0R
 *
 * PARAMETERS:  Walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with one argument, one target, and no
 *              return value.
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_1A_1T_0R (
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     **operand = &walk_state->operands[0];


	FUNCTION_TRACE_STR ("Ex_opcode_1A_1T_0R", acpi_ps_get_opcode_name (walk_state->opcode));


	switch (walk_state->opcode) {

	case AML_LOAD_OP:

		status = acpi_ex_load_op (operand[0], operand[1]);
		break;

	default:                        /* Unknown opcode */

		REPORT_ERROR (("Acpi_ex_opcode_1A_1T_0R: Unknown opcode %X\n",
			walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}


cleanup:

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_1A_1T_1R
 *
 * PARAMETERS:  Walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with one argument, one target, and a
 *              return value.
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_1A_1T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *return_desc = NULL;
	acpi_operand_object     *return_desc2 = NULL;
	u32                     temp32;
	u32                     i;
	u32                     j;
	acpi_integer            digit;


	FUNCTION_TRACE_STR ("Ex_opcode_1A_1T_1R", acpi_ps_get_opcode_name (walk_state->opcode));


	/* Create a return object of type Integer for most opcodes */

	switch (walk_state->opcode) {
	case AML_BIT_NOT_OP:
	case AML_FIND_SET_LEFT_BIT_OP:
	case AML_FIND_SET_RIGHT_BIT_OP:
	case AML_FROM_BCD_OP:
	case AML_TO_BCD_OP:
	case AML_COND_REF_OF_OP:

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		break;
	}


	switch (walk_state->opcode) {

	case AML_BIT_NOT_OP:            /* Not (Operand, Result)  */

		return_desc->integer.value = ~operand[0]->integer.value;
		break;


	case AML_FIND_SET_LEFT_BIT_OP:  /* Find_set_left_bit (Operand, Result) */


		return_desc->integer.value = operand[0]->integer.value;

		/*
		 * Acpi specification describes Integer type as a little
		 * endian unsigned value, so this boundary condition is valid.
		 */
		for (temp32 = 0; return_desc->integer.value && temp32 < ACPI_INTEGER_BIT_SIZE; ++temp32) {
			return_desc->integer.value >>= 1;
		}

		return_desc->integer.value = temp32;
		break;


	case AML_FIND_SET_RIGHT_BIT_OP: /* Find_set_right_bit (Operand, Result) */


		return_desc->integer.value = operand[0]->integer.value;

		/*
		 * The Acpi specification describes Integer type as a little
		 * endian unsigned value, so this boundary condition is valid.
		 */
		for (temp32 = 0; return_desc->integer.value && temp32 < ACPI_INTEGER_BIT_SIZE; ++temp32) {
			return_desc->integer.value <<= 1;
		}

		/* Since the bit position is one-based, subtract from 33 (65) */

		return_desc->integer.value = temp32 == 0 ? 0 : (ACPI_INTEGER_BIT_SIZE + 1) - temp32;
		break;


	case AML_FROM_BCD_OP:           /* From_bcd (BCDValue, Result) */

		/*
		 * The 64-bit ACPI integer can hold 16 4-bit BCD integers
		 */
		return_desc->integer.value = 0;
		for (i = 0; i < ACPI_MAX_BCD_DIGITS; i++) {
			/* Get one BCD digit */

			digit = (acpi_integer) ((operand[0]->integer.value >> (i * 4)) & 0xF);

			/* Check the range of the digit */

			if (digit > 9) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "BCD digit too large: %d\n",
					(u32) digit));
				status = AE_AML_NUMERIC_OVERFLOW;
				goto cleanup;
			}

			if (digit > 0) {
				/* Sum into the result with the appropriate power of 10 */

				for (j = 0; j < i; j++) {
					digit *= 10;
				}

				return_desc->integer.value += digit;
			}
		}
		break;


	case AML_TO_BCD_OP:             /* To_bcd (Operand, Result) */

		if (operand[0]->integer.value > ACPI_MAX_BCD_VALUE) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "BCD overflow: %8.8X%8.8X\n",
				HIDWORD(operand[0]->integer.value), LODWORD(operand[0]->integer.value)));
			status = AE_AML_NUMERIC_OVERFLOW;
			goto cleanup;
		}

		return_desc->integer.value = 0;
		for (i = 0; i < ACPI_MAX_BCD_DIGITS; i++) {
			/* Divide by nth factor of 10 */

			temp32 = 0;
			digit = operand[0]->integer.value;
			for (j = 0; j < i; j++) {
				acpi_ut_short_divide (&digit, 10, &digit, &temp32);
			}

			/* Create the BCD digit from the remainder above */

			if (digit > 0) {
				return_desc->integer.value += (temp32 << (i * 4));
			}
		}
		break;


	case AML_COND_REF_OF_OP:        /* Cond_ref_of (Source_object, Result) */

		/*
		 * This op is a little strange because the internal return value is
		 * different than the return value stored in the result descriptor
		 * (There are really two return values)
		 */
		if ((acpi_namespace_node *) operand[0] == acpi_gbl_root_node) {
			/*
			 * This means that the object does not exist in the namespace,
			 * return FALSE
			 */
			return_desc->integer.value = 0;

			/*
			 * Must delete the result descriptor since there is no reference
			 * being returned
			 */
			acpi_ut_remove_reference (operand[1]);
			goto cleanup;
		}

		/* Get the object reference and store it */

		status = acpi_ex_get_object_reference (operand[0], &return_desc2, walk_state);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		status = acpi_ex_store (return_desc2, operand[1], walk_state);

		/* The object exists in the namespace, return TRUE */

		return_desc->integer.value = ACPI_INTEGER_MAX;
		goto cleanup;
		break;


	case AML_STORE_OP:              /* Store (Source, Target) */

		/*
		 * A store operand is typically a number, string, buffer or lvalue
		 * Be careful about deleting the source object,
		 * since the object itself may have been stored.
		 */
		status = acpi_ex_store (operand[0], operand[1], walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Normally, we would remove a reference on the Operand[0] parameter;
		 * But since it is being used as the internal return object
		 * (meaning we would normally increment it), the two cancel out,
		 * and we simply don't do anything.
		 */
		walk_state->result_obj = operand[0];
		walk_state->operands[0] = NULL; /* Prevent deletion */
		return_ACPI_STATUS (status);
		break;


	/*
	 * ACPI 2.0 Opcodes
	 */
	case AML_COPY_OP:               /* Copy (Source, Target) */

		status = AE_NOT_IMPLEMENTED;
		goto cleanup;
		break;


	case AML_TO_DECSTRING_OP:       /* To_decimal_string (Data, Result) */

		status = acpi_ex_convert_to_string (operand[0], &return_desc, 10, ACPI_UINT32_MAX, walk_state);
		break;


	case AML_TO_HEXSTRING_OP:       /* To_hex_string (Data, Result) */

		status = acpi_ex_convert_to_string (operand[0], &return_desc, 16, ACPI_UINT32_MAX, walk_state);
		break;


	case AML_TO_BUFFER_OP:          /* To_buffer (Data, Result) */

		status = acpi_ex_convert_to_buffer (operand[0], &return_desc, walk_state);
		break;


	case AML_TO_INTEGER_OP:         /* To_integer (Data, Result) */

		status = acpi_ex_convert_to_integer (operand[0], &return_desc, walk_state);
		break;


	/*
	 * These are two obsolete opcodes
	 */
	case AML_SHIFT_LEFT_BIT_OP:     /*  Shift_left_bit (Source, Bit_num) */
	case AML_SHIFT_RIGHT_BIT_OP:    /*  Shift_right_bit (Source, Bit_num) */


		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "%s is obsolete and not implemented\n",
				  acpi_ps_get_opcode_name (walk_state->opcode)));
		status = AE_SUPPORT;
		goto cleanup;
		break;


	default:                        /* Unknown opcode */

		REPORT_ERROR (("Acpi_ex_opcode_1A_1T_1R: Unknown opcode %X\n",
			walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}


	/*
	 * Store the return value computed above into the target object
	 */
	status = acpi_ex_store (return_desc, operand[1], walk_state);


cleanup:

	walk_state->result_obj = return_desc;

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (return_desc);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_1A_0T_1R
 *
 * PARAMETERS:  Walk_state          - Current state (contains AML opcode)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with one argument, no target, and a return value
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_1A_0T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *temp_desc;
	acpi_operand_object     *return_desc = NULL;
	acpi_status             status = AE_OK;
	u32                     type;
	acpi_integer            value;


	FUNCTION_TRACE_STR ("Ex_opcode_1A_0T_0R", acpi_ps_get_opcode_name (walk_state->opcode));


	/* Get the operand and decode the opcode */

	switch (walk_state->opcode) {

	case AML_LNOT_OP:               /* LNot (Operand) */

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		return_desc->integer.value = !operand[0]->integer.value;
		break;


	case AML_DECREMENT_OP:          /* Decrement (Operand)  */
	case AML_INCREMENT_OP:          /* Increment (Operand)  */

		/*
		 * Since we are expecting a Reference operand, it
		 * can be either a Node or an internal object.
		 */
		return_desc = operand[0];
		if (VALID_DESCRIPTOR_TYPE (operand[0], ACPI_DESC_TYPE_INTERNAL)) {
			/* Internal reference object - prevent deletion */

			acpi_ut_add_reference (return_desc);
		}

		/*
		 * Convert the Return_desc Reference to a Number
		 * (This removes a reference on the Return_desc object)
		 */
		status = acpi_ex_resolve_operands (AML_LNOT_OP, &return_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "%s: bad operand(s) %s\n",
				acpi_ps_get_opcode_name (walk_state->opcode), acpi_format_exception(status)));

			goto cleanup;
		}

		/*
		 * Return_desc is now guaranteed to be an Integer object
		 * Do the actual increment or decrement
		 */
		if (AML_INCREMENT_OP == walk_state->opcode) {
			return_desc->integer.value++;
		}
		else {
			return_desc->integer.value--;
		}

		/* Store the result back in the original descriptor */

		status = acpi_ex_store (return_desc, operand[0], walk_state);
		break;


	case AML_TYPE_OP:               /* Object_type (Source_object) */

		if (INTERNAL_TYPE_REFERENCE == operand[0]->common.type) {
			/*
			 * Not a Name -- an indirect name pointer would have
			 * been converted to a direct name pointer in Resolve_operands
			 */
			switch (operand[0]->reference.opcode) {
			case AML_ZERO_OP:
			case AML_ONE_OP:
			case AML_ONES_OP:
			case AML_REVISION_OP:

				/* Constants are of type Integer */

				type = ACPI_TYPE_INTEGER;
				break;


			case AML_DEBUG_OP:

				/* Per 1.0b spec, Debug object is of type "Debug_object" */

				type = ACPI_TYPE_DEBUG_OBJECT;
				break;


			case AML_INDEX_OP:

				/* Get the type of this reference (index into another object) */

				type = operand[0]->reference.target_type;
				if (type == ACPI_TYPE_PACKAGE) {
					/*
					 * The main object is a package, we want to get the type
					 * of the individual package element that is referenced by
					 * the index.
					 */
					type = (*(operand[0]->reference.where))->common.type;
				}

				break;


			case AML_LOCAL_OP:
			case AML_ARG_OP:

				type = acpi_ds_method_data_get_type (operand[0]->reference.opcode,
						  operand[0]->reference.offset, walk_state);
				break;


			default:

				REPORT_ERROR (("Acpi_ex_opcode_1A_0T_1R/Type_op: Internal error - Unknown Reference subtype %X\n",
					operand[0]->reference.opcode));
				status = AE_AML_INTERNAL;
				goto cleanup;
			}
		}

		else {
			/*
			 * It's not a Reference, so it must be a direct name pointer.
			 */
			type = acpi_ns_get_type ((acpi_namespace_node *) operand[0]);

			/* Convert internal types to external types */

			switch (type) {
			case INTERNAL_TYPE_REGION_FIELD:
			case INTERNAL_TYPE_BANK_FIELD:
			case INTERNAL_TYPE_INDEX_FIELD:

				type = ACPI_TYPE_FIELD_UNIT;
			}

		}

		/* Allocate a descriptor to hold the type. */

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		return_desc->integer.value = type;
		break;


	case AML_SIZE_OF_OP:            /* Size_of (Source_object) */

		temp_desc = operand[0];
		if (VALID_DESCRIPTOR_TYPE (operand[0], ACPI_DESC_TYPE_NAMED)) {
			temp_desc = acpi_ns_get_attached_object ((acpi_namespace_node *) operand[0]);
		}

		if (!temp_desc) {
			value = 0;
		}

		else {
			switch (temp_desc->common.type) {
			case ACPI_TYPE_BUFFER:
				value = temp_desc->buffer.length;
				break;

			case ACPI_TYPE_STRING:
				value = temp_desc->string.length;
				break;

			case ACPI_TYPE_PACKAGE:
				value = temp_desc->package.count;
				break;

			case INTERNAL_TYPE_REFERENCE:

				/* TBD: this must be a reference to a buf/str/pkg?? */

				value = 4;
				break;

			default:
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Not Buf/Str/Pkg - found type %X\n",
					temp_desc->common.type));
				status = AE_AML_OPERAND_TYPE;
				goto cleanup;
			}
		}

		/*
		 * Now that we have the size of the object, create a result
		 * object to hold the value
		 */
		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		return_desc->integer.value = value;
		break;


	case AML_REF_OF_OP:             /* Ref_of (Source_object) */

		status = acpi_ex_get_object_reference (operand[0], &return_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}
		break;


	case AML_DEREF_OF_OP:           /* Deref_of (Obj_reference) */

		/* Check for a method local or argument */

		if (!VALID_DESCRIPTOR_TYPE (operand[0], ACPI_DESC_TYPE_NAMED)) {
			/*
			 * Must resolve/dereference the local/arg reference first
			 */
			switch (operand[0]->reference.opcode) {
			/* Set Operand[0] to the value of the local/arg */

			case AML_LOCAL_OP:
			case AML_ARG_OP:

				acpi_ds_method_data_get_value (operand[0]->reference.opcode,
						operand[0]->reference.offset, walk_state, &temp_desc);

				/*
				 * Delete our reference to the input object and
				 * point to the object just retrieved
				 */
				acpi_ut_remove_reference (operand[0]);
				operand[0] = temp_desc;
				break;

			default:

				/* Index op - handled below */
				break;
			}
		}


		/* Operand[0] may have changed from the code above */

		if (VALID_DESCRIPTOR_TYPE (operand[0], ACPI_DESC_TYPE_NAMED)) {
			/* Get the actual object from the Node (This is the dereference) */

			return_desc = ((acpi_namespace_node *) operand[0])->object;

			/* Returning a pointer to the object, add another reference! */

			acpi_ut_add_reference (return_desc);
		}

		else {
			/*
			 * This must be a reference object produced by the Index
			 * ASL operation -- check internal opcode
			 */
			if ((operand[0]->reference.opcode != AML_INDEX_OP) &&
				(operand[0]->reference.opcode != AML_REF_OF_OP)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown opcode in ref(%p) - %X\n",
					operand[0], operand[0]->reference.opcode));

				status = AE_TYPE;
				goto cleanup;
			}


			switch (operand[0]->reference.opcode) {
			case AML_INDEX_OP:

				/*
				 * Supported target types for the Index operator are
				 * 1) A Buffer
				 * 2) A Package
				 */
				if (operand[0]->reference.target_type == ACPI_TYPE_BUFFER_FIELD) {
					/*
					 * The target is a buffer, we must create a new object that
					 * contains one element of the buffer, the element pointed
					 * to by the index.
					 *
					 * NOTE: index into a buffer is NOT a pointer to a
					 * sub-buffer of the main buffer, it is only a pointer to a
					 * single element (byte) of the buffer!
					 */
					return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
					if (!return_desc) {
						status = AE_NO_MEMORY;
						goto cleanup;
					}

					temp_desc = operand[0]->reference.object;
					return_desc->integer.value =
						temp_desc->buffer.pointer[operand[0]->reference.offset];

					/* TBD: [Investigate] (see below) Don't add an additional
					 * ref!
					 */
				}

				else if (operand[0]->reference.target_type == ACPI_TYPE_PACKAGE) {
					/*
					 * The target is a package, we want to return the referenced
					 * element of the package.  We must add another reference to
					 * this object, however.
					 */
					return_desc = *(operand[0]->reference.where);
					if (!return_desc) {
						/*
						 * We can't return a NULL dereferenced value.  This is
						 * an uninitialized package element and is thus a
						 * severe error.
						 */

						ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "NULL package element obj %p\n",
							operand[0]));
						status = AE_AML_UNINITIALIZED_ELEMENT;
						goto cleanup;
					}

					acpi_ut_add_reference (return_desc);
				}

				else {
					ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown Target_type %X in obj %p\n",
						operand[0]->reference.target_type, operand[0]));
					status = AE_AML_OPERAND_TYPE;
					goto cleanup;
				}

				break;


			case AML_REF_OF_OP:

				return_desc = operand[0]->reference.object;

				/* Add another reference to the object! */

				acpi_ut_add_reference (return_desc);
				break;
			}
		}

		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_1A_0T_1R: Unknown opcode %X\n",
			walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
	}


cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (return_desc);
	}

	walk_state->result_obj = return_desc;
	return_ACPI_STATUS (status);
}

