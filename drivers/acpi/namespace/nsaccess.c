/*******************************************************************************
 *
 * Module Name: nsaccess - Top-level functions for accessing ACPI namespace
 *              $Revision: 135 $
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
#include "acdispat.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsaccess")


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
	acpi_status             status = AE_OK;
	const predefined_names  *init_val = NULL;
	acpi_namespace_node     *new_node;
	acpi_operand_object     *obj_desc;


	FUNCTION_TRACE ("Ns_root_initialize");


	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

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

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Entering predefined entries into namespace\n"));

	for (init_val = acpi_gbl_pre_defined_names; init_val->name; init_val++) {
		status = acpi_ns_lookup (NULL, init_val->name, init_val->type,
				 IMODE_LOAD_PASS2, NS_NO_UPSEARCH,
				 NULL, &new_node);

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

			case ACPI_TYPE_INTEGER:

				obj_desc->integer.value =
						(acpi_integer) STRTOUL (init_val->val, NULL, 10);
				break;


			case ACPI_TYPE_STRING:

				/*
				 * Build an object around the static string
				 */
				obj_desc->string.length = STRLEN (init_val->val);
				obj_desc->string.pointer = init_val->val;
				obj_desc->common.flags |= AOPOBJ_STATIC_POINTER;
				break;


			case ACPI_TYPE_MUTEX:

				obj_desc->mutex.sync_level =
						 (u16) STRTOUL (init_val->val, NULL, 10);

				if (STRCMP (init_val->name, "_GL_") == 0) {
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
				REPORT_ERROR (("Unsupported initial type value %X\n",
					init_val->type));
				acpi_ut_remove_reference (obj_desc);
				obj_desc = NULL;
				continue;
			}

			/* Store pointer to value descriptor in the Node */

			acpi_ns_attach_object (new_node, obj_desc, obj_desc->common.type);

			/* Remove local reference to the object */

			acpi_ut_remove_reference (obj_desc);
		}
	}


unlock_and_exit:
	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
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
	acpi_object_type8       type,
	operating_mode          interpreter_mode,
	u32                     flags,
	acpi_walk_state         *walk_state,
	acpi_namespace_node     **return_node)
{
	acpi_status             status;
	acpi_namespace_node     *prefix_node;
	acpi_namespace_node     *current_node = NULL;
	acpi_namespace_node     *scope_to_push = NULL;
	acpi_namespace_node     *this_node = NULL;
	u32                     num_segments;
	acpi_name               simple_name;
	u8                      null_name_path = FALSE;
	acpi_object_type8       type_to_check_for;
	acpi_object_type8       this_search_type;
	u32                     local_flags = flags & ~NS_ERROR_IF_FOUND;

	DEBUG_EXEC              (u32 i;)


	FUNCTION_TRACE ("Ns_lookup");


	if (!return_node) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	acpi_gbl_ns_lookup_count++;

	*return_node = ENTRY_NOT_FOUND;


	if (!acpi_gbl_root_node) {
		return (AE_NO_NAMESPACE);
	}

	/*
	 * Get the prefix scope.
	 * A null scope means use the root scope
	 */
	if ((!scope_info) ||
		(!scope_info->scope.node)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Null scope prefix, using root node (%p)\n",
			acpi_gbl_root_node));

		prefix_node = acpi_gbl_root_node;
	}
	else {
		prefix_node = scope_info->scope.node;
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


	/* TBD: [Restructure] - Move the pathname stuff into a new procedure */

	/* Examine the name pointer */

	if (!pathname) {
		/*  8-12-98 ASL Grammar Update supports null Name_path  */

		null_name_path = TRUE;
		num_segments = 0;
		this_node = acpi_gbl_root_node;

		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
			"Null Pathname (Zero segments),  Flags=%x\n", flags));
	}

	else {
		/*
		 * Valid name pointer (Internal name format)
		 *
		 * Check for prefixes.  As represented in the AML stream, a
		 * Pathname consists of an optional scope prefix followed by
		 * a segment part.
		 *
		 * If present, the scope prefix is either a Root_prefix (in
		 * which case the name is fully qualified), or zero or more
		 * Parent_prefixes (in which case the name's scope is relative
		 * to the current scope).
		 *
		 * The segment part consists of either:
		 *  - A single 4-byte name segment, or
		 *  - A Dual_name_prefix followed by two 4-byte name segments, or
		 *  - A Multi_name_prefix_op, followed by a byte indicating the
		 *    number of segments and the segments themselves.
		 */
		if (*pathname == AML_ROOT_PREFIX) {
			/* Pathname is fully qualified, look in root name table */

			current_node = acpi_gbl_root_node;

			/* point to segment part */

			pathname++;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Searching from root [%p]\n",
				current_node));

			/* Direct reference to root, "\" */

			if (!(*pathname)) {
				this_node = acpi_gbl_root_node;
				goto check_for_new_scope_and_exit;
			}
		}

		else {
			/* Pathname is relative to current scope, start there */

			current_node = prefix_node;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "Searching relative to pfx scope [%p]\n",
				prefix_node));

			/*
			 * Handle up-prefix (carat).  More than one prefix
			 * is supported
			 */
			while (*pathname == AML_PARENT_PREFIX) {
				/* Point to segment part or next Parent_prefix */

				pathname++;

				/*  Backup to the parent's scope  */

				this_node = acpi_ns_get_parent_object (current_node);
				if (!this_node) {
					/* Current scope has no parent scope */

					REPORT_ERROR (
						("Too many parent prefixes (^) - reached root\n"));
					return_ACPI_STATUS (AE_NOT_FOUND);
				}

				current_node = this_node;
			}
		}


		/*
		 * Examine the name prefix opcode, if any,
		 * to determine the number of segments
		 */
		if (*pathname == AML_DUAL_NAME_PREFIX) {
			num_segments = 2;

			/* point to first segment */

			pathname++;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Dual Pathname (2 segments, Flags=%X)\n", flags));
		}

		else if (*pathname == AML_MULTI_NAME_PREFIX_OP) {
			num_segments = (u32)* (u8 *) ++pathname;

			/* point to first segment */

			pathname++;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Multi Pathname (%d Segments, Flags=%X) \n",
				num_segments, flags));
		}

		else {
			/*
			 * No Dual or Multi prefix, hence there is only one
			 * segment and Pathname is already pointing to it.
			 */
			num_segments = 1;

			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Simple Pathname (1 segment, Flags=%X)\n", flags));
		}

