/******************************************************************************
 *
 * Name: aclocal.h - Internal data types used across the ACPI subsystem
 *       $Revision: 138 $
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

#ifndef __ACLOCAL_H__
#define __ACLOCAL_H__


#define WAIT_FOREVER                    ((u32) -1)

typedef void*                           acpi_mutex;
typedef u32                             ACPI_MUTEX_HANDLE;


#define ACPI_MEMORY_MODE                0x01
#define ACPI_LOGICAL_ADDRESSING         0x00
#define ACPI_PHYSICAL_ADDRESSING        0x01

/* Object descriptor types */

#define ACPI_CACHED_OBJECT              0x11    /* ORed in when object is cached */
#define ACPI_DESC_TYPE_STATE            0x20
#define ACPI_DESC_TYPE_STATE_UPDATE     0x21
#define ACPI_DESC_TYPE_STATE_PACKAGE    0x22
#define ACPI_DESC_TYPE_STATE_CONTROL    0x23
#define ACPI_DESC_TYPE_STATE_RPSCOPE    0x24
#define ACPI_DESC_TYPE_STATE_PSCOPE     0x25
#define ACPI_DESC_TYPE_STATE_WSCOPE     0x26
#define ACPI_DESC_TYPE_STATE_RESULT     0x27
#define ACPI_DESC_TYPE_STATE_NOTIFY     0x28
#define ACPI_DESC_TYPE_WALK             0x44
#define ACPI_DESC_TYPE_PARSER           0x66
#define ACPI_DESC_TYPE_INTERNAL         0x88
#define ACPI_DESC_TYPE_NAMED            0xAA


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


#if defined(ACPI_DEBUG) || defined(ENABLE_DEBUGGER)
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
#define OWNER_TYPE_TABLE                0x0
#define OWNER_TYPE_METHOD               0x1
#define FIRST_METHOD_ID                 0x0000
#define FIRST_TABLE_ID                  0x8000

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
	IMODE_LOAD_PASS1                = 0x01,
	IMODE_LOAD_PASS2                = 0x02,
	IMODE_EXECUTE                   = 0x0E

} operating_mode;


/*
 * The Node describes a named object that appears in the AML
 * An Acpi_node is used to store Nodes.
 *
 * Data_type is used to differentiate between internal descriptors, and MUST
 * be the first byte in this structure.
 */

typedef struct acpi_node
{
	u8                      data_type;
	u8                      type;           /* Type associated with this name */
	u16                     owner_id;
	u32                     name;           /* ACPI Name, always 4 chars per ACPI spec */


	union acpi_operand_obj  *object;        /* Pointer to attached ACPI object (optional) */
	struct acpi_node        *child;         /* first child */
	struct acpi_node        *peer;          /* Next peer*/
	u16                     reference_count; /* Current count of references and children */
	u8                      flags;

} acpi_namespace_node;


#define ENTRY_NOT_FOUND             NULL


/* Node flags */

#define ANOBJ_AML_ATTACHMENT            0x01
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
	u32                     length;
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

} find_context;


typedef struct
{
	acpi_namespace_node     *node;
} ns_search_data;


/*
 * Predefined Namespace items
 */
typedef struct
{
	NATIVE_CHAR             *name;
	acpi_object_type8       type;
	NATIVE_CHAR             *val;

} predefined_names;


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
	u8                      field_type;

} ACPI_CREATE_FIELD_INFO;

/*
 * Field flags: Bits 00 - 03 : Access_type (Any_acc, Byte_acc, etc.)
 *                   04      : Lock_rule (1 == Lock)
 *                   05 - 06 : Update_rule
 */

#define FIELD_ACCESS_TYPE_MASK      0x0F
#define FIELD_LOCK_RULE_MASK        0x10
#define FIELD_UPDATE_RULE_MASK      0x60


/*****************************************************************************
 *
 * Event typedefs and structs
 *
 ****************************************************************************/


/* Status bits. */

#define ACPI_STATUS_PMTIMER             0x0001
#define ACPI_STATUS_BUSMASTER           0x0010
#define ACPI_STATUS_GLOBAL              0x0020
#define ACPI_STATUS_POWER_BUTTON        0x0100
#define ACPI_STATUS_SLEEP_BUTTON        0x0200
#define ACPI_STATUS_RTC_ALARM           0x0400

/* Enable bits. */

