
/******************************************************************************
 *
 * Module Name: exstorob - AML Interpreter object store support, store to object
 *              $Revision: 37 $
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
#include "acparser.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "actables.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exstorob")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_copy_buffer_to_buffer
 *
 * PARAMETERS:  Source_desc         - Source object to copy
 *              Target_desc         - Destination object of the copy
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy a buffer object to another buffer object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_copy_buffer_to_buffer (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc)
{
	u32                     length;
	u8                      *buffer;


	PROC_NAME ("Ex_copy_buffer_to_buffer");


	/*
	 * We know that Source_desc is a buffer by now
	 */
	buffer = (u8 *) source_desc->buffer.pointer;
	length = source_desc->buffer.length;

	/*
	 * If target is a buffer of length zero, allocate a new
	 * buffer of the proper length
	 */
	if (target_desc->buffer.length == 0) {
		target_desc->buffer.pointer = ACPI_MEM_ALLOCATE (length);
		if (!target_desc->buffer.pointer) {
			return (AE_NO_MEMORY);
		}

		target_desc->buffer.length = length;
	}

	/*
	 * Buffer is a static allocation,
	 * only place what will fit in the buffer.
	 */
	if (length <= target_desc->buffer.length) {
		/* Clear existing buffer and copy in the new one */

		MEMSET (target_desc->buffer.pointer, 0, target_desc->buffer.length);
		MEMCPY (target_desc->buffer.pointer, buffer, length);
	}

	else {
		/*
		 * Truncate the source, copy only what will fit
		 */
		MEMCPY (target_desc->buffer.pointer, buffer, target_desc->buffer.length);

		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Truncating src buffer from %X to %X\n",
			length, target_desc->buffer.length));
	}

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_copy_string_to_string
 *
 * PARAMETERS:  Source_desc         - Source object to copy
 *              Target_desc         - Destination object of the copy
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Copy a String object to another String object
 *
 ******************************************************************************/

acpi_status
acpi_ex_copy_string_to_string (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *target_desc)
{
	u32                     length;
	u8                      *buffer;


	FUNCTION_ENTRY ();


	/*
	 * We know that Source_desc is a string by now.
	 */
	buffer = (u8 *) source_desc->string.pointer;
	length = source_desc->string.length;

	/*
	 * Setting a string value replaces the old string
	 */
	if (length < target_desc->string.length) {
		/* Clear old string and copy in the new one */

		MEMSET (target_desc->string.pointer, 0, target_desc->string.length);
		MEMCPY (target_desc->string.pointer, buffer, length);
	}

	else {
		/*
		 * Free the current buffer, then allocate a buffer
		 * large enough to hold the value
		 */
		if (target_desc->string.pointer &&
		   (!(target_desc->common.flags & AOPOBJ_STATIC_POINTER))) {
			/*
			 * Only free if not a pointer into the DSDT
			 */
			ACPI_MEM_FREE (target_desc->string.pointer);
		}

		target_desc->string.pointer = ACPI_MEM_ALLOCATE (length + 1);
		if (!target_desc->string.pointer) {
			return (AE_NO_MEMORY);
		}

		target_desc->string.length = length;
		MEMCPY (target_desc->string.pointer, buffer, length);
	}

	return (AE_OK);
}


