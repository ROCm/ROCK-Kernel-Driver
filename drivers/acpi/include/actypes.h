/******************************************************************************
 *
 * Name: actypes.h - Common data types for the entire ACPI subsystem
 *       $Revision: 239 $
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

#ifndef __ACTYPES_H__
#define __ACTYPES_H__

/*! [Begin] no source code translation (keep the typedefs) */



/*
 * Data type ranges
 */
#define ACPI_UINT8_MAX                  (UINT8)  0xFF
#define ACPI_UINT16_MAX                 (UINT16) 0xFFFF
#define ACPI_UINT32_MAX                 (UINT32) 0xFFFFFFFF
#define ACPI_UINT64_MAX                 (UINT64) 0xFFFFFFFFFFFFFFFF
#define ACPI_ASCII_MAX                  0x7F



/*
 * Data types - Fixed across all compilation models
 *
 * BOOLEAN      Logical Boolean.
 * INT8         8-bit  (1 byte) signed value
 * UINT8        8-bit  (1 byte) unsigned value
 * INT16        16-bit (2 byte) signed value
 * UINT16       16-bit (2 byte) unsigned value
 * INT32        32-bit (4 byte) signed value
 * UINT32       32-bit (4 byte) unsigned value
 * INT64        64-bit (8 byte) signed value
 * UINT64       64-bit (8 byte) unsigned value
 * NATIVE_INT   32-bit on IA-32, 64-bit on IA-64 signed value
 * NATIVE_UINT  32-bit on IA-32, 64-bit on IA-64 unsigned value
 */

#ifndef ACPI_MACHINE_WIDTH
#error ACPI_MACHINE_WIDTH not defined
#endif

#if ACPI_MACHINE_WIDTH == 64
/*
 * 64-bit type definitions
 */
typedef unsigned char                   UINT8;
typedef unsigned char                   BOOLEAN;
typedef unsigned short                  UINT16;
typedef int                             INT32;
typedef unsigned int                    UINT32;
typedef COMPILER_DEPENDENT_INT64        INT64;
typedef COMPILER_DEPENDENT_UINT64       UINT64;

typedef INT64                           NATIVE_INT;
typedef UINT64                          NATIVE_UINT;

typedef UINT32                          NATIVE_UINT_MAX32;
typedef UINT64                          NATIVE_UINT_MIN32;

typedef UINT64                          ACPI_TBLPTR;
typedef UINT64                          ACPI_IO_ADDRESS;
typedef UINT64                          ACPI_PHYSICAL_ADDRESS;
typedef UINT64                          ACPI_SIZE;

#define ALIGNED_ADDRESS_BOUNDARY        0x00000008      /* No hardware alignment support in IA64 */
#define ACPI_USE_NATIVE_DIVIDE                          /* Native 64-bit integer support */
#define ACPI_MAX_PTR                    ACPI_UINT64_MAX
#define ACPI_SIZE_MAX                   ACPI_UINT64_MAX


#elif ACPI_MACHINE_WIDTH == 16
/*
 * 16-bit type definitions
 */
typedef unsigned char                   UINT8;
typedef unsigned char                   BOOLEAN;
typedef unsigned int                    UINT16;
typedef long                            INT32;
typedef int                             INT16;
typedef unsigned long                   UINT32;

typedef struct
{
	UINT32                                  Lo;
	UINT32                                  Hi;

} UINT64;

typedef UINT16                          NATIVE_UINT;
typedef INT16                           NATIVE_INT;

typedef UINT16                          NATIVE_UINT_MAX32;
typedef UINT32                          NATIVE_UINT_MIN32;

typedef UINT32                          ACPI_TBLPTR;
typedef UINT32                          ACPI_IO_ADDRESS;
typedef char                            *ACPI_PHYSICAL_ADDRESS;
typedef UINT16                          ACPI_SIZE;

#define ALIGNED_ADDRESS_BOUNDARY        0x00000002
#define _HW_ALIGNMENT_SUPPORT
#define ACPI_USE_NATIVE_DIVIDE                          /* No 64-bit integers, ok to use native divide */
#define ACPI_MAX_PTR                    ACPI_UINT16_MAX
#define ACPI_SIZE_MAX                   ACPI_UINT16_MAX

/*
 * (16-bit only) internal integers must be 32-bits, so
 * 64-bit integers cannot be supported
 */
#define ACPI_NO_INTEGER64_SUPPORT


#elif ACPI_MACHINE_WIDTH == 32
/*
 * 32-bit type definitions (default)
 */
typedef unsigned char                   UINT8;
typedef unsigned char                   BOOLEAN;
typedef unsigned short                  UINT16;
typedef int                             INT32;
typedef unsigned int                    UINT32;
typedef COMPILER_DEPENDENT_INT64        INT64;
typedef COMPILER_DEPENDENT_UINT64       UINT64;

typedef INT32                           NATIVE_INT;
typedef UINT32                          NATIVE_UINT;

typedef UINT32                          NATIVE_UINT_MAX32;
typedef UINT32                          NATIVE_UINT_MIN32;

typedef UINT64                          ACPI_TBLPTR;
typedef UINT32                          ACPI_IO_ADDRESS;
typedef UINT64                          ACPI_PHYSICAL_ADDRESS;
typedef UINT32                          ACPI_SIZE;

