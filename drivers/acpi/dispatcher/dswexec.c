/******************************************************************************
 *
 * Module Name: dswexec - Dispatcher method execution callbacks;
 *                        dispatch to interpreter.
 *              $Revision: 79 $
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
#include "amlcode.h"
#include "acdispat.h"
#include "acinterp.h"
#include "acnamesp.h"
#include "acdebug.h"


#define _COMPONENT          ACPI_DISPATCHER
	 MODULE_NAME         ("dswexec")

/*
 * Dispatch tables for opcode classes
 */
ACPI_EXECUTE_OP         acpi_gbl_op_type_dispatch [] = {
			 acpi_ex_opcode_1A_0T_0R,
			 acpi_ex_opcode_1A_0T_1R,
			 acpi_ex_opcode_1A_1T_0R,
			 acpi_ex_opcode_1A_1T_1R,
			 acpi_ex_opcode_2A_0T_0R,
			 acpi_ex_opcode_2A_0T_1R,
			 acpi_ex_opcode_2A_1T_1R,
			 acpi_ex_opcode_2A_2T_1R,
			 acpi_ex_opcode_3A_0T_0R,
			 acpi_ex_opcode_3A_1T_1R,
			 acpi_ex_opcode_6A_0T_1R};

/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_get_predicate_value
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get the result of a predicate evaluation
 *
 ****************************************************************************/

acpi_status
acpi_ds_get_predicate_value (
	acpi_walk_state         *walk_state,
	u32                     has_result_obj) {
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE_PTR ("Ds_get_predicate_value", walk_state);


	walk_state->control_state->common.state = 0;

	if (has_result_obj) {
		status = acpi_ds_result_pop (&obj_desc, walk_state);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not get result from predicate evaluation, %s\n",
				acpi_format_exception (status)));

			return_ACPI_STATUS (status);
		}
	}

	else {
		status = acpi_ds_create_operand (walk_state, walk_state->op, 0);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		status = acpi_ex_resolve_to_value (&walk_state->operands [0], walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		obj_desc = walk_state->operands [0];
	}

	if (!obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No predicate Obj_desc=%p State=%p\n",
			obj_desc, walk_state));

		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}


	/*
	 * Result of predicate evaluation currently must
	 * be a number
	 */
	if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Bad predicate (not a number) Obj_desc=%p State=%p Type=%X\n",
			obj_desc, walk_state, obj_desc->common.type));

		status = AE_AML_OPERAND_TYPE;
		goto cleanup;
	}


	/* Truncate the predicate to 32-bits if necessary */

	acpi_ex_truncate_for32bit_table (obj_desc, walk_state);

	/*
	 * Save the result of the predicate evaluation on
	 * the control stack
	 */
	if (obj_desc->integer.value) {
		walk_state->control_state->common.value = TRUE;
	}

	else {
		/*
		 * Predicate is FALSE, we will just toss the
		 * rest of the package
		 */
		walk_state->control_state->common.value = FALSE;
		status = AE_CTRL_FALSE;
	}


cleanup:

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Completed a predicate eval=%X Op=%pn",
		walk_state->control_state->common.value, walk_state->op));

	 /* Break to debugger to display result */

	DEBUGGER_EXEC (acpi_db_display_result_object (obj_desc, walk_state));

	/*
	 * Delete the predicate result object (we know that
	 * we don't need it anymore)
	 */
	acpi_ut_remove_reference (obj_desc);

	walk_state->control_state->common.state = CONTROL_NORMAL;
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_begin_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Out_op          - Return op if a new one is created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Descending callback used during the execution of control
 *              methods.  This is where most operators and operands are
 *              dispatched to the interpreter.
 *
 ****************************************************************************/

