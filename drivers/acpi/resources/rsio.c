/*******************************************************************************
 *
 * Module Name: rsio - IO and DMA resource descriptors
 *              $Revision: 21 $
 *
 ******************************************************************************/

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
#include "acresrc.h"

#define _COMPONENT          ACPI_RESOURCES
	 ACPI_MODULE_NAME    ("rsio")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_io_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_io_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	u8                      *buffer = byte_stream_buffer;
	acpi_resource           *output_struct = (void *) *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;
	ACPI_SIZE               struct_size = ACPI_SIZEOF_RESOURCE (acpi_resource_io);


	ACPI_FUNCTION_TRACE ("Rs_io_resource");


	/*
	 * The number of bytes consumed are Constant
	 */
	*bytes_consumed = 8;

	output_struct->id = ACPI_RSTYPE_IO;

	/*
	 * Check Decode
	 */
	buffer += 1;
	temp8 = *buffer;

	output_struct->data.io.io_decode = temp8 & 0x01;

	/*
	 * Check Min_base Address
	 */
	buffer += 1;
	ACPI_MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

	output_struct->data.io.min_base_address = temp16;

	/*
	 * Check Max_base Address
	 */
	buffer += 2;
	ACPI_MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

	output_struct->data.io.max_base_address = temp16;

	/*
	 * Check Base alignment
	 */
	buffer += 2;
	temp8 = *buffer;

	output_struct->data.io.alignment = temp8;

	/*
	 * Check Range_length
	 */
	buffer += 1;
	temp8 = *buffer;

	output_struct->data.io.range_length = temp8;

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_fixed_io_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_fixed_io_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	u8                      *buffer = byte_stream_buffer;
	acpi_resource           *output_struct = (void *) *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;
	ACPI_SIZE               struct_size = ACPI_SIZEOF_RESOURCE (acpi_resource_fixed_io);


	ACPI_FUNCTION_TRACE ("Rs_fixed_io_resource");


	/*
	 * The number of bytes consumed are Constant
	 */
	*bytes_consumed = 4;

	output_struct->id = ACPI_RSTYPE_FIXED_IO;

	/*
	 * Check Range Base Address
	 */
	buffer += 1;
	ACPI_MOVE_UNALIGNED16_TO_16 (&temp16, buffer);

	output_struct->data.fixed_io.base_address = temp16;

	/*
	 * Check Range_length
	 */
	buffer += 2;
	temp8 = *buffer;

	output_struct->data.fixed_io.range_length = temp8;

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_io_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the Output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_io_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;


	ACPI_FUNCTION_TRACE ("Rs_io_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x47;
	buffer += 1;

	/*
	 * Io Information Byte
	 */
	temp8 = (u8) (linked_list->data.io.io_decode & 0x01);

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the Range minimum base address
	 */
	temp16 = (u16) linked_list->data.io.min_base_address;

	ACPI_MOVE_UNALIGNED16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the Range maximum base address
	 */
	temp16 = (u16) linked_list->data.io.max_base_address;

	ACPI_MOVE_UNALIGNED16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the base alignment
	 */
	temp8 = (u8) linked_list->data.io.alignment;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the range length
	 */
	temp8 = (u8) linked_list->data.io.range_length;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_fixed_io_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the Output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_fixed_io_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;


	ACPI_FUNCTION_TRACE ("Rs_fixed_io_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x4B;

	buffer += 1;

	/*
	 * Set the Range base address
	 */
	temp16 = (u16) linked_list->data.fixed_io.base_address;

	ACPI_MOVE_UNALIGNED16_TO_16 (buffer, &temp16);
	buffer += 2;

	/*
	 * Set the range length
	 */
	temp8 = (u8) linked_list->data.fixed_io.range_length;

	*buffer = temp8;
	buffer += 1;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dma_resource
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource input byte
 *                                        stream
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        consumed the Byte_stream_buffer is
 *                                        returned
 *              Output_buffer           - Pointer to the return data buffer
 *              Structure_size          - Pointer to where the number of bytes
 *                                        in the return data struct is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the resource byte stream and fill out the appropriate
 *              structure pointed to by the Output_buffer. Return the
 *              number of bytes consumed from the byte stream.
 *
 ******************************************************************************/

acpi_status
acpi_rs_dma_resource (
	u8                      *byte_stream_buffer,
	ACPI_SIZE               *bytes_consumed,
	u8                      **output_buffer,
	ACPI_SIZE               *structure_size)
{
	u8                      *buffer = byte_stream_buffer;
	acpi_resource           *output_struct = (void *) *output_buffer;
	u8                      temp8 = 0;
	u8                      index;
	u8                      i;
	ACPI_SIZE               struct_size = ACPI_SIZEOF_RESOURCE (acpi_resource_dma);


	ACPI_FUNCTION_TRACE ("Rs_dma_resource");


	/*
	 * The number of bytes consumed are Constant
	 */
	*bytes_consumed = 3;
	output_struct->id = ACPI_RSTYPE_DMA;

	/*
	 * Point to the 8-bits of Byte 1
	 */
	buffer += 1;
	temp8 = *buffer;

	/* Decode the IRQ bits */

	for (i = 0, index = 0; index < 8; index++) {
		if ((temp8 >> index) & 0x01) {
			output_struct->data.dma.channels[i] = index;
			i++;
		}
	}
	if (i == 0) {
		/* Zero channels is invalid! */

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Found Zero DMA channels in resource list\n"));
		return_ACPI_STATUS (AE_BAD_DATA);
	}
	output_struct->data.dma.number_of_channels = i;


	/*
	 * Calculate the structure size based upon the number of interrupts
	 */
	struct_size += ((ACPI_SIZE) output_struct->data.dma.number_of_channels - 1) * 4;

	/*
	 * Point to Byte 2
	 */
	buffer += 1;
	temp8 = *buffer;

	/*
	 * Check for transfer preference (Bits[1:0])
	 */
	output_struct->data.dma.transfer = temp8 & 0x03;

	if (0x03 == output_struct->data.dma.transfer) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid DMA.Transfer preference (3)\n"));
		return_ACPI_STATUS (AE_BAD_DATA);
	}

	/*
	 * Get bus master preference (Bit[2])
	 */
	output_struct->data.dma.bus_master = (temp8 >> 2) & 0x01;

	/*
	 * Get channel speed support (Bits[6:5])
	 */
	output_struct->data.dma.type = (temp8 >> 5) & 0x03;

	/*
	 * Set the Length parameter
	 */
	output_struct->length = (u32) struct_size;

	/*
	 * Return the final size of the structure
	 */
	*structure_size = struct_size;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_dma_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's return buffer
 *              Bytes_consumed          - Pointer to where the number of bytes
 *                                        used in the Output_buffer is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Take the linked list resource structure and fills in the
 *              the appropriate bytes in a byte stream
 *
 ******************************************************************************/

acpi_status
acpi_rs_dma_stream (
	acpi_resource           *linked_list,
	u8                      **output_buffer,
	ACPI_SIZE               *bytes_consumed)
{
	u8                      *buffer = *output_buffer;
	u16                     temp16 = 0;
	u8                      temp8 = 0;
	u8                      index;


	ACPI_FUNCTION_TRACE ("Rs_dma_stream");


	/*
	 * The descriptor field is static
	 */
	*buffer = 0x2A;
	buffer += 1;
	temp8 = 0;

	/*
	 * Loop through all of the Channels and set the mask bits
	 */
	for (index = 0;
		 index < linked_list->data.dma.number_of_channels;
		 index++) {
		temp16 = (u16) linked_list->data.dma.channels[index];
		temp8 |= 0x1 << temp16;
	}

	*buffer = temp8;
	buffer += 1;

	/*
	 * Set the DMA Info
	 */
	temp8 = (u8) ((linked_list->data.dma.type & 0x03) << 5);
	temp8 |= ((linked_list->data.dma.bus_master & 0x01) << 2);
	temp8 |= (linked_list->data.dma.transfer & 0x03);

	*buffer = temp8;
	buffer += 1;

	/*
	 * Return the number of bytes consumed in this operation
	 */
	*bytes_consumed = ACPI_PTR_DIFF (buffer, *output_buffer);
	return_ACPI_STATUS (AE_OK);
}

