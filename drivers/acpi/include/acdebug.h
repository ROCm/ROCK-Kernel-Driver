/******************************************************************************
 *
 * Name: acdebug.h - ACPI/AML debugger
 *       $Revision: 65 $
 *
 *****************************************************************************/

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

#ifndef __ACDEBUG_H__
#define __ACDEBUG_H__


#define ACPI_DEBUG_BUFFER_SIZE  4196

typedef struct command_info
{
	char                    *name;          /* Command Name */
	u8                      min_args;       /* Minimum arguments required */

} COMMAND_INFO;


typedef struct argument_info
{
	char                    *name;          /* Argument Name */

} ARGUMENT_INFO;


#define PARAM_LIST(pl)                  pl

#define DBTEST_OUTPUT_LEVEL(lvl)        if (acpi_gbl_db_opt_verbose)

#define VERBOSE_PRINT(fp)               DBTEST_OUTPUT_LEVEL(lvl) {\
			  acpi_os_printf PARAM_LIST(fp);}

#define EX_NO_SINGLE_STEP       1
#define EX_SINGLE_STEP          2


/* Prototypes */


/*
 * dbapi - external debugger interfaces
 */

acpi_status
acpi_db_initialize (
	void);

void
acpi_db_terminate (
	void);

acpi_status
acpi_db_single_step (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op,
	u32                     op_type);


/*
 * dbcmds - debug commands and output routines
 */

void
acpi_db_display_table_info (
	char                    *table_arg);

void
acpi_db_unload_acpi_table (
	char                    *table_arg,
	char                    *instance_arg);

void
acpi_db_set_method_breakpoint (
	char                    *location,
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

void
acpi_db_set_method_call_breakpoint (
	acpi_parse_object       *op);

void
acpi_db_disassemble_aml (
	char                    *statements,
	acpi_parse_object       *op);

void
acpi_db_dump_namespace (
	char                    *start_arg,
	char                    *depth_arg);

void
acpi_db_dump_namespace_by_owner (
	char                    *owner_arg,
	char                    *depth_arg);

void
acpi_db_send_notify (
	char                    *name,
	u32                     value);

void
acpi_db_set_method_data (
	char                    *type_arg,
	char                    *index_arg,
	char                    *value_arg);

acpi_status
acpi_db_display_objects (
	char                    *obj_type_arg,
	char                    *display_count_arg);

acpi_status
acpi_db_find_name_in_namespace (
	char                    *name_arg);

void
acpi_db_set_scope (
	char                    *name);

void
acpi_db_find_references (
	char                    *object_arg);

void
acpi_db_display_locks (void);


void
acpi_db_display_resources (
	char                    *object_arg);

void
acpi_db_check_integrity (
	void);

acpi_status
acpi_db_integrity_walk (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value);

acpi_status
acpi_db_walk_and_match_name (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value);

acpi_status
acpi_db_walk_for_references (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value);

acpi_status
acpi_db_walk_for_specific_objects (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value);


/*
 * dbdisply - debug display commands
 */

void
acpi_db_display_method_info (
	acpi_parse_object       *op);

void
acpi_db_decode_and_display_object (
	char                    *target,
	char                    *output_type);

void
acpi_db_decode_node (
	acpi_namespace_node     *node);

void
acpi_db_display_result_object (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state);

acpi_status
acpi_db_display_all_methods (
	char                    *display_count_arg);

void
acpi_db_display_internal_object (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state);

void
acpi_db_display_arguments (
	void);

void
acpi_db_display_locals (
	void);

void
acpi_db_display_results (
	void);

void
acpi_db_display_calling_tree (
	void);

void
acpi_db_display_argument_object (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state);

void
acpi_db_dump_parser_descriptor (
	acpi_parse_object       *op);

void *
acpi_db_get_pointer (
	void                    *target);

void
acpi_db_decode_internal_object (
	acpi_operand_object     *obj_desc);


/*
 * dbexec - debugger control method execution
 */

void
acpi_db_execute (
	char                    *name,
	char                    **args,
	u32                     flags);

void
acpi_db_create_execution_threads (
	char                    *num_threads_arg,
	char                    *num_loops_arg,
	char                    *method_name_arg);

acpi_status
acpi_db_execute_method (
	acpi_db_method_info     *info,
	acpi_buffer             *return_obj);

void
acpi_db_execute_setup (
	acpi_db_method_info     *info);

u32
acpi_db_get_outstanding_allocations (
	void);

void ACPI_SYSTEM_XFACE
acpi_db_method_thread (
	void                    *context);


/*
 * dbfileio - Debugger file I/O commands
 */

acpi_object_type
acpi_db_match_argument (
	char                    *user_argument,
	ARGUMENT_INFO           *arguments);

acpi_status
ae_local_load_table (
	acpi_table_header       *table_ptr);

void
acpi_db_close_debug_file (
	void);

void
acpi_db_open_debug_file (
	char                    *name);

acpi_status
acpi_db_load_acpi_table (
	char                    *filename);

acpi_status
acpi_db_get_acpi_table (
	char                    *filename);

/*
 * dbhistry - debugger HISTORY command
 */

void
acpi_db_add_to_history (
	char                    *command_line);

void
acpi_db_display_history (void);

char *
acpi_db_get_from_history (
	char                    *command_num_arg);


/*
 * dbinput - user front-end to the AML debugger
 */

acpi_status
acpi_db_command_dispatch (
	char                    *input_buffer,
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op);

void ACPI_SYSTEM_XFACE
acpi_db_execute_thread (
	void                    *context);

acpi_status
acpi_db_user_commands (
	char                    prompt,
	acpi_parse_object       *op);

void
acpi_db_display_help (
	char                    *help_type);

char *
acpi_db_get_next_token (
	char                    *string,
	char                    **next);

u32
acpi_db_get_line (
	char                    *input_buffer);

u32
acpi_db_match_command (
	char                    *user_command);

void
acpi_db_single_thread (
	void);


/*
 * dbstats - Generation and display of ACPI table statistics
 */

void
acpi_db_generate_statistics (
	acpi_parse_object       *root,
	u8                      is_method);


acpi_status
acpi_db_display_statistics (
	char                    *type_arg);

acpi_status
acpi_db_classify_one_object (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value);

void
acpi_db_count_namespace_objects (
	void);

void
acpi_db_enumerate_object (
	acpi_operand_object     *obj_desc);


/*
 * dbutils - AML debugger utilities
 */

void
acpi_db_set_output_destination (
	u32                     where);

void
acpi_db_dump_buffer (
	u32                     address);

void
acpi_db_dump_object (
	acpi_object             *obj_desc,
	u32                     level);

void
acpi_db_prep_namestring (
	char                    *name);


acpi_status
acpi_db_second_pass_parse (
	acpi_parse_object       *root);

acpi_namespace_node *
acpi_db_local_ns_lookup (
	char                    *name);


#endif  /* __ACDEBUG_H__ */
