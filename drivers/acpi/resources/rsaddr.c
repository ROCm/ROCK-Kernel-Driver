/*******************************************************************************
 *
 * Module Name: rsaddr - Address resource descriptors (16/32/64)
 *              $Revision: 19 $
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
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
	 MODULE_NAME         ("rsaddr")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_address16_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes consumed from
 *                                        the Byte_stream_buffer
 *              Output_buffer           - Pointer to the user's return buffer
 *              Structure_size          - u32 pointer that is filled with
 *                                        the number of bytes in the filled
 *                                        in structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_address16_resource (
	u8                      *byte_stream_buffer,
	u32                     *bytes_consumed,
	u8                      **output_buffer,
	u32                     *structure_size)
{
	u8                      *buffer = byte_stream_buffer;
	acpi_resource           *output_struct = (acpi_resource *) *output_buffer;
	NATIVE_CHAR             *temp_ptr;
	u32                     struct_size = SIZEOF_RESOURCE (acpi_resource_address16);
	u32                     index;
	u16                     temp16;
	u8                      temp8;


	FUNCTION_TRACE ("Rs_address16_resource");

	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;
	MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

	*bytes_consumed = temp16 + 3;
	output_struct->id = ACPI_RSTYPE_ADDRESS16;

	/*
	 * Get the Resource Type (Byte3)
	 */
	buffer += 2;
	temp8 = *buffer;

	/* Values 0-2 are valid */
	if (temp8 > 2) {
		return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
	}

	output_struct->data.address16.resource_type = temp8 & 0x03;

	/*
	 * Get the General Flags (Byte4)
	 */
	buffer += 1;
	temp8 = *buffer;

	/*
	 * Producer / Consumer
	 */
	output_struct->data.address16.producer_consumer = temp8 & 0x01;

	/*
	 * Decode
	 */
	output_struct->data.address16.decode = (temp8 >> 1) & 0x01;

	/*
	 * Min Address Fixed
	 */
	output_struct->data.address16.min_address_fixed = (temp8 >> 2) & 0x01;

	/*
	 * Max Address Fixed
	 */
	output_struct->data.address16.max_address_fixed = (temp8 >> 3) & 0x01;

	/*
	 * Get the Type Specific Flags (Byte5)
	 */
	buffer += 1;
	temp8 = *buffer;

	if (MEMORY_RANGE == output_struct->data.address16.resource_type) {
		output_struct->data.address16.attribute.memory.read_write_attribute =
				(u16) (temp8 & 0x01);
		output_struct->data.address16.attribute.memory.cache_attribute =
				(u16) ((temp8 >> 1) & 0x0F);
	}

	else {
		if (IO_RANGE == output_struct->data.address16.resource_type) {
			output_struct->data.address16.attribute.io.range_attribute =
				(u16) (temp8 & 0x03);
		}

		else {
			/* BUS_NUMBER_RANGE == Address16.Data->Resource_type */
			/* Nothing needs to be filled in */
		}
	}

	/*
	 * Get Granularity (Bytes 6-7)
	 */
	buffer += 1;
	MOVE_UNALIGNED16_TO_16 (&output_struct->data.address16.granularity,
			 buffer);

	/*
	 * Get Min_address_range (Bytes 8-9)
	 */
	buffer += 2;
	MOVE_UNALIGNED16_TO_16 (&output_struct->data.address16.min_address_range,
			 buffer);

	/*
	 * Get Max_address_range (Bytes 10-11)
	 */
	buffer += 2;
	MOVE_UNALIGNED16_TO_16 (&output_struct->data.address16.max_address_range,
			 buffer);

	/*
	 * Get Address_translation_offset (Bytes 12-13)
	 */
	buffer += 2;
	MOVE_UNALIGNED16_TO_16 (&output_struct->data.address16.address_translation_offset,
			 buffer);

	/*
	 * Get Address_length (Bytes 14-15)
	 */
	buffer += 2;
	MOVE_UNALIGNED16_TO_16 (&output_struct->data.address16.address_length,
			 buffer);

	/*
	 * Resource Source Index (if present)
	 */
	buffer += 2;

	/*
	 * This will leave us pointing to the Resource Source Index
	 * If it is present, then save it off and calculate the
	 * pointer to where the null terminated string goes:
	 * Each Interrupt takes 32-bits + the 5 bytes of the
	 * stream that are default.
	 */
	if (*bytes_consumed > 16) {
		/* Dereference the Index */

		temp8 = *buffer;
		output_struct->data.address16.resource_source.index = (u32) temp8;

		/* Point to the String */

		buffer += 1;

		/* Point the String pointer to the end of this structure */

		output_struct->data.address16.resource_source.string_ptr =
				(NATIVE_CHAR *)((u8 * )output_struct + struct_size);

		temp_ptr = output_struct->data.address16.resource_source.string_ptr;

		/* Copy the string into the buffer */

		index = 0;

		while (0x00 != *buffer) {
			*temp_ptr = *buffer;

			temp_ptr += 1;
			buffer += 1;
			index += 1;
		}

		/*
		 * Add the terminating null
		 */
		*temp_ptr = 0x00;

		output_struct->data.address16.resource_source.string_length = index + 1;

		/*
		 * In order for the Struct_size to fall on a 32-bit boundary,
		 * calculate the length of the string and expand the
		 * Struct_size to the next 32-bit boundary.
		 */
		temp8 = (u8) (index + 1);
		struct_size += ROUND_UP_TO_32_bITS (temp8);
	}

	else {
		output_struct->data.address16.resource_source.index = 0x00;
		output_struct->data.address16.resource_source.string_length = 0;
		output_struct->data.address16.resource_source.string_ptr = NULL;
	}

	/*
	 * Set the Length parameter
	 */
	output_struct->length = struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_address16_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes of the
 *                                        Output_buffer used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_address16_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	u32                     *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u8                      *length_field;
	u8                      temp8;
	NATIVE_CHAR             *temp_pointer = NULL;
	u32                     actual_bytes;


	FUNCTION_TRACE ("Rs_address16_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x88;
	buffer += 1;

	/*
	 * Save a pointer to the Length field - to be filled in later
	 */
	length_field = buffer;
	buffer += 2;

	/*
	 * Set the Resource Type (Memory, Io, Bus_number)
	 */
	temp8 = (u8) (linked_list->data.address16.resource_type & 0x03);
	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the general flags
	 */
	temp8 = (u8) (linked_list->data.address16.producer_consumer & 0x01);

	temp8 |= (linked_list->data.address16.decode & 0x01) << 1;
	temp8 |= (linked_list->data.address16.min_address_fixed & 0x01) << 2;
	temp8 |= (linked_list->data.address16.max_address_fixed & 0x01) << 3;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the type specific flags
	 */
	temp8 = 0;

	if (MEMORY_RANGE == linked_list->data.address16.resource_type) {
		temp8 = (u8)
			(linked_list->data.address16.attribute.memory.read_write_attribute &
			 0x01);

		temp8 |=
			(linked_list->data.address16.attribute.memory.cache_attribute &
			 0x0F) << 1;
	}

	else if (IO_RANGE == linked_list->data.address16.resource_type) {
		temp8 = (u8)
			(linked_list->data.address16.attribute.io.range_attribute &
			 0x03);
	}

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the address space granularity
	 */
	MOVE_UNALIGNED16_TO_16 (buffer,
			   &linked_list->data.address16.granularity);
	buffer += 2;

	/*
	 * Set the address range minimum
	 */
	MOVE_UNALIGNED16_TO_16 (buffer,
			   &linked_list->data.address16.min_address_range);
	buffer += 2;

	/*
	 * Set the address range maximum
	 */
	MOVE_UNALIGNED16_TO_16 (buffer,
			   &linked_list->data.address16.max_address_range);
	buffer += 2;

	/*
	 * Set the address translation offset
	 */
	MOVE_UNALIGNED16_TO_16 (buffer,
			   &linked_list->data.address16.address_translation_offset);
	buffer += 2;

	/*
	 * Set the address length
	 */
	MOVE_UNALIGNED16_TO_16 (buffer,
			   &linked_list->data.address16.address_length);
	buffer += 2;

	/*
	 * Resource Source Index and Resource Source are optional
	 */
	if (0 != linked_list->data.address16.resource_source.string_length) {
		temp8 = (u8) linked_list->data.address16.resource_source.index;

		*buffer = temp8;
		buffer += 1;

		temp_pointer = (NATIVE_CHAR *) buffer;

		/*
		 * Copy the string
		 */
		STRCPY (temp_pointer,
				linked_list->data.address16.resource_source.string_ptr);

		/*
		 * Buffer needs to be set to the length of the sting + one for the
		 *  terminating null
		 */
		buffer += (STRLEN (linked_list->data.address16.resource_source.string_ptr)
				 + 1);
	}

	/*
	 * Return the number of bytes consumed in this operation
	 */
	actual_bytes = POINTER_DIFF (buffer, *output_buffer);
	*bytes_consumed = actual_bytes;

	/*
	 * Set the length field to the number of bytes consumed
	 * minus the header size (3 bytes)
	 */
	actual_bytes -= 3;
	MOVE_UNALIGNED16_TO_16 (length_field, &actual_bytes);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_address32_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes consumed from
 *                                        the Byte_stream_buffer
 *              Output_buffer           - Pointer to the user's return buffer
 *              Structure_size          - u32 pointer that is filled with
 *                                        the number of bytes in the filled
 *                                        in structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_address32_resource (
	u8                      *byte_stream_buffer,
	u32                     *bytes_consumed,
	u8                      **output_buffer,
	u32                     *structure_size)
{
	u8                      *buffer;
	acpi_resource           *output_struct;
	u16                     temp16;
	u8                      temp8;
	NATIVE_CHAR             *temp_ptr;
	u32                     struct_size;
	u32                     index;


	FUNCTION_TRACE ("Rs_address32_resource");


	buffer = byte_stream_buffer;
	output_struct = (acpi_resource *) *output_buffer;

	struct_size = SIZEOF_RESOURCE (acpi_resource_address32);

	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;
	MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

	*bytes_consumed = temp16 + 3;

	output_struct->id = ACPI_RSTYPE_ADDRESS32;

	/*
	 * Get the Resource Type (Byte3)
	 */
	buffer += 2;
	temp8 = *buffer;

	/* Values 0-2 are valid */
	if(temp8 > 2) {
		return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
	}

	output_struct->data.address32.resource_type = temp8 & 0x03;

	/*
	 * Get the General Flags (Byte4)
	 */
	buffer += 1;
	temp8 = *buffer;

	/*
	 * Producer / Consumer
	 */
	output_struct->data.address32.producer_consumer = temp8 & 0x01;

	/*
	 * Decode
	 */
	output_struct->data.address32.decode = (temp8 >> 1) & 0x01;

	/*
	 * Min Address Fixed
	 */
	output_struct->data.address32.min_address_fixed = (temp8 >> 2) & 0x01;

	/*
	 * Max Address Fixed
	 */
	output_struct->data.address32.max_address_fixed = (temp8 >> 3) & 0x01;

	/*
	 * Get the Type Specific Flags (Byte5)
	 */
	buffer += 1;
	temp8 = *buffer;

	if (MEMORY_RANGE == output_struct->data.address32.resource_type) {
		output_struct->data.address32.attribute.memory.read_write_attribute =
				(u16) (temp8 & 0x01);

		output_struct->data.address32.attribute.memory.cache_attribute =
				(u16) ((temp8 >> 1) & 0x0F);
	}

	else {
		if (IO_RANGE == output_struct->data.address32.resource_type) {
			output_struct->data.address32.attribute.io.range_attribute =
				(u16) (temp8 & 0x03);
		}

		else {
			/* BUS_NUMBER_RANGE == Output_struct->Data.Address32.Resource_type */
			/* Nothing needs to be filled in */
		}
	}

	/*
	 * Get Granularity (Bytes 6-9)
	 */
	buffer += 1;
	MOVE_UNALIGNED32_TO_32 (&output_struct->data.address32.granularity,
			 buffer);

	/*
	 * Get Min_address_range (Bytes 10-13)
	 */
	buffer += 4;
	MOVE_UNALIGNED32_TO_32 (&output_struct->data.address32.min_address_range,
			 buffer);

	/*
	 * Get Max_address_range (Bytes 14-17)
	 */
	buffer += 4;
	MOVE_UNALIGNED32_TO_32 (&output_struct->data.address32.max_address_range,
			 buffer);

	/*
	 * Get Address_translation_offset (Bytes 18-21)
	 */
	buffer += 4;
	MOVE_UNALIGNED32_TO_32 (&output_struct->data.address32.address_translation_offset,
			 buffer);

	/*
	 * Get Address_length (Bytes 22-25)
	 */
	buffer += 4;
	MOVE_UNALIGNED32_TO_32 (&output_struct->data.address32.address_length,
			 buffer);

	/*
	 * Resource Source Index (if present)
	 */
	buffer += 4;

	/*
	 * This will leave us pointing to the Resource Source Index
	 * If it is present, then save it off and calculate the
	 * pointer to where the null terminated string goes:
	 */
	if (*bytes_consumed > 26) {
		/* Dereference the Index */

		temp8 = *buffer;
		output_struct->data.address32.resource_source.index =
				(u32) temp8;

		/* Point to the String */

		buffer += 1;

		/* Point the String pointer to the end of this structure */

		output_struct->data.address32.resource_source.string_ptr =
				(NATIVE_CHAR *)((u8 *)output_struct + struct_size);

		temp_ptr = output_struct->data.address32.resource_source.string_ptr;

		/* Copy the string into the buffer */

		index = 0;

		while (0x00 != *buffer) {
			*temp_ptr = *buffer;

			temp_ptr += 1;
			buffer += 1;
			index += 1;
		}

		/*
		 * Add the terminating null
		 */
		*temp_ptr = 0x00;
		output_struct->data.address32.resource_source.string_length = index + 1;

		/*
		 * In order for the Struct_size to fall on a 32-bit boundary,
		 *  calculate the length of the string and expand the
		 *  Struct_size to the next 32-bit boundary.
		 */
		temp8 = (u8) (index + 1);
		struct_size += ROUND_UP_TO_32_bITS (temp8);
	}

	else {
		output_struct->data.address32.resource_source.index = 0x00;
		output_struct->data.address32.resource_source.string_length = 0;
		output_struct->data.address32.resource_source.string_ptr = NULL;
	}

	/*
	 * Set the Length parameter
	 */
	output_struct->length = struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_address32_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes of the
 *                                        Output_buffer used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_address32_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	u32                     *bytes_consumed)
{
	u8                      *buffer;
	u16                     *length_field;
	u8                      temp8;
	NATIVE_CHAR             *temp_pointer;


	FUNCTION_TRACE ("Rs_address32_stream");


	buffer = *output_buffer;

	/*
	 * The descriptor field is static
	 */
	*buffer = 0x87;
	buffer += 1;

	/*
	 * Set a pointer to the Length field - to be filled in later
	 */

	length_field = (u16 *) buffer;
	buffer += 2;

	/*
	 * Set the Resource Type (Memory, Io, Bus_number)
	 */
	temp8 = (u8) (linked_list->data.address32.resource_type & 0x03);

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the general flags
	 */
	temp8 = (u8) (linked_list->data.address32.producer_consumer & 0x01);
	temp8 |= (linked_list->data.address32.decode & 0x01) << 1;
	temp8 |= (linked_list->data.address32.min_address_fixed & 0x01) << 2;
	temp8 |= (linked_list->data.address32.max_address_fixed & 0x01) << 3;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the type specific flags
	 */
	temp8 = 0;

	if(MEMORY_RANGE == linked_list->data.address32.resource_type) {
		temp8 = (u8)
			(linked_list->data.address32.attribute.memory.read_write_attribute &
			0x01);

		temp8 |=
			(linked_list->data.address32.attribute.memory.cache_attribute &
			 0x0F) << 1;
	}

	else if (IO_RANGE == linked_list->data.address32.resource_type) {
		temp8 = (u8)
			(linked_list->data.address32.attribute.io.range_attribute &
			 0x03);
	}

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the address space granularity
	 */
	MOVE_UNALIGNED32_TO_32 (buffer,
			  &linked_list->data.address32.granularity);
	buffer += 4;

	/*
	 * Set the address range minimum
	 */
	MOVE_UNALIGNED32_TO_32 (buffer,
			  &linked_list->data.address32.min_address_range);
	buffer += 4;

	/*
	 * Set the address range maximum
	 */
	MOVE_UNALIGNED32_TO_32 (buffer,
			  &linked_list->data.address32.max_address_range);
	buffer += 4;

	/*
	 * Set the address translation offset
	 */
	MOVE_UNALIGNED32_TO_32 (buffer,
			  &linked_list->data.address32.address_translation_offset);
	buffer += 4;

	/*
	 * Set the address length
	 */
	MOVE_UNALIGNED32_TO_32 (buffer,
			  &linked_list->data.address32.address_length);
	buffer += 4;

	/*
	 * Resource Source Index and Resource Source are optional
	 */
	if (0 != linked_list->data.address32.resource_source.string_length) {
		temp8 = (u8) linked_list->data.address32.resource_source.index;

		*buffer = temp8;
		buffer += 1;

		temp_pointer = (NATIVE_CHAR *) buffer;

		/*
		 * Copy the string
		 */
		STRCPY (temp_pointer,
			linked_list->data.address32.resource_source.string_ptr);

		/*
		 * Buffer needs to be set to the length of the sting + one for the
		 *  terminating null
		 */
		buffer += (STRLEN (linked_list->data.address32.resource_source.string_ptr) + 1);
	}

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = POINTER_DIFF (buffer, *output_buffer);

	/*
	 * Set the length field to the number of bytes consumed
	 *  minus the header size (3 bytes)
	 */
	*length_field = (u16) (*bytes_consumed - 3);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_address64_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes consumed from
 *                                        the Byte_stream_buffer
 *              Output_buffer           - Pointer to the user's return buffer
 *              Structure_size          - u32 pointer that is filled with
 *                                        the number of bytes in the filled
 *                                        in structure
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_address64_resource (
	u8                      *byte_stream_buffer,
	u32                     *bytes_consumed,
	u8                      **output_buffer,
	u32                     *structure_size)
{
	u8                      *buffer;
	acpi_resource           *output_struct;
	u16                     temp16;
	u8                      temp8;
	NATIVE_CHAR             *temp_ptr;
	u32                     struct_size;
	u32                     index;


	FUNCTION_TRACE ("Rs_address64_resource");


	buffer = byte_stream_buffer;
	output_struct = (acpi_resource *) *output_buffer;

	struct_size = SIZEOF_RESOURCE (acpi_resource_address64);

	/*
	 * Point past the Descriptor to get the number of bytes consumed
	 */
	buffer += 1;
	MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

	*bytes_consumed = temp16 + 3;
	output_struct->id = ACPI_RSTYPE_ADDRESS64;

	/*
	 * Get the Resource Type (Byte3)
	 */
	buffer += 2;
	temp8 = *buffer;

	/* Values 0-2 are valid */
	if(temp8 > 2) {
		return_ACPI_STATUS (AE_AML_INVALID_RESOURCE_TYPE);
	}

	output_struct->data.address64.resource_type = temp8 & 0x03;

	/*
	 * Get the General Flags (Byte4)
	 */
	buffer += 1;
	temp8 = *buffer;

	/*
	 * Producer / Consumer
	 */
	output_struct->data.address64.producer_consumer = temp8 & 0x01;

	/*
	 * Decode
	 */
	output_struct->data.address64.decode = (temp8 >> 1) & 0x01;

	/*
	 * Min Address Fixed
	 */
	output_struct->data.address64.min_address_fixed = (temp8 >> 2) & 0x01;

	/*
	 * Max Address Fixed
	 */
	output_struct->data.address64.max_address_fixed = (temp8 >> 3) & 0x01;

	/*
	 * Get the Type Specific Flags (Byte5)
	 */
	buffer += 1;
	temp8 = *buffer;

	if (MEMORY_RANGE == output_struct->data.address64.resource_type) {
		output_struct->data.address64.attribute.memory.read_write_attribute =
				(u16) (temp8 & 0x01);

		output_struct->data.address64.attribute.memory.cache_attribute =
				(u16) ((temp8 >> 1) & 0x0F);
	}

	else {
		if (IO_RANGE == output_struct->data.address64.resource_type) {
			output_struct->data.address64.attribute.io.range_attribute =
				(u16) (temp8 & 0x03);
		}

		else {
			/* BUS_NUMBER_RANGE == Output_struct->Data.Address64.Resource_type */
			/* Nothing needs to be filled in */
		}
	}

	/*
	 * Get Granularity (Bytes 6-13)
	 */
	buffer += 1;
	MOVE_UNALIGNED64_TO_64 (&output_struct->data.address64.granularity,
			 buffer);

	/*
	 * Get Min_address_range (Bytes 14-21)
	 */
	buffer += 8;
	MOVE_UNALIGNED64_TO_64 (&output_struct->data.address64.min_address_range,
			 buffer);

	/*
	 * Get Max_address_range (Bytes 22-29)
	 */
	buffer += 8;
	MOVE_UNALIGNED64_TO_64 (&output_struct->data.address64.max_address_range,
			 buffer);

	/*
	 * Get Address_translation_offset (Bytes 30-37)
	 */
	buffer += 8;
	MOVE_UNALIGNED64_TO_64 (&output_struct->data.address64.address_translation_offset,
			 buffer);

	/*
	 * Get Address_length (Bytes 38-45)
	 */
	buffer += 8;
	MOVE_UNALIGNED64_TO_64 (&output_struct->data.address64.address_length,
			 buffer);

	/*
	 * Resource Source Index (if present)
	 */
	buffer += 8;

	/*
	 * This will leave us pointing to the Resource Source Index
	 * If it is present, then save it off and calculate the
	 * pointer to where the null terminated string goes:
	 * Each Interrupt takes 32-bits + the 5 bytes of the
	 * stream that are default.
	 */
	if (*bytes_consumed > 46) {
		/* Dereference the Index */

		temp8 = *buffer;
		output_struct->data.address64.resource_source.index =
				(u32) temp8;

		/* Point to the String */

		buffer += 1;

		/* Point the String pointer to the end of this structure */

		output_struct->data.address64.resource_source.string_ptr =
				(NATIVE_CHAR *)((u8 *)output_struct + struct_size);

		temp_ptr = output_struct->data.address64.resource_source.string_ptr;

		/* Copy the string into the buffer */

		index = 0;

		while (0x00 != *buffer) {
			*temp_ptr = *buffer;

			temp_ptr += 1;
			buffer += 1;
			index += 1;
		}

		/*
		 * Add the terminating null
		 */
		*temp_ptr = 0x00;

		output_struct->data.address64.resource_source.string_length = index + 1;

		/*
		 * In order for the Struct_size to fall on a 32-bit boundary,
		 * calculate the length of the string and expand the
		 * Struct_size to the next 32-bit boundary.
		 */
		temp8 = (u8) (index + 1);
		struct_size += ROUND_UP_TO_32_bITS (temp8);
	}

	else {
		output_struct->data.address64.resource_source.index = 0x00;
		output_struct->data.address64.resource_source.string_length = 0;
		output_struct->data.address64.resource_source.string_ptr = NULL;
	}

	/*
	 * Set the Length parameter
	 */
	output_struct->length = struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_address64_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - u32 pointer that is filled with
 *                                        the number of bytes of the
 *                                        Output_buffer used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_address64_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	u32                     *bytes_consumed)
{
	u8                      *buffer;
	u16                     *length_field;
	u8                      temp8;
	NATIVE_CHAR             *temp_pointer;


	FUNCTION_TRACE ("Rs_address64_stream");


	buffer = *output_buffer;

	/*
	 * The descriptor field is static
	 */
	*buffer = 0x8A;
	buffer += 1;

	/*
	 * Set a pointer to the Length field - to be filled in later
	 */

	length_field = (u16 *)buffer;
	buffer += 2;

	/*
	 * Set the Resource Type (Memory, Io, Bus_number)
	 */
	temp8 = (u8) (linked_list->data.address64.resource_type & 0x03);

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the general flags
	 */
	temp8 = (u8) (linked_list->data.address64.producer_consumer & 0x01);
	temp8 |= (linked_list->data.address64.decode & 0x01) << 1;
	temp8 |= (linked_list->data.address64.min_address_fixed & 0x01) << 2;
	temp8 |= (linked_list->data.address64.max_address_fixed & 0x01) << 3;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the type specific flags
	 */
	temp8 = 0;

	if(MEMORY_RANGE == linked_list->data.address64.resource_type) {
		temp8 = (u8)
			(linked_list->data.address64.attribute.memory.read_write_attribute &
			0x01);

		temp8 |=
			(linked_list->data.address64.attribute.memory.cache_attribute &
			 0x0F) << 1;
	}

	else if (IO_RANGE == linked_list->data.address64.resource_type) {
		temp8 = (u8)
			(linked_list->data.address64.attribute.io.range_attribute &
			 0x03);
	}

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the address space granularity
	 */
	MOVE_UNALIGNED64_TO_64 (buffer,
			   &linked_list->data.address64.granularity);
	buffer += 8;

	/*
	 * Set the address range minimum
	 */
	MOVE_UNALIGNED64_TO_64 (buffer,
			   &linked_list->data.address64.min_address_range);
	buffer += 8;

	/*
	 * Set the address range maximum
	 */
	MOVE_UNALIGNED64_TO_64 (buffer,
			   &linked_list->data.address64.max_address_range);
	buffer += 8;

	/*
	 * Set the address translation offset
	 */
	MOVE_UNALIGNED64_TO_64 (buffer,
			   &linked_list->data.address64.address_translation_offset);
	buffer += 8;

	/*
	 * Set the address length
	 */
	MOVE_UNALIGNED64_TO_64 (buffer,
			   &linked_list->data.address64.address_length);
	buffer += 8;

	/*
	 * Resource Source Index and Resource Source are optional
	 */
	if (0 != linked_list->data.address64.resource_source.string_length) {
		temp8 = (u8) linked_list->data.address64.resource_source.index;

		*buffer = temp8;
		buffer += 1;

		temp_pointer = (NATIVE_CHAR *) buffer;

		/*
		 * Copy the string
		 */
		STRCPY (temp_pointer, linked_list->data.address64.resource_source.string_ptr);

		/*
		 * Buffer needs to be set to the length of the sting + one for the
		 *  terminating null
		 */
		buffer += (STRLEN (linked_list->data.address64.resource_source.string_ptr) + 1);
	}

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = POINTER_DIFF (buffer, *output_buffer);

	/*
	 * Set the length field to the number of bytes consumed
	 * minus the header size (3 bytes)
	 */
	*length_field = (u16) (*bytes_consumed - 3);
	return_ACPI_STATUS (AE_OK);
}

