/******************************************************************************
 *
 * Module Name: tbget - ACPI Table get* routines
 *              $Revision: 80 $
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
	 ACPI_MODULE_NAME    ("tbget")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              Table_info          - Where table info is returned
 *
 * RETURN:      None
 *
 * DESCRIPTION: Get entire table of unknown size.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table (
	ACPI_POINTER            *address,
	acpi_table_desc         *table_info)
{
	acpi_status             status;
	acpi_table_header       header;


	ACPI_FUNCTION_TRACE ("Tb_get_table");


	/*
	 * Get the header in order to get signature and table size
	 */
	status = acpi_tb_get_table_header (address, &header);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get the entire table */

	status = acpi_tb_get_table_body (address, &header, table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not get ACPI table (size %X), %s\n",
			header.length, acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_header
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              Return_header       - Where the table header is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an ACPI table header.  Works in both physical or virtual
 *              addressing mode.  Works with both physical or logical pointers.
 *              Table is either copied or mapped, depending on the pointer
 *              type and mode of the processor.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_header (
	ACPI_POINTER            *address,
	acpi_table_header       *return_header)
{
	acpi_status             status = AE_OK;
	acpi_table_header       *header = NULL;


	ACPI_FUNCTION_TRACE ("Tb_get_table_header");


	/*
	 * Flags contains the current processor mode (Virtual or Physical addressing)
	 * The Pointer_type is either Logical or Physical
	 */
	switch (address->pointer_type) {
	case ACPI_PHYSMODE_PHYSPTR:
	case ACPI_LOGMODE_LOGPTR:

		/* Pointer matches processor mode, copy the header */

		ACPI_MEMCPY (return_header, address->pointer.logical, sizeof (acpi_table_header));
		break;


	case ACPI_LOGMODE_PHYSPTR:

		/* Create a logical address for the physical pointer*/

		status = acpi_os_map_memory (address->pointer.physical, sizeof (acpi_table_header),
				  (void **) &header);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Could not map memory at %8.8X%8.8X for length %X\n",
				ACPI_HIDWORD (address->pointer.physical),
				ACPI_LODWORD (address->pointer.physical),
				sizeof (acpi_table_header)));
			return_ACPI_STATUS (status);
		}

		/* Copy header and delete mapping */

		ACPI_MEMCPY (return_header, header, sizeof (acpi_table_header));
		acpi_os_unmap_memory (header, sizeof (acpi_table_header));
		break;


	default:

		ACPI_REPORT_ERROR (("Invalid address flags %X\n",
			address->pointer_type));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_body
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              Header              - Header of the table to retrieve
 *              Table_info          - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an entire ACPI table with support to allow the host OS to
 *              replace the table with a newer version (table override.)
 *              Works in both physical or virtual
 *              addressing mode.  Works with both physical or logical pointers.
 *              Table is either copied or mapped, depending on the pointer
 *              type and mode of the processor.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_body (
	ACPI_POINTER            *address,
	acpi_table_header       *header,
	acpi_table_desc         *table_info)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Tb_get_table_body");


	if (!table_info || !address) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Attempt table override.
	 */
	status = acpi_tb_table_override (header, table_info);
	if (ACPI_SUCCESS (status)) {
		/* Table was overridden by the host OS */

		return_ACPI_STATUS (status);
	}

	/* No override, get the original table */

	status = acpi_tb_get_this_table (address, header, table_info);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_table_override
 *
 * PARAMETERS:  Header              - Pointer to table header
 *              Table_info          - Return info if table is overridden
 *
 * RETURN:      None
 *
 * DESCRIPTION: Attempts override of current table with a new one if provided
 *              by the host OS.
 *
 ******************************************************************************/

