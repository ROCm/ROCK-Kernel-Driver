/******************************************************************************
 *
 * Module Name: nsdump - table dumping routines for debug
 *              $Revision: 140 $
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
#include "acparser.h"


#define _COMPONENT          ACPI_NAMESPACE
	 ACPI_MODULE_NAME    ("nsdump")

#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)


/*******************************************************************************
 *
 * FUNCTION:    Acpi_ns_print_pathname
 *
 * PARAMETERS:  Num_segment         - Number of ACPI name segments
 *              Pathname            - The compressed (internal) path
 *
 * DESCRIPTION: Print an object's full namespace pathname
 *
 ******************************************************************************/

void
acpi_ns_print_pathname (
	u32                     num_segments,
	char                    *pathname)
{
	ACPI_FUNCTION_NAME ("Ns_print_pathname");


	if (!(acpi_dbg_level & ACPI_LV_NAMES) || !(acpi_dbg_layer & ACPI_NAMESPACE)) {
		return;
	}

		/* Print the entire name */

	ACPI_DEBUG_PRINT ((ACPI_DB_NAMES, "["));

	while (num_segments) {
		acpi_os_printf ("%4.4s", pathname);
		pathname += ACPI_NAME_SIZE;

		num_segments--;
		if (num_segments) {
			acpi_os_printf (".");
		}
	}

	acpi_os_printf ("]\n");
}


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
	acpi_buffer             buffer;
	acpi_status             status;


	ACPI_FUNCTION_TRACE ("Ns_dump_pathname");


	/* Do this only if the requested debug level and component are enabled */

	if (!(acpi_dbg_level & level) || !(acpi_dbg_layer & component)) {
		return_ACPI_STATUS (AE_OK);
	}

	/* Convert handle to a full pathname and print it (with supplied message) */

	buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;

	status = acpi_ns_handle_to_pathname (handle, &buffer);
	if (ACPI_SUCCESS (status)) {
		acpi_os_printf ("%s %s (Node %p)\n", msg, (char *) buffer.pointer, handle);
		ACPI_MEM_FREE (buffer.pointer);
	}

	return_ACPI_STATUS (status);
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
	acpi_object_type        obj_type;
	acpi_object_type        type;
	u32                     bytes_to_dump;
	u32                     downstream_sibling_mask = 0;
	u32                     level_tmp;
	u32                     which_bit;
	u32                     i;
	u32                     dbg_level;


	ACPI_FUNCTION_NAME ("Ns_dump_one_object");


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
				acpi_os_printf ("|");
			}
			else {
				acpi_os_printf (" ");
			}

			which_bit <<= 1;
		}
		else {
			if (acpi_ns_exist_downstream_sibling (this_node + 1)) {
				downstream_sibling_mask |= ((u32) 1 << (level - 1));
				acpi_os_printf ("+");
			}
			else {
				downstream_sibling_mask &= ACPI_UINT32_MAX ^ ((u32) 1 << (level - 1));
				acpi_os_printf ("+");
			}

			if (this_node->child == NULL) {
				acpi_os_printf ("-");
			}
			else if (acpi_ns_exist_downstream_sibling (this_node->child)) {
				acpi_os_printf ("+");
			}
			else {
				acpi_os_printf ("-");
			}
		}
	}

	/* Check the integrity of our data */

	if (type > INTERNAL_TYPE_MAX) {
		type = INTERNAL_TYPE_DEF_ANY;  /* prints as *ERROR* */
	}

	if (!acpi_ut_valid_acpi_name (this_node->name.integer)) {
		ACPI_REPORT_WARNING (("Invalid ACPI Name %08X\n", this_node->name.integer));
	}

	/*
	 * Now we can print out the pertinent information
	 */
	acpi_os_printf (" %4.4s %-12s %p",
			this_node->name.ascii, acpi_ut_get_type_name (type), this_node);

	dbg_level = acpi_dbg_level;
	acpi_dbg_level = 0;
	obj_desc = acpi_ns_get_attached_object (this_node);
	acpi_dbg_level = dbg_level;

	switch (info->display_type) {
	case ACPI_DISPLAY_SUMMARY:

		if (!obj_desc) {
			/* No attached object, we are done */

			acpi_os_printf ("\n");
			return (AE_OK);
		}

		switch (type) {
		case ACPI_TYPE_PROCESSOR:

			acpi_os_printf (" ID %X Len %.4X Addr %p\n",
					 obj_desc->processor.proc_id,
					 obj_desc->processor.length,
					 (char *) obj_desc->processor.address);
			break;


		case ACPI_TYPE_DEVICE:

			acpi_os_printf (" Notification object: %p", obj_desc);
			break;


		case ACPI_TYPE_METHOD:

			acpi_os_printf (" Args %X Len %.4X Aml %p\n",
					 (u32) obj_desc->method.param_count,
					 obj_desc->method.aml_length,
					 obj_desc->method.aml_start);
			break;


		case ACPI_TYPE_INTEGER:

			acpi_os_printf (" = %8.8X%8.8X\n",
					 ACPI_HIDWORD (obj_desc->integer.value),
					 ACPI_LODWORD (obj_desc->integer.value));
			break;


		case ACPI_TYPE_PACKAGE:

			if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
				acpi_os_printf (" Elements %.2X\n",
						 obj_desc->package.count);
			}
			else {
				acpi_os_printf (" [Length not yet evaluated]\n");
			}
			break;


		case ACPI_TYPE_BUFFER:

			if (obj_desc->common.flags & AOPOBJ_DATA_VALID) {
				acpi_os_printf (" Len %.2X",
						 obj_desc->buffer.length);

				/* Dump some of the buffer */

				if (obj_desc->buffer.length > 0) {
					acpi_os_printf (" =");
					for (i = 0; (i < obj_desc->buffer.length && i < 12); i++) {
						acpi_os_printf (" %.2hX", obj_desc->buffer.pointer[i]);
					}
				}
				acpi_os_printf ("\n");
			}
			else {
				acpi_os_printf (" [Length not yet evaluated]\n");
			}
			break;


		case ACPI_TYPE_STRING:

			acpi_os_printf (" Len %.2X", obj_desc->string.length);

			if (obj_desc->string.length > 0) {
				acpi_os_printf (" = \"%.32s\"", obj_desc->string.pointer);
				if (obj_desc->string.length > 32) {
					acpi_os_printf ("...");
				}
			}
			acpi_os_printf ("\n");
			break;


		case ACPI_TYPE_REGION:

			acpi_os_printf (" [%s]", acpi_ut_get_region_name (obj_desc->region.space_id));
			if (obj_desc->region.flags & AOPOBJ_DATA_VALID) {
				acpi_os_printf (" Addr %8.8X%8.8X Len %.4X\n",
						 ACPI_HIDWORD (obj_desc->region.address),
						 ACPI_LODWORD (obj_desc->region.address),
						 obj_desc->region.length);
			}
			else {
				acpi_os_printf (" [Address/Length not yet evaluated]\n");
			}
			break;


		case INTERNAL_TYPE_REFERENCE:

			acpi_os_printf (" [%s]\n",
					acpi_ps_get_opcode_name (obj_desc->reference.opcode));
			break;


		case ACPI_TYPE_BUFFER_FIELD:

			if (obj_desc->buffer_field.buffer_obj &&
				obj_desc->buffer_field.buffer_obj->buffer.node) {
				acpi_os_printf (" Buf [%4.4s]",
						obj_desc->buffer_field.buffer_obj->buffer.node->name.ascii);
			}
			break;


		case INTERNAL_TYPE_REGION_FIELD:

			acpi_os_printf (" Rgn [%4.4s]",
					obj_desc->common_field.region_obj->region.node->name.ascii);
			break;


		case INTERNAL_TYPE_BANK_FIELD:

			acpi_os_printf (" Rgn [%4.4s] Bnk [%4.4s]",
					obj_desc->common_field.region_obj->region.node->name.ascii,
					obj_desc->bank_field.bank_obj->common_field.node->name.ascii);
			break;


		case INTERNAL_TYPE_INDEX_FIELD:

			acpi_os_printf (" Idx [%4.4s] Dat [%4.4s]",
					obj_desc->index_field.index_obj->common_field.node->name.ascii,
					obj_desc->index_field.data_obj->common_field.node->name.ascii);
			break;


		default:

			acpi_os_printf (" Object %p\n", obj_desc);
			break;
		}

		/* Common field handling */

		switch (type) {
		case ACPI_TYPE_BUFFER_FIELD:
		case INTERNAL_TYPE_REGION_FIELD:
		case INTERNAL_TYPE_BANK_FIELD:
		case INTERNAL_TYPE_INDEX_FIELD:
			acpi_os_printf (" Off %.2X Len %.2X Acc %.2hd\n",
					(obj_desc->common_field.base_byte_offset * 8)
						+ obj_desc->common_field.start_field_bit_offset,
					obj_desc->common_field.bit_length,
					obj_desc->common_field.access_byte_width);
			break;

		default:
			break;
		}
		break;


	case ACPI_DISPLAY_OBJECTS:

		acpi_os_printf ("%p O:%p",
				this_node, obj_desc);

		if (!obj_desc) {
			/* No attached object, we are done */

			acpi_os_printf ("\n");
			return (AE_OK);
		}

		acpi_os_printf ("(R%d)",
				obj_desc->common.reference_count);

		switch (type) {
		case ACPI_TYPE_METHOD:

			/* Name is a Method and its AML offset/length are set */

			acpi_os_printf (" M:%p-%X\n", obj_desc->method.aml_start,
					  obj_desc->method.aml_length);
			break;

		case ACPI_TYPE_INTEGER:

			acpi_os_printf (" N:%X%X\n", ACPI_HIDWORD(obj_desc->integer.value),
					 ACPI_LODWORD(obj_desc->integer.value));
			break;

		case ACPI_TYPE_STRING:

			acpi_os_printf (" S:%p-%X\n", obj_desc->string.pointer,
					  obj_desc->string.length);
			break;

		case ACPI_TYPE_BUFFER:

			acpi_os_printf (" B:%p-%X\n", obj_desc->buffer.pointer,
					  obj_desc->buffer.length);
			break;

		default:

			acpi_os_printf ("\n");
			break;
		}
		break;


	default:
		acpi_os_printf ("\n");
		break;
	}

	/* If debug turned off, done */

	if (!(acpi_dbg_level & ACPI_LV_VALUES)) {
		return (AE_OK);
	}


	/* If there is an attached object, display it */

	dbg_level = acpi_dbg_level;
	acpi_dbg_level = 0;
	obj_desc = acpi_ns_get_attached_object (this_node);
	acpi_dbg_level = dbg_level;

	/* Dump attached objects */

	while (obj_desc) {
		obj_type = INTERNAL_TYPE_INVALID;
		acpi_os_printf ("      Attached Object %p: ", obj_desc);

		/* Decode the type of attached object and dump the contents */

		switch (ACPI_GET_DESCRIPTOR_TYPE (obj_desc)) {
		case ACPI_DESC_TYPE_NAMED:

			acpi_os_printf ("(Ptr to Node)\n");
			bytes_to_dump = sizeof (acpi_namespace_node);
			break;


		case ACPI_DESC_TYPE_OPERAND:

			obj_type = ACPI_GET_OBJECT_TYPE (obj_desc);

			if (obj_type > INTERNAL_TYPE_MAX) {
				acpi_os_printf ("(Ptr to ACPI Object type %X [UNKNOWN])\n", obj_type);
				bytes_to_dump = 32;
			}
			else {
				acpi_os_printf ("(Ptr to ACPI Object type %s, %X)\n",
						   acpi_ut_get_type_name (obj_type), obj_type);
				bytes_to_dump = sizeof (acpi_operand_object);
			}
			break;


		default:

			acpi_os_printf ("(String or Buffer ptr - not an object descriptor)\n");
			bytes_to_dump = 16;
			break;
		}

		ACPI_DUMP_BUFFER (obj_desc, bytes_to_dump);

		/* If value is NOT an internal object, we are done */

		if (ACPI_GET_DESCRIPTOR_TYPE (obj_desc) != ACPI_DESC_TYPE_OPERAND) {
			goto cleanup;
		}

		/*
		 * Valid object, get the pointer to next level, if any
		 */
		switch (obj_type) {
		case ACPI_TYPE_STRING:
			obj_desc = (void *) obj_desc->string.pointer;
			break;

		case ACPI_TYPE_BUFFER:
			obj_desc = (void *) obj_desc->buffer.pointer;
			break;

		case ACPI_TYPE_BUFFER_FIELD:
			obj_desc = (acpi_operand_object *) obj_desc->buffer_field.buffer_obj;
			break;

		case ACPI_TYPE_PACKAGE:
			obj_desc = (void *) obj_desc->package.elements;
			break;

		case ACPI_TYPE_METHOD:
			obj_desc = (void *) obj_desc->method.aml_start;
			break;

		case INTERNAL_TYPE_REGION_FIELD:
			obj_desc = (void *) obj_desc->field.region_obj;
			break;

		case INTERNAL_TYPE_BANK_FIELD:
			obj_desc = (void *) obj_desc->bank_field.region_obj;
			break;

		case INTERNAL_TYPE_INDEX_FIELD:
			obj_desc = (void *) obj_desc->index_field.index_obj;
			break;

		default:
			goto cleanup;
		}

		obj_type = INTERNAL_TYPE_INVALID;  /* Terminate loop after next pass */
	}