#define ACPI_ENABLE_PMTIMER             0x0001
#define ACPI_ENABLE_GLOBAL              0x0020
#define ACPI_ENABLE_POWER_BUTTON        0x0100
#define ACPI_ENABLE_SLEEP_BUTTON        0x0200
#define ACPI_ENABLE_RTC_ALARM           0x0400


/*
 * Entry in the Address_space (AKA Operation Region) table
 */

typedef struct
{
	acpi_adr_space_handler  handler;
	void                    *context;

} acpi_adr_space_info;


/* Values and addresses of the GPE registers (both banks) */

typedef struct
{
	u16                     status_addr;    /* Address of status reg */
	u16                     enable_addr;    /* Address of enable reg */
	u8                      status;         /* Current value of status reg */
	u8                      enable;         /* Current value of enable reg */
	u8                      wake_enable;    /* Mask of bits to keep enabled when sleeping */
	u8                      gpe_base;       /* Base GPE number */

} acpi_gpe_registers;


#define ACPI_GPE_LEVEL_TRIGGERED        1
#define ACPI_GPE_EDGE_TRIGGERED         2


/* Information about each particular GPE level */

typedef struct
{
	u8                      type;           /* Level or Edge */

	acpi_handle             method_handle;  /* Method handle for direct (fast) execution */
	acpi_gpe_handler        handler;        /* Address of handler, if any */
	void                    *context;       /* Context to be passed to handler */

} acpi_gpe_level_info;


/* Information about each particular fixed event */

typedef struct
{
	acpi_event_handler      handler;        /* Address of handler. */
	void                    *context;       /* Context to be passed to handler */

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


#define CONTROL_NORMAL                  0xC0
#define CONTROL_CONDITIONAL_EXECUTING   0xC1
#define CONTROL_PREDICATE_EXECUTING     0xC2
#define CONTROL_PREDICATE_FALSE         0xC3
#define CONTROL_PREDICATE_TRUE          0xC4


/* Forward declarations */
struct acpi_walk_state;
struct acpi_walk_list;
struct acpi_parse_obj;
struct acpi_obj_mutex;


#define ACPI_STATE_COMMON                  /* Two 32-bit fields and a pointer */\
	u8                      data_type;          /* To differentiate various internal objs */\
	u8                      flags; \
	u16                     value; \
	u16                     state; \
	u16                     acpi_eval; \
	void                    *next; \

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
	struct acpi_parse_obj   *predicate_op;
	u8                      *aml_predicate_start; /* Start of if/while predicate */

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
	struct acpi_parse_obj   *op;            /* current op being parsed */
	u8                      *arg_end;       /* current argument end */
	u8                      *pkg_end;       /* current package end */
	u32                     arg_list;       /* next argument to parse */
	u32                     arg_count;      /* Number of fixed arguments */

} acpi_pscope_state;


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
	struct acpi_parse_obj   **out_op);

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
	u32                     parse_args;     /* Grammar/Parse time arguments */
	u32                     runtime_args;   /* Interpret time arguments */
	u16                     flags;          /* Misc flags */
	u8                      class;          /* Opcode class */
	u8                      type;           /* Opcode type */

#ifdef _OPCODE_NAMES
	NATIVE_CHAR             *name;          /* op name (debug only) */
#endif

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
	struct acpi_parse_obj   *arg;           /* arguments and contained ops */

} acpi_parse_value;


#define ACPI_PARSE_COMMON \
	u8                      data_type;      /* To differentiate various internal objs */\
	u8                      flags;          /* Type of Op */\
	u16                     opcode;         /* AML opcode */\
	u32                     aml_offset;     /* offset of declaration in AML */\
	struct acpi_parse_obj   *parent;        /* parent op */\
	struct acpi_parse_obj   *next;          /* next op */\
	DEBUG_ONLY_MEMBERS (\
	NATIVE_CHAR             op_name[16])    /* op name (debug only) */\
			  /* NON-DEBUG members below: */\
	acpi_namespace_node     *node;          /* for use by interpreter */\
	acpi_parse_value        value;          /* Value or args associated with the opcode */\


/*
 * generic operation (eg. If, While, Store)
 */
typedef struct acpi_parse_obj
{
	ACPI_PARSE_COMMON
} acpi_parse_object;


/*
 * Extended Op for named ops (Scope, Method, etc.), deferred ops (Methods and Op_regions),
 * and bytelists.
 */
