
/******************************************************************************
 *
 * Module Name: exnames - interpreter/scanner name load/execute
 *              $Revision: 93 $
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
	 ACPI_MODULE_NAME    ("exnames")


/* AML Package Length encodings */

#define ACPI_AML_PACKAGE_TYPE1   0x40
#define ACPI_AML_PACKAGE_TYPE2   0x4000
#define ACPI_AML_PACKAGE_TYPE3   0x400000
#define ACPI_AML_PACKAGE_TYPE4   0x40000000


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_allocate_name_string
 *
 * PARAMETERS:  Prefix_count        - Count of parent levels. Special cases:
 *                                    (-1) = root,  0 = none
 *              Num_name_segs       - count of 4-character name segments
 *
 * RETURN:      A pointer to the allocated string segment.  This segment must
 *              be deleted by the caller.
 *
 * DESCRIPTION: Allocate a buffer for a name string. Ensure allocated name
 *              string is long enough, and set up prefix if any.
 *
 ******************************************************************************/

char *
acpi_ex_allocate_name_string (
	u32                     prefix_count,
	u32                     num_name_segs)
{
	char                    *temp_ptr;
	char                    *name_string;
	u32                      size_needed;

	ACPI_FUNCTION_TRACE ("Ex_allocate_name_string");


	/*
	 * Allow room for all \ and ^ prefixes, all segments, and a Multi_name_prefix.
	 * Also, one byte for the null terminator.
	 * This may actually be somewhat longer than needed.
	 */
	if (prefix_count == ACPI_UINT32_MAX) {
		/* Special case for root */

		size_needed = 1 + (ACPI_NAME_SIZE * num_name_segs) + 2 + 1;
	}
	else {
		size_needed = prefix_count + (ACPI_NAME_SIZE * num_name_segs) + 2 + 1;
	}

	/*
	 * Allocate a buffer for the name.
	 * This buffer must be deleted by the caller!
	 */
	name_string = ACPI_MEM_ALLOCATE (size_needed);
	if (!name_string) {
		ACPI_REPORT_ERROR (("Ex_allocate_name_string: Could not allocate size %d\n", size_needed));
		return_PTR (NULL);
	}

	temp_ptr = name_string;

	/* Set up Root or Parent prefixes if needed */

	if (prefix_count == ACPI_UINT32_MAX) {
		*temp_ptr++ = AML_ROOT_PREFIX;
	}
	else {
		while (prefix_count--) {
			*temp_ptr++ = AML_PARENT_PREFIX;
		}
	}


	/* Set up Dual or Multi prefixes if needed */

	if (num_name_segs > 2) {
		/* Set up multi prefixes   */

		*temp_ptr++ = AML_MULTI_NAME_PREFIX_OP;
		*temp_ptr++ = (char) num_name_segs;
	}
	else if (2 == num_name_segs) {
		/* Set up dual prefixes */

		*temp_ptr++ = AML_DUAL_NAME_PREFIX;
	}

	/*
	 * Terminate string following prefixes. Acpi_ex_name_segment() will
	 * append the segment(s)
	 */
	*temp_ptr = 0;

	return_PTR (name_string);
}

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_name_segment
 *
 * PARAMETERS:  Interpreter_mode    - Current running mode (load1/Load2/Exec)
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Execute a name segment (4 bytes)
 *
 ******************************************************************************/

acpi_status
acpi_ex_name_segment (
	u8                      **in_aml_address,
	char                    *name_string)
{
	char                    *aml_address = (void *) *in_aml_address;
	acpi_status             status = AE_OK;
	u32                     index;
	char                    char_buf[5];


	ACPI_FUNCTION_TRACE ("Ex_name_segment");


	/*
	 * If first character is a digit, then we know that we aren't looking at a
	 * valid name segment
	 */
	char_buf[0] = *aml_address;

	if ('0' <= char_buf[0] && char_buf[0] <= '9') {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "leading digit: %c\n", char_buf[0]));
		return_ACPI_STATUS (AE_CTRL_PENDING);
	}

	ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Bytes from stream:\n"));

	for (index = 0;
		(index < ACPI_NAME_SIZE) && (acpi_ut_valid_acpi_character (*aml_address));
		index++) {
		char_buf[index] = *aml_address++;
		ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "%c\n", char_buf[index]));
	}


	/* Valid name segment  */

	if (index == 4) {
		/* Found 4 valid characters */

		char_buf[4] = '\0';

		if (name_string) {
			ACPI_STRCAT (name_string, char_buf);
			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"Appended to - %s \n", name_string));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_NAMES,
				"No Name string - %s \n", char_buf));
		}
	}
	else if (index == 0) {
		/*
		 * First character was not a valid name character,
		 * so we are looking at something other than a name.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"Leading character is not alpha: %02Xh (not a name)\n",
			char_buf[0]));
		status = AE_CTRL_PENDING;
	}
	else {
		/* Segment started with one or more valid characters, but fewer than 4 */

		status = AE_AML_BAD_NAME;
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Bad character %02x in name, at %p\n",
			*aml_address, aml_address));
	}

	*in_aml_address = (u8 *) aml_address;
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_get_name_string
 *
 * PARAMETERS:  Data_type           - Data type to be associated with this name
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Get a name, including any prefixes.
 *
 ******************************************************************************/

