/******************************************************************************
 *
 * Name: aclocal.h - Internal data types used across the ACPI subsystem
 *       $Revision: 176 $
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

#ifndef __ACLOCAL_H__
#define __ACLOCAL_H__


#define WAIT_FOREVER                    ((u32) -1)

typedef void*                           acpi_mutex;
typedef u32                             ACPI_MUTEX_HANDLE;


/* Total number of aml opcodes defined */

#define AML_NUM_OPCODES                 0x7E


/*****************************************************************************
 *
 * Mutex typedefs and structs
 *
 ****************************************************************************/


/*
 * Predefined handles for the mutex objects used within the subsystem
 * All mutex objects are automatically created by Acpi_ut_mutex_initialize.
 *
 * The acquire/release ordering protocol is implied via this list.  Mutexes
 * with a lower value must be acquired before mutexes with a higher value.
 *
 * NOTE: any changes here must be reflected in the Acpi_gbl_Mutex_names table also!
 */

#define ACPI_MTX_EXECUTE                0
#define ACPI_MTX_INTERPRETER            1
#define ACPI_MTX_PARSER                 2
#define ACPI_MTX_DISPATCHER             3
#define ACPI_MTX_TABLES                 4
#define ACPI_MTX_OP_REGIONS             5
#define ACPI_MTX_NAMESPACE              6
#define ACPI_MTX_EVENTS                 7
#define ACPI_MTX_HARDWARE               8
#define ACPI_MTX_CACHES                 9
#define ACPI_MTX_MEMORY                 10
#define ACPI_MTX_DEBUG_CMD_COMPLETE     11
#define ACPI_MTX_DEBUG_CMD_READY        12

#define MAX_MTX                         12
#define NUM_MTX                         MAX_MTX+1


#if defined(ACPI_DEBUG_OUTPUT) || defined(ACPI_DEBUGGER)
#ifdef DEFINE_ACPI_GLOBALS

/* Names for the mutexes used in the subsystem */

static NATIVE_CHAR          *acpi_gbl_mutex_names[] =
{
	"ACPI_MTX_Execute",
	"ACPI_MTX_Interpreter",
	"ACPI_MTX_Parser",
	"ACPI_MTX_Dispatcher",
	"ACPI_MTX_Tables",
	"ACPI_MTX_Op_regions",
	"ACPI_MTX_Namespace",
	"ACPI_MTX_Events",
	"ACPI_MTX_Hardware",
	"ACPI_MTX_Caches",
	"ACPI_MTX_Memory",
	"ACPI_MTX_Debug_cmd_complete",
	"ACPI_MTX_Debug_cmd_ready",
};

#endif
#endif


/* Table for the global mutexes */

typedef struct acpi_mutex_info
{
	acpi_mutex                  mutex;
	u32                         use_count;
	u32                         owner_id;

} acpi_mutex_info;

/* This owner ID means that the mutex is not in use (unlocked) */

#define ACPI_MUTEX_NOT_ACQUIRED         (u32) (-1)


/* Lock flag parameter for various interfaces */

#define ACPI_MTX_DO_NOT_LOCK            0
#define ACPI_MTX_LOCK                   1


typedef u16                             acpi_owner_id;
#define ACPI_OWNER_TYPE_TABLE           0x0
#define ACPI_OWNER_TYPE_METHOD          0x1
#define ACPI_FIRST_METHOD_ID            0x0000
#define ACPI_FIRST_TABLE_ID             0x8000

/* TBD: [Restructure] get rid of the need for this! */

#define TABLE_ID_DSDT                   (acpi_owner_id) 0x8000


/* Field access granularities */

#define ACPI_FIELD_BYTE_GRANULARITY     1
#define ACPI_FIELD_WORD_GRANULARITY     2
#define ACPI_FIELD_DWORD_GRANULARITY    4
#define ACPI_FIELD_QWORD_GRANULARITY    8

/*****************************************************************************
 *
 * Namespace typedefs and structs
 *
 ****************************************************************************/


/* Operational modes of the AML interpreter/scanner */

typedef enum
{
	ACPI_IMODE_LOAD_PASS1               = 0x01,
	ACPI_IMODE_LOAD_PASS2               = 0x02,
	ACPI_IMODE_EXECUTE                  = 0x0E

} acpi_interpreter_mode;


