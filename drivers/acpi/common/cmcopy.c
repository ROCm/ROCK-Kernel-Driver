/******************************************************************************
 *
 * Module Name: cmcopy - Internal to external object translation utilities
 *              $Revision: 61 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 R. Byron Moore
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


#define _COMPONENT          MISCELLANEOUS
	 MODULE_NAME         ("cmcopy")


typedef struct search_st
{
	ACPI_OPERAND_OBJECT         *internal_obj;
	u32                         index;
	ACPI_OBJECT                 *external_obj;

} PKG_SEARCH_INFO;


/* Used to traverse nested packages */

PKG_SEARCH_INFO                 level[MAX_PACKAGE_DEPTH];

/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_build_external_simple_object
 *
 * PARAMETERS:  *Internal_obj   - Pointer to the object we are examining
 *              *Buffer         - Where the object is returned
 *              *Space_used     - Where the data length is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to place a simple object in a user
 *                  buffer.
 *
 *              The buffer is assumed to have sufficient space for the object.
 *
 ******************************************************************************/

static ACPI_STATUS
acpi_cm_build_external_simple_object (
	ACPI_OPERAND_OBJECT     *internal_obj,
	ACPI_OBJECT             *external_obj,
	u8                      *data_space,
	u32                     *buffer_space_used)
{
	u32                     length = 0;
	u8                      *source_ptr = NULL;


	/*
	 * Check for NULL object case (could be an uninitialized
	 * package element
	 */

	if (!internal_obj) {
		*buffer_space_used = 0;
		return (AE_OK);
	}

	/* Always clear the external object */

	MEMSET (external_obj, 0, sizeof (ACPI_OBJECT));

	/*
	 * In general, the external object will be the same type as
	 * the internal object
	 */

	external_obj->type = internal_obj->common.type;

	/* However, only a limited number of external types are supported */

	switch (external_obj->type)
	{

	case ACPI_TYPE_STRING:

		length = internal_obj->string.length + 1;
		external_obj->string.length = internal_obj->string.length;
		external_obj->string.pointer = (NATIVE_CHAR *) data_space;
		source_ptr = (u8 *) internal_obj->string.pointer;
		break;


	case ACPI_TYPE_BUFFER:

		length = internal_obj->buffer.length;
		external_obj->buffer.length = internal_obj->buffer.length;
		external_obj->buffer.pointer = data_space;
		source_ptr = (u8 *) internal_obj->buffer.pointer;
		break;


	case ACPI_TYPE_NUMBER:

		external_obj->number.value= internal_obj->number.value;
		break;


	case INTERNAL_TYPE_REFERENCE:

		/*
		 * This is an object reference.  We use the object type of "Any"
		 * to indicate a reference object containing a handle to an ACPI
		 * named object.
		 */

		external_obj->type = ACPI_TYPE_ANY;
		external_obj->reference.handle = internal_obj->reference.node;
		break;


	case ACPI_TYPE_PROCESSOR:

		external_obj->processor.proc_id =
				 internal_obj->processor.proc_id;

		external_obj->processor.pblk_address =
				 internal_obj->processor.address;

		external_obj->processor.pblk_length =
				 internal_obj->processor.length;
		break;

	case ACPI_TYPE_POWER:

		external_obj->power_resource.system_level =
				   internal_obj->power_resource.system_level;

		external_obj->power_resource.resource_order =
				   internal_obj->power_resource.resource_order;
		break;

	default:
		return (AE_CTRL_RETURN_VALUE);
		break;
	}


	/* Copy data if necessary (strings or buffers) */

	if (length) {
		/*
		 * Copy the return data to the caller's buffer
		 */
		MEMCPY ((void *) data_space, (void *) source_ptr, length);
	}


	*buffer_space_used = (u32) ROUND_UP_TO_NATIVE_WORD (length);

	return (AE_OK);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_build_external_package_object
 *
 * PARAMETERS:  *Internal_obj   - Pointer to the object we are returning
 *              *Buffer         - Where the object is returned
 *              *Space_used     - Where the object length is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to place a package object in a user
 *              buffer.  A package object by definition contains other objects.
 *
 *              The buffer is assumed to have sufficient space for the object.
 *              The caller must have verified the buffer length needed using the
 *              Acpi_cm_get_object_size function before calling this function.
 *
 ******************************************************************************/

static ACPI_STATUS
acpi_cm_build_external_package_object (
	ACPI_OPERAND_OBJECT     *internal_obj,
	u8                      *buffer,
	u32                     *space_used)
{
	u8                      *free_space;
	ACPI_OBJECT             *external_obj;
	u32                     current_depth = 0;
	ACPI_STATUS             status;
	u32                     length = 0;
	u32                     this_index;
	u32                     object_space;
	ACPI_OPERAND_OBJECT     *this_internal_obj;
	ACPI_OBJECT             *this_external_obj;
	PKG_SEARCH_INFO         *level_ptr;


	/*
	 * First package at head of the buffer
	 */
	external_obj = (ACPI_OBJECT *) buffer;

	/*
	 * Free space begins right after the first package
	 */
	free_space = buffer + ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT));


	/*
	 * Initialize the working variables
	 */

	MEMSET ((void *) level, 0, sizeof (level));

	level[0].internal_obj   = internal_obj;
	level[0].external_obj   = external_obj;
	level[0].index          = 0;
	level_ptr               = &level[0];
	current_depth           = 0;

	external_obj->type              = internal_obj->common.type;
	external_obj->package.count     = internal_obj->package.count;
	external_obj->package.elements  = (ACPI_OBJECT *) free_space;


	/*
	 * Build an array of ACPI_OBJECTS in the buffer
	 * and move the free space past it
	 */

	free_space += external_obj->package.count *
			  ROUND_UP_TO_NATIVE_WORD (sizeof (ACPI_OBJECT));


	while (1) {
		this_index      = level_ptr->index;
		this_internal_obj =
				(ACPI_OPERAND_OBJECT  *)
				level_ptr->internal_obj->package.elements[this_index];
		this_external_obj =
				(ACPI_OBJECT *)
				&level_ptr->external_obj->package.elements[this_index];


		/*
		 * Check for
		 * 1) Null object -- OK, this can happen if package
		 *              element is never initialized
		 * 2) Not an internal object - can be Node instead
		 * 3) Any internal object other than a package.
		 *
		 * The more complex package case is handled later
		 */

		if ((!this_internal_obj) ||
			(!VALID_DESCRIPTOR_TYPE (
				this_internal_obj, ACPI_DESC_TYPE_INTERNAL)) ||
			(!IS_THIS_OBJECT_TYPE (
				this_internal_obj, ACPI_TYPE_PACKAGE)))
		{
			/*
			 * This is a simple or null object -- get the size
			 */

			status =
				acpi_cm_build_external_simple_object (this_internal_obj,
						   this_external_obj,
						   free_space,
						   &object_space);
			if (ACPI_FAILURE (status)) {
				return (status);
			}

			free_space  += object_space;
			length      += object_space;

			level_ptr->index++;
			while (level_ptr->index >=
					level_ptr->internal_obj->package.count)
			{
				/*
				 * We've handled all of the objects at this
				 * level.  This means that we have just
				 * completed a package.  That package may
				 * have contained one or more packages
				 * itself
				 */
				if (current_depth == 0) {
					/*
					 * We have handled all of the objects
					 * in the top level package just add
					 * the length of the package objects
					 * and get out
					 */
					*space_used = length;
					return (AE_OK);
				}

				/*
				 * go back up a level and move the index
				 * past the just completed package object.
				 */
				current_depth--;
				level_ptr = &level[current_depth];
				level_ptr->index++;
			}
		}


		else {
			/*
			 * This object is a package
			 * -- we must go one level deeper
			 */
			if (current_depth >= MAX_PACKAGE_DEPTH-1) {
				/*
				 * Too many nested levels of packages
				 * for us to handle
				 */
				return (AE_LIMIT);
			}

			/*
			 * Build the package object
			 */
			this_external_obj->type = ACPI_TYPE_PACKAGE;
			this_external_obj->package.count =
					 this_internal_obj->package.count;
			this_external_obj->package.elements =
					  (ACPI_OBJECT *) free_space;

			/*
			 * Save space for the array of objects (Package elements)
			 * update the buffer length counter
			 */
			object_space = (u32) ROUND_UP_TO_NATIVE_WORD (
					   this_external_obj->package.count *
					   sizeof (ACPI_OBJECT));

			free_space              += object_space;
			length                  += object_space;

			current_depth++;
			level_ptr               = &level[current_depth];
			level_ptr->internal_obj = this_internal_obj;
			level_ptr->external_obj = this_external_obj;
			level_ptr->index        = 0;
		}
	}
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_build_external_object
 *
 * PARAMETERS:  *Internal_obj   - The internal object to be converted
 *              *Buffer_ptr     - Where the object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function is called to build an API object to be returned to
 *              the caller.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_cm_build_external_object (
	ACPI_OPERAND_OBJECT     *internal_obj,
	ACPI_BUFFER             *ret_buffer)
{
	ACPI_STATUS             status;


	if (IS_THIS_OBJECT_TYPE (internal_obj, ACPI_TYPE_PACKAGE)) {
		/*
		 * Package objects contain other objects (which can be objects)
		 * buildpackage does it all
		 */
		status =
			acpi_cm_build_external_package_object (internal_obj,
					 ret_buffer->pointer,
					 &ret_buffer->length);
	}

	else {
		/*
		 * Build a simple object (no nested objects)
		 */
		status =
			acpi_cm_build_external_simple_object (internal_obj,
					  (ACPI_OBJECT *) ret_buffer->pointer,
					  ((u8 *) ret_buffer->pointer +
					  ROUND_UP_TO_NATIVE_WORD (
							   sizeof (ACPI_OBJECT))),
							&ret_buffer->length);
		/*
		 * build simple does not include the object size in the length
		 * so we add it in here
		 */
		ret_buffer->length += sizeof (ACPI_OBJECT);
	}

	return (status);
}


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_build_internal_simple_object
 *
 * PARAMETERS:  *External_obj   - The external object to be converted
 *              *Internal_obj   - Where the internal object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: This function copies an external object to an internal one.
 *              NOTE: Pointers can be copied, we don't need to copy data.
 *              (The pointers have to be valid in our address space no matter
 *              what we do with them!)
 *
 ******************************************************************************/

