/******************************************************************************
 *
 * Module Name: tbxfroot - Find the root ACPI table (RSDT)
 *              $Revision: 64 $
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
	 ACPI_MODULE_NAME    ("tbxfroot")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_find_table
 *
 * PARAMETERS:  Signature           - String with ACPI table signature
 *              Oem_id              - String with the table OEM ID
 *              Oem_table_id        - String with the OEM Table ID.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find an ACPI table (in the RSDT/XSDT) that matches the
 *              Signature, OEM ID and OEM Table ID.
 *
 ******************************************************************************/

acpi_status
acpi_tb_find_table (
	NATIVE_CHAR             *signature,
	NATIVE_CHAR             *oem_id,
	NATIVE_CHAR             *oem_table_id,
	acpi_table_header       **table_ptr)
{
	acpi_status             status;
	acpi_table_header       *table;


	ACPI_FUNCTION_TRACE ("Tb_find_table");


	/* Validate string lengths */

	if ((ACPI_STRLEN (signature)  > ACPI_NAME_SIZE) ||
		(ACPI_STRLEN (oem_id)     > sizeof (table->oem_id)) ||
		(ACPI_STRLEN (oem_table_id) > sizeof (table->oem_table_id))) {
		return_ACPI_STATUS (AE_AML_STRING_LIMIT);
	}

	/* Find the table */

	status = acpi_get_firmware_table (signature, 1,
			   ACPI_LOGICAL_ADDRESSING, &table);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Check Oem_id and Oem_table_id */

	if ((oem_id[0]     && ACPI_STRCMP (oem_id, table->oem_id)) ||
		(oem_table_id[0] && ACPI_STRCMP (oem_table_id, table->oem_table_id))) {
		return_ACPI_STATUS (AE_AML_NAME_NOT_FOUND);
	}

	*table_ptr = table;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_firmware_table
 *
 * PARAMETERS:  Signature       - Any ACPI table signature
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *              Flags           - Physical/Virtual support
 *              Ret_buffer      - pointer to a structure containing a buffer to
 *                                receive the table
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to get an ACPI table.  The caller
 *              supplies an Out_buffer large enough to contain the entire ACPI
 *              table.  Upon completion
 *              the Out_buffer->Length field will indicate the number of bytes
 *              copied into the Out_buffer->Buf_ptr buffer. This table will be
 *              a complete table including the header.
 *
 ******************************************************************************/

acpi_status
acpi_get_firmware_table (
	acpi_string             signature,
	u32                     instance,
	u32                     flags,
	acpi_table_header       **table_pointer)
{
	ACPI_POINTER            rsdp_address;
	ACPI_POINTER            address;
	acpi_status             status;
	acpi_table_header       header;
	acpi_table_desc         table_info;
	acpi_table_desc         rsdt_info;
	u32                     table_count;
	u32                     i;
	u32                     j;


	ACPI_FUNCTION_TRACE ("Acpi_get_firmware_table");


	/*
	 * Ensure that at least the table manager is initialized.  We don't
	 * require that the entire ACPI subsystem is up for this interface
	 */

	/*
	 *  If we have a buffer, we must have a length too
	 */
	if ((instance == 0)                 ||
		(!signature)                    ||
		(!table_pointer)) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	rsdt_info.pointer = NULL;

	if (!acpi_gbl_RSDP) {
		/* Get the RSDP */

		status = acpi_os_get_root_pointer (flags, &rsdp_address);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "RSDP  not found\n"));
			return_ACPI_STATUS (AE_NO_ACPI_TABLES);
		}

		/* Map and validate the RSDP */

		if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
			status = acpi_os_map_memory (rsdp_address.pointer.physical, sizeof (RSDP_DESCRIPTOR),
					  (void **) &acpi_gbl_RSDP);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}
		else {
			acpi_gbl_RSDP = rsdp_address.pointer.logical;
		}

		/*
		 *  The signature and checksum must both be correct
		 */
		if (ACPI_STRNCMP ((NATIVE_CHAR *) acpi_gbl_RSDP, RSDP_SIG, sizeof (RSDP_SIG)-1) != 0) {
			/* Nope, BAD Signature */

			return_ACPI_STATUS (AE_BAD_SIGNATURE);
		}

		if (acpi_tb_checksum (acpi_gbl_RSDP, ACPI_RSDP_CHECKSUM_LENGTH) != 0) {
			/* Nope, BAD Checksum */

			return_ACPI_STATUS (AE_BAD_CHECKSUM);
		}
	}

	/* Get the RSDT and validate it */

	acpi_tb_get_rsdt_address (&address);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"RSDP located at %p, RSDT physical=%8.8X%8.8X \n",
		acpi_gbl_RSDP,
		ACPI_HIDWORD (address.pointer.value),
		ACPI_LODWORD (address.pointer.value)));

	/* Insert Processor_mode flags */

	address.pointer_type |= flags;

	status = acpi_tb_get_table (&address, &rsdt_info);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_tb_validate_rsdt (rsdt_info.pointer);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Get the number of table pointers within the RSDT */

	table_count = acpi_tb_get_table_count (acpi_gbl_RSDP, rsdt_info.pointer);

	address.pointer_type = acpi_gbl_table_flags | flags;

	/*
	 * Search the RSDT/XSDT for the correct instance of the
	 * requested table
	 */
	for (i = 0, j = 0; i < table_count; i++) {
		/* Get the next table pointer, handle RSDT vs. XSDT */

		if (acpi_gbl_RSDP->revision < 2) {
			address.pointer.value = ((RSDT_DESCRIPTOR *) rsdt_info.pointer)->table_offset_entry[i];
		}
		else {
			address.pointer.value = ACPI_GET_ADDRESS (
				((xsdt_descriptor *) rsdt_info.pointer)->table_offset_entry[i]);
		}

		/* Get the table header */

		status = acpi_tb_get_table_header (&address, &header);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* Compare table signatures and table instance */

		if (!ACPI_STRNCMP (header.signature, signature, ACPI_NAME_SIZE)) {
			/* An instance of the table was found */

			j++;
			if (j >= instance) {
				/* Found the correct instance, get the entire table */

				status = acpi_tb_get_table_body (&address, &header, &table_info);
				if (ACPI_FAILURE (status)) {
					goto cleanup;
				}

				*table_pointer = table_info.pointer;
				goto cleanup;
			}
		}
	}

	/* Did not find the table */

	status = AE_NOT_EXIST;


