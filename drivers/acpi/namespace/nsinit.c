/******************************************************************************
 *
 * Module Name: nsinit - namespace initialization
 *              $Revision: 33 $
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
#include "acdispat.h"
#include "acinterp.h"

#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsinit")


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_initialize_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Walk the entire namespace and perform any necessary
 *              initialization on the objects found therein
 *
 ******************************************************************************/

acpi_status
acpi_ns_initialize_objects (
	void)
{
	acpi_status             status;
	acpi_init_walk_info     info;


	FUNCTION_TRACE ("Ns_initialize_objects");


	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"**** Starting initialization of namespace objects ****\n"));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "Completing Region and Field initialization:"));


	info.field_count = 0;
	info.field_init = 0;
	info.op_region_count = 0;
	info.op_region_init = 0;
	info.object_count = 0;


	/* Walk entire namespace from the supplied root */

	status = acpi_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT,
			  ACPI_UINT32_MAX, acpi_ns_init_one_object,
			  &info, NULL);
	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Walk_namespace failed! %x\n", status));
	}

	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK,
		"\n%d/%d Regions, %d/%d Fields initialized (%d nodes total)\n",
		info.op_region_init, info.op_region_count, info.field_init,
		info.field_count, info.object_count));
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%d Control Methods found\n", info.method_count));
	ACPI_DEBUG_PRINT ((ACPI_DB_DISPATCH,
		"%d Op Regions found\n", info.op_region_count));

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_initialize_devices
 *
 * PARAMETERS:  None
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: Walk the entire namespace and initialize all ACPI devices.
 *              This means running _INI on all present devices.
 *
 *              Note: We install PCI config space handler on region access,
 *              not here.
 *
 ******************************************************************************/

acpi_status
acpi_ns_initialize_devices (
	void)
{
	acpi_status             status;
	acpi_device_walk_info   info;


	FUNCTION_TRACE ("Ns_initialize_devices");


	info.device_count = 0;
	info.num_STA = 0;
	info.num_INI = 0;


	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "Executing device _INI methods:"));

	status = acpi_ns_walk_namespace (ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			  ACPI_UINT32_MAX, FALSE, acpi_ns_init_one_device, &info, NULL);

	if (ACPI_FAILURE (status)) {
		ACPI_DEBUG_PRINT ((ACPI_DB_ERROR, "Walk_namespace failed! %x\n", status));
	}


	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK,
		"\n%d Devices found: %d _STA, %d _INI\n",
		info.device_count, info.num_STA, info.num_INI));

	return_ACPI_STATUS (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_init_one_object
 *
 * PARAMETERS:  Obj_handle      - Node
 *              Level           - Current nesting level
 *              Context         - Points to a init info struct
 *              Return_value    - Not used
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Callback from Acpi_walk_namespace. Invoked for every object
 *              within the  namespace.
 *
 *              Currently, the only objects that require initialization are:
 *              1) Methods
 *              2) Op Regions
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_object (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	acpi_object_type8       type;
	acpi_status             status;
	acpi_init_walk_info     *info = (acpi_init_walk_info *) context;
	acpi_namespace_node     *node = (acpi_namespace_node *) obj_handle;
	acpi_operand_object     *obj_desc;


	PROC_NAME ("Ns_init_one_object");


	info->object_count++;


	/* And even then, we are only interested in a few object types */

	type = acpi_ns_get_type (obj_handle);
	obj_desc = node->object;
	if (!obj_desc) {
		return (AE_OK);
	}

	if ((type != ACPI_TYPE_REGION) &&
		(type != ACPI_TYPE_BUFFER_FIELD)) {
		return (AE_OK);
	}


	/*
	 * Must lock the interpreter before executing AML code
	 */
	status = acpi_ex_enter_interpreter ();
	if (ACPI_FAILURE (status)) {
		return (status);
	}

	switch (type) {

	case ACPI_TYPE_REGION:

		info->op_region_count++;
		if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
			break;
		}

		info->op_region_init++;
		status = acpi_ds_get_region_arguments (obj_desc);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ERROR, "\n"));
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"%s while getting region arguments [%4.4s]\n",
					acpi_format_exception (status), (char*)&node->name));
		}

		if (!(acpi_dbg_level & ACPI_LV_INIT)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "."));
		}

		break;


	case ACPI_TYPE_BUFFER_FIELD:

		info->field_count++;
		if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
			break;
		}

		info->field_init++;
		status = acpi_ds_get_buffer_field_arguments (obj_desc);
		if (ACPI_FAILURE (status)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_ERROR, "\n"));
			ACPI_DEBUG_PRINT ((ACPI_DB_ERROR,
					"%s while getting buffer field arguments [%4.4s]\n",
					acpi_format_exception (status), (char*)&node->name));
		}
		if (!(acpi_dbg_level & ACPI_LV_INIT)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "."));
		}


		break;

	default:
		break;
	}


	/*
	 * We ignore errors from above, and always return OK, since
	 * we don't want to abort the walk on a single error.
	 */
	acpi_ex_exit_interpreter ();
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_init_one_device
 *
 * PARAMETERS:  acpi_walk_callback
 *
 * RETURN:      acpi_status
 *
 * DESCRIPTION: This is called once per device soon after ACPI is enabled
 *              to initialize each device. It determines if the device is
 *              present, and if so, calls _INI.
 *
 ******************************************************************************/

acpi_status
acpi_ns_init_one_device (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_status             status;
	acpi_namespace_node    *node;
	u32                     flags;
	acpi_device_walk_info  *info = (acpi_device_walk_info *) context;


	FUNCTION_TRACE ("Ns_init_one_device");


	if (!(acpi_dbg_level & ACPI_LV_INIT)) {
		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_OK, "."));
	}

	info->device_count++;

	acpi_ut_acquire_mutex (ACPI_MTX_NAMESPACE);

	node = acpi_ns_map_handle_to_node (obj_handle);
	if (!node) {
		acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);
		return (AE_BAD_PARAMETER);
	}

	acpi_ut_release_mutex (ACPI_MTX_NAMESPACE);

	/*
	 * Run _STA to determine if we can run _INI on the device.
	 */
	DEBUG_EXEC (acpi_ut_display_init_pathname (node, "_STA [Method]"));
	status = acpi_ut_execute_STA (node, &flags);
	if (ACPI_FAILURE (status)) {
		/* Ignore error and move on to next device */

		return_ACPI_STATUS (AE_OK);
	}

	info->num_STA++;

	if (!(flags & 0x01)) {
		/* don't look at children of a not present device */

		return_ACPI_STATUS(AE_CTRL_DEPTH);
	}


	/*
	 * The device is present. Run _INI.
	 */
	DEBUG_EXEC (acpi_ut_display_init_pathname (obj_handle, "_INI [Method]"));
	status = acpi_ns_evaluate_relative (obj_handle, "_INI", NULL, NULL);
	if (AE_NOT_FOUND == status) {
		/* No _INI means device requires no initialization */

		status = AE_OK;
	}

	else if (ACPI_FAILURE (status)) {
		/* Ignore error and move on to next device */

#ifdef ACPI_DEBUG
		NATIVE_CHAR *scope_name = acpi_ns_get_table_pathname (obj_handle);

		ACPI_DEBUG_PRINT ((ACPI_DB_WARN, "%s._INI failed: %s\n",
				scope_name, acpi_format_exception (status)));

		ACPI_MEM_FREE (scope_name);
#endif
	}

	else {
		/* Count of successful INIs */

		info->num_INI++;
	}

	return_ACPI_STATUS (AE_OK);
}
