/******************************************************************************
 *
 * Module Name: tbrsdt - ACPI RSDT table utilities
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
	 ACPI_MODULE_NAME    ("tbrsdt")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_verify_rsdp
 *
 * PARAMETERS:  Address         - RSDP (Pointer to RSDT)
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


	out_address->pointer_type = acpi_gbl_table_flags | ACPI_LOGICAL_ADDRESSING;

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
 * FUNCTION:    Acpi_tb_get_table_rsdt
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Load and validate the RSDP (ptr) and RSDT (table)
 *
 ******************************************************************************/

acpi_status
acpi_tb_get_table_rsdt (
	void)
{
	acpi_table_desc         table_info;
	acpi_status             status;
	ACPI_POINTER            address;


	ACPI_FUNCTION_TRACE ("Tb_get_table_rsdt");


	/* Get the RSDT/XSDT via the RSDP */

	acpi_tb_get_rsdt_address (&address);

	status = acpi_tb_get_table (&address, &table_info);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not get the RSDT/XSDT, %s\n",
			acpi_format_exception (status)));
		return_ACPI_STATUS (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"RSDP located at %p, points to RSDT physical=%8.8X%8.8X \n",
		acpi_gbl_RSDP,
		ACPI_HIDWORD (address.pointer.value),
		ACPI_LODWORD (address.pointer.value)));

	/* Check the RSDT or XSDT signature */

	status = acpi_tb_validate_rsdt (table_info.pointer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get the number of tables defined in the RSDT or XSDT */

	acpi_gbl_rsdt_table_count = acpi_tb_get_table_count (acpi_gbl_RSDP, table_info.pointer);

	/* Convert and/or copy to an XSDT structure */

	status = acpi_tb_convert_to_xsdt (&table_info);
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


