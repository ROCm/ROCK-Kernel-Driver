/******************************************************************************
 *
 * Module Name: pstree - Parser op tree manipulation/traversal/search
 *              $Revision: 35 $
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
	 MODULE_NAME         ("pstree")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_arg
 *
 * PARAMETERS:  Op              - Get an argument for this op
 *              Argn            - Nth argument to get
 *
 * RETURN:      The argument (as an Op object).  NULL if argument does not exist
 *
 * DESCRIPTION: Get the specified op's argument.
 *
 ******************************************************************************/

acpi_parse_object *
acpi_ps_get_arg (
	acpi_parse_object       *op,
	u32                     argn)
{
	acpi_parse_object       *arg = NULL;
	const acpi_opcode_info  *op_info;


	FUNCTION_ENTRY ();


	/* Get the info structure for this opcode */

	op_info = acpi_ps_get_opcode_info (op->opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		/* Invalid opcode or ASCII character */

		return (NULL);
	}

	/* Check if this opcode requires argument sub-objects */

	if (!(op_info->flags & AML_HAS_ARGS)) {
		/* Has no linked argument objects */

		return (NULL);
	}

	/* Get the requested argument object */

	arg = op->value.arg;
	while (arg && argn) {
		argn--;
		arg = arg->next;
	}

	return (arg);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_append_arg
 *
 * PARAMETERS:  Op              - Append an argument to this Op.
 *              Arg             - Argument Op to append
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Append an argument to an op's argument list (a NULL arg is OK)
 *
 ******************************************************************************/

void
acpi_ps_append_arg (
	acpi_parse_object       *op,
	acpi_parse_object       *arg)
{
	acpi_parse_object       *prev_arg;
	const acpi_opcode_info  *op_info;


	FUNCTION_ENTRY ();


	if (!op) {
		return;
	}

	/* Get the info structure for this opcode */

	op_info = acpi_ps_get_opcode_info (op->opcode);
	if (op_info->class == AML_CLASS_UNKNOWN) {
		/* Invalid opcode */

		REPORT_ERROR (("Ps_append_arg: Invalid AML Opcode: 0x%2.2X\n", op->opcode));
		return;
	}

	/* Check if this opcode requires argument sub-objects */

	if (!(op_info->flags & AML_HAS_ARGS)) {
		/* Has no linked argument objects */

		return;
	}


	/* Append the argument to the linked argument list */

	if (op->value.arg) {
		/* Append to existing argument list */

		prev_arg = op->value.arg;
		while (prev_arg->next) {
			prev_arg = prev_arg->next;
		}
		prev_arg->next = arg;
	}

	else {
		/* No argument list, this will be the first argument */

		op->value.arg = arg;
	}


	/* Set the parent in this arg and any args linked after it */

	while (arg) {
		arg->parent = op;
		arg = arg->next;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_child
 *
 * PARAMETERS:  Op              - Get the child of this Op
 *
 * RETURN:      Child Op, Null if none is found.
 *
 * DESCRIPTION: Get op's children or NULL if none
 *
 ******************************************************************************/

acpi_parse_object *
acpi_ps_get_child (
	acpi_parse_object       *op)
{
	acpi_parse_object       *child = NULL;


	FUNCTION_ENTRY ();


	switch (op->opcode) {
	case AML_SCOPE_OP:
	case AML_ELSE_OP:
	case AML_DEVICE_OP:
	case AML_THERMAL_ZONE_OP:
	case AML_INT_METHODCALL_OP:

		child = acpi_ps_get_arg (op, 0);
		break;


	case AML_BUFFER_OP:
	case AML_PACKAGE_OP:
	case AML_METHOD_OP:
	case AML_IF_OP:
	case AML_WHILE_OP:
	case AML_FIELD_OP:

		child = acpi_ps_get_arg (op, 1);
		break;


	case AML_POWER_RES_OP:
	case AML_INDEX_FIELD_OP:

		child = acpi_ps_get_arg (op, 2);
		break;


	case AML_PROCESSOR_OP:
	case AML_BANK_FIELD_OP:

		child = acpi_ps_get_arg (op, 3);
		break;

	}

	return (child);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_get_depth_next
 *
 * PARAMETERS:  Origin          - Root of subtree to search
 *              Op              - Last (previous) Op that was found
 *
 * RETURN:      Next Op found in the search.
 *
 * DESCRIPTION: Get next op in tree (walking the tree in depth-first order)
 *              Return NULL when reaching "origin" or when walking up from root
 *
 ******************************************************************************/

acpi_parse_object *
acpi_ps_get_depth_next (
	acpi_parse_object       *origin,
	acpi_parse_object       *op)
{
	acpi_parse_object       *next = NULL;
	acpi_parse_object       *parent;
	acpi_parse_object       *arg;


	FUNCTION_ENTRY ();


	if (!op) {
		return (NULL);
	}

	/* look for an argument or child */

	next = acpi_ps_get_arg (op, 0);
	if (next) {
		return (next);
	}

	/* look for a sibling */

	next = op->next;
	if (next) {
		return (next);
	}

	/* look for a sibling of parent */

	parent = op->parent;

	while (parent) {
		arg = acpi_ps_get_arg (parent, 0);
		while (arg && (arg != origin) && (arg != op)) {
			arg = arg->next;
		}

		if (arg == origin) {
			/* reached parent of origin, end search */

			return (NULL);
		}

		if (parent->next) {
			/* found sibling of parent */
			return (parent->next);
		}

		op = parent;
		parent = parent->parent;
	}

	return (next);
}


