/*******************************************************************************
 *
 * Module Name: nsnames - Name manipulation and search
 *              $Revision: 79 $
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
#include "amlcode.h"
#include "acnamesp.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsnames")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_build_external_path
 *
 * PARAMETERS:  Node            - NS node whose pathname is needed
 *              Size            - Size of the pathname
 *              *Name_buffer    - Where to return the pathname
 *
 * RETURN:      Places the pathname into the Name_buffer, in external format
 *              (name segments separated by path separators)
 *
 * DESCRIPTION: Generate a full pathaname
 *
 ******************************************************************************/

void
acpi_ns_build_external_path (
	acpi_namespace_node     *node,
	ACPI_SIZE               size,
	NATIVE_CHAR             *name_buffer)
{
	ACPI_SIZE               index;
	acpi_namespace_node     *parent_node;


	ACPI_FUNCTION_NAME ("Ns_build_external_path");


	/* Special case for root */

	index = size - 1;
	if (index < ACPI_NAME_SIZE) {
		name_buffer[0] = AML_ROOT_PREFIX;
		name_buffer[1] = 0;
		return;
	}

	/* Store terminator byte, then build name backwards */

	parent_node = node;
	name_buffer[index] = 0;

	while ((index > ACPI_NAME_SIZE) && (parent_node != acpi_gbl_root_node)) {
		index -= ACPI_NAME_SIZE;

		/* Put the name into the buffer */

		ACPI_MOVE_UNALIGNED32_TO_32 ((name_buffer + index), &parent_node->name);
		parent_node = acpi_ns_get_parent_node (parent_node);

		/* Prefix name with the path separator */

		index--;
		name_buffer[index] = PATH_SEPARATOR;
	}

	/* Overwrite final separator with the root prefix character */

	name_buffer[index] = AML_ROOT_PREFIX;

	if (index != 0) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Could not construct pathname; index=%X, size=%X, Path=%s\n",
			(u32) index, (u32) size, &name_buffer[size]));
	}

	return;
}


#ifdef ACPI_DEBUG_OUTPUT
/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_get_external_pathname
 *
 * PARAMETERS:  Node            - NS node whose pathname is needed
 *
 * RETURN:      Pointer to storage containing the fully qualified name of
 *              the node, In external format (name segments separated by path
 *              separators.)
 *
 * DESCRIPTION: Used for debug printing in Acpi_ns_search_table().
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_ns_get_external_pathname (
	acpi_namespace_node     *node)
{
	NATIVE_CHAR             *name_buffer;
	ACPI_SIZE               size;


	ACPI_FUNCTION_TRACE_PTR ("Ns_get_external_pathname", node);


	/* Calculate required buffer size based on depth below root */

	size = acpi_ns_get_pathname_length (node);

	/* Allocate a buffer to be returned to caller */

	name_buffer = ACPI_MEM_CALLOCATE (size);
	if (!name_buffer) {
		ACPI_REPORT_ERROR (("Ns_get_table_pathname: allocation failure\n"));
		return_PTR (NULL);
	}

	/* Build the path in the allocated buffer */

	acpi_ns_build_external_path (node, size, name_buffer);
	return_PTR (name_buffer);
}
#endif


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

ACPI_SIZE
acpi_ns_get_pathname_length (
	acpi_namespace_node     *node)
{
	ACPI_SIZE               size;
	acpi_namespace_node     *next_node;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * Compute length of pathname as 5 * number of name segments.
	 * Go back up the parent tree to the root
	 */
	size = 0;
	next_node = node;

	while (next_node && (next_node != acpi_gbl_root_node)) {
		size += PATH_SEGMENT_LENGTH;
		next_node = acpi_ns_get_parent_node (next_node);
	}

	return (size + 1);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_handle_to_pathname
 *
 * PARAMETERS:  Target_handle           - Handle of named object whose name is
 *                                        to be found
 *              Buffer                  - Where the pathname is returned
 *
 * RETURN:      Status, Buffer is filled with pathname if status is AE_OK
 *
 * DESCRIPTION: Build and return a full namespace pathname
 *
 ******************************************************************************/

acpi_status
acpi_ns_handle_to_pathname (
	acpi_handle             target_handle,
	acpi_buffer             *buffer)
{
	acpi_status             status;
	acpi_namespace_node     *node;
	ACPI_SIZE               required_size;


	ACPI_FUNCTION_TRACE_PTR ("Ns_handle_to_pathname", target_handle);


	node = acpi_ns_map_handle_to_node (target_handle);
	if (!node) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Determine size required for the caller buffer */

	required_size = acpi_ns_get_pathname_length (node);

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer (buffer, required_size);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Build the path in the caller buffer */

	acpi_ns_build_external_path (node, required_size, buffer->pointer);

	ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "%s [%X] \n", (char *) buffer->pointer, (u32) required_size));
	return_ACPI_STATUS (AE_OK);
}