#define ALIGNED_ADDRESS_BOUNDARY        0x00000004
#define _HW_ALIGNMENT_SUPPORT
#define ACPI_MAX_PTR                    ACPI_UINT32_MAX
#define ACPI_SIZE_MAX                   ACPI_UINT32_MAX

#else
#error unknown ACPI_MACHINE_WIDTH
#endif


/*
 * Miscellaneous common types
 */

typedef UINT32                          UINT32_BIT;
typedef NATIVE_UINT                     ACPI_PTRDIFF;
typedef char                            NATIVE_CHAR;


#ifdef DEFINE_ALTERNATE_TYPES
/*
 * Types used only in translated source, defined here to enable
 * cross-platform compilation only.
 */
typedef INT32                           s32;
typedef UINT8                           u8;
typedef UINT16                          u16;
typedef UINT32                          u32;
typedef UINT64                          u64;
#endif
/*! [End] no source code translation !*/


/*
 * Pointer overlays to avoid lots of typecasting for
 * code that accepts both physical and logical pointers.
 */
typedef union acpi_ptrs
{
	ACPI_PHYSICAL_ADDRESS       physical;
	void                        *logical;
	ACPI_TBLPTR                 value;

} ACPI_POINTERS;

typedef struct acpi_pointer
{
	u32                         pointer_type;
	union acpi_ptrs             pointer;

} ACPI_POINTER;

/* Pointer_types for above */

#define ACPI_PHYSICAL_POINTER           0x01
#define ACPI_LOGICAL_POINTER            0x02

/* Processor mode */

#define ACPI_PHYSICAL_ADDRESSING        0x04
#define ACPI_LOGICAL_ADDRESSING         0x08
#define ACPI_MEMORY_MODE                0x0C

#define ACPI_PHYSMODE_PHYSPTR           ACPI_PHYSICAL_ADDRESSING | ACPI_PHYSICAL_POINTER
#define ACPI_LOGMODE_PHYSPTR            ACPI_LOGICAL_ADDRESSING  | ACPI_PHYSICAL_POINTER
#define ACPI_LOGMODE_LOGPTR             ACPI_LOGICAL_ADDRESSING  | ACPI_LOGICAL_POINTER


/*
 * Useful defines
 */

#ifdef FALSE
#undef FALSE
#endif
#define FALSE                           (1 == 0)

#ifdef TRUE
#undef TRUE
#endif
#define TRUE                            (1 == 1)

#ifndef NULL
#define NULL                            (void *) 0
#endif


/*
 * Local datatypes
 */

typedef u32                             acpi_status;    /* All ACPI Exceptions */
typedef u32                             acpi_name;      /* 4-byte ACPI name */
typedef char*                           acpi_string;    /* Null terminated ASCII string */
typedef void*                           acpi_handle;    /* Actually a ptr to an Node */

typedef struct
{
	u32                         lo;
	u32                         hi;

} uint64_struct;

typedef union
{
	u64                         full;
	uint64_struct               part;

} uint64_overlay;

typedef struct
{
	u32                         lo;
	u32                         hi;

} UINT32_STRUCT;


/*
 * Acpi integer width. In ACPI version 1, integers are
 * 32 bits.  In ACPI version 2, integers are 64 bits.
 * Note that this pertains to the ACPI integer type only, not
 * other integers used in the implementation of the ACPI CA
 * subsystem.
 */
#ifdef ACPI_NO_INTEGER64_SUPPORT

/* 32-bit integers only, no 64-bit support */

typedef u32                             acpi_integer;
#define ACPI_INTEGER_MAX                ACPI_UINT32_MAX
#define ACPI_INTEGER_BIT_SIZE           32
#define ACPI_MAX_BCD_VALUE              99999999
#define ACPI_MAX_BCD_DIGITS             8
#define ACPI_MAX_DECIMAL_DIGITS         10

#define ACPI_USE_NATIVE_DIVIDE          /* Use compiler native 32-bit divide */


#else

/* 64-bit integers */

typedef u64                             acpi_integer;
#define ACPI_INTEGER_MAX                ACPI_UINT64_MAX
#define ACPI_INTEGER_BIT_SIZE           64
#define ACPI_MAX_BCD_VALUE              9999999999999999
#define ACPI_MAX_BCD_DIGITS             16
#define ACPI_MAX_DECIMAL_DIGITS         19

#if ACPI_MACHINE_WIDTH == 64
#define ACPI_USE_NATIVE_DIVIDE          /* Use compiler native 64-bit divide */
#endif
#endif


/*
 * Constants with special meanings
 */

#define ACPI_ROOT_OBJECT                (acpi_handle) ACPI_PTR_ADD (char, NULL, ACPI_MAX_PTR)


/*
 * Initialization sequence
 */
#define ACPI_FULL_INITIALIZATION        0x00
#define ACPI_NO_ADDRESS_SPACE_INIT      0x01
#define ACPI_NO_HARDWARE_INIT           0x02
#define ACPI_NO_EVENT_INIT              0x04
#define ACPI_NO_HANDLER_INIT            0x08
#define ACPI_NO_ACPI_ENABLE             0x10
#define ACPI_NO_DEVICE_INIT             0x20
#define ACPI_NO_OBJECT_INIT             0x40

