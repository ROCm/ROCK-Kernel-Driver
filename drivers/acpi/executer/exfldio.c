/******************************************************************************
 *
 * Module Name: exfldio - Aml Field I/O
 *              $Revision: 88 $
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
#include "acinterp.h"
#include "amlcode.h"
#include "acevents.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exfldio")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_setup_region
 *
 * PARAMETERS:  *Obj_desc               - Field to be read or written
 *              Field_datum_byte_offset - Byte offset of this datum within the
 *                                        parent field
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Common processing for Acpi_ex_extract_from_field and
 *              Acpi_ex_insert_into_field. Initialize the
 *
 ******************************************************************************/

acpi_status
acpi_ex_setup_region (
	acpi_operand_object     *obj_desc,
	u32                     field_datum_byte_offset)
{
	acpi_status             status = AE_OK;
	acpi_operand_object     *rgn_desc;


	ACPI_FUNCTION_TRACE_U32 ("Ex_setup_region", field_datum_byte_offset);


	rgn_desc = obj_desc->common_field.region_obj;

	if (ACPI_GET_OBJECT_TYPE (rgn_desc) != ACPI_TYPE_REGION) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Needed Region, found type %X (%s)\n",
			ACPI_GET_OBJECT_TYPE (rgn_desc),
			acpi_ut_get_object_type_name (rgn_desc)));

		return_ACPI_STATUS (AE_AML_OPERAND_TYPE);
	}

	/*
	 * If the Region Address and Length have not been previously evaluated,
	 * evaluate them now and save the results.
	 */
	if (!(rgn_desc->region.flags & AOPOBJ_DATA_VALID)) {
		status = acpi_ds_get_region_arguments (rgn_desc);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
	}

	/*
	 * Validate the request.  The entire request from the byte offset for a
	 * length of one field datum (access width) must fit within the region.
	 * (Region length is specified in bytes)
	 */
	if (rgn_desc->region.length < (obj_desc->common_field.base_byte_offset
			   + field_datum_byte_offset
			   + obj_desc->common_field.access_byte_width)) {
		if (rgn_desc->region.length < obj_desc->common_field.access_byte_width) {
			/*
			 * This is the case where the Access_type (Acc_word, etc.) is wider
			 * than the region itself.  For example, a region of length one
			 * byte, and a field with Dword access specified.
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Field [%4.4s] access width (%d bytes) too large for region [%4.4s] (length %X)\n",
				obj_desc->common_field.node->name.ascii, obj_desc->common_field.access_byte_width,
				rgn_desc->region.node->name.ascii, rgn_desc->region.length));
		}

		/*
		 * Offset rounded up to next multiple of field width
		 * exceeds region length, indicate an error
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Field [%4.4s] Base+Offset+Width %X+%X+%X is beyond end of region [%4.4s] (length %X)\n",
			obj_desc->common_field.node->name.ascii, obj_desc->common_field.base_byte_offset,
			field_datum_byte_offset, obj_desc->common_field.access_byte_width,
			rgn_desc->region.node->name.ascii, rgn_desc->region.length));

		return_ACPI_STATUS (AE_AML_REGION_LIMIT);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_access_region
 *
 * PARAMETERS:  *Obj_desc               - Field to be read
 *              Field_datum_byte_offset - Byte offset of this datum within the
 *                                        parent field
 *              *Value                  - Where to store value (must be 32 bits)
 *              Read_write              - Read or Write flag
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or Write a single field datum to an Operation Region.
 *
 ******************************************************************************/

acpi_status
acpi_ex_access_region (
	acpi_operand_object     *obj_desc,
	u32                     field_datum_byte_offset,
	acpi_integer            *value,
	u32                     read_write)
{
	acpi_status             status;
	acpi_operand_object     *rgn_desc;
	ACPI_PHYSICAL_ADDRESS   address;


	ACPI_FUNCTION_TRACE ("Ex_access_region");


	/*
	 * The physical address of this field datum is:
	 *
	 * 1) The base of the region, plus
	 * 2) The base offset of the field, plus
	 * 3) The current offset into the field
	 */
	rgn_desc = obj_desc->common_field.region_obj;
	address = rgn_desc->region.address
			 + obj_desc->common_field.base_byte_offset
			 + field_datum_byte_offset;

	if (read_write == ACPI_READ) {
		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "[READ]"));
	}
	else {
		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "[WRITE]"));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_BFIELD,
		" Region[%s-%X] Access %X Base:Off %X:%X at %8.8X%8.8X\n",
		acpi_ut_get_region_name (rgn_desc->region.space_id),
		rgn_desc->region.space_id,
		obj_desc->common_field.access_byte_width,
		obj_desc->common_field.base_byte_offset,
		field_datum_byte_offset,
		ACPI_HIDWORD (address), ACPI_LODWORD (address)));

	/* Invoke the appropriate Address_space/Op_region handler */

	status = acpi_ev_address_space_dispatch (rgn_desc, read_write,
			  address, ACPI_MUL_8 (obj_desc->common_field.access_byte_width), value);

	if (ACPI_FAILURE (status)) {
		if (status == AE_NOT_IMPLEMENTED) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Region %s(%X) not implemented\n",
				acpi_ut_get_region_name (rgn_desc->region.space_id),
				rgn_desc->region.space_id));
		}

		else if (status == AE_NOT_EXIST) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Region %s(%X) has no handler\n",
				acpi_ut_get_region_name (rgn_desc->region.space_id),
				rgn_desc->region.space_id));
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_register_overflow
 *
 * PARAMETERS:  *Obj_desc               - Register(Field) to be written
 *              Value                   - Value to be stored
 *
 * RETURN:      TRUE if value overflows the field, FALSE otherwise
 *
 * DESCRIPTION: Check if a value is out of range of the field being written.
 *              Used to check if the values written to Index and Bank registers
 *              are out of range.  Normally, the value is simply truncated
 *              to fit the field, but this case is most likely a serious
 *              coding error in the ASL.
 *
 ******************************************************************************/

