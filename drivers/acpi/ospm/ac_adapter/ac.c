/*****************************************************************************
 *
 * Module Name: ac.c
 *   $Revision: 19 $
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
#include "ac.h"


#define _COMPONENT		ACPI_AC_ADAPTER
	MODULE_NAME 		("ac")


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:	ac_print
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION: Prints out information on a specific ac_adapter.
 *
 ****************************************************************************/

void
ac_print (
	AC_CONTEXT		*ac_adapter)
{

	return;
}


/****************************************************************************
 *
 * FUNCTION:	ac_add_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ac_add_device(
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS 		status = AE_OK;
	BM_DEVICE		*device = NULL;
	AC_CONTEXT		*ac_adapter = NULL;
	ACPI_DEVICE_INFO	info;

	if (!context || *context) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 * Get information on this device.
	 */
	status = bm_get_device_info(device_handle, &device);
	if (ACPI_FAILURE(status)) {
		return(status);
	}

	/*
	 * Allocate a new AC_CONTEXT structure.
	 */
	ac_adapter = acpi_os_callocate(sizeof(AC_CONTEXT));
	if (!ac_adapter) {
		return(AE_NO_MEMORY);
	}

	ac_adapter->device_handle = device->handle;
	ac_adapter->acpi_handle = device->acpi_handle;

	/*
	 * Get information on this object.
	 */
	status = acpi_get_object_info(ac_adapter->acpi_handle, &info);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * _UID?
	 * -----
	 */
	if (info.valid & ACPI_VALID_UID) {
		strncpy(ac_adapter->uid, info.unique_id, sizeof(info.unique_id));
	}
	else {
		strncpy(ac_adapter->uid, "0", sizeof("0"));
	}

	/*
	 * _STA?
	 * -----
	 */
	if (!(info.valid & ACPI_VALID_STA)) {
		status = AE_ERROR;
		goto end;
	}

	status = ac_osl_add_device(ac_adapter);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	*context = ac_adapter;

	ac_print(ac_adapter);

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(ac_adapter);
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	ac_remove_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ac_remove_device (
	void			**context)
{
	ACPI_STATUS 		status = AE_OK;
	AC_CONTEXT		*ac_adapter = NULL;

	if (!context || !*context) {
		return(AE_BAD_PARAMETER);
	}

	ac_adapter = (AC_CONTEXT*)*context;

	ac_osl_remove_device(ac_adapter);

	acpi_os_free(ac_adapter);

	*context = NULL;

	return(status);
}


/****************************************************************************
 *                             External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:	ac_initialize
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ac_initialize (void)
{
	ACPI_STATUS		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	driver.notify = &ac_notify;
	driver.request = &ac_request;

	/*
	 * Register driver for AC Adapter devices.
	 */
	MEMCPY(criteria.hid, AC_HID_AC_ADAPTER, sizeof(AC_HID_AC_ADAPTER));

	status = bm_register_driver(&criteria, &driver);

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	ac_terminate
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ac_terminate (void)
{
	ACPI_STATUS 		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Unregister driver for AC Adapter devices.
	 */
	MEMCPY(criteria.hid, AC_HID_AC_ADAPTER, sizeof(AC_HID_AC_ADAPTER));

	driver.notify = &ac_notify;
	driver.request = &ac_request;

	status = bm_unregister_driver(&criteria, &driver);

	return(status);
}


/*****************************************************************************
 *
 * FUNCTION:	ac_notify
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/
ACPI_STATUS
ac_notify (
	BM_NOTIFY		notify_type,
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS 		status = AE_OK;

	if (!context) {
		return(AE_BAD_PARAMETER);
	}

	switch (notify_type) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = ac_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = ac_remove_device(context);
		break;

	case AC_NOTIFY_STATUS_CHANGE:
		status = ac_osl_generate_event(notify_type,
			((AC_CONTEXT*)*context));
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	ac_request
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ac_request (
	BM_REQUEST		*request,
	void			*context)
{
	ACPI_STATUS 		status = AE_OK;

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