typedef struct acpi_parse2_obj
{
	ACPI_PARSE_COMMON
	u8                      *data;          /* AML body or bytelist data */
	u32                     length;         /* AML length */
	u32                     name;           /* 4-byte name or zero if no name */

} acpi_parse2_object;


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

	struct acpi_parse_obj   *start_op;      /* root of parse tree */
	struct acpi_node        *start_node;
	union acpi_gen_state    *scope;         /* current scope */


	struct acpi_parse_obj   *start_scope;


} acpi_parse_state;


/*****************************************************************************
 *
 * Hardware and PNP
 *
 ****************************************************************************/


/* PCI */
#define PCI_ROOT_HID_STRING             "PNP0A03"

/*
 * The #define's and enum below establish an abstract way of identifying what
 * register block and register is to be accessed.  Do not change any of the
 * values as they are used in switch statements and offset calculations.
 */

#define REGISTER_BLOCK_MASK             0xFF00  /* Register Block Id    */
#define BIT_IN_REGISTER_MASK            0x00FF  /* Bit Id in the Register Block Id    */
#define BYTE_IN_REGISTER_MASK           0x00FF  /* Register Offset in the Register Block    */

#define REGISTER_BLOCK_ID(reg_id)       (reg_id & REGISTER_BLOCK_MASK)
#define REGISTER_BIT_ID(reg_id)         (reg_id & BIT_IN_REGISTER_MASK)
#define REGISTER_OFFSET(reg_id)         (reg_id & BYTE_IN_REGISTER_MASK)

/*
 * Access Rule
 *  To access a Register Bit:
 *  -> Use Bit Name (= Register Block Id | Bit Id) defined in the enum.
 *
 *  To access a Register:
 *  -> Use Register Id (= Register Block Id | Register Offset)
 */


/*
 * Register Block Id
 */
#define PM1_STS                         0x0100
#define PM1_EN                          0x0200
#define PM1_CONTROL                     0x0300
#define PM1A_CONTROL                    0x0400
#define PM1B_CONTROL                    0x0500
#define PM2_CONTROL                     0x0600
#define PM_TIMER                        0x0700
#define PROCESSOR_BLOCK                 0x0800
#define GPE0_STS_BLOCK                  0x0900
#define GPE0_EN_BLOCK                   0x0A00
#define GPE1_STS_BLOCK                  0x0B00
#define GPE1_EN_BLOCK                   0x0C00
#define SMI_CMD_BLOCK                   0x0D00

/*
 * Address space bitmasks for mmio or io spaces
 */

#define SMI_CMD_ADDRESS_SPACE           0x01
#define PM1_BLK_ADDRESS_SPACE           0x02
#define PM2_CNT_BLK_ADDRESS_SPACE       0x04
#define PM_TMR_BLK_ADDRESS_SPACE        0x08
#define GPE0_BLK_ADDRESS_SPACE          0x10
#define GPE1_BLK_ADDRESS_SPACE          0x20

/*
 * Control bit definitions
 */
#define TMR_STS                         (PM1_STS | 0x01)
#define BM_STS                          (PM1_STS | 0x02)
#define GBL_STS                         (PM1_STS | 0x03)
#define PWRBTN_STS                      (PM1_STS | 0x04)
#define SLPBTN_STS                      (PM1_STS | 0x05)
#define RTC_STS                         (PM1_STS | 0x06)
#define WAK_STS                         (PM1_STS | 0x07)

#define TMR_EN                          (PM1_EN | 0x01)
			 /* no BM_EN */
#define GBL_EN                          (PM1_EN | 0x03)
#define PWRBTN_EN                       (PM1_EN | 0x04)
#define SLPBTN_EN                       (PM1_EN | 0x05)
#define RTC_EN                          (PM1_EN | 0x06)
#define WAK_EN                          (PM1_EN | 0x07)

#define SCI_EN                          (PM1_CONTROL | 0x01)
#define BM_RLD                          (PM1_CONTROL | 0x02)
#define GBL_RLS                         (PM1_CONTROL | 0x03)
#define SLP_TYPE_A                      (PM1_CONTROL | 0x04)
#define SLP_TYPE_B                      (PM1_CONTROL | 0x05)
#define SLP_EN                          (PM1_CONTROL | 0x06)

#define ARB_DIS                         (PM2_CONTROL | 0x01)

#define TMR_VAL                         (PM_TIMER | 0x01)

