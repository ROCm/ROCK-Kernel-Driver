/******************************************************************************
 *
 * Module Name: nsparse - namespace interface to AML parser
 *              $Revision: 1 $
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


#include "acpi.h"
#include "acnamesp.h"
#include "acparser.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsparse")


/*******************************************************************************
 *
 * FUNCTION:    Ns_one_complete_parse
 *
 * PARAMETERS:  Pass_number             - 1 or 2
 *              Table_desc              - The table to be parsed.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform one complete parse of an ACPI/AML table.
 *
 ******************************************************************************/

acpi_status
acpi_ns_one_complete_parse (
	u32                     pass_number,
	acpi_table_desc         *table_desc)
{
	acpi_parse_object       *parse_root;
	acpi_status             status;
	acpi_walk_state         *walk_state;


	ACPI_FUNCTION_TRACE ("Ns_one_complete_parse");


	/* Create and init a Root Node */

	parse_root = acpi_ps_create_scope_op ();
	if (!parse_root) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}


	/* Create and initialize a new walk state */

	walk_state = acpi_ds_create_walk_state (TABLE_ID_DSDT,
			   NULL, NULL, NULL);
	if (!walk_state) {
		acpi_ps_free_op (parse_root);
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	status = acpi_ds_init_aml_walk (walk_state, parse_root, NULL, table_desc->aml_start,
			  table_desc->aml_length, NULL, NULL, pass_number);
	if (ACPI_FAILURE (status)) {
		acpi_ds_delete_walk_state (walk_state);
		return_ACPI_STATUS (status);
	}

	/* Parse the AML */

	ACPI_DEBUG_PRINT ((ACPI_DB_PARSE, "*PARSE* pass %d parse\n", pass_number));
	status = acpi_ps_parse_aml (walk_state);

	acpi_ps_delete_parse_tree (parse_root);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_parse_table
 *
 * PARAMETERS:  Table_desc      - An ACPI table descriptor for table to parse
 *              Start_node      - Where to enter the table into the namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Parse AML within an ACPI table and return a tree of ops
 *
 ******************************************************************************/

acpi_status
acpi_ns_parse_table (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *start_node)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ns_parse_table");


	/*
	 * AML Parse, pass 1
	 *
	 * In this pass, we load most of the namespace.  Control methods
	 * are not parsed until later.  A parse tree is not created.  Instead,
	 * each Parser Op subtree is deleted when it is finished.  This saves
	 * a great deal of memory, and allows a small cache of parse objects
	 * to service the entire parse.  The second pass of the parse then
	 * performs another complete parse of the AML..
	 */
	status = acpi_ns_one_complete_parse (1, table_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * AML Parse, pass 2
	 *
	 * In this pass, we resolve forward references and other things
	 * that could not be completed during the first pass.
	 * Another complete parse of the AML is performed, but the
	 * overhead of this is compensated for by the fact that the
	 * parse objects are all cached.
	 */
	status = acpi_ns_one_complete_parse (2, table_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	return_ACPI_STATUS (status);
}


