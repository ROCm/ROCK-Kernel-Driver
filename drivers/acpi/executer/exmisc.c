
/******************************************************************************
 *
 * Module Name: exmisc - ACPI AML (p-code) execution - specific opcodes
 *              $Revision: 92 $
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
 * FUNCTION:    Acpi_ex_get_object_reference
 *
 * PARAMETERS:  Obj_desc        - Create a reference to this object
 *              Return_desc        - Where to store the reference
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Obtain and return a "reference" to the target object
 *              Common code for the Ref_of_op and the Cond_ref_of_op.
 *
 ******************************************************************************/

acpi_status
acpi_ex_get_object_reference (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **return_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_PTR ("Ex_get_object_reference", obj_desc);


	if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_INTERNAL)) {
		if (obj_desc->common.type != INTERNAL_TYPE_REFERENCE) {
			*return_desc = NULL;
			status = AE_TYPE;
			goto cleanup;
		}

		/*
		 * Not a Name -- an indirect name pointer would have
		 * been converted to a direct name pointer in Acpi_ex_resolve_operands
		 */
		switch (obj_desc->reference.opcode) {
		case AML_LOCAL_OP:
		case AML_ARG_OP:

			*return_desc = (void *) acpi_ds_method_data_get_node (obj_desc->reference.opcode,
					  obj_desc->reference.offset, walk_state);
			break;

		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "(Internal) Unknown Ref subtype %02x\n",
				obj_desc->reference.opcode));
			*return_desc = NULL;
			status = AE_AML_INTERNAL;
			goto cleanup;
		}

	}

	else if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED)) {
		/* Must be a named object;  Just return the Node */

		*return_desc = obj_desc;
	}

	else {
		*return_desc = NULL;
		status = AE_TYPE;
	}


cleanup:

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Obj=%p Ref=%p\n", obj_desc, *return_desc));
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_do_concatenate
 *
 * PARAMETERS:  *Obj_desc           - Object to be converted.  Must be an
 *                                    Integer, Buffer, or String
 *              Walk_state          - Current walk state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Concatenate two objects OF THE SAME TYPE.
 *
 ******************************************************************************/

