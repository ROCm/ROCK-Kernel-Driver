/*******************************************************************************
 *
 * Module Name: dbinput - user front-end to the AML debugger
 *              $Revision: 72 $
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
#include "actables.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "acdebug.h"


#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbinput")


/*
 * Globals that are specific to the debugger
 */

NATIVE_CHAR                 acpi_gbl_db_line_buf[80];
NATIVE_CHAR                 acpi_gbl_db_parsed_buf[80];
NATIVE_CHAR                 acpi_gbl_db_scope_buf[40];
NATIVE_CHAR                 acpi_gbl_db_debug_filename[40];
NATIVE_CHAR                 *acpi_gbl_db_args[DB_MAX_ARGS];
NATIVE_CHAR                 *acpi_gbl_db_buffer = NULL;
NATIVE_CHAR                 *acpi_gbl_db_filename = NULL;
u8                          acpi_gbl_db_output_to_file = FALSE;

u32                         acpi_gbl_db_debug_level = ACPI_LV_VERBOSITY2;
u32                         acpi_gbl_db_console_debug_level = NORMAL_DEFAULT | ACPI_LV_TABLES;
u8                          acpi_gbl_db_output_flags = DB_CONSOLE_OUTPUT;


u8                          acpi_gbl_db_opt_tables    = FALSE;
u8                          acpi_gbl_db_opt_disasm    = FALSE;
u8                          acpi_gbl_db_opt_stats     = FALSE;
u8                          acpi_gbl_db_opt_parse_jit = FALSE;
u8                          acpi_gbl_db_opt_verbose   = TRUE;
u8                          acpi_gbl_db_opt_ini_methods = TRUE;

/*
 * Statistic globals
 */
u16                         acpi_gbl_obj_type_count[INTERNAL_TYPE_NODE_MAX+1];
u16                         acpi_gbl_node_type_count[INTERNAL_TYPE_NODE_MAX+1];
u16                         acpi_gbl_obj_type_count_misc;
u16                         acpi_gbl_node_type_count_misc;
u32                         acpi_gbl_num_nodes;
u32                         acpi_gbl_num_objects;


u32                         acpi_gbl_size_of_parse_tree;
u32                         acpi_gbl_size_of_method_trees;
u32                         acpi_gbl_size_of_node_entries;
u32                         acpi_gbl_size_of_acpi_objects;

/*
 * Top-level debugger commands.
 *
 * This list of commands must match the string table below it
 */

enum acpi_ex_debugger_commands
{
	CMD_NOT_FOUND = 0,
	CMD_NULL,
	CMD_ALLOCATIONS,
	CMD_ARGS,
	CMD_ARGUMENTS,
	CMD_BREAKPOINT,
	CMD_CALL,
	CMD_CLOSE,
	CMD_DEBUG,
	CMD_DUMP,
	CMD_ENABLEACPI,
	CMD_EVENT,
	CMD_EXECUTE,
	CMD_EXIT,
	CMD_FIND,
	CMD_GO,
	CMD_HELP,
	CMD_HELP2,
	CMD_HISTORY,
	CMD_HISTORY_EXE,
	CMD_HISTORY_LAST,
	CMD_INFORMATION,
	CMD_INTO,
	CMD_LEVEL,
	CMD_LIST,
	CMD_LOAD,
	CMD_LOCALS,
	CMD_LOCKS,
	CMD_METHODS,
	CMD_NAMESPACE,
	CMD_NOTIFY,
	CMD_OBJECT,
	CMD_OPEN,
	CMD_OWNER,
	CMD_PREFIX,
	CMD_QUIT,
	CMD_REFERENCES,
	CMD_RESOURCES,
	CMD_RESULTS,
	CMD_SET,
	CMD_STATS,
	CMD_STOP,
	CMD_TABLES,
	CMD_TERMINATE,
	CMD_THREADS,
	CMD_TREE,
	CMD_UNLOAD
};

#define CMD_FIRST_VALID     2


