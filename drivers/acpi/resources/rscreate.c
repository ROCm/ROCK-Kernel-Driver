/*******************************************************************************
 *
 * Module Name: rscreate - Create resource lists/tables
 *              $Revision: 58 $
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
#include "amlcode.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_RESOURCES
	 ACPI_MODULE_NAME    ("rscreate")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_create_resource_list
 *
 * PARAMETERS:  Byte_stream_buffer      - Pointer to the resource byte stream
 *              Output_buffer           - Pointer to the user's buffer
 *
 * RETURN:      Status  - AE_OK if okay, else a valid acpi_status code
 *              If Output_buffer is not large enough, Output_buffer_length
 *              indicates how large Output_buffer should be, else it
 *              indicates how may u8 elements of Output_buffer are valid.
 *
 * DESCRIPTION: Takes the byte stream returned from a _CRS, _PRS control method
 *              execution and parses the stream to create a linked list
 *              of device resources.
 *
 ******************************************************************************/

acpi_status
acpi_rs_create_resource_list (
	acpi_operand_object     *byte_stream_buffer,
	acpi_buffer             *output_buffer)
{

	acpi_status             status;
	u8                      *byte_stream_start;
	ACPI_SIZE               list_size_needed = 0;
	u32                     byte_stream_buffer_length;


	ACPI_FUNCTION_TRACE ("Rs_create_resource_list");


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Byte_stream_buffer = %p\n", byte_stream_buffer));

	/*
	 * Params already validated, so we don't re-validate here
	 */
	byte_stream_buffer_length = byte_stream_buffer->buffer.length;
	byte_stream_start = byte_stream_buffer->buffer.pointer;

	/*
	 * Pass the Byte_stream_buffer into a module that can calculate
	 * the buffer size needed for the linked list
	 */
	status = acpi_rs_get_list_length (byte_stream_start, byte_stream_buffer_length,
			 &list_size_needed);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Status=%X List_size_needed=%X\n",
		status, (u32) list_size_needed));
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer (output_buffer, list_size_needed);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Do the conversion */

	status = acpi_rs_byte_stream_to_list (byte_stream_start, byte_stream_buffer_length,
			  output_buffer->pointer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Output_buffer %p Length %X\n",
			output_buffer->pointer, (u32) output_buffer->length));
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_create_pci_routing_table
 *
 * PARAMETERS:  Package_object          - Pointer to an acpi_operand_object
 *                                        package
 *              Output_buffer           - Pointer to the user's buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid acpi_status code.
 *              If the Output_buffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and Output_buffer->Length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Takes the acpi_operand_object  package and creates a
 *              linked list of PCI interrupt descriptions
 *
 * NOTE: It is the caller's responsibility to ensure that the start of the
 * output buffer is aligned properly (if necessary).
 *
 ******************************************************************************/

