/******************************************************************************
 *
 * Module Name: utcopy - Internal to external object translation utilities
 *              $Revision: 104 $
 *
 *****************************************************************************/

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


#define _COMPONENT          ACPI_UTILITIES
	 ACPI_MODULE_NAME    ("utcopy")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_isimple_to_esimple
 *
 * PARAMETERS:  *Internal_object    - Pointer to the object we are examining
 *              *Buffer             - Where the object is returned
 *              *Space_used         - Where the data length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to place a simple object in a user
 *              buffer.
 *
 *              The buffer is assumed to have sufficient space for the object.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_copy_isimple_to_esimple (
	acpi_operand_object     *internal_object,
	acpi_object             *external_object,
	u8                      *data_space,
	ACPI_SIZE               *buffer_space_used)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ut_copy_isimple_to_esimple");


	*buffer_space_used = 0;

	/*
	 * Check for NULL object case (could be an uninitialized
	 * package element)
	 */
	if (!internal_object) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Always clear the external object */

	ACPI_MEMSET (external_object, 0, sizeof (acpi_object));

	/*
	 * In general, the external object will be the same type as
	 * the internal object
	 */
	external_object->type = ACPI_GET_OBJECT_TYPE (internal_object);

	/* However, only a limited number of external types are supported */

	switch (ACPI_GET_OBJECT_TYPE (internal_object)) {
	case ACPI_TYPE_STRING:

		external_object->string.pointer = (NATIVE_CHAR *) data_space;
		external_object->string.length = internal_object->string.length;
		*buffer_space_used = ACPI_ROUND_UP_TO_NATIVE_WORD ((ACPI_SIZE) internal_object->string.length + 1);

		ACPI_MEMCPY ((void *) data_space, (void *) internal_object->string.pointer,
				 (ACPI_SIZE) internal_object->string.length + 1);
		break;


	case ACPI_TYPE_BUFFER:

		external_object->buffer.pointer = data_space;
		external_object->buffer.length = internal_object->buffer.length;
		*buffer_space_used = ACPI_ROUND_UP_TO_NATIVE_WORD (internal_object->string.length);

		ACPI_MEMCPY ((void *) data_space, (void *) internal_object->buffer.pointer,
				 internal_object->buffer.length);
		break;


	case ACPI_TYPE_INTEGER:

		external_object->integer.value = internal_object->integer.value;
		break;


	case INTERNAL_TYPE_REFERENCE:

		/*
		 * This is an object reference.  Attempt to dereference it.
		 */
		switch (internal_object->reference.opcode) {
		case AML_INT_NAMEPATH_OP:

			/* For namepath, return the object handle ("reference") */

		default:
			/*
			 * Use the object type of "Any" to indicate a reference
			 * to object containing a handle to an ACPI named object.
			 */
			external_object->type = ACPI_TYPE_ANY;
			external_object->reference.handle = internal_object->reference.node;
			break;
		}
		break;


	case ACPI_TYPE_PROCESSOR:

		external_object->processor.proc_id    = internal_object->processor.proc_id;
		external_object->processor.pblk_address = internal_object->processor.address;
		external_object->processor.pblk_length = internal_object->processor.length;
		break;


	case ACPI_TYPE_POWER:

		external_object->power_resource.system_level =
				   internal_object->power_resource.system_level;

		external_object->power_resource.resource_order =
				   internal_object->power_resource.resource_order;
		break;


	default:
		/*
		 * There is no corresponding external object type
		 */
		return_ACPI_STATUS (AE_SUPPORT);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_ielement_to_eelement
 *
 * PARAMETERS:  ACPI_PKG_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy one package element to another package element
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_ielement_to_eelement (
	u8                      object_type,
	acpi_operand_object     *source_object,
	acpi_generic_state      *state,
	void                    *context)
{
	acpi_status             status = AE_OK;
	acpi_pkg_info           *info = (acpi_pkg_info *) context;
	ACPI_SIZE               object_space;
	u32                     this_index;
	acpi_object             *target_object;


	ACPI_FUNCTION_ENTRY ();


	this_index   = state->pkg.index;
	target_object = (acpi_object *)
			  &((acpi_object *)(state->pkg.dest_object))->package.elements[this_index];

	switch (object_type) {
	case ACPI_COPY_TYPE_SIMPLE:

		/*
		 * This is a simple or null object
		 */
		status = acpi_ut_copy_isimple_to_esimple (source_object,
				  target_object, info->free_space, &object_space);
		if (ACPI_FAILURE (status)) {
			return (status);
		}
		break;


	case ACPI_COPY_TYPE_PACKAGE:

		/*
		 * Build the package object
		 */
		target_object->type             = ACPI_TYPE_PACKAGE;
		target_object->package.count    = source_object->package.count;
		target_object->package.elements = ACPI_CAST_PTR (acpi_object, info->free_space);

		/*
		 * Pass the new package object back to the package walk routine
		 */
		state->pkg.this_target_obj = target_object;

		/*
		 * Save space for the array of objects (Package elements)
		 * update the buffer length counter
		 */
		object_space = ACPI_ROUND_UP_TO_NATIVE_WORD (
				   (ACPI_SIZE) target_object->package.count * sizeof (acpi_object));
		break;


	default:
		return (AE_BAD_PARAMETER);
	}

	info->free_space  += object_space;
	info->length      += object_space;
	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_ipackage_to_epackage
 *
 * PARAMETERS:  *Internal_object    - Pointer to the object we are returning
 *              *Buffer             - Where the object is returned
 *              *Space_used         - Where the object length is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to place a package object in a user
 *              buffer.  A package object by definition contains other objects.
 *
 *              The buffer is assumed to have sufficient space for the object.
 *              The caller must have verified the buffer length needed using the
 *              Acpi_ut_get_object_size function before calling this function.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_copy_ipackage_to_epackage (
	acpi_operand_object     *internal_object,
	u8                      *buffer,
	ACPI_SIZE               *space_used)
{
	acpi_object             *external_object;
	acpi_status             status;
	acpi_pkg_info           info;


	ACPI_FUNCTION_TRACE ("Ut_copy_ipackage_to_epackage");


	/*
	 * First package at head of the buffer
	 */
	external_object = ACPI_CAST_PTR (acpi_object, buffer);

	/*
	 * Free space begins right after the first package
	 */
	info.length      = ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (acpi_object));
	info.free_space  = buffer + ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (acpi_object));
	info.object_space = 0;
	info.num_packages = 1;

	external_object->type            = ACPI_GET_OBJECT_TYPE (internal_object);
	external_object->package.count   = internal_object->package.count;
	external_object->package.elements = ACPI_CAST_PTR (acpi_object, info.free_space);

	/*
	 * Leave room for an array of ACPI_OBJECTS in the buffer
	 * and move the free space past it
	 */
	info.length    += (ACPI_SIZE) external_object->package.count *
			 ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (acpi_object));
	info.free_space += external_object->package.count *
			 ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (acpi_object));

	status = acpi_ut_walk_package_tree (internal_object, external_object,
			 acpi_ut_copy_ielement_to_eelement, &info);

	*space_used = info.length;
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_iobject_to_eobject
 *
 * PARAMETERS:  *Internal_object    - The internal object to be converted
 *              *Buffer_ptr         - Where the object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function is called to build an API object to be returned to
 *              the caller.
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_iobject_to_eobject (
	acpi_operand_object     *internal_object,
	acpi_buffer             *ret_buffer)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ut_copy_iobject_to_eobject");


	if (ACPI_GET_OBJECT_TYPE (internal_object) == ACPI_TYPE_PACKAGE) {
		/*
		 * Package object:  Copy all subobjects (including
		 * nested packages)
		 */
		status = acpi_ut_copy_ipackage_to_epackage (internal_object,
				  ret_buffer->pointer, &ret_buffer->length);
	}
	else {
		/*
		 * Build a simple object (no nested objects)
		 */
		status = acpi_ut_copy_isimple_to_esimple (internal_object,
				  (acpi_object *) ret_buffer->pointer,
				  ((u8 *) ret_buffer->pointer +
				  ACPI_ROUND_UP_TO_NATIVE_WORD (sizeof (acpi_object))),
				  &ret_buffer->length);
		/*
		 * build simple does not include the object size in the length
		 * so we add it in here
		 */
		ret_buffer->length += sizeof (acpi_object);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_esimple_to_isimple
 *
 * PARAMETERS:  *External_object   - The external object to be converted
 *              *Internal_object   - Where the internal object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: This function copies an external object to an internal one.
 *              NOTE: Pointers can be copied, we don't need to copy data.
 *              (The pointers have to be valid in our address space no matter
 *              what we do with them!)
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_esimple_to_isimple (
	acpi_object             *external_object,
	acpi_operand_object     **ret_internal_object)
{
	acpi_operand_object     *internal_object;


	ACPI_FUNCTION_TRACE ("Ut_copy_esimple_to_isimple");


	/*
	 * Simple types supported are: String, Buffer, Integer
	 */
	switch (external_object->type) {
	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:
	case ACPI_TYPE_INTEGER:

		internal_object = acpi_ut_create_internal_object ((u8) external_object->type);
		if (!internal_object) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}
		break;

	default:
		/*
		 * Whatever other type -- it is not supported
		 */
		return_ACPI_STATUS (AE_SUPPORT);
	}


	switch (external_object->type) {

	/* Must COPY string and buffer contents */

	case ACPI_TYPE_STRING:

		internal_object->string.pointer = ACPI_MEM_CALLOCATE ((ACPI_SIZE) external_object->string.length + 1);
		if (!internal_object->string.pointer) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		ACPI_MEMCPY (internal_object->string.pointer,
				  external_object->string.pointer,
				  external_object->string.length);

		internal_object->string.length = external_object->string.length;
		break;


	case ACPI_TYPE_BUFFER:

		internal_object->buffer.pointer = ACPI_MEM_CALLOCATE (external_object->buffer.length);
		if (!internal_object->buffer.pointer) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		ACPI_MEMCPY (internal_object->buffer.pointer,
				  external_object->buffer.pointer,
				  external_object->buffer.length);

		internal_object->buffer.length = external_object->buffer.length;
		break;


	case ACPI_TYPE_INTEGER:

		internal_object->integer.value  = external_object->integer.value;
		break;

	default:
		/* Other types can't get here */
		break;
	}

	*ret_internal_object = internal_object;
	return_ACPI_STATUS (AE_OK);
}


