/******************************************************************************
 *
 * Module Name: tbutils - Table manipulation utilities
 *              $Revision: 42 $
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
#include "actables.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_TABLES
	 MODULE_NAME         ("tbutils")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_handle_to_object
 *
 * PARAMETERS:  Table_id            - Id for which the function is searching
 *              Table_desc          - Pointer to return the matching table
 *                                      descriptor.
 *
 * RETURN:      Search the tables to find one with a matching Table_id and
 *              return a pointer to that table descriptor.
 *
 ******************************************************************************/

acpi_status
acpi_tb_handle_to_object (
	u16                     table_id,
	acpi_table_desc         **table_desc)
{
	u32                     i;
	acpi_table_desc         *list_head;


	PROC_NAME ("Tb_handle_to_object");


	for (i = 0; i < ACPI_TABLE_MAX; i++) {
		list_head = &acpi_gbl_acpi_tables[i];
		do {
			if (list_head->table_id == table_id) {
				*table_desc = list_head;
				return (AE_OK);
			}

			list_head = list_head->next;

		} while (list_head != &acpi_gbl_acpi_tables[i]);
	}


	ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Table_id=%X does not exist\n", table_id));
	return (AE_BAD_PARAMETER);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_system_table_pointer
 *
 * PARAMETERS:  *Where              - Pointer to be examined
 *
 * RETURN:      TRUE if Where is within the AML stream (in one of the ACPI
 *              system tables such as the DSDT or an SSDT.)
 *              FALSE otherwise
 *
 ******************************************************************************/

u8
acpi_tb_system_table_pointer (
	void                    *where)
{
	u32                     i;
	acpi_table_desc         *table_desc;
	acpi_table_header       *table;


	/* No function trace, called too often! */


	/* Ignore null pointer */

	if (!where) {
		return (FALSE);
	}


	/* Check for a pointer within the DSDT */

	if ((acpi_gbl_DSDT) &&
		(IS_IN_ACPI_TABLE (where, acpi_gbl_DSDT))) {
		return (TRUE);
	}


	/* Check each of the loaded SSDTs (if any)*/

	table_desc = &acpi_gbl_acpi_tables[ACPI_TABLE_SSDT];

	for (i = 0; i < acpi_gbl_acpi_tables[ACPI_TABLE_SSDT].count; i++) {
		table = table_desc->pointer;

		if (IS_IN_ACPI_TABLE (where, table)) {
			return (TRUE);
		}

		table_desc = table_desc->next;
	}


	/* Check each of the loaded PSDTs (if any)*/

	table_desc = &acpi_gbl_acpi_tables[ACPI_TABLE_PSDT];

	for (i = 0; i < acpi_gbl_acpi_tables[ACPI_TABLE_PSDT].count; i++) {
		table = table_desc->pointer;

		if (IS_IN_ACPI_TABLE (where, table)) {
			return (TRUE);
		}

		table_desc = table_desc->next;
	}


	/* Pointer does not point into any system table */

	return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_validate_table_header
 *
 * PARAMETERS:  Table_header        - Logical pointer to the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Check an ACPI table header for validity
 *
 * NOTE:  Table pointers are validated as follows:
 *          1) Table pointer must point to valid physical memory
 *          2) Signature must be 4 ASCII chars, even if we don't recognize the
 *             name
 *          3) Table must be readable for length specified in the header
 *          4) Table checksum must be valid (with the exception of the FACS
 *              which has no checksum for some odd reason)
 *
 ******************************************************************************/

