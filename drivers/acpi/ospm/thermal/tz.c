/*****************************************************************************
 *
 * Module Name: tz.c
 *   $Revision: 40 $
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
 * TBD: 1. Finish /proc interface (threshold values, _SCP changes, etc.)
 *	2. Update policy for ACPI 2.0 compliance
 *	3. Check for all required methods prior to enabling a threshold
 *	4. Support for multiple processors in a zone (passive cooling devices)
 */

#include <acpi.h>
#include <bm.h>
#include "tz.h"

#define _COMPONENT		ACPI_THERMAL
	MODULE_NAME		("tz")


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    tz_print
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Prints out information on a specific thermal zone.
 *
 ****************************************************************************/

void
tz_print (
	TZ_CONTEXT		*thermal_zone)
{
#ifdef ACPI_DEBUG
	acpi_buffer		buffer;
	u32			i,j = 0;
	TZ_THRESHOLD            *threshold = NULL;

	PROC_NAME("tz_print");

	if (!thermal_zone) {
		return;
	}

	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return;
	}

	/*
	 * Get the full pathname for this ACPI object.
	 */
	acpi_get_name(thermal_zone->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * Print out basic thermal zone information.
	 */
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Thermal_zone[%02x]:[%p] %s\n", thermal_zone->device_handle, thermal_zone->acpi_handle, buffer.pointer));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   temperature[%d] state[%08x]\n", thermal_zone->policy.temperature, thermal_zone->policy.state));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   cooling_mode[%08x] polling_freq[%d]\n", thermal_zone->policy.cooling_mode, thermal_zone->policy.polling_freq));

	for (i=0; i<thermal_zone->policy.threshold_list.count; i++) {

		threshold = &(thermal_zone->policy.threshold_list.thresholds[i]);

		switch (threshold->type) {
		case TZ_THRESHOLD_CRITICAL:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   critical[%d]\n", threshold->temperature));
			break;
		case TZ_THRESHOLD_PASSIVE:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   passive[%d]: tc1[%d] tc2[%d] tsp[%d]\n", threshold->temperature, thermal_zone->policy.passive.tc1, thermal_zone->policy.passive.tc2, thermal_zone->policy.passive.tsp));
			break;
		case TZ_THRESHOLD_ACTIVE:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   active[%d]: index[%d]\n", threshold->temperature, threshold->index));
			break;
		default:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   unknown[%d]\n", threshold->temperature));
			break;
		}

		if (threshold->cooling_devices.count > 0) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|     cooling_devices"));
			for (j=0; (j<threshold->cooling_devices.count && j<10); j++) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "[%02x]", threshold->cooling_devices.handles[j]));
			}
			
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));
		}
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	acpi_os_free(buffer.pointer);
#endif /*ACPI_DEBUG*/

	return;
}


/****************************************************************************
 *
 * FUNCTION:    tz_get_temperaturee
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_get_temperature (
	TZ_CONTEXT              *thermal_zone,
	u32                     *temperature)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("tz_get_temperature");

	if (!thermal_zone || !temperature) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Evaluate the _TMP driver method to get the current temperature.
	 */
	status = bm_evaluate_simple_integer(thermal_zone->acpi_handle,
		"_TMP", temperature);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_set_cooling_preference
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_set_cooling_preference (
	TZ_CONTEXT              *thermal_zone,
	TZ_COOLING_MODE         cooling_mode)
{
	acpi_status             status = AE_OK;
	acpi_object_list        arg_list;
	acpi_object             arg0;

	FUNCTION_TRACE("tz_set_cooling_preference");

	if (!thermal_zone || ((cooling_mode != TZ_COOLING_MODE_ACTIVE) &&
		(cooling_mode != TZ_COOLING_MODE_PASSIVE))) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Build the argument list, which simply consists of the current
	 * cooling preference.
	 */
	MEMSET(&arg_list, 0, sizeof(acpi_object));
	arg_list.count = 1;
	arg_list.pointer = &arg0;

	MEMSET(&arg0, 0, sizeof(acpi_object));
	arg0.type = ACPI_TYPE_INTEGER;
	arg0.integer.value = cooling_mode;

	/*
	 * Evaluate "_SCP" - setting the new cooling preference.
	 */
	status = acpi_evaluate_object(thermal_zone->acpi_handle, "_SCP",
		&arg_list, NULL);

	return_ACPI_STATUS(status);
}