/*
 * The Node describes a named object that appears in the AML
 * An Acpi_node is used to store Nodes.
 *
 * Data_type is used to differentiate between internal descriptors, and MUST
 * be the first byte in this structure.
 */

typedef union acpi_name_union
{
	u32                     integer;
	char                    ascii[4];
} ACPI_NAME_UNION;

typedef struct acpi_node
{
	u8                      descriptor;     /* Used to differentiate object descriptor types */
	u8                      type;           /* Type associated with this name */
	u16                     owner_id;
	ACPI_NAME_UNION         name;           /* ACPI Name, always 4 chars per ACPI spec */


	union acpi_operand_obj  *object;        /* Pointer to attached ACPI object (optional) */
	struct acpi_node        *child;         /* first child */
	struct acpi_node        *peer;          /* Next peer*/
	u16                     reference_count; /* Current count of references and children */
	u8                      flags;

} acpi_namespace_node;


#define ACPI_ENTRY_NOT_FOUND            NULL


/* Node flags */

#define ANOBJ_RESERVED                  0x01
#define ANOBJ_END_OF_PEER_LIST          0x02
#define ANOBJ_DATA_WIDTH_32             0x04     /* Parent table is 64-bits */
#define ANOBJ_METHOD_ARG                0x08
#define ANOBJ_METHOD_LOCAL              0x10
#define ANOBJ_METHOD_NO_RETVAL          0x20
#define ANOBJ_METHOD_SOME_NO_RETVAL     0x40

#define ANOBJ_IS_BIT_OFFSET             0x80


/*
 * ACPI Table Descriptor.  One per ACPI table
 */
typedef struct acpi_table_desc
{
	struct acpi_table_desc  *prev;
	struct acpi_table_desc  *next;
	struct acpi_table_desc  *installed_desc;
	acpi_table_header       *pointer;
	void                    *base_pointer;
	u8                      *aml_start;
	u64                     physical_address;
	u32                     aml_length;
	ACPI_SIZE               length;
	u32                     count;
	acpi_owner_id           table_id;
	u8                      type;
	u8                      allocation;
	u8                      loaded_into_namespace;

} acpi_table_desc;


typedef struct
{
	NATIVE_CHAR             *search_for;
	acpi_handle             *list;
	u32                     *count;

} acpi_find_context;


typedef struct
{
	acpi_namespace_node     *node;
} acpi_ns_search_data;


/*
 * Predefined Namespace items
 */
typedef struct
{
	NATIVE_CHAR             *name;
	u8                      type;
	NATIVE_CHAR             *val;

} acpi_predefined_names;


/* Object types used during package copies */


#define ACPI_COPY_TYPE_SIMPLE           0
#define ACPI_COPY_TYPE_PACKAGE          1

/* Info structure used to convert external<->internal namestrings */

typedef struct acpi_namestring_info
{
	NATIVE_CHAR             *external_name;
	NATIVE_CHAR             *next_external_char;
	NATIVE_CHAR             *internal_name;
	u32                     length;
	u32                     num_segments;
	u32                     num_carats;
	u8                      fully_qualified;

} acpi_namestring_info;


/* Field creation info */

typedef struct
{
	acpi_namespace_node     *region_node;
	acpi_namespace_node     *field_node;
	acpi_namespace_node     *register_node;
	acpi_namespace_node     *data_register_node;
	u32                     bank_value;
	u32                     field_bit_position;
	u32                     field_bit_length;
	u8                      field_flags;
	u8                      attribute;
	u8                      field_type;

} ACPI_CREATE_FIELD_INFO;


/*****************************************************************************
 *
 * Event typedefs and structs
 *
 ****************************************************************************/

/* Information about each GPE register block */

typedef struct
{
	u8                      address_space_id;
	acpi_generic_address    *block_address;
	u16                     register_count;
	u8                      block_base_number;

} ACPI_GPE_BLOCK_INFO;

/* Information about a particular GPE register pair */

typedef struct
{
	acpi_generic_address    status_address; /* Address of status reg */
	acpi_generic_address    enable_address; /* Address of enable reg */
	u8                      status;         /* Current value of status reg */
	u8                      enable;         /* Current value of enable reg */
	u8                      wake_enable;    /* Mask of bits to keep enabled when sleeping */
	u8                      base_gpe_number; /* Base GPE number for this register */

} ACPI_GPE_REGISTER_INFO;