cleanup:
	acpi_os_unmap_memory (rsdt_info.pointer, (ACPI_SIZE) rsdt_info.pointer->length);
	return_ACPI_STATUS (status);
}


/* TBD: Move to a new file */

#if ACPI_MACHINE_WIDTH != 16

/*******************************************************************************
 *
 * FUNCTION:    Acpi_find_root_pointer
 *
 * PARAMETERS:  **Rsdp_address          - Where to place the RSDP address
 *              Flags                   - Logical/Physical addressing
 *
 * RETURN:      Status, Physical address of the RSDP
 *
 * DESCRIPTION: Find the RSDP
 *
 ******************************************************************************/

acpi_status
acpi_find_root_pointer (
	u32                     flags,
	ACPI_POINTER            *rsdp_address)
{
	acpi_table_desc         table_info;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_find_root_pointer");


	/* Get the RSDP */

	status = acpi_tb_find_rsdp (&table_info, flags);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "RSDP structure not found\n"));
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	rsdp_address->pointer_type = ACPI_PHYSICAL_POINTER;
	rsdp_address->pointer.physical = table_info.physical_address;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_scan_memory_for_rsdp
 *
 * PARAMETERS:  Start_address       - Starting pointer for search
 *              Length              - Maximum length to search
 *
 * RETURN:      Pointer to the RSDP if found, otherwise NULL.
 *
 * DESCRIPTION: Search a block of memory for the RSDP signature
 *
 ******************************************************************************/

