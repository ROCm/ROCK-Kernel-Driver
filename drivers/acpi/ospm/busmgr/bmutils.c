/*****************************************************************************
 *
 * Module Name: bmutils.c
 *   $Revision: 43 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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


#include <acpi.h>
#include "bm.h"


#define _COMPONENT		ACPI_BUS
	MODULE_NAME		("bmutils")


#ifdef ACPI_DEBUG
#define DEBUG_EVAL_ERROR(l,h,p,s)    bm_print_eval_error(l,h,p,s)
#else
#define DEBUG_EVAL_ERROR(l,h,p,s)
#endif


/****************************************************************************
 *                            External Functions
 ****************************************************************************/

/****************************************************************************
 *
 * FUNCTION:    bm_print_eval_error
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

void
bm_print_eval_error (
	u32                     debug_level,
	acpi_handle             handle,
	acpi_string             pathname,
	acpi_status             status)
{
	acpi_buffer		buffer;
	acpi_status		local_status;

	PROC_NAME("bm_print_eval_error");

	buffer.length = 256;
	buffer.pointer = acpi_os_callocate(buffer.length);
	if (!buffer.pointer) {
		return;
	}

	local_status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	if (ACPI_FAILURE(local_status)) {
		ACPI_DEBUG_PRINT((ACPI_DEBUG_LEVEL(debug_level), "Evaluate object [%p], %s\n", handle,
			acpi_format_exception(status)));
		return;
	}

	if (pathname) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Evaluate object [%s.%s], %s\n", (char*)buffer.pointer, pathname,
			acpi_format_exception(status)));
	}
	else {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Evaluate object [%s], %s\n", (char*)buffer.pointer,
			acpi_format_exception(status)));
	}

	acpi_os_free(buffer.pointer);
}


/****************************************************************************
 *
 * FUNCTION:    bm_copy_to_buffer
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_copy_to_buffer (
	acpi_buffer             *buffer,
	void                    *data,
	u32                     length)
{
	FUNCTION_TRACE("bm_copy_to_buffer");

	if (!buffer || (!buffer->pointer) || !data || (length == 0)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (length > buffer->length) {
		buffer->length = length;
		return_ACPI_STATUS(AE_BUFFER_OVERFLOW);
	}

	buffer->length = length;
	MEMCPY(buffer->pointer, data, length);

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_cast_buffer
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_cast_buffer (
	acpi_buffer             *buffer,
	void                    **pointer,
	u32                     length)
{
	FUNCTION_TRACE("bm_cast_buffer");

	if (!buffer || !buffer->pointer || !pointer || length == 0) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (length > buffer->length) {
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	*pointer = buffer->pointer;

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_extract_package_data
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_extract_package_data (
	acpi_object             *package,
	acpi_buffer             *format,
	acpi_buffer             *buffer)
{
	u32                     tail_offset = 0;
	u32                     size_required = 0;
	char			*format_string = NULL;
	u32                     format_count = 0;
	u32                     i = 0;
	u8                      *head = NULL;
	u8                      *tail = NULL;

	FUNCTION_TRACE("bm_extract_package_data");

	if (!package || (package->type != ACPI_TYPE_PACKAGE) || (package->package.count < 1)) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid 'package' argument\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!format || !format->pointer || (format->length < 1)) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid 'format' argument\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	if (!buffer) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid 'buffer' argument\n"));
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	format_count = (format->length/sizeof(char)) - 1;
	if (format_count > package->package.count) {
		ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Format specifies more objects [%d] than exist in package [%d].", format_count, package->package.count));
		return_ACPI_STATUS(AE_BAD_DATA);
	}

	format_string = (char*)format->pointer;

	/*
	 * Calculate size_required.
	 */
	for (i=0; i<format_count; i++) {

		acpi_object *element = &(package->package.elements[i]);

		if (!element) {
			return_ACPI_STATUS(AE_BAD_DATA);
		}

		switch (element->type) {

		case ACPI_TYPE_INTEGER:
			switch (format_string[i]) {
			case 'N':
				size_required += sizeof(acpi_integer);
				tail_offset += sizeof(acpi_integer);
				break;
			case 'S':
				size_required += sizeof(char*) + sizeof(acpi_integer) + sizeof(char);
				tail_offset += sizeof(char*);
				break;
			default:
				ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid package element [%d]: got number, expecing [%c].\n", i, format_string[i]));
				return_ACPI_STATUS(AE_BAD_DATA);
				break;
			}
			break;

		case ACPI_TYPE_STRING:
		case ACPI_TYPE_BUFFER:
			switch (format_string[i]) {
			case 'S':
				size_required += sizeof(char*) + (element->string.length * sizeof(char)) + sizeof(char);
				tail_offset += sizeof(char*);
				break;
			case 'B':
				size_required += sizeof(u8*) + (element->buffer.length * sizeof(u8));
				tail_offset += sizeof(u8*);
				break;
			default:
				ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid package element [%d] got string/buffer, expecing [%c].\n", i, format_string[i]));
				return_ACPI_STATUS(AE_BAD_DATA);
				break;
			}
			break;

		case ACPI_TYPE_PACKAGE:
		default:
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found unsupported element at index=%d\n", i));
			/* TBD: handle nested packages... */
			return_ACPI_STATUS(AE_SUPPORT);
			break;
		}
	}

	/* 
	 * Validate output buffer. 
	 */
	if (buffer->length < size_required) {
		buffer->length = size_required;
		return_ACPI_STATUS(AE_BUFFER_OVERFLOW);
	}
	else if (buffer->length != size_required || !buffer->pointer) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	head = buffer->pointer;
	tail = buffer->pointer + tail_offset;

	/* 
	 * Extract package data.
	 */
	for (i=0; i<format_count; i++) {

		u8 **pointer = NULL;
		acpi_object *element = &(package->package.elements[i]);

		switch (element->type) {

		case ACPI_TYPE_INTEGER:
			switch (format_string[i]) {
			case 'N':
				*((acpi_integer*)head) = element->integer.value;
				head += sizeof(acpi_integer);
				break;
			case 'S':
				pointer = (u8**)head;
				*pointer = tail;
				*((acpi_integer*)tail) = element->integer.value;
				head += sizeof(acpi_integer*);
				tail += sizeof(acpi_integer);
				/* NULL terminate string */
				*tail = (char)0;
				tail += sizeof(char);
				break;
			default:
				/* Should never get here */
				break;
			}
			break;

		case ACPI_TYPE_STRING:
		case ACPI_TYPE_BUFFER:
			switch (format_string[i]) {
			case 'S':
				pointer = (u8**)head;
				*pointer = tail;
				memcpy(tail, element->string.pointer, element->string.length);
				head += sizeof(char*);
				tail += element->string.length * sizeof(char);
				/* NULL terminate string */
				*tail = (char)0;
				tail += sizeof(char);
				break;
			case 'B':
				pointer = (u8**)head;
				*pointer = tail;
				memcpy(tail, element->buffer.pointer, element->buffer.length);
				head += sizeof(u8*);
				tail += element->buffer.length * sizeof(u8);
				break;
			default:
				/* Should never get here */
				break;
			}
			break;

		case ACPI_TYPE_PACKAGE:
			/* TBD: handle nested packages... */
		default:
			/* Should never get here */
			break;
		}
	}

	return_ACPI_STATUS(AE_OK);
}