#define ACPI_GPE_LEVEL_TRIGGERED        1
#define ACPI_GPE_EDGE_TRIGGERED         2


/* Information about each particular GPE level */

typedef struct
{
	acpi_handle             method_handle;  /* Method handle for direct (fast) execution */
	acpi_gpe_handler        handler;        /* Address of handler, if any */
	void                    *context;       /* Context to be passed to handler */
	u8                      type;           /* Level or Edge */
	u8                      bit_mask;


} ACPI_GPE_NUMBER_INFO;


typedef struct
{
	u8                      number_index;

} ACPI_GPE_INDEX_INFO;

/* Information about each particular fixed event */

typedef struct
{
	acpi_event_handler      handler;        /* Address of handler. */
	void                    *context;       /* Context to be passed to handler */

} ACPI_FIXED_EVENT_HANDLER;


typedef struct
{
	u8                      status_register_id;
	u8                      enable_register_id;
	u16                     status_bit_mask;
	u16                     enable_bit_mask;

} acpi_fixed_event_info;

/* Information used during field processing */

typedef struct
{
	u8                      skip_field;
	u8                      field_flag;
	u32                     pkg_length;

} acpi_field_info;


/*****************************************************************************
 *
 * Generic "state" object for stacks
 *
 ****************************************************************************/


#define ACPI_CONTROL_NORMAL                  0xC0
#define ACPI_CONTROL_CONDITIONAL_EXECUTING   0xC1
#define ACPI_CONTROL_PREDICATE_EXECUTING     0xC2
#define ACPI_CONTROL_PREDICATE_FALSE         0xC3
#define ACPI_CONTROL_PREDICATE_TRUE          0xC4


/* Forward declarations */
struct acpi_walk_state;
struct acpi_obj_mutex;
union acpi_parse_obj;


#define ACPI_STATE_COMMON                  /* Two 32-bit fields and a pointer */\
	u8                      data_type;          /* To differentiate various internal objs */\
	u8                      flags;      \
	u16                     value;      \
	u16                     state;      \
	u16                     reserved;   \
	void                    *next;      \

typedef struct acpi_common_state
{
	ACPI_STATE_COMMON
} acpi_common_state;


/*
 * Update state - used to traverse complex objects such as packages
 */
typedef struct acpi_update_state
{
	ACPI_STATE_COMMON
	union acpi_operand_obj  *object;

} acpi_update_state;


/*
 * Pkg state - used to traverse nested package structures
 */
typedef struct acpi_pkg_state
{
	ACPI_STATE_COMMON
	union acpi_operand_obj  *source_object;
	union acpi_operand_obj  *dest_object;
	struct acpi_walk_state  *walk_state;
	void                    *this_target_obj;
	u32                     num_packages;
	u16                     index;

} acpi_pkg_state;


/*
 * Control state - one per if/else and while constructs.
 * Allows nesting of these constructs
 */
typedef struct acpi_control_state
{
	ACPI_STATE_COMMON
	union acpi_parse_obj    *predicate_op;
	u8                      *aml_predicate_start;   /* Start of if/while predicate */
	u8                      *package_end;           /* End of if/while block */
	u16                     opcode;

} acpi_control_state;


/*
 * Scope state - current scope during namespace lookups
 */
typedef struct acpi_scope_state
{
	ACPI_STATE_COMMON
	acpi_namespace_node     *node;

} acpi_scope_state;


typedef struct acpi_pscope_state
{
	ACPI_STATE_COMMON
	union acpi_parse_obj    *op;                    /* current op being parsed */
	u8                      *arg_end;               /* current argument end */
	u8                      *pkg_end;               /* current package end */
	u32                     arg_list;               /* next argument to parse */
	u32                     arg_count;              /* Number of fixed arguments */

} acpi_pscope_state;


/*
 * Thread state - one per thread across multiple walk states.  Multiple walk
 * states are created when there are nested control methods executing.
 */
typedef struct acpi_thread_state
{
	ACPI_STATE_COMMON
	struct acpi_walk_state  *walk_state_list;       /* Head of list of Walk_states for this thread */
	union acpi_operand_obj  *acquired_mutex_list;   /* List of all currently acquired mutexes */
	u32                     thread_id;              /* Running thread ID */
	u16                     current_sync_level;     /* Mutex Sync (nested acquire) level */

} ACPI_THREAD_STATE;


