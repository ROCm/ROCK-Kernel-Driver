/******************************************************************************
 *
 * Module Name: utglobal - Global variables for the ACPI subsystem
 *              $Revision: 133 $
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

#define DEFINE_ACPI_GLOBALS

#include "acpi.h"
#include "acevents.h"
#include "acnamesp.h"
#include "acinterp.h"
#include "amlcode.h"


#define _COMPONENT          ACPI_UTILITIES
	 MODULE_NAME         ("utglobal")


/******************************************************************************
 *
 * FUNCTION:    Acpi_format_exception
 *
 * PARAMETERS:  Status       - The acpi_status code to be formatted
 *
 * RETURN:      A string containing the exception  text
 *
 * DESCRIPTION: This function translates an ACPI exception into an ASCII string.
 *
 ******************************************************************************/

const char *
acpi_format_exception (
	acpi_status             status)
{
	const char              *exception = "UNKNOWN_STATUS_CODE";
	acpi_status             sub_status;


	sub_status = (status & ~AE_CODE_MASK);


	switch (status & AE_CODE_MASK) {
	case AE_CODE_ENVIRONMENTAL:

		if (sub_status <= AE_CODE_ENV_MAX) {
			exception = acpi_gbl_exception_names_env [sub_status];
		}
		break;

	case AE_CODE_PROGRAMMER:

		if (sub_status <= AE_CODE_PGM_MAX) {
			exception = acpi_gbl_exception_names_pgm [sub_status -1];
		}
		break;

	case AE_CODE_ACPI_TABLES:

		if (sub_status <= AE_CODE_TBL_MAX) {
			exception = acpi_gbl_exception_names_tbl [sub_status -1];
		}
		break;

	case AE_CODE_AML:

		if (sub_status <= AE_CODE_AML_MAX) {
			exception = acpi_gbl_exception_names_aml [sub_status -1];
		}
		break;

	case AE_CODE_CONTROL:

		if (sub_status <= AE_CODE_CTRL_MAX) {
			exception = acpi_gbl_exception_names_ctrl [sub_status -1];
		}
		break;

	default:
		break;
	}


	return ((const char *) exception);
}


/******************************************************************************
 *
 * Static global variable initialization.
 *
 ******************************************************************************/

/*
 * We want the debug switches statically initialized so they
 * are already set when the debugger is entered.
 */

/* Debug switch - level and trace mask */

#ifdef ACPI_DEBUG
u32                         acpi_dbg_level = DEBUG_DEFAULT;
#else
u32                         acpi_dbg_level = NORMAL_DEFAULT;
#endif

/* Debug switch - layer (component) mask */

u32                         acpi_dbg_layer = ACPI_COMPONENT_DEFAULT;
u32                         acpi_gbl_nesting_level = 0;


/* Debugger globals */

u8                          acpi_gbl_db_terminate_threads = FALSE;
u8                          acpi_gbl_method_executing = FALSE;

/* System flags */

u32                         acpi_gbl_system_flags = 0;
u32                         acpi_gbl_startup_flags = 0;

/* System starts unitialized! */

u8                          acpi_gbl_shutdown = TRUE;

const u8                    acpi_gbl_decode_to8bit [8] = {1,2,4,8,16,32,64,128};

const NATIVE_CHAR           *acpi_gbl_db_sleep_states[ACPI_NUM_SLEEP_STATES] = {
			  "\\_S0_","\\_S1_","\\_S2_","\\_S3_",
			  "\\_S4_","\\_S5_","\\_S4B"};


/******************************************************************************
 *
 * Namespace globals
 *
 ******************************************************************************/


/*
 * Names built-in to the interpreter
 *
 * Initial values are currently supported only for types String and Number.
 * To avoid type punning, both are specified as strings in this table.
 *
 * NOTES:
 * 1) _SB_ is defined to be a device to allow _SB_/_INI to be run
 *    during the initialization sequence.
 */

