/*******************************************************************************
 *
 * Module Name: utdelete - object deletion and reference count utilities
 *              $Revision: 81 $
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
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"
#include "acparser.h"

#define _COMPONENT          ACPI_UTILITIES
	 MODULE_NAME         ("utdelete")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_internal_obj
 *
 * PARAMETERS:  *Object        - Pointer to the list to be deleted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Low level object deletion, after reference counts have been
 *              updated (All reference counts, including sub-objects!)
 *
 ******************************************************************************/

void
acpi_ut_delete_internal_obj (
	acpi_operand_object     *object)
{
	void                    *obj_pointer = NULL;
	acpi_operand_object     *handler_desc;


	FUNCTION_TRACE_PTR ("Ut_delete_internal_obj", object);


	if (!object) {
		return_VOID;
	}

	/*
	 * Must delete or free any pointers within the object that are not
	 * actual ACPI objects (for example, a raw buffer pointer).
	 */
	switch (object->common.type) {

	case ACPI_TYPE_STRING:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "**** String %p, ptr %p\n",
			object, object->string.pointer));

		/* Free the actual string buffer */

		if (!(object->common.flags & AOPOBJ_STATIC_POINTER)) {
			obj_pointer = object->string.pointer;
		}
		break;


	case ACPI_TYPE_BUFFER:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "**** Buffer %p, ptr %p\n",
			object, object->buffer.pointer));

		/* Free the actual buffer */

		obj_pointer = object->buffer.pointer;
		break;


	case ACPI_TYPE_PACKAGE:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, " **** Package of count %X\n",
			object->package.count));

		/*
		 * Elements of the package are not handled here, they are deleted
		 * separately
		 */

		/* Free the (variable length) element pointer array */

		obj_pointer = object->package.elements;
		break;


	case ACPI_TYPE_MUTEX:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "***** Mutex %p, Semaphore %p\n",
			object, object->mutex.semaphore));

		acpi_ex_unlink_mutex (object);
		acpi_os_delete_semaphore (object->mutex.semaphore);
		break;


	case ACPI_TYPE_EVENT:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "***** Event %p, Semaphore %p\n",
			object, object->event.semaphore));

		acpi_os_delete_semaphore (object->event.semaphore);
		object->event.semaphore = NULL;
		break;


	case ACPI_TYPE_METHOD:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "***** Method %p\n", object));

		/* Delete the method semaphore if it exists */

		if (object->method.semaphore) {
			acpi_os_delete_semaphore (object->method.semaphore);
			object->method.semaphore = NULL;
		}

		break;


	case ACPI_TYPE_REGION:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "***** Region %p\n", object));

		if (object->region.extra) {
			/*
			 * Free the Region_context if and only if the handler is one of the
			 * default handlers -- and therefore, we created the context object
			 * locally, it was not created by an external caller.
			 */
			handler_desc = object->region.addr_handler;
			if ((handler_desc) &&
				(handler_desc->addr_handler.hflags == ADDR_HANDLER_DEFAULT_INSTALLED)) {
				obj_pointer = object->region.extra->extra.region_context;
			}

			/* Now we can free the Extra object */

			acpi_ut_delete_object_desc (object->region.extra);
		}
		break;


	case ACPI_TYPE_BUFFER_FIELD:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "***** Buffer Field %p\n", object));

		if (object->buffer_field.extra) {
			acpi_ut_delete_object_desc (object->buffer_field.extra);
		}
		break;

	default:
		break;
	}


	/*
	 * Delete any allocated memory found above
	 */
	if (obj_pointer) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Deleting Obj Ptr %p \n", obj_pointer));
		ACPI_MEM_FREE (obj_pointer);
	}

	/* Only delete the object if it was dynamically allocated */

	if (object->common.flags & AOPOBJ_STATIC_ALLOCATION) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Object %p [%s] static allocation, no delete\n",
			object, acpi_ut_get_type_name (object->common.type)));
	}

	if (!(object->common.flags & AOPOBJ_STATIC_ALLOCATION)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Deleting object %p [%s]\n",
			object, acpi_ut_get_type_name (object->common.type)));

		acpi_ut_delete_object_desc (object);
	}

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_delete_internal_object_list
 *
 * PARAMETERS:  *Obj_list       - Pointer to the list to be deleted
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function deletes an internal object list, including both
 *              simple objects and package objects
 *
 ******************************************************************************/