/*
 * Result values - used to accumulate the results of nested
 * AML arguments
 */
typedef struct acpi_result_values
{
	ACPI_STATE_COMMON
	union acpi_operand_obj  *obj_desc [OBJ_NUM_OPERANDS];
	u8                      num_results;
	u8                      last_insert;

} acpi_result_values;


typedef
acpi_status (*acpi_parse_downwards) (
	struct acpi_walk_state  *walk_state,
	union acpi_parse_obj    **out_op);

typedef
acpi_status (*acpi_parse_upwards) (
	struct acpi_walk_state  *walk_state);


/*
 * Notify info - used to pass info to the deferred notify
 * handler/dispatcher.
 */
typedef struct acpi_notify_info
{
	ACPI_STATE_COMMON
	acpi_namespace_node     *node;
	union acpi_operand_obj  *handler_obj;

} acpi_notify_info;


/* Generic state is union of structs above */

typedef union acpi_gen_state
{
	acpi_common_state       common;
	acpi_control_state      control;
	acpi_update_state       update;
	acpi_scope_state        scope;
	acpi_pscope_state       parse_scope;
	acpi_pkg_state          pkg;
	ACPI_THREAD_STATE       thread;
	acpi_result_values      results;
	acpi_notify_info        notify;

} acpi_generic_state;


/*****************************************************************************
 *
 * Interpreter typedefs and structs
 *
 ****************************************************************************/

typedef
acpi_status (*ACPI_EXECUTE_OP) (
	struct acpi_walk_state  *walk_state);


/*****************************************************************************
 *
 * Parser typedefs and structs
 *
 ****************************************************************************/

/*
 * AML opcode, name, and argument layout
 */
typedef struct acpi_opcode_info
{
#if defined(ACPI_DISASSEMBLER) || defined(ACPI_DEBUG_OUTPUT)
	NATIVE_CHAR             *name;          /* Opcode name (disassembler/debug only) */
#endif
	u32                     parse_args;     /* Grammar/Parse time arguments */
	u32                     runtime_args;   /* Interpret time arguments */
	u32                     flags;          /* Misc flags */
	u8                      object_type;    /* Corresponding internal object type */
	u8                      class;          /* Opcode class */
	u8                      type;           /* Opcode type */

} acpi_opcode_info;


typedef union acpi_parse_val
{
	acpi_integer            integer;        /* integer constant (Up to 64 bits) */
	uint64_struct           integer64;      /* Structure overlay for 2 32-bit Dwords */
	u32                     integer32;      /* integer constant, 32 bits only */
	u16                     integer16;      /* integer constant, 16 bits only */
	u8                      integer8;       /* integer constant, 8 bits only */
	u32                     size;           /* bytelist or field size */
	NATIVE_CHAR             *string;        /* NULL terminated string */
	u8                      *buffer;        /* buffer or string */
	NATIVE_CHAR             *name;          /* NULL terminated string */
	union acpi_parse_obj    *arg;           /* arguments and contained ops */

} acpi_parse_value;


#define ACPI_PARSE_COMMON \
	u8                      data_type;      /* To differentiate various internal objs */\
	u8                      flags;          /* Type of Op */\
	u16                     aml_opcode;     /* AML opcode */\
	u32                     aml_offset;     /* offset of declaration in AML */\
	union acpi_parse_obj    *parent;        /* parent op */\
	union acpi_parse_obj    *next;          /* next op */\
	ACPI_DISASM_ONLY_MEMBERS (\
	u8                      disasm_flags;   /* Used during AML disassembly */\
	u8                      disasm_opcode;  /* Subtype used for disassembly */\
	NATIVE_CHAR             aml_op_name[16]) /* op name (debug only) */\
			  /* NON-DEBUG members below: */\
	acpi_namespace_node     *node;          /* for use by interpreter */\
	acpi_parse_value        value;          /* Value or args associated with the opcode */\

#define ACPI_DASM_BUFFER        0x00
#define ACPI_DASM_RESOURCE      0x01
#define ACPI_DASM_STRING        0x02
#define ACPI_DASM_UNICODE       0x03
#define ACPI_DASM_EISAID        0x04
#define ACPI_DASM_MATCHOP       0x05

/*
 * generic operation (for example:  If, While, Store)
 */
typedef struct acpi_parseobj_common
{
	ACPI_PARSE_COMMON
} ACPI_PARSE_OBJ_COMMON;


