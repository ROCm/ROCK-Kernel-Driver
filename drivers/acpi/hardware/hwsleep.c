
/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Interface
 *              $Revision: 22 $
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
#include "acnamesp.h"
#include "achware.h"

#define _COMPONENT          ACPI_HARDWARE
	 MODULE_NAME         ("hwsleep")


/******************************************************************************
 *
 * FUNCTION:    Acpi_set_firmware_waking_vector
 *
 * PARAMETERS:  Physical_address    - Physical address of ACPI real mode
 *                                    entry point.
 *
 * RETURN:      AE_OK or AE_ERROR
 *
 * DESCRIPTION: Access function for d_firmware_waking_vector field in FACS
 *
 ******************************************************************************/

acpi_status
acpi_set_firmware_waking_vector (
	ACPI_PHYSICAL_ADDRESS physical_address)
{

	FUNCTION_TRACE ("Acpi_set_firmware_waking_vector");


	/* Make sure that we have an FACS */

	if (!acpi_gbl_FACS) {
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	/* Set the vector */

	if (acpi_gbl_FACS->vector_width == 32) {
		* (u32 *) acpi_gbl_FACS->firmware_waking_vector = (u32) physical_address;
	}
	else {
		*acpi_gbl_FACS->firmware_waking_vector = physical_address;
	}

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_firmware_waking_vector
 *
 * PARAMETERS:  *Physical_address   - Output buffer where contents of
 *                                    the Firmware_waking_vector field of
 *                                    the FACS will be stored.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Access function for d_firmware_waking_vector field in FACS
 *
 ******************************************************************************/

acpi_status
acpi_get_firmware_waking_vector (
	ACPI_PHYSICAL_ADDRESS *physical_address)
{

	FUNCTION_TRACE ("Acpi_get_firmware_waking_vector");


	if (!physical_address) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Make sure that we have an FACS */

	if (!acpi_gbl_FACS) {
		return_ACPI_STATUS (AE_NO_ACPI_TABLES);
	}

	/* Get the vector */

	if (acpi_gbl_FACS->vector_width == 32) {
		*physical_address = * (u32 *) acpi_gbl_FACS->firmware_waking_vector;
	}
	else {
		*physical_address = *acpi_gbl_FACS->firmware_waking_vector;
	}

	return_ACPI_STATUS (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_enter_sleep_state
 *
 * PARAMETERS:  Sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a system sleep state (see ACPI 2.0 spec p 231)
 *
 ******************************************************************************/

acpi_status
acpi_enter_sleep_state (
	u8                  sleep_state)
{
	acpi_status         status;
	acpi_object_list    arg_list;
	acpi_object         arg;
	u8                  type_a;
	u8                  type_b;
	u16                 PM1Acontrol;
	u16                 PM1Bcontrol;


	FUNCTION_TRACE ("Acpi_enter_sleep_state");


	/*
	 * _PSW methods could be run here to enable wake-on keyboard, LAN, etc.
	 */
	status = acpi_hw_obtain_sleep_type_register_data (sleep_state, &type_a, &type_b);
	if (!ACPI_SUCCESS (status)) {
		return status;
	}

	/* run the _PTS and _GTS methods */

	MEMSET(&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	MEMSET(&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	acpi_evaluate_object (NULL, "\\_PTS", &arg_list, NULL);
	acpi_evaluate_object (NULL, "\\_GTS", &arg_list, NULL);

	/* clear wake status */

	acpi_hw_register_bit_access (ACPI_WRITE, ACPI_MTX_LOCK, WAK_STS, 1);

	disable ();

	acpi_hw_disable_non_wakeup_gpes();

	PM1Acontrol = (u16) acpi_hw_register_read (ACPI_MTX_LOCK, PM1_CONTROL);

	ACPI_DEBUG_PRINT ((ACPI_DB_OK, "Entering S%d\n", sleep_state));

	/* mask off SLP_EN and SLP_TYP fields */

	PM1Acontrol &= ~(SLP_TYPE_X_MASK | SLP_EN_MASK);
	PM1Bcontrol = PM1Acontrol;

	/* mask in SLP_TYP */

	PM1Acontrol |= (type_a << acpi_hw_get_bit_shift (SLP_TYPE_X_MASK));
	PM1Bcontrol |= (type_b << acpi_hw_get_bit_shift (SLP_TYPE_X_MASK));

	/* write #1: fill in SLP_TYP data */

	acpi_hw_register_write (ACPI_MTX_LOCK, PM1A_CONTROL, PM1Acontrol);
	acpi_hw_register_write (ACPI_MTX_LOCK, PM1B_CONTROL, PM1Bcontrol);

	/* mask in SLP_EN */

	PM1Acontrol |= (1 << acpi_hw_get_bit_shift (SLP_EN_MASK));
	PM1Bcontrol |= (1 << acpi_hw_get_bit_shift (SLP_EN_MASK));

	/* flush caches */

	wbinvd();

	/* write #2: SLP_TYP + SLP_EN */

	acpi_hw_register_write (ACPI_MTX_LOCK, PM1A_CONTROL, PM1Acontrol);
	acpi_hw_register_write (ACPI_MTX_LOCK, PM1B_CONTROL, PM1Bcontrol);

	/*
	 * Wait a second, then try again. This is to get S4/5 to work on all machines.
	 */
	if (sleep_state > ACPI_STATE_S3) {
		acpi_os_stall(1000000);

		acpi_hw_register_write (ACPI_MTX_LOCK, PM1_CONTROL,
			(1 << acpi_hw_get_bit_shift (SLP_EN_MASK)));
	}

	/* wait until we enter sleep state */

	do {
		acpi_os_stall(10000);
	}
	while (!acpi_hw_register_bit_access (ACPI_READ, ACPI_MTX_LOCK, WAK_STS));

	acpi_hw_enable_non_wakeup_gpes();

	enable ();

	return_ACPI_STATUS (AE_OK);
}

/******************************************************************************
 *
 * FUNCTION:    Acpi_leave_sleep_state
 *
 * PARAMETERS:  Sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *
 ******************************************************************************/

acpi_status
acpi_leave_sleep_state (
	u8                  sleep_state)
{
	acpi_object_list    arg_list;
	acpi_object         arg;


	FUNCTION_TRACE ("Acpi_leave_sleep_state");


	MEMSET (&arg_list, 0, sizeof(arg_list));
	arg_list.count = 1;
	arg_list.pointer = &arg;

	MEMSET (&arg, 0, sizeof(arg));
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	acpi_evaluate_object (NULL, "\\_BFS", &arg_list, NULL);
	acpi_evaluate_object (NULL, "\\_WAK", &arg_list, NULL);

	/* _WAK returns stuff - do we want to look at it? */

	acpi_hw_enable_non_wakeup_gpes();

	return_ACPI_STATUS (AE_OK);
}
