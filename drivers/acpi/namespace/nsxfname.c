/******************************************************************************
 *
 * Module Name: nsxfname - Public interfaces to the ACPI subsystem
 *                         ACPI Namespace oriented interfaces
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2003, R. Byron Moore
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


#include "acpi.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsxfname")


/****************************************************************************
 *
 * FUNCTION:    acpi_get_handle
 *
 * PARAMETERS:  Parent          - Object to search under (search scope).
 *              path_name       - Pointer to an asciiz string containing the
 *                                  name
 *              ret_handle      - Where the return handle is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine will search for a caller specified name in the
 *              name space.  The caller can restrict the search region by
 *              specifying a non NULL parent.  The parent value is itself a
 *              namespace handle.
 *
 ******************************************************************************/

acpi_status
acpi_get_handle (
	acpi_handle                     parent,
	acpi_string                     pathname,
	acpi_handle                     *ret_handle)
{
	acpi_status                     status;
	struct acpi_namespace_node      *node = NULL;
	struct acpi_namespace_node      *prefix_node = NULL;


	ACPI_FUNCTION_ENTRY ();


	/* Parameter Validation */

	if (!ret_handle || !pathname) {
		return (AE_BAD_PARAMETER);
	}

	/* Convert a parent handle to a prefix node */

	if (parent) {
		status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		prefix_node = acpi_ns_map_handle_to_node (parent);
		if (!prefix_node) {
			(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
			return (AE_BAD_PARAMETER);
		}

		status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
	}

	/* Special case for root, since we can't search for it */

	if (ACPI_STRCMP (pathname, ACPI_NS_ROOT_PATH) == 0) {
		*ret_handle = acpi_ns_convert_entry_to_handle (acpi_gbl_root_node);
		return (AE_OK);
	}

	/*
	 *  Find the Node and convert to a handle
	 */
	status = acpi_ns_get_node_by_path (pathname, prefix_node, ACPI_NS_NO_UPSEARCH, &node);

	*ret_handle = NULL;
	if (ACPI_SUCCESS (status)) {
		*ret_handle = acpi_ns_convert_entry_to_handle (node);
	}

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    acpi_get_name
 *
 * PARAMETERS:  Handle          - Handle to be converted to a pathname
 *              name_type       - Full pathname or single segment
 *              Buffer          - Buffer for returned path
 *
 * RETURN:      Pointer to a string containing the fully qualified Name.
 *
 * DESCRIPTION: This routine returns the fully qualified name associated with
 *              the Handle parameter.  This and the acpi_pathname_to_handle are
 *              complementary functions.
 *
 ******************************************************************************/

acpi_status
acpi_get_name (
	acpi_handle                     handle,
	u32                             name_type,
	struct acpi_buffer              *buffer)
{
	acpi_status                     status;
	struct acpi_namespace_node      *node;


	/* Parameter validation */

	if (name_type > ACPI_NAME_TYPE_MAX) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_validate_buffer (buffer);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	if (name_type == ACPI_FULL_PATHNAME) {
		/* Get the full pathname (From the namespace root) */

		status = acpi_ns_handle_to_pathname (handle, buffer);
		return (status);
	}

	/*
	 * Wants the single segment ACPI name.
	 * Validate handle and convert to a namespace Node
	 */
	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer (buffer, ACPI_PATH_SEGMENT_LENGTH);
	if (ACPI_FAILURE (status)) {
		goto unlock_and_exit;
	}

	/* Just copy the ACPI name from the Node and zero terminate it */

	ACPI_STRNCPY (buffer->pointer, node->name.ascii,
			 ACPI_NAME_SIZE);
	((char *) buffer->pointer) [ACPI_NAME_SIZE] = 0;
	status = AE_OK;


unlock_and_exit:

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    acpi_get_object_info
 *
 * PARAMETERS:  Handle          - Object Handle
 *              Info            - Where the info is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns information about an object as gleaned from the
 *              namespace node and possibly by running several standard
 *              control methods (Such as in the case of a device.)
 *
 ******************************************************************************/

acpi_status
acpi_get_object_info (
	acpi_handle                     handle,
	struct acpi_device_info         *info)
{
	struct acpi_device_id           hid;
	struct acpi_device_id           uid;
	acpi_status                     status;
	u32                             device_status = 0;
	acpi_integer                    address = 0;
	struct acpi_namespace_node      *node;


	/* Parameter validation */

	if (!handle || !info) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return (AE_BAD_PARAMETER);
	}

	info->type = node->type;
	info->name = node->name.integer;

	status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/*
	 * If not a device, we are all done.
	 */
	if (info->type != ACPI_TYPE_DEVICE) {
		return (AE_OK);
	}


	/*
	 * Get extra info for ACPI devices only.  Run the
	 * _HID, _UID, _STA, and _ADR methods.  Note: none
	 * of these methods are required, so they may or may
	 * not be present.  The Info->Valid bits are used
	 * to indicate which methods ran successfully.
	 */
	info->valid = 0;

	/* Execute the _HID method and save the result */

	status = acpi_ut_execute_HID (node, &hid);
	if (ACPI_SUCCESS (status)) {
		ACPI_STRNCPY (info->hardware_id, hid.buffer, sizeof(info->hardware_id));
		info->valid |= ACPI_VALID_HID;
	}

	/* Execute the _UID method and save the result */

	status = acpi_ut_execute_UID (node, &uid);
	if (ACPI_SUCCESS (status)) {
		ACPI_STRCPY (info->unique_id, uid.buffer);
		info->valid |= ACPI_VALID_UID;
	}

	/*
	 * Execute the _STA method and save the result
	 * _STA is not always present
	 */
	status = acpi_ut_execute_STA (node, &device_status);
	if (ACPI_SUCCESS (status)) {
		info->current_status = device_status;
		info->valid |= ACPI_VALID_STA;
	}

	/*
	 * Execute the _ADR method and save result if successful
	 * _ADR is not always present
	 */
	status = acpi_ut_evaluate_numeric_object (METHOD_NAME__ADR,
			  node, &address);

	if (ACPI_SUCCESS (status)) {
		info->address = address;
		info->valid |= ACPI_VALID_ADR;
	}

	return (AE_OK);
}