acpi_status
acpi_rs_create_pci_routing_table (
	acpi_operand_object     *package_object,
	acpi_buffer             *output_buffer)
{
	u8                      *buffer;
	acpi_operand_object     **top_object_list = NULL;
	acpi_operand_object     **sub_object_list = NULL;
	acpi_operand_object     *package_element = NULL;
	ACPI_SIZE               buffer_size_needed = 0;
	u32                     number_of_elements = 0;
	u32                     index = 0;
	acpi_pci_routing_table  *user_prt = NULL;
	acpi_namespace_node     *node;
	acpi_status             status;
	acpi_buffer             path_buffer;


	ACPI_FUNCTION_TRACE ("Rs_create_pci_routing_table");


	/* Params already validated, so we don't re-validate here */

	/*
	 * Get the required buffer length
	 */
	status = acpi_rs_get_pci_routing_table_length (package_object,
			 &buffer_size_needed);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Buffer_size_needed = %X\n", (u32) buffer_size_needed));

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer (output_buffer, buffer_size_needed);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 * Loop through the ACPI_INTERNAL_OBJECTS - Each object should contain an
	 * acpi_integer Address, a u8 Pin, a Name and a u8 Source_index.
	 */
	top_object_list  = package_object->package.elements;
	number_of_elements = package_object->package.count;
	buffer           = output_buffer->pointer;
	user_prt         = ACPI_CAST_PTR (acpi_pci_routing_table, buffer);

	for (index = 0; index < number_of_elements; index++) {
		/*
		 * Point User_prt past this current structure
		 *
		 * NOTE: On the first iteration, User_prt->Length will
		 * be zero because we cleared the return buffer earlier
		 */
		buffer += user_prt->length;
		user_prt = ACPI_CAST_PTR (acpi_pci_routing_table, buffer);

		/*
		 * Fill in the Length field with the information we have at this point.
		 * The minus four is to subtract the size of the u8 Source[4] member
		 * because it is added below.
		 */
		user_prt->length = (sizeof (acpi_pci_routing_table) -4);

		/*
		 * Dereference the sub-package
		 */
		package_element = *top_object_list;

		/*
		 * The Sub_object_list will now point to an array of the four IRQ
		 * elements: Address, Pin, Source and Source_index
		 */
		sub_object_list = package_element->package.elements;

		/*
		 * 1) First subobject:  Dereference the Address
		 */
		if (ACPI_GET_OBJECT_TYPE (*sub_object_list) == ACPI_TYPE_INTEGER) {
			user_prt->address = (*sub_object_list)->integer.value;
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Need Integer, found %s\n",
				acpi_ut_get_object_type_name (*sub_object_list)));
			return_ACPI_STATUS (AE_BAD_DATA);
		}

		/*
		 * 2) Second subobject: Dereference the Pin
		 */
		sub_object_list++;

		if (ACPI_GET_OBJECT_TYPE (*sub_object_list) == ACPI_TYPE_INTEGER) {
			user_prt->pin = (u32) (*sub_object_list)->integer.value;
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Need Integer, found %s\n",
				acpi_ut_get_object_type_name (*sub_object_list)));
			return_ACPI_STATUS (AE_BAD_DATA);
		}

		/*
		 * 3) Third subobject: Dereference the Source Name
		 */
		sub_object_list++;

		switch (ACPI_GET_OBJECT_TYPE (*sub_object_list)) {
		case INTERNAL_TYPE_REFERENCE:

			if ((*sub_object_list)->reference.opcode != AML_INT_NAMEPATH_OP) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Need name, found reference op %X\n",
					(*sub_object_list)->reference.opcode));
				return_ACPI_STATUS (AE_BAD_DATA);
			}

			node = (*sub_object_list)->reference.node;

			/* Use *remaining* length of the buffer as max for pathname */

			path_buffer.length = output_buffer->length -
					   (u32) ((u8 *) user_prt->source - (u8 *) output_buffer->pointer);
			path_buffer.pointer = user_prt->source;

			status = acpi_ns_handle_to_pathname ((acpi_handle) node, &path_buffer);

			user_prt->length += ACPI_STRLEN (user_prt->source) + 1; /* include null terminator */
			break;


		case ACPI_TYPE_STRING:

			ACPI_STRCPY (user_prt->source,
				  (*sub_object_list)->string.pointer);

			/* Add to the Length field the length of the string */

			user_prt->length += (*sub_object_list)->string.length;
			break;


		case ACPI_TYPE_INTEGER:
			/*
			 * If this is a number, then the Source Name is NULL, since the
			 * entire buffer was zeroed out, we can leave this alone.
			 *
			 * Add to the Length field the length of the u32 NULL
			 */
			user_prt->length += sizeof (u32);
			break;


		default:

		   ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Need Integer, found %s\n",
				acpi_ut_get_object_type_name (*sub_object_list)));
		   return_ACPI_STATUS (AE_BAD_DATA);
		}

		/* Now align the current length */

		user_prt->length = ACPI_ROUND_UP_TO_64_bITS (user_prt->length);

		/*
		 * 4) Fourth subobject: Dereference the Source Index
		 */
		sub_object_list++;

		if (ACPI_GET_OBJECT_TYPE (*sub_object_list) == ACPI_TYPE_INTEGER) {
			user_prt->source_index = (u32) (*sub_object_list)->integer.value;
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Need Integer, found %s\n",
				acpi_ut_get_object_type_name (*sub_object_list)));
			return_ACPI_STATUS (AE_BAD_DATA);
		}

		/* Point to the next acpi_operand_object */

		top_object_list++;
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Output_buffer %p Length %X\n",
			output_buffer->pointer, (u32) output_buffer->length));
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_rs_create_byte_stream
 *
 * PARAMETERS:  Linked_list_buffer      - Pointer to the resource linked list
 *              Output_buffer           - Pointer to the user's buffer
 *
 * RETURN:      Status  AE_OK if okay, else a valid acpi_status code.
 *              If the Output_buffer is too small, the error will be
 *              AE_BUFFER_OVERFLOW and Output_buffer->Length will point
 *              to the size buffer needed.
 *
 * DESCRIPTION: Takes the linked list of device resources and
 *              creates a bytestream to be used as input for the
 *              _SRS control method.
 *
 ******************************************************************************/

acpi_status
acpi_rs_create_byte_stream (
	acpi_resource           *linked_list_buffer,
	acpi_buffer             *output_buffer)
{
	acpi_status             status;
	ACPI_SIZE               byte_stream_size_needed = 0;


	ACPI_FUNCTION_TRACE ("Rs_create_byte_stream");


	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Linked_list_buffer = %p\n", linked_list_buffer));

	/*
	 * Params already validated, so we don't re-validate here
	 *
	 * Pass the Linked_list_buffer into a module that calculates
	 * the buffer size needed for the byte stream.
	 */
	status = acpi_rs_get_byte_stream_length (linked_list_buffer,
			 &byte_stream_size_needed);

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Byte_stream_size_needed=%X, %s\n",
		(u32) byte_stream_size_needed, acpi_format_exception (status)));
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Validate/Allocate/Clear caller buffer */

	status = acpi_ut_initialize_buffer (output_buffer, byte_stream_size_needed);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Do the conversion */

	status = acpi_rs_list_to_byte_stream (linked_list_buffer, byte_stream_size_needed,
			  output_buffer->pointer);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Output_buffer %p Length %X\n",
			output_buffer->pointer, (u32) output_buffer->length));
	return_ACPI_STATUS (AE_OK);
}

