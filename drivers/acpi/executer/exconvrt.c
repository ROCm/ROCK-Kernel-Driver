/******************************************************************************
 *
 * Module Name: exconvrt - Object conversion routines
 *              $Revision: 24 $
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
#include "acparser.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "acevents.h"
#include "amlcode.h"
#include "acdispat.h"


#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exconvrt")


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
	char                    *pointer;
	acpi_integer            result;
	u32                     integer_size = sizeof (acpi_integer);


	FUNCTION_ENTRY ();


	switch (obj_desc->common.type) {
	case ACPI_TYPE_INTEGER:
		*result_desc = obj_desc;
		return (AE_OK);

	case ACPI_TYPE_STRING:
		pointer = obj_desc->string.pointer;
		count   = obj_desc->string.length;
		break;

	case ACPI_TYPE_BUFFER:
		pointer = (char *) obj_desc->buffer.pointer;
		count   = obj_desc->buffer.length;
		break;

	default:
		return (AE_TYPE);
	}

	/*
	 * Create a new integer
	 */
	ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_INTEGER);
	if (!ret_desc) {
		return (AE_NO_MEMORY);
	}


	/* Handle both ACPI 1.0 and ACPI 2.0 Integer widths */

	if (walk_state->method_node->flags & ANOBJ_DATA_WIDTH_32) {
		/*
		 * We are running a method that exists in a 32-bit ACPI table.
		 * Truncate the value to 32 bits by zeroing out the upper 32-bit field
		 */
		integer_size = sizeof (u32);
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

	if (count > integer_size) {
		count = integer_size;
	}

	/*
	 * String conversion is different than Buffer conversion
	 */
	switch (obj_desc->common.type) {
	case ACPI_TYPE_STRING:

		/* TBD: Need to use 64-bit STRTOUL */

		/*
		 * Convert string to an integer
		 * String must be hexadecimal as per the ACPI specification
		 */
		result = STRTOUL (pointer, NULL, 16);
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
	}

	/* Save the Result, delete original descriptor, store new descriptor */

	ret_desc->integer.value = result;

	if (*result_desc == obj_desc) {
		if (walk_state->opcode != AML_STORE_OP) {
			acpi_ut_remove_reference (obj_desc);
		}
	}

	*result_desc = ret_desc;
	return (AE_OK);
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
 * DESCRIPTION: Convert an ACPI Object to an Buffer
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
	u32                     integer_size = sizeof (acpi_integer);
	u8                      *new_buf;


	FUNCTION_ENTRY ();


	switch (obj_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		/*
		 * Create a new Buffer
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_BUFFER);
		if (!ret_desc) {
			return (AE_NO_MEMORY);
		}

		/* Handle both ACPI 1.0 and ACPI 2.0 Integer widths */

		if (walk_state->method_node->flags & ANOBJ_DATA_WIDTH_32) {
			/*
			 * We are running a method that exists in a 32-bit ACPI table.
			 * Truncate the value to 32 bits by zeroing out the upper
			 * 32-bit field
			 */
			integer_size = sizeof (u32);
		}

		/* Need enough space for one integers */

		ret_desc->buffer.length = integer_size;
		new_buf = ACPI_MEM_CALLOCATE (integer_size);
		if (!new_buf) {
			REPORT_ERROR
				(("Ex_convert_to_buffer: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return (AE_NO_MEMORY);
		}

		/* Copy the integer to the buffer */

		for (i = 0; i < integer_size; i++) {
			new_buf[i] = (u8) (obj_desc->integer.value >> (i * 8));
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


	case ACPI_TYPE_STRING:
		*result_desc = obj_desc;
		break;


	case ACPI_TYPE_BUFFER:
		*result_desc = obj_desc;
		break;


	default:
		return (AE_TYPE);
		break;
   }

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_ascii
 *
 * PARAMETERS:  Integer
 *
 * RETURN:      Actual string length
 *
 * DESCRIPTION: Convert an ACPI Integer to a hex string
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
	u8                      hex_digit;
	acpi_integer            digit;
	u32                     remainder;
	u32                     length = sizeof (acpi_integer);
	u8                      leading_zero = TRUE;


	FUNCTION_ENTRY ();


	switch (base) {
	case 10:

		remainder = 0;
		for (i = ACPI_MAX_DECIMAL_DIGITS; i > 0 ; i--) {
			/* Divide by nth factor of 10 */

			digit = integer;
			for (j = 1; j < i; j++) {
				acpi_ut_short_divide (&digit, 10, &digit, &remainder);
			}

			/* Create the decimal digit */

			if (digit != 0) {
				leading_zero = FALSE;
			}

			if (!leading_zero) {
				string[k] = (u8) (ASCII_ZERO + remainder);
				k++;
			}
		}
		break;

	case 16:

		/* Copy the integer to the buffer */

		for (i = 0, j = ((length * 2) -1); i < (length * 2); i++, j--) {

			hex_digit = acpi_ut_hex_to_ascii_char (integer, (j * 4));
			if (hex_digit != ASCII_ZERO) {
				leading_zero = FALSE;
			}

			if (!leading_zero) {
				string[k] = hex_digit;
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
		string [0] = ASCII_ZERO;
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
	u32                     integer_size = sizeof (acpi_integer);
	u8                      *new_buf;
	u8                      *pointer;


	FUNCTION_ENTRY ();


	switch (obj_desc->common.type) {
	case ACPI_TYPE_INTEGER:

		/* Handle both ACPI 1.0 and ACPI 2.0 Integer widths */

		if (walk_state->method_node->flags & ANOBJ_DATA_WIDTH_32) {
			/*
			 * We are running a method that exists in a 32-bit ACPI table.
			 * Truncate the value to 32 bits by zeroing out the upper
			 * 32-bit field
			 */
			integer_size = sizeof (u32);
		}

		string_length = integer_size * 2;
		if (base == 10) {
			string_length = ACPI_MAX_DECIMAL_DIGITS;
		}

		/*
		 * Create a new String
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_STRING);
		if (!ret_desc) {
			return (AE_NO_MEMORY);
		}

		/* Need enough space for one ASCII integer plus null terminator */

		new_buf = ACPI_MEM_CALLOCATE (string_length + 1);
		if (!new_buf) {
			REPORT_ERROR
				(("Ex_convert_to_string: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return (AE_NO_MEMORY);
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
				return (AE_AML_STRING_LIMIT);
			}
		}

		/*
		 * Create a new string object
		 */
		ret_desc = acpi_ut_create_internal_object (ACPI_TYPE_STRING);
		if (!ret_desc) {
			return (AE_NO_MEMORY);
		}

		/* String length is the lesser of the Max or the actual length */

		if (max_length < string_length) {
			string_length = max_length;
		}

		new_buf = ACPI_MEM_CALLOCATE (string_length + 1);
		if (!new_buf) {
			REPORT_ERROR
				(("Ex_convert_to_string: Buffer allocation failure\n"));
			acpi_ut_remove_reference (ret_desc);
			return (AE_NO_MEMORY);
		}

		/*
		 * Convert each byte of the buffer to two ASCII characters plus a space.
		 */
		pointer = obj_desc->buffer.pointer;
		index = 0;
		for (i = 0, index = 0; i < obj_desc->buffer.length; i++) {
			index = acpi_ex_convert_to_ascii (pointer[i], base, &new_buf[index]);

			new_buf[index] = ' ';
			index++;
		}

		/* Null terminate */

		new_buf [index-1] = 0;
		ret_desc->buffer.pointer = new_buf;
		ret_desc->string.length = STRLEN ((char *) new_buf);


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

			return (AE_NOT_IMPLEMENTED);
		}
		break;


	default:
		return (AE_TYPE);
		break;
   }

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_convert_to_target_type
 *
 * PARAMETERS:  *Obj_desc       - Object to be converted.
 *              Walk_state      - Current method state
 *
 * RETURN:      Status
 *
 * DESCRIPTION:
 *
 ******************************************************************************/

acpi_status
acpi_ex_convert_to_target_type (
	acpi_object_type8       destination_type,
	acpi_operand_object     **obj_desc,
	acpi_walk_state         *walk_state)
{
	acpi_status             status = AE_OK;


	FUNCTION_TRACE ("Ex_convert_to_target_type");


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

			if (destination_type != (*obj_desc)->common.type) {
				ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"Target does not allow conversion of type %s to %s\n",
					acpi_ut_get_type_name ((*obj_desc)->common.type),
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
			status = acpi_ex_convert_to_integer (*obj_desc, obj_desc, walk_state);
			break;


		case ACPI_TYPE_STRING:

			/*
			 * The operand must be a String.  We can convert an
			 * Integer or Buffer if necessary
			 */
			status = acpi_ex_convert_to_string (*obj_desc, obj_desc, 16, ACPI_UINT32_MAX, walk_state);
			break;


		case ACPI_TYPE_BUFFER:

			/*
			 * The operand must be a String.  We can convert an
			 * Integer or Buffer if necessary
			 */
			status = acpi_ex_convert_to_buffer (*obj_desc, obj_desc, walk_state);
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


