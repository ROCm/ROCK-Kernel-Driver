
/******************************************************************************
 *
 * Module Name: hwgpe - Low level GPE enable/disable/clear functions
 *              $Revision: 42 $
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
#include "acevents.h"

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwgpe")


/******************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_gpe_bit_mask
 *
 * PARAMETERS:  Gpe_number      - The GPE
 *
 * RETURN:      Gpe register bitmask for this gpe level
 *
 * DESCRIPTION: Get the bitmask for this GPE
 *
 ******************************************************************************/

u8
acpi_hw_get_gpe_bit_mask (
	u32                     gpe_number)
{
	return (acpi_gbl_gpe_number_info [acpi_ev_get_gpe_number_index (gpe_number)].bit_mask);
}


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

acpi_status
acpi_hw_enable_gpe (
	u32                     gpe_number)
{
	u32                     in_byte;
	u32                     register_index;
	u8                      bit_mask;
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	/* Translate GPE number to index into global registers array. */

	register_index = acpi_ev_get_gpe_register_index (gpe_number);

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/*
	 * Read the current value of the register, set the appropriate bit
	 * to enable the GPE, and write out the new register.
	 */
	status = acpi_hw_low_level_read (8, &in_byte,
			  &acpi_gbl_gpe_register_info[register_index].enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	status = acpi_hw_low_level_write (8, (in_byte | bit_mask),
			  &acpi_gbl_gpe_register_info[register_index].enable_address, 0);

	return (status);
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
	u8                      bit_mask;


	ACPI_FUNCTION_ENTRY ();


	/* Translate GPE number to index into global registers array. */

	register_index = acpi_ev_get_gpe_register_index (gpe_number);

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/*
	 * Set the bit so we will not disable this when sleeping
	 */
	acpi_gbl_gpe_register_info[register_index].wake_enable |= bit_mask;
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

acpi_status
acpi_hw_disable_gpe (
	u32                     gpe_number)
{
	u32                     in_byte;
	u32                     register_index;
	u8                      bit_mask;
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	/* Translate GPE number to index into global registers array. */

	register_index = acpi_ev_get_gpe_register_index (gpe_number);

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/*
	 * Read the current value of the register, clear the appropriate bit,
	 * and write out the new register value to disable the GPE.
	 */
	status = acpi_hw_low_level_read (8, &in_byte,
			  &acpi_gbl_gpe_register_info[register_index].enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	status = acpi_hw_low_level_write (8, (in_byte & ~bit_mask),
			  &acpi_gbl_gpe_register_info[register_index].enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	acpi_hw_disable_gpe_for_wakeup(gpe_number);
	return (AE_OK);
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
	u8                      bit_mask;


	ACPI_FUNCTION_ENTRY ();


	/* Translate GPE number to index into global registers array. */

	register_index = acpi_ev_get_gpe_register_index (gpe_number);

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/*
	 * Clear the bit so we will disable this when sleeping
	 */
	acpi_gbl_gpe_register_info[register_index].wake_enable &= ~bit_mask;
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

acpi_status
acpi_hw_clear_gpe (
	u32                     gpe_number)
{
	u32                     register_index;
	u8                      bit_mask;
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	/* Translate GPE number to index into global registers array. */

	register_index = acpi_ev_get_gpe_register_index (gpe_number);

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/*
	 * Write a one to the appropriate bit in the status register to
	 * clear this GPE.
	 */
	status = acpi_hw_low_level_write (8, bit_mask,
			  &acpi_gbl_gpe_register_info[register_index].status_address, 0);

	return (status);
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

acpi_status
acpi_hw_get_gpe_status (
	u32                     gpe_number,
	acpi_event_status       *event_status)
{
	u32                     in_byte = 0;
	u32                     register_index = 0;
	u8                      bit_mask = 0;
	ACPI_GPE_REGISTER_INFO  *gpe_register_info;
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	if (!event_status) {
		return (AE_BAD_PARAMETER);
	}

	(*event_status) = 0;

	/* Translate GPE number to index into global registers array. */

	register_index = acpi_ev_get_gpe_register_index (gpe_number);
	gpe_register_info = &acpi_gbl_gpe_register_info[register_index];

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/* GPE Enabled? */

	status = acpi_hw_low_level_read (8, &in_byte, &gpe_register_info->enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (bit_mask & in_byte) {
		(*event_status) |= ACPI_EVENT_FLAG_ENABLED;
	}

	/* GPE Enabled for wake? */

	if (bit_mask & gpe_register_info->wake_enable) {
		(*event_status) |= ACPI_EVENT_FLAG_WAKE_ENABLED;
	}

	/* GPE active (set)? */

	status = acpi_hw_low_level_read (8, &in_byte, &gpe_register_info->status_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (bit_mask & in_byte) {
		(*event_status) |= ACPI_EVENT_FLAG_SET;
	}
	return (AE_OK);
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
 *              modifies Acpi_gbl_Gpe_register_info[i].Enable, so it should not be
 *              given the chance to run until after non-wake GPEs are
 *              re-enabled.
 *
 ******************************************************************************/

acpi_status
acpi_hw_disable_non_wakeup_gpes (
	void)
{
	u32                     i;
	ACPI_GPE_REGISTER_INFO  *gpe_register_info;
	u32                     in_value;
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		gpe_register_info = &acpi_gbl_gpe_register_info[i];

		/*
		 * Read the enabled status of all GPEs. We
		 * will be using it to restore all the GPEs later.
		 */
		status = acpi_hw_low_level_read (8, &in_value,
				 &gpe_register_info->enable_address, 0);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		gpe_register_info->enable = (u8) in_value;

		/*
		 * Disable all GPEs except wakeup GPEs.
		 */
		status = acpi_hw_low_level_write (8, gpe_register_info->wake_enable,
				&gpe_register_info->enable_address, 0);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}
	return (AE_OK);
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

acpi_status
acpi_hw_enable_non_wakeup_gpes (
	void)
{
	u32                     i;
	ACPI_GPE_REGISTER_INFO  *gpe_register_info;
	acpi_status             status;


	ACPI_FUNCTION_ENTRY ();


	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		gpe_register_info = &acpi_gbl_gpe_register_info[i];

		/*
		 * We previously stored the enabled status of all GPEs.
		 * Blast them back in.
		 */
		status = acpi_hw_low_level_write (8, gpe_register_info->enable,
				 &gpe_register_info->enable_address, 0);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}
	return (AE_OK);
}
