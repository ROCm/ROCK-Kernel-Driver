/*****************************************************************************
 *
 * Module Name: bn.c
 *   $Revision: 27 $
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
 *  Foundation, Inc., 59 Temple Plxxe, Suite 330, Boston, MA  02111-1307  USA
 */


#include <acpi.h>
#include "bn.h"


#define _COMPONENT		ACPI_BUTTON
	MODULE_NAME		("bn")


/*****************************************************************************
 *                            Internal Functions
 *****************************************************************************/

/*****************************************************************************
 *
 * FUNCTION:	bn_print
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION: Prints out information on a specific button.
 *
 ****************************************************************************/

void
bn_print (
	BN_CONTEXT		*button)
{
#ifdef ACPI_DEBUG
	acpi_buffer		buffer;

	PROC_NAME("bn_print");

	if (!button) {
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
	acpi_get_name(button->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * Print out basic button information.
	 */
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	switch (button->type) {

	case BN_TYPE_POWER_BUTTON:
	case BN_TYPE_POWER_BUTTON_FIXED:
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Power_button[%02x]:[%p] %s\n", button->device_handle, button->acpi_handle, (char*)buffer.pointer));
		break;

	case BN_TYPE_SLEEP_BUTTON:
	case BN_TYPE_SLEEP_BUTTON_FIXED:
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Sleep_button[%02x]:[%p] %s\n", button->device_handle, button->acpi_handle, (char*)buffer.pointer));
		break;

	case BN_TYPE_LID_SWITCH:
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Lid_switch[%02x]:[%p] %s\n", button->device_handle, button->acpi_handle, (char*)buffer.pointer));
		break;
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	acpi_os_free(buffer.pointer);
#endif /*ACPI_DEBUG*/

	return;
}


/****************************************************************************
 *
 * FUNCTION:	bn_add_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bn_add_device(
	BM_HANDLE		device_handle,
	void			**context)
{
	acpi_status		status = AE_OK;
	BM_DEVICE		*device = NULL;
	BN_CONTEXT		*button = NULL;

	FUNCTION_TRACE("bn_add_device");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Adding button device [%02x].\n", device_handle));

	if (!context || *context) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid context.\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Get information on this device.
	 */
	status = bm_get_device_info( device_handle, &device );
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Allocate a new BN_CONTEXT structure.
	 */
	button = acpi_os_callocate(sizeof(BN_CONTEXT));
	if (!button) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	button->device_handle = device->handle;
	button->acpi_handle = device->acpi_handle;

	/*
	 * Power Button?
	 * -------------
	 * Either fixed-feature or generic (namespace) types.
	 */
	if (strncmp(device->id.hid, BN_HID_POWER_BUTTON,
		sizeof(BM_DEVICE_HID)) == 0) {

		if (device->id.type == BM_TYPE_FIXED_BUTTON) {

			button->type = BN_TYPE_POWER_BUTTON_FIXED;

			/* Register for fixed-feature events. */
			status = acpi_install_fixed_event_handler(
				ACPI_EVENT_POWER_BUTTON, bn_notify_fixed,
				(void*)button);
		}
		else {
			button->type = BN_TYPE_POWER_BUTTON;
		}

	}

	/*
	 * Sleep Button?
	 * -------------
	 * Either fixed-feature or generic (namespace) types.
	 */
	else if (strncmp( device->id.hid, BN_HID_SLEEP_BUTTON,
		sizeof(BM_DEVICE_HID)) == 0) {

		if (device->id.type == BM_TYPE_FIXED_BUTTON) {

			button->type = BN_TYPE_SLEEP_BUTTON_FIXED;

			/* Register for fixed-feature events. */
			status = acpi_install_fixed_event_handler(
				ACPI_EVENT_SLEEP_BUTTON, bn_notify_fixed,
				(void*)button);
		}
		else {
			button->type = BN_TYPE_SLEEP_BUTTON;
		}
	}

	/*
	 * LID Switch?
	 * -----------
	 */
	else if (strncmp( device->id.hid, BN_HID_LID_SWITCH,
		sizeof(BM_DEVICE_HID)) == 0) {
		button->type = BN_TYPE_LID_SWITCH;
	}

	status = bn_osl_add_device(button);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	*context = button;

	bn_print(button);

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(button);
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bn_remove_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bn_remove_device(
	void			**context)
{
	acpi_status		status = AE_OK;
	BN_CONTEXT		*button = NULL;

	FUNCTION_TRACE("bn_remove_device");

	if (!context || !*context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	button = (BN_CONTEXT*)*context;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing button device [%02x].\n", button->device_handle));

	/*
	 * Unregister for fixed-feature events.
	 */
	switch (button->type) {
	case BN_TYPE_POWER_BUTTON_FIXED:
		status = acpi_remove_fixed_event_handler(
			ACPI_EVENT_POWER_BUTTON, bn_notify_fixed);
		break;
	case BN_TYPE_SLEEP_BUTTON_FIXED:
		status = acpi_remove_fixed_event_handler(
			ACPI_EVENT_SLEEP_BUTTON, bn_notify_fixed);
		break;
	}

	bn_osl_remove_device(button);

	acpi_os_free(button);

	*context = NULL;

	return_ACPI_STATUS(status);
}


/*****************************************************************************
 *			      External Functions
 *****************************************************************************/

/*****************************************************************************
 *
 * FUNCTION:	bn_initialize
 *
 * PARAMETERS:	<none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *

 ****************************************************************************/

acpi_status
bn_initialize (void)
{
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("bn_initialize");

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	driver.notify = &bn_notify;
	driver.request = &bn_request;

	/*
	 * Register for power buttons.
	 */
	MEMCPY(criteria.hid, BN_HID_POWER_BUTTON, sizeof(BN_HID_POWER_BUTTON));
	bm_register_driver(&criteria, &driver);

	/*
	 * Register for sleep buttons.
	 */
	MEMCPY(criteria.hid, BN_HID_SLEEP_BUTTON, sizeof(BN_HID_SLEEP_BUTTON));
	bm_register_driver(&criteria, &driver);

	/*
	 * Register for LID switches.
	 */
	MEMCPY(criteria.hid, BN_HID_LID_SWITCH, sizeof(BN_HID_LID_SWITCH));
	bm_register_driver(&criteria, &driver);

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:	bn_terminate
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bn_terminate (void)
{
	acpi_status		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("bn_terminate");

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	driver.notify = &bn_notify;
	driver.request = &bn_request;

	/*
	 * Unregister for power buttons.
	 */
	MEMCPY(criteria.hid, BN_HID_POWER_BUTTON, sizeof(BN_HID_POWER_BUTTON));
	status = bm_unregister_driver(&criteria, &driver);

	/*
	 * Unregister for sleep buttons.
	 */
	MEMCPY(criteria.hid, BN_HID_SLEEP_BUTTON, sizeof(BN_HID_SLEEP_BUTTON));
	status = bm_unregister_driver(&criteria, &driver);

	/*
	 * Unregister for LID switches.
	 */
	MEMCPY(criteria.hid, BN_HID_LID_SWITCH, sizeof(BN_HID_LID_SWITCH));
	status = bm_unregister_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bn_notify_fixed
 *
 * PARAMETERS:	<none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bn_notify_fixed (
	void			*context)
{
	acpi_status		status = AE_OK;

	FUNCTION_TRACE("bn_notify_fixed");

	if (!context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Status change event detected.\n"));

	status = bn_osl_generate_event(BN_NOTIFY_STATUS_CHANGE,
		((BN_CONTEXT*)context));

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bn_notify
 *
 * PARAMETERS:	<none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bn_notify (
	BM_NOTIFY		notify_type,
	BM_HANDLE		device_handle,
	void			**context)
{
	acpi_status		status = AE_OK;

	FUNCTION_TRACE("bn_notify");

	if (!context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	switch (notify_type) {
	case BM_NOTIFY_DEVICE_ADDED:
		status = bn_add_device(device_handle, context);
		break;
		
	case BM_NOTIFY_DEVICE_REMOVED:
		status = bn_remove_device(context);
		break;
		
	case BN_NOTIFY_STATUS_CHANGE:
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Status change event detected.\n"));
		status = bn_osl_generate_event(BN_NOTIFY_STATUS_CHANGE,
			((BN_CONTEXT*)*context));
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:	bn_request
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bn_request (
	BM_REQUEST		*request,
	void			*context)
{
	acpi_status		status = AE_OK;

	FUNCTION_TRACE("bn_request");

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Handle Request:
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
