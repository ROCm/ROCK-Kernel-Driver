/*******************************************************************************
 *
 * Module Name: nssearch - Namespace search
 *              $Revision: 92 $
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
	 ACPI_MODULE_NAME    ("nssearch")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_search_node
 *
 * PARAMETERS:  *Target_name        - Ascii ACPI name to search for
 *              *Node               - Starting node where search will begin
 *              Type                - Object type to match
 *              **Return_node       - Where the matched Named obj is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search a single level of the namespace.  Performs a
 *              simple search of the specified level, and does not add
 *              entries or search parents.
 *
 *
 *      Named object lists are built (and subsequently dumped) in the
 *      order in which the names are encountered during the namespace load;
 *
 *      All namespace searching is linear in this implementation, but
 *      could be easily modified to support any improved search
 *      algorithm.  However, the linear search was chosen for simplicity
 *      and because the trees are small and the other interpreter
 *      execution overhead is relatively high.
 *
 ******************************************************************************/

acpi_status
acpi_ns_search_node (
	u32                     target_name,
	acpi_namespace_node     *node,
	acpi_object_type        type,
	acpi_namespace_node     **return_node)
{
	acpi_namespace_node     *next_node;


	ACPI_FUNCTION_TRACE ("Ns_search_node");


#ifdef ACPI_DEBUG_OUTPUT
	if (ACPI_LV_NAMES & acpi_dbg_level) {
		NATIVE_CHAR         *scope_name;

		scope_name = acpi_ns_get_external_pathname (node);
		if (scope_name) {
			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Searching %s [%p] For %4.4s (%s)\n",
				scope_name, node, (char *) &target_name, acpi_ut_get_type_name (type)));

			ACPI_MEM_FREE (scope_name);
		}
	}
#endif

	/*
	 * Search for name at this namespace level, which is to say that we
	 * must search for the name among the children of this object
	 */
	next_node = node->child;
	while (next_node) {
		/* Check for match against the name */

		if (next_node->name.integer == target_name) {
			/*
			 * Found matching entry.
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Name %4.4s Type [%s] found at %p\n",
				(char *) &target_name, acpi_ut_get_type_name (next_node->type), next_node));

			*return_node = next_node;
			return_ACPI_STATUS (AE_OK);
		}

		/*
		 * The last entry in the list points back to the parent,
		 * so a flag is used to indicate the end-of-list
		 */
		if (next_node->flags & ANOBJ_END_OF_PEER_LIST) {
			/* Searched entire list, we are done */

			break;
		}

		/* Didn't match name, move on to the next peer object */

		next_node = next_node->peer;
	}

	/* Searched entire namespace level, not found */

	ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Name %4.4s Type [%s] not found at %p\n",
		(char *) &target_name, acpi_ut_get_type_name (type), next_node));

	return_ACPI_STATUS (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_search_parent_tree
 *
 * PARAMETERS:  *Target_name        - Ascii ACPI name to search for
 *              *Node               - Starting node where search will begin
 *              Type                - Object type to match
 *              **Return_node       - Where the matched Named Obj is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Called when a name has not been found in the current namespace
 *              level.  Before adding it or giving up, ACPI scope rules require
 *              searching enclosing scopes in cases identified by Acpi_ns_local().
 *
 *              "A name is located by finding the matching name in the current
 *              name space, and then in the parent name space. If the parent
 *              name space does not contain the name, the search continues
 *              recursively until either the name is found or the name space
 *              does not have a parent (the root of the name space).  This
 *              indicates that the name is not found" (From ACPI Specification,
 *              section 5.3)
 *
 ******************************************************************************/