ACPI_STATUS
acpi_cm_build_internal_simple_object (
	ACPI_OBJECT             *external_obj,
	ACPI_OPERAND_OBJECT     *internal_obj)
{


	internal_obj->common.type = (u8) external_obj->type;

	switch (external_obj->type)
	{

	case ACPI_TYPE_STRING:

		internal_obj->string.length = external_obj->string.length;
		internal_obj->string.pointer = external_obj->string.pointer;
		break;


	case ACPI_TYPE_BUFFER:

		internal_obj->buffer.length = external_obj->buffer.length;
		internal_obj->buffer.pointer = external_obj->buffer.pointer;
		break;


	case ACPI_TYPE_NUMBER:
		/*
		 * Number is included in the object itself
		 */
		internal_obj->number.value  = external_obj->number.value;
		break;


	default:
		return (AE_CTRL_RETURN_VALUE);
		break;
	}


	return (AE_OK);
}


#ifdef ACPI_FUTURE_IMPLEMENTATION

/* Code to convert packages that are parameters to control methods */

/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_build_internal_package_object
 *
 * PARAMETERS:  *Internal_obj   - Pointer to the object we are returning
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
 *              Acpi_cm_get_object_size function before calling this function.
 *
 ******************************************************************************/

static ACPI_STATUS
acpi_cm_build_internal_package_object (
	ACPI_OPERAND_OBJECT     *internal_obj,
	u8                      *buffer,
	u32                     *space_used)
{
	u8                      *free_space;
	ACPI_OBJECT             *external_obj;
	u32                     current_depth = 0;
	u32                     length = 0;
	u32                     this_index;
	u32                     object_space = 0;
	ACPI_OPERAND_OBJECT     *this_internal_obj;
	ACPI_OBJECT             *this_external_obj;
	PKG_SEARCH_INFO         *level_ptr;


	/*
	 * First package at head of the buffer
	 */
	external_obj = (ACPI_OBJECT *)buffer;

	/*
	 * Free space begins right after the first package
	 */
	free_space = buffer + sizeof(ACPI_OBJECT);


	/*
	 * Initialize the working variables
	 */

	MEMSET ((void *) level, 0, sizeof(level));

	level[0].internal_obj   = internal_obj;
	level[0].external_obj   = external_obj;
	level_ptr               = &level[0];
	current_depth           = 0;

	external_obj->type              = internal_obj->common.type;
	external_obj->package.count     = internal_obj->package.count;
	external_obj->package.elements  = (ACPI_OBJECT *)free_space;


	/*
	 * Build an array of ACPI_OBJECTS in the buffer
	 * and move the free space past it
	 */

	free_space += external_obj->package.count * sizeof(ACPI_OBJECT);


	while (1) {
		this_index      = level_ptr->index;

		this_internal_obj = (ACPI_OPERAND_OBJECT *)
				 &level_ptr->internal_obj->package.elements[this_index];

		this_external_obj = (ACPI_OBJECT *)
				 &level_ptr->external_obj->package.elements[this_index];

		if (IS_THIS_OBJECT_TYPE (this_internal_obj, ACPI_TYPE_PACKAGE)) {
			/*
			 * If this object is a package then we go one deeper
			 */
			if (current_depth >= MAX_PACKAGE_DEPTH-1) {
				/*
				 * Too many nested levels of packages for us to handle
				 */
				return (AE_LIMIT);
			}

			/*
			 * Build the package object
			 */
			this_external_obj->type             = ACPI_TYPE_PACKAGE;
			this_external_obj->package.count    = this_internal_obj->package.count;
			this_external_obj->package.elements = (ACPI_OBJECT *) free_space;

			/*
			 * Save space for the array of objects (Package elements)
			 * update the buffer length counter
			 */
			object_space            = this_external_obj->package.count *
					   sizeof (ACPI_OBJECT);

			free_space              += object_space;
			length                  += object_space;

			current_depth++;
			level_ptr               = &level[current_depth];
			level_ptr->internal_obj = this_internal_obj;
			level_ptr->external_obj = this_external_obj;
			level_ptr->index        = 0;

		}   /* if object is a package */

		else {
			free_space  += object_space;
			length      += object_space;

			level_ptr->index++;
			while (level_ptr->index >=
					level_ptr->internal_obj->package.count)
			{
				/*
				 * We've handled all of the objects at
				 * this level,  This means that we have
				 * just completed a package.  That package
				 * may have contained one or more packages
				 * itself
				 */
				if (current_depth == 0) {
					/*
					 * We have handled all of the objects
					 * in the top level package just add
					 * the length of the package objects
					 * and get out
					 */
					*space_used = length;
					return (AE_OK);
				}

				/*
				 * go back up a level and move the index
				 * past the just completed package object.
				 */
				current_depth--;
				level_ptr = &level[current_depth];
				level_ptr->index++;
			}
		}   /* else object is NOT a package */
	}   /* while (1)  */
}

