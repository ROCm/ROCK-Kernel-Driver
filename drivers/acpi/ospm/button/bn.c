/*****************************************************************************
 *
 * Module Name: bn.c
 *   $Revision: 22 $
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

ACPI_STATUS
bn_add_device(
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS		status = AE_OK;
	BM_DEVICE		*device = NULL;
	BN_CONTEXT		*button = NULL;

	if (!context || *context) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 * Get information on this device.
	 */
	status = bm_get_device_info( device_handle, &device );
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	/*
	 * Allocate a new BN_CONTEXT structure.
	 */
	button = acpi_os_callocate(sizeof(BN_CONTEXT));
	if (!button) {
		return(AE_NO_MEMORY);
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

	return(status);
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

ACPI_STATUS
bn_remove_device(
	void			**context)
{
	ACPI_STATUS		status = AE_OK;
	BN_CONTEXT		*button = NULL;

	if (!context || !*context) {
		return(AE_BAD_PARAMETER);
	}

	button = (BN_CONTEXT*)*context;

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

	return(status);
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

ACPI_STATUS
bn_initialize (void)
{
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

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

	return(AE_OK);
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

ACPI_STATUS
bn_terminate (void)
{
	ACPI_STATUS		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

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

	return(status);
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

ACPI_STATUS
bn_notify_fixed (
	void			*context)
{
	ACPI_STATUS		status = AE_OK;

	if (!context) {
		return(AE_BAD_PARAMETER);
	}

	status = bn_osl_generate_event(BN_NOTIFY_STATUS_CHANGE,
		((BN_CONTEXT*)context));

	return(status);
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

ACPI_STATUS
bn_notify (
	BM_NOTIFY		notify_type,
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS		status = AE_OK;

	if (!context) {
		return(AE_BAD_PARAMETER);
	}

	switch (notify_type) {
	case BM_NOTIFY_DEVICE_ADDED:
		status = bn_add_device(device_handle, context);
		break;
		
	case BM_NOTIFY_DEVICE_REMOVED:
		status = bn_remove_device(context);
		break;
		
	case BN_NOTIFY_STATUS_CHANGE:
		status = bn_osl_generate_event(BN_NOTIFY_STATUS_CHANGE,
			((BN_CONTEXT*)*context));
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return(status);
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

ACPI_STATUS
bn_request (
	BM_REQUEST		*request,
	void			*context)
{
	ACPI_STATUS		status = AE_OK;

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context) {
		return(AE_BAD_PARAMETER);
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

	return(status);
}