acpi_status
acpi_ds_exec_begin_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       **out_op)
{
	acpi_parse_object       *op;
	acpi_status             status = AE_OK;
	u32                     opcode_class;


	FUNCTION_TRACE_PTR ("Ds_exec_begin_op", walk_state);


	op = walk_state->op;
	if (!op) {
		status = acpi_ds_load2_begin_op (walk_state, out_op);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		op = *out_op;
		walk_state->op = op;
		walk_state->op_info = acpi_ps_get_opcode_info (op->opcode);
		walk_state->opcode = op->opcode;
	}

	if (op == walk_state->origin) {
		if (out_op) {
			*out_op = op;
		}

		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * If the previous opcode was a conditional, this opcode
	 * must be the beginning of the associated predicate.
	 * Save this knowledge in the current scope descriptor
	 */
	if ((walk_state->control_state) &&
		(walk_state->control_state->common.state ==
			CONTROL_CONDITIONAL_EXECUTING)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Exec predicate Op=%p State=%p\n",
				  op, walk_state));

		walk_state->control_state->common.state = CONTROL_PREDICATE_EXECUTING;

		/* Save start of predicate */

		walk_state->control_state->control.predicate_op = op;
	}


	opcode_class = walk_state->op_info->class;

	/* We want to send namepaths to the load code */

	if (op->opcode == AML_INT_NAMEPATH_OP) {
		opcode_class = AML_CLASS_NAMED_OBJECT;
	}

	/*
	 * Handle the opcode based upon the opcode type
	 */
	switch (opcode_class) {
	case AML_CLASS_CONTROL:

		status = acpi_ds_result_stack_push (walk_state);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		status = acpi_ds_exec_begin_control_op (walk_state, op);
		break;


	case AML_CLASS_NAMED_OBJECT:

		if (walk_state->walk_type == WALK_METHOD) {
			/*
			 * Found a named object declaration during method
			 * execution;  we must enter this object into the
			 * namespace.  The created object is temporary and
			 * will be deleted upon completion of the execution
			 * of this method.
			 */
			status = acpi_ds_load2_begin_op (walk_state, NULL);
		}


		if (op->opcode == AML_REGION_OP) {
			status = acpi_ds_result_stack_push (walk_state);
		}

		break;


	/* most operators with arguments */

	case AML_CLASS_EXECUTE:
	case AML_CLASS_CREATE:

		/* Start a new result/operand state */

		status = acpi_ds_result_stack_push (walk_state);
		break;


	default:
		break;
	}

	/* Nothing to do here during method execution */

	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ds_exec_end_op
 *
 * PARAMETERS:  Walk_state      - Current state of the parse tree walk
 *              Op              - Op that has been just been completed in the
 *                                walk;  Arguments have now been evaluated.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Ascending callback used during the execution of control
 *              methods.  The only thing we really need to do here is to
 *              notice the beginning of IF, ELSE, and WHILE blocks.
 *
 ****************************************************************************/

