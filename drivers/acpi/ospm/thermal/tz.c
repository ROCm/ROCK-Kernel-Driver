/*****************************************************************************
 *
 * Module Name: tz.c
 *   $Revision: 44 $
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


#include <acpi.h>
#include <bm.h>
#include "tz.h"


#define _COMPONENT		ACPI_THERMAL
	MODULE_NAME		("tz")


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

extern int TZP;


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    tz_print
 *
 ****************************************************************************/

void
tz_print (
	TZ_CONTEXT		*tz)
{
#ifdef ACPI_DEBUG
	acpi_buffer		buffer;
	u32			i,j = 0;
	TZ_THRESHOLDS		*thresholds = NULL;

	FUNCTION_TRACE("tz_print");

	if (!tz)
		return;

	thresholds = &(tz->policy.thresholds);

	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer)
		return;

	/*
	 * Get the full pathname for this ACPI object.
	 */
	acpi_get_name(tz->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * Print out basic thermal zone information.
	 */
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Thermal_zone[%02x]:[%p] %s\n", tz->device_handle, tz->acpi_handle, (char*)buffer.pointer));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   temperature[%d] state[%08x]\n", tz->policy.temperature, tz->policy.state));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   cooling_mode[%08x] polling_freq[%d]\n", tz->policy.cooling_mode, tz->policy.polling_freq));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   critical[%d]\n", thresholds->critical.temperature));
	if (thresholds->hot.is_valid)
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   hot[%d]\n", thresholds->hot.temperature));
	if (thresholds->passive.is_valid) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   passive[%d]: tc1[%d] tc2[%d] tsp[%d]\n", thresholds->passive.temperature, thresholds->passive.tc1, thresholds->passive.tc2, thresholds->passive.tsp));
		if (thresholds->passive.devices.count > 0) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|     devices"));
			for (j=0; (j<thresholds->passive.devices.count && j<10); j++) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "[%02x]", thresholds->passive.devices.handles[j]));
			}
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "\n"));
		}
	}
	for (i=0; i<TZ_MAX_ACTIVE_THRESHOLDS; i++) {
		if (!thresholds->active[i].is_valid)
			break;
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   active[%d]: index[%d]\n", thresholds->active[i].temperature, i));
		if (thresholds->active[i].devices.count > 0) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|     devices"));
			for (j=0; (j<thresholds->active[i].devices.count && j<10); j++) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "[%02x]", thresholds->active[i].devices.handles[j]));
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
 ****************************************************************************/

acpi_status
tz_get_temperature (
	TZ_CONTEXT		*tz)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("tz_get_temperature");

	if (!tz) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Evaluate the _TMP method to get the current temperature.
	 */
	status = bm_evaluate_simple_integer(tz->acpi_handle, "_TMP", &(tz->policy.temperature));
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Temperature is %d d_k\n", tz->policy.temperature));

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_set_cooling_preference
 *
 ****************************************************************************/