/***************************************************************************
 *
 * FUNCTION:    tz_get_single_threshold
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_get_single_threshold (
	TZ_CONTEXT              *thermal_zone,
	TZ_THRESHOLD            *threshold)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("tz_get_single_threshold");

	if (!thermal_zone || !threshold) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	switch (threshold->type) {

	/*
	 * Critical Threshold:
	 * -------------------
	 */
	case TZ_THRESHOLD_CRITICAL:
		threshold->index = 0;
		threshold->cooling_devices.count = 0;
		status = bm_evaluate_simple_integer(
			thermal_zone->acpi_handle, "_CRT",
			&(threshold->temperature));
		break;

	/*
	 * Passive Threshold:
	 * ------------------
	 * Evaluate _PSV to get the threshold temperature and _PSL to get
	 * references to all passive cooling devices.
	 */
	case TZ_THRESHOLD_PASSIVE:
		threshold->index = 0;
		threshold->cooling_devices.count = 0;
		status = bm_evaluate_simple_integer(
			thermal_zone->acpi_handle, "_PSV",
			&(threshold->temperature));
		if (ACPI_SUCCESS(status)) {
			status = bm_evaluate_reference_list(
				thermal_zone->acpi_handle, "_PSL",
				&(threshold->cooling_devices));
		}

		break;

	/*
	 * Active Thresholds:
	 * ------------------
	 * Evaluate _ACx to get all threshold temperatures, and _ALx to get
	 * references to all passive cooling devices.
	 */
	case TZ_THRESHOLD_ACTIVE:
		 {
			char object_name[5] = {'_','A', 'C',
				('0'+threshold->index),'\0'};
			status = bm_evaluate_simple_integer(
				thermal_zone->acpi_handle, object_name,
				&(threshold->temperature));
			if (ACPI_SUCCESS(status)) {
				object_name[2] = 'L';
				status = bm_evaluate_reference_list(
					thermal_zone->acpi_handle,
					object_name,
					&(threshold->cooling_devices));
			}
		}
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_get_thresholds
 *
 * PARAMETERS:  thermal_zone          - Identifies the thermal zone to parse.
 *              buffer      - Output buffer.
 *
 * RETURN:      acpi_status result code.
 *
 * DESCRIPTION: Builds a TZ_THRESHOLD_LIST structure containing information
 *              on all thresholds for a given thermal zone.
 *
 * NOTES:       The current design limits the number of cooling devices
 *              per theshold to the value specified by BM_MAX_HANDLES.
 *              This simplifies parsing of thresholds by allowing a maximum
 *              threshold list size to be computed (and enforced) -- which
 *              allows all thresholds to be parsed in a single pass (since
 *              memory must be contiguous when returned in the acpi_buffer).
 *
 ****************************************************************************/

