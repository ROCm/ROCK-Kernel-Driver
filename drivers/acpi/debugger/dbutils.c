/*******************************************************************************
 *
 * Module Name: dbutils - AML debugger utilities
 *              $Revision: 45 $
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
#include "acdispat.h"


#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbutils")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_set_output_destination
 *
 * PARAMETERS:  Output_flags        - Current flags word
 *
 * RETURN:      None
 *
 * DESCRIPTION: Set the current destination for debugger output.  Alos sets
 *              the debug output level accordingly.
 *
 ******************************************************************************/

void
acpi_db_set_output_destination (
	u32                     output_flags)
{

	acpi_gbl_db_output_flags = (u8) output_flags;

	if (output_flags & DB_REDIRECTABLE_OUTPUT) {
		if (acpi_gbl_db_output_to_file) {
			acpi_dbg_level = acpi_gbl_db_debug_level;
		}
	}
	else {
		acpi_dbg_level = acpi_gbl_db_console_debug_level;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_dump_buffer
 *
 * PARAMETERS:  Address             - Pointer to the buffer
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a portion of a buffer
 *
 ******************************************************************************/

void
acpi_db_dump_buffer (
	u32                     address)
{

	acpi_os_printf ("\n_location %X:\n", address);

	acpi_dbg_level |= ACPI_LV_TABLES;
	acpi_ut_dump_buffer ((u8 *) address, 64, DB_BYTE_DISPLAY, ACPI_UINT32_MAX);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_dump_object
 *
 * PARAMETERS:  Obj_desc        - External ACPI object to dump
 *              Level           - Nesting level.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Dump the contents of an ACPI external object
 *
 ******************************************************************************/

void
acpi_db_dump_object (
	acpi_object             *obj_desc,
	u32                     level)
{
	u32                     i;


	if (!obj_desc) {
		acpi_os_printf ("[Null Object]\n");
		return;
	}

	for (i = 0; i < level; i++) {
		acpi_os_printf (" ");
	}

	switch (obj_desc->type) {
	case ACPI_TYPE_ANY:

		acpi_os_printf ("[Object Reference] = %p\n", obj_desc->reference.handle);
		break;


	case ACPI_TYPE_INTEGER:

		acpi_os_printf ("[Integer] = %8.8X%8.8X\n", HIDWORD (obj_desc->integer.value),
				 LODWORD (obj_desc->integer.value));
		break;


	case ACPI_TYPE_STRING:

		acpi_os_printf ("[String] Value: ");
		for (i = 0; i < obj_desc->string.length; i++) {
			acpi_os_printf ("%c", obj_desc->string.pointer[i]);
		}
		acpi_os_printf ("\n");
		break;


	case ACPI_TYPE_BUFFER:

		acpi_os_printf ("[Buffer] = ");
		acpi_ut_dump_buffer ((u8 *) obj_desc->buffer.pointer, obj_desc->buffer.length, DB_DWORD_DISPLAY, _COMPONENT);
		break;


	case ACPI_TYPE_PACKAGE:

		acpi_os_printf ("[Package] Contains %d Elements: \n", obj_desc->package.count);

		for (i = 0; i < obj_desc->package.count; i++) {
			acpi_db_dump_object (&obj_desc->package.elements[i], level+1);
		}
		break;


	case INTERNAL_TYPE_REFERENCE:

		acpi_os_printf ("[Object Reference] = %p\n", obj_desc->reference.handle);
		break;


	case ACPI_TYPE_PROCESSOR:

		acpi_os_printf ("[Processor]\n");
		break;


	case ACPI_TYPE_POWER:

		acpi_os_printf ("[Power Resource]\n");
		break;


	default:

		acpi_os_printf ("[Unknown Type] %X \n", obj_desc->type);
		break;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_prep_namestring
 *
 * PARAMETERS:  Name            - String to prepare
 *
 * RETURN:      None
 *
 * DESCRIPTION: Translate all forward slashes and dots to backslashes.
 *
 ******************************************************************************/

void
acpi_db_prep_namestring (
	NATIVE_CHAR             *name)
{


	if (!name) {
		return;
	}

	STRUPR (name);

	/* Convert a leading forward slash to a backslash */

	if (*name == '/') {
		*name = '\\';
	}

	/* Ignore a leading backslash, this is the root prefix */

	if (*name == '\\') {
		name++;
	}

	/* Convert all slash path separators to dots */

	while (*name) {
		if ((*name == '/') ||
			(*name == '\\')) {
			*name = '.';
		}

		name++;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_second_pass_parse
 *
 * PARAMETERS:  Root            - Root of the parse tree
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Second pass parse of the ACPI tables.  We need to wait until
 *              second pass to parse the control methods
 *
 ******************************************************************************/

acpi_status
acpi_db_second_pass_parse (
	acpi_parse_object       *root)
{
	acpi_parse_object       *op = root;
	acpi_parse2_object      *method;
	acpi_parse_object       *search_op;
	acpi_parse_object       *start_op;
	acpi_status             status = AE_OK;
	u32                     base_aml_offset;
	acpi_walk_state         *walk_state;


	FUNCTION_ENTRY ();


	acpi_os_printf ("Pass two parse ....\n");


	while (op) {
		if (op->opcode == AML_METHOD_OP) {
			method = (acpi_parse2_object *) op;

			walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT,
					   NULL, NULL, NULL);
			if (!walk_state) {
				return (AE_NO_MEMORY);
			}


			walk_state->parser_state.aml        =
			walk_state->parser_state.aml_start  = method->data;
			walk_state->parser_state.aml_end    =
			walk_state->parser_state.pkg_end    = method->data + method->length;
			walk_state->parser_state.start_scope = op;

			walk_state->descending_callback     = acpi_ds_load1_begin_op;
			walk_state->ascending_callback      = acpi_ds_load1_end_op;


			status = acpi_ps_parse_aml (walk_state);


			base_aml_offset = (method->value.arg)->aml_offset + 1;
			start_op = (method->value.arg)->next;
			search_op = start_op;

			while (search_op) {
				search_op->aml_offset += base_aml_offset;
				search_op = acpi_ps_get_depth_next (start_op, search_op);
			}

		}

		if (op->opcode == AML_REGION_OP) {
			/* TBD: [Investigate] this isn't quite the right thing to do! */
			/*
			 *
			 * Method = (ACPI_DEFERRED_OP *) Op;
			 * Status = Acpi_ps_parse_aml (Op, Method->Body, Method->Body_length);
			 */
		}

		if (ACPI_FAILURE (status)) {
			break;
		}

		op = acpi_ps_get_depth_next (root, op);
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_local_ns_lookup
 *
 * PARAMETERS:  Name            - Name to lookup
 *
 * RETURN:      Pointer to a namespace node
 *
 * DESCRIPTION: Lookup a name in the ACPI namespace
 *
 ******************************************************************************/

acpi_namespace_node *
acpi_db_local_ns_lookup (
	NATIVE_CHAR             *name)
{
	NATIVE_CHAR             *internal_path;
	acpi_status             status;
	acpi_namespace_node     *node = NULL;


	acpi_db_prep_namestring (name);

	/* Build an internal namestring */

	status = acpi_ns_internalize_name (name, &internal_path);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Invalid namestring: %s\n", name);
		return (NULL);
	}

	/* Lookup the name */

	/* TBD: [Investigate] what scope do we use? */
	/* Use the root scope for the start of the search */

	status = acpi_ns_lookup (NULL, internal_path, ACPI_TYPE_ANY, IMODE_EXECUTE,
			   NS_NO_UPSEARCH | NS_DONT_OPEN_SCOPE, NULL, &node);

	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("Could not locate name: %s %s\n", name, acpi_format_exception (status));
	}


	ACPI_MEM_FREE (internal_path);

	return (node);
}


#endif /* ENABLE_DEBUGGER */


