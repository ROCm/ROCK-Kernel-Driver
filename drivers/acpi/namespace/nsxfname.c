/******************************************************************************
 *
 * Module Name: nsxfname - Public interfaces to the ACPI subsystem
 *                         ACPI Namespace oriented interfaces
 *              $Revision: 82 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 R. Byron Moore
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
#include "acinterp.h"
#include "acnamesp.h"
#include "amlcode.h"
#include "acparser.h"
#include "acdispat.h"
#include "acevents.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsxfname")


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_handle
 *
 * PARAMETERS:  Parent          - Object to search under (search scope).
 *              Path_name       - Pointer to an asciiz string containing the
 *                                  name
 *              Ret_handle      - Where the return handle is placed
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
	acpi_handle             parent,
	acpi_string             pathname,
	acpi_handle             *ret_handle)
{
	acpi_status             status;
	acpi_namespace_node     *node = NULL;
	acpi_namespace_node     *prefix_node = NULL;


	FUNCTION_ENTRY ();


	/* Parameter Validation */

	if (!ret_handle || !pathname) {
		return (AE_BAD_PARAMETER);
	}

	/* Convert a parent handle to a prefix node */

	if (parent) {
		acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

		prefix_node = acpi_ns_map_handle_to_node (parent);
		if (!prefix_node) {
			acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
			return (AE_BAD_PARAMETER);
		}

		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	}

	/* Special case for root, since we can't search for it */

	if (STRCMP (pathname, NS_ROOT_PATH) == 0) {
		*ret_handle = acpi_ns_convert_entry_to_handle (acpi_gbl_root_node);
		return (AE_OK);
	}

	/*
	 *  Find the Node and convert to a handle
	 */
	status = acpi_ns_get_node (pathname, prefix_node, &node);

	*ret_handle = NULL;
	if (ACPI_SUCCESS (status)) {
		*ret_handle = acpi_ns_convert_entry_to_handle (node);
	}

	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_name
 *
 * PARAMETERS:  Handle          - Handle to be converted to a pathname
 *              Name_type       - Full pathname or single segment
 *              Ret_path_ptr    - Buffer for returned path
 *
 * RETURN:      Pointer to a string containing the fully qualified Name.
 *
 * DESCRIPTION: This routine returns the fully qualified name associated with
 *              the Handle parameter.  This and the Acpi_pathname_to_handle are
 *              complementary functions.
 *
 ******************************************************************************/

acpi_status
acpi_get_name (
	acpi_handle             handle,
	u32                     name_type,
	acpi_buffer             *ret_path_ptr)
{
	acpi_status             status;
	acpi_namespace_node     *node;


	/* Buffer pointer must be valid always */

	if (!ret_path_ptr || (name_type > ACPI_NAME_TYPE_MAX)) {
		return (AE_BAD_PARAMETER);
	}

	/* Allow length to be zero and ignore the pointer */

	if ((ret_path_ptr->length) &&
	   (!ret_path_ptr->pointer)) {
		return (AE_BAD_PARAMETER);
	}

	if (name_type == ACPI_FULL_PATHNAME) {
		/* Get the full pathname (From the namespace root) */

		status = acpi_ns_handle_to_pathname (handle, &ret_path_ptr->length,
				   ret_path_ptr->pointer);
		return (status);
	}

	/*
	 * Wants the single segment ACPI name.
	 * Validate handle and convert to an Node
	 */
	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Check if name will fit in buffer */

	if (ret_path_ptr->length < PATH_SEGMENT_LENGTH) {
		ret_path_ptr->length = PATH_SEGMENT_LENGTH;
		status = AE_BUFFER_OVERFLOW;
		goto unlock_and_exit;
	}

	/* Just copy the ACPI name from the Node and zero terminate it */

	STRNCPY (ret_path_ptr->pointer, (NATIVE_CHAR *) &node->name,
			 ACPI_NAME_SIZE);
	((NATIVE_CHAR *) ret_path_ptr->pointer) [ACPI_NAME_SIZE] = 0;
	status = AE_OK;


unlock_and_exit:

	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_get_object_info
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
	acpi_handle             handle,
	acpi_device_info        *info)
{
	acpi_device_id          hid;
	acpi_device_id          uid;
	acpi_status             status;
	u32                     device_status = 0;
	acpi_integer            address = 0;
	acpi_namespace_node     *node;


	/* Parameter validation */

	if (!handle || !info) {
		return (AE_BAD_PARAMETER);
	}

	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return (AE_BAD_PARAMETER);
	}

	info->type      = node->type;
	info->name      = node->name;

	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

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
		STRNCPY (info->hardware_id, hid.buffer, sizeof(info->hardware_id));

		info->valid |= ACPI_VALID_HID;
	}

	/* Execute the _UID method and save the result */

	status = acpi_ut_execute_UID (node, &uid);
	if (ACPI_SUCCESS (status)) {
		STRCPY (info->unique_id, uid.buffer);

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

