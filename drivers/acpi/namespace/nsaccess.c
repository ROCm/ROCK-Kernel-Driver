/*******************************************************************************
 *
 * Module Name: nsaccess - Top-level functions for accessing ACPI namespace
 *              $Revision: 161 $
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
#include "acdispat.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsaccess")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_root_initialize
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate and initialize the default root named objects
 *
 * MUTEX:       Locks namespace for entire execution
 *
 ******************************************************************************/

acpi_status
acpi_ns_root_initialize (void)
{
	acpi_status                 status;
	const acpi_predefined_names *init_val = NULL;
	acpi_namespace_node         *new_node;
	acpi_operand_object         *obj_desc;


	ACPI_FUNCTION_TRACE ("Ns_root_initialize");


	status = acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * The global root ptr is initially NULL, so a non-NULL value indicates
	 * that Acpi_ns_root_initialize() has already been called; just return.
	 */
	if (acpi_gbl_root_node) {
		status = AE_OK;
		goto unlock_and_exit;
	}

	/*
	 * Tell the rest of the subsystem that the root is initialized
	 * (This is OK because the namespace is locked)
	 */
	acpi_gbl_root_node = &acpi_gbl_root_node_struct;

	/* Enter the pre-defined names in the name table */

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
		"Entering predefined entries into namespace\n"));

	for (init_val = acpi_gbl_pre_defined_names; init_val->name; init_val++) {
		status = acpi_ns_lookup (NULL, init_val->name, init_val->type,
				  ACPI_IMODE_LOAD_PASS2, ACPI_NS_NO_UPSEARCH, NULL, &new_node);

		if (ACPI_FAILURE (status) || (!new_node)) /* Must be on same line for code converter */ {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Could not create predefined name %s, %s\n",
				init_val->name, acpi_format_exception (status)));
		}

		/*
		 * Name entered successfully.
		 * If entry in Pre_defined_names[] specifies an
		 * initial value, create the initial value.
		 */
		if (init_val->val) {
			/*
			 * Entry requests an initial value, allocate a
			 * descriptor for it.
			 */
			obj_desc = acpi_ut_create_internal_object (init_val->type);
			if (!obj_desc) {
				status = AE_NO_MEMORY;
				goto unlock_and_exit;
			}

			/*
			 * Convert value string from table entry to
			 * internal representation. Only types actually
			 * used for initial values are implemented here.
			 */
			switch (init_val->type) {
			case ACPI_TYPE_METHOD:
				obj_desc->method.param_count =
						(u8) ACPI_STRTOUL (init_val->val, NULL, 10);
				obj_desc->common.flags |= AOPOBJ_DATA_VALID;

#if defined (ACPI_NO_METHOD_EXECUTION) || defined (ACPI_CONSTANT_EVAL_ONLY)

				/* Compiler cheats by putting parameter count in the Owner_iD */

				new_node->owner_id = obj_desc->method.param_count;
#endif
				break;

			case ACPI_TYPE_INTEGER:

				obj_desc->integer.value =
						(acpi_integer) ACPI_STRTOUL (init_val->val, NULL, 10);
				break;


			case ACPI_TYPE_STRING:

				/*
				 * Build an object around the static string
				 */
				obj_desc->string.length = ACPI_STRLEN (init_val->val);
				obj_desc->string.pointer = init_val->val;
				obj_desc->common.flags |= AOPOBJ_STATIC_POINTER;
				break;


			case ACPI_TYPE_MUTEX:

				obj_desc->mutex.sync_level =
						 (u16) ACPI_STRTOUL (init_val->val, NULL, 10);

				if (ACPI_STRCMP (init_val->name, "_GL_") == 0) {
					/*
					 * Create a counting semaphore for the
					 * global lock
					 */
					status = acpi_os_create_semaphore (ACPI_NO_UNIT_LIMIT,
							 1, &obj_desc->mutex.semaphore);
					if (ACPI_FAILURE (status)) {
						goto unlock_and_exit;
					}

					/*
					 * We just created the mutex for the
					 * global lock, save it
					 */
					acpi_gbl_global_lock_semaphore = obj_desc->mutex.semaphore;
				}
				else {
					/* Create a mutex */

					status = acpi_os_create_semaphore (1, 1,
							   &obj_desc->mutex.semaphore);
					if (ACPI_FAILURE (status)) {
						goto unlock_and_exit;
					}
				}
				break;


			default:
				ACPI_REPORT_ERROR (("Unsupported initial type value %X\n",
					init_val->type));
				acpi_ut_remove_reference (obj_desc);
				obj_desc = NULL;
				continue;
			}

			/* Store pointer to value descriptor in the Node */

			status = acpi_ns_attach_object (new_node, obj_desc, ACPI_GET_OBJECT_TYPE (obj_desc));

			/* Remove local reference to the object */

			acpi_ut_remove_reference (obj_desc);
		}
	}