const COMMAND_INFO          acpi_gbl_db_commands[] =
{ {"<NOT FOUND>",  0},
	{"<NULL>",       0},
	{"ALLOCATIONS",  0},
	{"ARGS",         0},
	{"ARGUMENTS",    0},
	{"BREAKPOINT",   1},
	{"CALL",         0},
	{"CLOSE",        0},
	{"DEBUG",        1},
	{"DUMP",         1},
	{"ENABLEACPI",   0},
	{"EVENT",        1},
	{"EXECUTE",      1},
	{"EXIT",         0},
	{"FIND",         1},
	{"GO",           0},
	{"HELP",         0},
	{"?",            0},
	{"HISTORY",      0},
	{"!",            1},
	{"!!",           0},
	{"INFORMATION",  0},
	{"INTO",         0},
	{"LEVEL",        0},
	{"LIST",         0},
	{"LOAD",         1},
	{"LOCALS",       0},
	{"LOCKS",        0},
	{"METHODS",      0},
	{"NAMESPACE",    0},
	{"NOTIFY",       2},
	{"OBJECT",       1},
	{"OPEN",         1},
	{"OWNER",        1},
	{"PREFIX",       0},
	{"QUIT",         0},
	{"REFERENCES",   1},
	{"RESOURCES",    1},
	{"RESULTS",      0},
	{"SET",          3},
	{"STATS",        0},
	{"STOP",         0},
	{"TABLES",       0},
	{"TERMINATE",    0},
	{"THREADS",      3},
	{"TREE",         0},
	{"UNLOAD",       1},
	{NULL,           0}
};


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_help
 *
 * PARAMETERS:  Help_type       - Subcommand (optional)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a usage message.
 *
 ******************************************************************************/

