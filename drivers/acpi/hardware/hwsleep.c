
/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Interface
 *              $Revision: 46 $
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

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwsleep")


/******************************************************************************
 *
 * FUNCTION:    Acpi_set_firmware_waking_vector
 *
 * PARAMETERS:  Physical_address    - Physical address of ACPI real mode
 *                                    entry point.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Access function for d_firmware_waking_vector field in FACS
 *
 ******************************************************************************/

acpi_status
acpi_set_firmware_waking_vector (
	ACPI_PHYSICAL_ADDRESS physical_address)
{

	ACPI_FUNCTION_TRACE ("Acpi_set_firmware_waking_vector");


	/* Set the vector */

	if (acpi_gbl_common_fACS.vector_width == 32) {
		*(ACPI_CAST_PTR (u32, acpi_gbl_common_fACS.firmware_waking_vector))
				= (u32) physical_address;
	}
	else {
		*acpi_gbl_common_fACS.firmware_waking_vector
				= physical_address;
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
 * DESCRIPTION: Access function for Firmware_waking_vector field in FACS
 *
 ******************************************************************************/

acpi_status
acpi_get_firmware_waking_vector (
	ACPI_PHYSICAL_ADDRESS *physical_address)
{

	ACPI_FUNCTION_TRACE ("Acpi_get_firmware_waking_vector");


	if (!physical_address) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Get the vector */

	if (acpi_gbl_common_fACS.vector_width == 32) {
		*physical_address = (ACPI_PHYSICAL_ADDRESS)
			*(ACPI_CAST_PTR (u32, acpi_gbl_common_fACS.firmware_waking_vector));
	}
	else {
		*physical_address =
			*acpi_gbl_common_fACS.firmware_waking_vector;
	}

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_enter_sleep_state_prep
 *
 * PARAMETERS:  Sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Prepare to enter a system sleep state (see ACPI 2.0 spec p 231)
 *              This function must execute with interrupts enabled.
 *              We break sleeping into 2 stages so that OSPM can handle
 *              various OS-specific tasks between the two steps.
 *
 ******************************************************************************/

acpi_status
acpi_enter_sleep_state_prep (
	u8                  sleep_state)
{
	acpi_status         status;
	acpi_object_list    arg_list;
	acpi_object         arg;


	ACPI_FUNCTION_TRACE ("Acpi_enter_sleep_state_prep");


	/*
	 * _PSW methods could be run here to enable wake-on keyboard, LAN, etc.
	 */
	status = acpi_get_sleep_type_data (sleep_state,
			  &acpi_gbl_sleep_type_a, &acpi_gbl_sleep_type_b);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Setup parameter object */

	arg_list.count = 1;
	arg_list.pointer = &arg;

	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	/* Run the _PTS and _GTS methods */

	status = acpi_evaluate_object (NULL, "\\_PTS", &arg_list, NULL);
	if (ACPI_FAILURE (status) && status != AE_NOT_FOUND) {
		return_ACPI_STATUS (status);
	}

	status = acpi_evaluate_object (NULL, "\\_GTS", &arg_list, NULL);
	if (ACPI_FAILURE (status) && status != AE_NOT_FOUND) {
		return_ACPI_STATUS (status);
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
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/

acpi_status
acpi_enter_sleep_state (
	u8                      sleep_state)
{
	u32                     PM1Acontrol;
	u32                     PM1Bcontrol;
	ACPI_BIT_REGISTER_INFO  *sleep_type_reg_info;
	ACPI_BIT_REGISTER_INFO  *sleep_enable_reg_info;
	u32                     in_value;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Acpi_enter_sleep_state");


	if ((acpi_gbl_sleep_type_a > ACPI_SLEEP_TYPE_MAX) ||
		(acpi_gbl_sleep_type_b > ACPI_SLEEP_TYPE_MAX)) {
		ACPI_REPORT_ERROR (("Sleep values out of range: A=%X B=%X\n",
			acpi_gbl_sleep_type_a, acpi_gbl_sleep_type_b));
		return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
	}


	sleep_type_reg_info = acpi_hw_get_bit_register_info (ACPI_BITREG_SLEEP_TYPE_A);
	sleep_enable_reg_info = acpi_hw_get_bit_register_info (ACPI_BITREG_SLEEP_ENABLE);

	/* Clear wake status */

	status = acpi_set_register (ACPI_BITREG_WAKE_STATUS, 1, ACPI_MTX_LOCK);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_clear_acpi_status();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Disable BM arbitration */

	status = acpi_set_register (ACPI_BITREG_ARB_DISABLE, 1, ACPI_MTX_LOCK);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_disable_non_wakeup_gpes();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get current value of PM1A control */

	status = acpi_hw_register_read (ACPI_MTX_LOCK, ACPI_REGISTER_PM1_CONTROL, &PM1Acontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}
	ACPI_DEBUG_PRINT ((ACPI_DB_OK, "Entering S%d\n", sleep_state));

	/* Clear SLP_EN and SLP_TYP fields */

	PM1Acontrol &= ~(sleep_type_reg_info->access_bit_mask | sleep_enable_reg_info->access_bit_mask);
	PM1Bcontrol = PM1Acontrol;

	/* Insert SLP_TYP bits */

	PM1Acontrol |= (acpi_gbl_sleep_type_a << sleep_type_reg_info->bit_position);
	PM1Bcontrol |= (acpi_gbl_sleep_type_b << sleep_type_reg_info->bit_position);

	/* Write #1: fill in SLP_TYP data */

	status = acpi_hw_register_write (ACPI_MTX_LOCK, ACPI_REGISTER_PM1A_CONTROL, PM1Acontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_register_write (ACPI_MTX_LOCK, ACPI_REGISTER_PM1B_CONTROL, PM1Bcontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Insert SLP_ENABLE bit */

	PM1Acontrol |= sleep_enable_reg_info->access_bit_mask;
	PM1Bcontrol |= sleep_enable_reg_info->access_bit_mask;

	/* Write #2: SLP_TYP + SLP_EN */

	ACPI_FLUSH_CPU_CACHE ();

	status = acpi_hw_register_write (ACPI_MTX_LOCK, ACPI_REGISTER_PM1A_CONTROL, PM1Acontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_register_write (ACPI_MTX_LOCK, ACPI_REGISTER_PM1B_CONTROL, PM1Bcontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Wait a second, then try again. This is to get S4/5 to work on all machines.
	 */
	if (sleep_state > ACPI_STATE_S3) {
		/*
		 * We wait so long to allow chipsets that poll this reg very slowly to
		 * still read the right value. Ideally, this entire block would go
		 * away entirely.
		 */
		acpi_os_stall (10000000);

		status = acpi_hw_register_write (ACPI_MTX_LOCK, ACPI_REGISTER_PM1_CONTROL,
				 sleep_enable_reg_info->access_bit_mask);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Wait until we enter sleep state */

	do {
		status = acpi_get_register (ACPI_BITREG_WAKE_STATUS, &in_value, ACPI_MTX_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Spin until we wake */

	} while (!in_value);

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
	acpi_status         status;


	ACPI_FUNCTION_TRACE ("Acpi_leave_sleep_state");


	/* Ensure Enter_sleep_state_prep -> Enter_sleep_state ordering */

	acpi_gbl_sleep_type_a = ACPI_SLEEP_TYPE_INVALID;

	/* Setup parameter object */

	arg_list.count = 1;
	arg_list.pointer = &arg;

	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = sleep_state;

	/* Ignore any errors from these methods */

	status = acpi_evaluate_object (NULL, "\\_BFS", &arg_list, NULL);
	if (ACPI_FAILURE (status) && status != AE_NOT_FOUND) {
		ACPI_REPORT_ERROR (("Method _BFS failed, %s\n", acpi_format_exception (status)));
	}

	status = acpi_evaluate_object (NULL, "\\_WAK", &arg_list, NULL);
	if (ACPI_FAILURE (status) && status != AE_NOT_FOUND) {
		ACPI_REPORT_ERROR (("Method _WAK failed, %s\n", acpi_format_exception (status)));
	}

	/* _WAK returns stuff - do we want to look at it? */

	status = acpi_hw_enable_non_wakeup_gpes();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Disable BM arbitration */
	status = acpi_set_register (ACPI_BITREG_ARB_DISABLE, 0, ACPI_MTX_LOCK);

	return_ACPI_STATUS (status);
}
