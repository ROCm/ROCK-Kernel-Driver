/******************************************************************************
 *
 * Module Name: bm.c
 *   $Revision: 48 $
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <acpi.h>
#include "bm.h"


#define _COMPONENT		ACPI_BUS
	MODULE_NAME		("bm")


/****************************************************************************
 *                                  Globals
 ****************************************************************************/

extern fadt_descriptor_rev2	acpi_fadt;
/* TBD: Make dynamically sizeable. */
BM_NODE_LIST			node_list;


/****************************************************************************
 *                            Internal Functions
 ****************************************************************************/

/*****************************************************************************
 *
 * FUNCTION:    bm_print_object
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
bm_print_object (
	acpi_handle		handle)
{
	acpi_buffer		buffer;
	acpi_handle		parent;
	acpi_object_type	type;

	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return;
	}

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	acpi_get_parent(handle, &parent);
	acpi_get_type(handle, &type);

	/*
	 * TBD: Hack to get around scope identification problem.
	 */
	if (type == ACPI_TYPE_ANY) {
		if (ACPI_SUCCESS(acpi_get_next_object(ACPI_TYPE_ANY,
			handle, 0, NULL))) {
			type = INTERNAL_TYPE_SCOPE;
		}
	}

	switch (type)
	 {
	case INTERNAL_TYPE_SCOPE:
		acpi_os_printf("SCOPE: ");
		break;
	case ACPI_TYPE_INTEGER:
		acpi_os_printf("SIMPLE (number): ");
		break;
	case ACPI_TYPE_STRING:
		acpi_os_printf("SIMPLE (string): ");
		break;
	case ACPI_TYPE_BUFFER:
		acpi_os_printf("SIMPLE (buffer): ");
		break;
	case ACPI_TYPE_PACKAGE:
		acpi_os_printf("SIMPLE (package): ");
		break;
	case ACPI_TYPE_FIELD_UNIT:
		acpi_os_printf("FIELD UNIT: ");
		break;
	case ACPI_TYPE_DEVICE:
		acpi_os_printf("DEVICE: ");
		break;
	case ACPI_TYPE_EVENT:
		acpi_os_printf("EVENT: ");
		break;
	case ACPI_TYPE_METHOD:
		acpi_os_printf("CONTROL METHOD: ");
		break;
	case ACPI_TYPE_MUTEX:
		acpi_os_printf("MUTEX: ");
		break;
	case ACPI_TYPE_REGION:
		acpi_os_printf("OPERATION REGION: ");
		break;
	case ACPI_TYPE_POWER:
		acpi_os_printf("POWER RESOURCE: ");
		break;
	case ACPI_TYPE_PROCESSOR:
		acpi_os_printf("PROCESSOR: ");
		break;
	case ACPI_TYPE_THERMAL:
		acpi_os_printf("THERMAL ZONE: ");
		break;
	case ACPI_TYPE_BUFFER_FIELD:
		acpi_os_printf("BUFFER FIELD: ");
		break;
	case ACPI_TYPE_DDB_HANDLE:
		acpi_os_printf("DDB HANDLE: ");
		break;
	default:
		acpi_os_printf("OTHER (%d): ", type);
		break;
	}

	acpi_os_printf("Object[%p][%s] parent[%p].\n", handle, (char*)buffer.pointer, parent);

	acpi_os_free(buffer.pointer);
}


