/*******************************************************************************
 *
 * Module Name: dbxface - AML Debugger external interfaces
 *              $Revision: 61 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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
#include "acdebug.h"
#include "acdisasm.h"


#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 ACPI_MODULE_NAME    ("dbxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_single_step
 *
 * PARAMETERS:  Walk_state      - Current walk
 *              Op              - Current executing op
 *              Opcode_class    - Class of the current AML Opcode
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called just before execution of an AML opcode.
 *
 ******************************************************************************/

acpi_status
acpi_db_single_step (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	u32                     opcode_class)
{
	acpi_parse_object       *next;
	acpi_status             status = AE_OK;
	u32                     original_debug_level;
	acpi_parse_object       *display_op;
	acpi_parse_object       *parent_op;


	ACPI_FUNCTION_ENTRY ();

	/* Check for single-step breakpoint */

	if (walk_state->method_breakpoint &&
	   (walk_state->method_breakpoint <= op->common.aml_offset)) {
		/* Check if the breakpoint has been reached or passed */
		/* Hit the breakpoint, resume single step, reset breakpoint */

		acpi_os_printf ("***Break*** at AML offset %X\n", op->common.aml_offset);
		acpi_gbl_cm_single_step = TRUE;
		acpi_gbl_step_to_next_call = FALSE;
		walk_state->method_breakpoint = 0;
	}

	/* Check for user breakpoint (Must be on exact Aml offset) */

	else if (walk_state->user_breakpoint &&
			(walk_state->user_breakpoint == op->common.aml_offset)) {
		acpi_os_printf ("***User_breakpoint*** at AML offset %X\n", op->common.aml_offset);
		acpi_gbl_cm_single_step = TRUE;
		acpi_gbl_step_to_next_call = FALSE;
		walk_state->method_breakpoint = 0;
	}


	/*
	 * Check if this is an opcode that we are interested in --
	 * namely, opcodes that have arguments
	 */
	if (op->common.aml_opcode == AML_INT_NAMEDFIELD_OP) {
		return (AE_OK);
	}

	switch (opcode_class) {
	case AML_CLASS_UNKNOWN:
	case AML_CLASS_ARGUMENT:    /* constants, literals, etc.  do nothing */
		return (AE_OK);

	default:
		/* All other opcodes -- continue */
		break;
	}

	/*
	 * Under certain debug conditions, display this opcode and its operands
	 */
	if ((acpi_gbl_db_output_to_file)        ||
		(acpi_gbl_cm_single_step)           ||
		(acpi_dbg_level & ACPI_LV_PARSE)) {
		if ((acpi_gbl_db_output_to_file)    ||
			(acpi_dbg_level & ACPI_LV_PARSE)) {
			acpi_os_printf ("\n[Aml_debug] Next AML Opcode to execute:\n");
		}

		/*
		 * Display this op (and only this op - zero out the NEXT field temporarily,
		 * and disable parser trace output for the duration of the display because
		 * we don't want the extraneous debug output)
		 */
		original_debug_level = acpi_dbg_level;
		acpi_dbg_level &= ~(ACPI_LV_PARSE | ACPI_LV_FUNCTIONS);
		next = op->common.next;
		op->common.next = NULL;


		display_op = op;
		parent_op = op->common.parent;
		if (parent_op) {
			if ((walk_state->control_state) &&
				(walk_state->control_state->common.state == ACPI_CONTROL_PREDICATE_EXECUTING)) {
				/*
				 * We are executing the predicate of an IF or WHILE statement
				 * Search upwards for the containing IF or WHILE so that the
				 * entire predicate can be displayed.
				 */
				while (parent_op) {
					if ((parent_op->common.aml_opcode == AML_IF_OP) ||
						(parent_op->common.aml_opcode == AML_WHILE_OP)) {
						display_op = parent_op;
						break;
					}
					parent_op = parent_op->common.parent;
				}
			}
			else {
				while (parent_op) {
					if ((parent_op->common.aml_opcode == AML_IF_OP)   ||
						(parent_op->common.aml_opcode == AML_ELSE_OP) ||
						(parent_op->common.aml_opcode == AML_SCOPE_OP) ||
						(parent_op->common.aml_opcode == AML_METHOD_OP) ||
						(parent_op->common.aml_opcode == AML_WHILE_OP)) {
						break;
					}
					display_op = parent_op;
					parent_op = parent_op->common.parent;
				}
			}
		}

		/* Now we can display it */

		acpi_dm_disassemble (walk_state, display_op, ACPI_UINT32_MAX);

		if ((op->common.aml_opcode == AML_IF_OP) ||
			(op->common.aml_opcode == AML_WHILE_OP)) {
			if (walk_state->control_state->common.value) {
				acpi_os_printf ("Predicate = [True], IF block was executed\n");
			}
			else {
				acpi_os_printf ("Predicate = [False], Skipping IF block\n");
			}
		}

		else if (op->common.aml_opcode == AML_ELSE_OP) {
			acpi_os_printf ("Predicate = [False], ELSE block was executed\n");
		}

		/* Restore everything */

		op->common.next = next;
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
		if (op->common.aml_opcode != AML_INT_METHODCALL_OP) {
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
	if (op->common.aml_opcode == AML_INT_METHODCALL_OP) {
		acpi_gbl_cm_single_step = FALSE; /* No more single step while executing called method */

		/* Set the breakpoint on/before the call, it will stop execution as soon as we return */

		walk_state->method_breakpoint = 1; /* Must be non-zero! */
	}


	/* TBD: [Investigate] what are the namespace locking issues here */

	/* Acpi_ut_release_mutex (ACPI_MTX_NAMESPACE); */

	/* Go into the command loop and await next user command */

	acpi_gbl_method_executing = TRUE;
	status = AE_CTRL_TRUE;
	while (status == AE_CTRL_TRUE) {
		if (acpi_gbl_debugger_configuration == DEBUGGER_MULTI_THREADED) {
			/* Handshake with the front-end that gets user command lines */

			status = acpi_ut_release_mutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
			if (ACPI_FAILURE (status)) {
				return (status);
			}
			status = acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_READY);
			if (ACPI_FAILURE (status)) {
				return (status);
			}
		}

		else {
			/* Single threaded, we must get a command line ourselves */

			/* Force output to console until a command is entered */

			acpi_db_set_output_destination (ACPI_DB_CONSOLE_OUTPUT);

			/* Different prompt if method is executing */

			if (!acpi_gbl_method_executing) {
				acpi_os_printf ("%1c ", ACPI_DEBUGGER_COMMAND_PROMPT);
			}
			else {
				acpi_os_printf ("%1c ", ACPI_DEBUGGER_EXECUTE_PROMPT);
			}

			/* Get the user input line */

			(void) acpi_os_get_line (acpi_gbl_db_line_buf);
		}

		status = acpi_db_command_dispatch (acpi_gbl_db_line_buf, walk_state, op);
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

acpi_status
acpi_db_initialize (void)
{
	acpi_status             status;


	/* Init globals */

	acpi_gbl_db_buffer          = NULL;
	acpi_gbl_db_filename        = NULL;
	acpi_gbl_db_output_to_file  = FALSE;

	acpi_gbl_db_debug_level     = ACPI_LV_VERBOSITY2;
	acpi_gbl_db_console_debug_level = NORMAL_DEFAULT | ACPI_LV_TABLES;
	acpi_gbl_db_output_flags    = ACPI_DB_CONSOLE_OUTPUT;

	acpi_gbl_db_opt_tables      = FALSE;
	acpi_gbl_db_opt_disasm      = FALSE;
	acpi_gbl_db_opt_stats       = FALSE;
	acpi_gbl_db_opt_verbose     = TRUE;
	acpi_gbl_db_opt_ini_methods = TRUE;

	acpi_gbl_db_buffer = acpi_os_allocate (ACPI_DEBUG_BUFFER_SIZE);
	if (!acpi_gbl_db_buffer) {
		return (AE_NO_MEMORY);
	}
	ACPI_MEMSET (acpi_gbl_db_buffer, 0, ACPI_DEBUG_BUFFER_SIZE);

	/* Initial scope is the root */

	acpi_gbl_db_scope_buf [0] = '\\';
	acpi_gbl_db_scope_buf [1] = 0;
	acpi_gbl_db_scope_node = acpi_gbl_root_node;

	/*
	 * If configured for multi-thread support, the debug executor runs in
	 * a separate thread so that the front end can be in another address
	 * space, environment, or even another machine.
	 */
	if (acpi_gbl_debugger_configuration & DEBUGGER_MULTI_THREADED) {
		/* These were created with one unit, grab it */

		status = acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
		if (ACPI_FAILURE (status)) {
			acpi_os_printf ("Could not get debugger mutex\n");
			return (status);
		}
		status = acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_READY);
		if (ACPI_FAILURE (status)) {
			acpi_os_printf ("Could not get debugger mutex\n");
			return (status);
		}

		/* Create the debug execution thread to execute commands */

		status = acpi_os_queue_for_execution (0, acpi_db_execute_thread, NULL);
		if (ACPI_FAILURE (status)) {
			acpi_os_printf ("Could not start debugger thread\n");
			return (status);
		}
	}

	if (!acpi_gbl_db_opt_verbose) {
		acpi_gbl_db_opt_disasm = TRUE;
		acpi_gbl_db_opt_stats = FALSE;
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_terminate
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Stop debugger
 *
 ******************************************************************************/

void
acpi_db_terminate (void)
{

	if (acpi_gbl_db_table_ptr) {
		acpi_os_free (acpi_gbl_db_table_ptr);
	}
	if (acpi_gbl_db_buffer) {
		acpi_os_free (acpi_gbl_db_buffer);
	}
}


#endif /* ENABLE_DEBUGGER */