#define GPE0_STS                        (GPE0_STS_BLOCK | 0x01)
#define GPE0_EN                         (GPE0_EN_BLOCK  | 0x01)

#define GPE1_STS                        (GPE1_STS_BLOCK | 0x01)
#define GPE1_EN                         (GPE1_EN_BLOCK  | 0x01)


#define TMR_STS_MASK                    0x0001
#define BM_STS_MASK                     0x0010
#define GBL_STS_MASK                    0x0020
#define PWRBTN_STS_MASK                 0x0100
#define SLPBTN_STS_MASK                 0x0200
#define RTC_STS_MASK                    0x0400
#define WAK_STS_MASK                    0x8000

#define ALL_FIXED_STS_BITS              (TMR_STS_MASK   | BM_STS_MASK  | GBL_STS_MASK \
					  | PWRBTN_STS_MASK | SLPBTN_STS_MASK \
					  | RTC_STS_MASK | WAK_STS_MASK)

#define TMR_EN_MASK                     0x0001
#define GBL_EN_MASK                     0x0020
#define PWRBTN_EN_MASK                  0x0100
#define SLPBTN_EN_MASK                  0x0200
#define RTC_EN_MASK                     0x0400

#define SCI_EN_MASK                     0x0001
#define BM_RLD_MASK                     0x0002
#define GBL_RLS_MASK                    0x0004
#define SLP_TYPE_X_MASK                 0x1C00
#define SLP_EN_MASK                     0x2000

#define ARB_DIS_MASK                    0x0001
#define TMR_VAL_MASK                    0xFFFFFFFF

#define GPE0_STS_MASK
#define GPE0_EN_MASK

#define GPE1_STS_MASK
#define GPE1_EN_MASK


#define ACPI_READ                       1
#define ACPI_WRITE                      2


/*****************************************************************************
 *
 * Resource descriptors
 *
 ****************************************************************************/


/* Resource_type values */

#define RESOURCE_TYPE_MEMORY_RANGE              0
#define RESOURCE_TYPE_IO_RANGE                  1
#define RESOURCE_TYPE_BUS_NUMBER_RANGE          2

/* Resource descriptor types and masks */

#define RESOURCE_DESC_TYPE_LARGE                0x80
#define RESOURCE_DESC_TYPE_SMALL                0x00

#define RESOURCE_DESC_TYPE_MASK                 0x80
#define RESOURCE_DESC_SMALL_MASK                0x78        /* Only bits 6:3 contain the type */


/*
 * Small resource descriptor types
 * Note: The 3 length bits (2:0) must be zero
 */
#define RESOURCE_DESC_IRQ_FORMAT                0x20
#define RESOURCE_DESC_DMA_FORMAT                0x28
#define RESOURCE_DESC_START_DEPENDENT           0x30
#define RESOURCE_DESC_END_DEPENDENT             0x38
#define RESOURCE_DESC_IO_PORT                   0x40
#define RESOURCE_DESC_FIXED_IO_PORT             0x48
#define RESOURCE_DESC_SMALL_VENDOR              0x70
#define RESOURCE_DESC_END_TAG                   0x78

/*
 * Large resource descriptor types
 */

#define RESOURCE_DESC_MEMORY_24                 0x81
#define RESOURCE_DESC_GENERAL_REGISTER          0x82
#define RESOURCE_DESC_LARGE_VENDOR              0x84
#define RESOURCE_DESC_MEMORY_32                 0x85
#define RESOURCE_DESC_FIXED_MEMORY_32           0x86
#define RESOURCE_DESC_DWORD_ADDRESS_SPACE       0x87
#define RESOURCE_DESC_WORD_ADDRESS_SPACE        0x88
#define RESOURCE_DESC_EXTENDED_XRUPT            0x89
#define RESOURCE_DESC_QWORD_ADDRESS_SPACE       0x8A


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

#define ASCII_ZERO                      0x30

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

} db_method_info;


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


#define MEM_MALLOC                      0
#define MEM_CALLOC                      1
#define MAX_MODULE_NAME                 16

#define ACPI_COMMON_DEBUG_MEM_HEADER \
	struct acpi_debug_mem_block *previous; \
	struct acpi_debug_mem_block *next; \
	u32                         size; \
	u32                         component; \
	u32                         line; \
	NATIVE_CHAR                 module[MAX_MODULE_NAME]; \
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
