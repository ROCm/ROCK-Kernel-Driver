/******************************************************************************
 *
 * Module Name: tbget - ACPI Table get* routines
 *              $Revision: 77 $
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
 * FUNCTION:    Acpi_tb_table_override
 *
 * PARAMETERS:  *Table_info         - Info for current table
 *
 * RETURN:      None
 *
 * DESCRIPTION: Attempts override of current table with a new one if provided
 *              by the host OS.
 *
 ******************************************************************************/

void
acpi_tb_table_override (
	acpi_table_desc         *table_info)
{
	acpi_table_header       *new_table;
	acpi_status             status;
	ACPI_POINTER            address;
	acpi_table_desc         new_table_info;


	ACPI_FUNCTION_TRACE ("Acpi_tb_table_override");


	status = acpi_os_table_override (table_info->pointer, &new_table);
	if (ACPI_FAILURE (status)) {
		/* Some severe error from the OSL, but we basically ignore it */

		ACPI_REPORT_ERROR (("Could not override ACPI table, %s\n",
			acpi_format_exception (status)));
		return_VOID;
	}

	if (!new_table) {
		/* No table override */

		return_VOID;
	}

	/*
	 * We have a new table to override the old one.  Get a copy of
	 * the new one.  We know that the new table has a logical pointer.
	 */
	address.pointer_type    = ACPI_LOGICAL_POINTER;
	address.pointer.logical = new_table;

	status = acpi_tb_get_table (&address, &new_table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not copy ACPI table override\n"));
		return_VOID;
	}

	/*
	 * Delete the original table
	 */
	acpi_tb_delete_single_table (table_info);

	/* Copy the table info */

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Successful table override [%4.4s]\n",
		((acpi_table_header *) new_table_info.pointer)->signature));

	ACPI_MEMCPY (table_info, &new_table_info, sizeof (acpi_table_desc));
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table_with_override
 *
 * PARAMETERS:  Address             - Physical or logical address of table
 *              *Table_info         - Where the table info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Gets and installs the table with possible table override by OS.
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_with_override (
	ACPI_POINTER            *address,
	acpi_table_desc         *table_info)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_tb_get_table_with_override");


	status = acpi_tb_get_table (address, table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not get ACPI table, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/*
	 * Attempt override.  It either happens or it doesn't, no status
	 */
	acpi_tb_table_override (table_info);

	/* Install the table */

	status = acpi_tb_install_table (table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not install ACPI table, %s\n",
			acpi_format_exception (status)));
	}

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


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_get_table
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
acpi_tb_get_table (
	ACPI_POINTER            *address,
	acpi_table_desc         *table_info)
{
	acpi_table_header       *table_header = NULL;
	acpi_table_header       *full_table = NULL;
	ACPI_SIZE               size;
	u8                      allocation;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Tb_get_table");


	if (!table_info || !address) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	switch (address->pointer_type) {
	case ACPI_LOGICAL_POINTER:

		/*
		 * Getting data from a buffer, not BIOS tables
		 */
		table_header = address->pointer.logical;

		/* Allocate buffer for the entire table */

		full_table = ACPI_MEM_ALLOCATE (table_header->length);
		if (!full_table) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Copy the entire table (including header) to the local buffer */

		size = (ACPI_SIZE) table_header->length;
		ACPI_MEMCPY (full_table, table_header, size);

		/* Save allocation type */

		allocation = ACPI_MEM_ALLOCATED;
		break;


	case ACPI_PHYSICAL_POINTER:

		/*
		 * Not reading from a buffer, just map the table's physical memory
		 * into our address space.
		 */
		size = SIZE_IN_HEADER;

		status = acpi_tb_map_acpi_table (address->pointer.physical, &size, &full_table);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Save allocation type */

		allocation = ACPI_MEM_MAPPED;
		break;


	default:
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Return values */

	table_info->pointer     = full_table;
	table_info->length      = size;
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
 * FUNCTION:    Acpi_tb_get_all_tables
 *
 * PARAMETERS:  Number_of_tables    - Number of tables to get
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
acpi_tb_get_all_tables (
	u32                     number_of_tables)
{
	acpi_status             status = AE_OK;
	u32                     index;
	acpi_table_desc         table_info;
	ACPI_POINTER            address;


	ACPI_FUNCTION_TRACE ("Tb_get_all_tables");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Number of tables: %d\n", number_of_tables));


	/*
	 * Loop through all table pointers found in RSDT.
	 * This will NOT include the FACS and DSDT - we must get
	 * them after the loop.
	 *
	 * The ONLY table we are interested in getting here is the FADT.
	 */
	for (index = 0; index < number_of_tables; index++) {
		/* Clear the Table_info each time */

		ACPI_MEMSET (&table_info, 0, sizeof (acpi_table_desc));

		/* Get the table via the XSDT */

		address.pointer_type  = acpi_gbl_table_flags;
		address.pointer.value = ACPI_GET_ADDRESS (acpi_gbl_XSDT->table_offset_entry[index]);

		status = acpi_tb_get_table (&address, &table_info);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Recognize and install the table */

		status = acpi_tb_install_table (&table_info);
		if (ACPI_FAILURE (status)) {
			/*
			 * Unrecognized or unsupported table, delete it and ignore the
			 * error.  Just get as many tables as we can, later we will
			 * determine if there are enough tables to continue.
			 */
			(void) acpi_tb_uninstall_table (&table_info);
			status = AE_OK;
		}
	}

	if (!acpi_gbl_FADT) {
		ACPI_REPORT_ERROR (("No FADT present in R/XSDT\n"));
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
	 * Get the FACS (must have the FADT first, from loop above)
	 * Acpi_tb_get_table_facs will fail if FADT pointer is not valid
	 */
	address.pointer_type  = acpi_gbl_table_flags;
	address.pointer.value = ACPI_GET_ADDRESS (acpi_gbl_FADT->Xfirmware_ctrl);

	status = acpi_tb_get_table (&address, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not get the FACS, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	/* Install the FACS */

	status = acpi_tb_install_table (&table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not install the FACS, %s\n",
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
	 * Get/install the DSDT (We know that the FADT is valid now)
	 */
	address.pointer_type  = acpi_gbl_table_flags;
	address.pointer.value = ACPI_GET_ADDRESS (acpi_gbl_FADT->Xdsdt);

	status = acpi_tb_get_table_with_override (&address, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_REPORT_ERROR (("Could not get the DSDT\n"));
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
	ACPI_POINTER            *address)
{
	acpi_table_desc         table_info;
	acpi_status             status;
	RSDP_DESCRIPTOR         *rsdp;


	ACPI_FUNCTION_TRACE ("Tb_verify_rsdp");


	switch (address->pointer_type) {
	case ACPI_LOGICAL_POINTER:

		rsdp = address->pointer.logical;
		break;

	case ACPI_PHYSICAL_POINTER:
		/*
		 * Obtain access to the RSDP structure
		 */
		status = acpi_os_map_memory (address->pointer.physical, sizeof (RSDP_DESCRIPTOR),
				  (void **) &rsdp);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
		break;

	default:
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 *  The signature and checksum must both be correct
	 */
	if (ACPI_STRNCMP ((NATIVE_CHAR *) rsdp, RSDP_SIG, sizeof (RSDP_SIG)-1) != 0) {
		/* Nope, BAD Signature */

		status = AE_BAD_SIGNATURE;
		goto cleanup;
	}

	/* Check the standard checksum */

	if (acpi_tb_checksum (rsdp, ACPI_RSDP_CHECKSUM_LENGTH) != 0) {
		status = AE_BAD_CHECKSUM;
		goto cleanup;
	}

	/* Check extended checksum if table version >= 2 */

	if (rsdp->revision >= 2) {
		if (acpi_tb_checksum (rsdp, ACPI_RSDP_XCHECKSUM_LENGTH) != 0) {
			status = AE_BAD_CHECKSUM;
			goto cleanup;
		}
	}

	/* The RSDP supplied is OK */

	table_info.pointer     = ACPI_CAST_PTR (acpi_table_header, rsdp);
	table_info.length      = sizeof (RSDP_DESCRIPTOR);
	table_info.allocation  = ACPI_MEM_MAPPED;
	table_info.base_pointer = rsdp;

	/* Save the table pointers and allocation info */

	status = acpi_tb_init_table_descriptor (ACPI_TABLE_RSDP, &table_info);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Save the RSDP in a global for easy access */

	acpi_gbl_RSDP = ACPI_CAST_PTR (RSDP_DESCRIPTOR, table_info.pointer);
	return_ACPI_STATUS (status);


	/* Error exit */
cleanup:

	if (acpi_gbl_table_flags & ACPI_PHYSICAL_POINTER) {
		acpi_os_unmap_memory (rsdp, sizeof (RSDP_DESCRIPTOR));
	}
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

void
acpi_tb_get_rsdt_address (
	ACPI_POINTER            *out_address)
{

	ACPI_FUNCTION_ENTRY ();


	out_address->pointer_type = acpi_gbl_table_flags;

	/*
	 * For RSDP revision 0 or 1, we use the RSDT.
	 * For RSDP revision 2 (and above), we use the XSDT
	 */
	if (acpi_gbl_RSDP->revision < 2) {
		out_address->pointer.value = acpi_gbl_RSDP->rsdt_physical_address;
	}
	else {
		out_address->pointer.value = ACPI_GET_ADDRESS (acpi_gbl_RSDP->xsdt_physical_address);
	}
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
	int                     no_match;


	ACPI_FUNCTION_NAME ("Tb_validate_rsdt");


	/*
	 * For RSDP revision 0 or 1, we use the RSDT.
	 * For RSDP revision 2 and above, we use the XSDT
	 */
	if (acpi_gbl_RSDP->revision < 2) {
		no_match = ACPI_STRNCMP ((char *) table_ptr, RSDT_SIG,
				  sizeof (RSDT_SIG) -1);
	}
	else {
		no_match = ACPI_STRNCMP ((char *) table_ptr, XSDT_SIG,
				  sizeof (XSDT_SIG) -1);
	}

	if (no_match) {
		/* Invalid RSDT or XSDT signature */

		ACPI_REPORT_ERROR (("Invalid signature where RSDP indicates RSDT/XSDT should be located\n"));

		ACPI_DUMP_BUFFER (acpi_gbl_RSDP, 20);

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ERROR,
			"RSDT/XSDT signature at %X (%p) is invalid\n",
			acpi_gbl_RSDP->rsdt_physical_address,
			(void *) (NATIVE_UINT) acpi_gbl_RSDP->rsdt_physical_address));

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
	ACPI_POINTER            *address,
	u32                     flags,
	ACPI_SIZE               *size,
	acpi_table_header       **table_ptr)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * What mode is the processor in? (Virtual or Physical addressing)
	 */
	if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
		/* Incoming pointer can be either logical or physical */

		switch (address->pointer_type) {
		case ACPI_PHYSICAL_POINTER:

			*size = SIZE_IN_HEADER;
			status = acpi_tb_map_acpi_table (address->pointer.physical, size, table_ptr);
			break;

		case ACPI_LOGICAL_POINTER:

			*table_ptr = address->pointer.logical;
			*size = 0;
			break;

		default:
			return (AE_BAD_PARAMETER);
		}
	}
	else {
		/* In Physical addressing mode, all pointers must be physical */

		switch (address->pointer_type) {
		case ACPI_PHYSICAL_POINTER:
			*size = 0;
			*table_ptr = address->pointer.logical;
			break;

		case ACPI_LOGICAL_POINTER:

			status = AE_BAD_PARAMETER;
			break;

		default:
			return (AE_BAD_PARAMETER);
		}
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
	ACPI_POINTER            address;


	ACPI_FUNCTION_TRACE ("Tb_get_table_rsdt");


	/* Get the RSDT/XSDT from the RSDP */

	acpi_tb_get_rsdt_address (&address);
	status = acpi_tb_get_table (&address, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not get the R/XSDT, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"RSDP located at %p, RSDT physical=%8.8X%8.8X \n",
		acpi_gbl_RSDP,
		ACPI_HIDWORD (address.pointer.value),
		ACPI_LODWORD (address.pointer.value)));

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


