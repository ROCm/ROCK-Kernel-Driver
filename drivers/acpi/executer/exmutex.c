
/******************************************************************************
 *
 * Module Name: exmutex - ASL Mutex Acquire/Release functions
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


#include <acpi/acpi.h>
#include <acpi/acinterp.h>

#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exmutex")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_unlink_mutex
 *
 * PARAMETERS:  *obj_desc           - The mutex to be unlinked
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a mutex from the "acquired_mutex" list
 *
 ******************************************************************************/

void
acpi_ex_unlink_mutex (
	union acpi_operand_object       *obj_desc)
{
	struct acpi_thread_state        *thread = obj_desc->mutex.owner_thread;


	if (!thread) {
		return;
	}

	if (obj_desc->mutex.next) {
		(obj_desc->mutex.next)->mutex.prev = obj_desc->mutex.prev;
	}

	if (obj_desc->mutex.prev) {
		(obj_desc->mutex.prev)->mutex.next = obj_desc->mutex.next;
	}
	else {
		thread->acquired_mutex_list = obj_desc->mutex.next;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_link_mutex
 *
 * PARAMETERS:  *obj_desc           - The mutex to be linked
 *              *list_head          - head of the "acquired_mutex" list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a mutex to the "acquired_mutex" list for this walk
 *
 ******************************************************************************/

void
acpi_ex_link_mutex (
	union acpi_operand_object       *obj_desc,
	struct acpi_thread_state        *thread)
{
	union acpi_operand_object       *list_head;


	list_head = thread->acquired_mutex_list;

	/* This object will be the first object in the list */

	obj_desc->mutex.prev = NULL;
	obj_desc->mutex.next = list_head;

	/* Update old first object to point back to this object */

	if (list_head) {
		list_head->mutex.prev = obj_desc;
	}

	/* Update list head */

	thread->acquired_mutex_list = obj_desc;
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_acquire_mutex
 *
 * PARAMETERS:  *time_desc          - The 'time to delay' object descriptor
 *              *obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire an AML mutex
 *
 ******************************************************************************/

acpi_status
acpi_ex_acquire_mutex (
	union acpi_operand_object       *time_desc,
	union acpi_operand_object       *obj_desc,
	struct acpi_walk_state          *walk_state)
{
	acpi_status                     status;


	ACPI_FUNCTION_TRACE_PTR ("ex_acquire_mutex", obj_desc);


	if (!obj_desc) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* Sanity check -- we must have a valid thread ID */

	if (!walk_state->thread) {
		ACPI_REPORT_ERROR (("Cannot acquire Mutex [%4.4s], null thread info\n",
				obj_desc->mutex.node->name.ascii));
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}

	/*
	 * Current Sync must be less than or equal to the sync level of the
	 * mutex.  This mechanism provides some deadlock prevention
	 */
	if (walk_state->thread->current_sync_level > obj_desc->mutex.sync_level) {
		ACPI_REPORT_ERROR (("Cannot acquire Mutex [%4.4s], incorrect sync_level\n",
				obj_desc->mutex.node->name.ascii));
		return_ACPI_STATUS (AE_AML_MUTEX_ORDER);
	}

	/*
	 * Support for multiple acquires by the owning thread
	 */

	if ((obj_desc->mutex.owner_thread) &&
		(obj_desc->mutex.owner_thread->thread_id == walk_state->thread->thread_id)) {
		/*
		 * The mutex is already owned by this thread,
		 * just increment the acquisition depth
		 */
		obj_desc->mutex.acquisition_depth++;
		return_ACPI_STATUS (AE_OK);
	}

	/* Acquire the mutex, wait if necessary */

	status = acpi_ex_system_acquire_mutex (time_desc, obj_desc);
	if (ACPI_FAILURE (status)) {
		/* Includes failure from a timeout on time_desc */

		return_ACPI_STATUS (status);
	}

	/* Have the mutex, update mutex and walk info */

	obj_desc->mutex.owner_thread    = walk_state->thread;
	obj_desc->mutex.acquisition_depth = 1;

	walk_state->thread->current_sync_level = obj_desc->mutex.sync_level;

	/* Link the mutex to the current thread for force-unlock at method exit */

	acpi_ex_link_mutex (obj_desc, walk_state->thread);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_release_mutex
 *
 * PARAMETERS:  *obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release a previously acquired Mutex.
 *
 ******************************************************************************/

acpi_status
acpi_ex_release_mutex (
	union acpi_operand_object       *obj_desc,
	struct acpi_walk_state          *walk_state)
{
	acpi_status                     status;


	ACPI_FUNCTION_TRACE ("ex_release_mutex");


	if (!obj_desc) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* The mutex must have been previously acquired in order to release it */

	if (!obj_desc->mutex.owner_thread) {
		ACPI_REPORT_ERROR (("Cannot release Mutex [%4.4s], not acquired\n",
				obj_desc->mutex.node->name.ascii));
		return_ACPI_STATUS (AE_AML_MUTEX_NOT_ACQUIRED);
	}

	/* Sanity check -- we must have a valid thread ID */

	if (!walk_state->thread) {
		ACPI_REPORT_ERROR (("Cannot release Mutex [%4.4s], null thread info\n",
				obj_desc->mutex.node->name.ascii));
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}

	/* The Mutex is owned, but this thread must be the owner */

	if (obj_desc->mutex.owner_thread->thread_id != walk_state->thread->thread_id) {
		ACPI_REPORT_ERROR ((
			"Thread %X cannot release Mutex [%4.4s] acquired by thread %X\n",
			walk_state->thread->thread_id,
			obj_desc->mutex.node->name.ascii,
			obj_desc->mutex.owner_thread->thread_id));
		return_ACPI_STATUS (AE_AML_NOT_OWNER);
	}

	/*
	 * The sync level of the mutex must be less than or
	 * equal to the current sync level
	 */
	if (obj_desc->mutex.sync_level > walk_state->thread->current_sync_level) {
		ACPI_REPORT_ERROR (("Cannot release Mutex [%4.4s], incorrect sync_level\n",
				obj_desc->mutex.node->name.ascii));
		return_ACPI_STATUS (AE_AML_MUTEX_ORDER);
	}

	/*
	 * Match multiple Acquires with multiple Releases
	 */
	obj_desc->mutex.acquisition_depth--;
	if (obj_desc->mutex.acquisition_depth != 0) {
		/* Just decrement the depth and return */

		return_ACPI_STATUS (AE_OK);
	}

	/* Unlink the mutex from the owner's list */

	acpi_ex_unlink_mutex (obj_desc);

	/* Release the mutex */

	status = acpi_ex_system_release_mutex (obj_desc);

	/* Update the mutex and walk state */

	obj_desc->mutex.owner_thread = NULL;
	walk_state->thread->current_sync_level = obj_desc->mutex.sync_level;

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_release_all_mutexes
 *
 * PARAMETERS:  *mutex_list           - Head of the mutex list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release all mutexes in the list
 *
 ******************************************************************************/

void
acpi_ex_release_all_mutexes (
	struct acpi_thread_state        *thread)
{
	union acpi_operand_object       *next = thread->acquired_mutex_list;
	union acpi_operand_object       *this;
	acpi_status                     status;


	ACPI_FUNCTION_ENTRY ();


	/*
	 * Traverse the list of owned mutexes, releasing each one.
	 */
	while (next) {
		this = next;
		next = this->mutex.next;

		this->mutex.acquisition_depth = 1;
		this->mutex.prev             = NULL;
		this->mutex.next             = NULL;

		 /* Release the mutex */

		status = acpi_ex_system_release_mutex (this);
		if (ACPI_FAILURE (status)) {
			continue;
		}

		/* Mark mutex unowned */

		this->mutex.owner_thread     = NULL;
	}
}


