
/******************************************************************************
 *
 * Module Name: exmutex - ASL Mutex Acquire/Release functions
 *              $Revision: 7 $
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
#include "achware.h"
#include "acevents.h"

#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exmutex")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_unlink_mutex
 *
 * PARAMETERS:  *Obj_desc           - The mutex to be unlinked
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Remove a mutex from the "Acquired_mutex" list
 *
 ******************************************************************************/

void
acpi_ex_unlink_mutex (
	acpi_operand_object     *obj_desc)
{

	if (obj_desc->mutex.next) {
		(obj_desc->mutex.next)->mutex.prev = obj_desc->mutex.prev;
	}
	if (obj_desc->mutex.prev) {
		(obj_desc->mutex.prev)->mutex.next = obj_desc->mutex.next;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_link_mutex
 *
 * PARAMETERS:  *Obj_desc           - The mutex to be linked
 *              *List_head          - head of the "Acquired_mutex" list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Add a mutex to the "Acquired_mutex" list for this walk
 *
 ******************************************************************************/

void
acpi_ex_link_mutex (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     *list_head)
{

	/* This object will be the first object in the list */

	obj_desc->mutex.prev = list_head;
	obj_desc->mutex.next = list_head->mutex.next;

	/* Update old first object to point back to this object */

	if (list_head->mutex.next) {
		(list_head->mutex.next)->mutex.prev = obj_desc;
	}

	/* Update list head */

	list_head->mutex.next = obj_desc;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_acquire_mutex
 *
 * PARAMETERS:  *Time_desc          - The 'time to delay' object descriptor
 *              *Obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Acquire an AML mutex
 *
 ******************************************************************************/

acpi_status
acpi_ex_acquire_mutex (
	acpi_operand_object     *time_desc,
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ex_acquire_mutex", obj_desc);

	if (!obj_desc) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/*
	 * Current Sync must be less than or equal to the sync level of the
	 * mutex.  This mechanism provides some deadlock prevention
	 */
	if (walk_state->current_sync_level > obj_desc->mutex.sync_level) {
		return_ACPI_STATUS (AE_AML_MUTEX_ORDER);
	}

	/*
	 * If the mutex is already owned by this thread,
	 * just increment the acquisition depth
	 */
	if (obj_desc->mutex.owner == walk_state) {
		obj_desc->mutex.acquisition_depth++;
		return_ACPI_STATUS (AE_OK);
	}

	/* Acquire the mutex, wait if necessary */

	status = acpi_ex_system_acquire_mutex (time_desc, obj_desc);
	if (ACPI_FAILURE (status)) {
		/* Includes failure from a timeout on Time_desc */

		return_ACPI_STATUS (status);
	}

	/* Have the mutex, update mutex and walk info */

	obj_desc->mutex.owner = walk_state;
	obj_desc->mutex.acquisition_depth = 1;
	walk_state->current_sync_level = obj_desc->mutex.sync_level;

	/* Link the mutex to the walk state for force-unlock at method exit */

	acpi_ex_link_mutex (obj_desc, (acpi_operand_object *)
			 &(walk_state->walk_list->acquired_mutex_list));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_release_mutex
 *
 * PARAMETERS:  *Obj_desc           - The object descriptor for this op
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release a previously acquired Mutex.
 *
 ******************************************************************************/

acpi_status
acpi_ex_release_mutex (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status;


	FUNCTION_TRACE ("Ex_release_mutex");


	if (!obj_desc) {
		return_ACPI_STATUS (AE_BAD_PARAMETER);
	}

	/* The mutex must have been previously acquired in order to release it */

	if (!obj_desc->mutex.owner) {
		return_ACPI_STATUS (AE_AML_MUTEX_NOT_ACQUIRED);
	}

	/* The Mutex is owned, but this thread must be the owner */

	if (obj_desc->mutex.owner != walk_state) {
		return_ACPI_STATUS (AE_AML_NOT_OWNER);
	}

	/*
	 * The sync level of the mutex must be less than or
	 * equal to the current sync level
	 */
	if (obj_desc->mutex.sync_level > walk_state->current_sync_level) {
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


	/* Release the mutex */

	status = acpi_ex_system_release_mutex (obj_desc);

	/* Update the mutex and walk state */

	obj_desc->mutex.owner = NULL;
	walk_state->current_sync_level = obj_desc->mutex.sync_level;

	/* Unlink the mutex from the owner's list */

	acpi_ex_unlink_mutex (obj_desc);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_release_all_mutexes
 *
 * PARAMETERS:  *Mutex_list           - Head of the mutex list
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release all mutexes in the list
 *
 ******************************************************************************/

acpi_status
acpi_ex_release_all_mutexes (
	acpi_operand_object     *list_head)
{
	acpi_operand_object     *next = list_head->mutex.next;
	acpi_operand_object     *this;


	FUNCTION_ENTRY ();


	/*
	 * Traverse the list of owned mutexes, releasing each one.
	 */
	while (next) {
		this = next;
		next = this->mutex.next;

		/* Mark mutex un-owned */

		this->mutex.owner = NULL;
		this->mutex.prev = NULL;
		this->mutex.next = NULL;
		this->mutex.acquisition_depth = 0;

		 /* Release the mutex */

		acpi_ex_system_release_mutex (this);
	}

	return (AE_OK);
}


