/****************************************************************************
 *
 * Module Name: tzpolicy.c -
 *   $Revision: 30 $
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
 * TBD: 1. Support performance-limit control for non-processor devices
 *         (those listed in _TZD, e.g. graphics).
 */

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

	if (ACPI_FAILURE(status))
		return status;
	else
		return request.status;
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

	if (!tz) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (tz->policy.temperature >= tz->policy.thresholds.critical.temperature) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Critical (S5) threshold reached.\n"));
		/* TBD:	Need method for shutting down system. */
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    tz_policy_hot
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_policy_hot(
	TZ_CONTEXT		*tz)
{
	FUNCTION_TRACE("tz_policy_hot");

	if (!tz || !tz->policy.thresholds.hot.is_valid) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (tz->policy.temperature >= tz->policy.thresholds.hot.temperature) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Critical (S4) threshold reached.\n"));
		/* TBD:	Need method for invoking OS-level critical suspend. */
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
	TZ_PASSIVE_THRESHOLD	*passive = NULL;
	static u32		last_temperature = 0;
	s32			trend = 0;
	u32			i = 0;

	FUNCTION_TRACE("tz_policy_passive");

	if (!tz || !tz->policy.thresholds.passive.is_valid) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	passive = &(tz->policy.thresholds.passive);

	if (tz->policy.temperature >= passive->temperature) {
		/*
		 * Thermal trend?
		 * --------------
		 * Using the passive cooling equation (see the ACPI
		 * Specification), calculate the current thermal trend
		 * (a.k.a. performance delta).
		 */
		trend = passive->tc1 * (tz->policy.temperature - last_temperature) + passive->tc2 * (tz->policy.temperature - passive->temperature);
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "trend[%d] = TC1[%d]*(temp[%d]-last[%d]) + TC2[%d]*(temp[%d]-passive[%d])\n", trend, passive->tc1, tz->policy.temperature, last_temperature, passive->tc2, tz->policy.temperature, passive->temperature));

		last_temperature = tz->policy.temperature;

		/*
		 * Heating Up?
		 * -----------
		 * Decrease thermal performance limit on all passive
		 * cooling devices (processors).
		 */
		if (trend > 0) {
			for (i=0; i<passive->devices.count; i++)
				set_performance_limit(passive->devices.handles[i], PR_PERF_DEC);
		}
		/*
		 * Cooling Off?
		 * ------------
		 * Increase thermal performance limit on all passive
		 * cooling devices (processors).
		 */
		else if (trend < 0) {
			for (i=0; i<passive->devices.count; i++)
				set_performance_limit(passive->devices.handles[i], PR_PERF_INC);
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
	TZ_ACTIVE_THRESHOLD	*active = NULL;
	u32                     i,j = 0;

	FUNCTION_TRACE("tz_policy_active");

	if (!tz || !tz->policy.thresholds.active[0].is_valid) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	for (i=0; i<TZ_MAX_ACTIVE_THRESHOLDS; i++) {

		active = &(tz->policy.thresholds.active[i]);
		if (!active || !active->is_valid)
			break;

		/*
		 * Above Threshold?
		 * ----------------
		 * If not already enabled, turn ON all cooling devices
		 * associated with this active threshold.
		 */
		if ((tz->policy.temperature >= active->temperature) && (active->cooling_state != TZ_COOLING_ENABLED)) {
			for (j = 0; j < active->devices.count; j++) {
				status = bm_set_device_power_state(active->devices.handles[j], ACPI_STATE_D0);
				if (ACPI_SUCCESS(status)) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Cooling device [%02x] now ON.\n", active->devices.handles[j]));
				}
				else {
					ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Unable to turn ON cooling device [%02x].\n", active->devices.handles[j]));
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
			for (j = 0; j < active->devices.count; j++) {
				status = bm_set_device_power_state(active->devices.handles[j], ACPI_STATE_D3);
				if (ACPI_SUCCESS(status)) {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Cooling device [%02x] now OFF.\n", active->devices.handles[j]));
				}
				else {
					ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Unable to turn OFF cooling device [%02x].\n", active->devices.handles[j]));
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
	TZ_POLICY		*policy = NULL;
	TZ_THRESHOLDS		*thresholds = NULL;
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
	policy = &(tz->policy);
	thresholds = &(tz->policy.thresholds);

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
	status = tz_get_temperature(tz);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/*
	 * Calculate State:
	 * ----------------
	 */
	policy->state = TZ_STATE_OK;

	/* Critical? */
	if (policy->temperature >= thresholds->critical.temperature)
		policy->state |= TZ_STATE_CRITICAL;

	/* Hot? */
	if ((thresholds->hot.is_valid) &&  (policy->temperature >= thresholds->hot.temperature))
		policy->state |= TZ_STATE_CRITICAL;

	/* Passive? */
	if ((thresholds->passive.is_valid) && (policy->temperature >= thresholds->passive.temperature))
		policy->state |= TZ_STATE_PASSIVE;

	/* Active? */
	if (thresholds->active[0].is_valid) {
		for (i=0; i<TZ_MAX_ACTIVE_THRESHOLDS; i++) {
			if ((thresholds->active[i].is_valid) && (policy->temperature >= thresholds->active[i].temperature)) {
				policy->state |= TZ_STATE_ACTIVE;
				if (i > active_index)
					active_index = i;
			}
		}
		policy->state |= active_index;
	}

	/*
	 * Invoke Policy:
	 * --------------
	 * Note that policy must be invoked both when 'going into' a
	 * policy state (e.g. to allow fans to be turned on) and 'going
	 * out of' a policy state (e.g. to allow fans to be turned off);
	 * thus we must preserve the previous state.
	 */
	if (policy->state & TZ_STATE_CRITICAL)
		tz_policy_critical(tz);
	if (policy->state & TZ_STATE_HOT)
		tz_policy_hot(tz);
	if ((policy->state & TZ_STATE_PASSIVE) || (previous_state & TZ_STATE_PASSIVE))
		tz_policy_passive(tz);
	if ((policy->state & TZ_STATE_ACTIVE) || (previous_state & TZ_STATE_ACTIVE))
		tz_policy_active(tz);

	/*
	 * Calculate Sleep Time:
	 * ---------------------
	 * If we're in the passive state, use _TSP's value.  Otherwise
	 * use _TZP or the OS's default polling frequency.  If no polling
	 * frequency is specified then we'll wait forever (that is, until
	 * a thermal event occurs -- e.g. never poll).  Note that _TSP
	 * and _TZD values are given in 1/10th seconds.
	 */
	if (policy->state & TZ_STATE_PASSIVE)
		sleep_time = thresholds->passive.tsp * 100;
	else if (policy->polling_freq > 0)
		sleep_time = policy->polling_freq * 100;
	else
		sleep_time = WAIT_FOREVER;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Thermal_zone[%02x]: temperature[%d] state[%08x]\n", tz->device_handle, policy->temperature, policy->state));
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Scheduling next poll in [%d]ms.\n", sleep_time));

	/*
	 * Schedule Next Poll:
	 * -------------------
	 */
	if (sleep_time < WAIT_FOREVER) {
		if (timer_pending(&(policy->timer)))
			mod_timer(&(policy->timer), (HZ*sleep_time)/1000);
		else {
			policy->timer.data = (u32)tz;
			policy->timer.function = tz_policy_run;
			policy->timer.expires = jiffies + (HZ*sleep_time)/1000;
			add_timer(&(policy->timer));
		}
	}
	else {
		if (timer_pending(&(policy->timer)))
			del_timer(&(policy->timer));
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
	status = acpi_os_queue_for_execution(OSD_PRIORITY_GPE,  tz_policy_check, (void*)data);
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
	TZ_THRESHOLDS           *thresholds = NULL;
	u32                     i,j = 0;

	FUNCTION_TRACE("tz_policy_add_device");

	if (!tz) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Adding policy for thermal zone [%02x].\n", tz->device_handle));

	/*
	 * Get Thresholds:
	 * ---------------
	 */
	status = tz_get_thresholds(tz);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Initialize Policies:
	 * --------------------
	 */
	if (tz->policy.thresholds.passive.is_valid) {
		for (i=0; i<tz->policy.thresholds.passive.devices.count; i++)
			set_performance_limit(tz->policy.thresholds.passive.devices.handles[i], PR_PERF_MAX);
		tz_policy_passive(tz);
	}
	if (tz->policy.thresholds.active[0].is_valid)
		tz_policy_active(tz);

	/*
	 * Initialize Policy Timer:
	 * ------------------------
	 */
	init_timer(&(tz->policy.timer));

	/*
	 * Start Policy:
	 * -------------
	 * Run an initial check using this zone's policy.
	 */
	tz_policy_check(tz);

	return_ACPI_STATUS(AE_OK);
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
	if (timer_pending(&(tz->policy.timer)))
		del_timer(&(tz->policy.timer));

	/*
	 * Reset thermal performance limit on all processors back to max.
	 */
	if (tz->policy.thresholds.passive.is_valid) {
		for (i=0; i<tz->policy.thresholds.passive.devices.count; i++)
			set_performance_limit(tz->policy.thresholds.passive.devices.handles[i], PR_PERF_MAX);
	}

	return_ACPI_STATUS(AE_OK);
}
