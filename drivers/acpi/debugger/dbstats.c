/*******************************************************************************
 *
 * Module Name: dbstats - Generation and display of ACPI table statistics
 *              $Revision: 40 $
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
	{NULL}           /* Must be null terminated */
};

#define CMD_ALLOCATIONS     0
#define CMD_OBJECTS         1
#define CMD_MEMORY          2
#define CMD_MISC            3
#define CMD_TABLES          4
#define CMD_SIZES           5


/*
 * Statistic globals
 */
u16                         acpi_gbl_obj_type_count[INTERNAL_TYPE_NODE_MAX+1];
u16                         acpi_gbl_node_type_count[INTERNAL_TYPE_NODE_MAX+1];
u16                         acpi_gbl_obj_type_count_misc;
u16                         acpi_gbl_node_type_count_misc;
u32                         num_nodes;
u32                         num_objects;


u32                         size_of_parse_tree;
u32                         size_of_method_trees;
u32                         size_of_node_entries;
u32                         size_of_acpi_objects;


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
	ACPI_OPERAND_OBJECT     *obj_desc)
{
	u32                     type;
	u32                     i;


	if (!obj_desc)
	{
		return;
	}


	/* Enumerate this object first */

	num_objects++;

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

ACPI_STATUS
acpi_db_classify_one_object (
	ACPI_HANDLE             obj_handle,
	u32                     nesting_level,
	void                    *context,
	void                    **return_value)
{
	ACPI_NAMESPACE_NODE     *node;
	ACPI_OPERAND_OBJECT     *obj_desc;
	u32                     type;


	num_nodes++;

	node = (ACPI_NAMESPACE_NODE *) obj_handle;
	obj_desc = ((ACPI_NAMESPACE_NODE *) obj_handle)->object;

	acpi_db_enumerate_object (obj_desc);

	type = node->type;
	if (type > INTERNAL_TYPE_INVALID)
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

	Size_of_parse_tree          = (Num_grammar_elements - Num_method_elements) * (u32) sizeof (ACPI_PARSE_OBJECT);
	Size_of_method_trees        = Num_method_elements * (u32) sizeof (ACPI_PARSE_OBJECT);
	Size_of_node_entries        = Num_nodes * (u32) sizeof (ACPI_NAMESPACE_NODE);
	Size_of_acpi_objects        = Num_nodes * (u32) sizeof (ACPI_OPERAND_OBJECT);

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

ACPI_STATUS
acpi_db_count_namespace_objects (
	void)
{
	u32                     i;


	num_nodes = 0;
	num_objects = 0;

	acpi_gbl_obj_type_count_misc = 0;
	for (i = 0; i < INTERNAL_TYPE_INVALID; i++)
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

ACPI_STATUS
acpi_db_display_statistics (
	NATIVE_CHAR             *type_arg)
{
	u32                     i;
	u32                     type;


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

#ifndef PARSER_ONLY

	acpi_db_count_namespace_objects ();
#endif


	switch (type)
	{
#ifndef PARSER_ONLY
	case CMD_ALLOCATIONS:
		acpi_ut_dump_allocation_info ();
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
			num_nodes, num_objects);


/*
		Acpi_os_printf ("\n");

		Acpi_os_printf ("ASL/AML Grammar Usage:\n\n");
		Acpi_os_printf ("Elements Inside Methods:....% 7ld\n", Num_method_elements);
		Acpi_os_printf ("Elements Outside Methods:...% 7ld\n", Num_grammar_elements - Num_method_elements);
		Acpi_os_printf ("Total Grammar Elements:.....% 7ld\n", Num_grammar_elements);
*/
		break;

	case CMD_MEMORY:

		acpi_os_printf ("\n_dynamic Memory Estimates:\n\n");
		acpi_os_printf ("Parse Tree without Methods:.% 7ld\n", size_of_parse_tree);
		acpi_os_printf ("Control Method Parse Trees:.% 7ld (If parsed simultaneously)\n", size_of_method_trees);
		acpi_os_printf ("Namespace Nodes:............% 7ld (%d nodes)\n", sizeof (ACPI_NAMESPACE_NODE) * num_nodes, num_nodes);
		acpi_os_printf ("Named Internal Objects......% 7ld\n", size_of_acpi_objects);
		acpi_os_printf ("State Cache size............% 7ld\n", acpi_gbl_generic_state_cache_depth * sizeof (ACPI_GENERIC_STATE));
		acpi_os_printf ("Parse Cache size............% 7ld\n", acpi_gbl_parse_cache_depth * sizeof (ACPI_PARSE_OBJECT));
		acpi_os_printf ("Object Cache size...........% 7ld\n", acpi_gbl_object_cache_depth * sizeof (ACPI_OPERAND_OBJECT));
		acpi_os_printf ("Walk_state Cache size........% 7ld\n", acpi_gbl_walk_state_cache_depth * sizeof (ACPI_WALK_STATE));

		acpi_os_printf ("\n");

		acpi_os_printf ("Cache Statistics:\n\n");
		acpi_os_printf ("State Cache requests........% 7ld\n", acpi_gbl_state_cache_requests);
		acpi_os_printf ("State Cache hits............% 7ld\n", acpi_gbl_state_cache_hits);
		acpi_os_printf ("State Cache depth...........% 7ld (%d remaining entries)\n", acpi_gbl_generic_state_cache_depth,
				  MAX_STATE_CACHE_DEPTH - acpi_gbl_generic_state_cache_depth);
		acpi_os_printf ("Parse Cache requests........% 7ld\n", acpi_gbl_parse_cache_requests);
		acpi_os_printf ("Parse Cache hits............% 7ld\n", acpi_gbl_parse_cache_hits);
		acpi_os_printf ("Parse Cache depth...........% 7ld (%d remaining entries)\n", acpi_gbl_parse_cache_depth,
				  MAX_PARSE_CACHE_DEPTH - acpi_gbl_parse_cache_depth);
		acpi_os_printf ("Ext Parse Cache requests....% 7ld\n", acpi_gbl_ext_parse_cache_requests);
		acpi_os_printf ("Ext Parse Cache hits........% 7ld\n", acpi_gbl_ext_parse_cache_hits);
		acpi_os_printf ("Ext Parse Cache depth.......% 7ld (%d remaining entries)\n", acpi_gbl_ext_parse_cache_depth,
				  MAX_EXTPARSE_CACHE_DEPTH - acpi_gbl_ext_parse_cache_depth);
		acpi_os_printf ("Object Cache requests.......% 7ld\n", acpi_gbl_object_cache_requests);
		acpi_os_printf ("Object Cache hits...........% 7ld\n", acpi_gbl_object_cache_hits);
		acpi_os_printf ("Object Cache depth..........% 7ld (%d remaining entries)\n", acpi_gbl_object_cache_depth,
				  MAX_OBJECT_CACHE_DEPTH - acpi_gbl_object_cache_depth);
		acpi_os_printf ("Walk_state Cache requests....% 7ld\n", acpi_gbl_walk_state_cache_requests);
		acpi_os_printf ("Walk_state Cache hits........% 7ld\n", acpi_gbl_walk_state_cache_hits);
		acpi_os_printf ("Walk_state Cache depth.......% 7ld (%d remaining entries)\n", acpi_gbl_walk_state_cache_depth,
				  MAX_WALK_CACHE_DEPTH - acpi_gbl_walk_state_cache_depth);
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

		acpi_os_printf ("Parse_object   %3d\n", sizeof (ACPI_PARSE_OBJECT));
		acpi_os_printf ("Parse2_object  %3d\n", sizeof (ACPI_PARSE2_OBJECT));
		acpi_os_printf ("Operand_object %3d\n", sizeof (ACPI_OPERAND_OBJECT));
		acpi_os_printf ("Namespace_node %3d\n", sizeof (ACPI_NAMESPACE_NODE));

		break;

	}

	acpi_os_printf ("\n");
	return (AE_OK);
}


#endif /* ENABLE_DEBUGGER  */
