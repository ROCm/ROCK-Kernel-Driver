/*******************************************************************************
 *
 * Module Name: nsxfobj - Public interfaces to the ACPI subsystem
 *                         ACPI Object oriented interfaces
 *              $Revision: 113 $
 *
 ******************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
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
	 ACPI_MODULE_NAME    ("nsxfobj")

/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_type
 *
 * PARAMETERS:  Handle          - Handle of object whose type is desired
 *              *Ret_type       - Where the type will be placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This routine returns the type associatd with a particular handle
 *
 ******************************************************************************/

acpi_status
acpi_get_type (
	acpi_handle             handle,
	acpi_object_type        *ret_type)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	/* Parameter Validation */

	if (!ret_type) {
		return (AE_BAD_PARAMETER);
	}

	/*
	 * Special case for the predefined Root Node
	 * (return type ANY)
	 */
	if (handle == ACPI_ROOT_OBJECT) {
		*ret_type = ACPI_TYPE_ANY;
		return (AE_OK);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return (AE_BAD_PARAMETER);
	}

	*ret_type = node->type;


	status = acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_parent
 *
 * PARAMETERS:  Handle          - Handle of object whose parent is desired
 *              Ret_handle      - Where the parent handle will be placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Returns a handle to the parent of the object represented by
 *              Handle.
 *
 ******************************************************************************/

acpi_status
acpi_get_parent (
	acpi_handle             handle,
	acpi_handle             *ret_handle)
{
	acpi_namespace_node     *node;
	acpi_status             status;


	if (!ret_handle) {
		return (AE_BAD_PARAMETER);
	}

	/* Special case for the predefined Root Node (no parent) */

	if (handle == ACPI_ROOT_OBJECT) {
		return (AE_NULL_ENTRY);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* Convert and validate the handle */

	node = acpi_ns_map_handle_to_node (handle);
	if (!node) {
		status = AE_BAD_PARAMETER;
		goto unlock_and_exit;
	}

	/* Get the parent entry */

	*ret_handle =
		acpi_ns_convert_entry_to_handle (acpi_ns_get_parent_node (node));

	/* Return exeption if parent is null */

	if (!acpi_ns_get_parent_node (node)) {
		status = AE_NULL_ENTRY;
	}


unlock_and_exit:

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_get_next_object
 *
 * PARAMETERS:  Type            - Type of object to be searched for
 *              Parent          - Parent object whose children we are getting
 *              Last_child      - Previous child that was found.
 *                                The NEXT child will be returned
 *              Ret_handle      - Where handle to the next object is placed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Return the next peer object within the namespace.  If Handle is
 *              valid, Scope is ignored.  Otherwise, the first object within
 *              Scope is returned.
 *
 ******************************************************************************/

acpi_status
acpi_get_next_object (
	acpi_object_type        type,
	acpi_handle             parent,
	acpi_handle             child,
	acpi_handle             *ret_handle)
{
	acpi_status             status;
	acpi_namespace_node     *node;
	acpi_namespace_node     *parent_node = NULL;
	acpi_namespace_node     *child_node = NULL;


	/* Parameter validation */

	if (type > ACPI_TYPE_MAX) {
		return (AE_BAD_PARAMETER);
	}

	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	/* If null handle, use the parent */

	if (!child) {
		/* Start search at the beginning of the specified scope */

		parent_node = acpi_ns_map_handle_to_node (parent);
		if (!parent_node) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}
	else {
		/* Non-null handle, ignore the parent */
		/* Convert and validate the handle */

		child_node = acpi_ns_map_handle_to_node (child);
		if (!child_node) {
			status = AE_BAD_PARAMETER;
			goto unlock_and_exit;
		}
	}

	/* Internal function does the real work */

	node = acpi_ns_get_next_node (type, parent_node, child_node);
	if (!node) {
		status = AE_NOT_FOUND;
		goto unlock_and_exit;
	}

	if (ret_handle) {
		*ret_handle = acpi_ns_convert_entry_to_handle (node);
	}


unlock_and_exit:

	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return (status);
}


