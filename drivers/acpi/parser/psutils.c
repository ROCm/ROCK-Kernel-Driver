/******************************************************************************
 *
 * Module Name: psutils - Parser miscellaneous utilities (Parser only)
 *              $Revision: 44 $
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
	 MODULE_NAME         ("psutils")


#define PARSEOP_GENERIC     0x01
#define PARSEOP_NAMED       0x02
#define PARSEOP_DEFERRED    0x04
#define PARSEOP_BYTELIST    0x08
#define PARSEOP_IN_CACHE    0x80


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_init_op
 *
 * PARAMETERS:  Op              - A newly allocated Op object
 *              Opcode          - Opcode to store in the Op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate an acpi_op, choose op type (and thus size) based on
 *              opcode
 *
 ******************************************************************************/

void
acpi_ps_init_op (
	acpi_parse_object       *op,
	u16                     opcode)
{
	const acpi_opcode_info  *aml_op;


	FUNCTION_ENTRY ();


	op->data_type = ACPI_DESC_TYPE_PARSER;
	op->opcode = opcode;

	aml_op = acpi_ps_get_opcode_info (opcode);

	DEBUG_ONLY_MEMBERS (STRNCPY (op->op_name, aml_op->name,
			   sizeof (op->op_name)));
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_alloc_op
 *
 * PARAMETERS:  Opcode          - Opcode that will be stored in the new Op
 *
 * RETURN:      Pointer to the new Op.
 *
 * DESCRIPTION: Allocate an acpi_op, choose op type (and thus size) based on
 *              opcode.  A cache of opcodes is available for the pure
 *              GENERIC_OP, since this is by far the most commonly used.
 *
 ******************************************************************************/

acpi_parse_object*
acpi_ps_alloc_op (
	u16                     opcode)
{
	acpi_parse_object       *op = NULL;
	u32                     size;
	u8                      flags;
	const acpi_opcode_info  *op_info;


	FUNCTION_ENTRY ();


	op_info = acpi_ps_get_opcode_info (opcode);

	/* Allocate the minimum required size object */

	if (op_info->flags & AML_DEFER) {
		size = sizeof (acpi_parse2_object);
		flags = PARSEOP_DEFERRED;
	}

	else if (op_info->flags & AML_NAMED) {
		size = sizeof (acpi_parse2_object);
		flags = PARSEOP_NAMED;
	}

	else if (opcode == AML_INT_BYTELIST_OP) {
		size = sizeof (acpi_parse2_object);
		flags = PARSEOP_BYTELIST;
	}

	else {
		size = sizeof (acpi_parse_object);
		flags = PARSEOP_GENERIC;
	}


	if (size == sizeof (acpi_parse_object)) {
		/*
		 * The generic op is by far the most common (16 to 1)
		 */
		op = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_PSNODE);
	}

	else {
		op = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_PSNODE_EXT);
	}

	/* Initialize the Op */

	if (op) {
		acpi_ps_init_op (op, opcode);
		op->flags = flags;
	}

	return (op);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_free_op
 *
 * PARAMETERS:  Op              - Op to be freed
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free an Op object.  Either put it on the GENERIC_OP cache list
 *              or actually free it.
 *
 ******************************************************************************/

void
acpi_ps_free_op (
	acpi_parse_object       *op)
{
	PROC_NAME ("Ps_free_op");


	if (op->opcode == AML_INT_RETURN_VALUE_OP) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Free retval op: %p\n", op));
	}

	if (op->flags == PARSEOP_GENERIC) {
		acpi_ut_release_to_cache (ACPI_MEM_LIST_PSNODE, op);
	}

	else {
		acpi_ut_release_to_cache (ACPI_MEM_LIST_PSNODE_EXT, op);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ps_delete_parse_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Free all objects that are on the parse cache list.
 *
 ******************************************************************************/

void
acpi_ps_delete_parse_cache (
	void)
{
	FUNCTION_TRACE ("Ps_delete_parse_cache");


	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_PSNODE);
	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_PSNODE_EXT);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Utility functions
 *
 * DESCRIPTION: Low level character and object functions
 *
 ******************************************************************************/


/*
 * Is "c" a namestring lead character?
 */
u8
acpi_ps_is_leading_char (
	u32                     c)
{
	return ((u8) (c == '_' || (c >= 'A' && c <= 'Z')));
}


/*
 * Is "c" a namestring prefix character?
 */
u8
acpi_ps_is_prefix_char (
	u32                     c)
{
	return ((u8) (c == '\\' || c == '^'));
}


/*
 * Get op's name (4-byte name segment) or 0 if unnamed
 */
u32
acpi_ps_get_name (
	acpi_parse_object       *op)
{


	/* The "generic" object has no name associated with it */

	if (op->flags & PARSEOP_GENERIC) {
		return (0);
	}

	/* Only the "Extended" parse objects have a name */

	return (((acpi_parse2_object *) op)->name);
}


/*
 * Set op's name
 */
void
acpi_ps_set_name (
	acpi_parse_object       *op,
	u32                     name)
{

	/* The "generic" object has no name associated with it */

	if (op->flags & PARSEOP_GENERIC) {
		return;
	}

	((acpi_parse2_object *) op)->name = name;
}

