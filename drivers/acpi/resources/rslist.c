/*******************************************************************************
 *
 * Module Name: rslist - Linked list utilities
 *              $Revision: 31 $
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
	 ACPI_MODULE_NAME    ("rslist")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_get_resource_type
 *
 * PARAMETERS:  Resource_start_byte     - Byte 0 of a resource descriptor
 *
 * RETURN:      The Resource Type (Name) with no extraneous bits
 *
 * DESCRIPTION: Extract the Resource Type/Name from the first byte of
 *              a resource descriptor.
 *
 ******************************************************************************/

u8
acpi_rs_get_resource_type (
	u8                      resource_start_byte)
{

	ACPI_FUNCTION_ENTRY ();


	/*
	 * Determine if this is a small or large resource
	 */
	switch (resource_start_byte & ACPI_RDESC_TYPE_MASK) {
	case ACPI_RDESC_TYPE_SMALL:

		/*
		 * Small Resource Type -- Only bits 6:3 are valid
		 */
		return ((u8) (resource_start_byte & ACPI_RDESC_SMALL_MASK));


	case ACPI_RDESC_TYPE_LARGE:

		/*
		 * Large Resource Type -- All bits are valid
		 */
		return (resource_start_byte);


	default:
		/* No other types of resource descriptor */
		break;
	}

	return (0xFF);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_byte_stream_to_list
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource byte stream
 *              Byte_stream_buffer_length - Length of Byte_stream_buffer
 *              Output_buffer           - Pointer to the buffer that will
 *                                        contain the output structures
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource byte stream and parses it, creating a
 *              linked list of resources in the caller's output buffer
 *
 ******************************************************************************/

acpi_status
acpi_rs_byte_stream_to_list (
	u8                      *byte_stream_buffer,
	u32                     byte_stream_buffer_length,
	u8                      *output_buffer)
{
	acpi_status             status;
	ACPI_SIZE               bytes_parsed = 0;
	u8                      resource_type = 0;
	ACPI_SIZE               bytes_consumed = 0;
	u8                      *buffer = output_buffer;
	ACPI_SIZE               structure_size = 0;
	u8                      end_tag_processed = FALSE;
	acpi_resource           *resource;

	ACPI_FUNCTION_TRACE ("Rs_byte_stream_to_list");


	while (bytes_parsed < byte_stream_buffer_length &&
			!end_tag_processed) {
		/*
		 * The next byte in the stream is the resource type
		 */
		resource_type = acpi_rs_get_resource_type (*byte_stream_buffer);

		switch (resource_type) {
		case ACPI_RDESC_TYPE_MEMORY_24:
			/*
			 * 24-Bit Memory Resource
			 */
			status = acpi_rs_memory24_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_LARGE_VENDOR:
			/*
			 * Vendor Defined Resource
			 */
			status = acpi_rs_vendor_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_MEMORY_32:
			/*
			 * 32-Bit Memory Range Resource
			 */
			status = acpi_rs_memory32_range_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_FIXED_MEMORY_32:
			/*
			 * 32-Bit Fixed Memory Resource
			 */
			status = acpi_rs_fixed_memory32_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE:
			/*
			 * 64-Bit Address Resource
			 */
			status = acpi_rs_address64_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE:
			/*
			 * 32-Bit Address Resource
			 */
			status = acpi_rs_address32_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE:
			/*
			 * 16-Bit Address Resource
			 */
			status = acpi_rs_address16_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_EXTENDED_XRUPT:
			/*
			 * Extended IRQ
			 */
			status = acpi_rs_extended_irq_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_IRQ_FORMAT:
			/*
			 * IRQ Resource
			 */
			status = acpi_rs_irq_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_DMA_FORMAT:
			/*
			 * DMA Resource
			 */
			status = acpi_rs_dma_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_START_DEPENDENT:
			/*
			 * Start Dependent Functions Resource
			 */
			status = acpi_rs_start_depend_fns_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_END_DEPENDENT:
			/*
			 * End Dependent Functions Resource
			 */
			status = acpi_rs_end_depend_fns_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_IO_PORT:
			/*
			 * IO Port Resource
			 */
			status = acpi_rs_io_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_FIXED_IO_PORT:
			/*
			 * Fixed IO Port Resource
			 */
			status = acpi_rs_fixed_io_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_SMALL_VENDOR:
			/*
			 * Vendor Specific Resource
			 */
			status = acpi_rs_vendor_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		case ACPI_RDESC_TYPE_END_TAG:
			/*
			 * End Tag
			 */
			end_tag_processed = TRUE;
			status = acpi_rs_end_tag_resource (byte_stream_buffer,
					 &bytes_consumed, &buffer, &structure_size);
			break;


		default:
			/*
			 * Invalid/Unknown resource type
			 */
			status = AE_AML_INVALID_RESOURCE_TYPE;
			break;
		}

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Update the return value and counter
		 */
		bytes_parsed += bytes_consumed;

		/*
		 * Set the byte stream to point to the next resource
		 */
		byte_stream_buffer += bytes_consumed;

		/*
		 * Set the Buffer to the next structure
		 */
		resource = ACPI_CAST_PTR (acpi_resource, buffer);
		resource->length = ACPI_ALIGN_RESOURCE_SIZE(resource->length);
		buffer += ACPI_ALIGN_RESOURCE_SIZE(structure_size);

	} /*  end while */

	/*
	 * Check the reason for exiting the while loop
	 */
	if (!end_tag_processed) {
		return_ACPI_STATUS (AE_AML_NO_RESOURCE_END_TAG);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_list_to_byte_stream
 *
 * PARAMETERS:  Linked_list             - Pointer to the resource linked list
 *              Byte_steam_size_needed  - Calculated size of the byte stream
 *                                        needed from calling
 *                                        Acpi_rs_get_byte_stream_length()
 *                                        The size of the Output_buffer is
 *                                        guaranteed to be >=
 *                                        Byte_stream_size_needed
 *              Output_buffer           - Pointer to the buffer that will
 *                                        contain the byte stream
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Takes the resource linked list and parses it, creating a
 *              byte stream of resources in the caller's output buffer
 *
 ******************************************************************************/

acpi_status
acpi_rs_list_to_byte_stream (
	acpi_resource           *linked_list,
	ACPI_SIZE               byte_stream_size_needed,
	u8                      *output_buffer)
{
	acpi_status             status;
	u8                      *buffer = output_buffer;
	ACPI_SIZE               bytes_consumed = 0;
	u8                      done = FALSE;


	ACPI_FUNCTION_TRACE ("Rs_list_to_byte_stream");


	while (!done) {
		switch (linked_list->id) {
		case ACPI_RSTYPE_IRQ:
			/*
			 * IRQ Resource
			 */
			status = acpi_rs_irq_stream (linked_list, &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_DMA:
			/*
			 * DMA Resource
			 */
			status = acpi_rs_dma_stream (linked_list, &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_START_DPF:
			/*
			 * Start Dependent Functions Resource
			 */
			status = acpi_rs_start_depend_fns_stream (linked_list,
					  &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_END_DPF:
			/*
			 * End Dependent Functions Resource
			 */
			status = acpi_rs_end_depend_fns_stream (linked_list,
					  &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_IO:
			/*
			 * IO Port Resource
			 */
			status = acpi_rs_io_stream (linked_list, &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_FIXED_IO:
			/*
			 * Fixed IO Port Resource
			 */
			status = acpi_rs_fixed_io_stream (linked_list, &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_VENDOR:
			/*
			 * Vendor Defined Resource
			 */
			status = acpi_rs_vendor_stream (linked_list, &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_END_TAG:
			/*
			 * End Tag
			 */
			status = acpi_rs_end_tag_stream (linked_list, &buffer, &bytes_consumed);

			/*
			 * An End Tag indicates the end of the Resource Template
			 */
			done = TRUE;
			break;

		case ACPI_RSTYPE_MEM24:
			/*
			 * 24-Bit Memory Resource
			 */
			status = acpi_rs_memory24_stream (linked_list, &buffer, &bytes_consumed);
			break;

		case ACPI_RSTYPE_MEM32:
			/*
			 * 32-Bit Memory Range Resource
			 */
			status = acpi_rs_memory32_range_stream (linked_list, &buffer,
					 &bytes_consumed);
			break;

		case ACPI_RSTYPE_FIXED_MEM32:
			/*
			 * 32-Bit Fixed Memory Resource
			 */
			status = acpi_rs_fixed_memory32_stream (linked_list, &buffer,
					 &bytes_consumed);
			break;

		case ACPI_RSTYPE_ADDRESS16:
			/*
			 * 16-Bit Address Descriptor Resource
			 */
			status = acpi_rs_address16_stream (linked_list, &buffer,
					 &bytes_consumed);
			break;

		case ACPI_RSTYPE_ADDRESS32:
			/*
			 * 32-Bit Address Descriptor Resource
			 */
			status = acpi_rs_address32_stream (linked_list, &buffer,
					 &bytes_consumed);
			break;

		case ACPI_RSTYPE_ADDRESS64:
			/*
			 * 64-Bit Address Descriptor Resource
			 */
			status = acpi_rs_address64_stream (linked_list, &buffer,
					 &bytes_consumed);
			break;

		case ACPI_RSTYPE_EXT_IRQ:
			/*
			 * Extended IRQ Resource
			 */
			status = acpi_rs_extended_irq_stream (linked_list, &buffer,
					 &bytes_consumed);
			break;

		default:
			/*
			 * If we get here, everything is out of sync,
			 *  so exit with an error
			 */
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Invalid descriptor type (%X) in resource list\n",
				linked_list->id));
			status = AE_BAD_DATA;
			break;

		} /* switch (Linked_list->Id) */

		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}

		/*
		 * Set the Buffer to point to the open byte
		 */
		buffer += bytes_consumed;

		/*
		 * Point to the next object
		 */
		linked_list = ACPI_PTR_ADD (acpi_resource,
				  linked_list, linked_list->length);
	}

	return_ACPI_STATUS (AE_OK);
}

