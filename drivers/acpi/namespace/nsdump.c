/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
 *              $Revision: 99 $
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
#include "acinterp.h"
#include "acnamesp.h"
#include "actables.h"


#define _COMPONENT          ACPI_NAMESPACE
	 MODULE_NAME         ("nsdump")


#if defined(ACPI_DEBUG) || defined(ENABLE_DEBUGGER)

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_pathname
 *
 * PARAMETERS:  Handle              - Object
 *              Msg                 - Prefix message
 *              Level               - Desired debug level
 *              Component           - Caller's component ID
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *              Manages allocation/freeing of a pathname buffer
 *
 ******************************************************************************/

acpi_status
acpi_ns_dump_pathname (
	acpi_handle             handle,
	NATIVE_CHAR             *msg,
	u32                     level,
	u32                     component)
{
	NATIVE_CHAR             *buffer;
	u32                     length;


	FUNCTION_TRACE ("Ns_dump_pathname");


	/* Do this only if the requested debug level and component are enabled */

	if (!(acpi_dbg_level & level) || !(acpi_dbg_layer & component)) {
		return_ACPI_STATUS (AE_OK);
	}

	buffer = ACPI_MEM_ALLOCATE (PATHNAME_MAX);
	if (!buffer) {
		return_ACPI_STATUS (AE_NO_MEMORY);
	}

	/* Convert handle to a full pathname and print it (with supplied message) */

	length = PATHNAME_MAX;
	if (ACPI_SUCCESS (acpi_ns_handle_to_pathname (handle, &length, buffer))) {
		acpi_os_printf ("%s %s (%p)\n", msg, buffer, handle);
	}

	ACPI_MEM_FREE (buffer);

	return_ACPI_STATUS (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_one_object
 *
 * PARAMETERS:  Handle              - Node to be dumped
 *              Level               - Nesting level of the handle
 *              Context             - Passed into Walk_namespace
 *
 * DESCRIPTION: Dump a single Node
 *              This procedure is a User_function called by Acpi_ns_walk_namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ns_dump_one_object (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	ACPI_WALK_INFO          *info = (ACPI_WALK_INFO *) context;
	acpi_namespace_node     *this_node;
	u8                      *value;
	acpi_operand_object     *obj_desc = NULL;
	acpi_object_type8       obj_type;
	acpi_object_type8       type;
	u32                     bytes_to_dump;
	u32                     downstream_sibling_mask = 0;
	u32                     level_tmp;
	u32                     which_bit;


	PROC_NAME ("Ns_dump_one_object");


	this_node = acpi_ns_convert_handle_to_entry (obj_handle);

	level_tmp   = level;
	type        = this_node->type;
	which_bit   = 1;


	if (!(acpi_dbg_level & info->debug_level)) {
		return (AE_OK);
	}

	if (!obj_handle) {
		ACPI_DEBUG_PRINT ((ACPI_DB_INFO, "Null object handle\n"));
		return (AE_OK);
	}

	/* Check if the owner matches */

	if ((info->owner_id != ACPI_UINT32_MAX) &&
		(info->owner_id != this_node->owner_id)) {
		return (AE_OK);
	}


	/* Indent the object according to the level */

	while (level_tmp--) {

		/* Print appropriate characters to form tree structure */

		if (level_tmp) {
			if (downstream_sibling_mask & which_bit) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "|"));
			}

			else {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " "));
			}

			which_bit <<= 1;
		}

		else {
			if (acpi_ns_exist_downstream_sibling (this_node + 1)) {
				downstream_sibling_mask |= (1 << (level - 1));
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "+"));
			}

			else {
				downstream_sibling_mask &= ACPI_UINT32_MAX ^ (1 << (level - 1));
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "+"));
			}

			if (this_node->child == NULL) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "-"));
			}

			else if (acpi_ns_exist_downstream_sibling (this_node->child)) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "+"));
			}

			else {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "-"));
			}
		}
	}


	/* Check the integrity of our data */

	if (type > INTERNAL_TYPE_MAX) {
		type = INTERNAL_TYPE_DEF_ANY;                                /* prints as *ERROR* */
	}

	if (!acpi_ut_valid_acpi_name (this_node->name)) {
		REPORT_WARNING (("Invalid ACPI Name %08X\n", this_node->name));
	}

	/*
	 * Now we can print out the pertinent information
	 */
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " %4.4s %-9s ", &this_node->name, acpi_ut_get_type_name (type)));
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "%p S:%p O:%p",  this_node, this_node->child, this_node->object));


	if (!this_node->object) {
		/* No attached object, we are done */

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
		return (AE_OK);
	}

	switch (type) {

	case ACPI_TYPE_METHOD:

		/* Name is a Method and its AML offset/length are set */

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " M:%p-%X\n",
				 ((acpi_operand_object  *) this_node->object)->method.pcode,
				 ((acpi_operand_object  *) this_node->object)->method.pcode_length));

		break;


	case ACPI_TYPE_INTEGER:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " N:%X\n",
				 ((acpi_operand_object  *) this_node->object)->integer.value));
		break;


	case ACPI_TYPE_STRING:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " S:%p-%X\n",
				 ((acpi_operand_object  *) this_node->object)->string.pointer,
				 ((acpi_operand_object  *) this_node->object)->string.length));
		break;


	case ACPI_TYPE_BUFFER:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " B:%p-%X\n",
				 ((acpi_operand_object  *) this_node->object)->buffer.pointer,
				 ((acpi_operand_object  *) this_node->object)->buffer.length));
		break;


	default:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
		break;
	}

	/* If debug turned off, done */

	if (!(acpi_dbg_level & ACPI_LV_VALUES)) {
		return (AE_OK);
	}


	/* If there is an attached object, display it */

	value = this_node->object;

	/* Dump attached objects */

	while (value) {
		obj_type = INTERNAL_TYPE_INVALID;

		/* Decode the type of attached object and dump the contents */

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "        Attached Object %p: ", value));

		if (acpi_tb_system_table_pointer (value)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to AML Code)\n"));
			bytes_to_dump = 16;
		}

		else if (VALID_DESCRIPTOR_TYPE (value, ACPI_DESC_TYPE_NAMED)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to Node)\n"));
			bytes_to_dump = sizeof (acpi_namespace_node);
		}


		else if (VALID_DESCRIPTOR_TYPE (value, ACPI_DESC_TYPE_INTERNAL)) {
			obj_desc = (acpi_operand_object *) value;
			obj_type = obj_desc->common.type;

			if (obj_type > INTERNAL_TYPE_MAX) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to ACPI Object type %X [UNKNOWN])\n", obj_type));
				bytes_to_dump = 32;
			}

			else {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to ACPI Object type %X [%s])\n",
						   obj_type, acpi_ut_get_type_name (obj_type)));
				bytes_to_dump = sizeof (acpi_operand_object);
			}
		}

		else {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(String or Buffer - not descriptor)\n", value));
			bytes_to_dump = 16;
		}

		DUMP_BUFFER (value, bytes_to_dump);

		/* If value is NOT an internal object, we are done */

		if ((acpi_tb_system_table_pointer (value)) ||
			(VALID_DESCRIPTOR_TYPE (value, ACPI_DESC_TYPE_NAMED))) {
			goto cleanup;
		}

		/*
		 * Valid object, get the pointer to next level, if any
		 */
		switch (obj_type) {
		case ACPI_TYPE_STRING:
			value = (u8 *) obj_desc->string.pointer;
			break;

		case ACPI_TYPE_BUFFER:
			value = (u8 *) obj_desc->buffer.pointer;
			break;

		case ACPI_TYPE_BUFFER_FIELD:
			value = (u8 *) obj_desc->buffer_field.buffer_obj;
			break;

		case ACPI_TYPE_PACKAGE:
			value = (u8 *) obj_desc->package.elements;
			break;

		case ACPI_TYPE_METHOD:
			value = (u8 *) obj_desc->method.pcode;
			break;

		case INTERNAL_TYPE_REGION_FIELD:
			value = (u8 *) obj_desc->field.region_obj;
			break;

		case INTERNAL_TYPE_BANK_FIELD:
			value = (u8 *) obj_desc->bank_field.region_obj;
			break;

		case INTERNAL_TYPE_INDEX_FIELD:
			value = (u8 *) obj_desc->index_field.index_obj;
			break;

	   default:
			goto cleanup;
		}

		obj_type = INTERNAL_TYPE_INVALID;    /* Terminate loop after next pass */
	}

