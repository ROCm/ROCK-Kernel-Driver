
/******************************************************************************
 *
 * Module Name: psfind - Parse tree search routine
 *              $Revision: 24 $
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

#define _COMPONENT          ACPI_PARSER
	 MODULE_NAME         ("psfind")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_parent
 *
 * PARAMETERS:  Op              - Get the parent of this Op
 *
 * RETURN:      The Parent op.
 *
 * DESCRIPTION: Get op's parent
 *
 ******************************************************************************/

static ACPI_PARSE_OBJECT*
acpi_ps_get_parent (
	ACPI_PARSE_OBJECT       *op)
{
	ACPI_PARSE_OBJECT       *parent = op;


	/* Traverse the tree upward (to root if necessary) */

	while (parent) {
		switch (parent->opcode) {
		case AML_SCOPE_OP:
		case AML_PACKAGE_OP:
		case AML_METHOD_OP:
		case AML_DEVICE_OP:
		case AML_POWER_RES_OP:
		case AML_THERMAL_ZONE_OP:

			return (parent->parent);
		}

		parent = parent->parent;
	}

	return (parent);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_find_name
 *
 * PARAMETERS:  Scope           - Scope to search
 *              Name            - ACPI name to search for
 *              Opcode          - Opcode to search for
 *
 * RETURN:      Op containing the name
 *
 * DESCRIPTION: Find name segment from a list of acpi_ops.  Searches a single
 *              scope, no more.
 *
 ******************************************************************************/

static ACPI_PARSE_OBJECT *
acpi_ps_find_name (
	ACPI_PARSE_OBJECT       *scope,
	u32                     name,
	u32                     opcode)
{
	ACPI_PARSE_OBJECT       *op;
	ACPI_PARSE_OBJECT       *field;


	/* search scope level for matching name segment */

	op = acpi_ps_get_child (scope);

	while (op) {

		if (acpi_ps_is_field_op (op->opcode)) {
			/* Field, search named fields */

			field = acpi_ps_get_child (op);
			while (field) {
				if (acpi_ps_is_named_op (field->opcode) &&
				   acpi_ps_get_name (field) == name &&
				   (!opcode || field->opcode == opcode)) {
					return (field);
				}

				field = field->next;
			}
		}

		else if (acpi_ps_is_create_field_op (op->opcode)) {
			if (op->opcode == AML_CREATE_FIELD_OP) {
				field = acpi_ps_get_arg (op, 3);
			}

			else {
				/* Create_xXXField, check name */

				field = acpi_ps_get_arg (op, 2);
			}

			if ((field) &&
				(field->value.string) &&
				(!STRNCMP (field->value.string, (char *) &name, ACPI_NAME_SIZE))) {
				return (op);
			}
		}

		else if ((acpi_ps_is_named_op (op->opcode)) &&
				 (acpi_ps_get_name (op) == name) &&
				 (!opcode || op->opcode == opcode || opcode == AML_SCOPE_OP)) {
			break;
		}

		op = op->next;
	}

	return (op);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_find
 *
 * PARAMETERS:  Scope           - Where to begin the search
 *              Path            - ACPI Path to the named object
 *              Opcode          - Opcode associated with the object
 *              Create          - if TRUE, create the object if not found.
 *
 * RETURN:      Op if found, NULL otherwise.
 *
 * DESCRIPTION: Find object within scope
 *
 ******************************************************************************/

ACPI_PARSE_OBJECT*
acpi_ps_find (
	ACPI_PARSE_OBJECT       *scope,
	NATIVE_CHAR             *path,
	u16                     opcode,
	u32                     create)
{
	u32                     seg_count;
	u32                     name;
	u32                     name_op;
	ACPI_PARSE_OBJECT       *op = NULL;
	u8                      unprefixed = TRUE;


	if (!scope || !path) {
		return (NULL);
	}


	acpi_gbl_ps_find_count++;


	/* Handle all prefixes in the name path */

	while (acpi_ps_is_prefix_char (GET8 (path))) {
		switch (GET8 (path)) {

		case '\\':

			/* Could just use a global for "root scope" here */

			while (scope->parent) {
				scope = scope->parent;
			}

			/* get first object within the scope */
			/* TBD: [Investigate] OR - set next in root scope to point to the same value as arg */

			/* Scope = Scope->Value.Arg; */

			break;


		case '^':

			/* Go up to the next valid scoping Op (method, scope, etc.) */

			if (acpi_ps_get_parent (scope)) {
				scope = acpi_ps_get_parent (scope);
			}

			break;
		}

		unprefixed = FALSE;
		path++;
	}

	/* get name segment count */

	switch (GET8 (path)) {
	case '\0':
		seg_count = 0;

		/* Null name case */

		if (unprefixed) {
			op = NULL;
		}
		else {
			op = scope;
		}


		return (op);
		break;

	case AML_DUAL_NAME_PREFIX:
		seg_count = 2;
		path++;
		break;

	case AML_MULTI_NAME_PREFIX_OP:
		seg_count = GET8 (path + 1);
		path += 2;
		break;

	default:
		seg_count = 1;
		break;
	}

	/* match each name segment */

	while (scope && seg_count) {
		MOVE_UNALIGNED32_TO_32 (&name, path);
		path += 4;
		seg_count --;

		if (seg_count) {
			name_op = 0;
		}
		else {
			name_op = opcode;
		}

		op = acpi_ps_find_name (scope, name, name_op);

		if (!op) {
			if (create) {
				/* Create a new Scope level */

				if (seg_count) {
					op = acpi_ps_alloc_op (AML_SCOPE_OP);
				}
				else {
					op = acpi_ps_alloc_op (opcode);
				}

				if (op) {
					acpi_ps_set_name (op, name);
					acpi_ps_append_arg (scope, op);

				}
			}

			else if (unprefixed) {
				/* Search higher scopes for unprefixed name */

				while (!op && scope->parent) {
					scope = scope->parent;
					op = acpi_ps_find_name (scope, name, opcode);

				}
			}

		}

		unprefixed = FALSE;
		scope = op;
	}

	return (op);
}


