/*****************************************************************************
 *
 * Module Name: sm.c
 *   $Revision: 16 $
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
#include "sm.h"


#define _COMPONENT		ACPI_SYSTEM
	MODULE_NAME 		("sm")


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:	sm_print
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION: Prints out information on a specific system.
 *
 ****************************************************************************/

void
sm_print (
	SM_CONTEXT		*system)
{

	return;
}


/****************************************************************************
 *
 * FUNCTION:	sm_add_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
sm_add_device(
	BM_HANDLE		device_handle,
	void			**context)
{
	ACPI_STATUS 		status = AE_OK;
	BM_DEVICE		*device = NULL;
	SM_CONTEXT		*system = NULL;
	u8			i, type_a, type_b;


	if (!context || *context) {
		return(AE_BAD_PARAMETER);
	}

	/*
	 * Allocate a new SM_CONTEXT structure.
	 */
	system = acpi_os_callocate(sizeof(SM_CONTEXT));
	if (!system) {
		return(AE_NO_MEMORY);
	}

	/*
	 * Get information on this device.
	 */
	status = bm_get_device_info(device_handle, &device);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	system->device_handle = device->handle;
	system->acpi_handle = device->acpi_handle;

	/*
	 * Sx States:
	 * ----------
	 * Figure out which Sx states are supported.
	 */
	for (i=0; i<SM_MAX_SYSTEM_STATES; i++) {
		if (ACPI_SUCCESS(acpi_hw_obtain_sleep_type_register_data(
			i,
			&type_a,
			&type_b))) {
			system->states[i] = TRUE;
		}
	}

	status = sm_osl_add_device(system);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	*context = system;

	sm_print(system);

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(system);
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	sm_remove_device
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
sm_remove_device (
	void			**context)
{
	ACPI_STATUS 		status = AE_OK;
	SM_CONTEXT		*system = NULL;

	if (!context || !*context) {
		return(AE_BAD_PARAMETER);
	}

	system = (SM_CONTEXT*)*context;

	status = sm_osl_remove_device(system);

	acpi_os_free(system);

	*context = NULL;

	return(status);
}


/****************************************************************************
 *                             External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:	sm_initialize
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
sm_initialize (void)
{
	ACPI_STATUS		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Register driver for the System device.
	 */
	criteria.type = BM_TYPE_SYSTEM;

	driver.notify = &sm_notify;
	driver.request = &sm_request;

	status = bm_register_driver(&criteria, &driver);

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	sm_terminate
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
sm_terminate (void)
{
	ACPI_STATUS 		status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Unregister driver for System devices.
	 */
	criteria.type = BM_TYPE_SYSTEM;

	driver.notify = &sm_notify;
	driver.request = &sm_request;

	status = bm_unregister_driver(&criteria, &driver);

	return(status);
}


/*****************************************************************************
 *
 * FUNCTION:	sm_notify
 *
 * PARAMETERS:	<none>
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/
ACPI_STATUS
sm_notify (
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
		status = sm_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = sm_remove_device(context);
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:	sm_request
 *
 * PARAMETERS:	
 *
 * RETURN:	
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
sm_request (
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