acpi_status
acpi_ex_get_name_string (
	acpi_object_type        data_type,
	u8                      *in_aml_address,
	char                    **out_name_string,
	u32                     *out_name_length)
{
	acpi_status             status = AE_OK;
	u8                      *aml_address = in_aml_address;
	char                    *name_string = NULL;
	u32                     num_segments;
	u32                     prefix_count = 0;
	u8                      has_prefix = FALSE;


	ACPI_FUNCTION_TRACE_PTR ("Ex_get_name_string", aml_address);


	if (ACPI_TYPE_LOCAL_REGION_FIELD == data_type  ||
		ACPI_TYPE_LOCAL_BANK_FIELD == data_type    ||
		ACPI_TYPE_LOCAL_INDEX_FIELD == data_type) {
		/* Disallow prefixes for types associated with Field_unit names */

		name_string = acpi_ex_allocate_name_string (0, 1);
		if (!name_string) {
			status = AE_NO_MEMORY;
		}
		else {
			status = acpi_ex_name_segment (&aml_address, name_string);
		}
	}
	else {
		/*
		 * Data_type is not a field name.
		 * Examine first character of name for root or parent prefix operators
		 */
		switch (*aml_address) {
		case AML_ROOT_PREFIX:

			ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Root_prefix(\\) at %p\n", aml_address));

			/*
			 * Remember that we have a Root_prefix --
			 * see comment in Acpi_ex_allocate_name_string()
			 */
			aml_address++;
			prefix_count = ACPI_UINT32_MAX;
			has_prefix = TRUE;
			break;


		case AML_PARENT_PREFIX:

			/* Increment past possibly multiple parent prefixes */

			do {
				ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Parent_prefix (^) at %p\n", aml_address));

				aml_address++;
				prefix_count++;

			} while (*aml_address == AML_PARENT_PREFIX);

			has_prefix = TRUE;
			break;


		default:

			/* Not a prefix character */

			break;
		}


		/* Examine first character of name for name segment prefix operator */

		switch (*aml_address) {
		case AML_DUAL_NAME_PREFIX:

			ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Dual_name_prefix at %p\n", aml_address));

			aml_address++;
			name_string = acpi_ex_allocate_name_string (prefix_count, 2);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			/* Indicate that we processed a prefix */

			has_prefix = TRUE;

			status = acpi_ex_name_segment (&aml_address, name_string);
			if (ACPI_SUCCESS (status)) {
				status = acpi_ex_name_segment (&aml_address, name_string);
			}
			break;


		case AML_MULTI_NAME_PREFIX_OP:

			ACPI_DEBUG_PRINT ((ACPI_DB_LOAD, "Multi_name_prefix at %p\n", aml_address));

			/* Fetch count of segments remaining in name path */

			aml_address++;
			num_segments = *aml_address;

			name_string = acpi_ex_allocate_name_string (prefix_count, num_segments);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			/* Indicate that we processed a prefix */

			aml_address++;
			has_prefix = TRUE;

			while (num_segments &&
					(status = acpi_ex_name_segment (&aml_address, name_string)) == AE_OK) {
				num_segments--;
			}

			break;


		case 0:

			/* Null_name valid as of 8-12-98 ASL/AML Grammar Update */

			if (prefix_count == ACPI_UINT32_MAX) {
				ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "Name_seg is \"\\\" followed by NULL\n"));
			}

			/* Consume the NULL byte */

			aml_address++;
			name_string = acpi_ex_allocate_name_string (prefix_count, 0);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			break;


		default:

			/* Name segment string */

			name_string = acpi_ex_allocate_name_string (prefix_count, 1);
			if (!name_string) {
				status = AE_NO_MEMORY;
				break;
			}

			status = acpi_ex_name_segment (&aml_address, name_string);
			break;
		}
	}

	if (AE_CTRL_PENDING == status && has_prefix) {
		/* Ran out of segments after processing a prefix */

		ACPI_REPORT_ERROR (
			("Ex_do_name: Malformed Name at %p\n", name_string));
		status = AE_AML_BAD_NAME;
	}

	*out_name_string = name_string;
	*out_name_length = (u32) (aml_address - in_aml_address);

	return_ACPI_STATUS (status);
}


