
/******************************************************************************
 *
 * Module Name: hwgpe - Low level GPE enable/disable/clear functions
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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

#include <acpi/acpi.h>
#include <acpi/acevents.h>

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwgpe")


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_gpe_bit_mask
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      Gpe register bitmask for this gpe level
 *
 * DESCRIPTION: Get the bitmask for this GPE
 *
 ******************************************************************************/

u8
acpi_hw_get_gpe_bit_mask (
	u32                             gpe_number)
{
	return (acpi_gbl_gpe_number_info [acpi_ev_get_gpe_number_index (gpe_number)].bit_mask);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_enable_gpe
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Enable a single GPE.
 *
 ******************************************************************************/

acpi_status
acpi_hw_enable_gpe (
	u32                             gpe_number)
{
	u32                             in_byte;
	acpi_status                     status;
	struct acpi_gpe_register_info   *gpe_register_info;


	ACPI_FUNCTION_ENTRY ();


	/* Get the info block for the entire GPE register */

	gpe_register_info = acpi_ev_get_gpe_register_info (gpe_number);
	if (!gpe_register_info) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Read the current value of the register, set the appropriate bit
	 * to enable the GPE, and write out the new register.
	 */
	status = acpi_hw_low_level_read (8, &in_byte,
			  &gpe_register_info->enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Write with the new GPE bit enabled */

	status = acpi_hw_low_level_write (8, (in_byte | acpi_hw_get_gpe_bit_mask (gpe_number)),
			  &gpe_register_info->enable_address, 0);

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_enable_gpe_for_wakeup
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Keep track of which GPEs the OS has requested not be
 *              disabled when going to sleep.
 *
 ******************************************************************************/

void
acpi_hw_enable_gpe_for_wakeup (
	u32                             gpe_number)
{
	struct acpi_gpe_register_info   *gpe_register_info;


	ACPI_FUNCTION_ENTRY ();


	/* Get the info block for the entire GPE register */

	gpe_register_info = acpi_ev_get_gpe_register_info (gpe_number);
	if (!gpe_register_info) {
		return;
	}

	/*
	 * Set the bit so we will not disable this when sleeping
	 */
	gpe_register_info->wake_enable |= acpi_hw_get_gpe_bit_mask (gpe_number);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_disable_gpe
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Disable a single GPE.
 *
 ******************************************************************************/

acpi_status
acpi_hw_disable_gpe (
	u32                             gpe_number)
{
	u32                             in_byte;
	acpi_status                     status;
	struct acpi_gpe_register_info   *gpe_register_info;


	ACPI_FUNCTION_ENTRY ();


	/* Get the info block for the entire GPE register */

	gpe_register_info = acpi_ev_get_gpe_register_info (gpe_number);
	if (!gpe_register_info) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Read the current value of the register, clear the appropriate bit,
	 * and write out the new register value to disable the GPE.
	 */
	status = acpi_hw_low_level_read (8, &in_byte,
			  &gpe_register_info->enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Write the byte with this GPE bit cleared */

	status = acpi_hw_low_level_write (8, (in_byte & ~(acpi_hw_get_gpe_bit_mask (gpe_number))),
			  &gpe_register_info->enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	acpi_hw_disable_gpe_for_wakeup(gpe_number);
	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_disable_gpe_for_wakeup
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Keep track of which GPEs the OS has requested not be
 *              disabled when going to sleep.
 *
 ******************************************************************************/

void
acpi_hw_disable_gpe_for_wakeup (
	u32                             gpe_number)
{
	struct acpi_gpe_register_info   *gpe_register_info;


	ACPI_FUNCTION_ENTRY ();


	/* Get the info block for the entire GPE register */

	gpe_register_info = acpi_ev_get_gpe_register_info (gpe_number);
	if (!gpe_register_info) {
		return;
	}

	/*
	 * Clear the bit so we will disable this when sleeping
	 */
	gpe_register_info->wake_enable &= ~(acpi_hw_get_gpe_bit_mask (gpe_number));
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_clear_gpe
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Clear a single GPE.
 *
 ******************************************************************************/

acpi_status
acpi_hw_clear_gpe (
	u32                             gpe_number)
{
	acpi_status                     status;
	struct acpi_gpe_register_info   *gpe_register_info;


	ACPI_FUNCTION_ENTRY ();


	/* Get the info block for the entire GPE register */

	gpe_register_info = acpi_ev_get_gpe_register_info (gpe_number);
	if (!gpe_register_info) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Write a one to the appropriate bit in the status register to
	 * clear this GPE.
	 */
	status = acpi_hw_low_level_write (8, acpi_hw_get_gpe_bit_mask (gpe_number),
			  &gpe_register_info->status_address, 0);

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_get_gpe_status
 *
 * PARAMETERS:  gpe_number      - The GPE
 *
 * RETURN:      None
 *
 * DESCRIPTION: Return the status of a single GPE.
 *
 ******************************************************************************/

acpi_status
acpi_hw_get_gpe_status (
	u32                             gpe_number,
	acpi_event_status               *event_status)
{
	u32                             in_byte;
	u8                              bit_mask;
	struct acpi_gpe_register_info   *gpe_register_info;
	acpi_status                     status;
	acpi_event_status               local_event_status = 0;


	ACPI_FUNCTION_ENTRY ();


	if (!event_status) {
		return (AE_BAD_PARAMETER);
	}

	/* Get the info block for the entire GPE register */

	gpe_register_info = acpi_ev_get_gpe_register_info (gpe_number);
	if (!gpe_register_info) {
		return (AE_BAD_PARAMETER);
	}

	/* Get the register bitmask for this GPE */

	bit_mask = acpi_hw_get_gpe_bit_mask (gpe_number);

	/* GPE Enabled? */

	status = acpi_hw_low_level_read (8, &in_byte, &gpe_register_info->enable_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (bit_mask & in_byte) {
		local_event_status |= ACPI_EVENT_FLAG_ENABLED;
	}

	/* GPE Enabled for wake? */

	if (bit_mask & gpe_register_info->wake_enable) {
		local_event_status |= ACPI_EVENT_FLAG_WAKE_ENABLED;
	}

	/* GPE active (set)? */

	status = acpi_hw_low_level_read (8, &in_byte, &gpe_register_info->status_address, 0);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (bit_mask & in_byte) {
		local_event_status |= ACPI_EVENT_FLAG_SET;
	}

	/* Set return value */

	(*event_status) = local_event_status;
	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_hw_disable_non_wakeup_gpes
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Disable all non-wakeup GPEs
 *              Call with interrupts disabled. The interrupt handler also
 *              modifies acpi_gbl_gpe_register_info[i].Enable, so it should not be
 *              given the chance to run until after non-wake GPEs are
 *              re-enabled.
 *
 ******************************************************************************/

acpi_status
acpi_hw_disable_non_wakeup_gpes (
	void)
{
	u32                             i;
	struct acpi_gpe_register_info   *gpe_register_info;
	u32                             in_value;
	acpi_status                     status;


	ACPI_FUNCTION_ENTRY ();


	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		/* Get the info block for the entire GPE register */

		gpe_register_info = &acpi_gbl_gpe_register_info[i];
		if (!gpe_register_info) {
			return (AE_BAD_PARAMETER);
		}

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
 * FUNCTION:    acpi_hw_enable_non_wakeup_gpes
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
	u32                             i;
	struct acpi_gpe_register_info   *gpe_register_info;
	acpi_status                     status;


	ACPI_FUNCTION_ENTRY ();


	for (i = 0; i < acpi_gbl_gpe_register_count; i++) {
		/* Get the info block for the entire GPE register */

		gpe_register_info = &acpi_gbl_gpe_register_info[i];
		if (!gpe_register_info) {
			return (AE_BAD_PARAMETER);
		}

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
