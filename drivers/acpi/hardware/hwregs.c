
/*******************************************************************************
 *
 * Module Name: hwregs - Read/write access functions for the various ACPI
 *                       control and status registers.
 *              $Revision: 121 $
 *
 ******************************************************************************/

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
#include "achware.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwregs")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_clear_acpi_status
 *
 * PARAMETERS:  none
 *
 * RETURN:      none
 *
 * DESCRIPTION: Clears all fixed and general purpose status bits
 *
 ******************************************************************************/

void
acpi_hw_clear_acpi_status (void)
{
	NATIVE_UINT             i;
	NATIVE_UINT             gpe_block;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Hw_clear_acpi_status");


	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %04X to %04X\n",
		ACPI_BITMASK_ALL_FIXED_STATUS,
		(u16) ACPI_GET_ADDRESS (acpi_gbl_FADT->Xpm1a_evt_blk.address)));


	status = acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE);
	if (ACPI_FAILURE (status)) {
		return_VOID;
	}

	acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_STATUS,
			ACPI_BITMASK_ALL_FIXED_STATUS);

	/* Clear the fixed events */

	if (ACPI_VALID_ADDRESS (acpi_gbl_FADT->Xpm1b_evt_blk.address)) {
		acpi_hw_low_level_write (16, ACPI_BITMASK_ALL_FIXED_STATUS,
				&acpi_gbl_FADT->Xpm1b_evt_blk, 0);
	}

	/* Clear the GPE Bits */

	for (gpe_block = 0; gpe_block < ACPI_MAX_GPE_BLOCKS; gpe_block++) {
		for (i = 0; i < acpi_gbl_gpe_block_info[gpe_block].register_count; i++) {
			acpi_hw_low_level_write (8, 0xFF,
				acpi_gbl_gpe_block_info[gpe_block].block_address, i);
		}
	}

	(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_sleep_type_data
 *
 * PARAMETERS:  Sleep_state         - Numeric sleep state
 *              *Sleep_type_a        - Where SLP_TYPa is returned
 *              *Sleep_type_b        - Where SLP_TYPb is returned
 *
 * RETURN:      Status - ACPI status
 *
 * DESCRIPTION: Obtain the SLP_TYPa and SLP_TYPb values for the requested sleep
 *              state.
 *
 ******************************************************************************/

acpi_status
acpi_hw_get_sleep_type_data (
	u8                      sleep_state,
	u8                      *sleep_type_a,
	u8                      *sleep_type_b)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *obj_desc;


	ACPI_FUNCTION_TRACE ("Hw_get_sleep_type_data");


	/*
	 *  Validate parameters
	 */
	if ((sleep_state > ACPI_S_STATES_MAX) ||
		!sleep_type_a || !sleep_type_b) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 *  Acpi_evaluate the namespace object containing the values for this state
	 */
	status = acpi_ns_evaluate_by_name ((NATIVE_CHAR *) acpi_gbl_db_sleep_states[sleep_state],
			  NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if (!obj_desc) {
		ACPI_REPORT_ERROR (("Missing Sleep State object\n"));
		return_ACPI_STATUS (AE_NOT_EXIST);
	}

	/*
	 *  We got something, now ensure it is correct.  The object must
	 *  be a package and must have at least 2 numeric values as the
	 *  two elements
	 */

	/* Even though Acpi_evaluate_object resolves package references,
	 * Ns_evaluate doesn't. So, we do it here.
	 */
	status = acpi_ut_resolve_package_references(obj_desc);

	if (obj_desc->package.count < 2) {
		/* Must have at least two elements */

		ACPI_REPORT_ERROR (("Sleep State package does not have at least two elements\n"));
		status = AE_AML_NO_OPERAND;
	}
	else if (((obj_desc->package.elements[0])->common.type != ACPI_TYPE_INTEGER) ||
			 ((obj_desc->package.elements[1])->common.type != ACPI_TYPE_INTEGER)) {
		/* Must have two  */

		ACPI_REPORT_ERROR (("Sleep State package elements are not both of type Number\n"));
		status = AE_AML_OPERAND_TYPE;
	}
	else {
		/*
		 *  Valid _Sx_ package size, type, and value
		 */
		*sleep_type_a = (u8) (obj_desc->package.elements[0])->integer.value;
		*sleep_type_b = (u8) (obj_desc->package.elements[1])->integer.value;
	}

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Bad Sleep object %p type %X\n",
			obj_desc, obj_desc->common.type));
	}

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_register_bit_mask
 *
 * PARAMETERS:  Register_id     - index of ACPI Register to access
 *
 * RETURN:      The bit mask to be used when accessing the register
 *
 * DESCRIPTION: Map Register_id into a register bit mask.
 *
 ******************************************************************************/

