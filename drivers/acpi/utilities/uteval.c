/******************************************************************************
 *
 * Module Name: uteval - Object evaluation
 *              $Revision: 31 $
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
#include "acnamesp.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_UTILITIES
	 MODULE_NAME         ("uteval")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_evaluate_numeric_object
 *
 * PARAMETERS:  *Object_name        - Object name to be evaluated
 *              Device_node         - Node for the device
 *              *Address            - Where the value is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: evaluates a numeric namespace object for a selected device
 *              and stores results in *Address.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_numeric_object (
	NATIVE_CHAR             *object_name,
	acpi_namespace_node     *device_node,
	acpi_integer            *address)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	FUNCTION_TRACE ("Ut_evaluate_numeric_object");


	/* Execute the method */

	status = acpi_ns_evaluate_relative (device_node, object_name, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		if (status == AE_NOT_FOUND) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "%s on %4.4s was not found\n",
				object_name, (char*)&device_node->name));
		}
		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "%s on %4.4s failed with status %s\n",
				object_name, (char*)&device_node->name,
				acpi_format_exception (status)));
		}

		return_ACPI_STATUS (status);
	}


	/* Did we get a return object? */

	if (!obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No object was returned from %s\n",
			object_name));
		return_ACPI_STATUS (AE_TYPE);
	}

	/* Is the return object of the correct type? */

	if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
		status = AE_TYPE;
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Type returned from %s was not a number: %X \n",
			object_name, obj_desc->common.type));
	}
	else {
		/*
		 * Since the structure is a union, setting any field will set all
		 * of the variables in the union
		 */
		*address = obj_desc->integer.value;
	}

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference (obj_desc);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_execute_HID
 *
 * PARAMETERS:  Device_node         - Node for the device
 *              *Hid                - Where the HID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _HID control method that returns the hardware
 *              ID of the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_HID (
	acpi_namespace_node     *device_node,
	acpi_device_id          *hid)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	FUNCTION_TRACE ("Ut_execute_HID");


	/* Execute the method */

	status = acpi_ns_evaluate_relative (device_node,
			 METHOD_NAME__HID, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		if (status == AE_NOT_FOUND) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "_HID on %4.4s was not found\n",
				(char*)&device_node->name));
		}

		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "_HID on %4.4s failed %s\n",
				(char*)&device_node->name, acpi_format_exception (status)));
		}

		return_ACPI_STATUS (status);
	}

	/* Did we get a return object? */

	if (!obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No object was returned from _HID\n"));
		return_ACPI_STATUS (AE_TYPE);
	}

	/*
	 *  A _HID can return either a Number (32 bit compressed EISA ID) or
	 *  a string
	 */
	if ((obj_desc->common.type != ACPI_TYPE_INTEGER) &&
		(obj_desc->common.type != ACPI_TYPE_STRING)) {
		status = AE_TYPE;
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Type returned from _HID not a number or string: %s(%X) \n",
			acpi_ut_get_type_name (obj_desc->common.type), obj_desc->common.type));
	}

	else {
		if (obj_desc->common.type == ACPI_TYPE_INTEGER) {
			/* Convert the Numeric HID to string */

			acpi_ex_eisa_id_to_string ((u32) obj_desc->integer.value, hid->buffer);
		}

		else {
			/* Copy the String HID from the returned object */

			STRNCPY(hid->buffer, obj_desc->string.pointer, sizeof(hid->buffer));
		}
	}


	/* On exit, we must delete the return object */

	acpi_ut_remove_reference (obj_desc);

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_execute_UID
 *
 * PARAMETERS:  Device_node         - Node for the device
 *              *Uid                - Where the UID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _UID control method that returns the hardware
 *              ID of the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_UID (
	acpi_namespace_node     *device_node,
	acpi_device_id          *uid)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	PROC_NAME ("Ut_execute_UID");


	/* Execute the method */

	status = acpi_ns_evaluate_relative (device_node,
			 METHOD_NAME__UID, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		if (status == AE_NOT_FOUND) {
			ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "_UID on %4.4s was not found\n",
				(char*)&device_node->name));
		}

		else {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"_UID on %4.4s failed %s\n",
				(char*)&device_node->name, acpi_format_exception (status)));
		}

		return (status);
	}

	/* Did we get a return object? */

	if (!obj_desc) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No object was returned from _UID\n"));
		return (AE_TYPE);
	}

	/*
	 *  A _UID can return either a Number (32 bit compressed EISA ID) or
	 *  a string
	 */
	if ((obj_desc->common.type != ACPI_TYPE_INTEGER) &&
		(obj_desc->common.type != ACPI_TYPE_STRING)) {
		status = AE_TYPE;
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Type returned from _UID was not a number or string: %X \n",
			obj_desc->common.type));
	}

	else {
		if (obj_desc->common.type == ACPI_TYPE_INTEGER) {
			/* Convert the Numeric UID to string */

			acpi_ex_unsigned_integer_to_string (obj_desc->integer.value, uid->buffer);
		}

		else {
			/* Copy the String UID from the returned object */

			STRNCPY(uid->buffer, obj_desc->string.pointer, sizeof(uid->buffer));
		}
	}


	/* On exit, we must delete the return object */

	acpi_ut_remove_reference (obj_desc);

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_execute_STA
 *
 * PARAMETERS:  Device_node         - Node for the device
 *              *Flags              - Where the status flags are returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes _STA for selected device and stores results in
 *              *Flags.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_STA (
	acpi_namespace_node     *device_node,
	u32                     *flags)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	FUNCTION_TRACE ("Ut_execute_STA");


	/* Execute the method */

	status = acpi_ns_evaluate_relative (device_node,
			 METHOD_NAME__STA, NULL, &obj_desc);
	if (AE_NOT_FOUND == status) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO,
			"_STA on %4.4s was not found, assuming present.\n",
			(char*)&device_node->name));

		*flags = 0x0F;
		status = AE_OK;
	}

	else if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "_STA on %4.4s failed %s\n",
			(char*)&device_node->name,
			acpi_format_exception (status)));
	}

	else /* success */ {
		/* Did we get a return object? */

		if (!obj_desc) {
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "No object was returned from _STA\n"));
			return_ACPI_STATUS (AE_TYPE);
		}

		/* Is the return object of the correct type? */

		if (obj_desc->common.type != ACPI_TYPE_INTEGER) {
			status = AE_TYPE;
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
				"Type returned from _STA was not a number: %X \n",
				obj_desc->common.type));
		}

		else {
			/* Extract the status flags */

			*flags = (u32) obj_desc->integer.value;
		}

		/* On exit, we must delete the return object */

		acpi_ut_remove_reference (obj_desc);
	}

	return_ACPI_STATUS (status);
}