acpi_status
tz_set_cooling_preference (
	TZ_CONTEXT              *tz,
	TZ_COOLING_MODE         cooling_mode)
{
	acpi_status             status = AE_OK;
	acpi_object_list        arg_list;
	acpi_object             arg0;

	FUNCTION_TRACE("tz_set_cooling_preference");

	if (!tz || ((cooling_mode != TZ_COOLING_MODE_ACTIVE) && (cooling_mode != TZ_COOLING_MODE_PASSIVE))) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Build the argument list, which simply consists of the current
	 * cooling preference.
	 */
	memset(&arg_list, 0, sizeof(acpi_object));
	arg_list.count = 1;
	arg_list.pointer = &arg0;

	memset(&arg0, 0, sizeof(acpi_object));
	arg0.type = ACPI_TYPE_INTEGER;
	arg0.integer.value = cooling_mode;

	/*
	 * Evaluate "_SCP" - setting the new cooling preference.
	 */
	status = acpi_evaluate_object(tz->acpi_handle, "_SCP", &arg_list, NULL);
	if (ACPI_FAILURE(status)) {
		tz->policy.cooling_mode = -1;
		return_ACPI_STATUS(status);
	}

	tz->policy.cooling_mode = cooling_mode;

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_get_thresholds
 *
 ****************************************************************************/

acpi_status
tz_get_thresholds (
	TZ_CONTEXT		*tz)
{
	acpi_status		status = AE_OK;
	TZ_THRESHOLDS		*thresholds = NULL;
	u32			value = 0;
	u32                     i = 0;

	FUNCTION_TRACE("acpi_tz_get_thresholds");

	if (!tz) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	thresholds = &(tz->policy.thresholds);

	/* Critical Shutdown (required) */

	status = bm_evaluate_simple_integer(tz->acpi_handle, "_CRT", &value);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "No critical threshold\n"));
		return_ACPI_STATUS(status);
	}
	else {
		thresholds->critical.temperature = value;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found critical threshold [%d]\n", thresholds->critical.temperature));

	}

	/* Critical Sleep (optional) */

	status = bm_evaluate_simple_integer(tz->acpi_handle, "_HOT", &value);
	if (ACPI_FAILURE(status)) {
		thresholds->hot.is_valid = 0;
		thresholds->hot.temperature = 0;
	}
	else {
		thresholds->hot.is_valid = 1;
		thresholds->hot.temperature = value;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found hot threshold [%d]\n", thresholds->hot.temperature));
	}

	/* Passive: Processors (optional) */

	status = bm_evaluate_simple_integer(tz->acpi_handle, "_PSV", &value);
	if (ACPI_FAILURE(status)) {
		thresholds->passive.is_valid = 0;
		thresholds->passive.temperature = 0;
	}
	else {
		thresholds->passive.is_valid = 1;
		thresholds->passive.temperature = value;

		status = bm_evaluate_simple_integer(tz->acpi_handle, "_TC1", &value);
		if (ACPI_FAILURE(status)) {
			thresholds->passive.is_valid = 0;
		}
		thresholds->passive.tc1 = value;

		status = bm_evaluate_simple_integer(tz->acpi_handle, "_TC2", &value);
		if (ACPI_FAILURE(status)) {
			thresholds->passive.is_valid = 0;
		}
		thresholds->passive.tc2 = value;

		status = bm_evaluate_simple_integer(tz->acpi_handle, "_TSP", &value);
		if (ACPI_FAILURE(status)) {
			thresholds->passive.is_valid = 0;
		}
		thresholds->passive.tsp = value;

		status = bm_evaluate_reference_list(tz->acpi_handle, "_PSL", &(thresholds->passive.devices));
		if (ACPI_FAILURE(status)) {
			thresholds->passive.is_valid = 0;
		}

		if (thresholds->passive.is_valid) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found passive threshold [%d]\n", thresholds->passive.temperature));
		}
		else {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid passive threshold\n"));
		}
	}

	/* Active: Fans, etc. (optional) */

	for (i=0; i<TZ_MAX_ACTIVE_THRESHOLDS; i++) {

		char name[5] = {'_','A','C',('0'+i),'\0'};

		status = bm_evaluate_simple_integer(tz->acpi_handle, name, &value);
		if (ACPI_FAILURE(status)) {
			thresholds->active[i].is_valid = 0;
			thresholds->active[i].temperature = 0;
			break;
		}

		thresholds->active[i].temperature = value;
		name[2] = 'L';

		status = bm_evaluate_reference_list(tz->acpi_handle, name, &(thresholds->active[i].devices));
		if (ACPI_SUCCESS(status)) {
			thresholds->active[i].is_valid = 1;
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found active threshold [%d]:[%d]\n", i, thresholds->active[i].temperature));
		}
		else {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Invalid active threshold [%d]\n", i));
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    tz_add_device
 *
 ****************************************************************************/

acpi_status
tz_add_device (
	BM_HANDLE               device_handle,
	void                    **context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT              *tz = NULL;
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
	tz = acpi_os_callocate(sizeof(TZ_CONTEXT));
	if (!tz) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	tz->device_handle = device->handle;
	tz->acpi_handle = device->acpi_handle;

	/* TBD: How to manage 'uid' when zones are Pn_p? */
	sprintf(tz->uid, "%d", zone_count++);

	/*
	 * Temperature:
	 * ------------
	 * Make sure we can read the zone's current temperature (_TMP).
	 * If we can't, there's no use in doing any policy (abort).
	 */
	status = tz_get_temperature(tz);
	if (ACPI_FAILURE(status))
		goto end;

	/*
	 * Polling Frequency:
	 * ------------------
	 * If _TZP doesn't exist use the OS default polling frequency.
	 */
	status = bm_evaluate_simple_integer(tz->acpi_handle, "_TZP", &(tz->policy.polling_freq));
	if (ACPI_FAILURE(status)) {
		tz->policy.polling_freq = TZP;
	}
	status = AE_OK;

	/*
	 * Cooling Preference:
	 * -------------------
	 * Default to ACTIVE (noisy) cooling until policy decides otherwise.
	 * Note that _SCP is optional.
	 */
	tz_set_cooling_preference(tz, TZ_COOLING_MODE_ACTIVE);

	/*
	 * Start Policy:
	 * -------------
	 * Thermal policy is included in the kernel (this driver) because
	 * of the critical role it plays in avoiding nuclear meltdown. =O
	 */
	status = tz_policy_add_device(tz);
	if (ACPI_FAILURE(status))
		goto end;

	status = tz_osl_add_device(tz);
	if (ACPI_FAILURE(status))
		goto end;

	*context = tz;

	tz_print(tz);

end:
	if (ACPI_FAILURE(status))
		acpi_os_free(tz);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    tz_remove_device
 *
 ****************************************************************************/

acpi_status
tz_remove_device (
	void			**context)
{
	acpi_status		status = AE_OK;
	TZ_CONTEXT		*tz = NULL;

	FUNCTION_TRACE("tz_remove_device");

	if (!context || !*context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	tz = (TZ_CONTEXT*)(*context);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing thermal zone [%02x].\n", tz->device_handle));

	status = tz_osl_remove_device(tz);

	/*
	 * Remove Policy:
	 * --------------
	 * TBD: Move all thermal zone policy to user-mode daemon...
	 */
	status = tz_policy_remove_device(tz);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_os_free(tz);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    tz_initialize
 *
 ****************************************************************************/

acpi_status
tz_initialize (void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("tz_initialize");

	memset(&criteria, 0, sizeof(BM_DEVICE_ID));
	memset(&driver, 0, sizeof(BM_DRIVER));

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
 ****************************************************************************/

acpi_status
tz_terminate (void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("tz_terminate");

	memset(&criteria, 0, sizeof(BM_DEVICE_ID));
	memset(&driver, 0, sizeof(BM_DRIVER));

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
 ****************************************************************************/

acpi_status
tz_notify (
	BM_NOTIFY               notify_type,
	BM_HANDLE               device_handle,
	void                    **context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT		*tz = NULL;

	FUNCTION_TRACE("tz_notify");

	if (!context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	tz = (TZ_CONTEXT*)*context;

	switch (notify_type) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = tz_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = tz_remove_device(context);
		break;

	case TZ_NOTIFY_TEMPERATURE_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Temperature (_TMP) change event detected.\n"));
		tz_policy_check(*context);
		status = tz_get_temperature(tz);
		if (ACPI_SUCCESS(status)) {
			status = tz_osl_generate_event(notify_type, tz);
		}
		break;

	case TZ_NOTIFY_THRESHOLD_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Threshold (_SCP) change event detected.\n"));
		status = tz_policy_remove_device(tz);
		if (ACPI_SUCCESS(status)) {
			status = tz_policy_add_device(tz);
		}
		status = tz_osl_generate_event(notify_type, tz);
		break;

	case TZ_NOTIFY_DEVICE_LISTS_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Device lists (_ALx, _PSL, _TZD) change event detected.\n"));
		status = tz_policy_remove_device(tz);
		if (ACPI_SUCCESS(status)) {
			status = tz_policy_add_device(tz);
		}
		status = tz_osl_generate_event(notify_type, tz);
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
 ****************************************************************************/

acpi_status
tz_request (
	BM_REQUEST		*request,
	void                    *context)
{
	acpi_status             status = AE_OK;
	TZ_CONTEXT              *tz = NULL;

	FUNCTION_TRACE("tz_request");

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	tz = (TZ_CONTEXT*)context;

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