#ifdef ACPI_FUTURE_IMPLEMENTATION

/* Code to convert packages that are parameters to control methods */

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_epackage_to_ipackage
 *
 * PARAMETERS:  *Internal_object   - Pointer to the object we are returning
 *              *Buffer         - Where the object is returned
 *              *Space_used     - Where the length of the object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to place a package object in a user
 *              buffer.  A package object by definition contains other objects.
 *
 *              The buffer is assumed to have sufficient space for the object.
 *              The caller must have verified the buffer length needed using the
 *              Acpi_ut_get_object_size function before calling this function.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_copy_epackage_to_ipackage (
	acpi_operand_object     *internal_object,
	u8                      *buffer,
	u32                     *space_used)
{
	u8                      *free_space;
	acpi_object             *external_object;
	u32                     length = 0;
	u32                     this_index;
	u32                     object_space = 0;
	acpi_operand_object     *this_internal_obj;
	acpi_object             *this_external_obj;


	ACPI_FUNCTION_TRACE ("Ut_copy_epackage_to_ipackage");


	/*
	 * First package at head of the buffer
	 */
	external_object = (acpi_object *)buffer;

	/*
	 * Free space begins right after the first package
	 */
	free_space = buffer + sizeof(acpi_object);


	external_object->type              = ACPI_GET_OBJECT_TYPE (internal_object);
	external_object->package.count     = internal_object->package.count;
	external_object->package.elements  = (acpi_object *)free_space;

	/*
	 * Build an array of ACPI_OBJECTS in the buffer
	 * and move the free space past it
	 */
	free_space += external_object->package.count * sizeof(acpi_object);


	/* Call Walk_package */

}

