/******************************************************************************
 *
 * Module Name: nsload - namespace loading/expanding/contracting procedures
 *              $Revision: 47 $
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
#include "acinterp.h"
#include "acnamesp.h"
#include "amlcode.h"
#include "acparser.h"
#include "acdispat.h"
#include "acdebug.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsload")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_load_namespace
 *
 * PARAMETERS:  Display_aml_during_load
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load the name space from what ever is pointed to by DSDT.
 *              (DSDT points to either the BIOS or a buffer.)
 *
 ******************************************************************************/

acpi_status
acpi_ns_load_namespace (
	void)
{
	acpi_status             status;


	FUNCTION_TRACE ("Acpi_load_name_space");


	/* There must be at least a DSDT installed */

	if (acpi_gbl_DSDT == NULL) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "DSDT is not in memory\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}


	/*
	 * Load the namespace.  The DSDT is required,
	 * but the SSDT and PSDT tables are optional.
	 */
	status = acpi_ns_load_table_by_type (ACPI_TABLE_DSDT);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Ignore exceptions from these */

	acpi_ns_load_table_by_type (ACPI_TABLE_SSDT);
	acpi_ns_load_table_by_type (ACPI_TABLE_PSDT);


	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK,
		"ACPI Namespace successfully loaded at root %p\n",
		acpi_gbl_root_node));


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_one_parse_pass
 *
 * PARAMETERS:
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
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


	FUNCTION_TRACE ("Ns_one_complete_parse");


	/* Create and init a Root Node */

	parse_root = acpi_ps_alloc_op (AML_SCOPE_OP);
	if (!parse_root) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	((acpi_parse2_object *) parse_root)->name = ACPI_ROOT_NAME;


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


	FUNCTION_TRACE ("Ns_parse_table");


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


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_load_table
 *
 * PARAMETERS:  Table_desc      - Descriptor for table to be loaded
 *              Node            - Owning NS node
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load one ACPI table into the namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_load_table (
	acpi_table_desc         *table_desc,
	acpi_namespace_node     *node)
{
	acpi_status             status;


	FUNCTION_TRACE ("Ns_load_table");


	if (!table_desc->aml_start) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null AML pointer\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "AML block at %p\n", table_desc->aml_start));


	if (!table_desc->aml_length) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Zero-length AML block\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/*
	 * Parse the table and load the namespace with all named
	 * objects found within.  Control methods are NOT parsed
	 * at this time.  In fact, the control methods cannot be
	 * parsed until the entire namespace is loaded, because
	 * if a control method makes a forward reference (call)
	 * to another control method, we can't continue parsing
	 * because we don't know how many arguments to parse next!
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "**** Loading table into namespace ****\n"));

	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	status = acpi_ns_parse_table (table_desc, node->child);
	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Now we can parse the control methods.  We always parse
	 * them here for a sanity check, and if configured for
	 * just-in-time parsing, we delete the control method
	 * parse trees.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"**** Begin Table Method Parsing and Object Initialization ****\n"));

	status = acpi_ds_initialize_objects (table_desc, node);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"**** Completed Table Method Parsing and Object Initialization ****\n"));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_load_table_by_type
 *
 * PARAMETERS:  Table_type          - Id of the table type to load
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table or tables into the namespace.  All tables
 *              of the given type are loaded.  The mechanism allows this
 *              routine to be called repeatedly.
 *
 ******************************************************************************/

