/*****************************************************************************
 *
 * Module Name: bt.c
 *   $Revision: 29 $
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
#include "bt.h"


#define _COMPONENT		ACPI_BATTERY
	MODULE_NAME 		("bt")


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:	bt_print
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION: Prints out information on a specific battery.
 *
 ****************************************************************************/

void
bt_print (
	BT_CONTEXT		*battery)
{
#ifdef ACPI_DEBUG
	acpi_buffer 		buffer;

	PROC_NAME("bt_print");

	if (!battery) {
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
	acpi_get_name(battery->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * Print out basic battery information.
	 */

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Battery[%02x]:[%p] %s\n", battery->device_handle, battery->acpi_handle, (char*)buffer.pointer));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   uid[%s] is_present[%d] power_units[%s]\n", battery->uid, battery->is_present, battery->power_units));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	acpi_os_free(buffer.pointer);
#endif /*ACPI_DEBUG*/

	return;
}


/****************************************************************************
 *
 * FUNCTION:	bt_get_info
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 * NOTES:	Allocates battery_info - which must be freed by the caller.
 *
 ****************************************************************************/

acpi_status
bt_get_info (
	BT_CONTEXT		*battery,
	BT_BATTERY_INFO 	**battery_info)
{
	acpi_status 		status = AE_OK;
	acpi_buffer 		bif_buffer, package_format, package_data;
	acpi_object 		*package = NULL;

	FUNCTION_TRACE("bt_get_info");

	if (!battery || !battery_info || *battery_info) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	memset(&bif_buffer, 0, sizeof(acpi_buffer));

	/*
	 * Evalute _BIF:
	 * -------------
	 * And be sure to deallocate bif_buffer.pointer!
	 */
	status = bm_evaluate_object(battery->acpi_handle, "_BIF", NULL,
		&bif_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Extract Package Data:
	 * ---------------------
	 * Type-cast this bif_buffer to a package and use helper
	 * functions to convert results into BT_BATTERY_INFO structure.
	 * The first attempt is just to get the size of the package
	 * data; the second gets the data (once we know the required
	 * bif_buffer size).
	 */
	status = bm_cast_buffer(&bif_buffer, (void**)&package,
		sizeof(acpi_object));
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	package_format.length = sizeof("NNNNNNNNNSSSS");
	package_format.pointer = "NNNNNNNNNSSSS";

	memset(&package_data, 0, sizeof(acpi_buffer));

	status = bm_extract_package_data(package, &package_format,
		&package_data);
	if (status != AE_BUFFER_OVERFLOW) {
		if (status == AE_OK) {
			status = AE_ERROR;
		}
		goto end;
	}

	package_data.pointer = acpi_os_callocate(package_data.length);
	if (!package_data.pointer) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	status = bm_extract_package_data(package, &package_format,
		&package_data);
	if (ACPI_FAILURE(status)) {
		acpi_os_free(package_data.pointer);
		goto end;
	}

	*battery_info = package_data.pointer;

end:
	acpi_os_free(bif_buffer.pointer);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bt_get_status
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_get_status (
	BT_CONTEXT		*battery,
	BT_BATTERY_STATUS	**battery_status)
{
	acpi_status 		status = AE_OK;
	acpi_buffer 		bst_buffer, package_format, package_data;
	acpi_object 		*package = NULL;

	FUNCTION_TRACE("bt_get_status");

	if (!battery || !battery_status || *battery_status) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	memset(&bst_buffer, 0, sizeof(acpi_buffer));

	/*
	 * Evalute _BST:
	 * -------------
	 * And be sure to deallocate bst_buffer.pointer!
	 */
	status = bm_evaluate_object(battery->acpi_handle, "_BST",
		NULL, &bst_buffer);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Extract Package Data:
	 * ---------------------
	 * Type-cast this bst_buffer to a package and use helper
	 * functions to convert results into BT_BATTERY_STATUS structure.
	 * The first attempt is just to get the size of the package data;
	 * the second gets the data (once we know the required bst_buffer
	 * size).
	 */
	status = bm_cast_buffer(&bst_buffer, (void**)&package,
		sizeof(acpi_object));
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	package_format.length = sizeof("NNNN");
	package_format.pointer = "NNNN";

	memset(&package_data, 0, sizeof(acpi_buffer));

	status = bm_extract_package_data(package, &package_format,
		&package_data);
	if (status != AE_BUFFER_OVERFLOW) {
		if (status == AE_OK) {
			status = AE_ERROR;
		}
		goto end;
	}

	package_data.pointer = acpi_os_callocate(package_data.length);
	if (!package_data.pointer) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	status = bm_extract_package_data(package, &package_format,
		&package_data);
	if (ACPI_FAILURE(status)) {
		acpi_os_free(package_data.pointer);
		goto end;
	}

	*battery_status = package_data.pointer;

end:
	acpi_os_free(bst_buffer.pointer);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bt_check_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_check_device (
	BT_CONTEXT		*battery)
{
	acpi_status 		status = AE_OK;
	BM_DEVICE_STATUS	battery_status = BM_STATUS_UNKNOWN;
	u32 			was_present = FALSE;
	BT_BATTERY_INFO 	*battery_info = NULL;

	FUNCTION_TRACE("bt_check_device");

	if (!battery) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	was_present = battery->is_present;

	/*
	 * Battery Present?
	 * ----------------
	 * Get the device status and check if battery slot is occupied.
	 */
	status = bm_get_device_status(battery->device_handle, &battery_status);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Unable to get battery status.\n"));
		return_ACPI_STATUS(status);
	}

	if (battery_status & BM_STATUS_BATTERY_PRESENT) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Battery socket occupied.\n"));
		battery->is_present = TRUE;
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Battery socket not occupied.\n"));
		battery->is_present = FALSE;
	}

	/*
	 * Battery Appeared?
	 * -----------------
	 */
	if (!was_present && battery->is_present) {

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Battery insertion detected.\n"));

		/*
		 * Units of Power?
		 * ---------------
		 * Get the 'units of power', as we'll need this to report
		 * status information.
		 */
		status = bt_get_info(battery, &battery_info);
		if (ACPI_SUCCESS(status)) {
			battery->power_units = (battery_info->power_unit)
				? BT_POWER_UNITS_AMPS : BT_POWER_UNITS_WATTS;
			acpi_os_free(battery_info);
		}
	}

	/*
	 * Battery Disappeared?
	 * --------------------
	 */
	else if (was_present && !battery->is_present) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Battery removal detected.\n"));
		battery->power_units = BT_POWER_UNITS_DEFAULT;
	}

	return_ACPI_STATUS(status);
}


