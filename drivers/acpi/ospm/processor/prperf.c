/*****************************************************************************
 *
 * Module Name: prperf.c
 *              $Revision: 21 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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

/*
 * TBD: 1. Support ACPI 2.0 processor performance states (not just throttling).
 *      2. Fully implement thermal -vs- power management limit control.
 */


#include <acpi.h>
#include <bm.h>
#include "pr.h"

#define _COMPONENT		ACPI_PROCESSOR
	MODULE_NAME		("prperf")


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

extern fadt_descriptor_rev2	acpi_fadt;
const u32			POWER_OF_2[] = {1,2,4,8,16,32,64,128,256,512};


/****************************************************************************
 *
 * FUNCTION:    pr_perf_get_frequency
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_perf_get_frequency (
	PR_CONTEXT		*processor,
	u32			*frequency) {
	acpi_status		status = AE_OK;

	FUNCTION_TRACE("pr_perf_get_frequency");

	if (!processor || !frequency) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* TBD: Generic method to calculate processor frequency. */

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_perf_get_state
 *
 * PARAMETERS:
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

/* TBD:	Include support for _real_ performance states (not just throttling). */

acpi_status
pr_perf_get_state (
	PR_CONTEXT              *processor,
	u32                     *state)
{
	u32                     pblk_value = 0;
	u32                     duty_mask = 0;
	u32                     duty_cycle = 0;

	FUNCTION_TRACE("pr_perf_get_state");

	if (!processor || !state) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (processor->performance.state_count == 1) {
		*state = 0;
		return_ACPI_STATUS(AE_OK);
	}

	acpi_os_read_port(processor->pblk.address, &pblk_value, 32);

	/*
	 * Throttling Enabled?
	 * -------------------
	 * If so, calculate the current throttling state, otherwise return
	 * '100% performance' (state 0).
	 */
	if (pblk_value & 0x00000010) {

		duty_mask = processor->performance.state_count - 1;
		duty_mask <<= acpi_fadt.duty_offset;

		duty_cycle = pblk_value & duty_mask;
		duty_cycle >>= acpi_fadt.duty_offset;

		if (duty_cycle == 0) {
			*state = 0;
		}
		else {
			*state = processor->performance.state_count -
				duty_cycle;
		}
	}
	else {
		*state = 0;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Processor [%02x] is at performance state [%d%%].\n", processor->device_handle, processor->performance.state[*state].performance));

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    pr_perf_set_state
 *
 * PARAMETERS:
 *
 * RETURN:      AE_OK
 *              AE_BAD_PARAMETER
 *              AE_BAD_DATA         Invalid target throttling state.
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

/* TBD: Includes support for _real_ performance states (not just throttling). */

acpi_status
pr_perf_set_state (
	PR_CONTEXT              *processor,
	u32                     state)
{
	u32                     pblk_value = 0;
	u32                     duty_mask = 0;
	u32                     duty_cycle = 0;
	u32                     i = 0;

	FUNCTION_TRACE ("pr_perf_set_state");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (state > (processor->performance.state_count - 1)) {
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	if ((state == processor->performance.active_state) ||
		(processor->performance.state_count == 1)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Calculate Duty Cycle/Mask:
	 * --------------------------
	 * Note that we don't support duty_cycle values that span bit 4.
	 */
	if (state) {
		duty_cycle = processor->performance.state_count - state;
		duty_cycle <<= acpi_fadt.duty_offset;
	}
	else {
		duty_cycle = 0;
	}

	duty_mask = ~((u32)(processor->performance.state_count - 1));
	for (i=0; i<acpi_fadt.duty_offset; i++) {
		duty_mask <<= acpi_fadt.duty_offset;
		duty_mask += 1;
	}

	/*
	 * Disable Throttling:
	 * -------------------
	 * Got to turn it off before you can change the duty_cycle value.
	 * Throttling is disabled by writing a 0 to bit 4.
	 */
	acpi_os_read_port(processor->pblk.address, &pblk_value, 32);
	if (pblk_value & 0x00000010) {
		pblk_value &= 0xFFFFFFEF;
		acpi_os_write_port(processor->pblk.address, pblk_value, 32);
	}

	/*
	 * Set Duty Cycle:
	 * ---------------
	 * Mask off the old duty_cycle value, mask in the new.
	 */
	pblk_value &= duty_mask;
	pblk_value |= duty_cycle;
	acpi_os_write_port(processor->pblk.address, pblk_value, 32);

	/*
	 * Enable Throttling:
	 * ------------------
	 * But only for non-zero (non-100% performance) states.
	 */
	if (state) {
		pblk_value |= 0x00000010;
		acpi_os_write_port(processor->pblk.address, pblk_value, 32);
	}

	processor->performance.active_state = state;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Processor [%02x] set to performance state [%d%%].\n", processor->device_handle, processor->performance.state[state].performance));

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    pr_perf_set_limit
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_perf_set_limit (
	PR_CONTEXT              *processor,
	u32                     limit)
{
	acpi_status		status = AE_OK;
	PR_PERFORMANCE		*performance = NULL;

	FUNCTION_TRACE ("pr_perf_set_limit");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	performance = &(processor->performance);

	/*
	 * Set Limit:
	 * ----------
	 * TBD:  Properly manage thermal and power limits (only set
	 *	 performance state iff...).
	 */
	switch (limit) {

	case PR_PERF_DEC:
		if (performance->active_state <
			(performance->state_count-1)) {
			status = pr_perf_set_state(processor,
				(performance->active_state+1));
		}
		break;

	case PR_PERF_INC:
		if (performance->active_state > 0) {
			status = pr_perf_set_state(processor,
				(performance->active_state-1));
		}
		break;

	case PR_PERF_MAX:
		if (performance->active_state != 0) {
			status = pr_perf_set_state(processor, 0);
		}
		break;

	default:
		return_ACPI_STATUS(AE_BAD_DATA);
		break;
	}

	if (ACPI_SUCCESS(status)) {
		performance->thermal_limit = performance->active_state;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Processor [%02x] thermal performance limit set to [%d%%].\n", processor->device_handle, processor->performance.state[performance->active_state].performance));

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *                             External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    pr_perf_add_device
 *
 * PARAMETERS:  processor		Our processor-specific context.
 *
 * RETURN:      AE_OK
 *              AE_BAD_PARAMETER
 *
 * DESCRIPTION: Calculates the number of throttling states and the state
 *              performance/power values.
 *
 ****************************************************************************/

/* TBD: Support duty_cycle values that span bit 4. */

acpi_status
pr_perf_add_device (
	PR_CONTEXT              *processor)
{
	acpi_status             status = AE_OK;
	u32                     i = 0;
	u32                     performance_step = 0;
	u32                     percentage = 0;

	FUNCTION_TRACE("pr_perf_add_device");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Valid PBLK?
	 * -----------
	 * For SMP it is common to have the first (boot) processor have a
	 * valid PBLK while all others do not -- which implies that
	 * throttling has system-wide effects (duty_cycle programmed into
	 * the chipset effects all processors).
	 */
	if ((processor->pblk.length < 6) || !processor->pblk.address) {
		processor->performance.state_count = 1;
	}

	/*
	 * Valid Duty Offset/Width?
	 * ------------------------
	 * We currently only support duty_cycle values that fall within
	 * bits 0-3, as things get complicated when this value spans bit 4
	 * (the throttling enable/disable bit).
	 */
	else if ((acpi_fadt.duty_offset + acpi_fadt.duty_width) > 4) {
		processor->performance.state_count = 1;
	}

	/*
	 * Compute State Count:
	 * --------------------
	 * The number of throttling states is computed as 2^duty_width,
	 * but limited by PR_MAX_THROTTLE_STATES.  Note that a duty_width
	 * of zero results is one throttling state (100%).
	 */
	else {
		processor->performance.state_count =
			POWER_OF_2[acpi_fadt.duty_width];
	}

	if (processor->performance.state_count > PR_MAX_THROTTLE_STATES) {
		processor->performance.state_count = PR_MAX_THROTTLE_STATES;
	}

	/*
	 * Compute State Values:
	 * ---------------------
	 * Note that clock throttling displays a linear power/performance
	 * relationship (at 50% performance the CPU will consume 50% power).
	 */
	performance_step = (1000 / processor->performance.state_count);

	for (i=0; i<processor->performance.state_count; i++) {
		percentage = (1000 - (performance_step * i))/10;
		processor->performance.state[i].performance = percentage;
		processor->performance.state[i].power = percentage;
	}

	/*
	 * Get Current State:
	 * ------------------
	 */
	status = pr_perf_get_state(processor, &(processor->performance.active_state));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Set to Maximum Performance:
	 * ---------------------------
	 * We'll let subsequent policy (e.g. thermal/power) decide to lower
	 * performance if it so chooses, but for now crank up the speed.
	 */
	if (0 != processor->performance.active_state) {
		status = pr_perf_set_state(processor, 0);
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    pr_perf_remove_device
 *
 * PARAMETERS:
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
pr_perf_remove_device (
	PR_CONTEXT              *processor)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("pr_perf_remove_device");

	if (!processor) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	MEMSET(&(processor->performance), 0, sizeof(PR_PERFORMANCE));

	return_ACPI_STATUS(status);
}