acpi_status
acpi_ut_delete_internal_object_list (
	acpi_operand_object     **obj_list)
{
	acpi_operand_object     **internal_obj;


	FUNCTION_TRACE ("Ut_delete_internal_object_list");


	/* Walk the null-terminated internal list */

	for (internal_obj = obj_list; *internal_obj; internal_obj++) {
		acpi_ut_remove_reference (*internal_obj);
	}

	/* Free the combined parameter pointer list and object array */

	ACPI_MEM_FREE (obj_list);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_update_ref_count
 *
 * PARAMETERS:  *Object         - Object whose ref count is to be updated
 *              Action          - What to do
 *
 * RETURN:      New ref count
 *
 * DESCRIPTION: Modify the ref count and return it.
 *
 ******************************************************************************/

static void
acpi_ut_update_ref_count (
	acpi_operand_object     *object,
	u32                     action)
{
	u16                     count;
	u16                     new_count;


	PROC_NAME ("Ut_update_ref_count");

	if (!object) {
		return;
	}


	count = object->common.reference_count;
	new_count = count;

	/*
	 * Reference count action (increment, decrement, or force delete)
	 */
	switch (action) {

	case REF_INCREMENT:

		new_count++;
		object->common.reference_count = new_count;

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Obj %p Refs=%X, [Incremented]\n",
			object, new_count));
		break;


	case REF_DECREMENT:

		if (count < 1) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Obj %p Refs=%X, can't decrement! (Set to 0)\n",
				object, new_count));

			new_count = 0;
		}

		else {
			new_count--;

			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Obj %p Refs=%X, [Decremented]\n",
				object, new_count));
		}

		if (object->common.type == ACPI_TYPE_METHOD) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Method Obj %p Refs=%X, [Decremented]\n",
				object, new_count));
		}

		object->common.reference_count = new_count;
		if (new_count == 0) {
			acpi_ut_delete_internal_obj (object);
		}

		break;


	case REF_FORCE_DELETE:

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Obj %p Refs=%X, Force delete! (Set to 0)\n",
			object, count));

		new_count = 0;
		object->common.reference_count = new_count;
		acpi_ut_delete_internal_obj (object);
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown action (%X)\n", action));
		break;
	}


	/*
	 * Sanity check the reference count, for debug purposes only.
	 * (A deleted object will have a huge reference count)
	 */
	if (count > MAX_REFERENCE_COUNT) {

		ACPI_DEBUG_PRINT ((ACPI_DB_WARN,
			"**** Warning **** Large Reference Count (%X) in object %p\n\n",
			count, object));
	}

	return;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_update_object_reference
 *
 * PARAMETERS:  *Object             - Increment ref count for this object
 *                                    and all sub-objects
 *              Action              - Either REF_INCREMENT or REF_DECREMENT or
 *                                    REF_FORCE_DELETE
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Increment the object reference count
 *
 * Object references are incremented when:
 * 1) An object is attached to a Node (namespace object)
 * 2) An object is copied (all subobjects must be incremented)
 *
 * Object references are decremented when:
 * 1) An object is detached from an Node
 *
 ******************************************************************************/