const predefined_names      acpi_gbl_pre_defined_names[] =
{ {"_GPE",    INTERNAL_TYPE_DEF_ANY},
	{"_PR_",    INTERNAL_TYPE_DEF_ANY},
	{"_SB_",    ACPI_TYPE_DEVICE},
	{"_SI_",    INTERNAL_TYPE_DEF_ANY},
	{"_TZ_",    INTERNAL_TYPE_DEF_ANY},
	{"_REV",    ACPI_TYPE_INTEGER, "2"},
	{"_OS_",    ACPI_TYPE_STRING, ACPI_OS_NAME},
	{"_GL_",    ACPI_TYPE_MUTEX, "0"},
	{NULL,      ACPI_TYPE_ANY}           /* Table terminator */
};


/*
 * Properties of the ACPI Object Types, both internal and external.
 *
 * Elements of Acpi_ns_properties are bit significant
 * and the table is indexed by values of acpi_object_type
 */

const u8                    acpi_gbl_ns_properties[] =
{
	NSP_NORMAL,                 /* 00 Any              */
	NSP_NORMAL,                 /* 01 Number           */
	NSP_NORMAL,                 /* 02 String           */
	NSP_NORMAL,                 /* 03 Buffer           */
	NSP_LOCAL,                  /* 04 Package          */
	NSP_NORMAL,                 /* 05 Field_unit       */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 06 Device           */
	NSP_LOCAL,                  /* 07 Acpi_event       */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 08 Method           */
	NSP_LOCAL,                  /* 09 Mutex            */
	NSP_LOCAL,                  /* 10 Region           */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 11 Power            */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 12 Processor        */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 13 Thermal          */
	NSP_NORMAL,                 /* 14 Buffer_field     */
	NSP_NORMAL,                 /* 15 Ddb_handle       */
	NSP_NORMAL,                 /* 16 Debug Object     */
	NSP_NORMAL,                 /* 17 Def_field        */
	NSP_NORMAL,                 /* 18 Bank_field       */
	NSP_NORMAL,                 /* 19 Index_field      */
	NSP_NORMAL,                 /* 20 Reference        */
	NSP_NORMAL,                 /* 21 Alias            */
	NSP_NORMAL,                 /* 22 Notify           */
	NSP_NORMAL,                 /* 23 Address Handler  */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 24 Resource Desc    */
	NSP_NEWSCOPE | NSP_LOCAL,   /* 25 Resource Field   */
	NSP_NORMAL,                 /* 26 Def_field_defn   */
	NSP_NORMAL,                 /* 27 Bank_field_defn  */
	NSP_NORMAL,                 /* 28 Index_field_defn */
	NSP_NORMAL,                 /* 29 If               */
	NSP_NORMAL,                 /* 30 Else             */
	NSP_NORMAL,                 /* 31 While            */
	NSP_NEWSCOPE,               /* 32 Scope            */
	NSP_LOCAL,                  /* 33 Def_any          */
	NSP_NORMAL,                 /* 34 Extra            */
	NSP_NORMAL                  /* 35 Invalid          */
};


/* Hex to ASCII conversion table */

const NATIVE_CHAR           acpi_gbl_hex_to_ascii[] =
			  {'0','1','2','3','4','5','6','7',
			  '8','9','A','B','C','D','E','F'};

/*****************************************************************************
 *
 * FUNCTION:    Acpi_ut_hex_to_ascii_char
 *
 * PARAMETERS:  Integer             - Contains the hex digit
 *              Position            - bit position of the digit within the
 *                                    integer
 *
 * RETURN:      Ascii character
 *
 * DESCRIPTION: Convert a hex digit to an ascii character
 *
 ****************************************************************************/

u8
acpi_ut_hex_to_ascii_char (
	acpi_integer            integer,
	u32                     position)
{

	return (acpi_gbl_hex_to_ascii[(integer >> position) & 0xF]);
}


