
/******************************************************************************
 *
 * Module Name: hwgpe - Low level GPE enable/disable/clear functions
 *              $Revision: 35 $
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
#include "acnamesp.h"
#include "acevents.h"

#define _COMPONENT          ACPI_HARDWARE
	 MODULE_NAME         ("hwgpe")


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_enable_gpe
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Enable a single GPE.
 *
 ******************************************************************************/

void
acpi_hw_enable_gpe (
	u32                     gpe_number)
{
	u32                     in_byte;
	u32                     register_index;
	u32                     bit_mask;


	FUNCTION_ENTRY ();


	/*
	 * Translate GPE number to index into global registers array.
	 */
	register_index = acpi_gbl_gpe_valid[gpe_number];

	/*
	 * Figure out the bit offset for this GPE within the target register.
	 */
	bit_mask = acpi_gbl_decode_to8bit [MOD_8 (gpe_number)];

	/*
	 * Read the current value of the register, set the appropriate bit
	 * to enable the GPE, and write out the new register.
	 */
	in_byte = 0;
	acpi_os_read_port (acpi_gbl_gpe_registers[register_index].enable_addr, &in_byte, 8);
	acpi_os_write_port (acpi_gbl_gpe_registers[register_index].enable_addr,
			   (in_byte | bit_mask), 8);
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_enable_gpe_for_wakeup
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Keep track of which GPEs the OS has requested not be
 *              disabled when going to sleep.
 *
 ******************************************************************************/

void
acpi_hw_enable_gpe_for_wakeup (
	u32                     gpe_number)
{
	u32                     register_index;
	u32                     bit_mask;


	FUNCTION_ENTRY ();


	/*
	 * Translate GPE number to index into global registers array.
	 */
	register_index = acpi_gbl_gpe_valid[gpe_number];

	/*
	 * Figure out the bit offset for this GPE within the target register.
	 */
	bit_mask = acpi_gbl_decode_to8bit [MOD_8 (gpe_number)];

	/*
	 * Set the bit so we will not disable this when sleeping
	 */
	acpi_gbl_gpe_registers[register_index].wake_enable |= bit_mask;
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_disable_gpe
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Disable a single GPE.
 *
 ******************************************************************************/

void
acpi_hw_disable_gpe (
	u32                     gpe_number)
{
	u32                     in_byte;
	u32                     register_index;
	u32                     bit_mask;


	FUNCTION_ENTRY ();


	/*
	 * Translate GPE number to index into global registers array.
	 */
	register_index = acpi_gbl_gpe_valid[gpe_number];

	/*
	 * Figure out the bit offset for this GPE within the target register.
	 */
	bit_mask = acpi_gbl_decode_to8bit [MOD_8 (gpe_number)];

	/*
	 * Read the current value of the register, clear the appropriate bit,
	 * and write out the new register value to disable the GPE.
	 */
	in_byte = 0;
	acpi_os_read_port (acpi_gbl_gpe_registers[register_index].enable_addr, &in_byte, 8);
	acpi_os_write_port (acpi_gbl_gpe_registers[register_index].enable_addr,
			 (in_byte & ~bit_mask), 8);

	acpi_hw_disable_gpe_for_wakeup(gpe_number);
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_disable_gpe_for_wakeup
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Keep track of which GPEs the OS has requested not be
 *              disabled when going to sleep.
 *
 ******************************************************************************/

void
acpi_hw_disable_gpe_for_wakeup (
	u32                     gpe_number)
{
	u32                     register_index;
	u32                     bit_mask;


	FUNCTION_ENTRY ();


	/*
	 * Translate GPE number to index into global registers array.
	 */
	register_index = acpi_gbl_gpe_valid[gpe_number];

	/*
	 * Figure out the bit offset for this GPE within the target register.
	 */
	bit_mask = acpi_gbl_decode_to8bit [MOD_8 (gpe_number)];

	/*
	 * Clear the bit so we will disable this when sleeping
	 */
	acpi_gbl_gpe_registers[register_index].wake_enable &= ~bit_mask;
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_clear_gpe
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Clear a single GPE.
 *
 ******************************************************************************/

void
acpi_hw_clear_gpe (
	u32                     gpe_number)
{
	u32                     register_index;
	u32                     bit_mask;


	FUNCTION_ENTRY ();


	/*
	 * Translate GPE number to index into global registers array.
	 */
	register_index = acpi_gbl_gpe_valid[gpe_number];

	/*
	 * Figure out the bit offset for this GPE within the target register.
	 */
	bit_mask = acpi_gbl_decode_to8bit [MOD_8 (gpe_number)];

	/*
	 * Write a one to the appropriate bit in the status register to
	 * clear this GPE.
	 */
	acpi_os_write_port (acpi_gbl_gpe_registers[register_index].status_addr, bit_mask, 8);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_gpe_status
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Return the status of a single GPE.
 *
 ******************************************************************************/

void
acpi_hw_get_gpe_status (
	u32                     gpe_number,
	acpi_event_status       *event_status)
{
	u32                     in_byte = 0;
	u32                     register_index = 0;
	u32                     bit_mask = 0;


	FUNCTION_ENTRY ();


	if (!event_status) {
		return;
	}

	(*event_status) = 0;

	/*
	 * Translate GPE number to index into global registers array.
	 */
	register_index = acpi_gbl_gpe_valid[gpe_number];

	/*
	 * Figure out the bit offset for this GPE within the target register.
	 */
	bit_mask = acpi_gbl_decode_to8bit [MOD_8 (gpe_number)];

	/*
	 * Enabled?:
	 */
	in_byte = 0;
	acpi_os_read_port (acpi_gbl_gpe_registers[register_index].enable_addr, &in_byte, 8);
	if (bit_mask & in_byte) {
		(*event_status) |= ACPI_EVENT_FLAG_ENABLED;
	}

	/*
	 * Enabled for wake?:
	 */
	if (bit_mask & acpi_gbl_gpe_registers[register_index].wake_enable) {
		(*event_status) |= ACPI_EVENT_FLAG_WAKE_ENABLED;
	}

	/*
	 * Set?
	 */
	in_byte = 0;
	acpi_os_read_port (acpi_gbl_gpe_registers[register_index].status_addr, &in_byte, 8);
	if (bit_mask & in_byte) {
		(*event_status) |= ACPI_EVENT_FLAG_SET;
	}
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_disable_non_wakeup_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Disable all non-wakeup GPEs
 *              Call with interrupts disabled. The interrupt handler also
 *              modifies Acpi_gbl_Gpe_registers[i].Enable, so it should not be
 *              given the chance to run until after non-wake GPEs are
 *              re-enabled.
 *
 ******************************************************************************/

void
acpi_hw_disable_non_wakeup_gpes (
	void)
{
	u32                     i;

	FUNCTION_ENTRY ();

	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		/*
		 * Read the enabled status of all GPEs. We
		 * will be using it to restore all the GPEs later.
		 */
		acpi_os_read_port (acpi_gbl_gpe_registers[i].enable_addr,
				&acpi_gbl_gpe_registers[i].enable, 8);

		/*
		 * Disable all GPEs but wakeup GPEs.
		 */
		acpi_os_write_port(acpi_gbl_gpe_registers[i].enable_addr,
				acpi_gbl_gpe_registers[i].wake_enable, 8);
	}
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_enable_non_wakeup_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Enable all non-wakeup GPEs we previously enabled.
 *
 ******************************************************************************/

void
acpi_hw_enable_non_wakeup_gpes (
	void)
{
	u32                     i;

	FUNCTION_ENTRY ();

	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		/*
		 * We previously stored the enabled status of all GPEs.
		 * Blast them back in.
		 */
		acpi_os_write_port(acpi_gbl_gpe_registers[i].enable_addr,
				acpi_gbl_gpe_registers[i].enable, 8);
	}
}