/*
 * Initialization state
 */
#define ACPI_INITIALIZED_OK             0x01

/*
 * Power state values
 */

#define ACPI_STATE_UNKNOWN              (u8) 0xFF

#define ACPI_STATE_S0                   (u8) 0
#define ACPI_STATE_S1                   (u8) 1
#define ACPI_STATE_S2                   (u8) 2
#define ACPI_STATE_S3                   (u8) 3
#define ACPI_STATE_S4                   (u8) 4
#define ACPI_STATE_S5                   (u8) 5
#define ACPI_S_STATES_MAX               ACPI_STATE_S5
#define ACPI_S_STATE_COUNT              6

#define ACPI_STATE_D0                   (u8) 0
#define ACPI_STATE_D1                   (u8) 1
#define ACPI_STATE_D2                   (u8) 2
#define ACPI_STATE_D3                   (u8) 3
#define ACPI_D_STATES_MAX               ACPI_STATE_D3
#define ACPI_D_STATE_COUNT              4

#define ACPI_STATE_C0                   (u8) 0
#define ACPI_STATE_C1                   (u8) 1
#define ACPI_STATE_C2                   (u8) 2
#define ACPI_STATE_C3                   (u8) 3
#define ACPI_C_STATES_MAX               ACPI_STATE_C3
#define ACPI_C_STATE_COUNT              4

/*
 * Sleep type invalid value
 */
#define ACPI_SLEEP_TYPE_MAX             0x7
#define ACPI_SLEEP_TYPE_INVALID         0xFF

/*
 * Standard notify values
 */
#define ACPI_NOTIFY_BUS_CHECK           (u8) 0
#define ACPI_NOTIFY_DEVICE_CHECK        (u8) 1
#define ACPI_NOTIFY_DEVICE_WAKE         (u8) 2
#define ACPI_NOTIFY_EJECT_REQUEST       (u8) 3
#define ACPI_NOTIFY_DEVICE_CHECK_LIGHT  (u8) 4
#define ACPI_NOTIFY_FREQUENCY_MISMATCH  (u8) 5
#define ACPI_NOTIFY_BUS_MODE_MISMATCH   (u8) 6
#define ACPI_NOTIFY_POWER_FAULT         (u8) 7


/*
 *  Table types.  These values are passed to the table related APIs
 */

typedef u32                             acpi_table_type;

#define ACPI_TABLE_RSDP                 (acpi_table_type) 0
#define ACPI_TABLE_DSDT                 (acpi_table_type) 1
#define ACPI_TABLE_FADT                 (acpi_table_type) 2
#define ACPI_TABLE_FACS                 (acpi_table_type) 3
#define ACPI_TABLE_PSDT                 (acpi_table_type) 4
#define ACPI_TABLE_SSDT                 (acpi_table_type) 5
#define ACPI_TABLE_XSDT                 (acpi_table_type) 6
#define ACPI_TABLE_MAX                  6
#define NUM_ACPI_TABLES                 (ACPI_TABLE_MAX+1)


/*
 * Types associated with names.  The first group of
 * values correspond to the definition of the ACPI
 * Object_type operator (See the ACPI Spec). Therefore,
 * only add to the first group if the spec changes.
 *
 * Types must be kept in sync with the Acpi_ns_properties
 * and Acpi_ns_type_names arrays
 */

typedef u32                             acpi_object_type;

#define ACPI_TYPE_ANY                   0x00
#define ACPI_TYPE_INTEGER               0x01  /* Byte/Word/Dword/Zero/One/Ones */
#define ACPI_TYPE_STRING                0x02
#define ACPI_TYPE_BUFFER                0x03
#define ACPI_TYPE_PACKAGE               0x04  /* Byte_const, multiple Data_term/Constant/Super_name */
#define ACPI_TYPE_FIELD_UNIT            0x05
#define ACPI_TYPE_DEVICE                0x06  /* Name, multiple Node */
#define ACPI_TYPE_EVENT                 0x07
#define ACPI_TYPE_METHOD                0x08  /* Name, Byte_const, multiple Code */
#define ACPI_TYPE_MUTEX                 0x09
#define ACPI_TYPE_REGION                0x0A
#define ACPI_TYPE_POWER                 0x0B  /* Name,Byte_const,Word_const,multi Node */
#define ACPI_TYPE_PROCESSOR             0x0C  /* Name,Byte_const,DWord_const,Byte_const,multi Nm_o */
#define ACPI_TYPE_THERMAL               0x0D  /* Name, multiple Node */
#define ACPI_TYPE_BUFFER_FIELD          0x0E
#define ACPI_TYPE_DDB_HANDLE            0x0F
#define ACPI_TYPE_DEBUG_OBJECT          0x10

#define ACPI_TYPE_MAX                   0x10

/*
 * This section contains object types that do not relate to the ACPI Object_type operator.
 * They are used for various internal purposes only.  If new predefined ACPI_TYPEs are
 * added (via the ACPI specification), these internal types must move upwards.
 * Also, values exceeding the largest official ACPI Object_type must not overlap with
 * defined AML opcodes.
 */
#define INTERNAL_TYPE_BEGIN             0x11