#ifdef ACPI_DEBUG

		/* TBD: [Restructure] Make this a procedure */

		/* Debug only: print the entire name that we are about to lookup */

		ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "["));

		for (i = 0; i < num_segments; i++) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_NAMES, "%4.4s/", (char*)&pathname[i * 4]));
		}
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_NAMES, "]\n"));
#endif
	}


	/*
	 * Search namespace for each segment of the name.
	 * Loop through and verify/add each name segment.
	 */
	while (num_segments-- && current_node) {
		/*
		 * Search for the current name segment under the current
		 * named object.  The Type is significant only at the last (topmost)
		 * level.  (We don't care about the types along the path, only
		 * the type of the final target object.)
		 */
		this_search_type = ACPI_TYPE_ANY;
		if (!num_segments) {
			this_search_type = type;
			local_flags = flags;
		}

		/* Pluck one ACPI name from the front of the pathname */

		MOVE_UNALIGNED32_TO_32 (&simple_name, pathname);

		/* Try to find the ACPI name */

		status = acpi_ns_search_and_enter (simple_name, walk_state,
				   current_node, interpreter_mode,
				   this_search_type, local_flags,
				   &this_node);

		if (ACPI_FAILURE (status)) {
			if (status == AE_NOT_FOUND) {
				/* Name not found in ACPI namespace  */

				ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
					"Name [%4.4s] not found in scope %p\n",
					(char*)&simple_name, current_node));
			}

			return_ACPI_STATUS (status);
		}


		/*
		 * If 1) This is the last segment (Num_segments == 0)
		 *    2) and looking for a specific type
		 *       (Not checking for TYPE_ANY)
		 *    3) Which is not an alias
		 *    4) which is not a local type (TYPE_DEF_ANY)
		 *    5) which is not a local type (TYPE_SCOPE)
		 *    6) which is not a local type (TYPE_INDEX_FIELD_DEFN)
		 *    7) and type of object is known (not TYPE_ANY)
		 *    8) and object does not match request
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

			REPORT_WARNING (
				("Ns_lookup: %4.4s, type %X, checking for type %X\n",
				(char*)&simple_name, this_node->type, type_to_check_for));
		}

		/*
		 * If this is the last name segment and we are not looking for a
		 * specific type, but the type of found object is known, use that type
		 * to see if it opens a scope.
		 */
		if ((0 == num_segments) && (ACPI_TYPE_ANY == type)) {
			type = this_node->type;
		}

		if ((num_segments || acpi_ns_opens_scope (type)) &&
			(this_node->child == NULL)) {
			/*
			 * More segments or the type implies enclosed scope,
			 * and the next scope has not been allocated.
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Load mode=%X  This_node=%p\n",
				interpreter_mode, this_node));
		}

		current_node = this_node;

		/* point to next name segment */

		pathname += ACPI_NAME_SIZE;
	}


	/*
	 * Always check if we need to open a new scope
	 */
check_for_new_scope_and_exit:

	if (!(flags & NS_DONT_OPEN_SCOPE) && (walk_state)) {
		/*
		 * If entry is a type which opens a scope,
		 * push the new scope on the scope stack.
		 */
		if (acpi_ns_opens_scope (type_to_check_for)) {
			/*  8-12-98 ASL Grammar Update supports null Name_path  */

			if (null_name_path) {
				/* TBD: [Investigate] - is this the correct thing to do? */

				scope_to_push = NULL;
			}
			else {
				scope_to_push = this_node;
			}

			status = acpi_ds_scope_stack_push (scope_to_push, type,
					   walk_state);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Set global scope to %p\n", scope_to_push));
		}
	}

	*return_node = this_node;
	return_ACPI_STATUS (AE_OK);
}