#endif /* Future implementation */


/******************************************************************************
 *
 * FUNCTION:    Acpi_cm_build_internal_object
 *
 * PARAMETERS:  *Internal_obj   - The external object to be converted
 *              *Buffer_ptr     - Where the internal object is returned
 *
 * RETURN:      Status          - the status of the call
 *
 * DESCRIPTION: Converts an external object to an internal object.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_cm_build_internal_object (
	ACPI_OBJECT             *external_obj,
	ACPI_OPERAND_OBJECT     *internal_obj)
{
	ACPI_STATUS             status;


	if (external_obj->type == ACPI_TYPE_PACKAGE) {
		/*
		 * Package objects contain other objects (which can be objects)
		 * buildpackage does it all
		 *
		 * TBD: Package conversion must be completed and tested
		 * NOTE: this code converts packages as input parameters to
		 * control methods only.  This is a very, very rare case.
		 */
/*
		Status = Acpi_cm_build_internal_package_object(Internal_obj,
				 Ret_buffer->Pointer,
				 &Ret_buffer->Length);
*/
		return (AE_NOT_IMPLEMENTED);
	}

	else {
		/*
		 * Build a simple object (no nested objects)
		 */
		status = acpi_cm_build_internal_simple_object (external_obj, internal_obj);
		/*
		 * build simple does not include the object size in the length
		 * so we add it in here
		 */
	}

	return (status);
}

