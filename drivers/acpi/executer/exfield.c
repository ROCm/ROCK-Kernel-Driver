/******************************************************************************
 *
 * Module Name: exfield - ACPI AML (p-code) execution - field manipulation
 *              $Revision: 95 $
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
#include "acdispat.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "achware.h"
#include "acevents.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exfield")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_read_data_from_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Number of bytes to transfer
 *
 * RETURN:      Status3
 *
 * DESCRIPTION: Read or write a named field
 *
 ******************************************************************************/

acpi_status
acpi_ex_read_data_from_field (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **ret_buffer_desc)
{
	acpi_status             status;
	acpi_operand_object     *buffer_desc;
	u32                     length;
	void                    *buffer;


	FUNCTION_TRACE_PTR ("Ex_read_data_from_field", obj_desc);


	/* Parameter validation */

	if (!obj_desc) {
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}

	/*
	 * Allocate a buffer for the contents of the field.
	 *
	 * If the field is larger than the size of an acpi_integer, create
	 * a BUFFER to hold it.  Otherwise, use an INTEGER.  This allows
	 * the use of arithmetic operators on the returned value if the
	 * field size is equal or smaller than an Integer.
	 *
	 * Note: Field.length is in bits.
	 */
	length = ROUND_BITS_UP_TO_BYTES (obj_desc->field.bit_length);

	if (length > sizeof (acpi_integer)) {
		/* Field is too large for an Integer, create a Buffer instead */

		buffer_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		if (!buffer_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Create the actual read buffer */

		buffer_desc->buffer.pointer = ACPI_MEM_CALLOCATE (length);
		if (!buffer_desc->buffer.pointer) {
			acpi_ut_remove_reference (buffer_desc);
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		buffer_desc->buffer.length = length;
		buffer = buffer_desc->buffer.pointer;
	}

	else {
		/* Field will fit within an Integer (normal case) */

		buffer_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
		if (!buffer_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		length = sizeof (buffer_desc->integer.value);
		buffer = &buffer_desc->integer.value;
	}


	/* Read from the appropriate field */

	switch (obj_desc->common.type) {
	case ACPI_TYPE_BUFFER_FIELD:
		status = acpi_ex_access_buffer_field (ACPI_READ, obj_desc, buffer, length);
		break;

	case INTERNAL_TYPE_REGION_FIELD:
		status = acpi_ex_access_region_field (ACPI_READ, obj_desc, buffer, length);
		break;

	case INTERNAL_TYPE_BANK_FIELD:
		status = acpi_ex_access_bank_field (ACPI_READ, obj_desc, buffer, length);
		break;

	case INTERNAL_TYPE_INDEX_FIELD:
		status = acpi_ex_access_index_field (ACPI_READ, obj_desc, buffer, length);
		break;

	default:
		status = AE_AML_INTERNAL;
	}


	if (ACPI_FAILURE (status)) {
		acpi_ut_remove_reference (buffer_desc);
	}

	else if (ret_buffer_desc) {
		*ret_buffer_desc = buffer_desc;
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_write_data_to_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Number of bytes to transfer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a named field
 *
 ******************************************************************************/


acpi_status
acpi_ex_write_data_to_field (
	acpi_operand_object     *source_desc,
	acpi_operand_object     *obj_desc)
{
	acpi_status             status;
	u32                     length;
	void                    *buffer;


	FUNCTION_TRACE_PTR ("Ex_write_data_to_field", obj_desc);


	/* Parameter validation */

	if (!source_desc || !obj_desc) {
		return_ACPI_STATUS (AE_AML_NO_OPERAND);
	}


	/*
	 * Get a pointer to the data to be written
	 */
	switch (source_desc->common.type) {
	case ACPI_TYPE_INTEGER:
		buffer = &source_desc->integer.value;
		length = sizeof (source_desc->integer.value);
		break;

	case ACPI_TYPE_BUFFER:
		buffer = source_desc->buffer.pointer;
		length = source_desc->buffer.length;
		break;

	case ACPI_TYPE_STRING:
		buffer = source_desc->string.pointer;
		length = source_desc->string.length;
		break;

	default:
		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}


	/*
	 * Decode the type of field to be written
	 */
	switch (obj_desc->common.type) {
	case ACPI_TYPE_BUFFER_FIELD:
		status = acpi_ex_access_buffer_field (ACPI_WRITE, obj_desc, buffer, length);
		break;

	case INTERNAL_TYPE_REGION_FIELD:
		status = acpi_ex_access_region_field (ACPI_WRITE, obj_desc, buffer, length);
		break;

	case INTERNAL_TYPE_BANK_FIELD:
		status = acpi_ex_access_bank_field (ACPI_WRITE, obj_desc, buffer, length);
		break;

	case INTERNAL_TYPE_INDEX_FIELD:
		status = acpi_ex_access_index_field (ACPI_WRITE, obj_desc, buffer, length);
		break;

	default:
		return_ACPI_STATUS (AE_AML_INTERNAL);
	}


	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_access_buffer_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Number of bytes to transfer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a named field
 *
 ******************************************************************************/

acpi_status
acpi_ex_access_buffer_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ex_access_buffer_field", obj_desc);


	/*
	 * If the Buffer_field arguments have not been previously evaluated,
	 * evaluate them now and save the results.
	 */
	if (!(obj_desc->common.flags & AOPOBJ_DATA_VALID)) {
		status = acpi_ds_get_buffer_field_arguments (obj_desc);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}


	status = acpi_ex_common_access_field (mode, obj_desc, buffer, buffer_length);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_access_region_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Number of bytes to transfer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a named field
 *
 ******************************************************************************/

acpi_status
acpi_ex_access_region_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;
	u8                      locked;


	FUNCTION_TRACE_PTR ("Ex_access_region_field", obj_desc);


	/*
	 * Get the global lock if needed
	 */
	locked = acpi_ex_acquire_global_lock (obj_desc->field.lock_rule);

	status = acpi_ex_common_access_field (mode, obj_desc, buffer, buffer_length);


	/*
	 * Release global lock if we acquired it earlier
	 */
	acpi_ex_release_global_lock (locked);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_access_bank_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Number of bytes to transfer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a Bank Field
 *
 ******************************************************************************/

acpi_status
acpi_ex_access_bank_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;
	u8                      locked;


	FUNCTION_TRACE_PTR ("Ex_access_bank_field", obj_desc);


	/*
	 * Get the global lock if needed
	 */
	locked = acpi_ex_acquire_global_lock (obj_desc->bank_field.lock_rule);


	/*
	 * Write the Bank_value to the Bank_register to select the bank.
	 * The Bank_value for this Bank_field is specified in the
	 * Bank_field ASL declaration. The Bank_register is always a Field in
	 * an operation region.
	 */
	status = acpi_ex_common_access_field (ACPI_WRITE,
			 obj_desc->bank_field.bank_register_obj,
			 &obj_desc->bank_field.value,
			 sizeof (obj_desc->bank_field.value));
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/*
	 * The bank was successfully selected, now read or write the actual
	 * data.
	 */
	status = acpi_ex_common_access_field (mode, obj_desc, buffer, buffer_length);


cleanup:
	/*
	 * Release global lock if we acquired it earlier
	 */
	acpi_ex_release_global_lock (locked);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_access_index_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Number of bytes to transfer
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a Index Field
 *
 ******************************************************************************/

acpi_status
acpi_ex_access_index_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;
	u8                      locked;


	FUNCTION_TRACE_PTR ("Ex_access_index_field", obj_desc);


	/*
	 * Get the global lock if needed
	 */
	locked = acpi_ex_acquire_global_lock (obj_desc->index_field.lock_rule);


	/*
	 * Set Index value to select proper Data register
	 */
	status = acpi_ex_common_access_field (ACPI_WRITE,
			 obj_desc->index_field.index_obj,
			 &obj_desc->index_field.value,
			 sizeof (obj_desc->index_field.value));
	if (ACPI_FAILURE (status)) {
		goto cleanup;
	}

	/* Now read/write the data register */

	status = acpi_ex_common_access_field (mode, obj_desc->index_field.data_obj,
			  buffer, buffer_length);

cleanup:
	/*
	 * Release global lock if we acquired it earlier
	 */
	acpi_ex_release_global_lock (locked);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_common_access_field
 *
 * PARAMETERS:  Mode                - ACPI_READ or ACPI_WRITE
 *              *Field_node         - Parent node for field to be accessed
 *              *Buffer             - Value(s) to be read or written
 *              Buffer_length       - Size of buffer, in bytes.  Must be large
 *                                    enough for all bits of the field.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or write a named field
 *
 ******************************************************************************/

acpi_status
acpi_ex_common_access_field (
	u32                     mode,
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;


	FUNCTION_TRACE_PTR ("Ex_common_access_field", obj_desc);


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Obj=%p Type=%X Buf=%p Len=%X\n",
		obj_desc, obj_desc->common.type, buffer, buffer_length));
	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Mode=%d Bit_len=%X Bit_off=%X Byte_off=%X\n",
		mode, obj_desc->common_field.bit_length,
		obj_desc->common_field.start_field_bit_offset,
		obj_desc->common_field.base_byte_offset));


	/* Perform the actual read or write of the field */

	switch (mode) {
	case ACPI_READ:

		status = acpi_ex_extract_from_field (obj_desc, buffer, buffer_length);
		break;


	case ACPI_WRITE:

		status = acpi_ex_insert_into_field (obj_desc, buffer, buffer_length);
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Unknown I/O Mode: %X\n", mode));
		status = AE_BAD_PARAMETER;
		break;
	}


	return_ACPI_STATUS (status);
}

