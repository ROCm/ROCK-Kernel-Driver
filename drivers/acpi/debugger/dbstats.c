/*******************************************************************************
 *
 * Module Name: dbstats - Generation and display of ACPI table statistics
 *              $Revision: 47 $
 *
 ******************************************************************************/

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


#include <acpi.h>
#include <acdebug.h>
#include <amlcode.h>
#include <acparser.h>
#include <acnamesp.h>

#ifdef ENABLE_DEBUGGER

#define _COMPONENT          ACPI_DEBUGGER
	 MODULE_NAME         ("dbstats")

/*
 * Statistics subcommands
 */
ARGUMENT_INFO               acpi_db_stat_types [] =
{ {"ALLOCATIONS"},
	{"OBJECTS"},
	{"MEMORY"},
	{"MISC"},
	{"TABLES"},
	{"SIZES"},
	{"STACK"},
	{NULL}           /* Must be null terminated */
};

#define CMD_ALLOCATIONS     0
#define CMD_OBJECTS         1
#define CMD_MEMORY          2
#define CMD_MISC            3
#define CMD_TABLES          4
#define CMD_SIZES           5
#define CMD_STACK           6


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_enumerate_object
 *
 * PARAMETERS:  Obj_desc            - Object to be counted
 *
 * RETURN:      None
 *
 * DESCRIPTION: Add this object to the global counts, by object type.
 *              Recursively handles subobjects and packages.
 *
 *              [TBD] Restructure - remove recursion.
 *
 ******************************************************************************/

void
acpi_db_enumerate_object (
	acpi_operand_object     *obj_desc)
{
	u32                     type;
	u32                     i;


	if (!obj_desc)
	{
		return;
	}


	/* Enumerate this object first */

	acpi_gbl_num_objects++;

	type = obj_desc->common.type;
	if (type > INTERNAL_TYPE_NODE_MAX)
	{
		acpi_gbl_obj_type_count_misc++;
	}
	else
	{
		acpi_gbl_obj_type_count [type]++;
	}

	/* Count the sub-objects */

	switch (type)
	{
	case ACPI_TYPE_PACKAGE:
		for (i = 0; i< obj_desc->package.count; i++)
		{
			acpi_db_enumerate_object (obj_desc->package.elements[i]);
		}
		break;

	case ACPI_TYPE_DEVICE:
		acpi_db_enumerate_object (obj_desc->device.sys_handler);
		acpi_db_enumerate_object (obj_desc->device.drv_handler);
		acpi_db_enumerate_object (obj_desc->device.addr_handler);
		break;

	case ACPI_TYPE_REGION:
		acpi_db_enumerate_object (obj_desc->region.addr_handler);
		break;

	case ACPI_TYPE_POWER:
		acpi_db_enumerate_object (obj_desc->power_resource.sys_handler);
		acpi_db_enumerate_object (obj_desc->power_resource.drv_handler);
		break;

	case ACPI_TYPE_PROCESSOR:
		acpi_db_enumerate_object (obj_desc->processor.sys_handler);
		acpi_db_enumerate_object (obj_desc->processor.drv_handler);
		acpi_db_enumerate_object (obj_desc->processor.addr_handler);
		break;

	case ACPI_TYPE_THERMAL:
		acpi_db_enumerate_object (obj_desc->thermal_zone.sys_handler);
		acpi_db_enumerate_object (obj_desc->thermal_zone.drv_handler);
		acpi_db_enumerate_object (obj_desc->thermal_zone.addr_handler);
		break;
	}
}


#ifndef PARSER_ONLY

/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_classify_one_object
 *
 * PARAMETERS:  Callback for Walk_namespace
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Enumerate both the object descriptor (including subobjects) and
 *              the parent namespace node.
 *
 ******************************************************************************/

acpi_status
acpi_db_classify_one_object (
	acpi_handle             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	acpi_namespace_node     *node;
	acpi_operand_object     *obj_desc;
	u32                     type;


	acpi_gbl_num_nodes++;

	node = (acpi_namespace_node *) obj_handle;
	obj_desc = ((acpi_namespace_node *) obj_handle)->object;

	acpi_db_enumerate_object (obj_desc);

	type = node->type;
	if (type > INTERNAL_TYPE_NODE_MAX)
	{
		acpi_gbl_node_type_count_misc++;
	}

	else
	{
		acpi_gbl_node_type_count [type]++;
	}

	return AE_OK;


	/* TBD: These need to be counted during the initial parsing phase */
	/*
	if (Acpi_ps_is_named_op (Op->Opcode))
	{
		Num_nodes++;
	}

	if (Is_method)
	{
		Num_method_elements++;
	}

	Num_grammar_elements++;
	Op = Acpi_ps_get_depth_next (Root, Op);

	Size_of_parse_tree          = (Num_grammar_elements - Num_method_elements) * (u32) sizeof (acpi_parse_object);
	Size_of_method_trees        = Num_method_elements * (u32) sizeof (acpi_parse_object);
	Size_of_node_entries        = Num_nodes * (u32) sizeof (acpi_namespace_node);
	Size_of_acpi_objects        = Num_nodes * (u32) sizeof (acpi_operand_object);

	*/
}


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_count_namespace_objects
 *
 * PARAMETERS:  None
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Count and classify the entire namespace, including all
 *              namespace nodes and attached objects.
 *
 ******************************************************************************/