ACPI_BIT_REGISTER_INFO *
acpi_hw_get_bit_register_info (
	u32                     register_id)
{
	ACPI_FUNCTION_NAME ("Hw_get_bit_register_info");


	if (register_id > ACPI_BITREG_MAX) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid Bit_register ID: %X\n", register_id));
		return (NULL);
	}

	return (&acpi_gbl_bit_register_info[register_id]);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_bit_register_read
 *
 * PARAMETERS:  Register_id     - index of ACPI Register to access
 *              Use_lock        - Lock the hardware
 *
 * RETURN:      Value is read from specified Register.  Value returned is
 *              normalized to bit0 (is shifted all the way right)
 *
 * DESCRIPTION: ACPI Bit_register read function.
 *
 ******************************************************************************/

u32
acpi_hw_bit_register_read (
	u32                     register_id,
	u32                     flags)
{
	u32                     register_value = 0;
	ACPI_BIT_REGISTER_INFO  *bit_reg_info;


	ACPI_FUNCTION_TRACE ("Hw_bit_register_read");


	if (flags & ACPI_MTX_LOCK) {
		if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE))) {
			return_VALUE (0);
		}
	}

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info (register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, bit_reg_info->parent_register);

	if (flags & ACPI_MTX_LOCK) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	/* Normalize the value that was read */

	register_value = ((register_value & bit_reg_info->access_bit_mask) >> bit_reg_info->bit_position);

	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "ACPI Register_read: got %X\n", register_value));
	return_VALUE (register_value);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_hw_bit_register_write
 *
 * PARAMETERS:  Register_id     - ID of ACPI Bit_register to access
 *              Value           - (only used on write) value to write to the
 *                                Register, NOT pre-normalized to the bit pos.
 *              Flags           - Lock the hardware or not
 *
 * RETURN:      Value written to from specified Register.  This value
 *              is shifted all the way right.
 *
 * DESCRIPTION: ACPI Bit Register write function.
 *
 ******************************************************************************/

u32
acpi_hw_bit_register_write (
	u32                     register_id,
	u32                     value,
	u32                     flags)
{
	u32                     register_value = 0;
	ACPI_BIT_REGISTER_INFO  *bit_reg_info;


	ACPI_FUNCTION_TRACE_U32 ("Hw_bit_register_write", register_id);


	if (flags & ACPI_MTX_LOCK) {
		if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE))) {
			return_VALUE (0);
		}
	}

	/* Get the info structure corresponding to the requested ACPI Register */

	bit_reg_info = acpi_hw_get_bit_register_info (register_id);
	if (!bit_reg_info) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Always do a register read first so we can insert the new bits  */

	register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, bit_reg_info->parent_register);

	/*
	 * Decode the Register ID
	 * Register id = Register block id | bit id
	 *
	 * Check bit id to fine locate Register offset.
	 * Check Mask to determine Register offset, and then read-write.
	 */
	switch (bit_reg_info->parent_register) {
	case ACPI_REGISTER_PM1_STATUS:

		/*
		 * Status Registers are different from the rest.  Clear by
		 * writing 1, writing 0 has no effect.  So, the only relevent
		 * information is the single bit we're interested in, all others should
		 * be written as 0 so they will be left unchanged
		 */
		value = ACPI_REGISTER_PREPARE_BITS (value, bit_reg_info->bit_position, bit_reg_info->access_bit_mask);
		if (value) {
			acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_STATUS,
				(u16) value);
			register_value = 0;
		}
		break;


	case ACPI_REGISTER_PM1_ENABLE:

		ACPI_REGISTER_INSERT_VALUE (register_value, bit_reg_info->bit_position, bit_reg_info->access_bit_mask, value);

		acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_ENABLE, (u16) register_value);
		break;


	case ACPI_REGISTER_PM1_CONTROL:

		/*
		 * Read the PM1 Control register.
		 * Note that at this level, the fact that there are actually TWO
		 * registers (A and B - and that B may not exist) is abstracted.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM1 control: Read %X\n", register_value));

		ACPI_REGISTER_INSERT_VALUE (register_value, bit_reg_info->bit_position, bit_reg_info->access_bit_mask, value);

		acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, register_id,
				(u16) register_value);
		break;


	case ACPI_REGISTER_PM2_CONTROL:

		register_value = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM2_CONTROL);

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "PM2 control: Read %X from %8.8X%8.8X\n",
			register_value, ACPI_HIDWORD (acpi_gbl_FADT->Xpm2_cnt_blk.address),
			ACPI_LODWORD (acpi_gbl_FADT->Xpm2_cnt_blk.address)));

		ACPI_REGISTER_INSERT_VALUE (register_value, bit_reg_info->bit_position, bit_reg_info->access_bit_mask, value);

		ACPI_DEBUG_PRINT ((ACPI_DB_IO, "About to write %04X to %8.8X%8.8X\n",
			register_value,
			ACPI_HIDWORD (acpi_gbl_FADT->Xpm2_cnt_blk.address),
			ACPI_LODWORD (acpi_gbl_FADT->Xpm2_cnt_blk.address)));

		acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK,
				   ACPI_REGISTER_PM2_CONTROL, (u8) (register_value));
		break;


	default:
		break;
	}

	if (flags & ACPI_MTX_LOCK) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	/* Normalize the value that was read */

	register_value = ((register_value & bit_reg_info->access_bit_mask) >> bit_reg_info->bit_position);

	ACPI_DEBUG_PRINT ((ACPI_DB_IO, "ACPI Register_write actual %X\n", register_value));
	return_VALUE (register_value);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_register_read
 *
 * PARAMETERS:  Use_lock               - Mutex hw access.
 *              Register_id            - Register_iD + Offset.
 *
 * RETURN:      Value read or written.
 *
 * DESCRIPTION: Acpi register read function.  Registers are read at the
 *              given offset.
 *
 ******************************************************************************/

