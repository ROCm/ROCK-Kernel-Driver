
/******************************************************************************
 *
 * Module Name: exutils - interpreter/scanner utilities
 *              $Revision: 85 $
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


/*
 * DEFINE_AML_GLOBALS is tested in amlcode.h
 * to determine whether certain global names should be "defined" or only
 * "declared" in the current compilation.  This enhances maintainability
 * by enabling a single header file to embody all knowledge of the names
 * in question.
 *
 * Exactly one module of any executable should #define DEFINE_GLOBALS
 * before #including the header files which use this convention.  The
 * names in question will be defined and initialized in that module,
 * and declared as extern in all other modules which #include those
 * header files.
 */

#define DEFINE_AML_GLOBALS

#include "acpi.h"
#include "acparser.h"
#include "acinterp.h"
#include "amlcode.h"
#include "acnamesp.h"
#include "acevents.h"
#include "acparser.h"

#define _COMPONENT          ACPI_EXECUTER
	 MODULE_NAME         ("exutils")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_enter_interpreter
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Enter the interpreter execution region
 *              TBD: should be a macro
 *
 ******************************************************************************/

acpi_status
acpi_ex_enter_interpreter (void)
{
	acpi_status             status;

	FUNCTION_TRACE ("Ex_enter_interpreter");


	status = acpi_ut_acquire_mutex (ACPI_MTX_EXECUTE);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_exit_interpreter
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Exit the interpreter execution region
 *
 * Cases where the interpreter is unlocked:
 *      1) Completion of the execution of a control method
 *      2) Method blocked on a Sleep() AML opcode
 *      3) Method blocked on an Acquire() AML opcode
 *      4) Method blocked on a Wait() AML opcode
 *      5) Method blocked to acquire the global lock
 *      6) Method blocked to execute a serialized control method that is
 *          already executing
 *      7) About to invoke a user-installed opregion handler
 *
 *              TBD: should be a macro
 *
 ******************************************************************************/