/******************************************************************************
 *
 * Table globals
 *
 * NOTE: This table includes ONLY the ACPI tables that the subsystem consumes.
 * it is NOT an exhaustive list of all possible ACPI tables.  All ACPI tables
 * that are not used by the subsystem are simply ignored.
 *
 ******************************************************************************/


acpi_table_desc             acpi_gbl_acpi_tables[NUM_ACPI_TABLES];


ACPI_TABLE_SUPPORT          acpi_gbl_acpi_table_data[NUM_ACPI_TABLES] =
{
	/***********    Name,    Signature,  Signature size,    How many allowed?,   Supported?  Global typed pointer */

	/* RSDP 0 */ {RSDP_NAME, RSDP_SIG, sizeof (RSDP_SIG)-1, ACPI_TABLE_SINGLE,   AE_OK,      NULL},
	/* DSDT 1 */ {DSDT_SIG,  DSDT_SIG, sizeof (DSDT_SIG)-1, ACPI_TABLE_SINGLE,   AE_OK,      (void **) &acpi_gbl_DSDT},
	/* FADT 2 */ {FADT_SIG,  FADT_SIG, sizeof (FADT_SIG)-1, ACPI_TABLE_SINGLE,   AE_OK,      (void **) &acpi_gbl_FADT},
	/* FACS 3 */ {FACS_SIG,  FACS_SIG, sizeof (FACS_SIG)-1, ACPI_TABLE_SINGLE,   AE_OK,      (void **) &acpi_gbl_FACS},
	/* PSDT 4 */ {PSDT_SIG,  PSDT_SIG, sizeof (PSDT_SIG)-1, ACPI_TABLE_MULTIPLE, AE_OK,      NULL},
	/* SSDT 5 */ {SSDT_SIG,  SSDT_SIG, sizeof (SSDT_SIG)-1, ACPI_TABLE_MULTIPLE, AE_OK,      NULL},
	/* XSDT 6 */ {XSDT_SIG,  XSDT_SIG, sizeof (RSDT_SIG)-1, ACPI_TABLE_SINGLE,   AE_OK,      NULL},
};


#ifdef ACPI_DEBUG

/*
 * Strings and procedures used for debug only
 *
 */


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_mutex_name
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a mutex ID into a name string (Debug only)
 *
 ****************************************************************************/

NATIVE_CHAR *
acpi_ut_get_mutex_name (
	u32                     mutex_id)
{

	if (mutex_id > MAX_MTX)
	{
		return ("Invalid Mutex ID");
	}

	return (acpi_gbl_mutex_names[mutex_id]);
}


/*
 * Elements of Acpi_gbl_Ns_type_names below must match
 * one-to-one with values of acpi_object_type
 *
 * The type ACPI_TYPE_ANY (Untyped) is used as a "don't care" when searching; when
 * stored in a table it really means that we have thus far seen no evidence to
 * indicatewhat type is actually going to be stored for this entry.
 */

static const NATIVE_CHAR    acpi_gbl_bad_type[] = "UNDEFINED";
#define TYPE_NAME_LENGTH    12                           /* Maximum length of each string */

static const NATIVE_CHAR    *acpi_gbl_ns_type_names[] = /* printable names of ACPI types */
{
	/* 00 */ "Untyped",
	/* 01 */ "Integer",
	/* 02 */ "String",
	/* 03 */ "Buffer",
	/* 04 */ "Package",
	/* 05 */ "Field_unit",
	/* 06 */ "Device",
	/* 07 */ "Event",
	/* 08 */ "Method",
	/* 09 */ "Mutex",
	/* 10 */ "Region",
	/* 11 */ "Power",
	/* 12 */ "Processor",
	/* 13 */ "Thermal",
	/* 14 */ "Buffer_field",
	/* 15 */ "Ddb_handle",
	/* 16 */ "Debug_object",
	/* 17 */ "Region_field",
	/* 18 */ "Bank_field",
	/* 19 */ "Index_field",
	/* 20 */ "Reference",
	/* 21 */ "Alias",
	/* 22 */ "Notify",
	/* 23 */ "Addr_handler",
	/* 24 */ "Resource_desc",
	/* 25 */ "Resource_fld",
	/* 26 */ "Region_fld_dfn",
	/* 27 */ "Bank_fld_dfn",
	/* 28 */ "Index_fld_dfn",
	/* 29 */ "If",
	/* 30 */ "Else",
	/* 31 */ "While",
	/* 32 */ "Scope",
	/* 33 */ "Def_any",
	/* 34 */ "Extra",
	/* 35 */ "Invalid"
};


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_type_name
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a Type ID into a name string (Debug only)
 *
 ****************************************************************************/

