/*****************************************************************************
 *
 * Module Name: bmnotify.c
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
	 MODULE_NAME		("bmnotify")


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    bm_generate_notify
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_generate_notify (
	BM_NODE			*node,
	u32			notify_type)
{
	acpi_status		status = AE_OK;
	BM_DEVICE		*device = NULL;

	FUNCTION_TRACE("bm_generate_notify");

	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	device = &(node->device);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Sending notify [%02x] to device [%02x].\n", notify_type, node->device.handle));

	if (!BM_IS_DRIVER_CONTROL(device)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "No driver installed for device [%02x].\n", device->handle));
		return_ACPI_STATUS(AE_NOT_EXIST);
	}

	status = node->driver.notify(notify_type, node->device.handle,
		&(node->driver.context));

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_device_check
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_device_check (
	BM_NODE			*node,
	u32			*status_change)
{
	acpi_status             status = AE_OK;
	BM_DEVICE		*device = NULL;
	BM_DEVICE_STATUS	old_status = BM_STATUS_UNKNOWN;

	FUNCTION_TRACE("bm_device_check");

	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	device = &(node->device);

	if (status_change) {
		*status_change = FALSE;
	}

	old_status = device->status;

	/*
	 * Parent Present?
	 * ---------------
	 * Only check this device if its parent is present (which implies
	 * this device MAY be present).
	 */
	if (!BM_NODE_PRESENT(node->parent)) {
		return_ACPI_STATUS(AE_OK);
	}

	/*
	 * Get Status:
	 * -----------
	 * And see if the status has changed.
	 */
	status = bm_get_status(device);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}
	
	if (old_status == node->device.status) {
		return_ACPI_STATUS(AE_OK);
	}

	if (status_change) {
		*status_change = TRUE;
	}
	
	/*
	 * Device Insertion?
	 * -----------------
	 */
	if ((device->status & BM_STATUS_PRESENT) &&
		!(old_status & BM_STATUS_PRESENT)) {
		/* TBD: Make sure driver is loaded, and if not, load. */
		status = bm_generate_notify(node, BM_NOTIFY_DEVICE_ADDED);
	}

	/*
	 * Device Removal?
	 * ---------------
	 */
	else if (!(device->status & BM_STATUS_PRESENT) &&
		(old_status & BM_STATUS_PRESENT)) {
		/* TBD: Unload driver if last device instance. */
		status = bm_generate_notify(node, BM_NOTIFY_DEVICE_REMOVED);
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_bus_check
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_bus_check (
	BM_NODE			*parent_node)
{
	acpi_status             status = AE_OK;
	u32			status_change = FALSE;

	FUNCTION_TRACE("bm_bus_check");

	if (!parent_node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Status Change?
	 * --------------
	 */
	status = bm_device_check(parent_node, &status_change);
	if (ACPI_FAILURE(status) || !status_change) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Enumerate Scope:
	 * ----------------
	 * TBD: Enumerate child devices within this device's scope and
	 *       run bm_device_check()'s on them...
	 */

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    bm_notify
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
bm_notify (
	acpi_handle             acpi_handle,
	u32                     notify_value,
	void                    *context)
{
	acpi_status             status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_notify");

	/*
	 * Resolve the ACPI handle.
	 */
	status = bm_get_node(0, acpi_handle, &node);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Recieved notify [%02x] for unknown device [%p].\n", notify_value, acpi_handle));
		return_VOID;
	}

	/*
	 * Device-Specific or Standard?
	 * ----------------------------
	 * Device-specific notifies are forwarded to the control module's
	 * notify() function for processing.  Standard notifies are handled
	 * internally.
	 */
	if (notify_value > 0x7F) {
		status = bm_generate_notify(node, notify_value);
	}
	else {
		switch (notify_value) {

		case BM_NOTIFY_BUS_CHECK:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received BUS CHECK notification for device [%02x].\n", node->device.handle));
			status = bm_bus_check(node);
			break;

		case BM_NOTIFY_DEVICE_CHECK:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received DEVICE CHECK notification for device [%02x].\n", node->device.handle));
			status = bm_device_check(node, NULL);
			break;

		case BM_NOTIFY_DEVICE_WAKE:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received DEVICE WAKE notification for device [%02x].\n", node->device.handle));
			/* TBD */
			break;

		case BM_NOTIFY_EJECT_REQUEST:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received EJECT REQUEST notification for device [%02x].\n", node->device.handle));
			/* TBD */
			break;

		case BM_NOTIFY_DEVICE_CHECK_LIGHT:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received DEVICE CHECK LIGHT notification for device [%02x].\n", node->device.handle));
			/* TBD: Exactly what does the 'light' mean? */
			status = bm_device_check(node, NULL);
			break;

		case BM_NOTIFY_FREQUENCY_MISMATCH:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received FREQUENCY MISMATCH notification for device [%02x].\n", node->device.handle));
			/* TBD */
			break;

		case BM_NOTIFY_BUS_MODE_MISMATCH:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received BUS MODE MISMATCH notification for device [%02x].\n", node->device.handle));
			/* TBD */
			break;

		case BM_NOTIFY_POWER_FAULT:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received POWER FAULT notification.\n"));
			/* TBD */
			break;

		default:
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Received unknown/unsupported notification.\n"));
			break;
		}
	}

	return_VOID;
}