u8
acpi_ex_register_overflow (
	acpi_operand_object     *obj_desc,
	acpi_integer            value)
{

	if (obj_desc->common_field.bit_length >= ACPI_INTEGER_BIT_SIZE) {
		/*
		 * The field is large enough to hold the maximum integer, so we can
		 * never overflow it.
		 */
		return (FALSE);
	}

	if (value >= ((acpi_integer) 1 << obj_desc->common_field.bit_length)) {
		/*
		 * The Value is larger than the maximum value that can fit into
		 * the register.
		 */
		return (TRUE);
	}

	/* The Value will fit into the field with no truncation */

	return (FALSE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_field_datum_io
 *
 * PARAMETERS:  *Obj_desc               - Field to be read
 *              Field_datum_byte_offset - Byte offset of this datum within the
 *                                        parent field
 *              *Value                  - Where to store value (must be 64 bits)
 *              Read_write              - Read or Write flag
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Read or Write a single datum of a field.  The Field_type is
 *              demultiplexed here to handle the different types of fields
 *              (Buffer_field, Region_field, Index_field, Bank_field)
 *
 ******************************************************************************/

acpi_status
acpi_ex_field_datum_io (
	acpi_operand_object     *obj_desc,
	u32                     field_datum_byte_offset,
	acpi_integer            *value,
	u32                     read_write)
{
	acpi_status             status;
	acpi_integer            local_value;


	ACPI_FUNCTION_TRACE_U32 ("Ex_field_datum_io", field_datum_byte_offset);


	if (read_write == ACPI_READ) {
		if (!value) {
			local_value = 0;
			value = &local_value; /* To support reads without saving return value */
		}

		/* Clear the entire return buffer first, [Very Important!] */

		*value = 0;
	}

	/*
	 * The four types of fields are:
	 *
	 * Buffer_fields - Read/write from/to a Buffer
	 * Region_fields - Read/write from/to a Operation Region.
	 * Bank_fields  - Write to a Bank Register, then read/write from/to an Op_region
	 * Index_fields - Write to an Index Register, then read/write from/to a Data Register
	 */
	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_BUFFER_FIELD:
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

		if (read_write == ACPI_READ) {
			/*
			 * Copy the data from the source buffer.
			 * Length is the field width in bytes.
			 */
			ACPI_MEMCPY (value, (obj_desc->buffer_field.buffer_obj)->buffer.pointer
					  + obj_desc->buffer_field.base_byte_offset
					  + field_datum_byte_offset,
					  obj_desc->common_field.access_byte_width);
		}
		else {
			/*
			 * Copy the data to the target buffer.
			 * Length is the field width in bytes.
			 */
			ACPI_MEMCPY ((obj_desc->buffer_field.buffer_obj)->buffer.pointer
					+ obj_desc->buffer_field.base_byte_offset
					+ field_datum_byte_offset,
					value, obj_desc->common_field.access_byte_width);
		}

		status = AE_OK;
		break;


	case INTERNAL_TYPE_BANK_FIELD:

		/* Ensure that the Bank_value is not beyond the capacity of the register */

		if (acpi_ex_register_overflow (obj_desc->bank_field.bank_obj,
				  (acpi_integer) obj_desc->bank_field.value)) {
			return_ACPI_STATUS (AE_AML_REGISTER_LIMIT);
		}

		/*
		 * For Bank_fields, we must write the Bank_value to the Bank_register
		 * (itself a Region_field) before we can access the data.
		 */
		status = acpi_ex_insert_into_field (obj_desc->bank_field.bank_obj,
				 &obj_desc->bank_field.value,
				 sizeof (obj_desc->bank_field.value));
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Now that the Bank has been selected, fall through to the
		 * Region_field case and write the datum to the Operation Region
		 */

		/*lint -fallthrough */


	case INTERNAL_TYPE_REGION_FIELD:
		/*
		 * For simple Region_fields, we just directly access the owning
		 * Operation Region.
		 */
		status = acpi_ex_setup_region (obj_desc, field_datum_byte_offset);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		status = acpi_ex_access_region (obj_desc, field_datum_byte_offset, value,
				  read_write);
		break;


	case INTERNAL_TYPE_INDEX_FIELD:


		/* Ensure that the Index_value is not beyond the capacity of the register */

		if (acpi_ex_register_overflow (obj_desc->index_field.index_obj,
				  (acpi_integer) obj_desc->index_field.value)) {
			return_ACPI_STATUS (AE_AML_REGISTER_LIMIT);
		}

		/* Write the index value to the Index_register (itself a Region_field) */

		status = acpi_ex_insert_into_field (obj_desc->index_field.index_obj,
				 &obj_desc->index_field.value,
				 sizeof (obj_desc->index_field.value));
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		if (read_write == ACPI_READ) {
			/* Read the datum from the Data_register */

			status = acpi_ex_extract_from_field (obj_desc->index_field.data_obj,
					  value, obj_desc->common_field.access_byte_width);
		}
		else {
			/* Write the datum to the Data register */

			status = acpi_ex_insert_into_field (obj_desc->index_field.data_obj,
					  value, obj_desc->common_field.access_byte_width);
		}
		break;


	default:

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "%p, Wrong object type - %s\n",
			obj_desc, acpi_ut_get_object_type_name (obj_desc)));
		status = AE_AML_INTERNAL;
		break;
	}

	if (ACPI_SUCCESS (status)) {
		if (read_write == ACPI_READ) {
			ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "Value Read=%8.8X%8.8X\n",
					   ACPI_HIDWORD (*value), ACPI_LODWORD (*value)));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "Value Written=%8.8X%8.8X\n",
					   ACPI_HIDWORD (*value), ACPI_LODWORD (*value)));
		}
	}

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_write_with_update_rule
 *
 * PARAMETERS:  *Obj_desc           - Field to be set
 *              Value               - Value to store
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Apply the field update rule to a field write
 *
 ******************************************************************************/

