/******************************************************************************
 *
 * Module Name: exconvrt - Object conversion routines
 *              $Revision: 39 $
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


#define _COMPONENT          ACPI_EXECUTER
	 ACPI_MODULE_NAME    ("exconvrt")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_to_integer
 *
 * PARAMETERS:  *Obj_desc       - Object to be converted.  Must be an
 *                                Integer, Buffer, or String
 *              Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an ACPI Object to an integer.
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_integer (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc,
	acpi_walk_state         *walk_state)
{
	u32                     i;
	acpi_operand_object     *ret_desc;
	u32                     count;
	u8                      *pointer;
	acpi_integer            result;
	acpi_status             status;


	ACPI_FUNCTION_TRACE_PTR ("Ex_convert_to_integer", obj_desc);


	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_INTEGER:
		*result_desc = obj_desc;
		return_ACPI_STATUS (AE_OK);

	case ACPI_TYPE_STRING:
		pointer = (u8 *) obj_desc->string.pointer;
		count   = obj_desc->string.length;
		break;

	case ACPI_TYPE_BUFFER:
		pointer = obj_desc->buffer.pointer;
		count   = obj_desc->buffer.length;
		break;

	default:
		return_ACPI_STATUS (AE_TYPE);
	}

	/*
	 * Convert the buffer/string to an integer.  Note that both buffers and
	 * strings are treated as raw data - we don't convert ascii to hex for
	 * strings.
	 *
	 * There are two terminating conditions for the loop:
	 * 1) The size of an integer has been reached, or
	 * 2) The end of the buffer or string has been reached
	 */
	result = 0;

	/* Transfer no more than an integer's worth of data */

	if (count > acpi_gbl_integer_byte_width) {
		count = acpi_gbl_integer_byte_width;
	}

	/*
	 * String conversion is different than Buffer conversion
	 */
	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_STRING:

		/*
		 * Convert string to an integer
		 * String must be hexadecimal as per the ACPI specification
		 */
		status = acpi_ut_strtoul64 ((char *) pointer, 16, &result);
		if (ACPI_FAILURE (status)) {
			return_ACPI_STATUS (status);
		}
		break;


	case ACPI_TYPE_BUFFER:

		/*
		 * Buffer conversion - we simply grab enough raw data from the
		 * buffer to fill an integer
		 */
		for (i = 0; i < count; i++) {
			/*
			 * Get next byte and shift it into the Result.
			 * Little endian is used, meaning that the first byte of the buffer
			 * is the LSB of the integer
			 */
			result |= (((acpi_integer) pointer[i]) << (i * 8));
		}
		break;


	default:
		/* No other types can get here */
		break;
	}

	/*
	 * Create a new integer
	 */
	ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
	if (!ret_desc) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Save the Result, delete original descriptor, store new descriptor */

	ret_desc->integer.value = result;

	if (*result_desc == obj_desc) {
		if (walk_state->opcode != AML_STORE_OP) {
			acpi_ut_remove_reference (obj_desc);
		}
	}

	*result_desc = ret_desc;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_to_buffer
 *
 * PARAMETERS:  *Obj_desc       - Object to be converted.  Must be an
 *                                Integer, Buffer, or String
 *              Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an ACPI Object to a Buffer
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_buffer (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *ret_desc;
	u32                     i;
	u8                      *new_buf;


	ACPI_FUNCTION_TRACE_PTR ("Ex_convert_to_buffer", obj_desc);


	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_INTEGER:

		/*
		 * Create a new Buffer object
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		if (!ret_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Need enough space for one integer */

		new_buf = ACPI_MEM_CALLOCATE (acpi_gbl_integer_byte_width);
		if (!new_buf) {
			ACPI_REPORT_ERROR
				(("Ex_convert_to_buffer: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Copy the integer to the buffer */

		for (i = 0; i < acpi_gbl_integer_byte_width; i++) {
			new_buf[i] = (u8) (obj_desc->integer.value >> (i * 8));
		}

		/* Complete buffer object initialization */

		ret_desc->buffer.flags |= AOPOBJ_DATA_VALID;
		ret_desc->buffer.pointer = new_buf;
		ret_desc->buffer.length = acpi_gbl_integer_byte_width;

		/* Return the new buffer descriptor */

		*result_desc = ret_desc;
		break;


	case ACPI_TYPE_STRING:
		/*
		 * Create a new Buffer object
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		if (!ret_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Need enough space for one integer */

		new_buf = ACPI_MEM_CALLOCATE (obj_desc->string.length);
		if (!new_buf) {
			ACPI_REPORT_ERROR
				(("Ex_convert_to_buffer: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		ACPI_STRNCPY ((char *) new_buf, (char *) obj_desc->string.pointer, obj_desc->string.length);
		ret_desc->buffer.flags |= AOPOBJ_DATA_VALID;
		ret_desc->buffer.pointer = new_buf;
		ret_desc->buffer.length = obj_desc->string.length;

		/* Return the new buffer descriptor */

		*result_desc = ret_desc;
		break;


	case ACPI_TYPE_BUFFER:
		*result_desc = obj_desc;
		break;


	default:
		return_ACPI_STATUS (AE_TYPE);
	}

	/* Mark buffer initialized */

	(*result_desc)->common.flags |= AOPOBJ_DATA_VALID;
	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_ascii
 *
 * PARAMETERS:  Integer         - Value to be converted
 *              Base            - 10 or 16
 *              String          - Where the string is returned
 *
 * RETURN:      Actual string length
 *
 * DESCRIPTION: Convert an ACPI Integer to a hex or decimal string
 *
 ******************************************************************************/

u32
acpi_ex_convert_to_ascii (
	acpi_integer            integer,
	u32                     base,
	u8                      *string)
{
	u32                     i;
	u32                     j;
	u32                     k = 0;
	char                    hex_digit;
	acpi_integer            digit;
	u32                     remainder;
	u32                     length = sizeof (acpi_integer);
	u8                      leading_zero = TRUE;


	ACPI_FUNCTION_ENTRY ();


	switch (base) {
	case 10:

		remainder = 0;
		for (i = ACPI_MAX_DECIMAL_DIGITS; i > 0 ; i--) {
			/* Divide by nth factor of 10 */

			digit = integer;
			for (j = 1; j < i; j++) {
				(void) acpi_ut_short_divide (&digit, 10, &digit, &remainder);
			}

			/* Create the decimal digit */

			if (digit != 0) {
				leading_zero = FALSE;
			}

			if (!leading_zero) {
				string[k] = (u8) (ACPI_ASCII_ZERO + remainder);
				k++;
			}
		}
		break;

	case 16:

		/* Copy the integer to the buffer */

		for (i = 0, j = ((length * 2) -1); i < (length * 2); i++, j--) {

			hex_digit = acpi_ut_hex_to_ascii_char (integer, (j * 4));
			if (hex_digit != ACPI_ASCII_ZERO) {
				leading_zero = FALSE;
			}

			if (!leading_zero) {
				string[k] = (u8) hex_digit;
				k++;
			}
		}
		break;

	default:
		break;
	}

	/*
	 * Since leading zeros are supressed, we must check for the case where
	 * the integer equals 0.
	 *
	 * Finally, null terminate the string and return the length
	 */
	if (!k) {
		string [0] = ACPI_ASCII_ZERO;
		k = 1;
	}
	string [k] = 0;

	return (k);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_to_string
 *
 * PARAMETERS:  *Obj_desc       - Object to be converted.  Must be an
 *                                Integer, Buffer, or String
 *              Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Convert an ACPI Object to a string
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_string (
	acpi_operand_object     *obj_desc,
	acpi_operand_object     **result_desc,
	u32                     base,
	u32                     max_length,
	acpi_walk_state         *walk_state)
{
	acpi_operand_object     *ret_desc;
	u32                     i;
	u32                     index;
	u32                     string_length;
	u8                      *new_buf;
	u8                      *pointer;


	ACPI_FUNCTION_TRACE_PTR ("Ex_convert_to_string", obj_desc);


	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_INTEGER:

		string_length = acpi_gbl_integer_byte_width * 2;
		if (base == 10) {
			string_length = ACPI_MAX_DECIMAL_DIGITS;
		}

		/*
		 * Create a new String
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_STRING);
		if (!ret_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Need enough space for one ASCII integer plus null terminator */

		new_buf = ACPI_MEM_CALLOCATE ((ACPI_SIZE) string_length + 1);
		if (!new_buf) {
			ACPI_REPORT_ERROR
				(("Ex_convert_to_string: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* Convert */

		i = acpi_ex_convert_to_ascii (obj_desc->integer.value, base, new_buf);

		/* Null terminate at the correct place */

		if (max_length < i) {
			new_buf[max_length] = 0;
			ret_desc->string.length = max_length;
		}
		else {
			new_buf [i] = 0;
			ret_desc->string.length = i;
		}

		ret_desc->buffer.pointer = new_buf;

		/* Return the new buffer descriptor */

		if (*result_desc == obj_desc) {
			if (walk_state->opcode != AML_STORE_OP) {
				acpi_ut_remove_reference (obj_desc);
			}
		}

		*result_desc = ret_desc;
		break;


	case ACPI_TYPE_BUFFER:

		string_length = obj_desc->buffer.length * 3;
		if (base == 10) {
			string_length = obj_desc->buffer.length * 4;
		}

		if (max_length > ACPI_MAX_STRING_CONVERSION) {
			if (string_length > ACPI_MAX_STRING_CONVERSION) {
				return_ACPI_STATUS (AE_AML_STRING_LIMIT);
			}
		}

		/*
		 * Create a new string object
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_STRING);
		if (!ret_desc) {
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/* String length is the lesser of the Max or the actual length */

		if (max_length < string_length) {
			string_length = max_length;
		}

		new_buf = ACPI_MEM_CALLOCATE ((ACPI_SIZE) string_length + 1);
		if (!new_buf) {
			ACPI_REPORT_ERROR
				(("Ex_convert_to_string: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return_ACPI_STATUS (AE_NO_MEMORY);
		}

		/*
		 * Convert each byte of the buffer to two ASCII characters plus a space.
		 */
		pointer = obj_desc->buffer.pointer;
		index = 0;
		for (i = 0, index = 0; i < obj_desc->buffer.length; i++) {
			index = acpi_ex_convert_to_ascii ((acpi_integer) pointer[i], base, &new_buf[index]);

			new_buf[index] = ' ';
			index++;
		}

		/* Null terminate */

		new_buf [index-1] = 0;
		ret_desc->buffer.pointer = new_buf;
		ret_desc->string.length = ACPI_STRLEN ((char *) new_buf);

		/* Return the new buffer descriptor */

		if (*result_desc == obj_desc) {
			if (walk_state->opcode != AML_STORE_OP) {
				acpi_ut_remove_reference (obj_desc);
			}
		}

		*result_desc = ret_desc;
		break;


	case ACPI_TYPE_STRING:

		if (max_length >= obj_desc->string.length) {
			*result_desc = obj_desc;
		}

		else {
			/* Must copy the string first and then truncate it */

			return_ACPI_STATUS (AE_NOT_IMPLEMENTED);
		}
		break;


	default:
		return_ACPI_STATUS (AE_TYPE);
	}

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_to_target_type
 *
 * PARAMETERS:  Destination_type    - Current type of the destination
 *              Source_desc         - Source object to be converted.
 *              Walk_state          - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Implements "implicit conversion" rules for storing an object.
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_target_type (
	acpi_object_type        destination_type,
	acpi_operand_object     *source_desc,
	acpi_operand_object     **result_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;


	ACPI_FUNCTION_TRACE ("Ex_convert_to_target_type");


	/* Default behavior */

	*result_desc = source_desc;

	/*
	 * If required by the target,
	 * perform implicit conversion on the source before we store it.
	 */
	switch (GET_CURRENT_ARG_TYPE (walk_state->op_info->runtime_args)) {
	case ARGI_SIMPLE_TARGET:
	case ARGI_FIXED_TARGET:
	case ARGI_INTEGER_REF:      /* Handles Increment, Decrement cases */

		switch (destination_type) {
		case INTERNAL_TYPE_REGION_FIELD:
			/*
			 * Named field can always handle conversions
			 */
			break;

		default:
			/* No conversion allowed for these types */

			if (destination_type != ACPI_GET_OBJECT_TYPE (source_desc)) {
				ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
					"Explicit operator, will store (%s) over existing type (%s)\n",
					acpi_ut_get_object_type_name (source_desc),
					acpi_ut_get_type_name (destination_type)));
				status = AE_TYPE;
			}
		}
		break;


	case ARGI_TARGETREF:

		switch (destination_type) {
		case ACPI_TYPE_INTEGER:
		case ACPI_TYPE_BUFFER_FIELD:
		case INTERNAL_TYPE_BANK_FIELD:
		case INTERNAL_TYPE_INDEX_FIELD:
			/*
			 * These types require an Integer operand.  We can convert
			 * a Buffer or a String to an Integer if necessary.
			 */
			status = acpi_ex_convert_to_integer (source_desc, result_desc, walk_state);
			break;


		case ACPI_TYPE_STRING:

			/*
			 * The operand must be a String.  We can convert an
			 * Integer or Buffer if necessary
			 */
			status = acpi_ex_convert_to_string (source_desc, result_desc, 16, ACPI_UINT32_MAX, walk_state);
			break;


		case ACPI_TYPE_BUFFER:

			/*
			 * The operand must be a Buffer.  We can convert an
			 * Integer or String if necessary
			 */
			status = acpi_ex_convert_to_buffer (source_desc, result_desc, walk_state);
			break;


		default:
			status = AE_AML_INTERNAL;
			break;
		}
		break;


	case ARGI_REFERENCE:
		/*
		 * Create_xxxx_field cases - we are storing the field object into the name
		 */
		break;


	default:
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Unknown Target type ID 0x%X Op %s Dest_type %s\n",
			GET_CURRENT_ARG_TYPE (walk_state->op_info->runtime_args),
			walk_state->op_info->name, acpi_ut_get_type_name (destination_type)));

		status = AE_AML_INTERNAL;
	}

	/*
	 * Source-to-Target conversion semantics:
	 *
	 * If conversion to the target type cannot be performed, then simply
	 * overwrite the target with the new object and type.
	 */
	if (status == AE_TYPE) {
		status = AE_OK;
	}

	return_ACPI_STATUS (status);
}


