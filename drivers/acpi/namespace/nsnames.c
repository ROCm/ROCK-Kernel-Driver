/*******************************************************************************
 *
 * Module Name: nsnames - Name manipulation and search
 *              $Revision: 64 $
 *
 ******************************************************************************/

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
#include "amlcode.h"
#include "acinterp.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsnames")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_table_pathname
 *
 * PARAMETERS:  Node        - Scope whose name is needed
 *
 * RETURN:      Pointer to storage containing the fully qualified name of
 *              the scope, in Label format (all segments strung together
 *              with no separators)
 *
 * DESCRIPTION: Used for debug printing in Acpi_ns_search_table().
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_ns_get_table_pathname (
	acpi_namespace_node     *node)
{
	NATIVE_CHAR             *name_buffer;
	u32                     size;
	acpi_name               name;
	acpi_namespace_node     *child_node;
	acpi_namespace_node     *parent_node;


	FUNCTION_TRACE_PTR ("Ns_get_table_pathname", node);


	if (!acpi_gbl_root_node || !node) {
		/*
		 * If the name space has not been initialized,
		 * this function should not have been called.
		 */
		return_PTR (NULL);
	}

	child_node = node->child;


	/* Calculate required buffer size based on depth below root */

	size = 1;
	parent_node = child_node;
	while (parent_node) {
		parent_node = acpi_ns_get_parent_object (parent_node);
		if (parent_node) {
			size += ACPI_NAME_SIZE;
		}
	}


	/* Allocate a buffer to be returned to caller */

	name_buffer = ACPI_MEM_CALLOCATE (size + 1);
	if (!name_buffer) {
		REPORT_ERROR (("Ns_get_table_pathname: allocation failure\n"));
		return_PTR (NULL);
	}


	/* Store terminator byte, then build name backwards */

	name_buffer[size] = '\0';
	while ((size > ACPI_NAME_SIZE) &&
		acpi_ns_get_parent_object (child_node)) {
		size -= ACPI_NAME_SIZE;
		name = acpi_ns_find_parent_name (child_node);

		/* Put the name into the buffer */

		MOVE_UNALIGNED32_TO_32 ((name_buffer + size), &name);
		child_node = acpi_ns_get_parent_object (child_node);
	}

	name_buffer[--size] = AML_ROOT_PREFIX;

	if (size != 0) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Bad pointer returned; size=%X\n", size));
	}

	return_PTR (name_buffer);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_pathname_length
 *
 * PARAMETERS:  Node        - Namespace node
 *
 * RETURN:      Length of path, including prefix
 *
 * DESCRIPTION: Get the length of the pathname string for this node
 *
 ******************************************************************************/

u32
acpi_ns_get_pathname_length (
	acpi_namespace_node     *node)
{
	u32                     size;
	acpi_namespace_node     *next_node;


	FUNCTION_ENTRY ();


	/*
	 * Compute length of pathname as 5 * number of name segments.
	 * Go back up the parent tree to the root
	 */
	for (size = 0, next_node = node;
		  acpi_ns_get_parent_object (next_node);
		  next_node = acpi_ns_get_parent_object (next_node)) {
		size += PATH_SEGMENT_LENGTH;
	}

	/* Special case for size still 0 - no parent for "special" nodes */

	if (!size) {
		size = PATH_SEGMENT_LENGTH;
	}

	return (size + 1);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_handle_to_pathname
 *
 * PARAMETERS:  Target_handle           - Handle of named object whose name is
 *                                        to be found
 *              Buf_size                - Size of the buffer provided
 *              User_buffer             - Where the pathname is returned
 *
 * RETURN:      Status, Buffer is filled with pathname if status is AE_OK
 *
 * DESCRIPTION: Build and return a full namespace pathname
 *
 * MUTEX:       Locks Namespace
 *
 ******************************************************************************/

acpi_status
acpi_ns_handle_to_pathname (
	acpi_handle             target_handle,
	u32                     *buf_size,
	NATIVE_CHAR             *user_buffer)
{
	acpi_status             status = AE_OK;
	acpi_namespace_node     *node;
	u32                     path_length;
	u32                     user_buf_size;
	acpi_name               name;
	u32                     size;


	FUNCTION_TRACE_PTR ("Ns_handle_to_pathname", target_handle);


	if (!acpi_gbl_root_node) {
		/*
		 * If the name space has not been initialized,
		 * this function should not have been called.
		 */
		return_ACPI_STATUS (AE_NO_NAMESPACE);
	}

	node = acpi_ns_map_handle_to_node (target_handle);
	if (!node) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	/* Set return length to the required path length */

	path_length = acpi_ns_get_pathname_length (node);
	size = path_length - 1;

	user_buf_size = *buf_size;
	*buf_size = path_length;

	/* Check if the user buffer is sufficiently large */

	if (path_length > user_buf_size) {
		status = AE_BUFFER_OVERFLOW;
		goto exit;
	}

	/* Store null terminator */

	user_buffer[size] = 0;
	size -= ACPI_NAME_SIZE;

	/* Put the original ACPI name at the end of the path */

	MOVE_UNALIGNED32_TO_32 ((user_buffer + size),
			 &node->name);

	user_buffer[--size] = PATH_SEPARATOR;

	/* Build name backwards, putting "." between segments */

	while ((size > ACPI_NAME_SIZE) && node) {
		size -= ACPI_NAME_SIZE;
		name = acpi_ns_find_parent_name (node);
		MOVE_UNALIGNED32_TO_32 ((user_buffer + size), &name);

		user_buffer[--size] = PATH_SEPARATOR;
		node = acpi_ns_get_parent_object (node);
	}

	/*
	 * Overlay the "." preceding the first segment with
	 * the root name "\"
	 */
	user_buffer[size] = '\\';

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Len=%X, %s \n", path_length, user_buffer));

exit:
	return_ACPI_STATUS (status);
}