acpi_status
tz_get_thresholds (
	TZ_CONTEXT              *thermal_zone,
	TZ_THRESHOLD_LIST       *threshold_list)
{
	acpi_status             status = AE_OK;
	TZ_THRESHOLD            *threshold = NULL;
	u32                     i = 0;

	FUNCTION_TRACE("tz_get_thresholds");

	if (!thermal_zone || !threshold_list) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	threshold_list->count = 0;

	/*
	 * Critical threshold:
	 * -------------------
	 * Every thermal zone must have one!
	 */
	threshold = &(threshold_list->thresholds[threshold_list->count]);
	threshold->type = TZ_THRESHOLD_CRITICAL;

	status = tz_get_single_threshold(thermal_zone, threshold);
	if (ACPI_SUCCESS(status)) {
		(threshold_list->count)++;
	}
	else {
		return_ACPI_STATUS(status);
	}


	/*
	 * Passive threshold:
	 * ------------------
	 */
	threshold = &(threshold_list->thresholds[threshold_list->count]);
	threshold->type = TZ_THRESHOLD_PASSIVE;

	status = tz_get_single_threshold(thermal_zone, threshold);
	if (ACPI_SUCCESS(status)) {
		(threshold_list->count)++;
	}

	/*
	 * Active threshold:
	 * -----------------
	 * Note that active thresholds are sorted by index (e.g. _AC0,
	 * _AC1, ...), and thus from highest (_AC0) to lowest (_AC9)
	 * temperature.
	 */
	for (i = 0; i < TZ_MAX_ACTIVE_THRESHOLDS; i++) {

		threshold = &(threshold_list->thresholds[threshold_list->count]);
		threshold->type = TZ_THRESHOLD_ACTIVE;
		threshold->index = i;

		status = tz_get_single_threshold(thermal_zone, threshold);
		if (ACPI_SUCCESS(status)) {
			(threshold_list->count)++;
		}
		else {
			threshold->type = TZ_THRESHOLD_UNKNOWN;
			threshold->index = 0;
			thermal_zone->policy.active.threshold_count = i;
			break;
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    tz_add_device
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_add_device (
	BM_HANDLE               device_handle,
	void                    **context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT              *thermal_zone = NULL;
	BM_DEVICE		*device = NULL;
	acpi_handle             tmp_handle = NULL;
	static u32		zone_count = 0;

	FUNCTION_TRACE("tz_add_device");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Adding thermal zone [%02x].\n", device_handle));

	if (!context || *context) {
		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "Invalid context for device [%02x].\n", device_handle));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Get information on this device.
	 */
	status = bm_get_device_info(device_handle, &device);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Allocate a new Thermal Zone device.
	 */
	thermal_zone = acpi_os_callocate(sizeof(TZ_CONTEXT));
	if (!thermal_zone) {
		return AE_NO_MEMORY;
	}

	thermal_zone->device_handle = device->handle;
	thermal_zone->acpi_handle = device->acpi_handle;

	/* TBD: How to manage 'uid' when zones are Pn_p? */
	sprintf(thermal_zone->uid, "%d", zone_count++);

	/*
	 * _TMP?
	 * -----
	 */
	status = acpi_get_handle(thermal_zone->acpi_handle, "_TMP",
		&tmp_handle);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * Initialize Policy:
	 * ------------------
	 * TBD: Move all thermal zone policy to user-mode daemon...
	 */
	status = tz_policy_add_device(thermal_zone);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	status = tz_osl_add_device(thermal_zone);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	*context = thermal_zone;

	tz_print(thermal_zone);

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(thermal_zone);
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_remove_device
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_remove_device (
	void			**context)
{
	acpi_status		status = AE_OK;
	TZ_CONTEXT		*thermal_zone = NULL;

	FUNCTION_TRACE("tz_remove_device");

	if (!context || !*context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	thermal_zone = (TZ_CONTEXT*)(*context);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing thermal zone [%02x].\n", thermal_zone->device_handle));

	status = tz_osl_remove_device(thermal_zone);

	/*
	 * Remove Policy:
	 * --------------
	 * TBD: Move all thermal zone policy to user-mode daemon...
	 */
	status = tz_policy_remove_device(thermal_zone);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_os_free(thermal_zone);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    tz_initialize
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_initialize (void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("tz_initialize");

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Register driver for thermal zone devices.
	 */
	criteria.type = BM_TYPE_THERMAL_ZONE;

	driver.notify = &tz_notify;
	driver.request = &tz_request;

	status = bm_register_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_terminate
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_terminate (void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("tz_terminate");

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Unregister driver for thermal zone devices.
	 */
	criteria.type = BM_TYPE_THERMAL_ZONE;

	driver.notify = &tz_notify;
	driver.request = &tz_request;

	status = bm_unregister_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_notify
 *
 * PARAMETERS:  <none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/
acpi_status
tz_notify (
	BM_NOTIFY               notify_type,
	BM_HANDLE               device_handle,
	void                    **context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT		*thermal_zone = NULL;

	FUNCTION_TRACE("tz_notify");

	if (!context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	thermal_zone = (TZ_CONTEXT*)*context;

	switch (notify_type) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = tz_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = tz_remove_device(context);
		break;

	case TZ_NOTIFY_TEMPERATURE_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Temperature (_TMP) change event detected.\n"));
		/* -------------------------------------------- */
		/* TBD: Remove when policy moves to user-mode. */
		tz_policy_check(*context);
		/* -------------------------------------------- */
		status = tz_get_temperature(thermal_zone,
			&(thermal_zone->policy.temperature));
		if (ACPI_SUCCESS(status)) {
			status = tz_osl_generate_event(notify_type,
				thermal_zone);
		}
		break;

	case TZ_NOTIFY_THRESHOLD_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Threshold (_SCP) change event detected.\n"));
		/* -------------------------------------------- */
		/* TBD: Remove when policy moves to user-mode. */
		status = tz_policy_remove_device(thermal_zone);
		if (ACPI_SUCCESS(status)) {
			status = tz_policy_add_device(thermal_zone);
		}
		/* -------------------------------------------- */
		status = tz_osl_generate_event(notify_type, thermal_zone);
		break;

	case TZ_NOTIFY_DEVICE_LISTS_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Device lists (_ALx, _PSL, _TZD) change event detected.\n"));
		/* -------------------------------------------- */
		/* TBD: Remove when policy moves to user-mode. */
		status = tz_policy_remove_device(thermal_zone);
		if (ACPI_SUCCESS(status)) {
			status = tz_policy_add_device(thermal_zone);
		}
		/* -------------------------------------------- */
		status = tz_osl_generate_event(notify_type, thermal_zone);
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_request
 *
 * PARAMETERS:
 *
 * RETURN:      Exception code.
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
tz_request (
	BM_REQUEST		*request,
	void                    *context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT              *thermal_zone = NULL;

	FUNCTION_TRACE("tz_request");

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	thermal_zone = (TZ_CONTEXT*)context;

	/*
	 * Handle request:
	 * ---------------
	 */
	switch (request->command) {

	default:
		status = AE_SUPPORT;
		break;
	}

	request->status = status;

	return_ACPI_STATUS(status);
}