#define INTERNAL_TYPE_REGION_FIELD      0x11
#define INTERNAL_TYPE_BANK_FIELD        0x12
#define INTERNAL_TYPE_INDEX_FIELD       0x13
#define INTERNAL_TYPE_REFERENCE         0x14  /* Arg#, Local#, Name, Debug; used only in descriptors */
#define INTERNAL_TYPE_ALIAS             0x15
#define INTERNAL_TYPE_NOTIFY            0x16
#define INTERNAL_TYPE_ADDRESS_HANDLER   0x17
#define INTERNAL_TYPE_RESOURCE          0x18
#define INTERNAL_TYPE_RESOURCE_FIELD    0x19


#define INTERNAL_TYPE_NODE_MAX          0x19

/* These are pseudo-types because there are never any namespace nodes with these types */

#define INTERNAL_TYPE_FIELD_DEFN        0x1A  /* Name, Byte_const, multiple Field_element */
#define INTERNAL_TYPE_BANK_FIELD_DEFN   0x1B  /* 2 Name,DWord_const,Byte_const,multi Field_element */
#define INTERNAL_TYPE_INDEX_FIELD_DEFN  0x1C  /* 2 Name, Byte_const, multiple Field_element */
#define INTERNAL_TYPE_IF                0x1D
#define INTERNAL_TYPE_ELSE              0x1E
#define INTERNAL_TYPE_WHILE             0x1F
#define INTERNAL_TYPE_SCOPE             0x20  /* Name, multiple Node */
#define INTERNAL_TYPE_DEF_ANY           0x21  /* type is Any, suppress search of enclosing scopes */
#define INTERNAL_TYPE_EXTRA             0x22
#define INTERNAL_TYPE_DATA              0x23

#define INTERNAL_TYPE_MAX               0x23

#define INTERNAL_TYPE_INVALID           0x24
#define ACPI_TYPE_NOT_FOUND             0xFF


/*
 * Bitmapped ACPI types
 * Used internally only
 */
#define ACPI_BTYPE_ANY                  0x00000000
#define ACPI_BTYPE_INTEGER              0x00000001
#define ACPI_BTYPE_STRING               0x00000002
#define ACPI_BTYPE_BUFFER               0x00000004
#define ACPI_BTYPE_PACKAGE              0x00000008
#define ACPI_BTYPE_FIELD_UNIT           0x00000010
#define ACPI_BTYPE_DEVICE               0x00000020
#define ACPI_BTYPE_EVENT                0x00000040
#define ACPI_BTYPE_METHOD               0x00000080
#define ACPI_BTYPE_MUTEX                0x00000100
#define ACPI_BTYPE_REGION               0x00000200
#define ACPI_BTYPE_POWER                0x00000400
#define ACPI_BTYPE_PROCESSOR            0x00000800
#define ACPI_BTYPE_THERMAL              0x00001000
#define ACPI_BTYPE_BUFFER_FIELD         0x00002000
#define ACPI_BTYPE_DDB_HANDLE           0x00004000
#define ACPI_BTYPE_DEBUG_OBJECT         0x00008000
#define ACPI_BTYPE_REFERENCE            0x00010000
#define ACPI_BTYPE_RESOURCE             0x00020000

#define ACPI_BTYPE_COMPUTE_DATA         (ACPI_BTYPE_INTEGER | ACPI_BTYPE_STRING | ACPI_BTYPE_BUFFER)

#define ACPI_BTYPE_DATA                 (ACPI_BTYPE_COMPUTE_DATA  | ACPI_BTYPE_PACKAGE)
#define ACPI_BTYPE_DATA_REFERENCE       (ACPI_BTYPE_DATA | ACPI_BTYPE_REFERENCE | ACPI_BTYPE_DDB_HANDLE)
#define ACPI_BTYPE_DEVICE_OBJECTS       (ACPI_BTYPE_DEVICE | ACPI_BTYPE_THERMAL | ACPI_BTYPE_PROCESSOR)
#define ACPI_BTYPE_OBJECTS_AND_REFS     0x0001FFFF  /* ARG or LOCAL */
#define ACPI_BTYPE_ALL_OBJECTS          0x0000FFFF

/*
 * All I/O
 */
#define ACPI_READ                       0
#define ACPI_WRITE                      1


/*
 * Acpi_event Types: Fixed & General Purpose
 */

typedef u32                             acpi_event_type;

#define ACPI_EVENT_FIXED                0
#define ACPI_EVENT_GPE                  1

/*
 * Fixed events
 */

#define ACPI_EVENT_PMTIMER              0
#define ACPI_EVENT_GLOBAL               1
#define ACPI_EVENT_POWER_BUTTON         2
#define ACPI_EVENT_SLEEP_BUTTON         3
#define ACPI_EVENT_RTC                  4
#define ACPI_EVENT_MAX                  4
#define ACPI_NUM_FIXED_EVENTS           ACPI_EVENT_MAX + 1

#define ACPI_GPE_INVALID                0xFF
#define ACPI_GPE_MAX                    0xFF
#define ACPI_NUM_GPE                    256

#define ACPI_EVENT_LEVEL_TRIGGERED      1
#define ACPI_EVENT_EDGE_TRIGGERED       2