acpi_status
acpi_tb_validate_table_header (
	acpi_table_header       *table_header)
{
	acpi_name               signature;


	PROC_NAME ("Tb_validate_table_header");


	/* Verify that this is a valid address */

	if (!acpi_os_readable (table_header, sizeof (acpi_table_header))) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Cannot read table header at %p\n", table_header));
		return (AE_BAD_ADDRESS);
	}


	/* Ensure that the signature is 4 ASCII characters */

	MOVE_UNALIGNED32_TO_32 (&signature, &table_header->signature);
	if (!acpi_ut_valid_acpi_name (signature)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Table signature at %p [%p] has invalid characters\n",
			table_header, &signature));

		REPORT_WARNING (("Invalid table signature %4.4s found\n", (char*)&signature));
		DUMP_BUFFER (table_header, sizeof (acpi_table_header));
		return (AE_BAD_SIGNATURE);
	}


	/* Validate the table length */

	if (table_header->length < sizeof (acpi_table_header)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Invalid length in table header %p name %4.4s\n",
			table_header, (char*)&signature));

		REPORT_WARNING (("Invalid table header length found\n"));
		DUMP_BUFFER (table_header, sizeof (acpi_table_header));
		return (AE_BAD_HEADER);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_map_acpi_table
 *
 * PARAMETERS:  Physical_address        - Physical address of table to map
 *              *Size                   - Size of the table.  If zero, the size
 *                                        from the table header is used.
 *                                        Actual size is returned here.
 *              **Logical_address       - Logical address of mapped table
 *
 * RETURN:      Logical address of the mapped table.
 *
 * DESCRIPTION: Maps the physical address of table into a logical address
 *
 ******************************************************************************/

acpi_status
acpi_tb_map_acpi_table (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	u32                     *size,
	acpi_table_header       **logical_address)
{
	acpi_table_header       *table;
	u32                     table_size = *size;
	acpi_status             status = AE_OK;


	PROC_NAME ("Tb_map_acpi_table");


	/* If size is zero, look at the table header to get the actual size */

	if ((*size) == 0) {
		/* Get the table header so we can extract the table length */

		status = acpi_os_map_memory (physical_address, sizeof (acpi_table_header),
				  (void **) &table);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		/* Extract the full table length before we delete the mapping */

		table_size = table->length;

		/*
		 * Validate the header and delete the mapping.
		 * We will create a mapping for the full table below.
		 */
		status = acpi_tb_validate_table_header (table);

		/* Always unmap the memory for the header */

		acpi_os_unmap_memory (table, sizeof (acpi_table_header));

		/* Exit if header invalid */

		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}


	/* Map the physical memory for the correct length */

	status = acpi_os_map_memory (physical_address, table_size, (void **) &table);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Mapped memory for ACPI table, length=%d(%X) at %p\n",
		table_size, table_size, table));

	*size = table_size;
	*logical_address = table;

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_verify_table_checksum
 *
 * PARAMETERS:  *Table_header           - ACPI table to verify
 *
 * RETURN:      8 bit checksum of table
 *
 * DESCRIPTION: Does an 8 bit checksum of table and returns status.  A correct
 *              table should have a checksum of 0.
 *
 ******************************************************************************/

acpi_status
acpi_tb_verify_table_checksum (
	acpi_table_header       *table_header)
{
	u8                      checksum;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Tb_verify_table_checksum");


	/* Compute the checksum on the table */

	checksum = acpi_tb_checksum (table_header, table_header->length);

	/* Return the appropriate exception */

	if (checksum) {
		REPORT_WARNING (("Invalid checksum (%X) in table %4.4s\n",
			checksum, (char*)&table_header->signature));

		status = AE_BAD_CHECKSUM;
	}


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_checksum
 *
 * PARAMETERS:  Buffer              - Buffer to checksum
 *              Length              - Size of the buffer
 *
 * RETURNS      8 bit checksum of buffer
 *
 * DESCRIPTION: Computes an 8 bit checksum of the buffer(length) and returns it.
 *
 ******************************************************************************/

u8
acpi_tb_checksum (
	void                    *buffer,
	u32                     length)
{
	u8                      *limit;
	u8                      *rover;
	u8                      sum = 0;


	if (buffer && length) {
		/*  Buffer and Length are valid   */

		limit = (u8 *) buffer + length;

		for (rover = buffer; rover < limit; rover++) {
			sum = (u8) (sum + *rover);
		}
	}

	return (sum);
}