/****************************************************************************
 *
 * FUNCTION:    bm_print_node
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
bm_print_node (
	BM_NODE			*node,
	u32                     flags)
{
#ifdef ACPI_DEBUG
	acpi_buffer             buffer;
	BM_DEVICE		*device = NULL;
	char                    *type_string = NULL;

	PROC_NAME("bm_print_node");

	if (!node) {
		return;
	}

	device = &(node->device);

	if (flags & BM_PRINT_PRESENT) {
		if (!BM_DEVICE_PRESENT(device)) {
			return;
		}
	}

	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return;
	}

	acpi_get_name(device->acpi_handle, ACPI_FULL_PATHNAME, &buffer);

	switch(device->id.type) {
	case BM_TYPE_SYSTEM:
		type_string = " System";
		break;
	case BM_TYPE_SCOPE:
		type_string = "  Scope";
		break;
	case BM_TYPE_PROCESSOR:
		type_string = "   Proc";
		break;
	case BM_TYPE_THERMAL_ZONE:
		type_string = "Thermal";
		break;
	case BM_TYPE_POWER_RESOURCE:
		type_string = "  Power";
		break;
	case BM_TYPE_FIXED_BUTTON:
		type_string = " Button";
		break;
	case BM_TYPE_DEVICE:
		type_string = " Device";
		break;
	default:
		type_string = "Unknown";
		break;
	}

	if (!(flags & BM_PRINT_GROUP)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+-------------------------------------------------------------------------------\n"));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "| %s[%02x]:[%p] flags[%02x] hid[%s] %s\n", type_string, device->handle, device->acpi_handle, device->flags, (device->id.hid[0] ? device->id.hid : "       "), (char*)buffer.pointer));

	if (flags & BM_PRINT_IDENTIFICATION) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   identification: uid[%s] adr[%08x]\n", device->id.uid, device->id.adr));
	}

	if (flags & BM_PRINT_LINKAGE) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   linkage: this[%p] parent[%p] next[%p]\n", node, node->parent, node->next));
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|     scope.head[%p] scope.tail[%p]\n", node->scope.head, node->scope.tail));
	}

	if (flags & BM_PRINT_POWER) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|   power: state[D%d] flags[%08x]\n", device->power.state, device->power.flags));
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "|     S0[%02x] S1[%02x] S2[%02x] S3[%02x] S4[%02x] S5[%02x]\n", device->power.dx_supported[0], device->power.dx_supported[1], device->power.dx_supported[2], device->power.dx_supported[3], device->power.dx_supported[4], device->power.dx_supported[5]));
	}

	if (!(flags & BM_PRINT_GROUP)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+-------------------------------------------------------------------------------\n"));
	}

	acpi_os_free(buffer.pointer);
#endif /*ACPI_DEBUG*/

	return;
}


