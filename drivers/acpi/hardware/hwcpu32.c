/******************************************************************************
 *
 * Name: hwcpu32.c - CPU support for IA32 (Throttling, Cx_states)
 *              $Revision: 39 $
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
	 MODULE_NAME         ("Hwcpu32")


#define BIT_4               0x10  /* TBD: [investigate] is this correct?  */


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_enter_c1
 *
 * PARAMETERS:  Pblk_address    - Address of the processor control block
 *              Pm_timer_ticks  - Number of PM timer ticks elapsed while asleep
 *
 * RETURN:      Function status.
 *
 * DESCRIPTION: Set C1 state on IA32 processor (halt)
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_enter_c1(
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks)
{
	u32                     timer = 0;


	if (!pm_timer_ticks) {
		/*
		 * Enter C1:
		 * ---------
		 */
		enable();
		halt();
		*pm_timer_ticks = ACPI_UINT32_MAX;
	}
	else {
		timer = acpi_hw_pmt_ticks ();

		/*
		 * Enter C1:
		 * ---------
		 */
		enable ();
		halt ();

		/*
		 * Compute Time in C1:
		 * -------------------
		 */
		timer = acpi_hw_pmt_ticks () - timer;

		*pm_timer_ticks = timer;
	}

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_enter_c2
 *
 * PARAMETERS:  Pblk_address    - Address of the processor control block
 *              Pm_timer_ticks  - Number of PM timer ticks elapsed while asleep
 *
 * RETURN:      <none>
 *
 * DESCRIPTION: Set C2 state on IA32 processor
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_enter_c2(
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks)
{
	u32                     timer = 0;


	if (!pblk_address || !pm_timer_ticks) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Disable interrupts before all C2/C3 transitions.
	 */
	disable ();

	timer = acpi_hw_pmt_ticks ();

	/*
	 * Enter C2:
	 * ---------
	 * Read from the P_LVL2 (P_BLK+4) register to invoke a C2 transition.
	 */
	acpi_os_in8 ((ACPI_IO_ADDRESS) (pblk_address + 4));

	/*
	 * Perform Dummy Op:
	 * -----------------
	 * We have to do something useless after reading LVL2 because chipsets
	 * cannot guarantee that STPCLK# gets asserted in time to freeze execution.
	 */
	acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, PM2_CONTROL);

	/*
	 * Compute Time in C2:
	 * -------------------
	 */
	timer = acpi_hw_pmt_ticks () - timer;

	*pm_timer_ticks = timer;

	/*
	 * Re-enable interrupts after coming out of C2/C3.
	 */
	enable ();

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_enter_c3
 *
 * PARAMETERS:  Pblk_address    - Address of the processor control block
 *              Pm_timer_ticks  - Number of PM timer ticks elapsed while asleep
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Set C3 state on IA32 processor (UP only, cache coherency via
 *              disabling bus mastering)
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_enter_c3(
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks)
{
	u32                     timer = 0;
	u32                     bus_master_status = 0;


	if (!pblk_address || !pm_timer_ticks) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Check the BM_STS bit, if it is set, do not enter C3
	 *  but clear the bit (with a write) and exit, telling
	 *  the calling module that we spent zero time in C3.
	 *  If bus mastering continues, this action should
	 *  eventually cause a demotion to C2
	 */
	if (1 == (bus_master_status =
		acpi_hw_register_bit_access (ACPI_READ, ACPI_MTX_LOCK, BM_STS)))
	{
		/*
		 * Clear the BM_STS bit by setting it.
		 */
		acpi_hw_register_bit_access (ACPI_WRITE, ACPI_MTX_LOCK, BM_STS, 1);
		*pm_timer_ticks = 0;
		return (AE_OK);
	}

	/*
	 * Disable interrupts before all C2/C3 transitions.
	 */
	disable();

	/*
	 * Disable Bus Mastering:
	 * ----------------------
	 * Set the PM2_CNT.ARB_DIS bit (bit #0), preserving all other bits.
	 */
	 acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, ARB_DIS, 1);

	/*
	 * Get the timer base before entering C state
	 */
	timer = acpi_hw_pmt_ticks ();

	/*
	 * Enter C3:
	 * ---------
	 * Read from the P_LVL3 (P_BLK+5) register to invoke a C3 transition.
	 */
	acpi_os_in8 ((ACPI_IO_ADDRESS)(pblk_address + 5));

	/*
	 * Perform Dummy Op:
	 * -----------------
	 * We have to do something useless after reading LVL3 because chipsets
	 * cannot guarantee that STPCLK# gets asserted in time to freeze execution.
	 */
	acpi_hw_register_read (ACPI_MTX_DO_NOT_LOCK, PM2_CONTROL);
	/*
	 * Immediately compute the time in the C state
	 */
	timer = acpi_hw_pmt_ticks() - timer;

	/*
	 * Re-Enable Bus Mastering:
	 * ------------------------
	 * Clear the PM2_CNT.ARB_DIS bit (bit #0), preserving all other bits.
	 */
	acpi_hw_register_bit_access(ACPI_WRITE, ACPI_MTX_LOCK, ARB_DIS, 0);

	/* TBD: [Unhandled]: Support 24-bit timers (this algorithm assumes 32-bit) */

	*pm_timer_ticks = timer;

	/*
	 * Re-enable interrupts after coming out of C2/C3.
	 */
	enable();

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_enter_cx
 *
 * PARAMETERS:  Processor_handle    - handle of the processor
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: Invoke the currently active processor Cx handler to put this
 *              processor to sleep.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_enter_cx (
	ACPI_IO_ADDRESS         pblk_address,
	u32                     *pm_timer_ticks)
{

	if (!acpi_hw_cx_handlers[acpi_hw_active_cx_state]) {
		return (AE_SUPPORT);
	}

	return (acpi_hw_cx_handlers[acpi_hw_active_cx_state] (pblk_address, pm_timer_ticks));
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_set_cx
 *
 * PARAMETERS:  State               - value (1-3) of the Cx state to 'make active'
 *
 * RETURN:      Function status.
 *
 * DESCRIPTION: Sets the state to use during calls to Acpi_hw_enter_cx().
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_set_cx (
	u32                     cx_state)
{
	/*
	 * Supported State?
	 * ----------------
	 */
	if ((cx_state < 1) || (cx_state > 3)) {
		return (AE_BAD_PARAMETER);
	}

	if (!acpi_hw_cx_handlers[cx_state]) {
		return (AE_SUPPORT);
	}

	/*
	 * New Cx State?
	 * -------------
	 * We only care when moving from one state to another...
	 */
	if (acpi_hw_active_cx_state == cx_state) {
		return (AE_OK);
	}

	/*
	 * Prepare to Use New State:
	 * -------------------------
	 * If the new Cx_state is C3, the BM_RLD bit must be set to allow
	 *  the generation of a bus master requets to cause the processor
	 *  in the C3 state to transition to the C0 state.
	 */
	switch (cx_state)
	{
	case 3:
		acpi_hw_register_bit_access (ACPI_WRITE, ACPI_MTX_LOCK, BM_RLD, 1);
		break;
	}

	/*
	 * Clean up from Old State:
	 * ------------------------
	 * If the old Cx_state was C3, the BM_RLD bit is reset. When the
	 *  bit is reset, the generation of a bus master request does not
	 *  effect any processor in the C3 state.
	 */
	switch (acpi_hw_active_cx_state)
	{
	case 3:
		acpi_hw_register_bit_access (ACPI_WRITE, ACPI_MTX_LOCK, BM_RLD, 0);
		break;
	}

	/*
	 * Enable:
	 * -------
	 */
	acpi_hw_active_cx_state = cx_state;

	return (AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_cx_info
 *
 * PARAMETERS:  Cx_states       - Information (latencies) on all Cx states
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: This function is called both to initialize Cx handling
 *              and retrieve the current Cx information (latency values).
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_get_cx_info (
	u32                     cx_states[])
{
	u8                      SMP_system = FALSE;


	if (!cx_states) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 *  TBD: [Unhandled] need to init SMP_system using info from the MAPIC
	 *       table.
	 */

	/*
	 * Set Defaults:
	 * -------------
	 * C0 and C1 support is implied (but what about that PROC_C1 register
	 * in the FADT?!?!).  Set C2/C3 to max. latency (not supported until
	 * proven otherwise).
	 */
	cx_states[0] = 0;
	cx_states[1] = 0;
	cx_states[2] = MAX_CX_STATE_LATENCY;
	cx_states[3] = MAX_CX_STATE_LATENCY;

	/*
	 * C2 Supported?
	 * -------------
	 * We're only supporting C2 when the latency is <= 100 microseconds,
	 * and on SMP systems when P_LVL2_UP (which indicates C2 only on UP)
	 * is not set.
	 */
	if (acpi_gbl_FADT->plvl2_lat <= 100) {
		if (!SMP_system) {
			acpi_hw_cx_handlers[2] = acpi_hw_enter_c2;
			cx_states[2] = acpi_gbl_FADT->plvl2_lat;
		}

		else if (!acpi_gbl_FADT->plvl2_up) {
			acpi_hw_cx_handlers[2] = acpi_hw_enter_c2;
			cx_states[2] = acpi_gbl_FADT->plvl2_lat;
		}
	}

	/*
	 * C3 Supported?
	 * -------------
	 * We're only supporting C3 on UP systems when the latency is
	 * <= 1000 microseconds and that include the ability to disable
	 * Bus Mastering while in C3 (ARB_DIS) but allows Bus Mastering
	 * requests to wake the system from C3 (BM_RLD).  Note his method
	 * of maintaining cache coherency (disabling of bus mastering)
	 * cannot be used on SMP systems, and flushing caches (e.g. WBINVD)
	 * is simply too costly (at this time).
	 */
	if (acpi_gbl_FADT->plvl3_lat <= 1000) {
		if (!SMP_system && (acpi_gbl_FADT->Xpm2_cnt_blk.address &&
			acpi_gbl_FADT->pm2_cnt_len))
		{
			acpi_hw_cx_handlers[3] = acpi_hw_enter_c3;
			cx_states[3] = acpi_gbl_FADT->plvl3_lat;
		}
	}

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_get_cx_handler
 *
 * PARAMETERS:  State           - the Cx state
 *              Handler         - pointer to location for the returned handler
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: This function is called to get an installed Cx state handler.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_get_cx_handler (
	u32                     cx_state,
	ACPI_C_STATE_HANDLER    *handler)
{

	if ((cx_state == 0) || (cx_state >= MAX_CX_STATES) || !handler) {
		return(AE_BAD_PARAMETER);
	}

	*handler = acpi_hw_cx_handlers[cx_state];

	return(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_hw_set_cx_handler
 *
 * PARAMETERS:  Cx_state        - the Cx state
 *              Handler         - new Cx state handler
 *
 * RETURN:      Status of function
 *
 * DESCRIPTION: This function is called to install a new Cx state handler.
 *
 ****************************************************************************/

ACPI_STATUS
acpi_hw_set_cx_handler (
	u32                     cx_state,
	ACPI_C_STATE_HANDLER    handler)
{

	if ((cx_state == 0) || (cx_state >= MAX_CX_STATES) || !handler) {
		return(AE_BAD_PARAMETER);
	}

	acpi_hw_cx_handlers[cx_state] = handler;

	return(AE_OK);
}


/**************************************************************************
 *
 *  FUNCTION:    Acpi_hw_local_pow
 *
 *  PARAMETERS:  x,y operands
 *
 *  RETURN:      result
 *
 *  DESCRIPTION: Compute x ^ y
 *
 *************************************************************************/

NATIVE_UINT
acpi_hw_local_pow (
	NATIVE_UINT             x,
	NATIVE_UINT             y)
{
	NATIVE_UINT             i;
	NATIVE_UINT             result = 1;


	for (i = 0; i < y; i++) {
		result = result * x;
	}

	return (result);
}


/**************************************************************************
 *
 *  FUNCTION:    Acpi_hw_enable_throttling
 *
 *  PARAMETERS:  Pblk_address       - Address of Pcnt (Processor Control)
 *                                      register
 *
 *  RETURN:      none
 *
 *  DESCRIPTION: Enable throttling by setting the THT_EN bit.
 *
 *************************************************************************/

void
acpi_hw_enable_throttling (
	ACPI_IO_ADDRESS         pblk_address)
{
	u32                     pblk_value;


	pblk_value = acpi_os_in32 (pblk_address);
	pblk_value = pblk_value | BIT_4;
	acpi_os_out32 (pblk_address, pblk_value);

	return;
}


/**************************************************************************
 *
 *  FUNCTION:   Acpi_hw_disable_throttling
 *
 *  PARAMETERS: Pblk_address        - Address of Pcnt (Processor Control)
 *                                      register
 *
 *  RETURN:     none
 *
 *  DESCRIPTION:Disable throttling by clearing the THT_EN bit
 *
 *************************************************************************/

void
acpi_hw_disable_throttling (
	ACPI_IO_ADDRESS         pblk_address)
{
	u32                     pblk_value;


	pblk_value = acpi_os_in32 (pblk_address);
	pblk_value = pblk_value & (~(u32)BIT_4);
	acpi_os_out32 (pblk_address, pblk_value);

	return;
}


/**************************************************************************
 *
 *  FUNCTION:    Acpi_hw_get_duty_cycle
 *
 *  PARAMETERS:  Duty_offset         Pcnt register duty cycle field offset
 *               Pblk_address        Pcnt register address in chipset
 *               Num_throttle_states # of CPU throttle states this system
 *                                      supports
 *
 *  RETURN:      none
 *
 *  DESCRIPTION: Get the duty cycle from the chipset
 *
 *************************************************************************/

u32
acpi_hw_get_duty_cycle (
	u8                      duty_offset,
	ACPI_IO_ADDRESS         pblk_address,
	u32                     num_throttle_states)
{
	NATIVE_UINT             index;
	u32                     duty32_value;
	u32                     pcnt_mask_off_duty_field;


	/*
	 *  Use Num_throttle_states - 1 as mask [ex. 8 - 1 = 7 (Fh)]
	 *  and then shift it into the right position
	 */
	pcnt_mask_off_duty_field = num_throttle_states - 1;

	/*
	 *  Read in the current value from the port
	 */
	duty32_value = acpi_os_in32 ((ACPI_IO_ADDRESS) pblk_address);

	/*
	 *  Shift the the value to LSB
	 */
	for (index = 0; index < (NATIVE_UINT) duty_offset; index++) {
		duty32_value = duty32_value >> 1;
	}

	/*
	 *  Get the duty field only
	 */
	duty32_value = duty32_value & pcnt_mask_off_duty_field;

	return ((u32) duty32_value);
}


/**************************************************************************
 *
 * FUNCTION:    Acpi_hw_program_duty_cycle
 *
 * PARAMETERS:  Duty_offset         Pcnt register duty cycle field offset
 *              Duty_cycle          duty cycle to program into chipset
 *              Pblk_address        Pcnt register address in chipset
 *              Num_throttle_states # of CPU throttle states this system
 *                                      supports
 *
 * RETURN:      none
 *
 * DESCRIPTION: Program chipset with specified duty cycle by bit-shifting the
 *              duty cycle bits to the appropriate offset, reading the duty
 *              cycle register, OR-ing in the duty cycle, and writing it to
 *              the Pcnt register.
 *
 *************************************************************************/

void
acpi_hw_program_duty_cycle (
	u8                      duty_offset,
	u32                     duty_cycle,
	ACPI_IO_ADDRESS         pblk_address,
	u32                     num_throttle_states)
{
	NATIVE_UINT             index;
	u32                     duty32_value;
	u32                     pcnt_mask_off_duty_field;
	u32                     port_value;


	/*
	 *  valid Duty_cycle passed
	 */
	duty32_value = duty_cycle;

	/*
	 *  use Num_throttle_states - 1 as mask [ex. 8 - 1 = 7 (Fh)]
	 *  and then shift it into the right position
	 */
	pcnt_mask_off_duty_field = num_throttle_states - 1;

	/*
	 *  Shift the mask
	 */
	for (index = 0; index < (NATIVE_UINT) duty_offset; index++) {
		pcnt_mask_off_duty_field = pcnt_mask_off_duty_field << 1;
		duty32_value = duty32_value << 1;
	}

	/*
	 *  Read in the current value from the port
	 */
	port_value = acpi_os_in32 ((ACPI_IO_ADDRESS) pblk_address);

	/*
	 *  Mask off the duty field so we don't OR in junk!
	 */
	port_value = port_value & (~pcnt_mask_off_duty_field);

	/*
	 *  OR in the bits we want to write out to the port
	 */
	port_value = (port_value | duty32_value) & (~(u32)BIT_4);

	/*
	 *  write it to the port
	 */
	acpi_os_out32 ((ACPI_IO_ADDRESS) pblk_address, port_value);

	return;
}


