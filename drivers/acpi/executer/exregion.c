
/******************************************************************************
 *
 * Module Name: exregion - ACPI default Op_region (address space) handlers
 *              $Revision: 74 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "achware.h"
#include "acevents.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exregion")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_system_memory_space_handler
 *
 * PARAMETERS:  Function            - Read or Write operation
 *              Address             - Where in the space to read or write
 *              Bit_width           - Field width in bits (8, 16, or 32)
 *              Value               - Pointer to in or out value
 *              Handler_context     - Pointer to Handler's context
 *              Region_context      - Pointer to context specific to the
 *                                    accessed region
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handler for the System Memory address space (Op Region)
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_memory_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;
	void                    *logical_addr_ptr = NULL;
	acpi_mem_space_context  *mem_info = region_context;
	u32                     length;
	u32                     window_size;


	ACPI_FUNCTION_TRACE ("Ex_system_memory_space_handler");


	/* Validate and translate the bit width */

	switch (bit_width) {
	case 8:
		length = 1;
		break;

	case 16:
		length = 2;
		break;

	case 32:
		length = 4;
		break;

	case 64:
		length = 8;
		break;

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid System_memory width %d\n",
			bit_width));
		return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
	}

	/*
	 * Does the request fit into the cached memory mapping?
	 * Is 1) Address below the current mapping? OR
	 *    2) Address beyond the current mapping?
	 */
	if ((address < mem_info->mapped_physical_address) ||
		(((acpi_integer) address + length) >
			((acpi_integer) mem_info->mapped_physical_address + mem_info->mapped_length))) {
		/*
		 * The request cannot be resolved by the current memory mapping;
		 * Delete the existing mapping and create a new one.
		 */
		if (mem_info->mapped_length) {
			/* Valid mapping, delete it */

			acpi_os_unmap_memory (mem_info->mapped_logical_address,
					   mem_info->mapped_length);
		}

		/*
		 * Don't attempt to map memory beyond the end of the region, and
		 * constrain the maximum mapping size to something reasonable.
		 */
		window_size = (u32) ((mem_info->address + mem_info->length) - address);
		if (window_size > SYSMEM_REGION_WINDOW_SIZE) {
			window_size = SYSMEM_REGION_WINDOW_SIZE;
		}

		/* Create a new mapping starting at the address given */

		status = acpi_os_map_memory (address, window_size,
				  (void **) &mem_info->mapped_logical_address);
		if (ACPI_FAILURE (status)) {
			mem_info->mapped_length = 0;
			return_ACPI_STATUS (status);
		}

		/* Save the physical address and mapping size */

		mem_info->mapped_physical_address = address;
		mem_info->mapped_length = window_size;
	}

	/*
	 * Generate a logical pointer corresponding to the address we want to
	 * access
	 */
	logical_addr_ptr = mem_info->mapped_logical_address +
			  ((acpi_integer) address - (acpi_integer) mem_info->mapped_physical_address);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"System_memory %d (%d width) Address=%8.8X%8.8X\n", function, bit_width,
		ACPI_HIDWORD (address), ACPI_LODWORD (address)));

   /* Perform the memory read or write */

	switch (function) {
	case ACPI_READ:

		*value = 0;
		switch (bit_width) {
		case 8:
			*value = (u32)* (u8 *) logical_addr_ptr;
			break;

		case 16:
			ACPI_MOVE_UNALIGNED16_TO_16 (value, logical_addr_ptr);
			break;

		case 32:
			ACPI_MOVE_UNALIGNED32_TO_32 (value, logical_addr_ptr);
			break;

		case 64:
			ACPI_MOVE_UNALIGNED64_TO_64 (value, logical_addr_ptr);
			break;
		}
		break;

	case ACPI_WRITE:

		switch (bit_width) {
		case 8:
			*(u8 *) logical_addr_ptr = (u8) *value;
			break;

		case 16:
			ACPI_MOVE_UNALIGNED16_TO_16 (logical_addr_ptr, value);
			break;

		case 32:
			ACPI_MOVE_UNALIGNED32_TO_32 (logical_addr_ptr, value);
			break;

		case 64:
			ACPI_MOVE_UNALIGNED64_TO_64 (logical_addr_ptr, value);
			break;
		}
		break;

	default:
		status = AE_BAD_PARAMETER;
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_system_io_space_handler
 *
 * PARAMETERS:  Function            - Read or Write operation
 *              Address             - Where in the space to read or write
 *              Bit_width           - Field width in bits (8, 16, or 32)
 *              Value               - Pointer to in or out value
 *              Handler_context     - Pointer to Handler's context
 *              Region_context      - Pointer to context specific to the
 *                                    accessed region
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handler for the System IO address space (Op Region)
 *
 ******************************************************************************/

acpi_status
acpi_ex_system_io_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ex_system_io_space_handler");


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"System_iO %d (%d width) Address=%8.8X%8.8X\n", function, bit_width,
		ACPI_HIDWORD (address), ACPI_LODWORD (address)));

	/* Decode the function parameter */

	switch (function) {
	case ACPI_READ:

		*value = 0;
		status = acpi_os_read_port ((ACPI_IO_ADDRESS) address, value, bit_width);
		break;

	case ACPI_WRITE:

		status = acpi_os_write_port ((ACPI_IO_ADDRESS) address, *value, bit_width);
		break;

	default:
		status = AE_BAD_PARAMETER;
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_pci_config_space_handler
 *
 * PARAMETERS:  Function            - Read or Write operation
 *              Address             - Where in the space to read or write
 *              Bit_width           - Field width in bits (8, 16, or 32)
 *              Value               - Pointer to in or out value
 *              Handler_context     - Pointer to Handler's context
 *              Region_context      - Pointer to context specific to the
 *                                    accessed region
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handler for the PCI Config address space (Op Region)
 *
 ******************************************************************************/

acpi_status
acpi_ex_pci_config_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;
	acpi_pci_id             *pci_id;
	u16                     pci_register;


	ACPI_FUNCTION_TRACE ("Ex_pci_config_space_handler");


	/*
	 *  The arguments to Acpi_os(Read|Write)Pci_configuration are:
	 *
	 *  Pci_segment is the PCI bus segment range 0-31
	 *  Pci_bus     is the PCI bus number range 0-255
	 *  Pci_device  is the PCI device number range 0-31
	 *  Pci_function is the PCI device function number
	 *  Pci_register is the Config space register range 0-255 bytes
	 *
	 *  Value - input value for write, output address for read
	 *
	 */
	pci_id      = (acpi_pci_id *) region_context;
	pci_register = (u16) (ACPI_SIZE) address;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Pci_config %d (%d) Seg(%04x) Bus(%04x) Dev(%04x) Func(%04x) Reg(%04x)\n",
		function, bit_width, pci_id->segment, pci_id->bus, pci_id->device,
		pci_id->function, pci_register));

	switch (function) {
	case ACPI_READ:

		*value = 0;
		status = acpi_os_read_pci_configuration (pci_id, pci_register, value, bit_width);
		break;

	case ACPI_WRITE:

		status = acpi_os_write_pci_configuration (pci_id, pci_register, *value, bit_width);
		break;

	default:

		status = AE_BAD_PARAMETER;
		break;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_cmos_space_handler
 *
 * PARAMETERS:  Function            - Read or Write operation
 *              Address             - Where in the space to read or write
 *              Bit_width           - Field width in bits (8, 16, or 32)
 *              Value               - Pointer to in or out value
 *              Handler_context     - Pointer to Handler's context
 *              Region_context      - Pointer to context specific to the
 *                                    accessed region
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handler for the CMOS address space (Op Region)
 *
 ******************************************************************************/

acpi_status
acpi_ex_cmos_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ex_cmos_space_handler");


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_pci_bar_space_handler
 *
 * PARAMETERS:  Function            - Read or Write operation
 *              Address             - Where in the space to read or write
 *              Bit_width           - Field width in bits (8, 16, or 32)
 *              Value               - Pointer to in or out value
 *              Handler_context     - Pointer to Handler's context
 *              Region_context      - Pointer to context specific to the
 *                                    accessed region
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handler for the PCI Bar_target address space (Op Region)
 *
 ******************************************************************************/

acpi_status
acpi_ex_pci_bar_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ex_pci_bar_space_handler");


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_data_table_space_handler
 *
 * PARAMETERS:  Function            - Read or Write operation
 *              Address             - Where in the space to read or write
 *              Bit_width           - Field width in bits (8, 16, or 32)
 *              Value               - Pointer to in or out value
 *              Handler_context     - Pointer to Handler's context
 *              Region_context      - Pointer to context specific to the
 *                                    accessed region
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Handler for the Data Table address space (Op Region)
 *
 ******************************************************************************/

acpi_status
acpi_ex_data_table_space_handler (
	u32                     function,
	ACPI_PHYSICAL_ADDRESS   address,
	u32                     bit_width,
	acpi_integer            *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;
	u32                     byte_width = ACPI_DIV_8 (bit_width);
	u32                     i;
	char                    *logical_addr_ptr;


	ACPI_FUNCTION_TRACE ("Ex_data_table_space_handler");


	logical_addr_ptr = ACPI_PHYSADDR_TO_PTR (address);


   /* Perform the memory read or write */

	switch (function) {
	case ACPI_READ:

		for (i = 0; i < byte_width; i++) {
			((char *) value) [i] = logical_addr_ptr[i];
		}
		break;

	case ACPI_WRITE:

		return_ACPI_STATUS (AE_SUPPORT);
	}

	return_ACPI_STATUS (status);
}