/****************************************************************************
 *
 * FUNCTION:    bm_evaluate_object
 *
 * PARAMETERS:
 *
 * RETURN:      AE_OK
 *              AE_BUFFER_OVERFLOW  Evaluated object returned data, but
 *                                  caller did not provide buffer.
 *
 * DESCRIPTION: Helper for acpi_evaluate_object that handles buffer
 *              allocation.  Note that the caller is responsible for
 *              freeing buffer->pointer!
 *
 ****************************************************************************/

acpi_status
bm_evaluate_object (
	acpi_handle             handle,
	acpi_string             pathname,
	acpi_object_list        *arguments,
	acpi_buffer             *buffer)
{
	acpi_status             status = AE_OK;

	FUNCTION_TRACE("bm_evaluate_object");

	/* If caller provided a buffer it must be unallocated/zero'd. */
	if ((buffer) && (buffer->length != 0 || buffer->pointer)) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	/*
	 * Evalute Object:
	 * ---------------
	 * The first attempt is just to get the size of the object data
	 * (that is unless there's no return data, e.g. _INI); the second
	 * gets the data.
	 */
	status = acpi_evaluate_object(handle, pathname, arguments, buffer);
	if (ACPI_SUCCESS(status)) {
		return_ACPI_STATUS(status);
	}
	else if ((buffer) && (status == AE_BUFFER_OVERFLOW)) {

		/* Gotta allocate -- CALLER MUST FREE! */
		buffer->pointer = acpi_os_callocate(buffer->length);
		if (!buffer->pointer) {
			return_ACPI_STATUS(AE_NO_MEMORY);
		}

		/* Re-evaluate -- this time it should work */
		status = acpi_evaluate_object(handle, pathname,
			arguments, buffer);
	}

	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			DEBUG_EVAL_ERROR(ACPI_LV_WARN, handle, pathname,
				status);
		}
		if (buffer && buffer->pointer) {
			acpi_os_free(buffer->pointer);
			buffer->pointer = NULL;
			buffer->length = 0;
		}
	}

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_evaluate_simple_integer
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status
bm_evaluate_simple_integer (
	acpi_handle             handle,
	acpi_string             pathname,
	u32                     *data)
{
	acpi_status             status = AE_OK;
	acpi_object             *element = NULL;
	acpi_buffer             buffer;

	FUNCTION_TRACE("bm_evaluate_simple_integer");

	if (!data) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	MEMSET(&buffer, 0, sizeof(acpi_buffer));

	/*
	 * Evaluate Object:
	 * ----------------
	 */
	status = bm_evaluate_object(handle, pathname, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "failed to evaluate object (%s)\n",
			acpi_format_exception(status)));
		goto end;
	}

	/*
	 * Validate Data:
	 * --------------
	 */
	status = bm_cast_buffer(&buffer, (void**)&element,
		sizeof(acpi_object));
	if (ACPI_FAILURE(status)) {
		DEBUG_EVAL_ERROR(ACPI_LV_WARN, handle, pathname, status);
		goto end;
	}

	if (element->type != ACPI_TYPE_INTEGER) {
		status = AE_BAD_DATA;
		DEBUG_EVAL_ERROR(ACPI_LV_WARN, handle, pathname, status);
		goto end;
	}

	*data = element->integer.value;

