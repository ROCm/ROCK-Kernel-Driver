/******************************************************************************
 *
 * Module Name: exconfig - Namespace reconfiguration (Load/Unload opcodes)
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acevents.h"
#include "actables.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exconfig")


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_load_table_op
 *
 * PARAMETERS:  Rgn_desc        - Op region where the table will be obtained
 *              Ddb_handle      - Where a handle to the table will be returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load an ACPI table
 *
 ****************************************************************************/

acpi_status
acpi_ex_load_op (
	acpi_operand_object     *rgn_desc,
	acpi_operand_object     *ddb_handle)
{
	acpi_status             status;
	acpi_operand_object     *table_desc = NULL;
	u8                      *table_ptr;
	u8                      *table_data_ptr;
	acpi_table_header       table_header;
	acpi_table_desc         table_info;
	u32                     i;


	FUNCTION_TRACE ("Ex_load_op");

	/* TBD: [Unhandled] Object can be either a field or an opregion */


	/* Get the table header */

	table_header.length = 0;
	for (i = 0; i < sizeof (acpi_table_header); i++) {
		status = acpi_ev_address_space_dispatch (rgn_desc, ACPI_READ_ADR_SPACE,
				   (ACPI_PHYSICAL_ADDRESS) i, 8,
				   (u32 *) ((u8 *) &table_header + i));
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

	MEMCPY (table_ptr, &table_header, sizeof (acpi_table_header));
	table_data_ptr = table_ptr + sizeof (acpi_table_header);


	/* Get the table from the op region */

	for (i = 0; i < table_header.length; i++) {
		status = acpi_ev_address_space_dispatch (rgn_desc, ACPI_READ_ADR_SPACE,
				   (ACPI_PHYSICAL_ADDRESS) i, 8,
				   (u32 *) (table_data_ptr + i));
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}
	}


	/* Table must be either an SSDT or a PSDT */

	if ((!STRNCMP (table_header.signature,
			  acpi_gbl_acpi_table_data[ACPI_TABLE_PSDT].signature,
			  acpi_gbl_acpi_table_data[ACPI_TABLE_PSDT].sig_length)) &&
		(!STRNCMP (table_header.signature,
				 acpi_gbl_acpi_table_data[ACPI_TABLE_SSDT].signature,
				 acpi_gbl_acpi_table_data[ACPI_TABLE_SSDT].sig_length))) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Table has invalid signature [%4.4s], must be SSDT or PSDT\n",
			(char*)table_header.signature));
		status = AE_BAD_SIGNATURE;
		goto cleanup;
	}

	/* Create an object to be the table handle */

	table_desc = acpi_ut_create_internal_object (INTERNAL_TYPE_REFERENCE);
	if (!table_desc) {
		status = AE_NO_MEMORY;
		goto cleanup;
	}


	/* Install the new table into the local data structures */

	table_info.pointer     = (acpi_table_header *) table_ptr;
	table_info.length      = table_header.length;
	table_info.allocation  = ACPI_MEM_ALLOCATED;
	table_info.base_pointer = table_ptr;

	status = acpi_tb_install_table (NULL, &table_info);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Add the table to the namespace */

	/* TBD: [Restructure] - change to whatever new interface is appropriate */
/*
	Status = Acpi_load_namespace ();
	if (ACPI_FAILURE (Status))
	{
*/
		/* TBD: [Errors] Unload the table on failure ? */
/*
		goto Cleanup;
	}
*/


	/* TBD: [Investigate] we need a pointer to the table desc */

	/* Init the table handle */

	table_desc->reference.opcode = AML_LOAD_OP;
	table_desc->reference.object = table_info.installed_desc;

	/* TBD: store the tabledesc into the Ddb_handle target */
	/* Ddb_handle = Table_desc; */

	return_ACPI_STATUS (status);


cleanup:

	ACPI_MEM_FREE (table_desc);
	ACPI_MEM_FREE (table_ptr);
	return_ACPI_STATUS (status);
}


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ex_unload_table
 *
 * PARAMETERS:  Ddb_handle          - Handle to a previously loaded table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Unload an ACPI table
 *
 ****************************************************************************/

acpi_status
acpi_ex_unload_table (
	acpi_operand_object     *ddb_handle)
{
	acpi_status             status = AE_NOT_IMPLEMENTED;
	acpi_operand_object     *table_desc = ddb_handle;
	acpi_table_desc         *table_info;


	FUNCTION_TRACE ("Ex_unload_table");


	/*
	 * Validate the handle
	 * Although the handle is partially validated in Acpi_ex_reconfiguration(),
	 * when it calls Acpi_ex_resolve_operands(), the handle is more completely
	 * validated here.
	 */
	if ((!ddb_handle) ||
		(!VALID_DESCRIPTOR_TYPE (ddb_handle, ACPI_DESC_TYPE_INTERNAL)) ||
		(((acpi_operand_object  *)ddb_handle)->common.type !=
				INTERNAL_TYPE_REFERENCE)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Get the actual table descriptor from the Ddb_handle */

	table_info = (acpi_table_desc *) table_desc->reference.object;

	/*
	 * Delete the entire namespace under this table Node
	 * (Offset contains the Table_id)
	 */
	status = acpi_ns_delete_namespace_by_owner (table_info->table_id);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Delete the table itself */

	acpi_tb_uninstall_table (table_info->installed_desc);

	/* Delete the table descriptor (Ddb_handle) */

	acpi_ut_remove_reference (table_desc);

	return_ACPI_STATUS (status);
}