/*
 * Extended Op for named ops (Scope, Method, etc.), deferred ops (Methods and Op_regions),
 * and bytelists.
 */
typedef struct acpi_parseobj_named
{
	ACPI_PARSE_COMMON
	u8                      *path;
	u8                      *data;          /* AML body or bytelist data */
	u32                     length;         /* AML length */
	u32                     name;           /* 4-byte name or zero if no name */

} ACPI_PARSE_OBJ_NAMED;


/* The parse node is the fundamental element of the parse tree */

typedef struct acpi_parseobj_asl
{
	ACPI_PARSE_COMMON

	union acpi_parse_obj        *child;


	union acpi_parse_obj        *parent_method;
	char                        *filename;
	char                        *external_name;
	char                        *namepath;
	u32                         extra_value;
	u32                         column;
	u32                         line_number;
	u32                         logical_line_number;
	u32                         logical_byte_offset;
	u32                         end_line;
	u32                         end_logical_line;
	u32                         acpi_btype;
	u32                         aml_length;
	u32                         aml_subtree_length;
	u32                         final_aml_length;
	u32                         final_aml_offset;
	u32                         compile_flags;
	u16                         parse_opcode;
	u8                          aml_opcode_length;
	u8                          aml_pkg_len_bytes;
	u8                          extra;
	char                        parse_op_name[12];

} ACPI_PARSE_OBJ_ASL;


typedef union acpi_parse_obj
{
	ACPI_PARSE_OBJ_COMMON       common;
	ACPI_PARSE_OBJ_NAMED        named;
	ACPI_PARSE_OBJ_ASL          asl;

} acpi_parse_object;


/*
 * Parse state - one state per parser invocation and each control
 * method.
 */
typedef struct acpi_parse_state
{
	u32                     aml_size;
	u8                      *aml_start;     /* first AML byte */
	u8                      *aml;           /* next AML byte */
	u8                      *aml_end;       /* (last + 1) AML byte */
	u8                      *pkg_start;     /* current package begin */
	u8                      *pkg_end;       /* current package end */
	union acpi_parse_obj    *start_op;      /* root of parse tree */
	struct acpi_node        *start_node;
	union acpi_gen_state    *scope;         /* current scope */
	union acpi_parse_obj    *start_scope;

} acpi_parse_state;


/* Parse object flags */

#define ACPI_PARSEOP_GENERIC                    0x01
#define ACPI_PARSEOP_NAMED                      0x02
#define ACPI_PARSEOP_DEFERRED                   0x04
#define ACPI_PARSEOP_BYTELIST                   0x08
#define ACPI_PARSEOP_IN_CACHE                   0x80

/* Parse object Disasm_flags */

#define ACPI_PARSEOP_IGNORE                     0x01
#define ACPI_PARSEOP_PARAMLIST                  0x02
#define ACPI_PARSEOP_EMPTY_TERMLIST             0x04
#define ACPI_PARSEOP_SPECIAL                    0x10


/*****************************************************************************
 *
 * Hardware (ACPI registers) and PNP
 *
 ****************************************************************************/

#define PCI_ROOT_HID_STRING         "PNP0A03"

typedef struct
{
	u8                      parent_register;
	u8                      bit_position;
	u16                     access_bit_mask;

} ACPI_BIT_REGISTER_INFO;


/*
 * Register IDs
 * These are the full ACPI registers
 */
#define ACPI_REGISTER_PM1_STATUS                0x01
#define ACPI_REGISTER_PM1_ENABLE                0x02
#define ACPI_REGISTER_PM1_CONTROL               0x03
#define ACPI_REGISTER_PM1A_CONTROL              0x04
#define ACPI_REGISTER_PM1B_CONTROL              0x05
#define ACPI_REGISTER_PM2_CONTROL               0x06
#define ACPI_REGISTER_PM_TIMER                  0x07
#define ACPI_REGISTER_PROCESSOR_BLOCK           0x08
#define ACPI_REGISTER_SMI_COMMAND_BLOCK         0x09


/* Masks used to access the Bit_registers */

#define ACPI_BITMASK_TIMER_STATUS               0x0001
#define ACPI_BITMASK_BUS_MASTER_STATUS          0x0010
#define ACPI_BITMASK_GLOBAL_LOCK_STATUS         0x0020
#define ACPI_BITMASK_POWER_BUTTON_STATUS        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_STATUS        0x0200
#define ACPI_BITMASK_RT_CLOCK_STATUS            0x0400
#define ACPI_BITMASK_WAKE_STATUS                0x8000

