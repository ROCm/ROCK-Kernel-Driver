/******************************************************************************
 *
 * Module Name: exconfig - Namespace reconfiguration (Load/Unload opcodes)
 *              $Revision: 67 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acevents.h"
#include "actables.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exconfig")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_add_table
 *
 * PARAMETERS:  Table               - Pointer to raw table
 *              Parent_node         - Where to load the table (scope)
 *              Ddb_handle          - Where to return the table handle.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common function to Install and Load an ACPI table with a
 *              returned table handle.
 *
 ******************************************************************************/

acpi_status
acpi_ex_add_table (
	acpi_table_header       *table,
	acpi_namespace_node     *parent_node,
	acpi_operand_object     **ddb_handle)
{
	acpi_status             status;
	acpi_table_desc         table_info;
	acpi_operand_object     *obj_desc;


	ACPI_FUNCTION_TRACE ("Ex_add_table");


	/* Create an object to be the table handle */

	obj_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_REFERENCE);
	if (!obj_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Install the new table into the local data structures */

	table_info.pointer     = table;
	table_info.length      = (ACPI_SIZE) table->length;
	table_info.allocation  = ACPI_MEM_ALLOCATED;
	table_info.base_pointer = table;

	status = acpi_tb_install_table (&table_info);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Add the table to the namespace */

	status = acpi_ns_load_table (table_info.installed_desc, parent_node);
	if (ACPI_FAILURE (status)) {
		/* Uninstall table on error */

		(void) acpi_tb_uninstall_table (table_info.installed_desc);
		goto cleanup;
	}

	/* Init the table handle */

	obj_desc->reference.opcode = AML_LOAD_OP;
	obj_desc->reference.object = table_info.installed_desc;
	*ddb_handle = obj_desc;
	return_ACPI_STATUS (AE_OK);


cleanup:
	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_load_table_op
 *
 * PARAMETERS:  Walk_state          - Current state with operands
 *              Return_desc         - Where to store the return object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table
 *
 ******************************************************************************/

acpi_status
acpi_ex_load_table_op (
	acpi_walk_state         *walk_state,
	acpi_operand_object     **return_desc)
{
	acpi_status             status;
	acpi_operand_object     **operand = &walk_state->operands[0];
	acpi_table_header       *table;
	acpi_namespace_node     *parent_node;
	acpi_namespace_node     *start_node;
	acpi_namespace_node     *parameter_node = NULL;
	acpi_operand_object     *ddb_handle;


	ACPI_FUNCTION_TRACE ("Ex_load_table_op");


#if 0
	/*
	 * Make sure that the signature does not match one of the tables that
	 * is already loaded.
	 */
	status = acpi_tb_match_signature (operand[0]->string.pointer, NULL);
	if (status == AE_OK) {
		/* Signature matched -- don't allow override */

		return_ACPI_STATUS (AE_ALREADY_EXISTS);
	}
#endif

	/* Find the ACPI table */

	status = acpi_tb_find_table (operand[0]->string.pointer,
			   operand[1]->string.pointer,
			   operand[2]->string.pointer, &table);
	if (ACPI_FAILURE (status)) {
		if (status != AE_NOT_FOUND) {
			return_ACPI_STATUS (status);
		}

		/* Not found, return an Integer=0 and AE_OK */

		ddb_handle = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!ddb_handle) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		ddb_handle->integer.value = 0;
		*return_desc = ddb_handle;

		return_ACPI_STATUS (AE_OK);
	}

	/* Default nodes */

	start_node = walk_state->scope_info->scope.node;
	parent_node = acpi_gbl_root_node;

	/* Root_path (optional parameter) */

	if (operand[3]->string.length > 0) {
		/*
		 * Find the node referenced by the Root_path_string. This is the
		 * location within the namespace where the table will be loaded.
		 */
		status = acpi_ns_get_node_by_path (operand[3]->string.pointer, start_node,
				   ACPI_NS_SEARCH_PARENT, &parent_node);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Parameter_path (optional parameter) */

	if (operand[4]->string.length > 0) {
		if ((operand[4]->string.pointer[0] != '\\') &&
			(operand[4]->string.pointer[0] != '^')) {
			/*
			 * Path is not absolute, so it will be relative to the node
			 * referenced by the Root_path_string (or the NS root if omitted)
			 */
			start_node = parent_node;
		}

		/*
		 * Find the node referenced by the Parameter_path_string
		 */
		status = acpi_ns_get_node_by_path (operand[4]->string.pointer, start_node,
				   ACPI_NS_SEARCH_PARENT, &parameter_node);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Load the table into the namespace */

	status = acpi_ex_add_table (table, parent_node, &ddb_handle);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Parameter Data (optional) */

	if (parameter_node) {
		/* Store the parameter data into the optional parameter object */

		status = acpi_ex_store (operand[5], ACPI_CAST_PTR (acpi_operand_object, parameter_node),
				 walk_state);
		if (ACPI_FAILURE (status)) {
			(void) acpi_ex_unload_table (ddb_handle);
		}
	}

	return_ACPI_STATUS  (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_load_op
 *
 * PARAMETERS:  Obj_desc        - Region or Field where the table will be
 *                                obtained
 *              Target          - Where a handle to the table will be stored
 *              Walk_state      - Current state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table from a field or operation region
 *
 ******************************************************************************/

acpi_status
acpi_ex_load_op (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *target,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;
	acpi_operand_object     *ddb_handle;
	acpi_operand_object     *buffer_desc = NULL;
	acpi_table_header       *table_ptr = NULL;
	u8                      *table_data_ptr;
	acpi_table_header       table_header;
	u32                     i;

	ACPI_FUNCTION_TRACE ("Ex_load_op");


	/* Object can be either an Op_region or a Field */

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Load from Region %p %s\n",
			obj_desc, acpi_ut_get_object_type_name (obj_desc)));

		/* Get the table header */

		table_header.length = 0;
		for (i = 0; i < sizeof (acpi_table_header); i++) {
			status = acpi_ev_address_space_dispatch (obj_desc, ACPI_READ,
					   (ACPI_PHYSICAL_ADDRESS) i, 8,
					   ((u8 *) &table_header) + i);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}

		/* Allocate a buffer for the entire table */

		table_ptr = ACPI_MEM_ALLOCATE (table_header.length);
		if (!table_ptr) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Copy the header to the buffer */

		ACPI_MEMCPY (table_ptr, &table_header, sizeof (acpi_table_header));
		table_data_ptr = ACPI_PTR_ADD (u8, table_ptr, sizeof (acpi_table_header));

		/* Get the table from the op region */

		for (i = 0; i < table_header.length; i++) {
			status = acpi_ev_address_space_dispatch (obj_desc, ACPI_READ,
					   (ACPI_PHYSICAL_ADDRESS) i, 8,
					   ((u8 *) table_data_ptr + i));
			if (ACPI_FAILURE (status)) {
				goto cleanup;
			}
		}
		break;


	case ACPI_TYPE_BUFFER_FIELD:
	case INTERNAL_TYPE_REGION_FIELD:
	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:

		ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Load from Field %p %s\n",
			obj_desc, acpi_ut_get_object_type_name (obj_desc)));

		/*
		 * The length of the field must be at least as large as the table.
		 * Read the entire field and thus the entire table.  Buffer is
		 * allocated during the read.
		 */
		status = acpi_ex_read_data_from_field (walk_state, obj_desc, &buffer_desc);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		table_ptr = ACPI_CAST_PTR (acpi_table_header, buffer_desc->buffer.pointer);
		break;


	default:
		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}

	/* The table must be either an SSDT or a PSDT */

	if ((!ACPI_STRNCMP (table_ptr->signature,
			  acpi_gbl_acpi_table_data[ACPI_TABLE_PSDT].signature,
			  acpi_gbl_acpi_table_data[ACPI_TABLE_PSDT].sig_length)) &&
		(!ACPI_STRNCMP (table_ptr->signature,
				 acpi_gbl_acpi_table_data[ACPI_TABLE_SSDT].signature,
				 acpi_gbl_acpi_table_data[ACPI_TABLE_SSDT].sig_length))) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Table has invalid signature [%4.4s], must be SSDT or PSDT\n",
			table_ptr->signature));
		status = AE_BAD_SIGNATURE;
		goto cleanup;
	}

	/* Install the new table into the local data structures */

	status = acpi_ex_add_table (table_ptr, acpi_gbl_root_node, &ddb_handle);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Store the Ddb_handle into the Target operand */

	status = acpi_ex_store (ddb_handle, target, walk_state);
	if (ACPI_FAILURE (status)) {
		(void) acpi_ex_unload_table (ddb_handle);
	}

	return_ACPI_STATUS (status);


