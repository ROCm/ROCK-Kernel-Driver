/******************************************************************************
 *
 * Module Name: tbget - ACPI Table get* routines
 *              $Revision: 56 $
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
#include "achware.h"
#include "actables.h"


#define _COMPONENT          ACPI_TABLES
	 MODULE_NAME         ("tbget")

#define RSDP_CHECKSUM_LENGTH 20


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


	FUNCTION_TRACE ("Tb_get_table_ptr");


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


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table
 *
 * PARAMETERS:  Physical_address        - Physical address of table to retrieve
 *              *Buffer_ptr             - If Buffer_ptr is valid, read data from
 *                                         buffer rather than searching memory
 *              *Table_info             - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Maps the physical address of table into a logical address
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	acpi_table_header       *buffer_ptr,
	acpi_table_desc         *table_info)
{
	acpi_table_header       *table_header = NULL;
	acpi_table_header       *full_table = NULL;
	u32                     size;
	u8                      allocation;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Tb_get_table");


	if (!table_info) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	if (buffer_ptr) {
		/*
		 * Getting data from a buffer, not BIOS tables
		 */
		table_header = buffer_ptr;
		status = acpi_tb_validate_table_header (table_header);
		if (ACPI_FAILURE (status)) {
			/* Table failed verification, map all errors to BAD_DATA */

			return_ACPI_STATUS (AE_BAD_DATA);
		}

		/* Allocate buffer for the entire table */

		full_table = ACPI_MEM_ALLOCATE (table_header->length);
		if (!full_table) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Copy the entire table (including header) to the local buffer */

		size = table_header->length;
		MEMCPY (full_table, buffer_ptr, size);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
	}


	/*
	 * Not reading from a buffer, just map the table's physical memory
	 * into our address space.
	 */
	else {
		size = SIZE_IN_HEADER;

		status = acpi_tb_map_acpi_table (physical_address, &size, &full_table);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
	}


	/* Return values */

	table_info->pointer     = full_table;
	table_info->length      = size;
	table_info->allocation  = allocation;
	table_info->base_pointer = full_table;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_all_tables
 *
 * PARAMETERS:  Number_of_tables    - Number of tables to get
 *              Table_ptr           - Input buffer pointer, optional
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate all tables other than the RSDT.  The RSDT must
 *              already be loaded and validated.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_all_tables (
	u32                     number_of_tables,
	acpi_table_header       *table_ptr)
{
	acpi_status             status = AE_OK;
	u32                     index;
	acpi_table_desc         table_info;


	FUNCTION_TRACE ("Tb_get_all_tables");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Number of tables: %d\n", number_of_tables));


	/*
	 * Loop through all table pointers found in RSDT.
	 * This will NOT include the FACS and DSDT - we must get
	 * them after the loop
	 */
	for (index = 0; index < number_of_tables; index++) {
		/* Clear the Table_info each time */

		MEMSET (&table_info, 0, sizeof (acpi_table_desc));

		/* Get the table via the XSDT */

		status = acpi_tb_get_table ((ACPI_PHYSICAL_ADDRESS)
				 ACPI_GET_ADDRESS (acpi_gbl_XSDT->table_offset_entry[index]),
				 table_ptr, &table_info);

		/* Ignore a table that failed verification */

		if (status == AE_BAD_DATA) {
			continue;
		}

		/* However, abort on serious errors */

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Recognize and install the table */

		status = acpi_tb_install_table (table_ptr, &table_info);
		if (ACPI_FAILURE (status)) {
			/*
			 * Unrecognized or unsupported table, delete it and ignore the
			 * error.  Just get as many tables as we can, later we will
			 * determine if there are enough tables to continue.
			 */
			acpi_tb_uninstall_table (&table_info);
		}
	}


	/*
	 * Convert the FADT to a common format.  This allows earlier revisions of the
	 * table to coexist with newer versions, using common access code.
	 */
	status = acpi_tb_convert_table_fadt ();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}


	/*
	 * Get the minimum set of ACPI tables, namely:
	 *
	 * 1) FADT (via RSDT in loop above)
	 * 2) FACS
	 * 3) DSDT
	 *
	 */

	/*
	 * Get the FACS (must have the FADT first, from loop above)
	 * Acpi_tb_get_table_facs will fail if FADT pointer is not valid
	 */
	status = acpi_tb_get_table_facs (table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Install the FACS */

	status = acpi_tb_install_table (table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
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
	 * Get the DSDT (We know that the FADT is valid now)
	 */
	status = acpi_tb_get_table ((ACPI_PHYSICAL_ADDRESS) ACPI_GET_ADDRESS (acpi_gbl_FADT->Xdsdt),
			  table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Install the DSDT */

	status = acpi_tb_install_table (table_ptr, &table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Dump the DSDT Header */

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "Hex dump of DSDT Header:\n"));
	DUMP_BUFFER ((u8 *) acpi_gbl_DSDT, sizeof (acpi_table_header));

	/* Dump the entire DSDT */

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES,
		"Hex dump of DSDT (After header), size %d (%x)\n",
		acpi_gbl_DSDT->length, acpi_gbl_DSDT->length));
	DUMP_BUFFER ((u8 *) (acpi_gbl_DSDT + 1), acpi_gbl_DSDT->length);

	/*
	 * Initialize the capabilities flags.
	 * Assumes that platform supports ACPI_MODE since we have tables!
	 */
	acpi_gbl_system_flags |= acpi_hw_get_mode_capabilities ();


	/* Always delete the RSDP mapping, we are done with it */

	acpi_tb_delete_acpi_table (ACPI_TABLE_RSDP);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_verify_rsdp
 *
 * PARAMETERS:  Number_of_tables    - Where the table count is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate the RSDP (ptr) and RSDT (table)
 *
 ******************************************************************************/