u8 *
acpi_tb_scan_memory_for_rsdp (
	u8                      *start_address,
	u32                     length)
{
	u32                     offset;
	u8                      *mem_rover;


	ACPI_FUNCTION_TRACE ("Tb_scan_memory_for_rsdp");


	/* Search from given start addr for the requested length  */

	for (offset = 0, mem_rover = start_address;
		 offset < length;
		 offset += RSDP_SCAN_STEP, mem_rover += RSDP_SCAN_STEP) {

		/* The signature and checksum must both be correct */

		if (ACPI_STRNCMP ((NATIVE_CHAR *) mem_rover,
				RSDP_SIG, sizeof (RSDP_SIG)-1) == 0 &&
			acpi_tb_checksum (mem_rover, ACPI_RSDP_CHECKSUM_LENGTH) == 0) {
			/* If so, we have found the RSDP */

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
				"RSDP located at physical address %p\n",mem_rover));
			return_PTR (mem_rover);
		}
	}

	/* Searched entire block, no RSDP was found */

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,"Searched entire block, no RSDP was found.\n"));
	return_PTR (NULL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_tb_find_rsdp
 *
 * PARAMETERS:  *Table_info             - Where the table info is returned
 *              Flags                   - Current memory mode (logical vs.
 *                                        physical addressing)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search lower 1_mbyte of memory for the root system descriptor
 *              pointer structure.  If it is found, set *RSDP to point to it.
 *
 *              NOTE: The RSDP must be either in the first 1_k of the Extended
 *              BIOS Data Area or between E0000 and FFFFF (ACPI 1.0 section
 *              5.2.2; assertion #421).
 *
 ******************************************************************************/

acpi_status
acpi_tb_find_rsdp (
	acpi_table_desc         *table_info,
	u32                     flags)
{
	u8                      *table_ptr;
	u8                      *mem_rover;
	u64                     phys_addr;
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Tb_find_rsdp");


	/*
	 * Scan supports either 1) Logical addressing or 2) Physical addressing
	 */
	if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
		/*
		 * 1) Search EBDA (low memory) paragraphs
		 */
		status = acpi_os_map_memory ((u64) LO_RSDP_WINDOW_BASE, LO_RSDP_WINDOW_SIZE,
				  (void **) &table_ptr);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr, LO_RSDP_WINDOW_SIZE);
		acpi_os_unmap_memory (table_ptr, LO_RSDP_WINDOW_SIZE);

		if (mem_rover) {
			/* Found it, return the physical address */

			phys_addr = LO_RSDP_WINDOW_BASE;
			phys_addr += ACPI_PTR_DIFF (mem_rover,table_ptr);

			table_info->physical_address = phys_addr;
			return_ACPI_STATUS (AE_OK);
		}

		/*
		 * 2) Search upper memory: 16-byte boundaries in E0000h-F0000h
		 */
		status = acpi_os_map_memory ((u64) HI_RSDP_WINDOW_BASE, HI_RSDP_WINDOW_SIZE,
				  (void **) &table_ptr);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr, HI_RSDP_WINDOW_SIZE);
		acpi_os_unmap_memory (table_ptr, HI_RSDP_WINDOW_SIZE);

		if (mem_rover) {
			/* Found it, return the physical address */

			phys_addr = HI_RSDP_WINDOW_BASE;
			phys_addr += ACPI_PTR_DIFF (mem_rover, table_ptr);

			table_info->physical_address = phys_addr;
			return_ACPI_STATUS (AE_OK);
		}
	}

	/*
	 * Physical addressing
	 */
	else {
		/*
		 * 1) Search EBDA (low memory) paragraphs
		 */
		mem_rover = acpi_tb_scan_memory_for_rsdp (ACPI_PHYSADDR_TO_PTR (LO_RSDP_WINDOW_BASE),
				  LO_RSDP_WINDOW_SIZE);
		if (mem_rover) {
			/* Found it, return the physical address */

			table_info->physical_address = ACPI_TO_INTEGER (mem_rover);
			return_ACPI_STATUS (AE_OK);
		}

		/*
		 * 2) Search upper memory: 16-byte boundaries in E0000h-F0000h
		 */
		mem_rover = acpi_tb_scan_memory_for_rsdp (ACPI_PHYSADDR_TO_PTR (HI_RSDP_WINDOW_BASE),
				  HI_RSDP_WINDOW_SIZE);
		if (mem_rover) {
			/* Found it, return the physical address */

			table_info->physical_address = ACPI_TO_INTEGER (mem_rover);
			return_ACPI_STATUS (AE_OK);
		}
	}

	/* RSDP signature was not found */

	return_ACPI_STATUS (AE_NOT_FOUND);
}

#endif