/****************************************************************************
 *
 * FUNCTION:    bm_print_hierarchy
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
bm_print_hierarchy (void)
{
#ifdef ACPI_DEBUG
	u32			i = 0;

	FUNCTION_TRACE("bm_print_hierarchy");

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));

	for (i = 0; i < node_list.count; i++) {
		bm_print_node(node_list.nodes[i], BM_PRINT_GROUP | BM_PRINT_PRESENT);
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_INFO, "+------------------------------------------------------------\n"));
#endif /*ACPI_DEBUG*/

	return_VOID;
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_status
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_get_status (
	BM_DEVICE		*device)
{
	acpi_status           status = AE_OK;

	if (!device) {
		return AE_BAD_PARAMETER;
	}

	device->status = BM_STATUS_UNKNOWN;

	/*
	 * Dynamic Status?
	 * ---------------
	 * If _STA isn't present we just return the default status.
	 */
	if (!(device->flags & BM_FLAGS_DYNAMIC_STATUS)) {
		device->status = BM_STATUS_DEFAULT;
		return AE_OK;
	}

	/*
	 * Evaluate _STA:
	 * --------------
	 */
	status = bm_evaluate_simple_integer(device->acpi_handle, "_STA",
		&(device->status));

	return status;
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_identification
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_get_identification (
	BM_DEVICE		*device)
{
	acpi_status             status = AE_OK;
	acpi_device_info        info;

	if (!device) {
		return AE_BAD_PARAMETER;
	}

	if (!(device->flags & BM_FLAGS_IDENTIFIABLE)) {
		return AE_OK;
	}

	device->id.uid[0] = BM_UID_UNKNOWN;
	device->id.hid[0] = BM_HID_UNKNOWN;
	device->id.adr = BM_ADDRESS_UNKNOWN;

	/*
	 * Get Object Info:
	 * ----------------
	 * Evalute _UID, _HID, and _ADR...
	 */
	status = acpi_get_object_info(device->acpi_handle, &info);
	if (ACPI_FAILURE(status)) {
		return status;
	}

	if (info.valid & ACPI_VALID_UID) {
		MEMCPY((void*)device->id.uid, (void*)info.unique_id,
			sizeof(BM_DEVICE_UID));
	}

	if (info.valid & ACPI_VALID_HID) {
		MEMCPY((void*)device->id.hid, (void*)info.hardware_id,
			sizeof(BM_DEVICE_HID));
	}

	if (info.valid & ACPI_VALID_ADR) {
		device->id.adr = info.address;
	}

	return status;
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_flags
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_get_flags (
	BM_DEVICE		*device)
{
	acpi_handle		acpi_handle = NULL;

	if (!device) {
		return AE_BAD_PARAMETER;
	}

	device->flags = BM_FLAGS_UNKNOWN;

	switch (device->id.type) {

	case BM_TYPE_DEVICE:

		/*
		 * Presence of _DCK indicates a docking station.
		 */
		if (ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_DCK", &acpi_handle))) {
			device->flags |= BM_FLAGS_DOCKING_STATION;
		}

		/*
		 * Presence of _EJD and/or _EJx indicates 'ejectable'.
		 * TBD: _EJx...
		 */
		if (ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_EJD", &acpi_handle))) {
			device->flags |= BM_FLAGS_EJECTABLE;
		}

		/*
		 * Presence of _PR0 or _PS0 indicates 'power manageable'.
		 */
		if (ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_PR0", &acpi_handle)) ||
			ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_PS0", &acpi_handle))) {
			device->flags |= BM_FLAGS_POWER_CONTROL;
		}

		/*
		 * Presence of _CRS indicates 'configurable'.
		 */
		if (ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_CRS", &acpi_handle))) {
			device->flags |= BM_FLAGS_CONFIGURABLE;
		}

		/* Fall through to next case statement. */

	case BM_TYPE_PROCESSOR:
	case BM_TYPE_THERMAL_ZONE:
	case BM_TYPE_POWER_RESOURCE:
		/*
		 * Presence of _HID or _ADR indicates 'identifiable'.
		 */
		if (ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_HID", &acpi_handle)) ||
		   ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
		   "_ADR", &acpi_handle))) {
			device->flags |= BM_FLAGS_IDENTIFIABLE;
		}

		/*
		 * Presence of _STA indicates 'dynamic status'.
		 */
		if (ACPI_SUCCESS(acpi_get_handle(device->acpi_handle,
			"_STA", &acpi_handle))) {
			device->flags |= BM_FLAGS_DYNAMIC_STATUS;
		}

		break;
	}

	return AE_OK;
}


/****************************************************************************
 *
 * FUNCTION:    bm_add_namespace_device
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_add_namespace_device (
	acpi_handle             acpi_handle,
	acpi_object_type        acpi_type,
	BM_NODE			*parent,
	BM_NODE			**child)
{
	acpi_status             status = AE_OK;
	BM_NODE			*node = NULL;
	BM_DEVICE		*device = NULL;

	FUNCTION_TRACE("bm_add_namespace_device");

	if (!parent || !child) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (node_list.count > BM_HANDLES_MAX) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	(*child) = NULL;

	/*
	 * Create Node:
	 * ------------
	 */
	node = acpi_os_callocate(sizeof(BM_NODE));
	if (!node) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	node->parent = parent;
	node->next = NULL;

	device = &(node->device);

	device->handle = node_list.count;
	device->acpi_handle = acpi_handle;

	/*
	 * Device Type:
	 * ------------
	 */
	switch (acpi_type) {
	case INTERNAL_TYPE_SCOPE:
		device->id.type = BM_TYPE_SCOPE;
		break;
	case ACPI_TYPE_PROCESSOR:
		device->id.type = BM_TYPE_PROCESSOR;
		break;
	case ACPI_TYPE_THERMAL:
		device->id.type = BM_TYPE_THERMAL_ZONE;
		break;
	case ACPI_TYPE_POWER:
		device->id.type = BM_TYPE_POWER_RESOURCE;
		break;
	case ACPI_TYPE_DEVICE:
		device->id.type = BM_TYPE_DEVICE;
		break;
	}

	/*
	 * Get Other Device Info:
	 * ----------------------
	 * But only if this device's parent is present (which implies
	 * this device MAY be present).
	 */
	if (BM_NODE_PRESENT(node->parent)) {
		/*
		 * Device Flags
		 */
		status = bm_get_flags(device);
		if (ACPI_FAILURE(status)) {
			goto end;
		}

		/*
		 * Device Identification
		 */
		status = bm_get_identification(device);
		if (ACPI_FAILURE(status)) {
			goto end;
		}

		/*
		 * Device Status
		 */
		status = bm_get_status(device);
		if (ACPI_FAILURE(status)) {
			goto end;
		}

		/*
		 * Power Management:
		 * -----------------
		 * If this node doesn't provide direct power control
		 * then we inherit PM capabilities from its parent.
		 *
		 * TBD: Inherit!
		 */
		if (BM_IS_POWER_CONTROL(device)) {
			status = bm_get_pm_capabilities(node);
			if (ACPI_FAILURE(status)) {
				goto end;
			}
		}
	}