cleanup:
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
	return (AE_OK);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_objects
 *
 * PARAMETERS:  Type                - Object type to be dumped
 *              Max_depth           - Maximum depth of dump.  Use ACPI_UINT32_MAX
 *                                    for an effectively unlimited depth.
 *              Owner_id            - Dump only objects owned by this ID.  Use
 *                                    ACPI_UINT32_MAX to match all owners.
 *              Start_handle        - Where in namespace to start/end search
 *
 * DESCRIPTION: Dump typed objects within the loaded namespace.
 *              Uses Acpi_ns_walk_namespace in conjunction with Acpi_ns_dump_one_object.
 *
 ******************************************************************************/

void
acpi_ns_dump_objects (
	acpi_object_type8       type,
	u32                     max_depth,
	u32                     owner_id,
	acpi_handle             start_handle)
{
	ACPI_WALK_INFO          info;


	FUNCTION_ENTRY ();


	info.debug_level = ACPI_LV_TABLES;
	info.owner_id = owner_id;

	acpi_ns_walk_namespace (type, start_handle, max_depth, NS_WALK_NO_UNLOCK, acpi_ns_dump_one_object,
			   (void *) &info, NULL);
}


#ifndef _ACPI_ASL_COMPILER
/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_one_device
 *
 * PARAMETERS:  Handle              - Node to be dumped
 *              Level               - Nesting level of the handle
 *              Context             - Passed into Walk_namespace
 *
 * DESCRIPTION: Dump a single Node that represents a device
 *              This procedure is a User_function called by Acpi_ns_walk_namespace.
 *
 ******************************************************************************/