u32
acpi_hw_register_read (
	u8                      use_lock,
	u32                     register_id)
{
	u32                     value = 0;
	u32                     bank_offset;


	ACPI_FUNCTION_TRACE ("Hw_register_read");


	if (ACPI_MTX_LOCK == use_lock) {
		if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE))) {
			return_VALUE (0);
		}
	}

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:           /* 16-bit access */

		value =  acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1a_evt_blk, 0);
		value |= acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1b_evt_blk, 0);
		break;


	case ACPI_REGISTER_PM1_ENABLE:           /* 16-bit access*/

		bank_offset = ACPI_DIV_2 (acpi_gbl_FADT->pm1_evt_len);
		value =  acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1a_evt_blk, bank_offset);
		value |= acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1b_evt_blk, bank_offset);
		break;


	case ACPI_REGISTER_PM1_CONTROL:          /* 16-bit access */

		value =  acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1a_cnt_blk, 0);
		value |= acpi_hw_low_level_read (16, &acpi_gbl_FADT->Xpm1b_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM2_CONTROL:          /* 8-bit access */

		value =  acpi_hw_low_level_read (8, &acpi_gbl_FADT->Xpm2_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM_TIMER:             /* 32-bit access */

		value =  acpi_hw_low_level_read (32, &acpi_gbl_FADT->Xpm_tmr_blk, 0);
		break;

	case ACPI_REGISTER_SMI_COMMAND_BLOCK:    /* 8-bit access */

		acpi_os_read_port (acpi_gbl_FADT->smi_cmd, &value, 8);
		break;

	default:
		/* Value will be returned as 0 */
		break;
	}

	if (ACPI_MTX_LOCK == use_lock) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	return_VALUE (value);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_register_write
 *
 * PARAMETERS:  Use_lock               - Mutex hw access.
 *              Register_id            - Register_iD + Offset.
 *
 * RETURN:      Value read or written.
 *
 * DESCRIPTION: Acpi register Write function.  Registers are written at the
 *              given offset.
 *
 ******************************************************************************/

void
acpi_hw_register_write (
	u8                      use_lock,
	u32                     register_id,
	u32                     value)
{
	u32                     bank_offset;


	ACPI_FUNCTION_TRACE ("Hw_register_write");


	if (ACPI_MTX_LOCK == use_lock) {
		if (ACPI_FAILURE (acpi_ut_acquire_mutex (ACPI_MTX_HARDWARE))) {
			return_VOID;
		}
	}

	switch (register_id) {
	case ACPI_REGISTER_PM1_STATUS:           /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_evt_blk, 0);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_evt_blk, 0);
		break;


	case ACPI_REGISTER_PM1_ENABLE:           /* 16-bit access*/

		bank_offset = ACPI_DIV_2 (acpi_gbl_FADT->pm1_evt_len);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_evt_blk, bank_offset);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_evt_blk, bank_offset);
		break;


	case ACPI_REGISTER_PM1_CONTROL:          /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_cnt_blk, 0);
		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM1A_CONTROL:         /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1a_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM1B_CONTROL:         /* 16-bit access */

		acpi_hw_low_level_write (16, value, &acpi_gbl_FADT->Xpm1b_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM2_CONTROL:          /* 8-bit access */

		acpi_hw_low_level_write (8, value, &acpi_gbl_FADT->Xpm2_cnt_blk, 0);
		break;


	case ACPI_REGISTER_PM_TIMER:             /* 32-bit access */

		acpi_hw_low_level_write (32, value, &acpi_gbl_FADT->Xpm_tmr_blk, 0);
		break;


	case ACPI_REGISTER_SMI_COMMAND_BLOCK:    /* 8-bit access */

		/* SMI_CMD is currently always in IO space */

		acpi_os_write_port (acpi_gbl_FADT->smi_cmd, value, 8);
		break;


	default:
		value = 0;
		break;
	}

	if (ACPI_MTX_LOCK == use_lock) {
		(void) acpi_ut_release_mutex (ACPI_MTX_HARDWARE);
	}

	return_VOID;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_low_level_read
 *
 * PARAMETERS:  Register            - GAS register structure
 *              Offset              - Offset from the base address in the GAS
 *              Width               - 8, 16, or 32
 *
 * RETURN:      Value read
 *
 * DESCRIPTION: Read from either memory, IO, or PCI config space.
 *
 ******************************************************************************/

