/*******************************************************************************
 *
 * Module Name: dbdisasm - parser op tree display routines
 *              $Revision: 67 $
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
#include "acparser.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acdebug.h"


#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 ACPI_MODULE_NAME    ("dbdisasm")


#define BLOCK_PAREN         1
#define BLOCK_BRACE         2
#define DB_NO_OP_INFO       "            [%2.2d]  "
#define DB_FULL_OP_INFO     "%5.5X #%4.4hX [%2.2d]  "


NATIVE_CHAR                 *acpi_gbl_db_disasm_indent = "....";


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_block_type
 *
 * PARAMETERS:  Op              - Object to be examined
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Type of block for this op (parens or braces)
 *
 ******************************************************************************/

u32
acpi_db_block_type (
	acpi_parse_object       *op)
{

	switch (op->common.aml_opcode) {
	case AML_METHOD_OP:
		return (BLOCK_BRACE);

	default:
		break;
	}

	return (BLOCK_PAREN);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_display_object_pathname
 *
 * PARAMETERS:  Op              - Object whose pathname is to be obtained
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Diplay the pathname associated with a named object.  Two
 *              versions. One searches the parse tree (for parser-only
 *              applications suchas Acpi_dump), and the other searches the
 *              ACPI namespace (the parse tree is probably deleted)
 *
 ******************************************************************************/

#ifdef PARSER_ONLY

acpi_status
acpi_ps_display_object_pathname (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_parse_object       *target_op;
	char                    *name;


	if (op->common.flags & ACPI_PARSEOP_GENERIC) {
		name = op->common.value.name;
		if (name[0] == '\\') {
			acpi_os_printf (" (Fully Qualified Pathname)");
			return (AE_OK);
		}
	}
	else {
		name = (char *) &op->named.name;
	}

	/* Search parent tree up to the root if necessary */

	target_op = acpi_ps_find (op, name, 0, 0);
	if (!target_op) {
		/*
		 * Didn't find the name in the parse tree.  This may be
		 * a problem, or it may simply be one of the predefined names
		 * (such as _OS_).  Rather than worry about looking up all
		 * the predefined names, just display the name as given
		 */
		acpi_os_printf (" **** Path not found in parse tree");
	}
	else {
		/* The target was found, print the name and complete path */

		acpi_os_printf (" (Path ");
		acpi_db_display_path (target_op);
		acpi_os_printf (")");
	}

	return (AE_OK);
}

#else

acpi_status
acpi_ps_display_object_pathname (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	acpi_status             status;
	acpi_namespace_node     *node;
	acpi_buffer             buffer;
	u32                     debug_level;


	/* Save current debug level so we don't get extraneous debug output */

	debug_level = acpi_dbg_level;
	acpi_dbg_level = 0;

	/* Just get the Node out of the Op object */

	node = op->common.node;
	if (!node) {
		/* Node not defined in this scope, look it up */

		status = acpi_ns_lookup (walk_state->scope_info, op->common.value.string, ACPI_TYPE_ANY,
				  ACPI_IMODE_EXECUTE, ACPI_NS_SEARCH_PARENT, walk_state, &(node));

		if (ACPI_FAILURE (status)) {
			/*
			 * We can't get the pathname since the object
			 * is not in the namespace.  This can happen during single
			 * stepping where a dynamic named object is *about* to be created.
			 */
			acpi_os_printf (" [Path not found]");
			goto exit;
		}

		/* Save it for next time. */

		op->common.node = node;
	}

	/* Convert Named_desc/handle to a full pathname */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
	status = acpi_ns_handle_to_pathname (node, &buffer);
	if (ACPI_FAILURE (status)) {
		acpi_os_printf ("****Could not get pathname****)");
		goto exit;
	}

	acpi_os_printf (" (Path %s)", buffer.pointer);
	ACPI_MEM_FREE (buffer.pointer);


exit:
	/* Restore the debug level */

	acpi_dbg_level = debug_level;
	return (status);
}

#endif


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_op
 *
 * PARAMETERS:  Origin          - Starting object
 *              Num_opcodes     - Max number of opcodes to be displayed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display parser object and its children
 *
 ******************************************************************************/

void
acpi_db_display_op (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *origin,
	u32                     num_opcodes)
{
	acpi_parse_object       *op = origin;
	acpi_parse_object       *arg;
	acpi_parse_object       *depth;
	u32                     depth_count = 0;
	u32                     last_depth = 0;
	u32                     i;
	u32                     j;


	if (!op) {
		acpi_db_display_opcode (walk_state, op);
		return;
	}


	while (op) {
		/* Indentation */

		depth_count = 0;
		if (!acpi_gbl_db_opt_verbose) {
			depth_count++;
		}

		/* Determine the nesting depth of this argument */

		for (depth = op->common.parent; depth; depth = depth->common.parent) {
			arg = acpi_ps_get_arg (depth, 0);
			while (arg && arg != origin) {
				arg = arg->common.next;
			}

			if (arg) {
				break;
			}

			depth_count++;
		}

		/* Open a new block if we are nested further than last time */

		if (depth_count > last_depth) {
			VERBOSE_PRINT ((DB_NO_OP_INFO, last_depth));
			for (i = 0; i < last_depth; i++) {
				acpi_os_printf ("%s", acpi_gbl_db_disasm_indent);
			}

			if (acpi_db_block_type (op) == BLOCK_PAREN) {
				acpi_os_printf ("(\n");
			}
			else {
				acpi_os_printf ("{\n");
			}
		}

		/* Close a block if we are nested less than last time */

		else if (depth_count < last_depth) {
			for (j = last_depth; j >= (depth_count + 1); j--) {
				VERBOSE_PRINT ((DB_NO_OP_INFO, (j - 1)));
				for (i = 1; i < j; i++) {
					acpi_os_printf ("%s", acpi_gbl_db_disasm_indent);
				}

				if (acpi_db_block_type (op) == BLOCK_PAREN) {
					acpi_os_printf (")\n");
				}
				else {
					acpi_os_printf ("}\n");
				}
			}
		}

		/* In verbose mode, print the AML offset, opcode and depth count */

		VERBOSE_PRINT ((DB_FULL_OP_INFO, (u32) op->common.aml_offset,
				op->common.aml_opcode, depth_count));


		/* Indent the output according to the depth count */

		for (i = 0; i < depth_count; i++) {
			acpi_os_printf ("%s", acpi_gbl_db_disasm_indent);
		}

		/* Now print the opcode */

		acpi_db_display_opcode (walk_state, op);

		/* Resolve a name reference */

		if ((op->common.aml_opcode == AML_INT_NAMEPATH_OP && op->common.value.name) &&
			(op->common.parent) &&
			(acpi_gbl_db_opt_verbose)) {
			(void) acpi_ps_display_object_pathname (walk_state, op);
		}

		acpi_os_printf ("\n");

		/* Get the next node in the tree */

		op = acpi_ps_get_depth_next (origin, op);
		last_depth = depth_count;

		num_opcodes--;
		if (!num_opcodes) {
			op = NULL;
		}
	}

	/* Close the last block(s) */

	depth_count = last_depth -1;
	for (i = 0; i < last_depth; i++) {
		VERBOSE_PRINT ((DB_NO_OP_INFO, last_depth - i));
		for (j = 0; j < depth_count; j++) {
			acpi_os_printf ("%s", acpi_gbl_db_disasm_indent);
		}
		acpi_os_printf ("}\n");
		depth_count--;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_namestring
 *
 * PARAMETERS:  Name                - ACPI Name string to store
 *
 * RETURN:      None
 *
 * DESCRIPTION: Display namestring. Handles prefix characters
 *
 ******************************************************************************/

void
acpi_db_display_namestring (
	NATIVE_CHAR             *name)
{
	u32                     seg_count;


	if (!name) {
		acpi_os_printf ("<NULL NAME PTR>");
		return;
	}

	/* Handle all Scope Prefix operators */

	while (acpi_ps_is_prefix_char (ACPI_GET8 (name))) {
		/* Append prefix character */

		acpi_os_printf ("%1c", ACPI_GET8 (name));
		name++;
	}

	switch (ACPI_GET8 (name)) {
	case 0:
		seg_count = 0;
		break;

	case AML_DUAL_NAME_PREFIX:
		seg_count = 2;
		name++;
		break;

	case AML_MULTI_NAME_PREFIX_OP:
		seg_count = (u32) ACPI_GET8 (name + 1);
		name += 2;
		break;

	default:
		seg_count = 1;
		break;
	}

	while (seg_count) {
		/* Append Name segment */

		acpi_os_printf ("%4.4s", name);

		seg_count--;
		if (seg_count) {
			/* Not last name, append dot separator */

			acpi_os_printf (".");
		}
		name += ACPI_NAME_SIZE;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_path
 *
 * PARAMETERS:  Op                  - Named Op whose path is to be constructed
 *
 * RETURN:      None
 *
 * DESCRIPTION: Walk backwards from current scope and display the name
 *              of each previous level of scope up to the root scope
 *              (like "pwd" does with file systems)
 *
 ******************************************************************************/

void
acpi_db_display_path (
	acpi_parse_object       *op)
{
	acpi_parse_object       *prev;
	acpi_parse_object       *search;
	u32                     name;
	u8                      do_dot = FALSE;
	acpi_parse_object       *name_path;
	const acpi_opcode_info  *op_info;


	/* We are only interested in named objects */

	op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
	if (!(op_info->flags & AML_NSNODE)) {
		return;
	}

	if (op_info->flags & AML_CREATE) {
		/* Field creation - check for a fully qualified namepath */

		if (op->common.aml_opcode == AML_CREATE_FIELD_OP) {
			name_path = acpi_ps_get_arg (op, 3);
		}
		else {
			name_path = acpi_ps_get_arg (op, 2);
		}

		if ((name_path) &&
			(name_path->common.value.string) &&
			(name_path->common.value.string[0] == '\\')) {
			acpi_db_display_namestring (name_path->common.value.string);
			return;
		}
	}

	prev = NULL;            /* Start with Root Node */

	while (prev != op) {
		/* Search upwards in the tree to find scope with "prev" as its parent */

		search = op;
		for (; ;) {
			if (search->common.parent == prev) {
				break;
			}

			/* Go up one level */

			search = search->common.parent;
		}

		if (prev) {
			op_info = acpi_ps_get_opcode_info (search->common.aml_opcode);
			if (!(op_info->flags & AML_FIELD)) {
				/* below root scope, append scope name */

				if (do_dot) {
					/* append dot */

					acpi_os_printf (".");
				}

				if (op_info->flags & AML_CREATE) {
					if (op->common.aml_opcode == AML_CREATE_FIELD_OP) {
						name_path = acpi_ps_get_arg (op, 3);
					}
					else {
						name_path = acpi_ps_get_arg (op, 2);
					}

					if ((name_path) &&
						(name_path->common.value.string)) {
						acpi_os_printf ("%4.4s", name_path->common.value.string);
					}
				}
				else {
					name = acpi_ps_get_name (search);
					acpi_os_printf ("%4.4s", &name);
				}

				do_dot = TRUE;
			}
		}

		prev = search;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_opcode
 *
 * PARAMETERS:  Op                  - Op that is to be printed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store printed op in a Buffer and return its length
 *              (or -1 if out of space)
 *
 * NOTE: Terse mode prints out ASL-like code.  Verbose mode adds more info.
 *
 ******************************************************************************/

void
acpi_db_display_opcode (
	acpi_walk_state         *walk_state,
	acpi_parse_object       *op)
{
	u8                      *byte_data;
	u32                     byte_count;
	u32                     i;
	const acpi_opcode_info  *op_info = NULL;
	u32                     name;


	if (!op) {
		acpi_os_printf ("<NULL OP PTR>");
		return;
	}

	/* op and arguments */

	switch (op->common.aml_opcode) {
	case AML_BYTE_OP:

		if (acpi_gbl_db_opt_verbose) {
			acpi_os_printf ("(u8) 0x%2.2hX", op->common.value.integer8);
		}
		else {
			acpi_os_printf ("0x%2.2hX", op->common.value.integer8);
		}
		break;


	case AML_WORD_OP:

		if (acpi_gbl_db_opt_verbose) {
			acpi_os_printf ("(u16) 0x%4.4hX", op->common.value.integer16);
		}
		else {
			acpi_os_printf ("0x%4.4hX", op->common.value.integer16);
		}
		break;


	case AML_DWORD_OP:

		if (acpi_gbl_db_opt_verbose) {
			acpi_os_printf ("(u32) 0x%8.8X", op->common.value.integer32);
		}
		else {
			acpi_os_printf ("0x%8.8X", op->common.value.integer32);
		}
		break;


	case AML_QWORD_OP:

		if (acpi_gbl_db_opt_verbose) {
			acpi_os_printf ("(u64) 0x%8.8X%8.8X", op->common.value.integer64.hi,
					 op->common.value.integer64.lo);
		}
		else {
			acpi_os_printf ("0x%8.8X%8.8X", op->common.value.integer64.hi,
					 op->common.value.integer64.lo);
		}
		break;


	case AML_STRING_OP:

		if (op->common.value.string) {
			acpi_os_printf ("\"%s\"", op->common.value.string);
		}
		else {
			acpi_os_printf ("<\"NULL STRING PTR\">");
		}
		break;


	case AML_INT_STATICSTRING_OP:

		if (op->common.value.string) {
			acpi_os_printf ("\"%s\"", op->common.value.string);
		}
		else {
			acpi_os_printf ("\"<NULL STATIC STRING PTR>\"");
		}
		break;


	case AML_INT_NAMEPATH_OP:

		acpi_db_display_namestring (op->common.value.name);
		break;


	case AML_INT_NAMEDFIELD_OP:

		acpi_os_printf ("Named_field (Length 0x%8.8X)  ", op->common.value.integer32);
		break;


	case AML_INT_RESERVEDFIELD_OP:

		acpi_os_printf ("Reserved_field (Length 0x%8.8X) ", op->common.value.integer32);
		break;


	case AML_INT_ACCESSFIELD_OP:

		acpi_os_printf ("Access_field (Length 0x%8.8X) ", op->common.value.integer32);
		break;


	case AML_INT_BYTELIST_OP:

		if (acpi_gbl_db_opt_verbose) {
			acpi_os_printf ("Byte_list   (Length 0x%8.8X)  ", op->common.value.integer32);
		}
		else {
			acpi_os_printf ("0x%2.2X", op->common.value.integer32);

			byte_count = op->common.value.integer32;
			byte_data = op->named.data;

			for (i = 0; i < byte_count; i++) {
				acpi_os_printf (", 0x%2.2X", byte_data[i]);
			}
		}
		break;


	default:

		/* Just get the opcode name and print it */

		op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
		acpi_os_printf ("%s", op_info->name);


#ifndef PARSER_ONLY
		if ((op->common.aml_opcode == AML_INT_RETURN_VALUE_OP) &&
			(walk_state) &&
			(walk_state->results) &&
			(walk_state->results->results.num_results)) {
			acpi_db_decode_internal_object (
				walk_state->results->results.obj_desc [walk_state->results->results.num_results-1]);
		}
#endif
		break;
	}

	if (!op_info) {
		/* If there is another element in the list, add a comma */

		if (op->common.next) {
			acpi_os_printf (",");
		}
	}

	/*
	 * If this is a named opcode, print the associated name value
	 */
	op_info = acpi_ps_get_opcode_info (op->common.aml_opcode);
	if (op && (op_info->flags & AML_NAMED)) {
		name = acpi_ps_get_name (op);
		acpi_os_printf (" %4.4s", &name);

		if ((acpi_gbl_db_opt_verbose) && (op->common.aml_opcode != AML_INT_NAMEDFIELD_OP)) {
			(void) acpi_ps_display_object_pathname (walk_state, op);
		}
	}
}

#endif  /* ENABLE_DEBUGGER */