acpi_status
acpi_tb_verify_rsdp (
	ACPI_PHYSICAL_ADDRESS   rsdp_physical_address)
{
	acpi_table_desc         table_info;
	acpi_status             status;
	u8                      *table_ptr;


	FUNCTION_TRACE ("Tb_verify_rsdp");


	/*
	 * Obtain access to the RSDP structure
	 */
	status = acpi_os_map_memory (rsdp_physical_address, sizeof (RSDP_DESCRIPTOR),
			  (void **) &table_ptr);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 *  The signature and checksum must both be correct
	 */
	if (STRNCMP ((NATIVE_CHAR *) table_ptr, RSDP_SIG, sizeof (RSDP_SIG)-1) != 0) {
		/* Nope, BAD Signature */

		status = AE_BAD_SIGNATURE;
		goto cleanup;
	}

	if (acpi_tb_checksum (table_ptr, RSDP_CHECKSUM_LENGTH) != 0) {
		/* Nope, BAD Checksum */

		status = AE_BAD_CHECKSUM;
		goto cleanup;
	}

	/* TBD: Check extended checksum if table version >= 2 */

	/* The RSDP supplied is OK */

	table_info.pointer     = (acpi_table_header *) table_ptr;
	table_info.length      = sizeof (RSDP_DESCRIPTOR);
	table_info.allocation  = ACPI_MEM_MAPPED;
	table_info.base_pointer = table_ptr;

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor (ACPI_TABLE_RSDP, &table_info);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}


	/* Save the RSDP in a global for easy access */

	acpi_gbl_RSDP = (RSDP_DESCRIPTOR *) table_info.pointer;
	return_ACPI_STATUS (status);


	/* Error exit */
cleanup:

	acpi_os_unmap_memory (table_ptr, sizeof (RSDP_DESCRIPTOR));
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_rsdt_address
 *
 * PARAMETERS:  None
 *
 * RETURN:      RSDT physical address
 *
 * DESCRIPTION: Extract the address of the RSDT or XSDT, depending on the
 *              version of the RSDP
 *
 ******************************************************************************/

