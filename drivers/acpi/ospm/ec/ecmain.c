/*****************************************************************************
 *
 * Module Name: ecmain.c
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
#ifdef ACPI_DEBUG
	acpi_buffer             buffer;
#endif /*ACPI_DEBUG*/

	PROC_NAME("ec_print");

	if (!ec) {
		return;
	}

	acpi_os_printf("EC: found, GPE %d\n", ec->gpe_bit);

#ifdef ACPI_DEBUG
	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return;
	}

	/*
	 * Get the full pathname for this ACPI object.
	 */
	acpi_get_name(ec->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	/*
	 * Print out basic thermal zone information.
	 */
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| Embedded_controller[%02x]:[%p] %s\n", ec->device_handle, ec->acpi_handle, (char*)buffer.pointer));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   gpe_bit[%02x] status/command_port[%02x] data_port[%02x]\n", ec->gpe_bit, ec->status_port, ec->data_port));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	acpi_os_free(buffer.pointer);
#endif /*ACPI_DEBUG*/

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

acpi_status
ec_get_port_values(
	EC_CONTEXT              *ec)
{
	acpi_status             status = AE_OK;
	acpi_buffer             buffer;
	acpi_resource           *resource = NULL;

	FUNCTION_TRACE("ec_get_port_values");

	if (!ec) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	buffer.length = 0;
	buffer.pointer = NULL;

	status = acpi_get_current_resources(ec->acpi_handle, &buffer);
	if (status != AE_BUFFER_OVERFLOW) {
		return_ACPI_STATUS(status);
	}

	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	status = acpi_get_current_resources(ec->acpi_handle, &buffer);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	resource = (acpi_resource *) buffer.pointer;
	ec->data_port = resource->data.io.min_base_address;

	resource = NEXT_RESOURCE(resource);

	ec->status_port = ec->command_port =
		resource->data.io.min_base_address;
end:
	acpi_os_free(buffer.pointer);

	return_ACPI_STATUS(status);
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

acpi_status
ec_add_device(
	BM_HANDLE               device_handle,
	void                    **context)
{
	acpi_status             status = AE_OK;
	BM_DEVICE		*device = NULL;
	EC_CONTEXT              *ec = NULL;
	u8                      gpe_handler = FALSE;
	u8                      space_handler = FALSE;

	FUNCTION_TRACE("ec_add_device");

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Adding EC device [%02x].\n", device_handle));

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
	 * Allocate a new EC_CONTEXT structure.
	 */
	ec = acpi_os_callocate(sizeof(EC_CONTEXT));
	if (!ec) {
		return_ACPI_STATUS(AE_NO_MEMORY);
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
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "EC _GLK failed\n"));
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

	return_ACPI_STATUS(status);
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

acpi_status
ec_remove_device(
	void                    **context)
{
	acpi_status             status = AE_OK;
	EC_CONTEXT              *ec = NULL;

	FUNCTION_TRACE("ec_remove_device");

	if (!context || !*context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	ec = (EC_CONTEXT*)*context;

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing EC device [%02x].\n", ec->device_handle));

	ec_remove_space_handler(ec);

	ec_remove_gpe_handler(ec);

	if (ec->mutex) {
		acpi_os_delete_semaphore(ec->mutex);
	}

	acpi_os_free(ec);

	*context = NULL;

	return_ACPI_STATUS(status);
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

acpi_status
ec_initialize (void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("ec_initialize");

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Register driver for AC Adapter devices.
	 */
	MEMCPY(criteria.hid, EC_HID_EC, sizeof(EC_HID_EC));

	driver.notify = &ec_notify;
	driver.request = &ec_request;

	status = bm_register_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
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

acpi_status
ec_terminate(void)
{
	acpi_status             status = AE_OK;
	BM_DEVICE_ID		criteria;
	BM_DRIVER		driver;

	FUNCTION_TRACE("ec_terminate");

	MEMSET(&criteria, 0, sizeof(BM_DEVICE_ID));
	MEMSET(&driver, 0, sizeof(BM_DRIVER));

	/*
	 * Unregister driver for AC Adapter devices.
	 */
	MEMCPY(criteria.hid, EC_HID_EC, sizeof(EC_HID_EC));

	driver.notify = &ec_notify;
	driver.request = &ec_request;

	status = bm_unregister_driver(&criteria, &driver);

	return_ACPI_STATUS(status);
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

acpi_status
ec_notify (
	BM_NOTIFY               notify,
	BM_HANDLE               device_handle,
	void                    **context)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("ec_notify");

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

	return_ACPI_STATUS(status);
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

acpi_status
ec_request (
	BM_REQUEST              *request,
	void                    *context)
{
	acpi_status             status = AE_OK;
	EC_REQUEST              *ec_request = NULL;
	EC_CONTEXT              *ec = NULL;

	FUNCTION_TRACE("ec_request");

	/*
	 * Must have a valid request structure and context.
	 */
	if (!request || !context)
		return_ACPI_STATUS(AE_BAD_PARAMETER);

	/*
	 * buffer must contain a valid EC_REQUEST structure.
	 */
	status = bm_cast_buffer(&(request->buffer), (void**)&ec_request,
		sizeof(EC_REQUEST));
	if (ACPI_FAILURE(status))
		return_ACPI_STATUS(status);

	/*
	 * context contains information specific to this EC.
	 */
	ec = (EC_CONTEXT*)context;

	/*
	 * Perform the Transaction.
	 */
	status = ec_transaction(ec, ec_request);

	return_ACPI_STATUS(status);
}