acpi_status
acpi_ex_do_concatenate (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *obj_desc2,
	acpi_operand_object     **actual_return_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	u32                     i;
	acpi_integer            this_integer;
	acpi_operand_object     *return_desc;
	NATIVE_CHAR             *new_buf;
	u32                     integer_size = sizeof (acpi_integer);


	FUNCTION_ENTRY ();


	/*
	 * There are three cases to handle:
	 * 1) Two Integers concatenated to produce a buffer
	 * 2) Two Strings concatenated to produce a string
	 * 3) Two Buffers concatenated to produce a buffer
	 */
	switch (obj_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		/* Handle both ACPI 1.0 and ACPI 2.0 Integer widths */

		if (walk_state->method_node->flags & ANOBJ_DATA_WIDTH_32) {
			/*
			 * We are running a method that exists in a 32-bit ACPI table.
			 * Truncate the value to 32 bits by zeroing out the upper
			 * 32-bit field
			 */
			integer_size = sizeof (u32);
		}

		/* Result of two integers is a buffer */

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		if (!return_desc) {
			return (AE_NO_MEMORY);
		}

		/* Need enough space for two integers */

		return_desc->buffer.length = integer_size * 2;
		new_buf = ACPI_MEM_CALLOCATE (return_desc->buffer.length);
		if (!new_buf) {
			REPORT_ERROR
				(("Ex_do_concatenate: Buffer allocation failure\n"));
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		return_desc->buffer.pointer = (u8 *) new_buf;

		/* Convert the first integer */

		this_integer = obj_desc->integer.value;
		for (i = 0; i < integer_size; i++) {
			new_buf[i] = (u8) this_integer;
			this_integer >>= 8;
		}

		/* Convert the second integer */

		this_integer = obj_desc2->integer.value;
		for (; i < (integer_size * 2); i++) {
			new_buf[i] = (u8) this_integer;
			this_integer >>= 8;
		}

		break;


	case ACPI_TYPE_STRING:

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_STRING);
		if (!return_desc) {
			return (AE_NO_MEMORY);
		}

		/* Operand0 is string  */

		new_buf = ACPI_MEM_ALLOCATE (obj_desc->string.length +
				  obj_desc2->string.length + 1);
		if (!new_buf) {
			REPORT_ERROR
				(("Ex_do_concatenate: String allocation failure\n"));
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		STRCPY (new_buf, obj_desc->string.pointer);
		STRCPY (new_buf + obj_desc->string.length,
				  obj_desc2->string.pointer);

		/* Point the return object to the new string */

		return_desc->string.pointer = new_buf;
		return_desc->string.length = obj_desc->string.length +=
				  obj_desc2->string.length;
		break;


	case ACPI_TYPE_BUFFER:

		/* Operand0 is a buffer */

		return_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		if (!return_desc) {
			return (AE_NO_MEMORY);
		}

		new_buf = ACPI_MEM_ALLOCATE (obj_desc->buffer.length +
				  obj_desc2->buffer.length);
		if (!new_buf) {
			REPORT_ERROR
				(("Ex_do_concatenate: Buffer allocation failure\n"));
			status = AE_NO_MEMORY;
			goto cleanup;
		}

		MEMCPY (new_buf, obj_desc->buffer.pointer,
				  obj_desc->buffer.length);
		MEMCPY (new_buf + obj_desc->buffer.length, obj_desc2->buffer.pointer,
				   obj_desc2->buffer.length);

		/*
		 * Point the return object to the new buffer
		 */

		return_desc->buffer.pointer    = (u8 *) new_buf;
		return_desc->buffer.length     = obj_desc->buffer.length +
				 obj_desc2->buffer.length;
		break;


	default:
		status = AE_AML_INTERNAL;
		return_desc = NULL;
	}


	*actual_return_desc = return_desc;
	return (AE_OK);


cleanup:

	acpi_ut_remove_reference (return_desc);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_do_math_op
 *
 * PARAMETERS:  Opcode              - AML opcode
 *              Operand0            - Integer operand #0
 *              Operand0            - Integer operand #1
 *
 * RETURN:      Integer result of the operation
 *
 * DESCRIPTION: Execute a math AML opcode. The purpose of having all of the
 *              math functions here is to prevent a lot of pointer dereferencing
 *              to obtain the operands.
 *
 ******************************************************************************/

acpi_integer
acpi_ex_do_math_op (
	u16                     opcode,
	acpi_integer            operand0,
	acpi_integer            operand1)
{


	switch (opcode) {
	case AML_ADD_OP:                /* Add (Operand0, Operand1, Result) */

		return (operand0 + operand1);


	case AML_BIT_AND_OP:            /* And (Operand0, Operand1, Result) */

		return (operand0 & operand1);


	case AML_BIT_NAND_OP:           /* NAnd (Operand0, Operand1, Result) */

		return (~(operand0 & operand1));


	case AML_BIT_OR_OP:             /* Or (Operand0, Operand1, Result) */

		return (operand0 | operand1);


	case AML_BIT_NOR_OP:            /* NOr (Operand0, Operand1, Result) */

		return (~(operand0 | operand1));


	case AML_BIT_XOR_OP:            /* XOr (Operand0, Operand1, Result) */

		return (operand0 ^ operand1);


	case AML_MULTIPLY_OP:           /* Multiply (Operand0, Operand1, Result) */

		return (operand0 * operand1);


	case AML_SHIFT_LEFT_OP:         /* Shift_left (Operand, Shift_count, Result) */

		return (operand0 << operand1);


	case AML_SHIFT_RIGHT_OP:        /* Shift_right (Operand, Shift_count, Result) */

		return (operand0 >> operand1);


	case AML_SUBTRACT_OP:           /* Subtract (Operand0, Operand1, Result) */

		return (operand0 - operand1);

	default:

		return (0);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_do_logical_op
 *
 * PARAMETERS:  Opcode              - AML opcode
 *              Operand0            - Integer operand #0
 *              Operand0            - Integer operand #1
 *
 * RETURN:      TRUE/FALSE result of the operation
 *
 * DESCRIPTION: Execute a logical AML opcode. The purpose of having all of the
 *              functions here is to prevent a lot of pointer dereferencing
 *              to obtain the operands and to simplify the generation of the
 *              logical value.
 *
 *              Note: cleanest machine code seems to be produced by the code
 *              below, rather than using statements of the form:
 *                  Result = (Operand0 == Operand1);
 *
 ******************************************************************************/

u8
acpi_ex_do_logical_op (
	u16                     opcode,
	acpi_integer            operand0,
	acpi_integer            operand1)
{


	switch (opcode) {

	case AML_LAND_OP:               /* LAnd (Operand0, Operand1) */

		if (operand0 && operand1) {
			return (TRUE);
		}
		break;


	case AML_LEQUAL_OP:             /* LEqual (Operand0, Operand1) */

		if (operand0 == operand1) {
			return (TRUE);
		}
		break;


	case AML_LGREATER_OP:           /* LGreater (Operand0, Operand1) */

		if (operand0 > operand1) {
			return (TRUE);
		}
		break;


	case AML_LLESS_OP:              /* LLess (Operand0, Operand1) */

		if (operand0 < operand1) {
			return (TRUE);
		}
		break;


	case AML_LOR_OP:                 /* LOr (Operand0, Operand1) */

		if (operand0 || operand1) {
			return (TRUE);
		}
		break;
	}

	return (FALSE);
}


