/******************************************************************************
 *
 * Module Name: tbinstal - ACPI table installation and removal
 *              $Revision: 62 $
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
#include "actables.h"


#define _COMPONENT          ACPI_TABLES
	 ACPI_MODULE_NAME    ("tbinstal")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_match_signature
 *
 * PARAMETERS:  Signature           - Table signature to match
 *              Table_info          - Return data
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Compare signature against the list of "ACPI-subsystem-owned"
 *              tables (DSDT/FADT/SSDT, etc.) Returns the Table_type_iD on match.
 *
 ******************************************************************************/

acpi_status
acpi_tb_match_signature (
	NATIVE_CHAR             *signature,
	acpi_table_desc         *table_info,
	u8                      search_type)
{
	NATIVE_UINT             i;


	ACPI_FUNCTION_TRACE ("Tb_match_signature");


	/*
	 * Search for a signature match among the known table types
	 */
	for (i = 0; i < NUM_ACPI_TABLES; i++) {
		if ((acpi_gbl_acpi_table_data[i].flags & ACPI_TABLE_TYPE_MASK) != search_type) {
			continue;
		}

		if (!ACPI_STRNCMP (signature, acpi_gbl_acpi_table_data[i].signature,
				   acpi_gbl_acpi_table_data[i].sig_length)) {
			/* Found a signature match, return index if requested */

			if (table_info) {
				table_info->type = (u8) i;
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				"Table [%4.4s] matched and is a required ACPI table\n",
				(char *) acpi_gbl_acpi_table_data[i].signature));

			return_ACPI_STATUS (AE_OK);
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Table [%4.4s] is not a required ACPI table - ignored\n",
		(char *) signature));

	return_ACPI_STATUS (AE_TABLE_NOT_SUPPORTED);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_install_table
 *
 * PARAMETERS:  Table_info          - Return value from Acpi_tb_get_table_body
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate all tables other than the RSDT.  The RSDT must
 *              already be loaded and validated.
 *              Install the table into the global data structs.
 *
 ******************************************************************************/

acpi_status
acpi_tb_install_table (
	acpi_table_desc         *table_info)
{
	acpi_status             status;

	ACPI_FUNCTION_TRACE ("Tb_install_table");


	/* Lock tables while installing */

	status = acpi_ut_acquire_mutex (ACPI_MTX_TABLES);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not acquire table mutex for [%4.4s], %s\n",
			table_info->pointer->signature, acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/* Install the table into the global data structure */

	status = acpi_tb_init_table_descriptor (table_info->type, table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not install ACPI table [%s], %s\n",
			table_info->pointer->signature, acpi_format_exception (status)));
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%s located at %p\n",
		acpi_gbl_acpi_table_data[table_info->type].name, table_info->pointer));

	(void) acpi_ut_release_mutex (ACPI_MTX_TABLES);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_recognize_table
 *
 * PARAMETERS:  Table_info          - Return value from Acpi_tb_get_table_body
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check a table signature for a match against known table types
 *
 * NOTE:  All table pointers are validated as follows:
 *          1) Table pointer must point to valid physical memory
 *          2) Signature must be 4 ASCII chars, even if we don't recognize the
 *             name
 *          3) Table must be readable for length specified in the header
 *          4) Table checksum must be valid (with the exception of the FACS
 *             which has no checksum for some odd reason)
 *
 ******************************************************************************/

