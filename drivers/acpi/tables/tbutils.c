/******************************************************************************
 *
 * Module Name: tbutils - Table manipulation utilities
 *              $Revision: 56 $
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
	 ACPI_MODULE_NAME    ("tbutils")


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


	ACPI_FUNCTION_NAME ("Tb_handle_to_object");


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
 *              which has no checksum because it contains variable fields)
 *
 ******************************************************************************/

acpi_status
acpi_tb_validate_table_header (
	acpi_table_header       *table_header)
{
	acpi_name               signature;


	ACPI_FUNCTION_NAME ("Tb_validate_table_header");


	/* Verify that this is a valid address */

	if (!acpi_os_readable (table_header, sizeof (acpi_table_header))) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Cannot read table header at %p\n", table_header));
		return (AE_BAD_ADDRESS);
	}

	/* Ensure that the signature is 4 ASCII characters */

	ACPI_MOVE_UNALIGNED32_TO_32 (&signature, table_header->signature);
	if (!acpi_ut_valid_acpi_name (signature)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Table signature at %p [%p] has invalid characters\n",
			table_header, &signature));

		ACPI_REPORT_WARNING (("Invalid table signature found: [%4.4s]\n",
			(char *) &signature));
		ACPI_DUMP_BUFFER (table_header, sizeof (acpi_table_header));
		return (AE_BAD_SIGNATURE);
	}

	/* Validate the table length */

	if (table_header->length < sizeof (acpi_table_header)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Invalid length in table header %p name %4.4s\n",
			table_header, (char *) &signature));

		ACPI_REPORT_WARNING (("Invalid table header length (0x%X) found\n",
			(u32) table_header->length));
		ACPI_DUMP_BUFFER (table_header, sizeof (acpi_table_header));
		return (AE_BAD_HEADER);
	}

	return (AE_OK);
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


	ACPI_FUNCTION_TRACE ("Tb_verify_table_checksum");


	/* Compute the checksum on the table */

	checksum = acpi_tb_checksum (table_header, table_header->length);

	/* Return the appropriate exception */

	if (checksum) {
		ACPI_REPORT_WARNING (("Invalid checksum (%X) in table %4.4s\n",
			(u32) checksum, table_header->signature));

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
	const u8                *limit;
	const u8                *rover;
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