#define ACPI_BITMASK_ALL_FIXED_STATUS           (ACPI_BITMASK_TIMER_STATUS          | \
			 ACPI_BITMASK_BUS_MASTER_STATUS     | \
			 ACPI_BITMASK_GLOBAL_LOCK_STATUS    | \
			 ACPI_BITMASK_POWER_BUTTON_STATUS   | \
			 ACPI_BITMASK_SLEEP_BUTTON_STATUS   | \
			 ACPI_BITMASK_RT_CLOCK_STATUS       | \
			 ACPI_BITMASK_WAKE_STATUS)

#define ACPI_BITMASK_TIMER_ENABLE               0x0001
#define ACPI_BITMASK_GLOBAL_LOCK_ENABLE         0x0020
#define ACPI_BITMASK_POWER_BUTTON_ENABLE        0x0100
#define ACPI_BITMASK_SLEEP_BUTTON_ENABLE        0x0200
#define ACPI_BITMASK_RT_CLOCK_ENABLE            0x0400

#define ACPI_BITMASK_SCI_ENABLE                 0x0001
#define ACPI_BITMASK_BUS_MASTER_RLD             0x0002
#define ACPI_BITMASK_GLOBAL_LOCK_RELEASE        0x0004
#define ACPI_BITMASK_SLEEP_TYPE_X               0x1C00
#define ACPI_BITMASK_SLEEP_ENABLE               0x2000

#define ACPI_BITMASK_ARB_DISABLE                0x0001


/* Raw bit position of each Bit_register */

#define ACPI_BITPOSITION_TIMER_STATUS           0x00
#define ACPI_BITPOSITION_BUS_MASTER_STATUS      0x04
#define ACPI_BITPOSITION_GLOBAL_LOCK_STATUS     0x05
#define ACPI_BITPOSITION_POWER_BUTTON_STATUS    0x08
#define ACPI_BITPOSITION_SLEEP_BUTTON_STATUS    0x09
#define ACPI_BITPOSITION_RT_CLOCK_STATUS        0x0A
#define ACPI_BITPOSITION_WAKE_STATUS            0x0F

#define ACPI_BITPOSITION_TIMER_ENABLE           0x00
#define ACPI_BITPOSITION_GLOBAL_LOCK_ENABLE     0x05
#define ACPI_BITPOSITION_POWER_BUTTON_ENABLE    0x08
#define ACPI_BITPOSITION_SLEEP_BUTTON_ENABLE    0x09
#define ACPI_BITPOSITION_RT_CLOCK_ENABLE        0x0A

#define ACPI_BITPOSITION_SCI_ENABLE             0x00
#define ACPI_BITPOSITION_BUS_MASTER_RLD         0x01
#define ACPI_BITPOSITION_GLOBAL_LOCK_RELEASE    0x02
#define ACPI_BITPOSITION_SLEEP_TYPE_X           0x0A
#define ACPI_BITPOSITION_SLEEP_ENABLE           0x0D

#define ACPI_BITPOSITION_ARB_DISABLE            0x00


/*****************************************************************************
 *
 * Resource descriptors
 *
 ****************************************************************************/


/* Resource_type values */

#define ACPI_RESOURCE_TYPE_MEMORY_RANGE         0
#define ACPI_RESOURCE_TYPE_IO_RANGE             1
#define ACPI_RESOURCE_TYPE_BUS_NUMBER_RANGE     2

/* Resource descriptor types and masks */

#define ACPI_RDESC_TYPE_LARGE                   0x80
#define ACPI_RDESC_TYPE_SMALL                   0x00

#define ACPI_RDESC_TYPE_MASK                    0x80
#define ACPI_RDESC_SMALL_MASK                   0x78 /* Only bits 6:3 contain the type */


/*
 * Small resource descriptor types
 * Note: The 3 length bits (2:0) must be zero
 */
#define ACPI_RDESC_TYPE_IRQ_FORMAT              0x20
#define ACPI_RDESC_TYPE_DMA_FORMAT              0x28
#define ACPI_RDESC_TYPE_START_DEPENDENT         0x30
#define ACPI_RDESC_TYPE_END_DEPENDENT           0x38
#define ACPI_RDESC_TYPE_IO_PORT                 0x40
#define ACPI_RDESC_TYPE_FIXED_IO_PORT           0x48
#define ACPI_RDESC_TYPE_SMALL_VENDOR            0x70
#define ACPI_RDESC_TYPE_END_TAG                 0x78