ACPI_PHYSICAL_ADDRESS
acpi_tb_get_rsdt_address (void)
{
	ACPI_PHYSICAL_ADDRESS   physical_address;


	FUNCTION_ENTRY ();


	/*
	 * For RSDP revision 0 or 1, we use the RSDT.
	 * For RSDP revision 2 (and above), we use the XSDT
	 */
	if (acpi_gbl_RSDP->revision < 2) {
#ifdef _IA64
		/* 0.71 RSDP has 64bit Rsdt address field */
		physical_address = ((RSDP_DESCRIPTOR_REV071 *)acpi_gbl_RSDP)->rsdt_physical_address;
#else
		physical_address = (ACPI_PHYSICAL_ADDRESS) acpi_gbl_RSDP->rsdt_physical_address;
#endif
	}

	else {
		physical_address = (ACPI_PHYSICAL_ADDRESS)
				   ACPI_GET_ADDRESS (acpi_gbl_RSDP->xsdt_physical_address);
	}

	return (physical_address);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_validate_rsdt
 *
 * PARAMETERS:  Table_ptr       - Addressable pointer to the RSDT.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Validate signature for the RSDT or XSDT
 *
 ******************************************************************************/

acpi_status
acpi_tb_validate_rsdt (
	acpi_table_header       *table_ptr)
{
	u32                     no_match;


	PROC_NAME ("Tb_validate_rsdt");


	/*
	 * For RSDP revision 0 or 1, we use the RSDT.
	 * For RSDP revision 2 (and above), we use the XSDT
	 */
	if (acpi_gbl_RSDP->revision < 2) {
		no_match = STRNCMP ((char *) table_ptr, RSDT_SIG,
				  sizeof (RSDT_SIG) -1);
	}
	else {
		no_match = STRNCMP ((char *) table_ptr, XSDT_SIG,
				  sizeof (XSDT_SIG) -1);
	}


	if (no_match) {
		/* Invalid RSDT or XSDT signature */

		REPORT_ERROR (("Invalid signature where RSDP indicates RSDT/XSDT should be located\n"));

		DUMP_BUFFER (acpi_gbl_RSDP, 20);

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ERROR,
			"RSDT/XSDT signature at %X is invalid\n",
			acpi_gbl_RSDP->rsdt_physical_address));

		return (AE_BAD_SIGNATURE);
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_pointer
 *
 * PARAMETERS:  Physical_address    - Address from RSDT
 *              Flags               - virtual or physical addressing
 *              Table_ptr           - Addressable address (output)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create an addressable pointer to an ACPI table
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_pointer (
	ACPI_PHYSICAL_ADDRESS   physical_address,
	u32                     flags,
	u32                     *size,
	acpi_table_header       **table_ptr)
{
	acpi_status             status;


	FUNCTION_ENTRY ();


	if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
		*size = SIZE_IN_HEADER;
		status = acpi_tb_map_acpi_table (physical_address, size, table_ptr);
	}

	else {
		*size = 0;
		*table_ptr = (acpi_table_header *) (ACPI_TBLPTR) physical_address;

		status = AE_OK;
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_rsdt
 *
 * PARAMETERS:  Number_of_tables    - Where the table count is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate the RSDP (ptr) and RSDT (table)
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_rsdt (
	u32                     *number_of_tables)
{
	acpi_table_desc         table_info;
	acpi_status             status;
	ACPI_PHYSICAL_ADDRESS   physical_address;


	FUNCTION_TRACE ("Tb_get_table_rsdt");


	/*
	 * Get the RSDT from the RSDP
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"RSDP located at %p, RSDT physical=%8.8X%8.8X \n",
		acpi_gbl_RSDP, HIDWORD(acpi_gbl_RSDP->rsdt_physical_address),
		LODWORD(acpi_gbl_RSDP->rsdt_physical_address)));


	physical_address = acpi_tb_get_rsdt_address ();


	/* Get the RSDT/XSDT */

	status = acpi_tb_get_table (physical_address, NULL, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not get the RSDT, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}


	/* Check the RSDT or XSDT signature */

	status = acpi_tb_validate_rsdt (table_info.pointer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}


	/*
	 * Valid RSDT signature, verify the checksum.  If it fails, just
	 * print a warning and ignore it.
	 */
	status = acpi_tb_verify_table_checksum (table_info.pointer);


	/* Convert and/or copy to an XSDT structure */

	status = acpi_tb_convert_to_xsdt (&table_info, number_of_tables);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor (ACPI_TABLE_XSDT, &table_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	acpi_gbl_XSDT = (xsdt_descriptor *) table_info.pointer;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "XSDT located at %p\n", acpi_gbl_XSDT));

	return_ACPI_STATUS (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_facs
 *
 * PARAMETERS:  *Buffer_ptr             - If Buffer_ptr is valid, read data from
 *                                        buffer rather than searching memory
 *              *Table_info             - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns a pointer to the FACS as defined in FADT.  This
 *              function assumes the global variable FADT has been
 *              correctly initialized.  The value of FADT->Firmware_ctrl
 *              into a far pointer which is returned.
 *
 *****************************************************************************/

acpi_status
acpi_tb_get_table_facs (
	acpi_table_header       *buffer_ptr,
	acpi_table_desc         *table_info)
{
	acpi_table_header       *table_ptr = NULL;
	u32                     size;
	u8                      allocation;
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Tb_get_table_facs");


	/* Must have a valid FADT pointer */

	if (!acpi_gbl_FADT) {
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	size = sizeof (FACS_DESCRIPTOR);
	if (buffer_ptr) {
		/*
		 * Getting table from a file -- allocate a buffer and
		 * read the table.
		 */
		table_ptr = ACPI_MEM_ALLOCATE (size);
		if(!table_ptr) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		MEMCPY (table_ptr, buffer_ptr, size);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
	}

	else {
		/* Just map the physical memory to our address space */

		status = acpi_tb_map_acpi_table ((ACPI_PHYSICAL_ADDRESS) ACPI_GET_ADDRESS (acpi_gbl_FADT->Xfirmware_ctrl),
				   &size, &table_ptr);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
	}


	/* Return values */

	table_info->pointer     = table_ptr;
	table_info->length      = size;
	table_info->allocation  = allocation;
	table_info->base_pointer = table_ptr;

	return_ACPI_STATUS (status);
}

