/******************************************************************************
 *
 * Module Name: exoparg2 - AML execution - opcodes with 2 arguments
 *              $Revision: 97 $
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
#include "acnamesp.h"
#include "acinterp.h"
#include "acevents.h"
#include "amlcode.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exoparg2")


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
 * FUNCTION:    Acpi_ex_opcode_2A_0T_0R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with two arguments, no target, and no return
 *              value.
 *
 * ALLOCATION:  Deletes both operands
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_2A_0T_0R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_namespace_node     *node;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_STR ("Ex_opcode_2A_0T_0R", acpi_ps_get_opcode_name (walk_state->opcode));


	/* Examine the opcode */

	switch (walk_state->opcode) {

	case AML_NOTIFY_OP:         /* Notify (Notify_object, Notify_value) */

		/* The first operand is a namespace node */

		node = (acpi_namespace_node *) operand[0];

		/* The node must refer to a device or thermal zone */

		if (node && operand[1])     /* TBD: is this check necessary? */ {
			switch (node->type) {
			case ACPI_TYPE_DEVICE:
			case ACPI_TYPE_THERMAL:

				/*
				 * Dispatch the notify to the appropriate handler
				 * NOTE: the request is queued for execution after this method
				 * completes.  The notify handlers are NOT invoked synchronously
				 * from this thread -- because handlers may in turn run other
				 * control methods.
				 */
				status = acpi_ev_queue_notify_request (node,
						 (u32) operand[1]->integer.value);
				break;

			default:
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unexpected notify object type %X\n",
					node->type));

				status = AE_AML_OPERAND_TYPE;
				break;
			}
		}
		break;

	default:

		REPORT_ERROR (("Acpi_ex_opcode_2A_0T_0R: Unknown opcode %X\n", walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_2A_2T_1R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a dyadic operator (2 operands) with 2 output targets
 *              and one implicit return value.
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_2A_2T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *return_desc1 = NULL;
	acpi_operand_object     *return_desc2 = NULL;
	acpi_status             status;


	FUNCTION_TRACE_STR ("Ex_opcode_2A_2T_1R", acpi_ps_get_opcode_name (walk_state->opcode));


	/*
	 * Execute the opcode
	 */
	switch (walk_state->opcode) {
	case AML_DIVIDE_OP:             /* Divide (Dividend, Divisor, Remainder_result Quotient_result) */

		return_desc1 = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc1) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		return_desc2 = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc2) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/* Quotient to Return_desc1, remainder to Return_desc2 */

		status = acpi_ut_divide (&operand[0]->integer.value, &operand[1]->integer.value,
				   &return_desc1->integer.value, &return_desc2->integer.value);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}
		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_2A_2T_1R: Unknown opcode %X\n",
				walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
		break;
	}


	/* Store the results to the target reference operands */

	status = acpi_ex_store (return_desc2, operand[2], walk_state);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	status = acpi_ex_store (return_desc1, operand[3], walk_state);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Return the remainder */

	walk_state->result_obj = return_desc1;


