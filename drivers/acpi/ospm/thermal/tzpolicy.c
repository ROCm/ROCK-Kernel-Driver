/****************************************************************************
 *
 * Module Name: tzpolicy.c -
 *   $Revision: 28 $
 *
 ****************************************************************************/

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
 * TBD: 1. Move to user-space!
 *	2. Support ACPI 2.0 items (e.g. _TZD, _HOT).
 *      3. Support performance-limit control for non-processor devices
 *         (those listed in _TZD, e.g. graphics).
 */

/* TBD: Linux specific */
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/pm.h>

#include <acpi.h>
#include <bm.h>
#include "tz.h"


#define _COMPONENT		ACPI_THERMAL
	MODULE_NAME		("tzpolicy")


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

extern int TZP;

void
tz_policy_run (
	unsigned long           data);


/****************************************************************************
 *                              Internal Functions
 ****************************************************************************/

acpi_status
set_performance_limit (
	BM_HANDLE               device_handle,
	u32			flag)
{
	acpi_status             status;
	BM_REQUEST              request;

	request.status = AE_OK;
	request.handle = device_handle;
	request.command = PR_COMMAND_SET_PERF_LIMIT;
	request.buffer.length = sizeof(u32);
	request.buffer.pointer = &flag;

	status = bm_request(&request);

	if (ACPI_FAILURE(status)) {
		return status;
	}
	else {
		return request.status;
	}
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_critical
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_policy_critical(
	TZ_CONTEXT		*tz)
{
	FUNCTION_TRACE("tz_policy_critical");

	if (!tz || !tz->policy.critical.threshold) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (tz->policy.temperature >=
		tz->policy.critical.threshold->temperature) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Critical threshold reached - shutting down system.\n"));
		/* TBD:	Need method for calling 'halt' - OSL function? */
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_passive
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_policy_passive(
	TZ_CONTEXT		*tz)
{
	TZ_PASSIVE_POLICY	*passive = NULL;
	static u32		last_temperature = 0;
	s32			trend = 0;
	u32			i = 0;

	FUNCTION_TRACE("tz_policy_passive");

	if (!tz || !tz->policy.passive.threshold) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	passive = &(tz->policy.passive);

	if (tz->policy.temperature >= passive->threshold->temperature) {
		/*
		 * Thermal trend?
		 * --------------
		 * Using the passive cooling equation (see the ACPI
		 * Specification), calculate the current thermal trend
		 * (a.k.a. performance delta).
		 */
		trend = passive->tc1 *
			(tz->policy.temperature - last_temperature) +
			passive->tc2 *
			(tz->policy.temperature - passive->threshold->temperature);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "trend[%d] = TC1[%d]*(temp[%d]-last[%d]) + TC2[%d]*(temp[%d]-passive[%d])\n",
			trend, passive->tc1, tz->policy.temperature,
			last_temperature, passive->tc2, tz->policy.temperature,
			passive->threshold->temperature));

		last_temperature = tz->policy.temperature;

		/*
		 * Heating Up?
		 * -----------
		 * Decrease thermal performance limit on all passive
		 * cooling devices (processors).
		 */
		if (trend > 0) {
			for (i=0; i<passive->threshold->cooling_devices.count; i++) {
				set_performance_limit(
					passive->threshold->cooling_devices.handles[i],
					PR_PERF_DEC);
			}
		}
		/*
		 * Cooling Off?
		 * ------------
		 * Increase thermal performance limit on all passive
		 * cooling devices (processors).
		 */
		else if (trend < 0) {
			for (i=0; i<passive->threshold->cooling_devices.count; i++) {
				set_performance_limit(
					passive->threshold->cooling_devices.handles[i],
					PR_PERF_INC);
			}
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_active
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_policy_active(
	TZ_CONTEXT              *tz)
{
	acpi_status             status = AE_OK;
	TZ_THRESHOLD            *active = NULL;
	u32                     i,j = 0;

	FUNCTION_TRACE("tz_policy_active");

	if (!tz || !tz->policy.active.threshold) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	for (i = 0; i < TZ_MAX_ACTIVE_THRESHOLDS; i++) {

		active = tz->policy.active.threshold[i];
		if (!active) {
			break;
		}

		/*
		 * Above Threshold?
		 * ----------------
		 * If not already enabled, turn ON all cooling devices
		 * associated with this active threshold.
		 */
		if ((tz->policy.temperature >= active->temperature) &&
			(active->cooling_state != TZ_COOLING_ENABLED)) {

			for (j = 0; j < active->cooling_devices.count; j++) {

				status = bm_set_device_power_state(
					active->cooling_devices.handles[j],
					ACPI_STATE_D0);

				if (ACPI_SUCCESS(status)) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Cooling device [%02x] now ON.\n", active->cooling_devices.handles[j]));
				}
				else {
					ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Unable to turn ON cooling device [%02x].\n", active->cooling_devices.handles[j]));
				}
			}

			active->cooling_state = TZ_COOLING_ENABLED;
		}
		/*
		 * Below Threshold?
		 * ----------------
		 * Turn OFF all cooling devices associated with this
		 * threshold.  Note that by checking "if not disabled" we
		 * turn off all cooling devices for thresholds in the
		 * TZ_COOLING_STATE_UNKNOWN state, useful as a level-set
		 * during the first pass.
		 */
		else if (active->cooling_state != TZ_COOLING_DISABLED) {

			for (j = 0; j < active->cooling_devices.count; j++) {

				status = bm_set_device_power_state(
					active->cooling_devices.handles[j],
					ACPI_STATE_D3);

				if (ACPI_SUCCESS(status)) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Cooling device [%02x] now OFF.\n", active->cooling_devices.handles[j]));
				}
				else {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Unable to turn OFF cooling device [%02x].\n", active->cooling_devices.handles[j]));
				}
			}

			active->cooling_state = TZ_COOLING_DISABLED;
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_check
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Note that this function will get called whenever:
 *                1. A thermal event occurs.
 *                2. The polling/sampling time period expires.
 *
 ****************************************************************************/

void
tz_policy_check (
	void                    *context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT              *tz = NULL;
	u32                     previous_temperature = 0;
	u32                     previous_state = 0;
	u32                     active_index = 0;
	u32                     i = 0;
	u32                     sleep_time = 0;

	FUNCTION_TRACE("tz_policy_check");

	if (!context) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	tz = (TZ_CONTEXT*)context;

	/*
	 * Preserve Previous State:
	 * ------------------------
	 */
	previous_temperature = tz->policy.temperature;
	previous_state = tz->policy.state;

	/*
	 * Get Temperature:
	 * ----------------
	 */
	status = tz_get_temperature(tz, &(tz->policy.temperature));
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/*
	 * Calculate State:
	 * ----------------
	 */
	tz->policy.state = TZ_STATE_OK;

	/* Critical? */
	if ((tz->policy.critical.threshold) &&
		(tz->policy.temperature >= tz->policy.critical.threshold->temperature)) {
		tz->policy.state |= TZ_STATE_CRITICAL;
	}

	/* Passive? */
	if ((tz->policy.passive.threshold) &&
		(tz->policy.temperature >= tz->policy.passive.threshold->temperature)) {
		tz->policy.state |= TZ_STATE_PASSIVE;
	}

	/* Active? */
	if (tz->policy.active.threshold[0]) {
		for (i=0; i<tz->policy.active.threshold_count; i++) {
			if ((tz->policy.active.threshold[i]) &&
				(tz->policy.temperature >= tz->policy.active.threshold[i]->temperature)) {
			    tz->policy.state |= TZ_STATE_ACTIVE;
			    if (tz->policy.active.threshold[i]->index > active_index) {
				    active_index = tz->policy.active.threshold[i]->index;
			    }
			}
		}
		tz->policy.state |= active_index;
	}

	/*
	 * Invoke Policy:
	 * --------------
	 * Note that policy must be invoked both when 'going into' a
	 * policy state (e.g. to allow fans to be turned on) and 'going
	 * out of' a policy state (e.g. to allow fans to be turned off);
	 * thus we must preserve the previous state.
	 */
	if (tz->policy.state & TZ_STATE_CRITICAL) {
		tz_policy_critical(tz);
	}
	if ((tz->policy.state & TZ_STATE_PASSIVE) ||
		(previous_state & TZ_STATE_PASSIVE)) {
		tz_policy_passive(tz);
	}
	if ((tz->policy.state & TZ_STATE_ACTIVE) ||
		(previous_state & TZ_STATE_ACTIVE)) {
		tz_policy_active(tz);
	}

	/*
	 * Calculate Sleep Time:
	 * ---------------------
	 * If we're in the passive state, use _TSP's value.  Otherwise
	 * use _TZP or the OS's default polling frequency.  If no polling
	 * frequency is specified then we'll wait forever (that is, until
	 * a thermal event occurs -- e.g. never poll).  Note that _TSP
	 * and _TZD values are given in 1/10th seconds.
	 */
	if (tz->policy.state & TZ_STATE_PASSIVE) {
		sleep_time = tz->policy.passive.tsp * 100;
	}
	else if (tz->policy.polling_freq > 0) {
		sleep_time = tz->policy.polling_freq * 100;
	}
	else {
		sleep_time = WAIT_FOREVER;
	}

#ifdef ACPI_DEBUG
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Thermal_zone[%02x]: temperature[%d] state[%08x]\n", tz->device_handle, tz->policy.temperature, tz->policy.state));
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Scheduling next poll in [%d]ms.\n", sleep_time));
#endif /*ACPI_DEBUG*/

	/*
	 * Schedule Next Poll:
	 * -------------------
	 */
	if (sleep_time < WAIT_FOREVER) {
		if (timer_pending(&(tz->policy.timer))) {
			mod_timer(&(tz->policy.timer),
				(HZ*sleep_time)/1000);
		}
		else {
			tz->policy.timer.data = (u32)tz;
			tz->policy.timer.function = tz_policy_run;
			tz->policy.timer.expires =
				jiffies + (HZ*sleep_time)/1000;
			add_timer(&(tz->policy.timer));
		}
	}
	else {
		if (timer_pending(&(tz->policy.timer))) {
			del_timer(&(tz->policy.timer));
		}
	}

	return_VOID;
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_run
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
tz_policy_run (
	unsigned long           data)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("tz_policy_run");

	if (!data) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) context.\n"));
		return_VOID;
	}

	/*
	 * Defer to Non-Interrupt Level:
	 * -----------------------------
	 * Note that all Linux kernel timers run at interrupt-level (ack!).
	 */
	status = acpi_os_queue_for_execution(OSD_PRIORITY_GPE,
		tz_policy_check, (void*)data);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Error invoking thermal policy.\n"));
	}

	return_VOID;
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_add_device
 *
 * PARAMETERS:
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_policy_add_device (
	TZ_CONTEXT		*tz)
{
	acpi_status             status = AE_OK;
	TZ_THRESHOLD            *threshold = NULL;
	u32                     i,j = 0;

	FUNCTION_TRACE("tz_policy_add_device");

	if (!tz) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Adding policy for thermal zone [%02x].\n", tz->device_handle));

	/*
	 * Temperature:
	 * ------------
	 * Make sure we can read the zone's current temperature (_TMP).
	 * If we can't, there's no use in doing any policy (abort).
	 */
	status = tz_get_temperature(tz, &(tz->policy.temperature));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Polling Frequency:
	 * ------------------
	 * If a _TZP object doesn't exist, use the OS default polling
	 * frequency.
	 */
	status = bm_evaluate_simple_integer(tz->acpi_handle, "_TZP",
		&(tz->policy.polling_freq));
	if (ACPI_FAILURE(status)) {
		tz->policy.polling_freq = TZP;
	}
	status = AE_OK;

	/*
	 * Get Thresholds:
	 * ---------------
	 * Get all of the zone's thresholds, parse, and organize for
	 * later use.
	 */
	status = tz_get_thresholds(tz, &(tz->policy.threshold_list));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Initialize Policies:
	 * --------------------
	 */
	for (i = 0; i < tz->policy.threshold_list.count; i++) {

		threshold = &(tz->policy.threshold_list.thresholds[i]);

		switch (threshold->type) {

		case TZ_THRESHOLD_CRITICAL:
			tz->policy.critical.threshold = threshold;
			break;

		case TZ_THRESHOLD_PASSIVE:

			/*
			 * Set thermal performance limit on all processors
			 * to max.
			 */
			for (j=0; j<threshold->cooling_devices.count; j++) {
				set_performance_limit(
					threshold->cooling_devices.handles[j],
					PR_PERF_MAX);
			}

			/*
			 * Get passive cooling constants.
			 */
			status = bm_evaluate_simple_integer(tz->acpi_handle,
				"_TC1", &(tz->policy.passive.tc1));
			if (ACPI_FAILURE(status)) {
				break;
			}

			status = bm_evaluate_simple_integer(tz->acpi_handle,
				"_TC2", &(tz->policy.passive.tc2));
			if (ACPI_FAILURE(status)) {
				break;
			}

			status = bm_evaluate_simple_integer(tz->acpi_handle,
				"_TSP", &(tz->policy.passive.tsp));
			if (ACPI_FAILURE(status)) {
				break;
			}

			tz->policy.passive.threshold = threshold;

			tz_policy_passive(tz);

			break;

		case TZ_THRESHOLD_ACTIVE:
			tz->policy.active.threshold[threshold->index] = threshold;
			tz_policy_active(tz);
			break;
		}
	}

	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Initialize Policy Timer:
	 * ------------------------
	 * TBD: Linux-specific - remove when policy moves to user-space.
	 */
	init_timer(&(tz->policy.timer));

	/*
	 * Start Policy:
	 * -------------
	 * Run an initial check using this zone's policy.
	 */
	tz_policy_check(tz);

	return_ACPI_STATUS(status);
}


/*****************************************************************************
 *
 * FUNCTION:    tz_policy_remove_device
 *
 * PARAMETERS:
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_policy_remove_device(
	TZ_CONTEXT		*tz)
{
	u32			i = 0;

	FUNCTION_TRACE("tz_remove_device");

	if (!tz) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing policy for thermal zone [%02x].\n", tz->device_handle));

	/*
	 * Delete the thermal zone policy timer entry, if exists.
	 */
	if (timer_pending(&(tz->policy.timer))) {
		del_timer(&(tz->policy.timer));
	}

	/*
	 * Reset thermal performance limit on all processors back to max.
	 */
	if (tz->policy.passive.threshold) {
		for (i=0; i<tz->policy.passive.threshold->cooling_devices.count; i++) {
			set_performance_limit(
				tz->policy.passive.threshold->cooling_devices.handles[i],
				PR_PERF_MAX);
		}
	}

	return_ACPI_STATUS(AE_OK);
}
