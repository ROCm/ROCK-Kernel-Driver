
/******************************************************************************
 *
 * Module Name: amstoren - AML Interpreter object store support,
 *                         Store to Node (namespace object)
 *              $Revision: 24 $
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
	 MODULE_NAME         ("amstoren")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_aml_store_object_to_node
 *
 * PARAMETERS:  *Val_desc           - Value to be stored
 *              *Node           - Named object to recieve the value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the object to the named object.
 *
 *              The Assignment of an object to a named object is handled here
 *              The val passed in will replace the current value (if any)
 *              with the input value.
 *
 *              When storing into an object the data is converted to the
 *              target object type then stored in the object.  This means
 *              that the target object type (for an initialized target) will
 *              not be changed by a store operation.
 *
 *              NOTE: the global lock is acquired early.  This will result
 *              in the global lock being held a bit longer.  Also, if the
 *              function fails during set up we may get the lock when we
 *              don't really need it.  I don't think we care.
 *
 ******************************************************************************/

ACPI_STATUS
acpi_aml_store_object_to_node (
	ACPI_OPERAND_OBJECT     *val_desc,
	ACPI_NAMESPACE_NODE     *node,
	ACPI_WALK_STATE         *walk_state)
{
	ACPI_STATUS             status = AE_OK;
	u8                      *buffer = NULL;
	u32                     length = 0;
	u32                     mask;
	u32                     new_value;
	u8                      locked = FALSE;
	u8                      *location=NULL;
	ACPI_OPERAND_OBJECT     *dest_desc;
	OBJECT_TYPE_INTERNAL    destination_type = ACPI_TYPE_ANY;


	/*
	 *  Assuming the parameters are valid!!!
	 */
	ACPI_ASSERT((node) && (val_desc));

	destination_type = acpi_ns_get_type (node);

	/*
	 *  First ensure we have a value that can be stored in the target
	 */
	switch (destination_type)
	{
		/* Type of Name's existing value */

	case INTERNAL_TYPE_ALIAS:

		/*
		 *  Aliases are resolved by Acpi_aml_prep_operands
		 */

		status = AE_AML_INTERNAL;
		break;


	case INTERNAL_TYPE_BANK_FIELD:
	case INTERNAL_TYPE_INDEX_FIELD:
	case ACPI_TYPE_FIELD_UNIT:
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
	case INTERNAL_TYPE_DEF_FIELD:

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


	case ACPI_TYPE_PACKAGE:

		/*
		 *  TBD: [Unhandled] Not real sure what to do here
		 */
		status = AE_NOT_IMPLEMENTED;
		break;


	default:

		/*
		 * All other types than Alias and the various Fields come here.
		 * Store Val_desc as the new value of the Name, and set
		 * the Name's type to that of the value being stored in it.
		 * Val_desc reference count is incremented by Attach_object.
		 */

		status = acpi_ns_attach_object (node, val_desc, val_desc->common.type);

		goto clean_up_and_bail_out;
		break;
	}

	/* Exit now if failure above */

	if (ACPI_FAILURE (status)) {
		goto clean_up_and_bail_out;
	}

	/*
	 *  Get descriptor for object attached to Node
	 */
	dest_desc = acpi_ns_get_attached_object (node);
	if (!dest_desc) {
		/*
		 *  There is no existing object attached to this Node
		 */
		status = AE_AML_INTERNAL;
		goto clean_up_and_bail_out;
	}

	/*
	 *  Make sure the destination Object is the same as the Node
	 */
	if (dest_desc->common.type != (u8) destination_type) {
		status = AE_AML_INTERNAL;
		goto clean_up_and_bail_out;
	}

	/*
	 * Acpi_everything is ready to execute now, We have
	 * a value we can handle, just perform the update
	 */

	switch (destination_type)
	{
		/* Type of Name's existing value */

	case INTERNAL_TYPE_BANK_FIELD:

		/*
		 * Get the global lock if needed
		 */
		locked = acpi_aml_acquire_global_lock (dest_desc->bank_field.lock_rule);

		/*
		 *  Set Bank value to select proper Bank
		 *  Perform the update (Set Bank Select)
		 */

		status = acpi_aml_access_named_field (ACPI_WRITE,
				 dest_desc->bank_field.bank_select,
				 &dest_desc->bank_field.value,
				 sizeof (dest_desc->bank_field.value));
		if (ACPI_SUCCESS (status)) {
			/* Set bank select successful, set data value  */

			status = acpi_aml_access_named_field (ACPI_WRITE,
					   dest_desc->bank_field.bank_select,
					   &val_desc->bank_field.value,
					   sizeof (val_desc->bank_field.value));
		}

		break;


	case INTERNAL_TYPE_DEF_FIELD:

		/*
		 * Get the global lock if needed
		 */
		locked = acpi_aml_acquire_global_lock (val_desc->field.lock_rule);

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

		status = acpi_aml_access_named_field (ACPI_WRITE,
				  node, buffer, length);

		break;      /* Global Lock released below   */


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
			 *  archetecture independance.  Just clear the whole thing
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
		 *  Buffer is a static allocation,
		 *  only place what will fit in the buffer.
		 */
		if (length <= dest_desc->buffer.length) {
			/*
			 *  Zero fill first, not willing to do pointer arithmetic for
			 *  archetecture independence.  Just clear the whole thing
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


	case INTERNAL_TYPE_INDEX_FIELD:

		/*
		 * Get the global lock if needed
		 */
		locked = acpi_aml_acquire_global_lock (dest_desc->index_field.lock_rule);

		/*
		 *  Set Index value to select proper Data register
		 *  perform the update (Set index)
		 */

		status = acpi_aml_access_named_field (ACPI_WRITE,
				 dest_desc->index_field.index,
				 &dest_desc->index_field.value,
				 sizeof (dest_desc->index_field.value));

		if (ACPI_SUCCESS (status)) {
			/* set index successful, next set Data value */

			status = acpi_aml_access_named_field (ACPI_WRITE,
					   dest_desc->index_field.data,
					   &val_desc->number.value,
					   sizeof (val_desc->number.value));
		}
		break;


	case ACPI_TYPE_FIELD_UNIT:


		/*
		 * If the Field Buffer and Index have not been previously evaluated,
		 * evaluate them and save the results.
		 */
		if (!(dest_desc->common.flags & AOPOBJ_DATA_VALID)) {
			status = acpi_ds_get_field_unit_arguments (dest_desc);
			if (ACPI_FAILURE (status)) {
				return (status);
			}
		}

		if ((!dest_desc->field_unit.container ||
			ACPI_TYPE_BUFFER != dest_desc->field_unit.container->common.type))
		{
			status = AE_AML_INTERNAL;
			goto clean_up_and_bail_out;
		}

		/*
		 *  Get the global lock if needed
		 */
		locked = acpi_aml_acquire_global_lock (dest_desc->field_unit.lock_rule);

		/*
		 * TBD: [Unhandled] REMOVE this limitation
		 * Make sure the operation is within the limits of our implementation
		 * this is not a Spec limitation!!
		 */
		if (dest_desc->field_unit.length + dest_desc->field_unit.bit_offset > 32) {
			status = AE_NOT_IMPLEMENTED;
			goto clean_up_and_bail_out;
		}

		/* Field location is (base of buffer) + (byte offset) */

		location = dest_desc->field_unit.container->buffer.pointer
				  + dest_desc->field_unit.offset;

		/*
		 * Construct Mask with 1 bits where the field is,
		 * 0 bits elsewhere
		 */
		mask = ((u32) 1 << dest_desc->field_unit.length) - ((u32)1
				   << dest_desc->field_unit.bit_offset);

		/* Zero out the field in the buffer */

		MOVE_UNALIGNED32_TO_32 (&new_value, location);
		new_value &= ~mask;

		/*
		 * Shift and mask the new value into position,
		 * and or it into the buffer.
		 */
		new_value |= (val_desc->number.value << dest_desc->field_unit.bit_offset) &
				 mask;

		/* Store back the value */

		MOVE_UNALIGNED32_TO_32 (location, &new_value);

		break;


	case ACPI_TYPE_NUMBER:


		dest_desc->number.value = val_desc->number.value;

		/* Truncate value if we are executing from a 32-bit ACPI table */

		acpi_aml_truncate_for32bit_table (dest_desc, walk_state);
		break;


	case ACPI_TYPE_PACKAGE:

		/*
		 *  TBD: [Unhandled] Not real sure what to do here
		 */
		status = AE_NOT_IMPLEMENTED;
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

	/*
	 * Release global lock if we acquired it earlier
	 */
	acpi_aml_release_global_lock (locked);

	return (status);
}


