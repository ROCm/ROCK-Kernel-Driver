
/******************************************************************************
 *
 * Module Name: exoparg6 - AML execution - opcodes with 6 arguments
 *              $Revision: 4 $
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
#include "acparser.h"
#include "amlcode.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exoparg6")


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
 * FUNCTION:    Acpi_ex_do_match
 *
 * PARAMETERS:  Match_op        - The AML match operand
 *              Package_value   - Value from the target package
 *              Match_value     - Value to be matched
 *
 * RETURN:      TRUE if the match is successful, FALSE otherwise
 *
 * DESCRIPTION: Implements the low-level match for the ASL Match operator
 *
 ******************************************************************************/

u8
acpi_ex_do_match (
	u32                     match_op,
	acpi_integer            package_value,
	acpi_integer            match_value)
{

	switch (match_op) {
	case MATCH_MTR:   /* always true */

		break;


	case MATCH_MEQ:   /* true if equal   */

		if (package_value != match_value) {
			return (FALSE);
		}
		break;


	case MATCH_MLE:   /* true if less than or equal  */

		if (package_value > match_value) {
			return (FALSE);
		}
		break;


	case MATCH_MLT:   /* true if less than   */

		if (package_value >= match_value) {
			return (FALSE);
		}
		break;


	case MATCH_MGE:   /* true if greater than or equal   */

		if (package_value < match_value) {
			return (FALSE);
		}
		break;


	case MATCH_MGT:   /* true if greater than    */

		if (package_value <= match_value) {
			return (FALSE);
		}
		break;


	default:    /* undefined   */

		return (FALSE);
	}


	return TRUE;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_6A_0T_1R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute opcode with 6 arguments, no target, and a return value
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_6A_0T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *return_desc = NULL;
	acpi_status             status = AE_OK;
	u32                     index;
	acpi_operand_object     *this_element;


	FUNCTION_TRACE_STR ("Ex_opcode_6A_0T_1R", acpi_ps_get_opcode_name (walk_state->opcode));


	switch (walk_state->opcode) {
	case AML_MATCH_OP:
		/*
		 * Match (Search_package[0], Match_op1[1], Match_object1[2],
		 *                          Match_op2[3], Match_object2[4], Start_index[5])
		 */

		/* Validate match comparison sub-opcodes */

		if ((operand[1]->integer.value > MAX_MATCH_OPERATOR) ||
			(operand[3]->integer.value > MAX_MATCH_OPERATOR)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "operation encoding out of range\n"));
			status = AE_AML_OPERAND_VALUE;
			goto cleanup;
		}

		index = (u32) operand[5]->integer.value;
		if (index >= (u32) operand[0]->package.count) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Index beyond package end\n"));
			status = AE_AML_PACKAGE_LIMIT;
			goto cleanup;
		}

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;

		}

		/* Default return value if no match found */

		return_desc->integer.value = ACPI_INTEGER_MAX;

		/*
		 * Examine each element until a match is found.  Within the loop,
		 * "continue" signifies that the current element does not match
		 * and the next should be examined.
		 * Upon finding a match, the loop will terminate via "break" at
		 * the bottom.  If it terminates "normally", Match_value will be -1
		 * (its initial value) indicating that no match was found.  When
		 * returned as a Number, this will produce the Ones value as specified.
		 */
		for ( ; index < operand[0]->package.count; index++) {
			this_element = operand[0]->package.elements[index];

			/*
			 * Treat any NULL or non-numeric elements as non-matching.
			 * TBD [Unhandled] - if an element is a Name,
			 *      should we examine its value?
			 */
			if (!this_element ||
				this_element->common.type != ACPI_TYPE_INTEGER) {
				continue;
			}


			/*
			 * Within these switch statements:
			 *      "break" (exit from the switch) signifies a match;
			 *      "continue" (proceed to next iteration of enclosing
			 *          "for" loop) signifies a non-match.
			 */
			if (!acpi_ex_do_match ((u32) operand[1]->integer.value,
					   this_element->integer.value, operand[2]->integer.value)) {
				continue;
			}


			if (!acpi_ex_do_match ((u32) operand[3]->integer.value,
					   this_element->integer.value, operand[4]->integer.value)) {
				continue;
			}

			/* Match found: Index is the return value */

			return_desc->integer.value = index;
			break;
		}

		break;


	case AML_LOAD_TABLE_OP:

		status = AE_NOT_IMPLEMENTED;
		goto cleanup;
		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_3A_0T_0R: Unknown opcode %X\n",
				walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
		break;
	}


	walk_state->result_obj = return_desc;


cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (return_desc);
	}

	return_ACPI_STATUS (status);
}