acpi_status
acpi_tb_table_override (
	acpi_table_header       *header,
	acpi_table_desc         *table_info)
{
	acpi_table_header       *new_table;
	acpi_status             status;
	ACPI_POINTER            address;


	ACPI_FUNCTION_TRACE ("Tb_table_override");


	/*
	 * The OSL will examine the header and decide whether to override this
	 * table.  If it decides to override, a table will be returned in New_table,
	 * which we will then copy.
	 */
	status = acpi_os_table_override (header, &new_table);
	if (ACPI_FAILURE (status)) {
		/* Some severe error from the OSL, but we basically ignore it */

		ACPI_REPORT_ERROR (("Could not override ACPI table, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	if (!new_table) {
		/* No table override */

		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	/*
	 * We have a new table to override the old one.  Get a copy of
	 * the new one.  We know that the new table has a logical pointer.
	 */
	address.pointer_type    = ACPI_LOGICAL_POINTER | ACPI_LOGICAL_ADDRESSING;
	address.pointer.logical = new_table;

	status = acpi_tb_get_this_table (&address, new_table, table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not copy override ACPI table, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/* Copy the table info */

	ACPI_REPORT_INFO (("Table [%4.4s] replaced by host OS\n",
		table_info->pointer->signature));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_this_table
 *
 * PARAMETERS:  Address             - Address of table to retrieve.  Can be
 *                                    Logical or Physical
 *              Header              - Header of the table to retrieve
 *              Table_info          - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get an entire ACPI table.  Works in both physical or virtual
 *              addressing mode.  Works with both physical or logical pointers.
 *              Table is either copied or mapped, depending on the pointer
 *              type and mode of the processor.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_this_table (
	ACPI_POINTER            *address,
	acpi_table_header       *header,
	acpi_table_desc         *table_info)
{
	acpi_table_header       *full_table = NULL;
	u8                      allocation;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Tb_get_this_table");


	/*
	 * Flags contains the current processor mode (Virtual or Physical addressing)
	 * The Pointer_type is either Logical or Physical
	 */
	switch (address->pointer_type) {
	case ACPI_PHYSMODE_PHYSPTR:
	case ACPI_LOGMODE_LOGPTR:

		/* Pointer matches processor mode, copy the table to a new buffer */

		full_table = ACPI_MEM_ALLOCATE (header->length);
		if (!full_table) {
			ACPI_REPORT_ERROR (("Could not allocate table memory for [%4.4s] length %X\n",
				header->signature, header->length));
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Copy the entire table (including header) to the local buffer */

		ACPI_MEMCPY (full_table, address->pointer.logical, header->length);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
		break;


	case ACPI_LOGMODE_PHYSPTR:

		/*
		 * Just map the table's physical memory
		 * into our address space.
		 */
		status = acpi_os_map_memory (address->pointer.physical, (ACPI_SIZE) header->length,
				  (void **) &full_table);
		if (ACPI_FAILURE (status)) {
			ACPI_REPORT_ERROR (("Could not map memory for table [%4.4s] at %8.8X%8.8X for length %X\n",
				header->signature,
				ACPI_HIDWORD (address->pointer.physical),
				ACPI_LODWORD (address->pointer.physical), header->length));
			return (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid address flags %X\n",
			address->pointer_type));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Validate checksum for _most_ tables,
	 * even the ones whose signature we don't recognize
	 */
	if (table_info->type != ACPI_TABLE_FACS) {
		status = acpi_tb_verify_table_checksum (full_table);

#if (!ACPI_CHECKSUM_ABORT)
		if (ACPI_FAILURE (status)) {
			/* Ignore the error if configuration says so */

			status = AE_OK;
		}
#endif
	}

	/* Return values */

	table_info->pointer     = full_table;
	table_info->length      = (ACPI_SIZE) header->length;
	table_info->allocation  = allocation;
	table_info->base_pointer = full_table;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Found table [%4.4s] at %8.8X%8.8X, mapped/copied to %p\n",
		full_table->signature,
		ACPI_HIDWORD (address->pointer.physical),
		ACPI_LODWORD (address->pointer.physical), full_table));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_ptr
 *
 * PARAMETERS:  Table_type      - one of the defined table types
 *              Instance        - Which table of this type
 *              Table_ptr_loc   - pointer to location to place the pointer for
 *                                return
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get the pointer to an ACPI table.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_ptr (
	acpi_table_type         table_type,
	u32                     instance,
	acpi_table_header       **table_ptr_loc)
{
	acpi_table_desc         *table_desc;
	u32                     i;


	ACPI_FUNCTION_TRACE ("Tb_get_table_ptr");


	if (!acpi_gbl_DSDT) {
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	if (table_type > ACPI_TABLE_MAX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * For all table types (Single/Multiple), the first
	 * instance is always in the list head.
	 */
	if (instance == 1) {
		/*
		 * Just pluck the pointer out of the global table!
		 * Will be null if no table is present
		 */
		*table_ptr_loc = acpi_gbl_acpi_tables[table_type].pointer;
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Check for instance out of range
	 */
	if (instance > acpi_gbl_acpi_tables[table_type].count) {
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/* Walk the list to get the desired table
	 * Since the if (Instance == 1) check above checked for the
	 * first table, setting Table_desc equal to the .Next member
	 * is actually pointing to the second table.  Therefore, we
	 * need to walk from the 2nd table until we reach the Instance
	 * that the user is looking for and return its table pointer.
	 */
	table_desc = acpi_gbl_acpi_tables[table_type].next;
	for (i = 2; i < instance; i++) {
		table_desc = table_desc->next;
	}

	/* We are now pointing to the requested table's descriptor */

	*table_ptr_loc = table_desc->pointer;

	return_ACPI_STATUS (AE_OK);
}