unlock_and_exit:
	(void) acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_lookup
 *
 * PARAMETERS:  Prefix_node     - Search scope if name is not fully qualified
 *              Pathname        - Search pathname, in internal format
 *                                (as represented in the AML stream)
 *              Type            - Type associated with name
 *              Interpreter_mode - IMODE_LOAD_PASS2 => add name if not found
 *              Flags           - Flags describing the search restrictions
 *              Walk_state      - Current state of the walk
 *              Return_node     - Where the Node is placed (if found
 *                                or created successfully)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Find or enter the passed name in the name space.
 *              Log an error if name not found in Exec mode.
 *
 * MUTEX:       Assumes namespace is locked.
 *
 ******************************************************************************/

acpi_status
acpi_ns_lookup (
	acpi_generic_state      *scope_info,
	NATIVE_CHAR             *pathname,
	acpi_object_type        type,
	acpi_interpreter_mode   interpreter_mode,
	u32                     flags,
	acpi_walk_state         *walk_state,
	acpi_namespace_node     **return_node)
{
	acpi_status             status;
	NATIVE_CHAR             *path = pathname;
	acpi_namespace_node     *prefix_node;
	acpi_namespace_node     *current_node = NULL;
	acpi_namespace_node     *this_node = NULL;
	u32                     num_segments;
	acpi_name               simple_name;
	acpi_object_type        type_to_check_for;
	acpi_object_type        this_search_type;
	u32                     search_parent_flag = ACPI_NS_SEARCH_PARENT;
	u32                     local_flags = flags & ~(ACPI_NS_ERROR_IF_FOUND |
			   ACPI_NS_SEARCH_PARENT);


	ACPI_FUNCTION_TRACE ("Ns_lookup");


	if (!return_node) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	acpi_gbl_ns_lookup_count++;
	*return_node = ACPI_ENTRY_NOT_FOUND;

	if (!acpi_gbl_root_node) {
		return_ACPI_STATUS (AE_NO_NAMESPACE);
	}

	/*
	 * Get the prefix scope.
	 * A null scope means use the root scope
	 */
	if ((!scope_info) ||
		(!scope_info->scope.node)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
			"Null scope prefix, using root node (%p)\n",
			acpi_gbl_root_node));

		prefix_node = acpi_gbl_root_node;
	}
	else {
		prefix_node = scope_info->scope.node;
		if (ACPI_GET_DESCRIPTOR_TYPE (prefix_node) != ACPI_DESC_TYPE_NAMED) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "[%p] Not a namespace node\n",
				prefix_node));
			return_ACPI_STATUS (AE_AML_INTERNAL);
		}

		/*
		 * This node might not be a actual "scope" node (such as a
		 * Device/Method, etc.)  It could be a Package or other object node.
		 * Backup up the tree to find the containing scope node.
		 */
		while (!acpi_ns_opens_scope (prefix_node->type) &&
				prefix_node->type != ACPI_TYPE_ANY) {
			prefix_node = acpi_ns_get_parent_node (prefix_node);
		}
	}

	/*
	 * This check is explicitly split to relax the Type_to_check_for
	 * conditions for Bank_field_defn. Originally, both Bank_field_defn and
	 * Def_field_defn caused Type_to_check_for to be set to ACPI_TYPE_REGION,
	 * but the Bank_field_defn may also check for a Field definition as well
	 * as an Operation_region.
	 */
	if (INTERNAL_TYPE_FIELD_DEFN == type) {
		/* Def_field_defn defines fields in a Region */

		type_to_check_for = ACPI_TYPE_REGION;
	}
	else if (INTERNAL_TYPE_BANK_FIELD_DEFN == type) {
		/* Bank_field_defn defines data fields in a Field Object */

		type_to_check_for = ACPI_TYPE_ANY;
	}
	else {
		type_to_check_for = type;
	}

	/*
	 * Begin examination of the actual pathname
	 */
	if (!pathname) {
		/* A Null Name_path is allowed and refers to the root */

		num_segments = 0;
		this_node    = acpi_gbl_root_node;
		path     = "";

		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
			"Null Pathname (Zero segments), Flags=%X\n", flags));
	}
	else {
		/*
		 * Name pointer is valid (and must be in internal name format)
		 *
		 * Check for scope prefixes:
		 *
		 * As represented in the AML stream, a namepath consists of an
		 * optional scope prefix followed by a name segment part.
		 *
		 * If present, the scope prefix is either a Root Prefix (in
		 * which case the name is fully qualified), or one or more
		 * Parent Prefixes (in which case the name's scope is relative
		 * to the current scope).
		 */
		if (*path == (u8) AML_ROOT_PREFIX) {
			/* Pathname is fully qualified, start from the root */

			this_node = acpi_gbl_root_node;
			search_parent_flag = ACPI_NS_NO_UPSEARCH;

			/* Point to name segment part */

			path++;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Path is absolute from root [%p]\n", this_node));
		}
		else {
			/* Pathname is relative to current scope, start there */

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Searching relative to prefix scope [%p]\n",
				prefix_node));

			/*
			 * Handle multiple Parent Prefixes (carat) by just getting
			 * the parent node for each prefix instance.
			 */
			this_node = prefix_node;
			while (*path == (u8) AML_PARENT_PREFIX) {
				/* Name is fully qualified, no search rules apply */

				search_parent_flag = ACPI_NS_NO_UPSEARCH;
				/*
				 * Point past this prefix to the name segment
				 * part or the next Parent Prefix
				 */
				path++;

				/* Backup to the parent node */

				this_node = acpi_ns_get_parent_node (this_node);
				if (!this_node) {
					/* Current scope has no parent scope */

					ACPI_REPORT_ERROR (
						("ACPI path has too many parent prefixes (^) - reached beyond root node\n"));
					return_ACPI_STATUS (AE_NOT_FOUND);
				}
			}

			if (search_parent_flag == ACPI_NS_NO_UPSEARCH) {
				ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
					"Path is absolute with one or more carats\n"));
			}
		}

		/*
		 * Determine the number of ACPI name segments in this pathname.
		 *
		 * The segment part consists of either:
		 *  - A Null name segment (0)
		 *  - A Dual_name_prefix followed by two 4-byte name segments
		 *  - A Multi_name_prefix followed by a byte indicating the
		 *      number of segments and the segments themselves.
		 *  - A single 4-byte name segment
		 *
		 * Examine the name prefix opcode, if any, to determine the number of
		 * segments.
		 */
		switch (*path) {
		case 0:
			/*
			 * Null name after a root or parent prefixes. We already
			 * have the correct target node and there are no name segments.
			 */
			num_segments = 0;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Prefix-only Pathname (Zero name segments), Flags=%X\n", flags));
			break;

		case AML_DUAL_NAME_PREFIX:

			/* More than one Name_seg, search rules do not apply */

			search_parent_flag = ACPI_NS_NO_UPSEARCH;

			/* Two segments, point to first name segment */

			num_segments = 2;
			path++;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Dual Pathname (2 segments, Flags=%X)\n", flags));
			break;

		case AML_MULTI_NAME_PREFIX_OP:

			/* More than one Name_seg, search rules do not apply */

			search_parent_flag = ACPI_NS_NO_UPSEARCH;

			/* Extract segment count, point to first name segment */

			path++;
			num_segments = (u32) (u8) *path;
			path++;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Multi Pathname (%d Segments, Flags=%X) \n",
				num_segments, flags));
			break;

		default:
			/*
			 * Not a Null name, no Dual or Multi prefix, hence there is
			 * only one name segment and Pathname is already pointing to it.
			 */
			num_segments = 1;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Simple Pathname (1 segment, Flags=%X)\n", flags));
			break;
		}

		ACPI_DEBUG_EXEC (acpi_ns_print_pathname (num_segments, path));
	}


	/*
	 * Search namespace for each segment of the name.  Loop through and
	 * verify (or add to the namespace) each name segment.
	 *
	 * The object type is significant only at the last name
	 * segment.  (We don't care about the types along the path, only
	 * the type of the final target object.)
	 */
	this_search_type = ACPI_TYPE_ANY;
	current_node = this_node;
	while (num_segments && current_node) {
		num_segments--;
		if (!num_segments) {
			/*
			 * This is the last segment, enable typechecking
			 */
			this_search_type = type;

			/*
			 * Only allow automatic parent search (search rules) if the caller
			 * requested it AND we have a single, non-fully-qualified Name_seg
			 */
			if ((search_parent_flag != ACPI_NS_NO_UPSEARCH) &&
				(flags & ACPI_NS_SEARCH_PARENT)) {
				local_flags |= ACPI_NS_SEARCH_PARENT;
			}

			/* Set error flag according to caller */

			if (flags & ACPI_NS_ERROR_IF_FOUND) {
				local_flags |= ACPI_NS_ERROR_IF_FOUND;
			}
		}

		/* Extract one ACPI name from the front of the pathname */

		ACPI_MOVE_UNALIGNED32_TO_32 (&simple_name, path);

		/* Try to find the single (4 character) ACPI name */

		status = acpi_ns_search_and_enter (simple_name, walk_state, current_node,
				  interpreter_mode, this_search_type, local_flags, &this_node);
		if (ACPI_FAILURE (status)) {
			if (status == AE_NOT_FOUND) {
				/* Name not found in ACPI namespace */

				ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
					"Name [%4.4s] not found in scope [%4.4s] %p\n",
					(char *) &simple_name, (char *) &current_node->name,
					current_node));
			}

			return_ACPI_STATUS (status);
		}

		/*
		 * Sanity typecheck of the target object:
		 *
		 * If 1) This is the last segment (Num_segments == 0)
		 *    2) And we are looking for a specific type
		 *       (Not checking for TYPE_ANY)
		 *    3) Which is not an alias
		 *    4) Which is not a local type (TYPE_DEF_ANY)
		 *    5) Which is not a local type (TYPE_SCOPE)
		 *    6) Which is not a local type (TYPE_INDEX_FIELD_DEFN)
		 *    7) And the type of target object is known (not TYPE_ANY)
		 *    8) And target object does not match what we are looking for
		 *
		 * Then we have a type mismatch.  Just warn and ignore it.
		 */
		if ((num_segments       == 0)                               &&
			(type_to_check_for  != ACPI_TYPE_ANY)                   &&
			(type_to_check_for  != INTERNAL_TYPE_ALIAS)             &&
			(type_to_check_for  != INTERNAL_TYPE_DEF_ANY)           &&
			(type_to_check_for  != INTERNAL_TYPE_SCOPE)             &&
			(type_to_check_for  != INTERNAL_TYPE_INDEX_FIELD_DEFN)  &&
			(this_node->type    != ACPI_TYPE_ANY)                   &&
			(this_node->type    != type_to_check_for)) {
			/* Complain about a type mismatch */

			ACPI_REPORT_WARNING (
				("Ns_lookup: %4.4s, type %X, checking for type %X\n",
				(char *) &simple_name, this_node->type, type_to_check_for));
		}

		/*
		 * If this is the last name segment and we are not looking for a
		 * specific type, but the type of found object is known, use that type
		 * to see if it opens a scope.
		 */
		if ((num_segments == 0) && (type == ACPI_TYPE_ANY)) {
			type = this_node->type;
		}

		/* Point to next name segment and make this node current */

		path += ACPI_NAME_SIZE;
		current_node = this_node;
	}

	/*
	 * Always check if we need to open a new scope
	 */
	if (!(flags & ACPI_NS_DONT_OPEN_SCOPE) && (walk_state)) {
		/*
		 * If entry is a type which opens a scope, push the new scope on the
		 * scope stack.
		 */
		if (acpi_ns_opens_scope (type_to_check_for)) {
			status = acpi_ds_scope_stack_push (this_node, type, walk_state);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Setting current scope to [%4.4s] (%p)\n",
				this_node->name.ascii, this_node));
		}
	}

	*return_node = this_node;
	return_ACPI_STATUS (AE_OK);
}