acpi_status
acpi_tb_recognize_table (
	acpi_table_desc         *table_info,
	u8                      search_type)
{
	acpi_table_header       *table_header;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Tb_recognize_table");


	/* Ensure that we have a valid table pointer */

	table_header = (acpi_table_header *) table_info->pointer;
	if (!table_header) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * We only "recognize" a limited number of ACPI tables -- namely, the
	 * ones that are used by the subsystem (DSDT, FADT, etc.)
	 *
	 * An AE_TABLE_NOT_SUPPORTED means that the table was not recognized.
	 * This can be any one of many valid ACPI tables, it just isn't one of
	 * the tables that is consumed by the core subsystem
	 */
	status = acpi_tb_match_signature (table_header->signature, table_info, search_type);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_tb_validate_table_header (table_header);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Return the table type and length via the info struct */

	table_info->length = (ACPI_SIZE) table_header->length;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_init_table_descriptor
 *
 * PARAMETERS:  Table_type          - The type of the table
 *              Table_info          - A table info struct
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Install a table into the global data structs.
 *
 ******************************************************************************/

acpi_status
acpi_tb_init_table_descriptor (
	acpi_table_type         table_type,
	acpi_table_desc         *table_info)
{
	acpi_table_desc         *list_head;
	acpi_table_desc         *table_desc;


	ACPI_FUNCTION_TRACE_U32 ("Tb_init_table_descriptor", table_type);

	/*
	 * Install the table into the global data structure
	 */
	list_head   = &acpi_gbl_acpi_tables[table_type];
	table_desc  = list_head;

	/*
	 * Two major types of tables:  1) Only one instance is allowed.  This
	 * includes most ACPI tables such as the DSDT.  2) Multiple instances of
	 * the table are allowed.  This includes SSDT and PSDTs.
	 */
	if (ACPI_IS_SINGLE_TABLE (acpi_gbl_acpi_table_data[table_type].flags)) {
		/*
		 * Only one table allowed, and a table has alread been installed
		 *  at this location, so return an error.
		 */
		if (list_head->pointer) {
			return_ACPI_STATUS (AE_ALREADY_EXISTS);
		}

		table_desc->count = 1;
		table_desc->prev = NULL;
		table_desc->next = NULL;
	}
	else {
		/*
		 * Multiple tables allowed for this table type, we must link
		 * the new table in to the list of tables of this type.
		 */
		if (list_head->pointer) {
			table_desc = ACPI_MEM_CALLOCATE (sizeof (acpi_table_desc));
			if (!table_desc) {
				return_ACPI_STATUS (AE_NO_MEMORY);
			}

			list_head->count++;

			/* Update the original previous */

			list_head->prev->next = table_desc;

			/* Update new entry */

			table_desc->prev = list_head->prev;
			table_desc->next = list_head;

			/* Update list head */

			list_head->prev = table_desc;
		}
		else {
			table_desc->count = 1;
		}
	}

	/* Common initialization of the table descriptor */

	table_desc->type                = table_info->type;
	table_desc->pointer             = table_info->pointer;
	table_desc->base_pointer        = table_info->base_pointer;
	table_desc->length              = table_info->length;
	table_desc->allocation          = table_info->allocation;
	table_desc->aml_start           = (u8 *) (table_desc->pointer + 1),
	table_desc->aml_length          = (u32) (table_desc->length -
			 (u32) sizeof (acpi_table_header));
	table_desc->table_id            = acpi_ut_allocate_owner_id (ACPI_OWNER_TYPE_TABLE);
	table_desc->loaded_into_namespace = FALSE;

	/*
	 * Set the appropriate global pointer (if there is one) to point to the
	 * newly installed table
	 */
	if (acpi_gbl_acpi_table_data[table_type].global_ptr) {
		*(acpi_gbl_acpi_table_data[table_type].global_ptr) = table_info->pointer;
	}

	/* Return Data */

	table_info->table_id        = table_desc->table_id;
	table_info->installed_desc  = table_desc;

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_delete_acpi_tables
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all internal ACPI tables
 *
 ******************************************************************************/

void
acpi_tb_delete_acpi_tables (void)
{
	acpi_table_type             type;


	/*
	 * Free memory allocated for ACPI tables
	 * Memory can either be mapped or allocated
	 */
	for (type = 0; type < NUM_ACPI_TABLES; type++) {
		acpi_tb_delete_acpi_table (type);
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_delete_acpi_table
 *
 * PARAMETERS:  Type                - The table type to be deleted
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete an internal ACPI table
 *              Locks the ACPI table mutex
 *
 ******************************************************************************/

void
acpi_tb_delete_acpi_table (
	acpi_table_type             type)
{

	ACPI_FUNCTION_TRACE_U32 ("Tb_delete_acpi_table", type);


	if (type > ACPI_TABLE_MAX) {
		return_VOID;
	}

	if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_TABLES))) {
		return;
	}

	/* Clear the appropriate "typed" global table pointer */

	switch (type) {
	case ACPI_TABLE_RSDP:
		acpi_gbl_RSDP = NULL;
		break;

	case ACPI_TABLE_DSDT:
		acpi_gbl_DSDT = NULL;
		break;

	case ACPI_TABLE_FADT:
		acpi_gbl_FADT = NULL;
		break;

	case ACPI_TABLE_FACS:
		acpi_gbl_FACS = NULL;
		break;

	case ACPI_TABLE_XSDT:
		acpi_gbl_XSDT = NULL;
		break;

	case ACPI_TABLE_SSDT:
	case ACPI_TABLE_PSDT:
	default:
		break;
	}

	/* Free the table */

	acpi_tb_free_acpi_tables_of_type (&acpi_gbl_acpi_tables[type]);

	(void) acpi_ut_release_mutex (ACPI_MTX_TABLES);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_free_acpi_tables_of_type
 *
 * PARAMETERS:  Table_info          - A table info struct
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Free the memory associated with an internal ACPI table
 *              Table mutex should be locked.
 *
 ******************************************************************************/

void
acpi_tb_free_acpi_tables_of_type (
	acpi_table_desc         *list_head)
{
	acpi_table_desc         *table_desc;
	u32                     count;
	u32                     i;


	ACPI_FUNCTION_TRACE_PTR ("Tb_free_acpi_tables_of_type", list_head);


	/* Get the head of the list */

	table_desc  = list_head;
	count       = list_head->count;

	/*
	 * Walk the entire list, deleting both the allocated tables
	 * and the table descriptors
	 */
	for (i = 0; i < count; i++) {
		table_desc = acpi_tb_uninstall_table (table_desc);
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_delete_single_table
 *
 * PARAMETERS:  Table_info          - A table info struct
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Low-level free for a single ACPI table.  Handles cases where
 *              the table was allocated a buffer or was mapped.
 *
 ******************************************************************************/

void
acpi_tb_delete_single_table (
	acpi_table_desc         *table_desc)
{

	if (!table_desc) {
		return;
	}

	if (table_desc->pointer) {
		/* Valid table, determine type of memory allocation */

		switch (table_desc->allocation) {
		case ACPI_MEM_NOT_ALLOCATED:
			break;

		case ACPI_MEM_ALLOCATED:

			ACPI_MEM_FREE (table_desc->base_pointer);
			break;

		case ACPI_MEM_MAPPED:

			acpi_os_unmap_memory (table_desc->base_pointer, table_desc->length);
			break;

		default:
			break;
		}
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_uninstall_table
 *
 * PARAMETERS:  Table_info          - A table info struct
 *
 * RETURN:      Pointer to the next table in the list (of same type)
 *
 * DESCRIPTION: Free the memory associated with an internal ACPI table that
 *              is either installed or has never been installed.
 *              Table mutex should be locked.
 *
 ******************************************************************************/

acpi_table_desc *
acpi_tb_uninstall_table (
	acpi_table_desc         *table_desc)
{
	acpi_table_desc         *next_desc;


	ACPI_FUNCTION_TRACE_PTR ("Acpi_tb_uninstall_table", table_desc);


	if (!table_desc) {
		return_PTR (NULL);
	}

	/* Unlink the descriptor */

	if (table_desc->prev) {
		table_desc->prev->next = table_desc->next;
	}

	if (table_desc->next) {
		table_desc->next->prev = table_desc->prev;
	}

	/* Free the memory allocated for the table itself */

	acpi_tb_delete_single_table (table_desc);

	/* Free the table descriptor (Don't delete the list head, tho) */

	if ((table_desc->prev) == (table_desc->next)) {
		next_desc = NULL;

		/* Clear the list head */

		table_desc->pointer  = NULL;
		table_desc->length   = 0;
		table_desc->count    = 0;
	}
	else {
		/* Free the table descriptor */

		next_desc = table_desc->next;
		ACPI_MEM_FREE (table_desc);
	}

	return_PTR (next_desc);
}