end:
	acpi_os_free(buffer.pointer);

	return_ACPI_STATUS(status);
}


/****************************************************************************
 *
 * FUNCTION:    bm_evaluate_reference_list
 *
 * PARAMETERS:
 *
 * RETURN:
 *
 * DESCRIPTION:
 *
 ****************************************************************************/

acpi_status  
bm_evaluate_reference_list (
	acpi_handle             handle,
	acpi_string             pathname,
	BM_HANDLE_LIST          *reference_list)
{
	acpi_status             status = AE_OK;
	acpi_object             *package = NULL;
	acpi_object             *element = NULL;
	acpi_handle  		reference_handle = NULL;
	acpi_buffer             buffer;
	u32                     i = 0;

	FUNCTION_TRACE("bm_evaluate_reference_list");

	if (!reference_list) {
		return_ACPI_STATUS(AE_BAD_PARAMETER);
	}

	MEMSET(&buffer, 0, sizeof(acpi_buffer));

	/*
	 * Evaluate Object:
	 * ----------------
	 */
	status = bm_evaluate_object(handle, pathname, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		goto end;
	}

	/*
	 * Validate Package:
	 * -----------------
	 */
	status = bm_cast_buffer(&buffer, (void**)&package,
		sizeof(acpi_object));
	if (ACPI_FAILURE(status)) {
		DEBUG_EVAL_ERROR(ACPI_LV_WARN, handle, pathname, status);
		goto end;
	}

	if (package->type != ACPI_TYPE_PACKAGE) {
		status = AE_BAD_DATA;
		DEBUG_EVAL_ERROR(ACPI_LV_WARN, handle, pathname, status);
		goto end;
	}

	if (package->package.count > BM_HANDLES_MAX) {
		package->package.count = BM_HANDLES_MAX;
	}

	/*
	 * Parse Package Data:
	 * -------------------
	 */
	for (i = 0; i < package->package.count; i++) {

		element = &(package->package.elements[i]);

		if (!element || (element->type != ACPI_TYPE_STRING)) {
			status = AE_BAD_DATA;
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Invalid element in package (not a device reference).\n"));
			DEBUG_EVAL_ERROR (ACPI_LV_WARN, handle, pathname, status);
			break;
		}

		/*
		 * Resolve reference string (e.g. "\_PR_.CPU_") to an
		 * acpi_handle.
		 */
		status = acpi_get_handle(handle,
			element->string.pointer, &reference_handle);
		if (ACPI_FAILURE(status)) {
			status = AE_BAD_DATA;
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Unable to resolve device reference [%s].\n", element->string.pointer));
			DEBUG_EVAL_ERROR (ACPI_LV_WARN, handle, pathname, status);
			break;
		}

		/*
		 * Resolve acpi_handle to BM_HANDLE.
		 */
		status = bm_get_handle(reference_handle,
			&(reference_list->handles[i]));
		if (ACPI_FAILURE(status)) {
			status = AE_BAD_DATA;
			ACPI_DEBUG_PRINT((ACPI_DB_WARN, "Unable to resolve device reference for [%p].\n", reference_handle));
			DEBUG_EVAL_ERROR (ACPI_LV_WARN, handle, pathname, status);
			break;
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resolved reference [%s]->[%p]->[%02x]\n", element->string.pointer, reference_handle, reference_list->handles[i]));

		(reference_list->count)++;
	}

end:
	acpi_os_free(buffer.pointer);

	return_ACPI_STATUS(status);
}