void
acpi_ex_exit_interpreter (void)
{
	FUNCTION_TRACE ("Ex_exit_interpreter");


	acpi_ut_release_mutex (ACPI_MTX_EXECUTE);

	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_validate_object_type
 *
 * PARAMETERS:  Type            Object type to validate
 *
 * DESCRIPTION: Determine if a type is a valid ACPI object type
 *
 ******************************************************************************/

u8
acpi_ex_validate_object_type (
	acpi_object_type        type)
{

	FUNCTION_ENTRY ();


	if ((type > ACPI_TYPE_MAX && type < INTERNAL_TYPE_BEGIN) ||
		(type > INTERNAL_TYPE_MAX)) {
		return (FALSE);
	}

	return (TRUE);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_truncate_for32bit_table
 *
 * PARAMETERS:  Obj_desc        - Object to be truncated
 *              Walk_state      - Current walk state
 *                                (A method must be executing)
 *
 * RETURN:      none
 *
 * DESCRIPTION: Truncate a number to 32-bits if the currently executing method
 *              belongs to a 32-bit ACPI table.
 *
 ******************************************************************************/

void
acpi_ex_truncate_for32bit_table (
	acpi_operand_object     *obj_desc,
	acpi_walk_state         *walk_state)
{

	FUNCTION_ENTRY ();


	/*
	 * Object must be a valid number and we must be executing
	 * a control method
	 */
	if ((!obj_desc) ||
		(obj_desc->common.type != ACPI_TYPE_INTEGER) ||
		(!walk_state->method_node)) {
		return;
	}

	if (walk_state->method_node->flags & ANOBJ_DATA_WIDTH_32) {
		/*
		 * We are running a method that exists in a 32-bit ACPI table.
		 * Truncate the value to 32 bits by zeroing out the upper 32-bit field
		 */
		obj_desc->integer.value &= (acpi_integer) ACPI_UINT32_MAX;
	}
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_acquire_global_lock
 *
 * PARAMETERS:  Rule            - Lock rule: Always_lock, Never_lock
 *
 * RETURN:      TRUE/FALSE indicating whether the lock was actually acquired
 *
 * DESCRIPTION: Obtain the global lock and keep track of this fact via two
 *              methods.  A global variable keeps the state of the lock, and
 *              the state is returned to the caller.
 *
 ******************************************************************************/

u8
acpi_ex_acquire_global_lock (
	u32                     rule)
{
	u8                      locked = FALSE;
	acpi_status             status;


	FUNCTION_TRACE ("Ex_acquire_global_lock");


	/* Only attempt lock if the Rule says so */

	if (rule == (u32) GLOCK_ALWAYS_LOCK) {
		/* We should attempt to get the lock */

		status = acpi_ev_acquire_global_lock ();
		if (ACPI_SUCCESS (status)) {
			locked = TRUE;
		}

		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Could not acquire Global Lock, %s\n",
				acpi_format_exception (status)));
		}
	}

	return_VALUE (locked);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_release_global_lock
 *
 * PARAMETERS:  Locked_by_me    - Return value from corresponding call to
 *                                Acquire_global_lock.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Release the global lock if it is locked.
 *
 ******************************************************************************/

acpi_status
acpi_ex_release_global_lock (
	u8                      locked_by_me)
{

	FUNCTION_TRACE ("Ex_release_global_lock");


	/* Only attempt unlock if the caller locked it */

	if (locked_by_me) {
		/* OK, now release the lock */

		acpi_ev_release_global_lock ();
	}


	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_digits_needed
 *
 * PARAMETERS:  Value           - Value to be represented
 *              Base            - Base of representation
 *
 * RETURN:      the number of digits needed to represent Value in Base
 *
 ******************************************************************************/

u32
acpi_ex_digits_needed (
	acpi_integer            value,
	u32                     base)
{
	u32                     num_digits = 0;


	FUNCTION_TRACE ("Ex_digits_needed");


	if (base < 1) {
		REPORT_ERROR (("Ex_digits_needed: Internal error - Invalid base\n"));
	}

	else {
		/*
		 * acpi_integer is unsigned, which is why we don't worry about a '-'
		 */
		for (num_digits = 1;
			(acpi_ut_short_divide (&value, base, &value, NULL));
			++num_digits) { ; }
	}

	return_VALUE (num_digits);
}


/*******************************************************************************
 *
 * FUNCTION:    ntohl
 *
 * PARAMETERS:  Value           - Value to be converted
 *
 * DESCRIPTION: Convert a 32-bit value to big-endian (swap the bytes)
 *
 ******************************************************************************/

static u32
_ntohl (
	u32                     value)
{
	union {
		u32                 value;
		u8                  bytes[4];
	} out;

	union {
		u32                 value;
		u8                  bytes[4];
	} in;


	FUNCTION_ENTRY ();


	in.value = value;

	out.bytes[0] = in.bytes[3];
	out.bytes[1] = in.bytes[2];
	out.bytes[2] = in.bytes[1];
	out.bytes[3] = in.bytes[0];

	return (out.value);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_eisa_id_to_string
 *
 * PARAMETERS:  Numeric_id      - EISA ID to be converted
 *              Out_string      - Where to put the converted string (8 bytes)
 *
 * DESCRIPTION: Convert a numeric EISA ID to string representation
 *
 ******************************************************************************/

acpi_status
acpi_ex_eisa_id_to_string (
	u32                     numeric_id,
	NATIVE_CHAR             *out_string)
{
	u32                     id;


	FUNCTION_ENTRY ();


	/* swap to big-endian to get contiguous bits */

	id = _ntohl (numeric_id);

	out_string[0] = (char) ('@' + ((id >> 26) & 0x1f));
	out_string[1] = (char) ('@' + ((id >> 21) & 0x1f));
	out_string[2] = (char) ('@' + ((id >> 16) & 0x1f));
	out_string[3] = acpi_ut_hex_to_ascii_char (id, 12);
	out_string[4] = acpi_ut_hex_to_ascii_char (id, 8);
	out_string[5] = acpi_ut_hex_to_ascii_char (id, 4);
	out_string[6] = acpi_ut_hex_to_ascii_char (id, 0);
	out_string[7] = 0;

	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ex_unsigned_integer_to_string
 *
 * PARAMETERS:  Value           - Value to be converted
 *              Out_string      - Where to put the converted string (8 bytes)
 *
 * RETURN:      Convert a number to string representation
 *
 ******************************************************************************/

acpi_status
acpi_ex_unsigned_integer_to_string (
	acpi_integer            value,
	NATIVE_CHAR             *out_string)
{
	u32                     count;
	u32                     digits_needed;
	u32                     remainder;


	FUNCTION_ENTRY ();


	digits_needed = acpi_ex_digits_needed (value, 10);
	out_string[digits_needed] = 0;

	for (count = digits_needed; count > 0; count--) {
		acpi_ut_short_divide (&value, 10, &value, &remainder);
		out_string[count-1] = (NATIVE_CHAR) ('0' + remainder);
	}

	return (AE_OK);
}