/*
 * GPEs
 */

#define ACPI_EVENT_WAKE_ENABLE          0x1

#define ACPI_EVENT_WAKE_DISABLE         0x1


/*
 * Acpi_event Status:
 * -------------
 * The encoding of acpi_event_status is illustrated below.
 * Note that a set bit (1) indicates the property is TRUE
 * (e.g. if bit 0 is set then the event is enabled).
 * +-------------+-+-+-+
 * |   Bits 31:3 |2|1|0|
 * +-------------+-+-+-+
 *          |     | | |
 *          |     | | +- Enabled?
 *          |     | +--- Enabled for wake?
 *          |     +----- Set?
 *          +----------- <Reserved>
 */
typedef u32                             acpi_event_status;

#define ACPI_EVENT_FLAG_DISABLED        (acpi_event_status) 0x00
#define ACPI_EVENT_FLAG_ENABLED         (acpi_event_status) 0x01
#define ACPI_EVENT_FLAG_WAKE_ENABLED    (acpi_event_status) 0x02
#define ACPI_EVENT_FLAG_SET             (acpi_event_status) 0x04


/* Notify types */

#define ACPI_SYSTEM_NOTIFY              0
#define ACPI_DEVICE_NOTIFY              1
#define ACPI_MAX_NOTIFY_HANDLER_TYPE    1

#define ACPI_MAX_SYS_NOTIFY                  0x7f


/* Address Space (Operation Region) Types */

typedef u8                              ACPI_ADR_SPACE_TYPE;

#define ACPI_ADR_SPACE_SYSTEM_MEMORY    (ACPI_ADR_SPACE_TYPE) 0
#define ACPI_ADR_SPACE_SYSTEM_IO        (ACPI_ADR_SPACE_TYPE) 1
#define ACPI_ADR_SPACE_PCI_CONFIG       (ACPI_ADR_SPACE_TYPE) 2
#define ACPI_ADR_SPACE_EC               (ACPI_ADR_SPACE_TYPE) 3
#define ACPI_ADR_SPACE_SMBUS            (ACPI_ADR_SPACE_TYPE) 4
#define ACPI_ADR_SPACE_CMOS             (ACPI_ADR_SPACE_TYPE) 5
#define ACPI_ADR_SPACE_PCI_BAR_TARGET   (ACPI_ADR_SPACE_TYPE) 6
#define ACPI_ADR_SPACE_DATA_TABLE       (ACPI_ADR_SPACE_TYPE) 7


/*
 * Bit_register IDs
 * These are bitfields defined within the full ACPI registers
 */
#define ACPI_BITREG_TIMER_STATUS                0x00
#define ACPI_BITREG_BUS_MASTER_STATUS           0x01
#define ACPI_BITREG_GLOBAL_LOCK_STATUS          0x02
#define ACPI_BITREG_POWER_BUTTON_STATUS         0x03
#define ACPI_BITREG_SLEEP_BUTTON_STATUS         0x04
#define ACPI_BITREG_RT_CLOCK_STATUS             0x05
#define ACPI_BITREG_WAKE_STATUS                 0x06

#define ACPI_BITREG_TIMER_ENABLE                0x07
#define ACPI_BITREG_GLOBAL_LOCK_ENABLE          0x08
#define ACPI_BITREG_POWER_BUTTON_ENABLE         0x09
#define ACPI_BITREG_SLEEP_BUTTON_ENABLE         0x0A
#define ACPI_BITREG_RT_CLOCK_ENABLE             0x0B
#define ACPI_BITREG_WAKE_ENABLE                 0x0C

#define ACPI_BITREG_SCI_ENABLE                  0x0D
#define ACPI_BITREG_BUS_MASTER_RLD              0x0E
#define ACPI_BITREG_GLOBAL_LOCK_RELEASE         0x0F
#define ACPI_BITREG_SLEEP_TYPE_A                0x10
#define ACPI_BITREG_SLEEP_TYPE_B                0x11
#define ACPI_BITREG_SLEEP_ENABLE                0x12

#define ACPI_BITREG_ARB_DISABLE                 0x13

#define ACPI_BITREG_MAX                         0x13
#define ACPI_NUM_BITREG                         ACPI_BITREG_MAX + 1

/*
 * External ACPI object definition
 */

typedef union acpi_obj
{
	acpi_object_type            type;   /* See definition of Acpi_ns_type for values */
	struct
	{
		acpi_object_type            type;
		acpi_integer                value;      /* The actual number */
	} integer;

	struct
	{
		acpi_object_type            type;
		u32                         length;     /* # of bytes in string, excluding trailing null */
		NATIVE_CHAR                 *pointer;   /* points to the string value */
	} string;

	struct
	{
		acpi_object_type            type;
		u32                         length;     /* # of bytes in buffer */
		u8                          *pointer;   /* points to the buffer */
	} buffer;

	struct
	{
		acpi_object_type            type;
		u32                         fill1;
		acpi_handle                 handle;     /* object reference */
	} reference;

	struct
	{
		acpi_object_type            type;
		u32                         count;      /* # of elements in package */
		union acpi_obj              *elements;  /* Pointer to an array of ACPI_OBJECTs */
	} package;

	struct
	{
		acpi_object_type            type;
		u32                         proc_id;
		ACPI_IO_ADDRESS             pblk_address;
		u32                         pblk_length;
	} processor;

	struct
	{
		acpi_object_type            type;
		u32                         system_level;
		u32                         resource_order;
	} power_resource;

} acpi_object;


