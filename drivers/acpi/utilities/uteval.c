/******************************************************************************
 *
 * Module Name: uteval - Object evaluation
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
#include "acnamesp.h"
#include "acinterp.h"


#define _COMPONENT          ACPI_UTILITIES
	 ACPI_MODULE_NAME    ("uteval")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_evaluate_object
 *
 * PARAMETERS:  Prefix_node         - Starting node
 *              Path                - Path to object from starting node
 *              Expected_return_types - Bitmap of allowed return types
 *              Return_desc         - Where a return value is stored
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Evaluates a namespace object and verifies the type of the
 *              return object.  Common code that simplifies accessing objects
 *              that have required return objects of fixed types.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_object (
	acpi_namespace_node     *prefix_node,
	char                    *path,
	u32                     expected_return_btypes,
	acpi_operand_object     **return_desc)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;
	u32                     return_btype;


	ACPI_FUNCTION_TRACE ("Ut_evaluate_object");


	/* Evaluate the object/method */

	status = acpi_ns_evaluate_relative (prefix_node, path, NULL, &obj_desc);
	if (ACPI_FAILURE (status)) {
		if (status == AE_NOT_FOUND) {
			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC, "[%4.4s.%s] was not found\n",
				prefix_node->name.ascii, path));
		}
		else {
			ACPI_REPORT_METHOD_ERROR ("Method execution failed",
				prefix_node, path, status);
		}

		return_ACPI_STATUS (status);
	}

	/* Did we get a return object? */

	if (!obj_desc) {
		if (expected_return_btypes) {
			ACPI_REPORT_METHOD_ERROR ("No object was returned from",
				prefix_node, path, AE_NOT_EXIST);

			return_ACPI_STATUS (AE_NOT_EXIST);
		}

		return_ACPI_STATUS (AE_OK);
	}

	/* Map the return object type to the bitmapped type */

	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_INTEGER:
		return_btype = ACPI_BTYPE_INTEGER;
		break;

	case ACPI_TYPE_BUFFER:
		return_btype = ACPI_BTYPE_BUFFER;
		break;

	case ACPI_TYPE_STRING:
		return_btype = ACPI_BTYPE_STRING;
		break;

	case ACPI_TYPE_PACKAGE:
		return_btype = ACPI_BTYPE_PACKAGE;
		break;

	default:
		return_btype = 0;
		break;
	}

	/* Is the return object one of the expected types? */

	if (!(expected_return_btypes & return_btype)) {
		ACPI_REPORT_METHOD_ERROR ("Return object type is incorrect",
			prefix_node, path, AE_TYPE);

		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
			"Type returned from %s was incorrect: %X\n",
			path, ACPI_GET_OBJECT_TYPE (obj_desc)));

		/* On error exit, we must delete the return object */

		acpi_ut_remove_reference (obj_desc);
		return_ACPI_STATUS (AE_TYPE);
	}

	/* Object type is OK, return it */

	*return_desc = obj_desc;
	return_ACPI_STATUS (AE_OK);
}


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
 * DESCRIPTION: Evaluates a numeric namespace object for a selected device
 *              and stores result in *Address.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_evaluate_numeric_object (
	char                    *object_name,
	acpi_namespace_node     *device_node,
	acpi_integer            *address)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ut_evaluate_numeric_object");


	status = acpi_ut_evaluate_object (device_node, object_name,
			 ACPI_BTYPE_INTEGER, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/* Get the returned Integer */

	*address = obj_desc->integer.value;

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


	ACPI_FUNCTION_TRACE ("Ut_execute_HID");


	status = acpi_ut_evaluate_object (device_node, METHOD_NAME__HID,
			 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_INTEGER) {
		/* Convert the Numeric HID to string */

		acpi_ex_eisa_id_to_string ((u32) obj_desc->integer.value, hid->buffer);
	}
	else {
		/* Copy the String HID from the returned object */

		ACPI_STRNCPY (hid->buffer, obj_desc->string.pointer, sizeof(hid->buffer));
	}

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ut_execute_CID
 *
 * PARAMETERS:  Device_node         - Node for the device
 *              *Cid                - Where the CID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Executes the _CID control method that returns one or more
 *              compatible hardware IDs for the device.
 *
 *              NOTE: Internal function, no parameter validation
 *
 ******************************************************************************/

acpi_status
acpi_ut_execute_CID (
	acpi_namespace_node     *device_node,
	acpi_device_id          *cid)
{
	acpi_operand_object     *obj_desc;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ut_execute_CID");


	status = acpi_ut_evaluate_object (device_node, METHOD_NAME__CID,
			 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING | ACPI_BTYPE_PACKAGE, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	/*
	 *  A _CID can return either a single compatible ID or a package of compatible
	 *  IDs.  Each compatible ID can be a Number (32 bit compressed EISA ID) or
	 *  string (PCI ID format, e.g. "PCI\VEN_vvvv&DEV_dddd&SUBSYS_ssssssss").
	 */
	switch (ACPI_GET_OBJECT_TYPE (obj_desc)) {
	case ACPI_TYPE_INTEGER:

		/* Convert the Numeric CID to string */

		acpi_ex_eisa_id_to_string ((u32) obj_desc->integer.value, cid->buffer);
		break;

	case ACPI_TYPE_STRING:

		/* Copy the String CID from the returned object */

		ACPI_STRNCPY (cid->buffer, obj_desc->string.pointer, sizeof (cid->buffer));
		break;

	case ACPI_TYPE_PACKAGE:

		/* TBD: Parse package elements; need different return struct, etc. */

		status = AE_SUPPORT;
		break;

	default:

		status = AE_TYPE;
		break;
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


	ACPI_FUNCTION_TRACE ("Ut_execute_UID");


	status = acpi_ut_evaluate_object (device_node, METHOD_NAME__UID,
			 ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING, &obj_desc);
	if (ACPI_FAILURE (status)) {
		return_ACPI_STATUS (status);
	}

	if (ACPI_GET_OBJECT_TYPE (obj_desc) == ACPI_TYPE_INTEGER) {
		/* Convert the Numeric UID to string */

		acpi_ex_unsigned_integer_to_string (obj_desc->integer.value, uid->buffer);
	}
	else {
		/* Copy the String UID from the returned object */

		ACPI_STRNCPY (uid->buffer, obj_desc->string.pointer, sizeof (uid->buffer));
	}

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
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


	ACPI_FUNCTION_TRACE ("Ut_execute_STA");


	status = acpi_ut_evaluate_object (device_node, METHOD_NAME__STA,
			 ACPI_BTYPE_INTEGER, &obj_desc);
	if (ACPI_FAILURE (status)) {
		if (AE_NOT_FOUND == status) {
			ACPI_DEBUG_PRINT ((ACPI_DB_EXEC,
				"_STA on %4.4s was not found, assuming device is present\n",
				device_node->name.ascii));

			*flags = 0x0F;
			status = AE_OK;
		}

		return_ACPI_STATUS (status);
	}

	/* Extract the status flags */

	*flags = (u32) obj_desc->integer.value;

	/* On exit, we must delete the return object */

	acpi_ut_remove_reference (obj_desc);
	return_ACPI_STATUS (status);
}
