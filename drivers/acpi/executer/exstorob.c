
/******************************************************************************
 *
 * Module Name: exstorob - AML Interpreter object store support, store to object
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


#include "acpi.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exstorob")


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_buffer_to_buffer
 *
 * PARAMETERS:  source_desc         - Source object to copy
 *              target_desc         - Destination object of the copy
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy a buffer object to another buffer object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_buffer_to_buffer (
	union acpi_operand_object       *source_desc,
	union acpi_operand_object       *target_desc)
{
	u32                             length;
	u8                              *buffer;


	ACPI_FUNCTION_TRACE_PTR ("ex_store_buffer_to_buffer", source_desc);


	/*
	 * We know that source_desc is a buffer by now
	 */
	buffer = (u8 *) source_desc->buffer.pointer;
	length = source_desc->buffer.length;

	/*
	 * If target is a buffer of length zero or is a static buffer,
	 * allocate a new buffer of the proper length
	 */
	if ((target_desc->buffer.length == 0) ||
		(target_desc->common.flags & AOPOBJ_STATIC_POINTER)) {
		target_desc->buffer.pointer = ACPI_MEM_ALLOCATE (length);
		if (!target_desc->buffer.pointer) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		target_desc->common.flags &= ~AOPOBJ_STATIC_POINTER;
		target_desc->buffer.length = length;
	}

	/*
	 * Buffer is a static allocation,
	 * only place what will fit in the buffer.
	 */
	if (length <= target_desc->buffer.length) {
		/* Clear existing buffer and copy in the new one */

		ACPI_MEMSET (target_desc->buffer.pointer, 0, target_desc->buffer.length);
		ACPI_MEMCPY (target_desc->buffer.pointer, buffer, length);
	}
	else {
		/*
		 * Truncate the source, copy only what will fit
		 */
		ACPI_MEMCPY (target_desc->buffer.pointer, buffer, target_desc->buffer.length);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Truncating src buffer from %X to %X\n",
			length, target_desc->buffer.length));
	}

	/* Copy flags */

	target_desc->buffer.flags = source_desc->buffer.flags;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    acpi_ex_store_string_to_string
 *
 * PARAMETERS:  source_desc         - Source object to copy
 *              target_desc         - Destination object of the copy
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy a String object to another String object
 *
 ******************************************************************************/

acpi_status
acpi_ex_store_string_to_string (
	union acpi_operand_object       *source_desc,
	union acpi_operand_object       *target_desc)
{
	u32                             length;
	u8                              *buffer;


	ACPI_FUNCTION_TRACE_PTR ("ex_store_string_to_string", source_desc);


	/*
	 * We know that source_desc is a string by now.
	 */
	buffer = (u8 *) source_desc->string.pointer;
	length = source_desc->string.length;

	/*
	 * Replace existing string value if it will fit and the string
	 * pointer is not a static pointer (part of an ACPI table)
	 */
	if ((length < target_desc->string.length) &&
	   (!(target_desc->common.flags & AOPOBJ_STATIC_POINTER))) {
		/*
		 * String will fit in existing non-static buffer.
		 * Clear old string and copy in the new one
		 */
		ACPI_MEMSET (target_desc->string.pointer, 0, (acpi_size) target_desc->string.length + 1);
		ACPI_MEMCPY (target_desc->string.pointer, buffer, length);
	}
	else {
		/*
		 * Free the current buffer, then allocate a new buffer
		 * large enough to hold the value
		 */
		if (target_desc->string.pointer &&
		   (!(target_desc->common.flags & AOPOBJ_STATIC_POINTER))) {
			/*
			 * Only free if not a pointer into the DSDT
			 */
			ACPI_MEM_FREE (target_desc->string.pointer);
		}

		target_desc->string.pointer = ACPI_MEM_CALLOCATE ((acpi_size) length + 1);
		if (!target_desc->string.pointer) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		target_desc->common.flags &= ~AOPOBJ_STATIC_POINTER;
		ACPI_MEMCPY (target_desc->string.pointer, buffer, length);
	}

	/* Set the new target length */

	target_desc->string.length = length;
	return_ACPI_STATUS (AE_OK);
}