u32
acpi_hw_low_level_read (
	u32                     width,
	acpi_generic_address    *reg,
	u32                     offset)
{
	u32                     value = 0;
	ACPI_PHYSICAL_ADDRESS   mem_address;
	ACPI_IO_ADDRESS         io_address;
	acpi_pci_id             pci_id;
	u16                     pci_register;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within
	 */
	if ((!reg) ||
		(!ACPI_VALID_ADDRESS (reg->address))) {
		return 0;
	}

	/*
	 * Three address spaces supported:
	 * Memory, Io, or PCI config.
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		mem_address = (ACPI_PHYSICAL_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_read_memory (mem_address, &value, width);
		break;


	case ACPI_ADR_SPACE_SYSTEM_IO:

		io_address = (ACPI_IO_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_read_port (io_address, &value, width);
		break;


	case ACPI_ADR_SPACE_PCI_CONFIG:

		pci_id.segment = 0;
		pci_id.bus     = 0;
		pci_id.device  = ACPI_PCI_DEVICE (ACPI_GET_ADDRESS (reg->address));
		pci_id.function = ACPI_PCI_FUNCTION (ACPI_GET_ADDRESS (reg->address));
		pci_register   = (u16) (ACPI_PCI_REGISTER (ACPI_GET_ADDRESS (reg->address)) + offset);

		acpi_os_read_pci_configuration (&pci_id, pci_register, &value, width);
		break;
	}

	return value;
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_low_level_write
 *
 * PARAMETERS:  Width               - 8, 16, or 32
 *              Value               - To be written
 *              Register            - GAS register structure
 *              Offset              - Offset from the base address in the GAS
 *
 *
 * RETURN:      Value read
 *
 * DESCRIPTION: Read from either memory, IO, or PCI config space.
 *
 ******************************************************************************/

void
acpi_hw_low_level_write (
	u32                     width,
	u32                     value,
	acpi_generic_address    *reg,
	u32                     offset)
{
	ACPI_PHYSICAL_ADDRESS   mem_address;
	ACPI_IO_ADDRESS         io_address;
	acpi_pci_id             pci_id;
	u16                     pci_register;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * Must have a valid pointer to a GAS structure, and
	 * a non-zero address within
	 */
	if ((!reg) ||
		(!ACPI_VALID_ADDRESS (reg->address))) {
		return;
	}

	/*
	 * Three address spaces supported:
	 * Memory, Io, or PCI config.
	 */
	switch (reg->address_space_id) {
	case ACPI_ADR_SPACE_SYSTEM_MEMORY:

		mem_address = (ACPI_PHYSICAL_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_write_memory (mem_address, value, width);
		break;


	case ACPI_ADR_SPACE_SYSTEM_IO:

		io_address = (ACPI_IO_ADDRESS) (ACPI_GET_ADDRESS (reg->address) + offset);

		acpi_os_write_port (io_address, value, width);
		break;


	case ACPI_ADR_SPACE_PCI_CONFIG:

		pci_id.segment = 0;
		pci_id.bus     = 0;
		pci_id.device  = ACPI_PCI_DEVICE (ACPI_GET_ADDRESS (reg->address));
		pci_id.function = ACPI_PCI_FUNCTION (ACPI_GET_ADDRESS (reg->address));
		pci_register   = (u16) (ACPI_PCI_REGISTER (ACPI_GET_ADDRESS (reg->address)) + offset);

		acpi_os_write_pci_configuration (&pci_id, pci_register, value, width);
		break;
	}
}
