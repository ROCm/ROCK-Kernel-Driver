/*******************************************************************************
 *
 * Module Name: dbxface - AML Debugger external interfaces
 *              $Revision: 37 $
 *
 ******************************************************************************/

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
#include "acnamesp.h"
#include "acparser.h"
#include "acevents.h"
#include "acinterp.h"
#include "acdebug.h"


#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_single_step
 *
 * PARAMETERS:  Walk_state      - Current walk
 *              Op              - Current executing op
 *              Op_type         - Type of the current AML Opcode
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called just before execution of an AML opcode.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_db_single_step (
	ACPI_WALK_STATE         *walk_state,
	ACPI_PARSE_OBJECT       *op,
	u8                      op_type)
{
	ACPI_PARSE_OBJECT       *next;
	ACPI_STATUS             status = AE_OK;
	u32                     original_debug_level;
	ACPI_PARSE_OBJECT       *display_op;


	/* Is there a breakpoint set? */

	if (walk_state->method_breakpoint) {
		/* Check if the breakpoint has been reached or passed */

		if (walk_state->method_breakpoint <= op->aml_offset) {
			/* Hit the breakpoint, resume single step, reset breakpoint */

			acpi_os_printf ("***Break*** at AML offset %X\n", op->aml_offset);
			acpi_gbl_cm_single_step = TRUE;
			acpi_gbl_step_to_next_call = FALSE;
			walk_state->method_breakpoint = 0;
		}
	}


	/*
	 * Check if this is an opcode that we are interested in --
	 * namely, opcodes that have arguments
	 */

	if (op->opcode == AML_INT_NAMEDFIELD_OP) {
		return (AE_OK);
	}

	switch (op_type) {
	case OPTYPE_UNDEFINED:
	case OPTYPE_CONSTANT:           /* argument type only */
	case OPTYPE_LITERAL:            /* argument type only */
	case OPTYPE_DATA_TERM:          /* argument type only */
	case OPTYPE_LOCAL_VARIABLE:     /* argument type only */
	case OPTYPE_METHOD_ARGUMENT:    /* argument type only */
		return (AE_OK);
		break;

	case OPTYPE_NAMED_OBJECT:
		switch (op->opcode) {
		case AML_INT_NAMEPATH_OP:
			return (AE_OK);
			break;
		}
	}


	/*
	 * Under certain debug conditions, display this opcode and its operands
	 */

	if ((output_to_file)                    ||
		(acpi_gbl_cm_single_step)           ||
		(acpi_dbg_level & TRACE_PARSE)) {
		if ((output_to_file)                ||
			(acpi_dbg_level & TRACE_PARSE)) {
			acpi_os_printf ("\n[Aml_debug] Next AML Opcode to execute:\n");
		}

		/*
		 * Display this op (and only this op - zero out the NEXT field temporarily,
		 * and disable parser trace output for the duration of the display because
		 * we don't want the extraneous debug output)
		 */

		original_debug_level = acpi_dbg_level;
		acpi_dbg_level &= ~(TRACE_PARSE | TRACE_FUNCTIONS);
		next = op->next;
		op->next = NULL;


		display_op = op;
		if (op->parent) {
			if ((op->parent->opcode == AML_IF_OP) ||
				(op->parent->opcode == AML_WHILE_OP)) {
				display_op = op->parent;
			}
		}

		/* Now we can display it */

		acpi_db_display_op (walk_state, display_op, ACPI_UINT32_MAX);

		if ((op->opcode == AML_IF_OP) ||
			(op->opcode == AML_WHILE_OP)) {
			if (walk_state->control_state->common.value) {
				acpi_os_printf ("Predicate was TRUE, executed block\n");
			}
			else {
				acpi_os_printf ("Predicate is FALSE, skipping block\n");
			}
		}

		else if (op->opcode == AML_ELSE_OP) {
			/* TBD */
		}


		/* Restore everything */

		op->next = next;
		acpi_os_printf ("\n");
		acpi_dbg_level = original_debug_level;
   }


	/* If we are not single stepping, just continue executing the method */

	if (!acpi_gbl_cm_single_step) {
		return (AE_OK);
	}


	/*
	 * If we are executing a step-to-call command,
	 * Check if this is a method call.
	 */

	if (acpi_gbl_step_to_next_call) {
		if (op->opcode != AML_INT_METHODCALL_OP) {
			/* Not a method call, just keep executing */

			return (AE_OK);
		}

		/* Found a method call, stop executing */

		acpi_gbl_step_to_next_call = FALSE;
	}


	/*
	 * If the next opcode is a method call, we will "step over" it
	 * by default.
	 */

	if (op->opcode == AML_INT_METHODCALL_OP) {
		acpi_gbl_cm_single_step = FALSE; /* No more single step while executing called method */

		/* Set the breakpoint on the call, it will stop execution as soon as we return */

		/* TBD: [Future] don't kill the user breakpoint! */

		walk_state->method_breakpoint = /* Op->Aml_offset + */ 1; /* Must be non-zero! */
	}


	/* TBD: [Investigate] what are the namespace locking issues here */

	/* Acpi_ut_release_mutex (ACPI_MTX_NAMESPACE); */

	/* Go into the command loop and await next user command */

	acpi_gbl_method_executing = TRUE;
	status = AE_CTRL_TRUE;
	while (status == AE_CTRL_TRUE) {
		if (acpi_gbl_debugger_configuration == DEBUGGER_MULTI_THREADED) {
			/* Handshake with the front-end that gets user command lines */

			acpi_ut_release_mutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
			acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_READY);
		}

		else {
			/* Single threaded, we must get a command line ourselves */

			/* Force output to console until a command is entered */

			acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);

			/* Different prompt if method is executing */

			if (!acpi_gbl_method_executing) {
				acpi_os_printf ("%1c ", DB_COMMAND_PROMPT);
			}
			else {
				acpi_os_printf ("%1c ", DB_EXECUTE_PROMPT);
			}

			/* Get the user input line */

			acpi_os_get_line (line_buf);
		}

		status = acpi_db_command_dispatch (line_buf, walk_state, op);
	}

	/* Acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE); */


	/* User commands complete, continue execution of the interrupted method */

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Init and start debugger
 *
 ******************************************************************************/

int
acpi_db_initialize (void)
{


	/* Init globals */

	buffer = acpi_os_allocate (BUFFER_SIZE);

	/* Initial scope is the root */

	scope_buf [0] = '\\';
	scope_buf [1] = 0;


	/*
	 * If configured for multi-thread support, the debug executor runs in
	 * a separate thread so that the front end can be in another address
	 * space, environment, or even another machine.
	 */

	if (acpi_gbl_debugger_configuration & DEBUGGER_MULTI_THREADED) {
		/* These were created with one unit, grab it */

		acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
		acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_READY);

		/* Create the debug execution thread to execute commands */

		acpi_os_queue_for_execution (0, acpi_db_execute_thread, NULL);
	}

	if (!opt_verbose) {
		INDENT_STRING = "    ";
		opt_disasm = TRUE;
		opt_stats = FALSE;
	}


	return (0);
}


#endif /* ENABLE_DEBUGGER */