acpi_status
acpi_ex_write_with_update_rule (
	acpi_operand_object     *obj_desc,
	acpi_integer            mask,
	acpi_integer            field_value,
	u32                     field_datum_byte_offset)
{
	acpi_status             status = AE_OK;
	acpi_integer            merged_value;
	acpi_integer            current_value;


	ACPI_FUNCTION_TRACE_U32 ("Ex_write_with_update_rule", mask);


	/* Start with the new bits  */

	merged_value = field_value;

	/* If the mask is all ones, we don't need to worry about the update rule */

	if (mask != ACPI_INTEGER_MAX) {
		/* Decode the update rule */

		switch (obj_desc->common_field.field_flags & AML_FIELD_UPDATE_RULE_MASK) {
		case AML_FIELD_UPDATE_PRESERVE:
			/*
			 * Check if update rule needs to be applied (not if mask is all
			 * ones)  The left shift drops the bits we want to ignore.
			 */
			if ((~mask << (ACPI_MUL_8 (sizeof (mask)) -
					 ACPI_MUL_8 (obj_desc->common_field.access_byte_width))) != 0) {
				/*
				 * Read the current contents of the byte/word/dword containing
				 * the field, and merge with the new field value.
				 */
				status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
						  &current_value, ACPI_READ);
				merged_value |= (current_value & ~mask);
			}
			break;

		case AML_FIELD_UPDATE_WRITE_AS_ONES:

			/* Set positions outside the field to all ones */

			merged_value |= ~mask;
			break;

		case AML_FIELD_UPDATE_WRITE_AS_ZEROS:

			/* Set positions outside the field to all zeros */

			merged_value &= mask;
			break;

		default:
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Write_with_update_rule: Unknown Update_rule setting: %X\n",
				(obj_desc->common_field.field_flags & AML_FIELD_UPDATE_RULE_MASK)));
			return_ACPI_STATUS (AE_AML_OPERAND_VALUE);
		}
	}

	/* Write the merged value */

	status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
			  &merged_value, ACPI_WRITE);

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Mask %8.8X%8.8X Datum_offset %X Value %8.8X%8.8X, Merged_value %8.8X%8.8X\n",
		ACPI_HIDWORD (mask), ACPI_LODWORD (mask),
		field_datum_byte_offset,
		ACPI_HIDWORD (field_value), ACPI_LODWORD (field_value),
		ACPI_HIDWORD (merged_value),ACPI_LODWORD (merged_value)));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_get_buffer_datum
 *
 * PARAMETERS:  Datum               - Where the Datum is returned
 *              Buffer              - Raw field buffer
 *              Byte_granularity    - 1/2/4/8 Granularity of the field
 *                                    (aka Datum Size)
 *              Offset              - Datum offset into the buffer
 *
 * RETURN:      none
 *
 * DESCRIPTION: Get a datum from the buffer according to the buffer field
 *              byte granularity
 *
 ******************************************************************************/

