/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
 *              $Revision: 105 $
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
#include "acparser.h"


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
	acpi_walk_info          *info = (acpi_walk_info *) context;
	acpi_namespace_node     *this_node;
	acpi_operand_object     *obj_desc = NULL;
	acpi_object_type8       obj_type;
	acpi_object_type8       type;
	u32                     bytes_to_dump;
	u32                     downstream_sibling_mask = 0;
	u32                     level_tmp;
	u32                     which_bit;
	u32                     i;


	PROC_NAME ("Ns_dump_one_object");


	this_node = acpi_ns_map_handle_to_node (obj_handle);

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
	ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " %4.4s %-12s %p",
			(char*)&this_node->name, acpi_ut_get_type_name (type), this_node));

	obj_desc = this_node->object;

	switch (info->display_type) {
	case ACPI_DISPLAY_SUMMARY:

		if (!obj_desc) {
			/* No attached object, we are done */

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
			return (AE_OK);
		}


		switch (type) {
		case ACPI_TYPE_PROCESSOR:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " ID %d Addr %.4X Len %.4X\n",
					 obj_desc->processor.proc_id,
					 obj_desc->processor.address,
					 obj_desc->processor.length));
			break;

		case ACPI_TYPE_DEVICE:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Notification object: %p", obj_desc));
			break;

		case ACPI_TYPE_METHOD:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Args %d Len %.4X Aml %p \n",
					 obj_desc->method.param_count,
					 obj_desc->method.aml_length,
					 obj_desc->method.aml_start));
			break;

		case ACPI_TYPE_INTEGER:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " = %8.8X%8.8X\n",
					 HIDWORD (obj_desc->integer.value),
					 LODWORD (obj_desc->integer.value)));
			break;

		case ACPI_TYPE_PACKAGE:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Elements %.2X\n",
					 obj_desc->package.count));
			break;

		case ACPI_TYPE_BUFFER:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Len %.2X",
					 obj_desc->buffer.length));

			/* Dump some of the buffer */

			if (obj_desc->buffer.length > 0) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " ="));
				for (i = 0; (i < obj_desc->buffer.length && i < 12); i++) {
					ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " %.2X",
							obj_desc->buffer.pointer[i]));
				}
			}
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
			break;

		case ACPI_TYPE_STRING:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Len %.2X",
					 obj_desc->string.length));

			if (obj_desc->string.length > 0) {
				 ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " = \"%.32s\"...",
						 obj_desc->string.pointer));
			}
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
			break;

		case ACPI_TYPE_REGION:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " [%s]",
					 acpi_ut_get_region_name (obj_desc->region.space_id)));
			if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Addr %8.8X%8.8X Len %.4X\n",
						 HIDWORD(obj_desc->region.address),
						 LODWORD(obj_desc->region.address),
						 obj_desc->region.length));
			}
			else {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " [Address/Length not evaluated]\n"));
			}
			break;

		case INTERNAL_TYPE_REFERENCE:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " [%s]\n",
					 acpi_ps_get_opcode_name (obj_desc->reference.opcode)));
			break;

		case ACPI_TYPE_BUFFER_FIELD:

			/* TBD: print Buffer name when we can easily get it */
			break;

		case INTERNAL_TYPE_REGION_FIELD:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Rgn [%4.4s]",
					 (char *) &obj_desc->common_field.region_obj->region.node->name));
			break;

		case INTERNAL_TYPE_BANK_FIELD:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Rgn [%4.4s]",
					 (char *) &obj_desc->common_field.region_obj->region.node->name));
			break;

		case INTERNAL_TYPE_INDEX_FIELD:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Rgn [%4.4s]",
					 (char *) &obj_desc->index_field.index_obj->common_field.region_obj->region.node->name));
			break;

		default:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Object %p\n", obj_desc));
			break;
		}

		/* Common field handling */

		switch (type) {
		case ACPI_TYPE_BUFFER_FIELD:
		case INTERNAL_TYPE_REGION_FIELD:
		case INTERNAL_TYPE_BANK_FIELD:
		case INTERNAL_TYPE_INDEX_FIELD:
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " Off %.2X Len %.2X Acc %.2d\n",
					 (obj_desc->common_field.base_byte_offset * 8) + obj_desc->common_field.start_field_bit_offset,
					 obj_desc->common_field.bit_length,
					 obj_desc->common_field.access_bit_width));
			break;
		}

		break;


	case ACPI_DISPLAY_OBJECTS:

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "%p O:%p",
				this_node, obj_desc));

		if (!obj_desc) {
			/* No attached object, we are done */

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
			return (AE_OK);
		}

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(R%d)",
				obj_desc->common.reference_count));

		switch (type) {

		case ACPI_TYPE_METHOD:

			/* Name is a Method and its AML offset/length are set */

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " M:%p-%X\n",
					 obj_desc->method.aml_start,
					 obj_desc->method.aml_length));

			break;


		case ACPI_TYPE_INTEGER:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " N:%X%X\n",
					 HIDWORD(obj_desc->integer.value),
					 LODWORD(obj_desc->integer.value)));
			break;


		case ACPI_TYPE_STRING:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " S:%p-%X\n",
					 obj_desc->string.pointer,
					 obj_desc->string.length));
			break;


		case ACPI_TYPE_BUFFER:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, " B:%p-%X\n",
					 obj_desc->buffer.pointer,
					 obj_desc->buffer.length));
			break;


		default:

			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "\n"));
			break;
		}
		break;
	}

	/* If debug turned off, done */

	if (!(acpi_dbg_level & ACPI_LV_VALUES)) {
		return (AE_OK);
	}


	/* If there is an attached object, display it */

	obj_desc = this_node->object;

	/* Dump attached objects */

	while (obj_desc) {
		obj_type = INTERNAL_TYPE_INVALID;

		/* Decode the type of attached object and dump the contents */

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "        Attached Object %p: ", obj_desc));

		if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED)) {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to Node)\n"));
			bytes_to_dump = sizeof (acpi_namespace_node);
		}


		else if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_INTERNAL)) {
			obj_type = obj_desc->common.type;

			if (obj_type > INTERNAL_TYPE_MAX) {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to ACPI Object type %X [UNKNOWN])\n", obj_type));
				bytes_to_dump = 32;
			}

			else {
				ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(Ptr to ACPI Object type %2.2X [%s])\n",
						   obj_type, acpi_ut_get_type_name (obj_type)));
				bytes_to_dump = sizeof (acpi_operand_object);
			}
		}

		else {
			ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "(String or Buffer - not descriptor)\n"));
			bytes_to_dump = 16;
		}

		DUMP_BUFFER (obj_desc, bytes_to_dump);

		/* If value is NOT an internal object, we are done */

		if (VALID_DESCRIPTOR_TYPE (obj_desc, ACPI_DESC_TYPE_NAMED)) {
			goto cleanup;
		}

		/*
		 * Valid object, get the pointer to next level, if any
		 */
		switch (obj_type) {
		case ACPI_TYPE_STRING:
			obj_desc = (acpi_operand_object *) obj_desc->string.pointer;
			break;

		case ACPI_TYPE_BUFFER:
			obj_desc = (acpi_operand_object *) obj_desc->buffer.pointer;
			break;

		case ACPI_TYPE_BUFFER_FIELD:
			obj_desc = (acpi_operand_object *) obj_desc->buffer_field.buffer_obj;
			break;

		case ACPI_TYPE_PACKAGE:
			obj_desc = (acpi_operand_object *) obj_desc->package.elements;
			break;

		case ACPI_TYPE_METHOD:
			obj_desc = (acpi_operand_object *) obj_desc->method.aml_start;
			break;

		case INTERNAL_TYPE_REGION_FIELD:
			obj_desc = (acpi_operand_object *) obj_desc->field.region_obj;
			break;

		case INTERNAL_TYPE_BANK_FIELD:
			obj_desc = (acpi_operand_object *) obj_desc->bank_field.region_obj;
			break;

		case INTERNAL_TYPE_INDEX_FIELD:
			obj_desc = (acpi_operand_object *) obj_desc->index_field.index_obj;
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
	u8                      display_type,
	u32                     max_depth,
	u32                     owner_id,
	acpi_handle             start_handle)
{
	acpi_walk_info          info;


	FUNCTION_ENTRY ();


	info.debug_level = ACPI_LV_TABLES;
	info.owner_id = owner_id;
	info.display_type = display_type;


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

		ACPI_DEBUG_PRINT_RAW ((ACPI_DB_TABLES, "    HID: %s, ADR: %8.8X%8.8X, Status: %x\n",
				  info.hardware_id, HIDWORD(info.address), LODWORD(info.address), info.current_status));
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


	acpi_ns_dump_objects (ACPI_TYPE_ANY, ACPI_DISPLAY_OBJECTS, max_depth,
			ACPI_UINT32_MAX, search_handle);
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
	acpi_walk_info          info;


	FUNCTION_ENTRY ();


	info.debug_level = debug_level;
	info.owner_id = ACPI_UINT32_MAX;

	acpi_ns_dump_one_object (handle, 1, &info, NULL);
}

#endif