acpi_status
acpi_ns_dump_one_device (
	acpi_handle             obj_handle,
	u32                     level,
	void                    *context,
	void                    **return_value)
{
	acpi_device_info        info;
	acpi_status             status;
	u32                     i;


	PROC_NAME ("Ns_dump_one_device");


	status = acpi_ns_dump_one_object (obj_handle, level, context, return_value);

	status = acpi_get_object_info (obj_handle, &info);
	if (ACPI_SUCCESS (status)) {
		for (i = 0; i < level; i++) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " "));
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "    HID: %.8X, ADR: %.8X, Status: %x\n",
				  info.hardware_id, info.address, info.current_status));
	}

	return (status);
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_root_devices
 *
 * PARAMETERS:  None
 *
 * DESCRIPTION: Dump all objects of type "device"
 *
 ******************************************************************************/

void
acpi_ns_dump_root_devices (void)
{
	acpi_handle             sys_bus_handle;


	PROC_NAME ("Ns_dump_root_devices");


	/* Only dump the table if tracing is enabled */

	if (!(ACPI_LV_TABLES & acpi_dbg_level)) {
		return;
	}

	acpi_get_handle (0, NS_SYSTEM_BUS, &sys_bus_handle);

	ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "Display of all devices in the namespace:\n"));
	acpi_ns_walk_namespace (ACPI_TYPE_DEVICE, sys_bus_handle, ACPI_UINT32_MAX, NS_WALK_NO_UNLOCK,
			   acpi_ns_dump_one_device, NULL, NULL);
}

#endif

/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_tables
 *
 * PARAMETERS:  Search_base         - Root of subtree to be dumped, or
 *                                    NS_ALL to dump the entire namespace
 *              Max_depth           - Maximum depth of dump.  Use INT_MAX
 *                                    for an effectively unlimited depth.
 *
 * DESCRIPTION: Dump the name space, or a portion of it.
 *
 ******************************************************************************/

void
acpi_ns_dump_tables (
	acpi_handle             search_base,
	u32                     max_depth)
{
	acpi_handle             search_handle = search_base;


	FUNCTION_TRACE ("Ns_dump_tables");


	if (!acpi_gbl_root_node) {
		/*
		 * If the name space has not been initialized,
		 * there is nothing to dump.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "name space not initialized!\n"));
		return_VOID;
	}

	if (NS_ALL == search_base) {
		/*  entire namespace    */

		search_handle = acpi_gbl_root_node;
		ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "\\\n"));
	}


	acpi_ns_dump_objects (ACPI_TYPE_ANY, max_depth, ACPI_UINT32_MAX, search_handle);
	return_VOID;
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_dump_entry
 *
 * PARAMETERS:  Handle              - Node to be dumped
 *              Debug_level         - Output level
 *
 * DESCRIPTION: Dump a single Node
 *
 ******************************************************************************/

void
acpi_ns_dump_entry (
	acpi_handle             handle,
	u32                     debug_level)
{
	ACPI_WALK_INFO          info;


	FUNCTION_ENTRY ();


	info.debug_level = debug_level;
	info.owner_id = ACPI_UINT32_MAX;

	acpi_ns_dump_one_object (handle, 1, &info, NULL);
}

#endif