cleanup:
	acpi_os_printf ("\n");
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
	acpi_object_type        type,
	u8                      display_type,
	u32                     max_depth,
	u32                     owner_id,
	acpi_handle             start_handle)
{
	acpi_walk_info          info;


	ACPI_FUNCTION_ENTRY ();


	info.debug_level = ACPI_LV_TABLES;
	info.owner_id = owner_id;
	info.display_type = display_type;


	(void) acpi_ns_walk_namespace (type, start_handle, max_depth,
			 ACPI_NS_WALK_NO_UNLOCK, acpi_ns_dump_one_object,
			 (void *) &info, NULL);
}


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


	ACPI_FUNCTION_TRACE ("Ns_dump_tables");


	if (!acpi_gbl_root_node) {
		/*
		 * If the name space has not been initialized,
		 * there is nothing to dump.
		 */
		ACPI_DEBUG_PRINT ((ACPI_DB_TABLES, "name space not initialized!\n"));
		return_VOID;
	}

	if (ACPI_NS_ALL == search_base) {
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


	ACPI_FUNCTION_ENTRY ();


	info.debug_level = debug_level;
	info.owner_id = ACPI_UINT32_MAX;
	info.display_type = ACPI_DISPLAY_SUMMARY;

	(void) acpi_ns_dump_one_object (handle, 1, &info, NULL);
}

#endif

