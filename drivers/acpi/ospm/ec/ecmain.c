/*****************************************************************************
 *
 * Module Name: ecmain.c
 *   $Revision: 26 $
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
#include "ec.h"

#define _COMPONENT		ACPI_EC
	MODULE_NAME		("ecmain")


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    ec_print
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Prints out information on a specific ec.
 *
 ****************************************************************************/

void
ec_print (
	EC_CONTEXT              *ec)
{

	if (!ec) {
		return;
	}

	acpi_os_printf("EC: found, GPE %d\n", ec->gpe_bit);


	return;
}


/****************************************************************************
 *
 * FUNCTION:    ec_get_port_values
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION: Evaluate _CRS to get the current resources (I/O port
 *              addresses) for this EC.
 *
 ****************************************************************************/

ACPI_STATUS
ec_get_port_values(
	EC_CONTEXT              *ec)
{
	ACPI_STATUS             status = AE_OK;
	ACPI_BUFFER             buffer;
	ACPI_RESOURCE           *resource = NULL;

	if (!ec) {
		return(AE_BAD_PARAMETER);
	}

	buffer.length = 0;
	buffer.pointer = NULL;

	status = acpi_get_current_resources(ec->acpi_handle, &buffer);
	if (status != AE_BUFFER_OVERFLOW) {
		return(status);
	}

	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return(AE_NO_MEMORY);
	}

	status = acpi_get_current_resources(ec->acpi_handle, &buffer);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	resource = (ACPI_RESOURCE *) buffer.pointer;
	ec->data_port = resource->data.io.min_base_address;

	resource = NEXT_RESOURCE(resource);

	ec->status_port = ec->command_port =
		resource->data.io.min_base_address;
end:
	acpi_os_free(buffer.pointer);

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_add_device
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_add_device(
	BM_HANDLE               device_handle,
	void                    **context)
{
	ACPI_STATUS             status = AE_OK;
	BM_DEVICE		*device = NULL;
	EC_CONTEXT              *ec = NULL;
	u8                      gpe_handler = FALSE;
	u8                      space_handler = FALSE;

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
	 * Allocate a new EC_CONTEXT structure.
	 */
	ec = acpi_os_callocate(sizeof(EC_CONTEXT));
	if (!ec) {
		return(AE_NO_MEMORY);
	}

	ec->device_handle = device->handle;
	ec->acpi_handle = device->acpi_handle;

	/*
	 * Get the I/O port addresses for the command/status and data ports.
	 */
	status = ec_get_port_values(ec);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * See if we need to obtain the global lock for EC transactions.
	 */
	status = bm_evaluate_simple_integer(ec->acpi_handle, "_GLK",
		&ec->use_global_lock);
	if (status == AE_NOT_FOUND) {
		ec->use_global_lock = 0;
	}
	else if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * Install a handler for servicing this EC's GPE.
	 */
	status = ec_install_gpe_handler(ec);
	if (ACPI_FAILURE(status)) {
		goto end;
	}
	else {
		gpe_handler = TRUE;
	}

	/*
	 * Install a handler for servicing this EC's address space.
	 */
	status = ec_install_space_handler(ec);
	if (ACPI_FAILURE(status)) {
		goto end;
	}
	else {
		space_handler = TRUE;
	}

	/*
	 * Create a semaphore to serialize EC transactions.
	 */
	status = acpi_os_create_semaphore(1,1, &(ec->mutex));
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * Context now contains information specific to this EC.  Note
	 * that we'll get this pointer back on every ec_request() and
	 * ec_notify().
	 */
	*context = ec;

	ec_print(ec);

end:
	if (ACPI_FAILURE(status)) {

		if (gpe_handler) {
			ec_remove_gpe_handler(ec);
		}

		if (space_handler) {
			ec_remove_space_handler(ec);
		}

		if (ec->mutex) {
			acpi_os_delete_semaphore(ec->mutex);
		}

		acpi_os_free(ec);
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_remove_device
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_remove_device(
	void                    **context)
{
	ACPI_STATUS             status = AE_OK;
	EC_CONTEXT              *ec = NULL;

	if (!context || !*context) {
		return(AE_BAD_PARAMETER);
	}

	ec = (EC_CONTEXT*)*context;

	ec_remove_space_handler(ec);

	ec_remove_gpe_handler(ec);

	if (ec->mutex) {
		acpi_os_delete_semaphore(ec->mutex);
	}

	acpi_os_free(ec);

	*context = NULL;

	return(status);
}


/****************************************************************************
 *                             External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    ec_initialize
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_initialize (void)
{
	ACPI_STATUS             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Register driver for AC Adapter devices.
	 */
	MEMCPY(criteria.hid, EC_HID_EC, sizeof(EC_HID_EC));

	driver.notify = &ec_notify;
	driver.request = &ec_request;

	status = bm_register_driver(&criteria, &driver);

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_terminate
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_terminate(void)
{
	ACPI_STATUS             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Unregister driver for AC Adapter devices.
	 */
	MEMCPY(criteria.hid, EC_HID_EC, sizeof(EC_HID_EC));

	driver.notify = &ec_notify;
	driver.request = &ec_request;

	status = bm_unregister_driver(&criteria, &driver);

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_notify
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_notify (
	BM_NOTIFY               notify,
	BM_HANDLE               device_handle,
	void                    **context)
{
	ACPI_STATUS             status = AE_OK;

	switch (notify) {

	case BM_NOTIFY_DEVICE_ADDED:
		status = ec_add_device(device_handle, context);
		break;

	case BM_NOTIFY_DEVICE_REMOVED:
		status = ec_remove_device(context);
		break;

	default:
		status = AE_SUPPORT;
		break;
	}

	return(status);
}


/****************************************************************************
 *
 * FUNCTION:    ec_request
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

ACPI_STATUS
ec_request (
	BM_REQUEST              *request,
	void                    *context)
{
	ACPI_STATUS             status = AE_OK;
	EC_REQUEST              *ec_request = NULL;
	EC_CONTEXT              *ec = NULL;

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context)
		return(AE_BAD_PARAMETER);

	/*
	 * buffer must contain a valid EC_REQUEST structure.
	 */
	status = bm_cast_buffer(&(request->buffer), (void**)&ec_request,
		sizeof(EC_REQUEST));
	if (ACPI_FAILURE(status))
		return(status);

	/*
	 * context contains information specific to this EC.
	 */
	ec = (EC_CONTEXT*)context;

	/*
	 * Perform the Transaction.
	 */
	status = ec_transaction(ec, ec_request);

	return(status);
}