void
acpi_ex_get_buffer_datum(
	acpi_integer            *datum,
	void                    *buffer,
	u32                     byte_granularity,
	u32                     offset)
{

	ACPI_FUNCTION_ENTRY ();


	switch (byte_granularity) {
	case ACPI_FIELD_BYTE_GRANULARITY:

		*datum = ((u8 *) buffer) [offset];
		break;

	case ACPI_FIELD_WORD_GRANULARITY:

		ACPI_MOVE_UNALIGNED16_TO_32 (datum, &(((u16 *) buffer) [offset]));
		break;

	case ACPI_FIELD_DWORD_GRANULARITY:

		ACPI_MOVE_UNALIGNED32_TO_32 (datum, &(((u32 *) buffer) [offset]));
		break;

	case ACPI_FIELD_QWORD_GRANULARITY:

		ACPI_MOVE_UNALIGNED64_TO_64 (datum, &(((u64 *) buffer) [offset]));
		break;

	default:
		/* Should not get here */
		break;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_set_buffer_datum
 *
 * PARAMETERS:  Merged_datum        - Value to store
 *              Buffer              - Receiving buffer
 *              Byte_granularity    - 1/2/4/8 Granularity of the field
 *                                    (aka Datum Size)
 *              Offset              - Datum offset into the buffer
 *
 * RETURN:      none
 *
 * DESCRIPTION: Store the merged datum to the buffer according to the
 *              byte granularity
 *
 ******************************************************************************/

void
acpi_ex_set_buffer_datum (
	acpi_integer            merged_datum,
	void                    *buffer,
	u32                     byte_granularity,
	u32                     offset)
{

	ACPI_FUNCTION_ENTRY ();


	switch (byte_granularity) {
	case ACPI_FIELD_BYTE_GRANULARITY:

		((u8 *) buffer) [offset] = (u8) merged_datum;
		break;

	case ACPI_FIELD_WORD_GRANULARITY:

		ACPI_MOVE_UNALIGNED16_TO_16 (&(((u16 *) buffer)[offset]), &merged_datum);
		break;

	case ACPI_FIELD_DWORD_GRANULARITY:

		ACPI_MOVE_UNALIGNED32_TO_32 (&(((u32 *) buffer)[offset]), &merged_datum);
		break;

	case ACPI_FIELD_QWORD_GRANULARITY:

		ACPI_MOVE_UNALIGNED64_TO_64 (&(((u64 *) buffer)[offset]), &merged_datum);
		break;

	default:
		/* Should not get here */
		break;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_extract_from_field
 *
 * PARAMETERS:  *Obj_desc           - Field to be read
 *              *Value              - Where to store value
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Retrieve the value of the given field
 *
 ******************************************************************************/

acpi_status
acpi_ex_extract_from_field (
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;
	u32                     field_datum_byte_offset;
	u32                     datum_offset;
	acpi_integer            previous_raw_datum;
	acpi_integer            this_raw_datum = 0;
	acpi_integer            merged_datum = 0;
	u32                     byte_field_length;
	u32                     datum_count;


	ACPI_FUNCTION_TRACE ("Ex_extract_from_field");


	/*
	 * The field must fit within the caller's buffer
	 */
	byte_field_length = ACPI_ROUND_BITS_UP_TO_BYTES (obj_desc->common_field.bit_length);
	if (byte_field_length > buffer_length) {
		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
			"Field size %X (bytes) too large for buffer (%X)\n",
			byte_field_length, buffer_length));

		return_ACPI_STATUS (AE_BUFFER_OVERFLOW);
	}

	/* Convert field byte count to datum count, round up if necessary */

	datum_count = ACPI_ROUND_UP_TO (byte_field_length,
			   obj_desc->common_field.access_byte_width);

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Byte_len=%X, Datum_len=%X, Byte_gran=%X\n",
		byte_field_length, datum_count,obj_desc->common_field.access_byte_width));

	/*
	 * Clear the caller's buffer (the whole buffer length as given)
	 * This is very important, especially in the cases where a byte is read,
	 * but the buffer is really a u32 (4 bytes).
	 */
	ACPI_MEMSET (buffer, 0, buffer_length);

	/* Read the first raw datum to prime the loop */

	field_datum_byte_offset = 0;
	datum_offset= 0;

	status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
			  &previous_raw_datum, ACPI_READ);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}


	/* We might actually be done if the request fits in one datum */

	if ((datum_count == 1) &&
		(obj_desc->common_field.flags & AOPOBJ_SINGLE_DATUM)) {
		/* 1) Shift the valid data bits down to start at bit 0 */

		merged_datum = (previous_raw_datum >> obj_desc->common_field.start_field_bit_offset);

		/* 2) Mask off any upper unused bits (bits not part of the field) */

		if (obj_desc->common_field.end_buffer_valid_bits) {
			merged_datum &= ACPI_MASK_BITS_ABOVE (obj_desc->common_field.end_buffer_valid_bits);
		}

		/* Store the datum to the caller buffer */

		acpi_ex_set_buffer_datum (merged_datum, buffer, obj_desc->common_field.access_byte_width,
				datum_offset);

		return_ACPI_STATUS (AE_OK);
	}


	/* We need to get more raw data to complete one or more field data */

	while (datum_offset < datum_count) {
		field_datum_byte_offset += obj_desc->common_field.access_byte_width;

		/*
		 * If the field is aligned on a byte boundary, we don't want
		 * to perform a final read, since this would potentially read
		 * past the end of the region.
		 *
		 * We could just split the aligned and non-aligned cases since the
		 * aligned case is so very simple, but this would require more code.
		 */
		if ((obj_desc->common_field.start_field_bit_offset != 0) ||
			((obj_desc->common_field.start_field_bit_offset == 0) &&
			(datum_offset < (datum_count -1)))) {
			/*
			 * Get the next raw datum, it contains some or all bits
			 * of the current field datum
			 */
			status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
					  &this_raw_datum, ACPI_READ);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}

		/*
		 * Create the (possibly) merged datum to be stored to the caller buffer
		 */
		if (obj_desc->common_field.start_field_bit_offset == 0) {
			/* Field is not skewed and we can just copy the datum */

			merged_datum = previous_raw_datum;
		}
		else {
			/*
			 * Put together the appropriate bits of the two raw data to make a
			 * single complete field datum
			 *
			 * 1) Normalize the first datum down to bit 0
			 */
			merged_datum = (previous_raw_datum >> obj_desc->common_field.start_field_bit_offset);

			/* 2) Insert the second datum "above" the first datum */

			merged_datum |= (this_raw_datum << obj_desc->common_field.datum_valid_bits);

			if ((datum_offset >= (datum_count -1))) {
				/*
				 * This is the last iteration of the loop.  We need to clear
				 * any unused bits (bits that are not part of this field) that
				 * came from the last raw datum before we store the final
				 * merged datum into the caller buffer.
				 */
				if (obj_desc->common_field.end_buffer_valid_bits) {
					merged_datum &=
						ACPI_MASK_BITS_ABOVE (obj_desc->common_field.end_buffer_valid_bits);
				}
			}
		}

		/*
		 * Store the merged field datum in the caller's buffer, according to
		 * the granularity of the field (size of each datum).
		 */
		acpi_ex_set_buffer_datum (merged_datum, buffer,
				obj_desc->common_field.access_byte_width, datum_offset);

		/*
		 * Save the raw datum that was just acquired since it may contain bits
		 * of the *next* field datum.  Update offsets
		 */
		previous_raw_datum = this_raw_datum;
		datum_offset++;
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_insert_into_field
 *
 * PARAMETERS:  *Obj_desc           - Field to be set
 *              Buffer              - Value to store
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Store the value into the given field
 *
 ******************************************************************************/

