
/******************************************************************************
 *
 * Module Name: exoparg3 - AML execution - opcodes with 3 arguments
 *              $Revision: 3 $
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
	 MODULE_NAME         ("exoparg3")


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
 * FUNCTION:    Acpi_ex_opcode_3A_0T_0R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute Triadic operator (3 operands)
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_3A_0T_0R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	ACPI_SIGNAL_FATAL_INFO  *fatal;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_STR ("Ex_opcode_3A_0T_0R", acpi_ps_get_opcode_name (walk_state->opcode));


	switch (walk_state->opcode) {

	case AML_FATAL_OP:          /* Fatal (Fatal_type Fatal_code Fatal_arg)   */

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Fatal_op: Type %x Code %x Arg %x <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n",
			(u32) operand[0]->integer.value, (u32) operand[1]->integer.value,
			(u32) operand[2]->integer.value));


		fatal = ACPI_MEM_ALLOCATE (sizeof (ACPI_SIGNAL_FATAL_INFO));
		if (fatal) {
			fatal->type     = (u32) operand[0]->integer.value;
			fatal->code     = (u32) operand[1]->integer.value;
			fatal->argument = (u32) operand[2]->integer.value;
		}

		/*
		 * Always signal the OS!
		 */
		acpi_os_signal (ACPI_SIGNAL_FATAL, fatal);

		/* Might return while OS is shutting down, just continue */

		ACPI_MEM_FREE (fatal);
		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_3A_0T_0R: Unknown opcode %X\n",
				walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
		break;
	}


cleanup:

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_opcode_3A_1T_1R
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute Triadic operator (3 operands)
 *
 ******************************************************************************/

acpi_status
acpi_ex_opcode_3A_1T_1R (
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *return_desc = NULL;
	char                    *buffer;
	acpi_status             status = AE_OK;
	u32                     index;
	u32                     length;


	FUNCTION_TRACE_STR ("Ex_opcode_3A_1T_1R", acpi_ps_get_opcode_name (walk_state->opcode));


	switch (walk_state->opcode) {
	case AML_MID_OP:        /* Mid  (Source[0], Index[1], Length[2], Result[3]) */

		/*
		 * Create the return object.  The Source operand is guaranteed to be
		 * either a String or a Buffer, so just use its type.
		 */
		return_desc = acpi_ut_create_internal_object (operand[0]->common.type);
		if (!return_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/* Get the Integer values from the objects */

		index = (u32) operand[1]->integer.value;
		length = (u32) operand[2]->integer.value;

		/*
		 * If the index is beyond the length of the String/Buffer, or if the
		 * requested length is zero, return a zero-length String/Buffer
		 */
		if ((index < operand[0]->string.length) &&
			(length > 0)) {
			/* Truncate request if larger than the actual String/Buffer */

			if ((index + length) >
				operand[0]->string.length) {
				length = operand[0]->string.length - index;
			}

			/* Allocate a new buffer for the String/Buffer */

			buffer = ACPI_MEM_CALLOCATE (length + 1);
			if (!buffer) {
				return (AE_NO_MEMORY);
			}

			/* Copy the portion requested */

			MEMCPY (buffer, operand[0]->string.pointer + index,
					length);

			/* Set the length of the new String/Buffer */

			return_desc->string.pointer = buffer;
			return_desc->string.length = length;
		}

		break;


	default:

		REPORT_ERROR (("Acpi_ex_opcode_3A_0T_0R: Unknown opcode %X\n",
				walk_state->opcode));
		status = AE_AML_BAD_OPCODE;
		goto cleanup;
		break;
	}

	/* Store the result in the target */

	status = acpi_ex_store (return_desc, operand[3], walk_state);

cleanup:

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (return_desc);
	}

	/* Set the return object and exit */

	walk_state->result_obj = return_desc;
	return_ACPI_STATUS (status);
}