static acpi_status
acpi_ns_search_parent_tree (
	u32                     target_name,
	acpi_namespace_node     *node,
	acpi_object_type        type,
	acpi_namespace_node     **return_node)
{
	acpi_status             status;
	acpi_namespace_node     *parent_node;


	ACPI_FUNCTION_TRACE ("Ns_search_parent_tree");


	parent_node = acpi_ns_get_parent_node (node);

	/*
	 * If there is no parent (i.e., we are at the root) or
	 * type is "local", we won't be searching the parent tree.
	 */
	if (!parent_node) {
		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "[%4.4s] has no parent\n",
			(char *) &target_name));
		 return_ACPI_STATUS (AE_NOT_FOUND);
	}

	if (acpi_ns_local (type)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
			"[%4.4s] type [%s] must be local to this scope (no parent search)\n",
			(char *) &target_name, acpi_ut_get_type_name (type)));
		return_ACPI_STATUS (AE_NOT_FOUND);
	}

	/* Search the parent tree */

	ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Searching parent for %4.4s\n", (char *) &target_name));

	/*
	 * Search parents until found the target or we have backed up to
	 * the root
	 */
	while (parent_node) {
		/*
		 * Search parent scope.  Use TYPE_ANY because we don't care about the
		 * object type at this point, we only care about the existence of
		 * the actual name we are searching for.  Typechecking comes later.
		 */
		status = acpi_ns_search_node (target_name, parent_node,
				   ACPI_TYPE_ANY, return_node);
		if (ACPI_SUCCESS (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Not found here, go up another level
		 * (until we reach the root)
		 */
		parent_node = acpi_ns_get_parent_node (parent_node);
	}

	/* Not found in parent tree */

	return_ACPI_STATUS (AE_NOT_FOUND);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_search_and_enter
 *
 * PARAMETERS:  Target_name         - Ascii ACPI name to search for (4 chars)
 *              Walk_state          - Current state of the walk
 *              *Node               - Starting node where search will begin
 *              Interpreter_mode    - Add names only in ACPI_MODE_LOAD_PASS_x.
 *                                    Otherwise,search only.
 *              Type                - Object type to match
 *              Flags               - Flags describing the search restrictions
 *              **Return_node       - Where the Node is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Search for a name segment in a single namespace level,
 *              optionally adding it if it is not found.  If the passed
 *              Type is not Any and the type previously stored in the
 *              entry was Any (i.e. unknown), update the stored type.
 *
 *              In ACPI_IMODE_EXECUTE, search only.
 *              In other modes, search and add if not found.
 *
 ******************************************************************************/

acpi_status
acpi_ns_search_and_enter (
	u32                     target_name,
	acpi_walk_state         *walk_state,
	acpi_namespace_node     *node,
	acpi_interpreter_mode   interpreter_mode,
	acpi_object_type        type,
	u32                     flags,
	acpi_namespace_node     **return_node)
{
	acpi_status             status;
	acpi_namespace_node     *new_node;


	ACPI_FUNCTION_TRACE ("Ns_search_and_enter");


	/* Parameter validation */

	if (!node || !target_name || !return_node) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Null param: Node %p Name %X Return_node %p\n",
			node, target_name, return_node));

		ACPI_REPORT_ERROR (("Ns_search_and_enter: Null parameter\n"));
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Name must consist of printable characters */

	if (!acpi_ut_valid_acpi_name (target_name)) {
		ACPI_REPORT_ERROR (("Ns_search_and_enter: Bad character in ACPI Name: %X\n",
			target_name));
		return_ACPI_STATUS (AE_BAD_CHARACTER);
	}

	/* Try to find the name in the namespace level specified by the caller */

	*return_node = ACPI_ENTRY_NOT_FOUND;
	status = acpi_ns_search_node (target_name, node, type, return_node);
	if (status != AE_NOT_FOUND) {
		/*
		 * If we found it AND the request specifies that a find is an error,
		 * return the error
		 */
		if ((status == AE_OK) &&
			(flags & ACPI_NS_ERROR_IF_FOUND)) {
			status = AE_ALREADY_EXISTS;
		}

		/*
		 * Either found it or there was an error
		 * -- finished either way
		 */
		return_ACPI_STATUS (status);
	}

	/*
	 * The name was not found.  If we are NOT performing the
	 * first pass (name entry) of loading the namespace, search
	 * the parent tree (all the way to the root if necessary.)
	 * We don't want to perform the parent search when the
	 * namespace is actually being loaded.  We want to perform
	 * the search when namespace references are being resolved
	 * (load pass 2) and during the execution phase.
	 */
	if ((interpreter_mode != ACPI_IMODE_LOAD_PASS1) &&
		(flags & ACPI_NS_SEARCH_PARENT)) {
		/*
		 * Not found at this level - search parent tree according
		 * to ACPI specification
		 */
		status = acpi_ns_search_parent_tree (target_name, node,
				 type, return_node);
		if (ACPI_SUCCESS (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * In execute mode, just search, never add names.  Exit now.
	 */
	if (interpreter_mode == ACPI_IMODE_EXECUTE) {
		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "%4.4s Not found in %p [Not adding]\n",
			(char *) &target_name, node));

		return_ACPI_STATUS (AE_NOT_FOUND);
	}

	/* Create the new named object */

	new_node = acpi_ns_create_node (target_name);
	if (!new_node) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Install the new object into the parent's list of children */

	acpi_ns_install_node (walk_state, node, new_node, type);
	*return_node = new_node;

	return_ACPI_STATUS (AE_OK);
}