cleanup:

	if (buffer_desc) {
		acpi_ut_remove_reference (buffer_desc);
	}
	else {
		ACPI_MEM_FREE (table_ptr);
	}
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_unload_table
 *
 * PARAMETERS:  Ddb_handle          - Handle to a previously loaded table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unload an ACPI table
 *
 ******************************************************************************/

acpi_status
acpi_ex_unload_table (
	acpi_operand_object     *ddb_handle)
{
	acpi_status             status = AE_NOT_IMPLEMENTED;
	acpi_operand_object     *table_desc = ddb_handle;
	acpi_table_desc         *table_info;


	ACPI_FUNCTION_TRACE ("Ex_unload_table");


	/*
	 * Validate the handle
	 * Although the handle is partially validated in Acpi_ex_reconfiguration(),
	 * when it calls Acpi_ex_resolve_operands(), the handle is more completely
	 * validated here.
	 */
	if ((!ddb_handle) ||
		(ACPI_GET_DESCRIPTOR_TYPE (ddb_handle) != ACPI_DESC_TYPE_OPERAND) ||
		(ACPI_GET_OBJECT_TYPE (ddb_handle) != INTERNAL_TYPE_REFERENCE)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Get the actual table descriptor from the Ddb_handle */

	table_info = (acpi_table_desc *) table_desc->reference.object;

	/*
	 * Delete the entire namespace under this table Node
	 * (Offset contains the Table_id)
	 */
	acpi_ns_delete_namespace_by_owner (table_info->table_id);

	/* Delete the table itself */

	(void) acpi_tb_uninstall_table (table_info->installed_desc);

	/* Delete the table descriptor (Ddb_handle) */

	acpi_ut_remove_reference (table_desc);
	return_ACPI_STATUS (status);
}