/*
 * List of objects, used as a parameter list for control method evaluation
 */

typedef struct acpi_obj_list
{
	u32                         count;
	acpi_object                 *pointer;

} acpi_object_list;


/*
 * Miscellaneous common Data Structures used by the interfaces
 */

#define ACPI_NO_BUFFER              0
#define ACPI_ALLOCATE_BUFFER        (ACPI_SIZE) (-1)
#define ACPI_ALLOCATE_LOCAL_BUFFER  (ACPI_SIZE) (-2)

typedef struct
{
	ACPI_SIZE                   length;         /* Length in bytes of the buffer */
	void                        *pointer;       /* pointer to buffer */

} acpi_buffer;


/*
 * Name_type for Acpi_get_name
 */

#define ACPI_FULL_PATHNAME              0
#define ACPI_SINGLE_NAME                1
#define ACPI_NAME_TYPE_MAX              1


/*
 * Structure and flags for Acpi_get_system_info
 */

#define ACPI_SYS_MODE_UNKNOWN           0x0000
#define ACPI_SYS_MODE_ACPI              0x0001
#define ACPI_SYS_MODE_LEGACY            0x0002
#define ACPI_SYS_MODES_MASK             0x0003


/*
 * ACPI Table Info.  One per ACPI table _type_
 */
typedef struct acpi_table_info
{
	u32                         count;

} acpi_table_info;


/*
 * System info returned by Acpi_get_system_info()
 */

typedef struct _acpi_sys_info
{
	u32                         acpi_ca_version;
	u32                         flags;
	u32                         timer_resolution;
	u32                         reserved1;
	u32                         reserved2;
	u32                         debug_level;
	u32                         debug_layer;
	u32                         num_table_types;
	acpi_table_info             table_info [NUM_ACPI_TABLES];

} acpi_system_info;


/*
 * Various handlers and callback procedures
 */

typedef
u32 (*acpi_event_handler) (
	void                        *context);

typedef
void (*acpi_gpe_handler) (
	void                        *context);

typedef
void (*acpi_notify_handler) (
	acpi_handle                 device,
	u32                         value,
	void                        *context);

typedef
void (*ACPI_OBJECT_HANDLER) (
	acpi_handle                 object,
	u32                         function,
	void                        *data);

typedef
acpi_status (*ACPI_INIT_HANDLER) (
	acpi_handle                 object,
	u32                         function);

#define ACPI_INIT_DEVICE_INI        1


/* Address Spaces (Operation Regions */

typedef
acpi_status (*acpi_adr_space_handler) (
	u32                         function,
	ACPI_PHYSICAL_ADDRESS       address,
	u32                         bit_width,
	acpi_integer                *value,
	void                        *handler_context,
	void                        *region_context);

#define ACPI_DEFAULT_HANDLER        NULL


typedef
acpi_status (*acpi_adr_space_setup) (
	acpi_handle                 region_handle,
	u32                         function,
	void                        *handler_context,
	void                        **region_context);

#define ACPI_REGION_ACTIVATE    0
#define ACPI_REGION_DEACTIVATE  1

typedef
acpi_status (*acpi_walk_callback) (
	acpi_handle                 obj_handle,
	u32                         nesting_level,
	void                        *context,
	void                        **return_value);


/* Interrupt handler return values */

#define ACPI_INTERRUPT_NOT_HANDLED      0x00
#define ACPI_INTERRUPT_HANDLED          0x01


/* Structure and flags for Acpi_get_device_info */

#define ACPI_VALID_HID                  0x1
#define ACPI_VALID_UID                  0x2
#define ACPI_VALID_ADR                  0x4
#define ACPI_VALID_STA                  0x8


#define ACPI_COMMON_OBJ_INFO \
	acpi_object_type            type;           /* ACPI object type */ \
	acpi_name                   name            /* ACPI object Name */


typedef struct
{
	ACPI_COMMON_OBJ_INFO;
} acpi_obj_info_header;


typedef struct
{
	ACPI_COMMON_OBJ_INFO;

	u32                         valid;              /*  Are the next bits legit? */
	NATIVE_CHAR                 hardware_id[9];     /*  _HID value if any */
	NATIVE_CHAR                 unique_id[9];       /*  _UID value if any */
	acpi_integer                address;            /*  _ADR value if any */
	u32                         current_status;     /*  _STA value */
} acpi_device_info;


/* Context structs for address space handlers */

typedef struct
{
	u16                         segment;
	u16                         bus;
	u16                         device;
	u16                         function;
} acpi_pci_id;


typedef struct
{
	u32                         length;
	ACPI_PHYSICAL_ADDRESS       address;
	ACPI_PHYSICAL_ADDRESS       mapped_physical_address;
	u8                          *mapped_logical_address;
	ACPI_SIZE                   mapped_length;
} acpi_mem_space_context;


/* Sleep states */

#define ACPI_NUM_SLEEP_STATES           7


/*
 * Definitions for Resource Attributes
 */