/*
 * Large resource descriptor types
 */

#define ACPI_RDESC_TYPE_MEMORY_24               0x81
#define ACPI_RDESC_TYPE_GENERAL_REGISTER        0x82
#define ACPI_RDESC_TYPE_LARGE_VENDOR            0x84
#define ACPI_RDESC_TYPE_MEMORY_32               0x85
#define ACPI_RDESC_TYPE_FIXED_MEMORY_32         0x86
#define ACPI_RDESC_TYPE_DWORD_ADDRESS_SPACE     0x87
#define ACPI_RDESC_TYPE_WORD_ADDRESS_SPACE      0x88
#define ACPI_RDESC_TYPE_EXTENDED_XRUPT          0x89
#define ACPI_RDESC_TYPE_QWORD_ADDRESS_SPACE     0x8A


/* String version of device HIDs and UIDs */

#define ACPI_DEVICE_ID_LENGTH                   0x09

typedef struct
{
	char            buffer[ACPI_DEVICE_ID_LENGTH];

} acpi_device_id;


/*****************************************************************************
 *
 * Miscellaneous
 *
 ****************************************************************************/

#define ACPI_ASCII_ZERO                      0x30


/*****************************************************************************
 *
 * Debugger
 *
 ****************************************************************************/

typedef struct dbmethodinfo
{
	acpi_handle             thread_gate;
	NATIVE_CHAR             *name;
	NATIVE_CHAR             **args;
	u32                     flags;
	u32                     num_loops;
	NATIVE_CHAR             pathname[128];

} acpi_db_method_info;


#define ACPI_DB_REDIRECTABLE_OUTPUT  0x01
#define ACPI_DB_CONSOLE_OUTPUT       0x02
#define ACPI_DB_DUPLICATE_OUTPUT     0x03


/*****************************************************************************
 *
 * Debug
 *
 ****************************************************************************/

typedef struct
{
	u32                     component_id;
	NATIVE_CHAR             *proc_name;
	NATIVE_CHAR             *module_name;

} acpi_debug_print_info;


/* Entry for a memory allocation (debug only) */

#define ACPI_MEM_MALLOC                      0
#define ACPI_MEM_CALLOC                      1
#define ACPI_MAX_MODULE_NAME                 16

#define ACPI_COMMON_DEBUG_MEM_HEADER \
	struct acpi_debug_mem_block *previous; \
	struct acpi_debug_mem_block *next; \
	u32                         size; \
	u32                         component; \
	u32                         line; \
	NATIVE_CHAR                 module[ACPI_MAX_MODULE_NAME]; \
	u8                          alloc_type;

typedef struct
{
	ACPI_COMMON_DEBUG_MEM_HEADER

} acpi_debug_mem_header;

typedef struct acpi_debug_mem_block
{
	ACPI_COMMON_DEBUG_MEM_HEADER
	u64                         user_space;

} acpi_debug_mem_block;


#define ACPI_MEM_LIST_GLOBAL            0
#define ACPI_MEM_LIST_NSNODE            1

#define ACPI_MEM_LIST_FIRST_CACHE_LIST  2
#define ACPI_MEM_LIST_STATE             2
#define ACPI_MEM_LIST_PSNODE            3
#define ACPI_MEM_LIST_PSNODE_EXT        4
#define ACPI_MEM_LIST_OPERAND           5
#define ACPI_MEM_LIST_WALK              6
#define ACPI_MEM_LIST_MAX               6
#define ACPI_NUM_MEM_LISTS              7


typedef struct
{
	void                        *list_head;
	u16                         link_offset;
	u16                         max_cache_depth;
	u16                         cache_depth;
	u16                         object_size;

#ifdef ACPI_DBG_TRACK_ALLOCATIONS

	/* Statistics for debug memory tracking only */

	u32                         total_allocated;
	u32                         total_freed;
	u32                         current_total_size;
	u32                         cache_requests;
	u32                         cache_hits;
	char                        *list_name;
#endif

} ACPI_MEMORY_LIST;


#endif /* __ACLOCAL_H__ */
