
/******************************************************************************
 *
 * Name: hwsleep.c - ACPI Hardware Sleep/Wake Interface
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2003, R. Byron Moore
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <acpi/acpi.h>

#define _COMPONENT          ACPI_HARDWARE
	 ACPI_MODULE_NAME    ("hwsleep")


/******************************************************************************
 *
 * FUNCTION:    acpi_set_firmware_waking_vector
 *
 * PARAMETERS:  physical_address    - Physical address of ACPI real mode
 *                                    entry point.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: access function for d_firmware_waking_vector field in FACS
 *
 ******************************************************************************/

acpi_status
acpi_set_firmware_waking_vector (
	acpi_physical_address physical_address)
{

	ACPI_FUNCTION_TRACE ("acpi_set_firmware_waking_vector");


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
 * FUNCTION:    acpi_get_firmware_waking_vector
 *
 * PARAMETERS:  *physical_address   - Output buffer where contents of
 *                                    the firmware_waking_vector field of
 *                                    the FACS will be stored.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Access function for firmware_waking_vector field in FACS
 *
 ******************************************************************************/

acpi_status
acpi_get_firmware_waking_vector (
	acpi_physical_address *physical_address)
{

	ACPI_FUNCTION_TRACE ("acpi_get_firmware_waking_vector");


	if (!physical_address) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Get the vector */

	if (acpi_gbl_common_fACS.vector_width == 32) {
		*physical_address = (acpi_physical_address)
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
 * FUNCTION:    acpi_enter_sleep_state_prep
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
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
	u8                          sleep_state)
{
	acpi_status                 status;
	struct acpi_object_list     arg_list;
	union acpi_object           arg;


	ACPI_FUNCTION_TRACE ("acpi_enter_sleep_state_prep");


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
 * FUNCTION:    acpi_enter_sleep_state
 *
 * PARAMETERS:  sleep_state         - Which sleep state to enter
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enter a system sleep state (see ACPI 2.0 spec p 231)
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/

acpi_status
acpi_enter_sleep_state (
	u8                              sleep_state)
{
	u32                             PM1Acontrol;
	u32                             PM1Bcontrol;
	struct acpi_bit_register_info   *sleep_type_reg_info;
	struct acpi_bit_register_info   *sleep_enable_reg_info;
	u32                             in_value;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("acpi_enter_sleep_state");


	if ((acpi_gbl_sleep_type_a > ACPI_SLEEP_TYPE_MAX) ||
		(acpi_gbl_sleep_type_b > ACPI_SLEEP_TYPE_MAX)) {
		ACPI_REPORT_ERROR (("Sleep values out of range: A=%X B=%X\n",
			acpi_gbl_sleep_type_a, acpi_gbl_sleep_type_b));
		return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
	}


	sleep_type_reg_info = acpi_hw_get_bit_register_info (ACPI_BITREG_SLEEP_TYPE_A);
	sleep_enable_reg_info = acpi_hw_get_bit_register_info (ACPI_BITREG_SLEEP_ENABLE);

	/* Clear wake status */

	status = acpi_set_register (ACPI_BITREG_WAKE_STATUS, 1, ACPI_MTX_DO_NOT_LOCK);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_clear_acpi_status(ACPI_MTX_DO_NOT_LOCK);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Disable BM arbitration */

	status = acpi_set_register (ACPI_BITREG_ARB_DISABLE, 1, ACPI_MTX_DO_NOT_LOCK);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_disable_non_wakeup_gpes();
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get current value of PM1A control */

	status = acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_CONTROL, &PM1Acontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}
	ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "Entering sleep state [S%d]\n", sleep_state));

	/* Clear SLP_EN and SLP_TYP fields */

	PM1Acontrol &= ~(sleep_type_reg_info->access_bit_mask | sleep_enable_reg_info->access_bit_mask);
	PM1Bcontrol = PM1Acontrol;

	/* Insert SLP_TYP bits */

	PM1Acontrol |= (acpi_gbl_sleep_type_a << sleep_type_reg_info->bit_position);
	PM1Bcontrol |= (acpi_gbl_sleep_type_b << sleep_type_reg_info->bit_position);

	/* Write #1: fill in SLP_TYP data */

	status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1A_CONTROL, PM1Acontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1B_CONTROL, PM1Bcontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Insert SLP_ENABLE bit */

	PM1Acontrol |= sleep_enable_reg_info->access_bit_mask;
	PM1Bcontrol |= sleep_enable_reg_info->access_bit_mask;

	/* Write #2: SLP_TYP + SLP_EN */

	ACPI_FLUSH_CPU_CACHE ();

	status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1A_CONTROL, PM1Acontrol);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1B_CONTROL, PM1Bcontrol);
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

		status = acpi_hw_register_write (ACPI_MTX_DO_NOT_LOCK, ACPI_REGISTER_PM1_CONTROL,
				 sleep_enable_reg_info->access_bit_mask);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/* Wait until we enter sleep state */

	do {
		status = acpi_get_register (ACPI_BITREG_WAKE_STATUS, &in_value, ACPI_MTX_DO_NOT_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/* Spin until we wake */

	} while (!in_value);

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_enter_sleep_state_s4bios
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform a S4 bios request.
 *              THIS FUNCTION MUST BE CALLED WITH INTERRUPTS DISABLED
 *
 ******************************************************************************/

acpi_status
acpi_enter_sleep_state_s4bios (
	void)
{
	u32                             in_value;
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("acpi_enter_sleep_state_s4bios");

	acpi_set_register (ACPI_BITREG_WAKE_STATUS, 1, ACPI_MTX_DO_NOT_LOCK);
	acpi_hw_clear_acpi_status(ACPI_MTX_DO_NOT_LOCK);

	acpi_hw_disable_non_wakeup_gpes();

	ACPI_FLUSH_CPU_CACHE();

	status = acpi_os_write_port (acpi_gbl_FADT->smi_cmd, (u32) acpi_gbl_FADT->S4bios_req, 8);

	do {
		acpi_os_stall(1000);
		status = acpi_get_register (ACPI_BITREG_WAKE_STATUS, &in_value, ACPI_MTX_DO_NOT_LOCK);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	} while (!in_value);

	return_ACPI_STATUS (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    acpi_leave_sleep_state
 *
 * PARAMETERS:  sleep_state         - Which sleep state we just exited
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Perform OS-independent ACPI cleanup after a sleep
 *
 ******************************************************************************/

acpi_status
acpi_leave_sleep_state (
	u8                          sleep_state)
{
	struct acpi_object_list     arg_list;
	union acpi_object           arg;
	acpi_status                 status;


	ACPI_FUNCTION_TRACE ("acpi_leave_sleep_state");


	/* Ensure enter_sleep_state_prep -> enter_sleep_state ordering */

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
