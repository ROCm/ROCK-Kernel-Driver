
/******************************************************************************
 *
 * Module Name: exmisc - ACPI AML (p-code) execution - specific opcodes
 *              $Revision: 83 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exmisc")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_triadic
 *
 * PARAMETERS:  Opcode              - The opcode to be executed
 *              Walk_state          - Current walk state
 *              Return_desc         - Where to store the return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute Triadic operator (3 operands)
 *
 * ALLOCATION:  Deletes one operand descriptor -- other remains on stack
 *
 ******************************************************************************/

acpi_status
acpi_ex_triadic (
	u16                     opcode,
	acpi_walk_state         *walk_state,
	acpi_operand_object     **return_desc)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *ret_desc = NULL;
	acpi_operand_object     *tmp_desc;
	ACPI_SIGNAL_FATAL_INFO  *fatal;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_triadic");


#define obj_desc1           operand[0]
#define obj_desc2           operand[1]
#define res_desc            operand[2]


	switch (opcode) {

	case AML_FATAL_OP:

		/* Def_fatal   :=  Fatal_op Fatal_type  Fatal_code  Fatal_arg   */

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Fatal_op: Type %x Code %x Arg %x <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n",
			(u32) obj_desc1->integer.value, (u32) obj_desc2->integer.value,
			(u32) res_desc->integer.value));


		fatal = ACPI_MEM_ALLOCATE (sizeof (ACPI_SIGNAL_FATAL_INFO));
		if (fatal) {
			fatal->type = (u32) obj_desc1->integer.value;
			fatal->code = (u32) obj_desc2->integer.value;
			fatal->argument = (u32) res_desc->integer.value;
		}

		/*
		 * Signal the OS
		 */
		acpi_os_signal (ACPI_SIGNAL_FATAL, fatal);

		/* Might return while OS is shutting down */

		ACPI_MEM_FREE (fatal);
		break;


	case AML_MID_OP:

		/* Def_mid      := Mid_op Source Index Length Result */

		/* Create the internal return object (string or buffer) */

		break;


	case AML_INDEX_OP:

		/* Def_index    := Index_op Source Index Destination */

		/* Create the internal return object */

		ret_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_REFERENCE);
		if (!ret_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		/*
		 * At this point, the Obj_desc1 operand is either a Package or a Buffer
		 */
		if (obj_desc1->common.type == ACPI_TYPE_PACKAGE) {
			/* Object to be indexed is a Package */

			if (obj_desc2->integer.value >= obj_desc1->package.count) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Index value beyond package end\n"));
				status = AE_AML_PACKAGE_LIMIT;
				goto cleanup;
			}

			if ((res_desc->common.type == INTERNAL_TYPE_REFERENCE) &&
				(res_desc->reference.opcode == AML_ZERO_OP)) {
				/*
				 * There is no actual result descriptor (the Zero_op Result
				 * descriptor is a placeholder), so just delete the placeholder and
				 * return a reference to the package element
				 */
				acpi_ut_remove_reference (res_desc);
			}

			else {
				/*
				 * Each element of the package is an internal object.  Get the one
				 * we are after.
				 */
				tmp_desc                      = obj_desc1->package.elements[obj_desc2->integer.value];
				ret_desc->reference.opcode    = AML_INDEX_OP;
				ret_desc->reference.target_type = tmp_desc->common.type;
				ret_desc->reference.object    = tmp_desc;

				status = acpi_ex_store (ret_desc, res_desc, walk_state);
				ret_desc->reference.object    = NULL;
			}

			/*
			 * The local return object must always be a reference to the package element,
			 * not the element itself.
			 */
			ret_desc->reference.opcode    = AML_INDEX_OP;
			ret_desc->reference.target_type = ACPI_TYPE_PACKAGE;
			ret_desc->reference.where     = &obj_desc1->package.elements[obj_desc2->integer.value];
		}

		else {
			/* Object to be indexed is a Buffer */

			if (obj_desc2->integer.value >= obj_desc1->buffer.length) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Index value beyond end of buffer\n"));
				status = AE_AML_BUFFER_LIMIT;
				goto cleanup;
			}

			ret_desc->reference.opcode      = AML_INDEX_OP;
			ret_desc->reference.target_type = ACPI_TYPE_BUFFER_FIELD;
			ret_desc->reference.object      = obj_desc1;
			ret_desc->reference.offset      = (u32) obj_desc2->integer.value;

			status = acpi_ex_store (ret_desc, res_desc, walk_state);
		}
		break;
	}


cleanup:

	/* Always delete operands */

	acpi_ut_remove_reference (obj_desc1);
	acpi_ut_remove_reference (obj_desc2);

	/* Delete return object on error */

	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (res_desc);

		if (ret_desc) {
			acpi_ut_remove_reference (ret_desc);
			ret_desc = NULL;
		}
	}

	/* Set the return object and exit */

	*return_desc = ret_desc;
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_hexadic
 *
 * PARAMETERS:  Opcode              - The opcode to be executed
 *              Walk_state          - Current walk state
 *              Return_desc         - Where to store the return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute Match operator
 *
 ******************************************************************************/

