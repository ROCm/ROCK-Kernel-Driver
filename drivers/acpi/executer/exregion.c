
/******************************************************************************
 *
 * Module Name: exregion - ACPI default Op_region (address space) handlers
 *              $Revision: 61 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "achware.h"
#include "acevents.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exregion")


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
	u32                     *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;
	void                    *logical_addr_ptr = NULL;
	acpi_mem_space_context  *mem_info = region_context;
	u32                     length;


	FUNCTION_TRACE ("Ex_system_memory_space_handler");


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

	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid System_memory width %d\n",
			bit_width));
		return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
		break;
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

		mem_info->mapped_length = 0; /* In case of failure below */

		/* Create a new mapping starting at the address given */

		status = acpi_os_map_memory (address, SYSMEM_REGION_WINDOW_SIZE,
				  (void **) &mem_info->mapped_logical_address);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Save the physical address and mapping size */

		mem_info->mapped_physical_address = address;
		mem_info->mapped_length = SYSMEM_REGION_WINDOW_SIZE;
	}


	/*
	 * Generate a logical pointer corresponding to the address we want to
	 * access
	 */

	/* TBD: should these pointers go to 64-bit in all cases ? */

	logical_addr_ptr = mem_info->mapped_logical_address +
			  ((acpi_integer) address - (acpi_integer) mem_info->mapped_physical_address);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"System_memory %d (%d width) Address=%8.8X%8.8X\n", function, bit_width,
		HIDWORD (address), LODWORD (address)));

   /* Perform the memory read or write */

	switch (function) {

	case ACPI_READ_ADR_SPACE:

		switch (bit_width) {
		case 8:
			*value = (u32)* (u8 *) logical_addr_ptr;
			break;

		case 16:
			MOVE_UNALIGNED16_TO_32 (value, logical_addr_ptr);
			break;

		case 32:
			MOVE_UNALIGNED32_TO_32 (value, logical_addr_ptr);
			break;
		}

		break;


	case ACPI_WRITE_ADR_SPACE:

		switch (bit_width) {
		case 8:
			*(u8 *) logical_addr_ptr = (u8) *value;
			break;

		case 16:
			MOVE_UNALIGNED16_TO_16 (logical_addr_ptr, value);
			break;

		case 32:
			MOVE_UNALIGNED32_TO_32 (logical_addr_ptr, value);
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
	u32                     *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_system_io_space_handler");


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"System_iO %d (%d width) Address=%8.8X%8.8X\n", function, bit_width,
		HIDWORD (address), LODWORD (address)));

	/* Decode the function parameter */

	switch (function) {

	case ACPI_READ_ADR_SPACE:

		*value = 0;
		status = acpi_os_read_port ((ACPI_IO_ADDRESS) address, value, bit_width);
		break;


	case ACPI_WRITE_ADR_SPACE:

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
	u32                     *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;
	acpi_pci_id             *pci_id;
	u16                     pci_register;


	FUNCTION_TRACE ("Ex_pci_config_space_handler");


	/*
	 *  The arguments to Acpi_os(Read|Write)Pci_cfg(Byte|Word|Dword) are:
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
	pci_register = (u16) address;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Pci_config %d (%d) Seg(%04x) Bus(%04x) Dev(%04x) Func(%04x) Reg(%04x)\n",
		function, bit_width, pci_id->segment, pci_id->bus, pci_id->device,
		pci_id->function, pci_register));

	switch (function) {

	case ACPI_READ_ADR_SPACE:

		*value = 0;
		status = acpi_os_read_pci_configuration (pci_id, pci_register, value, bit_width);
		break;


	case ACPI_WRITE_ADR_SPACE:

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
	u32                     *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_cmos_space_handler");


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
	u32                     *value,
	void                    *handler_context,
	void                    *region_context)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_pci_bar_space_handler");


	return_ACPI_STATUS (status);
}

