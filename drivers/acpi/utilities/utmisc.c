/*******************************************************************************
 *
 * Module Name: utmisc - common utility procedures
 *              $Revision: 52 $
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
#include "acevents.h"
#include "achware.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acdebug.h"


#define _COMPONENT          ACPI_UTILITIES
	 MODULE_NAME         ("utmisc")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_valid_acpi_name
 *
 * PARAMETERS:  Character           - The character to be examined
 *
 * RETURN:      1 if Character may appear in a name, else 0
 *
 * DESCRIPTION: Check for a valid ACPI name.  Each character must be one of:
 *              1) Upper case alpha
 *              2) numeric
 *              3) underscore
 *
 ******************************************************************************/

u8
acpi_ut_valid_acpi_name (
	u32                     name)
{
	NATIVE_CHAR             *name_ptr = (NATIVE_CHAR *) &name;
	u32                     i;


	FUNCTION_ENTRY ();


	for (i = 0; i < ACPI_NAME_SIZE; i++) {
		if (!((name_ptr[i] == '_') ||
			  (name_ptr[i] >= 'A' && name_ptr[i] <= 'Z') ||
			  (name_ptr[i] >= '0' && name_ptr[i] <= '9'))) {
			return (FALSE);
		}
	}

	return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_valid_acpi_character
 *
 * PARAMETERS:  Character           - The character to be examined
 *
 * RETURN:      1 if Character may appear in a name, else 0
 *
 * DESCRIPTION: Check for a printable character
 *
 ******************************************************************************/

u8
acpi_ut_valid_acpi_character (
	NATIVE_CHAR             character)
{

	FUNCTION_ENTRY ();

	return ((u8)   ((character == '_') ||
			   (character >= 'A' && character <= 'Z') ||
			   (character >= '0' && character <= '9')));
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_strupr
 *
 * PARAMETERS:  Src_string      - The source string to convert to
 *
 * RETURN:      Src_string
 *
 * DESCRIPTION: Convert string to uppercase
 *
 ******************************************************************************/

NATIVE_CHAR *
acpi_ut_strupr (
	NATIVE_CHAR             *src_string)
{
	NATIVE_CHAR             *string;


	FUNCTION_ENTRY ();


	/* Walk entire string, uppercasing the letters */

	for (string = src_string; *string; ) {
		*string = (char) TOUPPER (*string);
		string++;
	}


	return (src_string);
}

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_mutex_initialize
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create the system mutex objects.
 *
 ******************************************************************************/

acpi_status
acpi_ut_mutex_initialize (
	void)
{
	u32                     i;
	acpi_status             status;


	FUNCTION_TRACE ("Ut_mutex_initialize");


	/*
	 * Create each of the predefined mutex objects
	 */
	for (i = 0; i < NUM_MTX; i++) {
		status = acpi_ut_create_mutex (i);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_mutex_terminate
 *
 * PARAMETERS:  None.
 *
 * RETURN:      None.
 *
 * DESCRIPTION: Delete all of the system mutex objects.
 *
 ******************************************************************************/

void
acpi_ut_mutex_terminate (
	void)
{
	u32                     i;


	FUNCTION_TRACE ("Ut_mutex_terminate");


	/*
	 * Delete each predefined mutex object
	 */
	for (i = 0; i < NUM_MTX; i++) {
		acpi_ut_delete_mutex (i);
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be created
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE_U32 ("Ut_create_mutex", mutex_id);


	if (mutex_id > MAX_MTX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	if (!acpi_gbl_acpi_mutex_info[mutex_id].mutex) {
		status = acpi_os_create_semaphore (1, 1,
				  &acpi_gbl_acpi_mutex_info[mutex_id].mutex);
		acpi_gbl_acpi_mutex_info[mutex_id].owner_id = ACPI_MUTEX_NOT_ACQUIRED;
		acpi_gbl_acpi_mutex_info[mutex_id].use_count = 0;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Delete a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_delete_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	acpi_status             status;


	FUNCTION_TRACE_U32 ("Ut_delete_mutex", mutex_id);


	if (mutex_id > MAX_MTX) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}


	status = acpi_os_delete_semaphore (acpi_gbl_acpi_mutex_info[mutex_id].mutex);

	acpi_gbl_acpi_mutex_info[mutex_id].mutex = NULL;
	acpi_gbl_acpi_mutex_info[mutex_id].owner_id = ACPI_MUTEX_NOT_ACQUIRED;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_acquire_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be acquired
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_acquire_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	acpi_status             status;
	u32                     i;
	u32                     this_thread_id;


	PROC_NAME ("Ut_acquire_mutex");


	if (mutex_id > MAX_MTX) {
		return (AE_BAD_PARAMETER);
	}


	this_thread_id = acpi_os_get_thread_id ();

	/*
	 * Deadlock prevention.  Check if this thread owns any mutexes of value
	 * greater than or equal to this one.  If so, the thread has violated
	 * the mutex ordering rule.  This indicates a coding error somewhere in
	 * the ACPI subsystem code.
	 */
	for (i = mutex_id; i < MAX_MTX; i++) {
		if (acpi_gbl_acpi_mutex_info[i].owner_id == this_thread_id) {
			if (i == mutex_id) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
						"Mutex [%s] already acquired by this thread [%X]\n",
						acpi_ut_get_mutex_name (mutex_id), this_thread_id));

				return (AE_ALREADY_ACQUIRED);
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Invalid acquire order: Thread %X owns [%s], wants [%s]\n",
					this_thread_id, acpi_ut_get_mutex_name (i),
					acpi_ut_get_mutex_name (mutex_id)));

			return (AE_ACQUIRE_DEADLOCK);
		}
	}


	ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX,
			 "Thread %X attempting to acquire Mutex [%s]\n",
			 this_thread_id, acpi_ut_get_mutex_name (mutex_id)));

	status = acpi_os_wait_semaphore (acpi_gbl_acpi_mutex_info[mutex_id].mutex,
			   1, WAIT_FOREVER);

	if (ACPI_SUCCESS (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX, "Thread %X acquired Mutex [%s]\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id)));

		acpi_gbl_acpi_mutex_info[mutex_id].use_count++;
		acpi_gbl_acpi_mutex_info[mutex_id].owner_id = this_thread_id;
	}

	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Thread %X could not acquire Mutex [%s] %s\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id),
				 acpi_format_exception (status)));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_release_mutex
 *
 * PARAMETERS:  Mutex_iD        - ID of the mutex to be released
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release a mutex object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_release_mutex (
	ACPI_MUTEX_HANDLE       mutex_id)
{
	acpi_status             status;
	u32                     i;
	u32                     this_thread_id;


	PROC_NAME ("Ut_release_mutex");


	this_thread_id = acpi_os_get_thread_id ();
	ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX,
		"Thread %X releasing Mutex [%s]\n", this_thread_id,
		acpi_ut_get_mutex_name (mutex_id)));

	if (mutex_id > MAX_MTX) {
		return (AE_BAD_PARAMETER);
	}


	/*
	 * Mutex must be acquired in order to release it!
	 */
	if (acpi_gbl_acpi_mutex_info[mutex_id].owner_id == ACPI_MUTEX_NOT_ACQUIRED) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Mutex [%s] is not acquired, cannot release\n",
				acpi_ut_get_mutex_name (mutex_id)));

		return (AE_NOT_ACQUIRED);
	}


	/*
	 * Deadlock prevention.  Check if this thread owns any mutexes of value
	 * greater than this one.  If so, the thread has violated the mutex
	 * ordering rule.  This indicates a coding error somewhere in
	 * the ACPI subsystem code.
	 */
	for (i = mutex_id; i < MAX_MTX; i++) {
		if (acpi_gbl_acpi_mutex_info[i].owner_id == this_thread_id) {
			if (i == mutex_id) {
				continue;
			}

			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Invalid release order: owns [%s], releasing [%s]\n",
					acpi_ut_get_mutex_name (i), acpi_ut_get_mutex_name (mutex_id)));

			return (AE_RELEASE_DEADLOCK);
		}
	}


	/* Mark unlocked FIRST */

	acpi_gbl_acpi_mutex_info[mutex_id].owner_id = ACPI_MUTEX_NOT_ACQUIRED;

	status = acpi_os_signal_semaphore (acpi_gbl_acpi_mutex_info[mutex_id].mutex, 1);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Thread %X could not release Mutex [%s] %s\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id),
				 acpi_format_exception (status)));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_MUTEX, "Thread %X released Mutex [%s]\n",
				 this_thread_id, acpi_ut_get_mutex_name (mutex_id)));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_update_state_and_push
 *
 * PARAMETERS:  *Object         - Object to be added to the new state
 *              Action          - Increment/Decrement
 *              State_list      - List the state will be added to
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new state and push it
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_update_state_and_push (
	acpi_operand_object     *object,
	u16                     action,
	acpi_generic_state      **state_list)
{
	acpi_generic_state       *state;


	FUNCTION_ENTRY ();


	/* Ignore null objects; these are expected */

	if (!object) {
		return (AE_OK);
	}

	state = acpi_ut_create_update_state (object, action);
	if (!state) {
		return (AE_NO_MEMORY);
	}


	acpi_ut_push_generic_state (state_list, state);
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_pkg_state_and_push
 *
 * PARAMETERS:  *Object         - Object to be added to the new state
 *              Action          - Increment/Decrement
 *              State_list      - List the state will be added to
 *
 * RETURN:      None
 *
 * DESCRIPTION: Create a new state and push it
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_pkg_state_and_push (
	void                    *internal_object,
	void                    *external_object,
	u16                     index,
	acpi_generic_state      **state_list)
{
	acpi_generic_state       *state;


	FUNCTION_ENTRY ();


	state = acpi_ut_create_pkg_state (internal_object, external_object, index);
	if (!state) {
		return (AE_NO_MEMORY);
	}


	acpi_ut_push_generic_state (state_list, state);
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_push_generic_state
 *
 * PARAMETERS:  List_head           - Head of the state stack
 *              State               - State object to push
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Push a state object onto a state stack
 *
 ******************************************************************************/

void
acpi_ut_push_generic_state (
	acpi_generic_state      **list_head,
	acpi_generic_state      *state)
{
	FUNCTION_TRACE ("Ut_push_generic_state");


	/* Push the state object onto the front of the list (stack) */

	state->common.next = *list_head;
	*list_head = state;

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_pop_generic_state
 *
 * PARAMETERS:  List_head           - Head of the state stack
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Pop a state object from a state stack
 *
 ******************************************************************************/

acpi_generic_state *
acpi_ut_pop_generic_state (
	acpi_generic_state      **list_head)
{
	acpi_generic_state      *state;


	FUNCTION_TRACE ("Ut_pop_generic_state");


	/* Remove the state object at the head of the list (stack) */

	state = *list_head;
	if (state) {
		/* Update the list head */

		*list_head = state->common.next;
	}

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_generic_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a generic state object.  Attempt to obtain one from
 *              the global state cache;  If none available, create a new one.
 *
 ******************************************************************************/

acpi_generic_state *
acpi_ut_create_generic_state (void)
{
	acpi_generic_state      *state;


	FUNCTION_ENTRY ();


	state = acpi_ut_acquire_from_cache (ACPI_MEM_LIST_STATE);

	/* Initialize */

	if (state) {
		state->common.data_type = ACPI_DESC_TYPE_STATE;
	}

	return (state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_update_state
 *
 * PARAMETERS:  Object              - Initial Object to be installed in the
 *                                    state
 *              Action              - Update action to be performed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create an "Update State" - a flavor of the generic state used
 *              to update reference counts and delete complex objects such
 *              as packages.
 *
 ******************************************************************************/

acpi_generic_state *
acpi_ut_create_update_state (
	acpi_operand_object     *object,
	u16                     action)
{
	acpi_generic_state      *state;


	FUNCTION_TRACE_PTR ("Ut_create_update_state", object);


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return (NULL);
	}

	/* Init fields specific to the update struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_UPDATE;
	state->update.object = object;
	state->update.value  = action;

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_pkg_state
 *
 * PARAMETERS:  Object              - Initial Object to be installed in the
 *                                    state
 *              Action              - Update action to be performed
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a "Package State"
 *
 ******************************************************************************/

acpi_generic_state *
acpi_ut_create_pkg_state (
	void                    *internal_object,
	void                    *external_object,
	u16                     index)
{
	acpi_generic_state      *state;


	FUNCTION_TRACE_PTR ("Ut_create_pkg_state", internal_object);


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return (NULL);
	}

	/* Init fields specific to the update struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_PACKAGE;
	state->pkg.source_object = (acpi_operand_object *) internal_object;
	state->pkg.dest_object  = external_object;
	state->pkg.index        = index;
	state->pkg.num_packages = 1;

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_create_control_state
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a "Control State" - a flavor of the generic state used
 *              to support nested IF/WHILE constructs in the AML.
 *
 ******************************************************************************/

acpi_generic_state *
acpi_ut_create_control_state (
	void)
{
	acpi_generic_state      *state;


	FUNCTION_TRACE ("Ut_create_control_state");


	/* Create the generic state object */

	state = acpi_ut_create_generic_state ();
	if (!state) {
		return (NULL);
	}


	/* Init fields specific to the control struct */

	state->common.data_type = ACPI_DESC_TYPE_STATE_CONTROL;
	state->common.state     = CONTROL_CONDITIONAL_EXECUTING;

	return_PTR (state);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_generic_state
 *
 * PARAMETERS:  State               - The state object to be deleted
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Put a state object back into the global state cache.  The object
 *              is not actually freed at this time.
 *
 ******************************************************************************/

void
acpi_ut_delete_generic_state (
	acpi_generic_state      *state)
{
	FUNCTION_TRACE ("Ut_delete_generic_state");


	acpi_ut_release_to_cache (ACPI_MEM_LIST_STATE, state);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_generic_state_cache
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Purge the global state object cache.  Used during subsystem
 *              termination.
 *
 ******************************************************************************/

void
acpi_ut_delete_generic_state_cache (
	void)
{
	FUNCTION_TRACE ("Ut_delete_generic_state_cache");


	acpi_ut_delete_generic_cache (ACPI_MEM_LIST_STATE);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_resolve_package_references
 *
 * PARAMETERS:  Obj_desc        - The Package object on which to resolve refs
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk through a package and turn internal references into values
 *
 ******************************************************************************/

acpi_status
acpi_ut_resolve_package_references (
	acpi_operand_object     *obj_desc)
{
	u32                     count;
	acpi_operand_object     *sub_object;


	FUNCTION_TRACE ("Ut_resolve_package_references");


	if (obj_desc->common.type != ACPI_TYPE_PACKAGE) {
		/* The object must be a package */

		REPORT_ERROR (("Must resolve Package Refs on a Package\n"));
		return_ACPI_STATUS(AE_ERROR);
	}

	/*
	 * TBD: what about nested packages? */

	for (count = 0; count < obj_desc->package.count; count++) {
		sub_object = obj_desc->package.elements[count];

		if (sub_object->common.type == INTERNAL_TYPE_REFERENCE) {
			if (sub_object->reference.opcode == AML_ZERO_OP) {
				sub_object->common.type = ACPI_TYPE_INTEGER;
				sub_object->integer.value = 0;
			}

			else if (sub_object->reference.opcode == AML_ONE_OP) {
				sub_object->common.type = ACPI_TYPE_INTEGER;
				sub_object->integer.value = 1;
			}

			else if (sub_object->reference.opcode == AML_ONES_OP) {
				sub_object->common.type = ACPI_TYPE_INTEGER;
				sub_object->integer.value = ACPI_INTEGER_MAX;
			}
		}
	}

	return_ACPI_STATUS(AE_OK);
}

#ifdef ACPI_DEBUG

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_display_init_pathname
 *
 * PARAMETERS:  Obj_handle          - Handle whose pathname will be displayed
 *              Path                - Additional path string to be appended
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Display full pathnbame of an object, DEBUG ONLY
 *
 ******************************************************************************/

void
acpi_ut_display_init_pathname (
	acpi_handle             obj_handle,
	char                    *path)
{
	acpi_status             status;
	u32                     length = 128;
	char                    buffer[128];


	PROC_NAME ("Ut_display_init_pathname");


	status = acpi_ns_handle_to_pathname (obj_handle, &length, buffer);
	if (ACPI_SUCCESS (status)) {
		if (path) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "%s.%s\n", buffer, path));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_INIT, "%s\n", buffer));
		}
	}
}
#endif

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_walk_package_tree
 *
 * PARAMETERS:  Obj_desc        - The Package object on which to resolve refs
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk through a package
 *
 ******************************************************************************/

acpi_status
acpi_ut_walk_package_tree (
	acpi_operand_object     *source_object,
	void                    *target_object,
	ACPI_PKG_CALLBACK       walk_callback,
	void                    *context)
{
	acpi_status             status = AE_OK;
	acpi_generic_state      *state_list = NULL;
	acpi_generic_state      *state;
	u32                     this_index;
	acpi_operand_object     *this_source_obj;


	FUNCTION_TRACE ("Ut_walk_package_tree");


	state = acpi_ut_create_pkg_state (source_object, target_object, 0);
	if (!state) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	while (state) {
		this_index    = state->pkg.index;
		this_source_obj = (acpi_operand_object *)
				  state->pkg.source_object->package.elements[this_index];

		/*
		 * Check for
		 * 1) An uninitialized package element.  It is completely
		 *      legal to declare a package and leave it uninitialized
		 * 2) Not an internal object - can be a namespace node instead
		 * 3) Any type other than a package.  Packages are handled in else
		 *      case below.
		 */
		if ((!this_source_obj) ||
			(!VALID_DESCRIPTOR_TYPE (
					this_source_obj, ACPI_DESC_TYPE_INTERNAL)) ||
			(!IS_THIS_OBJECT_TYPE (
					this_source_obj, ACPI_TYPE_PACKAGE))) {

			status = walk_callback (ACPI_COPY_TYPE_SIMPLE, this_source_obj,
					 state, context);
			if (ACPI_FAILURE (status)) {
				/* TBD: must delete package created up to this point */

				return_ACPI_STATUS (status);
			}

			state->pkg.index++;
			while (state->pkg.index >= state->pkg.source_object->package.count) {
				/*
				 * We've handled all of the objects at this level,  This means
				 * that we have just completed a package.  That package may
				 * have contained one or more packages itself.
				 *
				 * Delete this state and pop the previous state (package).
				 */
				acpi_ut_delete_generic_state (state);
				state = acpi_ut_pop_generic_state (&state_list);


				/* Finished when there are no more states */

				if (!state) {
					/*
					 * We have handled all of the objects in the top level
					 * package just add the length of the package objects
					 * and exit
					 */
					return_ACPI_STATUS (AE_OK);
				}

				/*
				 * Go back up a level and move the index past the just
				 * completed package object.
				 */
				state->pkg.index++;
			}
		}

		else {
			/* This is a sub-object of type package */

			status = walk_callback (ACPI_COPY_TYPE_PACKAGE, this_source_obj,
					  state, context);
			if (ACPI_FAILURE (status)) {
				/* TBD: must delete package created up to this point */

				return_ACPI_STATUS (status);
			}


			/*
			 * The callback above returned a new target package object.
			 */

			/*
			 * Push the current state and create a new one
			 */
			acpi_ut_push_generic_state (&state_list, state);
			state = acpi_ut_create_pkg_state (this_source_obj,
					   state->pkg.this_target_obj, 0);
			if (!state) {
				/* TBD: must delete package created up to this point */

				return_ACPI_STATUS (AE_NO_MEMORY);
			}
		}
	}

	/* We should never get here */

	return (AE_AML_INTERNAL);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_report_error
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print error message
 *
 ******************************************************************************/

void
acpi_ut_report_error (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id)
{


	acpi_os_printf ("%8s-%04d: *** Error: ", module_name, line_number);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_report_warning
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print warning message
 *
 ******************************************************************************/

void
acpi_ut_report_warning (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id)
{

	acpi_os_printf ("%8s-%04d: *** Warning: ", module_name, line_number);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_report_info
 *
 * PARAMETERS:  Module_name         - Caller's module name (for error output)
 *              Line_number         - Caller's line number (for error output)
 *              Component_id        - Caller's component ID (for error output)
 *              Message             - Error message to use on failure
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print information message
 *
 ******************************************************************************/

void
acpi_ut_report_info (
	NATIVE_CHAR             *module_name,
	u32                     line_number,
	u32                     component_id)
{

	acpi_os_printf ("%8s-%04d: *** Info: ", module_name, line_number);
}