acpi_status
acpi_ex_hexadic (
	u16                     opcode,
	acpi_walk_state         *walk_state,
	acpi_operand_object     **return_desc)
{
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_operand_object     *ret_desc = NULL;
	acpi_status             status = AE_OK;
	u32                     index;
	u32                     match_value = (u32) -1;


	FUNCTION_TRACE ("Ex_hexadic");

#define pkg_desc            operand[0]
#define op1_desc            operand[1]
#define V1_desc             operand[2]
#define op2_desc            operand[3]
#define V2_desc             operand[4]
#define start_desc          operand[5]


	switch (opcode) {

		case AML_MATCH_OP:

		/* Validate match comparison sub-opcodes */

		if ((op1_desc->integer.value > MAX_MATCH_OPERATOR) ||
			(op2_desc->integer.value > MAX_MATCH_OPERATOR)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "operation encoding out of range\n"));
			status = AE_AML_OPERAND_VALUE;
			goto cleanup;
		}

		index = (u32) start_desc->integer.value;
		if (index >= (u32) pkg_desc->package.count) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Start position value out of range\n"));
			status = AE_AML_PACKAGE_LIMIT;
			goto cleanup;
		}

		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!ret_desc) {
			status = AE_NO_MEMORY;
			goto cleanup;

		}

		/*
		 * Examine each element until a match is found.  Within the loop,
		 * "continue" signifies that the current element does not match
		 * and the next should be examined.
		 * Upon finding a match, the loop will terminate via "break" at
		 * the bottom.  If it terminates "normally", Match_value will be -1
		 * (its initial value) indicating that no match was found.  When
		 * returned as a Number, this will produce the Ones value as specified.
		 */
		for ( ; index < pkg_desc->package.count; ++index) {
			/*
			 * Treat any NULL or non-numeric elements as non-matching.
			 * TBD [Unhandled] - if an element is a Name,
			 *      should we examine its value?
			 */
			if (!pkg_desc->package.elements[index] ||
				ACPI_TYPE_INTEGER != pkg_desc->package.elements[index]->common.type) {
				continue;
			}

			/*
			 * Within these switch statements:
			 *      "break" (exit from the switch) signifies a match;
			 *      "continue" (proceed to next iteration of enclosing
			 *          "for" loop) signifies a non-match.
			 */
			switch (op1_desc->integer.value) {

			case MATCH_MTR:   /* always true */

				break;


			case MATCH_MEQ:   /* true if equal   */

				if (pkg_desc->package.elements[index]->integer.value
					 != V1_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MLE:   /* true if less than or equal  */

				if (pkg_desc->package.elements[index]->integer.value
					 > V1_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MLT:   /* true if less than   */

				if (pkg_desc->package.elements[index]->integer.value
					 >= V1_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MGE:   /* true if greater than or equal   */

				if (pkg_desc->package.elements[index]->integer.value
					 < V1_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MGT:   /* true if greater than    */

				if (pkg_desc->package.elements[index]->integer.value
					 <= V1_desc->integer.value) {
					continue;
				}
				break;


			default:    /* undefined   */

				continue;
			}


			switch(op2_desc->integer.value) {

			case MATCH_MTR:

				break;


			case MATCH_MEQ:

				if (pkg_desc->package.elements[index]->integer.value
					 != V2_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MLE:

				if (pkg_desc->package.elements[index]->integer.value
					 > V2_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MLT:

				if (pkg_desc->package.elements[index]->integer.value
					 >= V2_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MGE:

				if (pkg_desc->package.elements[index]->integer.value
					 < V2_desc->integer.value) {
					continue;
				}
				break;


			case MATCH_MGT:

				if (pkg_desc->package.elements[index]->integer.value
					 <= V2_desc->integer.value) {
					continue;
				}
				break;


			default:

				continue;
			}

			/* Match found: exit from loop */

			match_value = index;
			break;
		}

		/* Match_value is the return value */

		ret_desc->integer.value = match_value;
		break;

	}


cleanup:

	/* Free the operands */

	acpi_ut_remove_reference (start_desc);
	acpi_ut_remove_reference (V2_desc);
	acpi_ut_remove_reference (op2_desc);
	acpi_ut_remove_reference (V1_desc);
	acpi_ut_remove_reference (op1_desc);
	acpi_ut_remove_reference (pkg_desc);


	/* Delete return object on error */

	if (ACPI_FAILURE (status) &&
		(ret_desc)) {
		acpi_ut_remove_reference (ret_desc);
		ret_desc = NULL;
	}


	/* Set the return object and exit */

	*return_desc = ret_desc;
	return_ACPI_STATUS (status);
}