acpi_status
acpi_ns_load_table_by_type (
	acpi_table_type         table_type)
{
	u32                     i;
	acpi_status             status = AE_OK;
	acpi_table_desc         *table_desc;


	FUNCTION_TRACE ("Ns_load_table_by_type");


	acpi_ut_acquire_mutex (ACPI_MTX_TABLES);


	/*
	 * Table types supported are:
	 * DSDT (one), SSDT/PSDT (multiple)
	 */
	switch (table_type) {

	case ACPI_TABLE_DSDT:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Loading DSDT\n"));

		table_desc = &acpi_gbl_acpi_tables[ACPI_TABLE_DSDT];

		/* If table already loaded into namespace, just return */

		if (table_desc->loaded_into_namespace) {
			goto unlock_and_exit;
		}

		table_desc->table_id = TABLE_ID_DSDT;

		/* Now load the single DSDT */

		status = acpi_ns_load_table (table_desc, acpi_gbl_root_node);
		if (ACPI_SUCCESS (status)) {
			table_desc->loaded_into_namespace = TRUE;
		}

		break;


	case ACPI_TABLE_SSDT:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Loading %d SSDTs\n",
			acpi_gbl_acpi_tables[ACPI_TABLE_SSDT].count));

		/*
		 * Traverse list of SSDT tables
		 */
		table_desc = &acpi_gbl_acpi_tables[ACPI_TABLE_SSDT];
		for (i = 0; i < acpi_gbl_acpi_tables[ACPI_TABLE_SSDT].count; i++) {
			/*
			 * Only attempt to load table if it is not
			 * already loaded!
			 */
			if (!table_desc->loaded_into_namespace) {
				status = acpi_ns_load_table (table_desc, acpi_gbl_root_node);
				if (ACPI_FAILURE (status)) {
					break;
				}

				table_desc->loaded_into_namespace = TRUE;
			}

			table_desc = table_desc->next;
		}
		break;


	case ACPI_TABLE_PSDT:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Loading %d PSDTs\n",
			acpi_gbl_acpi_tables[ACPI_TABLE_PSDT].count));

		/*
		 * Traverse list of PSDT tables
		 */
		table_desc = &acpi_gbl_acpi_tables[ACPI_TABLE_PSDT];

		for (i = 0; i < acpi_gbl_acpi_tables[ACPI_TABLE_PSDT].count; i++) {
			/* Only attempt to load table if it is not already loaded! */

			if (!table_desc->loaded_into_namespace) {
				status = acpi_ns_load_table (table_desc, acpi_gbl_root_node);
				if (ACPI_FAILURE (status)) {
					break;
				}

				table_desc->loaded_into_namespace = TRUE;
			}

			table_desc = table_desc->next;
		}

		break;


	default:
		status = AE_SUPPORT;
		break;
	}


unlock_and_exit:

	acpi_ut_release_mutex (ACPI_MTX_TABLES);

	return_ACPI_STATUS (status);

}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_delete_subtree
 *
 * PARAMETERS:  Start_handle        - Handle in namespace where search begins
 *
 * RETURNS      Status
 *
 * DESCRIPTION: Walks the namespace starting at the given handle and deletes
 *              all objects, entries, and scopes in the entire subtree.
 *
 *              TBD: [Investigate] What if any part of this subtree is in use?
 *              (i.e. on one of the object stacks?)
 *
 ******************************************************************************/

acpi_status
acpi_ns_delete_subtree (
	acpi_handle             start_handle)
{
	acpi_status             status;
	acpi_handle             child_handle;
	acpi_handle             parent_handle;
	acpi_handle             next_child_handle;
	acpi_handle             dummy;
	u32                     level;


	FUNCTION_TRACE ("Ns_delete_subtree");


	parent_handle = start_handle;
	child_handle = 0;
	level        = 1;

	/*
	 * Traverse the tree of objects until we bubble back up
	 * to where we started.
	 */
	while (level > 0) {
		/* Attempt to get the next object in this scope */

		status = acpi_get_next_object (ACPI_TYPE_ANY, parent_handle,
				  child_handle, &next_child_handle);

		child_handle = next_child_handle;


		/* Did we get a new object? */

		if (ACPI_SUCCESS (status)) {
			/* Check if this object has any children */

			if (ACPI_SUCCESS (acpi_get_next_object (ACPI_TYPE_ANY, child_handle,
					 0, &dummy))) {
				/*
				 * There is at least one child of this object,
				 * visit the object
				 */
				level++;
				parent_handle = child_handle;
				child_handle = 0;
			}
		}

		else {
			/*
			 * No more children in this object, go back up to
			 * the object's parent
			 */
			level--;

			/* Delete all children now */

			acpi_ns_delete_children (child_handle);

			child_handle = parent_handle;
			acpi_get_parent (parent_handle, &parent_handle);
		}
	}

	/* Now delete the starting object, and we are done */

	acpi_ns_delete_node (child_handle);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 *  FUNCTION:       Acpi_ns_unload_name_space
 *
 *  PARAMETERS:     Handle          - Root of namespace subtree to be deleted
 *
 *  RETURN:         Status
 *
 *  DESCRIPTION:    Shrinks the namespace, typically in response to an undocking
 *                  event.  Deletes an entire subtree starting from (and
 *                  including) the given handle.
 *
 ******************************************************************************/

acpi_status
acpi_ns_unload_namespace (
	acpi_handle             handle)
{
	acpi_status             status;


	FUNCTION_TRACE ("Ns_unload_name_space");


	/* Parameter validation */

	if (!acpi_gbl_root_node) {
		return_ACPI_STATUS (AE_NO_NAMESPACE);
	}

	if (!handle) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* This function does the real work */

	status = acpi_ns_delete_subtree (handle);

	return_ACPI_STATUS (status);
}