NATIVE_CHAR *
acpi_ut_get_type_name (
	u32                     type)
{

	if (type > INTERNAL_TYPE_INVALID)
	{
		return ((NATIVE_CHAR *) acpi_gbl_bad_type);
	}

	return ((NATIVE_CHAR *) acpi_gbl_ns_type_names[type]);
}


/* Region type decoding */

const NATIVE_CHAR *acpi_gbl_region_types[NUM_REGION_TYPES] =
{
	"System_memory",
	"System_iO",
	"PCIConfig",
	"Embedded_control",
	"SMBus",
	"CMOS",
	"PCIBar_target",
};


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ut_get_region_name
 *
 * PARAMETERS:  None.
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Translate a Space ID into a name string (Debug only)
 *
 ****************************************************************************/

NATIVE_CHAR *
acpi_ut_get_region_name (
	u8                      space_id)
{

	if (space_id >= USER_REGION_BEGIN)
	{
		return ("User_defined_region");
	}

	else if (space_id >= NUM_REGION_TYPES)
	{
		return ("Invalid_space_iD");
	}

	return ((NATIVE_CHAR *) acpi_gbl_region_types[space_id]);
}


/* Data used in keeping track of fields */

const NATIVE_CHAR *acpi_gbl_FEnames[NUM_FIELD_NAMES] =
{
	"skip",
	"?access?"
};              /* FE = Field Element */


const NATIVE_CHAR *acpi_gbl_match_ops[NUM_MATCH_OPS] =
{
	"Error",
	"MTR",
	"MEQ",
	"MLE",
	"MLT",
	"MGE",
	"MGT"
};


/* Access type decoding */

const NATIVE_CHAR *acpi_gbl_access_types[NUM_ACCESS_TYPES] =
{
	"Any_acc",
	"Byte_acc",
	"Word_acc",
	"DWord_acc",
	"Block_acc",
	"SMBSend_recv_acc",
	"SMBQuick_acc"
};


/* Update rule decoding */

const NATIVE_CHAR *acpi_gbl_update_rules[NUM_UPDATE_RULES] =
{
	"Preserve",
	"Write_as_ones",
	"Write_as_zeros"
};

#endif


/*****************************************************************************
 *
 * FUNCTION:    Acpi_ut_valid_object_type
 *
 * PARAMETERS:  None.
 *
 * RETURN:      TRUE if valid object type
 *
 * DESCRIPTION: Validate an object type
 *
 ****************************************************************************/

