/******************************************************************************
 *
 * Module Name: tbxface - Public interfaces to the ACPI subsystem
 *                         ACPI table oriented interfaces
 *              $Revision: 45 $
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
#include "acnamesp.h"
#include "acinterp.h"
#include "actables.h"


#define _COMPONENT          ACPI_TABLES
	 MODULE_NAME         ("tbxface")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_load_tables
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to load the ACPI tables from the
 *              provided RSDT
 *
 ******************************************************************************/

acpi_status
acpi_load_tables (void)
{
	ACPI_PHYSICAL_ADDRESS   rsdp_physical_address;
	acpi_status             status;
	u32                     number_of_tables = 0;


	FUNCTION_TRACE ("Acpi_load_tables");


	/* Get the RSDP */

	status = acpi_os_get_root_pointer (ACPI_LOGICAL_ADDRESSING,
			  &rsdp_physical_address);
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR (("Acpi_load_tables: Could not get RSDP, %s\n",
				  acpi_format_exception (status)));
		goto error_exit;
	}

	/* Map and validate the RSDP */

	status = acpi_tb_verify_rsdp (rsdp_physical_address);
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR (("Acpi_load_tables: RSDP Failed validation: %s\n",
				  acpi_format_exception (status)));
		goto error_exit;
	}

	/* Get the RSDT via the RSDP */

	status = acpi_tb_get_table_rsdt (&number_of_tables);
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR (("Acpi_load_tables: Could not load RSDT: %s\n",
				  acpi_format_exception (status)));
		goto error_exit;
	}

	/* Now get the rest of the tables */

	status = acpi_tb_get_all_tables (number_of_tables, NULL);
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR (("Acpi_load_tables: Error getting required tables (DSDT/FADT/FACS): %s\n",
				  acpi_format_exception (status)));
		goto error_exit;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_OK, "ACPI Tables successfully loaded\n"));


	/* Load the namespace from the tables */

	status = acpi_ns_load_namespace ();
	if (ACPI_FAILURE (status)) {
		REPORT_ERROR (("Acpi_load_tables: Could not load namespace: %s\n",
				  acpi_format_exception (status)));
		goto error_exit;
	}

	return_ACPI_STATUS (AE_OK);


error_exit:
	REPORT_ERROR (("Acpi_load_tables: Could not load tables: %s\n",
			  acpi_format_exception (status)));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_load_table
 *
 * PARAMETERS:  Table_ptr       - pointer to a buffer containing the entire
 *                                table to be loaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to load a table from the caller's
 *              buffer.  The buffer must contain an entire ACPI Table including
 *              a valid header.  The header fields will be verified, and if it
 *              is determined that the table is invalid, the call will fail.
 *
 ******************************************************************************/