/*
 *  Memory Attributes
 */
#define ACPI_READ_ONLY_MEMORY           (u8) 0x00
#define ACPI_READ_WRITE_MEMORY          (u8) 0x01

#define ACPI_NON_CACHEABLE_MEMORY       (u8) 0x00
#define ACPI_CACHABLE_MEMORY            (u8) 0x01
#define ACPI_WRITE_COMBINING_MEMORY     (u8) 0x02
#define ACPI_PREFETCHABLE_MEMORY        (u8) 0x03

/*
 *  IO Attributes
 *  The ISA IO ranges are:     n000-n0_fFh, n400-n4_fFh, n800-n8_fFh, n_c00-n_cFFh.
 *  The non-ISA IO ranges are: n100-n3_fFh, n500-n7_fFh, n900-n_bFFh, n_cD0-n_fFFh.
 */
#define ACPI_NON_ISA_ONLY_RANGES        (u8) 0x01
#define ACPI_ISA_ONLY_RANGES            (u8) 0x02
#define ACPI_ENTIRE_RANGE               (ACPI_NON_ISA_ONLY_RANGES | ACPI_ISA_ONLY_RANGES)

/*
 *  IO Port Descriptor Decode
 */
#define ACPI_DECODE_10                  (u8) 0x00    /* 10-bit IO address decode */
#define ACPI_DECODE_16                  (u8) 0x01    /* 16-bit IO address decode */

/*
 *  IRQ Attributes
 */
#define ACPI_EDGE_SENSITIVE             (u8) 0x00
#define ACPI_LEVEL_SENSITIVE            (u8) 0x01

#define ACPI_ACTIVE_HIGH                (u8) 0x00
#define ACPI_ACTIVE_LOW                 (u8) 0x01

#define ACPI_EXCLUSIVE                  (u8) 0x00
#define ACPI_SHARED                     (u8) 0x01

/*
 *  DMA Attributes
 */
#define ACPI_COMPATIBILITY              (u8) 0x00
#define ACPI_TYPE_A                     (u8) 0x01
#define ACPI_TYPE_B                     (u8) 0x02
#define ACPI_TYPE_F                     (u8) 0x03

#define ACPI_NOT_BUS_MASTER             (u8) 0x00
#define ACPI_BUS_MASTER                 (u8) 0x01

#define ACPI_TRANSFER_8                 (u8) 0x00
#define ACPI_TRANSFER_8_16              (u8) 0x01
#define ACPI_TRANSFER_16                (u8) 0x02

/*
 * Start Dependent Functions Priority definitions
 */
#define ACPI_GOOD_CONFIGURATION         (u8) 0x00
#define ACPI_ACCEPTABLE_CONFIGURATION   (u8) 0x01
#define ACPI_SUB_OPTIMAL_CONFIGURATION  (u8) 0x02

/*
 *  16, 32 and 64-bit Address Descriptor resource types
 */
#define ACPI_MEMORY_RANGE               (u8) 0x00
#define ACPI_IO_RANGE                   (u8) 0x01
#define ACPI_BUS_NUMBER_RANGE           (u8) 0x02

#define ACPI_ADDRESS_NOT_FIXED          (u8) 0x00
#define ACPI_ADDRESS_FIXED              (u8) 0x01

#define ACPI_POS_DECODE                 (u8) 0x00
#define ACPI_SUB_DECODE                 (u8) 0x01

#define ACPI_PRODUCER                   (u8) 0x00
#define ACPI_CONSUMER                   (u8) 0x01


/*
 *  Structures used to describe device resources
 */
typedef struct
{
	u32                         edge_level;
	u32                         active_high_low;
	u32                         shared_exclusive;
	u32                         number_of_interrupts;
	u32                         interrupts[1];

} acpi_resource_irq;

typedef struct
{
	u32                         type;
	u32                         bus_master;
	u32                         transfer;
	u32                         number_of_channels;
	u32                         channels[1];

} acpi_resource_dma;

typedef struct
{
	u32                         compatibility_priority;
	u32                         performance_robustness;

} acpi_resource_start_dpf;

/*
 * END_DEPENDENT_FUNCTIONS_RESOURCE struct is not
 *  needed because it has no fields
 */

typedef struct
{
	u32                         io_decode;
	u32                         min_base_address;
	u32                         max_base_address;
	u32                         alignment;
	u32                         range_length;

} acpi_resource_io;

typedef struct
{
	u32                         base_address;
	u32                         range_length;

} acpi_resource_fixed_io;

typedef struct
{
	u32                         length;
	u8                          reserved[1];

} acpi_resource_vendor;

typedef struct
{
	u8                          checksum;

} ACPI_RESOURCE_END_TAG;

typedef struct
{
	u32                         read_write_attribute;
	u32                         min_base_address;
	u32                         max_base_address;
	u32                         alignment;
	u32                         range_length;

} acpi_resource_mem24;

typedef struct
{
	u32                         read_write_attribute;
	u32                         min_base_address;
	u32                         max_base_address;
	u32                         alignment;
	u32                         range_length;

} acpi_resource_mem32;

typedef struct
{
	u32                         read_write_attribute;
	u32                         range_base_address;
	u32                         range_length;

} acpi_resource_fixed_mem32;