#endif /* Future implementation */


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_eobject_to_iobject
 *
 * PARAMETERS:  *Internal_object   - The external object to be converted
 *              *Buffer_ptr     - Where the internal object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: Converts an external object to an internal object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_eobject_to_iobject (
	acpi_object             *external_object,
	acpi_operand_object     **internal_object)
{
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ut_copy_eobject_to_iobject");


	if (external_object->type == ACPI_TYPE_PACKAGE) {
		/*
		 * Packages as external input to control methods are not supported,
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Packages as parameters not implemented!\n"));

		return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
	}

	else {
		/*
		 * Build a simple object (no nested objects)
		 */
		status = acpi_ut_copy_esimple_to_isimple (external_object, internal_object);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_simple_object
 *
 * PARAMETERS:  Source_desc         - The internal object to be copied
 *              Dest_desc           - New target object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Simple copy of one internal object to another.  Reference count
 *              of the destination object is preserved.
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_simple_object (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *dest_desc)
{
	u16                     reference_count;
	acpi_operand_object     *next_object;


	/* Save fields from destination that we don't want to overwrite */

	reference_count = dest_desc->common.reference_count;
	next_object = dest_desc->common.next_object;

	/* Copy the entire source object over the destination object*/

	ACPI_MEMCPY ((char *) dest_desc, (char *) source_desc, sizeof (acpi_operand_object));

	/* Restore the saved fields */

	dest_desc->common.reference_count = reference_count;
	dest_desc->common.next_object = next_object;

	/* Handle the objects with extra data */

	switch (ACPI_GET_OBJECT_TYPE (dest_desc)) {
	case ACPI_TYPE_BUFFER:

		dest_desc->buffer.node = NULL;
		dest_desc->common.flags = source_desc->common.flags;

		/* Fall through to common string/buffer case */
		/*lint -fallthrough */

	case ACPI_TYPE_STRING:

		/*
		 * Allocate and copy the actual string if and only if:
		 * 1) There is a valid string (length > 0)
		 * 2) The string is not static (not in an ACPI table) (in this case,
		 *    the actual pointer was already copied above)
		 */
		if ((source_desc->string.length) &&
			(!(source_desc->common.flags & AOPOBJ_STATIC_POINTER))) {
			dest_desc->string.pointer = ACPI_MEM_ALLOCATE (source_desc->string.length);
			if (!dest_desc->string.pointer) {
				return (AE_NO_MEMORY);
			}

			ACPI_MEMCPY (dest_desc->string.pointer, source_desc->string.pointer,
					  source_desc->string.length);
		}
		break;

	default:
		/* Nothing to do for other simple objects */
		break;
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_ielement_to_ielement
 *
 * PARAMETERS:  ACPI_PKG_CALLBACK
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy one package element to another package element
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_ielement_to_ielement (
	u8                      object_type,
	acpi_operand_object     *source_object,
	acpi_generic_state      *state,
	void                    *context)
{
	acpi_status             status = AE_OK;
	u32                     this_index;
	acpi_operand_object     **this_target_ptr;
	acpi_operand_object     *target_object;


	ACPI_FUNCTION_ENTRY ();


	this_index    = state->pkg.index;
	this_target_ptr = (acpi_operand_object **)
			   &state->pkg.dest_object->package.elements[this_index];

	switch (object_type) {
	case 0:

		/*
		 * This is a simple object, just copy it
		 */
		target_object = acpi_ut_create_internal_object (ACPI_GET_OBJECT_TYPE (source_object));
		if (!target_object) {
			return (AE_NO_MEMORY);
		}

		status = acpi_ut_copy_simple_object (source_object, target_object);
		if (ACPI_FAILURE (status)) {
			return (status);
		}

		*this_target_ptr = target_object;
		break;


	case 1:

		/*
		 * This object is a package - go down another nesting level
		 * Create and build the package object
		 */
		target_object = acpi_ut_create_internal_object (ACPI_TYPE_PACKAGE);
		if (!target_object) {
			return (AE_NO_MEMORY);
		}

		target_object->package.count = source_object->package.count;
		target_object->common.flags = source_object->common.flags;

		/*
		 * Pass the new package object back to the package walk routine
		 */
		state->pkg.this_target_obj = target_object;

		/*
		 * Store the object pointer in the parent package object
		 */
		*this_target_ptr = target_object;
		break;


	default:
		return (AE_BAD_PARAMETER);
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_ipackage_to_ipackage
 *
 * PARAMETERS:  *Source_obj     - Pointer to the source package object
 *              *Dest_obj       - Where the internal object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to copy an internal package object
 *              into another internal package object.
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_ipackage_to_ipackage (
	acpi_operand_object     *source_obj,
	acpi_operand_object     *dest_obj,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ut_copy_ipackage_to_ipackage");


	dest_obj->common.type   = ACPI_GET_OBJECT_TYPE (source_obj);
	dest_obj->common.flags  = source_obj->common.flags;
	dest_obj->package.count = source_obj->package.count;

	/*
	 * Create the object array and walk the source package tree
	 */
	dest_obj->package.elements = ACPI_MEM_CALLOCATE (
			   ((ACPI_SIZE) source_obj->package.count + 1) *
			   sizeof (void *));
	if (!dest_obj->package.elements) {
		ACPI_REPORT_ERROR (
			("Aml_build_copy_internal_package_object: Package allocation failure\n"));
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/*
	 * Copy the package element-by-element by walking the package "tree".
	 * This handles nested packages of arbitrary depth.
	 */
	status = acpi_ut_walk_package_tree (source_obj, dest_obj,
			 acpi_ut_copy_ielement_to_ielement, walk_state);
	if (ACPI_FAILURE (status)) {
		/* On failure, delete the destination package object */

		acpi_ut_remove_reference (dest_obj);
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_copy_iobject_to_iobject
 *
 * PARAMETERS:  Walk_state          - Current walk state
 *              Source_desc         - The internal object to be copied
 *              Dest_desc           - Where the copied object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy an internal object to a new internal object
 *
 ******************************************************************************/

acpi_status
acpi_ut_copy_iobject_to_iobject (
	acpi_operand_object     *source_desc,
	acpi_operand_object     **dest_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ut_copy_iobject_to_iobject");


	/* Create the top level object */

	*dest_desc = acpi_ut_create_internal_object (ACPI_GET_OBJECT_TYPE (source_desc));
	if (!*dest_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Copy the object and possible subobjects */

	if (ACPI_GET_OBJECT_TYPE (source_desc) == ACPI_TYPE_PACKAGE) {
		status = acpi_ut_copy_ipackage_to_ipackage (source_desc, *dest_desc,
				  walk_state);
	}
	else {
		status = acpi_ut_copy_simple_object (source_desc, *dest_desc);
	}

	return_ACPI_STATUS (status);
}