end:
	if (ACPI_FAILURE(status)) {
		acpi_os_free(node);
	}
	else {
		/*
		 * Add to the node_list.
		 */
		node_list.nodes[node_list.count++] = node;

		/*
		 * Formulate Hierarchy:
		 * --------------------
		 * Arrange within the namespace by assigning the parent and
		 * adding to the parent device's list of children (scope).
		 */
		if (!parent->scope.head) {
			parent->scope.head = node;
		}
		else {
			if (!parent->scope.tail) {
				(parent->scope.head)->next = node;
			}
			else {
				(parent->scope.tail)->next = node;
			}
		}
		parent->scope.tail = node;

		(*child) = node;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_enumerate_namespace
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_enumerate_namespace (void)
{
	acpi_status		status = AE_OK;
	acpi_handle             parent_handle = ACPI_ROOT_OBJECT;
	acpi_handle             child_handle = NULL;
	BM_NODE			*parent = NULL;
	BM_NODE			*child = NULL;
	acpi_object_type        acpi_type = 0;
	u32                     level = 1;

	FUNCTION_TRACE("bm_enumerate_namespace");

	parent = node_list.nodes[0];

	/*
	 * Enumerate ACPI Namespace:
	 * -------------------------
	 * Parse through the ACPI namespace, identify all 'devices',
	 * and create a new entry for each in our collection.
	 */
	while (level > 0) {

		/*
		 * Get the next object at this level.
		 */
		status = acpi_get_next_object(ACPI_TYPE_ANY, parent_handle, child_handle, &child_handle);
		if (ACPI_SUCCESS(status)) {
			/*
			 * TBD: This is a hack to get around the problem
			 *       identifying scope objects.  Scopes
			 *       somehow need to be uniquely identified.
			 */
			status = acpi_get_type(child_handle, &acpi_type);
			if (ACPI_SUCCESS(status) && (acpi_type == ACPI_TYPE_ANY)) {
				status = acpi_get_next_object(ACPI_TYPE_ANY, child_handle, 0, NULL);
				if (ACPI_SUCCESS(status)) {
					acpi_type = INTERNAL_TYPE_SCOPE;
				}
			}

			/*
			 * Device?
			 * -------
			 * If this object is a 'device', insert into the
			 * ACPI Bus Manager's local hierarchy and search
			 * the object's scope for any child devices (a
			 * depth-first search).
			 */
			switch (acpi_type) {
			case INTERNAL_TYPE_SCOPE:
			case ACPI_TYPE_DEVICE:
			case ACPI_TYPE_PROCESSOR:
			case ACPI_TYPE_THERMAL:
			case ACPI_TYPE_POWER:
				status = bm_add_namespace_device(child_handle, acpi_type, parent, &child);
				if (ACPI_SUCCESS(status)) {
					status = acpi_get_next_object(ACPI_TYPE_ANY, child_handle, 0, NULL);
					if (ACPI_SUCCESS(status)) {
						level++;
						parent_handle = child_handle;
						child_handle = 0;
						parent = child;
					}
				}
				break;
			}
		}

		/*
		 * Scope Exhausted:
		 * ----------------
		 * No more children in this object's scope, Go back up
		 * in the namespace tree to the object's parent.
		 */
		else {
			level--;
			child_handle = parent_handle;
			acpi_get_parent(parent_handle,
				&parent_handle);

			if (parent) {
				parent = parent->parent;
			}
			else {
				return_ACPI_STATUS(AE_NULL_ENTRY);
			}
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_add_fixed_feature_device
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_add_fixed_feature_device (
	BM_NODE			*parent,
	BM_DEVICE_TYPE		device_type,
	char			*device_hid)
{
	acpi_status             status = AE_OK;
	BM_NODE			*node = NULL;

	FUNCTION_TRACE("bm_add_fixed_feature_device");

	if (!parent) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (node_list.count > BM_HANDLES_MAX) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/*
	 * Allocate the new device and add to the device array.
	 */
	node = acpi_os_callocate(sizeof(BM_NODE));
	if (!node) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	/*
	 * Get device info.
	 */
	node->device.handle = node_list.count;
	node->device.acpi_handle = ACPI_ROOT_OBJECT;
	node->device.id.type = BM_TYPE_FIXED_BUTTON;
	if (device_hid) {
		MEMCPY((void*)node->device.id.hid, device_hid,
			sizeof(node->device.id.hid));
	}
	node->device.flags = BM_FLAGS_FIXED_FEATURE;
	node->device.status = BM_STATUS_DEFAULT;
	/* TBD: Device PM capabilities */

	/*
	 * Add to the node_list.
	 */
	node_list.nodes[node_list.count++] = node;

	/*
	 * Formulate Hierarchy:
	 * --------------------
	 * Arrange within the namespace by assigning the parent and
	 * adding to the parent device's list of children (scope).
	 */
	node->parent = parent;
	node->next = NULL;

	if (parent) {
		if (!parent->scope.head) {
			parent->scope.head = node;
		}
		else {
			if (!parent->scope.tail) {
				(parent->scope.head)->next = node;
			}
			else {
				(parent->scope.tail)->next = node;
			}
		}
		parent->scope.tail = node;
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_enumerate_fixed_features
 *
 * PARAMETERS:  <none>
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_enumerate_fixed_features (void)
{
	FUNCTION_TRACE("bm_enumerate_fixed_features");

	/*
	 * Root Object:
	 * ------------
	 * Fabricate the root object, which happens to always get a
	 * device_handle of zero.
	 */
	node_list.nodes[0] = acpi_os_callocate(sizeof(BM_NODE));
	if (NULL == (node_list.nodes[0])) {
		return_ACPI_STATUS(AE_NO_MEMORY);
	}

	node_list.nodes[0]->device.handle = BM_HANDLE_ROOT;
	node_list.nodes[0]->device.acpi_handle = ACPI_ROOT_OBJECT;
	node_list.nodes[0]->device.flags = BM_FLAGS_UNKNOWN;
	node_list.nodes[0]->device.status = BM_STATUS_DEFAULT;
	node_list.nodes[0]->device.id.type = BM_TYPE_SYSTEM;
	/* TBD: Get system PM capabilities (Sx states?) */

	node_list.count++;

	/*
	 * Fixed Features:
	 * ---------------
	 * Enumerate fixed-feature devices (e.g. power and sleep buttons).
	 */
	if (acpi_fadt.pwr_button == 0) {
		bm_add_fixed_feature_device(node_list.nodes[0],
			BM_TYPE_FIXED_BUTTON, BM_HID_POWER_BUTTON);
	}

	if (acpi_fadt.sleep_button == 0) {
		bm_add_fixed_feature_device(node_list.nodes[0],
			BM_TYPE_FIXED_BUTTON, BM_HID_SLEEP_BUTTON);
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_handle
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_get_handle (
	acpi_handle             acpi_handle,
	BM_HANDLE               *device_handle)
{
	acpi_status             status = AE_NOT_FOUND;
	u32			i = 0;

	FUNCTION_TRACE("bm_get_handle");

	if (!device_handle) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	*device_handle = BM_HANDLE_UNKNOWN;

	/*
	 * Search all devices for a match on the ACPI handle.
	 */
	for (i=0; i<node_list.count; i++) {

		if (!node_list.nodes[i]) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) node entry [%p] detected.\n", device_handle));
			status = AE_NULL_ENTRY;
			break;
		}

		if (node_list.nodes[i]->device.acpi_handle == acpi_handle) {
			*device_handle = node_list.nodes[i]->device.handle;
			status = AE_OK;
			break;
		}
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_get_node
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_get_node (
	BM_HANDLE               device_handle,
	acpi_handle             acpi_handle,
	BM_NODE			**node)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("bm_get_node");

	if (!node) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/* busmgr failed to init, but we're being called by subordinate drivers */
	if (node_list.count < 1) {
		return_ACPI_STATUS(AE_NOT_FOUND);
	}

	/*
	 * If no device handle, resolve acpi handle to device handle.
	 */
	if (!device_handle && acpi_handle) {
		status = bm_get_handle(acpi_handle, &device_handle);
		if (ACPI_FAILURE(status))
			return_ACPI_STATUS(status);
	}

	/*
	 * Valid device handle?
	 */
	if (device_handle > BM_HANDLES_MAX) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid node handle [%02x] detected.\n", device_handle));
		return_ACPI_STATUS(AE_ERROR);
	}

	*node = node_list.nodes[device_handle];

	/*
	 * Valid node?
	 */
	if (!(*node)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid (NULL) node entry [%02x] detected.\n", device_handle));
		return_ACPI_STATUS(AE_NULL_ENTRY);
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    bm_initialize
 *
 * PARAMETERS:  <none>
 *
 * RETURN:      Exception code.
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_initialize (void)
{
	acpi_status             status = AE_OK;
	u32                     start = 0;
	u32                     stop = 0;
	u32                     elapsed = 0;

	FUNCTION_TRACE("bm_initialize");

	MEMSET(&node_list, 0, sizeof(BM_NODE_LIST));

	status = acpi_get_timer(&start);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Building device hierarchy.\n"));

	/*
	 * Enumerate ACPI fixed-feature devices.
	 */
	status = bm_enumerate_fixed_features();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Enumerate the ACPI namespace.
	 */
	status = bm_enumerate_namespace();
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	acpi_get_timer(&stop);
	acpi_get_timer_duration(start, stop, &elapsed);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Building device hierarchy took [%d] microseconds.\n", elapsed));

	/*
	 * Display hierarchy.
	 */
	bm_print_hierarchy();

	/*
	 * Register for all standard and device-specific notifications.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Registering for all device notifications.\n"));

	status = acpi_install_notify_handler(ACPI_ROOT_OBJECT,
		ACPI_SYSTEM_NOTIFY, &bm_notify, NULL);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unable to register for standard notifications.\n"));
		return_ACPI_STATUS(status);
	}

	status = acpi_install_notify_handler(ACPI_ROOT_OBJECT,
		ACPI_DEVICE_NOTIFY, &bm_notify, NULL);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unable to register for device-specific notifications.\n"));
		return_ACPI_STATUS(status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "ACPI Bus Manager enabled.\n"));

	/*
	 * Initialize built-in power resource driver.
	 */
	bm_pr_initialize();

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_terminate
 *
 * PARAMETERS:  <none>
 *
 * RETURN:      Exception code.
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_terminate (void)
{
	acpi_status             status = AE_OK;
	u32                     i = 0;

	FUNCTION_TRACE("bm_terminate");

	/*
	 * Terminate built-in power resource driver.
	 */
	bm_pr_terminate();

	/*
	 * Unregister for all notifications.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Unregistering for device notifications.\n"));

	status = acpi_remove_notify_handler(ACPI_ROOT_OBJECT,
		ACPI_SYSTEM_NOTIFY, &bm_notify);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unable to un-register for standard notifications.\n"));
	}

	status = acpi_remove_notify_handler(ACPI_ROOT_OBJECT,
		ACPI_DEVICE_NOTIFY, &bm_notify);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unable to un-register for device-specific notifications.\n"));
	}

	/*
	 * Parse through the device array, freeing all entries.
	 */
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Removing device hierarchy.\n"));
	for (i = 0; i < node_list.count; i++) {
		if (node_list.nodes[i]) {
			acpi_os_free(node_list.nodes[i]);
		}
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "ACPI Bus Manager disabled.\n"));

	return_ACPI_STATUS(AE_OK);
}