cleanup:
	/*
	 * Since the remainder is not returned indirectly, remove a reference to
	 * it. Only the quotient is returned indirectly.
	 */
	acpi_ut_remove_reference (return_desc2);

	if (ACPI_FAILURE (status)) {
		/* Delete the return object */

		acpi_ut_remove_reference (return_desc1);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_2A_1T_1R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with two arguments, one target, and a return
 *              value.
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_2A_1T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand   = &walk_state->operands[0];
	acpi_operand_object     *return_desc = NULL;
	acpi_operand_object     *temp_desc;
	u32                     index;
	acpi_status             status      = AE_OK;


	FUNCTION_TRACE_STR ("Ex_opcode_2A_1T_1R", acpi_ps_get_opcode_name (walk_state->opcode));


	/*
	 * Execute the opcode
	 */
	if (walk_state->op_info->flags & AML_MATH) {
		/* All simple math opcodes (add, etc.) */

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		return_desc->integer.value = acpi_ex_do_math_op (walk_state->opcode,
				  operand[0]->integer.value,
				  operand[1]->integer.value);
		goto store_result_to_target;
	}


	switch (walk_state->opcode) {
	case AML_MOD_OP:                /* Mod (Dividend, Divisor, Remainder_result (ACPI 2.0) */

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/* Return_desc will contain the remainder */

		status = acpi_ut_divide (&operand[0]->integer.value, &operand[1]->integer.value,
				  NULL, &return_desc->integer.value);

		break;


	case AML_CONCAT_OP:             /* Concatenate (Data1, Data2, Result) */

		/*
		 * Convert the second operand if necessary.  The first operand
		 * determines the type of the second operand, (See the Data Types
		 * section of the ACPI specification.)  Both object types are
		 * guaranteed to be either Integer/String/Buffer by the operand
		 * resolution mechanism above.
		 */
		switch (operand[0]->common.type) {
		case ACPI_TYPE_INTEGER:
			status = acpi_ex_convert_to_integer (operand[1], &operand[1], walk_state);
			break;

		case ACPI_TYPE_STRING:
			status = acpi_ex_convert_to_string (operand[1], &operand[1], 16, ACPI_UINT32_MAX, walk_state);
			break;

		case ACPI_TYPE_BUFFER:
			status = acpi_ex_convert_to_buffer (operand[1], &operand[1], walk_state);
			break;

		default:
			status = AE_AML_INTERNAL;
		}

		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/*
		 * Both operands are now known to be the same object type
		 * (Both are Integer, String, or Buffer), and we can now perform the
		 * concatenation.
		 */
		status = acpi_ex_do_concatenate (operand[0], operand[1], &return_desc, walk_state);
		break;


	case AML_TO_STRING_OP:          /* To_string (Buffer, Length, Result) (ACPI 2.0) */

		status = acpi_ex_convert_to_string (operand[0], &return_desc, 16,
				  (u32) operand[1]->integer.value, walk_state);
		break;


	case AML_CONCAT_RES_OP:         /* Concatenate_res_template (Buffer, Buffer, Result) (ACPI 2.0) */

		status = AE_NOT_IMPLEMENTED;
		break;


	case AML_INDEX_OP:              /* Index (Source Index Result) */

		/* Create the internal return object */

		return_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_REFERENCE);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		index = (u32) operand[1]->integer.value;

		/*
		 * At this point, the Source operand is either a Package or a Buffer
		 */
		if (operand[0]->common.type == ACPI_TYPE_PACKAGE) {
			/* Object to be indexed is a Package */

			if (index >= operand[0]->package.count) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Index value beyond package end\n"));
				status = AE_AML_PACKAGE_LIMIT;
				goto cleanup;
			}

			if ((operand[2]->common.type == INTERNAL_TYPE_REFERENCE) &&
				(operand[2]->reference.opcode == AML_ZERO_OP)) {
				/*
				 * There is no actual result descriptor (the Zero_op Result
				 * descriptor is a placeholder), so just delete the placeholder and
				 * return a reference to the package element
				 */
				acpi_ut_remove_reference (operand[2]);
			}

			else {
				/*
				 * Each element of the package is an internal object.  Get the one
				 * we are after.
				 */
				temp_desc                        = operand[0]->package.elements [index];
				return_desc->reference.opcode    = AML_INDEX_OP;
				return_desc->reference.target_type = temp_desc->common.type;
				return_desc->reference.object    = temp_desc;

				status = acpi_ex_store (return_desc, operand[2], walk_state);
				return_desc->reference.object    = NULL;
			}

			/*
			 * The local return object must always be a reference to the package element,
			 * not the element itself.
			 */
			return_desc->reference.opcode    = AML_INDEX_OP;
			return_desc->reference.target_type = ACPI_TYPE_PACKAGE;
			return_desc->reference.where     = &operand[0]->package.elements [index];
		}

		else {
			/* Object to be indexed is a Buffer */

			if (index >= operand[0]->buffer.length) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Index value beyond end of buffer\n"));
				status = AE_AML_BUFFER_LIMIT;
				goto cleanup;
			}

			return_desc->reference.opcode      = AML_INDEX_OP;
			return_desc->reference.target_type = ACPI_TYPE_BUFFER_FIELD;
			return_desc->reference.object      = operand[0];
			return_desc->reference.offset      = index;

			status = acpi_ex_store (return_desc, operand[2], walk_state);
		}

		walk_state->result_obj = return_desc;
		goto cleanup;
		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_2A_1T_1R: Unknown opcode %X\n",
				walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		break;
	}


store_result_to_target:

	if (ACPI_SUCCESS (status)) {
		/*
		 * Store the result of the operation (which is now in Return_desc) into
		 * the Target descriptor.
		 */
		status = acpi_ex_store (return_desc, operand[2], walk_state);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		walk_state->result_obj = return_desc;
	}


cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (return_desc);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_2A_0T_1R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with 2 arguments, no target, and a return value
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_2A_0T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *return_desc = NULL;
	acpi_status             status = AE_OK;
	u8                      logical_result = FALSE;


	FUNCTION_TRACE_STR ("Ex_opcode_2A_0T_1R", acpi_ps_get_opcode_name (walk_state->opcode));


	/* Create the internal return object */

	return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
	if (!return_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}

	/*
	 * Execute the Opcode
	 */
	if (walk_state->op_info->flags & AML_LOGICAL) /* Logical_op (Operand0, Operand1) */ {
		logical_result = acpi_ex_do_logical_op (walk_state->opcode,
				 operand[0]->integer.value,
				 operand[1]->integer.value);
		goto store_logical_result;
	}


	switch (walk_state->opcode) {
	case AML_ACQUIRE_OP:            /* Acquire (Mutex_object, Timeout) */

		status = acpi_ex_acquire_mutex (operand[1], operand[0], walk_state);
		if (status == AE_TIME) {
			logical_result = TRUE;      /* TRUE = Acquire timed out */
			status = AE_OK;
		}
		break;


	case AML_WAIT_OP:               /* Wait (Event_object, Timeout) */

		status = acpi_ex_system_wait_event (operand[1], operand[0]);
		if (status == AE_TIME) {
			logical_result = TRUE;      /* TRUE, Wait timed out */
			status = AE_OK;
		}
		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_2A_0T_1R: Unknown opcode %X\n", walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
		break;
	}


store_logical_result:
	/*
	 * Set return value to according to Logical_result. logical TRUE (all ones)
	 * Default is FALSE (zero)
	 */
	if (logical_result) {
		return_desc->integer.value = ACPI_INTEGER_MAX;
	}

	walk_state->result_obj = return_desc;


cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (return_desc);
	}

	return_ACPI_STATUS (status);
}


