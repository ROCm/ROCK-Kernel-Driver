/*******************************************************************************
 *
 * Module Name: dbcmds - debug commands and output routines
 *              $Revision: 66 $
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
#include "acdispat.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acparser.h"
#include "acevents.h"
#include "acinterp.h"
#include "acdebug.h"
#include "actables.h"
#include "acresrc.h"

#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbcmds")


/*
 * Arguments for the Objects command
 * These object types map directly to the ACPI_TYPES
 */

ARGUMENT_INFO         acpi_db_object_types [] =
{ {"ANY"},
	{"NUMBERS"},
	{"STRINGS"},
	{"BUFFERS"},
	{"PACKAGES"},
	{"FIELDS"},
	{"DEVICES"},
	{"EVENTS"},
	{"METHODS"},
	{"MUTEXES"},
	{"REGIONS"},
	{"POWERRESOURCES"},
	{"PROCESSORS"},
	{"THERMALZONES"},
	{"BUFFERFIELDS"},
	{"DDBHANDLES"},
	{NULL}           /* Must be null terminated */
};


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_walk_for_references
 *
 * PARAMETERS:  Callback from Walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check if this namespace object refers to the target object
 *              that is passed in as the context value.
 *
 ******************************************************************************/

acpi_status
acpi_db_walk_for_references (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_operand_object     *obj_desc = (acpi_operand_object *) context;
	acpi_namespace_node     *node = (acpi_namespace_node *) obj_handle;


	/* Check for match against the namespace node itself */

	if (node == (void *) obj_desc) {
		acpi_os_printf ("Object is a Node [%4.4s]\n", &node->name);
	}

	/* Check for match against the object attached to the node */

	if (node->object == obj_desc) {
		acpi_os_printf ("Reference at Node->Object %p [%4.4s]\n", node, &node->name);
	}

	/* Check first child for a match */
	/* TBD: [Investigate] probably now obsolete with new datastructure */

	if (node->child == (void *) obj_desc) {
		acpi_os_printf ("Reference at Node->Child %p [%4.4s]\n", node, &node->name);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_find_references
 *
 * PARAMETERS:  Object_arg      - String with hex value of the object
 *
 * RETURN:      None
 *
 * DESCRIPTION: Search namespace for all references to the input object
 *
 ******************************************************************************/

void
acpi_db_find_references (
	NATIVE_CHAR             *object_arg)
{
	acpi_operand_object     *obj_desc;


	/* Convert string to object pointer */

	obj_desc = (acpi_operand_object *) STRTOUL (object_arg, NULL, 16);

	/* Search all nodes in namespace */

	acpi_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			  acpi_db_walk_for_references, (void *) obj_desc, NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_locks
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about internal mutexes.
 *
 ******************************************************************************/

void
acpi_db_display_locks (void)
{
	u32                     i;


	for (i = 0; i < MAX_MTX; i++) {
		acpi_os_printf ("%26s : %s\n", acpi_ut_get_mutex_name (i),
				 acpi_gbl_acpi_mutex_info[i].owner_id == ACPI_MUTEX_NOT_ACQUIRED
						? "Locked" : "Unlocked");
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_table_info
 *
 * PARAMETERS:  Table_arg       - String with name of table to be displayed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display information about loaded tables.  Current
 *              implementation displays all loaded tables.
 *
 ******************************************************************************/

void
acpi_db_display_table_info (
	NATIVE_CHAR             *table_arg)
{
	u32                     i;


	for (i = 0; i < NUM_ACPI_TABLES; i++) {
		if (acpi_gbl_acpi_tables[i].pointer) {
			acpi_os_printf ("%s at %p length %X\n", acpi_gbl_acpi_table_data[i].name,
					 acpi_gbl_acpi_tables[i].pointer, acpi_gbl_acpi_tables[i].length);
		}
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_unload_acpi_table
 *
 * PARAMETERS:  Table_arg       - Name of the table to be unloaded
 *              Instance_arg    - Which instance of the table to unload (if
 *                                there are multiple tables of the same type)
 *
 * RETURN:      Nonde
 *
 * DESCRIPTION: Unload an ACPI table.
 *              Instance is not implemented
 *
 ******************************************************************************/

void
acpi_db_unload_acpi_table (
	NATIVE_CHAR             *table_arg,
	NATIVE_CHAR             *instance_arg)
{
	u32                     i;
	acpi_status             status;


	/* Search all tables for the target type */

	for (i = 0; i < NUM_ACPI_TABLES; i++) {
		if (!STRNCMP (table_arg, acpi_gbl_acpi_table_data[i].signature,
				acpi_gbl_acpi_table_data[i].sig_length)) {
			/* Found the table, unload it */

			status = acpi_unload_table (i);
			if (ACPI_SUCCESS (status)) {
				acpi_os_printf ("[%s] unloaded and uninstalled\n", table_arg);
			}
			else {
				acpi_os_printf ("%s, while unloading [%s]\n",
					acpi_format_exception (status), table_arg);
			}

			return;
		}
	}

	acpi_os_printf ("Unknown table type [%s]\n", table_arg);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_set_method_breakpoint
 *
 * PARAMETERS:  Location            - AML offset of breakpoint
 *              Walk_state          - Current walk info
 *              Op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a breakpoint in a control method at the specified
 *              AML offset
 *
 ******************************************************************************/

void
acpi_db_set_method_breakpoint (
	NATIVE_CHAR             *location,
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	u32                     address;


	if (!op) {
		acpi_os_printf ("There is no method currently executing\n");
		return;
	}

	/* Get and verify the breakpoint address */

	address = STRTOUL (location, NULL, 16);
	if (address <= op->aml_offset) {
		acpi_os_printf ("Breakpoint %X is beyond current address %X\n", address, op->aml_offset);
	}

	/* Save breakpoint in current walk */

	walk_state->method_breakpoint = address;
	acpi_os_printf ("Breakpoint set at AML offset %X\n", address);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_set_method_call_breakpoint
 *
 * PARAMETERS:  Op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a breakpoint in a control method at the specified
 *              AML offset
 *
 ******************************************************************************/

void
acpi_db_set_method_call_breakpoint (
	acpi_parse_object       *op)
{


	if (!op) {
		acpi_os_printf ("There is no method currently executing\n");
		return;
	}


	acpi_gbl_step_to_next_call = TRUE;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_disassemble_aml
 *
 * PARAMETERS:  Statements          - Number of statements to disassemble
 *              Op                  - Current Op (from parse walk)
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display disassembled AML (ASL) starting from Op for the number
 *              of statements specified.
 *
 ******************************************************************************/

void
acpi_db_disassemble_aml (
	NATIVE_CHAR             *statements,
	acpi_parse_object       *op)
{
	u32                     num_statements = 8;


	if (!op) {
		acpi_os_printf ("There is no method currently executing\n");
		return;
	}

	if (statements) {
		num_statements = STRTOUL (statements, NULL, 0);
	}


	acpi_db_display_op (NULL, op, num_statements);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_dump_namespace
 *
 * PARAMETERS:  Start_arg       - Node to begin namespace dump
 *              Depth_arg       - Maximum tree depth to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump entire namespace or a subtree.  Each node is displayed
 *              with type and other information.
 *
 ******************************************************************************/

void
acpi_db_dump_namespace (
	NATIVE_CHAR             *start_arg,
	NATIVE_CHAR             *depth_arg)
{
	acpi_handle             subtree_entry = acpi_gbl_root_node;
	u32                     max_depth = ACPI_UINT32_MAX;


	/* No argument given, just start at the root and dump entire namespace */

	if (start_arg) {
		/* Check if numeric argument, must be a Node */

		if ((start_arg[0] >= 0x30) && (start_arg[0] <= 0x39)) {
			subtree_entry = (acpi_handle) STRTOUL (start_arg, NULL, 16);
			if (!acpi_os_readable (subtree_entry, sizeof (acpi_namespace_node))) {
				acpi_os_printf ("Address %p is invalid in this address space\n", subtree_entry);
				return;
			}

			if (!VALID_DESCRIPTOR_TYPE ((subtree_entry), ACPI_DESC_TYPE_NAMED)) {
				acpi_os_printf ("Address %p is not a valid Named object\n", subtree_entry);
				return;
			}
		}

		/* Alpha argument */

		else {
			/* The parameter is a name string that must be resolved to a Named obj*/

			subtree_entry = acpi_db_local_ns_lookup (start_arg);
			if (!subtree_entry) {
				subtree_entry = acpi_gbl_root_node;
			}
		}

		/* Now we can check for the depth argument */

		if (depth_arg) {
			max_depth = STRTOUL (depth_arg, NULL, 0);
		}
	}


	acpi_db_set_output_destination (DB_DUPLICATE_OUTPUT);
	acpi_os_printf ("ACPI Namespace (from %p subtree):\n", subtree_entry);

	/* Display the subtree */

	acpi_db_set_output_destination (DB_REDIRECTABLE_OUTPUT);
	acpi_ns_dump_objects (ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY, max_depth, ACPI_UINT32_MAX, subtree_entry);
	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_dump_namespace_by_owner
 *
 * PARAMETERS:  Owner_arg       - Owner ID whose nodes will be displayed
 *              Depth_arg       - Maximum tree depth to be dumped
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump elements of the namespace that are owned by the Owner_id.
 *
 ******************************************************************************/

void
acpi_db_dump_namespace_by_owner (
	NATIVE_CHAR             *owner_arg,
	NATIVE_CHAR             *depth_arg)
{
	acpi_handle             subtree_entry = acpi_gbl_root_node;
	u32                     max_depth = ACPI_UINT32_MAX;
	u16                     owner_id;


	owner_id = (u16) STRTOUL (owner_arg, NULL, 0);


	/* Now we can check for the depth argument */

	if (depth_arg) {
		max_depth = STRTOUL (depth_arg, NULL, 0);
	}


	acpi_db_set_output_destination (DB_DUPLICATE_OUTPUT);
	acpi_os_printf ("ACPI Namespace by owner %X:\n", owner_id);

	/* Display the subtree */

	acpi_db_set_output_destination (DB_REDIRECTABLE_OUTPUT);
	acpi_ns_dump_objects (ACPI_TYPE_ANY, ACPI_DISPLAY_SUMMARY, max_depth, owner_id, subtree_entry);
	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_send_notify
 *
 * PARAMETERS:  Name            - Name of ACPI object to send the notify to
 *              Value           - Value of the notify to send.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Send an ACPI notification.  The value specified is sent to the
 *              named object as an ACPI notify.
 *
 ******************************************************************************/

void
acpi_db_send_notify (
	NATIVE_CHAR             *name,
	u32                     value)
{
	acpi_namespace_node     *node;


	/* Translate name to an Named object */

	node = acpi_db_local_ns_lookup (name);
	if (!node) {
		return;
	}

	/* Decode Named object type */

	switch (node->type) {
	case ACPI_TYPE_DEVICE:
	case ACPI_TYPE_THERMAL:

		 /* Send the notify */

		acpi_ev_queue_notify_request (node, value);
		break;

	default:
		acpi_os_printf ("Named object is not a device or a thermal object\n");
		break;
	}

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_set_method_data
 *
 * PARAMETERS:  Type_arg        - L for local, A for argument
 *              Index_arg       - which one
 *              Value_arg       - Value to set.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set a local or argument for the running control method.
 *              NOTE: only object supported is Number.
 *
 ******************************************************************************/

void
acpi_db_set_method_data (
	NATIVE_CHAR             *type_arg,
	NATIVE_CHAR             *index_arg,
	NATIVE_CHAR             *value_arg)
{
	NATIVE_CHAR             type;
	u32                     index;
	u32                     value;
	acpi_walk_state         *walk_state;
	acpi_operand_object     *obj_desc;


	/* Validate Type_arg */

	STRUPR (type_arg);
	type = type_arg[0];
	if ((type != 'L') &&
		(type != 'A')) {
		acpi_os_printf ("Invalid SET operand: %s\n", type_arg);
		return;
	}

	/* Get the index and value */

	index = STRTOUL (index_arg, NULL, 16);
	value = STRTOUL (value_arg, NULL, 16);

	walk_state = acpi_ds_get_current_walk_state (acpi_gbl_current_walk_list);
	if (!walk_state) {
		acpi_os_printf ("There is no method currently executing\n");
		return;
	}


	/* Create and initialize the new object */

	obj_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
	if (!obj_desc) {
		acpi_os_printf ("Could not create an internal object\n");
		return;
	}

	obj_desc->integer.value = value;


	/* Store the new object into the target */

	switch (type) {
	case 'A':

		/* Set a method argument */

		if (index > MTH_NUM_ARGS) {
			acpi_os_printf ("Arg%d - Invalid argument name\n", index);
			return;
		}

		acpi_ds_store_object_to_local (AML_ARG_OP, index, obj_desc, walk_state);
		obj_desc = walk_state->arguments[index].object;

		acpi_os_printf ("Arg%d: ", index);
		acpi_db_display_internal_object (obj_desc, walk_state);
		break;

	case 'L':

		/* Set a method local */

		if (index > MTH_NUM_LOCALS) {
			acpi_os_printf ("Local%d - Invalid local variable name\n", index);
			return;
		}

		acpi_ds_store_object_to_local (AML_LOCAL_OP, index, obj_desc, walk_state);
		obj_desc = walk_state->local_variables[index].object;

		acpi_os_printf ("Local%d: ", index);
		acpi_db_display_internal_object (obj_desc, walk_state);
		break;

	default:
		break;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_walk_for_specific_objects
 *
 * PARAMETERS:  Callback from Walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display short info about objects in the namespace
 *
 ******************************************************************************/

acpi_status
acpi_db_walk_for_specific_objects (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;
	u32                     buf_size;
	NATIVE_CHAR             buffer[64];


	obj_desc = ((acpi_namespace_node *)obj_handle)->object;
	buf_size = sizeof (buffer) / sizeof (*buffer);

	/* Get and display the full pathname to this object */

	status = acpi_ns_handle_to_pathname (obj_handle, &buf_size, buffer);

	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could Not get pathname for object %p\n", obj_handle);
		return (AE_OK);
	}

	acpi_os_printf ("%32s", buffer);


	/* Display short information about the object */

	if (obj_desc) {
		switch (obj_desc->common.type) {
		case ACPI_TYPE_METHOD:
			acpi_os_printf (" #Args %d Concurrency %X", obj_desc->method.param_count, obj_desc->method.concurrency);
			break;

		case ACPI_TYPE_INTEGER:
			acpi_os_printf (" Value %X", obj_desc->integer.value);
			break;

		case ACPI_TYPE_STRING:
			acpi_os_printf (" \"%s\"", obj_desc->string.pointer);
			break;

		case ACPI_TYPE_REGION:
			acpi_os_printf (" Space_id %X Address %X Length %X", obj_desc->region.space_id, obj_desc->region.address, obj_desc->region.length);
			break;

		case ACPI_TYPE_PACKAGE:
			acpi_os_printf (" #Elements %X", obj_desc->package.count);
			break;

		case ACPI_TYPE_BUFFER:
			acpi_os_printf (" Length %X", obj_desc->buffer.length);
			break;
		}
	}

	acpi_os_printf ("\n");
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_objects
 *
 * PARAMETERS:  Obj_type_arg        - Type of object to display
 *              Display_count_arg   - Max depth to display
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display objects in the namespace of the requested type
 *
 ******************************************************************************/

acpi_status
acpi_db_display_objects (
	NATIVE_CHAR             *obj_type_arg,
	NATIVE_CHAR             *display_count_arg)
{
	acpi_object_type8       type;


	/* Get the object type */

	type = acpi_db_match_argument (obj_type_arg, acpi_db_object_types);
	if (type == ACPI_TYPE_NOT_FOUND) {
		acpi_os_printf ("Invalid or unsupported argument\n");
		return (AE_OK);
	}

	acpi_db_set_output_destination (DB_DUPLICATE_OUTPUT);
	acpi_os_printf ("Objects of type [%s] defined in the current ACPI Namespace: \n", acpi_ut_get_type_name (type));

	acpi_db_set_output_destination (DB_REDIRECTABLE_OUTPUT);

	/* Walk the namespace from the root */

	acpi_walk_namespace (type, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			   acpi_db_walk_for_specific_objects, (void *) &type, NULL);

	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_walk_and_match_name
 *
 * PARAMETERS:  Callback from Walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find a particular name/names within the namespace.  Wildcards
 *              are supported -- '?' matches any character.
 *
 ******************************************************************************/

acpi_status
acpi_db_walk_and_match_name (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_status             status;
	NATIVE_CHAR             *requested_name = (NATIVE_CHAR *) context;
	u32                     i;
	u32                     buf_size;
	NATIVE_CHAR             buffer[96];


	/* Check for a name match */

	for (i = 0; i < 4; i++) {
		/* Wildcard support */

		if ((requested_name[i] != '?') &&
			(requested_name[i] != ((NATIVE_CHAR *) (&((acpi_namespace_node *) obj_handle)->name))[i])) {
			/* No match, just exit */

			return (AE_OK);
		}
	}


	/* Get the full pathname to this object */

	buf_size = sizeof (buffer) / sizeof (*buffer);

	status = acpi_ns_handle_to_pathname (obj_handle, &buf_size, buffer);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could Not get pathname for object %p\n", obj_handle);
	}

	else {
		acpi_os_printf ("%32s (%p) - %s\n", buffer, obj_handle,
			acpi_ut_get_type_name (((acpi_namespace_node *) obj_handle)->type));
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_find_name_in_namespace
 *
 * PARAMETERS:  Name_arg        - The 4-character ACPI name to find.
 *                                wildcards are supported.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Search the namespace for a given name (with wildcards)
 *
 ******************************************************************************/

acpi_status
acpi_db_find_name_in_namespace (
	NATIVE_CHAR             *name_arg)
{

	if (STRLEN (name_arg) > 4) {
		acpi_os_printf ("Name must be no longer than 4 characters\n");
		return (AE_OK);
	}

	/* Walk the namespace from the root */

	acpi_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			   acpi_db_walk_and_match_name, name_arg, NULL);

	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_set_scope
 *
 * PARAMETERS:  Name                - New scope path
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Set the "current scope" as maintained by this utility.
 *              The scope is used as a prefix to ACPI paths.
 *
 ******************************************************************************/

void
acpi_db_set_scope (
	NATIVE_CHAR             *name)
{

	if (!name || name[0] == 0) {
		acpi_os_printf ("Current scope: %s\n", acpi_gbl_db_scope_buf);
		return;
	}

	acpi_db_prep_namestring (name);

	/* TBD: [Future] Validate scope here */

	if (name[0] == '\\') {
		STRCPY (acpi_gbl_db_scope_buf, name);
		STRCAT (acpi_gbl_db_scope_buf, "\\");
	}

	else {
		STRCAT (acpi_gbl_db_scope_buf, name);
		STRCAT (acpi_gbl_db_scope_buf, "\\");
	}

	acpi_os_printf ("New scope: %s\n", acpi_gbl_db_scope_buf);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_resources
 *
 * PARAMETERS:  Object_arg      - String with hex value of the object
 *
 * RETURN:      None
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

void
acpi_db_display_resources (
	NATIVE_CHAR             *object_arg)
{
#ifndef _IA16
	acpi_operand_object     *obj_desc;
	acpi_status             status;
	acpi_buffer             return_obj;


	acpi_db_set_output_destination (DB_REDIRECTABLE_OUTPUT);

	/* Convert string to object pointer */

	obj_desc = (acpi_operand_object *) STRTOUL (object_arg, NULL, 16);

	/* Prepare for a return object of arbitrary size */

	return_obj.pointer          = acpi_gbl_db_buffer;
	return_obj.length           = ACPI_DEBUG_BUFFER_SIZE;


	/* _PRT */

	acpi_os_printf ("Evaluating _PRT\n");

	status = acpi_evaluate_object (obj_desc, "_PRT", NULL, &return_obj);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could not obtain _PRT: %s\n", acpi_format_exception (status));
		goto get_crs;
	}

	return_obj.pointer          = acpi_gbl_db_buffer;
	return_obj.length           = ACPI_DEBUG_BUFFER_SIZE;

	status = acpi_get_irq_routing_table (obj_desc, &return_obj);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Get_irq_routing_table failed: %s\n", acpi_format_exception (status));
	}

	else {
		acpi_rs_dump_irq_list ((u8 *) acpi_gbl_db_buffer);
	}


	/* _CRS */

get_crs:
	acpi_os_printf ("Evaluating _CRS\n");

	return_obj.pointer          = acpi_gbl_db_buffer;
	return_obj.length           = ACPI_DEBUG_BUFFER_SIZE;

	status = acpi_evaluate_object (obj_desc, "_CRS", NULL, &return_obj);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could not obtain _CRS: %s\n", acpi_format_exception (status));
		goto get_prs;
	}

	return_obj.pointer          = acpi_gbl_db_buffer;
	return_obj.length           = ACPI_DEBUG_BUFFER_SIZE;

	status = acpi_get_current_resources (obj_desc, &return_obj);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Acpi_get_current_resources failed: %s\n", acpi_format_exception (status));
	}

	else {
		acpi_rs_dump_resource_list ((acpi_resource *) acpi_gbl_db_buffer);
	}


	/* _PRS */

get_prs:
	acpi_os_printf ("Evaluating _PRS\n");

	return_obj.pointer          = acpi_gbl_db_buffer;
	return_obj.length           = ACPI_DEBUG_BUFFER_SIZE;

	status = acpi_evaluate_object (obj_desc, "_PRS", NULL, &return_obj);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could not obtain _PRS: %s\n", acpi_format_exception (status));
		goto cleanup;
	}

	return_obj.pointer          = acpi_gbl_db_buffer;
	return_obj.length           = ACPI_DEBUG_BUFFER_SIZE;

	status = acpi_get_possible_resources (obj_desc, &return_obj);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Acpi_get_possible_resources failed: %s\n", acpi_format_exception (status));
	}

	else {
		acpi_rs_dump_resource_list ((acpi_resource *) acpi_gbl_db_buffer);
	}


cleanup:

	acpi_db_set_output_destination (DB_CONSOLE_OUTPUT);
	return;
#endif

}


#endif /* ENABLE_DEBUGGER */
