/*****************************************************************************
 *
 * Module Name: bmdriver.c
 *   $Revision: 21 $
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
#include "bm.h"

#define _COMPONENT		ACPI_BUS
	MODULE_NAME		("bmdriver")


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    bm_get_device_power_state
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_get_device_power_state (
	BM_HANDLE               device_handle,
	BM_POWER_STATE		*state)
{
	acpi_status             status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_get_device_power_state");

	if (!state) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	*state = ACPI_STATE_UNKNOWN;

	/*
	 * Resolve device handle to node.
	 */
	status = bm_get_node(device_handle, 0, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Get the current power state.
	 */
	status = bm_get_power_state(node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*state = node->device.power.state;

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_set_device_power_state
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_set_device_power_state (
	BM_HANDLE               device_handle,
	BM_POWER_STATE		state)
{
	acpi_status           status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_set_device_power_state");

	/*
	 * Resolve device handle to node.
	 */
	status = bm_get_node(device_handle, 0, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Set the current power state.
	 */
	status = bm_set_power_state(node, state);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_device_status
 *
 * PARAMETERS:
 *    device_handle is really an index number into the array of BM_DEVICE
 *                  structures in info_list.  This data item is passed to
 *                  the registered program's "notify" callback.  It is used
 *                  to retrieve the specific BM_DEVICE structure instance
 *                  associated with the callback.
 *    device_status is a pointer that receives the result of processing
 *                  the device's associated ACPI _STA.
 *
 * RETURN:
 *    The acpi_status value indicates success AE_OK or failure of the function
 *
 * DESCRIPTION: Evaluates the device's ACPI _STA, if it is present.
 *
 ****************************************************************************/

acpi_status
bm_get_device_status (
	BM_HANDLE               device_handle,
	BM_DEVICE_STATUS        *device_status)
{
	acpi_status           status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_get_device_status");

	if (!device_status) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	*device_status = BM_STATUS_UNKNOWN;

	/*
	 * Resolve device handle to node.
	 */
	status = bm_get_node(device_handle, 0, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Parent Present?
	 * ---------------
	 * If the parent isn't present we can't evalute _STA on the child.
	 * Return an unknown status.
	 */
	if (!BM_NODE_PRESENT(node->parent)) {
		return_ACPI_STATUS(AE_OK);
	}
	
	/*
	 * Dynamic Status?
	 * ---------------
	 * If _STA isn't present we just return the default status.
	 */
	if (!(node->device.flags & BM_FLAGS_DYNAMIC_STATUS)) {
		*device_status = BM_STATUS_DEFAULT;
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Evaluate _STA:
	 * --------------
	 */
	status = bm_evaluate_simple_integer(node->device.acpi_handle, "_STA",
		&(node->device.status));
	if (ACPI_SUCCESS(status)) {
		*device_status = node->device.status;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_device_info
 *
 * PARAMETERS:
 *    device_handle An index used to retrieve the associated BM_DEVICE info.
 *    device        A pointer to a BM_DEVICE structure instance pointer.
 *                  This pointed to BM_DEVICE structure will contain the
 *                  this device's information.
 *
 * RETURN:
 *    The acpi_status value indicates success AE_OK or failure of the function
 *
 * DESCRIPTION:
 *    Using the device_handle this function retrieves this device's
 *    BM_DEVICE structure instance and save's it in device.
 *
 ****************************************************************************/

acpi_status
bm_get_device_info (
	BM_HANDLE               device_handle,
	BM_DEVICE               **device)
{
	acpi_status           status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_get_device_info");

	if (!device) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Resolve device handle to internal device.
	 */
	status = bm_get_node(device_handle, 0, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	*device = &(node->device);

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_device_context
 *
 *    device_handle An index used to retrieve the associated BM_DEVICE info.
 *    context       A pointer to a BM_DRIVER_CONTEXT structure instance.
 *
 * RETURN:
 *    The acpi_status value indicates success AE_OK or failure of the function
 *
 * DESCRIPTION:
 *    Using the device_handle this function retrieves this device's
 *    BM_DRIVER_CONTEXT structure instance and save's it in context.
 *
 ****************************************************************************/

acpi_status
bm_get_device_context (
	BM_HANDLE               device_handle,
	BM_DRIVER_CONTEXT	*context)
{
	acpi_status           status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_get_device_context");

	if (!context) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	*context = NULL;

	/*
	 * Resolve device handle to internal device.
	 */
	status = bm_get_node(device_handle, 0, &node);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	if (!node->driver.context) {
		return_ACPI_STATUS(AE_NULL_ENTRY);
	}

	*context = node->driver.context;

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_register_driver
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_register_driver (
	BM_DEVICE_ID		*criteria,
	BM_DRIVER		*driver)
{
	acpi_status           status = AE_NOT_FOUND;
	BM_HANDLE_LIST          device_list;
	BM_NODE			*node = NULL;
	BM_DEVICE		*device = NULL;
	u32                     i = 0;

	FUNCTION_TRACE("bm_register_driver");

	if (!criteria || !driver || !driver->notify || !driver->request) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	MEMSET(&device_list, 0, sizeof(BM_HANDLE_LIST));

	/*
	 * Find Matches:
	 * -------------
	 * Search through the entire device hierarchy for matches against
	 * the given device criteria.
	 */
	status = bm_search(BM_HANDLE_ROOT, criteria, &device_list);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Install driver:
	 * ----------------
	 * For each match, record the driver information and execute the
	 * driver's Notify() funciton (if present) to notify the driver
	 * of the device's presence.
	 */
	for (i = 0; i < device_list.count; i++) {

		/* Resolve the device handle. */
		status = bm_get_node(device_list.handles[i], 0, &node);
		if (ACPI_FAILURE(status)) {
			continue;
		}

		device = &(node->device);

		/*
		 * Make sure another driver hasn't already registered for
		 * this device.
		 */
		if (BM_IS_DRIVER_CONTROL(device)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Another driver has already registered for device [%02x].\n", device->handle));
			continue;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Registering driver for device [%02x].\n", device->handle));

		/* Notify driver of new device. */
		status = driver->notify(BM_NOTIFY_DEVICE_ADDED,
			node->device.handle, &(node->driver.context));
		if (ACPI_SUCCESS(status)) {
			node->driver.notify = driver->notify;
			node->driver.request = driver->request;
			node->device.flags |= BM_FLAGS_DRIVER_CONTROL;
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_unregister_driver
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_unregister_driver (
	BM_DEVICE_ID		*criteria,
	BM_DRIVER		*driver)
{
	acpi_status           status = AE_NOT_FOUND;
	BM_HANDLE_LIST          device_list;
	BM_NODE			*node = NULL;
	BM_DEVICE		*device = NULL;
	u32                     i = 0;

	FUNCTION_TRACE("bm_unregister_driver");

	if (!criteria || !driver || !driver->notify || !driver->request) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	MEMSET(&device_list, 0, sizeof(BM_HANDLE_LIST));

	/*
	 * Find Matches:
	 * -------------
	 * Search through the entire device hierarchy for matches against
	 * the given device criteria.
	 */
	status = bm_search(BM_HANDLE_ROOT, criteria, &device_list);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Remove driver:
	 * ---------------
	 * For each match, execute the driver's Notify() function to allow
	 * the driver to cleanup each device instance.
	 */
	for (i = 0; i < device_list.count; i++) {

		/* Resolve the device handle. */
		status = bm_get_node(device_list.handles[i], 0, &node);
		if (ACPI_FAILURE(status)) {
			continue;
		}

		device = &(node->device);

		/*
		 * Make sure driver has really registered for this device.
		 */
		if (!BM_IS_DRIVER_CONTROL(device)) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Driver hasn't registered for device [%02x].\n", device->handle));
			continue;
		}

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Unregistering driver for device [%02x].\n", device->handle));

		/* Notify driver of device removal. */
		status = node->driver.notify(BM_NOTIFY_DEVICE_REMOVED,
			node->device.handle, &(node->driver.context));
		if (ACPI_SUCCESS(status)) {
			node->driver.notify = NULL;
			node->driver.request = NULL;
			node->driver.context = NULL;
			node->device.flags &= ~BM_FLAGS_DRIVER_CONTROL;
		}
	}

	return_ACPI_STATUS(AE_OK);
}