u8
acpi_ut_valid_object_type (
	u32                     type)
{

	if (type > ACPI_TYPE_MAX)
	{
		if ((type < INTERNAL_TYPE_BEGIN) ||
			(type > INTERNAL_TYPE_MAX))
		{
			return (FALSE);
		}
	}

	return (TRUE);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ut_allocate_owner_id
 *
 * PARAMETERS:  Id_type         - Type of ID (method or table)
 *
 * DESCRIPTION: Allocate a table or method owner id
 *
 ***************************************************************************/

acpi_owner_id
acpi_ut_allocate_owner_id (
	u32                     id_type)
{
	acpi_owner_id           owner_id = 0xFFFF;


	FUNCTION_TRACE ("Ut_allocate_owner_id");


	acpi_ut_acquire_mutex (ACPI_MTX_CACHES);

	switch (id_type)
	{
	case OWNER_TYPE_TABLE:

		owner_id = acpi_gbl_next_table_owner_id;
		acpi_gbl_next_table_owner_id++;

		if (acpi_gbl_next_table_owner_id == FIRST_METHOD_ID)
		{
			acpi_gbl_next_table_owner_id = FIRST_TABLE_ID;
		}
		break;


	case OWNER_TYPE_METHOD:

		owner_id = acpi_gbl_next_method_owner_id;
		acpi_gbl_next_method_owner_id++;

		if (acpi_gbl_next_method_owner_id == FIRST_TABLE_ID)
		{
			acpi_gbl_next_method_owner_id = FIRST_METHOD_ID;
		}
		break;
	}


	acpi_ut_release_mutex (ACPI_MTX_CACHES);

	return_VALUE (owner_id);
}


/****************************************************************************
 *
 * FUNCTION:    Acpi_ut_init_globals
 *
 * PARAMETERS:  none
 *
 * DESCRIPTION: Init library globals.  All globals that require specific
 *              initialization should be initialized here!
 *
 ***************************************************************************/

void
acpi_ut_init_globals (
	void)
{
	u32                     i;


	FUNCTION_TRACE ("Ut_init_globals");

	/* Memory allocation and cache lists */

	MEMSET (acpi_gbl_memory_lists, 0, sizeof (ACPI_MEMORY_LIST) * ACPI_NUM_MEM_LISTS);

	acpi_gbl_memory_lists[ACPI_MEM_LIST_STATE].link_offset      = (u16) (NATIVE_UINT) &(((acpi_generic_state *) NULL)->common.next);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE].link_offset     = (u16) (NATIVE_UINT) &(((acpi_parse_object *) NULL)->next);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE_EXT].link_offset = (u16) (NATIVE_UINT) &(((acpi_parse2_object *) NULL)->next);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_OPERAND].link_offset    = (u16) (NATIVE_UINT) &(((acpi_operand_object *) NULL)->cache.next);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_WALK].link_offset       = (u16) (NATIVE_UINT) &(((acpi_walk_state *) NULL)->next);

	acpi_gbl_memory_lists[ACPI_MEM_LIST_NSNODE].object_size     = sizeof (acpi_namespace_node);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_STATE].object_size      = sizeof (acpi_generic_state);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE].object_size     = sizeof (acpi_parse_object);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE_EXT].object_size = sizeof (acpi_parse2_object);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_OPERAND].object_size    = sizeof (acpi_operand_object);
	acpi_gbl_memory_lists[ACPI_MEM_LIST_WALK].object_size       = sizeof (acpi_walk_state);

	acpi_gbl_memory_lists[ACPI_MEM_LIST_STATE].max_cache_depth  = MAX_STATE_CACHE_DEPTH;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE].max_cache_depth = MAX_PARSE_CACHE_DEPTH;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE_EXT].max_cache_depth = MAX_EXTPARSE_CACHE_DEPTH;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_OPERAND].max_cache_depth = MAX_OBJECT_CACHE_DEPTH;
	acpi_gbl_memory_lists[ACPI_MEM_LIST_WALK].max_cache_depth   = MAX_WALK_CACHE_DEPTH;

	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_GLOBAL].list_name    = "Global Memory Allocation");
	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_NSNODE].list_name    = "Namespace Nodes");
	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_STATE].list_name     = "State Object Cache");
	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE].list_name    = "Parse Node Cache");
	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_PSNODE_EXT].list_name = "Extended Parse Node Cache");
	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_OPERAND].list_name   = "Operand Object Cache");
	ACPI_MEM_TRACKING (acpi_gbl_memory_lists[ACPI_MEM_LIST_WALK].list_name      = "Tree Walk Node Cache");

	/* ACPI table structure */

	for (i = 0; i < NUM_ACPI_TABLES; i++)
	{
		acpi_gbl_acpi_tables[i].prev        = &acpi_gbl_acpi_tables[i];
		acpi_gbl_acpi_tables[i].next        = &acpi_gbl_acpi_tables[i];
		acpi_gbl_acpi_tables[i].pointer     = NULL;
		acpi_gbl_acpi_tables[i].length      = 0;
		acpi_gbl_acpi_tables[i].allocation  = ACPI_MEM_NOT_ALLOCATED;
		acpi_gbl_acpi_tables[i].count       = 0;
	}


	/* Address Space handler array */

	for (i = 0; i < ACPI_NUM_ADDRESS_SPACES; i++)
	{
		acpi_gbl_address_spaces[i].handler  = NULL;
		acpi_gbl_address_spaces[i].context  = NULL;
	}

	/* Mutex locked flags */

	for (i = 0; i < NUM_MTX; i++)
	{
		acpi_gbl_acpi_mutex_info[i].mutex   = NULL;
		acpi_gbl_acpi_mutex_info[i].owner_id = ACPI_MUTEX_NOT_ACQUIRED;
		acpi_gbl_acpi_mutex_info[i].use_count = 0;
	}

	/* Global notify handlers */

	acpi_gbl_sys_notify.handler         = NULL;
	acpi_gbl_drv_notify.handler         = NULL;

	/* Global "typed" ACPI table pointers */

	acpi_gbl_RSDP                       = NULL;
	acpi_gbl_XSDT                       = NULL;
	acpi_gbl_FACS                       = NULL;
	acpi_gbl_FADT                       = NULL;
	acpi_gbl_DSDT                       = NULL;

	/* Global Lock support */

	acpi_gbl_global_lock_acquired       = FALSE;
	acpi_gbl_global_lock_thread_count   = 0;

	/* Miscellaneous variables */

	acpi_gbl_system_flags               = 0;
	acpi_gbl_startup_flags              = 0;
	acpi_gbl_rsdp_original_location     = 0;
	acpi_gbl_cm_single_step             = FALSE;
	acpi_gbl_db_terminate_threads       = FALSE;
	acpi_gbl_shutdown                   = FALSE;
	acpi_gbl_ns_lookup_count            = 0;
	acpi_gbl_ps_find_count              = 0;
	acpi_gbl_acpi_hardware_present      = TRUE;
	acpi_gbl_next_table_owner_id        = FIRST_TABLE_ID;
	acpi_gbl_next_method_owner_id       = FIRST_METHOD_ID;
	acpi_gbl_debugger_configuration     = DEBUGGER_THREADING;

	/* Hardware oriented */

	acpi_gbl_gpe0enable_register_save   = NULL;
	acpi_gbl_gpe1_enable_register_save  = NULL;
	acpi_gbl_original_mode              = SYS_MODE_UNKNOWN;   /*  original ACPI/legacy mode   */
	acpi_gbl_gpe_registers              = NULL;
	acpi_gbl_gpe_info                   = NULL;

	/* Namespace */

	acpi_gbl_root_node                  = NULL;

	acpi_gbl_root_node_struct.name      = ACPI_ROOT_NAME;
	acpi_gbl_root_node_struct.data_type = ACPI_DESC_TYPE_NAMED;
	acpi_gbl_root_node_struct.type      = ACPI_TYPE_ANY;
	acpi_gbl_root_node_struct.child     = NULL;
	acpi_gbl_root_node_struct.peer      = NULL;
	acpi_gbl_root_node_struct.object    = NULL;
	acpi_gbl_root_node_struct.flags     = ANOBJ_END_OF_PEER_LIST;


#ifdef ACPI_DEBUG
	acpi_gbl_lowest_stack_pointer       = ACPI_UINT32_MAX;
#endif

	return_VOID;
}


