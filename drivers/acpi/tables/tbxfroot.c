/******************************************************************************
 *
 * Module Name: tbxfroot - Find the root ACPI table (RSDT)
 *              $Revision: 39 $
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
	 MODULE_NAME         ("tbxfroot")

#define RSDP_CHECKSUM_LENGTH 20


/*******************************************************************************
 *
 * FUNCTION:    Acpi_find_root_pointer
 *
 * PARAMETERS:  **Rsdp_physical_address     - Where to place the RSDP address
 *
 * RETURN:      Status, Physical address of the RSDP
 *
 * DESCRIPTION: Find the RSDP
 *
 ******************************************************************************/

ACPI_STATUS
acpi_find_root_pointer (
	ACPI_PHYSICAL_ADDRESS   *rsdp_physical_address)
{
	ACPI_TABLE_DESC         table_info;
	ACPI_STATUS             status;


	/* Get the RSDP */

	status = acpi_tb_find_rsdp (&table_info, ACPI_LOGICAL_ADDRESSING);
	if (ACPI_FAILURE (status)) {
		return (AE_NO_ACPI_TABLES);
	}

	*rsdp_physical_address = table_info.physical_address;

	return (AE_OK);
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


	/* Search from given start addr for the requested length  */

	for (offset = 0, mem_rover = start_address;
		 offset < length;
		 offset += RSDP_SCAN_STEP, mem_rover += RSDP_SCAN_STEP) {

		/* The signature and checksum must both be correct */

		if (STRNCMP ((NATIVE_CHAR *) mem_rover,
				RSDP_SIG, sizeof (RSDP_SIG)-1) == 0 &&
			acpi_tb_checksum (mem_rover, RSDP_CHECKSUM_LENGTH) == 0) {
			/* If so, we have found the RSDP */

			return (mem_rover);
		}
	}

	/* Searched entire block, no RSDP was found */

	return (NULL);
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

ACPI_STATUS
acpi_tb_find_rsdp (
	ACPI_TABLE_DESC         *table_info,
	u32                     flags)
{
	u8                      *table_ptr;
	u8                      *mem_rover;
	UINT64                  phys_addr;
	ACPI_STATUS             status = AE_OK;


	/*
	 * Scan supports either 1) Logical addressing or 2) Physical addressing
	 */
	if ((flags & ACPI_MEMORY_MODE) == ACPI_LOGICAL_ADDRESSING) {
		/*
		 * 1) Search EBDA (low memory) paragraphs
		 */
		status = acpi_os_map_memory (LO_RSDP_WINDOW_BASE, LO_RSDP_WINDOW_SIZE,
				  (void **) &table_ptr);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr, LO_RSDP_WINDOW_SIZE);
		acpi_os_unmap_memory (table_ptr, LO_RSDP_WINDOW_SIZE);

		if (mem_rover) {
			/* Found it, return the physical address */

			phys_addr = LO_RSDP_WINDOW_BASE;
			phys_addr += (mem_rover - table_ptr);

			table_info->physical_address = phys_addr;

			return (AE_OK);
		}

		/*
		 * 2) Search upper memory: 16-byte boundaries in E0000h-F0000h
		 */
		status = acpi_os_map_memory (HI_RSDP_WINDOW_BASE, HI_RSDP_WINDOW_SIZE,
				  (void **) &table_ptr);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		mem_rover = acpi_tb_scan_memory_for_rsdp (table_ptr, HI_RSDP_WINDOW_SIZE);
		acpi_os_unmap_memory (table_ptr, HI_RSDP_WINDOW_SIZE);

		if (mem_rover) {
			/* Found it, return the physical address */

			phys_addr = HI_RSDP_WINDOW_BASE;
			phys_addr += (mem_rover - table_ptr);

			table_info->physical_address = phys_addr;

			return (AE_OK);
		}
	}


	/*
	 * Physical addressing
	 */
	else {
		/*
		 * 1) Search EBDA (low memory) paragraphs
		 */
		mem_rover = acpi_tb_scan_memory_for_rsdp ((u8 *) LO_RSDP_WINDOW_BASE,
				  LO_RSDP_WINDOW_SIZE);
		if (mem_rover) {
			/* Found it, return the physical address */

			table_info->physical_address = (ACPI_TBLPTR) mem_rover;
			return (AE_OK);
		}

		/*
		 * 2) Search upper memory: 16-byte boundaries in E0000h-F0000h
		 */
		mem_rover = acpi_tb_scan_memory_for_rsdp ((u8 *) HI_RSDP_WINDOW_BASE,
				  HI_RSDP_WINDOW_SIZE);
		if (mem_rover) {
			/* Found it, return the physical address */

			table_info->physical_address = (ACPI_TBLPTR) mem_rover;
			return (AE_OK);
		}
	}


	/* RSDP signature was not found */

	return (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_firmware_table
 *
 * PARAMETERS:  Signature       - Any ACPI table signature
 *              Instance        - the non zero instance of the table, allows
 *                                support for multiple tables of the same type
 *              Flags           - 0: Physical/Virtual support
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

ACPI_STATUS
acpi_get_firmware_table (
	ACPI_STRING             signature,
	u32                     instance,
	u32                     flags,
	ACPI_TABLE_HEADER       **table_pointer)
{
	ACPI_PHYSICAL_ADDRESS   physical_address;
	ACPI_TABLE_DESC         table_info;
	ACPI_TABLE_HEADER       *rsdt_ptr;
	ACPI_TABLE_HEADER       *table_ptr;
	ACPI_STATUS             status;
	u32                     rsdt_size;
	u32                     table_size;
	u32                     table_count;
	u32                     i;
	u32                     j;


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
		return (AE_BAD_PARAMETER);
	}

	/* Get the RSDP by scanning low memory */

	status = acpi_tb_find_rsdp (&table_info, flags);
	if (ACPI_FAILURE (status)) {
		return (AE_NO_ACPI_TABLES);
	}

	acpi_gbl_RSDP = (RSDP_DESCRIPTOR *) table_info.pointer;


	/* Get the RSDT and validate it */

	physical_address = acpi_tb_get_rsdt_address ();
	status = acpi_tb_get_table_pointer (physical_address, flags, &rsdt_size, &rsdt_ptr);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	status = acpi_tb_validate_rsdt (rsdt_ptr);
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}


	/* Get the number of table pointers within the RSDT */

	table_count = acpi_tb_get_table_count (acpi_gbl_RSDP, rsdt_ptr);


	/*
	 * Search the RSDT/XSDT for the correct instance of the
	 * requested table
	 */
	for (i = 0, j = 0; i < table_count; i++) {
		/* Get the next table pointer */

		if (acpi_gbl_RSDP->revision < 2) {
			physical_address = ((RSDT_DESCRIPTOR *) rsdt_ptr)->table_offset_entry[i];
		}
		else {
			physical_address = (ACPI_PHYSICAL_ADDRESS)
				ACPI_GET_ADDRESS (((XSDT_DESCRIPTOR *) rsdt_ptr)->table_offset_entry[i]);
		}

		/* Get addressibility if necessary */

		status = acpi_tb_get_table_pointer (physical_address, flags, &table_size, &table_ptr);
		if (ACPI_FAILURE (status)) {
			goto cleanup;
		}

		/* Compare table signatures and table instance */

		if (!STRNCMP ((char *) table_ptr, signature, STRLEN (signature))) {
			/* An instance of the table was found */

			j++;
			if (j >= instance) {
				/* Found the correct instance */

				*table_pointer = table_ptr;
				goto cleanup;
			}
		}

		/* Delete table mapping if using virtual addressing */

		if (table_size) {
			acpi_os_unmap_memory (table_ptr, table_size);
		}
	}

	/* Did not find the table */

	status = AE_NOT_EXIST;


cleanup:
	if (rsdt_size) {
		acpi_os_unmap_memory (rsdt_ptr, rsdt_size);
	}
	return (status);
}