/*****************************************************************************
 *
 * FUNCTION:	bt_add_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_add_device (
	BM_HANDLE		device_handle,
	void			**context)
{
	acpi_status 		status = AE_OK;
	BM_DEVICE		*device = NULL;
	BT_CONTEXT		*battery = NULL;

	FUNCTION_TRACE("bt_add_device");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Adding battery device [%02x].\n", device_handle));

	if (!context || *context) {
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
	 * Allocate a new BT_CONTEXT structure.
	 */
	battery = acpi_os_callocate(sizeof(BT_CONTEXT));
	if (!battery) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	battery->device_handle = device->handle;
	battery->acpi_handle = device->acpi_handle;
	strncpy(battery->uid, device->id.uid, sizeof(battery->uid));

	battery->power_units = BT_POWER_UNITS_DEFAULT;
	battery->is_present = FALSE;

	/*
	 * See if battery is really present.
	 */
	status = bt_check_device(battery);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	status = bt_osl_add_device(battery);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	*context = battery;

	bt_print(battery);

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(battery);
	}

	return_ACPI_STATUS(status);
}


/*****************************************************************************
 *
 * FUNCTION:	bt_remove_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_remove_device (
	void			**context)
{
	acpi_status 		status = AE_OK;
	BT_CONTEXT		*battery = NULL;

	FUNCTION_TRACE("bt_remove_device");

	if (!context || !*context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	battery = (BT_CONTEXT*)*context;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing battery device [%02x].\n", battery->device_handle));

	bt_osl_remove_device(battery);

	acpi_os_free(battery);

	*context = NULL;

	return_ACPI_STATUS(status);
}


/*****************************************************************************
 *                               External Functions
 *****************************************************************************/

/*****************************************************************************
 *
 * FUNCTION:	bt_initialize
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_initialize (void)
{
	acpi_status		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("bt_initialize");

	memset(&criteria, 0, sizeof(BM_DEVICE_ID));
	memset(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Register driver for driver method battery devices.
	 */
	MEMCPY(criteria.hid, BT_HID_CM_BATTERY, sizeof(BT_HID_CM_BATTERY));

	driver.notify = &bt_notify;
	driver.request = &bt_request;

	status = bm_register_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bt_terminate
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_terminate (void)
{
	acpi_status		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("bt_terminate");

	memset(&criteria, 0, sizeof(BM_DEVICE_ID));
	memset(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Unregister driver for driver method battery devices.
	 */
	MEMCPY(criteria.hid, BT_HID_CM_BATTERY, sizeof(BT_HID_CM_BATTERY));

	driver.notify = &bt_notify;
	driver.request = &bt_request;

	status = bm_unregister_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bt_notify
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_notify (
	BM_NOTIFY		notify_type,
	BM_HANDLE		device_handle,
	void			**context)
{
	acpi_status 		status = AE_OK;

	FUNCTION_TRACE("bt_notify");

	if (!context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	switch (notify_type) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = bt_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = bt_remove_device(context);
		break;

	case BT_NOTIFY_STATUS_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Status change (_BST) event detected.\n"));
		status = bt_osl_generate_event(notify_type,
			((BT_CONTEXT*)*context));
		break;

	case BT_NOTIFY_INFORMATION_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Information change (_BIF) event detected.\n"));
		status = bt_check_device((BT_CONTEXT*)*context);
		if (ACPI_SUCCESS(status)) {
			status = bt_osl_generate_event(notify_type,
				((BT_CONTEXT*)*context));
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
 * FUNCTION:	bt_request
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bt_request (
	BM_REQUEST		*request,
	void			*context)
{
	acpi_status 		status = AE_OK;

	FUNCTION_TRACE("bt_request");

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

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