acpi_status
acpi_db_count_namespace_objects (
	void)
{
	u32                     i;


	acpi_gbl_num_nodes = 0;
	acpi_gbl_num_objects = 0;

	acpi_gbl_obj_type_count_misc = 0;
	for (i = 0; i < (INTERNAL_TYPE_NODE_MAX -1); i++)
	{
		acpi_gbl_obj_type_count [i] = 0;
		acpi_gbl_node_type_count [i] = 0;
	}

	acpi_ns_walk_namespace (ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			   FALSE, acpi_db_classify_one_object, NULL, NULL);

	return (AE_OK);
}

#endif


/*******************************************************************************
 *
 * FUNCTION:    Acpi_db_display_statistics
 *
 * PARAMETERS:  Type_arg        - Subcommand
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Display various statistics
 *
 ******************************************************************************/

acpi_status
acpi_db_display_statistics (
	NATIVE_CHAR             *type_arg)
{
	u32                     i;
	u32                     type;
	u32                     outstanding;
	u32                     size;


	if (!acpi_gbl_DSDT)
	{
		acpi_os_printf ("*** Warning: There is no DSDT loaded\n");
	}

	if (!type_arg)
	{
		acpi_os_printf ("The following subcommands are available:\n  ALLOCATIONS, OBJECTS, MEMORY, MISC, SIZES, TABLES\n");
		return (AE_OK);
	}

	STRUPR (type_arg);
	type = acpi_db_match_argument (type_arg, acpi_db_stat_types);
	if (type == (u32) -1)
	{
		acpi_os_printf ("Invalid or unsupported argument\n");
		return (AE_OK);
	}


	switch (type)
	{
#ifndef PARSER_ONLY
	case CMD_ALLOCATIONS:
#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		acpi_ut_dump_allocation_info ();
#endif
		break;
#endif

	case CMD_TABLES:

		acpi_os_printf ("ACPI Table Information:\n\n");
		if (acpi_gbl_DSDT)
		{
			acpi_os_printf ("DSDT Length:................% 7ld (%X)\n", acpi_gbl_DSDT->length, acpi_gbl_DSDT->length);
		}
		break;

	case CMD_OBJECTS:

#ifndef PARSER_ONLY

		acpi_db_count_namespace_objects ();

		acpi_os_printf ("\n_objects defined in the current namespace:\n\n");

		acpi_os_printf ("%16.16s % 10.10s % 10.10s\n", "ACPI_TYPE", "NODES", "OBJECTS");

		for (i = 0; i < INTERNAL_TYPE_NODE_MAX; i++)
		{
			acpi_os_printf ("%16.16s % 10ld% 10ld\n", acpi_ut_get_type_name (i),
				acpi_gbl_node_type_count [i], acpi_gbl_obj_type_count [i]);
		}
		acpi_os_printf ("%16.16s % 10ld% 10ld\n", "Misc/Unknown",
			acpi_gbl_node_type_count_misc, acpi_gbl_obj_type_count_misc);

		acpi_os_printf ("%16.16s % 10ld% 10ld\n", "TOTALS:",
			acpi_gbl_num_nodes, acpi_gbl_num_objects);

#endif
		break;

	case CMD_MEMORY:

#ifdef ACPI_DBG_TRACK_ALLOCATIONS
		acpi_os_printf ("\n----Object and Cache Statistics---------------------------------------------\n");

		for (i = 0; i < ACPI_NUM_MEM_LISTS; i++)
		{
			acpi_os_printf ("\n%s\n", acpi_gbl_memory_lists[i].list_name);

			if (acpi_gbl_memory_lists[i].max_cache_depth > 0)
			{
				acpi_os_printf ("  Cache: [Depth Max Avail Size]         % 7d % 7d % 7d % 7d B\n",
						acpi_gbl_memory_lists[i].cache_depth,
						acpi_gbl_memory_lists[i].max_cache_depth,
						acpi_gbl_memory_lists[i].max_cache_depth - acpi_gbl_memory_lists[i].cache_depth,
						(acpi_gbl_memory_lists[i].cache_depth * acpi_gbl_memory_lists[i].object_size));

				acpi_os_printf ("  Cache: [Requests Hits Misses Obj_size] % 7d % 7d % 7d % 7d B\n",
						acpi_gbl_memory_lists[i].cache_requests,
						acpi_gbl_memory_lists[i].cache_hits,
						acpi_gbl_memory_lists[i].cache_requests - acpi_gbl_memory_lists[i].cache_hits,
						acpi_gbl_memory_lists[i].object_size);
			}

			outstanding = acpi_gbl_memory_lists[i].total_allocated -
					  acpi_gbl_memory_lists[i].total_freed -
					  acpi_gbl_memory_lists[i].cache_depth;

			if (acpi_gbl_memory_lists[i].object_size)
			{
				size = ROUND_UP_TO_1K (outstanding * acpi_gbl_memory_lists[i].object_size);
			}
			else
			{
				size = ROUND_UP_TO_1K (acpi_gbl_memory_lists[i].current_total_size);
			}

			acpi_os_printf ("  Mem:   [Alloc Free Outstanding Size]  % 7d % 7d % 7d % 7d Kb\n",
					acpi_gbl_memory_lists[i].total_allocated,
					acpi_gbl_memory_lists[i].total_freed,
					outstanding, size);
		}
#endif

		break;

	case CMD_MISC:

		acpi_os_printf ("\n_miscellaneous Statistics:\n\n");
		acpi_os_printf ("Calls to Acpi_ps_find:.. ........% 7ld\n", acpi_gbl_ps_find_count);
		acpi_os_printf ("Calls to Acpi_ns_lookup:..........% 7ld\n", acpi_gbl_ns_lookup_count);

		acpi_os_printf ("\n");

		acpi_os_printf ("Mutex usage:\n\n");
		for (i = 0; i < NUM_MTX; i++)
		{
			acpi_os_printf ("%-28s:     % 7ld\n", acpi_ut_get_mutex_name (i), acpi_gbl_acpi_mutex_info[i].use_count);
		}
		break;


	case CMD_SIZES:

		acpi_os_printf ("\n_internal object sizes:\n\n");

		acpi_os_printf ("Common         %3d\n", sizeof (ACPI_OBJECT_COMMON));
		acpi_os_printf ("Number         %3d\n", sizeof (ACPI_OBJECT_INTEGER));
		acpi_os_printf ("String         %3d\n", sizeof (ACPI_OBJECT_STRING));
		acpi_os_printf ("Buffer         %3d\n", sizeof (ACPI_OBJECT_BUFFER));
		acpi_os_printf ("Package        %3d\n", sizeof (ACPI_OBJECT_PACKAGE));
		acpi_os_printf ("Buffer_field   %3d\n", sizeof (ACPI_OBJECT_BUFFER_FIELD));
		acpi_os_printf ("Device         %3d\n", sizeof (ACPI_OBJECT_DEVICE));
		acpi_os_printf ("Event          %3d\n", sizeof (ACPI_OBJECT_EVENT));
		acpi_os_printf ("Method         %3d\n", sizeof (ACPI_OBJECT_METHOD));
		acpi_os_printf ("Mutex          %3d\n", sizeof (ACPI_OBJECT_MUTEX));
		acpi_os_printf ("Region         %3d\n", sizeof (ACPI_OBJECT_REGION));
		acpi_os_printf ("Power_resource %3d\n", sizeof (ACPI_OBJECT_POWER_RESOURCE));
		acpi_os_printf ("Processor      %3d\n", sizeof (ACPI_OBJECT_PROCESSOR));
		acpi_os_printf ("Thermal_zone   %3d\n", sizeof (ACPI_OBJECT_THERMAL_ZONE));
		acpi_os_printf ("Region_field   %3d\n", sizeof (ACPI_OBJECT_REGION_FIELD));
		acpi_os_printf ("Bank_field     %3d\n", sizeof (ACPI_OBJECT_BANK_FIELD));
		acpi_os_printf ("Index_field    %3d\n", sizeof (ACPI_OBJECT_INDEX_FIELD));
		acpi_os_printf ("Reference      %3d\n", sizeof (ACPI_OBJECT_REFERENCE));
		acpi_os_printf ("Notify_handler %3d\n", sizeof (ACPI_OBJECT_NOTIFY_HANDLER));
		acpi_os_printf ("Addr_handler   %3d\n", sizeof (ACPI_OBJECT_ADDR_HANDLER));
		acpi_os_printf ("Extra          %3d\n", sizeof (ACPI_OBJECT_EXTRA));

		acpi_os_printf ("\n");

		acpi_os_printf ("Parse_object   %3d\n", sizeof (acpi_parse_object));
		acpi_os_printf ("Parse2_object  %3d\n", sizeof (acpi_parse2_object));
		acpi_os_printf ("Operand_object %3d\n", sizeof (acpi_operand_object));
		acpi_os_printf ("Namespace_node %3d\n", sizeof (acpi_namespace_node));

		break;


	case CMD_STACK:

		size = acpi_gbl_entry_stack_pointer - acpi_gbl_lowest_stack_pointer;

		acpi_os_printf ("\n_subsystem Stack Usage:\n\n");
		acpi_os_printf ("Entry Stack Pointer        %X\n", acpi_gbl_entry_stack_pointer);
		acpi_os_printf ("Lowest Stack Pointer       %X\n", acpi_gbl_lowest_stack_pointer);
		acpi_os_printf ("Stack Use                  %X (%d)\n", size, size);
		acpi_os_printf ("Deepest Procedure Nesting  %d\n", acpi_gbl_deepest_nesting);
		break;
	}

	acpi_os_printf ("\n");
	return (AE_OK);
}


#endif /* ENABLE_DEBUGGER  */
