
/******************************************************************************
 *
 * Name: hwxface.c - Hardware access external interfaces
 *              $Revision: 36 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
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

#define _COMPONENT          HARDWARE
	 MODULE_NAME         ("hwxface")


/******************************************************************************
 *
 * Hardware globals
 *
 ******************************************************************************/


ACPI_C_STATE_HANDLER        acpi_hw_cx_handlers[MAX_CX_STATES] =
			  {NULL, acpi_hw_enter_c1, NULL, NULL};

u32                         acpi_hw_active_cx_state = 1;


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_processor_throttling_info
 *
 * PARAMETERS:  Processor_handle    - handle for the cpu to get info about
 *              User_buffer         - caller supplied buffer
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Get throttling capabilities for the processor, this routine
 *              builds the data directly into the callers buffer
 *
 ****************************************************************************/

ACPI_STATUS
acpi_get_processor_throttling_info (
	ACPI_HANDLE             processor_handle,
	ACPI_BUFFER             *user_buffer)
{
	NATIVE_UINT             percent_step;
	NATIVE_UINT             next_percent;
	NATIVE_UINT             num_throttle_states;
	NATIVE_UINT             buffer_space_needed;
	NATIVE_UINT             i;
	u8                      duty_width;
	ACPI_NAMESPACE_NODE     *cpu_node;
	ACPI_OPERAND_OBJECT     *cpu_obj;
	ACPI_CPU_THROTTLING_STATE *state_ptr;


	/*
	 *  Have to at least have a buffer to return info in
	 */
	if (!user_buffer) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 *  Convert and validate the device handle
	 */

	cpu_node = acpi_ns_convert_handle_to_entry (processor_handle);
	if (!cpu_node) {
		return (AE_BAD_PARAMETER);
	}

   /*
	*   Check for an existing internal object
	*/

	cpu_obj = acpi_ns_get_attached_object ((ACPI_HANDLE) cpu_node);
	if (!cpu_obj) {
		return (AE_NOT_FOUND);
	}

	/*
	 * (Duty Width on IA-64 is zero)
	 */
	duty_width = acpi_gbl_FADT->duty_width;

	/*
	 *  P0 must always have a P_BLK all others may be null
	 *  in either case, we can't throttle a processor that has no P_BLK
	 *
	 *  Also if no Duty width, one state and it is 100%
	 *
	 */
	if (!cpu_obj->processor.length || !duty_width ||
		(ACPI_UINT16_MAX < cpu_obj->processor.address))
	{
		/*
		 *  Acpi_even though we can't throttle, we still have one state (100%)
		 */
		num_throttle_states = 1;
	}

	else {
		num_throttle_states = (int) acpi_hw_local_pow (2,duty_width);
	}

	buffer_space_needed = num_throttle_states * sizeof (ACPI_CPU_THROTTLING_STATE);

	if ((user_buffer->length < buffer_space_needed) || !user_buffer->pointer) {
		user_buffer->length = buffer_space_needed;
		return (AE_BUFFER_OVERFLOW);
	}

	user_buffer->length = buffer_space_needed;
	state_ptr           = (ACPI_CPU_THROTTLING_STATE *) user_buffer->pointer;
	percent_step        = 1000 / num_throttle_states;

	/*
	 * Build each entry in the buffer.  Note that we're using the value
	 * 1000 and dividing each state by 10 to better avoid round-off
	 * accumulation.  Also note that the throttling STATES are ordered
	 * sequentially from 100% (state 0) on down (e.g. 87.5% = state 1),
	 * which is exactly opposite from duty cycle values (12.5% = state 1).
	 */
	for (i = 0, next_percent = 1000; i < num_throttle_states; i++) {
		state_ptr[i].state_number = i;
		state_ptr[i].percent_of_clock = next_percent / 10;
		next_percent -= percent_step;
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_processor_throttling_state
 *
 * PARAMETERS:  Processor_handle    - handle for the cpu to throttle
 *              Throttle_state      - throttling state to enter
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Get current hardware throttling state
 *
 ****************************************************************************/

ACPI_STATUS
acpi_get_processor_throttling_state (
	ACPI_HANDLE             processor_handle,
	u32                     *throttle_state)
{
	ACPI_NAMESPACE_NODE     *cpu_node;
	ACPI_OPERAND_OBJECT     *cpu_obj;
	u32                     num_throttle_states;
	u32                     duty_cycle;
	u8                      duty_offset;
	u8                      duty_width;


	/* Convert and validate the device handle */

	cpu_node = acpi_ns_convert_handle_to_entry (processor_handle);
	if (!cpu_node || !throttle_state) {
		return (AE_BAD_PARAMETER);
	}

   /* Check for an existing internal object */

	cpu_obj = acpi_ns_get_attached_object ((ACPI_HANDLE) cpu_node);
	if (!cpu_obj) {
		return (AE_NOT_FOUND);
	}

	/*
	 * No Duty fields in IA64 tables
	 */
	duty_offset = acpi_gbl_FADT->duty_offset;
	duty_width = acpi_gbl_FADT->duty_width;

	/*
	 *  Must have a valid P_BLK P0 must have a P_BLK all others may be null
	 *  in either case, we can't thottle a processor that has no P_BLK
	 *  that means we are in the only supported state (0 - 100%)
	 *
	 *  also, if Duty_width is zero there are no additional states
	 */
	if (!cpu_obj->processor.length || !duty_width ||
		(ACPI_UINT16_MAX < cpu_obj->processor.address))
	{
		*throttle_state = 0;
		return(AE_OK);
	}

	num_throttle_states = (u32) acpi_hw_local_pow (2,duty_width);

	/*
	 *  Get the current duty cycle value.
	 */
	duty_cycle = acpi_hw_get_duty_cycle (duty_offset,
			   cpu_obj->processor.address,
			   num_throttle_states);

	/*
	 * Convert duty cycle to throttling state (invert).
	 */
	if (duty_cycle == 0) {
		*throttle_state = 0;
	}

	else {
		*throttle_state = num_throttle_states - duty_cycle;
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_set_processor_throttling_state
 *
 * PARAMETERS:  Processor_handle    - handle for the cpu to throttle
 *              Throttle_state      - throttling state to enter
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Set hardware into requested throttling state, the handle
 *              passed in must have a valid P_BLK
 *
 ****************************************************************************/

ACPI_STATUS
acpi_set_processor_throttling_state (
	ACPI_HANDLE             processor_handle,
	u32                     throttle_state)
{
	ACPI_NAMESPACE_NODE    *cpu_node;
	ACPI_OPERAND_OBJECT    *cpu_obj;
	u32                     num_throttle_states = 0;
	u8                      duty_offset;
	u8                      duty_width;
	u32                     duty_cycle = 0;


	/* Convert and validate the device handle */

	cpu_node = acpi_ns_convert_handle_to_entry (processor_handle);
	if (!cpu_node) {
		return (AE_BAD_PARAMETER);
	}

   /* Check for an existing internal object */

	cpu_obj = acpi_ns_get_attached_object ((ACPI_HANDLE) cpu_node);
	if (!cpu_obj) {
		return (AE_NOT_FOUND);
	}

	/*
	 * No Duty fields in IA64 tables
	 */
	duty_offset = acpi_gbl_FADT->duty_offset;
	duty_width = acpi_gbl_FADT->duty_width;

	/*
	 *  Must have a valid P_BLK P0 must have a P_BLK all others may be null
	 *  in either case, we can't thottle a processor that has no P_BLK
	 *  that means we are in the only supported state (0 - 100%)
	 *
	 *  also, if Duty_width is zero there are no additional states
	 */
	if (!cpu_obj->processor.length || !duty_width ||
		(ACPI_UINT16_MAX < cpu_obj->processor.address))
	{
		/*
		 *  If caller wants to set the state to the only state we handle
		 *  we're done.
		 */
		if (throttle_state == 0) {
			return (AE_OK);
		}

		/*
		 *  Can't set this state
		 */
		return (AE_SUPPORT);
	}

	num_throttle_states = (u32) acpi_hw_local_pow (2,duty_width);

	/*
	 * Convert throttling state to duty cycle (invert).
	 */
	if (throttle_state > 0) {
		duty_cycle = num_throttle_states - throttle_state;
	}

	/*
	 *  Turn off throttling (don't muck with the h/w while throttling).
	 */
	acpi_hw_disable_throttling (cpu_obj->processor.address);

	/*
	 *  Program the throttling state.
	 */
	acpi_hw_program_duty_cycle (duty_offset, duty_cycle,
			 cpu_obj->processor.address, num_throttle_states);

	/*
	 *  Only enable throttling for non-zero states (0 - 100%)
	 */
	if (throttle_state) {
		acpi_hw_enable_throttling (cpu_obj->processor.address);
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_processor_cx_info
 *
 * PARAMETERS:  Processor_handle    - handle for the cpu return info about
 *              User_buffer         - caller supplied buffer
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Get Cx state latencies, this routine
 *              builds the data directly into the callers buffer
 *
 *
 ****************************************************************************/

ACPI_STATUS
acpi_get_processor_cx_info (
	ACPI_HANDLE             processor_handle,
	ACPI_BUFFER             *user_buffer)
{
	ACPI_STATUS             status = AE_OK;
	u32                     cx_state_latencies[4] = {0, 0, 0, 0};
	NATIVE_UINT             buffer_space_needed = 0;
	ACPI_CX_STATE           *state_ptr = NULL;
	NATIVE_UINT             i = 0;


	/*
	 *  Have to at least have a buffer to return info in
	 */
	if (!user_buffer) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_hw_get_cx_info (cx_state_latencies);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	buffer_space_needed = 4 * sizeof (ACPI_CX_STATE);

	if ((user_buffer->length < buffer_space_needed) || !user_buffer->pointer) {
		user_buffer->length = buffer_space_needed;
		return (AE_BUFFER_OVERFLOW);
	}

	user_buffer->length = buffer_space_needed;

	state_ptr = (ACPI_CX_STATE *) user_buffer->pointer;

	for (i = 0; i < 4; i++) {
		state_ptr[i].state_number = i;
		state_ptr[i].latency = cx_state_latencies[i];
	}

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_set_processor_sleep_state
 *
 * PARAMETERS:  Processor_handle    - handle for the cpu return info about
 *              Cx_state            - the Cx sleeping state (C1-C3) to make
 *                                      'active'
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Sets which Cx state will be used during calls to
 *              Acpi_processor_sleep ()
 *
 ****************************************************************************/

ACPI_STATUS
acpi_set_processor_sleep_state (
	ACPI_HANDLE             processor_handle,
	u32                     cx_state)
{
	ACPI_STATUS             status;


	status = acpi_hw_set_cx (cx_state);

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_processor_sleep
 *
 * PARAMETERS:  Processor_handle    - handle for the cpu to put to sleep (Cx)
 *              Time_sleeping       - time (in microseconds) elapsed while
 *                                      sleeping
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Puts the processor into the currently active sleep state (Cx)
 *
 ****************************************************************************/

ACPI_STATUS
acpi_processor_sleep (
	ACPI_HANDLE             processor_handle,
	u32                     *pm_timer_ticks)
{
	ACPI_NAMESPACE_NODE     *cpu_node = NULL;
	ACPI_OPERAND_OBJECT     *cpu_obj = NULL;
	ACPI_IO_ADDRESS         address = 0;


	/*
	 * Convert Processor_handle to Pblk_addres...
	 */

	/* Convert and validate the device handle */

	cpu_node = acpi_ns_convert_handle_to_entry (processor_handle);
	if (!cpu_node) {
		return (AE_BAD_PARAMETER);
	}

   /* Check for an existing internal object */

	cpu_obj = acpi_ns_get_attached_object ((ACPI_HANDLE) cpu_node);
	if (!cpu_obj) {
		return (AE_NOT_FOUND);
	}

	/* Get the processor register block (P_BLK) address */

	address = cpu_obj->processor.address;
	if (!cpu_obj->processor.length) {
		/* Ensure a NULL addresss (note that P_BLK isn't required for C1) */

		address = 0;
	}

	/*
	 * Enter the currently active Cx sleep state.
	 */
	return (acpi_hw_enter_cx (address, pm_timer_ticks));
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_get_timer
 *
 * PARAMETERS:  none
 *
 * RETURN:      Current value of the ACPI PMT (timer)
 *
 * DESCRIPTION: Obtains current value of ACPI PMT
 *
 ******************************************************************************/

ACPI_STATUS
acpi_get_timer (
	u32                     *out_ticks)
{

	if (!out_ticks) {
		return (AE_BAD_PARAMETER);
	}

	*out_ticks = acpi_hw_pmt_ticks ();

	return (AE_OK);
}


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

ACPI_STATUS
acpi_set_firmware_waking_vector (
	ACPI_PHYSICAL_ADDRESS physical_address)
{


	/* Make sure that we have an FACS */

	if (!acpi_gbl_FACS) {
		return (AE_NO_ACPI_TABLES);
	}

	/* Set the vector */

	if (acpi_gbl_FACS->vector_width == 32) {
		* (u32 *) acpi_gbl_FACS->firmware_waking_vector = (u32) physical_address;
	}
	else {
		*acpi_gbl_FACS->firmware_waking_vector = physical_address;
	}

	return (AE_OK);
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

ACPI_STATUS
acpi_get_firmware_waking_vector (
	ACPI_PHYSICAL_ADDRESS *physical_address)
{


	if (!physical_address) {
		return (AE_BAD_PARAMETER);
	}

	/* Make sure that we have an FACS */

	if (!acpi_gbl_FACS) {
		return (AE_NO_ACPI_TABLES);
	}

	/* Get the vector */

	if (acpi_gbl_FACS->vector_width == 32) {
		*physical_address = * (u32 *) acpi_gbl_FACS->firmware_waking_vector;
	}
	else {
		*physical_address = *acpi_gbl_FACS->firmware_waking_vector;
	}

	return (AE_OK);
}