acpi_status
acpi_ex_insert_into_field (
	acpi_operand_object     *obj_desc,
	void                    *buffer,
	u32                     buffer_length)
{
	acpi_status             status;
	u32                     field_datum_byte_offset;
	u32                     datum_offset;
	acpi_integer            mask;
	acpi_integer            merged_datum;
	acpi_integer            previous_raw_datum;
	acpi_integer            this_raw_datum;
	u32                     byte_field_length;
	u32                     datum_count;


	ACPI_FUNCTION_TRACE ("Ex_insert_into_field");


	/*
	 * Incoming buffer must be at least as long as the field, we do not
	 * allow "partial" field writes.  We do not care if the buffer is
	 * larger than the field, this typically happens when an integer is
	 * written to a field that is actually smaller than an integer.
	 */
	byte_field_length = ACPI_ROUND_BITS_UP_TO_BYTES (obj_desc->common_field.bit_length);
	if (buffer_length < byte_field_length) {
		ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD, "Buffer length %X too small for field %X\n",
			buffer_length, byte_field_length));

		return_ACPI_STATUS (AE_BUFFER_OVERFLOW);
	}

	/* Convert byte count to datum count, round up if necessary */

	datum_count = ACPI_ROUND_UP_TO (byte_field_length, obj_desc->common_field.access_byte_width);

	ACPI_DEBUG_PRINT ((ACPI_DB_BFIELD,
		"Byte_len=%X, Datum_len=%X, Byte_gran=%X\n",
		byte_field_length, datum_count, obj_desc->common_field.access_byte_width));

	/*
	 * Break the request into up to three parts (similar to an I/O request):
	 * 1) non-aligned part at start
	 * 2) aligned part in middle
	 * 3) non-aligned part at the end
	 */
	field_datum_byte_offset = 0;
	datum_offset= 0;

	/* Get a single datum from the caller's buffer */

	acpi_ex_get_buffer_datum (&previous_raw_datum, buffer,
			obj_desc->common_field.access_byte_width, datum_offset);

	/*
	 * Part1:
	 * Write a partial field datum if field does not begin on a datum boundary
	 * Note: The code in this section also handles the aligned case
	 *
	 * Construct Mask with 1 bits where the field is, 0 bits elsewhere
	 * (Only the bottom 5 bits of Bit_length are valid for a shift operation)
	 *
	 * Mask off bits that are "below" the field (if any)
	 */
	mask = ACPI_MASK_BITS_BELOW (obj_desc->common_field.start_field_bit_offset);

	/* If the field fits in one datum, may need to mask upper bits */

	if ((obj_desc->common_field.flags & AOPOBJ_SINGLE_DATUM) &&
		 obj_desc->common_field.end_field_valid_bits) {
		/* There are bits above the field, mask them off also */

		mask &= ACPI_MASK_BITS_ABOVE (obj_desc->common_field.end_field_valid_bits);
	}

	/* Shift and mask the value into the field position */

	merged_datum = (previous_raw_datum << obj_desc->common_field.start_field_bit_offset);
	merged_datum &= mask;

	/* Apply the update rule (if necessary) and write the datum to the field */

	status = acpi_ex_write_with_update_rule (obj_desc, mask, merged_datum,
			   field_datum_byte_offset);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* If the entire field fits within one datum, we are done. */

	if ((datum_count == 1) &&
	   (obj_desc->common_field.flags & AOPOBJ_SINGLE_DATUM)) {
		return_ACPI_STATUS (AE_OK);
	}

	/*
	 * Part2:
	 * Write the aligned data.
	 *
	 * We don't need to worry about the update rule for these data, because
	 * all of the bits in each datum are part of the field.
	 *
	 * The last datum must be special cased because it might contain bits
	 * that are not part of the field -- therefore the "update rule" must be
	 * applied in Part3 below.
	 */
	while (datum_offset < datum_count) {
		datum_offset++;
		field_datum_byte_offset += obj_desc->common_field.access_byte_width;

		/*
		 * Get the next raw buffer datum.  It may contain bits of the previous
		 * field datum
		 */
		acpi_ex_get_buffer_datum (&this_raw_datum, buffer,
				obj_desc->common_field.access_byte_width, datum_offset);

		/* Create the field datum based on the field alignment */

		if (obj_desc->common_field.start_field_bit_offset != 0) {
			/*
			 * Put together appropriate bits of the two raw buffer data to make
			 * a single complete field datum
			 */
			merged_datum =
				(previous_raw_datum >> obj_desc->common_field.datum_valid_bits) |
				(this_raw_datum << obj_desc->common_field.start_field_bit_offset);
		}
		else {
			/* Field began aligned on datum boundary */

			merged_datum = this_raw_datum;
		}

		/*
		 * Special handling for the last datum if the field does NOT end on
		 * a datum boundary.  Update Rule must be applied to the bits outside
		 * the field.
		 */
		if (datum_offset == datum_count) {
			/*
			 * If there are dangling non-aligned bits, perform one more merged write
			 * Else - field is aligned at the end, no need for any more writes
			 */
			if (obj_desc->common_field.end_field_valid_bits) {
				/*
				 * Part3:
				 * This is the last datum and the field does not end on a datum boundary.
				 * Build the partial datum and write with the update rule.
				 *
				 * Mask off the unused bits above (after) the end-of-field
				 */
				mask = ACPI_MASK_BITS_ABOVE (obj_desc->common_field.end_field_valid_bits);
				merged_datum &= mask;

				/* Write the last datum with the update rule */

				status = acpi_ex_write_with_update_rule (obj_desc, mask, merged_datum,
						   field_datum_byte_offset);
				if (ACPI_FAILURE (status)) {
					return_ACPI_STATUS (status);
				}
			}
		}
		else {
			/* Normal case -- write the completed datum */

			status = acpi_ex_field_datum_io (obj_desc, field_datum_byte_offset,
					  &merged_datum, ACPI_WRITE);
			if (ACPI_FAILURE (status)) {
				return_ACPI_STATUS (status);
			}
		}

		/*
		 * Save the most recent datum since it may contain bits of the *next*
		 * field datum.  Update current byte offset.
		 */
		previous_raw_datum = this_raw_datum;
	}

	return_ACPI_STATUS (status);
}