acpi_status
acpi_ut_update_object_reference (
	acpi_operand_object     *object,
	u16                     action)
{
	acpi_status             status;
	u32                     i;
	acpi_operand_object     *next;
	acpi_operand_object     *new;
	acpi_generic_state       *state_list = NULL;
	acpi_generic_state       *state;


	FUNCTION_TRACE_PTR ("Ut_update_object_reference", object);


	/* Ignore a null object ptr */

	if (!object) {
		return_ACPI_STATUS (AE_OK);
	}


	/*
	 * Make sure that this isn't a namespace handle or an AML pointer
	 */
	if (VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_NAMED)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Object %p is NS handle\n", object));
		return_ACPI_STATUS (AE_OK);
	}


	state = acpi_ut_create_update_state (object, action);

	while (state) {
		object = state->update.object;
		action = state->update.value;
		acpi_ut_delete_generic_state (state);

		/*
		 * All sub-objects must have their reference count incremented also.
		 * Different object types have different subobjects.
		 */
		switch (object->common.type) {

		case ACPI_TYPE_DEVICE:

			status = acpi_ut_create_update_state_and_push (object->device.addr_handler,
					   action, &state_list);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			acpi_ut_update_ref_count (object->device.sys_handler, action);
			acpi_ut_update_ref_count (object->device.drv_handler, action);
			break;


		case INTERNAL_TYPE_ADDRESS_HANDLER:

			/* Must walk list of address handlers */

			next = object->addr_handler.next;
			while (next) {
				new = next->addr_handler.next;
				acpi_ut_update_ref_count (next, action);

				next = new;
			}
			break;


		case ACPI_TYPE_PACKAGE:

			/*
			 * We must update all the sub-objects of the package
			 * (Each of whom may have their own sub-objects, etc.
			 */
			for (i = 0; i < object->package.count; i++) {
				/*
				 * Push each element onto the stack for later processing.
				 * Note: There can be null elements within the package,
				 * these are simply ignored
				 */
				status = acpi_ut_create_update_state_and_push (
						 object->package.elements[i], action, &state_list);
				if (ACPI_FAILURE (status)) {
					return_ACPI_STATUS (status);
				}
			}
			break;


		case ACPI_TYPE_BUFFER_FIELD:

			status = acpi_ut_create_update_state_and_push (
					 object->buffer_field.buffer_obj, action, &state_list);

			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
			break;


		case INTERNAL_TYPE_REGION_FIELD:

			status = acpi_ut_create_update_state_and_push (
					 object->field.region_obj, action, &state_list);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		   break;


		case INTERNAL_TYPE_BANK_FIELD:

			status = acpi_ut_create_update_state_and_push (
					 object->bank_field.bank_register_obj, action, &state_list);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			status = acpi_ut_create_update_state_and_push (
					 object->bank_field.region_obj, action, &state_list);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
			break;


		case INTERNAL_TYPE_INDEX_FIELD:

			status = acpi_ut_create_update_state_and_push (
					 object->index_field.index_obj, action, &state_list);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}

			status = acpi_ut_create_update_state_and_push (
					 object->index_field.data_obj, action, &state_list);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
			break;


		case ACPI_TYPE_REGION:
		case INTERNAL_TYPE_REFERENCE:

			/* No subobjects */
			break;
		}


		/*
		 * Now we can update the count in the main object.  This can only
		 * happen after we update the sub-objects in case this causes the
		 * main object to be deleted.
		 */
		acpi_ut_update_ref_count (object, action);


		/* Move on to the next object to be updated */

		state = acpi_ut_pop_generic_state (&state_list);
	}


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_add_reference
 *
 * PARAMETERS:  *Object        - Object whose reference count is to be
 *                                  incremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add one reference to an ACPI object
 *
 ******************************************************************************/

void
acpi_ut_add_reference (
	acpi_operand_object     *object)
{

	FUNCTION_TRACE_PTR ("Ut_add_reference", object);


	/*
	 * Ensure that we have a valid object
	 */
	if (!acpi_ut_valid_internal_object (object)) {
		return_VOID;
	}

	/*
	 * We have a valid ACPI internal object, now increment the reference count
	 */
	acpi_ut_update_object_reference (object, REF_INCREMENT);

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_remove_reference
 *
 * PARAMETERS:  *Object        - Object whose ref count will be decremented
 *
 * RETURN:      None
 *
 * DESCRIPTION: Decrement the reference count of an ACPI internal object
 *
 ******************************************************************************/

void
acpi_ut_remove_reference (
	acpi_operand_object     *object)
{

	FUNCTION_TRACE_PTR ("Ut_remove_reference", object);

	/*
	 * Allow a NULL pointer to be passed in, just ignore it.  This saves
	 * each caller from having to check.  Also, ignore NS nodes.
	 *
	 */
	if (!object ||
		(VALID_DESCRIPTOR_TYPE (object, ACPI_DESC_TYPE_NAMED))) {
		return_VOID;
	}

	/*
	 * Ensure that we have a valid object
	 */
	if (!acpi_ut_valid_internal_object (object)) {
		return_VOID;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Obj %p Refs=%X\n",
			object, object->common.reference_count));

	/*
	 * Decrement the reference count, and only actually delete the object
	 * if the reference count becomes 0.  (Must also decrement the ref count
	 * of all subobjects!)
	 */
	acpi_ut_update_object_reference (object, REF_DECREMENT);
	return_VOID;
}