typedef struct
{
	u16                         cache_attribute;
	u16                         read_write_attribute;

} acpi_memory_attribute;

typedef struct
{
	u16                         range_attribute;
	u16                         reserved;

} acpi_io_attribute;

typedef struct
{
	u16                         reserved1;
	u16                         reserved2;

} acpi_bus_attribute;

typedef union
{
	acpi_memory_attribute       memory;
	acpi_io_attribute           io;
	acpi_bus_attribute          bus;

} acpi_resource_attribute;

typedef struct
{
	u32                         index;
	u32                         string_length;
	NATIVE_CHAR                 *string_ptr;

} acpi_resource_source;

typedef struct
{
	u32                         resource_type;
	u32                         producer_consumer;
	u32                         decode;
	u32                         min_address_fixed;
	u32                         max_address_fixed;
	acpi_resource_attribute     attribute;
	u32                         granularity;
	u32                         min_address_range;
	u32                         max_address_range;
	u32                         address_translation_offset;
	u32                         address_length;
	acpi_resource_source        resource_source;

} acpi_resource_address16;

typedef struct
{
	u32                         resource_type;
	u32                         producer_consumer;
	u32                         decode;
	u32                         min_address_fixed;
	u32                         max_address_fixed;
	acpi_resource_attribute     attribute;
	u32                         granularity;
	u32                         min_address_range;
	u32                         max_address_range;
	u32                         address_translation_offset;
	u32                         address_length;
	acpi_resource_source        resource_source;

} acpi_resource_address32;

typedef struct
{
	u32                         resource_type;
	u32                         producer_consumer;
	u32                         decode;
	u32                         min_address_fixed;
	u32                         max_address_fixed;
	acpi_resource_attribute     attribute;
	u64                         granularity;
	u64                         min_address_range;
	u64                         max_address_range;
	u64                         address_translation_offset;
	u64                         address_length;
	acpi_resource_source        resource_source;

} acpi_resource_address64;

typedef struct
{
	u32                         producer_consumer;
	u32                         edge_level;
	u32                         active_high_low;
	u32                         shared_exclusive;
	u32                         number_of_interrupts;
	acpi_resource_source        resource_source;
	u32                         interrupts[1];

} acpi_resource_ext_irq;


/* ACPI_RESOURCE_TYPEs */

#define ACPI_RSTYPE_IRQ                 0
#define ACPI_RSTYPE_DMA                 1
#define ACPI_RSTYPE_START_DPF           2
#define ACPI_RSTYPE_END_DPF             3
#define ACPI_RSTYPE_IO                  4
#define ACPI_RSTYPE_FIXED_IO            5
#define ACPI_RSTYPE_VENDOR              6
#define ACPI_RSTYPE_END_TAG             7
#define ACPI_RSTYPE_MEM24               8
#define ACPI_RSTYPE_MEM32               9
#define ACPI_RSTYPE_FIXED_MEM32         10
#define ACPI_RSTYPE_ADDRESS16           11
#define ACPI_RSTYPE_ADDRESS32           12
#define ACPI_RSTYPE_ADDRESS64           13
#define ACPI_RSTYPE_EXT_IRQ             14

typedef u32                             acpi_resource_type;

typedef union
{
	acpi_resource_irq           irq;
	acpi_resource_dma           dma;
	acpi_resource_start_dpf     start_dpf;
	acpi_resource_io            io;
	acpi_resource_fixed_io      fixed_io;
	acpi_resource_vendor        vendor_specific;
	ACPI_RESOURCE_END_TAG       end_tag;
	acpi_resource_mem24         memory24;
	acpi_resource_mem32         memory32;
	acpi_resource_fixed_mem32   fixed_memory32;
	acpi_resource_address16     address16;
	acpi_resource_address32     address32;
	acpi_resource_address64     address64;
	acpi_resource_ext_irq       extended_irq;

} acpi_resource_data;

typedef struct acpi_resource
{
	acpi_resource_type          id;
	u32                         length;
	acpi_resource_data          data;

} acpi_resource;

#define ACPI_RESOURCE_LENGTH                12
#define ACPI_RESOURCE_LENGTH_NO_DATA        8       /* Id + Length fields */

#define ACPI_SIZEOF_RESOURCE(type)          (ACPI_RESOURCE_LENGTH_NO_DATA + sizeof (type))

#define ACPI_NEXT_RESOURCE(res)             (acpi_resource *)((u8 *) res + res->length)

#ifdef _HW_ALIGNMENT_SUPPORT
#define ACPI_ALIGN_RESOURCE_SIZE(length)    (length)
#else
#define ACPI_ALIGN_RESOURCE_SIZE(length)    ACPI_ROUND_UP_TO_NATIVE_WORD(length)
#endif

/*
 * END: of definitions for Resource Attributes
 */


typedef struct acpi_pci_routing_table
{
	u32                         length;
	u32                         pin;
	acpi_integer                address;        /* here for 64-bit alignment */
	u32                         source_index;
	NATIVE_CHAR                 source[4];      /* pad to 64 bits so sizeof() works in all cases */

} acpi_pci_routing_table;

/*
 * END: of definitions for PCI Routing tables
 */


#endif /* __ACTYPES_H__ */