void
acpi_db_display_help (
	NATIVE_CHAR             *help_type)
{


	/* No parameter, just give the overview */

	if (!help_type)
	{
		acpi_os_printf ("ACPI CA Debugger Commands\n\n");
		acpi_os_printf ("The following classes of commands are available. Help is available for\n");
		acpi_os_printf ("each class by entering \"Help <Class_name>\"\n\n");
		acpi_os_printf ("  [GENERAL]       General-Purpose Commands\n");
		acpi_os_printf ("  [NAMESPACE]     Namespace Access Commands\n");
		acpi_os_printf ("  [METHOD]        Control Method Execution Commands\n");
		acpi_os_printf ("  [FILE]          File I/O Commands\n");
		return;

	}


	/*
	 * Parameter is the command class
	 *
	 * The idea here is to keep each class of commands smaller than a screenful
	 */

	switch (help_type[0])
	{
	case 'G':
		acpi_os_printf ("\n_general-Purpose Commands\n\n");
		acpi_os_printf ("Allocations                       Display list of current memory allocations\n");
		acpi_os_printf ("Dump <Address>|<Namepath>\n");
		acpi_os_printf ("   [Byte|Word|Dword|Qword]        Display ACPI objects or memory\n");
		acpi_os_printf ("Enable_acpi                       Enable ACPI (hardware) mode\n");
		acpi_os_printf ("Help                              This help screen\n");
		acpi_os_printf ("History                           Display command history buffer\n");
		acpi_os_printf ("Level [<Debug_level>] [console]   Get/Set debug level for file or console\n");
		acpi_os_printf ("Locks                             Current status of internal mutexes\n");
		acpi_os_printf ("Quit or Exit                      Exit this command\n");
		acpi_os_printf ("Stats [Allocations|Memory|Misc\n");
		acpi_os_printf ("     |Objects|Tables]             Display namespace and memory statistics\n");
		acpi_os_printf ("Tables                            Display info about loaded ACPI tables\n");
		acpi_os_printf ("Unload <Table_sig> [Instance]     Unload an ACPI table\n");
		acpi_os_printf ("! <Command_number>                Execute command from history buffer\n");
		acpi_os_printf ("!!                                Execute last command again\n");
		return;

	case 'N':
		acpi_os_printf ("\n_namespace Access Commands\n\n");
		acpi_os_printf ("Debug <Namepath> [Arguments]      Single Step a control method\n");
		acpi_os_printf ("Event <F|G> <Value>               Generate Acpi_event (Fixed/GPE)\n");
		acpi_os_printf ("Execute <Namepath> [Arguments]    Execute control method\n");
		acpi_os_printf ("Find <Name> (? is wildcard)       Find ACPI name(s) with wildcards\n");
		acpi_os_printf ("Method                            Display list of loaded control methods\n");
		acpi_os_printf ("Namespace [<Addr>|<Path>] [Depth] Display loaded namespace tree/subtree\n");
		acpi_os_printf ("Notify <Name_path> <Value>        Send a notification\n");
		acpi_os_printf ("Objects <Object_type>             Display all objects of the given type\n");
		acpi_os_printf ("Owner <Owner_id> [Depth]          Display loaded namespace by object owner\n");
		acpi_os_printf ("Prefix [<Name_path>]              Set or Get current execution prefix\n");
		acpi_os_printf ("References <Addr>                 Find all references to object at addr\n");
		acpi_os_printf ("Resources xxx                     Get and display resources\n");
		acpi_os_printf ("Terminate                         Delete namespace and all internal objects\n");
		acpi_os_printf ("Thread <Threads><Loops><Name_path> Spawn threads to execute method(s)\n");
		return;

	case 'M':
		acpi_os_printf ("\n_control Method Execution Commands\n\n");
		acpi_os_printf ("Arguments (or Args)               Display method arguments\n");
		acpi_os_printf ("Breakpoint <Aml_offset>           Set an AML execution breakpoint\n");
		acpi_os_printf ("Call                              Run to next control method invocation\n");
		acpi_os_printf ("Go                                Allow method to run to completion\n");
		acpi_os_printf ("Information                       Display info about the current method\n");
		acpi_os_printf ("Into                              Step into (not over) a method call\n");
		acpi_os_printf ("List [# of Aml Opcodes]           Display method ASL statements\n");
		acpi_os_printf ("Locals                            Display method local variables\n");
		acpi_os_printf ("Results                           Display method result stack\n");
		acpi_os_printf ("Set <A|L> <#> <Value>             Set method data (Arguments/Locals)\n");
		acpi_os_printf ("Stop                              Terminate control method\n");
		acpi_os_printf ("Tree                              Display control method calling tree\n");
		acpi_os_printf ("<Enter>                           Single step next AML opcode (over calls)\n");
		return;

	case 'F':
		acpi_os_printf ("\n_file I/O Commands\n\n");
		acpi_os_printf ("Close                             Close debug output file\n");
		acpi_os_printf ("Open <Output Filename>            Open a file for debug output\n");
		acpi_os_printf ("Load <Input Filename>             Load ACPI table from a file\n");
		return;

	default:
		acpi_os_printf ("Unrecognized Command Class: %x\n", help_type);
		return;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_get_next_token
 *
 * PARAMETERS:  String          - Command buffer
 *              Next            - Return value, end of next token
 *
 * RETURN:      Pointer to the start of the next token.
 *
 * DESCRIPTION: Command line parsing.  Get the next token on the command line
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_db_get_next_token (
	NATIVE_CHAR             *string,
	NATIVE_CHAR             **next)
{
	NATIVE_CHAR             *start;

	/* At end of buffer? */

	if (!string || !(*string))
	{
		return (NULL);
	}


	/* Get rid of any spaces at the beginning */

	if (*string == ' ')
	{
		while (*string && (*string == ' '))
		{
			string++;
		}

		if (!(*string))
		{
			return (NULL);
		}
	}

	start = string;

	/* Find end of token */

	while (*string && (*string != ' '))
	{
		string++;
	}


	if (!(*string))
	{
		*next = NULL;
	}

	else
	{
		*string = 0;
		*next = string + 1;
	}

	return (start);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_get_line
 *
 * PARAMETERS:  Input_buffer        - Command line buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get the next command line from the user.  Gets entire line
 *              up to the next newline
 *
 ******************************************************************************/

u32
acpi_db_get_line (
	NATIVE_CHAR             *input_buffer)
{
	u32                     i;
	u32                     count;
	NATIVE_CHAR             *next;
	NATIVE_CHAR             *this;


	STRCPY (acpi_gbl_db_parsed_buf, input_buffer);
	STRUPR (acpi_gbl_db_parsed_buf);

	this = acpi_gbl_db_parsed_buf;
	for (i = 0; i < DB_MAX_ARGS; i++)
	{
		acpi_gbl_db_args[i] = acpi_db_get_next_token (this, &next);
		if (!acpi_gbl_db_args[i])
		{
			break;
		}

		this = next;
	}


	/* Uppercase the actual command */

	if (acpi_gbl_db_args[0])
	{
		STRUPR (acpi_gbl_db_args[0]);
	}

	count = i;
	if (count)
	{
		count--;  /* Number of args only */
	}

	return (count);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_match_command
 *
 * PARAMETERS:  User_command            - User command line
 *
 * RETURN:      Index into command array, -1 if not found
 *
 * DESCRIPTION: Search command array for a command match
 *
 ******************************************************************************/

u32
acpi_db_match_command (
	NATIVE_CHAR             *user_command)
{
	u32                     i;


	if (!user_command || user_command[0] == 0)
	{
		return (CMD_NULL);
	}

	for (i = CMD_FIRST_VALID; acpi_gbl_db_commands[i].name; i++)
	{
		if (STRSTR (acpi_gbl_db_commands[i].name, user_command) == acpi_gbl_db_commands[i].name)
		{
			return (i);
		}
	}

	/* Command not recognized */

	return (CMD_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_command_dispatch
 *
 * PARAMETERS:  Input_buffer        - Command line buffer
 *              Walk_state          - Current walk
 *              Op                  - Current (executing) parse op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Command dispatcher.  Called from two places:
 *
 ******************************************************************************/

acpi_status
acpi_db_command_dispatch (
	NATIVE_CHAR             *input_buffer,
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	u32                     temp;
	u32                     command_index;
	u32                     param_count;
	NATIVE_CHAR             *command_line;
	acpi_status             status = AE_CTRL_TRUE;


	/* If Acpi_terminate has been called, terminate this thread */

	if (acpi_gbl_db_terminate_threads)
	{
		return (AE_CTRL_TERMINATE);
	}

	param_count = acpi_db_get_line (input_buffer);
	command_index = acpi_db_match_command (acpi_gbl_db_args[0]);
	temp = 0;

	/* Verify that we have the minimum number of params */

	if (param_count < acpi_gbl_db_commands[command_index].min_args)
	{
		acpi_os_printf ("%d parameters entered, [%s] requires %d parameters\n",
				  param_count, acpi_gbl_db_commands[command_index].name, acpi_gbl_db_commands[command_index].min_args);
		return (AE_CTRL_TRUE);
	}

	/* Decode and dispatch the command */

	switch (command_index)
	{
	case CMD_NULL:
		if (op)
		{
			return (AE_OK);
		}
		break;

	case CMD_ALLOCATIONS:

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		acpi_ut_dump_allocations ((u32) -1, NULL);
#endif
		break;

	case CMD_ARGS:
	case CMD_ARGUMENTS:
		acpi_db_display_arguments ();
		break;

	case CMD_BREAKPOINT:
		acpi_db_set_method_breakpoint (acpi_gbl_db_args[1], walk_state, op);
		break;

	case CMD_CALL:
		acpi_db_set_method_call_breakpoint (op);
		status = AE_OK;
		break;

	case CMD_CLOSE:
		acpi_db_close_debug_file ();
		break;

	case CMD_DEBUG:
		acpi_db_execute (acpi_gbl_db_args[1], &acpi_gbl_db_args[2], EX_SINGLE_STEP);
		break;

	case CMD_DUMP:
		acpi_db_decode_and_display_object (acpi_gbl_db_args[1], acpi_gbl_db_args[2]);
		break;

	case CMD_ENABLEACPI:
		status = acpi_enable();
		if (ACPI_FAILURE(status))
		{
			acpi_os_printf("Acpi_enable failed (Status=%X)\n", status);
			return (status);
		}
		break;

	case CMD_EVENT:
		acpi_os_printf ("Event command not implemented\n");
		break;

	case CMD_EXECUTE:
		acpi_db_execute (acpi_gbl_db_args[1], &acpi_gbl_db_args[2], EX_NO_SINGLE_STEP);
		break;

	case CMD_FIND:
		acpi_db_find_name_in_namespace (acpi_gbl_db_args[1]);
		break;

	case CMD_GO:
		acpi_gbl_cm_single_step = FALSE;
		return (AE_OK);

	case CMD_HELP:
	case CMD_HELP2:
		acpi_db_display_help (acpi_gbl_db_args[1]);
		break;

	case CMD_HISTORY:
		acpi_db_display_history ();
		break;

	case CMD_HISTORY_EXE:
		command_line = acpi_db_get_from_history (acpi_gbl_db_args[1]);
		if (!command_line)
		{
			return (AE_CTRL_TRUE);
		}

		status = acpi_db_command_dispatch (command_line, walk_state, op);
		if (ACPI_SUCCESS (status))
		{
			status = AE_CTRL_TRUE;
		}
		return (status);
		break;

	case CMD_HISTORY_LAST:
		command_line = acpi_db_get_from_history (NULL);
		if (!command_line)
		{
			return (AE_CTRL_TRUE);
		}

		status = acpi_db_command_dispatch (command_line, walk_state, op);
		if (ACPI_SUCCESS (status))
		{
			status = AE_CTRL_TRUE;
		}
		return (status);

	case CMD_INFORMATION:
		acpi_db_display_method_info (op);
		break;

	case CMD_INTO:
		if (op)
		{
			acpi_gbl_cm_single_step = TRUE;

/* TBD: Must get current walk state */
			/* Acpi_gbl_Method_breakpoint = 0; */
			return (AE_OK);
		}
		break;

	case CMD_LEVEL:
		if (param_count == 0)
		{
			acpi_os_printf ("Current debug level for file output is:  %8.8lX\n", acpi_gbl_db_debug_level);
			acpi_os_printf ("Current debug level for console output is: %8.8lX\n", acpi_gbl_db_console_debug_level);
		}
		else if (param_count == 2)
		{
			temp = acpi_gbl_db_console_debug_level;
			acpi_gbl_db_console_debug_level = STRTOUL (acpi_gbl_db_args[1], NULL, 16);
			acpi_os_printf ("Debug Level for console output was %8.8lX, now %8.8lX\n", temp, acpi_gbl_db_console_debug_level);
		}
		else
		{
			temp = acpi_gbl_db_debug_level;
			acpi_gbl_db_debug_level = STRTOUL (acpi_gbl_db_args[1], NULL, 16);
			acpi_os_printf ("Debug Level for file output was %8.8lX, now %8.8lX\n", temp, acpi_gbl_db_debug_level);
		}
		break;

	case CMD_LIST:
		acpi_db_disassemble_aml (acpi_gbl_db_args[1], op);
		break;

	case CMD_LOAD:
		status = acpi_db_load_acpi_table (acpi_gbl_db_args[1]);
		if (ACPI_FAILURE (status))
		{
			return (status);
		}
		break;

	case CMD_LOCKS:
		acpi_db_display_locks ();
		break;

	case CMD_LOCALS:
		acpi_db_display_locals ();
		break;

	case CMD_METHODS:
		acpi_db_display_objects ("METHOD", acpi_gbl_db_args[1]);
		break;

	case CMD_NAMESPACE:
		acpi_db_dump_namespace (acpi_gbl_db_args[1], acpi_gbl_db_args[2]);
		break;

	case CMD_NOTIFY:
		temp = STRTOUL (acpi_gbl_db_args[2], NULL, 0);
		acpi_db_send_notify (acpi_gbl_db_args[1], temp);
		break;

	case CMD_OBJECT:
		acpi_db_display_objects (STRUPR (acpi_gbl_db_args[1]), acpi_gbl_db_args[2]);
		break;

	case CMD_OPEN:
		acpi_db_open_debug_file (acpi_gbl_db_args[1]);
		break;

	case CMD_OWNER:
		acpi_db_dump_namespace_by_owner (acpi_gbl_db_args[1], acpi_gbl_db_args[2]);
		break;

	case CMD_PREFIX:
		acpi_db_set_scope (acpi_gbl_db_args[1]);
		break;

	case CMD_REFERENCES:
		acpi_db_find_references (acpi_gbl_db_args[1]);
		break;

	case CMD_RESOURCES:
		acpi_db_display_resources (acpi_gbl_db_args[1]);
		break;

	case CMD_RESULTS:
		acpi_db_display_results ();
		break;

	case CMD_SET:
		acpi_db_set_method_data (acpi_gbl_db_args[1], acpi_gbl_db_args[2], acpi_gbl_db_args[3]);
		break;

	case CMD_STATS:
		acpi_db_display_statistics (acpi_gbl_db_args[1]);
		break;

	case CMD_STOP:
		return (AE_AML_ERROR);
		break;

	case CMD_TABLES:
		acpi_db_display_table_info (acpi_gbl_db_args[1]);
		break;

	case CMD_TERMINATE:
		acpi_db_set_output_destination (DB_REDIRECTABLE_OUTPUT);
		acpi_ut_subsystem_shutdown ();

		/* TBD: [Restructure] Need some way to re-initialize without re-creating the semaphores! */

		/*  Acpi_initialize (NULL); */
		break;

	case CMD_THREADS:
		acpi_db_create_execution_threads (acpi_gbl_db_args[1], acpi_gbl_db_args[2], acpi_gbl_db_args[3]);
		break;

	case CMD_TREE:
		acpi_db_display_calling_tree ();
		break;

	case CMD_UNLOAD:
		acpi_db_unload_acpi_table (acpi_gbl_db_args[1], acpi_gbl_db_args[2]);
		break;

	case CMD_EXIT:
	case CMD_QUIT:
		if (op)
		{
			acpi_os_printf ("Method execution terminated\n");
			return (AE_CTRL_TERMINATE);
		}

		if (!acpi_gbl_db_output_to_file)
		{
			acpi_dbg_level = DEBUG_DEFAULT;
		}

		/* Shutdown */

		/* Acpi_ut_subsystem_shutdown (); */
		acpi_db_close_debug_file ();

		acpi_gbl_db_terminate_threads = TRUE;

		return (AE_CTRL_TERMINATE);

	case CMD_NOT_FOUND:
		acpi_os_printf ("Unknown Command\n");
		return (AE_CTRL_TRUE);
	}


	/* Add all commands that come here to the history buffer */

	acpi_db_add_to_history (input_buffer);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_execute_thread
 *
 * PARAMETERS:  Context         - Not used
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread.  Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

void
acpi_db_execute_thread (
	void                    *context)
{
	acpi_status             status = AE_OK;


	while (status != AE_CTRL_TERMINATE)
	{
		acpi_gbl_method_executing = FALSE;
		acpi_gbl_step_to_next_call = FALSE;

		acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_READY);
		status = acpi_db_command_dispatch (acpi_gbl_db_line_buf, NULL, NULL);
		acpi_ut_release_mutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_single_thread
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Debugger execute thread.  Waits for a command line, then
 *              simply dispatches it.
 *
 ******************************************************************************/

void
acpi_db_single_thread (
	void)
{
	acpi_status             status = AE_OK;


	acpi_gbl_method_executing = FALSE;
	acpi_gbl_step_to_next_call = FALSE;

	status = acpi_db_command_dispatch (acpi_gbl_db_line_buf, NULL, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_user_commands
 *
 * PARAMETERS:  Prompt              - User prompt (depends on mode)
 *              Op                  - Current executing parse op
 *
 * RETURN:      None
 *
 * DESCRIPTION: Command line execution for the AML debugger.  Commands are
 *              matched and dispatched here.
 *
 ******************************************************************************/

acpi_status
acpi_db_user_commands (
	NATIVE_CHAR             prompt,
	acpi_parse_object       *op)
{
	acpi_status             status = AE_OK;


	/* TBD: [Restructure] Need a separate command line buffer for step mode */

	while (!acpi_gbl_db_terminate_threads)
	{
		/* Force output to console until a command is entered */

		acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);

		/* Different prompt if method is executing */

		if (!acpi_gbl_method_executing)
		{
			acpi_os_printf ("%1c ", DB_COMMAND_PROMPT);
		}
		else
		{
			acpi_os_printf ("%1c ", DB_EXECUTE_PROMPT);
		}

		/* Get the user input line */

		acpi_os_get_line (acpi_gbl_db_line_buf);


		/* Check for single or multithreaded debug */

		if (acpi_gbl_debugger_configuration & DEBUGGER_MULTI_THREADED)
		{
			/*
			 * Signal the debug thread that we have a command to execute,
			 * and wait for the command to complete.
			 */
			acpi_ut_release_mutex (ACPI_MTX_DEBUG_CMD_READY);
			acpi_ut_acquire_mutex (ACPI_MTX_DEBUG_CMD_COMPLETE);
		}

		else
		{
			/* Just call to the command line interpreter */

			acpi_db_single_thread ();
		}
	}


	/*
	 * Only this thread (the original thread) should actually terminate the subsystem,
	 * because all the semaphores are deleted during termination
	 */
	acpi_terminate ();
	return (status);
}


#endif  /* ENABLE_DEBUGGER */