acpi_status
acpi_load_table (
	acpi_table_header       *table_ptr)
{
	acpi_status             status;
	acpi_table_desc         table_info;


	FUNCTION_TRACE ("Acpi_load_table");


	if (!table_ptr) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Copy the table to a local buffer */

	status = acpi_tb_get_table (0, table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Install the new table into the local data structures */

	status = acpi_tb_install_table (NULL, &table_info);
	if (ACPI_FAILURE (status)) {
		/* Free table allocated by Acpi_tb_get_table */

		acpi_tb_delete_single_table (&table_info);
		return_ACPI_STATUS (status);
	}


	status = acpi_ns_load_table (table_info.installed_desc, acpi_gbl_root_node);
	if (ACPI_FAILURE (status)) {
		/* Uninstall table and free the buffer */

		acpi_tb_uninstall_table (table_info.installed_desc);
		return_ACPI_STATUS (status);
	}


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_unload_table
 *
 * PARAMETERS:  Table_type    - Type of table to be unloaded
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine is used to force the unload of a table
 *
 ******************************************************************************/

acpi_status
acpi_unload_table (
	acpi_table_type         table_type)
{
	acpi_table_desc         *list_head;


	FUNCTION_TRACE ("Acpi_unload_table");


	/* Parameter validation */

	if (table_type > ACPI_TABLE_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* Find all tables of the requested type */

	list_head = &acpi_gbl_acpi_tables[table_type];
	do {
		/*
		 * Delete all namespace entries owned by this table.  Note that these
		 * entries can appear anywhere in the namespace by virtue of the AML
		 * "Scope" operator.  Thus, we need to track ownership by an ID, not
		 * simply a position within the hierarchy
		 */
		acpi_ns_delete_namespace_by_owner (list_head->table_id);

		/* Delete (or unmap) the actual table */

		acpi_tb_delete_acpi_table (table_type);

	} while (list_head != &acpi_gbl_acpi_tables[table_type]);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_table_header
 *
 * PARAMETERS:  Table_type      - one of the defined table types
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *                                see Acpi_gbl_Acpi_table_flag
 *              Out_table_header - pointer to the acpi_table_header if successful
 *
 * DESCRIPTION: This function is called to get an ACPI table header.  The caller
 *              supplies an pointer to a data area sufficient to contain an ACPI
 *              acpi_table_header structure.
 *
 *              The header contains a length field that can be used to determine
 *              the size of the buffer needed to contain the entire table.  This
 *              function is not valid for the RSD PTR table since it does not
 *              have a standard header and is fixed length.
 *
 ******************************************************************************/

acpi_status
acpi_get_table_header (
	acpi_table_type         table_type,
	u32                     instance,
	acpi_table_header       *out_table_header)
{
	acpi_table_header       *tbl_ptr;
	acpi_status             status;


	FUNCTION_TRACE ("Acpi_get_table_header");


	if ((instance == 0)                 ||
		(table_type == ACPI_TABLE_RSDP) ||
		(!out_table_header)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Check the table type and instance */

	if ((table_type > ACPI_TABLE_MAX)   ||
		(IS_SINGLE_TABLE (acpi_gbl_acpi_table_data[table_type].flags) &&
		 instance > 1)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* Get a pointer to the entire table */

	status = acpi_tb_get_table_ptr (table_type, instance, &tbl_ptr);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * The function will return a NULL pointer if the table is not loaded
	 */
	if (tbl_ptr == NULL) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/*
	 * Copy the header to the caller's buffer
	 */
	MEMCPY ((void *) out_table_header, (void *) tbl_ptr,
			 sizeof (acpi_table_header));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_table
 *
 * PARAMETERS:  Table_type      - one of the defined table types
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *                                see Acpi_gbl_Acpi_table_flag
 *              Ret_buffer      - pointer to a structure containing a buffer to
 *                                receive the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get an ACPI table.  The caller
 *              supplies an Out_buffer large enough to contain the entire ACPI
 *              table.  The caller should call the Acpi_get_table_header function
 *              first to determine the buffer size needed.  Upon completion
 *              the Out_buffer->Length field will indicate the number of bytes
 *              copied into the Out_buffer->Buf_ptr buffer. This table will be
 *              a complete table including the header.
 *
 ******************************************************************************/

acpi_status
acpi_get_table (
	acpi_table_type         table_type,
	u32                     instance,
	acpi_buffer             *ret_buffer)
{
	acpi_table_header       *tbl_ptr;
	acpi_status             status;
	u32                     ret_buf_len;


	FUNCTION_TRACE ("Acpi_get_table");


	/*
	 *  If we have a buffer, we must have a length too
	 */
	if ((instance == 0)                 ||
		(!ret_buffer)                   ||
		((!ret_buffer->pointer) && (ret_buffer->length))) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Check the table type and instance */

	if ((table_type > ACPI_TABLE_MAX)   ||
		(IS_SINGLE_TABLE (acpi_gbl_acpi_table_data[table_type].flags) &&
		 instance > 1)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* Get a pointer to the entire table */

	status = acpi_tb_get_table_ptr (table_type, instance, &tbl_ptr);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Acpi_tb_get_table_ptr will return a NULL pointer if the
	 * table is not loaded.
	 */
	if (tbl_ptr == NULL) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/*
	 * Got a table ptr, assume it's ok and copy it to the user's buffer
	 */
	if (table_type == ACPI_TABLE_RSDP) {
		/*
		 *  RSD PTR is the only "table" without a header
		 */
		ret_buf_len = sizeof (RSDP_DESCRIPTOR);
	}
	else {
		ret_buf_len = tbl_ptr->length;
	}

	/*
	 * Verify we have space in the caller's buffer for the table
	 */
	if (ret_buffer->length < ret_buf_len) {
		ret_buffer->length = ret_buf_len;
		return_ACPI_STATUS (AE_BUFFER_OVERFLOW);
	}

	ret_buffer->length = ret_buf_len;

	MEMCPY ((void *) ret_buffer->pointer, (void *) tbl_ptr, ret_buf_len);

	return_ACPI_STATUS (AE_OK);
}