acpi_status
acpi_ds_exec_end_op (
	acpi_walk_state         *walk_state)
{
	acpi_parse_object       *op;
	acpi_status             status = AE_OK;
	u32                     op_type;
	u32                     op_class;
	acpi_parse_object       *next_op;
	acpi_parse_object       *first_arg;
	u32                     i;


	FUNCTION_TRACE_PTR ("Ds_exec_end_op", walk_state);


	op      = walk_state->op;
	op_type = walk_state->op_info->type;
	op_class = walk_state->op_info->class;

	if (op_class == AML_CLASS_UNKNOWN) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown opcode %X\n", op->opcode));
		return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
	}

	first_arg = op->value.arg;

	/* Init the walk state */

	walk_state->num_operands = 0;
	walk_state->return_desc = NULL;
	walk_state->result_obj = NULL;


	/* Call debugger for single step support (DEBUG build only) */

	DEBUGGER_EXEC (status = acpi_db_single_step (walk_state, op, op_class));
	DEBUGGER_EXEC (if (ACPI_FAILURE (status)) {return_ACPI_STATUS (status);});


	switch (op_class) {
	/* Decode the Opcode Class */

	case AML_CLASS_ARGUMENT: /* constants, literals, etc.  do nothing */
		break;

	/* most operators with arguments */

	case AML_CLASS_EXECUTE:

		/* Build resolved operand stack */

		status = acpi_ds_create_operands (walk_state, first_arg);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* Done with this result state (Now that operand stack is built) */

		status = acpi_ds_result_stack_pop (walk_state);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* Resolve all operands */

		status = acpi_ex_resolve_operands (walk_state->opcode,
				  &(walk_state->operands [walk_state->num_operands -1]),
				  walk_state);
		if (ACPI_FAILURE (status)) {
			/* TBD: must pop and delete operands */

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "[%s]: Could not resolve operands, %s\n",
				acpi_ps_get_opcode_name (walk_state->opcode), acpi_format_exception (status)));

			/*
			 * On error, we must delete all the operands and clear the
			 * operand stack
			 */
			for (i = 0; i < walk_state->num_operands; i++) {
				acpi_ut_remove_reference (walk_state->operands[i]);
				walk_state->operands[i] = NULL;
			}

			walk_state->num_operands = 0;
			goto cleanup;
		}

		DUMP_OPERANDS (WALK_OPERANDS, IMODE_EXECUTE, acpi_ps_get_opcode_name (walk_state->opcode),
				  walk_state->num_operands, "after Ex_resolve_operands");

		/*
		 * Dispatch the request to the appropriate interpreter handler
		 * routine.  There is one routine per opcode "type" based upon the
		 * number of opcode arguments and return type.
		 */
		status = acpi_gbl_op_type_dispatch [op_type] (walk_state);


		/* Delete argument objects and clear the operand stack */

		for (i = 0; i < walk_state->num_operands; i++) {
			/*
			 * Remove a reference to all operands, including both
			 * "Arguments" and "Targets".
			 */
			acpi_ut_remove_reference (walk_state->operands[i]);
			walk_state->operands[i] = NULL;
		}

		walk_state->num_operands = 0;

		/*
		 * If a result object was returned from above, push it on the
		 * current result stack
		 */
		if (ACPI_SUCCESS (status) &&
			walk_state->result_obj) {
			status = acpi_ds_result_push (walk_state->result_obj, walk_state);
		}

		break;


	default:

		switch (op_type) {
		case AML_TYPE_CONTROL:    /* Type 1 opcode, IF/ELSE/WHILE/NOOP */

			/* 1 Operand, 0 External_result, 0 Internal_result */

			status = acpi_ds_exec_end_control_op (walk_state, op);

			acpi_ds_result_stack_pop (walk_state);
			break;


		case AML_TYPE_METHOD_CALL:

			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Method invocation, Op=%p\n", op));

			/*
			 * (AML_METHODCALL) Op->Value->Arg->Node contains
			 * the method Node pointer
			 */
			/* Next_op points to the op that holds the method name */

			next_op = first_arg;

			/* Next_op points to first argument op */

			next_op = next_op->next;

			/*
			 * Get the method's arguments and put them on the operand stack
			 */
			status = acpi_ds_create_operands (walk_state, next_op);
			if (ACPI_FAILURE (status)) {
				break;
			}

			/*
			 * Since the operands will be passed to another
			 * control method, we must resolve all local
			 * references here (Local variables, arguments
			 * to *this* method, etc.)
			 */
			status = acpi_ds_resolve_operands (walk_state);
			if (ACPI_FAILURE (status)) {
				break;
			}

			/*
			 * Tell the walk loop to preempt this running method and
			 * execute the new method
			 */
			status = AE_CTRL_TRANSFER;

			/*
			 * Return now; we don't want to disturb anything,
			 * especially the operand count!
			 */
			return_ACPI_STATUS (status);
			break;


		case AML_TYPE_CREATE_FIELD:

			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
				"Executing Create_field Buffer/Index Op=%p\n", op));

			status = acpi_ds_load2_end_op (walk_state);
			if (ACPI_FAILURE (status)) {
				break;
			}

			status = acpi_ds_eval_buffer_field_operands (walk_state, op);
			break;


		case AML_TYPE_NAMED_FIELD:
		case AML_TYPE_NAMED_COMPLEX:
		case AML_TYPE_NAMED_SIMPLE:

			status = acpi_ds_load2_end_op (walk_state);
			if (ACPI_FAILURE (status)) {
				break;
			}

			if (op->opcode == AML_REGION_OP) {
				ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
					"Executing Op_region Address/Length Op=%p\n", op));

				status = acpi_ds_eval_region_operands (walk_state, op);
				if (ACPI_FAILURE (status)) {
					break;
				}

				status = acpi_ds_result_stack_pop (walk_state);
			}

			break;

		case AML_TYPE_UNDEFINED:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Undefined opcode type Op=%p\n", op));
			return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
			break;


		case AML_TYPE_BOGUS:
			ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH, "Internal opcode=%X type Op=%p\n",
				walk_state->opcode, op));
			break;

		default:

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Unimplemented opcode, class=%X type=%X Opcode=%X Op=%p\n",
				op_class, op_type, op->opcode, op));

			status = AE_NOT_IMPLEMENTED;
			break;
		}
	}

	/*
	 * ACPI 2.0 support for 64-bit integers:
	 * Truncate numeric result value if we are executing from a 32-bit ACPI table
	 */
	acpi_ex_truncate_for32bit_table (walk_state->result_obj, walk_state);

	/*
	 * Check if we just completed the evaluation of a
	 * conditional predicate
	 */

	if ((walk_state->control_state) &&
		(walk_state->control_state->common.state ==
			CONTROL_PREDICATE_EXECUTING) &&
		(walk_state->control_state->control.predicate_op == op)) {
		status = acpi_ds_get_predicate_value (walk_state, (u32) walk_state->result_obj);
		walk_state->result_obj = NULL;
	}


cleanup:
	if (walk_state->result_obj) {
		/* Break to debugger to display result */

		DEBUGGER_EXEC (acpi_db_display_result_object (walk_state->result_obj, walk_state));

		/*
		 * Delete the result op if and only if:
		 * Parent will not use the result -- such as any
		 * non-nested type2 op in a method (parent will be method)
		 */
		acpi_ds_delete_result_if_not_used (op, walk_state->result_obj, walk_state);
	}

	/* Always clear the object stack */

	/* TBD: [Investigate] Clear stack of return value,
	but don't delete it */
	walk_state->num_operands = 0;

	return_ACPI_STATUS (status);
}


