
/******************************************************************************
 *
 * Module Name: amstorob - AML Interpreter object store support, store to object
 *              $Revision: 18 $
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
#include "acparser.h"
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "actables.h"


#define _COMPONENT          INTERPRETER
	 MODULE_NAME         ("amstorob")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_store_object_to_object
 *
 * PARAMETERS:  *Val_desc           - Value to be stored
 *              *Dest_desc          - Object to receive the value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store an object to another object.
 *
 *              The Assignment of an object to another (not named) object
 *              is handled here.
 *              The val passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              This module allows destination types of Number, String,
 *              and Buffer.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_store_object_to_object (
	ACPI_OPERAND_OBJECT     *val_desc,
	ACPI_OPERAND_OBJECT     *dest_desc,
	ACPI_WALK_STATE         *walk_state)
{
	ACPI_STATUS             status = AE_OK;
	u8                      *buffer = NULL;
	u32                     length = 0;
	OBJECT_TYPE_INTERNAL    destination_type = dest_desc->common.type;


	/*
	 *  Assuming the parameters are valid!!!
	 */
	ACPI_ASSERT((dest_desc) && (val_desc));

	/*
	 *  First ensure we have a value that can be stored in the target
	 */
	switch (destination_type)
	{
		/* Type of Name's existing value */

	case ACPI_TYPE_NUMBER:

		/*
		 *  These cases all require only number values or values that
		 *  can be converted to numbers.
		 *
		 *  If value is not a Number, try to resolve it to one.
		 */

		if (val_desc->common.type != ACPI_TYPE_NUMBER) {
			/*
			 *  Initially not a number, convert
			 */
			status = acpi_aml_resolve_to_value (&val_desc, walk_state);
			if (ACPI_SUCCESS (status) &&
				(val_desc->common.type != ACPI_TYPE_NUMBER))
			{
				/*
				 *  Conversion successful but still not a number
				 */
				status = AE_AML_OPERAND_TYPE;
			}
		}

		break;

	case ACPI_TYPE_STRING:
	case ACPI_TYPE_BUFFER:

		/*
		 *  Storing into a Field in a region or into a buffer or into
		 *  a string all is essentially the same.
		 *
		 *  If value is not a valid type, try to resolve it to one.
		 */

		if ((val_desc->common.type != ACPI_TYPE_NUMBER) &&
			(val_desc->common.type != ACPI_TYPE_BUFFER) &&
			(val_desc->common.type != ACPI_TYPE_STRING))
		{
			/*
			 *  Initially not a valid type, convert
			 */
			status = acpi_aml_resolve_to_value (&val_desc, walk_state);
			if (ACPI_SUCCESS (status) &&
				(val_desc->common.type != ACPI_TYPE_NUMBER) &&
				(val_desc->common.type != ACPI_TYPE_BUFFER) &&
				(val_desc->common.type != ACPI_TYPE_STRING))
			{
				/*
				 *  Conversion successful but still not a valid type
				 */
				status = AE_AML_OPERAND_TYPE;
			}
		}
		break;


	default:

		/*
		 * TBD: [Unhandled] What other combinations must be implemented?
		 */
		status = AE_NOT_IMPLEMENTED;
		break;
	}

	/* Exit now if failure above */

	if (ACPI_FAILURE (status)) {
		goto clean_up_and_bail_out;
	}

	/*
	 * Acpi_everything is ready to execute now, We have
	 * a value we can handle, just perform the update
	 */

	switch (destination_type)
	{

	case ACPI_TYPE_STRING:

		/*
		 *  Perform the update
		 */

		switch (val_desc->common.type)
		{
		case ACPI_TYPE_NUMBER:
			buffer = (u8 *) &val_desc->number.value;
			length = sizeof (val_desc->number.value);
			break;

		case ACPI_TYPE_BUFFER:
			buffer = (u8 *) val_desc->buffer.pointer;
			length = val_desc->buffer.length;
			break;

		case ACPI_TYPE_STRING:
			buffer = (u8 *) val_desc->string.pointer;
			length = val_desc->string.length;
			break;
		}

		/*
		 *  Setting a string value replaces the old string
		 */

		if (length < dest_desc->string.length) {
			/*
			 *  Zero fill, not willing to do pointer arithmetic for
			 *  architecture independence.  Just clear the whole thing
			 */
			MEMSET(dest_desc->string.pointer, 0, dest_desc->string.length);
			MEMCPY(dest_desc->string.pointer, buffer, length);
		}
		else {
			/*
			 *  Free the current buffer, then allocate a buffer
			 *  large enough to hold the value
			 */
			if ( dest_desc->string.pointer &&
				!acpi_tb_system_table_pointer (dest_desc->string.pointer))
			{
				/*
				 *  Only free if not a pointer into the DSDT
				 */

				acpi_cm_free(dest_desc->string.pointer);
			}

			dest_desc->string.pointer = acpi_cm_allocate (length + 1);
			dest_desc->string.length = length;

			if (!dest_desc->string.pointer) {
				status = AE_NO_MEMORY;
				goto clean_up_and_bail_out;
			}

			MEMCPY(dest_desc->string.pointer, buffer, length);
		}
		break;


	case ACPI_TYPE_BUFFER:

		/*
		 *  Perform the update to the buffer
		 */

		switch (val_desc->common.type)
		{
		case ACPI_TYPE_NUMBER:
			buffer = (u8 *) &val_desc->number.value;
			length = sizeof (val_desc->number.value);
			break;

		case ACPI_TYPE_BUFFER:
			buffer = (u8 *) val_desc->buffer.pointer;
			length = val_desc->buffer.length;
			break;

		case ACPI_TYPE_STRING:
			buffer = (u8 *) val_desc->string.pointer;
			length = val_desc->string.length;
			break;
		}

		/*
		 * If the buffer is uninitialized,
		 *  memory needs to be allocated for the copy.
		 */
		if(0 == dest_desc->buffer.length) {
			dest_desc->buffer.pointer = acpi_cm_callocate(length);
			dest_desc->buffer.length = length;

			if (!dest_desc->buffer.pointer) {
				status = AE_NO_MEMORY;
				goto clean_up_and_bail_out;
			}
		}

		/*
		 *  Buffer is a static allocation,
		 *  only place what will fit in the buffer.
		 */
		if (length <= dest_desc->buffer.length) {
			/*
			 *  Zero fill first, not willing to do pointer arithmetic for
			 *  architecture independence.  Just clear the whole thing
			 */
			MEMSET(dest_desc->buffer.pointer, 0, dest_desc->buffer.length);
			MEMCPY(dest_desc->buffer.pointer, buffer, length);
		}
		else {
			/*
			 *  truncate, copy only what will fit
			 */
			MEMCPY(dest_desc->buffer.pointer, buffer, dest_desc->buffer.length);
		}
		break;

	case ACPI_TYPE_NUMBER:

		dest_desc->number.value = val_desc->number.value;

		/* Truncate value if we are executing from a 32-bit ACPI table */

		acpi_aml_truncate_for32bit_table (dest_desc, walk_state);
		break;

	default:

		/*
		 * All other types than Alias and the various Fields come here.
		 * Store Val_desc as the new value of the Name, and set
		 * the Name's type to that of the value being stored in it.
		 * Val_desc reference count is incremented by Attach_object.
		 */

		status = AE_NOT_IMPLEMENTED;
		break;
	}

clean_up_and_bail_out:

	return (status);
}

