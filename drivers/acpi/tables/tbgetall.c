/******************************************************************************
 *
 * Module Name: tbgetall - Get all required ACPI tables
 *              $Revision: 2 $
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
	 ACPI_MODULE_NAME    ("tbgetall")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_primary_table
 *
 * PARAMETERS:  Address             - Physical address of table to retrieve
 *              *Table_info         - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Maps the physical address of table into a logical address
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_primary_table (
	ACPI_POINTER            *address,
	acpi_table_desc         *table_info)
{
	acpi_status             status;
	acpi_table_header       header;


	ACPI_FUNCTION_TRACE ("Tb_get_primary_table");


	/* Ignore a NULL address in the RSDT */

	if (!address->pointer.value) {
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Get the header in order to get signature and table size
	 */
	status = acpi_tb_get_table_header (address, &header);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Clear the Table_info */

	ACPI_MEMSET (table_info, 0, sizeof (acpi_table_desc));

	/*
	 * Check the table signature and make sure it is recognized.
	 * Also checks the header checksum
	 */
	table_info->pointer = &header;
	status = acpi_tb_recognize_table (table_info, ACPI_TABLE_PRIMARY);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get the entire table */

	status = acpi_tb_get_table_body (address, &header, table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Install the table */

	status = acpi_tb_install_table (table_info);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_secondary_table
 *
 * PARAMETERS:  Address             - Physical address of table to retrieve
 *              *Table_info         - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Maps the physical address of table into a logical address
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_secondary_table (
	ACPI_POINTER            *address,
	acpi_string             signature,
	acpi_table_desc         *table_info)
{
	acpi_status             status;
	acpi_table_header       header;


	ACPI_FUNCTION_TRACE_STR ("Tb_get_secondary_table", signature);


	/* Get the header in order to match the signature */

	status = acpi_tb_get_table_header (address, &header);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Signature must match request */

	if (ACPI_STRNCMP (header.signature, signature, ACPI_NAME_SIZE)) {
		ACPI_REPORT_ERROR (("Incorrect table signature - wanted [%s] found [%4.4s]\n",
			signature, header.signature));
		return_ACPI_STATUS (AE_BAD_SIGNATURE);
	}

	/*
	 * Check the table signature and make sure it is recognized.
	 * Also checks the header checksum
	 */
	table_info->pointer = &header;
	status = acpi_tb_recognize_table (table_info, ACPI_TABLE_SECONDARY);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get the entire table */

	status = acpi_tb_get_table_body (address, &header, table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Install the table */

	status = acpi_tb_install_table (table_info);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_required_tables
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate tables other than the RSDT.  The RSDT must
 *              already be loaded and validated.
 *
 *              Get the minimum set of ACPI tables, namely:
 *
 *              1) FADT (via RSDT in loop below)
 *              2) FACS (via FADT)
 *              3) DSDT (via FADT)
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_required_tables (
	void)
{
	acpi_status             status = AE_OK;
	u32                     i;
	acpi_table_desc         table_info;
	ACPI_POINTER            address;


	ACPI_FUNCTION_TRACE ("Tb_get_required_tables");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%d ACPI tables in RSDT\n",
		acpi_gbl_rsdt_table_count));


	address.pointer_type  = acpi_gbl_table_flags | ACPI_LOGICAL_ADDRESSING;

	/*
	 * Loop through all table pointers found in RSDT.
	 * This will NOT include the FACS and DSDT - we must get
	 * them after the loop.
	 *
	 * The only tables we are interested in getting here is the FADT and
	 * any SSDTs.
	 */
	for (i = 0; i < acpi_gbl_rsdt_table_count; i++) {
		/* Get the table addresss from the common internal XSDT */

		address.pointer.value = ACPI_GET_ADDRESS (acpi_gbl_XSDT->table_offset_entry[i]);

		/*
		 * Get the tables needed by this subsystem (FADT and any SSDTs).
		 * NOTE: All other tables are completely ignored at this time.
		 */
		acpi_tb_get_primary_table (&address, &table_info);
	}

	/* We must have a FADT to continue */

	if (!acpi_gbl_FADT) {
		ACPI_REPORT_ERROR (("No FADT present in RSDT/XSDT\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	/*
	 * Convert the FADT to a common format.  This allows earlier revisions of the
	 * table to coexist with newer versions, using common access code.
	 */
	status = acpi_tb_convert_table_fadt ();
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not convert FADT to internal common format\n"));
		return_ACPI_STATUS (status);
	}

	/*
	 * Get the FACS (Pointed to by the FADT)
	 */
	address.pointer.value = ACPI_GET_ADDRESS (acpi_gbl_FADT->Xfirmware_ctrl);

	status = acpi_tb_get_secondary_table (&address, FACS_SIG, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not get/install the FACS, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/*
	 * Create the common FACS pointer table
	 * (Contains pointers to the original table)
	 */
	status = acpi_tb_build_common_facs (&table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Get/install the DSDT (Pointed to by the FADT)
	 */
	address.pointer.value = ACPI_GET_ADDRESS (acpi_gbl_FADT->Xdsdt);

	status = acpi_tb_get_secondary_table (&address, DSDT_SIG, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not get/install the DSDT\n"));
		return_ACPI_STATUS (status);
	}

	/* Set Integer Width (32/64) based upon DSDT revision */

	acpi_ut_set_integer_width (acpi_gbl_DSDT->revision);

	/* Dump the entire DSDT */

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES,
		"Hex dump of entire DSDT, size %d (0x%X), Integer width = %d\n",
		acpi_gbl_DSDT->length, acpi_gbl_DSDT->length, acpi_gbl_integer_bit_width));
	ACPI_DUMP_BUFFER ((u8 *) acpi_gbl_DSDT, acpi_gbl_DSDT->length);

	/* Always delete the RSDP mapping, we are done with it */

	acpi_tb_delete_acpi_table (ACPI_TABLE_RSDP);
	return_ACPI_STATUS (status);
}


